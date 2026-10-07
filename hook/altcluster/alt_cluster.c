/*
 * AltScreen cluster commands - see alt_cluster.h.
 *
 * AirPlay session commands are CFDictionaries {type, params} passed to
 * AirPlayReceiverSessionSendCommand(session, request, completion, ctx), exactly as
 * stock AirPlayReceiverSessionRequestUI builds its request.  The CF-lite calls come
 * from libairplay at runtime (dlsym), so the hook still links only libc/libz/socket.
 *
 * iOS side (iOS 26.1 DashBoard / CarKit):
 *   changeMapZoomLevel  -> DBInstrumentClusterRootViewController
 *                          session:receivedUnhandledRemoteEvent:withPayload:
 *                          payload uuid == cluster display hardwareIdentifier,
 *                          zoomDirection -> CRSUIClusterZoomAction
 *   showUI              -> CARSession showUI {uuid, url}
 */

#include "../framework/common.h"
#include "../framework/logging.h"
#include "../framework/bus.h"
#include "../framework/bus_protocol.h"
#include "alt_cluster.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

DEFINE_LOG_MODULE(ALTCLUSTER);

#define CF_STRING_ENCODING_UTF8 0x08000100u  /* libairplay passes the same value */
#define ALT_MAX_STEPS_PER_EVENT 4
#define ALT_MAX_URL_LEN         512

typedef const void* CFTypeRef;
typedef int32_t (*session_init_f)(void* session);
typedef void    (*session_fini_f)(void* session);
typedef int32_t (*send_command_f)(void* session, CFTypeRef request, void* completion, void* ctx);
typedef void*   (*dict_create_f)(CFTypeRef alloc, long capacity, CFTypeRef key_cb, CFTypeRef value_cb);
typedef void    (*dict_set_f)(void* dict, CFTypeRef key, CFTypeRef value);
typedef int32_t (*dict_set_i64_f)(void* dict, CFTypeRef key, int64_t value);
typedef CFTypeRef (*string_create_f)(CFTypeRef alloc, const char* cstr, uint32_t encoding);
typedef void    (*release_f)(CFTypeRef cf);

static struct {
    send_command_f send;
    dict_create_f dict_create;
    dict_set_f dict_set;
    dict_set_i64_f dict_set_i64;
    string_create_f string_create;
    release_f release;
    CFTypeRef key_callbacks;
    CFTypeRef value_callbacks;
    const CFTypeRef* bool_true;     /* kCFLBooleanTrue / kCFLBooleanFalse are variables */
    const CFTypeRef* bool_false;
    int resolved;   /* 0 = not tried, 1 = ok, -1 = unavailable */
} g_cf;

/* g_session is written by the platform hooks and read by senders.  A sender holds
 * g_lock across SendCommand, and Finalize takes g_lock before stock frees the
 * session, so a command can only reach a live session.  SendCommand only queues an
 * HTTP message (no cross-thread wait), but its own CFRelease could be the last one
 * and run Finalize on the sending thread: the lock is recursive for that case. */
static pthread_mutex_t g_lock;
static pthread_once_t g_lock_once = PTHREAD_ONCE_INIT;
static void* g_session;
static uint32_t g_session_generation;

static void alt_lock_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void alt_lock(void)   { pthread_once(&g_lock_once, alt_lock_init); pthread_mutex_lock(&g_lock); }
static void alt_unlock(void) { pthread_mutex_unlock(&g_lock); }

static int alt_resolve_cf(void) {
    if (g_cf.resolved) return g_cf.resolved > 0;
    g_cf.send = (send_command_f)dlsym(RTLD_DEFAULT, "AirPlayReceiverSessionSendCommand");
    g_cf.dict_create = (dict_create_f)dlsym(RTLD_DEFAULT, "CFDictionaryCreateMutable");
    g_cf.dict_set = (dict_set_f)dlsym(RTLD_DEFAULT, "CFDictionarySetValue");
    g_cf.dict_set_i64 = (dict_set_i64_f)dlsym(RTLD_DEFAULT, "CFDictionarySetInt64");
    g_cf.string_create = (string_create_f)dlsym(RTLD_DEFAULT, "CFStringCreateWithCString");
    g_cf.release = (release_f)dlsym(RTLD_DEFAULT, "CFRelease");
    g_cf.key_callbacks = dlsym(RTLD_DEFAULT, "kCFLDictionaryKeyCallBacksCFLTypes");
    g_cf.value_callbacks = dlsym(RTLD_DEFAULT, "kCFLDictionaryValueCallBacksCFLTypes");
    g_cf.bool_true = (const CFTypeRef*)dlsym(RTLD_DEFAULT, "kCFLBooleanTrue");
    g_cf.bool_false = (const CFTypeRef*)dlsym(RTLD_DEFAULT, "kCFLBooleanFalse");
    if (g_cf.send && g_cf.dict_create && g_cf.dict_set && g_cf.dict_set_i64 &&
        g_cf.string_create && g_cf.release && g_cf.key_callbacks && g_cf.value_callbacks) {
        g_cf.resolved = 1;
    } else {
        g_cf.resolved = -1;
        LOG_WARN(LOG_MODULE, "libairplay CF/command symbols unavailable; cluster commands disabled");
    }
    return g_cf.resolved > 0;
}

/* ---- session tracking (stock platform hooks) ---------------------------- */

HOOK_EXPORT int32_t AirPlayReceiverSessionPlatformInitialize(void* session) {
    static session_init_f real;
    if (!real) real = (session_init_f)dlsym(RTLD_NEXT, "AirPlayReceiverSessionPlatformInitialize");
    if (!real) return -6700;   /* kUnknownErr: nothing to forward to */
    int32_t err = real(session);
    if (err == 0 && session) {
        alt_lock();
        g_session = session;
        g_session_generation++;
        uint32_t gen = g_session_generation;
        alt_unlock();
        LOG_INFO(LOG_MODULE, "AirPlay session %p initialized (gen=%u)", session, gen);
        (void)gen;
    }
    return err;
}

HOOK_EXPORT void AirPlayReceiverSessionPlatformFinalize(void* session) {
    static session_fini_f real;
    if (!real) real = (session_fini_f)dlsym(RTLD_NEXT, "AirPlayReceiverSessionPlatformFinalize");
    alt_lock();
    int was_current = (session && session == g_session);
    if (was_current) g_session = NULL;
    alt_unlock();
    if (was_current) LOG_INFO(LOG_MODULE, "AirPlay session %p finalized", session);
    if (real) real(session);
}

/* ---- day/night from the head unit's own calls ------------------------------- */

/* dio_manager pushes its day/night mode (and every light-switch change) through
 * AirPlayReceiverSessionSetNightMode(session, Boolean).  Only observed here: the call is forwarded
 * with all four argument registers untouched, so the stock behaviour (and any signature detail
 * beyond the first two arguments) is preserved. */
typedef int32_t (*set_night_f)(void* session, uintptr_t a, uintptr_t b, uintptr_t c);

HOOK_EXPORT int32_t AirPlayReceiverSessionSetNightMode(void* session, uintptr_t a, uintptr_t b, uintptr_t c) {
    static set_night_f real;
    if (!real) real = (set_night_f)dlsym(RTLD_NEXT, "AirPlayReceiverSessionSetNightMode");
    if (!real) return -6700;
    if (hook_process_is_dio_manager()) {
        alt_cluster_set_night((a & 0xFF) != 0);
        LOG_INFO(LOG_MODULE, "head unit SetNightMode(%s)", (a & 0xFF) ? "night" : "day");
    }
    return real(session, a, b, c);
}

/* ---- command sending ----------------------------------------------------- */

/* Build {type: <type>, params: {uuid: <cluster>, <key>: <int or string>}} and send it
 * to the live session.  Exactly one of str_value / (int_key with int_value) is used. */
static int32_t alt_send(const char* type, const char* key, const char* str_value,
                        int use_int, int64_t int_value) {
    if (!alt_resolve_cf()) return -6735;   /* kUnsupportedErr */

    CFTypeRef s_type_key = g_cf.string_create(NULL, "type", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_params_key = g_cf.string_create(NULL, "params", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_uuid_key = g_cf.string_create(NULL, "uuid", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_key = g_cf.string_create(NULL, key, CF_STRING_ENCODING_UTF8);
    CFTypeRef s_type = g_cf.string_create(NULL, type, CF_STRING_ENCODING_UTF8);
    CFTypeRef s_uuid = g_cf.string_create(NULL, ALT_CLUSTER_DISPLAY_UUID, CF_STRING_ENCODING_UTF8);
    CFTypeRef s_value = str_value ? g_cf.string_create(NULL, str_value, CF_STRING_ENCODING_UTF8) : NULL;
    void* request = g_cf.dict_create(NULL, 0, g_cf.key_callbacks, g_cf.value_callbacks);
    void* params = g_cf.dict_create(NULL, 0, g_cf.key_callbacks, g_cf.value_callbacks);

    int32_t err = -6728;   /* kNoMemoryErr */
    if (s_type_key && s_params_key && s_uuid_key && s_key && s_type && s_uuid &&
        request && params && (use_int || s_value)) {
        g_cf.dict_set(params, s_uuid_key, s_uuid);
        if (use_int) g_cf.dict_set_i64(params, s_key, int_value);
        else         g_cf.dict_set(params, s_key, s_value);
        g_cf.dict_set(request, s_type_key, s_type);
        g_cf.dict_set(request, s_params_key, params);

        alt_lock();
        void* session = g_session;
        err = session ? g_cf.send(session, request, NULL, NULL) : -6709;   /* kNotFoundErr */
        alt_unlock();
    }

    CFTypeRef owned[] = { s_type_key, s_params_key, s_uuid_key, s_key, s_type, s_uuid,
                          s_value, request, params };
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); i++)
        if (owned[i]) g_cf.release(owned[i]);
    return err;
}


/* ---- day/night sync -------------------------------------------------------- */

static int g_night = -1;   /* guarded by g_lock */

void alt_cluster_set_night(int night) {
    alt_lock();
    g_night = night < 0 ? -1 : (night ? 1 : 0);
    alt_unlock();
}

int alt_cluster_night(void) {
    alt_lock();
    int n = g_night;
    alt_unlock();
    return n;
}

/* The stock shape: {type: setNightMode, params: {nightMode: <bool>}}, no display UUID. */
static int32_t alt_send_night(int night) {
    if (!alt_resolve_cf()) return -6735;
    if (!g_cf.bool_true || !g_cf.bool_false || !*g_cf.bool_true || !*g_cf.bool_false) return -6735;
    CFTypeRef s_type_key = g_cf.string_create(NULL, "type", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_params_key = g_cf.string_create(NULL, "params", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_night_key = g_cf.string_create(NULL, "nightMode", CF_STRING_ENCODING_UTF8);
    CFTypeRef s_type = g_cf.string_create(NULL, "setNightMode", CF_STRING_ENCODING_UTF8);
    void* request = g_cf.dict_create(NULL, 0, g_cf.key_callbacks, g_cf.value_callbacks);
    void* params = g_cf.dict_create(NULL, 0, g_cf.key_callbacks, g_cf.value_callbacks);
    int32_t err = -6728;   /* kNoMemoryErr */
    if (s_type_key && s_params_key && s_night_key && s_type && request && params) {
        g_cf.dict_set(params, s_night_key, night ? *g_cf.bool_true : *g_cf.bool_false);
        g_cf.dict_set(request, s_type_key, s_type);
        g_cf.dict_set(request, s_params_key, params);
        alt_lock();
        void* session = g_session;
        err = session ? g_cf.send(session, request, NULL, NULL) : -6709;   /* kNotFoundErr */
        alt_unlock();
    }
    CFTypeRef owned[] = { s_type_key, s_params_key, s_night_key, s_type, request, params };
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); i++)
        if (owned[i]) g_cf.release(owned[i]);
    return err;
}

/* A phone that already believes "day" ignores another setNightMode(day), and Google Maps'
 * cluster UI starts dark regardless; only a real change makes it re-evaluate (the car's own
 * light switch sends night then day, which is why that "fixes" it).  So the re-sync is a
 * short kick: the opposite mode, then the real one, ALT_NIGHT_KICK_DELAY_US apart. */
#ifndef ALT_NIGHT_KICK_DELAY_US
#define ALT_NIGHT_KICK_DELAY_US 300000
#endif

static int g_kick_busy;   /* guarded by g_lock */

static void* alt_night_kick_thread(void* arg) {
    int night = (int)(intptr_t)arg;
    int32_t err = alt_send_night(!night);
    if (err == 0) {
        usleep(ALT_NIGHT_KICK_DELAY_US);
        err = alt_send_night(night);   /* the real mode always goes last */
    } else {
        err = alt_send_night(night);   /* kick not possible: at least send the real mode */
    }
    if (err != 0) LOG_WARN(LOG_MODULE, "setNightMode %s failed err=%d", night ? "night" : "day", (int)err);
    else          LOG_INFO(LOG_MODULE, "cluster day/night re-sent: %s (kicked via %s)",
                           night ? "night" : "day", night ? "day" : "night");
    alt_lock();
    g_kick_busy = 0;
    alt_unlock();
    return NULL;
}

static void alt_on_night(uint16_t type, uint8_t flags, const uint8_t* payload, uint32_t len, void* ctx) {
    (void)type; (void)flags; (void)payload; (void)len; (void)ctx;
    if (access(ALT_NIGHT_SYNC_OFF, F_OK) == 0) {
        LOG_INFO(LOG_MODULE, "night sync disabled by %s", ALT_NIGHT_SYNC_OFF);
        return;
    }
    int night = alt_cluster_night();
    if (night < 0) {
        LOG_WARN(LOG_MODULE, "night sync: no nightMode seen from the head unit; nothing sent");
        return;
    }
    alt_lock();
    int busy = g_kick_busy;
    if (!busy) g_kick_busy = 1;
    alt_unlock();
    if (busy) return;   /* a kick is already running */
    pthread_t t;
    if (pthread_create(&t, NULL, alt_night_kick_thread, (void*)(intptr_t)night) != 0) {
        alt_lock(); g_kick_busy = 0; alt_unlock();
        LOG_WARN(LOG_MODULE, "night sync: cannot start the kick thread");
        return;
    }
    pthread_detach(t);
}

static void alt_on_zoom(uint16_t type, uint8_t flags, const uint8_t* payload, uint32_t len, void* ctx) {
    (void)type; (void)flags; (void)ctx;
    if (len < 1) return;
    int step = (int8_t)payload[0];
    if (step == 0) return;
    int dir = alt_zoom_direction_for_step(step);
    int count = step < 0 ? -step : step;
    if (count > ALT_MAX_STEPS_PER_EVENT) count = ALT_MAX_STEPS_PER_EVENT;
    for (int i = 0; i < count; i++) {
        int32_t err = alt_send("changeMapZoomLevel", "zoomDirection", NULL, 1, dir);
        if (err != 0) {
            LOG_WARN(LOG_MODULE, "changeMapZoomLevel dir=%d failed err=%d", dir, (int)err);
            return;
        }
    }
    LOG_INFO(LOG_MODULE, "changeMapZoomLevel dir=%s x%d", dir == ALT_ZOOM_OUT ? "out" : "in", count);
}

static void alt_on_ui_context(uint16_t type, uint8_t flags, const uint8_t* payload, uint32_t len, void* ctx) {
    (void)type; (void)flags; (void)ctx;
    char url[ALT_MAX_URL_LEN];
    if (len == 0 || len >= sizeof(url)) {
        LOG_WARN(LOG_MODULE, "showUI url length %u rejected", (unsigned)len);
        return;
    }
    memcpy(url, payload, len);
    url[len] = '\0';
    int32_t err = alt_send("showUI", "url", url, 0, 0);
    if (err != 0) LOG_WARN(LOG_MODULE, "showUI %s failed err=%d", url, (int)err);
    else          LOG_INFO(LOG_MODULE, "showUI %s", url);
}

static void alt_cluster_init(void) {
    if (!hook_process_is_dio_manager()) return;
    bus_on(CMD_ALT_ZOOM, alt_on_zoom, NULL);
    bus_on(CMD_ALT_UICTX, alt_on_ui_context, NULL);
    bus_on(CMD_ALT_NIGHT, alt_on_night, NULL);
    LOG_INFO(LOG_MODULE, "cluster commands ready (display %s)", ALT_CLUSTER_DISPLAY_UUID);
}

static void alt_cluster_shutdown(void) {
    bus_off(CMD_ALT_ZOOM);
    bus_off(CMD_ALT_UICTX);
    bus_off(CMD_ALT_NIGHT);
}

const hook_module_def_t alt_cluster_module_def = {
    .name = "altcluster",
    .priority = HOOK_PRIORITY_LOW,
    .on_init = alt_cluster_init,
    .on_shutdown = alt_cluster_shutdown,
};
