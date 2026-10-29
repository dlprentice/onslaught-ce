// SPDX-License-Identifier: GPL-3.0-only
/* messagebox_tests.py's crashing dialog backend: preloaded (32-bit) into the launcher, it makes SDL's first step towards
 * a message box on Linux, starting zenity (posix_spawn, even only to ask its version), crash with SIGSEGV, as SDL's own
 * X11 dialog did without the 32-bit libXrandr. The launcher itself starts processes with fork and exec, not these. */
#define _GNU_SOURCE
#include <signal.h>
#include <spawn.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *attr,
				char *const argv[], char *const envp[])
{
	(void)pid; (void)path; (void)fa; (void)attr; (void)argv; (void)envp;
#ifdef PR_SET_DUMPABLE
	prctl(PR_SET_DUMPABLE, 0);	/* no core file and no systemd-coredump record from this deliberate crash (RLIMIT_CORE only stops the file) */
#endif
	raise(SIGSEGV);
	return -1;
}

int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *attr,
				 char *const argv[], char *const envp[])
{
	return posix_spawn(pid, file, fa, attr, argv, envp);
}
