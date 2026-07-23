#ifndef SIGNALS_H
#define SIGNALS_H

/*
===============================================================================
signals/signals.h — signal infrastructure for the POSIX shell

Why this file exists:
    POSIX job control requires careful signal management.  The shell must
    protect itself from signals meant for foreground programs, each child
    must restore normal signal behaviour before exec(), and background
    children must be collected asynchronously through SIGCHLD.

    All signal-related types, constants, and function declarations live here.

Contents:
    1. kernel_sigaction_t — the raw struct the rt_sigaction syscall uses.
    2. Global state exposed to executor.c and shell_loop.c.
    3. Function declarations for signal setup and teardown.
===============================================================================
*/

/*===========================================================================
 kernel_sigset_t / kernel_sigaction_t

 Why hand-defined:
     <signal.h> is part of libc.  We cannot use it.  These structs match
     the in-kernel layout used by the rt_sigaction syscall on x86-64.

     Reference: linux/include/uapi/asm/signal.h
                linux/include/uapi/asm-generic/signal.h
===========================================================================*/

/*
 * kernel_sigset_t
 *
 * A bitmask of up to 64 signals packed into one 64-bit word.
 * Signal N is represented by bit (N-1) in sig[0].
 *
 * We set it to 0 in our sigaction structs, meaning "do not block any
 * additional signals while this handler is running."
 */
typedef struct {
  unsigned long sig[1]; /* 64 signals in one 64-bit word         */
} kernel_sigset_t;

/*
 * kernel_sigaction_t
 *
 * Passed to the rt_sigaction syscall to install or query a handler.
 *
 * Field layout (order matters — the kernel reads this as a raw struct):
 *
 *   sa_handler   : Handler function, SIG_DFL (0), or SIG_IGN (1).
 *   sa_flags     : Combination of SA_* constants (constants.h).
 *   sa_restorer  : Pointer to the signal return trampoline.
 *                  Required on x86-64 without libc.
 *                  We always use arch_signal_restorer() (arch/x86_64/arch.h).
 *   sa_mask      : Additional signals to block while handler runs.
 *                  We set this to 0 (block nothing extra).
 */
typedef struct {
  void (*sa_handler)(int);
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  kernel_sigset_t sa_mask;
} kernel_sigaction_t;

/*===========================================================================
 Global state (defined in signals.c, used throughout the shell)
===========================================================================*/

/*
 * g_sigchld_flag
 *
 * Set to 1 by sigchld_handler() when any child changes state.
 * Checked at the top of each shell_main() iteration.
 * When set, the main loop calls reap_background_jobs() to collect
 * finished background children and update the job table.
 *
 * volatile: the compiler must not optimise away reads of this variable,
 * because it is written from a signal handler (asynchronously).
 */
extern volatile int g_sigchld_flag;

/*
 * g_shell_pgid
 *
 * The process group ID of the shell itself.
 * Set once at startup: g_shell_pgid = sys_getpid().
 *
 * Used to restore terminal control to the shell after each foreground
 * pipeline finishes:
 *   sys_tcsetpgrp(STDIN_FILENO, g_shell_pgid)
 */
extern long g_shell_pgid;

/*===========================================================================
 Function declarations
===========================================================================*/

/*
 * setup_shell_signals
 *
 * Called once at shell startup (before the first prompt).
 *
 * What it does:
 *   - Installs SIG_IGN for SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU.
 *     The shell itself must not be killed or stopped by these signals —
 *     they are meant for the foreground program, not the shell.
 *   - Installs sigchld_handler for SIGCHLD with SA_RESTART so that
 *     blocked system calls (read) are automatically restarted instead
 *     of returning EINTR when a background child exits.
 */
void setup_shell_signals(void);

/*
 * reset_child_signals
 *
 * Called in every child process immediately after fork(), before exec().
 *
 * What it does:
 *   Restores SIG_DFL for every signal the shell was ignoring.
 *
 * Why this is necessary:
 *   Signal dispositions are inherited across fork().  If the shell ignores
 *   SIGINT, the child inherits that ignore.  Without reset_child_signals(),
 *   pressing Ctrl+C would have no effect on the foreground program because
 *   the program's SIGINT handler would still be SIG_IGN.
 *
 * When called:
 *   Inside run_stage() in executor.c, before the dup2 / exec sequence.
 */
void reset_child_signals(void);

#endif /* SIGNALS_H */
