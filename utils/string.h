/*
===============================================================================
string.h

Purpose:
    Declares the string and memory utility functions used by the educational
    POSIX shell.

Why this file exists:
    Since this project intentionally avoids libc, common string manipulation
    functions are reimplemented here.

Educational objective:
    Provide only the minimal functionality required by the shell instead of
    importing a full runtime library.

===============================================================================
*/

#ifndef STRING_H
#define STRING_H

#define NULL ((void*)0)

long my_strlen(
    const char *str
);

int my_strcmp(
    const char *a,
    const char *b
);

void *my_memcpy(
    void *dest,
    const void *src,
    long n
);

void *my_memset(
    void *ptr,
    int value,
    long n
);

char *my_strcpy(
    char *dest,
    const char *src
);

int my_strncmp(
    const char *a,
    const char *b,
    long n
);

#endif