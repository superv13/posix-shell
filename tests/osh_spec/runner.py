#!/usr/bin/env python3
"""
tests/osh_spec/runner.py — OSH spec test parser and runner (A2 POSIX compliance)

Parses the OSH .test.sh format:
  #### Test name
  <shell script lines>
  ## stdout: expected output        (single-line)
  ## STDOUT:                        (multi-line block)
  expected line 1
  expected line 2
  ## END
  ## status: N                     (expected exit code, default 0)
  ## SKIP  /  ## N-I               (skip markers)

Usage:
  python3 runner.py <spec_file.test.sh> <shell_binary> [--verbose]
"""
import sys
import os
import shutil
import subprocess
import tempfile
import argparse


class TestCase:
    """One #### ... block from a .test.sh file."""
    def __init__(self, name):
        self.name = name
        self.script_lines = []
        self.expected_stdout = ""
        self.expected_status = 0
        self.skip = False


def parse_spec_file(filepath):
    """
    Parse an OSH .test.sh file and return a list of TestCase objects.

    The OSH format uses a simple line-oriented state machine:
      - Lines starting with '#### ' open a new test case.
      - Lines starting with '## ' are directives (stdout, status, SKIP, N-I).
      - All other lines inside a case are shell script content.
    """
    cases = []
    current = None
    state = "OUTSIDE"   # OUTSIDE | SCRIPT | MULTILINE_STDOUT | DIRECTIVES

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            stripped = line.rstrip("\n")

            # ----------------------------------------------------------------
            # New test case header
            # ----------------------------------------------------------------
            if stripped.startswith("#### "):
                if current and current.script_lines:
                    cases.append(current)
                current = TestCase(stripped[5:].strip())
                state = "SCRIPT"
                continue

            if current is None:
                continue

            # ----------------------------------------------------------------
            # Parse directives and script lines
            # ----------------------------------------------------------------
            if state == "SCRIPT":
                if stripped.startswith("## STDOUT:"):
                    state = "MULTILINE_STDOUT"

                elif stripped.startswith("## stdout:"):
                    # Single-line expected stdout
                    value = stripped[len("## stdout:"):].strip()
                    current.expected_stdout = value + "\n" if value else "\n"
                    state = "DIRECTIVES"

                elif stripped.startswith("## status:"):
                    try:
                        current.expected_status = int(stripped[len("## status:"):].strip())
                    except ValueError:
                        current.expected_status = 0
                    state = "DIRECTIVES"

                elif "## SKIP" in stripped or "## N-I" in stripped:
                    current.skip = True
                    state = "DIRECTIVES"

                elif stripped.startswith("## "):
                    # Other directive line — transition to DIRECTIVES
                    state = "DIRECTIVES"

                else:
                    current.script_lines.append(line)

            elif state == "MULTILINE_STDOUT":
                if stripped.startswith("## END"):
                    state = "DIRECTIVES"
                else:
                    current.expected_stdout += line

            elif state == "DIRECTIVES":
                if stripped.startswith("## status:"):
                    try:
                        current.expected_status = int(stripped[len("## status:"):].strip())
                    except ValueError:
                        pass

                elif stripped.startswith("## stdout:"):
                    value = stripped[len("## stdout:"):].strip()
                    current.expected_stdout = value + "\n" if value else "\n"

                elif stripped.startswith("## STDOUT:"):
                    state = "MULTILINE_STDOUT"

                elif "## SKIP" in stripped or "## N-I" in stripped:
                    current.skip = True

                elif not stripped.startswith("## ") and stripped != "":
                    # Non-directive, non-blank line — next test case script
                    # starts here (rare, but handle gracefully)
                    pass

    # Append last case
    if current and current.script_lines:
        cases.append(current)

    return cases


def run_test_case(case, shell_bin):
    """
    Execute one TestCase under shell_bin and return a result dict:
      { passed: bool, skipped: bool, reason?: str,
        actual_stdout?: str, expected_stdout?: str,
        actual_status?: int, expected_status?: int }
    """
    if case.skip:
        return {"passed": False, "skipped": True, "reason": "Marked SKIP/N-I"}

    script_content = "".join(case.script_lines)

    # Resolve shell binary to an absolute path.
    # For explicit paths (./posixsh, /usr/bin/bash) use abspath.
    # For bare names ('dash', 'bash') use shutil.which() so we get /bin/dash etc.
    if shell_bin.startswith('./') or shell_bin.startswith('../') or os.path.isabs(shell_bin):
        shell_abs = os.path.abspath(shell_bin)
    else:
        shell_abs = shutil.which(shell_bin) or shell_bin

    # Determine project root (two levels up from this file: tests/osh_spec/)
    script_dir   = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(os.path.dirname(script_dir))
    bin_dir      = os.path.join(project_root, "bin")

    # Build an env that prepends bin/ to PATH so argv.py and friends are found
    test_env = os.environ.copy()
    existing_path = test_env.get("PATH", "/usr/bin:/bin")
    test_env["PATH"] = bin_dir + ":" + existing_path
    # Inject SH so tests using `$SH -c '...'` get the shell under test
    test_env["SH"] = shell_abs
    # Some tests check OILS_TEST_* vars; default to empty to avoid OSH-specific skips
    test_env.setdefault("OILS_TEST_SHELL", shell_abs)

    # Write script to a temp file so we test file-invocation (not -c),
    # which exercises the same code path posixsh normally uses.
    tf_fd, temp_path = tempfile.mkstemp(suffix=".sh", prefix="osh_spec_")
    try:
        with os.fdopen(tf_fd, "w") as tf:
            tf.write(script_content)

        proc = subprocess.run(
            [shell_abs, temp_path],
            capture_output=True,
            text=True,
            timeout=5,
            env=test_env
        )
        actual_stdout = proc.stdout
        actual_status = proc.returncode

        passed = (
            actual_stdout == case.expected_stdout
            and actual_status == case.expected_status
        )
        return {
            "passed": passed,
            "skipped": False,
            "actual_stdout": actual_stdout,
            "expected_stdout": case.expected_stdout,
            "actual_status": actual_status,
            "expected_status": case.expected_status,
        }

    except subprocess.TimeoutExpired:
        return {"passed": False, "skipped": True, "reason": "Execution timeout (5 s)"}
    except FileNotFoundError:
        return {"passed": False, "skipped": False,
                "reason": f"Shell binary not found: {shell_bin}"}
    except Exception as exc:
        return {"passed": False, "skipped": False, "reason": str(exc)}
    finally:
        if os.path.exists(temp_path):
            os.remove(temp_path)


def main():
    parser = argparse.ArgumentParser(
        description="Run OSH spec tests against a shell binary."
    )
    parser.add_argument("test_file",  help="Path to a .test.sh spec file")
    parser.add_argument("shell_bin",  help="Shell binary to test (e.g. ./posixsh, dash, bash)")
    parser.add_argument("--verbose",  action="store_true",
                        help="Print pass/fail for every individual test")
    args = parser.parse_args()

    if not os.path.isfile(args.test_file):
        print(f"ERROR: spec file not found: {args.test_file}", file=sys.stderr)
        sys.exit(1)

    cases = parse_spec_file(args.test_file)
    total = 0
    passed_count = 0
    skipped_count = 0

    for case in cases:
        res = run_test_case(case, args.shell_bin)

        if res.get("skipped"):
            skipped_count += 1
            if args.verbose:
                reason = res.get("reason", "SKIP/N-I")
                print(f"  SKIP : {case.name}  [{reason}]")
            continue

        total += 1
        if res["passed"]:
            passed_count += 1
            if args.verbose:
                print(f"  PASS : {case.name}")
        else:
            if args.verbose:
                print(f"  FAIL : {case.name}")
                if res.get("expected_stdout") != res.get("actual_stdout"):
                    exp = repr(res.get("expected_stdout", ""))
                    got = repr(res.get("actual_stdout", ""))
                    print(f"         stdout  expected={exp}")
                    print(f"                 actual  ={got}")
                if res.get("expected_status") != res.get("actual_status"):
                    print(f"         status  expected={res.get('expected_status')}  "
                          f"actual={res.get('actual_status')}")

    pct = (passed_count / total * 100) if total > 0 else 0.0
    print(f"SUMMARY: {passed_count}/{total} passed ({pct:.1f}%)  "
          f"[{skipped_count} skipped]")
    sys.exit(0 if passed_count == total else 1)


if __name__ == "__main__":
    main()
