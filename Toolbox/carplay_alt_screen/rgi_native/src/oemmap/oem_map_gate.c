/*
 * OEM cluster map startup gate - bus bridge
 *
 * Java -> CMD_OEM_MAP_READY [1] -> altscreen_oem_map_ready(1) in the AltScreen
 * preload.  Both libraries are preloaded into dio_manager, so the AltScreen
 * entry point is resolved with dlsym at delivery time; if it is absent (no
 * AltScreen preload) the message is simply dropped.
 */

#include "../framework/common.h"
#include "../framework/logging.h"
#include "../framework/bus.h"
#include "../framework/bus_protocol.h"
#include "oem_map_gate.h"

#include <dlfcn.h>

DEFINE_LOG_MODULE(OEMMAP);

static void oem_map_on_ready(uint16_t type, uint8_t flags, const uint8_t* payload,
                             uint32_t len, void* ctx) {
    typedef void (*ready_f)(int);
    ready_f ready;
    (void)type; (void)ctx;
    if (!(flags & BUS_FLAG_BINARY) || len != 1 || !payload || payload[0] != 1) return;
    ready = (ready_f)dlsym(RTLD_DEFAULT, "altscreen_oem_map_ready");
    if (ready) ready(1);
}

static void oem_map_gate_init(void) {
    if (!hook_process_is_dio_manager()) return;
    bus_on(CMD_OEM_MAP_READY, oem_map_on_ready, NULL);
    LOG_INFO(LOG_MODULE, "OEM map gate ready (bus 0x%04x)", CMD_OEM_MAP_READY);
}

static void oem_map_gate_shutdown(void) {
    bus_off(CMD_OEM_MAP_READY);
}

const hook_module_def_t oem_map_gate_module_def = {
    .name = "oemmapgate",
    .priority = HOOK_PRIORITY_LOW,
    .on_init = oem_map_gate_init,
    .on_shutdown = oem_map_gate_shutdown
};
