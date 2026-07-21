/*
 * nolibc_exit.c — Rung 5: The absolute syscall floor
 *
 * PURPOSE
 * -------
 * This is the minimal possible binary that Linux can execute.  It does
 * exactly one thing: call the exit_group syscall (NR 231) and terminate.
 *
 * No libc.  No CRT.  No shell features.  No signal setup.  Nothing.
 *
 * WHAT THIS PROVES
 * ----------------
 * strace -c ./nolibc_exit shows exactly 2 syscalls:
 *
 *   execve     — the kernel itself makes this call to load the binary
 *   exit_group — our one explicit syscall
 *
 * Those 2 are the IRREDUCIBLE minimum imposed by the Linux kernel on any
 * binary.  No program can do fewer.
 *
 * SIGNIFICANCE IN THE A1 LADDER
 * --------------------------------
 * Rung 4 (posixsh) shows N syscalls.
 * Rung 5 (nolibc_exit) shows 2 syscalls.
 *
 * The gap  N - 2  is posixsh's OWN deliberate shell-initialisation work:
 *
 *   getpid        — record shell PID for job control
 *   rt_sigaction  — install handlers for SIGINT, SIGQUIT, SIGTSTP,
 *                   SIGTTIN, SIGTTOU  (×5 calls)
 *   setpgid       — place shell in its own process group
 *
 * None of these is overhead; all are documented shell requirements.
 *
 * COMPILATION (matches Makefile rule)
 * ------------------------------------
 *   gcc -nostdlib -static -O0 -o nolibc_exit nolibc_exit.c
 *
 *   -nostdlib  : no CRT objects (no crt0.o, no libc)
 *   -static    : no dynamic linker
 *   -O0        : prevent any optimisation that might reorder or remove the asm
 *
 * INVOCATION (no arguments — just exits)
 * ----------------------------------------
 *   strace -c ./nolibc_exit
 *
 * Note: unlike all other rungs, there is NO "-c exit" argument.
 * This binary takes no arguments; it exits immediately on its own.
 *
 * SAME PATTERN AS posixsh
 * ------------------------
 * posixsh/runtime/start.c defines _start with __attribute__((naked)) and
 * inline asm to avoid a compiler-generated prologue corrupting rsp.
 * This file uses the identical pattern — but instead of calling shell_main,
 * it calls the exit syscall directly.
 */

/*
 * _start
 *
 * Entry point.  The naked attribute prevents GCC from emitting any
 * function prologue or epilogue, so the asm runs with rsp exactly as
 * the Linux kernel left it.
 *
 * x86-64 Linux syscall ABI:
 *   rax = syscall number
 *   rdi = arg1 (exit status)
 *   syscall instruction transfers control to the kernel
 *
 * SYS_exit_group = 231
 *   exit_group terminates all threads in the thread group.
 *   For a single-threaded program this is identical to SYS_exit (60),
 *   but exit_group is what glibc's exit() uses and what strace labels
 *   in its summary, so we use it for consistency.
 */
void __attribute__((naked)) _start(void)
{
    __asm__ volatile(
        /* rax = 231 (SYS_exit_group) */
        "mov    $231, %%rax\n\t"

        /* rdi = 0 (exit status 0) */
        "xor    %%rdi, %%rdi\n\t"

        /* transfer to kernel — does not return */
        "syscall\n\t"
        ::: "rax", "rdi", "memory"
    );
}
