// builtins/builtins.c — builtin command dispatcher

#include "builtins.h"
#include "../utils/string.h"
#include "echo.h"
#include "exit.h"
#include "pwd.h"
#include "cd.h"
#include "jobs_builtin.h"
#include "misc_builtins.h"

/*
===============================================================================
execute_builtin

Dispatches to the correct builtin handler based on argv[0].

Returns:
    1 — command was a builtin and was executed (stop processing)
    0 — not a builtin (caller should fork+exec)

Phase 4 additions:
    jobs, fg, bg — job control builtins
===============================================================================
*/

int execute_builtin(Command *cmd)
{
    if (cmd == 0)    return 0;
    if (cmd->argc == 0) return 0;
    if (!cmd->is_builtin) return 0;

    const char *name = cmd->argv[0];

    /* ── Phase 1/3 builtins ─────────────────────────────────────── */

    if (my_strcmp(name, "exit") == 0)
    {
        builtin_exit(cmd);
        return 1;
    }

    if (my_strcmp(name, "echo") == 0)
    {
        /*
         * echo: zero fork/exec — replaces open(PATH...) = ENOENT chain.
         * Eliminates ~9 errors and ~60 syscalls per echo invocation.
         */
        builtin_echo(cmd);
        return 1;
    }

    if (my_strcmp(name, "pwd") == 0)
    {
        builtin_pwd();
        return 1;
    }

    if (my_strcmp(name, "cd") == 0)
    {
        if (cmd->argc > 1)
            builtin_cd(cmd->argv[1]);
        else
            builtin_cd(0);   /* cd with no args: go to / or ignore */
        return 1;
    }

    /* ── Phase 4 builtins ───────────────────────────────────────── */

    if (my_strcmp(name, "jobs") == 0)
        return builtin_jobs(cmd);

    if (my_strcmp(name, "fg") == 0)
        return builtin_fg(cmd);

    if (my_strcmp(name, "bg") == 0)
        return builtin_bg(cmd);

    /* ── POSIX mandatory builtins (compliance additions) ────────────── */

    if (my_strcmp(name, "wait") == 0)
        return builtin_wait(cmd);

    if (my_strcmp(name, "eval") == 0)
        return builtin_eval(cmd);

    if (my_strcmp(name, ":")    == 0)
        return builtin_colon();

    if (my_strcmp(name, "true") == 0)
        return builtin_true();

    if (my_strcmp(name, "false") == 0)
        return builtin_false();

    if (my_strcmp(name, "export") == 0)
        return builtin_export(cmd);

    if (my_strcmp(name, "unset") == 0)
        return builtin_unset(cmd);

    if (my_strcmp(name, "read") == 0)
        return builtin_read(cmd);

    return 0;
}
