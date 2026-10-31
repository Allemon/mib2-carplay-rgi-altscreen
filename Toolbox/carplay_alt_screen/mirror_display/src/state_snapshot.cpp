#include "state_snapshot.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

static StateSnapshotResult failed(const char *stage, int error) {
    StateSnapshotResult result = { false, stage, error ? error : EIO, false };
    return result;
}

static StateSnapshotResult write_shared_snapshot(const char *path,
                                                 const char *data, size_t bytes) {
    // /tmp -> /dev/shmem on MHI2Q: rename() returns EXDEV. Use a bounded,
    // self-validating record rather than repeatedly attempting that operation.
    if (bytes > 3072u) return failed("frame", EOVERFLOW);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < bytes; ++i)
        hash = (hash ^ (unsigned char)data[i]) * 16777619u;
    char framed[4096];
    const int header = snprintf(framed, sizeof(framed),
        "snapshot_format=2\nsnapshot_bytes=%lu\nsnapshot_hash=%08lx\n",
        (unsigned long)bytes, (unsigned long)hash);
    if (header <= 0 || (size_t)header + bytes + 40u > sizeof(framed))
        return failed("frame", EOVERFLOW);
    memcpy(framed + header, data, bytes);
    const int tail = snprintf(framed + header + bytes,
        sizeof(framed) - header - bytes, "snapshot_end=%08lx\n", (unsigned long)hash);
    if (tail <= 0) return failed("frame", EIO);
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode))
        return failed("shared_open", ELOOP);
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags, 0644);
    if (fd < 0) return failed("shared_open", errno);
    size_t offset = 0;
    const size_t total = (size_t)header + bytes + (size_t)tail;
    const char *stage = 0;
    int error = 0;
    while (offset < total) {
        const ssize_t n = write(fd, framed + offset, total - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { stage = "shared_write"; error = n < 0 ? errno : EIO; break; }
        offset += (size_t)n;
    }
    if (!stage && fchmod(fd, 0644) != 0) {
        stage = "shared_permissions"; error = errno;
    }
    if (close(fd) != 0 && !stage) { stage = "shared_close"; error = errno; }
    if (stage) return failed(stage, error);
    StateSnapshotResult result = { true, "shared_checked", 0, false };
    return result;
}

StateSnapshotResult write_state_snapshot(const char *path, const char *data,
                                         size_t bytes) {
    if (!path || !*path || !data || !bytes) return failed("format", EINVAL);
    char temporary[512];
    const int n = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(temporary))
        return failed("path", ENAMETOOLONG);

    /* Unique O_EXCL temporary files avoid a stale .tmp owned by another uid
     * and do not follow a pre-existing .tmp symlink. No /tmp directory needed. */
    const int fd = mkstemp(temporary);
    if (fd < 0) return failed("create", errno);
    const char *stage = 0;
    int error = 0;
    size_t offset = 0;
    while (offset < bytes) {
        const ssize_t written = write(fd, data + offset, bytes - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            stage = "write";
            error = written < 0 ? errno : EIO;
            break;
        }
        offset += (size_t)written;
    }
    if (!stage && fchmod(fd, 0644) != 0) {
        stage = "permissions";
        error = errno;
    }
    if (close(fd) != 0 && !stage) {
        stage = "close";
        error = errno;
    }
    if (!stage && rename(temporary, path) != 0) {
        stage = "rename";
        error = errno;
    }
    if (stage && strcmp(stage, "rename") == 0 &&
        (error == EXDEV || ((error == ENOSYS || error == EINVAL || error == ENOTSUP) &&
         (strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/dev/shmem/", 11) == 0)))) {
        (void)unlink(temporary);
        return write_shared_snapshot(path, data, bytes);
    }
    if (stage) {
        /* Never unlink the published file: readers retain the last good
         * snapshot even if replacement is denied or storage is full. */
        (void)unlink(temporary);
        return failed(stage, error);
    }
    StateSnapshotResult result = { true, "complete", 0, true };
    return result;
}

StateSnapshotPublisher::StateSnapshotPublisher()
    : attempts_(0), failures_(0), consecutive_failures_(0),
      last_error_log_ms_(0), have_error_log_(false), have_success_log_(false) {}

bool StateSnapshotPublisher::publish(const char *path, const char *data,
                                     size_t bytes,
                                     unsigned long long monotonic_ms) {
    ++attempts_;
    const StateSnapshotResult result = write_state_snapshot(path, data, bytes);
    if (!result.published) {
        ++failures_;
        ++consecutive_failures_;
        if (!have_error_log_ || monotonic_ms < last_error_log_ms_ ||
            monotonic_ms - last_error_log_ms_ >= 10000ULL) {
            struct stat target;
            const int stat_rc = path ? stat(path, &target) : -1;
            const int stat_error = stat_rc == 0 ? 0 : (path ? errno : EINVAL);
            fprintf(stderr,
                    "direct111: PHASE=DISPLAYABLE3_STATE_PUBLISH result=FAILED "
                    "stage=%s errno=%d error='%s' path='%s' pid=%ld uid=%ld "
                    "euid=%ld target_stat_errno=%d target_mode=%lo "
                    "attempts=%lu failures=%lu consecutive=%lu "
                    "retry=next_observer_tick rate_limit_ms=10000 observe_only=1\n",
                    result.stage, result.error, strerror(result.error),
                    path ? path : "<null>", (long)getpid(), (long)getuid(),
                    (long)geteuid(), stat_error,
                    stat_rc == 0 ? (unsigned long)(target.st_mode & 0777) : 0UL,
                    attempts_, failures_, consecutive_failures_);
            have_error_log_ = true;
            last_error_log_ms_ = monotonic_ms;
        }
        return false;
    }
    if (!have_success_log_ || consecutive_failures_) {
        fprintf(stderr,
                "direct111: PHASE=DISPLAYABLE3_STATE_PUBLISH result=OK "
                "path='%s' pid=%ld uid=%ld euid=%ld bytes=%lu mode=0644 "
                "attempts=%lu failures=%lu recovered=%d atomic_replace=%d "
                "publish_mode=%s observe_only=1\n",
                path, (long)getpid(), (long)getuid(), (long)geteuid(),
                (unsigned long)bytes, attempts_, failures_,
                consecutive_failures_ ? 1 : 0, result.atomic_replace ? 1 : 0,
                result.atomic_replace ? "rename" : "shmem_checked");
        have_success_log_ = true;
    }
    consecutive_failures_ = 0;
    return true;
}
