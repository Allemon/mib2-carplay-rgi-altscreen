#ifndef QSHIM_FCNTL_H
#define QSHIM_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
extern int open(const char *path, int oflag, ...);
#endif
