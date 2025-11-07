#pragma once

#include "vtsh_common.h"

CommandList* vtsh_parse_line(const char* line);

void vtsh_free_command_list(CommandList* cmd_list);
