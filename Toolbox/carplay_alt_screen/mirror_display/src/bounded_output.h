#ifndef BOUNDED_OUTPUT_H
#define BOUNDED_OUTPUT_H
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
/* Launcher stdout/stderr share an O_APPEND file. Truncate the same inode;
 * QNX shared memory cannot rename it. Never change pipes/TTYs or stock slog. */
static void bound_private_output(void) {
    int saved = errno;
    struct stat st;
    if (fstat(STDERR_FILENO, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 1048576) {
        fflush(stderr);
        if (ftruncate(STDERR_FILENO, 0) == 0)
            fprintf(stderr, "PRIVATE_LOG_RESET limit=1048576 mode=same_inode\n");
    }
    errno = saved;
}
#endif
