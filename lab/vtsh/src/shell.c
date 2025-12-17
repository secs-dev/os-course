#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 4096
#define MAX_TOKENS 256

static void print_prompt(void) {
    if (isatty(STDIN_FILENO)) {
        (void)write(STDOUT_FILENO, "$ ", 2);
    }
}

static int read_line_fd(char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r == 0) {
            if (i == 0) return 0;  // EOF and nothing read
            break;                 // EOF after some data
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return 1;
}

static char *trim_left(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int is_redirect_token(const char *t) {
    return (strcmp(t, "<") == 0) || (strcmp(t, ">") == 0);
}

static int is_complex_redirect(const char *token) {
    // token like <file or >file or >a<b (complex)
    if (token[0] == '<' || token[0] == '>') {
        for (int i = 1; token[i] != '\0'; i++) {
            if (token[i] == '<' || token[i] == '>') return 1;
        }
    }
    return 0;
}

static int tokenize(const char *line, char *tokens[], int max_tokens) {
    int ntok = 0;
    const char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        // forbid >>
        if (*p == '>' && p[1] == '>') {
            if (ntok >= max_tokens - 1) break;
            char *t = malloc(3);
            if (!t) break;
            t[0] = '>';
            t[1] = '>';
            t[2] = '\0';
            tokens[ntok++] = t;
            p += 2;
            continue;
        }

        // handle <... or >... glued forms
        if (*p == '<' || *p == '>') {
            const char *start = p;
            p++;
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
            size_t len = (size_t)(p - start);

            int has_inner = 0;
            for (const char *c = start + 1; c < p; c++) {
                if (*c == '<' || *c == '>') {
                    has_inner = 1;
                    break;
                }
            }

            if (has_inner) {
                // complex token like >a<b => syntax error later
                if (ntok >= max_tokens - 1) break;
                char *t = malloc(len + 1);
                if (!t) break;
                memcpy(t, start, len);
                t[len] = '\0';
                tokens[ntok++] = t;
            } else if (len == 1) {
                // single < or >
                if (ntok >= max_tokens - 1) break;
                char *t = malloc(2);
                if (!t) break;
                t[0] = *start;
                t[1] = '\0';
                tokens[ntok++] = t;
            } else {
                // <file or >file => split into two tokens
                if (ntok >= max_tokens - 2) break;

                char *t1 = malloc(2);
                if (!t1) break;
                t1[0] = *start;
                t1[1] = '\0';
                tokens[ntok++] = t1;

                char *t2 = malloc(len);
                if (!t2) break;
                memcpy(t2, start + 1, len - 1);
                t2[len - 1] = '\0';
                tokens[ntok++] = t2;
            }
            continue;
        }

        // normal word
        const char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
        size_t len = (size_t)(p - start);

        if (len == 0) continue;
        if (ntok >= max_tokens - 1) break;

        char *t = malloc(len + 1);
        if (!t) break;
        memcpy(t, start, len);
        t[len] = '\0';
        tokens[ntok++] = t;
    }

    tokens[ntok] = NULL;
    return ntok;
}

static void free_tokens(char *tokens[], int ntok) {
    for (int i = 0; i < ntok; i++) free(tokens[i]);
}

static int get_self_exe_path(char *buf, size_t cap) {
    // Linux-specific: /proc/self/exe points to current executable
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return 1;
}

static int run_command_line(const char *line) {
    char *tokens[MAX_TOKENS];
    int ntok = tokenize(line, tokens, MAX_TOKENS);
    if (ntok == 0) return 0;

    // parse redirects and build argv
    char *argv[MAX_TOKENS];
    int argc = 0;
    const char *in_file = NULL;
    const char *out_file = NULL;

    int syntax_error = 0;

    for (int i = 0; i < ntok; i++) {
        if (strcmp(tokens[i], ">>") == 0) {  // unsupported
            syntax_error = 1;
            break;
        }

        if (is_complex_redirect(tokens[i])) {
            // any >a<b etc is syntax error for this lab
            syntax_error = 1;
            break;
        }

        if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
            if (i + 1 >= ntok) {
                syntax_error = 1;
                break;
            }
            if (is_redirect_token(tokens[i + 1]) || strcmp(tokens[i + 1], ">>") == 0) {
                syntax_error = 1;
                break;
            }

            if (strcmp(tokens[i], "<") == 0) {
                if (in_file != NULL) {
                    syntax_error = 1;
                    break;
                }
                in_file = tokens[i + 1];
            } else {
                if (out_file != NULL) {
                    syntax_error = 1;
                    break;
                }
                out_file = tokens[i + 1];
            }
            i++;  // skip filename
            continue;
        }

        argv[argc++] = tokens[i];
    }
    argv[argc] = NULL;

    if (syntax_error || argc == 0) {
        (void)write(STDOUT_FILENO, "Syntax error\n", 13);
        free_tokens(tokens, ntok);
        return 2;
    }

    // open redirections in parent
    int in_fd = -1, out_fd = -1;

    if (in_file != NULL) {
        in_fd = open(in_file, O_RDONLY);
        if (in_fd < 0) {
            (void)write(STDOUT_FILENO, "I/O error\n", 10);
            free_tokens(tokens, ntok);
            return 5;
        }
    }

    if (out_file != NULL) {
        out_fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out_fd < 0) {
            if (in_fd >= 0) close(in_fd);
            (void)write(STDOUT_FILENO, "I/O error\n", 10);
            free_tokens(tokens, ntok);
            return 5;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
        free_tokens(tokens, ntok);
        return 1;
    }

    if (pid == 0) {
        if (in_fd >= 0) {
            dup2(in_fd, STDIN_FILENO);
        }
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
        }
        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);

        // Fix nested shells: "./shell" should re-exec current binary (vtsh)
        if (strcmp(argv[0], "./shell") == 0) {
            char self[PATH_MAX];
            if (get_self_exe_path(self, sizeof(self))) {
                argv[0] = self;
                execv(argv[0], argv);
                _exit(127);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    int status = 0;
    (void)waitpid(pid, &status, 0);

    int rc = 0;
    if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
        if (rc == 127) {
            (void)write(STDOUT_FILENO, "Command not found\n", 18);
        }
        free_tokens(tokens, ntok);
        return rc;
    }

    free_tokens(tokens, ntok);
    return 1;
}

static int is_or_operator(const char *s) {
    return strcmp(s, "||") == 0;
}

static int run_with_or(const char *line) {
    // tokenize once to find ||
    char *tokens[MAX_TOKENS];
    int ntok = tokenize(line, tokens, MAX_TOKENS);
    if (ntok == 0) return 0;

    int or_pos = -1;
    for (int i = 0; i < ntok; i++) {
        if (is_or_operator(tokens[i])) {
            or_pos = i;
            break;
        }
    }

    if (or_pos == -1) {
        // run original line
        free_tokens(tokens, ntok);
        return run_command_line(line);
    }

    // build left/right strings
    char left[MAX_LINE] = {0};
    char right[MAX_LINE] = {0};

    for (int i = 0; i < or_pos; i++) {
        if (i > 0) strncat(left, " ", sizeof(left) - strlen(left) - 1);
        strncat(left, tokens[i], sizeof(left) - strlen(left) - 1);
    }
    for (int i = or_pos + 1; i < ntok; i++) {
        if (i > or_pos + 1) strncat(right, " ", sizeof(right) - strlen(right) - 1);
        strncat(right, tokens[i], sizeof(right) - strlen(right) - 1);
    }

    free_tokens(tokens, ntok);

    // empty sides => syntax error
    if (left[0] == '\0' || right[0] == '\0') {
        (void)write(STDOUT_FILENO, "Syntax error\n", 13);
        return 2;
    }

    int rc1 = run_command_line(left);
    if (rc1 != 0) return run_command_line(right);
    return rc1;
}

int main(void) {
    char line[MAX_LINE];

    while (1) {
        print_prompt();

        if (!read_line_fd(line, sizeof(line))) {
            break;
        }

        char *cmd = trim_left(line);
        if (*cmd == '\0') continue;

        (void)run_with_or(cmd);
    }

    return 0;
}
