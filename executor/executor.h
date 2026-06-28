#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
===============================================================================
executor.h

Purpose:
    Declares the public interface of the shell execution engine.

Why this file exists:
    After the parser constructs a Pipeline object, control is transferred
    to the executor. The executor is responsible for running builtin
    commands or launching external programs using Linux system calls.

Responsibilities:
    - Execute builtin commands (already identified by the parser)
    - Create child processes
    - Execute external programs
    - Wait for foreground processes
    - (Future) Handle pipes and redirection
    - (Future) Handle background jobs

Execution flow:

    shell_main()

          │

          ▼

    execute_pipeline()

          │

    ┌─────┴─────┐

 Builtin      External

    │             │

 execute     fork()

                │

            execve()

                │

            wait4()

===============================================================================
*/

#include "../parser/parser.h"

/*
===============================================================================
FUNCTION: execute_pipeline

Purpose:
    Executes a parsed command pipeline.

Parameters:
    pipeline : Parsed pipeline produced by the parser.

Returns:
     0 : Success.
    -1 : Execution error.

Current implementation (Phase 3):
    - N-stage pipelines ("a | b | c")
    - Input/output redirection ("<", ">", ">>")
    - Background execution ("cmd &")
    - PATH-resolved external commands and in-pipeline builtins

Future extensions:
    - Job table / job control (fg, bg, jobs) -- Phase 4
    - Signal handling, process groups -- Phase 4

===============================================================================
*/

int execute_pipeline(
    Pipeline *pipeline
);

#endif