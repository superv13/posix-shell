#ifndef BUILTINS_H
#define BUILTINS_H

#include "../parser/parser.h"

/*
===============================================================================
execute_builtin()

Purpose:
    Dispatches builtin commands.

Returns:
    1 : Command was a builtin and executed.
    0 : Command is not a builtin.

===============================================================================
*/

int execute_builtin(
    Command *cmd
);

#endif