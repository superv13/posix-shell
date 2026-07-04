#ifndef TRACE_H
#define TRACE_H

/*
===============================================================================
trace/trace.h — optional --trace diagnostic output

When the shell is invoked with --trace, every fork() and execve() call emits
a one-line [TRACE] message to stderr so the user can watch the execution
machinery.

Design:
    A single global int g_trace_mode is set to 1 at startup when --trace is
    present on the command line.  All trace calls are guarded by this flag so
    there is zero overhead when tracing is disabled.
===============================================================================
*/

/*
 * g_trace_mode
 *
 * Set to 1 if the shell was invoked with --trace.
 * Checked by every TRACE_* call below.
 */
extern int g_trace_mode;

/*
 * trace_fork
 *
 * Emits:  [TRACE] fork() -> <child_pid>
 *
 * Called from executor.c immediately after a successful fork().
 * child_pid is the value returned to the parent.
 */
void trace_fork(long child_pid);

/*
 * trace_execve
 *
 * Emits:  [TRACE] execve(<path>, ...)
 *
 * Called from executor.c immediately before execve().
 * path is the resolved executable path (e.g. /bin/echo).
 */
void trace_execve(const char *path);

/*
 * trace_sigaction
 *
 * Emits:  [TRACE] sigaction(signum, ...)
 *
 * Called from signals.c when installing a handler.
 * signum is the raw signal number.
 */
void trace_sigaction(int signum);

#endif  /* TRACE_H */
