//=============================================================================
// executor.c
//
// Purpose:
//   Implements the Phase 3 execution engine of the educational POSIX shell.
//
// Why this file exists:
//   The parser only understands command syntax. The executor is responsible
//   for transforming a parsed Pipeline into one or more running processes.
//
// Phase 3 implementation:
//   - PATH lookup                              (executor/path.c)
//   - N-stage pipelines  ("a | b | c")          -> N-1 pipe(), N fork()
//   - I/O redirection    ("<", ">", ">>")       -> open() + dup2()
//   - Background jobs    ("cmd &")              -> no wait4() in parent
//
// Pipeline wiring, conceptually, for "a | b | c":
//
//   stdin --> [a] --> pipe0 --> [b] --> pipe1 --> [c] --> stdout
//
//   Stage i (0-indexed) reads from pipes[i-1] (if i > 0)
//   Stage i writes to   pipes[i]   (if i < count-1)
//
// Why redirection is applied *after* pipe dup2():
//   A command can legally use both a pipe and a redirect, e.g.
//   "sort < unsorted.txt | uniq". The pipe wiring sets up the "default"
//   stdin/stdout for each stage; an explicit redirect on that same stage
//   then overrides it. Doing redirection second means "<"/">" always wins,
//   which matches POSIX shell semantics.
//
// Why every pipe fd is closed in every child:
//   Pipes only deliver EOF to a reader once *every* writable copy of the
//   write end is closed. If a child inherits pipe fds it never uses (e.g.
//   stage 0 inheriting pipes[1] meant for stage 1/2) and never closes them,
//   downstream readers can hang forever waiting for EOF that never comes.
//   We close every pipe fd in every child immediately after dup2(), keeping
//   only the two descriptors (0 and 1) that were just wired up.
//
//=============================================================================

#include "executor.h"

#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../executor/path.h"
#include "../builtins/builtins.h"

/*
===============================================================================
FUNCTION: write_str

Purpose:
    Tiny helper so error paths below stay readable. Wraps sys_write() with
    an implicit strlen(), matching the style already used in builtins.c.
===============================================================================
*/

static void write_str(
    int fd,
    const char *s
)
{
    sys_write(
        fd,
        s,
        my_strlen(s)
    );
}

/*
===============================================================================
FUNCTION: print_long

Purpose:
    Converts a long to decimal ASCII and writes it to fd, without using
    libc's printf()/itoa(). Used only for the "[bg pid N]" job notice.

Educational note:
    Digits are produced least-significant-first into a small stack buffer,
    then written out in reverse (most-significant-first), which is the
    standard zero-allocation integer-to-string technique.
===============================================================================
*/

static void print_long(
    int fd,
    long value
)
{
    char digits[20];
    int  i = 0;

    if (value == 0)
    {
        write_str(fd, "0");
        return;
    }

    int negative = (value < 0);
    if (negative)
    {
        value = -value;
    }

    while (value > 0 && i < (int)sizeof(digits))
    {
        digits[i++] = '0' + (value % 10);
        value /= 10;
    }

    char out[21];
    int  j = 0;

    if (negative)
    {
        out[j++] = '-';
    }

    while (i > 0)
    {
        out[j++] = digits[--i];
    }

    out[j] = '\0';

    write_str(fd, out);
}

/*
===============================================================================
FUNCTION: apply_redirections

Purpose:
    Opens any "<", ">" or ">>" target named in cmd and dup2()s it onto the
    correct standard file descriptor (0 for input, 1 for output).

Kernel interaction:
    open(pathname, flags, mode)   -- creates/opens the target file
    dup2(fd, 0 or 1)              -- makes that file *be* stdin/stdout
    close(fd)                     -- the original high-numbered fd is no
                                     longer needed once duped onto 0/1

Why dup2() and not just using the opened fd directly:
    execve() does not take a list of "this program's stdin is fd 7". Every
    program assumes its input is fd 0 and its output is fd 1. dup2() is how
    the shell rewires those fixed numbers before execve() replaces the
    process image.

Returns:
     0 : Success (or nothing to redirect).
    -1 : open() failed; caller should abandon this command.
===============================================================================
*/

static int apply_redirections(
    Command *cmd
)
{
    if (cmd->input_file[0] != '\0')
    {
        long fd =
            sys_open(
                cmd->input_file,
                O_RDONLY,
                0
            );

        if (fd < 0)
        {
            write_str(2, "posixsh: cannot open ");
            write_str(2, cmd->input_file);
            write_str(2, " for reading\n");
            return -1;
        }

        sys_dup2(fd, 0);
        sys_close(fd);
    }

    if (cmd->output_file[0] != '\0')
    {
        int flags =
            O_WRONLY | O_CREAT |
            (cmd->append ? O_APPEND : O_TRUNC);

        long fd =
            sys_open(
                cmd->output_file,
                flags,
                REDIR_CREATE_MODE
            );

        if (fd < 0)
        {
            write_str(2, "posixsh: cannot open ");
            write_str(2, cmd->output_file);
            write_str(2, " for writing\n");
            return -1;
        }

        sys_dup2(fd, 1);
        sys_close(fd);
    }

    return 0;
}

/*
===============================================================================
FUNCTION: close_all_pipes

Purpose:
    Closes every fd belonging to every pipe in this pipeline. Called by
    every child (after wiring its own two fds) and by the parent (once all
    children exist).
===============================================================================
*/

static void close_all_pipes(
    int pipes[][2],
    int num_pipes
)
{
    for (int i = 0; i < num_pipes; i++)
    {
        sys_close(pipes[i][0]);
        sys_close(pipes[i][1]);
    }
}

/*
===============================================================================
FUNCTION: run_stage

Purpose:
    Runs inside the freshly fork()'d child for pipeline stage `index`.
    Never returns: every path through this function ends in sys_exit().

Steps, in required order:
    1. dup2() the pipe ends this stage needs onto fd 0 / fd 1.
    2. Close every pipe fd (see file-level comment on why this matters).
    3. Apply explicit redirections (these override the pipe wiring above).
    4. Run as a builtin, if marked as one, OR resolve PATH and execve().
===============================================================================
*/

static void run_stage(
    Command *cmd,
    int      index,
    int      count,
    int      pipes[][2],
    int      num_pipes
)
{
    if (index > 0)
    {
        /* Not the first stage: stdin comes from the previous pipe. */
        sys_dup2(pipes[index - 1][0], 0);
    }

    if (index < count - 1)
    {
        /* Not the last stage: stdout goes to the next pipe. */
        sys_dup2(pipes[index][1], 1);
    }

    close_all_pipes(pipes, num_pipes);

    if (apply_redirections(cmd) < 0)
    {
        sys_exit(1);
    }

    if (cmd->argc == 0)
    {
        /*
         * A "command" with no words at all (e.g. a bare "> file" line).
         * Nothing to execute; the side effect (creating/truncating the
         * file) already happened in apply_redirections().
         */
        sys_exit(0);
    }

    if (cmd->is_builtin)
    {
        /*
         * Builtins inside a pipeline run inside this child, not the shell
         * process, since the child is already a throwaway subshell-like
         * process. This matches POSIX semantics: builtins that appear as
         * non-last/last stages of a pipe execute in a subshell and cannot
         * affect the parent shell's state (e.g. "cd /tmp | true" does not
         * change the interactive shell's directory).
         */
        if (execute_builtin(cmd))
        {
            sys_exit(0);
        }
        /* Recognised as a builtin keyword but not yet implemented
         * (jobs/fg/bg belong to Phase 4) -- fall through to PATH lookup
         * so the failure mode is consistent: "command not found". */
    }

    char *path =
        find_executable(cmd->argv[0]);

    if (path == 0)
    {
        write_str(2, "posixsh: ");
        write_str(2, cmd->argv[0]);
        write_str(2, ": command not found\n");
        sys_exit(127);
    }

    sys_execve(
        path,
        cmd->argv,
        0
    );

    /*
     * execve() returns only on failure.
     */
    write_str(2, "posixsh: ");
    write_str(2, cmd->argv[0]);
    write_str(2, ": exec failed\n");
    sys_exit(126);
}

/*
===============================================================================
FUNCTION: execute_pipeline

Purpose:
    Executes a parsed command pipeline of 1..MAX_PIPELINE_DEPTH stages,
    wiring N-1 pipes between them, then either waits for all stages
    (foreground) or returns immediately (background, "cmd &").

Process creation order matters:
    All N pipes are created *before* any fork(), so that every child can
    see every pipe fd at fork time. Each child then keeps only the two fds
    it actually needs and closes the rest (see close_all_pipes()).

Returns:
     0 : Pipeline launched successfully (background), or completed
         (foreground) -- the foreground exit status of the last stage is
         returned via the lower byte where relevant, 0 otherwise.
    -1 : pipe() or fork() failed; the pipeline could not be started.
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

    int count = pipeline->count;

    if (count == 0)
    {
        return 0;
    }

    if (count > MAX_PIPELINE_DEPTH)
    {
        write_str(2, "posixsh: too many pipeline stages\n");
        return -1;
    }

    int  num_pipes = count - 1;
    int  pipes[MAX_PIPELINE_DEPTH - 1][2];
    long pids[MAX_PIPELINE_DEPTH];

    /*
     * Step 1: create every pipe up front. (See file-level comment.)
     */
    for (int i = 0; i < num_pipes; i++)
    {
        if (sys_pipe(pipes[i]) < 0)
        {
            write_str(2, "posixsh: pipe failed\n");

            /* Best effort: close whatever pipes already succeeded. */
            close_all_pipes(pipes, i);

            return -1;
        }
    }

    /*
     * Step 2: fork one child per pipeline stage.
     */
    for (int i = 0; i < count; i++)
    {
        long pid = sys_fork();

        if (pid == 0)
        {
            /* Child: never returns. */
            run_stage(
                &pipeline->commands[i],
                i,
                count,
                pipes,
                num_pipes
            );
        }
        else if (pid > 0)
        {
            pids[i] = pid;
        }
        else
        {
            write_str(2, "posixsh: fork failed\n");
            close_all_pipes(pipes, num_pipes);
            return -1;
        }
    }

    /*
     * Step 3: the parent (shell) never reads or writes through these
     * pipes directly -- only the children do. Closing them here is what
     * lets EOF eventually propagate once the children finish.
     */
    close_all_pipes(pipes, num_pipes);

    if (pipeline->background)
    {
        /*
         * "cmd &" : do not wait. The shell returns to the prompt
         * immediately. Without a SIGCHLD handler (Phase 4), background
         * children become zombies until the shell exits or a future
         * wait4() call reaps them -- acceptable for the Phase 3 milestone.
         */
        write_str(1, "[bg pid ");
        print_long(1, pids[count - 1]);
        write_str(1, "]\n");

        return 0;
    }

    /*
     * Foreground: wait for every stage so the prompt does not reappear
     * until the whole pipeline has finished. The exit status reported is
     * that of the *last* stage, matching POSIX "$?" semantics for pipes.
     */
    int last_status = 0;

    for (int i = 0; i < count; i++)
    {
        int status = 0;

        sys_wait4(
            pids[i],
            &status,
            0
        );

        if (i == count - 1)
        {
            last_status = status;
        }
    }

    return last_status;
}
