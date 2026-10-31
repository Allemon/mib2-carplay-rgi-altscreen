/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_PTHREAD_H
#define QSHIM_PTHREAD_H
typedef unsigned long pthread_t;
/* Deliberately no mutex/once shim: QNX uses multiword sync objects, not the
 * former fabricated one-word definitions. Add only with a target ABI proof. */
extern int pthread_create(pthread_t *thread, const void *attr,
                          void *(*entry)(void *), void *arg);
extern unsigned long pthread_self(void);
#endif
