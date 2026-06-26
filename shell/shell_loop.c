// shell/shell_loop.c
#include "../include/wrappers.h"
#include "../include/constants.h"
#include "../utils/string.h"
#include "../builtins/builtins.h"
#include "../parser/tokenizer.h"
#include "../parser/parser.h"

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

Current Phase 2 behaviour:
    - Reads input
    - Tokenises it
    - Parses it into a Pipeline struct
    - Prints a debug summary of what was parsed
    - Does NOT yet execute anything (Phase 3)

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

        /* Print background flag */
        if (pipeline.background) {
            char bg_msg[] = "[background]\n";
            sys_write(1, bg_msg, my_strlen(bg_msg));
        }

        /* Print each command */
        for (int i = 0; i < pipeline.count; i++) {
            Command *cmd = &pipeline.commands[i];

            /* Command index */
            char cmd_label[] = "CMD[x]: ";
            cmd_label[4] = '0' + i;
            sys_write(1, cmd_label, my_strlen(cmd_label));

            /* All arguments */
            for (int j = 0; j < cmd->argc; j++) {
                sys_write(1, cmd->argv[j], my_strlen(cmd->argv[j]));
                if (j < cmd->argc - 1) sys_write(1, " ", 1);
            }
            sys_write(1, "\n", 1);

            /* Input redirect */
            if (cmd->input_file[0] != '\0') {
                char in_label[] = "  stdin  < ";
                sys_write(1, in_label, my_strlen(in_label));
                sys_write(1, cmd->input_file, my_strlen(cmd->input_file));
                sys_write(1, "\n", 1);
            }

            /* Output redirect */
            if (cmd->output_file[0] != '\0') {
                char out_label[] = "  stdout > ";
                if (cmd->append) out_label[9] = '>';   /* show >> */
                sys_write(1, out_label, my_strlen(out_label));
                sys_write(1, cmd->output_file, my_strlen(cmd->output_file));
                sys_write(1, "\n", 1);
            }

            /* Builtin flag */
            if (cmd->is_builtin) {
                char bi_label[] = "  [builtin]\n";
                sys_write(1, bi_label, my_strlen(bi_label));
            }
        }

        /* Phase 3: replace the debug block above with:  execute(&pipeline); */       
}
}