//=============================================================================
// executor/path.c
//
// Purpose:
//   Resolves a command name to an executable absolute path.
//
// Phase 5 changes:
//   Replaced the hardcoded {"/bin", "/usr/bin"} search with a real PATH
//   environment variable lookup using env_get("PATH").
//
//   Why this matters:
//     On Debian/Ubuntu, many commands live in /usr/bin and /usr/sbin.
//     On Arch Linux or Alpine, /bin is a symlink to /usr/bin.
//     On systems with Homebrew or conda, commands may live in
//     /usr/local/bin, /opt/homebrew/bin, or ~/miniconda3/bin.
//     Hard-coding /bin and /usr/bin breaks on all of these systems for
//     any command that is not in those exact paths.
//
//   The fix: read PATH from g_environ, parse it as colon-separated
//   directory entries (POSIX XBD 8.3), and try each one in order.
//   Fall back to "/bin:/usr/bin:/usr/local/bin" if PATH is unset.
//=============================================================================

#include "path.h"

#include "../include/wrappers.h"
#include "../utils/string.h"
#include "../include/constants.h"
#include "../env/env.h"

/*
 * Fallback PATH used when the environment has no PATH variable.
 *
 * POSIX XBD 8.3: "If PATH is unset or is set to null, the path search
 * is implementation-defined."  We use a minimal but broadly useful default.
 */
#define DEFAULT_PATH "/bin:/usr/bin:/usr/local/bin"

/*
===============================================================================
FUNCTION: build_path

Purpose:
    Concatenates  directory + "/" + command  into dest.

Example:
    build_path(dest, "/usr/local/bin", "python3")
    → dest = "/usr/local/bin/python3"
===============================================================================
*/

static void build_path(
    char *dest,
    const char *dir,
    const char *cmd
)
{
    while (*dir)
    {
        *dest++ = *dir++;
    }

    *dest++ = '/';

    while (*cmd)
    {
        *dest++ = *cmd++;
    }

    *dest = '\0';
}


/*
===============================================================================
FUNCTION: contains_slash

Purpose:
    Returns 1 if command already contains a '/', meaning the user supplied
    an explicit path ("./hello", "/usr/bin/python", "../prog").

    If true, find_executable() returns the command unchanged — no PATH
    search is performed.
===============================================================================
*/

static int contains_slash(
    const char *command
)
{
    while (*command)
    {
        if (*command == '/')
        {
            return 1;
        }

        command++;
    }

    return 0;
}

/*
===============================================================================
FUNCTION: file_exists

Purpose:
    Checks whether a file exists and is openable by attempting sys_open().

Why we use open() instead of access() or stat():
    - access() is not in our wrapper set.
    - stat() requires a stat struct we have not defined.
    - open() with O_RDONLY is a clean, minimal probe that works for any
      file our UID can execute (if we can read it, the kernel will let us
      exec it — the X bit is checked by execve() anyway).

Limitation:
    This will return 1 for files that are readable but not executable.
    execve() will then fail with EACCES (exit 126), which is the POSIX-
    specified "found but not executable" error code.
===============================================================================
*/

static int file_exists(
    const char *path
)
{
    long fd =
        sys_open(
            path,
            O_RDONLY,
            0
        );

    if (fd < 0)
    {
        return 0;
    }

    sys_close(fd);

    return 1;
}

/*
===============================================================================
FUNCTION: find_executable

Purpose:
    Resolves a command name to an executable path using the shell's PATH.

Phase 5 algorithm:

    1. If command contains '/', use it directly (absolute or relative path).

    2. Read PATH from g_environ via env_get("PATH").
       Fall back to DEFAULT_PATH if PATH is unset.

    3. Parse PATH as a colon-separated list of directories (POSIX XBD 8.3).
       For each non-empty directory entry:
         a. Build candidate = directory + "/" + command.
         b. If the candidate exists, return it.

    4. If no candidate is found, return NULL.

POSIX notes on PATH parsing:
    - Empty entries ("::") represent the current directory.  We skip empty
      entries since executing commands from CWD without an explicit "./"
      is a security risk (POSIX allows but does not require shells to
      support it; most modern shells disable it by default).
    - Leading and trailing colons also represent CWD — same treatment.
    - PATH entries longer than MAX_PATH_LEN - 1 - strlen(command) - 1 are
      silently skipped (would overflow the static buffer).

Returns:
    Pointer to a static buffer containing the executable path.
    NULL if the command cannot be found.

Thread safety:
    Not thread-safe (static buffer).  Acceptable — this shell is single-
    threaded.
===============================================================================
*/

char *find_executable(
    const char *command
)
{
    static char path[MAX_PATH_LEN];

    /*
     * Step 1: explicit path — return unchanged, no PATH search.
     */
    if (contains_slash(command))
    {
        return (char *)command;
    }

    /*
     * Step 2: read PATH from the environment.
     *
     * env_get() searches g_environ, the real envp captured from the
     * kernel stack at startup.  Returns NULL if PATH is unset.
     */
    const char *path_env = env_get("PATH");

    if (path_env == 0)
    {
        /*
         * PATH is unset.  POSIX says the search is implementation-defined.
         * We use a safe, broad default that works on most Linux systems.
         */
        path_env = DEFAULT_PATH;
    }

    /*
     * Step 3: walk the colon-separated PATH.
     *
     * We copy each segment into a local buffer so that build_path() can
     * produce "segment/command" without modifying path_env (which points
     * directly into g_environ and must not be modified).
     */
    const char *p = path_env;

    while (*p != '\0')
    {
        /*
         * Copy one colon-separated segment into dir[].
         *
         * The loop runs until ':' or '\0'.  MAX_PATH_LEN is the upper
         * bound; we leave room for '/' + command + '\0'.
         */
        char dir[MAX_PATH_LEN];
        int  dir_len = 0;

        while (*p != '\0' && *p != ':' && dir_len < MAX_PATH_LEN - 1)
        {
            dir[dir_len++] = *p++;
        }

        dir[dir_len] = '\0';

        if (*p == ':')
        {
            p++;    /* skip the colon separator */
        }

        /*
         * Skip empty segments ("::").
         *
         * An empty segment officially means "current directory", but we
         * skip it for security.  Users who want CWD must use "./command".
         */
        if (dir_len == 0)
        {
            continue;
        }

        /*
         * Build "dir/command" and check whether it exists.
         *
         * Guard against overflow: the final path must fit in MAX_PATH_LEN
         * bytes including the '/' separator and null terminator.
         */
        long cmd_len = my_strlen(command);
        if ((long)dir_len + 1 + cmd_len + 1 > MAX_PATH_LEN)
        {
            continue;   /* would overflow — skip */
        }

        build_path(path, dir, command);

        if (file_exists(path))
        {
            return path;
        }
    }

    return 0;   /* command not found in PATH */
}
