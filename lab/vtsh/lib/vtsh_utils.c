#include "vtsh_utils.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char* vtsh_prompt(void) {
  return "vtsh> ";
}

char* trim_whitespace(char* str) {
  char* end = str + strlen(str) - 1;
  while (end > str && (*end == ' ' || *end == '\t' || *end == '\n')) {
    *end = '\0';
    end--;
  }
  while (*str == ' ' || *str == '\t' || *str == '\n') {
    str++;
  }
  return str;
}

void print_prompt(void) {
  (void)printf("%s", vtsh_prompt());
  (void)fflush(stdout);
}
