/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_UNISTD_H
#define QSHIM_UNISTD_H
#include <stddef.h>
#include <stdint.h>
typedef long ssize_t;
typedef int32_t off_t;
extern ssize_t read(int fd, void *buf, size_t n);
extern ssize_t write(int fd, const void *buf, size_t n);
extern off_t lseek(int fd, off_t offset, int whence);
extern int close(int fd);
extern int unlink(const char *path);
extern int dup(int fd);
extern int open(const char *path, int oflag, ...);
extern int access(const char *path, int mode);
extern ssize_t readlink(const char *path, char *buf, size_t bufsiz);
extern ssize_t readlinkat(int fd, const char *path, char *buf, size_t bufsiz);
extern int getpid(void);
extern int kill(int pid, int sig);
extern unsigned int sleep(unsigned int s);
extern int usleep(unsigned int useconds);
extern long int sysconf(int name);
extern char *getcwd(char *buf, size_t size);
#define F_OK 0
#define R_OK 4
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#endif
