#include "exit.h"
#include "../include/wrappers.h"

/*
===============================================================================
BUILTIN: exit [n]

Purpose:
    Terminates the shell process with an optional exit code.

    POSIX XBD 2.14: "exit [n]"
      If n is given, the shell shall exit with the numeric value of n.
      If n is omitted, the exit status shall be the exit status of the
      last command executed.

Why we parse manually (no atoi/strtol):
    Those functions live in libc, which we deliberately avoid.
    A simple loop over ASCII digits is sufficient — exit codes are
    always small non-negative integers (0-255).
===============================================================================
*/

void builtin_exit(Command *cmd)
{
    int code = 0;

    if (cmd != (Command *)0 && cmd->argc >= 2 && cmd->argv[1] != (char *)0)
    {
        /*
         * Parse the optional numeric argument.
         * Only decimal digits are accepted; non-digit characters stop
         * parsing.  POSIX allows only integer values here.
         */
        const char *s = cmd->argv[1];
        while (*s >= '0' && *s <= '9')
        {
            code = code * 10 + (*s - '0');
            s++;
        }
        /* Mask to 8-bit range as waitpid only delivers the low byte. */
        code &= 0xFF;
    }

    sys_exit(code);
}