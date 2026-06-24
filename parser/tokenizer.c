// parser/tokenizer.c
#include "tokenizer.h"
#include "../utils/string.h"

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
    /* Reset current word buffer if we were building one */
    if (current_token) {
        current_token->value[0] = '\0';
        *word_len = 0;
    }
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
            } else {
                state = STATE_IN_WORD;
                current_token.type = TOKEN_WORD;
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_WORD) {
            /* If we hit a metacharacter, emit the word and let NORMAL handle the metacharacter */
            if (is_whitespace(c) || c == '|' || c == '>' || c == '<' || c == '&' || c == ';') {
                emit_token(tokens, token_count, TOKEN_WORD, &current_token, &word_len);
                state = STATE_NORMAL;
            } else if (c == '\'') {
                state = STATE_IN_SINGLE_QUOTE;
                i++;
            } else if (c == '"') {
                state = STATE_IN_DOUBLE_QUOTE;
                i++;
            } else {
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        } else if (state == STATE_IN_SINGLE_QUOTE) {
            if (c == '\'') {
                state = STATE_IN_WORD; /* Return to word state to allow 'a''b' -> "ab" */
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
                i++; /* Skip the backslash */
                add_char_to_word(&current_token, input[i], &word_len);
                i++;
            } else {
                add_char_to_word(&current_token, c, &word_len);
                i++;
            }
        }
    }
}