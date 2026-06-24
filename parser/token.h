// parser/token.h
#ifndef TOKEN_H
#define TOKEN_H

#include "../include/constants.h"

/* Token types produced by the tokenizer */
typedef enum {
    TOKEN_WORD,           /* Command name, argument, or filename */
    TOKEN_PIPE,           /* | */
    TOKEN_REDIR_OUT,      /* > */
    TOKEN_REDIR_APPEND,   /* >> */
    TOKEN_REDIR_IN,       /* < */
    TOKEN_BACKGROUND,     /* & */
    TOKEN_SEMICOLON,      /* ; */
    TOKEN_NEWLINE,        /* \n */
    TOKEN_EOF             /* End of input */
} TokenType;

/* Represents a single scanned unit */
typedef struct {
    TokenType type;
    char value[MAX_TOKEN_LEN]; /* Filled only when type == TOKEN_WORD */
} Token;

#endif