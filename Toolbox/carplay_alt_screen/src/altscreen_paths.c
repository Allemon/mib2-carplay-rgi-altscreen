/* altscreen_paths.c - see altscreen_paths.h. */
#include "altscreen_paths.h"
#include "altscreen_core.h"
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

/* Keep this order byte-for-byte equivalent to util_mountsd.sh discovery. A
 * runtime root is eligible only when the same volume has both root-level
 * Toolbox and the current transaction directory. This prevents an old Log
 * directory on another card from shadowing the SD selected by START. */
#define ALTSCREEN_DEV_ROOT "/tmp"
#define ALTSCREEN_VOLATILE_ROOT "/tmp"
#define ALTSCREEN_LOG_LEAF "altscreen_hook.log"
#define ALTSCREEN_PROFILE_LEAF "IAP2_PROFILE"

typedef struct {
    const char *toolbox;
    const char *state;
    /* Full leaf paths, concatenated by the preprocessor. A caller that stored a
     * published pointer must be able to read it forever, so no answer is ever
     * composed into a buffer that a later call can overwrite. */
    const char *log;
    const char *sd_log;
    const char *profile;
} AltPathCandidate;

#define ALTSCREEN_CANDIDATE(toolbox, state, sdlog) \
    { toolbox, state, ALTSCREEN_VOLATILE_ROOT "/" ALTSCREEN_LOG_LEAF, sdlog, state "/" ALTSCREEN_PROFILE_LEAF }

static const AltPathCandidate kCandidates[] = {
    ALTSCREEN_CANDIDATE("/net/mmx/fs/sda0/Toolbox", "/net/mmx/fs/sda0/MMI-Cockpit-Carplay/state", "/net/mmx/fs/sda0/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/net/mmx/fs/sda1/Toolbox", "/net/mmx/fs/sda1/MMI-Cockpit-Carplay/state", "/net/mmx/fs/sda1/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/net/mmx/fs/sdb0/Toolbox", "/net/mmx/fs/sdb0/MMI-Cockpit-Carplay/state", "/net/mmx/fs/sdb0/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/net/mmx/fs/sdb1/Toolbox", "/net/mmx/fs/sdb1/MMI-Cockpit-Carplay/state", "/net/mmx/fs/sdb1/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/fs/sda0/Toolbox", "/fs/sda0/MMI-Cockpit-Carplay/state", "/fs/sda0/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/fs/sda1/Toolbox", "/fs/sda1/MMI-Cockpit-Carplay/state", "/fs/sda1/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/fs/sdb0/Toolbox", "/fs/sdb0/MMI-Cockpit-Carplay/state", "/fs/sdb0/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    ALTSCREEN_CANDIDATE("/fs/sdb1/Toolbox", "/fs/sdb1/MMI-Cockpit-Carplay/state", "/fs/sdb1/MMI-Cockpit-Carplay/logs/altscreen_hook.log"),
    { NULL, NULL, NULL, NULL, NULL }
};

/* Flat /tmp contract: runtime diagnostics are single files directly under /tmp.
 * No operation may depend on creating a nested volatile directory. A late SD mount can
 * still win later. These strings are constants, so a stored fallback pointer
 * stays valid even after an SD root has been selected. */
static const char kDevLogPath[] = ALTSCREEN_VOLATILE_ROOT "/" ALTSCREEN_LOG_LEAF;
static const char kLegacyDevLogPath[] = ALTSCREEN_DEV_ROOT "/" ALTSCREEN_LOG_LEAF;
static const char kDevProfilePath[] = ALTSCREEN_DEV_ROOT "/" ALTSCREEN_PROFILE_LEAF;
static int g_volatile_root_state;

/* Selection state: -1 until an SD root is published, then the index of the
 * winning candidate in kCandidates. The only transition is -1 -> index, so a
 * published root is sticky for the whole process lifetime and no later call can
 * reset or replace it. */
static int g_selected = -1;

/* One report slot per published root, plus one for the retryable /tmp root.
 * A slot is claimed with a single 32-bit atomic read-modify-write, so concurrent
 * reporters cannot both log the same root and no report buffer is shared. */
static unsigned g_reported_slots;
#define ALTSCREEN_REPORT_SLOT_DEV 31
typedef char alt_report_slots_fit[
    (sizeof(kCandidates) / sizeof(kCandidates[0]) - 1u) < ALTSCREEN_REPORT_SLOT_DEV ? 1 : -1];

#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & 0170000) == 0120000)
#endif

/* Marker roots are security boundaries. Reject a symlink in any fixed path
 * component rather than following a byte-identical file outside the SD alias. */
static int path_kind(const char *path, int dir) {
    char part[ALTSCREEN_PATH_MAX];
    struct stat st;
    size_t i, n;
    memset(&st, 0, sizeof(st));
    if (!path || path[0] != '/') return 0;
    n = strlen(path);
    if (n < 2u || n >= sizeof(part)) return 0;
    for (i = 1u; i <= n; ++i) {
        if (path[i] != '/' && path[i] != 0) continue;
        memcpy(part, path, i); part[i] = 0;
        if (lstat(part, &st) != 0 || S_ISLNK(st.st_mode)) return 0;
        if (path[i] != 0 && !S_ISDIR(st.st_mode)) return 0;
    }
    if (dir) return S_ISDIR(st.st_mode) ? 1 : 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int dir_exists(const char *path) { return path_kind(path, 1); }

int altscreen_prepare_volatile_root(void) {
    int state = __atomic_load_n(&g_volatile_root_state, __ATOMIC_ACQUIRE);
    if (state != 0) return state > 0;
    if (dir_exists(ALTSCREEN_VOLATILE_ROOT) ||
        mkdir(ALTSCREEN_VOLATILE_ROOT, 0700) == 0 ||
        dir_exists(ALTSCREEN_VOLATILE_ROOT)) {
        __atomic_store_n(&g_volatile_root_state, 1, __ATOMIC_RELEASE);
        return 1;
    }
    __atomic_store_n(&g_volatile_root_state, -1, __ATOMIC_RELEASE);
    return 0;
}

/* Publish at most one candidate index. The winner is the first thread whose
 * compare-and-swap turns -1 into its own index; every concurrent loser, and every
 * later caller, answers with that same index instead of its own candidate. */
static int alt_selected_root_index(void) {
    int i = __atomic_load_n(&g_selected, __ATOMIC_ACQUIRE);
    if (i >= 0) return i;
    for (i = 0; kCandidates[i].state; ++i) {
        int expected = -1;
        int winner;
        if (!dir_exists(kCandidates[i].toolbox)) continue;
        if (!dir_exists(kCandidates[i].state)) continue;
        if (__atomic_compare_exchange_n(&g_selected, &expected, i,
                                        0 /* strong */, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return i;
        winner = __atomic_load_n(&g_selected, __ATOMIC_ACQUIRE);
        if (winner >= 0) return winner;
    }
    /* No card namespace visible yet. /tmp is diagnostic-only and stays
     * unpublished: dio_manager can be constructed before the SD filesystem
     * aliases appear during a full MMI boot, and the asynchronous init worker
     * retries this resolver without delaying the application startup thread.
     * Only an authoritative SD root becomes sticky. */
    return __atomic_load_n(&g_selected, __ATOMIC_ACQUIRE);
}

const char *altscreen_state_root(void) {
    int i = alt_selected_root_index();
    if (i < 0) return ALTSCREEN_DEV_ROOT;
    return kCandidates[i].state;
}

int altscreen_state_root_is_authoritative(void) {
    return alt_selected_root_index() >= 0;
}

int altscreen_state_path(const char *leaf, char *out, size_t cap) {
    const char *root;
    size_t rlen, llen;
    if (!leaf || !out || cap == 0) return 0;
    root = altscreen_state_root();
    rlen = strlen(root);
    llen = strlen(leaf);
    if (rlen + llen + 2 > cap) { out[0] = 0; return 0; }
    memcpy(out, root, rlen);
    out[rlen] = (char)47;
    memcpy(out + rlen + 1, leaf, llen + 1);
    return 1;
}

int altscreen_marker_present(const char *leaf) {
    char path[ALTSCREEN_PATH_MAX];
    /* /tmp remains a bounded diagnostic log fallback only. It can never arm
     * observation, mutation, iAP2 or private type-111 behavior. */
    if (!altscreen_state_root_is_authoritative()) return 0;
    if (!altscreen_state_path(leaf, path, sizeof(path))) return 0;
    return path_kind(path, 0);
}

/* Volatile storage remains preferred. The asynchronous writer may fall back
 * directly to the authoritative SD logs directory when the vehicle exposes no
 * writable /tmp. If both sinks fail, evidence is dropped and CarPlay continues. */
const char *altscreen_log_path(void) {
    return altscreen_prepare_volatile_root() ? kDevLogPath : kLegacyDevLogPath;
}

/* Optional third-tier sink.  It is returned only after an authoritative SD
 * transaction root is visible.  Callers must treat failure to open it as
 * diagnostic loss only; it is never an authorization or runtime prerequisite. */
const char *altscreen_sd_log_path(void) {
    int i = alt_selected_root_index();
    if (i < 0) return NULL;
    return kCandidates[i].sd_log;
}

const char *altscreen_profile_path(void) {
    int i = alt_selected_root_index();
    if (i < 0) return kDevProfilePath;
    return kCandidates[i].profile;
}

static int alt_report_claim(int slot) {
    unsigned bit = 1u << (unsigned)slot;
    unsigned previous = __atomic_fetch_or(&g_reported_slots, bit, __ATOMIC_ACQ_REL);
    return (previous & bit) == 0u;
}

/* Report once per selected root, so a reconnect cannot spam the log. Root and
 * both leaf paths come from the same published row and are logged together, so a
 * report line can never mix one root with another root's paths. */
void altscreen_paths_report(void) {
    const char *root = ALTSCREEN_DEV_ROOT;
    const char *log = altscreen_log_path();
    const char *profile = kDevProfilePath;
    int slot = ALTSCREEN_REPORT_SLOT_DEV;
    int authoritative = 0;
    int i = alt_selected_root_index();
    if (i >= 0) {
        root = kCandidates[i].state;
        log = altscreen_log_path();
        profile = kCandidates[i].profile;
        slot = i;
        authoritative = 1;
    }
    if (!alt_report_claim(slot)) return;
    altscreen_log("STATE_ROOT=%s sd_authoritative=%d log=%s profile=%s",
                  root, authoritative, log, profile);
}
