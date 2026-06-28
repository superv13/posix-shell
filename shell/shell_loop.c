// shell/shell_loop.c
#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../builtins/builtins.h"
#include "../parser/tokenizer.h"
#include "../parser/parser.h"
#include "../executor/executor.h"
/*
===============================================================================
shell_loop.c

Purpose:
    Implements the main read-eval loop of the educational POSIX shell.

Execution flow:

    Linux kernel
          ↓
       _start()          [runtime/start.c]
          ↓
      shell_main()       [this file]
          ↓  (loop)
      sys_read()         → raw input from kernel
          ↓
      tokenize()         → flat Token array   [parser/tokenizer.c]
          ↓
      parse()            → Pipeline struct    [parser/parser.c]
          ↓
      execute()          → Phase 3 (not yet implemented)

Current Phase 3 behaviour:
    - Reads input
    - Tokenises it
    - Parses it into a Pipeline struct
    - Dispatches single-command builtins (cd/pwd/exit) directly
    - Hands everything else to execute_pipeline(): pipes, redirection,
      background jobs, and external commands via fork()/execve()/wait4()

===============================================================================
*/

void shell_main(void)
{
    char buffer[MAX_INPUT];

    while (1) {

        /* ── Prompt ────────────────────────────────────────────────────── */
        /*
         * SYSCALL: write(1, prompt, len)
         * fd 1 = stdout. We call sys_write() directly — no printf, no libc.
         */
        char prompt[] = "posixsh> ";
        sys_write(1, prompt, my_strlen(prompt));   /* Fixed: was -1, dropped space */

        /* ── Read input ────────────────────────────────────────────────── */
        /*
         * SYSCALL: read(0, buffer, count)
         * fd 0 = stdin. Blocks until the user presses Enter.
         * Returns number of bytes read, 0 on EOF (Ctrl+D), <0 on error.
         */
        long bytes = sys_read(0, buffer, sizeof(buffer) - 1);

        if (bytes <= 0) break;          /* EOF (Ctrl+D) or read error → exit */

        buffer[bytes] = '\0';           /* Null-terminate the raw input */


        /* ── Tokenise ──────────────────────────────────────────────────── */
        Token tokens[MAX_TOKENS];
        int   token_count = 0;

        tokenize(buffer, tokens, &token_count);

        /* ── Parse ─────────────────────────────────────────────────────── */
        Pipeline pipeline;

        if (parse(tokens, token_count, &pipeline) == -1) {
            /*
             * SYSCALL: write(2, ...)
             * fd 2 = stderr. Error messages go to stderr, not stdout.
             */
            char err[] = "posixsh: syntax error\n";
            sys_write(2, err, my_strlen(err));
            continue;
        }

        /*
        * Execute builtin commands.
        *
        * Builtins execute inside the shell process and therefore bypass
        * the external command executor.
        */
        if (pipeline.count == 1)
        {
            if (execute_builtin(&pipeline.commands[0]))
            {
                continue;
            }
        }

        /* ── Phase 2 debug output ──────────────────────────────────────── */
        /*
         * Prints a summary of the parsed Pipeline so we can verify the
         * tokeniser and parser are working correctly before Phase 3.
         * This block will be replaced by execute(pipeline) in Phase 3.
         */
        if (pipeline.count == 0) {
            continue;                   /* Empty input — show prompt again */
        }

        execute_pipeline(
            &pipeline
        );
    }
}