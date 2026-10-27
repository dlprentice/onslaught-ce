/* Test-only LD_PRELOAD shim: STATX_SHIM=enosys makes syscall(SYS_statx) fail with ENOSYS; =nomnt reports statx
 * without STATX_MNT_ID and without STATX_ATTR_MOUNT_ROOT support (a pre-5.8 kernel). Other syscalls pass through. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
long syscall(long n, ...)
{
	static long (*real)(long, ...);
	if (!real) real = (long (*)(long, ...))dlsym(RTLD_NEXT, "syscall");
	va_list ap; va_start(ap, n);
	long a[6]; for (int i = 0; i < 6; ++i) a[i] = va_arg(ap, long);
	va_end(ap);
	const char *mode = getenv("STATX_SHIM");
	if (n == SYS_statx && mode && !strcmp(mode, "enosys")) { errno = ENOSYS; return -1; }
	long r = real(n, a[0], a[1], a[2], a[3], a[4], a[5]);
	if (n == SYS_statx && r == 0 && mode && !strcmp(mode, "nomnt"))
	{
		uint8_t *x = (uint8_t *)a[4];
		uint32_t mask; memcpy(&mask, x, 4); mask &= ~0x1000u; memcpy(x, &mask, 4);
		uint64_t am; memcpy(&am, x + 56, 8); am &= ~0x2000ull; memcpy(x + 56, &am, 8);
	}
	return r;
}
