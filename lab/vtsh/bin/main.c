#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "shell.h"
#include "vtsh.h"

int main() {
  char* buf = NULL;
  size_t cup = 2000;
  setvbuf(stdin, NULL, _IONBF, 0);
  while (true) {
    printf("%s", vtsh_prompt());
    size_t len = getline(&buf, &cup, stdin);

    if (len == -1) {
      break;
    } else if (strcmp(buf, "./shell\n") == 0) {
      init();
      continue;
    }

    run(&buf, &len);
  }
  return 0;
}