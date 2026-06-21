// shell/shell_loop.c
#include "../include/wrappers.h"
#include "../utils/string.h"
#include "../builtins/builtins.h"
/*
===============================================================================
shell_loop.c

Purpose:
    Implements the first executable component of the educational POSIX shell.

Why this file exists:
    This file demonstrates that our custom runtime environment is functional.
    It verifies that execution successfully flows from the Linux kernel to
    our custom _start() function and then into shell code without using libc.

Current behaviour:
    1. Linux kernel starts the executable.
    2. _start() transfers control to shell_main().
    3. shell_main() prints a message using sys_write().
    4. The program exits through sys_exit().

Educational objective:
    This is NOT yet a shell. It is a runtime validation step used to confirm
    that direct kernel interaction is working correctly.

Future responsibilities:
    shell_main() will evolve into the main shell loop responsible for:
        - displaying a prompt
        - reading user input
        - tokenising commands
        - parsing commands
        - executing commands
        - managing jobs and signals

Execution flow:

    Linux kernel
          ↓
       _start()
          ↓
      shell_main()
          ↓
      sys_write()
          ↓
    Linux kernel

===============================================================================
*/

void shell_main(void)
{   
    char buffer[1024];
    while(1){
        char prompt[] = "posixsh> ";

        /*Display prompt*/
        sys_write(1, prompt, my_strlen(prompt)-1);

        /*Read keyboardd input*/
        long bytes = sys_read(0, buffer, sizeof(buffer)-1);

        /*stop on EOF or Error*/
        if(bytes <= 0) break;

        buffer[bytes] = '\0';

        if(execute_builtin(buffer))
        {
            break;
        }

        /*Echo user input*/
        sys_write(1, buffer, bytes);
    }
}