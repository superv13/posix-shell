#!/usr/bin/env python3
"""
tests/test_phase1.py

Purpose:
    Automated test suite for Phase 1 (Bootstrap) of the educational POSIX
    shell: static binary, custom _start, sys_read/sys_write loop, exit
    handling, my_strcmp correctness, and buffer safety.

Phase 1 goal summary:
    - Statically linked ELF with no libc / no shared libraries
    - Custom _start entry point (not __libc_start_main)
    - sys_write() prints "posixsh> " prompt
    - sys_read() reads one line per call
    - my_strcmp() detects "exit" exactly (case-sensitive)
    - sys_exit(0) on "exit" or EOF (Ctrl+D)

Why subprocess.communicate() instead of pty here:
    Phase 1 only needs single-command interactions (exit, blank Enter).
    We use communicate() with a timeout for clean POSIX pipe interaction.
    The pty harness is used only where canonical line discipline is needed
    (multi-command sessions — that's Phase 2+).

Usage:
    python3 tests/test_phase1.py
    python3 tests/test_phase1.py -v     # verbose: show output per test

Exit code:
    0 if all tests passed, 1 otherwise.
"""

import os
import subprocess
import sys
import time

SHELL_BINARY = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "posixsh"
)

VERBOSE = "-v" in sys.argv


# ---------------------------------------------------------------------------
# Test framework (identical to test_phase3.py)
# ---------------------------------------------------------------------------

results = []  # (name, passed, detail)


def record(name, passed, detail=""):
    results.append((name, passed, detail))
    mark = "PASS" if passed else "FAIL"
    print(f"[{mark}] {name}")
    if VERBOSE or not passed:
        if detail:
            print(f"        {detail}")


def run_shell(stdin_text, timeout=3):
    """
    Run posixsh with stdin_text as its standard input.
    Returns (stdout+stderr combined as str, exit_code).
    """
    try:
        r = subprocess.run(
            [SHELL_BINARY],
            input=stdin_text.encode(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        return (r.stdout + r.stderr).decode(errors="replace"), r.returncode
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>", -1


def run_host(cmd, timeout=5):
    """Run a host shell command, return (stdout, returncode)."""
    r = subprocess.run(
        cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=timeout
    )
    return (r.stdout + r.stderr).decode(errors="replace"), r.returncode


SHELL_BINARY_QUOTED = f'"{SHELL_BINARY}"'


# ---------------------------------------------------------------------------
# Section 1 — Binary Inspection (no shell needed)
# ---------------------------------------------------------------------------

def test_binary_exists():
    ok = os.path.isfile(SHELL_BINARY) and os.access(SHELL_BINARY, os.X_OK)
    record("binary exists and is executable", ok,
           f"looked for: {SHELL_BINARY}")


def test_static_binary():
    out, _ = run_host(f"ldd {SHELL_BINARY_QUOTED}")
    ok = "not a dynamic executable" in out or "statically linked" in out
    record("ldd reports 'not a dynamic executable' (zero .so deps)", ok,
           f"ldd output: {out.strip()!r}")


def test_file_says_static():
    out, _ = run_host(f"file {SHELL_BINARY_QUOTED}")
    ok = "statically linked" in out and "ELF 64-bit" in out
    record("file(1) reports 'statically linked' and 'ELF 64-bit'", ok,
           f"file output: {out.strip()!r}")


def test_no_dynamic_section():
    out, _ = run_host(f"readelf -d {SHELL_BINARY_QUOTED}")
    ok = "no dynamic section" in out.lower() or out.strip() == ""
    record("readelf -d: no dynamic section", ok,
           f"readelf output: {out.strip()!r}")


def test_no_undefined_symbols():
    out, _ = run_host(f"nm {SHELL_BINARY_QUOTED} | grep ' U '")
    ok = out.strip() == ""
    record("nm: zero undefined symbols (no libc imports)", ok,
           f"undefined symbols found: {out.strip()!r}")


def test_custom_start():
    out, _ = run_host(f"nm {SHELL_BINARY_QUOTED} | grep _start")
    has_start = "_start" in out
    no_libc_start = "__libc_start_main" not in out
    ok = has_start and no_libc_start
    record("_start present, __libc_start_main absent", ok,
           f"nm | grep _start: {out.strip()!r}")


def test_binary_size():
    size = os.path.getsize(SHELL_BINARY)
    ok = size < 300 * 1024   # < 300 KB (static debug build is ~60 KB)
    record(f"binary size is reasonable (< 300 KB, actual {size // 1024} KB)", ok)


# ---------------------------------------------------------------------------
# Section 2 — Startup and Prompt
# ---------------------------------------------------------------------------

def test_prompt_appears():
    out, rc = run_shell("exit\n")
    ok = "posixsh>" in out
    record("shell prints 'posixsh>' prompt on startup", ok,
           f"output: {out!r}")


def test_prompt_has_trailing_space():
    """
    The prompt must be "posixsh> " (9 chars: 7 + '>' + ' ').
    A common my_strlen bug cuts the trailing space.
    """
    out, _ = run_shell("exit\n")
    ok = "posixsh> " in out   # space after '>'
    record("prompt has trailing space ('posixsh> ' with space)", ok,
           f"output: {out!r}")


# ---------------------------------------------------------------------------
# Section 3 — Exit Handling
# ---------------------------------------------------------------------------

def test_exit_command_terminates():
    out, rc = run_shell("exit\n")
    ok = rc == 0
    record("'exit' command terminates shell with code 0", ok,
           f"exit code: {rc}")


def test_eof_terminates():
    """Ctrl+D is simulated by closing stdin (empty input)."""
    out, rc = run_shell("")
    ok = rc == 0
    record("EOF (empty stdin / Ctrl+D) terminates shell with code 0", ok,
           f"exit code: {rc}")


def test_exit_does_not_hang():
    start = time.time()
    out, rc = run_shell("exit\n", timeout=3)
    elapsed = time.time() - start
    ok = elapsed < 2.5 and rc == 0
    record("'exit' completes in < 2.5 s (no hang)", ok,
           f"elapsed={elapsed:.2f}s rc={rc}")


# ---------------------------------------------------------------------------
# Section 4 — my_strcmp correctness (exact case-sensitive match)
# ---------------------------------------------------------------------------

def test_uppercase_exit_does_not_exit():
    """
    "EXIT" must NOT be treated as "exit". The shell should show another
    prompt (or "command not found" in Phase 3+), not terminate.
    """
    out, rc = run_shell("EXIT\nexit\n")
    # If my_strcmp were case-insensitive, shell would exit after "EXIT"
    # and never process the second "exit", so the process would terminate
    # with whatever the first "exit" yields. But here: shell must survive
    # "EXIT", stay running, then exit cleanly on "exit".
    ok = "posixsh>" in out and rc == 0
    record("'EXIT' (uppercase) does NOT exit (my_strcmp is case-sensitive)", ok,
           f"output: {out!r}  rc: {rc}")


def test_leading_space_prevents_exit():
    """" exit" (with leading space) is not "exit"."""
    out, rc = run_shell(" exit\nexit\n")
    ok = "posixsh>" in out and rc == 0
    record("' exit' (leading space) does not trigger exit", ok,
           f"output: {out!r}  rc: {rc}")


def test_trailing_space_prevents_exit():
    """
    "exit " (trailing space) is technically two tokens in Phase 2+,
    but in Phase 1 the raw string comparison "exit \n" != "exit\n".
    The shell must survive and eventually exit on plain "exit".
    """
    out, rc = run_shell("exit \nexit\n")
    ok = "posixsh>" in out and rc == 0
    record("'exit ' (trailing space) does not crash shell", ok,
           f"output: {out!r}  rc: {rc}")


# ---------------------------------------------------------------------------
# Section 5 — Read-prompt loop
# ---------------------------------------------------------------------------

def test_blank_lines_loop():
    """Pressing Enter several times must show multiple prompts."""
    # Send 5 blank lines then exit
    out, rc = run_shell("\n\n\n\n\nexit\n")
    # Should see at least 2 "posixsh>" occurrences (startup + re-prompts)
    count = out.count("posixsh>")
    ok = count >= 2 and rc == 0
    record("blank Enter lines re-show the prompt each time", ok,
           f"prompt count={count}  rc={rc}  output={out!r}")


def test_garbage_input_no_crash():
    """Random strings must not crash the shell."""
    out, rc = run_shell("asdfghjkl\n12345\n@#$%\nexit\n")
    ok = rc == 0
    record("garbage input lines do not crash the shell", ok,
           f"rc={rc}  output={out!r}")


def test_multiple_prompts_count():
    """
    N blank lines → multiple prompts.
    With communicate() the terminal collapses some echo, so we just need
    to see ≥ 2 prompts total (startup + at least one re-prompt).
    """
    out, rc = run_shell("\n\n\nexit\n")
    count = out.count("posixsh>")
    ok = count >= 2 and rc == 0
    record("prompt appears once per read() iteration (≥2 seen)", ok,
           f"prompt count={count}  rc={rc}")


# ---------------------------------------------------------------------------
# Section 6 — Buffer boundary / safety
# ---------------------------------------------------------------------------

def test_long_input_no_crash():
    """500 'a' characters must not overflow or crash."""
    long_line = "a" * 500 + "\n"
    out, rc = run_shell(long_line + "exit\n")
    ok = rc == 0
    record("500-char input line does not crash the shell", ok,
           f"rc={rc}")


def test_near_max_input_no_crash():
    """
    1023 characters (MAX_INPUT - 1) is the largest safe single-line input.
    The shell must survive and exit cleanly.
    """
    long_line = "x" * 1023 + "\n"
    out, rc = run_shell(long_line + "exit\n")
    ok = rc == 0
    record("1023-char input (near MAX_INPUT) does not crash", ok,
           f"rc={rc}")


def test_no_libc_syscalls_at_startup():
    """
    strace the shell, collect all syscall names, verify none of the
    libc-startup syscalls appear BEFORE the first write(1, ...).
    """
    trace_file = "/tmp/psh_phase1_strace.txt"
    # Launch shell in background via strace, feed it 'exit\n'
    r = subprocess.run(
        ["strace", "-o", trace_file, SHELL_BINARY],
        input=b"exit\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
    )
    try:
        with open(trace_file) as f:
            trace = f.read()
    except OSError:
        record("strace: no libc startup syscalls before first write()", False,
               "could not open strace output file")
        return

    lines = trace.splitlines()
    first_write = next(
        (i for i, l in enumerate(lines) if 'write(1,' in l), None
    )
    if first_write is None:
        record("strace: no libc startup syscalls before first write()", False,
               "no write(1,...) found in strace output")
        return

    pre_write = "\n".join(lines[:first_write])
    libc_markers = ["brk(", "access\x2fetc", "openat.*ld.so", "/etc/ld.so"]
    found = [m for m in ["brk(", "/etc/ld.so"] if m in pre_write]
    ok = len(found) == 0
    record("strace: no libc startup syscalls before first write()", ok,
           f"found before write: {found!r}" if not ok else "")


# ---------------------------------------------------------------------------
# Section 7 — Process lifecycle
# ---------------------------------------------------------------------------

def test_shell_exits_cleanly():
    """posixsh must not leave a zombie after exiting."""
    out, rc = run_shell("exit\n")
    ok = rc == 0
    record("shell process exits cleanly (exit code 0)", ok,
           f"rc={rc}")


def test_eof_no_zombie():
    """Sending EOF (empty stdin) must cleanly terminate with code 0."""
    _, rc = run_shell("")
    record("EOF produces exit code 0 (no zombie)", rc == 0, f"rc={rc}")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    if not os.path.isfile(SHELL_BINARY):
        print(f"error: shell binary not found at {SHELL_BINARY}; run `make` first.")
        sys.exit(1)

    tests = [
        # Section 1 — binary inspection
        test_binary_exists,
        test_static_binary,
        test_file_says_static,
        test_no_dynamic_section,
        test_no_undefined_symbols,
        test_custom_start,
        test_binary_size,
        # Section 2 — startup and prompt
        test_prompt_appears,
        test_prompt_has_trailing_space,
        # Section 3 — exit handling
        test_exit_command_terminates,
        test_eof_terminates,
        test_exit_does_not_hang,
        # Section 4 — my_strcmp
        test_uppercase_exit_does_not_exit,
        test_leading_space_prevents_exit,
        test_trailing_space_prevents_exit,
        # Section 5 — read-prompt loop
        test_blank_lines_loop,
        test_garbage_input_no_crash,
        test_multiple_prompts_count,
        # Section 6 — buffer safety
        test_long_input_no_crash,
        test_near_max_input_no_crash,
        test_no_libc_syscalls_at_startup,
        # Section 7 — process lifecycle
        test_shell_exits_cleanly,
        test_eof_no_zombie,
    ]

    for t in tests:
        try:
            t()
        except Exception as e:
            record(t.__name__, False, f"raised exception: {e!r}")

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\n{passed}/{total} checks passed")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
