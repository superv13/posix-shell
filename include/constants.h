#ifndef CONSTANTS_H
#define CONSTANTS_H

#define MAX_INPUT           1024   /* Max raw input bytes from keyboard */
#define MAX_TOKENS            64   /* Max tokens produced by tokenizer */
#define MAX_ARGS              32   /* Max arguments per single command */
#define MAX_PIPELINE_DEPTH     8   /* Max commands connected by pipes */
#define MAX_TOKEN_LEN        256   /* Max character length of one token */
#define MAX_FILENAME_LEN     256   /* Max character length of redirect filename */
#define MAX_PATH_LEN         256

/*
===============================================================================
Linux open() flags (x86_64 values, from the kernel's uapi/asm-generic/fcntl.h)

Why these are hand-defined:
    This project has zero libc dependency, so the <fcntl.h> values normally
    provided by the standard library do not exist here. The numeric values
    below are part of the stable Linux syscall ABI, not libc internals, so
    hand-defining them is correct and portable across libc-free builds.

Access mode (mutually exclusive, low 2 bits):
    O_RDONLY : Open for reading only.
    O_WRONLY : Open for writing only.
    O_RDWR   : Open for reading and writing.

Creation/status flags (bitwise OR'd with the access mode):
    O_CREAT  : Create the file if it does not exist.
    O_TRUNC  : Truncate an existing file to length 0 (used by ">").
    O_APPEND : Writes always go to the end of the file (used by ">>").
===============================================================================
*/

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040   /* 0100 octal */
#define O_TRUNC     0x0200   /* 01000 octal */
#define O_APPEND    0x0400   /* 02000 octal */

/* Default permission bits for files created via redirection: rw-r--r-- */
#define REDIR_CREATE_MODE 0644

#endif