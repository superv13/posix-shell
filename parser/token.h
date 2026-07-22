// parser/token.h
//
// Step 4 — && and || operators:
//   TOKEN_AND  : the '&&' token — run next pipeline only if previous exited 0
//   TOKEN_OR   : the '||' token — run next pipeline only if previous exited non-zero
//
#ifndef TOKEN_H
#define TOKEN_H

#include "../include/constants.h"

/* Token types produced by the tokenizer */
typedef enum {
    TOKEN_WORD,           /* Command name, argument, or filename */
    TOKEN_PIPE,           /* |                                   */
    TOKEN_REDIR_OUT,      /* >                                   */
    TOKEN_REDIR_APPEND,   /* >>                                  */
    TOKEN_REDIR_IN,       /* <                                   */
    TOKEN_REDIR_DUP_OUT,  /* N>&M  — dup stdout (or fd N) to M  */
    TOKEN_REDIR_DUP_IN,   /* N<&M  — dup stdin  (or fd N) to M  */
    TOKEN_BACKGROUND,     /* &  (single ampersand — background)  */
    TOKEN_AND,            /* && (AND list — run if prev exited 0)*/
    TOKEN_OR,             /* || (OR  list — run if prev failed)  */
    TOKEN_SEMICOLON,      /* ;                                   */
    TOKEN_NEWLINE,        /* \n                                  */
    TOKEN_BANG,           /* !  pipeline negation keyword        */
    TOKEN_EOF             /* End of input                        */
} TokenType;

/* Represents a single scanned unit */
typedef struct {
    TokenType type;
    char value[MAX_TOKEN_LEN]; /* Filled only when type == TOKEN_WORD */
} Token;

#endif