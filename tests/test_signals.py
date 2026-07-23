#!/usr/bin/env python3
"""
tests/test_signals.py — Phase 4: Signal handling tests
    (SIGINT/SIGTSTP on foreground, SIGCHLD reaping, SA_RESTART,
     child signal reset)

Strategy:
    Unlike the other Phase 3/4 test files, these tests need REAL signals
    (SIGINT, SIGTSTP, SIGQUIT) delivered to a foreground child at a precise
    moment — a plain "write all lines, read output after N seconds" harness
    can't guarantee that timing. So this file talks to the shell directly
    over a pseudo-terminal (pty.fork()) and performs *timed* raw writes:
    a shell command line, a short wait (to make sure the child has actually
    forked+exec'd and is running in the foreground), then a raw control
    byte (e.g. b"\\x03" for Ctrl-C), then more waits/lines as needed.

    Because a real tty is used, typing Ctrl-C/Ctrl-Z generates the actual
    SIGINT/SIGTSTP the kernel would generate for any interactive shell —
    exercising the exact code path in signals.c, not a simulation of it.

    We still call helper.compile_shell() to build the binary using your
    existing build step. We only need to *locate* the resulting binary
    ourselves; see _find_shell_binary() below.

FINDING THE BINARY:
    Set SHELL_BINARY=/path/to/your/shell if it isn't found automatically.

WHAT EACH SECTION VALIDATES:
    A. SIGINT on a foreground job   — Ctrl-C kills the foreground child
                                       (default disposition), the shell
                                       itself is unaffected and stays alive.
    B. SIGTSTP on a foreground job  — Ctrl-Z stops the foreground child;
                                       it shows up as Stopped in `jobs`;
                                       the shell is unaffected.
    C. SIGCHLD / SA_RESTART         — a background job finishing while the
                                       shell is blocked in read() with an
                                       empty input buffer must not corrupt
                                       or hang the next line of input.
    D. Child signal reset           — verifies reset_child_signals() really
                                       restores SIG_DFL for SIGINT and
                                       SIGQUIT in the child (if it didn't,
                                       the child would inherit the shell's
                                       SIG_IGN and the signal would have
                                       no effect, so the child would run
                                       to completion instead of dying early).
    E. Shell's own signal ignoring  — Ctrl-C/Ctrl-Z at an idle prompt (no
                                       foreground child) must not kill or
                                       confuse the shell itself.
"""

import sys
import os
import time
import signal
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
    matches (e.g. timing assertions). Mirrors the look of assert_contains."""
    status = "PASS" if condition else "FAIL"
    suffix = f"  ({detail})" if detail else ""
    print(f"  {name} ... {status}{suffix}")
    return condition


class Session:
    """
    A single pty-backed shell session with support for timed raw writes.
    """

    def __init__(self, binary):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            # child: exec the shell under test
            try:
                os.execv(binary, [binary])
            except Exception:
                os._exit(127)
        self.output = b""

    def send_line(self, text):
        os.write(self.fd, (text + "\n").encode())

    def send_raw(self, raw_bytes):
        os.write(self.fd, raw_bytes)

    def wait(self, seconds):
        """Sleep while still draining any output that arrives, so pipes
        never fill up and block the child."""
        end = time.time() + seconds
        while True:
            remaining = end - time.time()
            if remaining <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], remaining)
            if self.fd in r:
                self._read_available()

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
            os.killpg(pgid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.kill(self.pid, signal.SIGKILL)
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


def run_timed(binary, steps, tail_drain=0.5):
    """
    steps: list of tuples, each one of:
        ("line", "<command text>")   -- send text + newline
        ("raw",  b"<bytes>")         -- send raw bytes (e.g. control chars)
        ("wait", <seconds>)          -- sleep (while draining output)
    Returns (captured_text, elapsed_seconds).
    """
    session = Session(binary)
    start = time.time()
    try:
        for kind, payload in steps:
            if kind == "line":
                session.send_line(payload)
            elif kind == "raw":
                session.send_raw(payload)
            elif kind == "wait":
                session.wait(payload)
            else:
                raise ValueError(f"unknown step kind: {kind}")
        session.drain(tail_drain)
        elapsed = time.time() - start
        return session.text(), elapsed
    finally:
        session.close()


SHELL_BIN = None


# ──────────────────────────────────────────────────────────────────────────────
# A. SIGINT on a foreground job
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. SIGINT on Foreground Job (Ctrl-C) ──")
    results = []

    # Ctrl-C sent to a running "sleep 2" should kill it almost immediately —
    # well before the natural 2s completion — and the shell should still be
    # alive and responsive afterward.
    out, elapsed = run_timed(SHELL_BIN, [
        ("line", "sleep 2"),
        ("wait", 0.3),          # ensure sleep has actually forked/exec'd
        ("raw", b"\x03"),       # Ctrl-C -> SIGINT to foreground pgrp
        ("wait", 0.3),
        ("line", "echo alive_after_sigint"),
        ("wait", 0.4),
    ])
    results.append(check("A1  Ctrl-C interrupts foreground sleep well before it finishes",
                         elapsed < 1.6,
                         detail=f"elapsed={elapsed:.2f}s (expected < 1.6s, not ~2s+)"))
    results.append(assert_contains("A2  shell remains responsive after SIGINT", out, "alive_after_sigint"))

    # Shell process itself must not have been killed by the Ctrl-C (SIGINT
    # is SIG_IGN in the shell's own disposition — only the foreground pgrp's
    # child should be affected).
    out2, _ = run_timed(SHELL_BIN, [
        ("line", "sleep 2"),
        ("wait", 0.3),
        ("raw", b"\x03"),
        ("wait", 0.3),
        ("line", "echo second_command_ok"),
        ("wait", 0.3),
        ("line", "echo third_command_ok"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("A3  multiple commands still work after SIGINT", out2, "second_command_ok"))
    results.append(assert_contains("A4  shell keeps processing commands after SIGINT", out2, "third_command_ok"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. SIGTSTP on a foreground job
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. SIGTSTP on Foreground Job (Ctrl-Z) ──")
    results = []

    # Ctrl-Z should stop (not kill) the foreground child and return control
    # to the shell promptly; `jobs` should then show it as Stopped.
    out, elapsed = run_timed(SHELL_BIN, [
        ("line", "sleep 5"),
        ("wait", 0.3),
        ("raw", b"\x1a"),       # Ctrl-Z -> SIGTSTP to foreground pgrp
        ("wait", 0.3),
        ("line", "jobs"),
        ("wait", 0.3),
    ])
    results.append(check("B1  Ctrl-Z returns control well before the 5s sleep finishes",
                         elapsed < 1.5,
                         detail=f"elapsed={elapsed:.2f}s"))
    results.append(assert_contains("B2  stopped job appears with Stopped state", out, "Stopped"))
    results.append(assert_contains("B3  stopped job's command text is preserved", out, "sleep 5"))

    # Shell remains alive/responsive after handling SIGTSTP on a child
    out2, _ = run_timed(SHELL_BIN, [
        ("line", "sleep 5"),
        ("wait", 0.3),
        ("raw", b"\x1a"),
        ("wait", 0.3),
        ("line", "echo still_alive_after_sigtstp"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("B4  shell remains responsive after SIGTSTP", out2, "still_alive_after_sigtstp"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. SIGCHLD reaping / SA_RESTART
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. SIGCHLD Reaping & SA_RESTART ──")
    results = []

    # Basic reaping: a background job's completion is reported.
    out, _ = run_timed(SHELL_BIN, [
        ("line", "sleep 1 &"),
        ("wait", 1.4),          # job exits while shell is blocked on read()
        ("line", "echo after_reap"),
        ("wait", 0.4),
    ])
    results.append(assert_contains("C1  background job completion is reported (Done)", out, "Done"))
    results.append(assert_contains("C2  shell's read() resumes normally after SIGCHLD", out, "after_reap"))

    # SA_RESTART specifically: the shell must have been blocked in read()
    # with an EMPTY input buffer when SIGCHLD fires (no pending bytes),
    # since that is exactly the scenario SA_RESTART protects against
    # (a blocking read() getting EINTR). We enforce the empty-buffer window
    # via the "wait" step above, which sends nothing while sleeping.
    # If SA_RESTART were missing/mishandled, a naive implementation might
    # error out, skip the next line, or hang — any of which would cause
    # "after_reap" (checked above) to be missing or garbled.
    out2, _ = run_timed(SHELL_BIN, [
        ("line", "sleep 1 &"),
        ("wait", 1.4),
        ("line", "echo sa_restart_ok_1"),
        ("wait", 0.3),
        ("line", "echo sa_restart_ok_2"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("C3  first post-SIGCHLD command executes correctly", out2, "sa_restart_ok_1"))
    results.append(assert_contains("C4  second post-SIGCHLD command executes correctly", out2, "sa_restart_ok_2"))

    # Multiple background jobs finishing close together should all be
    # reaped without the shell losing track of subsequent input.
    out3, _ = run_timed(SHELL_BIN, [
        ("line", "sleep 1 &"),
        ("line", "sleep 1 &"),
        ("wait", 1.5),
        ("line", "echo multi_reap_ok"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("C5  multiple simultaneous SIGCHLDs don't break input handling", out3, "multi_reap_ok"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# D. Child signal reset (reset_child_signals)
# ──────────────────────────────────────────────────────────────────────────────

def section_d():
    print("\n── D. Child Signal Reset ──")
    results = []

    # If reset_child_signals() did NOT restore SIG_DFL for SIGINT, the child
    # would inherit the shell's SIG_IGN and Ctrl-C would have no effect —
    # "sleep 2" would run to completion (elapsed ~2s) instead of dying early.
    out, elapsed = run_timed(SHELL_BIN, [
    ("line", "sleep 20"),
    ("wait", 0.3),
    ("raw", b"\x03"),
    ("wait", 0.3),                  # shell should already be back
    ("line", "echo reset_sigint_ok"),
    ("wait", 0.3),
    ])

    results.append(check(
    "D1  Ctrl-C interrupts foreground child and returns prompt",
    "posixsh>" in out,
    detail=f"elapsed={elapsed:.2f}s"
    ))

    results.append(assert_contains(
        "D2  shell recovers after confirming SIGINT reset",
        out,
        "reset_sigint_ok"
    ))

    # Same check for SIGQUIT (Ctrl-\, 0x1c) — also SIG_IGN in the shell,
    # also restored to SIG_DFL in reset_child_signals().
    out2, elapsed2 = run_timed(SHELL_BIN, [
        ("line", "sleep 2"),
        ("wait", 0.3),
        ("raw", b"\x1c"),       # Ctrl-\ -> SIGQUIT
        ("wait", 0.3),
        ("line", "echo reset_sigquit_ok"),
        ("wait", 0.3),
    ])
    results.append(check("D3  child does NOT inherit shell's SIGQUIT=IGN (dies early)",
                         elapsed2 < 1.6,
                         detail=f"elapsed={elapsed2:.2f}s"))
    results.append(assert_contains("D4  shell recovers after confirming SIGQUIT reset", out2, "reset_sigquit_ok"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# E. Shell's own signal dispositions at an idle prompt
# ──────────────────────────────────────────────────────────────────────────────

def section_e():
    print("\n── E. Shell Ignores Signals At Idle Prompt ──")
    results = []

    # With no foreground child running, Ctrl-C must not kill/confuse the
    # shell itself (SIGINT is SIG_IGN in the shell's own process).
    out, _ = run_timed(SHELL_BIN, [
        ("wait", 0.2),
        ("raw", b"\x03"),
        ("wait", 0.2),
        ("line", "echo shell_survived_idle_sigint"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("E1  Ctrl-C at idle prompt does not kill the shell", out, "shell_survived_idle_sigint"))

    # Same for Ctrl-Z at idle prompt (SIGTSTP is SIG_IGN in the shell itself)
    out2, _ = run_timed(SHELL_BIN, [
        ("wait", 0.2),
        ("raw", b"\x1a"),
        ("wait", 0.2),
        ("line", "echo shell_survived_idle_sigtstp"),
        ("wait", 0.3),
    ])
    results.append(assert_contains("E2  Ctrl-Z at idle prompt does not stop the shell", out2, "shell_survived_idle_sigtstp"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    global SHELL_BIN

    print("=" * 55)
    print("  Phase 4 — Signal Handling Tests")
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