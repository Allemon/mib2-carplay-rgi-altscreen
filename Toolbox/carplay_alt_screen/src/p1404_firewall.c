/* p1404_firewall.c - dynamic, exact-port PF mutation for private stream111. */
#include "p1404_firewall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>
#if defined(QSHIM_FCNTL_H)
/* QNX 6.5 SDK: sockaddr len/family bytes, F_SETFD=2, FD_CLOEXEC=1. */
extern int socket(int, int, int);
extern int bind(int, const void *, unsigned);
extern int fcntl(int, int, ...);
struct pf_sockaddr { uint8_t len, family; uint16_t port; uint8_t addr[4], zero[8]; };
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

extern void altscreen_log(const char *fmt, ...);

#define PF_CAPTURE_MAX (128u * 1024u)
#define PF_TRANSACTION_MS 1500u
#define PF_OPEN_TRANSACTION_MS 3000u
#define PF_OPEN_HELPER_MS 1500u
#define PF_REAP_MS 100u
static volatile unsigned g_pf_guard;
static volatile unsigned g_pf_retry_guard;
static uint32_t g_pf_cleanup_pending[65536];
static unsigned g_pf_cleanup_count;
static int g_pf_maintenance_started;
static uint64_t g_pf_maintenance_attempt_ms;
extern void fullchain_deferred_retry_tick(void) __attribute__((weak));
extern void altscreen_bearer_retry_close(void) __attribute__((weak));
static void pf_queue_cleanup(uint16_t port, uint32_t generation);
static void *pf_maintenance_worker(void *unused);
/* Protected by the same lock as rule mutations. Fixed size, no queue or heap
 * lifetime; a later open of the same port invalidates an older cleanup lease. */
static uint32_t g_pf_port_generations[65536];
/* A lease owns cleanup only if this process actually added the rule. A
 * pre-existing OEM/other-tool pass may permit our connection, but is not ours
 * to delete when that connection stops. Same-process reopens retain ownership. */
static unsigned char g_pf_port_rule_added[65536];
static uint32_t g_pf_generation;
/* Kernel ownership survives thread overlap and vanishes on process exit.
 * TCP 19803 serializes whole-rule transactions; UDP loopback on the exact
 * video port reserves cleanup ownership without conflicting with its TCP
 * listener. No SO_REUSE*, marker file, directory, or shared-memory ABI. */
static int g_pf_transaction_fd = -1;
static int g_pf_owner_fds[65536]; /* stored as fd+1, zero means unowned */

static int pf_bind_token(uint16_t port, int datagram) {
    int fd, saved;
#if defined(QSHIM_FCNTL_H)
    struct pf_sockaddr address;
    fd = socket(2, datagram ? 2 : 1, datagram ? 17 : 6);
    memset(&address, 0, sizeof(address));
    address.len = sizeof(address); address.family = 2;
    address.addr[0] = 127; address.addr[3] = 1;
    ((unsigned char *)&address.port)[0] = (unsigned char)(port >> 8);
    ((unsigned char *)&address.port)[1] = (unsigned char)port;
#else
    struct sockaddr_in address;
    fd = socket(AF_INET, datagram ? SOCK_DGRAM : SOCK_STREAM, datagram ? IPPROTO_UDP : IPPROTO_TCP);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
#endif
    if (fd < 0) return -1;
    if (fcntl(fd, 2 /* F_SETFD */, 1 /* FD_CLOEXEC */) == 0 &&
        bind(fd, (const void *)&address, sizeof(address)) == 0) return fd;
    saved = errno; close(fd); errno = saved; return -1;
}
static int g_pending_child = -1; /* guarded; never accumulate timed-out children */
/* Diagnostics are transaction-local under g_pf_guard. */
static const char *g_pf_stage;
static const char *g_pf_program;
static const char *g_pf_command;
static int g_pf_status;
static char g_pf_error[384];
static uint64_t pf_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (unsigned long)ts.tv_nsec / 1000000u;
}
static int pf_expired(uint64_t deadline) {
    uint64_t now = pf_now_ms();
    if (!now || now >= deadline) { errno = ETIMEDOUT; return 1; }
    return 0;
}
static int pf_lock(uint64_t deadline) {
    for (;;) {
        if (pf_expired(deadline)) return 0;
        if (__sync_lock_test_and_set(&g_pf_guard, 1u) == 0u) {
            do {
                g_pf_transaction_fd = pf_bind_token(19803u, 0);
                if (g_pf_transaction_fd >= 0) return 1;
                usleep(5000);
            } while (!pf_expired(deadline));
            __sync_lock_release(&g_pf_guard);
            return 0;
        }
        usleep(5000);
    }
}
static void pf_unlock(void) {
    if (g_pf_transaction_fd >= 0) close(g_pf_transaction_fd);
    g_pf_transaction_fd = -1;
    __sync_lock_release(&g_pf_guard);
}

static int pf_token(const char **cursor, const char *end, const char *word) {
    const char *p = *cursor;
    size_t len = strlen(word);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
    if ((size_t)(end - p) < len || memcmp(p, word, len)) return 0;
    p += len;
    if (p != end && *p != ' ' && *p != '\t' && *p != '\r') return 0;
    *cursor = p;
    return 1;
}
static int is_blocker(const char *line, size_t n) {
    const char *p = line, *end = line + n;
    if (!pf_token(&p, end, "block")) return 0;
    (void)(pf_token(&p, end, "drop") || pf_token(&p, end, "return"));
    if (!pf_token(&p, end, "in")) return 0;
    (void)pf_token(&p, end, "quick");
    if (!pf_token(&p, end, "on") || !pf_token(&p, end, "carplay0")) return 0;
    /* A family-specific or qualified rule is not an all-family terminal block. */
    if (!pf_token(&p, end, "all")) return 0;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
    return p == end;
}

static int port_token_matches(const char *line, size_t n, const char *prefix, size_t len) {
    return n >= len && !memcmp(line, prefix, len) &&
           (n == len || line[len] == ' ' || line[len] == '\t' || line[len] == '\r');
}
static int is_our_rule(const char *line, size_t n, uint16_t port) {
    char a[160], b[160];
    const char *tail, *end = line + n;
    int na = snprintf(a, sizeof(a),
        "pass in quick on carplay0 proto tcp from any to any port = %u", (unsigned)port);
    int nb = snprintf(b, sizeof(b),
        "pass in quick on carplay0 proto tcp from any to any port %u", (unsigned)port);
    if (na <= 0 || nb <= 0) return 0;
    if (port_token_matches(line, n, a, (size_t)na)) tail = line + na;
    else if (port_token_matches(line, n, b, (size_t)nb)) tail = line + nb;
    else return 0;
    /* Recognize only the complete canonical rule that we emit. Tags, labels,
     * extra constraints and state options belong to another rule owner. */
    if (!pf_token(&tail, end, "keep") || !pf_token(&tail, end, "state")) return 0;
    while (tail < end && (*tail == ' ' || *tail == '\t' || *tail == '\r')) ++tail;
    return tail == end;
}

static int append_bytes(char *out, size_t cap, size_t *used,
                        const char *src, size_t n) {
    if (!out || !used || !src || *used + n + 1u > cap) return 0;
    memcpy(out + *used, src, n);
    *used += n;
    out[*used] = 0;
    return 1;
}

int p1404_alt111_firewall_rewrite(const char *rules, uint16_t port, int enable,
                                  char *out, size_t out_cap, int *changed_out) {
    const char *cur, *nl;
    size_t used = 0;
    int found = 0, blocker = 0, changed = 0, blocked = 0;
    char rule[192];
    int rule_n;
    if (changed_out) *changed_out = 0;
    if (!rules || !port || !out || out_cap < 2u) return 0;
    out[0] = 0;
    for (cur = rules; *cur; cur = nl ? nl + 1 : cur + strlen(cur)) {
        size_t n;
        nl = strchr(cur, '\n');
        n = nl ? (size_t)(nl - cur) : strlen(cur);
        if (is_blocker(cur, n)) blocked = 1;
        if (!blocked && is_our_rule(cur, n, port)) found = 1;
        if (!nl) break;
    }
    if (enable && found) {
        if (strlen(rules) + 1u > out_cap) return 0;
        memcpy(out, rules, strlen(rules) + 1u);
        return 1;
    }
    rule_n = snprintf(rule, sizeof(rule),
        "pass in quick on carplay0 proto tcp from any to any port = %u keep state\n",
        (unsigned)port);
    if (rule_n <= 0 || (size_t)rule_n >= sizeof(rule)) return 0;
    cur = rules;
    while (*cur) {
        size_t n;
        nl = strchr(cur, '\n');
        n = nl ? (size_t)(nl - cur) : strlen(cur);
        /* Opening only inserts our aperture. A pre-existing identical rule
         * after a terminal block is ineffective but still belongs to its old
         * owner. Cleanup removes at most our first effective aperture. */
        if (!enable && !blocker && !changed && is_our_rule(cur, n, port)) {
            changed = 1;
        } else {
            if (is_blocker(cur, n)) {
                if (enable && !blocker) {
                    if (!append_bytes(out, out_cap, &used, rule, (size_t)rule_n)) return 0;
                    changed = 1;
                }
                blocker = 1;
            }
            if (!append_bytes(out, out_cap, &used, cur, n)) return 0;
            if (nl && !append_bytes(out, out_cap, &used, "\n", 1u)) return 0;
        }
        if (!nl) break;
        cur = nl + 1;
    }
    if (enable && !blocker) return 0; /* fail closed if expected terminal block moved */
    if (changed_out) *changed_out = changed;
    return 1;
}

static int is_static_rule(const char *line, size_t n, uint16_t port) {
    static const char *const words[] = {
        "pass", "in", "quick", "on", "carplay0", "proto", "tcp",
        "from", "any", "to", "any", "port", "="
    };
    const char *p = line, *end = line + n;
    char number[8];
    unsigned i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (!pf_token(&p, end, words[i])) return 0;
    snprintf(number, sizeof(number), "%u", (unsigned)port);
    if (!pf_token(&p, end, number) || !pf_token(&p, end, "allow-opts")) return 0;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
    return p == end;
}

int p1404_alt111_static_rule_present(const char *conf, uint16_t port) {
    const char *cur, *nl;
    if (!conf || !port) return 0;
    for (cur = conf; *cur; cur = nl + 1) {
        size_t n;
        nl = strchr(cur, '\n');
        n = nl ? (size_t)(nl - cur) : strlen(cur);
        if (is_blocker(cur, n)) return 0; /* a pass after the block is dead */
        if (is_static_rule(cur, n, port)) return 1;
        if (!nl) break;
    }
    return 0;
}

int p1404_alt111_static_rule_loaded(uint16_t port) {
    char *buf;
    size_t used = 0;
    ssize_t got;
    int fd, present = 0;
    fd = open("/etc/pf.conf", O_RDONLY);
    if (fd < 0) return 0;
    buf = (char *)malloc(PF_CAPTURE_MAX + 1u);
    if (buf) {
        do {
            got = read(fd, buf + used, PF_CAPTURE_MAX - used);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) break;
            used += (size_t)got;
        } while (used < PF_CAPTURE_MAX);
        if (got >= 0 && used < PF_CAPTURE_MAX && !memchr(buf, 0, used)) {
            buf[used] = 0;
            present = p1404_alt111_static_rule_present(buf, port);
        }
        free(buf);
    }
    close(fd);
    return present;
}

/* QNX spawnp maps stdio and the transaction socket into the helper. If the
 * receiver dies mid-reload, its helper keeps the lock until it exits, so a
 * stale snapshot cannot race the next receiver. Port-owner sockets stay CLOEXEC.
 * No fork in the multithreaded
 * receiver, no shell, and no blocking pipe read/pclose/system in teardown. */
static const char *pf_program(void) {
#if defined(QSHIM_SPAWN_H) || defined(__QNXNTO__) || defined(ALT111_PF_QNX_TEST)
    /* dio_manager's inherited PATH need not include the administrative tools.
     * The boot recorder already proves /armle/sbin/pfctl on the target. */
    static const char *const paths[] = {
        "/armle/sbin/pfctl", "/mnt/app/armle/sbin/pfctl",
        "/sbin/pfctl", "/usr/sbin/pfctl", "/proc/boot/pfctl"
    };
    unsigned i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
        if (access(paths[i], 1 /* QNX X_OK */) == 0) return paths[i];
    errno = ENOENT;
    return NULL;
#else
    return "pfctl";
#endif
}

static int pf_spawn(const char *program, char *const argv[], int output,
                    int null_fd, int error_fd) {
#if defined(QSHIM_SPAWN_H) || defined(__QNXNTO__) || defined(ALT111_PF_QNX_TEST)
    int map[4];
    map[0] = null_fd; map[1] = output; map[2] = error_fd;
    map[3] = g_pf_transaction_fd;
    return spawnp(program, 4, map, NULL, argv, NULL);
#else
    /* Host tests exercise the same deadline/locking/rules code. */
    posix_spawn_file_actions_t actions;
    pid_t child;
    int rc;
    extern char **environ;
    rc = posix_spawn_file_actions_init(&actions);
    if (rc) { errno = rc; return -1; }
    rc = posix_spawn_file_actions_adddup2(&actions, null_fd, 0);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, output, 1);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, error_fd, 2);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, g_pf_transaction_fd, 3);
    if (!rc) rc = posix_spawnp(&child, program, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc) { errno = rc; return -1; }
    return (int)child;
#endif
}

static int run_pf(char *const argv[], int output, uint64_t deadline) {
    int child, null_fd, error_fd = -1, status, result, ok = 0, saved;
    char error_path[96];
    ssize_t bytes;
    unsigned i;
    uint64_t reap_deadline;
    if (g_pending_child > 0) {
        result = waitpid(g_pending_child, &status, WNOHANG);
        if (result == 0 || (result < 0 && errno != ECHILD)) {
            errno = ETIMEDOUT; return 0;
        }
        g_pending_child = -1;
    }
    if (pf_expired(deadline)) return 0;
    g_pf_command = argv[1];
    g_pf_stage = "resolve_helper";
    g_pf_status = -1; g_pf_error[0] = 0;
    g_pf_program = pf_program();
    if (!g_pf_program) return 0;
    g_pf_stage = "open_helper_stdio";
    null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) return 0;
    snprintf(error_path, sizeof(error_path), "/tmp/carplay_alt111_pf_%ld.stderr",
             (long)getpid());
    error_fd = open(error_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (error_fd < 0) { saved = errno; close(null_fd); errno = saved; return 0; }
    unlink(error_path);
    g_pf_stage = "spawn_helper";
    child = pf_spawn(g_pf_program, argv, output >= 0 ? output : null_fd,
                     null_fd, error_fd);
    saved = errno;
    close(null_fd);
    errno = saved;
    if (child < 0) goto done;
    g_pf_stage = "wait_helper";
    for (;;) {
        result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            g_pf_status = status;
            if (status == 0) { ok = 1; goto done; }
            g_pf_stage = "helper_exit";
            errno = EIO; goto done;
        }
        if (result < 0 && errno == ECHILD) goto done;
        if (result < 0 && errno != EINTR) break;
        if (pf_expired(deadline)) break;
        usleep(5000);
    }
    saved = errno;
    kill(child, 9); /* SIGKILL, identical in QNX and host */
    reap_deadline = pf_now_ms() + PF_REAP_MS;
    for (;;) {
        result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) break;
        if (pf_expired(reap_deadline)) { g_pending_child = child; break; }
        usleep(5000);
    }
    errno = saved;
done:
    saved = errno;
    if (lseek(error_fd, 0, SEEK_SET) >= 0) {
        bytes = read(error_fd, g_pf_error, sizeof(g_pf_error) - 1u);
        if (bytes > 0) {
            g_pf_error[bytes] = 0;
            for (i = 0; i < (unsigned)bytes; ++i)
                if ((unsigned char)g_pf_error[i] < 32u) g_pf_error[i] = ' ';
        }
    }
    close(error_fd);
    errno = saved;
    return ok;
}

static char *capture_rules(uint16_t port, uint64_t deadline) {
    char path[96], *buf = NULL;
    char *argv[] = { "pfctl", "-sr", NULL };
    size_t used = 0;
    int fd, saved;
    ssize_t n;
    snprintf(path, sizeof(path), "/tmp/carplay_alt111_pf_%ld_%u.capture",
             (long)getpid(), (unsigned)port);
    g_pf_stage = "open_capture";
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return NULL;
    unlink(path); /* anonymous regular file; a stalled helper cannot hold EOF */
    if (!run_pf(argv, fd, deadline)) goto done;
    g_pf_stage = "read_capture";
    if (lseek(fd, 0, SEEK_SET) < 0) goto done;
    buf = (char *)malloc(PF_CAPTURE_MAX + 1u);
    if (!buf) goto done;
    do {
        n = read(fd, buf + used, PF_CAPTURE_MAX - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto bad;
        if (!n) break;
        used += (size_t)n;
    } while (used < PF_CAPTURE_MAX);
    if (!used || used == PF_CAPTURE_MAX || memchr(buf, 0, used)) goto bad;
    buf[used] = 0;
    goto done;
bad:
    free(buf); buf = NULL; errno = EIO;
done:
    saved = errno; close(fd); errno = saved;
    return buf;
}

static int apply_rule(uint16_t port, int enable, struct p1404_pf_lease *lease,
                      int *port_conflict) {
    char *before = NULL, *after = NULL;
    char path[96];
    char *argv[] = { "pfctl", "-f", path, NULL };
    FILE *fp = NULL;
    size_t cap;
    int changed = 0, ok = 0, n, saved, write_ok, new_owner = -1, reload_attempted = 0;
    uint32_t uncertain_generation = 0;
    uint64_t start, deadline, command_deadline;
    if (port_conflict) *port_conflict = 0;
    if (!port || (!enable && lease && !lease->generation)) return 0;
    start = pf_now_ms();
    if (!start) { errno = EIO; return 0; }
    /* Opening needs a read AND a reload. Do not spend the reload's entire
     * budget waiting for the initial read. Cleanup remains bounded at 1.5s,
     * after stock audio has stopped. All retries share this absolute deadline. */
    deadline = start + (enable ? PF_OPEN_TRANSACTION_MS : PF_TRANSACTION_MS);
    if (!pf_lock(deadline)) {
        altscreen_log("ERROR PHASE=ALT111_PF_TRANSACTION port=%u enable=%d stage=lock errno=%d", (unsigned)port, enable, errno);
        return 0;
    }
    g_pf_stage = "capture_rules"; g_pf_program = NULL;
    g_pf_command = "none";
    g_pf_status = -1; g_pf_error[0] = 0;
    if (lease && !enable && g_pf_port_generations[port] != lease->generation) {
        g_pf_stage = "stale_cleanup_skipped";
        ok = 1;
        goto done;
    }
    if (lease && !enable && !g_pf_port_rule_added[port]) {
        g_pf_stage = "preexisting_rule_preserved";
        ok = 1;
        goto done;
    }
    if (enable && g_pf_generation == UINT32_MAX) {
        /* Never wrap an ownership identity onto a still-pending lease. */
        errno = EIO;
        goto done;
    }
    /* An old process cannot reload/remove a port held by another process.
     * A same-process reopen reuses the kernel token and advances generation.
     * The kernel picks the TCP listener port without checking UDP, so an
     * unrelated UDP socket on the same number (seen on P1201: udp *.65495)
     * also lands here. Nothing has run yet; report it so the caller can move
     * the listener to another port instead of failing the session. */
    if (!g_pf_owner_fds[port]) {
        g_pf_stage = "port_ownership";
        new_owner = pf_bind_token(port, 1);
        if (new_owner < 0) {
            if (enable && port_conflict) *port_conflict = 1;
            goto done;
        }
    }
    command_deadline = enable ? pf_now_ms() + PF_OPEN_HELPER_MS : deadline;
    if (command_deadline > deadline) command_deadline = deadline;
    before = capture_rules(port, command_deadline);
    if (!before && enable && errno == ETIMEDOUT && !pf_expired(deadline)) {
        altscreen_log("WARN PHASE=ALT111_PF_OPEN_RECHECK port=%u reason=read_timeout remaining_budget_ms=%llu",
                      (unsigned)port, (unsigned long long)(deadline - pf_now_ms()));
        before = capture_rules(port, deadline);
    }
    if (!before) goto done;
    cap = strlen(before) + 256u;
    after = (char *)malloc(cap);
    if (!after) goto done;
    g_pf_stage = "rewrite_rules";
    if (!p1404_alt111_firewall_rewrite(before, port, enable, after, cap, &changed)) {
        errno = EINVAL;
        goto done;
    }
    if (!changed) { ok = 1; goto done; }
    n = snprintf(path, sizeof(path), "/tmp/carplay_alt111_pf_%ld_%u.conf",
                 (long)getpid(), (unsigned)port);
    if (n <= 0 || (size_t)n >= sizeof(path)) goto done;
    g_pf_stage = "write_rules";
    fp = fopen(path, "wb");
    if (!fp) goto done;
    write_ok = fwrite(after, 1u, strlen(after), fp) == strlen(after);
    if (fclose(fp) != 0) write_ok = 0;
    if (!write_ok) {
        fp = NULL; unlink(path); goto done;
    }
    fp = NULL;
    command_deadline = enable ? pf_now_ms() + PF_OPEN_HELPER_MS : deadline;
    if (command_deadline > deadline) command_deadline = deadline;
    reload_attempted = 1;
    if (run_pf(argv, -1, command_deadline)) ok = 1;
    else if (enable && errno == ETIMEDOUT && !pf_expired(deadline)) {
        /* A timed-out helper may already have committed its atomic PF reload.
         * Read live rules before deciding. Never retry a stale whole-rules
         * snapshot, advertise an unverified port, or disable the firewall. */
        char *live;
        int needs_change = 1;
        saved = errno;
        live = capture_rules(port, deadline);
        if (live) {
            size_t verify_cap = strlen(live) + 256u;
            char *verify = (char *)malloc(verify_cap);
            if (verify && p1404_alt111_firewall_rewrite(live, port, 1,
                                                       verify, verify_cap, &needs_change) &&
                !needs_change) {
                ok = 1;
                g_pf_stage = "open_verified_after_timeout";
            }
            free(verify);
            free(live);
        }
        if (!ok) errno = saved;
    }
    saved = errno;
    unlink(path);
    errno = saved;
done:
    saved = errno;
    if (fp) fclose(fp);
    free(after); free(before);
    if (enable && ok) {
        if (changed) g_pf_port_rule_added[port] = 1;
        g_pf_port_generations[port] = ++g_pf_generation;
        if (new_owner >= 0) { g_pf_owner_fds[port] = new_owner + 1; new_owner = -1; }
        if (lease) { lease->port = port; lease->generation = g_pf_generation; }
    } else if (enable && reload_attempted && changed && g_pf_generation != UINT32_MAX) {
        /* A failed/timed-out reload may have committed. Preserve ownership of
         * that possible aperture until a fresh read+remove succeeds. Never
         * advertise this port or call an uncertain addition pre-existing. */
        g_pf_port_rule_added[port] = 1;
        g_pf_port_generations[port] = uncertain_generation = ++g_pf_generation;
        if (new_owner >= 0) { g_pf_owner_fds[port] = new_owner + 1; new_owner = -1; }
    } else if (!enable && ok && (!lease || g_pf_port_generations[port] == lease->generation)) {
        /* Release ownership only after verified removal (or preservation of a
         * pre-existing rule). A failed close keeps its generation for retry. */
        g_pf_port_generations[port] = 0;
        g_pf_port_rule_added[port] = 0;
        if (g_pf_owner_fds[port]) close(g_pf_owner_fds[port] - 1);
        g_pf_owner_fds[port] = 0;
    }
    if (new_owner >= 0) close(new_owner);
    altscreen_log("%s PHASE=ALT111_PF_TRANSACTION port=%u enable=%d result=%s stage=%s command=%s errno=%d wait_status=%d helper=%s elapsed_ms=%llu detail=%.383s",
                  ok ? "INFO" : "ERROR", (unsigned)port, enable,
                  ok ? "OK" : "FAILED", g_pf_stage, g_pf_command, ok ? 0 : saved,
                  g_pf_status, g_pf_program ? g_pf_program : "unavailable",
                  (unsigned long long)(pf_now_ms() - start), g_pf_error);
    pf_unlock();
    if (uncertain_generation) pf_queue_cleanup(port, uncertain_generation);
    errno = saved;
    return ok;
}

static void pf_retry_lock(void) {
    while (__sync_lock_test_and_set(&g_pf_retry_guard, 1u)) usleep(1000);
}
static void pf_retry_unlock(void) { __sync_lock_release(&g_pf_retry_guard); }

void p1404_alt111_firewall_maintenance_start(void) {
    pthread_t thread;
    int rc;
    uint64_t now = pf_now_ms();
    pf_retry_lock();
    if (g_pf_maintenance_started || (g_pf_maintenance_attempt_ms && now >= g_pf_maintenance_attempt_ms &&
            now - g_pf_maintenance_attempt_ms < 5000u)) { pf_retry_unlock(); return; }
    g_pf_maintenance_started = 1;
    g_pf_maintenance_attempt_ms = now;
    pf_retry_unlock();
    rc = pthread_create(&thread, NULL, pf_maintenance_worker, NULL);
    if (!rc) {
        int (*detach_fn)(pthread_t) = (int (*)(pthread_t))dlsym(RTLD_DEFAULT, "pthread_detach");
        int detach_rc = detach_fn ? detach_fn(thread) : -1;
        if (detach_rc) altscreen_log("WARN PHASE=ALT111_MAINTENANCE_DETACH rc=%d", detach_rc);
    } else {
        pf_retry_lock(); g_pf_maintenance_started = 0; pf_retry_unlock();
        altscreen_log("WARN PHASE=ALT111_MAINTENANCE_START rc=%d pending_cleanup_retained=1 retry_after_ms=5000", rc);
    }
}
static void pf_queue_cleanup(uint16_t port, uint32_t generation) {
    if (!port || !generation) return;
    pf_retry_lock();
    if (!g_pf_cleanup_pending[port]) ++g_pf_cleanup_count;
    if (g_pf_cleanup_pending[port] < generation) g_pf_cleanup_pending[port] = generation;
    pf_retry_unlock();
    p1404_alt111_firewall_maintenance_start();
}
static void *pf_maintenance_worker(void *unused) {
    uint32_t cursor = 1u;
    uint64_t next_pf = 0;
    unsigned delay_seconds = 1u;
    (void)unused;
    for (;;) {
        struct p1404_pf_lease lease = {0, 0};
        uint64_t now;
        unsigned i;
        if (fullchain_deferred_retry_tick) fullchain_deferred_retry_tick();
        if (altscreen_bearer_retry_close) altscreen_bearer_retry_close();
        now = pf_now_ms();
        if (now >= next_pf) {
            pf_retry_lock();
            for (i = 0; g_pf_cleanup_count && i < 65535u; ++i) {
                if (++cursor >= 65536u) cursor = 1u;
                if (g_pf_cleanup_pending[cursor]) {
                    lease.port = (uint16_t)cursor;
                    lease.generation = g_pf_cleanup_pending[cursor];
                    break;
                }
            }
            pf_retry_unlock();
            if (lease.generation) {
                int ok = apply_rule(lease.port, 0, &lease, NULL);
                if (ok) {
                    pf_retry_lock();
                    if (g_pf_cleanup_pending[lease.port] == lease.generation) { g_pf_cleanup_pending[lease.port] = 0; --g_pf_cleanup_count; }
                    pf_retry_unlock();
                    delay_seconds = 1u;
                    altscreen_log("PHASE=ALT111_PF_CLEANUP_RETRY port=%u generation=%u result=OK ownership_released=1", lease.port, lease.generation);
                } else if (delay_seconds < 30u) {
                    delay_seconds *= 2u;
                    if (delay_seconds > 30u) delay_seconds = 30u;
                }
                next_pf = pf_now_ms() + 1000u * delay_seconds;
            }
        }
        usleep(1000000u);
    }
    return NULL;
}

int p1404_alt111_firewall_open(uint16_t port) { p1404_alt111_firewall_maintenance_start(); return apply_rule(port, 1, NULL, NULL); }
int p1404_alt111_firewall_close(uint16_t port) { return apply_rule(port, 0, NULL, NULL); }
int p1404_alt111_firewall_open_owned_ex(uint16_t port, struct p1404_pf_lease *lease,
                                        int *port_conflict) {
    if (port_conflict) *port_conflict = 0;
    if (!lease) return 0;
    memset(lease, 0, sizeof(*lease));
    p1404_alt111_firewall_maintenance_start();
    return apply_rule(port, 1, lease, port_conflict);
}
int p1404_alt111_firewall_open_owned(uint16_t port, struct p1404_pf_lease *lease) {
    return p1404_alt111_firewall_open_owned_ex(port, lease, NULL);
}
int p1404_alt111_firewall_close_owned(const struct p1404_pf_lease *lease) {
    struct p1404_pf_lease copy;
    if (!lease) return 0;
    copy = *lease;
    {
        int ok = apply_rule(copy.port, 0, &copy, NULL);
        if (!ok) pf_queue_cleanup(copy.port, copy.generation);
        return ok;
    }
}
