// parser/tokenizer.c
//
// Phase 5 additions:
//   - $? expansion: replaced with the decimal value of g_last_status
//   - $$ expansion: replaced with the decimal value of g_shell_pgid
//   - Both expansions work in unquoted words and inside double quotes.
//   - Neither expansion happens inside single quotes (POSIX rule).
//
// Step 1 — Comment handling (POSIX XBD 2.3):
//   '#' appearing in STATE_NORMAL (i.e. not inside a word or quote) starts
//   a comment.  Everything from '#' to the end of the line is ignored.
//   '#' that appears mid-word (STATE_IN_WORD) is treated as a literal
//   character — this matches POSIX: "he#llo" is one word, not a comment.

#include "tokenizer.h"
#include "../utils/string.h"
#include "../env/env.h"          /* g_last_status             */
#include "../signals/signals.h"  /* g_shell_pgid              */

typedef enum {
    STATE_NORMAL,
    STATE_IN_WORD,
    STATE_IN_SINGLE_QUOTE,
    STATE_IN_DOUBLE_QUOTE
} TokenizerState;

static int is_whitespace(char c) {
    return c == ' ' || c == '\t';
}

static void add_char_to_word(Token *current_token, char c, int *len) {
    if (*len < MAX_TOKEN_LEN - 1) {
        current_token->value[*len] = c;
        (*len)++;
        current_token->value[*len] = '\0';
    }
}

static void emit_token(Token *tokens, int *count, TokenType type, Token *current_token, int *word_len) {
    if (*count < MAX_TOKENS) {
        if (type == TOKEN_WORD && current_token) {
            tokens[*count] = *current_token;
        } else {
            tokens[*count].type = type;
            tokens[*count].value[0] = '\0';
        }
        (*count)++;
    }
    if (current_token) {
        current_token->value[0] = '\0';
        *word_len = 0;
    }
}

/*
 * expand_integer
 *
 * Converts `value` to its decimal ASCII representation and appends it
 * character by character to the current token being built.
 *
 * Why character-by-character (not snprintf):
 *   snprintf is a libc function.  This project uses no libc.  We reverse-
 *   generate the digits into a local stack buffer and then walk it in
 *   reverse (most-significant-first) order, injecting each digit via
 *   add_char_to_word() so the token's length limit is respected.
 *
 * Called when the tokenizer encounters:
 *   $?   → expand_integer(tok, len, g_last_status)
 *   $$   → expand_integer(tok, len, g_shell_pgid)
 */
static void expand_integer(Token *tok, int *len, long value)
{
    char digits[20];
    int  n = 0;

    if (value < 0)
    {
        add_char_to_word(tok, '-', len);
        value = -value;
    }

    if (value == 0)
    {
        add_char_to_word(tok, '0', len);
        return;
    }

    /* Generate digits least-significant-first */
    while (value > 0 && n < (int)sizeof(digits))
    {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    /* Inject most-significant-first */
    while (n > 0)
    {
        add_char_to_word(tok, digits[--n], len);
    }
}

/*
 * try_dollar_expansion
 *
 * Called when the tokenizer sees '$' in a context where expansion is
 * allowed (unquoted word or double-quoted string; NOT single-quoted).
 *
 * Parameters:
 *   input    : full input buffer (to peek at the character after '$')
 *   i        : current position (pointing at '$')
 *   tok      : current token being built
 *   word_len : current length of tok->value
 *
 * Modifies *i to skip past the expanded characters.
 *
 * Returns 1 if an expansion was performed ($? or $$), 0 if '$' should
 * be treated as a literal character.
 *
 * POSIX expansions handled here (Phase 5):
 *
 *   $?   Last foreground pipeline exit status (g_last_status).
 *        POSIX XBD 2.5.2: "Shall expand to the decimal value of the
 *        exit status of the most recent pipeline."
 *
 *   $$   PID of the shell itself (g_shell_pgid, which equals the shell's
 *        PID after setpgid(0,0) in shell_main).
 *        POSIX XBD 2.5.2: "Shall expand to the decimal process ID of
 *        the invoking shell."
 *
 * Future expansions (Phase 6+):
 *   $1..$9, $0  — positional parameters (not yet implemented)
 *   $@, $*      — all positional parameters
 *   $#          — number of positional parameters
 *   ${VAR}      — general variable expansion
 */
static int try_dollar_expansion(
    const char *input,
    int        *i,
    Token      *tok,
    int        *word_len
)
{
    char next = input[*i + 1];

    if (next == '?')
    {
        /*
         * $? — last exit status.
         *
         * g_last_status holds the POSIX-formatted exit code:
         *   0–255 for normally-exited commands,
         *   128+sig for killed-by-signal commands,
         *   0 for background launches,
         *   2 for syntax errors.
         */
        expand_integer(tok, word_len, (long)g_last_status);
        *i += 2;    /* skip '$' and '?' */
        return 1;
    }

    if (next == '$')
    {
        /*
         * $$ — shell PID.
         *
         * g_shell_pgid is set in shell_main() via sys_getpid() after
         * setpgid(0,0).  Since PGID == PID for the shell process, this
         * gives the correct shell PID.
         *
         * Common use in scripts: mktemp /tmp/foo.$$ creates a unique
         * temporary file per shell invocation.
         */
        expand_integer(tok, word_len, g_shell_pgid);
        *i += 2;    /* skip '$' and '$' */
        return 1;
    }

    return 0;   /* not a handled expansion; treat '$' as literal */
}

void tokenize(const char *input, Token *tokens, int *token_count) {
    *token_count = 0;
    TokenizerState state = STATE_NORMAL;
    int i = 0;
    int word_len = 0;
    Token current_token;
    current_token.type = TOKEN_WORD;
    current_token.value[0] = '\0';

    while (1) {
        char c = input[i];

        /* Handle end of line or end of input */
        if (c == '\0' || c == '\n') {
            if (state == STATE_IN_WORD || state == STATE_IN_SINGLE_QUOTE || state == STATE_IN_DOUBLE_QUOTE) {
                emit_token(tokens, token_count, TOKEN_WORD, &current_token, &word_len);
            }
            if (c == '\n') {
                emit_token(tokens, token_count, TOKEN_NEWLINE, NULL, NULL);
            }
            emit_token(tokens, token_count, TOKEN_EOF, NULL, NULL);
            break;
        }

        if (state == STATE_NORMAL) {
            if (is_whitespace(c)) {
                i++;
            } else if (c == '|') {
                emit_token(tokens, token_count, TOKEN_PIPE, NULL, NULL);
                i++;
            } else if (c == '>') {
                if (input[i+1] == '>') {
                    emit_token(tokens, token_count, TOKEN_REDIR_APPEND, NULL, NULL);
                    i += 2;
                } else {
                    emit_token(tokens, token_count, TOKEN_REDIR_OUT, NULL, NULL);
                    i++;
                }
            } else if (c == '<') {
                emit_token(tokens, token_count, TOKEN_REDIR_IN, NULL, NULL);
                i++;
            } else if (c == '&') {
                emit_token(tokens, token_count, TOKEN_BACKGROUND, NULL, NULL);
                i++;
            } else if (c == ';') {
                emit_token(tokens, token_count, TOKEN_SEMICOLON, NULL, NULL);
                i++;
            } else if (c == '\'') {
                state = STATE_IN_SINGLE_QUOTE;
                current_token.type = TOKEN_WORD;
                i++;
            } else if (c == '"') {
                state = STATE_IN_DOUBLE_QUOTE;
                current_token.type = TOKEN_WORD;
                i++;
            } else if (c == '$') {
                /*
                 * Start a new word with a dollar expansion.
                 *
                 * POSIX: $? and $$ are valid as standalone tokens
                 * (e.g. "echo $?") as well as embedded in words
                 * (e.g. "echo exit-$?").  We enter STATE_IN_WORD so
                 * that subsequent non-whitespace characters continue
                 * building the same token.
                 */
                state = STATE_IN_WORD;
                current_token.type = TOKEN_WORD;
                if (!try_dollar_expansion(input, &i, &current_token, &word_len)) {
                    /* Not $? or $$ — treat '$' as a literal character */
                    add_char_to_word(&current_token, c, &word_len);
                    i++;
                }
            } else if (c == '#') {
                /*
                 * POSIX XBD 2.3 — Comment:
                 *   A word beginning with '#' that is unquoted introduces
                 *   a comment.  The comment runs to the end of the line.
                 *   We emit TOKEN_EOF here so the parser sees a complete
                 *   (possibly empty) command followed by end-of-input —
                 *   identical to an empty line.
                 *
                 *   '#' mid-word (STATE_IN_WORD) does NOT reach this
                 *   branch; it falls through to the else below and is
                 *   added as a literal character.  That is correct POSIX
                 *   behaviour: "echo he#llo" prints "he#llo".
                 */
                emit_token(tokens, token_count, TOKEN_EOF, NULL, NULL);
                break;
            } else {
                state = STATE_IN_WORD;
                current_token.type = TOKEN_WORD;
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_WORD) {
            if (is_whitespace(c) || c == '|' || c == '>' || c == '<' || c == '&' || c == ';') {
                emit_token(tokens, token_count, TOKEN_WORD, &current_token, &word_len);
                state = STATE_NORMAL;
            } else if (c == '\'') {
                state = STATE_IN_SINGLE_QUOTE;
                i++;
            } else if (c == '"') {
                state = STATE_IN_DOUBLE_QUOTE;
                i++;
            } else if (c == '$') {
                /*
                 * Dollar sign mid-word: attempt expansion.
                 * If not a recognised $X form, add '$' literally.
                 */
                if (!try_dollar_expansion(input, &i, &current_token, &word_len)) {
                    add_char_to_word(&current_token, c, &word_len);
                    i++;
                }
            } else {
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_SINGLE_QUOTE) {
            /*
             * POSIX: inside single quotes, NO characters are special —
             * not backslash, not dollar, not backtick.  Everything is
             * literal.  We do NOT call try_dollar_expansion here.
             */
            if (c == '\'') {
                state = STATE_IN_WORD;
                i++;
            } else {
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_DOUBLE_QUOTE) {
            if (c == '"') {
                state = STATE_IN_WORD;
                i++;
            }
            /* POSIX rule: backslash is only special before $, `, ", \ inside double quotes */
            else if (c == '\\' && (input[i+1] == '$' || input[i+1] == '`' || input[i+1] == '"' || input[i+1] == '\\')) {
                i++;
                add_char_to_word(&current_token, input[i], &word_len);
                i++;
            } else if (c == '$') {
                /*
                 * Dollar sign inside double quotes: attempt expansion.
                 * POSIX XBD 2.2.3: $, `, and \ retain their special
                 * meaning inside double quotes.  We handle $? and $$
                 * here; other $-forms are treated literally for now.
                 */
                if (!try_dollar_expansion(input, &i, &current_token, &word_len)) {
                    add_char_to_word(&current_token, c, &word_len);
                    i++;
                }
            } else {
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        }
    }
}
