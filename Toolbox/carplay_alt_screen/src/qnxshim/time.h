/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_TIME_H
#define QSHIM_TIME_H
typedef long time_t;
typedef long suseconds_t;
typedef long clock_t;
struct timeval { long tv_sec; long tv_usec; };
struct timespec { long tv_sec; long tv_nsec; };
#define CLOCK_MONOTONIC 2
extern int clock_gettime(int clock_id, struct timespec *ts);
extern time_t time(time_t *t);
extern int gettimeofday(struct timeval *tv, void *tz);
extern clock_t clock(void);
#endif
