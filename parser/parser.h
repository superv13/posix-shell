// parser/parser.h
#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "../include/constants.h"

/*
 * Represents one executable command (one stage of a pipeline).
 *
 * argv[] holds pointers directly into the token value arrays.
 * The tokens array must remain in scope for as long as this Command is used.
 * NULL-terminated at argv[argc] as required by execve() in Phase 3.
 */
typedef struct {
    char *argv[MAX_ARGS + 1];           /* Pointers into token values. NULL-terminated. */
    int   argc;                         /* Argument count */
    char  input_file[MAX_FILENAME_LEN]; /* Filename for < redirect. Empty = none. */
    char  output_file[MAX_FILENAME_LEN];/* Filename for > or >> redirect. Empty = none. */
    int   append;                       /* 0 = overwrite (>), 1 = append (>>) */
    int   is_builtin;                   /* 1 if cd, exit, pwd, jobs, fg, bg */
} Command;

/*
 * Represents a complete user input line — one or more commands joined by pipes.
 */
typedef struct {
    Command commands[MAX_PIPELINE_DEPTH];
    int     count;                      /* Number of commands in this pipeline */
    int     background;                 /* 1 if & was present, 0 otherwise */
} Pipeline;

/*
 * Parses a token array into a Pipeline struct.
 *
 * tokens      : Array of tokens produced by tokenize().
 *               argv[] pointers inside Pipeline will point into this array.
 *               This array must outlive the Pipeline.
 * token_count : Number of valid tokens in the array.
 * pipeline    : Caller-provided Pipeline struct to fill.
 *
 * Returns 0 on success, -1 on syntax error.
 */
int parse(Token *tokens, int token_count, Pipeline *pipeline);

#endif