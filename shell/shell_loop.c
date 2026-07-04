// shell/shell_loop.c — Phase 4 main shell loop

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

/*
===============================================================================
shell_main

Startup sequence (Phase 4):

    1. Parse --trace flag from argv
    2. setsid()         — create new session (required for PTY job control)
    3. TIOCSCTTY        — claim PTY slave (fd 0) as controlling terminal
    4. setpgid(0, 0)    — put shell in its own process group
    5. g_shell_pgid = getpid()
    6. setup_shell_signals()
    7. init_job_table()
    8. Loop:
         a. reap_background_jobs() always (catches done/stopped jobs
            even when SIGCHLD was missed or SA_RESTART ate the flag)
         b. print prompt
         c. read input
         d. tokenize / parse
         e. dispatch builtins or execute_pipeline

Why setsid + TIOCSCTTY are needed:
    When posixsh is launched as a subprocess (e.g. from Python's pty
    module in tests), it inherits the parent's session.  tcsetpgrp()
    requires the calling process to be in the same session as the
    terminal's controlling process — if posixsh is in Python's session
    and the PTY slave is NOT Python's controlling terminal, tcsetpgrp()
    fails silently with EPERM, which means Ctrl+Z and Ctrl+C signals
    never reach the foreground job's process group.

    setsid() creates a fresh session.  ioctl(0, TIOCSCTTY, 0) then
    makes the PTY slave (fd 0) the controlling terminal of that session.
    After these two calls tcsetpgrp() works correctly.
===============================================================================
*/

void shell_main(int argc, char **argv)
{
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
     * terminal.  Required so that the next call (TIOCSCTTY) can claim
     * the PTY slave as the controlling terminal of OUR session, not the
     * parent's session.
     *
     * Fails (returns -1) if the process is already a process group
     * leader — this is harmless; we proceed regardless.
     */
    sys_setsid();

    /* ── Step 3: claim the PTY slave as controlling terminal ─────────── */
    /*
     * SYSCALL: ioctl(0, TIOCSCTTY, 0)
     *
     * fd 0 (stdin) is the PTY slave.  After setsid() our session has no
     * controlling terminal.  This ioctl attaches fd 0 as the controlling
     * terminal.  Once done, tcsetpgrp(0, pgid) works correctly.
     *
     * If the shell is run from a real terminal (not a PTY subprocess),
     * this either succeeds (no-op) or fails harmlessly.
     */
    sys_tiocsctty(0);

    /* ── Step 4: put shell in its own process group ──────────────────── */
    /*
     * SYSCALL: setpgid(0, 0)
     *
     * Creates a new process group with PGID = shell PID.
     * This is the group tcsetpgrp restores to after each foreground job.
     *
     * Must be called AFTER setsid() — setsid() already resets the
     * process group, but the explicit setpgid(0,0) is safe and makes
     * the intent clear.
     */
    sys_setpgid(0, 0);

    /* ── Step 5: record shell PGID ───────────────────────────────────── */
    /*
     * SYSCALL: getpid()
     *
     * After setpgid(0,0), PGID == PID, so getpid() gives us both.
     */
    g_shell_pgid = sys_getpid();

    /* ── Step 6: install signal handlers ─────────────────────────────── */
    /*
     * After this call:
     *   SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU → SIG_IGN
     *   SIGCHLD → sigchld_handler (sets g_sigchld_flag, SA_RESTART)
     */
    setup_shell_signals();

    /* ── Step 7: initialise job table ────────────────────────────────── */
    init_job_table();

    char buffer[MAX_INPUT];

    while (1)
    {
        /* ── Reap background jobs ────────────────────────────────────── */
        /*
         * Always call reap_background_jobs(), not only when g_sigchld_flag
         * is set.  This handles two edge cases:
         *
         *   a) SA_RESTART caused the SIGCHLD to restart wait4() inside
         *      execute_pipeline before the flag was checked here, so the
         *      flag was never set even though a child exited.
         *
         *   b) A background job finished between two prompt iterations
         *      without firing a second SIGCHLD.
         *
         * reap_background_jobs() uses WNOHANG so it never blocks.
         * Clearing g_sigchld_flag first avoids a race where it is set
         * during the call — we will pick up the remaining children on the
         * next iteration.
         */
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
        Token tokens[MAX_TOKENS];
        int   token_count = 0;

        tokenize(buffer, tokens, &token_count);

        /* ── Parse ───────────────────────────────────────────────────── */
        Pipeline pipeline;

        if (parse(tokens, token_count, &pipeline) == -1)
        {
            char err[] = "posixsh: syntax error\n";
            sys_write(2, err, my_strlen(err));
            continue;
        }

        if (pipeline.count == 0) continue;  /* blank line */

        /* ── Builtin dispatch ────────────────────────────────────────── */
        if (pipeline.count == 1 && pipeline.commands[0].is_builtin)
        {
            execute_builtin(&pipeline.commands[0]);
            continue;
        }

        /* ── Execute pipeline ────────────────────────────────────────── */
        execute_pipeline(&pipeline);
    }

    sys_exit(0);
}
