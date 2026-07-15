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
 * execute_string
 *
 * Shared execution engine used by both -c mode and the interactive loop.
 * Tokenises, parses, and runs a single null-terminated input line in-place.
 * Returns 0 on success, -1 on syntax error, 1 on "exit" builtin.
 */
static int execute_line(char *buf)
{
    Token tokens[MAX_TOKENS];
    int   token_count = 0;

    tokenize(buf, tokens, &token_count);

    Pipeline pipeline;
    if (parse(tokens, token_count, &pipeline) == -1)
    {
        char err[] = "posixsh: syntax error\n";
        sys_write(2, err, my_strlen(err));
        g_last_status = 2;
        return -1;
    }

    if (pipeline.count == 0) return 0;   /* blank line */

    if (pipeline.count == 1 && pipeline.commands[0].is_builtin)
    {
        Command *cmd = &pipeline.commands[0];
        /*
         * Fast path: only run the builtin directly when there are no
         * redirections.  If the builtin has input_file or output_file
         * (e.g. "echo hello > /tmp/out"), we must go through
         * execute_pipeline() so the executor sets up the dup2() calls
         * before running the builtin.
         *
         * Without this check, "echo hello > file" would call
         * execute_builtin() before any file is opened, so the file
         * would never be created.
         */
        int has_redir = cmd->input_file[0] != '\0' || cmd->output_file[0] != '\0';
        if (!has_redir)
        {
            execute_builtin(cmd);
            g_last_status = 0;
            return 0;
        }
    }

    execute_pipeline(&pipeline);
    return 0;
}

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

    /* ── Step 1: parse --trace and -c flags ─────────────────────────── */
    /*
     * -c STRING  (POSIX-required)
     *   Execute STRING as a single script and exit immediately.
     *   This is the flag used by bash/dash for non-interactive invocation,
     *   e.g.  posixsh -c "echo hello"
     *
     *   In -c mode we skip:
     *     - setsid / TIOCSCTTY / setpgid  (no terminal needed)
     *     - the interactive read() loop   (we run the string, then exit)
     *   This makes posixsh -c directly comparable to bash -c for both
     *   performance benchmarks (perf_measure.sh) and syscall counts
     *   (strace_compare.sh).
     */
    char *c_script = (char *)0;  /* non-NULL if -c was given */

    /*
     * Login shell detection.
     *
     * When the system invokes a login shell it sets argv[0] to a dash-prefixed
     * version of the binary name, e.g. "-posixsh" instead of "posixsh".
     * POSIX requires the shell to recognise this and behave as a login shell.
     *
     * For now: detect it and ignore it safely (do not crash).
     * A full login shell would read /etc/profile and ~/.profile here.
     * We skip those because we do not implement variable expansion for
     * filenames yet — and sourcing is a Phase 6 feature.
     *
     * The critical requirement is: do not crash. argv[0][0] == '-' is enough
     * to detect this case.
     */
    int is_login_shell = 0;
    if (argc > 0 && argv[0] != (char *)0 && argv[0][0] == '-')
        is_login_shell = 1;

    (void)is_login_shell;   /* suppress unused-variable warning for now */

    for (int i = 1; i < argc; i++)
    {
        if (my_strcmp(argv[i], "--trace") == 0)
        {
            g_trace_mode = 1;
        }
        else if (my_strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            c_script = argv[i + 1];
            i++;   /* consumed the next argument */
        }
    }

    /* ── -c mode: run script and exit ───────────────────────────────── */
    /*
     * When -c is given we do NOT set up a new session or claim a terminal.
     * We just need the job table and signal handlers for pipeline execution.
     *
     * We walk through the script line-by-line, using \n as the delimiter,
     * so a multi-command -c string works correctly:
     *   posixsh -c $'echo a\necho b'
     */
    if (c_script)
    {
        g_shell_pgid = sys_getpid();
        setup_shell_signals();
        init_job_table();

        /* Walk through the script one newline-delimited line at a time */
        char line_buf[MAX_INPUT];
        const char *src = c_script;

        while (*src)
        {
            /* Copy up to the next '\n' or end of string */
            int len = 0;
            while (*src && *src != '\n' && len < (int)(sizeof(line_buf) - 1))
                line_buf[len++] = *src++;
            if (*src == '\n') src++;  /* skip the newline */
            line_buf[len] = '\0';

            if (len > 0)
                execute_line(line_buf);
        }

        sys_exit(g_last_status);
    }

    /* ── Interactive mode (original behaviour) ───────────────────────── */

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
        /*
         * SA_RESTART on SIGCHLD means the kernel transparently restarts
         * this read() if a child exits while we're blocked.  The loop
         * will call reap_background_jobs() at the top of the NEXT
         * iteration, printing "Done" just before the next prompt —
         * which is the correct user-visible timing.
         */
        long bytes = sys_read(0, buffer, (long)(sizeof(buffer) - 1));

        if (bytes <= 0) break;      /* EOF (Ctrl+D) or read error */

        buffer[bytes] = '\0';

        execute_line(buffer);
    }

    sys_exit(0);
}
