#include "vtsh_parser.h"

#include <stdlib.h>
#include <string.h>

static void cleanup_parse_error(
    char* line_copy, Command* cmd, CommandList* cmd_list
) {
  free(line_copy);
  free((void*)cmd->argv);
  free(cmd->input_file);
  free(cmd->output_file);
  free(cmd);
  free(cmd_list);
}

static int detect_operator_type(const char* line) {
  if (strstr(line, " || ")) {
    return 2;
  }
  return 0;
}

// < file
static int handle_input_redirect(char** token, Command* cmd) {
  if (cmd->input_file != NULL) {
    return -1; 
  }

  // NOLINTNEXTLINE
  *token = strtok(NULL, " ");
  if (!*token || strcmp(*token, ">") == 0 || (*token)[0] == '>') {
    return -1;  
  }

  cmd->input_file = strdup(*token);
  return 0;
}

// <file
static int handle_input_redirect_merged(const char* token, Command* cmd) {
  if (cmd->input_file != NULL) {
    return -1;  
  }
  cmd->input_file = strdup(token + 1);
  return 0;
}

// > file
static int handle_output_redirect(char** token, Command* cmd) {
  if (cmd->output_file != NULL) {
    return -1;  
  }

  // NOLINTNEXTLINE
  *token = strtok(NULL, " ");
  if (!*token) {
    return -1;  
  }

  cmd->output_file = strdup(*token);
  return 0;
}

// >file
static int handle_output_redirect_merged(const char* token, Command* cmd) {
  if (cmd->output_file != NULL) {
    return -1;  
  }

  if (token[1] == '>') { // проверка на >>
    return -1;  
  }

  cmd->output_file = strdup(token + 1);
  return 0;
}

static int add_command_to_list(
    Command* cmd, CommandList* cmd_list, size_t argc
) {
  if (argc == 0) {
    return 0;
  }

  cmd->argv[argc] = NULL;
  Command* new_commands = (Command*)realloc(
      cmd_list->commands, sizeof(Command) * (cmd_list->count + 1)
  );
  if (new_commands == NULL) {
    return -1;
  }

  cmd_list->commands = new_commands;
  cmd_list->commands[cmd_list->count] = *cmd;
  cmd_list->count++;
  return 0;
}

// NOLINTNEXTLINE
CommandList* vtsh_parse_line(const char* line) {
  CommandList* cmd_list = calloc(1, sizeof(CommandList));
  cmd_list->commands = NULL;
  cmd_list->count = 0;

  if (!line || strlen(line) == 0) {
    return cmd_list;
  }

  char* line_copy = strdup(line);
  char* current = line_copy;

  cmd_list->operator_type = detect_operator_type(line);

  Command* cmd = calloc(1, sizeof(Command));
  cmd->argv = (char**)calloc(MAX_ARGS, sizeof(char*));
  cmd->input_file = NULL;
  cmd->output_file = NULL;
  cmd->background = 0;

  size_t argc = 0;
  // NOLINTNEXTLINE
  char* token = strtok(current, " ");

  while (token) {
    if (strcmp(token, "<") == 0) {
      if (handle_input_redirect(&token, cmd) != 0) {
        cleanup_parse_error(line_copy, cmd, cmd_list);
        return NULL;
      }
    } else if (token[0] == '<' && strlen(token) > 1) {
      if (handle_input_redirect_merged(token, cmd) != 0) {
        cleanup_parse_error(line_copy, cmd, cmd_list);
        return NULL;
      }
    } else if (strcmp(token, ">") == 0) {
      if (handle_output_redirect(&token, cmd) != 0) {
        cleanup_parse_error(line_copy, cmd, cmd_list);
        return NULL;
      }
    } else if (token[0] == '>' && strlen(token) > 1) {
      if (handle_output_redirect_merged(token, cmd) != 0) {
        cleanup_parse_error(line_copy, cmd, cmd_list);
        return NULL;
      }
    } else if (strcmp(token, "||") == 0) {
      if (add_command_to_list(cmd, cmd_list, argc) != 0) {
        cleanup_parse_error(line_copy, cmd, cmd_list);
        return NULL;
      }
      cmd = calloc(1, sizeof(Command));
      cmd->argv = (char**)calloc(MAX_ARGS, sizeof(char*));
      cmd->input_file = NULL;
      cmd->output_file = NULL;
      cmd->background = 0;
      argc = 0;
    } else {
      cmd->argv[argc++] = strdup(token);
    }
    // NOLINTNEXTLINE
    token = strtok(NULL, " ");
  }

  if (argc > 0) {
    if (add_command_to_list(cmd, cmd_list, argc) != 0) {
      cleanup_parse_error(line_copy, cmd, cmd_list);
      return NULL;
    }
  } else {
    if (cmd->input_file != NULL || cmd->output_file != NULL) {
      cleanup_parse_error(line_copy, cmd, cmd_list);
      return NULL;
    }
    free((void*)cmd->argv);
    free(cmd);
  }

  free(line_copy);
  return cmd_list;
}

void vtsh_free_command_list(CommandList* cmd_list) {
  if (!cmd_list) {
    return;
  }

  for (size_t i = 0; i < cmd_list->count; i++) {
    Command* cmd = &cmd_list->commands[i];
    for (size_t j = 0; cmd->argv && cmd->argv[j]; j++) {
      free(cmd->argv[j]);
    }
    free((void*)cmd->argv);
    free(cmd->input_file);
    free(cmd->output_file);
  }
  free(cmd_list->commands);
  free(cmd_list);
}
