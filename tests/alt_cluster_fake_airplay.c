/* Fake libairplay for alt_cluster_test: the two platform hooks the module wraps,
 * AirPlayReceiverSessionSendCommand and just enough CF-lite to build a request.
 * Built as a shared library so the module's dlsym(RTLD_NEXT) reaches it the way
 * it reaches libairplay on the unit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FAKE_STRING = 1, FAKE_NUMBER, FAKE_DICT };
typedef struct fake_obj {
    int kind;
    int refs;
    char str[512];
    int64_t num;
    const struct fake_obj* keys[8];
    const struct fake_obj* values[8];
    int count;
} fake_obj;

int fake_live_objects;
int fake_platform_init_calls;
int fake_platform_fini_calls;
int fake_send_calls;
void* fake_last_session;
char fake_last_request[1024];

const char kCFLDictionaryKeyCallBacksCFLTypes[24];
const char kCFLDictionaryValueCallBacksCFLTypes[20];

static fake_obj* fake_new(int kind) {
    fake_obj* o = calloc(1, sizeof(*o));
    o->kind = kind;
    o->refs = 1;
    fake_live_objects++;
    return o;
}

const void* CFRetain(const void* cf) { ((fake_obj*)cf)->refs++; return cf; }

void CFRelease(const void* cf) {
    fake_obj* o = (fake_obj*)cf;
    if (--o->refs > 0) return;
    for (int i = 0; i < o->count; i++) { CFRelease(o->keys[i]); CFRelease(o->values[i]); }
    fake_live_objects--;
    free(o);
}

const void* CFStringCreateWithCString(const void* alloc, const char* s, uint32_t enc) {
    (void)alloc;
    if (enc != 0x08000100u) return NULL;
    fake_obj* o = fake_new(FAKE_STRING);
    snprintf(o->str, sizeof(o->str), "%s", s);
    return o;
}

void* CFDictionaryCreateMutable(const void* alloc, long cap, const void* kcb, const void* vcb) {
    (void)alloc; (void)cap;
    if (kcb != kCFLDictionaryKeyCallBacksCFLTypes || vcb != kCFLDictionaryValueCallBacksCFLTypes) return NULL;
    return fake_new(FAKE_DICT);
}

void CFDictionarySetValue(void* dict, const void* key, const void* value) {
    fake_obj* d = dict;
    d->keys[d->count] = CFRetain(key);
    d->values[d->count] = CFRetain(value);
    d->count++;
}

int32_t CFDictionarySetInt64(void* dict, const void* key, int64_t v) {
    fake_obj* n = fake_new(FAKE_NUMBER);
    n->num = v;
    CFDictionarySetValue(dict, key, n);
    CFRelease(n);
    return 0;
}

static void fake_dump(const fake_obj* o, char* out, size_t cap) {
    size_t len = strlen(out);
    if (o->kind == FAKE_STRING) { snprintf(out + len, cap - len, "%s", o->str); return; }
    if (o->kind == FAKE_NUMBER) { snprintf(out + len, cap - len, "%lld", (long long)o->num); return; }
    snprintf(out + len, cap - len, "{");
    for (int i = 0; i < o->count; i++) {
        len = strlen(out);
        snprintf(out + len, cap - len, "%s%s=", i ? "," : "", o->keys[i]->str);
        fake_dump(o->values[i], out, cap);
    }
    len = strlen(out);
    snprintf(out + len, cap - len, "}");
}

int32_t AirPlayReceiverSessionSendCommand(void* session, const void* request, void* cb, void* ctx) {
    (void)cb; (void)ctx;
    fake_send_calls++;
    fake_last_session = session;
    fake_last_request[0] = '\0';
    fake_dump(request, fake_last_request, sizeof(fake_last_request));
    return 0;
}

int32_t AirPlayReceiverSessionPlatformInitialize(void* session) {
    (void)session;
    fake_platform_init_calls++;
    return 0;
}

void AirPlayReceiverSessionPlatformFinalize(void* session) {
    (void)session;
    fake_platform_fini_calls++;
}
