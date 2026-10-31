/* Runtime evidence for the AUG22 universal relocation resolver.
 *
 * The resolver may run once from a very early loader constructor where native
 * logging is intentionally disabled.  The asynchronous AltScreen worker calls
 * this wrapper after altscreen_init()/altscreen_log_start_async(), so report the
 * resolver verdict exactly once there.  No constructor performs SD or /tmp I/O.
 */
#include "altscreen_core.h"

extern int aug22_dynamic_internal_redirects_ready(void);

static volatile unsigned g_aug22_dynamic_diag_reported;

int aug22_dynamic_internal_redirects_ready_logged(void) {
    int ready = aug22_dynamic_internal_redirects_ready();
    if (__sync_bool_compare_and_swap(&g_aug22_dynamic_diag_reported, 0u, 1u)) {
        altscreen_log(
            "PHASE=AUG22_DYNAMIC_RESOLVER result=%s resolver=ELF32_ARM_DYNAMIC_RELOCATION "
            "symbols=dynsym relocations=R_ARM_GLOB_DAT,R_ARM_JUMP_SLOT transaction=all_or_nothing "
            "stock_fail_open=YES persistent_sd_via_boot_recorder=YES",
            ready ? "PASS" : "REFUSED");
    }
    return ready;
}
