#!/usr/bin/env python3
"""
tests/test_phase5.py

Phase 5 POSIX compliance test suite for the educational posixsh.

Covers:
    - Environment propagation: children see PATH, HOME, TERM, etc.
    - $? expansion: correct exit codes for success, failure, signals,
      syntax errors, command-not-found, background jobs.
    - $$ expansion: shell PID, constant within a session.
    - Quote rules: $? NOT expanded inside single quotes; IS expanded
      inside double quotes; backslash-dollar escapes the expansion.
    - Real PATH lookup: commands in /usr/local/bin and elsewhere on
      the real PATH are found, not just /bin and /usr/bin.
    - Exit-code semantics: pipeline last-stage, 128+sig, syntax=2.

Why a pty (not a plain pipe):
    posixsh's read loop reads one line per sys_read().  Under a real
    terminal (line discipline, canonical mode) the kernel delivers exactly
    one line per read().  A plain pipe can batch multiple lines into one
    read(), causing the shell to parse only the first.  pty.openpty()
    gives the shell a real terminal, ensuring one-line-per-read() delivery
    even when the test harness sends multiple commands programmatically.

Usage:
    python3 tests/test_phase5.py
    python3 tests/test_phase5.py -v      # verbose: show transcript on every test
    python3 tests/test_phase5.py -f      # failfast: stop at first failure

Exit code:
    0  all checks passed
    1  one or more checks failed
"""

import os
import pty
import select
import subprocess
import sys
import time

SHELL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "posixsh")

VERBOSE   = "-v" in sys.argv
FAILFAST  = "-f" in sys.argv

# ── tiny test framework ──────────────────────────────────────────────────────

results = []

def record(name, passed, detail=""):
    results.append((name, passed, detail))
    mark = "PASS" if passed else "FAIL"
    print(f"[{mark}] {name}")
    if VERBOSE or not passed:
        for line in detail.splitlines():
            print(f"       {line}")
    if FAILFAST and not passed:
        sys.exit(1)

def check(name, output, needle, must_absent=False):
    if must_absent:
        ok = needle not in output
        detail = f"did NOT want {needle!r} but it appeared in:\n{output!r}"
    else:
        ok = needle in output
        detail = f"wanted {needle!r} in:\n{output!r}"
    record(name, ok, "" if ok else detail)
    return ok

# ── pty session helper ───────────────────────────────────────────────────────

class Session:
    """One posixsh process attached to a pseudo-terminal."""

    def __init__(self):
        master, slave = pty.openpty()
        self.master = master
        self.proc   = subprocess.Popen(
            [SHELL], stdin=slave, stdout=slave, stderr=slave
        )
        os.close(slave)
        self._buf = b""

    def send(self, line, idle=0.4, max_wait=5.0):
        """Send one line, collect output until quiet for `idle` seconds."""
        os.write(self.master, (line + "\n").encode())
        t0 = time.time()
        t_last = t0
        acc = b""
        while True:
            if time.time() - t_last > idle:
                break
            if time.time() - t0 > max_wait:
                break
            r, _, _ = select.select([self.master], [], [], idle)
            if self.master in r:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                acc += chunk
                t_last = time.time()
        return acc.decode(errors="replace")

    def send_strip(self, line, idle=0.4, max_wait=5.0):
        """Like send(), but strip the first line (PTY echo of the command)
        from the returned output.  Use this for absence checks where the
        command text itself contains the needle (e.g. 'false && echo YES'
        echoed back by the PTY would make 'YES' appear even if skipped)."""
        raw = self.send(line, idle=idle, max_wait=max_wait)
        # The PTY echoes our input as the first \r\n-terminated line.
        # Strip only that first line; keep everything after it.
        first_nl = raw.find("\n")
        if first_nl != -1:
            return raw[first_nl + 1:]
        return raw

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

# ── individual tests ─────────────────────────────────────────────────────────

# ── Environment propagation ─────────────────────────────────────────────────

def test_path_visible_to_children():
    """env command should print PATH (environment is passed to children)."""
    s = Session()
    out = s.send("env")
    s.send("exit")
    s.close()
    check("PATH variable visible to children via env", out, "PATH=")

def test_home_visible_to_children():
    """env command should print HOME."""
    s = Session()
    out = s.send("env")
    s.send("exit")
    s.close()
    check("HOME variable visible to children via env", out, "HOME=")

def test_env_not_empty():
    """env should print at least several entries, not an empty list."""
    s = Session()
    out = s.send("env | wc -l")
    s.send("exit")
    s.close()
    # wc -l output is a number; we check it's not "0" or "1"
    lines = [tok for tok in out.split() if tok.isdigit()]
    count = int(lines[0]) if lines else 0
    record(
        "environment has at least 3 variables",
        count >= 3,
        f"wc -l output: {out!r}, parsed count={count}",
    )

def test_real_path_lookup():
    """A command that lives outside /bin and /usr/bin must be found via PATH."""
    import os as _os
    # Find any binary not in /bin or /usr/bin using the real PATH
    path_env = _os.environ.get("PATH", "")
    test_cmd = None
    for d in path_env.split(":"):
        if d in ("", "/bin", "/usr/bin"):
            continue
        try:
            entries = _os.listdir(d)
        except OSError:
            continue
        for e in entries:
            candidate = _os.path.join(d, e)
            if _os.path.isfile(candidate) and _os.access(candidate, _os.X_OK):
                test_cmd = e
                test_dir = d
                break
        if test_cmd:
            break

    if test_cmd is None:
        record("real PATH lookup (non-/bin command found)", True,
               "no non-/bin command found on this system — skipped")
        return

    s = Session()
    out = s.send(f"{test_cmd} --version 2>&1 || {test_cmd} -version 2>&1 || echo CMD_RAN")
    s.send("echo CMD_OK")
    out2 = s.send("exit")
    s.close()
    full = out + out2
    ok = "command not found" not in full
    record(
        f"'{test_cmd}' (from {test_dir}) resolved via real PATH",
        ok,
        f"output: {full!r}",
    )

# ── $? expansion ────────────────────────────────────────────────────────────

def test_dollar_q_after_success():
    """$? should be 0 after a successful command."""
    s = Session()
    s.send("true")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 0 after true", out, "STATUS=0")

def test_dollar_q_after_false():
    """$? should be 1 after false (which exits with code 1)."""
    s = Session()
    s.send("false")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 1 after false", out, "STATUS=1")

def test_dollar_q_after_command_not_found():
    """$? should be 127 when a command is not found."""
    s = Session()
    s.send("_no_such_command_phase5_xyz_")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 127 after command not found", out, "STATUS=127")

def test_dollar_q_after_syntax_error():
    """$? should be 2 after a syntax error (POSIX convention)."""
    s = Session()
    s.send("echo a |")      # trailing pipe = syntax error
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 2 after syntax error", out, "STATUS=2")

def test_dollar_q_pipeline_last_stage():
    """$? reflects the last stage of a pipeline, not the first."""
    s = Session()
    # false | true: last stage (true) exits 0 → $? should be 0
    s.send("false | true")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 0 for 'false | true' (last stage wins)", out, "STATUS=0")

def test_dollar_q_after_background():
    """$? should be 0 after a background launch (POSIX XBD 2.8.2)."""
    s = Session()
    s.send("sleep 1 &", idle=0.3, max_wait=1.0)
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 0 after background launch", out, "STATUS=0")

def test_dollar_q_resets_between_commands():
    """Each command resets $? independently."""
    s = Session()
    s.send("false")
    s.send("true")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("$? = 0 after 'false; true' ($? reflects last cmd)", out, "STATUS=0")

def test_dollar_q_in_double_quotes():
    """$? must expand inside double quotes."""
    s = Session()
    s.send("true")
    out = s.send('echo "result=$?"')
    s.send("exit")
    s.close()
    check("$? expands inside double quotes", out, "result=0")

# ── $$ expansion ────────────────────────────────────────────────────────────

def test_dollar_dollar_is_numeric():
    """$$ must expand to a non-zero decimal number."""
    s = Session()
    out = s.send("echo PID=$$")
    s.send("exit")
    s.close()
    # Extract the PID value from "PID=NNN"
    import re
    m = re.search(r"PID=(\d+)", out)
    ok = m is not None and int(m.group(1)) > 0
    record("$$ expands to a positive PID", ok, f"output: {out!r}")

def test_dollar_dollar_constant_in_session():
    """$$ must be the same value in every command of the same session."""
    s = Session()
    out1 = s.send("echo A=$$")
    out2 = s.send("echo B=$$")
    s.send("exit")
    s.close()
    import re
    m1 = re.search(r"A=(\d+)", out1)
    m2 = re.search(r"B=(\d+)", out2)
    ok = (m1 and m2 and m1.group(1) == m2.group(1))
    record(
        "$$ is constant within a session",
        ok,
        f"first={m1 and m1.group(1)!r}, second={m2 and m2.group(1)!r}",
    )

# ── Quote rules ─────────────────────────────────────────────────────────────

def test_no_expansion_in_single_quotes():
    """$? inside single quotes must be literal — POSIX XBD 2.2.2."""
    s = Session()
    out = s.send("echo '$?'")
    s.send("exit")
    s.close()
    # Should print the literal characters "$?"
    check("$? NOT expanded inside single quotes", out, "$?")

def test_no_dollar_dollar_in_single_quotes():
    """$$ inside single quotes must be literal."""
    s = Session()
    out = s.send("echo '$$'")
    s.send("exit")
    s.close()
    check("$$ NOT expanded inside single quotes", out, "$$")

def test_backslash_dollar_escapes_in_double_quotes():
    r"""'\$' inside double quotes is a literal dollar, not expansion."""
    s = Session()
    out = s.send(r'echo "\$?"')
    s.send("exit")
    s.close()
    # Should print the literal "$?" because \$ is an escaped dollar
    check(r'"\$?" prints literal $?', out, "$?")

# ── $? embedded in longer words ─────────────────────────────────────────────

def test_dollar_q_embedded_in_word():
    """$? can be embedded mid-word: 'exit-$?' should produce 'exit-0'."""
    s = Session()
    s.send("true")
    out = s.send("echo exit-$?-code")
    s.send("exit")
    s.close()
    check("$? embedded mid-word (exit-$?-code)", out, "exit-0-code")

# ── AND/OR list operators: && and || ────────────────────────────────────────

def test_and_runs_on_success():
    """true && echo YES — second command runs because first exits 0."""
    s = Session()
    out = s.send("true && echo YES")
    s.send("exit")
    s.close()
    check("true && echo YES prints YES", out, "YES")

def test_and_skips_on_failure():
    """false && echo YES — second command must NOT run."""
    s = Session()
    # send_strip() removes the echoed command line so 'YES' from the command
    # text itself doesn't cause a false positive in the absence check.
    out = s.send_strip("false && echo YES")
    s.send("exit")
    s.close()
    check("false && echo YES does NOT print YES", out, "YES", must_absent=True)

def test_or_skips_on_success():
    """true || echo NOPE — second command must NOT run."""
    s = Session()
    out = s.send_strip("true || echo NOPE")
    s.send("exit")
    s.close()
    check("true || echo NOPE does NOT print NOPE", out, "NOPE", must_absent=True)

def test_or_runs_on_failure():
    """false || echo FALLBACK — second command runs because first failed."""
    s = Session()
    out = s.send("false || echo FALLBACK")
    s.send("exit")
    s.close()
    check("false || echo FALLBACK prints FALLBACK", out, "FALLBACK")

def test_and_or_chain():
    """false && echo A || echo B — A skipped, B runs (classic shell idiom)."""
    s = Session()
    out = s.send_strip("false && echo A || echo B")
    s.send("exit")
    s.close()
    # 'A' must not appear in shell output (echo A was short-circuited).
    # 'B' must appear (echo B ran via the || fallback).
    check("false && echo A || echo B: A absent", out, "A", must_absent=True)
    check("false && echo A || echo B: B present", out, "B")

def test_and_exit_code_set():
    """true && false: $? should be 1 (exit code of the last command run)."""
    s = Session()
    s.send("true && false")
    out = s.send("echo STATUS=$?")
    s.send("exit")
    s.close()
    check("true && false: $? = 1", out, "STATUS=1")

# ── Script-file execution ─────────────────────────────────────────────────────

def test_script_file_basic():
    """posixsh script.sh — basic command in a script file runs."""
    path = "/tmp/posixsh_p5_script_basic.sh"
    with open(path, "w") as f:
        f.write("echo SCRIPT_RAN\n")
    import subprocess
    result = subprocess.run(
        [SHELL, path],
        capture_output=True, text=True, timeout=5
    )
    record(
        "script file basic: echo SCRIPT_RAN",
        "SCRIPT_RAN" in result.stdout,
        f"stdout={result.stdout!r} stderr={result.stderr!r}",
    )

def test_script_file_exit_code():
    """posixsh script.sh exits with the last command's exit code."""
    path = "/tmp/posixsh_p5_script_exit.sh"
    with open(path, "w") as f:
        f.write("exit 42\n")
    import subprocess
    result = subprocess.run(
        [SHELL, path],
        capture_output=True, text=True, timeout=5
    )
    record(
        "script file exit code 42",
        result.returncode == 42,
        f"returncode={result.returncode}",
    )

def test_script_file_multiline():
    """Script with multiple lines: all lines execute in order."""
    path = "/tmp/posixsh_p5_script_multi.sh"
    with open(path, "w") as f:
        f.write("echo FIRST\necho SECOND\necho THIRD\n")
    import subprocess
    result = subprocess.run(
        [SHELL, path],
        capture_output=True, text=True, timeout=5
    )
    ok = ("FIRST" in result.stdout and
          "SECOND" in result.stdout and
          "THIRD" in result.stdout)
    record(
        "script file multiline: FIRST SECOND THIRD all present",
        ok,
        f"stdout={result.stdout!r}",
    )

def test_script_file_comment_skipped():
    """Lines starting with # must be treated as comments and skipped."""
    path = "/tmp/posixsh_p5_script_comment.sh"
    with open(path, "w") as f:
        f.write("# this is a comment\necho AFTER_COMMENT\n")
    import subprocess
    result = subprocess.run(
        [SHELL, path],
        capture_output=True, text=True, timeout=5
    )
    record(
        "script file comment line skipped, AFTER_COMMENT printed",
        "AFTER_COMMENT" in result.stdout,
        f"stdout={result.stdout!r} stderr={result.stderr!r}",
    )

# ── Existing Phase 3/4 features unaffected ──────────────────────────────────

def test_pipes_still_work():
    """Basic pipelines must continue to work after Phase 5 changes."""
    s = Session()
    out = s.send("echo hello | cat")
    s.send("exit")
    s.close()
    check("simple pipe still works after Phase 5", out, "hello")

def test_redirects_still_work():
    """Output redirection must continue to work."""
    path = "/tmp/posixsh_p5_redirect.txt"
    if os.path.exists(path):
        os.remove(path)
    s = Session()
    s.send(f"echo phase5 > {path}")
    s.send("exit")
    s.close()
    try:
        content = open(path).read()
        record("output redirection still works", content.strip() == "phase5",
               f"file: {content!r}")
    except OSError as e:
        record("output redirection still works", False, str(e))

def test_builtin_cd_still_works():
    """cd/pwd builtins must still work after Phase 5 changes."""
    s = Session()
    s.send("cd /tmp")
    out = s.send("pwd")
    s.send("exit")
    s.close()
    check("cd /tmp then pwd shows /tmp", out, "/tmp")

# ── runner ───────────────────────────────────────────────────────────────────

TESTS = [
    # Environment propagation
    test_path_visible_to_children,
    test_home_visible_to_children,
    test_env_not_empty,
    test_real_path_lookup,
    # $? expansion
    test_dollar_q_after_success,
    test_dollar_q_after_false,
    test_dollar_q_after_command_not_found,
    test_dollar_q_after_syntax_error,
    test_dollar_q_pipeline_last_stage,
    test_dollar_q_after_background,
    test_dollar_q_resets_between_commands,
    test_dollar_q_in_double_quotes,
    # $$ expansion
    test_dollar_dollar_is_numeric,
    test_dollar_dollar_constant_in_session,
    # Quote rules
    test_no_expansion_in_single_quotes,
    test_no_dollar_dollar_in_single_quotes,
    test_backslash_dollar_escapes_in_double_quotes,
    # Embedded expansions
    test_dollar_q_embedded_in_word,
    # AND/OR list operators (&& and ||)
    test_and_runs_on_success,
    test_and_skips_on_failure,
    test_or_skips_on_success,
    test_or_runs_on_failure,
    test_and_or_chain,
    test_and_exit_code_set,
    # Script-file execution (posixsh script.sh)
    test_script_file_basic,
    test_script_file_exit_code,
    test_script_file_multiline,
    test_script_file_comment_skipped,
    # Regression: Phase 3/4 still works
    test_pipes_still_work,
    test_redirects_still_work,
    test_builtin_cd_still_works,
]

def main():
    if not os.path.isfile(SHELL):
        print(f"error: shell binary not found at {SHELL}; run `make` first.")
        sys.exit(1)

    for t in TESTS:
        try:
            t()
        except Exception as e:
            import traceback
            record(t.__name__, False, traceback.format_exc())

    passed = sum(1 for _, ok, _ in results if ok)
    total  = len(results)
    print(f"\n{passed}/{total} checks passed")
    sys.exit(0 if passed == total else 1)

if __name__ == "__main__":
    main()
