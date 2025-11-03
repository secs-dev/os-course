#ifndef CSHELL_SHELL_H
#define CSHELL_SHELL_H

#include <stdbool.h>

#define MAX_TOKENS 256
#define MAX_CMDLINE 4096

typedef struct CommandSegment {
	char *argv[MAX_TOKENS];
	char *in_redir;    
	char *out_redir;   
	bool out_append;   
} CommandSegment;

typedef struct Pipeline {
	CommandSegment segments[MAX_TOKENS];
	int num_segments;
	bool run_in_background;
} Pipeline;

int builtin_cd(char **argv);
int builtin_pwd(char **argv);
int builtin_exit(char **argv);
int builtin_echo(char **argv);
int builtin_export(char **argv);
int builtin_unset(char **argv);

bool is_builtin(const char *cmd);
int run_builtin(char **argv);

int shell_run_interactive(void);
int shell_run_command(const char *cmdline);

#endif
