#ifndef MISC_BUILTINS_H
#define MISC_BUILTINS_H
#include "../parser/parser.h"

/* POSIX mandatory builtins */
int  builtin_wait(Command *cmd);   /* wait [pid] */
int  builtin_colon(void);          /* : (noop, always 0) */
int  builtin_true(void);           /* true */
int  builtin_false(void);          /* false (always 1) */
int  builtin_export(Command *cmd); /* export NAME=val */
int  builtin_unset(Command *cmd);  /* unset NAME */
int  builtin_read(Command *cmd);   /* read VAR */

/* eval: set via shell_loop.c at startup */
extern int (*g_eval_fn)(char *line);
int builtin_eval(Command *cmd);

#endif
