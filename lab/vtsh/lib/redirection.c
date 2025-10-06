#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "strfunc.h"

#define DEL "<>"

int is_rediredtion_simbol(char symb) {
  return (symb == '>') || (symb == '<');
}

int is_valid_filename(const char* name, const char red_option) {
  const char* invalid = "\\:*?\"<>|\n\t\r";
  if (!name || *name == '\0')
    return -1;

  for (const char* p = name; *p; ++p) {
    if (strchr(invalid, *p)) {
      if ((strchr(">", *p)) && red_option == '<') {
        return 1;
      } else if ((strchr("<", *p)) && red_option == '>') {
        return 1;
      }
      return -1;
    }
  }

  return 0;
}

int set_files_io(
    char* filename, char red_option, char** file_in, char** file_out
) {
  int is_valid = is_valid_filename(filename, red_option);
  if (is_valid == -1) {
    printf("Syntax error\n");
    return -1;
  } else if (is_valid == 1) {
    char* fln1 = strtok(filename, DEL);
    char* fln2 = strtok(NULL, DEL);
    set_files_io(fln1, red_option, file_in, file_out);
    if (red_option == '>') {
      red_option = '<';
    } else {
      red_option = '>';
    }
    set_files_io(fln2, red_option, file_in, file_out);
    return 0;
  }
  switch (red_option) {
    case '>': {
      if (*file_out != NULL) {
        printf("Syntax error\n");
        return -1;
      }
      *file_out = filename;
      break;
    }
    case '<': {
      if (*file_in != NULL) {
        printf("Syntax error\n");
        return -1;
      }
      *file_in = filename;
      break;
    }
  }
  return 0;
}

char** redirection_parsing(
    char** args, size_t len, char** file_in, char** file_out
) {
  char red_option = 0;
  char** res_args = malloc(sizeof(char*) * (len + 1));
  size_t counter = 0;
  int res = 0;

  for (size_t i = 0; args[i] != NULL; i++) {
    if (is_rediredtion_simbol(args[i][0])) {
      if (red_option != 0) {
        printf("Syntax error\n");
        return NULL;
      }
      if (strlen(args[i]) == 1) {
        red_option = args[i][0];
      } else {
        red_option = args[i][0];
        res = set_files_io(args[i] + 1, red_option, file_in, file_out);
        red_option = 0;
      }

    } else {
      if (red_option != 0) {
        res = set_files_io(args[i], red_option, file_in, file_out);
        red_option = 0;
        continue;
      }
      res_args[counter] = args[i];
      counter++;
    }
  }
  if (red_option != 0) {
    printf("Syntax error\n");
    res = -1;
  }
  res_args[counter] = NULL;
  // printf("%c %s\n", red_option, filename);
  free(args);
  if (res != 0) {
    return NULL;
  }
  return res_args;
}

int redirection(char** file_in, char** file_out) {
  if (*file_in) {
    FILE* file = fopen(*file_in, "r");
    if (!file) {
      printf("I/O error\n");
      // printf("%s\n", strerror(errno));
      return -1;
    }
    int fd = fileno(file);

    dup2(fd, STDIN_FILENO);
    fclose(file);
  }
  if (*file_out) {
    FILE* file = fopen(*file_out, "w");
    if (!file) {
      printf("I/O error\n");
      return -1;
    }
    int fd = fileno(file);

    dup2(fd, STDOUT_FILENO);
    fclose(file);
  }
  return 0;
}