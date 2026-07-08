// signals/signals.c — signal handler installation and management
//
// This file owns all signal state for the educational POSIX shell.
// It is the only place where sigaction() is called.

#include "signals.h"
#include "../include/wrappers.h"
#include "../trace/trace.h"

/*===========================================================================
 Global state definitions
===========================================================================*/

/*
 * g_sigchld_flag
 *
 * Written exclusively by sigchld_handler() (from signal context).
 * Read and cleared by the main shell loop.
 *
 * Only a volatile int write/read is used here — no locks — because:
 *   - int writes are atomic on x86-64 (naturally aligned).
 *   - The compiler is told not to cache this via `volatile`.
 *   - We only set it to 1 in the handler; the main loop clears it.
 *     Even if a second SIGCHLD arrives between the read and clear,
 *     the loop's next iteration will call reap_background_jobs() again.
 */
volatile int g_sigchld_flag = 0;

/*
 * g_shell_pgid
 *
 * Initialised in shell_main() with g_shell_pgid = sys_getpid().
 * Read-only after that.  No synchronisation needed.
 */
long g_shell_pgid = 0;


/*===========================================================================
 SIGCHLD handler
===========================================================================*/

/*
 * sigchld_handler
 *
 * Called asynchronously by the kernel whenever a child process:
 *   - exits (normally or killed by signal)
 *   - stops  (Ctrl+Z on a foreground child)
 *   - continues (SIGCONT sent to a stopped child)
 *
 * Async-signal-safety requirement:
 *   Signal handlers may only call async-signal-safe functions.
 *   Writing to a volatile int is safe.
 *   We do NOT call wait4() here — that is done in reap_background_jobs()
 *   from the main loop, where it is safe to update the job table.
 *
 * SA_RESTART (set in setup_shell_signals) ensures that if SIGCHLD
 * interrupts a blocking read() for user input, the kernel automatically
 * restarts the read() without returning EINTR to the shell loop.
 */
static void sigchld_handler(int sig)
{
    (void)sig;          /* parameter unused — suppress compiler warning */
    g_sigchld_flag = 1;
}


/*===========================================================================
 Helper: install one handler
===========================================================================*/

/*
 * install_handler
 *
 * Builds a kernel_sigaction_t and calls sys_sigaction().
 * Used internally by setup_shell_signals() and reset_child_signals().
 *
 * Parameters:
 *   signum  — signal number (SIGINT, SIGCHLD, etc.)
 *   handler — SIG_DFL, SIG_IGN, or a function pointer
 *   flags   — SA_RESTART | SA_RESTORER, etc.
 */
static void install_handler(
    int            signum,
    void         (*handler)(int),
    unsigned long  flags
)
{
    kernel_sigaction_t sa;

    sa.sa_handler  = handler;
    sa.sa_flags    = flags | SA_RESTORER;
    sa.sa_restorer = arch_signal_restorer;  /* from arch/x86_64/arch.h */
    sa.sa_mask.sig[0] = 0;                 /* block nothing extra      */

    trace_sigaction(signum);
    sys_sigaction(signum, &sa, 0);
}


/*===========================================================================
 Public API
===========================================================================*/

/*
 * setup_shell_signals
 *
 * Called once at shell startup.
 *
 * Signal disposition table after this call:
 *
 *   Signal   │ Shell disposition │ Why
 *   ─────────┼───────────────────┼────────────────────────────────────────
 *   SIGINT   │ SIG_IGN           │ Ctrl+C must kill the foreground job,
 *   SIGQUIT  │ SIG_IGN           │ not the shell.  Children inherit
 *   SIGTSTP  │ SIG_IGN           │ SIG_IGN but reset_child_signals()
 *   SIGTTIN  │ SIG_IGN           │ restores SIG_DFL before exec() so the
 *   SIGTTOU  │ SIG_IGN           │ program receives the signal normally.
 *   ─────────┼───────────────────┼────────────────────────────────────────
 *   SIGCHLD  │ sigchld_handler   │ Notifies the shell when any background
 *            │ + SA_RESTART      │ child exits or stops.  SA_RESTART keeps
 *            │                   │ read() from returning EINTR.
 *
 * SIGTTIN / SIGTTOU explanation:
 *   The shell ignores these so that if it accidentally reads or writes
 *   to the terminal while a foreground job owns it, the shell is
 *   suspended instead of crashing.  In practice the shell does not
 *   read/write while a foreground job runs, so these are a safety net.
 */
void setup_shell_signals(void)
{
    unsigned long ign_flags = SA_RESTORER;

    install_handler(SIGINT,  SIG_IGN, ign_flags);
    install_handler(SIGQUIT, SIG_IGN, ign_flags);
    install_handler(SIGTSTP, SIG_IGN, ign_flags);
    install_handler(SIGTTIN, SIG_IGN, ign_flags);
    install_handler(SIGTTOU, SIG_IGN, ign_flags);

    /*
     * SIGCHLD: use sigchld_handler WITH SA_RESTART.
     *
     * SA_RESTART makes the kernel transparently restart the blocked
     * sys_read() after SIGCHLD fires.  The shell loop then reaps
     * finished children at the top of the NEXT iteration — after the
     * user presses Enter — which is where the "Done" notification
     * should appear (just before the next prompt).
     *
     * Zombies are collected promptly because reap_background_jobs()
     * runs at the top of every loop iteration, and the g_sigchld_flag
     * check in the loop body triggers an immediate re-check whenever
     * the flag was set while read() was running.
     */
    install_handler(SIGCHLD, sigchld_handler, SA_RESTART);
}


/*
 * reset_child_signals
 *
 * Called inside every child process after fork(), before exec().
 *
 * Restores SIG_DFL for the signals the shell was ignoring.
 * Without this, exec'd programs inherit SIG_IGN and pressing Ctrl+C
 * would have no effect — the program would never terminate.
 *
 * SIGCHLD is also reset: the child should not run the shell's SIGCHLD
 * handler if it forks its own grandchildren.
 *
 * Note on SIGTTIN/SIGTTOU:
 *   We restore defaults so that if a child tries to read/write the
 *   terminal while it does not own it, it receives the standard
 *   stop signal rather than the shell's ignore disposition.
 */
void reset_child_signals(void)
{
    unsigned long dfl_flags = SA_RESTORER;

    install_handler(SIGINT,  SIG_DFL, dfl_flags);
    install_handler(SIGQUIT, SIG_DFL, dfl_flags);
    install_handler(SIGTSTP, SIG_DFL, dfl_flags);
    install_handler(SIGTTIN, SIG_DFL, dfl_flags);
    install_handler(SIGTTOU, SIG_DFL, dfl_flags);
    install_handler(SIGCHLD, SIG_DFL, dfl_flags);
}
