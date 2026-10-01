/*
 * AltScreen cluster view area - see alt_viewarea.h.
 *
 * AltScreen appends the cluster display to the displays array libairplay reports to
 * the iPhone (AirPlayReceiverSessionScreen_CopyDisplaysInfo), with one view area the
 * size of the whole 1440x542 frame and a safe area of the same size.  Apple Maps
 * centres the vehicle in the safe area; apps such as Amap / Baidu ignore the safe area
 * and lay out against the view area only, with the vehicle right of its centre.  The
 * view area is the only rectangle every app honours, so this module rewrites it (and
 * the safe area inside it) from /mnt/app/root/hooks/cluster_viewarea.cfg:
 *
 *     view <x> <y> <w> <h>
 *     safe <x> <y> <w> <h>        (optional; default = whole view area)
 *
 * The rectangles are written into AltScreen's own (mutable) dictionaries, so nothing
 * of its schema changes.  The iPhone reads the displays at connect: a new file applies
 * on the next phone connection.  No file, or an invalid one, leaves AltScreen's values.
 *
 * libairplay calls CopyDisplaysInfo through its PLT, so this interposer runs when
 * libcarplay_hook.so precedes libcarplay_altscreen.so in LD_PRELOAD (carplay_startup.sh);
 * RTLD_NEXT then reaches AltScreen's interposer, which calls stock.
 */

#include "../framework/common.h"
#include "../framework/logging.h"
#include "alt_cluster.h"
#include "alt_viewarea.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

DEFINE_LOG_MODULE(ALTVIEW);

#define CF_STRING_ENCODING_UTF8 0x08000100u

typedef const void* CFTypeRef;
typedef CFTypeRef (*copy_displays_f)(void* session, int32_t* err);
typedef long      (*array_count_f)(CFTypeRef array);
typedef CFTypeRef (*array_at_f)(CFTypeRef array, long index);
typedef CFTypeRef (*dict_get_f)(CFTypeRef dict, CFTypeRef key);
typedef int32_t   (*dict_set_i64_f)(void* dict, CFTypeRef key, int64_t value);
typedef int       (*string_get_f)(CFTypeRef str, char* buf, long size, uint32_t encoding);
typedef CFTypeRef (*string_create_f)(CFTypeRef alloc, const char* cstr, uint32_t encoding);
typedef int64_t   (*dict_get_i64_f)(CFTypeRef dict, CFTypeRef key, int32_t* err);
typedef unsigned long (*type_id_f)(CFTypeRef cf);
typedef unsigned long (*type_id0_f)(void);
typedef void      (*release_f)(CFTypeRef cf);

static struct {
    array_count_f array_count;
    array_at_f array_at;
    dict_get_f dict_get;
    dict_set_i64_f dict_set_i64;
    string_get_f string_get;
    string_create_f string_create;
    dict_get_i64_f dict_get_i64;
    type_id_f type_of;
    type_id0_f array_type, dict_type, string_type;
    release_f release;
    int resolved;
} g;

static const char* g_cfg_path = ALT_VIEWAREA_CFG;

void alt_viewarea_set_config_path(const char* path) { g_cfg_path = path ? path : ALT_VIEWAREA_CFG; }

static int resolve(void) {
    if (g.resolved) return g.resolved > 0;
    g.array_count = (array_count_f)dlsym(RTLD_DEFAULT, "CFArrayGetCount");
    g.array_at = (array_at_f)dlsym(RTLD_DEFAULT, "CFArrayGetValueAtIndex");
    g.dict_get = (dict_get_f)dlsym(RTLD_DEFAULT, "CFDictionaryGetValue");
    g.dict_set_i64 = (dict_set_i64_f)dlsym(RTLD_DEFAULT, "CFDictionarySetInt64");
    g.string_get = (string_get_f)dlsym(RTLD_DEFAULT, "CFStringGetCString");
    g.string_create = (string_create_f)dlsym(RTLD_DEFAULT, "CFStringCreateWithCString");
    g.dict_get_i64 = (dict_get_i64_f)dlsym(RTLD_DEFAULT, "CFDictionaryGetInt64");
    g.type_of = (type_id_f)dlsym(RTLD_DEFAULT, "CFGetTypeID");
    g.array_type = (type_id0_f)dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");
    g.dict_type = (type_id0_f)dlsym(RTLD_DEFAULT, "CFDictionaryGetTypeID");
    g.string_type = (type_id0_f)dlsym(RTLD_DEFAULT, "CFStringGetTypeID");
    g.release = (release_f)dlsym(RTLD_DEFAULT, "CFRelease");
    g.resolved = (g.array_count && g.array_at && g.dict_get && g.dict_set_i64 && g.string_get &&
                  g.string_create && g.dict_get_i64 && g.type_of && g.array_type && g.dict_type &&
                  g.string_type && g.release) ? 1 : -1;
    if (g.resolved < 0) LOG_WARN(LOG_MODULE, "libairplay CF symbols unavailable; view area untouched");
    return g.resolved > 0;
}

/* ---- config ------------------------------------------------------------- */

int alt_viewarea_parse(const char* text, alt_rect_t* view, alt_rect_t* safe, int* have_safe) {
    int got_view = 0;
    *have_safe = 0;
    const char* line = text;
    while (line && *line) {
        alt_rect_t r;
        char word[8];
        if (sscanf(line, "%7s %d %d %d %d", word, &r.x, &r.y, &r.w, &r.h) == 5) {
            if (strcmp(word, "view") == 0) { *view = r; got_view = 1; }
            else if (strcmp(word, "safe") == 0) { *safe = r; *have_safe = 1; }
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return got_view;
}

/* view inside the display, safe inside the view (safe is relative to the view area). */
int alt_viewarea_valid(const alt_rect_t* view, const alt_rect_t* safe, int64_t disp_w, int64_t disp_h) {
    if (view->x < 0 || view->y < 0 || view->w < 160 || view->h < 120) return 0;
    if (view->x + view->w > disp_w || view->y + view->h > disp_h) return 0;
    if (safe->x < 0 || safe->y < 0 || safe->w < 80 || safe->h < 80) return 0;
    if (safe->x + safe->w > view->w || safe->y + safe->h > view->h) return 0;
    return 1;
}

static int load_config(alt_rect_t* view, alt_rect_t* safe, int* have_safe) {
    FILE* f = fopen(g_cfg_path, "r");
    if (!f) return 0;
    char text[256];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return alt_viewarea_parse(text, view, safe, have_safe);
}

/* ---- dictionary access ---------------------------------------------------- */

static CFTypeRef key(const char* name) { return g.string_create(NULL, name, CF_STRING_ENCODING_UTF8); }

static CFTypeRef get(CFTypeRef dict, const char* name) {
    CFTypeRef k = key(name);
    if (!k) return NULL;
    CFTypeRef v = g.dict_get(dict, k);
    g.release(k);
    return v;
}

static int is(CFTypeRef cf, type_id0_f type) { return cf && g.type_of(cf) == type(); }

/* AirPlay CFUtils: 0 on success. */
static int get_i64(CFTypeRef dict, const char* name, int64_t* out) {
    CFTypeRef k = key(name);
    if (!k) return 0;
    int32_t err = -1;
    *out = g.dict_get_i64(dict, k, &err);
    g.release(k);
    return err == 0;
}

static void set_rect(void* dict, const alt_rect_t* r) {
    static const char* names[4] = { "originXPixels", "originYPixels", "widthPixels", "heightPixels" };
    int64_t values[4] = { r->x, r->y, r->w, r->h };
    for (int i = 0; i < 4; i++) {
        CFTypeRef k = key(names[i]);
        if (!k) continue;
        g.dict_set_i64(dict, k, values[i]);
        g.release(k);
    }
}

static CFTypeRef find_cluster(CFTypeRef displays) {
    if (!is(displays, g.array_type)) return NULL;
    long count = g.array_count(displays);
    for (long i = 0; i < count; i++) {
        CFTypeRef d = g.array_at(displays, i);
        if (!is(d, g.dict_type)) continue;
        CFTypeRef uuid = get(d, "uuid");
        char buf[64];
        if (is(uuid, g.string_type) && g.string_get(uuid, buf, sizeof(buf), CF_STRING_ENCODING_UTF8)
                && strcmp(buf, ALT_CLUSTER_DISPLAY_UUID) == 0)
            return d;
    }
    return NULL;
}

void alt_viewarea_apply(CFTypeRef displays) {
    alt_rect_t view, safe;
    int have_safe;
    if (!load_config(&view, &safe, &have_safe)) return;     /* no file: AltScreen's values */
    if (!resolve()) return;
    CFTypeRef cluster = find_cluster(displays);
    if (!cluster) {
        LOG_INFO(LOG_MODULE, "cluster display not in displays info; view area untouched");
        return;
    }
    int64_t disp_w = 0, disp_h = 0;
    CFTypeRef areas = get(cluster, "viewAreas");
    CFTypeRef area = is(areas, g.array_type) && g.array_count(areas) > 0 ? g.array_at(areas, 0) : NULL;
    CFTypeRef safe_dict = is(area, g.dict_type) ? get(area, "safeArea") : NULL;
    if (!get_i64(cluster, "widthPixels", &disp_w) || !get_i64(cluster, "heightPixels", &disp_h)
            || !is(area, g.dict_type) || !is(safe_dict, g.dict_type)) {
        LOG_WARN(LOG_MODULE, "unexpected AltScreen cluster schema; view area untouched");
        return;
    }
    if (!have_safe) { safe.x = 0; safe.y = 0; safe.w = view.w; safe.h = view.h; }
    if (!alt_viewarea_valid(&view, &safe, disp_w, disp_h)) {
        LOG_WARN(LOG_MODULE, "%s: view %d,%d %dx%d / safe %d,%d %dx%d outside %lldx%lld; untouched",
                 g_cfg_path, view.x, view.y, view.w, view.h, safe.x, safe.y, safe.w, safe.h,
                 (long long)disp_w, (long long)disp_h);
        return;
    }
    set_rect((void*)area, &view);
    set_rect((void*)safe_dict, &safe);
    LOG_INFO(LOG_MODULE, "cluster view area %d,%d %dx%d safe %d,%d %dx%d (display %lldx%lld)",
             view.x, view.y, view.w, view.h, safe.x, safe.y, safe.w, safe.h,
             (long long)disp_w, (long long)disp_h);
}

HOOK_EXPORT CFTypeRef AirPlayReceiverSessionScreen_CopyDisplaysInfo(void* session, int32_t* err) {
    static copy_displays_f next;
    if (!next) next = (copy_displays_f)dlsym(RTLD_NEXT, "AirPlayReceiverSessionScreen_CopyDisplaysInfo");
    if (!next) {
        if (err) *err = -6700;
        return NULL;
    }
    CFTypeRef displays = next(session, err);
    if (displays && hook_process_is_dio_manager()) alt_viewarea_apply(displays);
    return displays;
}
