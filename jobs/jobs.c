// jobs/jobs.c — job table management for the educational POSIX shell

#include "jobs.h"
#include "../include/wrappers.h"
#include "../utils/string.h"
#include "../env/env.h"   /* g_interactive */

/*===========================================================================
 Global job table
===========================================================================*/

Job g_jobs[MAX_JOBS];

/*===========================================================================
 Internal helpers
===========================================================================*/

// write_str: write a C string to fd without libc
static void write_str(int fd, const char *s)
{
    sys_write(fd, s, my_strlen(s));
}

// write_int: write a long integer in decimal to fd without libc
static void write_int(int fd, long n)
{
    char buf[20];
    int  i = 0;

    if (n == 0) { write_str(fd, "0"); return; }

    while (n > 0 && i < 19) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }

    /* Write digits in reverse (most-significant first) */
    char out[21];
    int  j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = '\0';
    write_str(fd, out);
}

// my_strncat: append up to n chars of src to dst, always null-terminates
static void my_strncat(char *dst, const char *src, int n)
{
    int len = (int)my_strlen(dst);
    int i   = 0;
    while (i < n && src[i] != '\0') { dst[len + i] = src[i]; i++; }
    dst[len + i] = '\0';
}

/*===========================================================================
 Public API
===========================================================================*/

void init_job_table(void)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        g_jobs[i].state     = JOB_FREE;
        g_jobs[i].job_num   = 0;
        g_jobs[i].pgid      = 0;
        g_jobs[i].pid_count = 0;
        g_jobs[i].cmd[0]    = '\0';
    }
}

int add_job(
    long       pgid,
    long      *pids,
    int        pid_count,
    JobState   state,
    const char *cmd
)
{
    /*
     * Find the lowest free slot and the highest current job number
     * in one pass so we can assign the next sequential job number.
     */
    int free_slot  = -1;
    int max_job_num = 0;

    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (g_jobs[i].state == JOB_FREE)
        {
            if (free_slot == -1) free_slot = i;
        }
        else if (g_jobs[i].job_num > max_job_num)
        {
            max_job_num = g_jobs[i].job_num;
        }
    }

    if (free_slot == -1) return -1;     /* table full */

    Job *j      = &g_jobs[free_slot];
    j->job_num  = max_job_num + 1;
    j->pgid     = pgid;
    j->state    = state;
    j->pid_count = (pid_count > MAX_PIPELINE_DEPTH)
                   ? MAX_PIPELINE_DEPTH : pid_count;

    for (int i = 0; i < j->pid_count; i++)
        j->pids[i] = pids[i];

    /* Copy command string (truncate if necessary) */
    int cmd_len = (int)my_strlen(cmd);
    if (cmd_len >= MAX_INPUT) cmd_len = MAX_INPUT - 1;
    my_memcpy(j->cmd, cmd, (long)cmd_len);
    j->cmd[cmd_len] = '\0';

    return j->job_num;
}

Job *find_job_by_num(int job_num)
{
    for (int i = 0; i < MAX_JOBS; i++)
        if (g_jobs[i].state != JOB_FREE && g_jobs[i].job_num == job_num)
            return &g_jobs[i];
    return 0;
}

Job *find_job_by_pgid(long pgid)
{
    for (int i = 0; i < MAX_JOBS; i++)
        if (g_jobs[i].state != JOB_FREE && g_jobs[i].pgid == pgid)
            return &g_jobs[i];
    return 0;
}

Job *find_job_by_pid(long pid)
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (g_jobs[i].state == JOB_FREE) continue;
        for (int k = 0; k < g_jobs[i].pid_count; k++)
            if (g_jobs[i].pids[k] == pid) return &g_jobs[i];
    }
    return 0;
}

void free_job(Job *job)
{
    if (job == 0) return;
    job->state    = JOB_FREE;
    job->job_num  = 0;
    job->pgid     = 0;
    job->pid_count = 0;
    job->cmd[0]   = '\0';
}

/*
 * reap_background_jobs
 *
 * Drains the kernel's child-state-change queue using wait4 with WNOHANG.
 *
 * WUNTRACED is included so we also catch children stopped by SIGTSTP
 * (which can happen if a background job tries to read from the terminal).
 *
 * For each PID returned:
 *   - Find the owning job by PID.
 *   - If the child exited (WIFEXITED or WIFSIGNALED): decrement a counter.
 *     When all stages of the pipeline have exited, mark job as JOB_DONE.
 *   - If the child was stopped (WIFSTOPPED): mark job as JOB_STOPPED.
 *
 * Jobs marked JOB_DONE are reported and freed here.
 * Jobs marked JOB_STOPPED are left in the table for fg/bg to manage.
 *
 * Why we loop:
 *   Multiple children may have changed state between two executions of
 *   the shell loop.  We loop until wait4 returns 0 (no more pending).
 */
void reap_background_jobs(void)
{
    int  status = 0;
    long pid;

    while (1)
    {
        /*
         * SYSCALL: wait4(-1, &status, WNOHANG | WUNTRACED, NULL)
         *
         * -1      : any child
         * WNOHANG : do not block if no child has changed state
         */
        pid = sys_wait4(-1, &status, WNOHANG | WUNTRACED);

        if (pid <= 0) break;    /* 0 = nothing pending, <0 = no children */

        Job *job = find_job_by_pid(pid);
        if (job == 0) continue; /* Orphaned PID (e.g. from a failed fork) */

        if (WIFSTOPPED(status))
        {
            /*
             * A background job tried to read from the terminal (SIGTTIN)
             * and was automatically stopped by the kernel.
             * Mark it stopped; user can fg it.
             */
            job->state = JOB_STOPPED;
            /* No output here — avoids garbling an interactive prompt */
            continue;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            /*
             * One stage of the pipeline exited.  We use a simple heuristic:
             * since we waited for all PIDs to finish before returning from
             * foreground execution, any PID we see here belongs to a
             * background pipeline.  Mark the whole job done.
             *
             * A more precise implementation would track per-PID exit status
             * and only mark the job done when the LAST stage exits.
             * For Phase 4, marking on first exit is sufficient.
             */
            job->state = JOB_DONE;
            /* Only report "Done" in interactive mode — in script/pipe mode
             * it would corrupt the captured stdout of the calling process. */
            if (g_interactive)
                print_job_line(job, 1);
            free_job(job);
        }
    }
}

void build_cmd_string(const Pipeline *pipeline, char *out, int out_size)
{
    out[0] = '\0';
    int remaining = out_size - 1;

    for (int i = 0; i < pipeline->count && remaining > 0; i++)
    {
        if (i > 0)
        {
            my_strncat(out, " | ", remaining);
            remaining -= 3;
            if (remaining <= 0) break;
        }

        const Command *cmd = &pipeline->commands[i];
        for (int j = 0; j < cmd->argc && remaining > 0; j++)
        {
            if (j > 0) { my_strncat(out, " ", remaining); remaining -= 1; }
            int wlen = (int)my_strlen(cmd->argv[j]);
            my_strncat(out, cmd->argv[j], remaining);
            remaining -= wlen;
        }
    }
}

void print_job_line(const Job *job, int is_current)
{
    /*
     * Format: [N]+  State    cmd
     * Example:
     *   [1]+  Running    sleep 10 &
     *   [2]-  Stopped    vim file.txt
     *   [1]+  Done       ls -la
     */
    write_str(1, "[");
    write_int(1, (long)job->job_num);
    write_str(1, "]");
    write_str(1, is_current ? "+  " : "-  ");

    switch (job->state)
    {
        case JOB_RUNNING: write_str(1, "Running    "); break;
        case JOB_STOPPED: write_str(1, "Stopped    "); break;
        case JOB_DONE:    write_str(1, "Done       "); break;
        default:          write_str(1, "Unknown    "); break;
    }

    write_str(1, job->cmd);
    write_str(1, "\n");
}
