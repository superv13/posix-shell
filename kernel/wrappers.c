// kernel/wrappers.c
#include "../include/syscall.h"
#include "../include/wrappers.h"

//=============================================================================
// SYSTEM CALL: sys_write
//
// Purpose:
//   Provides a thin wrapper around the Linux write() system call.
//   This function allows the shell to send bytes directly to a file
//   descriptor without using libc functions such as printf() or puts().
//
// Why this exists:
//   Since this shell has zero libc dependency, standard output functions
//   are unavailable. sys_write() is our explicit interface between the
//   shell and the Linux kernel.
//
// Kernel interaction:
//   write(fd, buf, count)
//
// Parameters:
//   fd    : File descriptor to write to.
//           0 -> stdin
//           1 -> stdout
//           2 -> stderr
//
//   buf   : Memory address of the data to be written.
//
//   count : Number of bytes to write.
//
// x86-64 syscall convention:
//   rax = syscall number (1)
//   rdi = fd
//   rsi = buf
//   rdx = count
//
// Special notes:
//   rcx and r11 are modified by the syscall instruction and must be
//   declared as clobbered registers.
//
// Educational note:
//   This wrapper intentionally exposes how userspace communicates
//   with the Linux kernel instead of hiding it behind libc.
//
//=============================================================================

void sys_write(
    int fd,
    const char *buf,
    long count
)
{
    syscall3(
        SYS_write,
        fd,
        (long)buf,
        count
    );
}

//=============================================================================
// SYSTEM CALL: sys_read
//
// Purpose:
//   Provides a thin wrapper around the Linux read() system call.
//
// Why this exists:
//   Since this shell has zero libc dependency, input must be obtained
//   directly from the Linux kernel instead of using functions such as
//   scanf(), fgets() or getchar().
//
// Kernel interaction:
//   read(fd, buf, count)
//
// Parameters:
//   fd    : File descriptor to read from.
//
//           0 -> stdin
//
//   buf   : Buffer where incoming bytes are stored.
//
//   count : Maximum number of bytes to read.
//
// Return value:
//   > 0 : Number of bytes read
//   = 0 : End of file (EOF)
//   < 0 : Error
//
// Educational note:
//   This wrapper explicitly exposes how userspace receives data from
//   the Linux kernel without relying on libc.
//
//=============================================================================

long sys_read(
    int fd,
    char *buf,
    long count
)
{
    return syscall3(
        SYS_read,
        fd,
        (long)buf,
        count
    );
}


//=============================================================================
// SYSTEM CALL: sys_exit
//
// Purpose:
//   Terminates the current process and returns a status code to
//   the operating system.
//
// Why this exists:
//   Normally, libc provides exit(). Since this shell does not use
//   libc, process termination must be requested directly from the
//   Linux kernel.
//
// Kernel interaction:
//   exit(status)
//
// Parameters:
//   status : Process exit code.
//            0 -> successful termination
//            non-zero -> abnormal termination
//
// x86-64 syscall convention:
//   rax = syscall number (60)
//   rdi = status
//
// Why while(1) exists:
//   sys_exit() never returns because the kernel destroys the process.
//   The infinite loop is a safety measure to prevent undefined
//   behaviour if execution ever continues unexpectedly.
//
// Educational note:
//   This function demonstrates that process termination is ultimately
//   a kernel operation rather than a language feature.
//
//=============================================================================

void sys_exit(int status)
{
    syscall1(
        SYS_exit,
        status
    );

    while(1);
}