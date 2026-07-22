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

/*
 * g_interactive
 *
 * Set to 1 in shell_main() only when the shell is attached to a terminal.
 * 0 in -c mode and script-file mode.
 */
int g_interactive = 0;

/*===========================================================================
 Environment overlay table

 Purpose:
   The kernel envp array (g_environ) is read-only.  To support export and
   unset builtins, we maintain a small static overlay of up to 64 entries.
   env_get() checks the overlay first; env_set() writes here.

 Format: each entry is a null-terminated "NAME=value" string in a fixed
   256-byte slot.  A leading '\0' in slot 0 means the slot is empty.
   A leading '=' means the variable is marked as unset (env_unset).
===========================================================================*/
#define ENV_OVERLAY_MAX   64
#define ENV_OVERLAY_SLOT  256

static char g_env_overlay[ENV_OVERLAY_MAX][ENV_OVERLAY_SLOT];
static int  g_env_overlay_count = 0;

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
    long name_len = my_strlen(name);

    /* Check overlay first — overrides kernel envp */
    for (int i = 0; i < g_env_overlay_count; i++)
    {
        const char *entry = g_env_overlay[i];
        if (entry[0] == '\0') continue;   /* empty slot */
        if (entry[0] == '=')              /* unset marker */
        {
            /* The variable name follows the '=' marker */
            if (my_strncmp(entry + 1, name, name_len) == 0
                && entry[1 + name_len] == '\0')
                return 0;   /* explicitly unset */
        }
        else if (my_strncmp(entry, name, name_len) == 0
                 && entry[name_len] == '=')
        {
            return entry + name_len + 1;
        }
    }

    /* Fall through to kernel envp */
    if (g_environ == 0) return 0;

    for (int i = 0; g_environ[i] != 0; i++)
    {
        const char *entry = g_environ[i];
        if (my_strncmp(entry, name, name_len) == 0
            && entry[name_len] == '=')
        {
            return entry + name_len + 1;
        }
    }

    return 0;
}

/*===========================================================================
 env_set — write or update a variable in the overlay table
===========================================================================*/
int env_set(const char *name, const char *value)
{
    long name_len  = my_strlen(name);
    long value_len = my_strlen(value);

    /* If total length won't fit in a slot, skip */
    if (name_len + 1 + value_len + 1 > ENV_OVERLAY_SLOT)
        return -1;

    /* Look for an existing entry to update */
    for (int i = 0; i < g_env_overlay_count; i++)
    {
        char *entry = g_env_overlay[i];
        if (entry[0] == '\0') continue;
        /* Match NAME= or =NAME (unset marker) */
        const char *cmp = (entry[0] == '=') ? entry + 1 : entry;
        if (my_strncmp(cmp, name, name_len) == 0
            && (cmp[name_len] == '=' || cmp[name_len] == '\0'))
        {
            /* Overwrite in place */
            int pos = 0;
            for (int k = 0; k < name_len; k++) entry[pos++] = name[k];
            entry[pos++] = '=';
            for (int k = 0; k < value_len; k++) entry[pos++] = value[k];
            entry[pos] = '\0';
            return 0;
        }
    }

    /* New entry */
    if (g_env_overlay_count >= ENV_OVERLAY_MAX) return -1;
    char *slot = g_env_overlay[g_env_overlay_count++];
    int pos = 0;
    for (int k = 0; k < name_len;  k++) slot[pos++] = name[k];
    slot[pos++] = '=';
    for (int k = 0; k < value_len; k++) slot[pos++] = value[k];
    slot[pos] = '\0';
    return 0;
}

/*===========================================================================
 env_unset — mark a variable as explicitly unset in the overlay
===========================================================================*/
void env_unset(const char *name)
{
    long name_len = my_strlen(name);

    /* Update existing overlay entry to unset marker */
    for (int i = 0; i < g_env_overlay_count; i++)
    {
        char *entry = g_env_overlay[i];
        if (entry[0] == '\0') continue;
        const char *cmp = (entry[0] == '=') ? entry + 1 : entry;
        if (my_strncmp(cmp, name, name_len) == 0
            && (cmp[name_len] == '=' || cmp[name_len] == '\0'))
        {
            /* Mark as unset: prefix '=' then name */
            entry[0] = '=';
            for (int k = 0; k < name_len; k++) entry[1 + k] = name[k];
            entry[1 + name_len] = '\0';
            return;
        }
    }

    /* Not in overlay yet — add an unset marker */
    if (g_env_overlay_count >= ENV_OVERLAY_MAX) return;
    char *slot = g_env_overlay[g_env_overlay_count++];
    slot[0] = '=';
    for (int k = 0; k < name_len; k++) slot[1 + k] = name[k];
    slot[1 + name_len] = '\0';
}
