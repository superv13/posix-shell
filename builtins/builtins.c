#include "builtins.h"

#include "../utils/string.h"

#include "exit.h"
#include "pwd.h"
#include "cd.h"

/*
===============================================================================
execute_builtin()

Purpose:
    Executes shell builtin commands.

Workflow:

    Command

       │

       ▼

   builtin ?

       │

   yes │ no

       ▼

 execute

===============================================================================
*/

int execute_builtin(
    Command *cmd
)
{
    if(cmd == 0)
    {
        return 0;
    }

    if(cmd->argc == 0)
    {
        return 0;
    }

    if(cmd->is_builtin == 0)
    {
        return 0;
    }

    if(
        my_strcmp(
            cmd->argv[0],
            "exit"
        ) == 0
    )
    {
        builtin_exit();

        return 1;
    }

    if(
        my_strcmp(
            cmd->argv[0],
            "pwd"
        ) == 0
    )
    {
        builtin_pwd();

        return 1;
    }

    if(
        my_strcmp(
            cmd->argv[0],
            "cd"
        ) == 0
    )
    {
        if(cmd->argc > 1)
        {
            builtin_cd(
                cmd->argv[1]
            );
        }

        return 1;
    }

    return 0;
}