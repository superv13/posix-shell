// kernel/wrappers.c — system call wrapper implementations
//
// Every function here does exactly one thing: place arguments into the
// correct registers and invoke the kernel via the architecture layer.
// No logic lives here — only the kernel interface.

#include "../include/syscall.h"
#include "../include/wrappers.h"
#include "../include/constants.h"

// ── I/O ──────────────────────────────────────────────────────────────────

void sys_write(int fd, const char *buf, long count)
{
    syscall3(SYS_write, (long)fd, (long)buf, count);
}

long sys_read(int fd, char *buf, long count)
{
    return syscall3(SYS_read, (long)fd, (long)buf, count);
}

// sys_open
//
// x86-64 ABI: rax=2, rdi=pathname, rsi=flags, rdx=mode
//
// flags examples (from constants.h, not fcntl.h):
//   O_RDONLY              — input redirection
//   O_WRONLY|O_CREAT|O_TRUNC  — output redirection (>)
//   O_WRONLY|O_CREAT|O_APPEND — append redirection (>>)
long sys_open(const char *pathname, int flags, int mode)
{
    return syscall3(SYS_open, (long)pathname, (long)flags, (long)mode);
}

// sys_close
//
// Critical for pipeline correctness: every unused pipe end must be closed
// or readers will never see EOF (see executor.c for the full explanation).
long sys_close(long fd)
{
    return syscall1(SYS_close, fd);
}

// ── Pipes ─────────────────────────────────────────────────────────────────

// sys_pipe
//
// x86-64 ABI: rax=22, rdi=pipefd
// Returns 0 on success.  pipefd[0]=read end, pipefd[1]=write end.
long sys_pipe(int pipefd[2])
{
    return syscall1(SYS_pipe, (long)pipefd);
}

// sys_dup2
//
// x86-64 ABI: rax=33, rdi=oldfd, rsi=newfd
//
// Makes newfd refer to the same open file as oldfd, closing newfd first
// if it was open.  The atomicity of close+redirect is why dup2() (not a
// manual close() then open()) is the correct tool for redirection.
long sys_dup2(long oldfd, long newfd)
{
    return syscall2(SYS_dup2, oldfd, newfd);
}

// ── Process ───────────────────────────────────────────────────────────────

long sys_fork(void)
{
    return syscall0(SYS_fork);
}

long sys_execve(const char *pathname, char *const argv[], char *const envp[])
{
    return syscall3(SYS_execve, (long)pathname, (long)argv, (long)envp);
}

// sys_wait4
//
// x86-64 ABI: rax=61, rdi=pid, rsi=status, rdx=options, r10=rusage(NULL)
//
// options flags (constants.h):
//   0         — block until child exits
//   WNOHANG   — return 0 immediately if no child has changed state
//   WUNTRACED — also return when a child is stopped, not just exited
//
// rusage: NULL — we do not collect resource statistics.
long sys_wait4(long pid, int *status, int options)
{
    return syscall4(SYS_wait4, pid, (long)status, (long)options, 0L);
}

void sys_exit(int status)
{
    syscall1(SYS_exit, (long)status);
    while (1);   /* safety — execution never reaches here */
}

// ── Process groups ────────────────────────────────────────────────────────

// sys_getpid
//
// Returns the PID of the calling process.
// Called once at shell startup to record the shell's own PID/PGID so
// the shell can reclaim terminal control after each foreground pipeline.
long sys_getpid(void)
{
    return syscall0(SYS_getpid);
}

// sys_setsid
//
// x86-64 ABI: rax=112
//
// Creates a new session with no controlling terminal.
// Prerequisite for claiming the PTY slave as the controlling terminal.
long sys_setsid(void)
{
    return syscall0(SYS_setsid);
}

// sys_tiocsctty
//
// ioctl(fd, TIOCSCTTY, 0)
//
// Makes `fd` the controlling terminal of the calling process's session.
// Called once after setsid() so the PTY slave becomes the shell's
// controlling terminal.  Without this, tcsetpgrp() returns EPERM.
long sys_tiocsctty(int fd)
{
    return syscall3(SYS_ioctl, (long)fd, (long)TIOCSCTTY, 0L);
}

// sys_setpgid
//
// x86-64 ABI: rax=109, rdi=pid, rsi=pgid
//
// setpgid(0, 0)  — put self into a new process group (PGID = own PID).
// setpgid(0, x)  — join existing process group x.
//
// Why called from both parent AND child after fork():
//   Without the parent-side call, a race exists where the parent calls
//   tcsetpgrp() before the child has run setpgid().  The terminal would
//   then be given to a group that does not yet contain the child.
//   Calling from both sides means the process group exists before
//   tcsetpgrp() regardless of which process runs first.
long sys_setpgid(long pid, long pgid)
{
    return syscall2(SYS_setpgid, pid, pgid);
}

long sys_getpgid(long pid)
{
    return syscall1(SYS_getpgid, pid);
}

// ── Terminal control ──────────────────────────────────────────────────────

// sys_tcsetpgrp
//
// Implemented via: ioctl(fd, TIOCSPGRP, &pgid)
//
// Transfers ownership of the terminal to process group `pgid`.
// After this call, keyboard-generated signals (SIGINT from Ctrl+C,
// SIGTSTP from Ctrl+Z) are delivered to `pgid`, not to the shell.
//
// Called before waiting for a foreground pipeline.
// Called again (with g_shell_pgid) after the pipeline finishes to
// give the terminal back to the shell.
long sys_tcsetpgrp(int fd, long pgid)
{
    /*
     * TIOCSPGRP takes a pointer to the pgid, not the pgid directly.
     * We copy to a local int because syscall3 takes long arguments and
     * the kernel expects int* for this ioctl.
     */
    int pg = (int)pgid;
    return syscall3(SYS_ioctl, (long)fd, (long)TIOCSPGRP, (long)&pg);
}

// sys_tcgetpgrp
//
// Implemented via: ioctl(fd, TIOCGPGRP, &pgid)
//
// Returns the PGID currently controlling the terminal.
// Used at shell startup to verify the shell is in the foreground.
long sys_tcgetpgrp(int fd)
{
    int pg = 0;
    syscall3(SYS_ioctl, (long)fd, (long)TIOCGPGRP, (long)&pg);
    return (long)pg;
}

// ── Signals ───────────────────────────────────────────────────────────────

// sys_sigaction
//
// x86-64 ABI: rax=13, rdi=signum, rsi=act, rdx=oldact, r10=sigsetsize
//
// sigsetsize MUST be KERNEL_SIGSET_SIZE (8).  The kernel uses this to
// validate the sa_mask field length.  Passing the wrong value causes
// EINVAL and the signal handler is not installed.
//
// `act` and `oldact` point to kernel_sigaction_t (signals/signals.h).
// Declared void* here to avoid a circular include between wrappers.h
// and signals.h.
long sys_sigaction(int signum, const void *act, void *oldact)
{
    return syscall4(
        SYS_rt_sigaction,
        (long)signum,
        (long)act,
        (long)oldact,
        (long)KERNEL_SIGSET_SIZE
    );
}

// sys_kill
//
// x86-64 ABI: rax=62, rdi=pid, rsi=sig
//
// pid > 0 : send to specific process.
// pid < 0 : send to entire process group |pid|.
//
// Usage in job control:
//   sys_kill(-pgid, SIGCONT)  — resume a stopped pipeline.
long sys_kill(long pid, int sig)
{
    return syscall2(SYS_kill, pid, (long)sig);
}

// ── Directory ─────────────────────────────────────────────────────────────

long sys_getcwd(char *buffer, long size)
{
    return syscall2(SYS_getcwd, (long)buffer, size);
}

long sys_chdir(const char *path)
{
    return syscall1(SYS_chdir, (long)path);
}
