/*
 * altscreen_paths.h - one state root for every marker, profile and log file.
 *
 * Why: the Toolbox may run from sda0/sda1/sdb0/sdb1 under /fs or the MMX
 * /net/mmx/fs alias, while the hook resolves paths inside dio_manager. If ENABLE
 * writes one namespace and the hook reads another, a marker exists but is never
 * seen. Runtime uses the exact util_mountsd order and requires root-level Toolbox
 * beside current, so a stale Log-only directory on a second card cannot shadow
 * the selected transaction.
 *
 * Discovery runs at most once: the first caller that finds both pieces on one
 * volume publishes that candidate index with a compare-and-swap, and every later
 * caller - including threads that lost the same race - answers with that same
 * root. The published root is never reset, so a caller that observed one root
 * can never be surprised by another one later. Until a root is published the
 * resolver answers with the diagnostic /tmp root, which is never authoritative
 * and never sticky, so a late SD mount still wins.
 *
 * Every pointer returned here points at a compile-time string literal: it
 * remains valid and unchanged for the whole process lifetime, and two callers
 * may hold two different paths at the same time. No returned path is built in a
 * shared buffer, and the resolver uses no heap, TLS, thread, or lock.
 */
#ifndef ALTSCREEN_PATHS_H
#define ALTSCREEN_PATHS_H

#include <stddef.h>

#define ALTSCREEN_PATH_MAX 160

/* Directory holding ACTIVE, ARMED* and IAP2_PROFILE. Logging uses the
 * dedicated volatile namespace returned by altscreen_log_path(). */
const char *altscreen_state_root(void);

/* 1 only for a fixed SD root paired with root-level Toolbox. /tmp is log-only. */
int altscreen_state_root_is_authoritative(void);

/* Build root/leaf into out. Returns 1 on success, 0 if the name does not fit. */
int altscreen_state_path(const char *leaf, char *out, size_t cap);

/* 1 when root/leaf exists. Never consults a second namespace. */
int altscreen_marker_present(const char *leaf);

/* Log the selected root and why. Safe to call repeatedly and from any thread;
 * reports once per root. */
void altscreen_paths_report(void);

/* Ensure the project-owned volatile namespace exists. Returns 1 when the
 * namespace is usable; 0 means altscreen_log_path() falls back to one flat /tmp
 * log for compatibility with QNX providers that cannot create subdirectories. */
int altscreen_prepare_volatile_root(void);

/* Canonical volatile log and profile locations, so nothing hardcodes them. */
const char *altscreen_log_path(void);
/* Optional authoritative SD fallback for the asynchronous writer. Returns NULL
 * while no current SD transaction root is visible. Failure to write this path
 * must never affect CarPlay behavior. */
const char *altscreen_sd_log_path(void);
const char *altscreen_profile_path(void);
#endif
