/* Values verified against QNX 6.5 SDK, not Linux wait options. */
#ifndef QSHIM_SYS_WAIT_H
#define QSHIM_SYS_WAIT_H
#define WNOHANG 0x0040
extern int waitpid(int pid, int *status, int options);
#endif
