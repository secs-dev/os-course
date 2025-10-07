#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "redirection.h"
#include "strfunc.h"

#define DEL "\n\t \v\f"

// struct exec_args {
//   char* file;
//   char** args
// };

// int exec_call(void* args) {
//   struct exec_args* res_args = (struct exec_args*)args;
//   int res = execvp(res_args->file, res_args->args);
//   if (res == -1) {
//     return -1;
//   }
//   return 0;
// }

// int clone_try(char** args){
//   const int STACK_SIZE = 1024 * 1024;
//   void* stack = malloc(STACK_SIZE);
//   if (!stack) {
//     perror("malloc");
//     exit(1);
//   }

//   struct exec_args res_args = {.file = args[0], .args = args};

//   pid_t pid = clone(exec_call, (char*)stack + STACK_SIZE, SIGCHLD, res_args);
//   if (pid == -1) {
//     perror("clone");
//     exit(1);
//   }
// }

int find_and_do_or(char** args) {
  for (size_t i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "||") == 0) {
      if (i == 0 || args[i + 1] == NULL) {
        return -1;
      }
      return args[i - 1] || args[i + 1];
    } else if (strstr(args[i], "||")) {
      char* arg1 = args[i];
      char* arg2 = NULL;
      arg2 = strstr(arg1, "||");
      if (strcmp(arg1, arg2) != 0 && (strlen(arg2) > 2)) {
        arg2 += strlen("||");
        return arg1 || arg2;
      }
      return -1;
    }
  }
  return -2;
}

int init() {
  return 0;
}

int run(char** buf, size_t* len) {
  time_t start_time;
  time(&start_time);
  char* file_in = 0;
  char* file_out = 0;
  char* del = "\n\t \v\f";

  // todo - shell_or

  char** args = parse_args(*buf, *len, DEL);

  int res = find_and_do_or(args);
  if (res == -1) {
    printf("Syntax error with ||\n");
    free(args);
    return 0;
  } else if (res == -2) {
  } else {
    printf("%d\n", res);
    free(args);
    return 0;
  }

  pid_t pid = fork();
  if (pid == 0) {
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

  time_t finish_time;
  time(&finish_time);
  time_t exec_time = finish_time - start_time;
  printf("Время выполнения команды %ld с\n", exec_time);
  return 0;
}