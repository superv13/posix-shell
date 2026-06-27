#ifndef PATH_H
#define PATH_H

/*
===============================================================================
FUNCTION: find_executable

Purpose:
    Searches common executable directories for a command.

Parameters:
    command : Command entered by the user.

Returns:
    Pointer to a static buffer containing the executable path,
    or NULL if the command cannot be found.

Current search order:
    /bin
    /usr/bin

Future:
    Search directories listed in the PATH environment variable.

===============================================================================
*/

char *find_executable(
    const char *command
);

#endif