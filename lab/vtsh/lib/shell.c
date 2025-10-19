#include <errno.h>
#include <sched.h>
#include <stdbool.h>
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

char* contains_or(char* buf) {
  char* contains = strstr(buf, "||");
  return contains;
}

int init() {
  return 0;
}

int run(char** buf, size_t* len) {
  char* file_in = 0;
  char* file_out = 0;
  char* del = "\n\t \v\f";
  char* command1 = *buf;
  size_t comm1_len = *len;
  char* command2 = NULL;
  size_t comm2_len = 0;
  int count = 0;

  char* shell_or = *buf;
  while (shell_or != NULL) {
    time_t start_time;
    time(&start_time);
    count++;

    if (count > 1) {
      command2 = command2 + 2;
      comm2_len -= 2;
      command1 = command2;
      comm1_len = comm2_len;
    }

    // todo - shell_or
    shell_or = contains_or(command1);

    if (shell_or != NULL) {
      command2 = shell_or;
      comm2_len = comm1_len - (command2 - command1);
      comm1_len = command2 - command1;
      char new_comm1[comm1_len];
      strncpy(new_comm1, command1, comm1_len);
      command1 = new_comm1;
      *(command1 + comm1_len) = '\0';
    }

    char** args = parse_args(command1, comm1_len, DEL);

    pid_t pid = fork();
    if (pid == 0) {
      int res = execvp(args[0], args);
      if (res == -1) {
        printf("Command not found\n");
        // printf("%s\n", strerror(errno)); ?????
      } else {
        exit(0);
      }
      exit(1);
    } else if (pid > 0) {
      int status;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
          shell_or = NULL;
        }
      }
    }
    free(args);

    time_t finish_time;
    time(&finish_time);
    time_t exec_time = finish_time - start_time;
    fprintf(stderr, "Время выполнения команды %ld с\n", exec_time);
  }
  return 0;
}