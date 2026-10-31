#ifndef PRIVATE111_DIRECT_SHM_H
#define PRIVATE111_DIRECT_SHM_H

#include <stdint.h>

/*
 * Shared-memory contracts for the private CarPlay type-111 direct-display path.
 *
 * The H264 ring is written from dio_manager by libcarplay_altscreen.so at the
 * already-proven ScreenStreamProcessData interception point.  A separate
 * sidecar may consume it without ever reading Window58.
 *
 * The decoded-frame ring is an optional stock-OMX fallback/diagnostic bridge.
 * It is filled from the private CScreenRender::render callback before the stock
 * renderer posts anything.  The direct-display sidecar can use this path while
 * an independent hardware decoder backend is being validated.
 */

#define P111_H264_SHM_NAME       "/carplay111_h264"
#define P111_H264_SHM_MAGIC      0x50313148u /* P11H */
#define P111_H264_SHM_VERSION    1u
#define P111_H264_RING_SIZE      (4u * 1024u * 1024u)
#define P111_H264_RECORD_MAGIC   0x48323634u /* H264 */

#define P111_H264_FLAG_ACTIVE    0x00000001u
#define P111_H264_FLAG_ANNEXB    0x00000002u
#define P111_H264_FLAG_SPS       0x00000004u
#define P111_H264_FLAG_PPS       0x00000008u
#define P111_H264_FLAG_IDR       0x00000010u
#define P111_H264_FLAG_AVCC      0x00000020u
#define P111_H264_FLAG_CONFIG    0x00000040u
#define P111_H264_FLAG_FRAME     0x00000080u
#define P111_H264_FLAG_WRAP      0x80000000u

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t payload_bytes;
    uint32_t flags;
} p111_h264_record_t;

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t active;
    volatile uint32_t generation;
    volatile uint32_t writer_pid;
    volatile uint32_t stream_cookie;

    volatile uint32_t write_pos;
    volatile uint32_t write_seq;
    volatile uint32_t total_bytes;
    volatile uint32_t packet_count;
    volatile uint32_t drop_count;
    volatile uint32_t wrap_count;
    volatile uint32_t last_payload_bytes;

    volatile uint32_t sps_count;
    volatile uint32_t pps_count;
    volatile uint32_t idr_count;
    volatile uint32_t annexb_count;

    uint8_t ring[P111_H264_RING_SIZE];
} p111_h264_shm_t;

#define P111_FRAME_SHM_NAME      "/carplay111_decoded"
#define P111_FRAME_SHM_MAGIC     0x50313146u /* P11F */
#define P111_FRAME_SHM_VERSION   1u
#define P111_FRAME_SLOTS         3u
#define P111_FRAME_SLOT_BYTES    (2u * 1024u * 1024u)

#define P111_FRAME_FORMAT_NV12   1u
#define P111_FRAME_FLAG_ACTIVE   0x00000001u

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t active;
    volatile uint32_t generation;
    volatile uint32_t writer_pid;
    volatile uint32_t stream_cookie;

    volatile uint32_t width;
    volatile uint32_t height;
    volatile uint32_t stride;
    volatile uint32_t format;
    volatile uint32_t frame_bytes;

    volatile uint32_t sequence;
    volatile uint32_t current_slot;
    volatile uint32_t frame_count;
    volatile uint32_t drop_count;
    volatile uint32_t last_copy_bytes;

    uint8_t data[P111_FRAME_SLOTS * P111_FRAME_SLOT_BYTES];
} p111_frame_shm_t;

#endif
