#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t got_sigint = 0;

static void on_sigint(int signo) {
	(void)signo;
	got_sigint = 1;
	ssize_t _w = write(STDOUT_FILENO, "\n", 1);
	(void)_w;
}

static void install_sigint_handler(void) {
	struct sigaction sa = {0};
	sa.sa_handler = on_sigint;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
}

static void reset_sigint_handler(void) {
	struct sigaction sa = {0};
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
}

static char *trim(char *s) {
	if (!s) return s;
	while (*s == ' ' || *s == '\t' || *s == '\n') s++;
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n')) --end;
	*end = '\0';
	return s;
}

static int tokenize(char *line, char **tokens, int max_tokens) {
	int count = 0;
	char *p = line;
	char tmp[MAX_CMDLINE];
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		if (count >= max_tokens - 1) break;

    if (*p == '|' || *p == '<' || *p == '>' || *p == '&') {
            if (*p == '>') {
                if (*(p+1) == '>') { tokens[count++] = strdup(">>"); p += 2; }
                else { tokens[count++] = strdup(">"); p += 1; }
            } else if (*p == '|') { tokens[count++] = strdup("|"); p += 1; }
            else if (*p == '<') { tokens[count++] = strdup("<"); p += 1; }
            else {
                if (*(p+1) == '&') { tokens[count++] = strdup("&&"); p += 2; }
                else { tokens[count++] = strdup("&"); p += 1; }
            }
            continue;
    }

		int tpos = 0;
		while (*p && *p != ' ' && *p != '\t' && *p != '|' && *p != '<' && *p != '>' && *p != '&') {
			if (*p == '\'' || *p == '"') {
				int quote = *p++;
				while (*p && *p != quote) {
					if (tpos < (int)sizeof(tmp)-1) tmp[tpos++] = *p;
					p++;
				}
				if (*p == quote) p++;
			} else {
				if (tpos < (int)sizeof(tmp)-1) tmp[tpos++] = *p;
				p++;
			}
		}
		tmp[tpos] = '\0';
		tokens[count++] = strdup(tmp);
	}
	tokens[count] = NULL;
	return count;
}

static int parse_pipeline(char **tokens, Pipeline *pl) {
	memset(pl, 0, sizeof(*pl));
	CommandSegment *seg = &pl->segments[0];
	pl->num_segments = 1;
	int argc = 0;
	for (int i = 0; tokens[i]; ++i) {
		char *t = tokens[i];
		if (strcmp(t, "|") == 0) {
			seg->argv[argc] = NULL;
			pl->num_segments++;
			seg = &pl->segments[pl->num_segments - 1];
			argc = 0;
			continue;
		}
		if (strcmp(t, "<") == 0) {
			pl->segments[pl->num_segments - 1].in_redir = tokens[++i];
			continue;
		}
		if (strcmp(t, ">") == 0 || strcmp(t, ">>") == 0) {
			pl->segments[pl->num_segments - 1].out_redir = tokens[++i];
			pl->segments[pl->num_segments - 1].out_append = (t[1] == '>');
			continue;
		}
		if (tokens[i+1] == NULL && strcmp(t, "&") == 0) {
			pl->run_in_background = true;
			continue;
		}
		seg->argv[argc++] = t;
	}
	seg->argv[argc] = NULL;
	return 0;
}

static int exec_pipeline(Pipeline *pl) {
	int num = pl->num_segments;
	int pipes[num > 1 ? num - 1 : 1][2];
	for (int i = 0; i < num - 1; ++i) {
		if (pipe(pipes[i]) != 0) { perror("pipe"); return 1; }
	}

	pid_t pids[num];
	for (int i = 0; i < num; ++i) {
		pid_t pid = fork();
		if (pid < 0) { perror("fork"); return 1; }
		if (pid == 0) {
			reset_sigint_handler();
			if (i == 0) {
				if (pl->segments[i].in_redir) {
					int fd = open(pl->segments[i].in_redir, O_RDONLY);
					if (fd < 0) { perror("open"); _exit(127); }
					dup2(fd, STDIN_FILENO); close(fd);
				}
			} else {
				dup2(pipes[i-1][0], STDIN_FILENO);
			}
			if (i == num - 1) {
				if (pl->segments[i].out_redir) {
					int flags = O_WRONLY | O_CREAT | (pl->segments[i].out_append ? O_APPEND : O_TRUNC);
					int fd = open(pl->segments[i].out_redir, flags, 0666);
					if (fd < 0) { perror("open"); _exit(127); }
					dup2(fd, STDOUT_FILENO); close(fd);
				}
			} else {
				dup2(pipes[i][1], STDOUT_FILENO);
			}
			for (int j = 0; j < num - 1; ++j) { close(pipes[j][0]); close(pipes[j][1]); }

            if (!pl->segments[i].argv[0]) _exit(0);
            if (is_builtin(pl->segments[i].argv[0])) {
				int rc = run_builtin(pl->segments[i].argv);
				_exit(rc);
			}
            if (strcmp(pl->segments[i].argv[0], "./shell") == 0) {
                char self_path[4096];
                ssize_t n = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
                if (n > 0) {
                    self_path[n] = '\0';
                    execv(self_path, pl->segments[i].argv);
                }
            }
            execvp(pl->segments[i].argv[0], pl->segments[i].argv);
            write(STDOUT_FILENO, "Command not found\n", 18);
            _exit(0);
		}
		pids[i] = pid;
	}
	for (int j = 0; j < num - 1; ++j) { close(pipes[j][0]); close(pipes[j][1]); }

	int status = 0;
	if (!pl->run_in_background) {
		for (int i = 0; i < num; ++i) {
			int st; waitpid(pids[i], &st, 0);
			if (i == num - 1) status = st;
		}
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static double diff_seconds(const struct timespec *a, const struct timespec *b) {
	long sec = b->tv_sec - a->tv_sec;
	long nsec = b->tv_nsec - a->tv_nsec;
	if (nsec < 0) { sec -= 1; nsec += 1000000000L; }
	return (double)sec + (double)nsec / 1e9;
}

int shell_run_command(const char *cmdline) {
    char *buf = strdup(cmdline);
    if (!buf) return 1;
    char *tokens[MAX_TOKENS] = {0};
    int n = tokenize(buf, tokens, MAX_TOKENS);
    if (n == 0) { free(buf); return 0; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int rc = 0;
    for (int i = 0; tokens[i]; ) {
            char *slice[MAX_TOKENS];
            int s = 0;
            while (tokens[i] && strcmp(tokens[i], "&&") != 0) {
                    slice[s++] = tokens[i++];
            }
            slice[s] = NULL;

            if (s > 0) {
                    Pipeline pl; parse_pipeline(slice, &pl);
                    if (pl.num_segments == 1 && pl.segments[0].argv[0] && is_builtin(pl.segments[0].argv[0]) && !pl.run_in_background && !pl.segments[0].in_redir && !pl.segments[0].out_redir) {
                            rc = run_builtin(pl.segments[0].argv);
                    } else {
                            rc = exec_pipeline(&pl);
                    }
            }

            if (tokens[i] && strcmp(tokens[i], "&&") == 0) {
                    i++; 
                    if (rc != 0) break;
            }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; tokens[i]; ++i) free(tokens[i]);
    free(buf);
    double elapsed = diff_seconds(&t0, &t1);
    fprintf(stderr, "time: %.3f s\n", elapsed);
    return rc;
}

int shell_run_interactive(void) {
	install_sigint_handler();
	setvbuf(stdin, NULL, _IONBF, 0);
	char line[MAX_CMDLINE];
	while (1) {
		got_sigint = 0;
		fputs("cshell$ ", stdout);
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) {
			putchar('\n');
			break;
		}
		char *cmd = trim(line);
		if (*cmd == '\0') continue;
		shell_run_command(cmd);
	}
	return 0;
}

int main(int argc, char **argv) {
	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
		return shell_run_command(argv[2]);
	}
	return shell_run_interactive();
}
