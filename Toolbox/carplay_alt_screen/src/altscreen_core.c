/* altscreen_core.c - state, logging and the profile driven identification patch. */
#include "altscreen_core.h"
#include "altscreen_profile.h"
#include "altscreen_paths.h"
#include "p1404_iap2.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

static struct altscreen_state g_state;
static unsigned long g_log_seq;
static char g_run_id[80];
static int g_run_id_loaded;

#define ALT_LOG_QUEUE_SLOTS 256u
#define ALT_LOG_LINE_BYTES 768u
#define ALT_LOG_MAX_BYTES (16L * 1024L * 1024L)
#define ALT_LOG_LOCK_SPINS 32
struct alt_log_entry { char line[ALT_LOG_LINE_BYTES]; };
static struct alt_log_entry g_log_queue[ALT_LOG_QUEUE_SLOTS];
static unsigned g_log_head;
static unsigned g_log_tail;
static volatile unsigned g_log_guard;
static volatile unsigned long g_log_dropped;
static volatile unsigned g_log_accepting;
static int g_log_thread_started;
static int g_log_limit_hit;

/*
 * Cluster pixel geometry is deliberately unset here. The native adapter reads
 * SCREEN_PROPERTY_SIZE from target display id 1 at runtime and publishes that
 * exact size before type 111 is advertised. There is no fixed pixel fallback.
 */
static const struct altscreen_display g_main = {
    "5E2D6D6C-8E32-4E2A-9A74-000000001080",
    CP_STREAM_MAIN_SCREEN, 1024, 480, 220, 103, 30
};
static struct altscreen_display g_cluster = {
    "b7e6c5a0-2222-4000-8000-000000000002",
    CP_STREAM_ALT_SCREEN, 0, 0, 200, 0, 30
};

static void run_id_unknown(void) {
    /* Keep this loader within the intentionally small P1404 libc shim. */
    strncpy(g_run_id, "UNKNOWN", sizeof(g_run_id));
    g_run_id[sizeof(g_run_id) - 1] = 0;
}

static void load_run_id_once(void) {
    char path[ALTSCREEN_PATH_MAX];
    FILE *f;
    size_t n;
    if (g_run_id_loaded) return;
    g_run_id_loaded = 1;
    run_id_unknown();
    if (!altscreen_state_path("run_id", path, sizeof(path))) return;
    f = fopen(path, "r");
    if (!f) return;
    /* fgets/strcpy are deliberately absent from the minimal P1404 shims. The
     * state file contains one short token; reserve one byte and read it bounded. */
    n = fread(g_run_id, 1, sizeof(g_run_id) - 1, f);
    if (n > 0) {
        g_run_id[n] = 0;
        while (n && (g_run_id[n - 1] == '\n' || g_run_id[n - 1] == '\r' ||
                     g_run_id[n - 1] == ' ' || g_run_id[n - 1] == '\t'))
            g_run_id[--n] = 0;
        if (!n) run_id_unknown();
    } else {
        run_id_unknown();
    }
    fclose(f);
}

const char *altscreen_run_id(void) {
    load_run_id_once();
    return g_run_id;
}

static int log_try_lock(void) {
    int i;
    for (i = 0; i < ALT_LOG_LOCK_SPINS; ++i)
        if (__sync_lock_test_and_set(&g_log_guard, 1u) == 0u) return 1;
    return 0;
}

static void log_unlock(void) { __sync_lock_release(&g_log_guard); }

static void *altscreen_log_writer(void *unused) {
    FILE *f = NULL;
    char line[ALT_LOG_LINE_BYTES];
    unsigned long reported_drops = 0;
    (void)unused;
    for (;;) {
        int have = 0;
        if (log_try_lock()) {
            if (g_log_tail != g_log_head) {
                memcpy(line, g_log_queue[g_log_tail].line, sizeof(line));
                g_log_tail = (g_log_tail + 1u) % ALT_LOG_QUEUE_SLOTS;
                have = 1;
            }
            log_unlock();
        }
        if (!have) {
            usleep(10000u);
            continue;
        }
        if (!f) {
            const char *volatile_path = altscreen_log_path();
            const char *sd_path;
            f = fopen(volatile_path, "a");
            if (!f) {
                sd_path = altscreen_sd_log_path();
                if (sd_path && strcmp(sd_path, volatile_path) != 0) f = fopen(sd_path, "a");
            }
        }
        if (!f) {
            /* Evidence loss is fail-open by design.  This is the dedicated
             * writer thread: producer/control/CarPlay threads never wait for
             * /tmp or SD I/O and no logging failure changes product state. */
            __sync_add_and_fetch(&g_log_dropped, 1UL);
            usleep(100000u);
            continue;
        }
        if (g_log_limit_hit) {
            __sync_add_and_fetch(&g_log_dropped, 1UL);
            continue;
        }
        if (ftell(f) >= ALT_LOG_MAX_BYTES) {
            fprintf(f, "[ALTSCREEN] ERROR LOG_LIMIT_REACHED max_bytes=%ld run_id=%s evidence_incomplete=1\n",
                    ALT_LOG_MAX_BYTES, altscreen_run_id());
            fflush(f);
            g_log_limit_hit = 1;
            /* The file cannot accept more evidence. Stop producer formatting
             * as well as disk writes; queued lines are drained by this worker. */
            __sync_lock_test_and_set(&g_log_accepting, 0u);
            continue;
        }
        fputs(line, f);
        if (g_log_dropped != reported_drops) {
            reported_drops = g_log_dropped;
            fprintf(f, "[ALTSCREEN] LOG_QUEUE_DROPPED total=%lu capacity=%u run_id=%s\n",
                    reported_drops, ALT_LOG_QUEUE_SLOTS - 1u, altscreen_run_id());
        }
        fflush(f);
    }
    return NULL;
}

int altscreen_log_start_async(void) {
    pthread_t thread;
    if (g_log_thread_started) return 1;
    if (pthread_create(&thread, NULL, altscreen_log_writer, NULL) != 0) return 0;
    g_log_thread_started = 1;
    return 1;
}

void altscreen_log(const char *fmt, ...) {
    va_list ap;
    struct timeval tv;
    unsigned long seq;
    unsigned long tid;
    unsigned next;
    unsigned long long ts_us = 0;
    int used;
    char *line;

    /* Dependency constructors and pre-READY stock forwarders may reach logging
     * helpers before the worker initializes paths. Drop those messages without
     * touching procfs, the SD card, pthread state, or the queue. The worker
     * re-reports every resolved binding after altscreen_init enables logging. */
    if (__sync_fetch_and_add(&g_log_accepting, 0u) == 0u) return;

    /* Producers perform only bounded memory work. Disk latency and rotation are
     * isolated to altscreen_log_writer; contention/full queue increments a
     * visible drop counter and immediately returns to stock CarPlay. */
    if (!log_try_lock()) { __sync_add_and_fetch(&g_log_dropped, 1UL); return; }
    next = (g_log_head + 1u) % ALT_LOG_QUEUE_SLOTS;
    if (next == g_log_tail) {
        log_unlock();
        __sync_add_and_fetch(&g_log_dropped, 1UL);
        return;
    }
    line = g_log_queue[g_log_head].line;
    if (gettimeofday(&tv, NULL) == 0)
        ts_us = (unsigned long long)tv.tv_sec * 1000000ULL + (unsigned long long)tv.tv_usec;
    seq = __sync_add_and_fetch(&g_log_seq, 1UL);
    tid = (unsigned long)pthread_self();
    used = snprintf(line, ALT_LOG_LINE_BYTES,
                    "[ALTSCREEN] seq=%lu epoch_us=%llu pid=%ld tid=%lu run_id=%s ",
                    seq, ts_us, (long)getpid(), tid, altscreen_run_id());
    if (used < 0) used = 0;
    if ((unsigned)used >= ALT_LOG_LINE_BYTES - 2u) used = (int)ALT_LOG_LINE_BYTES - 2;
    va_start(ap, fmt);
    (void)vsnprintf(line + used, ALT_LOG_LINE_BYTES - (unsigned)used - 1u, fmt, ap);
    va_end(ap);
    line[ALT_LOG_LINE_BYTES - 2u] = 0;
    used = (int)strlen(line);
    if ((unsigned)used >= ALT_LOG_LINE_BYTES - 1u) used = (int)ALT_LOG_LINE_BYTES - 2;
    line[used++] = '\n';
    line[used] = 0;
    g_log_head = next;
    log_unlock();
}

void altscreen_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_log_seq = 0;
    g_log_head = 0;
    g_log_tail = 0;
    g_log_guard = 0;
    g_log_dropped = 0;
    g_log_thread_started = 0;
    g_log_limit_hit = 0;
    g_run_id_loaded = 0;
    g_run_id[0] = 0;
    (void)altscreen_prepare_volatile_root();
    __sync_lock_test_and_set(&g_log_accepting, 1u);
    altscreen_log("PHASE=HOOK_INIT log=%s profile=%s main=%ux%u cluster=%ux%u stream_alt=%u",
                  altscreen_log_path(), altscreen_profile_current()->name,
                  g_main.width_pixels, g_main.height_pixels,
                  g_cluster.width_pixels, g_cluster.height_pixels, g_cluster.stream_type);
}

int altscreen_iap2_add_theme_assets(uint8_t *buf, size_t *len, size_t capacity) {
    const struct altscreen_profile *p = altscreen_profile_current();
    int r;
    if (!buf || !len) return -1;
    r = altscreen_tlv_advertise(buf, len, capacity, p);
    if (r > 0 || (r == 0 && p->modifies_bytes))
        altscreen_mark_theme_advertised();
    else
        altscreen_log("IAP2 advertise profile=%s result=%d no_bytes_changed=1", p->name, r);
    return r;
}

const char *altscreen_profile_name(void) { return altscreen_profile_current()->name; }

void altscreen_mark_theme_advertised(void) {
    if (g_state.theme_advertised) return;
    g_state.theme_advertised = 1;
    altscreen_log("PHASE=IAP2_THEME_SENT THEME_ADVERTISED=YES profile=%s",
                  altscreen_profile_current()->name);
}

void altscreen_mark_theme_available(int available) {
    if (available == CP_AVAIL_MALFORMED) {
        altscreen_log("PHASE=IAP2_PHONE_AVAILABILITY THEME_AVAILABLE=MALFORMED");
        return;
    }
    if (available == CP_AVAIL_UNKNOWN) {
        altscreen_log("PHASE=IAP2_PHONE_AVAILABILITY THEME_AVAILABLE=UNKNOWN");
        return;
    }
    g_state.theme_available = available ? 1 : 0;
    if (available)
        altscreen_log("PHASE=IAP2_PHONE_AVAILABILITY THEME_AVAILABLE=YES profile=%s",
                      altscreen_profile_current()->name);
    else
        altscreen_log("PHASE=IAP2_PHONE_AVAILABILITY THEME_AVAILABLE=NO profile=%s",
                      altscreen_profile_current()->name);
}

void altscreen_mark_info_alt_display(void) {
    if (g_state.info_alt_display) return;
    g_state.info_alt_display = 1;
    altscreen_log("PHASE=AIRPLAY_INFO_111 INFO_ALT_DISPLAY=YES streamType=%u uuid=%s geom=%ux%u@%u",
                  g_cluster.stream_type, g_cluster.uuid, g_cluster.width_pixels,
                  g_cluster.height_pixels, g_cluster.max_fps);
}

void altscreen_mark_info_alt_feature_advertised(void) {
    if (g_state.info_alt_feature_advertised) return;
    g_state.info_alt_feature_advertised = 1;
    altscreen_log("PHASE=AIRPLAY_INFO_FEATURE INFO_ALT_FEATURE_ADVERTISED=YES bit=0x%lx",
                  (unsigned long)CP_FEATURE_ALTSCREEN);
}

void altscreen_mark_phone_requested(void) {
    if (g_state.phone_requested_altscreen) return;
    g_state.phone_requested_altscreen = 1;
    altscreen_log("PHASE=PHONE_REQUEST_111 PHONE_REQUESTED_ALTSCREEN=YES");
}

void altscreen_mark_alt_feature_negotiated(void) {
    if (g_state.alt_feature_negotiated) return;
    g_state.alt_feature_negotiated = 1;
    altscreen_log("PHASE=SETUP_ALT_ACCEPTED ALT_FEATURE_NEGOTIATED=YES proof=enabledFeatures_intersection");
}

void altscreen_mark_ui_active(const char *proof) {
    if (g_state.alt_ui_active) return;
    g_state.alt_ui_active = 1;
    altscreen_log("PHASE=ALT_UI_ACTIVE ALT_UI_SHOW_FOCUS=YES proof=%s", proof ? proof : "-");
}

void altscreen_mark_stream_setup(uint32_t stream_type) {
    if (stream_type == CP_STREAM_ALT_SCREEN) {
        if (!g_state.stream_111_setup) {
            g_state.stream_111_setup = 1;
            altscreen_log("PHASE=STREAM_111_READY STREAM_111_SETUP=YES streamType=111");
        }
    } else {
        altscreen_log("STREAM_SETUP streamType=%u", stream_type);
    }
}

void altscreen_mark_video_config(const char *proof) {
    if (g_state.alt_video_config_rx) return;
    g_state.alt_video_config_rx = 1;
    altscreen_log("PHASE=VIDEO_111_CONFIG ALT_VIDEO_CONFIG_RX=YES proof=%s", proof ? proof : "-");
}

void altscreen_mark_video(uint32_t stream_type, size_t bytes) {
    if (stream_type != CP_STREAM_ALT_SCREEN || bytes == 0) return;
    g_state.alt_video_rx = 1;
    g_state.alt_video_packets++;
    g_state.alt_video_bytes += bytes;
    /* Log the first private packet, then only every 256 packets so the
     * SD card cannot become the bottleneck on ScreenStreamProcessData. */
    if (g_state.alt_video_packets == 1) {
        altscreen_log("PHASE=VIDEO_111_RX ALT_VIDEO_RX=1 ALT_VIDEO_BYTES=%lu ALT_VIDEO_PACKETS=%lu last=%u",
                      (unsigned long)g_state.alt_video_bytes,
                      (unsigned long)g_state.alt_video_packets, (unsigned)bytes);
    } else if ((g_state.alt_video_packets & 255u) == 0) {
        altscreen_log("PHASE=VIDEO_111_PROGRESS ALT_VIDEO_BYTES=%lu ALT_VIDEO_PACKETS=%lu last=%u",
                      (unsigned long)g_state.alt_video_bytes,
                      (unsigned long)g_state.alt_video_packets, (unsigned)bytes);
    }
}

void altscreen_mark_decoder_ready(const char *proof) {
    if (g_state.alt_decoder_ready) return;
    g_state.alt_decoder_ready = 1;
    altscreen_log("PHASE=DECODER_111_READY ALT_DECODER_READY=YES proof=%s", proof ? proof : "-");
}

void altscreen_mark_cockpit_visible(const char *proof) {
    if (g_state.cockpit_altscreen) return;
    g_state.cockpit_altscreen = 1;
    altscreen_log("PHASE=COCKPIT_111_VISIBLE COCKPIT_ALTSCREEN=YES proof=%s", proof ? proof : "-");
}

void altscreen_mark_cockpit_hidden(const char *proof) {
    if (!g_state.cockpit_altscreen && !g_state.alt_ui_active) return;
    g_state.cockpit_altscreen = 0;
    g_state.alt_ui_active = 0;
    altscreen_log("PHASE=COCKPIT_111_HIDDEN COCKPIT_ALTSCREEN=NO ALT_UI_SHOW_FOCUS=NO proof=%s",
                  proof ? proof : "-");
}

const struct altscreen_state *altscreen_get_state(void) { return &g_state; }
const struct altscreen_display *altscreen_main_display(void) { return &g_main; }
const struct altscreen_display *altscreen_cluster_display(void) { return &g_cluster; }
int altscreen_set_cluster_geometry(uint32_t width, uint32_t height) {
    if (!width || !height || width > 8192u || height > 8192u) return 0;
    g_cluster.width_pixels = width;
    g_cluster.height_pixels = height;
    altscreen_log("PHASE=NATIVE_111_GEOMETRY_PUBLISHED display=1 size=%ux%u fixed_fallback=0",
                  width, height);
    return 1;
}
