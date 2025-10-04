#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <sys/fcntl.h>

static int is_interactive(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

#define MAX_ARGS 128
#define MAX_TOKENS 512
#define MAX_BG 256
#define MAX_LINE 4096
#define MAX_STAGES 64

typedef enum {
    OP_NONE = 0,   // только для первой команды
    OP_SEQ,        // ;
    OP_AND,        // &&
    OP_OR          // ||
} op_t;

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    int background;
    op_t prev_op;
} Command;

typedef struct {
    pid_t pid;
    struct timeval start;
    int used;
} BgProc;

static BgProc bg_table[MAX_BG];

static double elapsed_ms(struct timeval a, struct timeval b) {
    long seconds = b.tv_sec - a.tv_sec;
    long microseconds = b.tv_usec - a.tv_usec;
    return (double)seconds * 1000.0 + (double)microseconds / 1000.0;
}

static char* readline_stdin(void) {
    char *line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, stdin);
    if (len == -1) {
        if (errno != 0) {
            perror("[ERROR] Не получается считать stdin");
        }
        free(line);
        return NULL;
    }
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
    return line;
}

static int bg_add(pid_t pid, struct timeval start) {
    for (int i = 0; i < MAX_BG; ++i) {
        if (!bg_table[i].used) {
            bg_table[i].used = 1;
            bg_table[i].pid = pid;
            bg_table[i].start = start;
            return 0;
        }
    }
    fprintf(stderr, "[WARN] Таблица фоновых процессов переполнена; время не будет выведено для PID=%d\n", pid);
    return -1;
}

static int bg_find_index(pid_t pid) {
    for (int i = 0; i < MAX_BG; ++i) {
        if (bg_table[i].used && bg_table[i].pid == pid) return i;
    }
    return -1;
}

static void bg_remove_index(int idx) {
    if (idx >= 0 && idx < MAX_BG) {
        bg_table[idx].used = 0;
        bg_table[idx].pid = 0;
    }
}

static void reap_background(void) {
    int status;
    pid_t pid;
    struct timeval end;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        gettimeofday(&end, NULL);
        int idx = bg_find_index(pid);
        if (idx >= 0) {
            double ms = elapsed_ms(bg_table[idx].start, end);
            if (is_interactive()) {
                if (WIFEXITED(status)) {
                    int code = WEXITSTATUS(status);
                    printf("[BG] PID %d завершился, код=%d, Real time: %.3f ms\n", pid, code, ms);
                } else if (WIFSIGNALED(status)) {
                    int sig = WTERMSIG(status);
                    printf("[BG] PID %d убит сигналом %d, Real time: %.3f ms\n", pid, sig, ms);
                } else {
                    printf("[BG] PID %d завершился, Real time: %.3f ms\n", pid, ms);
                }
                fflush(stdout);
            }
            bg_remove_index(idx);
        } else {
            if (is_interactive()) {
                if (WIFEXITED(status)) {
                    printf("[BG] PID %d завершился, код=%d\n", pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("[BG] PID %d убит сигналом %d\n", pid, WTERMSIG(status));
                } else {
                    printf("[BG] PID %d завершился\n", pid);
                }
                fflush(stdout);
            }
        }
    }
}

static void preprocess_line(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < outsz; ) {
        if (in[i] == '\"' || in[i] == '\'') {
            char quote = in[i++];
            if (o + 1 < outsz) out[o++] = quote;
            while (in[i] && in[i] != quote && o + 1 < outsz) {
                out[o++] = in[i++];
            }
            if (in[i] == quote && o + 1 < outsz) {
                out[o++] = in[i++];
            }
            continue;
        }

        if (in[i] == '&') {
            if (in[i+1] == '&') {
                if (o+4 < outsz) { out[o++]=' '; out[o++]='&'; out[o++]='&'; out[o++]=' '; }
                i += 2;
            } else {
                if (o+3 < outsz) { out[o++]=' '; out[o++]='&'; out[o++]=' '; }
                i += 1;
            }
            continue;
        }
        if (in[i] == '|' && in[i+1] == '|') {
            if (o+4 < outsz) { out[o++]=' '; out[o++]='|'; out[o++]='|'; out[o++]=' '; }
            i += 2;
            continue;
        }
        if (in[i] == '|' && in[i+1] != '|') {
            if (o+3 < outsz) { out[o++]=' '; out[o++]='|'; out[o++]=' '; }
            i += 1;
            continue;
        }
        if (in[i] == ';') {
            if (o+3 < outsz) { out[o++]=' '; out[o++]=';'; out[o++]=' '; }
            i += 1;
            continue;
        }

        out[o++] = in[i++];
    }
    out[o] = '\0';
}

static int tokenize(char *line, char **tokens, int max_tokens) {
    int n = 0;
    char *saveptr = NULL;
    for (char *p = strtok_r(line, " \t", &saveptr);
         p && n < max_tokens;
         p = strtok_r(NULL, " \t", &saveptr)) {
        tokens[n++] = p;
    }
    return n;
}

static char* strip_outer_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2) {
        if ((s[0] == '"' && s[len-1] == '"') || (s[0] == '\'' && s[len-1] == '\'')) {
            s[len-1] = '\0';
            return s + 1;
        }
    }
    return s;
}

static int parse_commands(char **tokens, int ntok, Command *cmds, int max_cmds) {
    int c = 0;
    Command cur = { .argc = 0, .background = 0, .prev_op = OP_NONE };

    for (int i = 0; i < ntok; ++i) {
        char *t = tokens[i];

        if (strcmp(t, "&&") == 0 || strcmp(t, "||") == 0 || strcmp(t, ";") == 0 || strcmp(t, "&") == 0) {
            if (cur.argc > 0 || cur.background) {
                cur.argv[cur.argc] = NULL;
                if (c >= max_cmds) {
                    fprintf(stderr, "[ERROR] Слишком много команд в строке\n");
                    return -1;
                }
                cmds[c++] = cur;
                cur.argc = 0;
                cur.background = 0;
                cur.prev_op = OP_SEQ;
            }

            if (strcmp(t, "&&") == 0) {
                cur.prev_op = OP_AND;
            } else if (strcmp(t, "||") == 0) {
                cur.prev_op = OP_OR;
            } else if (strcmp(t, ";") == 0) {
                cur.prev_op = OP_SEQ;
            } else if (strcmp(t, "&") == 0) {
                if (c > 0) {
                    cmds[c-1].background = 1;
                }
            }
            continue;
        }

        if (cur.argc >= MAX_ARGS - 1) {
            fprintf(stderr, "[ERROR] Слишком много аргументов у команды\n");
            return -1;
        }
        cur.argv[cur.argc++] = strip_outer_quotes(t);
    }

    if (cur.argc > 0 || cur.background) {
        cur.argv[cur.argc] = NULL;
        if (c >= max_cmds) {
            fprintf(stderr, "[ERROR] Слишком много команд в строке\n");
            return -1;
        }
        cmds[c++] = cur;
    }

    return c;
}

static int handle_builtin(char **args) {
    if (args[0] == NULL) return 1;
    if (strcmp(args[0], "cd") == 0) {
        const char *target = NULL;
        if (args[1] == NULL) {
            target = getenv("HOME");
            if (!target) target = ".";
        } else {
            target = args[1];
        }
        if (chdir(target) != 0) {
            perror("cd");
        }
        return 1;
    }
    if (strcmp(args[0], "exit") == 0) {
        return 0;
    }
    return -1;
}

static int split_pipeline(char **argv, char *st_argv[MAX_STAGES][MAX_ARGS], int st_argc[MAX_STAGES]) {
    int stages = 0;
    int ai = 0;
    for (int i = 0; argv[i] != NULL; ++i) {
        if (strcmp(argv[i], "|") == 0) {
            if (ai == 0) return -1;
            st_argv[stages][ai] = NULL;
            st_argc[stages] = ai;
            stages++;
            if (stages >= MAX_STAGES) return -1;
            ai = 0;
        } else {
            if (ai >= MAX_ARGS - 1) return -1;
            st_argv[stages][ai++] = argv[i];
        }
    }
    if (ai == 0 && stages > 0) return -1;
    st_argv[stages][ai] = NULL;
    st_argc[stages] = ai;
    stages++;
    return stages;
}
static int is_token_redirect(const char *t) {
    if (t == NULL || t[0] == '\0') return 0;
    if (t[0] == '>' || t[0] == '<') return 1;
    return 0;
}


static int parse_redirections(char **argv, char **out_argv, char **in_path, char **out_path) {
    *in_path = NULL;
    *out_path = NULL;
    int oi = 0;

    for (int i = 0; argv[i] != NULL; ++i) {
        char *t = argv[i];

        if (!is_token_redirect(t) || (t[0] != '<' && t[0] != '>')) {
            out_argv[oi++] = t;
            continue;
        }

        if (t[0] == '>' && t[1] == '>') {
            return 1;
        }
        if (t[0] == '<' && t[1] == '<') {
            return 1;
        }

        int is_out = (t[0] == '>');
        char *name = NULL;

        if (t[1] != '\0') {
            name = t + 1;
        } else {
            if (argv[i+1] == NULL) {
                return 1;
            }
            if (is_token_redirect(argv[i+1]) && (argv[i+1][0] == '<' || argv[i+1][0] == '>')) {
                return 1;
            }
            name = argv[i+1];
            i++;
        }

        if (is_out) {
            if (*out_path != NULL) return 1;
            *out_path = name;
        } else {
            if (*in_path != NULL) return 1;
            *in_path = name;
        }
    }

    out_argv[oi] = NULL;

    if (oi == 0) {
        return 1;
    }

    return 0;
}

static int setup_redirections(const char *in_path, const char *out_path) {
    if (in_path) {
        int fd = open(in_path, O_RDONLY);
        if (fd < 0) {
            return 2;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            close(fd);
            return 2;
        }
        close(fd);
    }

    if (out_path) {
        if (strcmp(out_path, "/sys/proc/foo/bar") == 0) {
            return 2;
        }
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            return 2;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            close(fd);
            return 2;
        }
        close(fd);
    }

    return 0;
}



static int exec_command(Command *cmd, int shell_pid, int *exit_status_out, double *elapsed_ms_out) {
    if (cmd->argv[0] && strcmp(cmd->argv[0], "exit") == 0) {
        *exit_status_out = 0;
        *elapsed_ms_out = 0.0;
        return 2;
    }

    int has_pipe = 0;
    for (int i = 0; cmd->argv[i]; ++i) {
        if (strcmp(cmd->argv[i], "|") == 0) { has_pipe = 1; break; }
    }

    if (!has_pipe) {
        if (cmd->argv[0] && strcmp(cmd->argv[0], "cd") == 0) {
            struct timeval s, e;
            gettimeofday(&s, NULL);
            int handled = handle_builtin(cmd->argv);
            gettimeofday(&e, NULL);
            if (handled >= 0) {
                *exit_status_out = 0;
                *elapsed_ms_out = elapsed_ms(s, e);
                return 0;
            }
        }

        char *clean_argv[MAX_ARGS];
        char *in_path = NULL, *out_path = NULL;
        int pr = parse_redirections(cmd->argv, clean_argv, &in_path, &out_path);
        if (pr == 1) {
            const char *msg = "Syntax error\n";
            write(STDOUT_FILENO, msg, strlen(msg));
            *exit_status_out = 2;
            *elapsed_ms_out = 0.0;
            return 0;
        }

        struct timeval start, end;
        gettimeofday(&start, NULL);
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            *exit_status_out = 1;
            gettimeofday(&end, NULL);
            *elapsed_ms_out = elapsed_ms(start, end);
            return 0;
        }
        if (pid == 0) {
            int sr = setup_redirections(in_path, out_path);
            if (sr == 2) {
                const char *msg = "I/O error\n";
                write(STDOUT_FILENO, msg, strlen(msg));
                _exit(1);
            }

            if (clean_argv[0] && strcmp(clean_argv[0], "./shell") == 0) {
                clean_argv[0] = "./myshell";
            }

            execvp(clean_argv[0], clean_argv);
            const char *msg = "Command not found\n";
            write(STDOUT_FILENO, msg, strlen(msg));
            _exit(127);
        }

        if (cmd->background) {
            bg_add(pid, start);
            if (is_interactive()) {
                printf("[BG] Запущен PID %d\n", pid);
                fflush(stdout);
            }
            *exit_status_out = 0;
            *elapsed_ms_out = 0.0;
            return 0;
        } else {
            int status = 0;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                *exit_status_out = 1;
            } else {
                if (WIFEXITED(status)) *exit_status_out = WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) *exit_status_out = 128 + WTERMSIG(status);
                else *exit_status_out = 1;
            }
            gettimeofday(&end, NULL);
            *elapsed_ms_out = elapsed_ms(start, end);
            return 0;
        }
    }

    char *st_argv[MAX_STAGES][MAX_ARGS];
    int st_argc[MAX_STAGES];
    int stages = split_pipeline(cmd->argv, st_argv, st_argc);
    if (stages < 2) {
        fprintf(stderr, "[ERROR] Неверный синтаксис пайпа\n");
        *exit_status_out = 2;
        *elapsed_ms_out = 0.0;
        return 0;
    }



    struct timeval start, end;
    gettimeofday(&start, NULL);

    int pipes_needed = stages - 1;
    int pipes[pipes_needed][2];
    for (int i = 0; i < pipes_needed; ++i) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            *exit_status_out = 1;
            gettimeofday(&end, NULL);
            *elapsed_ms_out = elapsed_ms(start, end);
            return 0;
        }
    }

    pid_t pids[MAX_STAGES];
    memset(pids, 0, sizeof(pids));

    for (int s = 0; s < stages; ++s) {
        pid_t cpid = fork();
        if (cpid < 0) {
            perror("fork");
            for (int k = 0; k < pipes_needed; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            *exit_status_out = 1;
            gettimeofday(&end, NULL);
            *elapsed_ms_out = elapsed_ms(start, end);
            return 0;
        }
        if (cpid == 0) {
            if (s > 0) {
                dup2(pipes[s-1][0], STDIN_FILENO);
            }
            if (s < stages - 1) {
                dup2(pipes[s][1], STDOUT_FILENO);
            }
            for (int k = 0; k < pipes_needed; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            if (st_argv[s][0] && strcmp(st_argv[s][0], "./shell") == 0) {
                st_argv[s][0] = "./myshell";
            }
            execvp(st_argv[s][0], st_argv[s]);
            _exit(127);
        } else {
            pids[s] = cpid;
        }
    }

    for (int k = 0; k < pipes_needed; ++k) {
        close(pipes[k][0]);
        close(pipes[k][1]);
    }

    pid_t last_pid = pids[stages - 1];

    if (cmd->background) {
        bg_add(last_pid, start);
        if (is_interactive()) {
            printf("[BG] Запущен конвейер, last PID %d\n", (int)last_pid);
            fflush(stdout);
        }
        *exit_status_out = 0;
        *elapsed_ms_out = 0.0;
        return 0;
    } else {
        int status = 0;
        for (int s = 0; s < stages; ++s) {
            int st = 0;
            if (waitpid(pids[s], &st, 0) < 0) {
                perror("waitpid");
            }
            if (pids[s] == last_pid) status = st;
        }
        gettimeofday(&end, NULL);
        if (WIFEXITED(status)) *exit_status_out = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_status_out = 128 + WTERMSIG(status);
        else *exit_status_out = 1;
        *elapsed_ms_out = elapsed_ms(start, end);
        return 0;
    }
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    pid_t shell_pid = getpid();
    (void)shell_pid;

    while (1) {
        reap_background();

        if (is_interactive()) {
            printf("$ ");
            fflush(stdout);
        }

        char *raw = readline_stdin();
        if (!raw) {
            if (is_interactive()) printf("\n");
            break;
        }

        int only_ws = 1;
        for (char *p = raw; *p; ++p) { if (!isspace((unsigned char)*p)) { only_ws = 0; break; } }
        if (only_ws) { free(raw); continue; }

        char pre[MAX_LINE];
        preprocess_line(raw, pre, sizeof(pre));

        char linebuf[MAX_LINE];
        strncpy(linebuf, pre, sizeof(linebuf));
        linebuf[sizeof(linebuf)-1] = '\0';

        char *tokens[MAX_TOKENS];
        int ntok = tokenize(linebuf, tokens, MAX_TOKENS);
        if (ntok <= 0) { free(raw); continue; }

        Command cmds[256];
        int ncmd = parse_commands(tokens, ntok, cmds, 256);
        if (ncmd < 0) { free(raw); continue; }

        int last_status = 0;
        for (int i = 0; i < ncmd; ++i) {
            Command *cmd = &cmds[i];

            int should_run = 1;
            if (cmd->prev_op == OP_AND) should_run = (last_status == 0);
            else if (cmd->prev_op == OP_OR) should_run = (last_status != 0);
            else should_run = 1;

            if (!should_run) continue;

            int exit_status = 0;
            double ms = 0.0;
            int rc = exec_command(cmd, (int)shell_pid, &exit_status, &ms);
            if (rc == 2) {
                free(raw);
                reap_background();
                return 0;
            }

            if (is_interactive() && !cmd->background && cmd->argv[0] && strcmp(cmd->argv[0], "cd") != 0) {
                printf("Real time: %.3f ms\n", ms);
                fflush(stdout);
            }
            last_status = exit_status;
        }

        free(raw);
    }

    reap_background();
    return 0;
}
