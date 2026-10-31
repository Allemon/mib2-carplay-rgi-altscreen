#ifndef QSHIM_SYS_MMAN_H
#define QSHIM_SYS_MMAN_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/*
 * QNX Neutrino 6.x mmap protection bits are NOT the Linux 0x1/0x2 values.
 * P1404 vehicle evidence showed both direct111 SHM writers reaching mmap()
 * and failing before any pixels/codec bytes reached the sidecar.  Keep these
 * exact QNX values in the freestanding universal-hook shim.
 */
#define PROT_NONE   0x00000000
#define PROT_READ   0x00000100
#define PROT_WRITE  0x00000200
#define PROT_EXEC   0x00000400
#define PROT_NOCACHE 0x00000800

#define MAP_SHARED  0x00000001
#define MAP_PRIVATE 0x00000002
#define MAP_FAILED ((void *)-1)

extern int shm_open(const char *name, int oflag, unsigned int mode);
extern int shm_unlink(const char *name);
extern int ftruncate(int fd, off_t length);
extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
extern int munmap(void *addr, size_t len);

#endif
