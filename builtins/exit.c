#include "exit.h"
#include "../include/wrappers.h"

/*
===============================================================================
BUILTIN: exit

Purpose:
    Terminates the shell process.

Educational note:
    Unlike external commands, this executes directly inside the shell.
===============================================================================
*/

void builtin_exit(void)
{
    sys_exit(0);
}