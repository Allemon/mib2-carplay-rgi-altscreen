#ifndef P1404_STREAM_RECOVERY_H
#define P1404_STREAM_RECOVERY_H

#include <stdint.h>
#include <string.h>

/* Short modular-us32 intervals, independent of wall-clock second changes.
 * A static map may legitimately send no frames: probe only twice, never
 * tear down a session or treat command acceptance as a recovered picture. */
#define ALT111_RECOVERY_IDLE_US 5000000u
#define ALT111_RECOVERY_RETRY_US 8000000u
#define ALT111_RECOVERY_ACK_US 4000000u
#define ALT111_RECOVERY_STABLE_US 2000000u
#define ALT111_RECOVERY_MAX_ATTEMPTS 2u
#define ALT111_RECOVERY_REQUEST 1u
#define ALT111_RECOVERY_TIMEOUT 2u
#define ALT111_RECOVERY_PROGRESS 4u
#define ALT111_RECOVERY_EXHAUSTED 8u

struct alt111_stream_recovery {
    uint32_t frame_generation, frame_count;
    uint32_t last_progress_at, last_submit_at, stable_at, stable_count;
    uint32_t command_seq, inflight_seq;
    unsigned attempts;
    int initialized, waiting_frame, exhausted_logged, have_submit;
};

/* Called under native_lock, including result(). Sequence survives producer
 * generation changes, so an old completion cannot satisfy a new attempt. */
static unsigned alt111_recovery_poll(struct alt111_stream_recovery *s,
        uint32_t now, uint32_t generation, uint32_t count, int eligible) {
    unsigned result = 0;
    uint32_t seq;
    if (!generation || !count) return 0;
    if (!s->initialized || s->frame_generation != generation) {
        seq = s->command_seq;
        memset(s, 0, sizeof(*s));
        s->command_seq = seq;
        s->initialized = 1;
        s->frame_generation = generation;
        s->frame_count = s->stable_count = count;
        s->last_progress_at = s->stable_at = now;
        return 0;
    }
    if (s->frame_count != count) {
        if ((uint32_t)(now - s->last_progress_at) >= ALT111_RECOVERY_IDLE_US) {
            s->stable_at = now;
            s->stable_count = count;
        }
        if (s->waiting_frame) result |= ALT111_RECOVERY_PROGRESS;
        s->waiting_frame = 0;
        s->inflight_seq = 0;
        s->frame_count = count;
        s->last_progress_at = now;
        /* One isolated frame must not replenish the request budget. */
        if ((uint32_t)(now - s->stable_at) >= ALT111_RECOVERY_STABLE_US &&
            (uint32_t)(count - s->stable_count) >= 30u) {
            s->attempts = 0;
            s->exhausted_logged = 0;
        }
        return result;
    }
    if (s->inflight_seq &&
        (uint32_t)(now - s->last_submit_at) >= ALT111_RECOVERY_ACK_US) {
        s->inflight_seq = 0;
        result |= ALT111_RECOVERY_TIMEOUT;
    }
    if (!eligible || (uint32_t)(now - s->last_progress_at) < ALT111_RECOVERY_IDLE_US)
        return result;
    if (s->have_submit &&
        (uint32_t)(now - s->last_submit_at) < ALT111_RECOVERY_RETRY_US)
        return result;
    if (s->attempts >= ALT111_RECOVERY_MAX_ATTEMPTS) {
        if (!s->exhausted_logged) {
            s->exhausted_logged = 1;
            result |= ALT111_RECOVERY_EXHAUSTED;
        }
        return result;
    }
    ++s->command_seq;
    if (!s->command_seq) ++s->command_seq;
    s->inflight_seq = s->command_seq;
    s->last_submit_at = now;
    s->have_submit = 1;
    ++s->attempts;
    s->waiting_frame = 1;
    return result | ALT111_RECOVERY_REQUEST;
}

/* Local admission/submission refusal did not reach the phone. Refund its
 * budget while retaining the backoff timestamp and fencing late callbacks. */
static int alt111_recovery_submit_failed(struct alt111_stream_recovery *s, uint32_t seq) {
    if (!seq || s->inflight_seq != seq) return 0;
    s->inflight_seq = 0;
    s->waiting_frame = 0;
    if (s->attempts) --s->attempts;
    return 1;
}

/* The first cluster ownership of a stream starts the presented-picture budget
 * afresh. Probes spent while the display was still waiting for its first
 * frames must not leave the visible session without recovery. The backoff
 * timestamp and any in-flight fence are kept. */
static void alt111_recovery_rearm(struct alt111_stream_recovery *s) {
    s->attempts = 0;
    s->exhausted_logged = 0;
}

static int alt111_recovery_result(struct alt111_stream_recovery *s, uint32_t seq) {
    if (!seq || s->inflight_seq != seq) return 0;
    s->inflight_seq = 0;
    return 1; /* Progress, not this ACK, establishes recovery. */
}

#endif
