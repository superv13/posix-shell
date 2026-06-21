#ifndef X86_64_ARCH_H
#define X86_64_ARCH_H

/*
===============================================================================
Linux x86_64 architecture abstraction layer

Educational POSIX Shell

Purpose:
    Encapsulates all x86_64 specific functionality required by the shell.

Responsibilities:
    1. Linux syscall numbers
    2. Linux x86_64 syscall ABI
    3. syscall0() ... syscall6()

Design principle:
    Every architecture-specific implementation must remain inside this file.

Educational objective:
    Porting the shell to a new architecture should require implementing
    only one new architecture file while leaving the rest of the shell
    unchanged.

===============================================================================
*/

/*===========================================================================
 Linux x86_64 syscall numbers
===========================================================================*/

/* I/O */

#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3

/* Process */

#define SYS_fork       57
#define SYS_execve     59
#define SYS_exit       60
#define SYS_wait4      61

/* Directory */

#define SYS_getcwd     79
#define SYS_chdir      80

/* Pipes */

#define SYS_pipe       22
#define SYS_dup2       33

/* Signals */

#define SYS_kill       62
#define SYS_rt_sigaction 13
#define SYS_setpgid   109


/*===========================================================================
 Linux x86_64 syscall ABI

 rax : syscall number
 rdi : arg1
 rsi : arg2
 rdx : arg3
 r10 : arg4
 r8  : arg5
 r9  : arg6

 return value : rax

 clobbered registers:
 rcx
 r11
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

        : "rcx",
          "r11",
          "memory"
    );

    return ret;
}


/* 1 argument */

static inline __attribute__((always_inline))
long syscall1(
    long number,
    long arg1
)
{
    long ret;

    __asm__ volatile(
        "syscall"

        : "=a"(ret)

        : "a"(number),
          "D"(arg1)

        : "rcx",
          "r11",
          "memory"
    );

    return ret;
}


/* 2 arguments */

static inline __attribute__((always_inline))
long syscall2(
    long number,
    long arg1,
    long arg2
)
{
    long ret;

    __asm__ volatile(
        "syscall"

        : "=a"(ret)

        : "a"(number),
          "D"(arg1),
          "S"(arg2)

        : "rcx",
          "r11",
          "memory"
    );

    return ret;
}


/* 3 arguments */

static inline __attribute__((always_inline))
long syscall3(
    long number,
    long arg1,
    long arg2,
    long arg3
)
{
    long ret;

    __asm__ volatile(
        "syscall"

        : "=a"(ret)

        : "a"(number),
          "D"(arg1),
          "S"(arg2),
          "d"(arg3)

        : "rcx",
          "r11",
          "memory"
    );

    return ret;
}

#endif