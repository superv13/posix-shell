// builtins/echo.c — echo builtin implementation

#include "echo.h"
#include "../include/wrappers.h"
#include "../utils/string.h"

/*
===============================================================================
builtin_echo

POSIX echo builtin.

Syscall profile:
    write(1, arg, len)   — one per argument (or combined via separator)
    write(1, "\n", 1)    — one trailing newline

vs. external /usr/bin/echo:
    fork()               — 1
    execve(...)          — 1
    open(path) ENOENT    — N (PATH probing before finding /usr/bin/echo)
    mmap() × 8           — dynamic linker loading libc
    ... ≈ 60+ extra syscalls

Phase 5 note:
    echo is the most-frequently called shell command in scripts.
    Making it a builtin eliminates the fork+exec overhead and the
    PATH-probe ENOENT errors from the strace comparison table.
===============================================================================
*/

void builtin_echo(Command *cmd)
{
    /*
     * Write each argument, separating them with a single space.
     *
     * argv[0] is "echo" itself — start from argv[1].
     * argv[argc] is NULL (guaranteed by the parser).
     */
    for (int i = 1; i < cmd->argc; i++)
    {
        if (i > 1)
        {
            /* POSIX: arguments separated by a single space */
            sys_write(1, " ", 1);
        }
        sys_write(1, cmd->argv[i], my_strlen(cmd->argv[i]));
    }

    /* POSIX: output terminated by a newline */
    sys_write(1, "\n", 1);
}
