#ifndef P1404_MONO_US32_H
#define P1404_MONO_US32_H
#include <stdint.h>
#include <time.h>
#include <errno.h>

/* Process-local short durations only. GPS/manual clock corrections must not
 * change frame age, readback duration or wheel/recovery pacing. Both the tap
 * and its control-plane consumers must use this exact clock and modulus. */
static inline uint32_t p1404_monotonic_us32(void) {
    struct timespec ts;
    int saved_errno = errno;
    uint32_t value = 0u;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        value = (uint32_t)ts.tv_sec * 1000000u + (uint32_t)ts.tv_nsec / 1000u;
    errno = saved_errno;
    return value;
}

/* The older lifecycle fields use seconds, despite obs_now_us()'s name.
 * Keep their units and thresholds while isolating them from wall time. */
static inline uint64_t p1404_monotonic_seconds(void) {
    struct timespec ts;
    int saved_errno = errno;
    uint64_t value = 0u;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        value = (uint64_t)(unsigned long)ts.tv_sec;
    errno = saved_errno;
    return value;
}
#endif
