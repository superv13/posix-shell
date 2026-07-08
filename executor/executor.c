// executor/executor.c — Phase 4 execution engine
//
// Phase 4 additions over Phase 3:
//   1. Every pipeline gets its own process group (PGID = first child PID).
//   2. Foreground pipelines receive terminal control via tcsetpgrp.
//   3. Children reset signal handlers to SIG_DFL before exec.
//   4. Ctrl+Z (SIGTSTP) on a foreground job: detected via WUNTRACED,
//      job added to table as JOB_STOPPED, terminal returned to shell.
//   5. Background jobs are added to the job table.
//   6. Terminal is always restored to the shell after foreground jobs.

#include "executor.h"
#include "path.h"

#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../builtins/builtins.h"
#include "../signals/signals.h"
#include "../jobs/jobs.h"
#include "../trace/trace.h"
#include "../env/env.h"

// Stdin file descriptor — the terminal we hand to foreground jobs
#define STDIN_FD  0

/*===========================================================================
 Internal helpers
===========================================================================*/

static void write_str(int fd, const char *s)
{
    sys_write(fd, s, my_strlen(s));
}

static void print_long(int fd, long value)
{
    char digits[20];
    int  i = 0;

    if (value == 0) { write_str(fd, "0"); return; }

    int negative = (value < 0);
    if (negative) value = -value;

    while (value > 0 && i < (int)sizeof(digits))
    {
        digits[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    char out[21];
    int  j = 0;
    if (negative) out[j++] = '-';
    while (i > 0) out[j++] = digits[--i];
    out[j] = '\0';
    write_str(fd, out);
}

/*===========================================================================
 apply_redirections — open files and dup2 onto stdin/stdout
 (unchanged from Phase 3)
===========================================================================*/

static int apply_redirections(Command *cmd)
{
    if (cmd->input_file[0] != '\0')
    {
        long fd = sys_open(cmd->input_file, O_RDONLY, 0);
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
        int flags = O_WRONLY | O_CREAT |
                    (cmd->append ? O_APPEND : O_TRUNC);

        long fd = sys_open(cmd->output_file, flags, REDIR_CREATE_MODE);
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

/*===========================================================================
 close_all_pipes
===========================================================================*/

static void close_all_pipes(int pipes[][2], int num_pipes)
{
    for (int i = 0; i < num_pipes; i++)
    {
        sys_close(pipes[i][0]);
        sys_close(pipes[i][1]);
    }
}

/*===========================================================================
 run_stage — runs inside a child process, never returns

 Phase 4 additions:
   1. sys_setpgid(0, pipeline_pgid)  — join the pipeline's process group.
   2. reset_child_signals()          — restore SIG_DFL before exec.
===========================================================================*/

static void run_stage(
    Command *cmd,
    int      index,
    int      count,
    int      pipes[][2],
    int      num_pipes,
    long     pipeline_pgid   /* Phase 4: PGID for this pipeline */
)
{
    /*
     * SYSCALL: setpgid(0, pipeline_pgid)
     *
     * Put this child into the pipeline's process group.
     * For the first child (index 0), pipeline_pgid == 0, which the
     * kernel interprets as "use my own PID as PGID", creating a new group.
     * For subsequent children, pipeline_pgid is the first child's PID,
     * so they all join the same group.
     *
     * Also called from the parent (in execute_pipeline) to avoid the race
     * where tcsetpgrp runs before the child's setpgid.
     */
    sys_setpgid(0, pipeline_pgid);

    /*
     * Reset all signal handlers to SIG_DFL.
     *
     * The shell ignores SIGINT, SIGTSTP, etc.  Without this call the
     * child would inherit SIG_IGN and Ctrl+C / Ctrl+Z would have no
     * effect on the running program.
     */
    reset_child_signals();

    /* Wire this stage's stdin/stdout to the adjacent pipes */
    if (index > 0)
        sys_dup2(pipes[index - 1][0], 0);

    if (index < count - 1)
        sys_dup2(pipes[index][1], 1);

    /*
     * Close every pipe fd.
     *
     * SYSCALL: close(fd) × 2 × num_pipes
     *
     * Each child inherits ALL pipe fds.  The ones not dup2'd onto
     * fd 0/1 must be closed or downstream readers never see EOF.
     */
    close_all_pipes(pipes, num_pipes);

    /* Apply explicit file redirections (override pipe wiring if present) */
    if (apply_redirections(cmd) < 0)
        sys_exit(1);

    if (cmd->argc == 0)
        sys_exit(0);

    if (cmd->is_builtin)
    {
        /*
         * Builtins inside a pipeline run in this child (a subshell).
         * They cannot affect the interactive shell's state (e.g. cd in
         * a pipeline does not change the shell's directory).
         */
        if (execute_builtin(cmd))
            sys_exit(0);
    }

    char *path = find_executable(cmd->argv[0]);

    if (path == 0)
    {
        write_str(2, "posixsh: ");
        write_str(2, cmd->argv[0]);
        write_str(2, ": command not found\n");
        sys_exit(127);
    }

    /*
     * SYSCALL: execve(path, argv, g_environ)
     *
     * Replace this process image with the target program.
     *
     * Phase 5 change: envp is now g_environ instead of NULL.
     *
     * Why this matters:
     *   With envp = NULL, every child process starts with an empty
     *   environment.  Programs that read PATH, HOME, TERM, LANG, or any
     *   other variable receive nothing.  "python3 -c 'import os; print(os.environ)'"
     *   would print an empty dict.
     *
     *   Passing g_environ (the real envp captured at _start from the
     *   kernel stack) gives every child the same environment the shell
     *   itself was started with — exactly the POSIX-required behaviour.
     *
     * Never returns on success.
     */
    trace_execve(path);   /* [TRACE] execve(<path>, ...) */
    sys_execve(path, cmd->argv, g_environ);

    write_str(2, "posixsh: ");
    write_str(2, cmd->argv[0]);
    write_str(2, ": exec failed\n");
    sys_exit(126);
}

/*===========================================================================
 execute_pipeline — Phase 4 complete implementation
===========================================================================*/

int execute_pipeline(Pipeline *pipeline)
{
    if (pipeline == 0) return -1;

    int count = pipeline->count;
    if (count == 0) return 0;

    if (count > MAX_PIPELINE_DEPTH)
    {
        write_str(2, "posixsh: too many pipeline stages\n");
        return -1;
    }

    int  num_pipes = count - 1;
    int  pipes[MAX_PIPELINE_DEPTH - 1][2];
    long pids[MAX_PIPELINE_DEPTH];
    long pipeline_pgid = 0;  /* Set to PID of first child after first fork */

    /* ── Step 1: Create all pipes ───────────────────────────────────── */
    for (int i = 0; i < num_pipes; i++)
    {
        if (sys_pipe(pipes[i]) < 0)
        {
            write_str(2, "posixsh: pipe failed\n");
            close_all_pipes(pipes, i);
            return -1;
        }
    }

    /* ── Step 2: Fork one child per stage ───────────────────────────── */
    for (int i = 0; i < count; i++)
    {
        /*
         * SYSCALL: fork()
         *
         * Creates a copy of the shell process.  The child inherits all
         * open file descriptors (including every pipe fd created above).
         */
        long pid = sys_fork();

        if (pid == 0)
        {
            /*
             * Child: pipeline_pgid is 0 for the first child
             * (setpgid(0,0) → creates new group with own PID).
             * For children 1..N-1 it is the first child's PID
             * (setpgid(0, pgid) → join that group).
             */
            run_stage(
                &pipeline->commands[i],
                i, count,
                pipes, num_pipes,
                pipeline_pgid
            );
            /* run_stage never returns */
        }

        if (pid < 0)
        {
            write_str(2, "posixsh: fork failed\n");
            close_all_pipes(pipes, num_pipes);
            return -1;
        }

        /* [TRACE] fork() -> <child_pid> */
        trace_fork(pid);

        pids[i] = pid;

        /*
         * Record the pipeline's PGID from the first child's PID.
         * The parent also calls setpgid() here (race-condition prevention):
         * either the parent or child will call it first; both calling
         * ensures the group exists by the time tcsetpgrp() is called.
         */
        if (i == 0)
        {
            pipeline_pgid = pid;

            /*
             * SYSCALL: setpgid(pid, pid)
             *
             * Put the first child in its own process group.
             * This mirrors the setpgid(0, 0) the child itself calls.
             * Calling from both parent and child avoids the race.
             */
            sys_setpgid(pid, pid);
        }
        else
        {
            /*
             * SYSCALL: setpgid(pid, pipeline_pgid)
             *
             * Add this child to the existing pipeline process group.
             */
            sys_setpgid(pid, pipeline_pgid);
        }
    }

    /* ── Step 3: Parent closes all pipe fds ─────────────────────────── */
    /*
     * SYSCALL: close(fd) × 2 × num_pipes
     *
     * If the parent holds any write end open, the child reading from
     * the corresponding read end will never see EOF.  Deadlock.
     */
    close_all_pipes(pipes, num_pipes);

    /* ── Step 4: Background vs foreground ───────────────────────────── */
    if (pipeline->background)
    {
        char cmd_str[MAX_INPUT];
        build_cmd_string(pipeline, cmd_str, MAX_INPUT);

        int jnum = add_job(
            pipeline_pgid,
            pids,
            count,
            JOB_RUNNING,
            cmd_str
        );

        write_str(1, "[");
        print_long(1, (long)jnum);
        write_str(1, "] ");
        print_long(1, pipeline_pgid);
        write_str(1, "\n");

        /*
         * Phase 5: POSIX XBD 2.8.2 — "If a pipeline is run in the
         * background, the exit status is 0."
         * g_last_status = 0 so that $? after "cmd &" is always 0.
         */
        g_last_status = 0;
        return 0;
    }

    /* ── Foreground: give terminal to the pipeline ───────────────────── */
    /*
     * SYSCALL: tcsetpgrp(STDIN_FD, pipeline_pgid)
     *
     * Transfers terminal ownership to the pipeline's process group so
     * Ctrl+C and Ctrl+Z reach the job, not the shell.
     */
    sys_tcsetpgrp(STDIN_FD, pipeline_pgid);

    /* ── Wait for every stage (foreground) ──────────────────────────── */
    int last_raw_status = 0;    /* raw wait4 status word of last stage */
    int all_stopped     = 0;

    for (int i = 0; i < count; i++)
    {
        int status = 0;

        /*
         * SYSCALL: wait4(pids[i], &status, WUNTRACED, NULL)
         *
         * WUNTRACED: also returns when the child is stopped (Ctrl+Z),
         * not only when it exits.  Without WUNTRACED, Ctrl+Z would cause
         * wait4 to block forever.
         */
        sys_wait4(pids[i], &status, WUNTRACED);

        if (WIFSTOPPED(status))
            all_stopped = 1;

        if (i == count - 1)
            last_raw_status = status;
    }

    /* ── Restore terminal to the shell ──────────────────────────────── */
    /*
     * SYSCALL: tcsetpgrp(STDIN_FD, g_shell_pgid)
     *
     * Return terminal to the shell regardless of whether the job exited
     * or was stopped.  Without this, the shell's next read() triggers
     * SIGTTIN and the shell stops itself.
     */
    sys_tcsetpgrp(STDIN_FD, g_shell_pgid);

    /*
     * Phase 5: convert raw wait4 status to POSIX $? exit code and store
     * in g_last_status so tokenizer.c can expand $?.
     *
     * Conversion rules (POSIX XBD 2.8.2):
     *
     *   WIFEXITED(s)   : exit code = WEXITSTATUS(s)        (0–255)
     *   WIFSIGNALED(s) : exit code = 128 + WTERMSIG(s)     (POSIX convention)
     *   WIFSTOPPED(s)  : job still alive; exit_code = 0    (not a final status)
     *
     * The "128 + sig" convention is not mandated by the letter of POSIX
     * but is required in practice: every script that does
     *   kill -TERM pid; test $? -gt 128
     * relies on it.  bash, dash, ksh, zsh all use 128+sig.
     */
    int exit_code = 0;

    if (WIFEXITED(last_raw_status))
    {
        exit_code = WEXITSTATUS(last_raw_status);
    }
    else if (WIFSIGNALED(last_raw_status))
    {
        exit_code = 128 + WTERMSIG(last_raw_status);
    }

    g_last_status = exit_code;

    /* ── Handle stopped foreground job ──────────────────────────────── */
    if (all_stopped)
    {
        char cmd_str[MAX_INPUT];
        build_cmd_string(pipeline, cmd_str, MAX_INPUT);

        int jnum = add_job(
            pipeline_pgid,
            pids,
            count,
            JOB_STOPPED,
            cmd_str
        );

        Job *j = find_job_by_num(jnum);
        if (j != 0)
            print_job_line(j, 1);

        return exit_code;
    }

    return exit_code;
}
