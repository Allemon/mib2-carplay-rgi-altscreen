#ifndef RGI_SUPERVISOR_CLOCK_H
#define RGI_SUPERVISOR_CLOCK_H
#include <stdio.h>
#include <time.h>

/* Query before renderer/lease initialization; do not open a display or port. */
static int rgi_supervisor_monotonic_seconds(void) {
    struct timespec stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0 || stamp.tv_sec < 0)
        return 2;
    return printf("%lu\n", (unsigned long)stamp.tv_sec) < 0 ? 2 : 0;
}
#endif
