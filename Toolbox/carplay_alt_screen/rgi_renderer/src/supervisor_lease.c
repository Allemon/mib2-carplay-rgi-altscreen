/* A kernel-owned lease: no /tmp directories, file locks or stale lock deletion.
 * The shell and renderer retain fd 9. It is released even after SIGKILL once
 * the renderer's parent watchdog exits. No listener or remote access is used. */
#include "supervisor_lease.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define RGI_LEASE_FD 9
#define RGI_LEASE_PORT 19801
static pid_t owner;

static int lease_port(void) {
    const char *s = getenv("RGI_SUPERVISOR_LEASE_PORT");
    char *end;
    long n;
    if (!s) return RGI_LEASE_PORT;
    n = strtol(s, &end, 10);
    return *s && !*end && n > 0 && n <= 65535 ? (int)n : -1;
}

int rgi_supervisor_exec(const char *script) {
    struct sockaddr_in addr;
    int fd, port = lease_port();
    char pid[32];
    if (!script || !*script || port < 0) {
        fprintf(stderr, "RGI_SUPERVISOR=FAIL stage=lease_arguments\n");
        return 2;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) goto fail;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    /* Never enable SO_REUSEADDR/PORT: the bind is the atomic election. */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int e = errno;
        close(fd);
        if (e == EADDRINUSE) {
            fprintf(stderr, "RGI_SUPERVISOR=ALREADY_RUNNING lease_port=%d\n", port);
            return 0;
        }
        errno = e;
        goto fail;
    }
    if (fd != RGI_LEASE_FD) {
        if (dup2(fd, RGI_LEASE_FD) < 0) { int e = errno; close(fd); errno = e; goto fail; }
        close(fd);
    }
    if (fcntl(RGI_LEASE_FD, F_SETFD, 0) < 0) goto fail;
    snprintf(pid, sizeof(pid), "%ld", (long)getpid());
    if (setenv("RGI_SUPERVISOR_OWNER", pid, 1) != 0) goto fail;
    fprintf(stderr, "RGI_SUPERVISOR=LEASE_ACQUIRED owner=%s lease_port=%d fd=9\n", pid, port);
    /* Same PID after exec, so existing stream ownership/STOP remains valid. */
    execl("/bin/sh", "sh", script, "--lease-held", (char *)0);
fail:
    fprintf(stderr, "RGI_SUPERVISOR=FAIL stage=lease_exec errno=%d detail=%s\n", errno, strerror(errno));
    return 2;
}

int rgi_supervisor_inherit(void) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char *end;
    const char *s = getenv("RGI_SUPERVISOR_OWNER");
    long n;
    if (!s) return 0; /* Standalone renderer development/test mode. */
    n = strtol(s, &end, 10);
    if (!*s || *end || n <= 1 || (long)(pid_t)n != n ||
        getsockname(RGI_LEASE_FD, (struct sockaddr *)&addr, &len) != 0 ||
        addr.sin_family != AF_INET || ntohs(addr.sin_port) != lease_port() ||
        addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || getppid() != (pid_t)n) {
        fprintf(stderr, "RGI_RENDERER=FAIL stage=supervisor_lease_inherit\n");
        return -1;
    }
    owner = (pid_t)n;
    return 0;
}

int rgi_supervisor_parent_alive(void) {
    return owner == 0 || getppid() == owner;
}
