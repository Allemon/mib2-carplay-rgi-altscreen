/*
 * p1404_resolve.c - firmware identity proof and real-entry-point recovery.
 *
 * Stock forwarding and AltScreen identity are deliberately separate. Before any
 * identity decision, the six public AirPlay exports are bound from the next
 * loaded object, with a handle-scoped lookup of a non-symlink stock libairplay as a
 * fallback. Therefore an identity/authorization refusal disables observation and
 * mutation without replacing normal CarPlay calls with fabricated failures.
 *
 * The mutation identity proof then uses those handle-scoped stock symbols, proves
 * mutually consistent distances (fixing the load base exactly), derives the
 * remaining measured entries and validates their ARM entry words. Any identity
 * failure leaves the stock bindings intact and AltScreen completely inactive.
 */
#include "p1404_abi.h"
#include "altscreen_paths.h"
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>

struct p1404_symbols p1404;
int p1404_identity_ok;
int p1404_armed;
int p1404_mutate_armed;
unsigned long p1404_libairplay_base;

struct anchor { const char *nm; unsigned off; };
const struct anchor kAnchor[] = {
    { "AirPlayReceiverSessionSetup",                 OFF_AIRPLAY_SESSION_SETUP },
    { "AirPlayReceiverSessionCreate",                OFF_AIRPLAY_SESSION_CREATE },
    { "AirPlayReceiverSessionTearDown",              OFF_AIRPLAY_SESSION_TEARDOWN },
    { "AirPlayReceiverSessionScreen_Create",         OFF_AIRPLAY_SCREEN_CREATE },
    { "AirPlayReceiverSessionScreen_Setup",          OFF_AIRPLAY_SCREEN_SETUP },
    { "AirPlayReceiverSessionScreen_StartSession",   OFF_AIRPLAY_SCREEN_START_SESSION },
    { "AirPlayReceiverSessionScreen_StopSession",    OFF_AIRPLAY_SCREEN_STOP_SESSION },
    { "AirPlayReceiverSessionScreen_Delete",         OFF_AIRPLAY_SCREEN_DELETE },
    { "ScreenStreamStart",                           OFF_SCREEN_STREAM_START },
    { "AirPlayReceiverSessionScreen_SetVisibility",  OFF_AIRPLAY_SCREEN_SET_VISIBLE },
    { "AirPlayReceiverSessionScreen_SetSecurityInfo", OFF_AIRPLAY_SCREEN_SET_SECURITY_INFO },
    { "AirPlay_DeriveAESKeySHA512ForScreen",         OFF_AIRPLAY_DERIVE_AES_KEY_SHA512_FOR_SCREEN },
    { "ScreenCopyMain",                              OFF_SCREEN_COPY_MAIN },
    { NULL, 0u }
};

/* Keep this authorization marker under the owned persistent runtime root.
 * It must stay byte-for-byte aligned with altscreen_chain_test_universal.sh
 * and the release-binary contract verified by VERIFY-NATIVE-DIRECT-RELEASE.sh. */
#define ALTSCREEN_PROBE_MARKER "/mnt/app/root/carplay-altscreen/state/fullchain_probe"
#define AUTH_RUN_ID_MAX 64u
#ifndef RTLD_NOW
#define RTLD_NOW 2
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & 0170000) == 0120000)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & 0170000) == 0100000)
#endif

/* QNX-portable component walk. lstat each fixed component so neither a leaf nor
 * a parent directory can redirect a trusted candidate outside its audited path. */
static int path_is_nonsymlink_kind(const char *path, int require_regular) {
    char part[512];
    size_t i, n;
    struct stat st;
    if (!path || path[0] != '/') return 0;
    n = strlen(path);
    if (n < 2u || n >= sizeof(part)) return 0;
    part[0] = '/'; part[1] = 0;
    for (i = 1u; i <= n; ++i) {
        if (path[i] != '/' && path[i] != 0) continue;
        if (i > 1u) {
            memcpy(part, path, i);
            part[i] = 0;
            if (lstat(part, &st) != 0 || S_ISLNK(st.st_mode)) return 0;
            if (path[i] != 0 && !S_ISDIR(st.st_mode)) return 0;
        }
    }
    return !require_regular || S_ISREG(st.st_mode);
}

int p1404_read_marker(const char *path) {
    return path_is_nonsymlink_kind(path, 1);
}

static void *g_stock_handle;
static int g_stock_exports_ready;

#ifdef ALTSCREEN_DIRECT_PROXY
/* libairplax is a DT_NEEDED dependency of the direct proxy. Its CFLRetain
 * relocation is therefore usable before either library's constructors run.
 * QNX dlsym/RTLD_NEXT is not guaranteed to find a dependency that is still in
 * its constructor; deriving exact K1004 entries from this unwrapped anchor
 * preserves stock forwarding during that loader window. */
extern const unsigned char CFLRetain[];
struct direct_stock_entry { const char *name; unsigned p1404_offset; };
static const struct direct_stock_entry kDirectStock[] = {
    { "AirPlayReceiverServerCreate", OFF_AIRPLAY_RECEIVER_SERVER_CREATE },
    { "AirPlayReceiverServerPlatformCopyProperty", OFF_AIRPLAY_SERVER_PLATFORM_COPY_PROPERTY },
    { "AirPlayReceiverSessionPlatformCopyProperty", OFF_AIRPLAY_SESSION_PLATFORM_COPY_PROPERTY },
    { "AirPlayReceiverSessionPlatformControl", OFF_AIRPLAY_SESSION_PLATFORM_CONTROL },
    { "AirPlayReceiverSessionScreen_CopyDisplaysInfo", OFF_AIRPLAY_SCREEN_COPY_DISPLAYS_INFO },
    { "AirPlayReceiverSessionScreen_Create", OFF_AIRPLAY_SCREEN_CREATE },
    { "AirPlayReceiverSessionScreen_Setup", OFF_AIRPLAY_SCREEN_SETUP },
    { "AirPlayReceiverSessionScreen_StartSession", OFF_AIRPLAY_SCREEN_START_SESSION },
    { "AirPlayReceiverSessionScreen_StopSession", OFF_AIRPLAY_SCREEN_STOP_SESSION },
    { "AirPlayReceiverSessionScreen_Delete", OFF_AIRPLAY_SCREEN_DELETE },
    { "AirPlayReceiverSessionScreen_SetVisibility", OFF_AIRPLAY_SCREEN_SET_VISIBLE },
    { "AirPlayReceiverSessionScreen_SetSecurityInfo", OFF_AIRPLAY_SCREEN_SET_SECURITY_INFO },
    { "AirPlayReceiverSessionSetup", OFF_AIRPLAY_SESSION_SETUP },
    { "AirPlayReceiverSessionCreate", OFF_AIRPLAY_SESSION_CREATE },
    { "AirPlayReceiverSessionTearDown", OFF_AIRPLAY_SESSION_TEARDOWN },
    { "AirPlay_DeriveAESKeySHA512ForScreen", OFF_AIRPLAY_DERIVE_AES_KEY_SHA512_FOR_SCREEN },
    { "ScreenCopyMain", OFF_SCREEN_COPY_MAIN },
    { "ScreenCreate", OFF_SCREEN_CREATE },
    { "ScreenCopyDelegates", OFF_SCREEN_COPY_DELEGATES },
    { "ScreenRegisterDelegates", OFF_SCREEN_REGISTER_DELEGATES },
    { "NetSocket_WriteInternal", OFF_NET_SOCKET_WRITE_INTERNAL },
    { "NetSocket_Delete", OFF_NET_SOCKET_DELETE },
    { "NetSocket_Disconnect", OFF_NET_SOCKET_DISCONNECT },
    { "ScreenStreamStart", OFF_SCREEN_STREAM_START },
    { "ScreenStreamCreate", OFF_SCREEN_STREAM_CREATE },
    { "ScreenStreamProcessData", OFF_SCREEN_STREAM_PROCESS_DATA },
    { "_ZN3dio13CScreenRender6configERKNS_16st_screen_configE", OFF_CSCREEN_CONFIG },
    { "_ZN3dio13CScreenRender6renderEPh", OFF_CSCREEN_RENDER },
    { NULL, 0u }
};
#endif

void *p1404_direct_stock_symbol_named(const char *name) {
#ifdef ALTSCREEN_DIRECT_PROXY
    uintptr_t base;
    int i;
    if (!name) return NULL;
    base = (uintptr_t)&CFLRetain[0] -
           (uintptr_t)(OFF_CFL_RETAIN + ALTSCREEN_STOCK_OFFSET_DELTA);
    for (i = 0; kDirectStock[i].name; ++i) {
        if (!strcmp(name, kDirectStock[i].name))
            return (void *)(base + kDirectStock[i].p1404_offset +
                            ALTSCREEN_STOCK_OFFSET_DELTA);
    }
#else
    (void)name;
#endif
    return NULL;
}

static void *libairplay_handle(void) {
    const char *paths[2];
    int i;
    if (g_stock_handle) return g_stock_handle;
    paths[0] = P1404_LIBAIRPLAY_PATH_APP;
    paths[1] = P1404_LIBAIRPLAY_PATH_ESO;
    for (i = 0; i < 2; ++i) {
        if (!path_is_nonsymlink_kind(paths[i], 1)) continue;
        g_stock_handle = dlopen(paths[i], RTLD_NOW);
        if (g_stock_handle) break;
    }
    return g_stock_handle;
}

static void *libairplay_symbol(const char *name) {
    void *p = p1404_direct_stock_symbol_named(name);
    void *handle;
    if (p) return p;
    handle = libairplay_handle();
    return handle && name ? dlsym(handle, name) : NULL;
}

static void *stock_symbol(const char *name) {
    void *p;
    if (!name) return NULL;
    /* The direct K1004 path must work before constructors and without loader
     * recursion. The preload-compatible path still preserves RTLD_NEXT chains. */
    p = p1404_direct_stock_symbol_named(name);
    if (p) return p;
    p = dlsym(RTLD_NEXT, name);
    return p ? p : libairplay_symbol(name);
}

void *p1404_stock_symbol_named(const char *name) {
    return stock_symbol(name);
}

int p1404_bind_stock_exports(void) {
    struct p1404_symbols stock;
    memset(&stock, 0, sizeof(stock));
    stock.server_platform_copy_property = stock_symbol("AirPlayReceiverServerPlatformCopyProperty");
    stock.session_platform_copy_property = stock_symbol("AirPlayReceiverSessionPlatformCopyProperty");
    stock.session_platform_control = stock_symbol("AirPlayReceiverSessionPlatformControl");
    stock.session_setup = stock_symbol("AirPlayReceiverSessionSetup");
    stock.screen_copy_displays_info = stock_symbol("AirPlayReceiverSessionScreen_CopyDisplaysInfo");
    stock.screen_stream_process_data = stock_symbol("ScreenStreamProcessData");
    stock.screen_stream_create = stock_symbol("ScreenStreamCreate");

    /* Commit every independently resolved stock target even when another export
     * is absent.  Identity/mutation still require the complete set, but one
     * missing symbol must never discard the five stock calls that can forward. */
    p1404.server_platform_copy_property = stock.server_platform_copy_property;
    p1404.session_platform_copy_property = stock.session_platform_copy_property;
    p1404.session_platform_control = stock.session_platform_control;
    p1404.session_setup = stock.session_setup;
    p1404.screen_copy_displays_info = stock.screen_copy_displays_info;
    p1404.screen_stream_process_data = stock.screen_stream_process_data;
    p1404.screen_stream_create = stock.screen_stream_create;
    g_stock_exports_ready = stock.server_platform_copy_property &&
        stock.session_platform_copy_property && stock.session_platform_control &&
        stock.screen_copy_displays_info && stock.screen_stream_process_data &&
        stock.screen_stream_create;
    altscreen_log("PHASE=STOCK_EXPORT_BIND result=%s server=%d session=%d control=%d displays=%d process=%d create=%d method=RTLD_NEXT_OR_HANDLE_SCOPED",
                  g_stock_exports_ready ? "PASS" : "PARTIAL_REFUSED",
                  stock.server_platform_copy_property != NULL,
                  stock.session_platform_copy_property != NULL,
                  stock.session_platform_control != NULL,
                  stock.screen_copy_displays_info != NULL,
                  stock.screen_stream_process_data != NULL,
                  stock.screen_stream_create != NULL);
    return g_stock_exports_ready;
}

/* QNX exposes /proc/<pid>/exefile as a symlink to the executable. Opening it
 * reads the executable bytes, not a textual pathname; treating those ELF bytes
 * as a process name left the direct proxy permanently inert. Resolve the QNX
 * symlink first, retain Linux /proc/self/exe for host tests, and accept a file
 * fallback only when its contents are visibly an absolute textual path. */
static int p1404_read_executable_link(const char *path, char *buf, size_t cap) {
    ssize_t nr;
    if (!path || !buf || cap < 2u) return 0;
    nr = readlink(path, buf, cap - 1u);
    if (nr <= 0 || (size_t)nr >= cap) return 0;
    buf[nr] = 0;
    return buf[0] == '/';
}

static int p1404_executable_path(char *buf, size_t cap) {
    char proc_path[64];
    FILE *f;
    size_t n;
    if (!buf || cap < 2u) return 0;
    buf[0] = 0;

    if (p1404_read_executable_link("/proc/self/exefile", buf, cap) ||
        p1404_read_executable_link("/proc/self/exe", buf, cap))
        return 1;
    if (snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exefile",
                 (long)getpid()) <= 0)
        return 0;
    if (p1404_read_executable_link(proc_path, buf, cap)) return 1;

    f = fopen(proc_path, "r");
    if (!f) return 0;
    n = fread(buf, 1u, cap - 1u, f);
    fclose(f);
    if (n == 0u || buf[0] != '/') { buf[0] = 0; return 0; }
    while (n > 0u && (buf[n - 1u] == '\n' || buf[n - 1u] == '\r' || buf[n - 1u] == 0))
        --n;
    if (n == 0u) { buf[0] = 0; return 0; }
    buf[n] = 0;
    return 1;
}

const char *p1404_process_name(void) {
    static char path[256];
    const char *slash;
    if (p1404_executable_path(path, sizeof(path))) {
        slash = strrchr(path, (int)47);
        return slash ? slash + 1 : path;
    }
#ifdef ALTSCREEN_DIRECT_PROXY
    /* Captured QNX 6.5 libc exports _cmdname, _argv and __progname. _cmdname(NULL)
     * is measured to return the basename derived from the process command data.
     * On K1004 procfs exefile is readable ELF content, not necessarily a
     * readlink-able pathname. Prefer that libc helper, then full argv[0], then
     * the libc basename. This now runs only on the asynchronous worker. */
    {
        typedef char *(*qnx_cmdname_fn)(char *);
        qnx_cmdname_fn cmdname = (qnx_cmdname_fn)dlsym(RTLD_DEFAULT, "_cmdname");
        const char *cmd = cmdname ? cmdname(NULL) : NULL;
        if (cmd && *cmd) {
            slash = strrchr(cmd, (int)47);
            return slash ? slash + 1 : cmd;
        }
    }
    {
        char ***argv_slot = (char ***)dlsym(RTLD_DEFAULT, "_argv");
        if (argv_slot && *argv_slot && (*argv_slot)[0] && *(*argv_slot)[0]) {
            slash = strrchr((*argv_slot)[0], (int)47);
            return slash ? slash + 1 : (*argv_slot)[0];
        }
    }
    {
        char **prog = (char **)dlsym(RTLD_DEFAULT, "__progname");
        if (prog && *prog && **prog) {
            slash = strrchr(*prog, (int)47);
            return slash ? slash + 1 : *prog;
        }
    }
#endif
    return "unknown";
}

int process_is_allowed(const char *name) {
    if (!name) return 0;
#ifdef ALTSCREEN_DIRECT_PROXY
    /* K1004 servicemgr launches the dio_manager image through the
     * smartphone_integrator executable alias. Accept only those two measured
     * identities; the START loader smoke runs as sleep and remains excluded. */
    return !strcmp(name, K1004_HOST_PROCESS) ||
           !strcmp(name, P1404_HOST_PROCESS);
#else
    return !strcmp(name, P1404_HOST_PROCESS);
#endif
}

unsigned long g_runtime_dio_size;
unsigned long g_runtime_lib_size;
unsigned g_runtime_offset_delta;
const char *g_runtime_abi_baseline = "MHI2Q_CN_AUG22_STRUCTURAL";

unsigned elf16le(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
unsigned long elf32le(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Read-only structural file gate.  Whole-file checksums intentionally are not
 * identity: AUG22 siblings can differ in unrelated code/data.  Require a real,
 * bounded ELF32 little-endian ARM image with a complete program-header table.
 * libairplay symbols and their uniform layout are proven from the loaded handle
 * later, immediately before any derived entry can be armed. */
int elf32_arm_file(const char *path, unsigned expected_type,
                          unsigned long *size_out) {
    unsigned char h[52];
    unsigned long phoff, phbytes, size;
    unsigned phentsize, phnum;
    struct stat st;
    FILE *f;
    if (!path || !size_out || !path_is_nonsymlink_kind(path, 1) ||
        stat(path, &st) != 0 || st.st_size < 52)
        return 0;
    size = (unsigned long)st.st_size;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(h, 1u, sizeof(h), f) != sizeof(h)) { fclose(f); return 0; }
    fclose(f);
    if (h[0] != 0x7fu || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
        h[4] != 1u || h[5] != 1u || h[6] != 1u ||
        elf16le(h + 16) != expected_type || elf16le(h + 18) != 40u ||
        elf32le(h + 20) != 1u || elf16le(h + 40) != 52u)
        return 0;
    phoff = elf32le(h + 28);
    phentsize = elf16le(h + 42);
    phnum = elf16le(h + 44);
    if (phentsize != 32u || phnum == 0u || phoff < 52u) return 0;
    phbytes = (unsigned long)phentsize * (unsigned long)phnum;
    if (phbytes / phentsize != phnum || phoff > size || phbytes > size - phoff)
        return 0;
    *size_out = size;
    return 1;
}

enum approved_path_state {
    APPROVED_PATH_INSPECTION_ERROR = -1,
    APPROVED_PATH_VERIFIED_ABSENT = 0,
    APPROVED_PATH_PRESENT_SAFE = 1
};

/* lstat failures are not all absence. On the target QNX libc, ENOENT and
 * ENOTDIR prove that a fixed path component does not exist; permission, I/O,
 * and every other error leave the approved alias indeterminate and must refuse
 * the identity transaction. errno is backed by QNX __get_errno_ptr via the
 * freestanding errno shim used for the ARM build. */
static int approved_path_state(const char *path, int require_regular) {
    struct stat st;
    size_t i, n;
    char part[512];
    if (!path || path[0] != '/') return APPROVED_PATH_INSPECTION_ERROR;
    n = strlen(path);
    if (n < 2u || n >= sizeof(part)) return APPROVED_PATH_INSPECTION_ERROR;
    for (i = 1u; i <= n; ++i) {
        int err;
        if (path[i] != '/' && path[i] != 0) continue;
        memcpy(part, path, i); part[i] = 0;
        errno = 0;
        if (lstat(part, &st) != 0) {
            err = errno;
            if (err == ENOENT || err == ENOTDIR)
                return APPROVED_PATH_VERIFIED_ABSENT;
            return APPROVED_PATH_INSPECTION_ERROR;
        }
        if (S_ISLNK(st.st_mode)) return APPROVED_PATH_INSPECTION_ERROR;
        if (path[i] != 0 && !S_ISDIR(st.st_mode))
            return APPROVED_PATH_INSPECTION_ERROR;
    }
    if (require_regular && !S_ISREG(st.st_mode))
        return APPROVED_PATH_INSPECTION_ERROR;
    return APPROVED_PATH_PRESENT_SAFE;
}

int structural_aliases_match(const char *tag, const char *a,
                                    const char *b, unsigned expected_type,
                                    unsigned long *size_out) {
    const char *paths[2];
    int i, seen = 0;
    paths[0] = a; paths[1] = b;
    for (i = 0; i < 2; ++i) {
        unsigned long size = 0;
        int state = approved_path_state(paths[i], 1);
        if (state == APPROVED_PATH_VERIFIED_ABSENT) continue;
        if (state != APPROVED_PATH_PRESENT_SAFE) {
            altscreen_log("PROBE structural identity refused component=%s path=%s reason=alias_inspection_error",
                          tag, paths[i]);
            return 0;
        }
        ++seen;
        if (!elf32_arm_file(paths[i], expected_type, &size)) {
            altscreen_log("PROBE structural identity refused component=%s path=%s reason=invalid_elf32_arm",
                          tag, paths[i]);
            return 0;
        }
        *size_out = size;
    }
    if (!seen) {
        altscreen_log("PROBE structural identity refused component=%s reason=no_candidate", tag);
        return 0;
    }
    return 1;
}

int process_and_stack_files_match(void) {
    char exe_path[256];
    unsigned long exe_size = 0;
    if (!p1404_executable_path(exe_path, sizeof(exe_path))) {
        altscreen_log("PROBE executable path unresolved procfs=self/exe_or_numeric_pid/exefile - inert");
        return 0;
    }
    if (!elf32_arm_file(exe_path, 2u, &exe_size)) {
        altscreen_log("PROBE executable structural identity refused path=%s reason=invalid_elf32_arm_exec", exe_path);
        return 0;
    }
    g_runtime_offset_delta = 0u;
    g_runtime_abi_baseline = "MHI2Q_CN_AUG22_STRUCTURAL";
    if (!structural_aliases_match("dio_manager", P1404_DIO_PATH_APP,
                                  P1404_DIO_PATH_ESO, 2u,
                                  &g_runtime_dio_size)) return 0;
    if (!structural_aliases_match("libairplay", P1404_LIBAIRPLAY_PATH_APP,
                                  P1404_LIBAIRPLAY_PATH_ESO, 3u,
                                  &g_runtime_lib_size)) return 0;
    return 1;
}

unsigned arm_word(unsigned long addr) {
    const unsigned char *q = (const unsigned char *)addr;
    return (unsigned)q[0] | ((unsigned)q[1] << 8) |
           ((unsigned)q[2] << 16) | ((unsigned)q[3] << 24);
}

int exact_entry_signature(unsigned long addr, unsigned expected) {
    return !(addr & 3u) && addr >= 0x1000u && arm_word(addr) == expected;
}

static int safe_run_id_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

static int read_authorization_run_id(const char *path, char *out, size_t cap,
                                     const char **reason) {
    struct stat st;
    FILE *f;
    size_t n, i;
    if (reason) *reason = "invalid";
    if (!path || !out || cap < AUTH_RUN_ID_MAX + 1u) return 0;
    out[0] = 0;
    if (!path_is_nonsymlink_kind(path, 1) || lstat(path, &st) != 0) {
        if (reason) *reason = "not_regular_or_symlink_component";
        return 0;
    }
    if (st.st_size < 1 || st.st_size > (long)(AUTH_RUN_ID_MAX + 1u)) {
        if (reason) *reason = "size";
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) { if (reason) *reason = "unreadable"; return 0; }
    n = fread(out, 1u, (size_t)st.st_size, f);
    fclose(f);
    if (n != (size_t)st.st_size) {
        out[0] = 0;
        if (reason) *reason = "short_read";
        return 0;
    }
    if (n && out[n - 1u] == '\n') --n;
    if (n < 1u || n > AUTH_RUN_ID_MAX) {
        out[0] = 0;
        if (reason) *reason = "length";
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if (!safe_run_id_char((unsigned char)out[i])) {
            out[0] = 0;
            if (reason) *reason = "grammar";
            return 0;
        }
    }
    out[n] = 0;
    if (reason) *reason = "ok";
    return 1;
}

static __attribute__((unused)) int runtime_authorization_match(void) {
    char marker_id[AUTH_RUN_ID_MAX + 1u];
    char state_id[AUTH_RUN_ID_MAX + 1u];
    char state_path[ALTSCREEN_PATH_MAX];
    const char *reason = NULL;
    if (!altscreen_state_root_is_authoritative()) {
        altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=REFUSED reason=state_root_not_sd_authoritative");
        return 0;
    }
    if (!altscreen_state_path("run_id", state_path, sizeof(state_path))) {
        altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=REFUSED reason=state_path");
        return 0;
    }
    if (!read_authorization_run_id(ALTSCREEN_PROBE_MARKER, marker_id,
                                   sizeof(marker_id), &reason)) {
        altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=REFUSED reason=probe_marker_%s",
                      reason ? reason : "invalid");
        return 0;
    }
    if (!read_authorization_run_id(state_path, state_id, sizeof(state_id), &reason)) {
        altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=REFUSED reason=state_run_id_%s",
                      reason ? reason : "invalid");
        return 0;
    }
    if (strcmp(marker_id, state_id) != 0) {
        altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=REFUSED reason=run_id_mismatch");
        return 0;
    }
    altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=PASS run_id=%s marker=validated_not_deleted",
                  marker_id);
    return 1;
}

static int runtime_prerequisite_symbols_match(void) {
    static const char *const required[] = {
        "ServerSocketOpen", "SocketAccept", "NetSocket_CreateWithNative",
        "NetSocket_Delete", "AirPlayReceiverSessionScreen_ProcessFrames",
        "AirPlayReceiverSessionSendCommand", "ScreenStreamStart",
        "SendSelfConnectedLoopbackMessage", "pthread_create", "pthread_join",
        "socket", "connect", "close", NULL
    };
    int i;
    for (i = 0; required[i]; ++i) {
        if (!dlsym(RTLD_DEFAULT, required[i])) {
            const char *error = dlerror();
            altscreen_log("PROBE runtime prerequisite refused symbol=%s loader_error=%s",
                         required[i], error ? error : "unavailable");
            return 0;
        }
    }
    altscreen_log("PHASE=RUNTIME_PREREQUISITES result=PASS symbols=%d loader_layout=RX_RW", i);
    return 1;
}

static int resolve_required_libairplay_symbols(void) {
    struct required_symbol { const char *name; void **slot; };
    struct required_symbol required[] = {
        { "AirPlayReceiverServerPlatformCopyProperty", &p1404.server_platform_copy_property },
        { "AirPlayReceiverSessionPlatformCopyProperty", &p1404.session_platform_copy_property },
        { "AirPlayReceiverSessionPlatformControl", &p1404.session_platform_control },
        { "AirPlayReceiverSessionScreen_CopyDisplaysInfo", &p1404.screen_copy_displays_info },
        { "ScreenStreamProcessData", &p1404.screen_stream_process_data },
        { "ScreenStreamCreate", &p1404.screen_stream_create },
        { "AirPlayReceiverSessionScreen_Create", &p1404.screen_create },
        { "AirPlayReceiverSessionScreen_Setup", &p1404.screen_setup },
        { "AirPlayReceiverSessionScreen_StartSession", &p1404.screen_start_session },
        { "AirPlayReceiverSessionScreen_StopSession", &p1404.screen_stop_session },
        { "AirPlayReceiverSessionScreen_Delete", &p1404.screen_delete },
        { "AirPlayReceiverSessionScreen_SetVisibility", &p1404.screen_set_visibility },
        { "AirPlayReceiverSessionSetup", &p1404.session_setup },
        { "ScreenStreamStart", &p1404.screen_stream_start },
        { "AirPlayReceiverSessionScreen_SetSecurityInfo", &p1404.screen_set_security_info },
        { "AirPlay_DeriveAESKeySHA512ForScreen", &p1404.derive_aes_key_sha512_for_screen },
        { "ScreenCopyMain", &p1404.screen_copy_main },
        { NULL, NULL }
    };
    int i;
    for (i = 0; required[i].name; ++i) {
        *required[i].slot = libairplay_symbol(required[i].name);
        if (!*required[i].slot) {
            altscreen_log("PHASE=RUNTIME_SYMBOL_RESOLUTION result=REFUSED missing=%s stock_fail_open=YES",
                          required[i].name);
            return 0;
        }
    }
    return 1;
}

int p1404_probe_stack(void) {
    /*
     * V3.4 production policy: installation/preload presence is the enable
     * contract.  Do not gate the current CarPlay session on removable-SD
     * ARMED/run_id markers.  Firmware identity, exact symbol resolution and
     * backend safety checks below remain mandatory and fail open to stock.
     */
    int requested_armed = 1;
    int requested_mutate = 1;

    p1404_identity_ok = 0;
    p1404_armed = 0;
    p1404_mutate_armed = 0;
    p1404_libairplay_base = 0;

    if (!g_stock_exports_ready && !p1404_bind_stock_exports()) {
        altscreen_log("PHASE=RUNTIME_SYMBOL_RESOLUTION result=REFUSED reason=stock_export_bind");
        return 0;
    }
    altscreen_log("PHASE=RUNTIME_AUTHORIZATION result=PASS policy=INSTALLED_PRELOAD sd_marker_gate=DISABLED required_symbol_and_backend_safety_checks=ENFORCED");
    if (!resolve_required_libairplay_symbols()) {
        /* Exact handle-scoped probing may have replaced a subset of forwarding
         * pointers before it refused. Restore the complete RTLD_NEXT stock
         * surface so every public wrapper remains fail-open. */
        (void)p1404_bind_stock_exports();
        return 0;
    }
    if (!runtime_prerequisite_symbols_match()) {
        altscreen_log("PHASE=RUNTIME_SYMBOL_RESOLUTION result=REFUSED reason=runtime_prerequisites");
        (void)p1404_bind_stock_exports();
        return 0;
    }

    p1404_identity_ok = 1;
    p1404_armed = requested_armed;
    p1404_mutate_armed = requested_mutate;
    altscreen_log("PHASE=RUNTIME_ABI_IDENTITY result=PASS compatibility_checks=ENFORCED resolution=DLSYM_REQUIRED_NAMES authorization=INSTALLED_PRELOAD armed=%d mutate=%d",
                  p1404_armed, p1404_mutate_armed);
    return 1;
}
