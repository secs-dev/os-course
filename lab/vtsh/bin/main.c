#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vtsh.h>

#define MAX_INPUT 1024
#define MAX_ARGS 20

int main(int argc, char *argv[]) {
    char input[MAX_INPUT];

    // Устанавливаем путь к исполняемому файлу
    if (argc > 0 && argv[0] != NULL) {
        set_vtsh_executable_path(argv[0]);
    }

    printf("%s", vtsh_prompt());
    fflush(stdout);

    while (1) {
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            break;
        } else {
            int i = 0;
            for (i = 0; i < strlen(input); i++) {
                if (input[i] == '\n') {
                    input[i] = '\0';
                    break;
                }
            }
            if (strcmp(input, "e") == 0 || strcmp(input, "exit") == 0 ||
                strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
                break;
            }
        }

        char *args[MAX_ARGS];

        if (strlen(input) == 0) {
            printf("%s", vtsh_prompt());
            fflush(stdout);
            continue;
        }

        if (strcmp(input, "cat") == 0) {
            char line[MAX_INPUT];
            while (fgets(line, MAX_INPUT, stdin) != NULL) {
                line[strcspn(line, "\n")] = 0;

                if (strlen(line) == 0) break;

                printf("%s\n", line);
                fflush(stdout);
            }
            printf("%s", vtsh_prompt());
            fflush(stdout);
        } else {
            parse_command(input, args);
            printf("%s", vtsh_prompt());
            fflush(stdout);
        }
    }

    return 0;
}
