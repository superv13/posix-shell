#ifndef ECHO_H
#define ECHO_H

#include "../parser/parser.h"

/*
 * builtin_echo
 *
 * POSIX-compliant echo builtin.
 *
 * Writes each argument to stdout separated by a single space, followed
 * by a newline.  No flag processing (-n, -e) — POSIX does not require
 * them for the basic echo builtin.
 *
 * Why this must be a builtin:
 *   External /usr/bin/echo costs a fork() + execve() + dynamic-linker
 *   startup.  Every echo in a script would trigger PATH probing
 *   (multiple open() = ENOENT calls) before finding /usr/bin/echo.
 *   As a builtin, echo runs zero fork/exec syscalls.
 */
void builtin_echo(Command *cmd);

#endif /* ECHO_H */
