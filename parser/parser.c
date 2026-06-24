// parser/parser.c
#include "parser.h"
#include "../utils/string.h"

/*
 * Resets a Command struct to a clean empty state.
 * Called before building each new command in the pipeline.
 *
 * Why we zero argv fully: execve() in Phase 3 requires the argv array to be
 * NULL-terminated. By zeroing all slots here, we guarantee the NULL sentinel
 * is always present even before we explicitly set argv[argc] = NULL later.
 */
static void init_command(Command *cmd)
{
    cmd->argc           = 0;
    cmd->input_file[0]  = '\0';
    cmd->output_file[0] = '\0';
    cmd->append         = 0;
    cmd->is_builtin     = 0;

    for (int i = 0; i <= MAX_ARGS; i++) {
        cmd->argv[i] = NULL;
    }
}

/*
 * Returns 1 if name matches a POSIX shell builtin, 0 otherwise.
 *
 * Why detection happens here and not in the executor:
 *   Builtins must NOT be forked. They run inside the shell process itself
 *   because they affect the shell's own state (e.g. cd changes the shell's
 *   working directory; exit terminates the shell process). The parser marks
 *   them here so the Phase 3 executor can skip fork() for these commands.
 *
 * Case-sensitive as per POSIX specification.
 */
static int is_builtin_command(const char *name)
{
    if (my_strcmp(name, "cd")   == 0) return 1;
    if (my_strcmp(name, "exit") == 0) return 1;
    if (my_strcmp(name, "pwd")  == 0) return 1;
    if (my_strcmp(name, "jobs") == 0) return 1;
    if (my_strcmp(name, "fg")   == 0) return 1;
    if (my_strcmp(name, "bg")   == 0) return 1;
    return 0;
}

/*
 * Parses a flat token array into a structured Pipeline.
 *
 * Walking strategy:
 *   We maintain one current_cmd being built. When we hit TOKEN_PIPE,
 *   we save current_cmd into the pipeline and start a fresh one.
 *   When we hit TOKEN_NEWLINE/EOF we save the final current_cmd.
 *
 * argv pointer safety:
 *   argv[n] points directly into tokens[i].value (no copy).
 *   The caller's tokens array must remain alive for the lifetime of pipeline.
 *   In shell_main(), both are declared in the same loop scope — this is safe.
 */
int parse(Token *tokens, int token_count, Pipeline *pipeline)
{
    pipeline->count      = 0;
    pipeline->background = 0;

    Command current_cmd;
    init_command(&current_cmd);

    int i = 0;
    while (i < token_count) {
        Token t = tokens[i];

        if (t.type == TOKEN_WORD) {
            /*
             * SYSCALL relevance: argv[0] becomes the program path passed to execve().
             * All subsequent words become the argument vector.
             * MAX_ARGS guards against stack overflow from extremely long commands.
             */
            if (current_cmd.argc < MAX_ARGS) {
                current_cmd.argv[current_cmd.argc] = tokens[i].value;

                /* Builtin detection only on the command name (argv[0]) */
                if (current_cmd.argc == 0) {
                    current_cmd.is_builtin = is_builtin_command(tokens[i].value);
                }
                current_cmd.argc++;
            }
            i++;

        } else if (t.type == TOKEN_PIPE) {
            /*
             * TOKEN_PIPE ends the current command and starts a new one.
             * A leading pipe (nothing before |) is a syntax error.
             *
             * SYSCALL relevance: each command in the pipeline becomes a separate
             * child process created by fork(). Pipes between them are created
             * by pipe() before any fork() in Phase 3.
             */
            if (current_cmd.argc == 0
                && current_cmd.input_file[0]  == '\0'
                && current_cmd.output_file[0] == '\0') {
                return -1; /* Syntax error: leading or double pipe */
            }
            if (pipeline->count < MAX_PIPELINE_DEPTH) {
                pipeline->commands[pipeline->count] = current_cmd;
                pipeline->count++;
            }
            init_command(&current_cmd);
            i++;

        } else if (t.type == TOKEN_REDIR_OUT
                || t.type == TOKEN_REDIR_APPEND
                || t.type == TOKEN_REDIR_IN) {
            /*
             * Redirection token must be immediately followed by a filename word.
             * The filename is stored in the Command struct and used by dup2()
             * in Phase 3 to redirect file descriptors before execve().
             */
            i++; /* Advance to the filename token */
            if (i >= token_count || tokens[i].type != TOKEN_WORD) {
                return -1; /* Syntax error: missing filename after redirect operator */
            }
            if (t.type == TOKEN_REDIR_IN) {
                my_strcpy(current_cmd.input_file, tokens[i].value);
            } else {
                my_strcpy(current_cmd.output_file, tokens[i].value);
                current_cmd.append = (t.type == TOKEN_REDIR_APPEND) ? 1 : 0;
            }
            i++;

        } else if (t.type == TOKEN_BACKGROUND) {
            /*
             * & means: do not waitpid() for this pipeline in Phase 3.
             * The shell immediately returns to the prompt.
             * A SIGCHLD handler (Phase 4) will later collect the child.
             */
            pipeline->background = 1;
            i++;

        } else if (t.type == TOKEN_SEMICOLON
                || t.type == TOKEN_NEWLINE
                || t.type == TOKEN_EOF) {
            /*
             * End of the pipeline. Save the last command if it has content.
             * A trailing pipe (e.g. "ls |") leaves current_cmd empty after
             * saving — detect that as a syntax error.
             */
            if (current_cmd.argc > 0
                || current_cmd.input_file[0]  != '\0'
                || current_cmd.output_file[0] != '\0') {
                if (pipeline->count < MAX_PIPELINE_DEPTH) {
                    pipeline->commands[pipeline->count] = current_cmd;
                    pipeline->count++;
                }
            } else if (pipeline->count > 0) {
                return -1; /* Syntax error: trailing pipe e.g. "ls |" */
            }
            break;

        } else {
            i++; /* Unknown token type — skip safely */
        }
    }

    /*
     * NULL-terminate argv for every command.
     *
     * Why: execve() requires argv[argc] == NULL as a sentinel.
     * init_command() already zeroed all argv slots, but we set it
     * explicitly here as a clear, documented guarantee for Phase 3.
     */
    for (int j = 0; j < pipeline->count; j++) {
        pipeline->commands[j].argv[pipeline->commands[j].argc] = NULL;
    }

    return 0;
}