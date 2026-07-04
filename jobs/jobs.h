#ifndef JOBS_H
#define JOBS_H

/*
===============================================================================
jobs/jobs.h — job table for background and stopped process management

Why this file exists:
    A POSIX shell tracks every pipeline that is not running in the
    foreground.  Each such pipeline is a "job".  Jobs can be:
      - Running in background (started with &, or moved there with bg)
      - Stopped (suspended with Ctrl+Z)
      - Done (exited but not yet reported to the user)

    The job table is a fixed-size static array — no heap allocation.
===============================================================================
*/

#include "../include/constants.h"
#include "../parser/parser.h"

/*===========================================================================
 Job state
===========================================================================*/

typedef enum
{
    JOB_FREE    = 0,   /* Slot is unused                                    */
    JOB_RUNNING = 1,   /* Pipeline is running in background                 */
    JOB_STOPPED = 2,   /* Pipeline was stopped by SIGTSTP (Ctrl+Z)          */
    JOB_DONE    = 3,   /* Pipeline exited; not yet reported to user         */
} JobState;


/*===========================================================================
 Job struct
===========================================================================*/

typedef struct
{
    int       job_num;                      /* 1-based job number (%1, %2…) */
    long      pgid;                         /* Process group ID              */
    long      pids[MAX_PIPELINE_DEPTH];     /* PIDs of all pipeline stages   */
    int       pid_count;                    /* Number of stages              */
    JobState  state;
    char      cmd[MAX_INPUT];               /* Command string for display    */
} Job;


/*===========================================================================
 Global job table (defined in jobs.c)
===========================================================================*/

extern Job g_jobs[MAX_JOBS];


/*===========================================================================
 Function declarations
===========================================================================*/

/*
 * init_job_table
 *
 * Marks every slot in g_jobs as JOB_FREE.
 * Called once at shell startup before the first prompt.
 */
void init_job_table(void);

/*
 * add_job
 *
 * Allocates a free slot, fills it with the given pipeline information,
 * and returns the job number (1-based).
 *
 * Parameters:
 *   pgid      — process group ID of the new job (PID of first stage)
 *   pids      — array of PIDs, one per pipeline stage
 *   pid_count — number of stages
 *   state     — initial state (JOB_RUNNING or JOB_STOPPED)
 *   cmd       — human-readable command string (for jobs output)
 *
 * Returns:
 *   > 0 : job number
 *   -1  : no free slot (job table is full)
 */
int add_job(
    long      pgid,
    long     *pids,
    int       pid_count,
    JobState  state,
    const char *cmd
);

/*
 * find_job_by_num
 *
 * Returns a pointer to the job with the given 1-based job number,
 * or NULL if not found.
 */
Job *find_job_by_num(int job_num);

/*
 * find_job_by_pgid
 *
 * Returns a pointer to the job with the given process group ID,
 * or NULL if not found.
 */
Job *find_job_by_pgid(long pgid);

/*
 * find_job_by_pid
 *
 * Searches all job slots for a job that contains `pid` in its pids array.
 * Used by reap_background_jobs() when wait4(-1) returns a PID that
 * needs to be matched to a specific job.
 *
 * Returns a pointer to the job, or NULL.
 */
Job *find_job_by_pid(long pid);

/*
 * free_job
 *
 * Marks the given job slot as JOB_FREE, releasing it for reuse.
 * Called after the job has been reported to the user (jobs / fg / bg).
 */
void free_job(Job *job);

/*
 * reap_background_jobs
 *
 * Called from the main shell loop when g_sigchld_flag is set.
 *
 * Calls wait4(-1, WNOHANG | WUNTRACED) in a loop to collect every
 * child that has changed state since the last check:
 *   - Exited children  : mark job as JOB_DONE, print "[N]+ Done  cmd"
 *   - Stopped children : mark job as JOB_STOPPED (already in table
 *                        from foreground-turned-background flow)
 *
 * Does NOT block.  If no child has changed state, returns immediately.
 */
void reap_background_jobs(void);

/*
 * build_cmd_string
 *
 * Constructs a human-readable command string from a Pipeline struct
 * and stores it in `out` (max `out_size` bytes including null terminator).
 *
 * Format: "cmd0 [| cmd1 [| cmd2 ...]]"
 * Used when adding a job so the "jobs" builtin can display it later.
 */
void build_cmd_string(const Pipeline *pipeline, char *out, int out_size);

/*
 * print_job_line
 *
 * Writes one job status line to stdout in the standard format:
 *   [N]+  Running    command &
 *   [N]+  Stopped    command
 *   [N]+  Done       command
 *
 * Parameters:
 *   job    — the job to describe
 *   is_current — if 1, prints "+" after the job number; else prints "-"
 */
void print_job_line(const Job *job, int is_current);

#endif  /* JOBS_H */
