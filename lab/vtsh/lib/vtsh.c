#include "vtsh.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vtsh_common.h"
#include "vtsh_executor.h"
#include "vtsh_parser.h"
#include "vtsh_utils.h"

static int is_exit_command(const char* trimmed) {
  return strncmp(trimmed, "exit", 4) == 0 &&
         (trimmed[4] == '\0' || trimmed[4] == '\n');
}

static void process_command_line(const char* trimmed) {
  CommandList* cmd_list = vtsh_parse_line(trimmed);
  if (cmd_list == NULL) {
    (void)printf("Syntax error\n");
  } else if (cmd_list->count > 0) {
    vtsh_execute_command(cmd_list);
  }
  if (cmd_list != NULL) {
    vtsh_free_command_list(cmd_list);
  }
}

void vtsh_loop() {
  char line[MAX_LINE_LENGTH];
  size_t pos = 0;

  while (1) {
    char chr = '\0';
    ssize_t bytes_read = read(STDIN_FILENO, &chr, 1);

    if (bytes_read <= 0) {
      break;
    }

    if (chr == '\n' || pos >= MAX_LINE_LENGTH - 1) {
      line[pos] = '\0';
      pos = 0;

      char* trimmed = trim_whitespace(line);

      if (strlen(trimmed) == 0) {
        print_prompt();
        continue;
      }

      if (is_exit_command(trimmed)) {
        break;
      }

      process_command_line(trimmed);

      print_prompt();
    } else {
      line[pos++] = chr;
    }
  }
}
