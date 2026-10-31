/* Exact 32-bit little-endian QNX 6.x legacy stat ABI used by the captured
 * P1404 libc.so.3.  This is not the Linux struct stat layout: target stat/lstat
 * write 72 bytes, st_size is at +8 and st_mode is at +44. */
#ifndef QSHIM_SYS_STAT_H
#define QSHIM_SYS_STAT_H
#include <stddef.h>
#include <stdint.h>

typedef int32_t off_t;
typedef uint32_t mode_t;

struct stat {
    uint32_t st_ino;        /* +0  */
    uint32_t st_ino_hi;     /* +4  */
    int32_t  st_size;       /* +8  */
    int32_t  st_size_hi;    /* +12 */
    uint32_t st_dev;        /* +16 */
    uint32_t st_rdev;       /* +20 */
    uint32_t st_uid;        /* +24 */
    uint32_t st_gid;        /* +28 */
    int32_t  st_mtime;      /* +32 */
    int32_t  st_atime;      /* +36 */
    int32_t  st_ctime;      /* +40 */
    uint32_t st_mode;       /* +44 */
    uint32_t st_nlink;      /* +48 */
    int32_t  st_blocksize;  /* +52 */
    int32_t  st_nblocks;    /* +56 */
    int32_t  st_blksize;    /* +60 */
    int32_t  st_blocks;     /* +64 */
    int32_t  st_blocks_hi;  /* +68 */
};

/* C99-compatible assertions also fire in the freestanding ARM production
 * compile; no host libc stat definition participates in this contract. */
#define QSHIM_STAT_ASSERT(name, condition) \
    typedef char qshim_stat_assert_##name[(condition) ? 1 : -1]
QSHIM_STAT_ASSERT(size_72, sizeof(struct stat) == 72u);
QSHIM_STAT_ASSERT(size_offset_8, offsetof(struct stat, st_size) == 8u);
QSHIM_STAT_ASSERT(mode_offset_44, offsetof(struct stat, st_mode) == 44u);
QSHIM_STAT_ASSERT(last_offset_68, offsetof(struct stat, st_blocks_hi) == 68u);
QSHIM_STAT_ASSERT(off_t_32, sizeof(off_t) == 4u);
QSHIM_STAT_ASSERT(mode_t_32, sizeof(mode_t) == 4u);
#undef QSHIM_STAT_ASSERT

extern int stat(const char *path, struct stat *buf);
extern int fstat(int fd, struct stat *buf);
extern int lstat(const char *path, struct stat *buf);
/* QNX 6.x libc mkdir ABI is the POSIX two-argument form. Keep the declaration
 * here because the freestanding cross-build deliberately uses -nostdinc. */
extern int mkdir(const char *path, mode_t mode);
#define S_ISREG(m) (((m) & 0170000) == 0100000)
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISLNK(m) (((m) & 0170000) == 0120000)
#endif
