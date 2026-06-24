// parser/tokenizer.h
#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"

/* 
 * Tokenizes raw input into an array of Token structs.
 * No memory allocation occurs here; the caller provides the buffer.
 */
void tokenize(const char *input, Token *tokens, int *token_count);

#endif