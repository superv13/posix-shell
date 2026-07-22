# Test Phase 3

## Phase 3 Manual Test Commands

Run these from your project root. Start a fresh `./posixsh` for each section unless stated otherwise.

---

### 🔧 Section 1 — Single Command Execution

---

**Test 1.1 — Basic external command runs**

```
posixsh> echo hello
```

**Expected:** `hello`
Confirms fork → execve("echo") → wait cycle works end to end.

---

**Test 1.2 — Command with multiple arguments**

```
posixsh> echo one two three
```

**Expected:** `one two three`
argv[0]="echo", argv[1]="one", argv[2]="two", argv[3]="three", argv[4]=NULL. Confirms NULL termination is correct.

---

**Test 1.3 — Command from /bin**

```
posixsh> ls /tmp
```

**Expected:** List of files in `/tmp`. Confirms PATH search finds `/bin/ls`.

---

**Test 1.4 — Command from /usr/bin**

```
posixsh> wc --version
```

**Expected:** Version line containing `wc`. Confirms PATH search reaches `/usr/bin`.

---

**Test 1.5 — Absolute path bypasses PATH search**

```
posixsh> /bin/echo direct
```

**Expected:** `direct/` in argv[0] → `find_executable` returns it unchanged, no PATH search.

---

**Test 1.6 — Unknown command reports error**

```
posixsh> thisdoesnotexist_xyz
```

**Expected:** `posixsh: thisdoesnotexist_xyz: command not found`
Exit code 127 returned from child. Shell continues normally.

---

**Test 1.7 — Shell continues after failed command**

```
posixsh> thisdoesnotexist_xyz
posixsh> echo still_alive
```

**Expected:** Error message then `still_alive`. Shell did not crash or exit.

---

**Test 1.8 — Command exits cleanly, prompt returns**

```
posixsh> sleep 0.1
posixsh> echo done
```

**Expected:** After sleep finishes, `done` is printed. Confirms `wait4` blocks until child exits before showing next prompt.

---

**Test 1.9 — Verify fork actually creates a child**

```bash
strace -f -e trace=fork,execve,wait4 ./posixsh
```

Type `echo hello` then `exit`.
**Expected strace output contains:**

```
fork()           = <some pid>
execve("/bin/echo", ["echo", "hello"], ...)
wait4(<pid>, ...)
```

Exactly one fork, one execve, one wait4 per command.

---

### 📁 Section 2 — PATH Search

---

**Test 2.1 — ls found in /bin**

```
posixsh> ls /tmp
```

**Expected:** Directory listing. `find_executable("ls")` tries `/bin/ls` → found.

---

**Test 2.2 — grep found in /usr/bin**

```
posixsh> grep root /etc/passwd
```

**Expected:** Line containing `root`. Confirms `/usr/bin/grep` found.

---

**Test 2.3 — wc found in /usr/bin**

```
posixsh> echo hello | wc -c
```

**Expected:** `6` (5 chars + newline).

---

**Test 2.4 — Command with same name in both /bin and /usr/bin uses first match**

```
posixsh> which ls
```

Then compare with what posixsh finds by running ls directly. PATH search order in `path.c` determines which one runs.

---

**Test 2.5 — Absolute path that does not exist reports error**

```
posixsh> /this/path/does/not/exist
```

**Expected:** `posixsh: /this/path/does/not/exist: command not found` or `exec failed`. Shell does not crash.

---

### 📤 Section 3 — Output Redirection `>`

---

**Test 3.1 — Basic output redirect creates file**

```
posixsh> echo hello > /tmp/p3_test1.txt
posixsh> cat /tmp/p3_test1.txt
```

**Expected:** `hello`
File is created, stdout of echo went to file not terminal.

---

**Test 3.2 — Output redirect overwrites existing file**

```
posixsh> echo first > /tmp/p3_overwrite.txt
posixsh> echo second > /tmp/p3_overwrite.txt
posixsh> cat /tmp/p3_overwrite.txt
```

**Expected:** `second`
Only `second` — not `firstsecond` or two lines. `O_TRUNC` flag works correctly.

---

**Test 3.3 — Redirect to /dev/null discards output**

```
posixsh> echo hello > /dev/null
```

**Expected:** Nothing printed to terminal. No error.

---

**Test 3.4 — Long output redirected to file**

```
posixsh> ls -la /usr/bin > /tmp/p3_long.txt
posixsh> wc -l /tmp/p3_long.txt
```

**Expected:** A number greater than 100. Confirms large writes work correctly.

---

**Test 3.5 — File is created with correct permissions**

```
posixsh> echo test > /tmp/p3_perms.txt
```

Then in normal terminal:

```bash
ls -l /tmp/p3_perms.txt
```

**Expected:** `-rw-r--r--` (0644 = `REDIR_CREATE_MODE`). Owner has read+write, group and others have read only.

---

**Test 3.6 — Redirect does not affect shell stdout**

```
posixsh> echo before
posixsh> echo hello > /tmp/p3_noaffect.txt
posixsh> echo after
```

**Expected:** `before` and `after` both appear on terminal. `hello` does not appear on terminal (went to file). The dup2 happened in the child, not the shell.

---

### 📥 Section 4 — Input Redirection `<`

---

**Test 4.1 — Basic input redirect reads from file**

```bash
echo "from_file_content" > /tmp/p3_input.txt
```

Then inside posixsh:

```
posixsh> cat < /tmp/p3_input.txt
```

**Expected:** `from_file_content`
cat's stdin is the file, not the keyboard.

---

**Test 4.2 — Sort reads from file via redirect**

```bash
printf "banana\napple\ncherry\n" > /tmp/p3_sort.txt
```

Then inside posixsh:

```
posixsh> sort < /tmp/p3_sort.txt
```

**Expected:**

```
apple
banana
cherry
```

---

**Test 4.3 — wc -l counts lines from file**

```bash
printf "a\nb\nc\nd\n" > /tmp/p3_wc.txt
```

Then inside posixsh:

```
posixsh> wc -l < /tmp/p3_wc.txt
```

**Expected:** `4`

---

**Test 4.4 — Input redirect from nonexistent file reports error**

```
posixsh> cat < /tmp/this_file_does_not_exist_xyz.txt
posixsh> echo survived
```

**Expected:** Error message about cannot open file, then `survived`. Shell does not crash.

---

### 📎 Section 5 — Append Redirection `>>`

---

**Test 5.1 — Append adds to existing content**

```
posixsh> echo first > /tmp/p3_append.txt
posixsh> echo second >> /tmp/p3_append.txt
posixsh> cat /tmp/p3_append.txt
```

**Expected:**

```
first
second
```

Both lines present. `O_APPEND` flag used, not `O_TRUNC`.

---

**Test 5.2 — Multiple appends accumulate**

```
posixsh> echo a > /tmp/p3_multi.txt
posixsh> echo b >> /tmp/p3_multi.txt
posixsh> echo c >> /tmp/p3_multi.txt
posixsh> echo d >> /tmp/p3_multi.txt
posixsh> wc -l < /tmp/p3_multi.txt
```

**Expected:** `4`

---

**Test 5.3 — Append creates file if it does not exist**

```
posixsh> rm -f /tmp/p3_newappend.txt
posixsh> echo hello >> /tmp/p3_newappend.txt
posixsh> cat /tmp/p3_newappend.txt
```

**Expected:** `helloO_CREAT` is in the flags for append — file is created if absent.

---

**Test 5.4 — `>` then `>>` prove they are different**

```
posixsh> echo line1 > /tmp/p3_diff.txt
posixsh> echo line2 > /tmp/p3_diff.txt
posixsh> cat /tmp/p3_diff.txt
```

**Expected:** Only `line2` — `>` truncated.

```
posixsh> echo line1 > /tmp/p3_diff2.txt
posixsh> echo line2 >> /tmp/p3_diff2.txt
posixsh> cat /tmp/p3_diff2.txt
```

**Expected:** Both `line1` and `line2` — `>>` appended.

---

### 🔗 Section 6 — Two-Stage Pipelines

---

**Test 6.1 — Basic two-stage pipe**

```
posixsh> echo hello | cat
```

**Expected:** `hello`
echo writes to pipe write-end. cat reads from pipe read-end. Output appears on terminal.

---

**Test 6.2 — grep filters echo output**

```
posixsh> echo hello | grep hello
```

**Expected:** `hello`

```
posixsh> echo hello | grep world
```

**Expected:** Nothing (grep found no match, exits with code 1, no output).

---

**Test 6.3 — wc counts words from echo**

```
posixsh> echo one two three | wc -w
```

**Expected:** `3`

---

**Test 6.4 — wc counts chars**

```
posixsh> echo hello | wc -c
```

**Expected:** `6` (5 letters + newline that echo appends)

---

**Test 6.5 — ls output piped to grep**

```
posixsh> ls /etc | grep passwd
```

**Expected:** `passwd` (and possibly `passwd-`)

---

**Test 6.6 — ls output piped to wc**

```
posixsh> ls /bin | wc -l
```

**Expected:** A number. Confirms ls produces output, wc receives it through the pipe.

---

**Test 6.7 — sort receives input through pipe**

```
posixsh> printf 'banana\napple\n' | sort
```

**Expected:**

```
apple
banana
```

---

**Test 6.8 — Pipe data flows in correct direction**

```
posixsh> echo left | cat
posixsh> cat /etc/hostname | cat
```

**Expected:** Both commands output the left-side content. Confirms read-end/write-end wiring is not reversed.

---

### 🔗🔗 Section 7 — Three-Stage and Longer Pipelines

---

**Test 7.1 — Three-stage pipeline**

```
posixsh> echo hello | cat | cat
```

**Expected:** `hello`
Two pipes created. Three children. Data flows through both pipes.

---

**Test 7.2 — Three stages with filtering**

```
posixsh> ls /etc | grep conf | wc -l
```

**Expected:** A number greater than 0. Confirms N-1 pipes for N commands formula works for N=3.

---

**Test 7.3 — Four-stage pipeline**

```
posixsh> echo hello | cat | cat | cat
```

**Expected:** `hello`

---

**Test 7.4 — Five-stage pipeline**

```
posixsh> ls /usr/bin | grep a | grep e | sort | head -5
```

**Expected:** Five command names containing both `a` and `e`, sorted alphabetically.

---

**Test 7.5 — Critical pipe fd hygiene test**

```
posixsh> yes | head -n 5
```

**Expected:**

```
y
y
y
y
y
```

Returns immediately. `yes` produces infinite output but `head -n 5` closes its stdin after 5 lines. This sends SIGPIPE to `yes` which terminates it. If any child forgot to close an unused pipe end, `yes` would never get SIGPIPE and this command would hang forever.

---

**Test 7.6 — Longer hygiene test**

```
posixsh> yes | head -n 3 | wc -l
```

**Expected:** `3`
Returns immediately. Three stages, pipe fd hygiene must be correct in all three children.

---

**Test 7.7 — Pipeline near MAX_PIPELINE_DEPTH**

```
posixsh> echo test | cat | cat | cat | cat | cat | cat | cat
```

(7 pipes, 8 stages = MAX_PIPELINE_DEPTH)
**Expected:** `test`
Exactly at the limit. Should work.

---

**Test 7.8 — Pipeline exceeding MAX_PIPELINE_DEPTH**

```
posixsh> echo test | cat | cat | cat | cat | cat | cat | cat | cat
```

(8 pipes, 9 stages — exceeds MAX_PIPELINE_DEPTH=8)
**Expected:** Either `test` (extra stages ignored) or an error message. Shell must NOT crash or hang.

---

### 🔀 Section 8 — Pipe Combined with Redirection

---

**Test 8.1 — Input redirect into pipeline**

```bash
echo "hello world" > /tmp/p3_pipein.txt
```

Then inside posixsh:

```
posixsh> cat < /tmp/p3_pipein.txt | wc -w
```

**Expected:** `2`
File feeds cat's stdin. cat's stdout feeds wc through pipe.

---

**Test 8.2 — Pipeline output redirected to file**

```
posixsh> echo hello | cat > /tmp/p3_pipeout.txt
posixsh> cat /tmp/p3_pipeout.txt
```

**Expected:** `hello`
cat's stdout was redirected to file (redirect overrides pipe on last stage).

---

**Test 8.3 — Both input and output redirect with pipe**

```bash
printf "banana\napple\n" > /tmp/p3_both_in.txt
```

Then inside posixsh:

```
posixsh> sort < /tmp/p3_both_in.txt | cat > /tmp/p3_both_out.txt
posixsh> cat /tmp/p3_both_out.txt
```

**Expected:**

```
apple
banana
```

---

**Test 8.4 — Three-stage with final output redirect**

```
posixsh> ls /etc | grep conf | sort > /tmp/p3_3stage.txt
posixsh> head -3 /tmp/p3_3stage.txt
```

**Expected:** Three sorted filenames containing `conf`. Confirms redirect applies to last stage only.

---

### ⚡ Section 9 — Background Execution `&`

---

**Test 9.1 — Background job returns prompt immediately**

```
posixsh> sleep 5 &
```

**Expected:** Shell prints job notification and immediately shows `posixsh>` prompt. Does NOT wait 5 seconds. You can type new commands right away.

---

**Test 9.2 — Shell is responsive during background job**

```
posixsh> sleep 10 &
posixsh> echo hello
```

**Expected:** `hello` appears immediately. Shell is fully usable while sleep runs in background.

---

**Test 9.3 — Multiple background jobs launch without blocking**

```
posixsh> sleep 10 &
posixsh> sleep 10 &
posixsh> sleep 10 &
posixsh> echo all_launched
```

**Expected:** `all_launched` appears quickly. Three background jobs started, none blocked the shell.

---

**Test 9.4 — Background job notification shows PID**

```
posixsh> sleep 10 &
```

**Expected:** Output contains `[1]` and a number (the PID). Format: `[1] 12345`

---

**Test 9.5 — Background pipeline**

```
posixsh> ls /usr/bin | grep a | sort > /tmp/p3_bgpipe.txt &
posixsh> echo launched
```

**Expected:** `launched` appears immediately. File `/tmp/p3_bgpipe.txt` is populated in the background.

```bash
sleep 1 && cat /tmp/p3_bgpipe.txt | head -3
```

**Expected:** A few command names.

---

### 🏠 Section 10 — Builtin Commands

---

**Test 10.1 — pwd shows current directory**

```
posixsh> pwd
```

**Expected:** An absolute path starting with `/`.

---

**Test 10.2 — cd changes directory**

```
posixsh> cd /tmp
posixsh> pwd
```

**Expected:** `/tmp`

---

**Test 10.3 — cd then pwd confirm the change**

```
posixsh> cd /var
posixsh> pwd
posixsh> cd /usr
posixsh> pwd
```

**Expected:** `/var` then `/usr`

---

**Test 10.4 — cd to nonexistent directory reports error**

```
posixsh> cd /this/does/not/exist/xyz
posixsh> echo survived
```

**Expected:** Error message from cd, then `survived`. Shell does not crash or exit.

---

**Test 10.5 — cd does not fork (affects shell's own cwd)**

```
posixsh> cd /tmp
posixsh> ls
```

**Expected:** Contents of `/tmp` listed. If `cd` were forked as an external command, only the child's cwd would change, and `ls` would still show the original directory.

---

**Test 10.6 — exit terminates shell**

```
posixsh> exit
```

**Expected:** Shell exits. Returns to normal terminal prompt.

---

### 🔬 Section 11 — Strace Verification

These run from your normal terminal, not inside posixsh.

---

**Test 11.1 — Single command syscall sequence**

```bash
strace -e trace=fork,execve,wait4,dup2,pipe,open,close ./posixsh
```

Type `echo hello` then `exit`.
**Expected sequence:**

```
write(1, "posixsh> ", 9)
read(0, "echo hello\n", ...)
fork()  = <child_pid>
--- child: execve("/bin/echo", ["echo", "hello"], NULL)
wait4(<child_pid>, ...)
write(1, "posixsh> ", 9)
read(0, "exit\n", ...)
exit(0)
```

---

**Test 11.2 — Pipe syscall sequence**

```bash
strace -e trace=fork,execve,pipe,dup2,close,wait4 ./posixsh
```

Type `echo hi | cat` then `exit`.
**Expected:** One `pipe()` call appears before two `fork()` calls. Each child has `dup2` calls connecting it to the pipe. Parent has `close()` calls for both pipe ends. Two `wait4()` calls.

---

**Test 11.3 — Output redirect syscall sequence**

```bash
strace -e trace=open,dup2,fork,execve,wait4,close ./posixsh
```

Type `echo test > /tmp/strace_p3.txt` then `exit`.
**Expected:** In child: `open("/tmp/strace_p3.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)` then `dup2(fd, 1)` then `close(fd)` then `execve`.

---

**Test 11.4 — Confirm no libc calls during command execution**

```bash
strace ./posixsh 2>&1 | grep -E "mmap|brk|malloc|openat.*ld"
```

Type `echo hello` then `exit`.
**Expected:** No output. None of these library-loading syscalls appear during command execution.

---

### 📊 Section 12 — Edge Cases

---

**Test 12.1 — Empty pipeline stages are rejected**

```
posixsh> | echo hello
posixsh> echo hello |
posixsh> echo | | cat
```

**Expected:** `syntax error` for each. Shell survives all three.

---

**Test 12.2 — Redirect with no filename is rejected**

```
posixsh> echo hello >
posixsh> echo survived
```

**Expected:** `syntax error` then `survived`.

---

**Test 12.3 — Pipe and redirect on same command**

```
posixsh> echo hello > /tmp/p3_edge.txt | cat
```

**Expected:** Either the redirect wins (file created, cat gets nothing) or the pipe wins. Either way shell must not crash.

---

**Test 12.4 — Command produces no output**

```
posixsh> true
posixsh> false
posixsh> echo after
```

**Expected:** `after`. `true` and `false` return immediately with no output. Shell waits for them via `wait4` and continues.

---

**Test 12.5 — Long-running foreground command blocks prompt**

```
posixsh> sleep 3
```

**Expected:** Prompt does NOT return for 3 seconds. Shell is blocked in `wait4`. After 3 seconds, prompt reappears. This confirms foreground execution correctly waits.

---

**Test 12.6 — Argument with special characters in quotes**

```
posixsh> echo 'hello | world > file & done'
```

**Expected:** `hello | world > file & done`
All special characters inside single quotes are literal. No operators are parsed. One WORD token.

---

**Test 12.7 — Redirect to same file from two commands**

```
posixsh> echo a > /tmp/p3_race.txt
posixsh> echo b > /tmp/p3_race.txt
posixsh> cat /tmp/p3_race.txt
```

**Expected:** `b` only. Each `>` truncates before writing.

---

### 📋 Summary — What Each Section Confirms

| Section | Phase 3 Component Validated |
| --- | --- |
| 1 | `fork()` → `execve()` → `wait4()` single command lifecycle |
| 2 | `find_executable()` PATH search in `/bin` and `/usr/bin` |
| 3 | Output redirect: `sys_open(O_TRUNC)` + `sys_dup2(fd, 1)` |
| 4 | Input redirect: `sys_open(O_RDONLY)` + `sys_dup2(fd, 0)` |
| 5 | Append redirect: `sys_open(O_APPEND)` vs `O_TRUNC` |
| 6 | Two-stage pipeline: `sys_pipe()`, two forks, dup2 wiring |
| 7 | N-stage pipelines: N-1 pipes, fd hygiene, `yes|head` liveness |
| 8 | Combined pipe + redirect on same pipeline |
| 9 | Background execution: no `wait4()`, job notification |
| 10 | Builtin commands run in shell process (cd affects shell cwd) |
| 11 | strace confirms exact syscall sequences |
| 12 | Edge cases: syntax errors, empty commands, special chars |