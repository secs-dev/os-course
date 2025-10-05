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

char* replace_all_chars(const char* src, const char* repl) {
  if (!src || !repl)
    return NULL;

  size_t len_src = strlen(src);
  size_t len_repl = strlen(repl);

  // Размер результата = длина исходной строки * длина замены + 1 для '\0'
  size_t len_result = len_src * len_repl + 1;

  char* result = malloc(len_result);
  if (!result)
    return NULL;  // Проверка на успешное выделение памяти

  result[0] = '\0';  // Инициализируем пустую строку

  // Конкатенация замены len_src раз
  for (size_t i = 0; i < len_src; i++) {
    strcat(result, repl);
  }

  return result;
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
      printf("%s 222 ", args[i]);
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