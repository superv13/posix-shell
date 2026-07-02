#==============================================================================
# Makefile — Educational POSIX Shell (Phase 4)
#
# Build flags:
#   -nostdlib       do not link libc or standard startup files
#   -static         produce a self-contained binary
#   -ffreestanding  no hosted-environment assumptions
#   -Wall           enable common warnings
#==============================================================================

CC     = gcc
CFLAGS = -nostdlib -static -ffreestanding -Wall -g

SRC = \
    runtime/start.c         \
    shell/shell_loop.c      \
    kernel/wrappers.c       \
    utils/string.c          \
    parser/tokenizer.c      \
    parser/parser.c         \
    executor/executor.c     \
    executor/path.c         \
    builtins/builtins.c     \
    builtins/cd.c           \
    builtins/pwd.c          \
    builtins/exit.c         \
    builtins/jobs_builtin.c \
    signals/signals.c       \
    jobs/jobs.c             \
    trace/trace.c

all:
	$(CC) $(CFLAGS) $(SRC) -o posixsh

clean:
	rm -f posixsh

.PHONY: all clean
