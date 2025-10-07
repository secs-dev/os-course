#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "redirection.h"
#include "strfunc.h"

#define DEL "\n\t \v\f"

int init() {
  return 0;
}

int run(char** buf, size_t* len) {
  char* file_in = 0;
  char* file_out = 0;
  char* del = "\n\t \v\f";

  char** args = parse_args(*buf, *len, DEL);

  // char** res_args = redirection_parsing(args, *len, &file_in, &file_out);

  // if (res_args == NULL) {
  //   return 0;
  // }

  pid_t pid = fork();
  if (pid == 0) {
    // int can_exec = redirection(&file_in, &file_out);
    // if (can_exec == -1) {
    //   exit(1);
    // }
    int res = execvp(args[0], args);
    if (res == -1) {
      printf("Command not found\n");
    }
    perror("execvp");
    exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  }
  free(args);
  return 0;
}