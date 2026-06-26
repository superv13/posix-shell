#include "pwd.h"
#include "../utils/string.h"
#include "../include/wrappers.h"

void builtin_pwd(void)
{
    /*
        Phase 3

        Requires:

        sys_getcwd()
    */
    char cwd[256];

    long ret = sys_getcwd(
        cwd,
        sizeof(cwd)
    );

    if (ret >= 0)
    {
        sys_write(
            1,
            cwd,
            my_strlen(cwd)
        );

        sys_write(
            1,
            "\n",
            1
        );
    }
}