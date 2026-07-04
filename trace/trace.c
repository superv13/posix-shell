// trace/trace.c — --trace mode diagnostic output

#include "trace.h"
#include "../include/wrappers.h"
#include "../utils/string.h"

/*===========================================================================
 Global state
===========================================================================*/

int g_trace_mode = 0;   /* 0 = off (default), 1 = on (--trace flag) */

/*===========================================================================
 Internal helpers (no libc — write directly to stderr)
===========================================================================*/

static void write_str(const char *s)
{
    sys_write(2, s, my_strlen(s));
}

static void write_long(long n)
{
    char buf[20];
    int  i = 0;

    if (n == 0) { write_str("0"); return; }

    int negative = (n < 0);
    if (negative) n = -n;

    while (n > 0 && i < (int)sizeof(buf) - 1)
    {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    if (negative) buf[i++] = '-';

    /* Reverse into output buffer */
    char out[22];
    int  j = 0;
    while (i > 0) out[j++] = buf[--i];
    out[j] = '\0';
    write_str(out);
}

static void write_int(int n)
{
    write_long((long)n);
}

/*===========================================================================
 Public API
===========================================================================*/

void trace_fork(long child_pid)
{
    if (!g_trace_mode) return;
    write_str("[TRACE] fork() -> ");
    write_long(child_pid);
    write_str("\n");
}

void trace_execve(const char *path)
{
    if (!g_trace_mode) return;
    write_str("[TRACE] execve() path=");
    write_str(path);
    write_str("\n");
}

void trace_sigaction(int signum)
{
    if (!g_trace_mode) return;
    write_str("[TRACE] sigaction(");
    write_int(signum);
    write_str(", ...)\n");
}
