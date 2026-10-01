/* alt_viewarea_test: the real view-area interposer against a fake libairplay.
 * No config -> AltScreen's values; a config rewrites the cluster's view and safe area
 * only; invalid configs are refused. */
#include "altcluster/alt_viewarea.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int fake_live, fake_copy_calls;
const void* AirPlayReceiverSessionScreen_CopyDisplaysInfo(void* session, int32_t* err);
const void* AirPlayReceiverSessionPlatformCopyProperty(void* session, uint32_t flags, const void* prop,
                                                       const void* qual, int32_t* err);
long CFArrayGetCount(const void* a);
const void* CFArrayGetValueAtIndex(const void* a, long i);
const void* CFDictionaryGetValue(const void* d, const void* k);
const void* CFStringCreateWithCString(const void* a, const char* s, uint32_t enc);
int64_t CFDictionaryGetInt64(const void* d, const void* k, int32_t* err);
void CFRelease(const void* cf);

int hook_process_is_dio_manager(void) { return 1; }

static int failures;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } } while (0)

static const void* get(const void* d, const char* k) {
    const void* s = CFStringCreateWithCString(NULL, k, 0x08000100u);
    const void* v = CFDictionaryGetValue(d, s);
    CFRelease(s);
    return v;
}
static int64_t num(const void* d, const char* k) {
    const void* s = CFStringCreateWithCString(NULL, k, 0x08000100u);
    int32_t err;
    int64_t v = CFDictionaryGetInt64(d, s, &err);
    CFRelease(s);
    return v;
}
static void areas(const void* displays, int64_t r[8]) {
    const void* cluster = CFArrayGetValueAtIndex(displays, 1);
    const void* area = CFArrayGetValueAtIndex(get(cluster, "viewAreas"), 0);
    const void* safe = get(area, "safeArea");
    r[0] = num(area, "originXPixels"); r[1] = num(area, "originYPixels");
    r[2] = num(area, "widthPixels");   r[3] = num(area, "heightPixels");
    r[4] = num(safe, "originXPixels"); r[5] = num(safe, "originYPixels");
    r[6] = num(safe, "widthPixels");   r[7] = num(safe, "heightPixels");
}
static void write_cfg(const char* path, const char* text) {
    FILE* f = fopen(path, "w"); fputs(text, f); fclose(f);
}
static const void* copy(void) {
    int32_t err = 1;
    const void* d = AirPlayReceiverSessionScreen_CopyDisplaysInfo(NULL, &err);
    CHECK(err == 0 && d, "stock result forwarded");
    return d;
}

int main(void) {
    char path[] = "/tmp/alt_viewarea_test.XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd); remove(path);
    alt_viewarea_set_config_path(path);
    int64_t r[8];

    const void* d = copy();
    areas(d, r);
    CHECK(r[2] == 1440 && r[3] == 542 && r[6] == 1440, "no config: AltScreen's full view area kept");
    CFRelease(d);

    write_cfg(path, "view 0 0 1134 542\nsafe 306 0 828 542\n");
    d = copy();
    areas(d, r);
    CHECK(r[0] == 0 && r[1] == 0 && r[2] == 1134 && r[3] == 542, "view area rewritten");
    CHECK(r[4] == 306 && r[5] == 0 && r[6] == 828 && r[7] == 542, "safe area rewritten");
    CHECK(num(CFArrayGetValueAtIndex(d, 1), "widthPixels") == 1440, "display size unchanged");
    CHECK(get(CFArrayGetValueAtIndex(d, 0), "viewAreas") == NULL, "main display untouched");
    CFRelease(d);

    /* The "displays" session property carries the same cluster entry on the car. */
    const void* k = CFStringCreateWithCString(NULL, "displays", 0x08000100u);
    int32_t err = 1;
    d = AirPlayReceiverSessionPlatformCopyProperty(NULL, 0, k, NULL, &err);
    areas(d, r);
    CHECK(err == 0 && r[2] == 1134 && r[4] == 306 && r[6] == 828, "displays property rewritten too");
    CFRelease(d);
    CFRelease(k);
    k = CFStringCreateWithCString(NULL, "other", 0x08000100u);
    d = AirPlayReceiverSessionPlatformCopyProperty(NULL, 0, k, NULL, &err);
    CHECK(d != NULL, "other properties pass through");
    CFRelease(d);
    CFRelease(k);

    write_cfg(path, "view 0 0 1200 542\n");
    d = copy();
    areas(d, r);
    CHECK(r[2] == 1200 && r[4] == 0 && r[6] == 1200, "safe defaults to the whole view area");
    CFRelease(d);

    const char* bad[] = { "view 0 0 1500 542\n", "view 300 0 1200 542\n", "view 0 0 1134 542\nsafe 400 0 828 542\n",
                          "view 0 0 50 542\n", "nonsense\n" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        write_cfg(path, bad[i]);
        d = copy();
        areas(d, r);
        CHECK(r[2] == 1440 && r[6] == 1440, bad[i]);
        CFRelease(d);
    }
    remove(path);
    CHECK(fake_live == 0, "no CF objects leaked");
    if (failures) return 1;
    printf("alt_viewarea_test: default kept, CopyDisplaysInfo + displays property rewritten, invalid refused PASS\n");
    return 0;
}
