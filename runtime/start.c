/*
===============================================================================
runtime/start.c — custom program entry point

Why this file exists:
    In a normal C program, Linux does not directly execute main().  The
    standard C runtime (crt0 + libc) performs initialization and then
    calls main().

    Since this project intentionally avoids libc, we must provide our own
    entry point.

Execution flow:

    Linux kernel
          ↓
       _start()          ← this file
          ↓
      shell_main(argc, argv)
          ↓
        sys_exit()

How _start reads argc/argv:
    On x86-64 Linux, at process entry the kernel pushes onto the stack:
        [rsp+0]   argc          (long)
        [rsp+8]   argv[0]       (char *)
        [rsp+16]  argv[1] ...
        ...
        [rsp+8*(argc+1)]  NULL  (argv sentinel)

    We read these with a tiny __asm__ that copies rsp into a local
    pointer, then index into it as a long array.
===============================================================================
*/

/* Forward declarations */
void shell_main(int argc, char **argv);
void sys_exit(int status);

/*
 * _start
 *
 * Entry point executed directly by the Linux kernel.
 *
 * Reads argc and argv from the initial stack layout, then calls shell_main.
 */
void __attribute__((naked)) _start(void)
{
    __asm__ volatile(
        /* rsp points to [argc, argv[0], argv[1], ..., NULL]   */
        "pop    %%rdi\n\t"       /* rdi = argc                  */
        "mov    %%rsp, %%rsi\n\t"/* rsi = &argv[0]              */
        "call   shell_main\n\t"
        "mov    %%rax, %%rdi\n\t"/* exit status = return value  */
        "call   sys_exit\n\t"
        ::: "memory"
    );
}