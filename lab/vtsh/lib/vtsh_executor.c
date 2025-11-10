#include "vtsh_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

// NOLINTNEXTLINE
extern char** environ;

static int check_file_accessibility(Command* cmd) {
  if (cmd->input_file) {
    if (access(cmd->input_file, R_OK) != 0) {
      (void)fprintf(stdout, "I/O error\n");
      return -1;
    }
  }

  if (cmd->output_file) {
    int fd_test =
        open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
    if (fd_test == -1) {
      (void)fprintf(stdout, "I/O error\n");
      return -1;
    }
    close(fd_test);
  }

  return 0;
}

static void setup_redirections(Command* cmd) {
  if (cmd->input_file) {
    int input_fd = open(cmd->input_file, O_RDONLY);
    if (input_fd != -1) {
      dup2(input_fd, STDIN_FILENO);
      close(input_fd);
    }
  }

  if (cmd->output_file) {
    int output_fd =
        open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
    if (output_fd != -1) {
      dup2(output_fd, STDOUT_FILENO);
      close(output_fd);
    }
  }
}

static int run_command_vfork(Command* cmd) {
  (void)fflush(stdout);
  (void)fflush(stderr);

  // NOLINTNEXTLINE
  pid_t pid = vfork();

  if (pid == -1) {
    perror("vfork failed");
    return 1;
  }

  if (pid == 0) {
    setup_redirections(cmd);
    execvp(cmd->argv[0], cmd->argv);

    if (errno == ENOENT) {
      (void)fprintf(stdout, "Command not found\n");
    } else {
      perror("execvp failed");
    }
    _exit(1);
  }

  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status)) { 
    return WEXITSTATUS(status); // все норм 
  }
  return 1;
}

int vtsh_run_command(Command* cmd, CommandList* cmd_list) {
  (void)cmd_list;

  if (cmd->argv[0] == NULL) {
    return 0;
  }
  if (check_file_accessibility(cmd) != 0) {
    return 1;
  }

  return run_command_vfork(cmd);
}

static void execute_or_commands(CommandList* cmd_list) {
  for (size_t i = 0; i < cmd_list->count; i++) {
    int result = vtsh_run_command(&cmd_list->commands[i], cmd_list);
    if (result == 0) {
      break;
    }
  }
}

void vtsh_execute_command(CommandList* cmd_list) {
  if (!cmd_list || cmd_list->count == 0) {
    return;
  }

  if (cmd_list->operator_type == 2) {
    execute_or_commands(cmd_list);
  } else {
    if (cmd_list->count > 0) {
      vtsh_run_command(&cmd_list->commands[0], cmd_list);
    }
  }
}
