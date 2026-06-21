/*
===============================================================================
start.c

Purpose:
    Defines the custom program entry point (_start) for the educational
    POSIX shell.

Why this file exists:
    In a normal C program, Linux does not directly execute main(). The
    standard C runtime (crt0 + libc) performs initialization and then
    calls main().

    Since this project intentionally avoids libc, we must provide our own
    entry point.

Execution flow:

    Normal C program:

        Linux kernel
              ↓
        _start() (provided by libc)
              ↓
        __libc_start_main()
              ↓
             main()

    This project:

        Linux kernel
              ↓
           _start()
              ↓
         shell_main()
              ↓
          sys_exit()

Educational objective:
    This file exposes the hidden startup sequence that is normally managed
    by libc and gives complete control over the shell runtime.

Future extensions:
    _start() will later be responsible for extracting:
        - argc
        - argv
        - environment variables (envp)

===============================================================================
*/

/* Forward declarations */
void shell_main(void);
void sys_exit(int status);

/*
Purpose:
    Entry point executed directly by the Linux kernel.

Current behaviour:
    Transfers control to shell_main() and terminates the process when
    shell execution finishes.

Why sys_exit() is used:
    Without libc, returning from _start() is unsafe because there is no
    runtime environment to receive the return value. Process termination
    must be explicitly requested from the kernel.
*/
void _start(void)
{
    shell_main();

    sys_exit(0);
}