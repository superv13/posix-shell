//=============================================================================
// executor.c
//
// Purpose:
//   Implements the execution engine of the educational POSIX shell.
//
// Why this file exists:
//   The parser only understands command syntax. The executor is responsible
//   for transforming a parsed Pipeline into one or more running processes.
//
// Current implementation:
//   - Executes a single foreground command.
//   - Uses fork(), execve() and wait4().
//   - Does not yet support:
//         * PATH lookup
//         * Pipes
//         * Redirection
//         * Background execution
//
//=============================================================================

#include "executor.h"

#include "../include/wrappers.h"
#include "../utils/string.h"
#include "../executor/path.h"

/*
===============================================================================
FUNCTION: execute_command

Purpose:
    Executes a single external command.

Current implementation:
    Expects argv[0] to contain the full executable path.

Future:
    PATH lookup will be added so commands like "ls" automatically resolve to
    "/bin/ls".

===============================================================================
*/

static int execute_command(
    Command *cmd
)
{
    long pid =
        sys_fork();

    if (pid == 0)
    {
        /* Child */

        char *path =
            find_executable(
                cmd->argv[0]
        );

        if (path == 0)
        {
            char msg[] =
                "posixsh: command not found\n";

            sys_write(
                2,
                msg,
                my_strlen(msg)
            );

            sys_exit(1);
        }

        sys_execve(
            path,
            cmd->argv,
            0
        );

        /*
         * execve() returns only if it fails.
         */

        char msg[] =
            "posixsh: execve failed\n";

        sys_write(
            2,
            msg,
            my_strlen(msg)
        );

        sys_exit(1);
    }

    if (pid > 0)
    {
        /* Parent */

        int status;

        sys_wait4(
            pid,
            &status,
            0
        );

        return 0;
    }

    /* fork failed */

    char msg[] =
        "posixsh: fork failed\n";

    sys_write(
        2,
        msg,
        my_strlen(msg)
    );

    return -1;
}


/*
===============================================================================
FUNCTION: execute_pipeline

Purpose:
    Executes a parsed command pipeline.

Current implementation:
    Supports only one foreground command.

===============================================================================
*/

int execute_pipeline(
    Pipeline *pipeline
)
{
    if (pipeline == 0)
    {
        return -1;
    }

    if (pipeline->count == 0)
    {
        return 0;
    }

    return execute_command(
        &pipeline->commands[0]
    );
}