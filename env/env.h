#ifndef ENV_H
#define ENV_H

/*
===============================================================================
env/env.h — shell environment and per-command exit-status globals

Why this module exists:
    Phase 5 adds three capabilities that require shared global state:

    1. g_environ  : The real envp array captured from the kernel stack at
                    startup (_start) and passed through to every execve()
                    call, so child processes inherit PATH, HOME, TERM, etc.

    2. env_get()  : Searches g_environ for a named variable, returning the
                    value string.  Used by path.c to read PATH and by any
                    future builtin that needs HOME, TERM, etc.

    3. g_last_status : The processed exit code (0–255, or 128+signum for
                    killed-by-signal) of the most recently completed
                    foreground pipeline.  Exposed here so the tokenizer can
                    expand $? without including executor headers.

Why not in signals.h or constants.h:
    signals.h already carries g_shell_pgid and the signal infrastructure.
    Merging environment state there would blur the separation of concerns.
    This file is the single authoritative header for shell-wide state that
    neither belongs to the kernel layer nor to the parser.
===============================================================================
*/

/*
 * g_environ
 *
 * Pointer to the NULL-terminated envp array received from the Linux kernel
 * at process startup (via the initial stack layout _start reads).
 *
 * Lifecycle:
 *   Set once in shell_main() immediately after _start calls it.
 *   Read-only after that.
 *   Passed verbatim to every sys_execve() call so children inherit the
 *   full environment the shell itself was started with.
 *
 * NULL when the shell somehow received no environment (rare; defensive
 * checks in env_get() handle this case).
 */
extern char **g_environ;

/*
 * g_last_status
 *
 * The exit code of the last foreground pipeline, in POSIX $? format:
 *   0–255    : normal exit (WEXITSTATUS of the last pipeline stage)
 *   128+sig  : killed by signal sig (WTERMSIG, following POSIX convention)
 *   0        : background launch succeeded (POSIX: async cmd sets $? = 0)
 *   0        : builtin executed successfully
 *
 * Updated by executor.c after every foreground pipeline wait loop.
 * Read by tokenizer.c when expanding $? in user input.
 *
 * Why here and not in executor.h:
 *   tokenizer.c must include this file to expand $?.  Including executor.h
 *   from tokenizer.c would create a circular dependency (executor.h includes
 *   parser.h which is the tokenizer's own sibling).
 */
extern int g_last_status;

/*
 * g_interactive
 *
 * 1 when the shell is running interactively (stdin is a tty).
 * 0 in script file mode or -c mode.
 * Used to suppress the prompt and "[N] PID" background job messages
 * in non-interactive contexts (e.g. compliance test runner).
 */
extern int g_interactive;

/*
 * env_set(name, value)
 *
 * Sets or updates an environment variable in the shell's overlay table.
 * Subsequent env_get() calls will return the new value.
 * Returns 0 on success, -1 if the overlay table is full.
 */
int env_set(const char *name, const char *value);

/*
 * env_unset(name)
 *
 * Removes a variable from the overlay table (and marks it absent).
 * After env_unset(), env_get() returns NULL for that name.
 */
void env_unset(const char *name);

/*
===============================================================================
FUNCTION: env_get

Purpose:
    Searches the shell's environment array (g_environ) for a variable with
    the given name and returns a pointer to its value string.

Parameters:
    name : Variable name to look for, e.g. "PATH", "HOME", "TERM".
           Must be null-terminated.  Must NOT contain '='.

Returns:
    Pointer to the value string (the part after '=' in the entry).
    NULL if the variable is not found, or if g_environ is NULL.

Why this exists rather than getenv():
    getenv() is a libc function; this project has zero libc dependency.
    env_get() does exactly what getenv() does, using only our own string
    utilities and a direct scan of the envp array.

Example:
    const char *path = env_get("PATH");
    // path now points into g_environ, e.g. "/usr/local/sbin:/usr/local/bin:..."
===============================================================================
*/
const char *env_get(const char *name);

#endif  /* ENV_H */
