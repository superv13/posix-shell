// builtins/jobs_builtin.c
// Implementation of the jobs, fg, and bg builtin commands.

#include "jobs_builtin.h"
#include "../jobs/jobs.h"
#include "../signals/signals.h"
#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"

#define STDIN_FD 0

/*===========================================================================
 Internal helpers
===========================================================================*/

static void write_str(int fd, const char *s)
{
    sys_write(fd, s, my_strlen(s));
}

/*
 * parse_job_arg
 *
 * Parses the optional %N argument from a fg/bg command.
 *
 *   argv[1] == NULL  → returns -1 (caller uses most-recent job)
 *   argv[1] == "%3"  → returns 3
 *   argv[1] == "3"   → returns 3
 *
 * Returns the job number, or -1 if no argument provided, or 0 on error.
 */
static int parse_job_arg(Command *cmd)
{
    if (cmd->argc < 2 || cmd->argv[1] == 0) return -1;  /* no arg */

    const char *arg = cmd->argv[1];
    if (arg[0] == '%') arg++;   /* skip optional % prefix */

    int n = 0;
    while (*arg >= '0' && *arg <= '9') n = n * 10 + (*arg++ - '0');

    return n;
}

/*
 * find_most_recent_job
 *
 * Returns the job with the highest job_num that is in the given state,
 * or NULL if no such job exists.
 * If state == JOB_FREE, returns the most recent job in any state.
 */
static Job *find_most_recent_job(JobState state)
{
    Job *best = 0;

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (g_jobs[i].state == JOB_FREE) continue;

        /* if a specific state is requested, skip non-matching */
        if (state != JOB_FREE && g_jobs[i].state != state) continue;

        if (best == 0 || g_jobs[i].job_num > best->job_num)
            best = &g_jobs[i];
    }

    return best;
}

/*===========================================================================
 builtin_jobs
===========================================================================*/

int builtin_jobs(Command *cmd)
{
    (void)cmd;  /* unused — jobs takes no arguments */

    int any = 0;

    /*
     * Print all non-FREE jobs in job_num order.
     * Find the highest job number to identify the "current" job (+).
     */
    int max_num = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (g_jobs[i].state != JOB_FREE && g_jobs[i].job_num > max_num)
            max_num = g_jobs[i].job_num;

    for (int n = 1; n <= max_num; n++)
    {
        Job *j = find_job_by_num(n);
        if (j == 0) continue;

        /* Clean up jobs that are done */
        if (j->state == JOB_DONE)
        {
            print_job_line(j, (j->job_num == max_num) ? 1 : 0);
            free_job(j);
            any = 1;
            continue;
        }

        print_job_line(j, (j->job_num == max_num) ? 1 : 0);
        any = 1;
    }

    (void)any;
    return 1;   /* handled */
}

/*===========================================================================
 builtin_fg
===========================================================================*/

int builtin_fg(Command *cmd)
{
    int job_num = parse_job_arg(cmd);

    /* Find the target job */
    Job *job = 0;

    if (job_num < 0)
    {
        /*
         * No argument: prefer the most recently stopped job.
         * If none stopped, take the most recent running job.
         */
        job = find_most_recent_job(JOB_STOPPED);
        if (job == 0)
            job = find_most_recent_job(JOB_RUNNING);
    }
    else
    {
        job = find_job_by_num(job_num);
    }

    if (job == 0)
    {
        write_str(2, "posixsh: fg: no such job\n");
        return 1;
    }

    /* Print the command we are about to foreground */
    write_str(1, job->cmd);
    write_str(1, "\n");

    /* Mark running before sending SIGCONT */
    job->state = JOB_RUNNING;

    /*
     * SYSCALL: kill(-pgid, SIGCONT)
     *
     * Sends SIGCONT to every process in the job's process group.
     * This resumes all stopped children simultaneously.
     * A negative first argument means "process group -pgid".
     */
    sys_kill(-job->pgid, SIGCONT);

    /*
     * Give the terminal to the job's process group.
     *
     * SYSCALL (via wrapper): ioctl(STDIN_FD, TIOCSPGRP, &pgid)
     *
     * After this, Ctrl+C and Ctrl+Z go to the job, not the shell.
     */
    sys_tcsetpgrp(STDIN_FD, job->pgid);

    /*
     * Wait for every stage in the pipeline.
     *
     * WUNTRACED: return if the job is stopped again (another Ctrl+Z).
     */
    int stopped_again = 0;
    long pgid_copy    = job->pgid;          /* save before possible free */
    int  pid_count    = job->pid_count;
    long pids_copy[MAX_PIPELINE_DEPTH];
    char cmd_copy[MAX_INPUT];

    for (int i = 0; i < pid_count; i++)
        pids_copy[i] = job->pids[i];

    my_strcpy(cmd_copy, job->cmd);

    for (int i = 0; i < pid_count; i++)
    {
        int status = 0;

        /*
         * SYSCALL: wait4(pid, &status, WUNTRACED, NULL)
         */
        sys_wait4(pids_copy[i], &status, WUNTRACED);

        if (WIFSTOPPED(status))
            stopped_again = 1;
    }

    /*
     * Return terminal to the shell.
     *
     * SYSCALL (via wrapper): ioctl(STDIN_FD, TIOCSPGRP, &g_shell_pgid)
     */
    sys_tcsetpgrp(STDIN_FD, g_shell_pgid);

    if (stopped_again)
    {
        /*
         * Job was stopped again (user pressed Ctrl+Z while running fg).
         * Update the table entry.
         */
        Job *j2 = find_job_by_pgid(pgid_copy);
        if (j2 != 0)
        {
            j2->state = JOB_STOPPED;
            print_job_line(j2, 1);
        }
    }
    else
    {
        /*
         * Job exited normally.  Remove from job table.
         */
        Job *j2 = find_job_by_pgid(pgid_copy);
        if (j2 != 0)
            free_job(j2);
    }

    return 1;   /* handled */
}

/*===========================================================================
 builtin_bg
===========================================================================*/

int builtin_bg(Command *cmd)
{
    int job_num = parse_job_arg(cmd);

    Job *job = 0;

    if (job_num < 0)
    {
        /* No argument: most recently stopped job */
        job = find_most_recent_job(JOB_STOPPED);
    }
    else
    {
        job = find_job_by_num(job_num);
    }

    if (job == 0)
    {
        write_str(2, "posixsh: bg: no such job\n");
        return 1;
    }

    if (job->state != JOB_STOPPED)
    {
        write_str(2, "posixsh: bg: job is already running\n");
        return 1;
    }

    job->state = JOB_RUNNING;

    /*
     * SYSCALL: kill(-pgid, SIGCONT)
     *
     * Resume the stopped process group in the background.
     * We do NOT give it terminal control — it runs without a terminal.
     * If it tries to read from the terminal, SIGTTIN will stop it again.
     */
    sys_kill(-job->pgid, SIGCONT);

    /* Print: "[N]+ cmd &" */
    print_job_line(job, 1);

    return 1;   /* handled */
}
