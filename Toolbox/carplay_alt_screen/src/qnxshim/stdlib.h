/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_STDLIB_H
#define QSHIM_STDLIB_H
#include <stddef.h>
extern void *malloc(size_t n);
extern void *calloc(size_t n, size_t s);
extern void *realloc(void *p, size_t n);
extern void free(void *p);
extern int atoi(const char *s);
extern long int strtol(const char *n, char **end, int base);
extern size_t wcstombs(char *d, const void *s, size_t n);
extern char *getenv(const char *name);
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);
extern int system(const char *command);
#endif