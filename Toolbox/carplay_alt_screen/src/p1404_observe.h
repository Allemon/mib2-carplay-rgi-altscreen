/*
 * p1404_observe.h - observe-only evidence capture for the AirPlay control plane.
 *
 * Rules this module exists to enforce:
 *   - control-plane calls may be dumped in full (bounded by the iAP2 hex helper);
 *   - the video hot path may only count and summarise, never hex dump per frame;
 *   - every marker carries profile, time, thread and caller context so a single
 *     vehicle run can be reconstructed afterwards;
 *   - logging must never be able to crash CarPlay: no allocation, no recursion into
 *     the bearer, and all buffers are static.
 */
#ifndef P1404_OBSERVE_H
#define P1404_OBSERVE_H

#include <stddef.h>
#include <stdint.h>

/* Per ScreenStream instance census: what was created, how much data arrived,
 * and a heuristic H.264 NAL mix. slot 0 is the first stream seen, which on P1404
 * is the stock MainScreen 110 path. */
struct obs_stream {
    void        *stream;
    uint64_t     creates;
    uint64_t     data_calls;
    uint64_t     bytes;
    uint64_t     nal_sps;
    uint64_t     nal_pps;
    uint64_t     nal_idr;
    uint64_t     nal_other;
    uint64_t     first_at;
    uint64_t     last_at;
    unsigned long first_thread;
    void        *first_caller;
};

/* Log entry/exit context for a control-plane hook. lr is the caller return address,
 * rc the value produced by the real implementation, name the hook label. */
void obs_call(const char *name, void *self, void *lr, long rc);

/* Return the live census slot for a stream, creating it on first sight. */
struct obs_stream *obs_stream_see(void *stream, void *lr, int created);

/* Count video bytes with a heuristic NAL classification. Hot path: no dumping. */
void obs_stream_data(void *stream, const void *data, size_t len);

/* Periodic summary, safe to call from the hot path at a low rate. */
void obs_streams_report(const char *why);

/* Current monotonic-ish timestamp in microseconds and the calling thread id. */
uint64_t     obs_now_us(void);
unsigned long obs_thread_id(void);
#endif