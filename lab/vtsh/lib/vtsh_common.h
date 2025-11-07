#pragma once

#include <stddef.h>

enum {
  MAX_LINE_LENGTH = 4096,
  MAX_ARGS = 256,
  FILE_MODE = 0644
};

typedef struct {
  char** argv;
  char* input_file;
  char* output_file;
  int background;
} Command;

typedef struct {
  Command* commands;
  size_t count;
  int operator_type;
} CommandList;
