#ifndef WRAPPERS_H
#define WRAPPERS_H

/*
===============================================================================
wrappers.h — system call wrapper declarations

Every kernel interaction in this shell passes through one of these functions.
Shell logic never invokes raw syscall instructions directly.
===============================================================================
*/

/* ── I/O ──────────────────────────────────────────────────────────────── */

void sys_write(int fd, const char *buf, long count);
long sys_read(int fd, char *buf, long count);
long sys_open(const char *pathname, int flags, int mode);
long sys_close(long fd);

/* ── Pipes ────────────────────────────────────────────────────────────── */

long sys_pipe(int pipefd[2]);
long sys_dup2(long oldfd, long newfd);

/* ── Process ──────────────────────────────────────────────────────────── */

long sys_fork(void);
long sys_execve(const char *pathname, char *const argv[], char *const envp[]);

/*
 * sys_wait4
 *
 * Waits for child `pid` to change state.
 *
 * options:
 *   0         — block until child exits.
 *   WNOHANG   — return immediately if no child has changed state.
 *   WUNTRACED — also return if a child was *stopped* (not just exited).
 *               Required to detect Ctrl+Z on a foreground job.
 *
 * status macros (defined in constants.h):
 *   WIFEXITED(s)   — child exited normally
 *   WIFSTOPPED(s)  — child was stopped by a signal (WUNTRACED only)
 *   WIFSIGNALED(s) — child was killed by a signal
 */
long sys_wait4(long pid, int *status, int options);

void sys_exit(int status);

/* ── Process groups ───────────────────────────────────────────────────── */

/*
 * sys_getpid
 *
 * Returns the PID of the calling process.
 * Used at shell startup to record g_shell_pgid.
 */
long sys_getpid(void);

/*
 * sys_setsid
 *
 * Creates a new session with the calling process as leader.
 * The calling process must NOT already be a process group leader.
 *
 * After setsid(), the process has no controlling terminal.
 * We then use ioctl(fd, TIOCSCTTY, 0) to claim the PTY slave as the
 * controlling terminal, which is required for tcsetpgrp() to work.
 *
 * Returns the new session ID (= caller's PID) on success, or -1.
 */
long sys_setsid(void);

/*
 * sys_setpgid
 *
 * Sets the process group ID of process `pid` to `pgid`.
 *
 * setpgid(0, 0)  — put the calling process into its own new process group
 *                  (PGID = the process's own PID).
 * setpgid(0, x)  — join existing process group x.
 *
 * Called from BOTH parent and child after fork() to avoid the race where
 * tcsetpgrp() runs before the child has called setpgid().  One of the two
 * calls will always succeed before it matters.
 */
long sys_setpgid(long pid, long pgid);

/*
 * sys_getpgid
 *
 * Returns the process group ID of process `pid`.
 * Passing 0 returns the calling process's own PGID.
 */
long sys_getpgid(long pid);

/* ── Terminal control ─────────────────────────────────────────────────── */

/*
 * sys_tcsetpgrp
 *
 * Gives terminal `fd` to process group `pgid`.
 * All subsequent keyboard signals (SIGINT, SIGTSTP) go to that group.
 *
 * Called before waiting for a foreground pipeline so the pipeline
 * receives Ctrl+C / Ctrl+Z directly.
 *
 * Implemented via ioctl(fd, TIOCSPGRP, &pgid).
 */
long sys_tcsetpgrp(int fd, long pgid);

/*
 * sys_tcgetpgrp
 *
 * Returns the process group ID currently in control of terminal `fd`.
 * Implemented via ioctl(fd, TIOCGPGRP, &pgid).
 */
long sys_tcgetpgrp(int fd);

/*
 * sys_tiocsctty
 *
 * ioctl(fd, TIOCSCTTY, 0) — make fd the controlling terminal of this
 * session.  Called once after setsid() to claim the PTY slave.
 */
long sys_tiocsctty(int fd);

/* ── Signals ──────────────────────────────────────────────────────────── */

/*
 * sys_sigaction
 *
 * Installs or queries a signal handler.
 *
 * `act` and `oldact` are pointers to kernel_sigaction_t (defined in
 * signals/signals.h).  They are declared void* here to avoid a circular
 * include dependency between wrappers.h and signals.h.
 *
 * The fourth argument (sigsetsize = KERNEL_SIGSET_SIZE = 8) is passed
 * automatically by this wrapper — callers do not supply it.
 */
long sys_sigaction(int signum, const void *act, void *oldact);

/*
 * sys_kill
 *
 * Sends signal `sig` to process or process group `pid`.
 *
 * pid > 0  — send to that specific process.
 * pid == 0 — send to every process in the calling process's group.
 * pid < 0  — send to every process in group |pid|.
 *
 * Used for:
 *   kill(-pgid, SIGCONT)  — resume a stopped background pipeline.
 *   kill(-pgid, SIGTERM)  — terminate a pipeline (future use).
 */
long sys_kill(long pid, int sig);

/* ── Directory ────────────────────────────────────────────────────────── */

long sys_getcwd(char *buffer, long size);
long sys_chdir(const char *path);

#endif  /* WRAPPERS_H */
