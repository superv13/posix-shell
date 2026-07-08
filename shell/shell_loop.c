// shell/shell_loop.c — Phase 5 main shell loop

#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../parser/tokenizer.h"
#include "../parser/parser.h"
#include "../builtins/builtins.h"
#include "../executor/executor.h"
#include "../signals/signals.h"
#include "../jobs/jobs.h"
#include "../trace/trace.h"
#include "../env/env.h"

/*
===============================================================================
shell_main

Phase 5 additions over Phase 4:

    - Accepts a third parameter, envp, from _start.
    - Stores it in g_environ (env/env.c) so every execve() call can pass
      the real environment to child processes.
    - Stores the return value of execute_pipeline() into g_last_status
      (via executor.c, which does it directly), enabling $? expansion.
    - Updates g_last_status = 0 after builtin dispatch (builtins succeed).

Why g_environ is set here and not in _start:
    _start is a naked function with no C variables.  It passes envp to
    shell_main via the rdx register (System V AMD64 ABI third argument).
    shell_main is the first normal C function and the right place to store
    the pointer before any command runs.

Startup sequence (Phase 5):

    1. g_environ  = envp           ← Phase 5: capture environment
    2. Parse --trace flag from argv
    3. setsid()
    4. TIOCSCTTY
    5. setpgid(0, 0)
    6. g_shell_pgid = getpid()
    7. setup_shell_signals()
    8. init_job_table()
    9. Loop:
         a. reap_background_jobs()
         b. print prompt
         c. read input
         d. tokenize (Phase 5: $? and $$ expanded here)
         e. parse
         f. dispatch builtins or execute_pipeline
            (Phase 5: executor passes g_environ to execve, updates g_last_status)
===============================================================================
*/

void shell_main(int argc, char **argv, char **envp)
{
    /* ── Phase 5: capture environment ────────────────────────────────── */
    /*
     * Store the envp pointer from the kernel stack so every child process
     * can be exec'd with the full environment.
     *
     * g_environ is used in two places:
     *   1. executor.c: sys_execve(path, cmd->argv, g_environ)
     *   2. executor/path.c: env_get("PATH") to search the real PATH
     */
    g_environ = envp;

    /* ── Step 1: parse --trace flag ─────────────────────────────────── */
    for (int i = 1; i < argc; i++)
    {
        if (my_strcmp(argv[i], "--trace") == 0)
        {
            g_trace_mode = 1;
            break;
        }
    }

    /* ── Step 2: create a new session ───────────────────────────────── */
    /*
     * SYSCALL: setsid()
     *
     * Makes this process the leader of a new session with no controlling
     * terminal.  Required so TIOCSCTTY below can claim the PTY slave as
     * OUR session's controlling terminal.
     *
     * Fails harmlessly if we are already a process group leader.
     */
    sys_setsid();

    /* ── Step 3: claim the PTY slave as controlling terminal ─────────── */
    /*
     * SYSCALL: ioctl(0, TIOCSCTTY, 0)
     *
     * After setsid(), our session has no controlling terminal.
     * This ioctl makes fd 0 (PTY slave) the controlling terminal so that
     * tcsetpgrp() works correctly for job control.
     */
    sys_tiocsctty(0);

    /* ── Step 4: put shell in its own process group ──────────────────── */
    /*
     * SYSCALL: setpgid(0, 0)
     *
     * PGID = PID after this call.
     * This is the group we restore terminal control to after each job.
     */
    sys_setpgid(0, 0);

    /* ── Step 5: record shell PGID ───────────────────────────────────── */
    /*
     * SYSCALL: getpid()
     *
     * After setpgid(0,0), PGID == PID.
     * g_shell_pgid is also used by the $$ tokenizer expansion.
     */
    g_shell_pgid = sys_getpid();

    /* ── Step 6: install signal handlers ─────────────────────────────── */
    setup_shell_signals();

    /* ── Step 7: initialise job table ────────────────────────────────── */
    init_job_table();

    char buffer[MAX_INPUT];

    while (1)
    {
        /* ── Reap background jobs ────────────────────────────────────── */
        g_sigchld_flag = 0;
        reap_background_jobs();

        /* ── Prompt ──────────────────────────────────────────────────── */
        char prompt[] = "posixsh> ";
        sys_write(1, prompt, my_strlen(prompt));

        /* ── Read input ──────────────────────────────────────────────── */
        long bytes = sys_read(0, buffer, (long)(sizeof(buffer) - 1));

        if (bytes <= 0) break;      /* EOF (Ctrl+D) or read error */

        buffer[bytes] = '\0';

        /* ── Tokenise ────────────────────────────────────────────────── */
        /*
         * Phase 5: tokenize() now expands $? and $$ in-place before the
         * parser sees them.  g_last_status and g_shell_pgid are read from
         * globals visible to tokenizer.c via env.h and signals.h.
         */
        Token tokens[MAX_TOKENS];
        int   token_count = 0;

        tokenize(buffer, tokens, &token_count);

        /* ── Parse ───────────────────────────────────────────────────── */
        Pipeline pipeline;

        if (parse(tokens, token_count, &pipeline) == -1)
        {
            char err[] = "posixsh: syntax error\n";
            sys_write(2, err, my_strlen(err));
            g_last_status = 2;      /* POSIX: syntax errors set $? = 2 */
            continue;
        }

        if (pipeline.count == 0) continue;  /* blank line */

        /* ── Builtin dispatch ────────────────────────────────────────── */
        /*
         * Single builtin in no-pipe context runs directly in the shell.
         * Phase 5: set g_last_status = 0 on successful builtin execution.
         * (Future: builtins could return their own status codes.)
         */
        if (pipeline.count == 1 && pipeline.commands[0].is_builtin)
        {
            execute_builtin(&pipeline.commands[0]);
            g_last_status = 0;
            continue;
        }

        /* ── Execute pipeline ────────────────────────────────────────── */
        /*
         * Phase 5: execute_pipeline() now:
         *   - passes g_environ to execve() so children inherit the env
         *   - reads PATH via env_get("PATH") for command resolution
         *   - updates g_last_status with the processed exit code
         */
        execute_pipeline(&pipeline);
        /* g_last_status was already updated inside execute_pipeline() */
    }

    sys_exit(0);
}
