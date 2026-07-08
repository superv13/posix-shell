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
      shell_main(argc, argv, envp)
          ↓
        sys_exit()

How _start reads argc / argv / envp:
    On x86-64 Linux, at process entry the kernel lays out the initial
    stack as follows (lower address at top):

        [rsp + 0]              argc            (long)
        [rsp + 8]              argv[0]         (char *)
        [rsp + 16]             argv[1]
        ...
        [rsp + 8*(argc+1)]     NULL            ← argv sentinel
        [rsp + 8*(argc+2)]     envp[0]         (char *)
        ...
        [rsp + ...]            NULL            ← envp sentinel

    We read these with a tiny __asm__ block in _start:

        pop  %rdi                    ← argc  (decrements rsp by 8)
        mov  %rsp, %rsi              ← argv  (rsp now points to argv[0])
        lea  8(%rsp,%rdi,8), %rdx   ← envp  (rsp + argc*8 + 8)

    The lea computes: rsi + argc*8 + 8
                    = argv + argc*8 + 8
                    = &argv[argc+1]
                    = &envp[0]

    This is a direct read of the kernel-supplied data — no libc needed.

Phase 5 change:
    _start now extracts envp from the initial stack and passes it to
    shell_main() as a third argument (rdx in the System V AMD64 ABI).
    shell_main() stores it in g_environ (env/env.c) so every execve()
    call can pass the real environment to child processes.
===============================================================================
*/

/* Forward declarations */
void shell_main(int argc, char **argv, char **envp);
void sys_exit(int status);

/*
 * _start
 *
 * Entry point executed directly by the Linux kernel.
 *
 * Reads argc, argv, and envp from the initial stack layout, then calls
 * shell_main with all three.  Never returns (shell_main calls sys_exit).
 *
 * Why __attribute__((naked)):
 *   A normal C function emits a prologue (push %rbp / mov %rsp,%rbp) that
 *   corrupts rsp before we can read it.  The naked attribute suppresses
 *   the prologue so the inline asm runs with rsp exactly as the kernel
 *   left it.
 */
void __attribute__((naked)) _start(void)
{
    __asm__ volatile(
        /*
         * x86-64 System V ABI calling convention:
         *   arg1 → rdi
         *   arg2 → rsi
         *   arg3 → rdx
         */

        /* ── argc ──────────────────────────────────────────────────────── */
        /*
         * pop %rdi
         *   Reads argc from [rsp] and stores it in rdi (arg1).
         *   rsp is then incremented by 8, now pointing to argv[0].
         */
        "pop    %%rdi\n\t"

        /* ── argv ──────────────────────────────────────────────────────── */
        /*
         * mov %rsp, %rsi
         *   rsp currently points to argv[0].
         *   Storing rsp in rsi gives shell_main the argv pointer (arg2).
         */
        "mov    %%rsp, %%rsi\n\t"

        /* ── envp ──────────────────────────────────────────────────────── */
        /*
         * lea 8(%rsp,%rdi,8), %rdx
         *
         *   Computes: rsp + rdi*8 + 8
         *           = argv[0] address + argc*8 + 8
         *           = address of argv[argc+1]     (past the NULL sentinel)
         *           = address of envp[0]
         *
         *   This is phase 5's key addition: we capture the envp pointer
         *   from the kernel stack so we can pass the real environment to
         *   every child process via execve(path, argv, g_environ).
         */
        "lea    8(%%rsp,%%rdi,8), %%rdx\n\t"

        /* ── call shell_main(argc, argv, envp) ─────────────────────────── */
        "call   shell_main\n\t"

        /* ── exit ──────────────────────────────────────────────────────── */
        /*
         * shell_main's return value is in rax.
         * Move it to rdi (first arg to sys_exit) and call sys_exit.
         * sys_exit calls the exit syscall, so _start truly never returns.
         */
        "mov    %%rax, %%rdi\n\t"
        "call   sys_exit\n\t"
        ::: "memory"
    );
}
