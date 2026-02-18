#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int test_mode = 0;

void vtsh_set_test_mode(int enabled) { test_mode = enabled; }
const char* vtsh_prompt() { return "vtsh> "; }

typedef enum {
    TOK_WORD = 0,
    TOK_PIPE,
    TOK_SEMI,
    TOK_AMP,
    TOK_REDIR
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    int glued;
} Token;

typedef enum {
    REDIR_IN = 0,
    REDIR_OUT,
    REDIR_OUT_APPEND,
    REDIR_ERR,
    REDIR_ERR_APPEND,
    REDIR_ERR_TO_OUT
} RedirOp;

typedef struct {
    RedirOp op;
    char *path;
} Redir;

typedef struct {
    char *argv[128];
    int argc;
    Redir redirs[32];
    int redir_count;
} Command;

static void trim_right(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                    s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void trim_left(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void trim(char *s) { trim_left(s); trim_right(s); }

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void append_char(char **buf, size_t *len, size_t *cap, char c) {
    if (*cap == 0) {
        *cap = 64;
        *buf = (char *)malloc(*cap);
        (*buf)[0] = '\0';
    } else if (*len + 2 >= *cap) {
        *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static void append_str(char **buf, size_t *len, size_t *cap, const char *s) {
    if (!s) return;
    while (*s) {
        append_char(buf, len, cap, *s);
        s++;
    }
}

static void expand_var(const char *line, size_t *i, char **buf, size_t *len, size_t *cap) {
    size_t start = *i + 1;
    if (!line[start]) {
        append_char(buf, len, cap, '$');
        (*i)++;
        return;
    }

    if (!(isalpha((unsigned char)line[start]) || line[start] == '_')) {
        append_char(buf, len, cap, '$');
        (*i)++;
        return;
    }

    size_t j = start;
    while (line[j] && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;

    char name[128];
    size_t n = j - start;
    if (n >= sizeof(name)) n = sizeof(name) - 1;
    memcpy(name, line + start, n);
    name[n] = '\0';

    const char *val = getenv(name);
    if (val) append_str(buf, len, cap, val);

    *i = j;
}

static int add_token(Token *tokens, int *count, int max, TokenType type, const char *text, int glued) {
    if (*count >= max) return -1;
    tokens[*count].type = type;
    tokens[*count].text = text ? strdup(text) : NULL;
    tokens[*count].glued = glued;
    (*count)++;
    return 0;
}

static int tokenize_line(const char *line, Token *tokens, int max) {
    size_t i = 0;
    size_t L = strlen(line);
    int count = 0;
    int in_single = 0;
    int in_double = 0;
    char *buf = NULL;
    size_t len = 0, cap = 0;

    while (i < L) {
        char c = line[i];

        if (!in_single && !in_double) {
            if (isspace((unsigned char)c)) {
                if (len > 0) {
                    if (add_token(tokens, &count, max, TOK_WORD, buf, 0) < 0) break;
                    len = 0;
                    if (buf) buf[0] = '\0';
                }
                i++;
                continue;
            }

            if (c == '\'' || c == '"') {
                in_single = (c == '\'');
                in_double = (c == '"');
                i++;
                continue;
            }

            if (c == '$') {
                expand_var(line, &i, &buf, &len, &cap);
                continue;
            }

            if (c == '|' || c == ';' || c == '&') {
                if (len > 0) {
                    if (add_token(tokens, &count, max, TOK_WORD, buf, 0) < 0) break;
                    len = 0;
                    if (buf) buf[0] = '\0';
                }
                TokenType t = (c == ';') ? TOK_SEMI : (c == '|') ? TOK_PIPE : TOK_AMP;
                if (add_token(tokens, &count, max, t, (c == ';') ? ";" : (c == '|') ? "|" : "&", 0) < 0) {
                    break;
                }
                i++;
                continue;
            }

            if (c == '2' && len == 0) {
                if (i + 3 < L && strncmp(line + i, "2>&1", 4) == 0) {
                    if (add_token(tokens, &count, max, TOK_REDIR, "2>&1", 0) < 0) break;
                    i += 4;
                    continue;
                }
                if (i + 2 < L && strncmp(line + i, "2>>", 3) == 0) {
                    size_t next = i + 3;
                    int glued = (next < L && !isspace((unsigned char)line[next]) &&
                                 line[next] != '|' && line[next] != ';' && line[next] != '&');
                    if (add_token(tokens, &count, max, TOK_REDIR, "2>>", glued) < 0) break;
                    i += 3;
                    continue;
                }
                if (i + 1 < L && line[i + 1] == '>') {
                    size_t next = i + 2;
                    int glued = (next < L && !isspace((unsigned char)line[next]) &&
                                 line[next] != '|' && line[next] != ';' && line[next] != '&');
                    if (add_token(tokens, &count, max, TOK_REDIR, "2>", glued) < 0) break;
                    i += 2;
                    continue;
                }
            }

            if (len == 0 && (c == '>' || c == '<')) {
                if (len > 0) {
                    if (add_token(tokens, &count, max, TOK_WORD, buf, 0) < 0) break;
                    len = 0;
                    if (buf) buf[0] = '\0';
                }
                if (c == '>' && i + 1 < L && line[i + 1] == '>') {
                    size_t next = i + 2;
                    int glued = (next < L && !isspace((unsigned char)line[next]) &&
                                 line[next] != '|' && line[next] != ';' && line[next] != '&');
                    if (add_token(tokens, &count, max, TOK_REDIR, ">>", glued) < 0) break;
                    i += 2;
                } else {
                    size_t next = i + 1;
                    int glued = (next < L && !isspace((unsigned char)line[next]) &&
                                 line[next] != '|' && line[next] != ';' && line[next] != '&');
                    if (add_token(tokens, &count, max, TOK_REDIR, (c == '>') ? ">" : "<", glued) < 0) break;
                    i++;
                }
                continue;
            }

            append_char(&buf, &len, &cap, c);
            i++;
        } else if (in_single) {
            if (c == '\'') {
                in_single = 0;
                i++;
                continue;
            }
            append_char(&buf, &len, &cap, c);
            i++;
        } else {
            if (c == '"') {
                in_double = 0;
                i++;
                continue;
            }
            if (c == '$') {
                expand_var(line, &i, &buf, &len, &cap);
                continue;
            }
            append_char(&buf, &len, &cap, c);
            i++;
        }
    }

    if (len > 0) {
        add_token(tokens, &count, max, TOK_WORD, buf, 0);
    }
    free(buf);
    return count;
}

static void free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++) {
        free(tokens[i].text);
    }
}

static int add_redir(Command *cmd, RedirOp op, const char *path) {
    if (cmd->redir_count >= (int)(sizeof(cmd->redirs) / sizeof(cmd->redirs[0]))) return -1;
    cmd->redirs[cmd->redir_count].op = op;
    cmd->redirs[cmd->redir_count].path = path ? strdup(path) : NULL;
    cmd->redir_count++;
    return 0;
}

static void free_command(Command *cmd) {
    for (int i = 0; i < cmd->argc; i++) free(cmd->argv[i]);
    for (int i = 0; i < cmd->redir_count; i++) free(cmd->redirs[i].path);
}

static int parse_command_tokens(Token *tokens, int n, Command *cmd, int *syntax_error) {
    cmd->argc = 0;
    cmd->redir_count = 0;
    int seen_in = 0;
    int seen_out = 0;

    for (int i = 0; i < n; i++) {
        Token *t = &tokens[i];
        if (t->type == TOK_WORD) {
            if (cmd->argc < (int)(sizeof(cmd->argv) / sizeof(cmd->argv[0]) - 1)) {
                cmd->argv[cmd->argc++] = strdup(t->text);
            }
            continue;
        }

        if (t->type == TOK_REDIR) {
            const char *op = t->text;
            if (strcmp(op, "2>&1") == 0) {
                add_redir(cmd, REDIR_ERR_TO_OUT, NULL);
                continue;
            }

            if ((strcmp(op, ">>") == 0 || strcmp(op, "2>>") == 0) && t->glued) {
                *syntax_error = 1;
                return -1;
            }

            if (i + 1 >= n || tokens[i + 1].type != TOK_WORD) {
                *syntax_error = 1;
                return -1;
            }

            const char *path = tokens[i + 1].text;
            if (strcmp(op, "<") == 0) {
                if (seen_in) {
                    *syntax_error = 1;
                    return -1;
                }
                seen_in = 1;
                add_redir(cmd, REDIR_IN, path);
            } else if (strcmp(op, ">") == 0) {
                if (seen_out) {
                    *syntax_error = 1;
                    return -1;
                }
                seen_out = 1;
                add_redir(cmd, REDIR_OUT, path);
            } else if (strcmp(op, ">>") == 0) {
                if (seen_out) {
                    *syntax_error = 1;
                    return -1;
                }
                seen_out = 1;
                add_redir(cmd, REDIR_OUT_APPEND, path);
            } else if (strcmp(op, "2>") == 0) {
                add_redir(cmd, REDIR_ERR, path);
            } else if (strcmp(op, "2>>") == 0) {
                add_redir(cmd, REDIR_ERR_APPEND, path);
            } else {
                *syntax_error = 1;
                return -1;
            }
            i++;
            continue;
        }
    }

    cmd->argv[cmd->argc] = NULL;
    return 0;
}

static int apply_redirs_in_child(Command *cmd) {
    for (int i = 0; i < cmd->redir_count; i++) {
        Redir *r = &cmd->redirs[i];
        int fd = -1;
        switch (r->op) {
            case REDIR_IN:
                fd = open(r->path, O_RDONLY);
                if (fd < 0) return -1;
                dup2(fd, STDIN_FILENO);
                close(fd);
                break;
            case REDIR_OUT:
                fd = open(r->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) return -1;
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            case REDIR_OUT_APPEND:
                fd = open(r->path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) return -1;
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            case REDIR_ERR:
                fd = open(r->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) return -1;
                dup2(fd, STDERR_FILENO);
                close(fd);
                break;
            case REDIR_ERR_APPEND:
                fd = open(r->path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) return -1;
                dup2(fd, STDERR_FILENO);
                close(fd);
                break;
            case REDIR_ERR_TO_OUT:
                dup2(STDOUT_FILENO, STDERR_FILENO);
                break;
        }
    }
    return 0;
}

static int apply_redirs_in_parent(Command *cmd, int *save_in, int *save_out, int *save_err) {
    *save_in = dup(STDIN_FILENO);
    *save_out = dup(STDOUT_FILENO);
    *save_err = dup(STDERR_FILENO);
    if (*save_in < 0 || *save_out < 0 || *save_err < 0) return -1;

    if (apply_redirs_in_child(cmd) < 0) return -1;
    return 0;
}

static void restore_fds(int save_in, int save_out, int save_err) {
    if (save_in >= 0) { dup2(save_in, STDIN_FILENO); close(save_in); }
    if (save_out >= 0) { dup2(save_out, STDOUT_FILENO); close(save_out); }
    if (save_err >= 0) { dup2(save_err, STDERR_FILENO); close(save_err); }
}

static void write_stdout(const char *msg) {
    if (!msg) return;
    write(STDOUT_FILENO, msg, strlen(msg));
}

static int is_builtin(Command *cmd) {
    if (cmd->argc == 0) return 0;
    if (strcmp(cmd->argv[0], "cd") == 0) {
        return 1;
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        return 1;
    }
    return 0;
}

static int run_builtin(Command *cmd) {
    if (cmd->argc == 0) return 0;
    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *path = (cmd->argc > 1) ? cmd->argv[1] : getenv("HOME");
        if (!path) path = "/";
        if (chdir(path) < 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }
    return -1;
}

static void sigchld_handler(int signo) {
    (void)signo;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved;
}

static void ensure_sigchld_handler(void) {
    static int installed = 0;
    if (installed) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    installed = 1;
}

static int execute_pipeline(Token *tokens, int n, int background) {
    Command cmds[32];
    int cmd_count = 0;
    int syntax_error = 0;

    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || tokens[i].type == TOK_PIPE) {
            if (i == start) {
                write_stdout("Syntax error");
                return -1;
            }
            if (cmd_count >= (int)(sizeof(cmds) / sizeof(cmds[0]))) {
                write_stdout("Syntax error");
                return -1;
            }
            if (parse_command_tokens(tokens + start, i - start, &cmds[cmd_count], &syntax_error) < 0) {
                write_stdout("Syntax error");
                for (int j = 0; j < cmd_count; j++) free_command(&cmds[j]);
                free_command(&cmds[cmd_count]);
                return -1;
            }
            cmd_count++;
            start = i + 1;
        }
    }

    if (cmd_count == 0) return 0;

    if (cmd_count == 1 && cmds[0].argc == 0) {
        int save_in = -1, save_out = -1, save_err = -1;
        if (cmds[0].redir_count > 0) {
            if (apply_redirs_in_parent(&cmds[0], &save_in, &save_out, &save_err) < 0) {
                write_stdout("I/O error");
                restore_fds(save_in, save_out, save_err);
                free_command(&cmds[0]);
                return 0;
            }
        }
        restore_fds(save_in, save_out, save_err);
        free_command(&cmds[0]);
        return 0;
    }

    if (cmd_count == 1 && !background && is_builtin(&cmds[0])) {
        int save_in = -1, save_out = -1, save_err = -1;
        if (cmds[0].redir_count > 0) {
            if (apply_redirs_in_parent(&cmds[0], &save_in, &save_out, &save_err) < 0) {
                write_stdout("I/O error");
                restore_fds(save_in, save_out, save_err);
                free_command(&cmds[0]);
                return 0;
            }
        }
        double t0 = now_sec();
        run_builtin(&cmds[0]);
        double t1 = now_sec();
        restore_fds(save_in, save_out, save_err);
        if (!test_mode && !background) {
            const char *t = getenv("BSH_TIME");
            if (t && strcmp(t, "1") == 0) {
                fprintf(stderr, "time=%.3f s\n", (t1 - t0));
            }
        }
        free_command(&cmds[0]);
        return 0;
    }

    double t0 = now_sec();
    int pipes[32][2];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            write_stdout("I/O error");
            for (int j = 0; j < cmd_count; j++) free_command(&cmds[j]);
            return -1;
        }
    }

    int builtin_flags[32];
    int direct_exec_flags[32];
    int dot_shell_flags[32];
    for (int i = 0; i < cmd_count; i++) {
        builtin_flags[i] = is_builtin(&cmds[i]);
        if (cmds[i].argc > 0) {
            direct_exec_flags[i] = (strchr(cmds[i].argv[0], '/') != NULL);
            dot_shell_flags[i] = (strcmp(cmds[i].argv[0], "./shell") == 0);
        } else {
            direct_exec_flags[i] = 0;
            dot_shell_flags[i] = 0;
        }
    }

    pid_t pids[32];
    for (int i = 0; i < cmd_count; i++) {
        pid_t pid = builtin_flags[i] ? fork() : vfork();
        if (pid < 0) {
            write_stdout("I/O error");
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (!background) {
                for (int j = 0; j < i; j++) waitpid(pids[j], NULL, 0);
            }
            for (int j = 0; j < cmd_count; j++) free_command(&cmds[j]);
            return -1;
        }
        if (pid == 0) {
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            if (i < cmd_count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (apply_redirs_in_child(&cmds[i]) < 0) {
                if (builtin_flags[i]) {
                    write_stdout("I/O error");
                } else {
                    write(STDOUT_FILENO, "I/O error", 9);
                }
                _exit(0);
            }

            if (cmds[i].argc == 0) {
                _exit(0);
            }

            if (builtin_flags[i]) {
                run_builtin(&cmds[i]);
                _exit(0);
            }

            if (direct_exec_flags[i]) {
                execv(cmds[i].argv[0], cmds[i].argv);
                if (dot_shell_flags[i]) {
                    execv("/proc/self/exe", cmds[i].argv);
                }
            } else {
                execvp(cmds[i].argv[0], cmds[i].argv);
            }
            write(STDOUT_FILENO, "Command not found\n", 17);
            _exit(127);
        }
        pids[i] = pid;
    }

    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (!background) {
        for (int i = 0; i < cmd_count; i++) waitpid(pids[i], NULL, 0);
    }

    double t1 = now_sec();
    if (!test_mode && !background) {
        const char *t = getenv("BSH_TIME");
        if (t && strcmp(t, "1") == 0) {
            fprintf(stderr, "time=%.3f s\n", (t1 - t0));
        }
    }

    for (int i = 0; i < cmd_count; i++) free_command(&cmds[i]);
    return 0;
}

static void execute_tokens(Token *tokens, int count) {
    int idx = 0;
    while (idx < count) {
        while (idx < count && tokens[idx].type == TOK_SEMI) idx++;
        if (idx >= count) break;

        int start = idx;
        int background = 0;
        while (idx < count && tokens[idx].type != TOK_SEMI && tokens[idx].type != TOK_AMP) idx++;
        if (idx < count && tokens[idx].type == TOK_AMP) background = 1;

        if (idx > start) {
            execute_pipeline(tokens + start, idx - start, background);
        }

        idx++;
    }
}

void vtsh_execute(const char *input_command) {
    const char *m = getenv("VT_TEST_MODE");
    if (m && strcmp(m, "1") == 0) test_mode = 1;
    ensure_sigchld_handler();

    if (!input_command) return;

    char buf[4096];
    strncpy(buf, input_command, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    while (line) {
        trim(line);
        if (line[0] != '\0') {
            Token tokens[256];
            int count = tokenize_line(line, tokens, 256);
            if (count > 0) execute_tokens(tokens, count);
            free_tokens(tokens, count);
        }
        line = strtok_r(NULL, "\n", &save);
    }
}
