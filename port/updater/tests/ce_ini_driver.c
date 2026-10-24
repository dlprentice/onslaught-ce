/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated shared-writer driver. Build with the port's SDL headers; no SDL library, game, or display is needed.
 * ce-ini-driver INI KEY VALUE [--wait] returns 0 on a successful write, 1 on refusal. --wait prints its PID and waits
 * for a newline so the tests can populate temporary-name collisions without racing the writer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
static BOOL WINAPI test_flush(HANDLE file);
static BOOL WINAPI test_set_info(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID info, DWORD size);
#define FlushFileBuffers test_flush
#define SetFileInformationByHandle test_set_info
#else
#include <unistd.h>
static int test_sync(int fd);
static int test_rename(int olddir, const char *oldname, int newdir, const char *newname);
#define fsync test_sync
#define renameat test_rename
#endif
#include "../ce_report.h"
#ifdef _WIN32
#undef FlushFileBuffers
#undef SetFileInformationByHandle
static BOOL WINAPI test_flush(HANDLE file)
{
	if (getenv("CE_TEST_FAIL_SYNC")) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
	return FlushFileBuffers(file);
}
static BOOL WINAPI test_set_info(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID info, DWORD size)
{
	if (kind == FileRenameInfo && getenv("CE_TEST_FAIL_RENAME")) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
	return SetFileInformationByHandle(file, kind, info, size);
}
#else
#undef fsync
#undef renameat
static int test_sync(int fd)
{
	if (getenv("CE_TEST_FAIL_SYNC")) { errno = EIO; return -1; }
	const char *parent = getenv("CE_TEST_SWAP_PARENT");
	if (parent)
	{
		const char *moved = getenv("CE_TEST_MOVED_PARENT"), *outside = getenv("CE_TEST_OUTSIDE");
		if (!moved || !outside || rename(parent, moved) != 0 || symlink(outside, parent) != 0) return -1;
	}
	return fsync(fd);
}
static int test_rename(int olddir, const char *oldname, int newdir, const char *newname)
{
	if (getenv("CE_TEST_FAIL_RENAME")) { errno = EACCES; return -1; }
	return renameat(olddir, oldname, newdir, newname);
}
#endif

int main(int argc, char **argv)
{
	if (argc < 4 || argc > 5) return 2;
	if (argc == 5)
	{
		if (strcmp(argv[4], "--wait")) return 2;
#ifdef _WIN32
		printf("%lu\n", (unsigned long)GetCurrentProcessId());
#else
		printf("%lu\n", (unsigned long)getpid());
#endif
		fflush(stdout);
		if (getchar() != '\n') return 2;
	}
	return ce_ini_write(argv[1], argv[2], argv[3]) ? 0 : 1;
}
