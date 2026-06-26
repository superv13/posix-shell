#ifndef WRAPPERS_H
#define WRAPPERS_H

/*
===============================================================================
wrappers.h

Purpose:
    Declares the system call wrapper functions used by the educational POSIX
    shell.

Why this file exists:
    This project intentionally avoids libc and communicates directly with the
    Linux kernel. Instead of calling standard library functions such as
    printf() or exit(), the shell uses its own syscall wrappers.

Design principle:
    Shell logic must never invoke raw syscall instructions directly.
    Every kernel interaction passes through a dedicated wrapper function.

Benefits:
    - Keeps shell logic independent of syscall numbers
    - Improves readability and maintainability
    - Makes kernel interactions explicit and documentable
    - Supports the educational objective of the project

Current wrappers:
    sys_write() : Write bytes to a file descriptor.
    sys_exit()  : Terminate the current process.

Future wrappers:
    sys_read()
    sys_open()
    sys_close()
    sys_fork()
    sys_execve()
    sys_waitpid()
    sys_pipe()
    sys_dup2()
    sys_sigaction()

===============================================================================
*/

/*
Purpose:
    Writes data directly to a file descriptor using the Linux write syscall.

Parameters:
    fd    : Destination file descriptor.
    buf   : Buffer containing data to write.
    count : Number of bytes to write.
*/
void sys_write(
    int fd,
    const char *buf,
    long count
);


/* Read */

long sys_read(
    int fd,
    char *buf,
    long count
);

/*
Purpose:
    Terminates the current process using the Linux exit syscall.

Parameters:
    status : Exit status returned to the operating system.
*/
void sys_exit(
    int status
);

/*
===============================================================================
SYSTEM CALL: sys_getcwd

Purpose:
    Returns the absolute pathname of the current working directory.

Parameters:
    buffer : Destination buffer.
    size   : Size of the destination buffer.

Returns:
    >=0 : Number of bytes written.
     <0 : Linux error code.

===============================================================================
*/

long sys_getcwd(
    char *buffer,
    long size
);


/*
===============================================================================
SYSTEM CALL: sys_chdir

Purpose:
    Changes the current working directory.

Parameters:
    path : Target directory.

Returns:
     0 : Success.
    <0 : Linux error code.

===============================================================================
*/

long sys_chdir(
    const char *path
);

#endif