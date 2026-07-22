# Test Phase 4

### Phase 4 Manual Test Commands

Run from your project root. Start a fresh `./posixsh` for each section unless stated otherwise. Phase 4 involves signals so some tests require you to press key combinations like Ctrl+C and Ctrl+Z at specific moments.

---

#### 🛡️ Section 1 — Shell Signal Immunity (Shell Must Never Die from These)

These confirm `setup_shell_signals()` correctly installed `SIG_IGN` for all protective signals.

---

**Test 1.1 — Ctrl+C at empty prompt does not kill shell**

Inside posixsh, press `Ctrl+C` at the prompt with nothing running:

```
posixsh> ^Cposixsh>
```

**Expected:** A new `posixsh>` prompt appears. Shell is alive. Without Phase 4, Ctrl+C would kill the shell process entirely.

---

**Test 1.2 — Multiple Ctrl+C at prompt**

Press Ctrl+C 5 times rapidly at the prompt:

```
posixsh> ^C^C^C^C^Cposixsh>
```

**Expected:** Shell survives all five. Prompt reappears after the last one.

---

**Test 1.3 — Ctrl+\ (SIGQUIT) at prompt does not kill shell**

Press `Ctrl+\` at the prompt:

```
posixsh> ^\posixsh>
```

**Expected:** Shell survives. No core dump. SIGQUIT is SIG_IGN for the shell.

---

**Test 1.4 — Ctrl+Z at prompt does not stop shell**

Press `Ctrl+Z` at the prompt with nothing running:

```
posixsh> ^Zposixsh>
```

**Expected:** Shell is NOT suspended. New prompt appears immediately. SIGTSTP is SIG_IGN for the shell.

---

**Test 1.5 — Shell survives all three in sequence**

```
posixsh> ^Cposixsh> ^Zposixsh> ^\posixsh> echo still_alive
```

**Expected:** `still_alive` — shell survived all three signals.

---

#### ⚡ Section 2 — SIGINT (Ctrl+C) Kills Foreground Job Only

---

**Test 2.1 — Ctrl+C kills foreground sleep, not shell**

```
posixsh> sleep 30
```

(sleep is now running in foreground)

Press `Ctrl+C`:

```
posixsh>
```

**Expected:** sleep is terminated immediately. Prompt returns. Shell is alive. Without process groups, Ctrl+C would kill the shell AND sleep together.

---

**Test 2.2 — Shell is functional after Ctrl+C**

```
posixsh> sleep 30
```

Press `Ctrl+C`, then:

```
posixsh> echo survived
```

**Expected:** `survived`. Shell is fully responsive after the kill.

---

**Test 2.3 — Ctrl+C on multi-stage pipeline kills all stages**

```
posixsh> yes | cat | cat
```

Press `Ctrl+C`:

**Expected:** All three processes terminate. Prompt returns immediately. No zombie processes left behind.

Verify no zombies:

bash

```bash
ps aux|grep defunct
```

**Expected:** No `<defunct>` entries for posixsh children.

---

**Test 2.4 — Multiple foreground jobs killed one at a time**

```
posixsh> sleep 30
```

Press Ctrl+C.

```
posixsh> sleep 30
```

Press Ctrl+C.

```
posixsh> sleep 30
```

Press Ctrl+C.

```
posixsh> echo all_killed
```

**Expected:** `all_killed`. Three separate kill cycles all worked correctly.

---

**Test 2.5 — Ctrl+C does not produce extra output**

```
posixsh> sleep 5
```

Press Ctrl+C.

**Expected:** Prompt reappears cleanly. No stray error messages from the shell itself (the killed child may or may not print anything depending on the terminal).

---

**Test 2.6 — Strace confirms SIGINT goes to child's process group, not shell**

In normal terminal:

bash

```bash
strace -etrace=setpgid,kill ./posixsh
```

Type `sleep 30` then press Ctrl+C, then `exit`.

**Expected:** `setpgid(<child_pid>, <child_pid>)` appears after fork. The SIGINT from Ctrl+C goes to the child's group. Shell's own setpgid confirms it is in a separate group.

---

#### ⏸️ Section 3 — SIGTSTP (Ctrl+Z) Suspends Foreground Job

---

**Test 3.1 — Ctrl+Z suspends foreground job, returns prompt**

```
posixsh> sleep 30
```

Press `Ctrl+Z`:

```
[1]+  Stopped    sleep 30posixsh>
```

**Expected:** Job is suspended (not killed). Prompt returns immediately. Job notification line appears showing `Stopped`.

---

**Test 3.2 — Suspended job still exists (not killed)**

```
posixsh> sleep 30
```

Press Ctrl+Z. Then in another terminal:

bash

```bash
ps aux|grepsleep
```

**Expected:** `sleep 30` process is visible with status `T` (stopped/traced). It was not killed — only suspended.

---

**Test 3.3 — Shell is functional after Ctrl+Z**

```
posixsh> sleep 30
```

Press Ctrl+Z, then:

```
posixsh> echo shell_works
```

**Expected:** `shell_works`. Shell is fully responsive while the job sits stopped in the background.

---

**Test 3.4 — Ctrl+Z on a pipeline stops all stages**

```
posixsh> yes | cat
```

Press Ctrl+Z.

**Expected:** Both `yes` and `cat` are stopped. Prompt returns. Job table shows the pipeline as one stopped job.

```
posixsh> jobs
```

**Expected:** `[1]+  Stopped    yes | cat` (or similar, showing the full command).

---

**Test 3.5 — Ctrl+Z produces a job number**

```
posixsh> sleep 30
```

Press Ctrl+Z. Look at the output:

**Expected:** Line starts with `[1]+` — job number 1 is assigned. If you stop another job it gets `[2]+` etc.

---

**Test 3.6 — Multiple Ctrl+Z stops produce sequential job numbers**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> jobs
```

**Expected:**

```
[1]-  Stopped    sleep 30[2]+  Stopped    sleep 30
```

Two entries with sequential job numbers.

---

#### 📋 Section 4 — `jobs` Command

---

**Test 4.1 — Empty job table produces no output**

```
posixsh> jobsposixsh>
```

**Expected:** Nothing printed between the two prompts. Job table is empty.

---

**Test 4.2 — Background job appears in jobs**

```
posixsh> sleep 30 &posixsh> jobs
```

**Expected:**

```
[1]+  Running    sleep 30
```

---

**Test 4.3 — Stopped job appears in jobs**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> jobs
```

**Expected:**

```
[1]+  Stopped    sleep 30
```

---

**Test 4.4 — Both running and stopped jobs appear**

```
posixsh> sleep 30 &posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> jobs
```

**Expected:** Two entries — one Running (`[1]`), one Stopped (`[2]`).

---

**Test 4.5 — jobs shows the full command string**

```
posixsh> sleep 30 &posixsh> jobs
```

**Expected:** The command string `sleep 30` appears in the output — not just the command name.

---

**Test 4.6 — jobs shows pipeline command string**

```
posixsh> ls /usr/bin | grep a | sort &posixsh> jobs
```

**Expected:** The jobs output shows the full pipeline string, something like:

```
[1]+  Running    ls /usr/bin | grep a | sort
```

---

**Test 4.7 — Completed background job shows Done**

```
posixsh> sleep 0.3 &
```

Wait 1 second, then:

```
posixsh> jobs
```

**Expected:** `[1]+  Done       sleep 0.3` — job completed and is reported.

---

**Test 4.8 — Done job is removed after being reported**

```
posixsh> sleep 0.3 &
```

Wait 1 second, then:

```
posixsh> jobsposixsh> jobs
```

**Expected:** First `jobs` shows `Done`. Second `jobs` shows nothing (job freed from table).

---

#### ▶️ Section 5 — `fg` Command

---

**Test 5.1 — fg resumes stopped job in foreground**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg
```

**Expected:** `sleep 30` resumes running. Prompt does NOT return (shell is waiting for it). Press Ctrl+C to kill it.

---

**Test 5.2 — fg by job number**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg %1
```

**Expected:** Same as 5.1 — sleep resumes. Press Ctrl+C.

---

**Test 5.3 — fg prints the command name before resuming**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg
```

**Expected:** `sleep 30` is printed to terminal (showing which job was resumed) then it runs.

---

**Test 5.4 — fg removes job from table after it exits**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg
```

Press Ctrl+C to kill it.

```
posixsh> jobs
```

**Expected:** Nothing — job table is empty. The job was removed when it was killed via fg.

---

**Test 5.5 — fg transfers terminal control correctly**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg
```

Now press Ctrl+C:

**Expected:** sleep dies (not the shell). This proves `tcsetpgrp` correctly gave the terminal to sleep's process group during fg. If terminal control was NOT transferred, Ctrl+C would go to the shell group and have no effect on sleep.

---

**Test 5.6 — fg %1 with two stopped jobs picks job 1**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> sleep 40
```

Press Ctrl+Z.

```
posixsh> fg %1
```

**Expected:** `sleep 30` resumes (the first one). Press Ctrl+C to kill it.

```
posixsh> jobs
```

**Expected:** Only `sleep 40` remains as Stopped.

---

**Test 5.7 — fg with no argument picks most recent stopped job**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> sleep 40
```

Press Ctrl+Z.

```
posixsh> fg
```

**Expected:** `sleep 40` resumes (the most recently stopped — highest job number). Press Ctrl+C.

---

**Test 5.8 — fg on nonexistent job reports error**

```
posixsh> fg %99posixsh> echo survived
```

**Expected:** `posixsh: fg: no such job` then `survived`. No crash.

---

**Test 5.9 — fg then Ctrl+Z re-stops the job**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> fg
```

Press Ctrl+Z again while sleep is running in foreground:

```
posixsh> jobs
```

**Expected:** `sleep 30` appears again as Stopped with a job number. The job was re-added to the table after being stopped again via fg.

---

**Test 5.10 — Shell reclaims terminal after fg job exits**

```
posixsh> sleep 0.3
```

Press Ctrl+Z.

```
posixsh> fg
```

Wait for sleep to finish naturally (0.3 seconds).

```
posixsh> echo terminal_ok
```

**Expected:** `terminal_ok` — shell successfully reclaimed the terminal after fg job exited naturally

---

#### ⏩ Section 6 — `bg` Command

---

**Test 6.1 — bg resumes stopped job in background**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> bg
```

**Expected:** Prompt returns immediately. Shell does NOT wait for sleep. Sleep is now running in background.

---

**Test 6.2 — bg job appears as Running in jobs**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> bgposixsh> jobs
```

**Expected:**

```
[1]+  Running    sleep 30
```

State changed from Stopped to Running.

---

**Test 6.3 — bg by job number**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> bg %1posixsh> jobs
```

**Expected:** `[1]+  Running    sleep 30`

---

**Test 6.4 — bg with two stopped jobs picks most recent**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> sleep 40
```

Press Ctrl+Z.

```
posixsh> bgposixsh> jobs
```

**Expected:** `sleep 40` (job 2, most recent) is now Running. `sleep 30` (job 1) remains Stopped.

---

**Test 6.5 — bg on nonexistent job reports error**

```
posixsh> bg %99posixsh> echo survived
```

**Expected:** `posixsh: bg: no such job` then `survived`. No crash.

---

**Test 6.6 — bg on already running job reports error**

```
posixsh> sleep 30 &posixsh> bg %1
```

**Expected:** `posixsh: bg: job is already running`

---

**Test 6.7 — Shell is fully usable after bg**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> bgposixsh> echo usableposixsh> pwdposixsh> echo still_usable
```

**Expected:** All three commands execute normally while sleep runs in background.

---

#### 🔔 Section 7 — Background Job Completion Notification

---

**Test 7.1 — Done notification appears before next prompt**

```
posixsh> sleep 0.3 &
```

Wait 1 second, then press Enter:

**Expected:** The Done notification appears BEFORE the next prompt:

```
[1]+  Done       sleep 0.3posixsh>
```

---

**Test 7.2 — Done notification appears when next command is run**

```
posixsh> sleep 0.3 &posixsh> sleep 1
```

(sleep 1 keeps the shell busy for 1 second, during which sleep 0.3 finishes)

After sleep 1 exits:

**Expected:** Done notification for `sleep 0.3` appears before the next prompt.

---

**Test 7.3 — Multiple background jobs report Done in order**

```
posixsh> sleep 0.2 &posixsh> sleep 0.4 &posixsh> sleep 0.6 &
```

Wait 1 second, then press Enter:

**Expected:** All three Done notifications appear (possibly in one batch before the next prompt).

---

**Test 7.4 — Completed job is removed from jobs table**

```
posixsh> sleep 0.2 &
```

Wait 1 second.

```
posixsh> jobs
```

**Expected:** Nothing shown — job is Done and freed. Or the Done notification appeared automatically already.

---

#### 🔀 Section 8 — Combined Job Control Workflows

---

**Test 8.1 — Full stop/bg/fg cycle**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> jobsposixsh> bgposixsh> jobsposixsh> fg
```

Press Ctrl+C.

```
posixsh> jobs
```

**Expected sequence:**

- After Ctrl+Z: `[1]+ Stopped sleep 30`
- After bg: `[1]+ Running sleep 30`
- After fg + Ctrl+C: jobs is empty

---

**Test 8.2 — Stop two jobs, resume each with fg by number**

```
posixsh> sleep 30
```

Press Ctrl+Z.

```
posixsh> sleep 40
```

Press Ctrl+Z.

```
posixsh> fg %1
```

Press Ctrl+C.

```
posixsh> fg %2
```

Press Ctrl+C.

```
posixsh> jobs
```

**Expected:** Both killed. `jobs` shows nothing.

---

**Test 8.3 — Background job + foreground job simultaneously**

```
posixsh> sleep 30 &posixsh> sleep 5
```

While sleep 5 is running, in another terminal:

bash

```bash
ps aux|grepsleep
```

**Expected:** Both sleep processes visible — one background (sleep 30), one foreground (sleep 5 — part of shell's foreground process group).

---

**Test 8.4 — Kill background job with kill command**

```
posixsh> sleep 30 &posixsh> jobs
```

Note the PID from the `[1] <PID>` notification. Then:

```
posixsh> kill <PID>posixsh>
```

Wait a moment, press Enter:

**Expected:** Done notification appears for the killed job.

---

**Test 8.5 — Maximum jobs in table**

Start 16 background jobs (MAX_JOBS = 16):

```
posixsh> sleep 30 &posixsh> sleep 30 &posixsh> sleep 30 &... (repeat 16 times total)posixsh> jobs
```

**Expected:** All 16 jobs listed with numbers [1] through [16].

Then start one more:

```
posixsh> sleep 30 &
```

**Expected:** Either error `job table full` or job replaces an older slot. Shell does NOT crash.

---

#### 🔍 Section 9 — `-trace` Mode

---

**Test 9.1 — Trace mode activates**

bash

```bash
./posixsh --trace
```

**Expected:** Shell starts normally, shows `posixsh>` prompt.

---

**Test 9.2 — Trace shows fork and execve for single command**

bash

```bash
./posixsh --trace
```

```
posixsh> echo hello
```

**Expected output includes:**

```
[TRACE] fork() -> <pid>[TRACE] execve() path=/bin/echohello
```

The actual `hello` also appears (command still executes normally).

---

**Test 9.3 — Trace shows two forks for a pipeline**

bash

```bash
./posixsh --trace
```

```
posixsh> echo hi | cat
```

**Expected:** TWO `[TRACE] fork()` lines appear — one for each pipeline stage. One or two `[TRACE] execve()` lines.

---

**Test 9.4 — Trace shows three forks for three-stage pipeline**

bash

```bash
./posixsh --trace
```

```
posixsh> ls | grep a | wc -l
```

**Expected:** THREE `[TRACE] fork()` lines. Three `[TRACE] execve()` lines. Actual output of the command also appears.

---

**Test 9.5 — Trace output goes to stderr, command output to stdout**

bash

```bash
./posixsh --trace2>/tmp/trace_output.txt
```

```
posixsh> echo helloposixsh> exit
```

**Expected:**

- Terminal shows: `hello` (stdout only)
- `/tmp/trace_output.txt` contains: `[TRACE] fork()...` and `[TRACE] execve()...` (stderr only)

Verify:

bash

```bash
cat /tmp/trace_output.txt
```

**Expected:** Trace lines are there. Confirms trace goes to fd 2 (stderr) not fd 1 (stdout).

---

**Test 9.6 — Trace mode does not affect command output**

bash

```bash
./posixsh --trace2>/dev/null
```

```
posixsh> echo helloposixsh> ls /tmp | wc -l
```

**Expected:** `hello` and the line count appear normally. Trace lines go to /dev/null. Commands behave identically to non-trace mode.

---

**Test 9.7 — Without --trace flag, no trace output**

bash

```bash
./posixsh2>/tmp/no_trace.txt
```

```
posixsh> echo helloposixsh> exit
```

bash

```bash
cat /tmp/no_trace.txt
```

**Expected:** Empty file. No `[TRACE]` lines when flag is absent.

---

**Test 9.8 — Trace shows process group setup**

bash

```bash
./posixsh --trace
```

```
posixsh> sleep 5
```

Press Ctrl+C.

**Expected:** Trace output contains `setpgid` or process group information showing the child was placed in its own group.

---

#### 🔬 Section 10 — Process Group Verification

---

**Test 10.1 — Foreground job is in its own process group**

In one terminal:

bash

```bash
./posixsh
```

```
posixsh> sleep 30
```

In another terminal while sleep is running:

bash

```bash
ps -o pid,pgid,ppid,cmd|grep -E"posixsh|sleep"
```

**Expected:**

```
PID    PGID   PPID  CMD1234   1234   nnnn  ./posixsh      ← shell is its own group leader5678   5678   1234  sleep 30       ← sleep is in a DIFFERENT group (its own PID = PGID)
```

Critical: `sleep` PGID must differ from posixsh PGID. If they are the same, Ctrl+C would kill both.

---

**Test 10.2 — Pipeline all stages share one process group**

In one terminal:

bash

```bash
./posixsh
```

```
posixsh> yes | cat | cat
```

In another terminal:

bash

```bash
ps -o pid,pgid,cmd|grep -E"yes|cat"
```

**Expected:** All three processes (`yes`, `cat`, `cat`) have the SAME PGID (= PID of the first child `yes`). This is how Ctrl+C kills all three together.

---

**Test 10.3 — Background job is in its own process group**

```
posixsh> sleep 30 &
```

In another terminal:

bash

```bash
ps -o pid,pgid,cmd|grep -E"posixsh|sleep"
```

**Expected:** `sleep 30` has a different PGID from posixsh. Background job is isolated.

---

**Test 10.4 — Terminal control transfer verified**

In one terminal:

bash

```bash
./posixsh
```

```
posixsh> sleep 30
```

In another terminal while sleep is running:

bash

```bash
cat /proc/$(pgrep posixsh)/fdinfo/0|grep flags
```

Or more simply:

bash

```bash
# Check which process group owns the terminalps -o pid,pgid,tpgid,cmd|grep -E"posixsh|sleep"
```

**Expected:** The `tpgid` (terminal process group) column shows sleep's PGID during foreground execution — not posixsh's PGID. This confirms `tcsetpgrp` worked.

---

**Test 10.5 — Shell reclaims terminal after foreground job**

```
posixsh> sleep 0.5
```

After it exits naturally:

bash

```bash
ps -o pid,pgid,tpgid,cmd|grep posixsh
```

**Expected:** `tpgid` matches posixsh's own PGID — shell reclaimed the terminal.

---

#### 🔄 Section 11 — SIGCHLD and Reaping

---

**Test 11.1 — No zombie processes after background job completes**

```
posixsh> sleep 0.3 &
```

Wait 2 seconds, then in another terminal:

bash

```bash
ps aux|grep defunct
```

**Expected:** No `<defunct>` entries. SIGCHLD handler fired, `reap_background_jobs()` called `wait4`, zombie collected.

---

**Test 11.2 — No zombie after foreground job killed by Ctrl+C**

```
posixsh> sleep 30
```

Press Ctrl+C. Then in another terminal:

bash

```bash
ps aux|grep defunct
```

**Expected:** No zombie. `wait4` in the foreground wait loop collected the killed child.

---

**Test 11.3 — Many background jobs, none become zombies**

```
posixsh> sleep 0.1 &posixsh> sleep 0.1 &posixsh> sleep 0.1 &posixsh> sleep 0.1 &posixsh> sleep 0.1 &
```

Wait 2 seconds:

bash

```bash
ps aux|grep defunct
```

**Expected:** No zombies. SIGCHLD handler and reaping loop handled all five.

---

#### 🔩 Section 12 — Strace Verification

Run from your normal terminal.

---

**Test 12.1 — Shell sets up its own process group at startup**

bash

```bash
strace -etrace=setpgid,getpid ./posixsh
```

Type `exit`.

**Expected:** `getpid()` call appears at startup to record `g_shell_pgid`. `setpgid(0, 0)` appears to put shell in its own group.

---

**Test 12.2 — sigaction installs handlers for all six signals**

bash

```bash
strace -etrace=rt_sigaction ./posixsh
```

Type `exit`.

**Expected:** Six `rt_sigaction` calls — one each for SIGINT(2), SIGQUIT(3), SIGTSTP(20), SIGTTIN(21), SIGTTOU(22), SIGCHLD(17). All installing either `SIG_IGN` or the SIGCHLD handler.

---

**Test 12.3 — Child resets signals before exec**

bash

```bash
strace -f -etrace=rt_sigaction,execve ./posixsh
```

Type `echo hello` then `exit`.

**Expected:** After `fork()`, in the child process (different PID in strace output), `rt_sigaction` calls appear BEFORE `execve`. These are `reset_child_signals()` restoring `SIG_DFL` for SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD.

---

**Test 12.4 — tcsetpgrp calls around foreground job**

bash

```bash
strace -etrace=ioctl ./posixsh
```

Type `sleep 0.5` then `exit`.

**Expected:**

```
ioctl(0, TIOCSPGRP, [<child_pgid>])   ← give terminal to child...ioctl(0, TIOCSPGRP, [<shell_pgid>])   ← reclaim terminal after job exits
```

Two ioctl calls with `TIOCSPGRP` bookending the foreground job.

---

**Test 12.5 — Background job has NO tcsetpgrp**

bash

```bash
strace -etrace=ioctl ./posixsh
```

Type `sleep 5 &` then `exit`.

**Expected:** NO `TIOCSPGRP` ioctl appears after the `&` command. Background jobs never get terminal control.

---

**Test 12.6 — fg produces tcsetpgrp for job then for shell**

bash

```bash
strace -etrace=ioctl,kill ./posixsh
```

Type `sleep 30`, press Ctrl+Z, type `fg`, press Ctrl+C, type `exit`.

**Expected:**

```
ioctl(0, TIOCSPGRP, [...])   ← foreground sleep originallyioctl(0, TIOCSPGRP, [...])   ← shell reclaims after Ctrl+Zkill(-<pgid>, SIGCONT)        ← fg sends SIGCONTioctl(0, TIOCSPGRP, [...])   ← fg gives terminal to jobioctl(0, TIOCSPGRP, [...])   ← shell reclaims after Ctrl+C
```

---

#### 📊 Summary — What Each Section Confirms

| Section | Phase 4 Component Validated |
| --- | --- |
| 1 | `setup_shell_signals()` — SIG_IGN for SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU |
| 2 | Process groups — SIGINT kills foreground group only, shell immune |
| 3 | SIGTSTP — `wait4(WUNTRACED)` detects stop, job added to table |
| 4 | Job table — `add_job`, `find_job_*`, `print_job_line`, `JobState` |
| 5 | `fg` — `SIGCONT`, `tcsetpgrp` to job, `wait4(WUNTRACED)`, terminal return |
| 6 | `bg` — `SIGCONT` only, no terminal handoff, state → Running |
| 7 | SIGCHLD handler — `g_sigchld_flag`, `reap_background_jobs`, Done notification |
| 8 | Combined workflows — full lifecycle of job control |
| 9 | `--trace` mode — fork/execve visibility, stderr separation |
| 10 | Process group isolation — verified via `/proc` and `ps` |
| 11 | Zombie prevention — `wait4` in SIGCHLD handler, no `<defunct>` |
| 12 | strace confirms: `rt_sigaction`, `setpgid`, `ioctl(TIOCSPGRP)`, `kill(-pgid)` |