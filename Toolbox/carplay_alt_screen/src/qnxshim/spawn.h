/* QNX 6.5 spawnp ABI; opaque inheritance is unused (NULL). */
#ifndef QSHIM_SPAWN_H
#define QSHIM_SPAWN_H
struct inheritance;
extern int spawnp(const char *file, int fd_count, const int fd_map[],
                  const struct inheritance *inherit, char *const argv[],
                  char *const envp[]);
#endif
