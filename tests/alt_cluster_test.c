/* alt_cluster_test: the real hook/altcluster module against a fake libairplay
 * (alt_cluster_fake_airplay.c, a separate shared library).  Checks the session
 * tracking through the platform hooks, the exact command dictionaries and that
 * nothing is sent to a finalized session. */
#include "framework/bus.h"
#include "framework/bus_protocol.h"
#include "altcluster/alt_cluster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int fake_live_objects, fake_platform_init_calls, fake_platform_fini_calls, fake_send_calls;
extern void* fake_last_session;
extern char fake_last_request[1024];

int32_t AirPlayReceiverSessionPlatformInitialize(void* session);
void AirPlayReceiverSessionPlatformFinalize(void* session);

/* ---- framework stubs ---- */
static bus_handler_t handlers[0x0200];
int hook_process_is_dio_manager(void) { return 1; }
hook_result_t bus_on(uint16_t type, bus_handler_t h, void* ctx) { (void)ctx; handlers[type] = h; return HOOK_OK; }
void bus_off(uint16_t type) { handlers[type] = NULL; }

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void deliver(uint16_t type, const void* payload, uint32_t len) {
    CHECK(handlers[type] != NULL, "handler registered");
    if (handlers[type]) handlers[type](type, 0, payload, len, NULL);
}

static void zoom(int8_t step) { deliver(CMD_ALT_ZOOM, &step, 1); }

int main(void) {
    const char* uuid = ALT_CLUSTER_DISPLAY_UUID;
    char expect[512];
    int session_a, session_b;

    alt_cluster_module_def.on_init();

    /* Before any AirPlay session nothing is sent. */
    zoom(1);
    CHECK(fake_send_calls == 0, "no session -> no command");

    CHECK(AirPlayReceiverSessionPlatformInitialize(&session_a) == 0, "stock init result forwarded");
    CHECK(fake_platform_init_calls == 1, "stock init called");

    /* +1 MapScale step = zoom index up = further out. */
    zoom(1);
    snprintf(expect, sizeof(expect),
             "{type=changeMapZoomLevel,params={uuid=%s,zoomDirection=1}}", uuid);
    CHECK(fake_send_calls == 1, "one step -> one command");
    CHECK(fake_last_session == &session_a, "sent to the live session");
    CHECK(strcmp(fake_last_request, expect) == 0, "zoom out request");

    /* -2 steps = two zoom-in commands; a burst is capped. */
    zoom(-2);
    snprintf(expect, sizeof(expect),
             "{type=changeMapZoomLevel,params={uuid=%s,zoomDirection=0}}", uuid);
    CHECK(fake_send_calls == 3, "two steps -> two commands");
    CHECK(strcmp(fake_last_request, expect) == 0, "zoom in request");
    zoom(-100);
    CHECK(fake_send_calls == 7, "burst capped at 4 commands");
    zoom(0);
    CHECK(fake_send_calls == 7, "zero step ignored");
    deliver(CMD_ALT_ZOOM, "", 0);
    CHECK(fake_send_calls == 7, "empty payload ignored");

    /* showUI with a URL. */
    const char* url = "maps:/car/instrumentcluster/map?maneuverLayout=topaligned";
    deliver(CMD_ALT_UICTX, url, (uint32_t)strlen(url));
    snprintf(expect, sizeof(expect), "{type=showUI,params={uuid=%s,url=%s}}", uuid, url);
    CHECK(fake_send_calls == 8, "showUI sent");
    CHECK(strcmp(fake_last_request, expect) == 0, "showUI request");
    char big[600];
    memset(big, 'a', sizeof(big));
    deliver(CMD_ALT_UICTX, big, sizeof(big));
    CHECK(fake_send_calls == 8, "oversized URL rejected");

    /* A newer session replaces the old one; finalizing the old one keeps it. */
    AirPlayReceiverSessionPlatformInitialize(&session_b);
    AirPlayReceiverSessionPlatformFinalize(&session_a);
    zoom(1);
    CHECK(fake_last_session == &session_b, "stale finalize does not drop the new session");

    /* After the live session is finalized nothing reaches it. */
    AirPlayReceiverSessionPlatformFinalize(&session_b);
    CHECK(fake_platform_fini_calls == 2, "stock finalize forwarded");
    int before = fake_send_calls;
    zoom(1);
    deliver(CMD_ALT_UICTX, url, (uint32_t)strlen(url));
    CHECK(fake_send_calls == before, "no command after finalize");

    CHECK(fake_live_objects == 0, "every CF object released");
    alt_cluster_module_def.on_shutdown();
    CHECK(handlers[CMD_ALT_ZOOM] == NULL && handlers[CMD_ALT_UICTX] == NULL, "handlers removed");

    if (failures) { printf("alt_cluster_test: %d FAILED\n", failures); return 1; }
    printf("alt_cluster_test: session tracking, zoom/showUI dictionaries, burst cap, finalize gate, no leaks PASS\n");
    return 0;
}
