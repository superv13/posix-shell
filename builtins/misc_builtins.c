// builtins/misc_builtins.c — POSIX mandatory builtins: wait, :, true, false,
//                            export, unset, read, eval

#include "misc_builtins.h"
#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../env/env.h"

/* ── eval function pointer ────────────────────────────────────────────── */
/* Set by shell_loop.c during initialisation so eval can feed text back   */
/* through the full tokenise → parse → execute pipeline without creating  */
/* a circular dependency between shell_loop.c and builtins.               */
int (*g_eval_fn)(char *line) = 0;

/* ── Helper: write a C string to fd ──────────────────────────────────── */
static void wb(int fd, const char *s)
{
    sys_write(fd, s, my_strlen(s));
}

/*=========================================================================
  builtin_wait — wait [pid]

  No argument: wait for all background children (waitpid(-1,...)).
  With argument: wait for the specified PID.

  POSIX: exit status is the status of the waited-for process, or 0 if
  there are no children to wait for.
=========================================================================*/
int builtin_wait(Command *cmd)
{
    int status = 0;

    if (cmd->argc <= 1)
    {
        /* Wait for all children */
        long pid;
        while ((pid = sys_wait4(-1, &status, 0)) > 0)
            ; /* keep waiting */
        g_last_status = 0;
    }
    else
    {
        /* Wait for specific PID */
        long pid = 0;
        const char *s = cmd->argv[1];
        for (int i = 0; s[i] >= '0' && s[i] <= '9'; i++)
            pid = pid * 10 + (s[i] - '0');

        if (pid <= 0)
        {
            wb(2, "posixsh: wait: invalid pid\n");
            g_last_status = 1;
            return 1;
        }

        long ret = sys_wait4(pid, &status, 0);
        if (ret < 0)
        {
            wb(2, "posixsh: wait: no child with that pid\n");
            g_last_status = 127;
        }
        else
        {
            /* Convert raw wait status to POSIX $? */
            if ((status & 0x7f) == 0)          /* WIFEXITED */
                g_last_status = (status >> 8) & 0xff;
            else if ((status & 0x7f) != 0x7f)  /* WIFSIGNALED */
                g_last_status = 128 + (status & 0x7f);
            else
                g_last_status = 0;
        }
    }

    return 1;
}

/*=========================================================================
  builtin_colon — : (noop)
  Always succeeds; used in POSIX as a guaranteed-true command.
=========================================================================*/
int builtin_colon(void)
{
    g_last_status = 0;
    return 1;
}

/*=========================================================================
  builtin_true / builtin_false
=========================================================================*/
int builtin_true(void)  { g_last_status = 0; return 1; }
int builtin_false(void) { g_last_status = 1; return 1; }

/*=========================================================================
  builtin_export — export [NAME[=VALUE] ...]

  Parses each argument:
    NAME=value   → calls env_set(NAME, value)
    NAME         → marks NAME for export (no-op if already set; set "" if not)

  In this shell, all variables go into the overlay so child processes
  inherit them via g_environ (future: build merged envp for execve).
=========================================================================*/
int builtin_export(Command *cmd)
{
    if (cmd->argc <= 1)
    {
        /* "export" with no args: POSIX says print all exported variables.
         * We just succeed silently for now. */
        g_last_status = 0;
        return 1;
    }

    for (int i = 1; i < cmd->argc; i++)
    {
        const char *arg = cmd->argv[i];

        /* Find '=' in the argument */
        int eq = -1;
        for (int k = 0; arg[k] != '\0'; k++)
        {
            if (arg[k] == '=') { eq = k; break; }
        }

        if (eq >= 0)
        {
            /* NAME=value */
            char name[64];
            int  nlen = (eq < 63) ? eq : 63;
            for (int k = 0; k < nlen; k++) name[k] = arg[k];
            name[nlen] = '\0';

            const char *value = arg + eq + 1;
            env_set(name, value);
        }
        else
        {
            /* NAME only: export existing value or set empty */
            const char *existing = env_get(arg);
            env_set(arg, existing ? existing : "");
        }
    }

    g_last_status = 0;
    return 1;
}

/*=========================================================================
  builtin_unset — unset NAME [NAME ...]
=========================================================================*/
int builtin_unset(Command *cmd)
{
    for (int i = 1; i < cmd->argc; i++)
        env_unset(cmd->argv[i]);

    g_last_status = 0;
    return 1;
}

/*=========================================================================
  builtin_read — read VAR

  Reads one line from stdin, trims the trailing newline, and stores the
  result in VAR via env_set().  Returns 0 on success, 1 on EOF.
=========================================================================*/
int builtin_read(Command *cmd)
{
    char buf[1024];
    int  len = 0;
    char ch;

    /* Read one character at a time until newline or EOF */
    while (len < (int)(sizeof(buf) - 1))
    {
        long n = sys_read(0, &ch, 1);
        if (n <= 0) break;          /* EOF or error */
        if (ch == '\n') break;
        buf[len++] = ch;
    }
    buf[len] = '\0';

    if (cmd->argc >= 2)
        env_set(cmd->argv[1], buf);

    /* Return 1 (failure) on EOF with no data, 0 otherwise */
    g_last_status = (len == 0) ? 1 : 0;
    return 1;
}

/*=========================================================================
  builtin_eval — eval [arg ...]

  Joins all arguments into one string with spaces and feeds it back
  through the shell's execute_line() via the g_eval_fn pointer.
=========================================================================*/
int builtin_eval(Command *cmd)
{
    if (cmd->argc <= 1)
    {
        g_last_status = 0;
        return 1;
    }

    if (g_eval_fn == 0)
    {
        wb(2, "posixsh: eval: not available\n");
        g_last_status = 1;
        return 1;
    }

    /* Join args into one line */
    char line[MAX_INPUT];
    int  pos = 0;

    for (int i = 1; i < cmd->argc && pos < MAX_INPUT - 2; i++)
    {
        const char *a = cmd->argv[i];
        for (int k = 0; a[k] != '\0' && pos < MAX_INPUT - 2; k++)
            line[pos++] = a[k];
        if (i + 1 < cmd->argc && pos < MAX_INPUT - 2)
            line[pos++] = ' ';
    }
    line[pos] = '\0';

    g_eval_fn(line);
    return 1;
}
