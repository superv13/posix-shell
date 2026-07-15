// shell/shell_loop.c — Phase 5 main shell loop
//
// Step 3 — Script file execution:
//   When argv[1] is a non-option argument (does not start with '-'),
//   the shell treats it as a script filename.  It opens the file with
//   sys_open(), reads the entire content into a static 64 KB buffer
//   with sys_read(), then walks the buffer line-by-line calling
//   execute_line() — exactly like -c mode.  No heap allocation.
//
//   POSIX requirement (XBD Utility Conventions):
//     "If the first operand is a file, the shell shall read commands
//     from that file."
//
//   Error behaviour:
//     If the file cannot be opened, print to stderr:
//       posixsh: <name>: cannot open script file
//     and exit with status 1.

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
 * run_one_pipeline
 *
 * Shared helper: given an already-parsed Pipeline, dispatch it the same
 * way the old execute_line did — builtin fast-path or execute_pipeline().
 * Called by execute_line for every segment in an AND/OR list.
 */
static void run_one_pipeline(Pipeline *pipeline)
{
    if (pipeline->count == 0) return;

    if (pipeline->count == 1 && pipeline->commands[0].is_builtin)
    {
        Command *cmd = &pipeline->commands[0];
        /*
         * Fast path for builtins with no redirections.
         * Redirected builtins (e.g. "echo hi > file") must go through
         * execute_pipeline() so dup2() is set up before the builtin runs.
         */
        int has_redir = cmd->input_file[0] != '\0' || cmd->output_file[0] != '\0';
        if (!has_redir)
        {
            execute_builtin(cmd);
            g_last_status = 0;
            return;
        }
    }

    execute_pipeline(pipeline);
}

/*
 * execute_line  (Step 4 — AND/OR list evaluation)
 *
 * Tokenises the input line once, then walks the flat token array
 * segment by segment, splitting on TOKEN_AND ('&&'), TOKEN_OR ('||'),
 * and TOKEN_SEMICOLON (';').  Each segment is a single pipeline.
 *
 * Short-circuit rules (POSIX XBD 2.9.3):
 *   &&  — execute the next pipeline only if the previous one exited 0.
 *   ||  — execute the next pipeline only if the previous one exited non-zero.
 *   ;   — always execute the next pipeline (unconditional sequencing).
 *
 * How segmentation works without changing parse()'s signature:
 *   We scan forward to find the next AND/OR/SEMICOLON/EOF boundary.
 *   We copy that slice to a small stack buffer and append TOKEN_EOF,
 *   giving parse() a well-formed self-contained token stream.
 *   argv[] pointers inside the resulting Pipeline point into the slice
 *   buffer, which outlives every call to run_one_pipeline().
 *
 * Example — "make && ./posixsh || echo failed":
 *   Segment 0: ["make"] ,           join_op=EOF  → always run
 *   Segment 1: ["./posixsh"],        join_op=&&   → run only if make exited 0
 *   Segment 2: ["echo", "failed"],   join_op=||   → run only if ./posixsh failed
 */
static int execute_line(char *buf)
{
    Token tokens[MAX_TOKENS];
    int   token_count = 0;

    tokenize(buf, tokens, &token_count);

    /*
     * join_op: the operator preceding the CURRENT segment.
     *   TOKEN_EOF       = first segment, always execute (no preceding op).
     *   TOKEN_AND       = execute only if g_last_status == 0.
     *   TOKEN_OR        = execute only if g_last_status != 0.
     *   TOKEN_SEMICOLON = always execute.
     */
    TokenType join_op = TOKEN_EOF;
    int pos = 0;

    while (pos < token_count)
    {
        /* Stop at a bare EOF or NEWLINE with nothing left to do */
        if (tokens[pos].type == TOKEN_EOF ||
            tokens[pos].type == TOKEN_NEWLINE)
            break;

        /* ── Find end of this pipeline segment ──────────────────────── */
        /*
         * Scan forward until we hit a list operator or end-of-input.
         * seg_end lands ON the separator token (or past the last token).
         */
        int seg_start = pos;
        int seg_end   = pos;
        while (seg_end < token_count)
        {
            TokenType t = tokens[seg_end].type;
            if (t == TOKEN_AND || t == TOKEN_OR  ||
                t == TOKEN_SEMICOLON              ||
                t == TOKEN_NEWLINE || t == TOKEN_EOF)
                break;
            seg_end++;
        }

        int seg_len = seg_end - seg_start;   /* tokens in this segment */

        /* ── Short-circuit evaluation ────────────────────────────────── */
        int skip = 0;
        if (join_op == TOKEN_AND && g_last_status != 0) skip = 1;
        if (join_op == TOKEN_OR  && g_last_status == 0) skip = 1;

        if (!skip && seg_len > 0)
        {
            /*
             * Build a copy of the segment with a trailing TOKEN_EOF.
             *
             * Why copy and not pass &tokens[seg_start] directly:
             *   parse() walks until it sees TOKEN_EOF/NEWLINE/SEMICOLON.
             *   Without an EOF sentinel, it would read into the next
             *   segment's tokens (e.g. into the '&&' token), which would
             *   either be silently skipped or misinterpreted.
             *   A local copy with appended TOKEN_EOF gives parse() a
             *   clean, isolated view of this one pipeline.
             *
             * argv[] pointers inside Pipeline point into seg_tokens[].
             * seg_tokens lives on the stack for the rest of execute_line,
             * so they are valid through run_one_pipeline().
             */
            Token seg_tokens[MAX_TOKENS];
            int   k;
            for (k = 0; k < seg_len && k < MAX_TOKENS - 1; k++)
                seg_tokens[k] = tokens[seg_start + k];
            seg_tokens[k].type     = TOKEN_EOF;
            seg_tokens[k].value[0] = '\0';
            int seg_count = k + 1;

            Pipeline pipeline;
            if (parse(seg_tokens, seg_count, &pipeline) == -1)
            {
                char err[] = "posixsh: syntax error\n";
                sys_write(2, err, my_strlen(err));
                g_last_status = 2;
                return -1;
            }

            run_one_pipeline(&pipeline);
        }

        /* ── Advance past the separator ──────────────────────────────── */
        if (seg_end < token_count &&
            (tokens[seg_end].type == TOKEN_AND  ||
             tokens[seg_end].type == TOKEN_OR   ||
             tokens[seg_end].type == TOKEN_SEMICOLON))
        {
            join_op = tokens[seg_end].type;
            pos     = seg_end + 1;
        }
        else
        {
            break;   /* TOKEN_EOF, TOKEN_NEWLINE, or past end of array */
        }
    }

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

Startup sequence (Phase 5 + Step 3):

    1. g_environ  = envp           ← Phase 5: capture environment
    2. Parse --trace / -c / script flags from argv
    3a. If -c STRING  → run string line-by-line, exit
    3b. If script file → open file, read into buffer, run line-by-line, exit
    4. (Interactive only)
       setsid() → TIOCSCTTY → setpgid(0,0) → getpid() → setup_shell_signals()
       → init_job_table() → read-eval loop
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
    char *c_script      = (char *)0;  /* non-NULL if -c was given      */
    char *script_file   = (char *)0;  /* non-NULL if a filename given   */

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
        else if (argv[i][0] != '-' && script_file == (char *)0
                 && c_script   == (char *)0)
        {
            /*
             * Step 3 — Script filename detection.
             *
             * The first non-option argument that is not consumed by -c is
             * treated as a script file to execute.  This matches POSIX
             * shell invocation: posixsh myscript.sh
             *
             * We only record the first such argument; subsequent positional
             * parameters ($1, $2, ...) are future work.
             */
            script_file = argv[i];
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

    /* ── Step 3: Script file mode ────────────────────────────────────── */
    /*
     * If a script filename was detected during argument parsing, read the
     * entire file into a static buffer and execute it line-by-line.
     *
     * Why a static buffer (not the stack):
     *   64 KB on the stack could trigger a stack overflow on some
     *   environments.  A static buffer is safer and makes the limit
     *   explicit at compile time.
     *
     * Why read the whole file at once:
     *   The line-by-line -c engine already handles a null-terminated
     *   character buffer.  Reading everything then walking it avoids
     *   partial-line edge cases from multiple sys_read() calls.
     *   64 KB covers virtually all real shell scripts; if a script is
     *   larger, we process only the first 64 KB and the last line may
     *   be truncated — this is documented behaviour for this shell.
     *
     * No session setup (setsid/TIOCSCTTY/setpgid) is needed because
     * script mode, like -c mode, is non-interactive: stdin is not a
     * terminal, so job control is irrelevant.
     */
    if (script_file)
    {
        /*
         * Static script buffer: 64 KB.
         *
         * 'static' means it lives in BSS (zero-initialised), not the
         * stack — safe even in deeply recursive or future async contexts.
         */
        static char script_buf[65536];

        long fd = sys_open(script_file, O_RDONLY, 0);
        if (fd < 0)
        {
            /*
             * Cannot open file — print POSIX-style error to stderr.
             * Format: "posixsh: <name>: cannot open script file\n"
             */
            char err_prefix[] = "posixsh: ";
            char err_suffix[] = ": cannot open script file\n";
            sys_write(2, err_prefix, my_strlen(err_prefix));
            sys_write(2, script_file, my_strlen(script_file));
            sys_write(2, err_suffix, my_strlen(err_suffix));
            sys_exit(1);
        }

        /*
         * Read up to sizeof(script_buf)-1 bytes in one call.
         *
         * sys_read() on a regular file returns the number of bytes
         * actually read (may be less than requested if the file is
         * smaller, or exactly sizeof-1 if the file is ≥ 64 KB).
         * We null-terminate immediately so the buffer is a valid C string.
         */
        long nread = sys_read(fd, script_buf, (long)(sizeof(script_buf) - 1));
        sys_close(fd);

        if (nread < 0) nread = 0;   /* read error — treat as empty */
        script_buf[nread] = '\0';

        g_shell_pgid = sys_getpid();
        setup_shell_signals();
        init_job_table();

        /*
         * Walk the buffer line-by-line, identical to -c mode.
         *
         * Empty lines are skipped (len == 0 check).
         * Lines whose first non-whitespace character is '#' are comments;
         * the tokenizer's Step 1 comment-handling discards them automatically
         * — no special check is needed here.
         */
        char line_buf[MAX_INPUT];
        const char *src = script_buf;

        while (*src)
        {
            int len = 0;
            while (*src && *src != '\n' && len < (int)(sizeof(line_buf) - 1))
                line_buf[len++] = *src++;
            if (*src == '\n') src++;  /* skip the delimiter */
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
