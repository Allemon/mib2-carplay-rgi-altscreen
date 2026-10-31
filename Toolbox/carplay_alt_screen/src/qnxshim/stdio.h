/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_STDIO_H
#define QSHIM_STDIO_H
#include <stddef.h>
typedef struct _QFILE FILE;
extern FILE *fopen(const char *path, const char *mode);
extern int fclose(FILE *stream);
extern int fflush(FILE *stream);
extern int fgetc(FILE *stream);
extern int fputc(int c, FILE *stream);
extern char *fgets(char *s, int size, FILE *stream);
extern int fputs(const char *s, FILE *stream);
extern int fprintf(FILE *stream, const char *fmt, ...);
extern int vfprintf(FILE *stream, const char *fmt, __builtin_va_list ap);
extern size_t fread(void *ptr, size_t size, size_t n, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t n, FILE *stream);
extern int fseek(FILE *stream, long offset, int whence);
extern long ftell(FILE *stream);
extern int snprintf(char *s, size_t n, const char *fmt, ...);
extern int vsnprintf(char *s, size_t n, const char *fmt, __builtin_va_list ap);
extern int sprintf(char *s, const char *fmt, ...);
extern int sscanf(const char *s, const char *fmt, ...);
extern FILE *popen(const char *command, const char *mode);
extern int pclose(FILE *stream);
/* Standard streams are intentionally undeclared: a null FILE* is not a QNX
 * standard stream. Production code uses explicit FILE handles only. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#ifndef EOF
#define EOF (-1)
#endif
