# Test Phase 2

## Phase 2 Manual Test Commands

All commands run inside `./posixsh` unless stated otherwise.

---

### 🔍 Section 1 — Basic Word Tokenization

---

**Test 1.1 — Single word command**

```
posixsh> pwd
```

**Expected:** Prints current directory path like `/home/mayank/posix-shell`. Confirms tokenizer produces one TOKEN_WORD and parser sets `argv[0] = "pwd"`, builtin runs.

---

**Test 1.2 — Command with one argument**

```
posixsh> echo hello
```

**Expected:** `hello`
Confirms two TOKEN_WORD tokens — `argv[0]="echo"`, `argv[1]="hello"`.

---

**Test 1.3 — Command with multiple arguments**

```
posixsh> echo one two three four five
```

**Expected:** `one two three four five`
Confirms tokenizer produces 6 TOKEN_WORD tokens, all stored correctly in `argv[]`.

---

**Test 1.4 — Multiple spaces between words are collapsed**

```
posixsh> echo     hello     world
```

**Expected:** `hello world`
Same as `echo hello world`. Tokenizer skips whitespace between words — spaces are delimiters, not content.

---

**Test 1.5 — Tab characters between words**

```
posixsh> echo	hello	world
```

(use actual Tab key between words)
**Expected:** `hello world`
Tokenizer treats `\t` as whitespace, same as space.

---

**Test 1.6 — Leading spaces before command**

```
posixsh>    echo leading
```

**Expected:** `leading`
Tokenizer starts in STATE_NORMAL and skips all leading whitespace before the first word.

---

**Test 1.7 — Trailing spaces after last argument**

```
posixsh> echo trailing
```

(spaces after "trailing")
**Expected:** `trailing`
Trailing whitespace after the last word is consumed before NEWLINE token. No empty extra argument.

---

### 📝 Section 2 — Quote Handling

---

**Test 2.1 — Single quotes preserve spaces**

```
posixsh> echo 'hello world'
```

**Expected:** `hello world` (with space, as one argument)
Without quotes it would be two separate arguments. Single quotes make the space literal — tokenizer stays in STATE_IN_SINGLE_QUOTE until closing `'`.

---

**Test 2.2 — Double quotes preserve spaces**

```
posixsh> echo "hello world"
```

**Expected:** `hello world`
Same as single quotes for plain content. STATE_IN_DOUBLE_QUOTE.

---

**Test 2.3 — Multiple quoted arguments**

```
posixsh> echo "first arg" "second arg"
```

**Expected:** `first arg second arg`
Two separate TOKEN_WORD tokens — `argv[1]="first arg"`, `argv[2]="second arg"`. The space between the closing `"` and opening `"` is the delimiter.

---

**Test 2.4 — Adjacent single-quote sections merge into one word**

```
posixsh> echo 'foo''bar'
```

**Expected:** `foobar`
Tokenizer exits single-quote mode then immediately re-enters it — no whitespace between them so no token boundary. Both sections merge into one TOKEN_WORD.

---

**Test 2.5 — Adjacent double-quote sections merge into one word**

```
posixsh> echo "foo""bar"
```

**Expected:** `foobar`
Same logic as above but with double quotes.

---

**Test 2.6 — Mixed quote types merge**

```
posixsh> echo 'foo'"bar"
```

**Expected:** `foobar`
Single-quote section followed immediately by double-quote section, no space → same token.

---

**Test 2.7 — Quoted metacharacter is literal**

```
posixsh> echo 'hello | world'
```

**Expected:** `hello | world`
The `|` is inside single quotes so it is NOT a TOKEN_PIPE. It is just a character in the WORD value.

---

**Test 2.8 — Quoted redirect is literal**

```
posixsh> echo 'hello > world'
```

**Expected:** `hello > world`
The `>` inside quotes is not TOKEN_REDIR_OUT. No file is created. The entire string is printed.

---

**Test 2.9 — Quoted ampersand is literal**

```
posixsh> echo 'hello & world'
```

**Expected:** `hello & world`
No background execution. The `&` is not TOKEN_BACKGROUND because it is inside single quotes.

---

**Test 2.10 — Double quotes with backslash escape before double quote**

```
posixsh> echo "say \"hi\""
```

**Expected:** `say "hi"`
Inside double quotes, `\"` is a POSIX-required escape sequence — backslash before `"` produces a literal `"`. The tokenizer handles this in STATE_IN_DOUBLE_QUOTE.

---

**Test 2.11 — Single quotes do NOT process backslash**

```
posixsh> echo 'back\slash'
```

**Expected:** `back\slash`
Inside single quotes, backslash is completely literal. No escape processing at all.

---

**Test 2.12 — Empty single quotes produce empty string argument**

```
posixsh> echo ''
```

**Expected:** (empty line)
Two adjacent `'` with nothing between them produce a zero-length TOKEN_WORD. Echo receives one empty argument and prints a newline only.

---

### ⚡ Section 3 — Operator Token Recognition

---

**Test 3.1 — Pipe operator creates two-stage pipeline**

```
posixsh> echo hello | cat
```

**Expected:** `hello`
TOKEN_PIPE causes parser to save first command and start second. Two children, connected by a pipe.

---

**Test 3.2 — Three-stage pipeline**

```
posixsh> echo hello | cat | cat
```

**Expected:** `hello`
Two TOKEN_PIPE tokens → pipeline.count = 3.

---

**Test 3.3 — Output redirect `>` creates file**

```
posixsh> echo test123 > /tmp/p2test.txt
cat /tmp/p2test.txt
```

**Expected:** First command produces no output (redirected). Second command prints `test123`.

---

**Test 3.4 — Output redirect overwrites existing file**

```
posixsh> echo first > /tmp/p2over.txt
posixsh> echo second > /tmp/p2over.txt
posixsh> cat /tmp/p2over.txt
```

**Expected:** `second`
O_TRUNC flag — file content replaced, not appended.

---

**Test 3.5 — Append redirect `>>` preserves existing content**

```
posixsh> echo line1 > /tmp/p2append.txt
posixsh> echo line2 >> /tmp/p2append.txt
posixsh> cat /tmp/p2append.txt
```

**Expected:**

```
line1
line2
```

TOKEN_REDIR_APPEND → O_APPEND flag. First line preserved, second added.

---

**Test 3.6 — `>>` vs `>` are distinguished correctly**

```
posixsh> echo first > /tmp/p2check.txt
posixsh> echo second >> /tmp/p2check.txt
posixsh> echo third > /tmp/p2check.txt
posixsh> cat /tmp/p2check.txt
```

**Expected:** `third`
Third command used `>` (overwrite), so only `third` remains.

---

**Test 3.7 — Input redirect `<` feeds file to command**

```
posixsh> echo "from file" > /tmp/p2in.txt
posixsh> cat < /tmp/p2in.txt
```

**Expected:** `from file`
TOKEN_REDIR_IN → command reads from file instead of terminal.

---

**Test 3.8 — Background `&` returns prompt immediately**

```
posixsh> sleep 5 &
```

**Expected:** Something like `[1] 12345` then `posixsh>` appears immediately. Shell does not wait 5 seconds. TOKEN_BACKGROUND → pipeline.background = 1 → skip wait4.

---

**Test 3.9 — Background with pipeline**

```
posixsh> sleep 5 | sleep 5 &
```

**Expected:** Job notification printed, prompt returns immediately. Both stages run in background.

---

**Test 3.10 — Pipe immediately adjacent to word (no space)**

```
posixsh> echo hi|cat
```

**Expected:** `hi`
Tokenizer: `echo` → WORD, then `hi` → WORD (because `|` is a metacharacter boundary), then `|` → TOKEN_PIPE, then `cat` → WORD. No spaces required around `|`.

---

**Test 3.11 — Redirect immediately adjacent to word**

```
posixsh> echo hi>/tmp/p2adj.txt
posixsh> cat /tmp/p2adj.txt
```

**Expected:** `hi`
No spaces around `>` required. Tokenizer recognizes `>` as a metacharacter mid-stream.

---

### 🏗️ Section 4 — Parser Structure Tests

---

**Test 4.1 — Redirect applies to correct command in pipeline**

```
posixsh> echo hello | cat > /tmp/p2pipe_redir.txt
posixsh> cat /tmp/p2pipe_redir.txt
```

**Expected:** `hello`
The `>` applies to `cat` (last stage), not to `echo`. Parser stores output_file on `commands[1]`, not `commands[0]`.

---

**Test 4.2 — Input redirect on first stage of pipeline**

```
posixsh> echo "hello" > /tmp/p2src.txt
posixsh> cat < /tmp/p2src.txt | cat
```

**Expected:** `hello`
Input redirect on `commands[0]`, pipe connects to `commands[1]`.

---

**Test 4.3 — Both redirects on single command**

```
posixsh> echo "src" > /tmp/p2both_src.txt
posixsh> cat < /tmp/p2both_src.txt > /tmp/p2both_dst.txt
posixsh> cat /tmp/p2both_dst.txt
```

**Expected:** `src`
Parser stores both `input_file` and `output_file` on the same command.

---

**Test 4.4 — NULL terminator on argv**

This is internal but observable — if argv is not NULL-terminated, execve crashes:

```
posixsh> ls -la /tmp
```

**Expected:** Long listing of /tmp. If argv[argc] were not NULL, execve would read garbage as extra arguments or crash.

---

**Test 4.5 — Max arguments handled safely**

```
posixsh> echo a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4
```

(31 arguments to echo — close to MAX_ARGS=32)
**Expected:** All characters printed on one line. No crash, no truncation error.

---

### 🔴 Section 5 — Syntax Error Handling

---

**Test 5.1 — Trailing pipe is a syntax error**

```
posixsh> ls |
```

**Expected:**

```
posixsh: syntax error
posixsh>
```

Parser: TOKEN_PIPE followed by TOKEN_NEWLINE → current_cmd is empty after pipe → error. Shell must NOT crash and must show new prompt.

---

**Test 5.2 — Leading pipe is a syntax error**

```
posixsh> | ls
```

**Expected:**

```
posixsh: syntax error
posixsh>
```

Parser: first token is TOKEN_PIPE before any WORD → empty command before pipe → error.

---

**Test 5.3 — Double pipe is a syntax error**

```
posixsh> ls || cat
```

**Expected:**

```
posixsh: syntax error
posixsh>
```

Note: `||` is a bash extension (OR operator). Our shell has no `||` — the parser sees TOKEN_PIPE then TOKEN_PIPE → second pipe immediately after saving empty command → error.

---

**Test 5.4 — `>` with no filename is a syntax error**

```
posixsh> echo hi >
```

**Expected:**

```
posixsh: syntax error
posixsh>
```

Parser: TOKEN_REDIR_OUT → advance → next token is TOKEN_NEWLINE, not TOKEN_WORD → error.

---

**Test 5.5 — `<` with no filename is a syntax error**

```
posixsh> cat
```

**Expected:**

```
posixsh: syntax error
posixsh>
```

---

**Test 5.6 — Shell continues after syntax errors**

```
posixsh> ls |
posixsh> | cat
posixsh> echo >
posixsh> echo survived
```

**Expected:** Three `syntax error` messages then `survived`. Shell must remain alive through all errors.

---

### 🏷️ Section 6 — Builtin Detection Tests

---

**Test 6.1 — cd is detected as builtin (changes shell's own directory)**

```
posixsh> cd /tmp
posixsh> pwd
```

**Expected:** `/tmp`
If `cd` were NOT a builtin, the shell would fork a child, the child's `cd` would run in the child's process, and the SHELL's directory would not change. `pwd` would still show the original directory. The fact that `/tmp` appears proves builtin detection works.

---

**Test 6.2 — cd with no argument**

```
posixsh> cd
posixsh> pwd
```

**Expected:** Either `/` or current directory (implementation dependent). Shell must not crash.

---

**Test 6.3 — cd to nonexistent directory shows error**

```
posixsh> cd /this/does/not/exist
```

**Expected:** Error message containing `cd:` or the path name. Shell stays alive, prompt returns.

---

**Test 6.4 — pwd is detected as builtin**

```
posixsh> cd /var
posixsh> pwd
```

**Expected:** `/var`
Confirms builtin pwd reflects the current directory after cd changed it.

---

**Test 6.5 — exit is detected as builtin**

```
posixsh> exit
```

**Expected:** Shell exits. Return to normal terminal.

If `exit` were NOT a builtin and were forked as an external command, only the CHILD process would exit. The shell would keep running. The fact that the shell itself terminates proves `is_builtin` detection is correct.

---

**Test 6.6 — Builtins inside a pipeline run as subshell (correct behavior)**

```
posixsh> cd /tmp | echo done
```

**Expected:** `done` is printed. The `cd` inside the pipeline runs in a child (subshell) so it does NOT change the shell's directory. Verify:

```
posixsh> pwd
```

**Expected:** Your original directory, NOT `/tmp`. This is correct POSIX behavior.

---

### 📊 Section 7 — Combined and Edge Case Tests

---

**Test 7.1 — Blank line after command**

```
posixsh> echo hello

posixsh>
```

**Expected:** `hello` appears, then blank Enter just shows new prompt. No crash.

---

**Test 7.2 — Only whitespace line**

```
posixsh>
```

(just spaces, then Enter)
**Expected:** Just a new `posixsh>` prompt. token_count = 0, pipeline.count = 0, skipped.

---

**Test 7.3 — Pipe with spaces in quoted argument**

```
posixsh> echo 'hello world' | cat
```

**Expected:** `hello world`
Quoted space is preserved through the pipe. cat receives the full string.

---

**Test 7.4 — Redirect with quoted filename**

```
posixsh> echo test > '/tmp/p2 space.txt'
posixsh> cat '/tmp/p2 space.txt'
```

**Expected:** `test`
Filename with space inside quotes is treated as one TOKEN_WORD. The file `/tmp/p2 space.txt` is created and read correctly.

---

**Test 7.5 — Multiple operators in one command**

```
posixsh> echo "src" > /tmp/p2src2.txt
posixsh> cat < /tmp/p2src2.txt | cat | cat > /tmp/p2dst2.txt
posixsh> cat /tmp/p2dst2.txt
```

**Expected:** `src`
Parser handles: input_file on commands[0], two TOKEN_PIPE creating three stages, output_file on commands[2].

---

**Test 7.6 — Large number of arguments**

```
posixsh> echo 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
```

**Expected:** All 30 numbers printed on one line. Tests that argv array is filled correctly up to MAX_ARGS.

---

**Test 7.7 — Strace shows correct pipeline syscall sequence**

In your normal terminal:

```bash
strace -e trace=pipe,fork,dup2,execve,close ./posixsh 2>/tmp/strace_p2.txt
```

Inside posixsh:

```
posixsh> echo hello | cat
posixsh> exit
```

Then:

```bash
cat /tmp/strace_p2.txt
```

**Expected sequence of syscalls:**

```
pipe(...)          ← one pipe created for the pipeline
fork()             ← first child (echo)
fork()             ← second child (cat)
dup2(...)          ← stdout of echo → pipe write end
dup2(...)          ← stdin of cat ← pipe read end
close(...)         ← close unused pipe ends
execve(".../echo", ...)
execve(".../cat", ...)
```

This confirms the parser produced the correct pipeline structure and the executor used it correctly.

---

**Test 7.8 — Token count confirmed with a complex line**

```bash
strace -e trace=write ./posixsh 2>&1
```

Inside posixsh:

```
posixsh> echo a b c | cat > /tmp/p2tc.txt
posixsh> exit
```

**Expected:** No strace error, file `/tmp/p2tc.txt` contains `a b c`. Confirms 8 tokens were handled: WORD WORD WORD WORD PIPE WORD WORD REDIR_OUT WORD EOF. (echo, a, b, c → pipe → cat → > → filename)

---

### 📊 Summary — What Each Section Tests

| Section | Phase 2 Component |
| --- | --- |
| 1 — Word tokenization | STATE_NORMAL, STATE_IN_WORD, whitespace handling |
| 2 — Quote handling | STATE_IN_SINGLE_QUOTE, STATE_IN_DOUBLE_QUOTE, merging |
| 3 — Operator tokens | TOKEN_PIPE, REDIR_OUT, REDIR_APPEND, REDIR_IN, BACKGROUND |
| 4 — Parser structure | Pipeline struct, Command argv, redirections, NULL termination |
| 5 — Syntax errors | parse() returning -1, error message, shell survival |
| 6 — Builtin detection | is_builtin flag, cd/pwd/exit run in shell process |
| 7 — Combined edge cases | All components working together, strace validation |