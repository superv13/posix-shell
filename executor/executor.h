#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
===============================================================================
executor/executor.h — Phase 4 execution engine interface
===============================================================================
*/

#include "../parser/parser.h"

/*
 * execute_pipeline
 *
 * Executes a fully parsed Pipeline.
 *
 * Phase 4 behaviour:
 *   - Each pipeline gets its own process group (PGID = first child PID).
 *   - Foreground pipelines receive terminal control via tcsetpgrp.
 *   - Ctrl+Z (SIGTSTP) is detected via WUNTRACED; job is moved to table.
 *   - Background pipelines are added to the job table.
 *   - Terminal is always restored to the shell after foreground jobs.
 *   - Children reset signal handlers to SIG_DFL before exec.
 *
 * Returns:
 *    0  : success
 *   -1  : a syscall failed (fork, pipe, etc.)
 *   other: last child's exit status
 */
int execute_pipeline(Pipeline *pipeline);

#endif  /* EXECUTOR_H */
