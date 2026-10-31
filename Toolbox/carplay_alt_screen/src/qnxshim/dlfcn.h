/* Minimal QNX/libc declarations for the freestanding armv7 build.
   Only symbols the P1404 system libc.so.3 / libpthread.so.1 actually export. */
#ifndef QSHIM_DLFCN_H
#define QSHIM_DLFCN_H
extern void *dlopen(const char *file, int flag);
extern void *dlsym(void *handle, const char *name);
extern char *dlerror(void);
extern int dlclose(void *handle);
/* QNX Neutrino ABI, not glibc. Verified against the vehicle libc.so.3
 * dlsym entry: cmn handle,#2 / cmn handle,#3. Zero is an invalid handle. */
#define RTLD_DEFAULT ((void *)-2L)
#define RTLD_NEXT    ((void *)-3L)
#endif
