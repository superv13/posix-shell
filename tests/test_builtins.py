#!/usr/bin/env python3
"""
tests/test_builtins.py — Phase 3: Builtin command tests (cd, pwd, exit)

Strategy:
    Same black-box approach as test_tokenizer.py: drive the shell through
    stdin via helper.run_shell() and inspect the captured terminal output
    (which includes the "posixsh>" prompt, so we can detect crashes/hangs
    even when we don't know the exact wording of an error message).

    All scratch directories/files live under /tmp/psh_builtins_test/ and
    are created before each section and removed at the end of main().

Sections:
    A. cd    — correctness (relative/absolute/./..) and error paths
               (nonexistent dir, cd into a file, too many args)
    B. pwd   — correctness after cd chains, extra-argument handling
    C. exit  — correctness (no arg, numeric arg) and error paths
               (non-numeric arg, too many args), plus "does exit actually
               stop the shell" checks

NOTE ON DOCKER:
    Tests that rely on permission bits (e.g. cd into a chmod 000 dir) are
    skipped automatically when running as root (common in Docker), since
    root bypasses directory permission checks on Linux.
"""

import sys
import os
import shutil

sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, run_shell, assert_contains, assert_not_contains

BASE = "/tmp/psh_builtins_test"


def t(name, cmds, want=None, no_want=None, timeout=1.5):
    """Run `cmds`, assert `want` IS in output, `no_want` is NOT. Returns bool."""
    out = run_shell(cmds, timeout=timeout)
    ok = True
    if want is not None:
        ok = assert_contains(name, out, want) and ok
    if no_want is not None:
        ok = assert_not_contains(name + " (no spurious output)", out, no_want) and ok
    return ok


def setup_tree():
    shutil.rmtree(BASE, ignore_errors=True)
    os.makedirs(f"{BASE}/a/b", exist_ok=True)
    with open(f"{BASE}/regular_file.txt", "w") as f:
        f.write("not a directory\n")
    os.makedirs(f"{BASE}/no_access", exist_ok=True)
    os.chmod(f"{BASE}/no_access", 0o000)


def teardown_tree():
    try:
        os.chmod(f"{BASE}/no_access", 0o755)
    except OSError:
        pass
    shutil.rmtree(BASE, ignore_errors=True)


# ──────────────────────────────────────────────────────────────────────────────
# A. cd — correctness and error paths
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. cd (correctness & error paths) ──")
    results = []

    # Absolute path cd
    results.append(t("A1  cd absolute path",
                     [f"cd {BASE}/a", "pwd"],
                     want=f"{BASE}/a"))

    # Relative path cd (chained)
    results.append(t("A2  cd relative path after absolute cd",
                     [f"cd {BASE}", "cd a", "pwd"],
                     want=f"{BASE}/a"))

    # cd .
    results.append(t("A3  cd . is a no-op",
                     [f"cd {BASE}/a", "cd .", "pwd"],
                     want=f"{BASE}/a"))

    # cd ..
    results.append(t("A4  cd .. moves up one level",
                     [f"cd {BASE}/a/b", "cd ..", "pwd"],
                     want=f"{BASE}/a"))

    # cd .. chained twice
    results.append(t("A5  cd .. twice from nested dir",
                     [f"cd {BASE}/a/b", "cd ..", "cd ..", "pwd"],
                     want=BASE))

    # cd with no args goes to $HOME
    home = os.environ.get("HOME")
    if home:
        results.append(t("A6  cd with no args goes to $HOME",
                         [f"cd {BASE}/a", "cd", "pwd"],
                         want=home))
    else:
        print("  A6  cd with no args goes to $HOME ... SKIP (no $HOME set)")

    # cd .. above root should not crash and should land at /
    results.append(t("A7  cd .. above root does not crash",
                     ["cd /", "cd ..", "cd ..", "pwd"],
                     want="/"))

    # Trailing slash handled
    results.append(t("A8  cd with trailing slash",
                     [f"cd {BASE}/a/"],
                     want="posixsh>"))

    # Double slashes collapsed
    results.append(t("A9  cd with doubled slashes",
                     [f"cd {BASE}//a", "pwd"],
                     want=f"{BASE}/a"))

    # --- error paths ---

    # cd into nonexistent directory: cwd must NOT change, shell must not crash
    results.append(t("A10 cd nonexistent directory (cwd unchanged, no crash)",
                     [f"cd {BASE}", f"cd {BASE}/does_not_exist_xyz", "pwd"],
                     want=BASE))

    # cd into a regular file (not a directory)
    results.append(t("A11 cd into a regular file fails gracefully",
                     [f"cd {BASE}", f"cd {BASE}/regular_file.txt", "pwd"],
                     want=BASE))

    # cd with too many arguments
    results.append(t("A12 cd with too many arguments fails gracefully",
                     [f"cd {BASE}", f"cd {BASE}/a {BASE}/a/b extra", "pwd"],
                     want=BASE))

    # cd permission denied — skip under root (Docker default), since root
    # bypasses directory permission bits on Linux
    if os.geteuid() == 0:
        print("  A13 cd into permission-denied dir ... SKIP (running as root)")
    else:
        results.append(t("A13 cd into permission-denied dir fails gracefully",
                         [f"cd {BASE}", f"cd {BASE}/no_access", "pwd"],
                         want=BASE))

    # Failed cd must not kill the shell — subsequent commands still run
    results.append(t("A14 failed cd does not crash shell (later commands run)",
                     [f"cd {BASE}/nope_xyz", f"cd {BASE}/a", "pwd"],
                     want=f"{BASE}/a"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. pwd — correctness and argument handling
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. pwd ──")
    results = []

    # Basic correctness
    results.append(t("B1  pwd prints current directory",
                     [f"cd {BASE}", "pwd"],
                     want=BASE))

    # pwd reflects each cd in a chain
    results.append(t("B2  pwd reflects chained cd calls",
                     [f"cd {BASE}", "cd a", "cd b", "pwd"],
                     want=f"{BASE}/a/b"))

    # pwd output is absolute (starts with /)
    results.append(t("B3  pwd output is absolute path",
                     [f"cd {BASE}/a", "pwd"],
                     want="/tmp/psh_builtins_test/a"))

    # pwd called multiple times in a row is stable/idempotent
    results.append(t("B4  repeated pwd calls are stable",
                     [f"cd {BASE}/a", "pwd", "pwd", "pwd"],
                     want=f"{BASE}/a"))

    # pwd with extraneous arguments should not crash (POSIX pwd ignores them)
    results.append(t("B5  pwd with extra arguments does not crash",
                     [f"cd {BASE}", "pwd extra args here"],
                     want="posixsh>"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. exit — correctness and error paths
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. exit ──")
    results = []

    # exit with no args terminates the session: nothing after it should run
    results.append(t("C1  exit with no args stops further commands",
                     ["exit", "echo should_not_appear"],
                     no_want="should_not_appear"))

    # exit with a numeric argument also terminates cleanly
    results.append(t("C2  exit N stops further commands",
                     ["exit 42", "echo should_not_appear"],
                     no_want="should_not_appear"))

    # exit is the last thing in a longer script — earlier commands still ran
    results.append(t("C3  commands before exit still execute",
                     ["echo before_exit", "exit"],
                     want="before_exit"))

    # exit with extra trailing whitespace/args on the numeric value
    results.append(t("C4  exit with numeric arg and surrounding spaces",
                     ["exit   7", "echo should_not_appear"],
                     no_want="should_not_appear"))

    # --- error paths ---

    # exit with a non-numeric argument: shell should report an error rather
    # than crash. We assume (bash-like) it does NOT exit on a bad argument,
    # so a later command should still run.
    results.append(t("C5  exit with non-numeric arg does not crash shell",
                     ["exit abc", "echo still_alive"],
                     want="still_alive"))

    # exit with too many arguments: same assumption — error, shell survives
    results.append(t("C6  exit with too many args does not crash shell",
                     ["exit 1 2 3", "echo still_alive"],
                     want="still_alive"))

    # EOF with no explicit exit command should still end the session cleanly
    # (verified indirectly: run_shell must return promptly, not hang/timeout)
    results.append(t("C7  EOF without exit terminates session cleanly",
                     ["echo last_command"],
                     want="last_command"))

    # A bad exit followed by a *good* exit should still terminate on the good one
    results.append(t("C8  valid exit after a rejected exit still terminates",
                     ["exit abc", "echo still_alive", "exit", "echo should_not_appear"],
                     want="still_alive",
                     no_want="should_not_appear"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  Phase 3 — Builtins Tests (cd, pwd, exit)")
    print("=" * 55)

    compile_shell()
    setup_tree()

    try:
        all_results = []
        all_results += section_a()
        all_results += section_b()
        all_results += section_c()
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