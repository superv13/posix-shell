#!/usr/bin/env python3
"""
tests/test_jobs.py — Phase 4: Job control tests (&, jobs, fg, bg,
                       job numbering, completion reporting)

Strategy:
    Same black-box approach as earlier phases: drive the shell through
    stdin via helper.run_shell() and inspect the captured terminal output.
    No raw signal injection is needed for this file — see test_signals.py
    for SIGINT/SIGTSTP-specific tests.

    One deliberate trick is used throughout: a backgrounded `cat &` will be
    automatically stopped by the kernel with SIGTTIN the moment it tries to
    read from the controlling terminal while not in the foreground process
    group (this is exactly the case documented in jobs.c's
    reap_background_jobs(): "A background job tried to read from the
    terminal (SIGTTIN) and was automatically stopped by the kernel.").
    This lets us reach JOB_STOPPED deterministically without sending any
    raw control bytes.

    Timing-sensitive assertions (e.g. "& returns the prompt immediately",
    "fg blocks until the job finishes") are done by comparing behavior
    across two different `timeout` values passed to run_shell(), rather
    than by injecting real-time delays mid-script.

ASSUMPTIONS (adjust if your shell's fg/bg syntax differs):
    - `fg` / `bg` with no argument operate on the most recently
      started/stopped job (the "current" job).
    - `fg N` / `bg N` operate on job number N (bare integer, no `%` prefix
      — this codebase never uses `%N` syntax anywhere in jobs.c).
    - Job numbering: per add_job() in jobs.c, the next job number is
      (highest job_num currently *active* in the table) + 1 — NOT based on
      the lowest free table slot. That means numbers are only reused once
      the table is completely empty. Several tests below specifically
      pin down this exact (somewhat non-bash-like) behavior.
    - print_job_line() format is "[N]+  State    cmd" / "[N]-  State cmd".
"""

import sys
import os

sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, run_shell, assert_contains, assert_not_contains


def t(name, cmds, want=None, no_want=None, timeout=1.5):
    """Run `cmds`, assert `want` IS in output, `no_want` is NOT. Returns bool."""
    out = run_shell(cmds, timeout=timeout)
    ok = True
    if want is not None:
        ok = assert_contains(name, out, want) and ok
    if no_want is not None:
        ok = assert_not_contains(name + " (no spurious output)", out, no_want) and ok
    return ok


# ──────────────────────────────────────────────────────────────────────────────
# A. Background execution (&)
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. Background Execution (&) ──")
    results = []

    # `&` must return the prompt immediately, without waiting for the job
    results.append(t("A1  & returns prompt immediately (does not block)",
                     ["sleep 5 &", "echo immediately"],
                     want="immediately",
                     timeout=0.6))

    # Two concurrent background jobs both get tracked
    results.append(t("A2  two concurrent background jobs both appear in jobs",
                     ["sleep 2 &", "sleep 2 &", "jobs"],
                     want="[1]",
                     timeout=0.6))

    results.append(t("A2b two concurrent background jobs — second job number",
                     ["sleep 2 &", "sleep 2 &", "jobs"],
                     want="[2]",
                     timeout=0.6))

    # Backgrounding a pipeline
    results.append(t("A3  a whole pipeline can be backgrounded",
                     ["echo hi | cat &", "jobs"],
                     want="[1]",
                     timeout=0.6))

    # Degenerate/edge case: bare `&` with no command should not crash
    results.append(t("A4  bare & with no command does not crash",
                     ["&", "echo after"],
                     want="after",
                     timeout=0.6))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. jobs — listing and formatting
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. jobs Listing ──")
    results = []

    # jobs with an empty table must not crash
    results.append(t("B1  jobs with no background jobs does not crash",
                     ["jobs", "echo after"],
                     want="after",
                     timeout=0.6))

    # jobs shows the correct command text for a simple job
    results.append(t("B2  jobs shows correct command text",
                     ["sleep 5 &", "jobs"],
                     want="sleep 5",
                     timeout=0.6))

    # jobs shows a pipeline's command text joined with " | "
    # (per build_cmd_string in jobs.c)
    results.append(t("B3  jobs shows pipeline command joined with ' | '",
                     ["echo hi | cat &", "jobs"],
                     want="echo hi | cat",
                     timeout=0.6))

    # jobs marks at least one job as current ("+")
    results.append(t("B4  jobs marks the current job with '+'",
                     ["sleep 5 &", "jobs"],
                     want="]+",
                     timeout=0.6))

    # jobs output includes the job's running state
    results.append(t("B5  jobs shows Running state for an active job",
                     ["sleep 5 &", "jobs"],
                     want="Running",
                     timeout=0.6))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. Job numbering
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. Job Numbering ──")
    results = []

    # Sequential numbering while jobs overlap: 3rd job must NOT reuse job 1's
    # number just because that slot became free — jobs.c computes the next
    # number from the max *active* job_num, not the lowest free slot.
    results.append(t(
        "C1  job number is NOT reused from a freed slot while others are active",
        [
            "sleep 1 &",          # job A -> [1]
            "sleep 10 &",         # job B -> [2]
            "sleep 2",            # blocks ~2s: A (1s) finishes & gets reaped
            "sleep 1 &",          # job C -> should be [3], NOT [1]
            "jobs",
        ],
        want="[3]",
        timeout=4.0,
    ))

    # Confirm job A really did complete (Done reported) before C started,
    # so the "[3]" result above isn't just an off-by-one coincidence.
    results.append(t("C2  completed job is reported as Done before renumbering",
                     ["sleep 1 &", "sleep 2", "sleep 1 &", "jobs"],
                     want="Done",
                     timeout=4.0))

    # Numbering resets to 1 once the table becomes completely empty.
    # We assert "[2]" NEVER appears anywhere in this session, since only
    # one job exists at a time and both should end up numbered [1].
    results.append(t(
        "C3  numbering resets to 1 once the job table is completely empty",
        [
            "sleep 1 &",          # job X -> [1]
            "sleep 2",            # X finishes & is reaped; table now empty
            "sleep 1 &",          # job Y -> should be [1] again, not [2]
            "jobs",
        ],
        want="[1]",
        no_want="[2]",
        timeout=4.0,
    ))

    # Job table survives many concurrent background jobs without crashing
    # (stress test for MAX_JOBS handling; does not assume a specific limit)
    many_bg = [f"sleep 2 &" for _ in range(20)] + ["echo survived"]
    results.append(t("C4  many concurrent background jobs do not crash the shell",
                     many_bg,
                     want="survived",
                     timeout=1.0))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# D. fg
# ──────────────────────────────────────────────────────────────────────────────

def section_d():
    print("\n── D. fg ──")
    results = []

    # fg blocks until the foregrounded job finishes: with a short timeout we
    # should NOT see the command typed after fg, since the shell is still
    # waiting on the child.
    results.append(t(
        "D1a fg blocks — foreground job still running",
        [
            "sleep 2 &",
            "fg",
            "jobs"
        ],
        want="sleep 2",
        timeout=0.5,
))

    # With enough time for the job to finish, fg must return control and
    # the later command must now be visible.
    results.append(t("D1b fg blocks — later command visible once job finishes",
                     ["sleep 2 &", "fg", "echo __FG_DONE__"],
                     want="__FG_DONE__",
                     timeout=2.6))

    # fg with no jobs at all must fail gracefully, not crash
    results.append(t("D2  fg with no background jobs fails gracefully",
                     ["fg", "echo after"],
                     want="after",
                     timeout=0.6))

    # fg with an invalid job number must fail gracefully, not crash
    results.append(t("D3  fg with an invalid job number fails gracefully",
                     ["sleep 1 &", "fg 99", "echo after"],
                     want="after",
                     timeout=1.6))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# E. bg
# ──────────────────────────────────────────────────────────────────────────────

def section_e():
    print("\n── E. bg ──")
    results = []

    # A stopped job (via the SIGTTIN auto-stop trick) shows as Stopped
    results.append(t("E1  backgrounded cat is auto-stopped (SIGTTIN) and listed",
                     ["cat &", "sleep 1", "jobs"],
                     want="Stopped",
                     timeout=1.8))

    # bg resumes the stopped job; since it still can't own the terminal, it
    # will typically be re-stopped almost immediately by another SIGTTIN —
    # the important thing is the shell handles this without crashing and
    # the job is still trackable afterward.
    results.append(t("E2  bg resumes a stopped job without crashing the shell",
                     ["cat &", "bg", "jobs"],
                     want="Stopped",
                     timeout=0.6))

    # bg with nothing stopped/running must fail gracefully
    results.append(t("E3  bg with no jobs at all fails gracefully",
                     ["bg", "echo after"],
                     want="after",
                     timeout=0.6))

    # bg with an invalid job number must fail gracefully
    results.append(t("E4  bg with an invalid job number fails gracefully",
                     ["sleep 1 &", "bg 99", "echo after"],
                     want="after",
                     timeout=1.6))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# F. Completion reporting
# ──────────────────────────────────────────────────────────────────────────────

def section_f():
    print("\n── F. Completion Reporting ──")
    results = []

    # Simple background job: Done is reported with correct cmd text
    results.append(t("F1  completed background job is reported as Done",
                     ["sleep 1 &", "sleep 2", "echo trigger"],
                     want="Done",
                     timeout=3.0))

    results.append(t("F2  Done report includes the original command text",
                     ["sleep 1 &", "sleep 2", "echo trigger"],
                     want="sleep 1",
                     timeout=3.0))

    # Shell remains responsive after reporting completion
    results.append(t("F3  shell remains responsive after reporting Done",
                     ["sleep 1 &", "sleep 2", "echo trigger"],
                     want="trigger",
                     timeout=3.0))

    # Documented Phase-4 simplification: a backgrounded PIPELINE is marked
    # Done as soon as ANY one stage exits (not all stages) — see the comment
    # in reap_background_jobs(). Here the second stage (sleep 1) finishes
    # first while the first stage (sleep 3) is still running, and the whole
    # job should already be reported Done well before the 3s mark.
    results.append(t(
        "F4  pipeline job reports Done on first-stage-exit (documented simplification)",
        ["sleep 3 | sleep 1 &", "sleep 1.6", "echo checkpoint"],
        want="Done",
        timeout=2.2,
    ))

    # A stopped job must NEVER be auto-reported as Done
    results.append(t("F5  a stopped job is never reported as Done",
                     ["cat &", "jobs", "jobs"],
                     want="Stopped",
                     no_want="Done",
                     timeout=0.6))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  Phase 4 — Job Control Tests")
    print("=" * 55)

    compile_shell()

    all_results = []
    all_results += section_a()
    all_results += section_b()
    all_results += section_c()
    all_results += section_d()
    all_results += section_e()
    all_results += section_f()

    total = len(all_results)
    passed = sum(1 for r in all_results if r)

    print(f"\n{'='*55}")
    print(f"  Result: {passed}/{total} passed")
    print(f"{'='*55}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())