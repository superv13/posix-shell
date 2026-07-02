#ifndef JOBS_BUILTIN_H
#define JOBS_BUILTIN_H

/*
===============================================================================
builtins/jobs_builtin.h

Declares the three job-control builtins: jobs, fg, bg.

Why builtins:
    All three must run inside the shell process itself.

    jobs: reads g_jobs[] — impossible from a forked child.
    fg:   calls tcsetpgrp() to hand terminal to the resumed job, then
          waits for it — meaningless if done in a subshell.
    bg:   sends SIGCONT to a stopped job's process group and updates the
          job table — must affect the interactive shell's state.
===============================================================================
*/

#include "../parser/parser.h"

/*
 * builtin_jobs
 *
 * Lists all jobs currently in the job table (running or stopped).
 * Ignores argv.
 *
 * Output format (one line per job):
 *   [1]+  Running    sleep 10 &
 *   [2]-  Stopped    vim notes.txt
 */
int builtin_jobs(Command *cmd);

/*
 * builtin_fg
 *
 * Brings a stopped or background job to the foreground.
 *
 * Usage: fg [%job_num]
 *   If no argument, resumes the most recently stopped/backgrounded job.
 *   If %N given, resumes job N.
 *
 * Steps:
 *   1. Find the job.
 *   2. Mark it JOB_RUNNING.
 *   3. SIGCONT → the job's process group.
 *   4. tcsetpgrp → give terminal to job.
 *   5. wait4(WUNTRACED) for all PIDs in the job.
 *   6. tcsetpgrp → return terminal to shell.
 *   7. If stopped again, mark JOB_STOPPED; else free the job.
 */
int builtin_fg(Command *cmd);

/*
 * builtin_bg
 *
 * Resumes a stopped job in the background.
 *
 * Usage: bg [%job_num]
 *   If no argument, resumes the most recently stopped job.
 *   If %N given, resumes job N.
 *
 * Steps:
 *   1. Find the job (must be JOB_STOPPED).
 *   2. Mark it JOB_RUNNING.
 *   3. SIGCONT → the job's process group.
 *   4. Print "[N]+ cmd &"
 *   Shell returns to the prompt immediately.
 */
int builtin_bg(Command *cmd);

#endif  /* JOBS_BUILTIN_H */
