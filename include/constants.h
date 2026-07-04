#ifndef CONSTANTS_H
#define CONSTANTS_H

/*===========================================================================
 Parser / input limits
===========================================================================*/

#define MAX_INPUT           1024   /* Max raw input bytes from keyboard      */
#define MAX_TOKENS            64   /* Max tokens produced by tokenizer       */
#define MAX_ARGS              32   /* Max arguments per single command       */
#define MAX_PIPELINE_DEPTH     8   /* Max commands connected by pipes        */
#define MAX_TOKEN_LEN        256   /* Max character length of one token      */
#define MAX_FILENAME_LEN     256   /* Max character length of redirect file  */
#define MAX_PATH_LEN         256   /* Max length of a resolved executable    */

/*===========================================================================
 Job control limits
===========================================================================*/

#define MAX_JOBS              16   /* Max concurrent jobs in the job table   */

/*===========================================================================
 Linux open() flags
 Source: linux/include/uapi/asm-generic/fcntl.h (stable ABI values)
===========================================================================*/

#define O_RDONLY          0x0000
#define O_WRONLY          0x0001
#define O_RDWR            0x0002
#define O_CREAT           0x0040   /* 0100 octal — create if absent          */
#define O_TRUNC           0x0200   /* 01000 octal — truncate on open         */
#define O_APPEND          0x0400   /* 02000 octal — writes go to end         */

/* Default permission bits for files created by redirection: rw-r--r-- */
#define REDIR_CREATE_MODE    0644

/*===========================================================================
 Linux signal numbers
 Source: linux/include/uapi/asm-generic/signal.h (stable ABI values)

 Why these are hand-defined:
     <signal.h> is part of libc.  This project has zero libc dependency.
     The values below are part of the stable Linux ABI.
===========================================================================*/

#define SIGHUP      1    /* Hangup — terminal closed                         */
#define SIGINT      2    /* Interrupt — Ctrl+C                               */
#define SIGQUIT     3    /* Quit — Ctrl+\ (also generates core dump)         */
#define SIGKILL     9    /* Kill — cannot be caught or ignored               */
#define SIGTERM    15    /* Termination request (polite kill)                 */
#define SIGCHLD    17    /* Child process changed state (exit / stop / cont) */
#define SIGCONT    18    /* Continue a stopped process                       */
#define SIGTSTP    20    /* Terminal stop — Ctrl+Z                           */
#define SIGTTIN    21    /* Background process tried to read from terminal   */
#define SIGTTOU    22    /* Background process tried to write to terminal    */

/*===========================================================================
 sigaction flags
 Source: linux/include/uapi/asm-generic/signal.h
===========================================================================*/

/*
 * SA_RESTORER
 *   Indicates that sa_restorer points to a valid signal return trampoline.
 *   Required on x86-64 when not using libc (which normally provides it).
 *   See arch/x86_64/arch.h: arch_signal_restorer().
 */
#define SA_RESTORER    0x04000000

/*
 * SA_RESTART
 *   Automatically restart system calls interrupted by this signal.
 *   Without this, a read() blocked on the terminal returns EINTR when
 *   a background child exits (SIGCHLD).  SA_RESTART makes the kernel
 *   retry the read() transparently — the shell's input loop keeps working.
 */
#define SA_RESTART     0x10000000

/*
 * SA_NOCLDSTOP
 *   Do not deliver SIGCHLD when a child is stopped (Ctrl+Z), only when
 *   it exits.  We install our own SIGTSTP handling for the stopped case.
 *   Without this flag, Ctrl+Z on a foreground job would trigger the SIGCHLD
 *   handler even though the child has not exited — causing a premature reap
 *   attempt that would fail (wait4 would return 0 with WNOHANG).
 *
 *   NOTE: We do NOT set this flag, because we WANT to track stopped children
 *   in the SIGCHLD handler so the job table updates correctly.
 *   This constant is defined here for documentation purposes.
 */
#define SA_NOCLDSTOP   0x00000001

/*===========================================================================
 SIG_DFL / SIG_IGN — special handler values
 Source: linux/include/uapi/asm-generic/signal.h
===========================================================================*/

#define SIG_DFL   ((void (*)(int)) 0)   /* Default kernel action            */
#define SIG_IGN   ((void (*)(int)) 1)   /* Ignore the signal entirely       */

/*===========================================================================
 wait4() / waitpid() option flags
 Source: linux/include/uapi/linux/wait.h
===========================================================================*/

/*
 * WNOHANG
 *   Return immediately if no child has changed state yet.
 *   Used in the SIGCHLD handler to reap background zombies without blocking.
 */
#define WNOHANG      1

/*
 * WUNTRACED
 *   Also return if a child has been stopped (not just exited).
 *   Used when waiting for foreground jobs so that Ctrl+Z (which stops
 *   the child) causes wait4() to return, letting the shell detect the
 *   stop and add the job to the background job table.
 */
#define WUNTRACED    2

/*===========================================================================
 wait status inspection macros
 Source: linux/include/uapi/sys/wait.h — pure bitfield operations,
         no libc dependency.
===========================================================================*/

/* True if the child terminated normally (via exit() or return from main). */
#define WIFEXITED(s)    (((s) & 0x7f) == 0)

/* Exit code from a normally-terminated child. */
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)

/* True if the child was killed by a signal. */
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)

/* Signal number that killed the child. */
#define WTERMSIG(s)     ((s) & 0x7f)

/* True if the child was stopped by a signal (requires WUNTRACED). */
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)

/* Signal number that stopped the child (valid when WIFSTOPPED). */
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)

/*===========================================================================
 ioctl() request codes for terminal process-group control
 Source: linux/include/uapi/asm-generic/ioctls.h
===========================================================================*/

/*
 * TIOCGPGRP — get terminal's foreground process group
 *   ioctl(fd, TIOCGPGRP, &pgid) fills pgid with the process group ID
 *   currently in control of the terminal.
 */
#define TIOCGPGRP   0x540F

/*
 * TIOCSPGRP — set terminal's foreground process group
 *   ioctl(fd, TIOCSPGRP, &pgid) transfers terminal control to the
 *   process group identified by pgid.
 *   The calling process must be in the same session as the terminal.
 */
#define TIOCSPGRP   0x5410

/*
 * TIOCSCTTY — set controlling terminal
 *   ioctl(fd, TIOCSCTTY, 0) makes fd the controlling terminal of the
 *   calling process's session.  Called after setsid() so the PTY slave
 *   becomes the shell's controlling terminal, enabling tcsetpgrp() to work.
 */
#define TIOCSCTTY   0x540E

/* Kernel sigset size in bytes (64 signals / 8 bits-per-byte = 8 bytes) */
#define KERNEL_SIGSET_SIZE  8

#endif  /* CONSTANTS_H */
