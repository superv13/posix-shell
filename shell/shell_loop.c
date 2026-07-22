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
#include "../builtins/misc_builtins.h"
#include "../executor/executor.h"
#include "../signals/signals.h"
#include "../jobs/jobs.h"
#include "../trace/trace.h"
#include "../env/env.h"

static const char *g_script_file = (const char *)0;
static int         g_script_line = 0;

/*
 * int_to_str
 *
 * Converts an integer value to a null-terminated string representation in buf.
 * Returns the number of characters written.
 */
static int int_to_str(int value, char *buf)
{
    char digits[16];
    int n = 0;
    int len = 0;

    if (value < 0)
    {
        buf[len++] = '-';
        value = -value;
    }

    if (value == 0)
    {
        buf[len++] = '0';
        buf[len] = '\0';
        return len;
    }

    while (value > 0 && n < (int)sizeof(digits))
    {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (n > 0)
    {
        buf[len++] = digits[--n];
    }

    buf[len] = '\0';
    return len;
}

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
        int has_redir = cmd->input_file[0] != '\0' || cmd->output_file[0] != '\0'
                     || cmd->dup_out_src >= 0 || cmd->dup_in_src >= 0;
        if (!has_redir)
        {
            /*
             * Reset status to 0 BEFORE calling the builtin.
             * Builtins that succeed silently (echo, pwd, cd) don't
             * touch g_last_status; defaulting to 0 prevents a prior
             * failed external command's 127 from leaking through.
             * Builtins that can fail (false, wait, cd on ENOENT) will
             * override g_last_status themselves.
             */
            g_last_status = 0;
            execute_builtin(cmd);
            /* Apply ! negation after the builtin has set its own status */
            if (pipeline->negate)
                g_last_status = (g_last_status == 0) ? 1 : 0;
            return;
        }
    }

    execute_pipeline(pipeline);
}


/*
 * SepType — kind of separator that ended a raw input segment
 */
typedef enum {
    SEP_NONE,       /* \0 or \n  — end of input, no more segments */
    SEP_AND,        /* &&        — run next only if prev exited 0  */
    SEP_OR,         /* ||        — run next only if prev failed     */
    SEP_SEMICOLON   /* ;         — always run next                  */
} SepType;

/*
 * find_segment
 *
 * Scans the raw input buffer starting at *pos, respecting single- and
 * double-quote contexts, until an unquoted separator (&&, ||, ;, \n, \0)
 * is found.  Copies the segment text (without the separator) into seg_buf
 * (null-terminated, at most seg_max-1 chars).
 *
 * Why scan the RAW buffer and not the token array:
 *   If we tokenize the whole line first and then slice token sub-arrays,
 *   every '$?' in every segment is expanded to the same value — the one
 *   g_last_status held BEFORE any command in the line ran.  That means
 *   "ls /bad ; echo $?" always prints 0 (or whatever the previous line left).
 *
 *   By scanning the raw buffer and calling tokenize() fresh for each
 *   segment, '$?' is expanded AFTER the previous segment has run, so it
 *   correctly reflects that segment's exit status.  This matches the
 *   behaviour of bash, dash, and every POSIX-conforming shell.
 *
 * Parameters:
 *   input   : full input line (null-terminated)
 *   pos     : in/out — start position on entry; updated past the separator
 *   seg_buf : output — segment text, null-terminated, no separator
 *   seg_max : size of seg_buf
 *   sep     : output — which separator ended the segment
 *
 * Returns the number of characters copied into seg_buf (may be 0).
 */
static int find_segment(const char *input, int *pos,
                        char *seg_buf, int seg_max, SepType *sep)
{
    int  p   = *pos;
    int  len = 0;
    char q   = 0;    /* current quote char: '\'' or '"', 0 = unquoted */

    while (input[p] != '\0' && input[p] != '\n')
    {
        char c = input[p];

        if (q)
        {
            /* Inside a quote — only the matching close quote exits */
            if (c == q) q = 0;
            if (len < seg_max - 1) seg_buf[len++] = c;
            p++;
            continue;
        }

        /* Unquoted: check for two-char separators first */
        if (c == '&' && input[p + 1] == '&')
        {
            *sep = SEP_AND;
            p += 2;
            goto done;
        }
        if (c == '|' && input[p + 1] == '|')
        {
            *sep = SEP_OR;
            p += 2;
            goto done;
        }
        if (c == ';')
        {
            *sep = SEP_SEMICOLON;
            p += 1;
            goto done;
        }

        /* Start of a quote */
        if (c == '\'' || c == '"') q = c;

        if (len < seg_max - 1) seg_buf[len++] = c;
        p++;
    }

    *sep = SEP_NONE;

done:
    seg_buf[len] = '\0';
    *pos = p;
    return len;
}

static int is_whitespace_str(const char *s)
{
    while (*s)
    {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
            return 0;
        s++;
    }
    return 1;
}

/*
 * execute_line  (Steps 4 + 5 — AND/OR list with per-segment expansion)
 *
 * Uses find_segment() to split the raw input line into pipeline segments
 * at unquoted &&, ||, ;, or end-of-line boundaries.  Each segment is
 * tokenised and parsed independently AFTER the previous segment's pipeline
 * has run.  This ensures that $? and $VAR expansions in each segment see
 * the up-to-date shell state (g_last_status, g_environ) at execution time,
 * not the stale state from before the line started.
 *
 * Short-circuit rules (POSIX XBD 2.9.3):
 *   &&  next pipeline runs only if g_last_status == 0
 *   ||  next pipeline runs only if g_last_status != 0
 *   ;   next pipeline always runs
 *
 * Example — "ls /bad ; echo $?":
 *   Segment 0 raw: "ls /bad "  → tokenise → run → g_last_status = 2
 *   Segment 1 raw: " echo $?"  → tokenise ($? now = 2) → run → prints "2"
 */
static int execute_line(char *buf)
{
    int     pos   = 0;
    SepType join  = SEP_NONE;   /* first segment always runs */
    int     first = 1;

    while (1)
    {
        char    seg[MAX_INPUT];
        SepType sep;
        int     seg_len = find_segment(buf, &pos, seg, MAX_INPUT, &sep);

        /* End of input with no segment */
        if (seg_len == 0 && sep == SEP_NONE)
        {
            if (!first && (join == SEP_AND || join == SEP_OR))
            {
                /* Fall through to report trailing && / || syntax error */
            }
            else
            {
                break;
            }
        }

        /* Check for list syntax errors: empty segments next to operators */
        int is_empty = is_whitespace_str(seg);
        if (is_empty)
        {
            int syntax_err = 0;
            if (first && sep != SEP_NONE)
            {
                syntax_err = 1; /* e.g. "&& echo A" or "; echo A" */
            }
            else if (!first && (join == SEP_AND || join == SEP_OR))
            {
                syntax_err = 1; /* e.g. "echo A &&" or "echo A && && echo B" */
            }
            else if (!first && join == SEP_SEMICOLON && sep != SEP_NONE)
            {
                syntax_err = 1; /* e.g. "echo A ; ; echo B" */
            }

            if (syntax_err)
            {
                if (g_script_file)
                {
                    char err_prefix[] = "posixsh: ";
                    char err_middle[] = ": line ";
                    char err_suffix[] = ": syntax error\n";
                    sys_write(2, err_prefix, my_strlen(err_prefix));
                    sys_write(2, g_script_file, my_strlen(g_script_file));
                    sys_write(2, err_middle, my_strlen(err_middle));
                    char num_buf[16];
                    int num_len = int_to_str(g_script_line, num_buf);
                    sys_write(2, num_buf, num_len);
                    sys_write(2, err_suffix, my_strlen(err_suffix));
                }
                else
                {
                    char err[] = "posixsh: syntax error\n";
                    sys_write(2, err, my_strlen(err));
                }
                g_last_status = 2;
                return -1;
            }
        }

        /* ── Short-circuit ───────────────────────────────────────────── */
        int skip = 0;
        if (!first)
        {
            if (join == SEP_AND && g_last_status != 0) skip = 1;
            if (join == SEP_OR  && g_last_status == 0) skip = 1;
        }
        first = 0;

        if (!skip && seg_len > 0 && !is_empty)
        {
            /*
             * Tokenise this segment fresh.
             *
             * $? and $VAR are expanded HERE, after the previous segment
             * has updated g_last_status and the environment.
             */
            Token tokens[MAX_TOKENS];
            int   token_count = 0;
            tokenize(seg, tokens, &token_count);

            Pipeline pipeline;
            if (parse(tokens, token_count, &pipeline) == -1)
            {
                if (g_script_file)
                {
                    char err_prefix[] = "posixsh: ";
                    char err_middle[] = ": line ";
                    char err_suffix[] = ": syntax error\n";
                    sys_write(2, err_prefix, my_strlen(err_prefix));
                    sys_write(2, g_script_file, my_strlen(g_script_file));
                    sys_write(2, err_middle, my_strlen(err_middle));
                    char num_buf[16];
                    int num_len = int_to_str(g_script_line, num_buf);
                    sys_write(2, num_buf, num_len);
                    sys_write(2, err_suffix, my_strlen(err_suffix));
                }
                else
                {
                    char err[] = "posixsh: syntax error\n";
                    sys_write(2, err, my_strlen(err));
                }
                g_last_status = 2;
                return -1;
            }

            run_one_pipeline(&pipeline);
        }

        if (sep == SEP_NONE) break;
        join = sep;
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
        /* non-interactive: g_interactive stays 0 */
        g_shell_pgid = sys_getpid();
        setup_shell_signals();
        init_job_table();
        g_eval_fn = execute_line;

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
            {
                if (execute_line(line_buf) == -1)
                {
                    sys_exit(2);
                }
            }
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
        g_eval_fn = execute_line;    /* non-interactive: g_interactive stays 0 */

        /*
         * Walk the buffer line-by-line, identical to -c mode.
         *
         * Empty lines are skipped (len == 0 check).
         * Lines whose first non-whitespace character is '#' are comments;
         * the tokenizer's Step 1 comment-handling discards them automatically
         * — no special check is needed here.
         */
        g_script_file = script_file;
        g_script_line = 0;
        char line_buf[MAX_INPUT];
        const char *src = script_buf;

        while (*src)
        {
            g_script_line++;
            int len = 0;
            while (*src && *src != '\n' && len < (int)(sizeof(line_buf) - 1))
                line_buf[len++] = *src++;
            if (*src == '\n') src++;  /* skip the delimiter */
            line_buf[len] = '\0';

            if (len > 0)
            {
                if (execute_line(line_buf) == -1)
                {
                    sys_exit(2);
                }
            }
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

    /* ── Step 8: wire eval builtin ───────────────────────────────────── */
    g_eval_fn    = execute_line;
    g_interactive = 1;   /* stdin is a tty — interactive mode */

    char buffer[MAX_INPUT];

    while (1)
    {
        /* ── Reap background jobs ────────────────────────────────────── */
        g_sigchld_flag = 0;
        reap_background_jobs();

        /* ── Prompt — only in interactive mode ──────────────────────── */
        if (g_interactive)
        {
            char prompt[] = "posixsh> ";
            sys_write(1, prompt, my_strlen(prompt));
        }

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
