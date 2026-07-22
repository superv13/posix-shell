#!/usr/bin/env python3
"""
tests/test_phase3.py

Purpose:
    Automated test suite for Phase 3 (Execution Engine) of the educational
    POSIX shell: pipelines, redirection, and background execution.

Why a pty instead of a plain pipe:
    posixsh's read loop does one sys_read() per prompt and assumes that
    call returns exactly one line. That assumption only holds under a
    real terminal's line discipline (canonical mode), which buffers input
    per-line. If you instead pipe a multi-line script straight into
    posixsh's stdin (`printf "a\\nb\\n" | ./posixsh`), the kernel may
    hand back *all* buffered bytes to a single sys_read(), and posixsh
    will silently parse only the first line and discard the rest.

    Opening a pseudo-terminal with Python's `pty` module gives posixsh a
    real tty, so each line we send is delivered to a separate sys_read(),
    exactly like a human typing at a keyboard. This is required for any
    multi-command test scenario.

Usage:
    python3 tests/test_phase3.py
    python3 tests/test_phase3.py -v        # show full transcript per test

Exit code:
    0 if all tests passed, 1 otherwise (useful in CI / make check).
"""

import os
import pty
import select
import subprocess
import sys
import time

SHELL_BINARY = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "posixsh"
)

VERBOSE = "-v" in sys.argv


class Session:
    """One posixsh process attached to a pseudo-terminal."""

    def __init__(self):
        master, slave = pty.openpty()
        self.master = master
        self.proc = subprocess.Popen(
            [SHELL_BINARY], stdin=slave, stdout=slave, stderr=slave
        )
        os.close(slave)
        self.buffer = ""

    def send_line(self, line, idle_timeout=0.4, max_wait=5.0):
        """
        Writes one line + newline, then reads until output goes quiet
        for `idle_timeout` seconds (or `max_wait` total elapses).
        Returns the newly produced output since the last send_line().
        """
        os.write(self.master, (line + "\n").encode())

        start = time.time()
        last_data = time.time()
        chunk_acc = b""

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
                chunk_acc += data
                last_data = time.time()

        text = chunk_acc.decode(errors="replace")
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
# Test framework
# ---------------------------------------------------------------------------

results = []  # (name, passed, detail)


def record(name, passed, detail=""):
    results.append((name, passed, detail))
    mark = "PASS" if passed else "FAIL"
    print(f"[{mark}] {name}")
    if VERBOSE or not passed:
        if detail:
            print(f"        {detail}")


def expect_contains(name, output, needle):
    ok = needle in output
    record(
        name,
        ok,
        f"expected to find {needle!r} in output:\n{output!r}" if not ok else "",
    )
    return ok


def expect_not_contains(name, output, needle):
    ok = needle not in output
    record(
        name,
        ok,
        f"did NOT expect {needle!r} in output:\n{output!r}" if not ok else "",
    )
    return ok


def expect_file_contents(name, path, expected_text):
    try:
        with open(path) as f:
            actual = f.read()
    except OSError as e:
        record(name, False, f"could not read {path}: {e}")
        return False
    ok = actual == expected_text
    record(
        name,
        ok,
        f"{path}: expected {expected_text!r}, got {actual!r}" if not ok else "",
    )
    return ok


# ---------------------------------------------------------------------------
# Individual test cases
# ---------------------------------------------------------------------------

def test_single_external_command():
    s = Session()
    out = s.send_line("echo hello")
    expect_contains("single external command (echo)", out, "hello")
    s.send_line("exit")
    s.close()


def test_path_lookup_vs_absolute():
    s = Session()
    out1 = s.send_line("ls /tmp >/dev/null && echo OK1")
    # Note: posixsh has no && operator (that's fine -- this just checks
    # PATH-resolved "ls" runs without "command not found").
    out1 = s.send_line("ls /tmp")
    expect_not_contains("PATH lookup resolves 'ls'", out1, "command not found")
    s.send_line("exit")
    s.close()


def test_builtins_unaffected():
    s = Session()
    out_pwd1 = s.send_line("pwd")
    s.send_line("cd /tmp")
    out_pwd2 = s.send_line("pwd")
    ok1 = expect_contains("pwd reports a path", out_pwd1, "/")
    ok2 = expect_contains("cd /tmp then pwd shows /tmp", out_pwd2, "/tmp")
    s.send_line("exit")
    s.close()
    return ok1 and ok2


def test_cd_nonexistent_directory():
    s = Session()
    out = s.send_line("cd /this/path/does/not/exist")
    expect_contains(
        "cd to missing directory reports an error",
        out,
        "cd:",
    )
    s.send_line("exit")
    s.close()


def test_redirect_overwrite():
    path = "/tmp/posixsh_test_overwrite.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    s.send_line(f"echo first > {path}")
    s.send_line(f"echo second > {path}")
    s.send_line("exit")
    s.close()
    expect_file_contents("'>' truncates on each use", path, "second\n")


def test_redirect_append():
    path = "/tmp/posixsh_test_append.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    s.send_line(f"echo first > {path}")
    s.send_line(f"echo second >> {path}")
    s.send_line("exit")
    s.close()
    expect_file_contents("'>>' appends instead of truncating", path, "first\nsecond\n")


def test_redirect_input():
    path = "/tmp/posixsh_test_input.txt"
    with open(path, "w") as f:
        f.write("line-from-file\n")
    s = Session()
    out = s.send_line(f"cat < {path}")
    s.send_line("exit")
    s.close()
    expect_contains("'<' feeds file contents to stdin", out, "line-from-file")


def test_pipe_and_redirect_combined():
    path = "/tmp/posixsh_test_combo.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    s.send_line(f"ls /tmp | grep posixsh_test > {path}")
    s.send_line("exit")
    s.close()
    try:
        with open(path) as f:
            content = f.read()
    except OSError as e:
        record("pipe + redirect combined", False, str(e))
        return
    record(
        "pipe + redirect combined ('ls | grep > file')",
        "posixsh_test" in content,
        f"file content: {content!r}",
    )


def test_three_stage_pipeline():
    s = Session()
    out = s.send_line("printf 'a\\nb\\na\\n' | grep a | wc -l")
    s.send_line("exit")
    s.close()
    # We don't control whitespace formatting of wc -l, just check the digit.
    expect_contains("3-stage pipeline produces correct count", out, "2")


def test_long_pipeline_no_hang():
    """
    Producer/consumer test: 'yes' produces infinite output; 'head -n 5'
    only wants 5 lines. If the executor leaked pipe fds (forgot to close
    unused ends in a child), 'yes' would never see EOF/SIGPIPE and this
    pipeline could hang forever instead of completing.
    """
    s = Session()
    start = time.time()
    out = s.send_line("yes | head -n 5 | wc -l", max_wait=4.0)
    elapsed = time.time() - start
    s.send_line("exit")
    s.close()
    ok = "5" in out and elapsed < 3.5
    record(
        "long pipeline with early-exiting consumer doesn't hang (pipe fd hygiene)",
        ok,
        f"elapsed={elapsed:.2f}s output={out!r}",
    )


def test_background_does_not_block():
    s = Session()
    start = time.time()
    out = s.send_line("sleep 3 &", idle_timeout=0.3, max_wait=2.0)
    elapsed = time.time() - start
    out2 = s.send_line("echo after_bg")
    s.send_line("exit")
    s.close()
    fast = elapsed < 1.0
    record("'sleep 3 &' returns to prompt immediately", fast, f"elapsed={elapsed:.2f}s")
    expect_contains("shell stays responsive after launching background job", out2, "after_bg")


def test_command_not_found():
    s = Session()
    out = s.send_line("this_command_should_not_exist_xyz")
    s.send_line("exit")
    s.close()
    expect_contains("unknown command reports a clean error", out, "command not found")


def test_empty_input_line():
    s = Session()
    out = s.send_line("")
    out2 = s.send_line("echo still_alive")
    s.send_line("exit")
    s.close()
    expect_contains("blank line doesn't crash the shell", out2, "still_alive")


def test_bare_redirection_no_command():
    path = "/tmp/posixsh_test_bare_redirect.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    out = s.send_line(f"> {path}")
    out2 = s.send_line("echo still_alive_2")
    s.send_line("exit")
    s.close()
    file_created = os.path.exists(path)
    record("bare '> file' with no command just touches the file", file_created)
    expect_contains("shell survives a command-less redirection", out2, "still_alive_2")


def test_pipeline_depth_limit():
    """
    constants.h defines MAX_PIPELINE_DEPTH=8. A 9-stage pipeline should be
    handled without overflowing any fixed-size array.

    Uses 'true' (exits immediately, never reads stdin) rather than 'cat'
    (which would block reading the controlling terminal forever when given
    no input source -- that's correct behavior, identical to real bash,
    but it would hang this specific test rather than exercising the limit).
    """
    s = Session()
    stages = " | ".join(["true"] * 9)
    out = s.send_line(stages, max_wait=2.0)
    out2 = s.send_line("echo still_alive_3")
    s.send_line("exit")
    s.close()
    expect_contains(
        "shell survives exceeding MAX_PIPELINE_DEPTH",
        out2,
        "still_alive_3",
    )


def test_multiple_background_jobs():
    s = Session()
    s.send_line("sleep 1 &", max_wait=1.0)
    s.send_line("sleep 1 &", max_wait=1.0)
    out = s.send_line("echo done_launching", max_wait=1.0)
    s.send_line("exit")
    s.close()
    expect_contains("shell can launch multiple background jobs in a row", out, "done_launching")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    if not os.path.isfile(SHELL_BINARY):
        print(f"error: shell binary not found at {SHELL_BINARY}; run `make` first.")
        sys.exit(1)

    tests = [
        test_single_external_command,
        test_path_lookup_vs_absolute,
        test_builtins_unaffected,
        test_cd_nonexistent_directory,
        test_redirect_overwrite,
        test_redirect_append,
        test_redirect_input,
        test_pipe_and_redirect_combined,
        test_three_stage_pipeline,
        test_long_pipeline_no_hang,
        test_background_does_not_block,
        test_command_not_found,
        test_empty_input_line,
        test_bare_redirection_no_command,
        test_pipeline_depth_limit,
        test_multiple_background_jobs,
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
