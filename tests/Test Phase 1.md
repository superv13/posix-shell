# Test Phase 1

## Phase 1 Manual Test Commands

Run these in sequence from your project root directory (where `posixsh` binary is).

---

### 🔧 Section 1 — Binary Inspection (run in your normal terminal, not inside posixsh)

---

**Test 1.1 — Check binary exists and is executable**

```bash
ls -lh ./posixsh
```

**Expected:** File exists, size is under 200 KB, permissions show `-rwxr-xr-x`

---

**Test 1.2 — Zero library dependencies**

```bash
ldd ./posixsh
```

**Expected:** `not a dynamic executable`
If you see any `.so` files listed → Phase 1 has failed its core goal.

---

**Test 1.3 — Statically linked confirmation**

```bash
file ./posixsh
```

**Expected:** Output contains `statically linked` and `ELF 64-bit`

---

**Test 1.4 — No dynamic section in ELF**

```bash
readelf -d ./posixsh
```

**Expected:** `There is no dynamic section in this file.`

---

**Test 1.5 — No shared library entries**

```bash
readelf -d ./posixsh | grep NEEDED
```

**Expected:** No output at all (blank). If any `NEEDED` line appears, the binary has a library dependency.

---

**Test 1.6 — Confirm only Linux syscalls are used (no libc calls)**

```bash
nm ./posixsh | grep -i ' U '
```

**Expected:** No output. `U` means undefined (imported from library). A libc-linked binary would show hundreds of undefined symbols like `printf`, `malloc`, etc.

---

**Test 1.7 — Check entry point is our custom _start**

```bash
readelf -h ./posixsh | grep Entry
nm ./posixsh | grep _start
```

**Expected:** Entry point address exists and `_start` appears as a `T` (text section) symbol. There should be NO `__libc_start_main` symbol.

---

### 🖥️ Section 2 — Startup and Prompt (run inside posixsh)

---

**Test 2.1 — Shell starts and shows prompt**

```bash
./posixsh
```

**Expected:**

```
posixsh>
```

Cursor sits after the `>` and a space. Shell is waiting for input.

---

**Test 2.2 — Prompt has trailing space**

Look carefully at the prompt after starting. Count the characters:
`posixsh>` then ONE space then cursor.

**Expected:** `posixsh>`  (9 characters total, space after `>`).
A common bug is `my_strlen(prompt) - 1` cutting the space — the prompt would look like `posixsh>` with no space.

---

### 🚪 Section 3 — Exit Handling

---

**Test 3.1 — exit command terminates shell**

Inside posixsh:

```
posixsh> exit
```

**Expected:** Shell exits immediately. Returns to your normal terminal prompt.

---

**Test 3.2 — Exit code is zero**

```bash
./posixsh
```

Then type:

```
posixsh> exit
```

Then in your normal terminal:

```bash
echo $?
```

**Expected:** `0`

---

**Test 3.3 — Ctrl+D (EOF) terminates shell**

```bash
./posixsh
```

Press `Ctrl+D` without typing anything.
**Expected:** Shell exits cleanly, returns to normal terminal. No crash, no error message.

---

**Test 3.4 — Ctrl+D exit code is also zero**

```bash
./posixsh; echo "exit code: $?"
```

Press `Ctrl+D` immediately.
**Expected:** `exit code: 0`

---

### 🔤 Section 4 — String Comparison Edge Cases (my_strcmp correctness)

These test that `my_strcmp` works correctly — Phase 1 uses it to detect `exit\n`.

---

**Test 4.1 — Uppercase EXIT does not exit**

Inside posixsh:

```
posixsh> EXIT
```

**Expected:** Shell does NOT exit. Shows a new prompt (or command not found in Phase 3+). If it exits → `my_strcmp` is case-insensitive, which is wrong.

---

**Test 4.2 — Leading space prevents exit match**

Inside posixsh:

```
posixsh>  exit
```

(type a space before exit)
**Expected:** Shell does NOT exit. `" exit\n"` is not equal to `"exit\n"`.

---

**Test 4.3 — Trailing space prevents exit match**

Inside posixsh:

```
posixsh> exit
```

(type a space after exit)
**Expected:** Shell does NOT exit. `"exit \n"` is not equal to `"exit\n"`.

---

**Test 4.4 — exit with argument does not exit (Phase 1 behavior)**

Inside posixsh:

```
posixsh> exit 0
```

**Expected:** In Phase 1 only — shell does NOT exit (because `"exit 0\n" != "exit\n"`). Shell shows new prompt. This is expected Phase 1 behavior. Phase 3 changes this.

---

### 🔄 Section 5 — Read-Prompt Loop (sys_read / sys_write loop correctness)

---

**Test 5.1 — Empty Enter shows new prompt**

Inside posixsh, press Enter 5 times without typing anything:

```
posixsh>
posixsh>
posixsh>
posixsh>
posixsh>
```

**Expected:** A fresh `posixsh>`  prompt appears after each Enter. The loop is running correctly.

---

**Test 5.2 — Garbage input shows new prompt**

Inside posixsh:

```
posixsh> asdfghjkl
posixsh> 12345
posixsh> @#$%^&*
```

**Expected:** Each line just shows a new `posixsh>`  prompt. No crash, no error. Shell has not exited.

---

**Test 5.3 — Multiple prompts confirm loop iteration count**

Inside posixsh, type 10 Enter presses then count the prompts:

```
posixsh> (Enter)
posixsh> (Enter)
... (10 times)
posixsh> exit
```

**Expected:** Exactly 11 prompts appear (10 blank + 1 for exit line). Confirms the loop runs exactly once per `sys_read` call.

---

### 📏 Section 6 — Buffer Boundary Tests (my_strlen / sys_read safety)

---

**Test 6.1 — Input near buffer limit does not crash**

Inside posixsh, paste exactly 500 characters of text (any character):

```bash
python3 -c "print('a'*500)" | head -c 500
```

Copy that output, then paste it into posixsh and press Enter.
**Expected:** Shell does not crash. Shows new prompt.

---

**Test 6.2 — Maximum safe input (1023 chars)**

```bash
./posixsh
```

Then paste 1023 `x` characters and press Enter:

```bash
python3 -c "import subprocess,os; p=subprocess.Popen(['./posixsh'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE); out,err=p.communicate(input=b'x'*1023+b'\nexit\n'); print(out.decode())"
```

**Expected:** Shell handles it and exits cleanly.

---

**Test 6.3 — Strace confirms only our syscalls appear**

```bash
strace ./posixsh 2>&1
```

Type `exit` when prompted.
**Expected output contains ONLY these syscall types:**

```
write(1, "posixsh> ", 9)
read(0, "exit\n", 1023)
write(...)   ← any echo writes
exit(0)
```

**Must NOT contain:** `mmap`, `brk`, `openat /etc/ld.so`, `access`, `fstat` — these are all libc startup syscalls. Their absence proves zero libc dependency.

---

**Test 6.4 — Count syscalls at startup**

```bash
strace ./posixsh 2>&1 | head -5
```

Type `exit` immediately.
**Expected:** First syscall should be `write(1, "posixsh> ", 9)` — nothing before it. A libc shell would have 30+ syscalls before the first prompt (library loading, memory setup, locale, etc.).

---

### ⚙️ Section 7 — Process and Signal Basics

---

**Test 7.1 — Shell runs as expected process**

In one terminal, start posixsh:

```bash
./posixsh
```

In another terminal:

```bash
ps aux | grep posixsh
```

**Expected:** One `posixsh` process appears. Note its PID.

---

**Test 7.2 — Shell has correct process group**

```bash
./posixsh &
SPID=$!
ps -o pid,pgid,cmd -p $SPID
kill $SPID
```

**Expected:** PID and PGID should both be the same number (shell is its own process group leader at startup in Phase 4).

---

**Test 7.3 — Shell process terminates cleanly (no zombie)**

```bash
./posixsh &
SPID=$!
sleep 0.5
kill -TERM $SPID
sleep 0.3
ps aux | grep posixsh | grep -v grep
```

**Expected:** No output (shell is gone, no zombie process left behind).

---

### ✅ Section 8 — Final Sanity Check

---

**Test 8.1 — Full sequence without crash**

Run this entire sequence inside one posixsh session:

```
posixsh> EXIT
posixsh>  exit
posixsh> exit
posixsh> (Enter 5 times)
posixsh> asdfgh
posixsh> 123456
posixsh> exit
```

**Expected:** Shell survives all of it and only exits on the final clean `exit`. Every line before it shows a new prompt.

---

**Test 8.2 — Compare prompt syscall with strace**

```bash
strace -e trace=write ./posixsh 2>&1
```

Type `exit`.
**Expected:**

```
write(1, "posixsh> ", 9)    ← exactly 9 bytes, fd 1 = stdout
write(...)
+++ exited with 0 +++
```

The `9` confirms `my_strlen("posixsh> ")` returned 9, not 8 (which would indicate a `-1` bug cutting the space).

---

### 📊 Summary — What Each Test Confirms

| Test | What Phase 1 component it validates |
| --- | --- |
| 1.1 – 1.7 | Binary compilation flags, zero libc, custom `_start` |
| 2.1 – 2.2 | `sys_write()` works, `my_strlen()` correct |
| 3.1 – 3.4 | `sys_exit()` works, exit code correct |
| 4.1 – 4.4 | `my_strcmp()` is case-sensitive, exact-match |
| 5.1 – 5.3 | `sys_read()` loop, read-prompt cycle |
| 6.1 – 6.4 | Buffer size safe, only our syscalls run |
| 7.1 – 7.3 | Process lifecycle correct |
| 8.1 – 8.2 | Combined sanity, `my_strlen` correctness |