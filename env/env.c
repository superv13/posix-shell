//=============================================================================
// env/env.c — shell environment globals and env_get()
//
// Phase 5 additions:
//   g_environ     : pointer to the envp array from the kernel stack
//   g_last_status : processed exit code of the last foreground pipeline
//   env_get()     : linear search of g_environ for a named variable
//
// Why no hash table:
//   A typical shell environment has under 100 variables.  Linear search
//   over 100 null-terminated strings costs a few microseconds at most,
//   and is simpler than a hash table that would require heap allocation
//   or a large static array.  This keeps the zero-malloc architecture intact.
//=============================================================================

#include "env.h"
#include "../utils/string.h"

/*===========================================================================
 Global definitions
===========================================================================*/

/*
 * g_environ
 *
 * Initialised to NULL; set to the actual envp by shell_main() on the first
 * line of execution (before any command runs), so it is always valid by the
 * time a command needs it.
 */
char **g_environ = 0;

/*
 * g_last_status
 *
 * Starts at 0 (as if a successful no-op was run before the first prompt).
 * Updated by executor.c after every foreground pipeline's wait loop.
 * Builtins leave it at 0 (success) unless they explicitly set it.
 */
int g_last_status = 0;

/*===========================================================================
 env_get
===========================================================================*/

/*
 * env_get
 *
 * Walks the g_environ array, which has the layout:
 *
 *   g_environ[0] = "HOME=/home/user"
 *   g_environ[1] = "PATH=/usr/bin:/bin"
 *   g_environ[2] = "TERM=xterm-256color"
 *   g_environ[3] = NULL                 ← sentinel
 *
 * For each entry, we check whether the first name_len characters match
 * `name` AND the character at position name_len is '=' (not a longer name
 * that shares a prefix, e.g. "PATHEXT" must not match "PATH").
 *
 * This is functionally equivalent to POSIX getenv(), without libc.
 *
 * Kernel interaction: none (pure memory scan).
 */
const char *env_get(
    const char *name
)
{
    if (g_environ == 0)
    {
        return 0;
    }

    long name_len = my_strlen(name);

    for (int i = 0; g_environ[i] != 0; i++)
    {
        const char *entry = g_environ[i];

        /*
         * Check: first name_len chars match AND the next char is '='.
         *
         * my_strncmp returns 0 if the first name_len chars are equal.
         * We must also confirm entry[name_len] == '=' to avoid matching
         * "PATHSOMETHING" when searching for "PATH".
         */
        if (my_strncmp(entry, name, name_len) == 0
            && entry[name_len] == '=')
        {
            /*
             * Return a pointer directly into g_environ's string,
             * starting after the '=' character.
             *
             * The caller must NOT write through this pointer — it points
             * into the original envp passed to us by the kernel.
             */
            return entry + name_len + 1;
        }
    }

    return 0;   /* not found */
}
