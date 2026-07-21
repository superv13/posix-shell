#==============================================================================
# Makefile — Educational POSIX Shell (Phase 5)
#
# Build flags:
#   -nostdlib       do not link libc or standard startup files
#   -static         produce a self-contained binary
#   -ffreestanding  no hosted-environment assumptions
#   -Wall           enable common warnings
#   -g              include debug symbols (for strace / gdb analysis)
#==============================================================================

CC     = gcc
CFLAGS = -nostdlib -static -ffreestanding -Wall -g

SRC = \
    runtime/start.c         \
    shell/shell_loop.c      \
    kernel/wrappers.c       \
    utils/string.c          \
    env/env.c               \
    parser/tokenizer.c      \
    parser/parser.c         \
    executor/executor.c     \
    executor/path.c         \
    builtins/builtins.c     \
    builtins/echo.c         \
    builtins/cd.c           \
    builtins/pwd.c          \
    builtins/exit.c         \
    builtins/jobs_builtin.c \
    signals/signals.c       \
    jobs/jobs.c             \
    trace/trace.c

# Build both debug (with -g) and stripped release binaries in one step.
# perf_measure.sh and strace_compare.sh default to posixsh_release so
# benchmarks always measure the optimised binary, not the debug build.
all: posixsh posixsh_release nolibc_exit

posixsh: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o posixsh

posixsh_release: $(SRC)
	$(CC) -nostdlib -static -ffreestanding -Wall $(SRC) -o posixsh_release
	strip posixsh_release

# ── Rung 5: absolute syscall floor ──────────────────────────────────────────
# nolibc_exit has _start in inline asm that calls exit_group directly.
# -nostdlib : no crt0/libc  -static : no ld.so  -O0 : no asm reordering
nolibc_exit: nolibc_exit.c
	$(CC) -nostdlib -static -O0 -o nolibc_exit nolibc_exit.c

clean:
	rm -f posixsh posixsh_release nolibc_exit

.PHONY: all clean release

# Explicit alias kept for backwards compatibility: "make release" still works.
release: posixsh_release