#!/usr/bin/env python3
"""
tests/osh_spec/compliance_report.py — A2 POSIX Compliance Report Generator

Runs all OSH spec categories against posixsh, dash, and bash, then prints the
Markdown compliance matrix for Section V.B of the paper.

Usage (from repo root):
  python3 tests/osh_spec/compliance_report.py [--spec-dir PATH] [--target BINARY]

Options:
  --spec-dir PATH    Directory containing .test.sh files
                     (default: tests/osh_spec/spec)
  --target   BINARY  Path to posixsh binary under test
                     (default: ./posixsh)
  --verbose          Print individual pass/fail lines while running
  --csv              Also emit machine-readable CSV after the Markdown table
"""
import sys
import os
import argparse

# Ensure runner.py (sibling file) is importable regardless of CWD.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runner import parse_spec_file, run_test_case  # noqa: E402


# ---------------------------------------------------------------------------
# Category definitions
# Each entry: (display_name, [candidate_filenames], posixsh_implemented)
#   posixsh_implemented=False  → posixsh gets "0.0% (Not Impl)" automatically
# ---------------------------------------------------------------------------
CATEGORIES = [
    # Implemented in posixsh
    ("Pipelines",                     ["pipeline.test.sh"],                True),
    ("I/O Redirections",              ["redirect.test.sh", "redir.test.sh"], True),
    ("Quoting",                       ["quote.test.sh"],                   True),
    ("Word expansion (`$VAR`)",       ["var-sub.test.sh", "var-op-len.test.sh"], True),
    ("Word splitting",                ["word-split.test.sh"],              True),
    ("Builtins (`cd`/`exit`/`pwd`/`echo`)", ["builtin-special.test.sh"], True),
    ("Background & job control",      ["background.test.sh", "job-control.test.sh"], True),
    # Not implemented in posixsh (no compound commands, no arith, no here-docs)
    ("Compound cmds (`if`/`while`/`for`)", ["if_.test.sh", "for-expr.test.sh"], False),
    ("Command substitution `$()`",    ["command-sub.test.sh"],             False),
    ("Here-documents (`<<`)",         ["here-doc.test.sh"],                False),
    ("Arithmetic expansion `$((..))`", ["arith.test.sh"],                  False),
]


def find_spec_file(spec_dir, candidates):
    """Return the first existing candidate path, or None."""
    for name in candidates:
        path = os.path.join(spec_dir, name)
        if os.path.isfile(path):
            return path
    return None


def evaluate_category(spec_file, shell_bin, verbose=False):
    """
    Run all non-skipped test cases in spec_file against shell_bin.
    Returns (passed, total).
    """
    if spec_file is None:
        return 0, 0

    cases = parse_spec_file(spec_file)
    total = 0
    passed = 0

    for case in cases:
        res = run_test_case(case, shell_bin)
        if res.get("skipped"):
            continue
        total += 1
        if res["passed"]:
            passed += 1
            if verbose:
                print(f"      PASS: {case.name}")
        else:
            if verbose:
                print(f"      FAIL: {case.name}")

    return passed, total


def fmt_rate(passed, total, not_impl=False):
    """Format a pass-rate cell for the Markdown table."""
    if not_impl:
        return "0.0% (Not Impl)"
    if total == 0:
        return "N/A (file missing)"
    pct = passed / total * 100
    return f"{passed}/{total} ({pct:.1f}%)"


def main():
    parser = argparse.ArgumentParser(
        description="Generate A2 POSIX compliance table (Markdown + optional CSV)."
    )
    parser.add_argument(
        "--spec-dir", default="tests/osh_spec/spec",
        help="Directory containing .test.sh files"
    )
    parser.add_argument(
        "--target", default="./posixsh",
        help="Path to posixsh binary under test"
    )
    parser.add_argument("--verbose", action="store_true",
                        help="Print per-test pass/fail while running")
    parser.add_argument("--csv", action="store_true",
                        help="Also emit CSV output after the Markdown table")
    args = parser.parse_args()

    shells = {
        "posixsh": args.target,
        "dash":    "dash",
        "bash":    "bash",
    }

    # Verify shell binaries are available
    for sh_name, sh_bin in shells.items():
        if not (os.path.isfile(sh_bin) and os.access(sh_bin, os.X_OK)) \
                and not any(
                    os.path.isfile(os.path.join(d, sh_bin))
                    for d in os.environ.get("PATH", "").split(":")
                ):
            print(f"WARNING: shell '{sh_name}' not found at '{sh_bin}' — "
                  f"results will show N/A", file=sys.stderr)

    # Collect results for all categories
    results = []
    for cat_name, file_candidates, posixsh_impl in CATEGORIES:
        spec_file = find_spec_file(args.spec_dir, file_candidates)
        row = {"category": cat_name, "posixsh_impl": posixsh_impl,
               "spec_file": spec_file}

        for sh_name, sh_bin in shells.items():
            if sh_name == "posixsh" and not posixsh_impl:
                row[sh_name] = (0, 0)   # "Not Impl"
            else:
                if args.verbose:
                    print(f"\n  [{sh_name}] {cat_name}")
                row[sh_name] = evaluate_category(spec_file, sh_bin,
                                                  verbose=args.verbose)
        results.append(row)

    # -----------------------------------------------------------------------
    # Print Markdown table
    # -----------------------------------------------------------------------
    print()
    print("## A2 POSIX Compliance Results")
    print()
    header = ("| Category"
              + " " * 22
              + " | posixsh Pass Rate     | dash Pass Rate        | bash Pass Rate        |")
    sep    = ("|:" + "-" * 30
              + " | :-------------------: | :-------------------: | :-------------------: |")
    print(header)
    print(sep)

    for row in results:
        posixsh_cell = fmt_rate(*row["posixsh"], not_impl=not row["posixsh_impl"])
        dash_cell    = fmt_rate(*row["dash"])
        bash_cell    = fmt_rate(*row["bash"])
        cat_field    = row["category"]
        print(f"| {cat_field:<31} | {posixsh_cell:<21} | {dash_cell:<21} | {bash_cell:<21} |")

    # -----------------------------------------------------------------------
    # CSV output (optional)
    # -----------------------------------------------------------------------
    if args.csv:
        print()
        print("## CSV")
        print("category,posixsh_passed,posixsh_total,dash_passed,dash_total,"
              "bash_passed,bash_total,posixsh_implemented")
        for row in results:
            pp, pt = row["posixsh"]
            dp, dt = row["dash"]
            bp, bt = row["bash"]
            impl = "1" if row["posixsh_impl"] else "0"
            print(f'"{row["category"]}",{pp},{pt},{dp},{dt},{bp},{bt},{impl}')


if __name__ == "__main__":
    main()
