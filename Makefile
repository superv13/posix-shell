#==============================================================================
# Makefile
#
# Purpose:
#   Builds the educational POSIX shell without any dependency on libc.
#
# Why this file exists:
#   A normal GCC build automatically links:
#
#       crt0.o
#       libc
#       startup objects
#
#   This project deliberately disables them to expose the complete
#   execution path between the Linux kernel and the shell.
#
# Build flow:
#
#   start.c
#        ↓
#   shell_loop.c
#        ↓
#   wrappers.c
#        ↓
#   parser/tokenizer.c
#        ↓
#   parser/parser.c
#        ↓
#   linker
#        ↓
#   posixsh
#
#==============================================================================

CC = gcc

#------------------------------------------------------------------------------
# Compiler flags
#
# -nostdlib
#   Do not link the standard C runtime or libc.
#
# -static
#   Produce a self-contained executable with no shared library dependencies.
#
# -ffreestanding
#   Inform the compiler that this program does not execute inside a hosted
#   environment and cannot assume the existence of libc facilities.
#
# -Wall
#   Enable common compiler warnings.
#------------------------------------------------------------------------------

CFLAGS = \
-nostdlib \
-static \
-ffreestanding \
-Wall

#------------------------------------------------------------------------------
# Source files
#------------------------------------------------------------------------------

SRC = \
runtime/start.c \
shell/shell_loop.c \
kernel/wrappers.c \
utils/string.c \
builtins/builtins.c \
parser/tokenizer.c \
parser/parser.c

#------------------------------------------------------------------------------
# Build target
#------------------------------------------------------------------------------

all:
	$(CC) $(CFLAGS) $(SRC) -o posixsh