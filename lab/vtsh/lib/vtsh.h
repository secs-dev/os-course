#pragma once

const char* vtsh_prompt();
int parse_command(char *input, char **args);
void set_vtsh_executable_path(char *path);
