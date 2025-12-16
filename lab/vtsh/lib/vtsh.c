#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_INPUT 1024
#define MAX_ARGS 20
#define HISTORY_FILE "history.txt"

struct command {
    char *command;
    char *args[MAX_ARGS];
    int count_args;
};

struct command commands[1024] = {};

static char *vtsh_executable_path = NULL;

void set_vtsh_executable_path(char *path) {
    if (vtsh_executable_path != NULL) {
        free(vtsh_executable_path);
    }
    vtsh_executable_path = path ? strdup(path) : NULL;
}

void add_to_history(char *command) {
    FILE *file = fopen(HISTORY_FILE, "a");
    if (file != NULL) {
        fprintf(file, "%s\n", command);
        fclose(file);
    }
}

void execute_command_history() {
    FILE *file = fopen(HISTORY_FILE, "r");
    if (file != NULL) {
        char line[MAX_INPUT];
        while (fgets(line, sizeof(line), file) != NULL) {
            printf("%s", line);
        }
        fclose(file);
    }
}

int execute_command(char **input) {
    fflush(stdout);

    if (strcmp(input[0], "./shell") == 0 || strcmp(input[0], "./vtsh") == 0 ||
        strcmp(input[0], "shell") == 0 || strcmp(input[0], "vtsh") == 0) {
        pid_t pid = vfork();
        if (pid == 0) {
            char *exec_path = vtsh_executable_path ? vtsh_executable_path : "./shell";
            char *new_args[] = {exec_path, NULL};
            execv(exec_path, new_args);
            // Если не получилось, пробуем через PATH
            execlp("vtsh", "vtsh", NULL);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        }
        return -1;
    }

    pid_t pid = vfork();
    if (pid == 0) {
        if (strchr(input[0], '/') != NULL) {
            execv(input[0], input);
        }

        execvp(input[0], input);

        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        int exit_status = WEXITSTATUS(status);

        if (exit_status == 127) {
            printf("Command not found\n");
        }

        return exit_status;
    } else {
        return -1;
    }
}

int add_command(char *input, int i) {
    char *input_copy = strdup(input);
    char *token = strtok(input_copy, " \n\t\r");

    if (token == NULL) {
        free(input_copy);
        return -1;
    }

    commands[i].command = strdup(token);
    commands[i].args[0] = strdup(token);
    commands[i].count_args = 1;

    int arg_count = 1;
    while ((token = strtok(NULL, " \n\t\r")) != NULL) {
        commands[i].args[arg_count] = strdup(token);
        commands[i].count_args++;
        arg_count++;
    }
    commands[i].args[arg_count] = NULL;

    add_to_history(input);
    free(input_copy);
    return 0;
}

int parse_command(char *input, char **args) {
    char *input_copy = strdup(input);
    char *save_token;

    if (strcmp(input, "history") == 0) {
        execute_command_history();
        free(input_copy);
        return 0;
    }

    if (strstr(input, ";") != NULL) {
        char *token = strtok_r(input_copy, ";", &save_token);
        int i = 0;
        int last_comm_status = 0;
        while (token != NULL) {
            while (*token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') end--;
            *(end + 1) = '\0';

            if (strcmp("history", token) == 0) {
                execute_command_history();
            }

            if (strlen(token) > 0) {
                add_command(token, i);
                last_comm_status = execute_command(commands[i].args);
            }

            token = strtok_r(NULL, ";", &save_token);
            i++;
        }
        free(input_copy);
        return last_comm_status;
    } else {
        // Обычная команда без ;
        int i = 0;
        char *token = strtok_r(input_copy, " \n\t\r", &save_token);

        if (token == NULL) {
            free(input_copy);
            return 0;
        }

        while (token != NULL && i < MAX_ARGS - 1) {
            args[i++] = strdup(token);
            token = strtok_r(NULL, " \n\t\r", &save_token);
        }
        args[i] = NULL;

        add_to_history(input);

        int result = execute_command(args);

        for (int j = 0; j < i; j++) {
            free(args[j]);
        }

        free(input_copy);
        return result;
    }
}

const char* vtsh_prompt() {
    return "vtsh> ";
}