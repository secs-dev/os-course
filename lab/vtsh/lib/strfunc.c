#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

char** parse_args(char* line, size_t len, char* del) {
  char* token = strtok(line, del);
  char** args = malloc(sizeof(char*) * (len + 1));
  size_t i = 0;
  while (token != NULL) {
    args[i] = token;
    // printf("%s", "parse_args\n");
    // printf("%s args\n", args[i]);
    i++;
    token = strtok(NULL, del);
  }
  args[i] = NULL;
  return args;
}
