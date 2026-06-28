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

/*
===============================================================================
SYSTEM CALL: sys_fork

Purpose:
    Creates a new child process by duplicating the calling process.

Why this wrapper exists:
    The educational POSIX shell avoids libc and invokes Linux system calls
    directly. This wrapper provides a clean interface between shell logic
    and the architecture-specific syscall layer.

Returns:
     0 : Returned in the child process.
    >0 : Child process ID (PID) returned in the parent process.
    <0 : Linux error code.

Educational note:
    Both parent and child continue execution from the instruction
    immediately following sys_fork(). The return value distinguishes
    between the two processes.

===============================================================================
*/

long sys_fork(void);


/*
===============================================================================
SYSTEM CALL: sys_execve

Purpose:
    Replaces the current process image with a new executable.

Parameters:
    pathname : Path of the executable.
    argv     : Argument vector.
    envp     : Environment variables.

Returns:
    Never returns on success.

    <0 on failure.

Educational note:
    On success, execution never returns to the caller because the current
    process image is completely replaced by the new program.

===============================================================================
*/

long sys_execve(
    const char *pathname,
    char *const argv[],
    char *const envp[]
);

/*
===============================================================================
SYSTEM CALL: sys_wait4

Purpose:
    Waits for a child process to terminate.

Why this wrapper exists:
    The educational POSIX shell invokes the Linux wait4() system call directly
    without relying on libc.

Parameters:
    pid     : Child process ID to wait for.
    status  : Pointer to receive the child's exit status.
    options : Wait options (normally 0).

Returns:
    >0 : PID of the terminated child.
    <0 : Linux error code.

Educational note:
    This wrapper is used by the shell to wait for foreground commands before
    displaying the next prompt.

===============================================================================
*/

long sys_wait4(
    long pid,
    int *status,
    int options
);

/*
===============================================================================
SYSTEM CALL: sys_open

Purpose:
    Opens a file and returns a file descriptor.

Parameters:
    pathname : Path to the file.
    flags    : Open flags.
    mode     : File permissions when creating a file.

Returns:
    >=0 : File descriptor.
     <0 : Linux error code.

===============================================================================
*/

long sys_open(
    const char *pathname,
    int flags,
    int mode
);


/*
===============================================================================
SYSTEM CALL: sys_close

Purpose:
    Closes an open file descriptor.

Parameters:
    fd : File descriptor.

Returns:
     0 : Success.
    <0 : Linux error code.

===============================================================================
*/

long sys_close(
    long fd
);

/*
===============================================================================
SYSTEM CALL: sys_pipe

Purpose:
    Creates a unidirectional data channel (a pipe) that can be used for
    interprocess communication.

Why this wrapper exists:
    Pipeline construction ("cmd1 | cmd2") requires connecting the stdout of
    one process to the stdin of the next. The kernel pipe() syscall creates
    a pair of connected file descriptors for exactly this purpose.

Parameters:
    pipefd : Caller-provided array of 2 ints. On success:
                 pipefd[0] -> read end
                 pipefd[1] -> write end

Returns:
     0 : Success.
    <0 : Linux error code.

Educational note:
    Both ends must eventually be dup2()'d into place (stdin/stdout) and the
    originals closed, or file descriptors leak and pipes never reach EOF.

===============================================================================
*/

long sys_pipe(
    int pipefd[2]
);

/*
===============================================================================
SYSTEM CALL: sys_dup2

Purpose:
    Duplicates a file descriptor onto a specific, chosen descriptor number,
    closing the target first if it was already open.

Why this wrapper exists:
    Redirection ("cmd > file") and pipelines both work by making file
    descriptor 0 (stdin) or 1 (stdout) point at something other than the
    terminal, *before* execve() replaces the process image. dup2() is the
    standard mechanism for this.

Parameters:
    oldfd : Existing, open file descriptor.
    newfd : Target descriptor number (usually 0 or 1).

Returns:
    >=0 : newfd on success.
     <0 : Linux error code.

===============================================================================
*/

long sys_dup2(
    long oldfd,
    long newfd
);

#endif