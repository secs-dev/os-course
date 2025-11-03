#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*builtin_fn)(char **argv);

typedef struct {
	const char *name;
	builtin_fn fn;
} builtin_entry;

int builtin_cd(char **argv) {
	const char *path = argv[1];
	if (!path) {
		path = getenv("HOME");
		if (!path) path = "/";
	}
	if (chdir(path) != 0) {
		perror("cd");
		return 1;
	}
	return 0;
}

int builtin_pwd(char **argv) {
	(void)argv;
	char buf[4096];
	if (getcwd(buf, sizeof(buf)) == NULL) {
		perror("pwd");
		return 1;
	}
	puts(buf);
	return 0;
}

int builtin_exit(char **argv) {
	int code = 0;
	if (argv[1]) code = atoi(argv[1]);
	exit(code);
}

int builtin_echo(char **argv) {
	int i = 1;
	int newline = 1;
	if (argv[1] && strcmp(argv[1], "-n") == 0) {
		newline = 0;
		i = 2;
	}
	for (; argv[i]; ++i) {
		if (i > (newline ? 1 : 2)) putchar(' ');
		fputs(argv[i], stdout);
	}
	if (newline) putchar('\n');
	return 0;
}

int builtin_export(char **argv) {
	for (int i = 1; argv[i]; ++i) {
		char *eq = strchr(argv[i], '=');
		if (!eq) {
			fprintf(stderr, "export: invalid format: %s\n", argv[i]);
			return 1;
		}
		*eq = '\0';
		if (setenv(argv[i], eq + 1, 1) != 0) {
			perror("export");
			return 1;
		}
	}
	return 0;
}

int builtin_unset(char **argv) {
	for (int i = 1; argv[i]; ++i) {
		if (unsetenv(argv[i]) != 0) {
			perror("unset");
			return 1;
		}
	}
	return 0;
}

static const builtin_entry BUILTINS[] = {
	{"cd", builtin_cd},
	{"pwd", builtin_pwd},
	{"exit", builtin_exit},
	{"echo", builtin_echo},
	{"export", builtin_export},
	{"unset", builtin_unset},
};

bool is_builtin(const char *cmd) {
	if (!cmd) return false;
	for (size_t i = 0; i < sizeof(BUILTINS)/sizeof(BUILTINS[0]); ++i) {
		if (strcmp(cmd, BUILTINS[i].name) == 0) return true;
	}
	return false;
}

int run_builtin(char **argv) {
	if (!argv || !argv[0]) return 1;
	for (size_t i = 0; i < sizeof(BUILTINS)/sizeof(BUILTINS[0]); ++i) {
		if (strcmp(argv[0], BUILTINS[i].name) == 0) {
			return BUILTINS[i].fn(argv);
		}
	}
	return 1;
}
