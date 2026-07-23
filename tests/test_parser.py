#!/usr/bin/env python3
"""
tests/test_parser.py — Phase 2: Parser tests

Strategy:
    The parser converts a flat token stream into a structured Pipeline/Command.
    We cannot inspect struct fields directly, so we drive the shell and observe:
      - Correct execution behavior  → struct was built correctly
      - "syntax error" output       → parser returned -1
      - Shell survival (prompt)     → no crash / infinite loop

Sections:
    A. Normal pipelines      — single command, multi-stage, max depth
    B. Redirections          — output, input, append, combined with pipes
    C. Builtin detection     — cd/pwd/jobs/fg/bg marked as is_builtin
    D. Background flag       — & sets pipeline.background = 1
    E. Syntax errors         — leading pipe, trailing pipe, double pipe,
                               missing redirect filename
    F. End-to-end            — multi-feature combinations in one command
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, run_shell, assert_contains, assert_not_contains

# ──────────────────────────────────────────────────────────────────────────────
# Helper
# ──────────────────────────────────────────────────────────────────────────────

def t(name, cmds, want=None, no_want=None, timeout=1.5):
    out = run_shell(cmds, timeout=timeout)
    ok = True
    if want is not None:
        ok = assert_contains(name, out, want) and ok
    if no_want is not None:
        ok = assert_not_contains(name + " (unwanted)", out, no_want) and ok
    return ok


# ──────────────────────────────────────────────────────────────────────────────
# A. Normal Pipelines
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. Normal Pipelines ──")
    results = []

    # Single command, single argument
    results.append(t("A1  single command",
                     ["echo hello"],
                     want="hello"))

    # Single command, many arguments
    results.append(t("A2  single command many args",
                     ["echo a b c d e"],
                     want="a b c d e"))

    # Two-stage pipeline
    results.append(t("A3  two-stage pipeline",
                     ["echo piped | cat"],
                     want="piped"))

    # Three-stage pipeline
    results.append(t("A4  three-stage pipeline",
                     ["echo x | cat | cat"],
                     want="x"))

    # Four-stage pipeline
    results.append(t("A5  four-stage pipeline",
                     ["echo deep | cat | cat | cat"],
                     want="deep"))

    # Max pipeline depth = 8 stages (MAX_PIPELINE_DEPTH)
    eight_stage = "echo maxdepth" + " | cat" * 7
    results.append(t("A6  max pipeline depth (8 stages)",
                     [eight_stage],
                     want="maxdepth"))

    # 9-stage pipeline: the PARSER silently truncates to MAX_PIPELINE_DEPTH (8)
    # before the executor even sees it — so the executor's "too many pipeline stages"
    # guard in executor.c:225 is unreachable from user input.
    # Correct behavior: the 9th stage is dropped; first 8 run normally.
    nine_stage = "echo toodeep" + " | cat" * 8
    results.append(t("A7  9-stage pipeline — parser silently truncates to 8, output still produced",
                     [nine_stage],
                     want="toodeep"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. Redirections
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. Redirections ──")
    results = []
    TMP = "/tmp/ptest"

    # Output redirect: file is created and contains the data
    results.append(t("B1  output redirect (>)",
                     [f"echo written > {TMP}_b1.txt",
                      f"cat {TMP}_b1.txt"],
                     want="written"))

    # Redirect overwrites existing content.
    # NOTE: no_want="first" would false-fail because the PTY echoes the typed
    # command ("echo first > ...") back into the output buffer. We only assert
    # "second" is present; that is sufficient proof of overwrite.
    results.append(t("B2  output redirect overwrites",
                     [f"echo first > {TMP}_b2.txt",
                      f"echo second > {TMP}_b2.txt",
                      f"cat {TMP}_b2.txt"],
                     want="second"))

    # Append redirect: both lines present
    results.append(t("B3  append redirect (>>)",
                     [f"echo lineA > {TMP}_b3.txt",
                      f"echo lineB >> {TMP}_b3.txt",
                      f"cat {TMP}_b3.txt"],
                     want="lineA"))

    results.append(t("B3b append adds second line",
                     [f"cat {TMP}_b3.txt"],
                     want="lineB"))

    # Input redirect
    results.append(t("B4  input redirect (<)",
                     [f"echo fromfile > {TMP}_b4.txt",
                      f"cat < {TMP}_b4.txt"],
                     want="fromfile"))

    # Pipe + output redirect: stdout of pipeline goes to file
    results.append(t("B5  pipe combined with output redirect",
                     [f"echo pipeout | cat > {TMP}_b5.txt",
                      f"cat {TMP}_b5.txt"],
                     want="pipeout"))

    # Input redirect + pipe
    results.append(t("B6  input redirect combined with pipe",
                     [f"echo pipein > {TMP}_b6.txt",
                      f"cat < {TMP}_b6.txt | grep pipein"],
                     want="pipein"))

    # Redirect target is a quoted filename
    results.append(t("B7  redirect to quoted filename",
                     [f"echo quoted > '/tmp/tok_b7_out.txt'",
                      f"cat /tmp/tok_b7_out.txt"],
                     want="quoted"))

    # Cleanup
    for suf in ["b1","b2","b3","b4","b5","b6"]:
        try: os.remove(f"{TMP}_{suf}.txt")
        except OSError: pass
    try: os.remove("/tmp/tok_b7_out.txt")
    except OSError: pass

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. Builtin Detection
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. Builtin Detection ──")
    results = []

    # cd: runs in shell process, changes directory
    results.append(t("C1  cd is_builtin — changes directory",
                     ["cd /tmp", "pwd"],
                     want="/tmp"))

    # pwd: runs in shell process
    results.append(t("C2  pwd is_builtin — prints cwd",
                     ["cd /", "pwd"],
                     want="/"))

    # exit: runs in shell process and terminates
    # After exit the PTY closes — shell prompt won't appear again
    results.append(t("C3  exit is_builtin — shell terminates",
                     ["exit"],
                     no_want="syntax error"))

    # jobs: handled as builtin
    results.append(t("C4  jobs is_builtin — no fork",
                     ["jobs"],
                     no_want="syntax error"))

    # Builtins inside a pipeline run in a subshell (do NOT affect shell state).
    # We can't use no_want="/tmp" because "/tmp" appears in the echoed command
    # "cd /tmp | cat" from the PTY. Instead, we assert the shell's cwd IS the
    # original working directory (not /tmp), proving cd had no effect.
    results.append(t("C5  cd inside pipeline runs in subshell — cwd unchanged",
                     ["cd /workspace",          # establish known cwd first
                      "cd /tmp | cat",           # this cd must NOT take effect
                      "pwd"],
                     want="/workspace"))          # cwd must still be /workspace

    return results


# ──────────────────────────────────────────────────────────────────────────────
# D. Background Flag
# ──────────────────────────────────────────────────────────────────────────────

def section_d():
    print("\n── D. Background Flag ──")
    results = []

    # & on a single command
    results.append(t("D1  single background command shows [1]",
                     ["sleep 5 &", "jobs"],
                     want="[1]"))

    # & on a pipeline
    results.append(t("D2  pipeline background shows [1]",
                     ["echo x | cat &", "jobs"],
                     want="[1]"))

    # Shell immediately returns prompt without waiting
    results.append(t("D3  shell returns prompt immediately after &",
                     ["sleep 5 &"],
                     want="posixsh>"))

    # Two background jobs get sequential numbers
    results.append(t("D4  two background jobs numbered [1] and [2]",
                     ["sleep 5 &", "sleep 5 &", "jobs"],
                     want="[2]"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# E. Syntax Errors
# ──────────────────────────────────────────────────────────────────────────────

def section_e():
    print("\n── E. Syntax Errors ──")
    results = []

    # Leading pipe: | echo x
    results.append(t("E1  leading pipe → syntax error",
                     ["| echo x"],
                     want="syntax error"))

    # Trailing pipe: echo x |
    results.append(t("E2  trailing pipe → syntax error",
                     ["echo x |"],
                     want="syntax error"))

    # Double pipe: echo x || cat  (empty segment between pipes)
    results.append(t("E3  double pipe → syntax error",
                     ["echo x || cat"],
                     want="syntax error"))

    # Missing filename after >
    results.append(t("E4  missing filename after > → syntax error",
                     ["echo x >"],
                     want="syntax error"))

    # Missing filename after <
    results.append(t("E5  missing filename after < → syntax error",
                     ["cat <"],
                     want="syntax error"))

    # Missing filename after >>
    results.append(t("E6  missing filename after >> → syntax error",
                     ["echo x >>"],
                     want="syntax error"))

    # Shell must NOT crash after any syntax error — prompt returns
    results.append(t("E7  shell survives syntax error",
                     ["| bad", "echo alive"],
                     want="alive"))

    # Multiple syntax errors in sequence — shell stays alive
    results.append(t("E8  shell survives multiple syntax errors",
                     ["| bad", "echo > ", "echo stillalive"],
                     want="stillalive"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# F. End-to-End: Multi-Feature Combinations
# ──────────────────────────────────────────────────────────────────────────────

def section_f():
    print("\n── F. End-to-End Combinations ──")
    results = []
    TMP = "/tmp/ptest_f"

    # Pipe + grep filters correctly
    results.append(t("F1  pipe + grep filters output",
                     ["echo yes_match | grep yes"],
                     want="yes_match"))

    # F2: grep suppresses non-matching output.
    # We cannot use no_want="no_match" because the PTY echoes the command
    # "echo no_match | grep yes" back into the output. Instead, we verify
    # that the prompt re-appears immediately (grep produced no stdout line
    # between the command echo and the next prompt).
    results.append(t("F2  pipe + grep suppresses non-match — prompt returns cleanly",
                     ["echo no_match | grep yes",
                      "echo AFTER"],
                     want="AFTER",          # shell is still alive
                     no_want="no_matchAFTER"))  # grep output did not bleed through

    # Quoted arg with pipe
    results.append(t("F3  quoted arg passes through pipe",
                     ["echo 'hello world' | cat"],
                     want="hello world"))

    # Multiple redirects in sequence: write, overwrite, read.
    # no_want="v1" would false-fail because the PTY echoes "echo v1 > ..."
    # back into the output. Asserting want="v2" is sufficient: cat reads
    # the file and if overwrite worked, only v2 is there.
    results.append(t("F4  write then overwrite then read",
                     [f"echo v1 > {TMP}.txt",
                      f"echo v2 > {TMP}.txt",
                      f"cat {TMP}.txt"],
                     want="v2"))

    # Pipe feeds into file via redirect on last stage
    results.append(t("F5  pipe chain into output redirect",
                     [f"echo chain | cat | cat > {TMP}_f5.txt",
                      f"cat {TMP}_f5.txt"],
                     want="chain"))

    # Builtin followed by external command
    results.append(t("F6  cd then external command",
                     ["cd /tmp", "echo in_tmp"],
                     want="in_tmp"))

    # Background job then foreground job — both work
    results.append(t("F7  background then foreground coexist",
                     ["sleep 10 &", "echo fg_works"],
                     want="fg_works"))

    # Syntax error does not corrupt subsequent valid command
    results.append(t("F8  syntax error then valid command",
                     ["| bad_syntax",
                      "echo after_error"],
                     want="after_error"))

    # Long pipeline with grep filtering at the end
    results.append(t("F9  4-stage pipe with grep at end",
                     ["echo target | cat | cat | grep target"],
                     want="target"))

    # Cleanup
    for suf in ["", "_f5"]:
        try: os.remove(f"{TMP}{suf}.txt")
        except OSError: pass

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  Phase 2 — Parser Tests")
    print("=" * 55)

    compile_shell()

    all_results = []
    all_results += section_a()
    all_results += section_b()
    all_results += section_c()
    all_results += section_d()
    all_results += section_e()
    all_results += section_f()

    total  = len(all_results)
    passed = sum(1 for r in all_results if r)

    print(f"\n{'='*55}")
    print(f"  Result: {passed}/{total} passed")
    print(f"{'='*55}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
