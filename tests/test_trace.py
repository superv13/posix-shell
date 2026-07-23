#!/usr/bin/env python3
"""
tests/test_trace.py — Phase 5: --trace mode diagnostic output tests
    (trace_fork, trace_execve, trace_sigaction, and the --trace on/off gate)

Strategy:
    trace.c writes its diagnostics directly to fd 2 with a hand-rolled
    write_str()/write_long() — no libc buffering, no printf — so the lines
    show up immediately and in-order relative to the shell's own prompt.
    Because trace output goes to the *pty slave* (stderr and stdout share
    the same underlying tty when the shell is run interactively), a
    pty.fork()-based session sees both interleaved exactly as a real
    terminal would, so we don't need to redirect/merge streams ourselves.

    We reuse the pty.fork() + timed-line harness from test_signals.py
    rather than helper.run_shell(), because run_shell() (as used by
    test_builtins.py/test_jobs.py) has no documented way to pass an
    extra argv flag like --trace to the shell binary. Launching the
    binary ourselves lets us control argv precisely.

WHAT EACH SECTION VALIDATES:
    A. --trace gate           — g_trace_mode defaults to 0 (off): with no
                                 flag, NOTHING starting with "[TRACE]" is
                                 ever printed, no matter what the shell
                                 does (fork, exec, signals). With --trace,
                                 at least one "[TRACE]" line appears.
    B. trace_fork()            — every forked child logs
                                 "[TRACE] fork() -> <child_pid>" with a
                                 positive integer pid, and a distinct line
                                 is emitted per fork (e.g. once per stage
                                 of a pipeline).
    C. trace_execve()          — the exact path handed to execve() is
                                 logged as "[TRACE] execve() path=<path>".
                                 We use an absolute-path command
                                 (e.g. /bin/echo) so there's no ambiguity
                                 from $PATH resolution baked into the
                                 assertion.
    D. trace_sigaction()        — signal handler installation at shell
                                 startup is logged as
                                 "[TRACE] sigaction(<signum>, ...)" for
                                 every signal the shell installs a
                                 handler for (checked here for SIGCHLD,
                                 which jobs.c/signals.c is known to
                                 install per test_signals.py's SA_RESTART
                                 coverage), and this happens BEFORE the
                                 first prompt is ever printed.
    E. Ordering / non-interference — trace lines are pure diagnostics:
                                 they must not corrupt normal stdout
                                 (echo output, prompt) or break the shell.
"""

import sys
import os
import time
import re
import signal as pysignal
import select
import pty

sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, assert_contains, assert_not_contains

CANDIDATE_BINARIES = [
    os.environ.get("SHELL_BINARY"),
    "./posixsh",
    "./build/posixsh",
    "../posixsh",
    "./shell",
    "./mysh",
]


def _find_shell_binary():
    for candidate in CANDIDATE_BINARIES:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return os.path.abspath(candidate)
    print("ERROR: could not find the shell binary. Set SHELL_BINARY env var.")
    sys.exit(1)


def check(name, condition, detail=""):
    """Lightweight pass/fail printer for checks that aren't simple substring
    matches (e.g. regex/count-based assertions). Mirrors assert_contains."""
    status = "PASS" if condition else "FAIL"
    suffix = f"  ({detail})" if detail else ""
    print(f"  {name} ... {status}{suffix}")
    return condition


class Session:
    """A single pty-backed shell session, launched with an explicit argv
    (so we can pass --trace), supporting timed line-based writes."""

    def __init__(self, binary, argv_extra=None):
        argv = [binary] + (argv_extra or [])
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                os.execv(binary, argv)
            except Exception:
                os._exit(127)
        self.output = b""

    def send_line(self, text):
        os.write(self.fd, (text + "\n").encode())

    def wait(self, seconds):
        """Sleep while draining any output that arrives, so pipes never
        fill up and block the child."""
        end = time.time() + seconds
        while True:
            remaining = end - time.time()
            if remaining <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], remaining)
            if self.fd in r:
                if not self._read_available():
                    break

    def _read_available(self):
        try:
            chunk = os.read(self.fd, 65536)
        except OSError:
            return False
        if not chunk:
            return False
        self.output += chunk
        return True

    def drain(self, seconds):
        self.wait(seconds)

    def text(self):
        return self.output.decode(errors="replace")

    def close(self):
        try:
            pgid = os.getpgid(self.pid)
            os.killpg(pgid, pysignal.SIGKILL)
        except OSError:
            pass
        try:
            os.kill(self.pid, pysignal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


def run_traced(binary, cmds, trace=True, timeout=1.5, startup_wait=0.3):
    """
    Launch the shell (with --trace if trace=True), send each of `cmds` as
    a line with a short settle wait, then drain for the remainder of
    `timeout`. Returns the full captured pty text.
    """
    argv_extra = ["--trace"] if trace else []
    session = Session(binary, argv_extra=argv_extra)
    try:
        session.wait(startup_wait)  # let startup-time tracing happen first
        for c in cmds:
            session.send_line(c)
            session.wait(0.15)
        remaining = timeout - startup_wait - 0.15 * len(cmds)
        session.drain(max(remaining, 0.2))
        return session.text()
    finally:
        session.close()


SHELL_BIN = None


# ──────────────────────────────────────────────────────────────────────────────
# A. --trace on/off gate
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. --trace On/Off Gate ──")
    results = []

    # Default (no --trace flag): g_trace_mode is 0, so NO "[TRACE]" line
    # should ever appear, even though the shell still forks/execs/installs
    # signal handlers internally.
    out = run_traced(SHELL_BIN, ["echo hi", "sleep 1 &", "jobs"],
                      trace=False, timeout=1.8)
    results.append(assert_not_contains(
        "A1  no [TRACE] output at all when --trace is not passed",
        out, "[TRACE]"))
    results.append(assert_contains(
        "A2  shell still functions normally without --trace",
        out, "hi"))

    # With --trace: at least one [TRACE] line must appear (sigaction calls
    # happen at startup, before any command is even typed).
    out2 = run_traced(SHELL_BIN, [], trace=True, timeout=0.8)
    results.append(assert_contains(
        "A3  --trace flag enables at least one [TRACE] line at startup",
        out2, "[TRACE]"))

    # With --trace, running a command produces additional [TRACE] lines
    # (at minimum a fork/execve pair) beyond whatever startup emitted.
    out3 = run_traced(SHELL_BIN, ["echo hi"], trace=True, timeout=1.2)
    trace_line_count = out3.count("[TRACE]")
    results.append(check(
        "A4  running a command adds further [TRACE] lines beyond startup",
        trace_line_count >= 2,
        detail=f"[TRACE] lines seen: {trace_line_count}"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. trace_fork()
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. trace_fork() ──")
    results = []

    out = run_traced(SHELL_BIN, ["echo hi"], trace=True, timeout=1.2)
    results.append(assert_contains(
        "B1  a simple command logs a fork() trace line",
        out, "[TRACE] fork() -> "))

    # The logged pid must be a positive integer, not garbage.
    m = re.search(r"\[TRACE\] fork\(\) -> (-?\d+)", out)
    results.append(check(
        "B2  fork() trace pid is a positive integer",
        bool(m) and int(m.group(1)) > 0,
        detail=f"parsed pid={m.group(1) if m else None}"))

    # A two-stage pipeline forks twice, so two distinct fork() trace lines
    # (potentially with different pids) should appear.
    out2 = run_traced(SHELL_BIN, ["echo hi | cat"], trace=True, timeout=1.4)
    fork_lines = re.findall(r"\[TRACE\] fork\(\) -> (-?\d+)", out2)
    results.append(check(
        "B3  a two-stage pipeline logs two fork() trace lines",
        len(fork_lines) >= 2,
        detail=f"fork lines found: {fork_lines}"))
    results.append(check(
        "B4  pipeline's two forked pids are distinct",
        len(fork_lines) >= 2 and len(set(fork_lines)) == len(fork_lines),
        detail=f"pids: {fork_lines}"))

    # Backgrounding also goes through fork(), same as foreground.
    out3 = run_traced(SHELL_BIN, ["sleep 1 &"], trace=True, timeout=1.2)
    results.append(assert_contains(
        "B5  backgrounding a job also logs a fork() trace line",
        out3, "[TRACE] fork() -> "))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. trace_execve()
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. trace_execve() ──")
    results = []

    # Use an absolute path so the asserted string can't be thrown off by
    # whatever $PATH-resolution scheme the shell uses internally.
    out = run_traced(SHELL_BIN, ["/bin/echo hi"], trace=True, timeout=1.2)
    results.append(assert_contains(
        "C1  running an absolute-path command logs its exact execve() path",
        out, "[TRACE] execve() path=/bin/echo"))

    # A pipeline logs one execve() trace line per stage.
    out2 = run_traced(SHELL_BIN, ["/bin/echo hi | /bin/cat"],
                       trace=True, timeout=1.4)
    exec_paths = re.findall(r"\[TRACE\] execve\(\) path=(\S+)", out2)
    results.append(check(
        "C2  a two-stage pipeline logs two execve() trace lines",
        len(exec_paths) >= 2,
        detail=f"paths found: {exec_paths}"))
    results.append(check(
        "C3  pipeline execve() paths match the commands run",
        any("echo" in p for p in exec_paths) and any("cat" in p for p in exec_paths),
        detail=f"paths found: {exec_paths}"))

    # A nonexistent command should still attempt (and log) the execve()
    # call before failing — the trace fires regardless of exec's outcome.
    out3 = run_traced(SHELL_BIN, ["/bin/does_not_exist_xyz"],
                       trace=True, timeout=1.0)
    results.append(assert_contains(
        "C4  execve() trace is logged even for a path that will fail to exec",
        out3, "[TRACE] execve() path=/bin/does_not_exist_xyz"))

    # Shell must still be alive/responsive after the failed exec above.
    out4 = run_traced(SHELL_BIN,
                       ["/bin/does_not_exist_xyz", "echo still_alive"],
                       trace=True, timeout=1.2)
    results.append(assert_contains(
        "C5  shell remains responsive after a failed execve()",
        out4, "still_alive"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# D. trace_sigaction()
# ──────────────────────────────────────────────────────────────────────────────

def section_d():
    print("\n── D. trace_sigaction() ──")
    results = []

    # Signal handler installation happens once at shell startup, before
    # the first prompt/command — so we look for it with an empty command
    # list (nothing typed at all).
    out = run_traced(SHELL_BIN, [], trace=True, timeout=0.8)
    results.append(assert_contains(
        "D1  sigaction() trace lines appear at shell startup",
        out, "[TRACE] sigaction("))

    sigaction_signums = set(
        int(n) for n in re.findall(r"\[TRACE\] sigaction\((-?\d+), \.\.\.\)", out)
    )
    results.append(check(
        "D2  at least one sigaction() call is logged",
        len(sigaction_signums) >= 1,
        detail=f"signums logged: {sorted(sigaction_signums)}"))

    # jobs.c/signals.c is known (per test_signals.py) to install a SIGCHLD
    # handler with SA_RESTART for background-job reaping, so its numeric
    # signal value should show up among the traced sigaction() calls.
    results.append(check(
        "D3  SIGCHLD handler installation is traced at startup",
        int(pysignal.SIGCHLD) in sigaction_signums,
        detail=f"SIGCHLD={int(pysignal.SIGCHLD)}, "
               f"signums logged: {sorted(sigaction_signums)}"))

    # Startup tracing happens before the shell can have processed any
    # input at all — running a no-op session (timeout right after launch)
    # should already contain the sigaction lines, not just after a command.
    out2 = run_traced(SHELL_BIN, [], trace=True, timeout=0.5, startup_wait=0.15)
    results.append(assert_contains(
        "D4  sigaction() trace lines appear even with no commands sent",
        out2, "[TRACE] sigaction("))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# E. Ordering / non-interference with normal shell behavior
# ──────────────────────────────────────────────────────────────────────────────

def section_e():
    print("\n── E. Trace Output Does Not Corrupt Normal Behavior ──")
    results = []

    # Trace output must not corrupt or replace the actual command output.
    out = run_traced(SHELL_BIN, ["echo hello_world"], trace=True, timeout=1.0)
    results.append(assert_contains(
        "E1  command's real stdout output is still present alongside tracing",
        out, "hello_world"))

    # The shell should still print its prompt normally with tracing on.
    results.append(assert_contains(
        "E2  prompt still appears with --trace enabled",
        out, "posixsh>"))

    # Multiple commands in one session should each get their own
    # fork()/execve() trace pair, and normal output for both should
    # still show up correctly (tracing shouldn't drop or merge commands).
    out2 = run_traced(SHELL_BIN, ["echo first_cmd", "echo second_cmd"],
                       trace=True, timeout=1.4)
    results.append(assert_contains("E3  first command output present", out2, "first_cmd"))
    results.append(assert_contains("E4  second command output present", out2, "second_cmd"))
    fork_count = out2.count("[TRACE] fork() -> ")
    results.append(check(
        "E5  two separate commands produce two separate fork() trace lines",
        fork_count >= 2,
        detail=f"fork() trace lines: {fork_count}"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    global SHELL_BIN

    print("=" * 55)
    print("  Phase 5 — Trace Mode Tests")
    print("=" * 55)

    compile_shell()
    SHELL_BIN = _find_shell_binary()

    all_results = []
    all_results += section_a()
    all_results += section_b()
    all_results += section_c()
    all_results += section_d()
    all_results += section_e()

    total = len(all_results)
    passed = sum(1 for r in all_results if r)

    print(f"\n{'='*55}")
    print(f"  Result: {passed}/{total} passed")
    print(f"{'='*55}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())