/* Exact errno access used by the freestanding ARM/QNX build. */
#ifndef QSHIM_ERRNO_H
#define QSHIM_ERRNO_H
extern int *__get_errno_ptr(void);
#define errno   (*__get_errno_ptr())
#define ENOENT  2
#define EINTR   4
#define EIO     5
#define EBADF   9
#define ECHILD  10
#define ENOMEM  12
#define EACCES  13
#define ENOTDIR 20
#define EINVAL  22
#define ETIMEDOUT 260
#endif
