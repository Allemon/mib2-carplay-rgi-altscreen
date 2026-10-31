/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_STRING_H
#define QSHIM_STRING_H
#include <stddef.h>
extern void *memcpy(void *d, const void *s, size_t n);
extern void *memmove(void *d, const void *s, size_t n);
extern void *memset(void *s, int c, size_t n);
extern int memcmp(const void *a, const void *b, size_t n);
extern void *memchr(const void *s, int c, size_t n);
extern size_t strlen(const char *s);
extern size_t strnlen(const char *s, size_t n);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern char *strstr(const char *h, const char *n);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, size_t n);
extern char *strncpy(char *d, const char *s, size_t n);
extern char *strerror(int errnum);
#endif
