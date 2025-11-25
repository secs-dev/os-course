#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef enum { OP_NONE, OP_AND, OP_OR, OP_SEQ } op_t;

static int g_quiet = 0;   
typedef struct {
    char **argv;
    int argc;
    int background;     
    op_t next_op;      
} command_t;

typedef struct bgproc {
    pid_t pid;
    struct timespec start;
    char *cmdline;
    struct bgproc *next;
} bgproc_t;

static volatile sig_atomic_t g_sigchld = 0;
static bgproc_t *g_bghead = NULL;

static void on_sigchld(int signo) {
    (void)signo;
    g_sigchld = 1;
}

static void add_bg(pid_t pid, struct timespec start, const char *cmdline) {
    bgproc_t *n = malloc(sizeof(*n));
    if (!n) return;
    n->pid = pid;
    n->start = start;
    n->cmdline = strdup(cmdline ? cmdline : "");
    n->next = g_bghead;
    g_bghead = n;
}

static int remove_bg(pid_t pid, struct timespec *start, char **cmdline) {
    bgproc_t **pp = &g_bghead;
    while (*pp) {
        if ((*pp)->pid == pid) {
            bgproc_t *n = *pp;
            *pp = n->next;
            if (start) *start = n->start;
            if (cmdline) *cmdline = n->cmdline;
            else free(n->cmdline);
            free(n);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

static double elapsed_sec(struct timespec a, struct timespec b) {
    long sec = b.tv_sec - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    if (nsec < 0) { sec--; nsec += 1000000000L; }
    return (double)sec + (double)nsec / 1e9;
}


typedef struct {
    char **data;
    int size, cap;
} vec_t;

static void vpush(vec_t *v, char *s) {
    if (v->size == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->data = realloc(v->data, v->cap * sizeof(char *));
    }
    v->data[v->size++] = s;
}

static char *substr(const char *s, size_t l) {
    char *p = malloc(l + 1);
    if (!p) return NULL;
    memcpy(p, s, l);
    p[l] = '\0';
    return p;
}

static vec_t tokenize(const char *line) {
    vec_t v = {0};
    const char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|')) {
            vpush(&v, substr(p, 2)); p += 2; continue;
        }
        if (*p == ';' || *p == '&') {
            vpush(&v, substr(p, 1)); p++; continue;
        }
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            const char *start = p;
            while (*p && *p != q) p++;
            vpush(&v, substr(start, (size_t)(p - start)));
            if (*p == q) p++;
            continue;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p) && *p!=';' && *p!='&') {
            if ((p[0]=='&'&&p[1]=='&')||(p[0]=='|'&&p[1]=='|')) break;
            p++;
        }
        vpush(&v, substr(start, (size_t)(p - start)));
    }
    vpush(&v, NULL);
    return v;
}

static void free_tokens(vec_t *v) {
    for (int i = 0; i+1 < v->size; ++i) free(v->data[i]);
    free(v->data);
    v->data = NULL; v->size = v->cap = 0;
}

static command_t *parse_commands(vec_t *tok, int *out_n, char **out_cmdline) {
    int n = 0, cap = 8;
    command_t *arr = calloc(cap, sizeof(*arr));
    if (!arr) return NULL;

    size_t totlen = 0; for (int i=0; i+1<tok->size; ++i) totlen += strlen(tok->data[i])+1;
    char *cmdline = malloc(totlen+1); if (!cmdline) { free(arr); return NULL; }
    cmdline[0]='\0';
    for (int i=0; i+1<tok->size; ++i) { strcat(cmdline, tok->data[i]); if (i+2<tok->size) strcat(cmdline, " "); }
    if (out_cmdline) *out_cmdline = cmdline; else free(cmdline);

    char **argv = NULL; int argc = 0, argc_cap = 0;
    op_t op_next = OP_SEQ;

    for (int i = 0; i+1 < tok->size; ++i) {
        char *t = tok->data[i];
        if (!strcmp(t, "&&") || !strcmp(t, "||") || !strcmp(t, ";") || !strcmp(t, "&")) {
            if (argc > 0) {
                if (n == cap) { cap*=2; arr = realloc(arr, cap*sizeof(*arr)); }
                arr[n].argv = malloc((argc+1)*sizeof(char*));
                for (int k=0;k<argc;k++) arr[n].argv[k]=strdup(argv[k]);  
                arr[n].argv[argc]=NULL;
                arr[n].argc = argc;
                arr[n].background = 0;
                arr[n].next_op = OP_SEQ;
                n++;
                free(argv); argv=NULL; argc=argc_cap=0;
            }
            if (!strcmp(t, "&&"))      op_next = OP_AND;
            else if (!strcmp(t, "||")) op_next = OP_OR;
            else if (!strcmp(t, ";"))  op_next = OP_SEQ;
            else if (!strcmp(t, "&")) {
                if (n > 0) arr[n-1].background = 1;
                op_next = OP_SEQ;
            }
            if (n > 0) arr[n-1].next_op = op_next;
            continue;
        } else {
            if (argc == argc_cap) { argc_cap = argc_cap?argc_cap*2:8; argv = realloc(argv, argc_cap*sizeof(char*)); }
            argv[argc++] = t;
        }
    }
    if (argc > 0) {
        if (n == cap) { cap*=2; arr = realloc(arr, cap*sizeof(*arr)); }
        arr[n].argv = malloc((argc+1)*sizeof(char*));
        for (int k=0;k<argc;k++) arr[n].argv[k]=strdup(argv[k]);      
        arr[n].argv[argc]=NULL;
        arr[n].argc = argc;
        arr[n].background = 0;
        arr[n].next_op = OP_SEQ;
        n++;
        free(argv);
    }

    *out_n = n;
    return arr;
}

static void free_commands(command_t *arr, int n) {
    for (int i=0;i<n;i++) {
        for (int k=0;k<arr[i].argc;k++) free(arr[i].argv[k]); 
        free(arr[i].argv);
    }
    free(arr);
}

static void reap_bg_if_needed(void) {
    if (!g_sigchld) return;
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;
        struct timespec start = {0}, end = {0};
        char *cmdline = NULL;
        if (!remove_bg(pid, &start, &cmdline)) {
            clock_gettime(CLOCK_MONOTONIC, &end);
            double dt = elapsed_sec(start, end);
            fprintf(stderr, "[%d] exited (unknown bg), dt=%.6f s\n", pid, dt);
        } else {
            clock_gettime(CLOCK_MONOTONIC, &end);
            double dt = elapsed_sec(start, end);
            if (WIFEXITED(status)) {
                fprintf(stderr, "[%d] done (exit=%d), time=%.6f s — %s\n",
                        pid, WEXITSTATUS(status), dt, cmdline);
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "[%d] killed by signal %d, time=%.6f s — %s\n",
                        pid, WTERMSIG(status), dt, cmdline);
            }
            free(cmdline);
        }
    }
    g_sigchld = 0;
}

static int run_command(command_t *cmd, const char *cmdline, int *out_status) {
    if (cmd->argc > 0 && strcmp(cmd->argv[0], "cd") == 0) {
        const char *dir = cmd->argc >= 2 ? cmd->argv[1] : getenv("HOME");
        if (!dir) dir = "/";
        int rc = chdir(dir);
        if (rc != 0) perror("cd");
        *out_status = (rc == 0) ? 0 : 1;
        return 0;
    }
    if (cmd->argc > 0 && strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        *out_status = 1;
        return -1;
    } else if (pid == 0) {

        execvp(cmd->argv[0], cmd->argv);


        int err = errno;
        if (g_quiet) {
            
            if (err == ENOENT && strchr(cmd->argv[0], '/') == NULL) {
                const char msg[] = "Command not found\n";
                (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
            }
            
        } else {
            errno = err;
            perror("execvp");
        }
        _exit(127);
    } else {
        
        if (cmd->background) {
            add_bg(pid, t0, cmdline);
            if (!g_quiet) {
                printf("[%d] started in background — %s\n", pid, cmdline);
                fflush(stdout);
            }
            *out_status = 0;
            return 0;
        } else {
            int status;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                *out_status = 1;
                return -1;
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double dt = elapsed_sec(t0, t1);

            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if (!g_quiet) {
                    printf("exit=%d, time=%.6f s — %s\n", code, dt, cmdline);
                }
                *out_status = code;
            } else if (WIFSIGNALED(status)) {
                if (!g_quiet) {
                    printf("signal=%d, time=%.6f s — %s\n",
                           WTERMSIG(status), dt, cmdline);
                }
                *out_status = 128 + WTERMSIG(status);
            } else {
                *out_status = 1;
            }
            return 0;
        }
    }
}



int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    g_quiet = !isatty(STDIN_FILENO);

    if (g_quiet) {

        setvbuf(stdin, NULL, _IONBF, 0);
    }

    char *line = NULL; size_t cap = 0;
    for (;;) {
        reap_bg_if_needed();

        if (!g_quiet) {
            printf("vtsh> ");
            fflush(stdout);
        }

        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) { if (!g_quiet) printf("\n"); break; }
        if (n == 0) continue;
        if (line[n-1] == '\n') line[n-1] = '\0';

        vec_t tok = tokenize(line);
        if (tok.size <= 1) { free_tokens(&tok); continue; }

        int ncmd = 0; char *cmdline = NULL;
        command_t *arr = parse_commands(&tok, &ncmd, &cmdline);
        free_tokens(&tok);
        if (!arr || ncmd == 0) { free(cmdline); continue; }

        int last_status = 0;
        op_t prev_op = OP_NONE;
        for (int i=0; i<ncmd; ++i) {
            bool should = true;
            if (i > 0) {
                if (prev_op == OP_AND && last_status != 0) should = false;
                if (prev_op == OP_OR  && last_status == 0)  should = false;
            }
            if (should) {
                run_command(&arr[i], cmdline, &last_status);
            }
            prev_op = arr[i].next_op;
        }
        free_commands(arr, ncmd);
        free(cmdline);
    }
    free(line);
    return 0;
}
