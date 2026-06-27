//=============================================================================
// path.c
//
// Purpose:
//   Resolves a command name to an executable path.
//
// Why this file exists:
//   The Linux execve() system call requires the pathname of an executable.
//   Users, however, typically enter only the command name (e.g. "ls").
//   This module searches a set of standard executable directories and
//   returns the first matching executable.
//
// Current implementation:
//   - Searches only /bin and /usr/bin.
//   - Does not yet use the PATH environment variable.
//   - If the command already contains '/', it is returned unchanged.
//
// Future work:
//   - Parse PATH from the environment.
//   - Search all PATH directories.
//   - Cache frequently used paths.
//
//=============================================================================

#include "path.h"

#include "../include/wrappers.h"
#include "../utils/string.h"
#include "../include/constants.h"



/*
===============================================================================
FUNCTION: build_path

Purpose:
    Concatenates

        directory + "/" + command

Example:

    "/bin"
        +
    "ls"

becomes

    "/bin/ls"

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
    Determines whether the command already specifies a pathname.

Examples:

    ls          -> 0
    ./hello     -> 1
    ../prog     -> 1
    /bin/ls     -> 1

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
    Checks whether an executable path exists.

Current implementation:
    Attempts to open the file.

Returns:
    1 : Exists
    0 : Does not exist

===============================================================================
*/

static int file_exists(
    const char *path
)
{
    long fd =
        sys_open(
            path,
            0,
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
    Searches standard executable directories.

Search order:

    1. Absolute/relative path (contains '/')
    2. /bin
    3. /usr/bin

Returns:
    Pointer to a static buffer containing the executable path.

Returns NULL if the command cannot be found.

===============================================================================
*/

char *find_executable(
    const char *command
)
{
    static char path[MAX_PATH_LEN];

    static const char *directories[] =
    {
        "/bin",
        "/usr/bin"
    };

    /*
     * User already supplied a pathname.
     */

    if (contains_slash(command))
    {
        return (char *)command;
    }

    /*
     * Search common executable directories.
     */

    for (int i = 0; i < 2; i++)
    {
        build_path(
            path,
            directories[i],
            command
        );

        if (file_exists(path))
        {
            return path;
        }
    }

    return 0;
}