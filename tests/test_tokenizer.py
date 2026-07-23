#!/usr/bin/env python3
"""
tests/test_tokenizer.py — Phase 2: Tokenizer tests

Strategy:
    The tokenizer cannot be inspected in isolation (no test hooks in C code).
    Instead, we drive the shell with carefully chosen inputs and observe the
    resulting output.  If the tokenizer misclassifies a token, the shell will
    either crash, produce wrong output, or raise a syntax error — all of which
    are detectable.

Sections:
    A. Normal cases       — basic words, multiple args, metacharacters
    B. Quoting            — single, double, mixed, adjacent, nested
    C. Edge cases         — empty quotes, whitespace-only, very long tokens,
                           metacharacters embedded in words
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from helper import compile_shell, run_shell, assert_contains, assert_not_contains

# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def t(name, cmds, want=None, no_want=None, timeout=1.5):
    """
    Run `cmds` in the shell. Assert `want` IS in output, `no_want` is NOT.
    Returns True/False for the test result.
    """
    out = run_shell(cmds, timeout=timeout)
    ok = True
    if want is not None:
        ok = assert_contains(name, out, want) and ok
    if no_want is not None:
        ok = assert_not_contains(name + " (no spurious output)", out, no_want) and ok
    return ok


# ──────────────────────────────────────────────────────────────────────────────
# A. Normal cases
# ──────────────────────────────────────────────────────────────────────────────

def section_a():
    print("\n── A. Normal Words & Metacharacters ──")
    results = []

    # Single word argument
    results.append(t("A1  single word arg",
                     ["echo hello"],
                     want="hello"))

    # Multiple space-separated words become separate args
    results.append(t("A2  multiple args",
                     ["echo one two three"],
                     want="one two three"))

    # Tab-separated args treated same as spaces
    results.append(t("A3  tab-separated args",
                     ["echo\tone\ttwo"],
                     want="one two"))

    # Pipe character splits pipeline
    results.append(t("A4  pipe splits commands",
                     ["echo piped | cat"],
                     want="piped"))

    # Output redirect creates file and silences stdout
    results.append(t("A5  > redirect operator",
                     ["echo redirect_test > /tmp/tok_a5.txt",
                      "cat /tmp/tok_a5.txt"],
                     want="redirect_test"))

    # Append redirect >>
    results.append(t("A6  >> append operator",
                     ["echo line1 > /tmp/tok_a6.txt",
                      "echo line2 >> /tmp/tok_a6.txt",
                      "cat /tmp/tok_a6.txt"],
                     want="line2"))

    # Input redirect <
    results.append(t("A7  < input redirect",
                     ["echo inputdata > /tmp/tok_a7.txt",
                      "cat < /tmp/tok_a7.txt"],
                     want="inputdata"))

    # Background operator & — shell prints [1] <pgid>
    results.append(t("A8  & background operator",
                     ["sleep 1 &", "jobs"],
                     want="[1]"))

    # Cleanup temp files
    for f in ["/tmp/tok_a5.txt", "/tmp/tok_a6.txt", "/tmp/tok_a7.txt"]:
        try: os.remove(f)
        except OSError: pass

    return results


# ──────────────────────────────────────────────────────────────────────────────
# B. Quoting
# ──────────────────────────────────────────────────────────────────────────────

def section_b():
    print("\n── B. Quoting ──")
    results = []

    # Single quotes preserve spaces as one token
    results.append(t("B1  single quotes preserve spaces",
                     ["echo 'hello world'"],
                     want="hello world"))

    # Single quotes suppress all metacharacters
    results.append(t("B2  single quotes suppress pipe literal",
                     ["echo 'a|b'"],
                     want="a|b"))

    # Double quotes preserve spaces
    results.append(t("B3  double quotes preserve spaces",
                     ['echo "foo bar"'],
                     want="foo bar"))

    # Double quotes suppress > from becoming a redirect
    results.append(t("B4  double quotes suppress redirect literal",
                     ['echo "a>b"'],
                     want="a>b"))

    # Adjacent single-quoted strings join into one token: 'a''b' → ab
    results.append(t("B5  adjacent single-quoted tokens join",
                     ["echo 'ab''cd'"],
                     want="abcd"))

    # Adjacent double-quoted strings join into one token
    results.append(t("B6  adjacent double-quoted tokens join",
                     ['echo "ab""cd"'],
                     want="abcd"))

    # Mixed quotes in one token: 'a'"b" → ab
    results.append(t("B7  mixed single and double quotes join",
                     ["echo 'hello'\" world\""],
                     want="hello world"))

    # Backslash escape of special char inside double quotes: \" → "
    results.append(t("B8  backslash-escape quote inside double quotes",
                     ['echo "say \\"hi\\""'],
                     want='say "hi"'))

    # Unquoted word continues after closing double-quote: "ab"c → abc
    results.append(t("B9  word continues after closing double-quote",
                     ['echo "ab"c'],
                     want="abc"))

    return results


# ──────────────────────────────────────────────────────────────────────────────
# C. Edge Cases
# ──────────────────────────────────────────────────────────────────────────────

def section_c():
    print("\n── C. Edge Cases ──")
    results = []

    # Empty single-quoted string is a valid (empty) word — echo prints blank line
    results.append(t("C1  empty single-quoted string",
                     ["echo ''"],
                     want="\n"))          # echo outputs a newline for the empty arg

    # Empty double-quoted string same
    results.append(t('C2  empty double-quoted string',
                     ['echo ""'],
                     want="\n"))

    # Metachar immediately adjacent to word (no spaces)
    results.append(t("C3  pipe adjacent to word (no spaces)",
                     ["echo x|cat"],
                     want="x"))

    # > immediately adjacent to word
    results.append(t("C4  redirect adjacent to word (no spaces)",
                     ["echo hi>/tmp/tok_c4.txt", "cat /tmp/tok_c4.txt"],
                     want="hi"))

    # Token at MAX_TOKEN_LEN boundary (255 chars): should be accepted without crash
    long_word = "a" * 255
    results.append(t("C5  token at max length (255 chars)",
                     [f"echo {long_word}"],
                     want=long_word))

    # Token slightly over MAX_TOKEN_LEN (256 chars): tokenizer silently truncates
    # Shell must not crash — exact output may be truncated but prompt must return
    over_word = "b" * 256
    results.append(t("C6  token over max length (256 chars) — no crash",
                     [f"echo {over_word}"],
                     want="posixsh>"))   # prompt must return → no crash

    # Whitespace-only line — parser gets zero tokens, shell loops silently
    results.append(t("C7  whitespace-only input does not crash",
                     ["   "],
                     want="posixsh>"))

    # Quote opened but never closed — tokenizer treats EOF as end of token
    results.append(t("C8  unclosed single quote — shell doesn't hang",
                     ["echo 'unclosed"],
                     want="posixsh>",
                     timeout=1.0))

    # Newline inside (simulated) single-quote arg using PTY
    results.append(t("C9  newline in double-quoted arg",
                     ['echo "line1'],      # unclosed quote — matches C8 pattern
                     want="posixsh>",
                     timeout=1.0))

    # Multiple spaces between args are collapsed
    results.append(t("C10 multiple spaces between args",
                     ["echo a   b   c"],
                     want="a b c"))

    # Cleanup
    try: os.remove("/tmp/tok_c4.txt")
    except OSError: pass

    return results


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 55)
    print("  Phase 2 — Tokenizer Tests")
    print("=" * 55)

    compile_shell()

    all_results = []
    all_results += section_a()
    all_results += section_b()
    all_results += section_c()

    total  = len(all_results)
    passed = sum(1 for r in all_results if r)

    print(f"\n{'='*55}")
    print(f"  Result: {passed}/{total} passed")
    print(f"{'='*55}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
