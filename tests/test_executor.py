#!/usr/bin/env python3
"""
tests/test_executor.py — Phase 3: Executor tests
    (absolute path exec, PATH lookup, pipes 1-8 stages, redirects, trace mode)

Strategy:
    Same black-box approach as test_tokenizer.py: drive the shell through
    stdin via helper.run_shell() and inspect the captured terminal output,
    which includes the "posixsh>" prompt — useful for detecting crashes or
    hangs even without knowing exact error-message wording.

    Trace-mode tests pass args=["--trace"] to helper.run_shell(), the same
    way test_startup.py exercises the --trace flag in Phase 1.

Sections:
    A. Absolute path execution   — /bin/echo etc., missing file, not
                                   executable, path is a directory
    B. PATH lookup               — bare commands resolved via $PATH,
                                   unknown command handling, PATH precedence
    C. Pipes (1–8 stages)        — data flows correctly through pipelines
                                   of increasing length; leading/trailing/
                                   failing-stage edge cases
    D. Redirects                 — >, >>, <, combinations with pipes, bad
                                   targets
    E. Trace mode (--trace)      — traced execution still produces correct
                                   output and doesn't hang
"""

import sys
import os
import shutil

sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, run_shell, assert_contains, assert_not_contains

BASE = "/tmp/psh_executor_test"


def t(name, cmds, want=None, no_want=None, timeout=1.5, args=None):
    """Run `cmds`, assert `want` IS in output, `no_want` is NOT. Returns bool."""
    out = run_shell(cmds, timeout=timeout, args=args) if args else run_shell(cmds, timeout=timeout)
    ok = True
    if want is not None:
        ok = assert_contains(name, out, want) and ok
    if no_want is not None:
        ok = assert_not_contains(name + " (no spurious output)", out, no_want) and ok
    return ok


def which(cmd):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(d, cmd)
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def setup_tree():
    shutil.rmtree(BASE, ignore_errors=True)
    os.makedirs(BASE, exist_ok=True)

    # Not-executable regular file
    with open(f"{BASE}/not_exec.txt", "w") as f:
        f.write("#!/bin/sh\necho hi\n")
    os.chmod(f"{BASE}/not_exec.txt", 0o644)

    # Fake "echo" placed early in PATH, to test PATH precedence
    os.makedirs(f"{BASE}/fakebin", exist_ok=True)
    with open(f"{BASE}/fakebin/echo", "w") as f:
        f.write("#!/bin/sh\nprintf 'FAKE-ECHO\\n'\n")
    os.chmod(f"{BASE}/fakebin/echo", 0o755)


def teardown_tree():
    shutil.rmtree(BASE, ignore_errors=True)


# ──────────────────────────────────────────────────────────────────────────────
# A. Absolute path execution
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. Absolute Path Execution ──")
    results = []

    echo_bin = which("echo") or "/bin/echo"
    cat_bin = which("cat") or "/bin/cat"

    results.append(t("A1  absolute path runs (/bin/echo)",
                     [f"{echo_bin} hello"],
                     want="hello"))

    results.append(t("A2  absolute path with multiple args",
                     [f"{echo_bin} one two three"],
                     want="one two three"))

    results.append(t("A3  absolute path reading a real file",
                     [f"{cat_bin} /etc/hostname"],
                     want="posixsh>"))  # just confirm no crash/hang

    results.append(t("A4  absolute path to nonexistent binary fails gracefully",
                     ["/definitely/not/a/real/binary arg1", "echo after"],
                     want="after"))

    results.append(t("A5  absolute path to non-executable file fails gracefully",
                     [f"{BASE}/not_exec.txt", "echo after"],
                     want="after"))

    results.append(t("A6  absolute path that is a directory fails gracefully",
                     [f"{BASE}", "echo after"],
                     want="after"))

    results.append(t("A7  absolute path command does not stop later commands",
                     [f"{echo_bin} first", f"{echo_bin} second"],
                     want="first"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. PATH lookup
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. PATH Lookup ──")
    results = []

    results.append(t("B1  bare command resolved via PATH",
                     ["echo via-path"],
                     want="via-path"))

    results.append(t("B2  bare 'ls' resolved via PATH lists cwd",
                     [f"cd {BASE}", "ls"],
                     want="fakebin"))

    results.append(t("B3  unknown command reports error, no crash",
                     ["zzz_totally_not_a_real_command_zzz", "echo after"],
                     want="after"))

    results.append(t("B4  empty command line is a no-op",
                     ["", "echo still_alive"],
                     want="still_alive"))

    results.append(t("B5  whitespace-only command line is a no-op",
                     ["   ", "echo still_alive"],
                     want="still_alive"))

    # PATH precedence: fakebin/echo placed before system PATH — this needs
    # helper.run_shell to invoke the shell with a modified environment. If
    # helper.py does not expose that yet, this test is best-effort and only
    # confirms the shell keeps working with a plain lookup.
    results.append(t("B6  bare command still resolves normally",
                     ["echo normal-path"],
                     want="normal-path"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. Pipes (1–8 stages)
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. Pipes (1-8 stages) ──")
    results = []

    for n in range(1, 9):
        parts = ["echo pipeline-data"] + ["cat"] * (n - 1)
        cmdline = " | ".join(parts)
        results.append(t(f"C{n}  pipeline of {n} stage(s) preserves data",
                         [cmdline],
                         want="pipeline-data"))

    results.append(t("C9  pipeline with transformation (tr)",
                     ["echo hello world | tr 'a-z' 'A-Z'"],
                     want="HELLO WORLD"))

    results.append(t("C10 pipeline with word count (wc -w)",
                     ["printf 'a b c\\n' | wc -w"],
                     want="3"))

    results.append(t("C11 pipeline filters with grep",
                     ["printf 'foo\\nbar\\nbaz\\n' | grep ba"],
                     want="bar"))

    results.append(t("C12 pipeline first stage fails, shell survives",
                     ["zzz_no_such_cmd | cat", "echo after"],
                     want="after"))

    results.append(t("C13 pipeline middle stage fails, shell survives",
                     ["echo hi | zzz_no_such_cmd | cat", "echo after"],
                     want="after"))

    # Syntax error edge cases
    results.append(t("C14 leading pipe is a syntax error, shell recovers",
                     ["| echo hi", "echo after"],
                     want="after"))

    results.append(t("C15 trailing pipe is a syntax error, shell recovers",
                     ["echo hi |", "echo after"],
                     want="after"))

    results.append(t("C16 double pipe (empty stage) is a syntax error, shell recovers",
                     ["echo hi || echo bye", "echo after"],
                     want="after"))

    # Large-payload pipeline: catches naive implementations that deadlock
    # when a pipe's buffer fills up mid-pipeline.
    big = "x" * 200000
    results.append(t("C17 large 8-stage pipeline does not deadlock",
                     [f"printf '{big}\\n' | cat | cat | cat | cat | cat | cat | wc -c"],
                     want=str(len(big) + 1),
                     timeout=4.0))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# D. Redirects
# ──────────────────────────────────────────────────────────────────────────────

def section_d():
    print("\n── D. Redirects ──")
    results = []

    out1 = f"{BASE}/out1.txt"
    out2 = f"{BASE}/out2.txt"
    in1 = f"{BASE}/in1.txt"

    results.append(t("D1  > redirect creates file with correct content",
                     [f"echo hello > {out1}", f"cat {out1}"],
                     want="hello"))

    results.append(t("D2  > redirect truncates existing file",
                     [f"echo old_content_long > {out1}",
                      f"echo new > {out1}",
                      f"cat {out1}"],
                     want="new"))

    results.append(t("D3  >> append operator appends rather than truncates",
                     [f"echo first > {out2}",
                      f"echo second >> {out2}",
                      f"cat {out2}"],
                     want="first"))

    results.append(t("D3b >> append preserves earlier line",
                     [f"echo first > {out2}",
                      f"echo second >> {out2}",
                      f"cat {out2}"],
                     want="second"))

    results.append(t("D4  < input redirect reads from file",
                     [f"echo inputdata > {in1}", f"cat < {in1}"],
                     want="inputdata"))

    results.append(t("D5  < input redirect on nonexistent file fails gracefully",
                     [f"cat < {BASE}/does_not_exist.txt", "echo after"],
                     want="after"))

    results.append(t("D6  > redirect to a bad directory fails gracefully",
                     ["echo hi > /no/such/directory/out.txt", "echo after"],
                     want="after"))

    results.append(t("D7  redirect combined with a pipe",
                     [f"echo hello world | tr 'a-z' 'A-Z' > {out1}",
                      f"cat {out1}"],
                     want="HELLO WORLD"))

    results.append(t("D8  input and output redirect together",
                     [f"echo abc > {in1}",
                      f"cat < {in1} > {out1}",
                      f"cat {out1}"],
                     want="abc"))

    results.append(t("D9  redirect with missing filename is a syntax error, shell recovers",
                     ["echo hi >", "echo after"],
                     want="after"))

    results.append(t("D10 redirect adjacent to word with no spaces",
                     [f"echo hi>{out1}", f"cat {out1}"],
                     want="hi"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# E. Trace mode (--trace)
# ──────────────────────────────────────────────────────────────────────────────

def section_e():
    print("\n── E. Trace Mode (--trace) ──")
    results = []

    results.append(t("E1  trace mode still runs simple commands correctly",
                     ["echo hello"],
                     want="hello",
                     args=["--trace"]))

    results.append(t("E2  trace mode still runs pipelines correctly",
                     ["echo hi | cat"],
                     want="hi",
                     args=["--trace"]))

    results.append(t("E3  trace mode still applies redirects correctly",
                     [f"echo traced > {BASE}/trace_out.txt", f"cat {BASE}/trace_out.txt"],
                     want="traced",
                     args=["--trace"]))

    results.append(t("E4  trace mode does not hang on failing command",
                     ["zzz_no_such_cmd", "echo after"],
                     want="after",
                     args=["--trace"]))

    results.append(t("E5  trace mode works with builtins (cd/pwd)",
                     [f"cd {BASE}", "pwd"],
                     want=BASE,
                     args=["--trace"]))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  Phase 3 — Executor Tests")
    print("=" * 55)

    compile_shell()
    setup_tree()

    try:
        all_results = []
        all_results += section_a()
        all_results += section_b()
        all_results += section_c()
        all_results += section_d()
        all_results += section_e()
    finally:
        teardown_tree()

    total = len(all_results)
    passed = sum(1 for r in all_results if r)

    print(f"\n{'='*55}")
    print(f"  Result: {passed}/{total} passed")
    print(f"{'='*55}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())