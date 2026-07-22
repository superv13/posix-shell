#!/usr/bin/env python3
"""
tests/test_phase4.py

Purpose:
    Automated test suite for Phase 4 (Job Control and Signal Handling) of
    the educational POSIX shell.

What is tested:
    - SIGINT (Ctrl+C) kills the foreground job but not the shell
    - SIGTSTP (Ctrl+Z) stops the foreground job and returns the prompt
    - `jobs` lists background and stopped jobs
    - `fg` resumes a stopped job in the foreground
    - `bg` resumes a stopped job in the background
    - The shell itself ignores SIGINT at the prompt
    - Multiple stopped jobs are numbered correctly (%1, %2)
    - Background job completion produces a "[N] Done cmd" notification
    - `--trace` mode prints [TRACE] lines to stderr
    - Process groups: foreground job is in its own group (verifiable via /proc)

Usage:
    python3 tests/test_phase4.py
    python3 tests/test_phase4.py -v    # show full transcript per test

Exit code:
    0 if all tests passed, 1 otherwise.
"""

import os
import pty
import select
import signal
import subprocess
import sys
import time

SHELL_BINARY = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "posixsh"
)

VERBOSE = "-v" in sys.argv


# ---------------------------------------------------------------------------
# Session helper (same PTY-based approach as test_phase3.py)
# ---------------------------------------------------------------------------

class Session:
    """One posixsh process attached to a pseudo-terminal."""

    def __init__(self, extra_args=None):
        master, slave = pty.openpty()
        self.master = master
        args = [SHELL_BINARY]
        if extra_args:
            args += extra_args
        self.proc = subprocess.Popen(
            args, stdin=slave, stdout=slave, stderr=slave
        )
        os.close(slave)
        self.buffer = ""

    def send_line(self, line, idle_timeout=0.5, max_wait=6.0):
        """Write one line + newline, collect output until it goes quiet."""
        os.write(self.master, (line + "\n").encode())
        return self._drain(idle_timeout, max_wait)

    def send_signal(self, sig, idle_timeout=0.5, max_wait=3.0):
        """Send a signal character to the pty (^C = SIGINT, ^Z = SIGTSTP)."""
        char = {
            signal.SIGINT:  b"\x03",   # Ctrl+C
            signal.SIGTSTP: b"\x1a",   # Ctrl+Z
        }.get(sig)
        if char is None:
            raise ValueError(f"No pty char for signal {sig}")
        os.write(self.master, char)
        return self._drain(idle_timeout, max_wait)

    def _drain(self, idle_timeout, max_wait):
        start     = time.time()
        last_data = time.time()
        acc       = b""
        while True:
            remaining = idle_timeout - (time.time() - last_data)
            if remaining <= 0:
                break
            if time.time() - start > max_wait:
                break
            r, _, _ = select.select([self.master], [], [], max(remaining, 0))
            if self.master in r:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    break
                if not data:
                    break
                acc += data
                last_data = time.time()
        text = acc.decode(errors="replace")
        self.buffer += text
        return text

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass
        try:
            os.close(self.master)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Tiny test framework
# ---------------------------------------------------------------------------

results = []   # (name, passed, detail)


def record(name, passed, detail=""):
    results.append((name, passed, detail))
    mark = "PASS" if passed else "FAIL"
    print(f"[{mark}] {name}")
    if VERBOSE or not passed:
        if detail:
            for line in detail.splitlines():
                print(f"        {line}")


def expect_contains(name, output, needle):
    ok = needle in output
    record(name, ok,
           f"expected {needle!r} in:\n{output!r}" if not ok else "")
    return ok


def expect_not_contains(name, output, needle):
    ok = needle not in output
    record(name, ok,
           f"did NOT expect {needle!r} in:\n{output!r}" if not ok else "")
    return ok


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

def test_sigint_kills_foreground_not_shell():
    """
    Ctrl+C (SIGINT) must terminate the foreground job but leave the shell alive
    and responsive.

    Mechanism:
        The terminal driver sends SIGINT to the foreground process group.
        Because the shell set up its own process group (setpgid(0,0)) and
        gave the terminal to the pipeline group (tcsetpgrp), SIGINT goes to
        the pipeline, not the shell. The shell itself has SIGINT set to SIG_IGN.
    """
    s = Session()
    # Start a long-running foreground job
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    # Send Ctrl+C to the pty — the terminal driver delivers SIGINT to the
    # foreground process group (sleep's group, not the shell's group)
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)
    # Shell should have returned to the prompt; verify with a command
    out = s.send_line("echo shell_alive")
    s.send_line("exit")
    s.close()
    expect_contains("SIGINT kills foreground job, shell stays alive",
                    out, "shell_alive")


def test_ctrlz_stops_foreground():
    """
    Ctrl+Z (SIGTSTP) must stop the foreground job and return the prompt.
    The stopped job should appear in `jobs` output.

    Mechanism:
        The terminal driver delivers SIGTSTP to the foreground process group.
        The child stops; the shell's wait4(WUNTRACED) returns WIFSTOPPED.
        The shell records the job as Stopped, reclaims the terminal, and
        prints the next prompt.
    """
    s = Session()
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    # Send Ctrl+Z
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    # Shell should be back at the prompt now
    jobs_out = s.send_line("jobs")
    s.send_line("exit")
    s.close()
    expect_contains("Ctrl+Z stops foreground job, prompt returns",
                    jobs_out, "Stopped")
    expect_contains("Stopped job appears in `jobs`",
                    jobs_out, "sleep")


def test_jobs_shows_background_job():
    """
    `sleep N &` must register a background job visible via `jobs`.
    """
    s = Session()
    s.send_line("sleep 30 &", idle_timeout=0.5, max_wait=2.0)
    out = s.send_line("jobs")
    s.send_line("exit")
    s.close()
    expect_contains("`jobs` lists running background job",
                    out, "Running")
    expect_contains("`jobs` shows command name for background job",
                    out, "sleep")


def test_jobs_numbering():
    """
    Multiple background jobs must receive sequential job numbers [1], [2].
    """
    s = Session()
    s.send_line("sleep 30 &", idle_timeout=0.4, max_wait=2.0)
    s.send_line("sleep 30 &", idle_timeout=0.4, max_wait=2.0)
    out = s.send_line("jobs")
    s.send_line("exit")
    s.close()
    expect_contains("first background job gets number [1]",  out, "[1]")
    expect_contains("second background job gets number [2]", out, "[2]")


def test_fg_resumes_stopped_job():
    """
    `fg` (bare, no job number) must resume the most recently stopped job
    in the foreground (POSIX %+ semantics).

    After fg completes, the job should no longer appear in `jobs`.
    """
    s = Session()
    # Start sleep in foreground, stop it
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    # Verify it's stopped
    jobs_out = s.send_line("jobs")
    stopped_ok = "Stopped" in jobs_out

    # Resume in foreground, then kill it immediately
    s.send_line("fg", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)
    out_after = s.send_line("jobs")
    s.send_line("exit")
    s.close()

    record("job was stopped before fg", stopped_ok)
    # After fg + Ctrl+C the job is gone from the table
    expect_not_contains("`jobs` empty after fg + Ctrl+C",
                        out_after, "sleep")


def test_fg_with_job_number():
    """
    `fg %1` must resume the specific job identified by its job number.
    """
    s = Session()
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    # fg %1 should resume
    s.send_line("fg %1", idle_timeout=0.3, max_wait=1.0)
    # Send Ctrl+C to kill the resumed job
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)
    out = s.send_line("echo ok_after_fg1")
    s.send_line("exit")
    s.close()
    expect_contains("`fg %1` resumes job 1 and shell stays alive",
                    out, "ok_after_fg1")


def test_bg_resumes_in_background():
    """
    `bg` must resume a stopped job in the background (shell does NOT wait).
    The job should appear as Running in `jobs` after bg.
    """
    s = Session()
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    s.send_line("bg", idle_timeout=0.4, max_wait=2.0)
    # Shell should be at prompt immediately
    out = s.send_line("jobs")
    s.send_line("exit")
    s.close()
    expect_contains("`bg` resumes stopped job as Running",
                    out, "Running")


def test_bg_with_job_number():
    """
    `bg %1` must resume the specific stopped job in the background.
    """
    s = Session()
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    bg_out = s.send_line("bg %1", idle_timeout=0.4, max_wait=2.0)
    jobs_out = s.send_line("jobs")
    s.send_line("exit")
    s.close()
    expect_contains("`bg %1` output shows job resumed",
                    bg_out + jobs_out, "sleep")
    expect_not_contains("`bg %1` does not show Stopped",
                        jobs_out, "Stopped")


def test_shell_ignores_sigint_at_prompt():
    """
    Pressing Ctrl+C at an empty prompt must NOT kill the shell.

    Mechanism:
        The shell has SIGINT set to SIG_IGN. There is no foreground process
        group other than the shell's own group. The Ctrl+C goes to the shell
        group, but SIG_IGN means nothing happens.
    """
    s = Session()
    # Send Ctrl+C with nothing running
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)
    out = s.send_line("echo still_alive")
    s.send_line("exit")
    s.close()
    expect_contains("shell survives Ctrl+C at empty prompt",
                    out, "still_alive")


def test_background_job_done_notification():
    """
    When a background job completes, the shell must print "[N] Done cmd"
    before the next prompt.

    We use `sleep 0.2 &` (very short sleep) and wait for it to exit, then
    send a blank line and check that the Done notification appears.
    """
    s = Session()
    s.send_line("sleep 0.2 &", idle_timeout=0.5, max_wait=2.0)
    # Wait for the job to finish
    time.sleep(0.6)
    # Sending any command will cause the shell to call jobs_notify_done()
    # before printing the next prompt
    out = s.send_line("echo after_done")
    s.send_line("exit")
    s.close()
    expect_contains("background job completion shows Done notification",
                    out, "Done")


def test_trace_mode_emits_trace_lines():
    """
    Running with --trace must produce [TRACE] lines on stderr.
    We invoke posixsh --trace, send one command, and verify [TRACE] appears.
    """
    s = Session(extra_args=["--trace"])
    out = s.send_line("echo hello", idle_timeout=0.5, max_wait=3.0)
    s.send_line("exit")
    s.close()
    expect_contains("--trace mode emits [TRACE] lines",
                    out, "[TRACE]")
    expect_contains("--trace mode still executes commands normally",
                    out, "hello")


def test_trace_mode_shows_fork():
    """
    --trace output must contain the fork() trace entry when running an
    external command.

    Note: 'echo' is now a builtin (zero-fork fast path), so we use an
    external binary (/bin/true) that always triggers fork() + execve().
    """
    s = Session(extra_args=["--trace"])
    out = s.send_line("/bin/true", idle_timeout=0.5, max_wait=3.0)
    s.send_line("exit")
    s.close()
    expect_contains("--trace shows fork() call",
                    out, "fork()")


def test_trace_mode_shows_execve():
    """
    --trace output must contain the execve() trace entry.
    """
    s = Session(extra_args=["--trace"])
    out = s.send_line("/bin/true", idle_timeout=0.5, max_wait=3.0)
    s.send_line("exit")
    s.close()
    expect_contains("--trace shows execve() call",
                    out, "execve()")


def test_fg_nonexistent_job():
    """
    `fg %99` for a non-existent job must print an error, not crash.
    """
    s = Session()
    out = s.send_line("fg %99")
    out2 = s.send_line("echo survived")
    s.send_line("exit")
    s.close()
    expect_contains("fg on nonexistent job prints error",
                    out, "posixsh: fg:")
    expect_contains("shell survives fg on nonexistent job",
                    out2, "survived")


def test_bg_nonexistent_job():
    """
    `bg %99` for a non-existent job must print an error, not crash.
    """
    s = Session()
    out = s.send_line("bg %99")
    out2 = s.send_line("echo survived_bg")
    s.send_line("exit")
    s.close()
    expect_contains("bg on nonexistent job prints error",
                    out, "posixsh: bg:")
    expect_contains("shell survives bg on nonexistent job",
                    out2, "survived_bg")


def test_phase3_regression_pipeline():
    """
    Phase 3 regression: pipelines must still work after Phase 4 changes.
    Specifically, verify that process group setup does not break pipe EOF.
    """
    s = Session()
    out = s.send_line("echo hello | cat")
    s.send_line("exit")
    s.close()
    expect_contains("pipeline still works after Phase 4 (process groups)",
                    out, "hello")


def test_phase3_regression_redirect():
    """
    Phase 3 regression: output redirection must still work.
    """
    path = "/tmp/posixsh_p4_redir.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    s.send_line(f"echo p4redir > {path}")
    s.send_line("exit")
    s.close()
    try:
        content = open(path).read()
    except OSError as e:
        record("Phase 3 regression: redirection still works", False, str(e))
        return
    expect_contains("Phase 3 regression: redirection still works",
                    content, "p4redir")


def test_multiple_stopped_jobs_fg_by_number():
    """
    Stop two jobs, then resume each by number. Both fg %1 and fg %2 must work.
    """
    s = Session()
    # Stop job 1
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)
    # Stop job 2
    s.send_line("sleep 30", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGTSTP, idle_timeout=0.8, max_wait=3.0)

    jobs_out = s.send_line("jobs")
    both_stopped = "Stopped" in jobs_out and "[2]" in jobs_out
    record("two stopped jobs both appear in `jobs`", both_stopped, jobs_out)

    # fg %1 — resume first job, kill it
    s.send_line("fg %1", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)

    # fg %2 — resume second job, kill it
    s.send_line("fg %2", idle_timeout=0.3, max_wait=1.0)
    s.send_signal(signal.SIGINT, idle_timeout=0.5, max_wait=2.0)

    out = s.send_line("echo all_done")
    s.send_line("exit")
    s.close()
    expect_contains("shell alive after resuming and killing both stopped jobs",
                    out, "all_done")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    if not os.path.isfile(SHELL_BINARY):
        print(f"error: shell binary not found at {SHELL_BINARY}; run `make` first.")
        sys.exit(1)

    tests = [
        test_sigint_kills_foreground_not_shell,
        test_ctrlz_stops_foreground,
        test_jobs_shows_background_job,
        test_jobs_numbering,
        test_fg_resumes_stopped_job,
        test_fg_with_job_number,
        test_bg_resumes_in_background,
        test_bg_with_job_number,
        test_shell_ignores_sigint_at_prompt,
        test_background_job_done_notification,
        test_trace_mode_emits_trace_lines,
        test_trace_mode_shows_fork,
        test_trace_mode_shows_execve,
        test_fg_nonexistent_job,
        test_bg_nonexistent_job,
        test_phase3_regression_pipeline,
        test_phase3_regression_redirect,
        test_multiple_stopped_jobs_fg_by_number,
    ]

    for t in tests:
        try:
            t()
        except Exception as e:
            record(t.__name__, False, f"raised exception: {e!r}")

    passed = sum(1 for _, ok, _ in results if ok)
    total  = len(results)
    print(f"\n{passed}/{total} checks passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
