#include "p1404_lock_wait.h"
#include <stdlib.h>
/* Included after fullchain_teardown_admitted. Each task holds the receiver,
 * request and a guard reservation until cleanup, so timeout cannot lose the
 * request or forget that another full teardown already completed. */
#ifndef P1404_DEFERRED_TEARDOWN_H
#define P1404_DEFERRED_TEARDOWN_H
#define ALT_DEFERRED_TEARDOWN_SLOTS 32u
struct fullchain_deferred_teardown {
    void *receiver;
    const void *params;
    int reason, full;
    int dynamic, thread_started;
    uint64_t last_start_ms;
    struct fullchain_deferred_teardown *next;
    volatile int ready;
    struct alt_session_lease fence;
    int audio_epochs_valid;
    uint32_t audio_epochs[256];
};
static struct fullchain_deferred_teardown deferred_teardowns[ALT_DEFERRED_TEARDOWN_SLOTS];
static volatile unsigned deferred_teardown_lock;
static struct fullchain_deferred_teardown *deferred_overflow;

static void deferred_teardown_acquire(void) {
    while (__sync_lock_test_and_set(&deferred_teardown_lock, 1u)) p1404_lock_wait_yield();
}
static void deferred_teardown_release(void) {
    __sync_lock_release(&deferred_teardown_lock);
}

static uint64_t deferred_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}
static void deferred_discard(struct fullchain_deferred_teardown *job) {
    int dynamic = job->dynamic;
    deferred_teardown_acquire();
    if (dynamic) {
        struct fullchain_deferred_teardown **p = &deferred_overflow;
        while (*p && *p != job) p = &(*p)->next;
        if (*p) *p = job->next;
    } else memset(job, 0, sizeof(*job));
    deferred_teardown_release();
    if (dynamic) free(job);
}
static int fullchain_unregistered_stop_pending(void *session) {
    unsigned i; int pending = 0;
    struct fullchain_deferred_teardown *job;
    deferred_teardown_acquire();
    for (i = 0; i < ALT_DEFERRED_TEARDOWN_SLOTS; ++i) {
        job = &deferred_teardowns[i];
        if (job->receiver == session && !job->fence.registered) pending = 1;
    }
    for (job = deferred_overflow; job; job = job->next)
        if (job->receiver == session && !job->fence.registered) pending = 1;
    deferred_teardown_release();
    return pending;
}

static void *fullchain_deferred_teardown_worker(void *arg) {
    struct fullchain_deferred_teardown *job = arg;
    struct alt_session_lease lease;
    unsigned char done = 0;
    int admission, finished = 0;
    unsigned retries = 0;
    while (!job->ready) usleep(1000u);
    __sync_synchronize();
    do {
        admission = fullchain_lifecycle_enter(job->receiver, ALT_SESSION_DEFERRED_TEARDOWN, &lease);
        if (admission == ALT_SESSION_ENTERED) {
            const void *request = job->params;
            void *owned = NULL;
            int filter = job->audio_epochs_valid ? fullchain_audio_filter(job->params, &lease, job->audio_epochs, &request, &owned) : 1;
            if (filter == 1) finished = fullchain_teardown_admitted(job->receiver, request, job->reason, &done, &lease);
            else {
                finished = filter == 2 || filter == 3;
                fullchain_lifecycle_leave(job->receiver, &lease, 0);
            }
            if (owned) alt_airplay_release_object(owned);
        }
        else {
            done = (unsigned char)lease.completed;
            finished = lease.completed;
            fullchain_lifecycle_leave(job->receiver, &lease, 0);
        }
        if (!finished) {
            ++retries;
            if (retries == 1u || (retries % 60u) == 0u)
                altscreen_log("WARN PHASE=TEARDOWN_DEFERRED_RETRY receiver=%p attempts=%u resources_retained=1", job->receiver, retries);
            /* One retained job, no recursive queue growth or busy loop. A
             * failed join/Stop may become safe later; never force deletion. */
            usleep(1000000u);
        }
    } while (!finished);
    altscreen_log("PHASE=TEARDOWN_DEFERRED_FINISH receiver=%p admission=%d done=%d",
                  job->receiver, admission, (int)done);
    deferred_teardown_acquire(); job->ready = 2; deferred_teardown_release();
    if (job->params) alt_airplay_release_object((void *)job->params);
    fullchain_lifecycle_leave(job->receiver, &job->fence, 0);
    deferred_discard(job);
    return NULL;
}

static void deferred_start(struct fullchain_deferred_teardown *job) {
    pthread_t thread;
    int rc;
    void *receiver = job->receiver;
    int (*create_fn)(pthread_t *, const void *, void *(*)(void *), void *) =
        (int (*)(pthread_t *, const void *, void *(*)(void *), void *))dlsym(RTLD_DEFAULT, "pthread_create");
    int (*detach_fn)(pthread_t) = (int (*)(pthread_t))dlsym(RTLD_DEFAULT, "pthread_detach");
    rc = create_fn && detach_fn ? create_fn(&thread, NULL, fullchain_deferred_teardown_worker, job) : -1;
    if (rc) {
        deferred_teardown_acquire(); job->thread_started = 0; deferred_teardown_release();
        altscreen_log("WARN PHASE=TEARDOWN_DEFERRED_QUEUE receiver=%p thread_rc=%d request_retained=1 maintenance_retry=1", receiver, rc);
    } else {
        rc = detach_fn(thread);
        if (rc) altscreen_log("WARN PHASE=TEARDOWN_DEFERRED_DETACH receiver=%p rc=%d", receiver, rc);
    }
}
void fullchain_deferred_retry_tick(void) {
    struct fullchain_deferred_teardown *job = NULL, *p;
    unsigned i; uint64_t now = deferred_now_ms();
    deferred_teardown_acquire();
    for (i = 0; i < ALT_DEFERRED_TEARDOWN_SLOTS; ++i) {
        p = &deferred_teardowns[i];
        if (p->ready == 1 && !p->thread_started && now - p->last_start_ms >= 1000u) { job = p; break; }
    }
    if (!job) for (p = deferred_overflow; p; p = p->next)
        if (p->ready == 1 && !p->thread_started && now - p->last_start_ms >= 1000u) { job = p; break; }
    if (job) { job->thread_started = 1; job->last_start_ms = now; }
    deferred_teardown_release();
    if (job) deferred_start(job);
}

static int fullchain_defer_teardown(void *receiver, const void *params, int reason,
                                   struct alt_session_lease *fence) {
    struct fullchain_deferred_teardown *job = NULL;
    int full;
    unsigned scope;
    unsigned i;
    scope = fullchain_teardown_scope(params, &full);

    deferred_teardown_acquire();
    for (i = 0; i < ALT_DEFERRED_TEARDOWN_SLOTS; ++i) {
        struct fullchain_deferred_teardown *current = &deferred_teardowns[i];
        if (current->ready == 1 && current->receiver == receiver && current->full) {
            deferred_teardown_release();
            return 1; /* existing retained request covers this stop */
        }
        if (!current->receiver && !job) job = current;
    }
    if (!job) {
        struct fullchain_deferred_teardown *p;
        for (p = deferred_overflow; p; p = p->next) {
            if (p->ready == 1 && p->receiver == receiver && p->full) {
                deferred_teardown_release(); return 1;
            }
        }
        deferred_teardown_release();
        job = calloc(1, sizeof(*job));
        deferred_teardown_acquire();
        if (job) { job->dynamic = 1; job->next = deferred_overflow; deferred_overflow = job; }
    }
    if (job) {
        job->receiver = receiver;
        job->params = params;
        job->reason = reason;
        job->full = full;
        job->fence = *fence;
    }
    deferred_teardown_release();
    if (!job) {
        altscreen_log("ERROR PHASE=TEARDOWN_DEFERRED_QUEUE receiver=%p reason=allocation_failed retry_required=1", receiver);
        return 0;
    }
    if (params && !alt_airplay_retain_object((void *)params)) {
        deferred_discard(job);
        return 0;
    }
    if (!full && !scope && fence->registered) job->audio_epochs_valid = fullchain_audio_snapshot(params, fence, job->audio_epochs);
    /* Unknown stream schemas remain serialized rather than allowing a delayed
     * teardown whose target cannot be distinguished from a replacement. */
    if (fence->registered && !alt_session_guard_defer(fence, full || (scope & (TEARDOWN_MAIN | TEARDOWN_ALT)) || !job->audio_epochs_valid)) {
        if (params) alt_airplay_release_object((void *)params);
        deferred_discard(job);
        return 0;
    }
    job->fence = *fence;
    /* Transfer ownership before spawning. Failed thread creation leaves the
     * request and reservation in this backlog for the maintenance worker. */
    memset(fence, 0, sizeof(*fence));
    deferred_teardown_acquire();
    job->thread_started = 1; job->last_start_ms = deferred_now_ms(); job->ready = 1;
    deferred_teardown_release();
    altscreen_log("PHASE=TEARDOWN_DEFERRED_QUEUE receiver=%p full=%d request_retained=1 guard_reservation_held=1", receiver, full);
    deferred_start(job);
    return 1;
}

static void fullchain_retry_cleanup(void *receiver, const void *params, int reason,
                                   struct alt_session_lease *owner) {
    struct alt_session_lease reservation;
    if (!alt_airplay_retain_object(receiver)) return;
    if (!alt_session_guard_reserve_retry(owner, &reservation)) {
        alt_airplay_release_object(receiver);
        return;
    }
    reservation.retained = 1;
    if (!fullchain_defer_teardown(receiver, params, reason, &reservation))
        altscreen_log("ERROR PHASE=TEARDOWN_CLEANUP_RETRY receiver=%p queued=0", receiver);
    fullchain_lifecycle_leave(receiver, &reservation, 0);
}
#endif
