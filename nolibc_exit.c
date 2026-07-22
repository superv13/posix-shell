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
 * strace -c ./nolibc_exit records exactly 1 completed syscall in summary mode:
 *
 *   execve(2)  — the kernel process-invocation and ELF-loading call
 *
 * Methodological Note on exit_group(231):
 *   Our code explicitly executes exit_group(0) via inline assembly.
 *   However, because exit_group terminates the process without returning,
 *   strace -c does not record a return event and therefore omits it from
 *   the summary count.  The call is real; the summary pairing cannot tally
 *   it.  This is a well-known strace -c accounting property, not a bug.
 *
 * SIGNIFICANCE IN THE A1 LADDER
 * --------------------------------
 * Rung 4 (posixsh) shows N syscalls.
 * Rung 5 (nolibc_exit) shows 1 syscall.
 *
 * The gap  N - 1  is posixsh's OWN deliberate shell-initialisation work:
 *
 *   getpid        — record shell PID for $$ variable and job control
 *   rt_sigaction  — install signal dispositions  (×6 calls):
 *                     SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU → SIG_IGN
 *                     SIGCHLD → custom background-job reaper handler
 *
 * None of these is overhead; all are documented POSIX shell requirements.
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
