#ifndef SYSCALL_H
#define SYSCALL_H

/*
===============================================================================
syscall.h

Purpose:
    Architecture selector for the educational POSIX shell.

Why this file exists:
    The shell must remain independent of any specific CPU architecture.
    This file chooses the appropriate architecture implementation at
    compile time.

Design principle:
    Higher-level modules (wrappers, parser, executor, shell loop)
    should never directly include architecture-specific files.

Current support:
    - x86_64

Future support:
    - ARM64
    - RISC-V

Educational objective:
    Porting the shell to a new architecture should require adding a
    single architecture file while leaving the remaining shell
    components unchanged.

===============================================================================
*/

#if defined(__x86_64__)

#include "../arch/x86_64/arch.h" // IWYU pragma: export

#elif defined(__aarch64__)

#include "../arch/arm64/arch.h"  // IWYU pragma: export

#elif defined(__riscv)

#include "../arch/riscv64/arch.h" // IWYU pragma: export

#else

#error "Unsupported architecture"

#endif

#endif