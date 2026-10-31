#ifndef ALTSCREEN_STREAM_SUPERVISOR_LEASE_H
#define ALTSCREEN_STREAM_SUPERVISOR_LEASE_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Boot/manual START election; no PID-file race or stale filesystem lock.
 * Only the shell keeps fd9. Display/renderer children explicitly close it. */
static int stream_supervisor_exec(const char *script) {
    struct sockaddr_in address;
    int fd;
    char owner[32], *end = NULL;
    const char *value = getenv("ALT111_STREAM_LEASE_PORT");
    long port = value ? strtol(value, &end, 10) : 19802;
    if (!script || !*script || (value && (!*value || *end)) || port < 1 || port > 65535)
        return 2;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=socket errno=%d\n", errno); return 2; }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int error = errno;
        close(fd);
        if (error == EADDRINUSE) {
            fprintf(stderr, "STREAM_SUPERVISOR=ALREADY_RUNNING lease_port=%ld verified_owner=UNKNOWN reason=port_occupied\n", port);
            return 0;
        }
        fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=bind port=%ld errno=%d\n", port, error);
        return 2;
    }
    if (fd != 9) {
        if (dup2(fd, 9) < 0) { fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=dup2 errno=%d\n", errno); close(fd); return 2; }
        close(fd);
    }
    if (fcntl(9, F_SETFD, 0) < 0) { fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=fcntl errno=%d\n", errno); return 2; }
    snprintf(owner, sizeof(owner), "%ld", (long)getpid());
    if (setenv("ALT111_STREAM_SUPERVISOR_OWNER", owner, 1) != 0) { fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=setenv errno=%d\n", errno); return 2; }
    execl("/bin/sh", "sh", script, "--lease-held", (char *)0);
    fprintf(stderr, "STREAM_SUPERVISOR=FAIL stage=exec errno=%d\n", errno);
    return 2;
}
#endif
