#pragma once

#include "vtsh_common.h"

int vtsh_run_command(Command* cmd, CommandList* cmd_list);

void vtsh_execute_command(CommandList* cmd_list);
