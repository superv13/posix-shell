#include "cd.h"

#include "../include/wrappers.h"
#include "../utils/string.h"

/*
===============================================================================
BUILTIN: cd

Purpose:
    Changes the shell's current working directory.

Why this builtin exists:
    Unlike external commands, changing the working directory must occur
    within the shell process itself. If executed in a child process, the
    shell's working directory would remain unchanged.

Current implementation:
    - Accepts a single directory path.
    - Invokes the Linux chdir() system call through sys_chdir().
    - Reports simple errors to stderr.

Future improvements:
    - Support "cd" with no arguments (HOME).
    - Support "cd -".
    - Update PWD and OLDPWD environment variables.

===============================================================================
*/

void builtin_cd(
    const char *path
)
{
    /* No directory supplied */

    if (path == 0)
    {
        char msg[] = "cd: missing operand\n";

        sys_write(
            2,
            msg,
            my_strlen(msg)
        );

        return;
    }

    /* Change working directory */

    if (sys_chdir(path) < 0)
    {
        char msg[] =
            "cd: unable to change directory\n";

        sys_write(
            2,
            msg,
            my_strlen(msg)
        );
    }
}