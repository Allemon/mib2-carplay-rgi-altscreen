/* Fake libairplay for alt_viewarea_test: CF-lite arrays/dicts/strings/numbers and an
 * AltScreen-shaped AirPlayReceiverSessionScreen_CopyDisplaysInfo to wrap (main display
 * + cluster display with one full-size view area and safe area). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_STRING = 1, T_NUMBER, T_DICT, T_ARRAY };
typedef struct obj {
    int kind, refs, count;
    char str[64];
    int64_t num;
    struct obj* keys[16];
    struct obj* values[16];
} obj;

int fake_live;
static obj* mk(int kind) { obj* o = calloc(1, sizeof(*o)); o->kind = kind; o->refs = 1; fake_live++; return o; }
const void* CFRetain(const void* cf) { ((obj*)cf)->refs++; return cf; }
void CFRelease(const void* cf) {
    obj* o = (obj*)cf;
    if (--o->refs > 0) return;
    for (int i = 0; i < o->count; i++) { if (o->keys[i]) CFRelease(o->keys[i]); CFRelease(o->values[i]); }
    fake_live--; free(o);
}
unsigned long CFGetTypeID(const void* cf) { return ((const obj*)cf)->kind; }
unsigned long CFStringGetTypeID(void) { return T_STRING; }
unsigned long CFDictionaryGetTypeID(void) { return T_DICT; }
unsigned long CFArrayGetTypeID(void) { return T_ARRAY; }
const void* CFStringCreateWithCString(const void* a, const char* s, uint32_t enc) {
    (void)a; if (enc != 0x08000100u) return NULL;
    obj* o = mk(T_STRING); snprintf(o->str, sizeof(o->str), "%s", s); return o;
}
int CFStringGetCString(const void* s, char* buf, long size, uint32_t enc) {
    (void)enc; snprintf(buf, size, "%s", ((const obj*)s)->str); return 1;
}
static int find(const obj* d, const char* k) {
    for (int i = 0; i < d->count; i++) if (strcmp(d->keys[i]->str, k) == 0) return i;
    return -1;
}
const void* CFDictionaryGetValue(const void* d, const void* k) {
    int i = find(d, ((const obj*)k)->str); return i < 0 ? NULL : ((const obj*)d)->values[i];
}
static void put(obj* d, const char* k, obj* v) {
    int i = find(d, k);
    if (i >= 0) { CFRelease(d->values[i]); d->values[i] = v; return; }
    d->keys[d->count] = (obj*)CFStringCreateWithCString(NULL, k, 0x08000100u);
    d->values[d->count++] = v;
}
int32_t CFDictionarySetInt64(void* d, const void* k, int64_t v) {
    obj* n = mk(T_NUMBER); n->num = v; put(d, ((const obj*)k)->str, n); return 0;
}
int64_t CFDictionaryGetInt64(const void* d, const void* k, int32_t* err) {
    const obj* v = CFDictionaryGetValue(d, k);
    if (!v || v->kind != T_NUMBER) { if (err) *err = -6727; return 0; }
    if (err) *err = 0;
    return v->num;
}
long CFArrayGetCount(const void* a) { return ((const obj*)a)->count; }
const void* CFArrayGetValueAtIndex(const void* a, long i) { return ((const obj*)a)->values[i]; }

static obj* rect(int64_t w, int64_t h) {
    obj* d = mk(T_DICT);
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "widthPixels" }, w);
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "heightPixels" }, h);
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "originXPixels" }, 0);
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "originYPixels" }, 0);
    return d;
}
static obj* display(const char* uuid, int64_t w, int64_t h, int with_areas) {
    obj* d = mk(T_DICT);
    put(d, "uuid", (obj*)CFStringCreateWithCString(NULL, uuid, 0x08000100u));
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "widthPixels" }, w);
    CFDictionarySetInt64(d, &(obj){ .kind = T_STRING, .str = "heightPixels" }, h);
    if (with_areas) {
        obj* area = rect(w, h);
        put(area, "safeArea", rect(w, h));
        obj* areas = mk(T_ARRAY);
        areas->values[areas->count++] = area;
        put(d, "viewAreas", areas);
    }
    return d;
}
/* AltScreen also adds the cluster to the "displays" session property. */
const void* AirPlayReceiverSessionPlatformCopyProperty(void* session, uint32_t flags, const void* prop,
                                                       const void* qual, int32_t* err);
const void* AirPlayReceiverSessionScreen_CopyDisplaysInfo(void* session, int32_t* err);
const void* AirPlayReceiverSessionPlatformCopyProperty(void* session, uint32_t flags, const void* prop,
                                                       const void* qual, int32_t* err) {
    (void)flags; (void)qual;
    if (strcmp(((const obj*)prop)->str, "displays") == 0)
        return AirPlayReceiverSessionScreen_CopyDisplaysInfo(session, err);
    if (err) *err = 0;
    return CFStringCreateWithCString(NULL, "not-displays", 0x08000100u);
}
int fake_copy_calls;
const void* AirPlayReceiverSessionScreen_CopyDisplaysInfo(void* session, int32_t* err) {
    (void)session; fake_copy_calls++;
    if (err) *err = 0;
    obj* a = mk(T_ARRAY);
    a->values[a->count++] = display("main-display", 800, 480, 0);
    a->values[a->count++] = display("b7e6c5a0-2222-4000-8000-000000000002", 1440, 542, 1);
    return a;
}
