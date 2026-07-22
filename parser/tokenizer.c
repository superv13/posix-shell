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
//
// Step 2 — Tilde expansion (POSIX XBD 2.6.1):
//   '~' at the very start of an unquoted word expands to $HOME.
//   Specifically:
//     ~        alone (followed by whitespace, metacharacter, or \0) → $HOME
//     ~/path   at word start followed by '/'                        → $HOME/path
//   '~' anywhere else (mid-word, inside quotes) is left as a literal.
//   If $HOME is unset, '~' is left unexpanded (POSIX XBD 2.6.1, line 3).
//
// Step 4 — && and || operators:
//   '&' followed immediately by another '&' emits TOKEN_AND  (&&).
//   '|' followed immediately by another '|' emits TOKEN_OR   (||).
//   A single '&' still emits TOKEN_BACKGROUND.
//   A single '|' still emits TOKEN_PIPE.
//   Both are recognised only in STATE_NORMAL (between words), matching POSIX.
//
// Step 5 — $VAR and ${VAR} expansion (POSIX XBD 2.6.2):
//   After '$', if the next character is a letter or '_', scan an identifier
//   (letters, digits, underscores) as the variable name, look it up via
//   env_get(), and copy its value into the token buffer.
//   '${VAR}' is the braced form: scan until '}', same lookup.
//   Unset variables expand to an empty string (POSIX XBD 2.6.2).
//   Expansion is suppressed inside single quotes (STATE_IN_SINGLE_QUOTE).

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
 * is_var_char
 *
 * Returns 1 if `c` is a valid character INSIDE an environment variable name:
 * letters (a-z, A-Z), digits (0-9), or underscore.
 *
 * This deliberately excludes the first character (which must be a letter or
 * underscore — that check is done inline in try_dollar_expansion).
 *
 * Why not use isalnum() from <ctype.h>:
 *   This project has zero libc dependency.  The ASCII ranges are stable and
 *   will never change, so a direct range comparison is correct and portable.
 */
static int is_var_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           (c == '_');
}

/*
 * expand_tilde
 *
 * Called from STATE_NORMAL when the tokenizer sees '~' as the very first
 * character of a new word.
 *
 * POSIX XBD 2.6.1 — Tilde Expansion:
 *   "If a word begins with an unquoted <tilde> character, all of the
 *   characters preceding the first unquoted <slash> in the word [...]
 *   constitute a tilde prefix."
 *
 * We handle the common shell case: a bare tilde prefix with no login name
 * (i.e. "~" or "~/..."), which expands to the value of HOME.
 *
 * Parameters:
 *   input    : full input buffer
 *   i        : current index (pointing AT the '~')
 *   tok      : current token being built (still empty at call time)
 *   word_len : current token length (0 at call time)
 *
 * Returns:
 *   1  if tilde was expanded — *i is advanced past the '~'
 *   0  if '~' is not a valid tilde prefix (leave it as a literal)
 *
 * Does NOT advance past the '/' or the rest of the path; the normal
 * STATE_IN_WORD loop handles those characters after we return.
 */
static int expand_tilde(
    const char *input,
    int        *i,
    Token      *tok,
    int        *word_len
)
{
    char next = input[*i + 1];

    /*
     * POSIX tilde-prefix without login name:
     *   ~ alone       : next char is \0, \n, space, tab, or a shell metachar
     *   ~/path        : next char is '/'
     *
     * Any other character after '~' (e.g. "~user" or "~1") would be a
     * login-name tilde prefix.  We do not implement login-name lookup
     * (requires /etc/passwd access which needs libc).  Leave those as
     * literal '~'.
     */
    if (next != '/' && next != '\0' && next != '\n' &&
        next != ' '  && next != '\t' &&
        next != '|'  && next != '<'  && next != '>' &&
        next != '&'  && next != ';')
    {
        return 0;   /* not a bare tilde prefix — treat '~' as literal */
    }

    /*
     * Look up $HOME.
     *
     * POSIX XBD 2.6.1:
     *   "If HOME is unset, the results are unspecified."
     * We choose the most useful behaviour: leave '~' unexpanded.
     */
    const char *home = env_get("HOME");
    if (home == 0)
    {
        return 0;   /* HOME unset — leave '~' as literal */
    }

    /*
     * Copy every character of $HOME into the token buffer.
     * add_char_to_word() enforces the MAX_TOKEN_LEN limit, so this
     * cannot overflow even with a very long HOME path.
     */
    for (int k = 0; home[k] != '\0'; k++)
    {
        add_char_to_word(tok, home[k], word_len);
    }

    *i += 1;    /* skip past '~'; the '/' and rest are handled by the caller */
    return 1;
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
 * Returns 1 if an expansion was performed, 0 if '$' should be treated
 * as a literal character.
 *
 * POSIX expansions handled (Step 5 completes the set):
 *
 *   $?       Last foreground pipeline exit status (g_last_status).
 *            POSIX XBD 2.5.2: decimal value of the most recent exit status.
 *
 *   $$       PID of the shell itself (g_shell_pgid).
 *            POSIX XBD 2.5.2: decimal process ID of the invoking shell.
 *
 *   $VAR     General environment variable (Step 5).
 *            Next char is a letter or '_': scan the identifier, call
 *            env_get(), copy the value.  Unset → empty string (POSIX).
 *
 *   ${VAR}   Braced form of general variable expansion (Step 5).
 *            Same as $VAR but delimited by '{' ... '}'.  Useful when the
 *            variable name is immediately followed by a letter or digit that
 *            would otherwise extend the name: "${VAR}suffix".
 *
 * Not yet implemented (future work):
 *   $0, $1..$9  — positional parameters
 *   $@, $*      — all positional parameters
 *   $#          — number of positional parameters
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
         *   0 for background launches (POSIX: async cmd sets $? = 0),
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

    /*
     * Step 5 — ${VAR} braced expansion.
     *
     * Syntax: '${' <name> '}'  where <name> is a letter or '_' followed
     * by zero or more letters, digits, or underscores.
     *
     * The braces are consumed but not included in the variable name.
     * If the closing '}' is missing, we fall through and treat '$' as a
     * literal (defensive: avoids undefined behaviour on malformed input).
     */
    if (next == '{')
    {
        int j = *i + 2;     /* position of first char inside '{}' */

        /* Variable name must start with a letter or underscore */
        if (!((input[j] >= 'a' && input[j] <= 'z') ||
              (input[j] >= 'A' && input[j] <= 'Z') ||
               input[j] == '_'))
        {
            return 0;   /* not a valid name — treat '$' as literal */
        }

        /* Scan to end of name */
        int name_start = j;
        while (is_var_char(input[j]))
            j++;

        /* Expect closing '}' */
        if (input[j] != '}')
            return 0;   /* malformed ${... — treat '$' as literal */

        /* Extract name into a local buffer */
        int   name_len = j - name_start;
        char  name_buf[64];     /* env var names > 63 chars are pathological */
        if (name_len >= (int)sizeof(name_buf))
            name_len = (int)sizeof(name_buf) - 1;
        for (int k = 0; k < name_len; k++)
            name_buf[k] = input[name_start + k];
        name_buf[name_len] = '\0';

        /* Look up and copy the value (empty string if unset) */
        const char *val = env_get(name_buf);
        if (val != 0)
        {
            for (int k = 0; val[k] != '\0'; k++)
                add_char_to_word(tok, val[k], word_len);
        }
        /* else: unset variable — expand to empty (POSIX XBD 2.6.2) */

        *i = j + 1;     /* skip past '$', '{', name, '}' */
        return 1;
    }

    /*
     * Step 5 — $VAR unbraced expansion.
     *
     * The character after '$' must be a letter or '_' to be a valid
     * variable name start (POSIX XBD 2.10.2).  Digits after '$' that
     * are not '?' are positional parameters ($1..$9) — not yet
     * implemented; they are treated as literal '$' for now.
     */
    if ((next >= 'a' && next <= 'z') ||
        (next >= 'A' && next <= 'Z') ||
         next == '_')
    {
        /* Scan the full variable name */
        int j = *i + 1;     /* position of first name char (= next) */
        while (is_var_char(input[j]))
            j++;
        /* input[j] is now the first character AFTER the name */

        /* Extract name into a local buffer */
        int  name_len   = j - (*i + 1);
        char name_buf[64];
        if (name_len >= (int)sizeof(name_buf))
            name_len = (int)sizeof(name_buf) - 1;
        for (int k = 0; k < name_len; k++)
            name_buf[k] = input[*i + 1 + k];
        name_buf[name_len] = '\0';

        /* Look up and copy the value (empty string if unset) */
        const char *val = env_get(name_buf);
        if (val != 0)
        {
            for (int k = 0; val[k] != '\0'; k++)
                add_char_to_word(tok, val[k], word_len);
        }
        /* else: unset variable — expand to empty (POSIX XBD 2.6.2) */

        *i = j;     /* skip past '$' + name */
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
                /*
                 * Step 4 — '|' vs '||'.
                 *
                 * Peek at the next character:
                 *   '||' → TOKEN_OR  (advance 2)
                 *    '|' → TOKEN_PIPE (advance 1)
                 *
                 * Must come before the general word-start path.
                 */
                if (input[i + 1] == '|') {
                    emit_token(tokens, token_count, TOKEN_OR, NULL, NULL);
                    i += 2;
                } else {
                    emit_token(tokens, token_count, TOKEN_PIPE, NULL, NULL);
                    i++;
                }
            } else if (c == '>') {
                /*
                 * '>' vs '>>' vs '>&M':
                 *   '>>N' or '>>'  → TOKEN_REDIR_APPEND
                 *   '>&-'          → TOKEN_REDIR_DUP_OUT (close)
                 *   '>&M'          → TOKEN_REDIR_DUP_OUT  value="1>M"
                 *   '>'            → TOKEN_REDIR_OUT
                 * Also handles N>&M where previous WORD token was a digit.
                 */
                if (input[i+1] == '>') {
                    emit_token(tokens, token_count, TOKEN_REDIR_APPEND, NULL, NULL);
                    i += 2;
                } else if (input[i+1] == '&') {
                    /* '>&M' — fd-dup: stdout → M */
                    int j = i + 2;
                    char val[16];
                    int vlen = 0;
                    /* encode as "src>dst" in value; src defaults to 1 */
                    val[vlen++] = '1'; val[vlen++] = '>';
                    if (input[j] == '-') {
                        val[vlen++] = '-'; j++;
                    } else {
                        while (input[j] >= '0' && input[j] <= '9' && vlen < 14)
                            val[vlen++] = input[j++];
                    }
                    val[vlen] = '\0';
                    if (*token_count < MAX_TOKENS) {
                        tokens[*token_count].type = TOKEN_REDIR_DUP_OUT;
                        for (int k = 0; k <= vlen; k++)
                            tokens[*token_count].value[k] = val[k];
                        (*token_count)++;
                    }
                    i = j;
                } else {
                    emit_token(tokens, token_count, TOKEN_REDIR_OUT, NULL, NULL);
                    i++;
                }
            } else if (c == '<') {
                if (input[i+1] == '&') {
                    /* '<&M' — fd-dup: stdin ← M */
                    int j = i + 2;
                    char val[16];
                    int vlen = 0;
                    val[vlen++] = '0'; val[vlen++] = '<';
                    if (input[j] == '-') {
                        val[vlen++] = '-'; j++;
                    } else {
                        while (input[j] >= '0' && input[j] <= '9' && vlen < 14)
                            val[vlen++] = input[j++];
                    }
                    val[vlen] = '\0';
                    if (*token_count < MAX_TOKENS) {
                        tokens[*token_count].type = TOKEN_REDIR_DUP_IN;
                        for (int k = 0; k <= vlen; k++)
                            tokens[*token_count].value[k] = val[k];
                        (*token_count)++;
                    }
                    i = j;
                } else {
                    emit_token(tokens, token_count, TOKEN_REDIR_IN, NULL, NULL);
                    i++;
                }
            } else if (c == '&') {
                /*
                 * Step 4 — '&' vs '&&'.
                 *
                 * Peek at the next character:
                 *   '&&' → TOKEN_AND        (advance 2)
                 *    '&' → TOKEN_BACKGROUND  (advance 1)
                 */
                if (input[i + 1] == '&') {
                    emit_token(tokens, token_count, TOKEN_AND, NULL, NULL);
                    i += 2;
                } else {
                    emit_token(tokens, token_count, TOKEN_BACKGROUND, NULL, NULL);
                    i++;
                }
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
            } else if (c == '~') {
                /*
                 * Step 2 — Tilde expansion (POSIX XBD 2.6.1).
                 *
                 * '~' at the start of an unquoted word may expand to $HOME.
                 * expand_tilde() decides whether expansion applies and, if so,
                 * fills the token buffer with the HOME value and advances *i
                 * past the '~'.  On return we enter STATE_IN_WORD so that any
                 * trailing characters (e.g. the '/foo' in '~/foo') continue
                 * building the same token.
                 *
                 * If expand_tilde() returns 0 (HOME unset, or not a bare tilde
                 * prefix), we fall through and add '~' as a literal character.
                 */
                state = STATE_IN_WORD;
                current_token.type = TOKEN_WORD;
                if (!expand_tilde(input, &i, &current_token, &word_len)) {
                    add_char_to_word(&current_token, c, &word_len);
                    i++;
                }
            } else if (c == '#') {
                /*
                 * POSIX XBD 2.3 — Comment.
                 */
                emit_token(tokens, token_count, TOKEN_EOF, NULL, NULL);
                break;
            } else if (c == '!' && word_len == 0) {
                /*
                 * '!' in NORMAL state (i.e. not mid-word) is the POSIX
                 * pipeline negation keyword.  Emit TOKEN_BANG so the parser
                 * can set pipeline->negate = 1.
                 *
                 * '!' mid-word (e.g. "echo foo!bar") falls through to the
                 * else branch and is treated as a literal character.
                 */
                emit_token(tokens, token_count, TOKEN_BANG, NULL, NULL);
                i++;
            } else {
                state = STATE_IN_WORD;
                current_token.type = TOKEN_WORD;
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_WORD) {
            /*
             * N>&M / N<&M — fd-dup redirect where N is the word we are
             * currently building (e.g. "1" in "echo hello 1>&2").
             *
             * Detect this BEFORE the generic "flush word on metachar" path:
             * if the current partial word is all digits AND the next two
             * characters are '>&' or '<&', consume the digit as the source
             * fd and emit TOKEN_REDIR_DUP_OUT or TOKEN_REDIR_DUP_IN.
             */
            int is_all_digits = (word_len > 0);
            for (int di = 0; di < word_len && is_all_digits; di++)
                if (current_token.value[di] < '0' || current_token.value[di] > '9')
                    is_all_digits = 0;

            if (is_all_digits && c == '>' && input[i+1] == '&') {
                /* Grab the source fd from the word buffer */
                int src = 0;
                for (int di = 0; di < word_len; di++)
                    src = src * 10 + (current_token.value[di] - '0');
                /* Reset word buffer — fd consumed as redirect, not argument */
                current_token.value[0] = '\0';
                word_len = 0;
                state = STATE_NORMAL;
                /* Build value string "src>dst" */
                int j = i + 2;
                char val[16];
                int vlen = 0;
                /* encode src */
                char tmp[8]; int tn = 0;
                int sv = src;
                if (sv == 0) { tmp[tn++] = '0'; }
                else { while (sv > 0) { tmp[tn++] = '0' + (sv % 10); sv /= 10; } }
                for (int k = tn-1; k >= 0; k--) val[vlen++] = tmp[k];
                val[vlen++] = '>';
                if (input[j] == '-') { val[vlen++] = '-'; j++; }
                else { while (input[j] >= '0' && input[j] <= '9' && vlen < 14) val[vlen++] = input[j++]; }
                val[vlen] = '\0';
                if (*token_count < MAX_TOKENS) {
                    tokens[*token_count].type = TOKEN_REDIR_DUP_OUT;
                    for (int k = 0; k <= vlen; k++) tokens[*token_count].value[k] = val[k];
                    (*token_count)++;
                }
                i = j;
            } else if (is_all_digits && c == '<' && input[i+1] == '&') {
                int src = 0;
                for (int di = 0; di < word_len; di++)
                    src = src * 10 + (current_token.value[di] - '0');
                current_token.value[0] = '\0';
                word_len = 0;
                state = STATE_NORMAL;
                int j = i + 2;
                char val[16];
                int vlen = 0;
                char tmp[8]; int tn = 0;
                int sv = src;
                if (sv == 0) { tmp[tn++] = '0'; }
                else { while (sv > 0) { tmp[tn++] = '0' + (sv % 10); sv /= 10; } }
                for (int k = tn-1; k >= 0; k--) val[vlen++] = tmp[k];
                val[vlen++] = '<';
                if (input[j] == '-') { val[vlen++] = '-'; j++; }
                else { while (input[j] >= '0' && input[j] <= '9' && vlen < 14) val[vlen++] = input[j++]; }
                val[vlen] = '\0';
                if (*token_count < MAX_TOKENS) {
                    tokens[*token_count].type = TOKEN_REDIR_DUP_IN;
                    for (int k = 0; k <= vlen; k++) tokens[*token_count].value[k] = val[k];
                    (*token_count)++;
                }
                i = j;
            } else if (is_whitespace(c) || c == '|' || c == '>' || c == '<' || c == '&' || c == ';') {
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
