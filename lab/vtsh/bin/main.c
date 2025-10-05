#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vtsh.h"

#define DEL "\n\t \v\f"

char** parse_args(char* line, size_t len) {
  char* token = strtok(line, DEL);
  char** args = malloc(sizeof(char*) * (len + 1));
  size_t i = 0;
  while (token != NULL) {
    args[i] = token;
    // printf("%s", "parse_args\n");
    // printf("%s args\n", args[i]);
    i++;
    token = strtok(NULL, DEL);
  }
  args[i] = NULL;
  return args;
}

int main() {
  char* buf = NULL;
  size_t cup = 2000;
  while (10000) {
    printf("%s", vtsh_prompt());
    size_t len = getline(&buf, &cup, stdin);

    // printf("%s111\n", buf);

    if (len == -1) {
      break;
    }

    char** args = parse_args(buf, len);
    for (size_t i = 0; args[i] != NULL; i++) {
      // printf("%s 222 ", args[i]);
    }
    // printf("\n");

    pid_t pid = fork();
    if (pid == 0) {
      int res = execvp(args[0], args);
      if (res == -1) {
        printf("Command not found\n");
      }
      perror("execvp");
      exit(1);
    } else {
      wait(NULL);
    }
    free(args);
  }
  return 0;
}