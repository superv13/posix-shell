#ifndef X86_64_ARCH_H
#define X86_64_ARCH_H

/*
===============================================================================
arch/x86_64/arch.h — Linux x86-64 architecture layer

Educational POSIX Shell

Purpose:
    Encapsulates every x86-64 specific detail required by the shell.

Responsibilities:
    1. Linux x86-64 syscall numbers
    2. Calling convention (register assignments, instruction)
    3. syscall0() ... syscall6() inline wrappers
    4. arch_signal_restorer() — trampoline required by rt_sigaction

Design principle:
    Every architecture-specific detail lives here and only here.
    Porting to ARM64 or RISC-V means writing one new file like this one
    and leaving every other file in the project unchanged.

x86-64 Linux syscall ABI:
    Instruction  : syscall
    Number       : rax
    Arguments    : rdi  rsi  rdx  r10  r8  r9
    Return value : rax
    Clobbered    : rcx  r11
===============================================================================
*/

/*===========================================================================
 Linux x86-64 syscall numbers
 Reference: linux/arch/x86/entry/syscalls/syscall_64.tbl
===========================================================================*/

/* Helper: turn a macro value into a string literal in asm */
#define stringify_literal(x)  #x
#define stringify(x)          stringify_literal(x)

/* I/O */
#define SYS_read             0
#define SYS_write            1
#define SYS_open             2
#define SYS_close            3
#define SYS_ioctl           16   /* Used for tcgetpgrp / tcsetpgrp          */

/* Pipes & file descriptor duplication */
#define SYS_pipe            22
#define SYS_dup2            33

/* Process */
#define SYS_getpid          39
#define SYS_fork            57
#define SYS_execve          59
#define SYS_exit            60
#define SYS_wait4           61

/* Signals */
#define SYS_kill            62
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_rt_sigreturn    15   /* Used by arch_signal_restorer()           */

/* Process groups */
#define SYS_setpgid        109
#define SYS_getpgid        121
#define SYS_setsid         112   /* Create new session (for PTY job control) */

/* Directory */
#define SYS_getcwd          79
#define SYS_chdir           80


/*===========================================================================
 Raw syscall invocation
===========================================================================*/

/* 0 arguments */
static inline __attribute__((always_inline))
long syscall0(long number)
{
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 1 argument */
static inline __attribute__((always_inline))
long syscall1(long number, long arg1)
{
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 2 arguments */
static inline __attribute__((always_inline))
long syscall2(long number, long arg1, long arg2)
{
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 3 arguments */
static inline __attribute__((always_inline))
long syscall3(long number, long arg1, long arg2, long arg3)
{
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 4 arguments */
static inline __attribute__((always_inline))
long syscall4(long number, long arg1, long arg2, long arg3, long arg4)
{
    long ret;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 5 arguments */
static inline __attribute__((always_inline))
long syscall5(long number,
              long arg1, long arg2, long arg3, long arg4, long arg5)
{
    long ret;
    register long r10 __asm__("r10") = arg4;
    register long r8  __asm__("r8")  = arg5;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 6 arguments */
static inline __attribute__((always_inline))
long syscall6(long number,
              long arg1, long arg2, long arg3, long arg4, long arg5, long arg6)
{
    long ret;
    register long r10 __asm__("r10") = arg4;
    register long r8  __asm__("r8")  = arg5;
    register long r9  __asm__("r9")  = arg6;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3),
          "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}


/*===========================================================================
 Signal return trampoline

 Purpose:
     When the kernel delivers a signal it pushes a frame onto the process
     stack and transfers control to the signal handler.  When the handler
     returns, execution lands here.  We call SYS_rt_sigreturn so the
     kernel can restore the original register state and resume normal
     execution.

 Why this is needed without libc:
     libc normally supplies this trampoline (called __restore_rt) and
     passes its address to the kernel via sa_restorer.  Without libc we
     must supply our own.  Skipping it causes a segfault on handler return
     because the kernel has nowhere valid to return to.

 Architecture note:
     This function is intentionally placed in the arch header because the
     implementation (syscall number + instruction) is architecture-specific.
     On ARM64 the equivalent calls SYS_rt_sigreturn (139) via svc #0.
===========================================================================*/

/*
 * arch_signal_restorer
 *
 * MUST be declared naked — no prologue, no epilogue, zero stack manipulation.
 *
 * Why naked is critical:
 *   When a signal is delivered, the kernel pushes a ucontext_t signal frame
 *   onto the process stack.  The handler runs with the stack pointing AT that
 *   frame.  When the handler returns, execution arrives here.  rt_sigreturn
 *   reads the frame from the CURRENT stack pointer.
 *
 *   A normal (non-naked) C function emits:
 *       push   %rbp          ← corrupts the frame (shifts rsp by 8)
 *       mov    %rsp, %rbp
 *       ...body...
 *       pop    %rbp
 *       ret
 *
 *   That push shifts rsp by 8 bytes before rt_sigreturn runs, so the kernel
 *   reads garbage instead of the saved register state → SIGSEGV.
 *
 *   With __attribute__((naked)) the compiler emits ONLY the inline asm,
 *   preserving the exact stack layout the kernel expects.
 */
__attribute__((naked))
static void arch_signal_restorer(void)
{
    __asm__ volatile(
        "movl $" stringify(SYS_rt_sigreturn) ", %%eax\n\t"
        "syscall\n\t"
        ::: "memory"
    );
}

#endif  /* X86_64_ARCH_H */
