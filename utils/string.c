//=============================================================================
// string.c
//
// Purpose:
//   Provides basic string and memory manipulation utilities for the
//   educational POSIX shell.
//
// Why this file exists:
//   Since this shell intentionally avoids libc, standard functions such as
//   strlen(), strcmp(), memcpy() and memset() are unavailable.
//
// Educational objective:
//   Reimplement only the minimal functionality required by the shell instead
//   of importing a complete runtime library.
//
// Future use cases:
//   - command parsing
//   - builtin command comparison
//   - argument processing
//   - token manipulation
//
//=============================================================================

#include "string.h"


//=============================================================================
// FUNCTION: my_strlen
//
// Purpose:
//   Computes the length of a null-terminated string.
//
// Parameters:
//   str : Input string.
//
// Returns:
//   Number of characters excluding the null terminator.
//
//=============================================================================

long my_strlen(const char *str)
{
    long len = 0;

    while (str[len] != '\0')
    {
        len++;
    }

    return len;
}


//=============================================================================
// FUNCTION: my_strcmp
//
// Purpose:
//   Lexicographically compares two strings.
//
// Parameters:
//   a : First string.
//   b : Second string.
//
// Returns:
//    0 : Strings are equal
//   <0 : a < b
//   >0 : a > b
//
//=============================================================================

int my_strcmp(
    const char *a,
    const char *b
)
{
    while (*a && *b)
    {
        if (*a != *b)
        {
            return *a - *b;
        }

        a++;
        b++;
    }

    return *a - *b;
}


//=============================================================================
// FUNCTION: my_memcpy
//
// Purpose:
//   Copies n bytes from source to destination.
//
// Parameters:
//   dest : Destination buffer.
//   src  : Source buffer.
//   n    : Number of bytes to copy.
//
// Returns:
//   Destination buffer.
//
//=============================================================================

void *my_memcpy(
    void *dest,
    const void *src,
    long n
)
{
    char *d = dest;

    const char *s = src;

    for (long i = 0; i < n; i++)
    {
        d[i] = s[i];
    }

    return dest;
}


//=============================================================================
// FUNCTION: my_memset
//
// Purpose:
//   Fills a memory region with a value.
//
// Parameters:
//   ptr   : Memory region.
//   value : Byte value.
//   n     : Number of bytes.
//
// Returns:
//   Pointer to the memory region.
//
//=============================================================================

void *my_memset(
    void *ptr,
    int value,
    long n
)
{
    char *p = ptr;

    for (long i = 0; i < n; i++)
    {
        p[i] = (char)value;
    }

    return ptr;
}

//=============================================================================
// FUNCTION: my_strcpy
//
// Purpose:
//   Copies a null-terminated string from source to destination.
//
// Parameters:
//   dest : Destination buffer.
//   src  : Source string.
//
// Returns:
//   Pointer to the destination buffer.
//
// Educational note:
//   This function is a minimal replacement for libc's strcpy() and is
//   primarily used by the tokenizer and parser.
//
//=============================================================================

char *my_strcpy(
    char *dest,
    const char *src
)
{
    char *start = dest;

    while (*src != '\0')
    {
        *dest = *src;

        dest++;
        src++;
    }

    *dest = '\0';

    return start;
}

//=============================================================================
// FUNCTION: my_strncmp
//
// Purpose:
//   Compares the first n characters of two strings.
//
// Parameters:
//   a : First string.
//   b : Second string.
//   n : Maximum number of characters to compare.
//
// Returns:
//    0 : Strings are equal for the first n characters
//   <0 : a < b
//   >0 : a > b
//
// Educational note:
//   This function is useful when the shell only needs to inspect a prefix
//   of a command instead of the entire string.
//
//=============================================================================

int my_strncmp(
    const char *a,
    const char *b,
    long n
)
{
    while (n > 0)
    {
        if (*a != *b)
        {
            return *a - *b;
        }

        if (*a == '\0')
        {
            return 0;
        }

        a++;
        b++;
        n--;
    }

    return 0;
}