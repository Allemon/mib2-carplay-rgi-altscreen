/* p1404_observe.c - synchronized bounded AirPlay observer census. */
#include "p1404_observe.h"
#include "altscreen_core.h"
#include "altscreen_profile.h"
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>

#define OBS_SLOTS        8
#define OBS_CALL_CAP     400
#define OBS_CALL_EVERY   256
#define OBS_DATA_EVERY   256
#define OBS_IDLE_SECONDS 5u
#define OBS_FULL_EVERY   4096u

static struct obs_stream g_streams[OBS_SLOTS];
static unsigned g_calls;
static uint64_t g_stream_seen[OBS_SLOTS];
static unsigned g_full_calls;
static volatile unsigned g_obs_guard;

static void obs_lock(void) {
    while (__sync_lock_test_and_set(&g_obs_guard, 1u) != 0u) usleep(1000u);
}
static void obs_unlock(void) { __sync_lock_release(&g_obs_guard); }

/* 32 bit arithmetic and constant shifts only: compiler runtime helpers not
 * exported by P1404 must never appear in the target ELF. */
uint64_t obs_now_us(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)(unsigned long)tv.tv_sec;
}

unsigned long obs_thread_id(void) { return pthread_self(); }

void obs_call(const char *name, void *self, void *lr, long rc) {
    unsigned n;
    if (!name) return;
    obs_lock();
    n = ++g_calls;
    obs_unlock();
    if (n <= OBS_CALL_CAP || (n % OBS_CALL_EVERY) == 0)
        altscreen_log("CALL %s self=%p lr=%p rc=%ld thread=%lu seq=%u profile=%s",
                      name, self, lr, rc, obs_thread_id(), n,
                      altscreen_profile_current()->name);
}

static struct obs_stream *stream_see_locked(void *stream, void *lr, int created,
                                             int *is_new) {
    struct obs_stream *slot = NULL;
    uint64_t now;
    int oldest = 0;
    int i;
    if (is_new) *is_new = 0;
    if (!stream) return NULL;
    now = obs_now_us(); /* seconds; observer timestamps use no ARM divide helper */
    for (i = 0; i < OBS_SLOTS; ++i) {
        if (g_streams[i].stream == stream) { slot = &g_streams[i]; break; }
        if (!g_streams[i].stream && !slot) slot = &g_streams[i];
    }
    if (!slot) {
        /* This is a bounded diagnostic census, not ownership of ScreenStreams.
         * Retire an idle entry without dereferencing its possibly freed pointer.
         * Keep active entries; saturation must never change video forwarding. */
        for (i = 1; i < OBS_SLOTS; ++i)
            if (g_stream_seen[i] < g_stream_seen[oldest]) oldest = i;
        if (now >= g_stream_seen[oldest] &&
            now - g_stream_seen[oldest] >= OBS_IDLE_SECONDS)
            slot = &g_streams[oldest];
    }
    if (!slot) return NULL;
    if (slot->stream != stream) {
        memset(slot, 0, sizeof(*slot));
        slot->stream = stream;
        slot->first_at = obs_now_us();
        slot->first_thread = obs_thread_id();
        slot->first_caller = lr;
        if (is_new) *is_new = 1;
    }
    if (created) ++slot->creates;
    g_stream_seen[slot - g_streams] = now;
    return slot;
}

/* Called under the census lock; one shared budget for see+data prevents the
 * two observations of each frame from becoming two per-frame error messages. */
static int stream_full_log_locked(void) {
    unsigned n = ++g_full_calls;
    return n == 1u || (n % OBS_FULL_EVERY) == 0u;
}

struct obs_stream *obs_stream_see(void *stream, void *lr, int created) {
    struct obs_stream *slot;
    struct obs_stream snap;
    int is_new = 0, full_log = 0;
    if (!stream) return NULL;
    obs_lock();
    slot = stream_see_locked(stream, lr, created, &is_new);
    if (slot) memcpy(&snap, slot, sizeof(snap));
    else full_log = stream_full_log_locked();
    obs_unlock();
    if (!slot) {
        if (full_log)
            altscreen_log("STREAM census full ptr=%p statistics_only=1 video_forwarding_unchanged=1 rate_limited=1", stream);
        return NULL;
    }
    if (is_new)
        altscreen_log("STREAM new ptr=%p thread=%lu caller=%p profile=%s", stream,
                      snap.first_thread, lr, altscreen_profile_current()->name);
    if (created)
        altscreen_log("STREAM create ptr=%p n=%u thread=%lu caller=%p", stream,
                      (unsigned)snap.creates, obs_thread_id(), lr);
    return slot;
}

/* Heuristic leading-NAL classifier for the generic census only. Private111
 * verdicts use the carry-aware full start-code scanner in altscreen_state.c. */
static void classify(struct obs_stream *s, const uint8_t *d, size_t n) {
    size_t off;
    unsigned nal;
    if (n < 5) return;
    if (d[0] == 0 && d[1] == 0 && d[2] == 1) off = 3;
    else if (n > 4 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1) off = 4;
    else return;
    nal = (unsigned)(d[off] & 0x1f);
    if (nal == 7) ++s->nal_sps;
    else if (nal == 8) ++s->nal_pps;
    else if (nal == 5) ++s->nal_idr;
    else ++s->nal_other;
}

void obs_stream_data(void *stream, const void *data, size_t len) {
    struct obs_stream *s;
    struct obs_stream snap;
    int is_new = 0, should_log = 0, full_log = 0;
    if (!stream || !data || !len) return;
    obs_lock();
    s = stream_see_locked(stream, NULL, 0, &is_new);
    if (s) {
        s->bytes += len;
        s->data_calls++;
        s->last_at = obs_now_us();
        classify(s, (const uint8_t *)data, len);
        should_log = (s->data_calls % OBS_DATA_EVERY) == 1;
        memcpy(&snap, s, sizeof(snap));
    } else full_log = stream_full_log_locked();
    obs_unlock();
    if (!s) {
        if (full_log)
            altscreen_log("STREAM census full ptr=%p statistics_only=1 video_forwarding_unchanged=1 rate_limited=1", stream);
        return;
    }
    if (is_new)
        altscreen_log("STREAM new ptr=%p thread=%lu caller=%p profile=%s", stream,
                      snap.first_thread, snap.first_caller,
                      altscreen_profile_current()->name);
    if (should_log)
        altscreen_log("STREAM data ptr=%p calls=%u bytes=%u MB=%u sps=%u pps=%u idr=%u other=%u",
                      stream, (unsigned)snap.data_calls, (unsigned)snap.bytes,
                      (unsigned)(unsigned long)(snap.bytes >> 20),
                      (unsigned)snap.nal_sps, (unsigned)snap.nal_pps,
                      (unsigned)snap.nal_idr, (unsigned)snap.nal_other);
}

void obs_streams_report(const char *why) {
    struct obs_stream snapshots[OBS_SLOTS];
    unsigned calls;
    int i, live = 0;
    const char *tag = why ? why : "-";
    obs_lock();
    memcpy(snapshots, g_streams, sizeof(snapshots));
    calls = g_calls;
    obs_unlock();
    for (i = 0; i < OBS_SLOTS; ++i) {
        struct obs_stream *s = &snapshots[i];
        if (!s->stream) continue;
        ++live;
        altscreen_log("CENSUS %s slot=%d ptr=%p creates=%u calls=%u bytes=%u MB=%u sps=%u pps=%u idr=%u other=%u thread=%lu caller=%p",
                      tag, i, s->stream, (unsigned)s->creates,
                      (unsigned)s->data_calls, (unsigned)s->bytes,
                      (unsigned)(unsigned long)(s->bytes >> 20),
                      (unsigned)s->nal_sps, (unsigned)s->nal_pps,
                      (unsigned)s->nal_idr, (unsigned)s->nal_other,
                      s->first_thread, s->first_caller);
    }
    altscreen_log("CENSUS %s live_streams=%d control_calls=%u profile=%s",
                  tag, live, calls, altscreen_profile_current()->name);
}
