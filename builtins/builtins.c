#include "../utils/string.h"
#include "../include/wrappers.h"

int execute_builtin(
    char *buffer
)
{
    /*
      read() keeps '\n'

      exit\n
    */

    if(
        my_strcmp(
            buffer,
            "exit\n"
        ) == 0
    )
    {
        return 1;
    }

    return 0;
}