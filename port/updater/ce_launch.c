/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_launch.c: Onslaught CE's launcher (port/updater/README.md). Finds the player's Battle Engine Aquila
 * install and starts the game from this folder with the game folder and
 * a per-user writable folder. It never writes into the game folder.
 *
 *   onslaught-ce [--version] [--no-update-check] [--update-only] [--record] [--no-record] [--report] [-- GAME ARGUMENTS]
 *
 * The log: the game's output and the launcher's own messages go to <user folder>/onslaught-ce.log (the run before is
 * onslaught-ce.old.log; each stops at 4 MiB) and still to the terminal. Two fixed leased fallback logs admit concurrent
 * runs without rotating an active log; all four data files together are bounded to 16 MiB. Its first lines name the version, the build, the
 * OS and the GPU (the game's "onslaught-ce: adapter" line). It never names the player's account: home is written ~ (Windows
 * %APPDATA% and the like) and the account name <user>. A continuation keeps the same log. A replay
 * (BEA_PORT_TAPE) or a harness recording (BEA_PORT_RECORD) gets no log, no update check and nothing else of the launcher's.
 * Session recording (--record, or record_sessions=1 in ce.ini): the game records its input to <user folder>/tapes/
 * YYYYMMDD-HHMMSS-PID-SERIAL.tape (port/platform/common/replay.cpp's record mode, on the shim clock at 30 Presents a
 * second) with a .json beside it naming the version, build, OS, date and game arguments; never during replay. While
 * record_sessions=1, each start asks first (Record, Not this time, Stop recording), because a recorded session hides the
 * game's CE page where the setting was switched on; --no-record also sets record_sessions=0 and starts unrecorded.
 * --report opens the pre-filled "Report a problem" page (ce_report.h) and prints the log folder.
 *
 * Automatic update checks and installation are suspended. --update-only prints manual-download guidance and exits 2
 * before opening settings, logs or caches. The historical --apply entry point refuses with exit 2. Ordinary startup
 * does not fetch an update feed, clean neighboring folders, stage an archive or rename an installation. Old launchers
 * remain unsafe to invoke for migration; publishing this successor does not repair an older launcher's behavior. */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include "monocypher-ed25519.h"
#include "sha256.h"
#include "ce_report.h"
#include "ce_launch_rules.h"

/* Quitting from the game's main menu ends WinMain with the WM_QUIT message's wParam, which the game posts as 13: an
 * ordinary close, not an error, so the launcher reports it as one and exits 0. */
#define GAME_QUIT_STATUS 13

#ifndef CE_VERSION
#define CE_VERSION "dev"
#endif
#ifndef CE_COMMIT
#define CE_COMMIT "unknown"		/* build.py: the source commit (+dirty when the tree had changes) */
#endif
/* The release key's public half is retained for signing tools; automatic feed checks are suspended. */
static const char PUBLIC_KEY_HEX[] = "b6cf39b727c92cd8a1682c31f315a5d38427dce912e406ccbd4802f0f7634510";

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <sys/stat.h>
#define PLATFORM "windows-i686"
#define SEP "\\"
#define GAME_EXE "bea.exe"
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PLATFORM "linux-i686"
#define SEP "/"
#define GAME_EXE "bea"
#endif

#define PATHN 4096
static char sHere[PATHN], sUser[PATHN], sIni[PATHN];
/* A replay (BEA_PORT_TAPE) or a recording someone else set up (BEA_PORT_RECORD, the test harness) as the launcher
 * started: the launcher then adds nothing of its own (no log, no update check, no build name for the CE layer). */
static int sReplay;
/* Only this launcher's recording, never an inherited harness recording or an earlier log's status. */
static int sRecordingRequested, sRecordingPublished, sRecordingFailed;
static char sRecordingPath[PATHN], sRecordingScan[PATHN + 96];
static size_t sRecordingScanLen;
static int sRecordingScanOverflow;

/* ---- the log ---------------------------------------------------------------------------------------------------- */

#define LOG_CAP (4u << 20)		/* each log file stops at 4 MB */
#define HOLD_CAP (1u << 20)
#define CE_LOG_SLOT_COUNT 3
static const char *const sLogNames[CE_LOG_SLOT_COUNT] = {
	"onslaught-ce.log", "onslaught-ce.concurrent-1.log", "onslaught-ce.concurrent-2.log"
};
static int sLogSlot = -1;
static int log_artifact_name(const char *name)
{
	if (!strcmp(name, "onslaught-ce.old.log")) return 1;
	for (int slot = 0; slot < CE_LOG_SLOT_COUNT; ++slot)
	{
		char lease[128];
		snprintf(lease, sizeof lease, "%s.lock", sLogNames[slot]);
		if (!strcmp(name, sLogNames[slot]) || !strcmp(name, lease)) return 1;
	}
	return 0;
}
#ifdef _WIN32
static HANDLE sLogLease = INVALID_HANDLE_VALUE;
#else
static int sLogLease = -1;
#endif
static unsigned long log_native_error(void);
static void log_release_lease(void);
static FILE *sLog;
static size_t sLogBytes;
static int sLogFull, sHolding;
static char *sHold;				/* what arrives before the GPU is known, so the GPU line stays near the top */
static size_t sHoldLen, sHoldCap;
static char sScan[1024];		/* the game's current output line, for the adapter line */
static size_t sScanLen;

static void log_file(const char *p, size_t n)
{
	static const char note[] = "\n[onslaught-ce: this log reached 4 MB; later output went to the terminal only]\n";
	int failed = 0;
	if (!sLog || sLogFull) return;
	if (sLogBytes + n + sizeof note > LOG_CAP)
	{
		size_t remaining = sLogBytes < LOG_CAP ? LOG_CAP - sLogBytes : 0;
		if (remaining < sizeof note - 1) { sLogFull = 1; return; }
		size_t room = remaining - (sizeof note - 1);
		size_t amount = room < n ? room : n;
		failed = fwrite(p, 1, amount, sLog) != amount;
		if (!failed) failed = fwrite(note, 1, sizeof note - 1, sLog) != sizeof note - 1;
		sLogFull = 1;
	}
	else
	{
		failed = fwrite(p, 1, n, sLog) != n;
		sLogBytes += n;
	}
	if (fflush(sLog)) failed = 1;
	if (failed)
	{
		unsigned long error = log_native_error();
		FILE *file = sLog; sLog = NULL;
		fclose(file); log_release_lease();
		free(sHold); sHold = NULL; sHoldLen = sHoldCap = 0; sHolding = 0;
		fprintf(stderr, "onslaught-ce: log write/flush failed error=%lu; output continues on the terminal only\n", error);
	}
}

#ifndef _WIN32
/* This machine's GPUs from sysfs ("NVIDIA 10de:28a0 (nvidia), Intel 8086:4688 (i915)"): on Linux, dxvk may report a
 * stand-in name to the game (its d3d9.hide*Gpu defaults), so the log names the real devices too. */
static void linux_gpus(char *out, size_t n)
{
	out[0] = 0;
	DIR *d = opendir("/sys/class/drm");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)))
	{
		if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-')) continue;
		char p[PATHN], v[16] = "", dev[16] = "", drv[PATHN] = "";
		FILE *f;
		snprintf(p, sizeof p, "/sys/class/drm/%s/device/vendor", e->d_name);
		if ((f = fopen(p, "r"))) { if (!fgets(v, sizeof v, f)) v[0] = 0; fclose(f); }
		snprintf(p, sizeof p, "/sys/class/drm/%s/device/device", e->d_name);
		if ((f = fopen(p, "r"))) { if (!fgets(dev, sizeof dev, f)) dev[0] = 0; fclose(f); }
		snprintf(p, sizeof p, "/sys/class/drm/%s/device/driver", e->d_name);
		ssize_t l = readlink(p, drv, sizeof drv - 1);
		drv[l > 0 ? l : 0] = 0;
		unsigned vid = (unsigned)strtoul(v, NULL, 16), did = (unsigned)strtoul(dev, NULL, 16);
		if (!vid) continue;
		const char *name = vid == 0x10de ? "NVIDIA " : vid == 0x8086 ? "Intel " : vid == 0x1002 ? "AMD " : "";
		const char *base = strrchr(drv, '/');
		size_t at = strlen(out);
		snprintf(out + at, n - at, "%s%s%04x:%04x (%s)", at ? ", " : "", name, vid, did, base ? base + 1 : "no driver");
	}
	closedir(d);
}
#endif

/* The GPU line, then everything held until it was known. */
static void log_release(const char *gpu)
{
	if (!sHolding) return;
	sHolding = 0;
	char line[1600];
	int k = snprintf(line, sizeof line, "GPU (as Direct3D reports it): %s\n", gpu);
#ifndef _WIN32
	char all[512];
	linux_gpus(all, sizeof all);
	const char *filter = getenv("DXVK_FILTER_DEVICE_NAME");
	if (*all && k > 0 && (size_t)k < sizeof line)
		k += snprintf(line + k, sizeof line - (size_t)k, "GPUs in this machine: %s%s%s\n", all,
					  filter && *filter ? "; DXVK_FILTER_DEVICE_NAME=" : "", filter && *filter ? filter : "");
#endif
	if (k > 0 && (size_t)k < sizeof line) k += snprintf(line + k, sizeof line - (size_t)k, "\n");
	log_file(line, k > 0 && (size_t)k < sizeof line ? (size_t)k : strlen(line));
	if (sHold) log_file(sHold, sHoldLen);
	free(sHold);
	sHold = NULL;
	sHoldLen = sHoldCap = 0;
}

static void log_bytes(const char *p, size_t n)
{
	if (!sHolding) { log_file(p, n); return; }
	if (sHoldLen + n > HOLD_CAP)
	{
		log_release("not reported yet (see below)");
		log_file(p, n);
		return;
	}
	if (sHoldLen + n > sHoldCap)
	{
		size_t cap = sHoldCap ? sHoldCap : 16384;
		while (cap < sHoldLen + n) cap *= 2;
		char *b = realloc(sHold, cap);
		if (!b) { log_release("not reported yet (see below)"); log_file(p, n); return; }
		sHold = b;
		sHoldCap = cap;
	}
	memcpy(sHold + sHoldLen, p, n);
	sHoldLen += n;
}

static int name_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

/* The log is meant to be attached to a public issue, so it never names the player's account: their home (Windows
 * %USERPROFILE%, %APPDATA%, %LOCALAPPDATA%) is written as ~ (or that variable), and the account name as a whole path
 * component anywhere else (/run/media/NAME/..., D:\Users\NAME\...) as <user>. */
static void scrub(const char *in, char *out, size_t n)
{
	struct { const char *value, *name; } base[3] = {{0, 0}, {0, 0}, {0, 0}};
#ifdef _WIN32
	base[0].value = getenv("LOCALAPPDATA"), base[0].name = "%LOCALAPPDATA%";
	base[1].value = getenv("APPDATA"), base[1].name = "%APPDATA%";
	base[2].value = getenv("USERPROFILE"), base[2].name = "%USERPROFILE%";
	const char *account = getenv("USERNAME");
#define PREFIX_EQ(a, b, l) (_strnicmp((a), (b), (l)) == 0)
#else
	base[0].value = getenv("HOME"), base[0].name = "~";
	const char *account = getenv("USER");
	if (!account || !*account) account = getenv("LOGNAME");
#define PREFIX_EQ(a, b, l) (strncmp((a), (b), (l)) == 0)
#endif
	size_t al = account ? strlen(account) : 0, o = 0;
	for (const char *p = in; *p && o + 1 < n;)
	{
		int done = 0;
		for (int b = 0; b < 3 && !done; b++)
		{
			const char *v = base[b].value;
			size_t l = v ? strlen(v) : 0;
			while (l > 1 && (v[l - 1] == '/' || v[l - 1] == '\\')) l--;
			if (l > 1 && (p == in || !name_char(p[-1])) && PREFIX_EQ(p, v, l) && !name_char(p[l]))
			{
				o += (size_t)snprintf(out + o, n - o, "%s", base[b].name);
				p += l;
				done = 1;
			}
		}
		if (!done && al > 1 && p > in && (p[-1] == '/' || p[-1] == '\\') && PREFIX_EQ(p, account, al) && !name_char(p[al]))
		{
			o += (size_t)snprintf(out + o, n - o, "<user>");
			p += al;
			done = 1;
		}
		if (o >= n) { o = n - 1; break; }
		if (!done) out[o++] = *p++;
	}
	out[o] = 0;
#undef PREFIX_EQ
}

/* One line of the game's output (without its newline) to the log, scrubbed; its adapter line names the GPU. */
static void game_line(int newline)
{
	static const char tag[] = "onslaught-ce: adapter ";
	char clean[4 * sizeof sScan + 2];
	sScan[sScanLen] = 0;
	scrub(sScan, clean, sizeof clean - 1);
	size_t l = strlen(clean);
	if (newline) clean[l++] = '\n';
	log_bytes(clean, l);
	if (newline && sScanLen && sScan[sScanLen - 1] == '\r') sScan[--sScanLen] = 0;
	if (newline && sHolding && !strncmp(sScan, tag, sizeof tag - 1)) log_release(sScan + sizeof tag - 1);
	sScanLen = 0;
}

/* A bounded, whole-line acknowledgement of this exact recording's no-replace publication. The ordinary log scanner
 * splits long lines, so it cannot be used to recognize status: a fragment must never become a new status line. */
static void recording_output(const char *p, size_t n)
{
	if (!sRecordingRequested) return;
	for (size_t i = 0; i < n; ++i)
	{
		if (p[i] != '\n')
		{
			if (sRecordingScanLen < sizeof sRecordingScan) sRecordingScan[sRecordingScanLen++] = p[i];
			else sRecordingScanOverflow = 1;
			continue;
		}
		if (sRecordingScanLen && sRecordingScan[sRecordingScanLen - 1] == '\r') --sRecordingScanLen;
		const char *prefixes[] = {"bea-port: ", "onslaught-ce: "};
		for (size_t k = 0; k < sizeof prefixes / sizeof prefixes[0]; ++k)
		{
			char expected[PATHN + 96];
			/* The fixed failure prefix is retained even when the diagnostic's tail was discarded. */
			int length = snprintf(expected, sizeof expected, "%srecording failed:", prefixes[k]);
			if (length > 0 && (size_t)length <= sRecordingScanLen &&
				!memcmp(sRecordingScan, expected, (size_t)length)) sRecordingFailed = 1;
			if (!sRecordingScanOverflow)
			{
				length = snprintf(expected, sizeof expected, "%srecording finalized: final tape published %s",
					prefixes[k], sRecordingPath);
				if (*sRecordingPath && length > 0 && (size_t)length == sRecordingScanLen &&
					!memcmp(sRecordingScan, expected, sRecordingScanLen)) sRecordingPublished = 1;
			}
		}
		sRecordingScanLen = 0;
		sRecordingScanOverflow = 0;
	}
}

/* The game's output: to the terminal as it comes, and to the log a line at a time. */
static void game_output(const char *p, size_t n)
{
	recording_output(p, n);
	fwrite(p, 1, n, stderr);
	fflush(stderr);
	for (size_t i = 0; i < n; i++)
	{
		if (p[i] == '\n') { game_line(1); continue; }
		if (sScanLen + 1 >= sizeof sScan) game_line(0);	/* a very long line goes to the log in pieces */
		sScan[sScanLen++] = p[i];
	}
}

/* The launcher's own messages: "onslaught-ce: ..." to the terminal and the log, scrubbed in both. */
static void say(const char *fmt, ...)
{
	char text[2048], clean[2048];
	int k = snprintf(text, sizeof text, "onslaught-ce: ");
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text + k, sizeof text - (size_t)k - 1, fmt, ap);
	va_end(ap);
	scrub(text, clean, sizeof clean - 1);
	strcat(clean, "\n");
	fputs(clean, stderr);
	fflush(stderr);
	log_bytes(clean, strlen(clean));
}

static void utc_now(char *out, size_t n, const char *format)
{
	time_t now = time(NULL);
	struct tm t;
#ifdef _WIN32
	gmtime_s(&t, &now);
#else
	gmtime_r(&now, &t);
#endif
	strftime(out, n, format, &t);
}

static unsigned long log_native_error(void)
{
#ifdef _WIN32
	DWORD error = GetLastError(); return error ? error : ERROR_GEN_FAILURE;
#else
	return errno ? (unsigned long)errno : EIO;
#endif
}

static unsigned long log_invalid_file(void)
{
#ifdef _WIN32
	return ERROR_INVALID_NAME;
#else
	return EINVAL;
#endif
}

static int log_missing(unsigned long error)
{
#ifdef _WIN32
	return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
	return error == ENOENT;
#endif
}

static int log_path(char *out, size_t n, const char *name)
{
	int length = snprintf(out, n, "%s" SEP "%s", sUser, name);
	return length > 0 && (size_t)length < n;
}

static void log_release_lease(void)
{
#ifdef _WIN32
	if (sLogLease != INVALID_HANDLE_VALUE)
	{
		OVERLAPPED offset = {0};
		UnlockFileEx(sLogLease, 0, 1, 0, &offset);
		CloseHandle(sLogLease); sLogLease = INVALID_HANDLE_VALUE;
	}
#else
	if (sLogLease != -1) { flock(sLogLease, LOCK_UN); close(sLogLease); sLogLease = -1; }
#endif
	sLogSlot = -1;
}

/* 1 owns a lifetime lease, 0 is ordinary contention, -1 is an error. Lease files are never unlinked: replacing a
 * locked inode would create two independent owners. No lease/data handle is inherited by the game or updater. */
static int log_acquire_lease(int slot, unsigned long *error)
{
	char name[128], path[PATHN];
	snprintf(name, sizeof name, "%s.lock", sLogNames[slot]);
	if (!log_path(path, sizeof path, name)) { *error = log_invalid_file(); return -1; }
#ifdef _WIN32
	HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE) { *error = log_native_error(); return -1; }
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle(h, &info)) { *error = log_native_error(); CloseHandle(h); return -1; }
	if (GetFileType(h) != FILE_TYPE_DISK || info.nNumberOfLinks != 1 ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{ *error = log_invalid_file(); CloseHandle(h); return -1; }
	if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)) { *error = log_native_error(); CloseHandle(h); return -1; }
	OVERLAPPED offset = {0};
	if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset))
	{
		*error = log_native_error(); CloseHandle(h);
		return *error == ERROR_LOCK_VIOLATION ? 0 : -1;
	}
	sLogLease = h;
#else
	struct stat before, info;
	if (!lstat(path, &before) && (!S_ISREG(before.st_mode) || before.st_nlink != 1)) { *error = log_invalid_file(); return -1; }
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) { *error = log_native_error(); return -1; }
	if (fstat(fd, &info)) { *error = log_native_error(); close(fd); return -1; }
	if (!S_ISREG(info.st_mode) || info.st_nlink != 1) { *error = log_invalid_file(); close(fd); return -1; }
	if (flock(fd, LOCK_EX | LOCK_NB))
	{
		*error = log_native_error(); close(fd);
		return *error == EWOULDBLOCK || *error == EAGAIN ? 0 : -1;
	}
	sLogLease = fd;
#endif
	sLogSlot = slot;
	return 1;
}

enum LogOpen { LOG_INSPECT, LOG_APPEND, LOG_CREATE };

/* Existing data paths must be regular nonlinks and carry the app header; only CREATE_NEW/O_EXCL creates output.
 * A lease authorizes retirement of a previous closed app log, never truncation of an existing pathname. */
static FILE *log_open_file(const char *path, enum LogOpen mode, unsigned long *error)
{
	int fd;
#ifdef _WIN32
	DWORD access = mode == LOG_INSPECT ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
	HANDLE h = CreateFileA(path, access, FILE_SHARE_READ, NULL, mode == LOG_CREATE ? CREATE_NEW : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE) { *error = log_native_error(); return NULL; }
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle(h, &info)) { *error = log_native_error(); CloseHandle(h); return NULL; }
	if (GetFileType(h) != FILE_TYPE_DISK || info.nNumberOfLinks != 1 ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
		info.nFileSizeHigh || info.nFileSizeLow > LOG_CAP)
	{ *error = log_invalid_file(); CloseHandle(h); return NULL; }
	if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)) { *error = log_native_error(); CloseHandle(h); return NULL; }
	fd = _open_osfhandle((intptr_t)h, _O_BINARY | (mode == LOG_INSPECT ? _O_RDONLY : _O_RDWR) | (mode == LOG_APPEND ? _O_APPEND : 0));
	if (fd < 0) { *error = log_native_error(); CloseHandle(h); return NULL; }
	FILE *file = _fdopen(fd, mode == LOG_INSPECT ? "rb" : mode == LOG_APPEND ? "a+b" : "wb");
	if (!file) { *error = log_native_error(); _close(fd); return NULL; }
#else
	int flags = mode == LOG_INSPECT ? O_RDONLY : mode == LOG_APPEND ? O_RDWR | O_APPEND : O_RDWR | O_CREAT | O_EXCL;
	fd = open(path, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
	if (fd < 0) { *error = log_native_error(); return NULL; }
	struct stat info;
	if (fstat(fd, &info)) { *error = log_native_error(); close(fd); return NULL; }
	if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0 || (unsigned long long)info.st_size > LOG_CAP)
	{ *error = log_invalid_file(); close(fd); return NULL; }
	FILE *file = fdopen(fd, mode == LOG_INSPECT ? "rb" : mode == LOG_APPEND ? "a+b" : "wb");
	if (!file) { *error = log_native_error(); close(fd); return NULL; }
#endif
	if (mode != LOG_CREATE)
	{
		static const char magic[] = "Onslaught CE v";
		char head[sizeof magic - 1];
		if (fseek(file, 0, SEEK_SET) || fread(head, 1, sizeof head, file) != sizeof head || memcmp(head, magic, sizeof head))
		{ *error = log_invalid_file(); fclose(file); return NULL; }
		if (mode == LOG_APPEND && fseek(file, 0, SEEK_END)) { *error = log_native_error(); fclose(file); return NULL; }
	}
	return file;
}

/* -1 refuses a foreign/failed path, 0 is absent, 1 is an app-generated regular file. */
static int log_check_file(const char *path, unsigned long *error)
{
	FILE *file = log_open_file(path, LOG_INSPECT, error);
	if (!file) return log_missing(*error) ? 0 : -1;
	if (fclose(file)) { *error = log_native_error(); return -1; }
	return 1;
}

static int log_remove_closed(const char *path, unsigned long *error)
{
	int present = log_check_file(path, error);
	if (present < 0) return 0;
	if (present && remove(path)) { *error = log_native_error(); return 0; }
	return 1;
}

static int log_prepare_primary(const char *path, unsigned long *error)
{
	int present = log_check_file(path, error);
	if (present <= 0) return present == 0;
	char old[PATHN];
	if (!log_path(old, sizeof old, "onslaught-ce.old.log")) { *error = log_invalid_file(); return 0; }
	if (!log_remove_closed(old, error)) return 0;
#ifdef _WIN32
	if (!MoveFileA(path, old)) { *error = log_native_error(); return 0; }
#else
	/* No replacement of a surprising destination: hard-link rotation is available for these regular same-dir files. */
	if (link(path, old) || remove(path)) { *error = log_native_error(); return 0; }
#endif
	return 1;
}

/* Starts this run's log: the last run's becomes onslaught-ce.old.log. `cont`: the launcher was restarted by an update
 * (BEA_CE_LOG_CONTINUE), so the log goes on in the same file and the run before it (perhaps the one being reported)
 * stays as onslaught-ce.old.log. */
static void log_open(int cont)
{
	char log[PATHN], os[256], date[64], head[1024];
	sLogBytes = 0; sLogFull = 0;
	int appended = 0;
	for (int slot = 0; slot < CE_LOG_SLOT_COUNT; ++slot)
	{
		unsigned long error = 0;
		int owned = log_acquire_lease(slot, &error);
		if (!owned) continue;
		if (owned < 0) { say("log lease failed slot=%s error=%lu; output continues on the terminal only", sLogNames[slot], error); return; }
		if (!log_path(log, sizeof log, sLogNames[slot])) { error = log_invalid_file(); }
		else if (cont && slot == 0)
		{
			sLog = log_open_file(log, LOG_APPEND, &error);
			if (sLog)
			{
				long at = ftell(sLog);
				if (at < 0) { error = log_native_error(); fclose(sLog); sLog = NULL; }
				else { sLogBytes = (size_t)at; sLogFull = sLogBytes >= LOG_CAP; appended = 1; }
			}
			else if (log_missing(error)) sLog = log_open_file(log, LOG_CREATE, &error);
		}
		else if (slot == 0 ? log_prepare_primary(log, &error) : log_remove_closed(log, &error))
			sLog = log_open_file(log, LOG_CREATE, &error);
		if (sLog) break;
		say("log open/rotation refused slot=%s error=%lu; existing files were not truncated", sLogNames[slot], error);
		log_release_lease();
	}
	if (!sLog) { say("no safe log slot available; output continues on the terminal only"); return; }
	if (appended) log_file("\n", 1);
	ce_os_name(os, sizeof os);
	utc_now(date, sizeof date, "%Y-%m-%d %H:%M:%S UTC");
	int k = snprintf(head, sizeof head, "Onslaught CE v%s" CE_VERSION_NOTE ", build %s (" PLATFORM ")\nOS: %s\n%s: %s\n", CE_VERSION,
					 CE_COMMIT, os, cont ? "Restarted after an update" : "Started", date);
	log_file(head, (size_t)k < sizeof head ? (size_t)k : sizeof head - 1);
	if (!sLog) return; /* A failed header write already closed the file and released its lease. */
	sHolding = 1;
	if (sLogSlot > 0) say("concurrent/fallback log=%s (4 MiB limit; primary log belongs to another run or could not rotate)", sLogNames[sLogSlot]);
	say("launcher process pid=%lu SDL=%d", (unsigned long)
#ifdef _WIN32
		GetCurrentProcessId(),
#else
		getpid(),
#endif
		SDL_GetVersion());
}

static void log_close(void)
{
	if (sLog)
	{
		log_release("not reported (the game did not reach its Direct3D device)");
		if (sLog)
		{
			FILE *file = sLog; sLog = NULL;
			if (fclose(file)) fprintf(stderr, "onslaught-ce: log close failed error=%lu\n", log_native_error());
		}
	}
	log_release_lease();
}

/* ---- small helpers ---------------------------------------------------------------------------------------------- */

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
	SetEnvironmentVariableA(name, value);
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

static void unset_env(const char *name)
{
#ifdef _WIN32
	SetEnvironmentVariableA(name, NULL);
	_putenv_s(name, "");
#else
	unsetenv(name);
#endif
}

/* Set while an update restarts the launcher: the new launcher continues this log instead of rotating it away. */
#define LOG_CONTINUE "BEA_CE_LOG_CONTINUE"

static int is_file(const char *p)
{
	FILE *f = fopen(p, "rb");
	if (f) fclose(f);
	return f != NULL;
}

/* p names a regular file, whether or not it can be read (is_file opens it, so an unreadable file fails there). */
static int file_exists(const char *p)
{
#ifdef _WIN32
	DWORD attr = GetFileAttributesA(p);
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return !stat(p, &st) && S_ISREG(st.st_mode);
#endif
}

static void make_dirs(const char *path)
{
	char t[PATHN];
	snprintf(t, sizeof t, "%s", path);
	for (char *p = t + 1; *p; p++)
		if (*p == '/' || *p == '\\')
		{
			char c = *p;
			*p = 0;
#ifdef _WIN32
			_mkdir(t);
#else
			mkdir(t, 0755);
#endif
			*p = c;
		}
#ifdef _WIN32
	_mkdir(t);
#else
	mkdir(t, 0755);
#endif
}

static char *read_all(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	if (!f) return NULL;
	size_t cap = 1 << 16, len = 0;
	char *b = malloc(cap + 1);
	for (size_t r; b && (r = fread(b + len, 1, cap - len, f)) > 0;)
		if ((len += r) == cap) b = realloc(b, (cap *= 2) + 1);
	fclose(f);
	if (!b) return NULL;
	b[len] = 0;
	if (n) *n = len;
	return b;
}

static int sha256_file(const char *p, uint8_t out[32])
{
	FILE *f = fopen(p, "rb");
	if (!f) return 0;
	SHA256_CTX c;
	sha256_init(&c);
	uint8_t buf[65536];
	for (size_t r; (r = fread(buf, 1, sizeof buf, f)) > 0;) sha256_update(&c, buf, r);
	int ok = !ferror(f);
	fclose(f);
	if (!ok) return 0;
	sha256_final(&c, out);
	return 1;
}

static int hex_to(const char *h, uint8_t *out, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		unsigned v;
		if (sscanf(h + 2 * i, "%2x", &v) != 1) return 0;
		out[i] = (uint8_t)v;
	}
	return 1;
}

/* ce.ini: key=value lines in the user folder. */
static int ini_get(const char *key, char *out, size_t n)
{
	char *t = read_all(sIni, NULL);
	if (!t) return 0;
	size_t k = strlen(key);
	int found = 0;
	for (char *l = t; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
		if (!strncmp(l, key, k) && l[k] == '=')
		{
			char *e = l + k + 1;
			size_t m = strcspn(e, "\r\n");
			snprintf(out, n, "%.*s", (int)m, e);
			found = 1;
			break;
		}
	free(t);
	return found;
}

/* ce_ini_write (ce_report.h): written whole beside ce.ini and renamed over it, so a failed write leaves it as it was. */
static int ini_set(const char *key, const char *value)
{
	if (ce_ini_write(sIni, key, value)) return 1;
	say("could not write %s to ce.ini; it is unchanged", key);
	return 0;
}

/* ---- finding the game ----------------------------------------------------------------------------------------- */

/* A supported install: its two hash-pinned data files (config/game-data.sha256) match. */
static const struct { const char *path, *name, *sha256; } GAME_FILES[] = {
	{"data" SEP "MissionScripts" SEP "onsldef.msl", "data/MissionScripts/onsldef.msl",
	 "a1c9b9383ea7f9255de207a86c0e6c1885465309d50334344fd82696a1cf36c6"},
	{"data" SEP "MissionScripts" SEP "text" SEP "text.stf", "data/MissionScripts/text/text.stf",
	 "fd318d6c2304eb8ffcfa718357c1715aadad69915f39851b19f442d8263b56ae"},
};

/* Case-insensitive path lookup on Linux (the game's own folders vary in case); Windows is already case-insensitive.
   Finds the file whether or not it can be read, so install_problem can say which of the two it is. */
static int find_ci(const char *dir, const char *rel, char *out, size_t n)
{
#ifdef _WIN32
	snprintf(out, n, "%s\\%s", dir, rel);
	return file_exists(out);
#else
	char cur[PATHN], part[PATHN];
	snprintf(cur, sizeof cur, "%s", dir);
	for (const char *p = rel; *p;)
	{
		size_t len = strcspn(p, "/");
		snprintf(part, sizeof part, "%.*s", (int)len, p);
		DIR *d = opendir(cur);
		if (!d) return 0;
		struct dirent *e;
		int hit = 0;
		while ((e = readdir(d)))
			if (!strcasecmp(e->d_name, part))
			{
				size_t l = strlen(cur);
				snprintf(cur + l, sizeof cur - l, "/%s", e->d_name);
				hit = 1;
				break;
			}
		closedir(d);
		if (!hit) return 0;
		p += len + (p[len] == '/');
	}
	snprintf(out, n, "%s", cur);
	return file_exists(out);
#endif
}

/* Why dir is not a supported install (into why), or 0 when it is one. */
static int install_problem(const char *dir, char *why, size_t n)
{
	char p[PATHN];
	for (size_t i = 0; i < sizeof GAME_FILES / sizeof *GAME_FILES; i++)
	{
		uint8_t got[32], want[32];
		if (!find_ci(dir, GAME_FILES[i].path, p, sizeof p))
			return snprintf(why, n, "it has no %s", GAME_FILES[i].name), 1;
		if (!sha256_file(p, got))
			return snprintf(why, n, "its %s could not be read", GAME_FILES[i].name), 1;
		if (!hex_to(GAME_FILES[i].sha256, want, 32) || memcmp(got, want, 32))
			return snprintf(why, n, "its %s differs from the supported edition's (hash mismatch)", GAME_FILES[i].name), 1;
	}
	return 0;
}

static int supported_install(const char *dir)
{
	char why[256];
	return !install_problem(dir, why, sizeof why);
}

/* Every Steam library listed in <steam>/steamapps/libraryfolders.vdf (and <steam> itself). */
static int steam_libraries(const char *steam, char *found, size_t n)
{
	char vdf[PATHN], game[PATHN];
	snprintf(game, sizeof game, "%s" SEP "steamapps" SEP "common" SEP "Battle Engine Aquila", steam);
	if (supported_install(game)) { snprintf(found, n, "%s", game); return 1; }
	snprintf(vdf, sizeof vdf, "%s" SEP "steamapps" SEP "libraryfolders.vdf", steam);
	char *t = read_all(vdf, NULL);
	if (!t) return 0;
	int ok = 0;
	for (char *p = t; !ok && (p = strstr(p, "\"path\""));)
	{
		p += 6;
		char *a = strchr(p, '"'), *b = a ? strchr(a + 1, '"') : NULL;
		if (!b) break;
		char lib[PATHN];
		size_t len = 0;
		for (char *c = a + 1; c < b && len + 1 < sizeof lib; c++)
			lib[len++] = (*c == '\\' && c[1] == '\\') ? (c++, '\\') : *c;   /* the VDF escapes backslashes */
		lib[len] = 0;
		snprintf(game, sizeof game, "%s" SEP "steamapps" SEP "common" SEP "Battle Engine Aquila", lib);
		if (supported_install(game)) { snprintf(found, n, "%s", game); ok = 1; }
		p = b + 1;
	}
	free(t);
	return ok;
}

static int find_steam_install(char *found, size_t n)
{
#ifdef _WIN32
	char steam[PATHN];
	DWORD len = sizeof steam;
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, NULL, steam, &len) == ERROR_SUCCESS)
	{
		for (char *c = steam; *c; c++) if (*c == '/') *c = '\\';
		if (steam_libraries(steam, found, n)) return 1;
	}
	return 0;
#else
	const char *home = getenv("HOME");
	if (!home) return 0;
	const char *roots[] = {"/.local/share/Steam", "/.steam/steam", "/.steam/root", "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
	char steam[PATHN];
	for (size_t i = 0; i < sizeof roots / sizeof *roots; i++)
	{
		snprintf(steam, sizeof steam, "%s%s", home, roots[i]);
		if (steam_libraries(steam, found, n)) return 1;
	}
	return 0;
#endif
}

#ifndef _WIN32
static volatile pid_t sChild;
static void forward_signal(int sig)
{
	if (sChild > 0) kill(sChild, sig);
}
#endif

static char sPicked[PATHN];
static volatile int sPickDone;

static void SDLCALL picked(void *userdata, const char *const *files, int filter)
{
	(void)userdata; (void)filter;
	if (files && files[0]) snprintf(sPicked, sizeof sPicked, "%s", files[0]);
	sPickDone = 1;
}

/* 1: a supported folder was chosen; 0: none was; -1: no message box could be shown (no display). */
static int ask_for_install(char *found, size_t n)
{
#ifndef _WIN32
	/* No desktop to ask on. (SDL's zenity fallback then fails like a dismissed box, exit 1, so ask first.) */
	const char *x11 = getenv("DISPLAY"), *wayland = getenv("WAYLAND_DISPLAY");
	if (!(x11 && *x11) && !(wayland && *wayland)) return -1;
#endif
	const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..."},
												{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
	for (;;)
	{
		const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE",
			"Onslaught CE plays the game files of your own copy of Battle Engine Aquila, and could not find it in your "
			"Steam libraries.\n\nChoose the folder that contains BEA.exe (the game's Steam or disc install folder). "
			"Onslaught CE only reads from it.", 2, buttons, NULL};
		int which = 0;
		if (!SDL_ShowMessageBox(&box, &which)) return -1;
		if (which != 1) return 0;
		sPickDone = 0;
		sPicked[0] = 0;
		SDL_ShowOpenFolderDialog(picked, NULL, NULL, NULL, false);
		while (!sPickDone)
		{
			SDL_PumpEvents();
			SDL_Delay(20);
		}
		if (!sPicked[0]) return 0;
		char why[256], text[512];
		if (!install_problem(sPicked, why, sizeof why)) { snprintf(found, n, "%s", sPicked); return 1; }
		say("the chosen folder is not a supported Battle Engine Aquila install: %s", why);
		snprintf(text, sizeof text, "That folder is not a supported Battle Engine Aquila install: %s.\n\n"
				 "Choose the folder that contains BEA.exe. If it is a copy of the game that should work, please report it "
				 "as a problem.", why);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Onslaught CE", text, NULL);
	}
}

/* 1: found (dir); 0: none chosen; -1: none found and no message box could be shown to ask for one. A folder named by
 * BEA_GAME_DIR or ce.ini's gamedir that is rejected is said, with the reason. */
static int find_game(char *dir, size_t n)
{
	const char *env = getenv("BEA_GAME_DIR");
	char why[256];
	if (env && *env)
	{
		if (!install_problem(env, why, sizeof why)) { snprintf(dir, n, "%s", env); return 1; }
		say("BEA_GAME_DIR %s is not a supported Battle Engine Aquila install: %s", env, why);
	}
	char ini[PATHN];
	if (ini_get("gamedir", ini, sizeof ini) && *ini)
	{
		if (!install_problem(ini, why, sizeof why)) { snprintf(dir, n, "%s", ini); return 1; }
		say("ce.ini's gamedir %s is not a supported Battle Engine Aquila install: %s", ini, why);
	}
	int found = find_steam_install(dir, n) ? 1 : ask_for_install(dir, n);
	if (found > 0) ini_set("gamedir", dir);
	return found;
}

/* ---- manual updates ------------------------------------------------------------------------------------------- */

/* Kept for the release-signing tools; this launcher does not fetch or verify an update feed while installation is
 * suspended. In particular, this command must run before settings/log/cache initialization and must not open a browser. */
static void manual_update_help(void)
{
	(void)PUBLIC_KEY_HEX;
	fprintf(stderr, "Onslaught CE: automatic update checking and installation are temporarily disabled.\n"
		"This command did not check whether your version is current.\n"
		"Download a corrected release from https://github.com/dlprentice/onslaught-ce/releases .\n"
		"Extract it into a new empty folder. Keep your existing installation, user folder and all backup/staging folders.\n"
		"Do not run an older launcher to perform the update. Relative BEA_USER_DIR may place launcher files and game\n"
		"careers in different folders. Identify and back up both; keep the originals and check careers after migration.\n"
		"Set BEA_USER_DIR to an absolute path for a separate working copy of the game's user-data folder.\n"
		"Keep it outside the original game and old CE folders. See README.txt for the full steps.\n");
}

/* ---- folders, launch ------------------------------------------------------------------------------------------ */



static int init_paths(void)
{
#ifdef _WIN32
	GetModuleFileNameA(NULL, sHere, sizeof sHere);
	char *s = strrchr(sHere, '\\');
	if (s) *s = 0;
	const char *appdata = getenv("APPDATA");
	snprintf(sUser, sizeof sUser, "%s\\Onslaught CE", appdata ? appdata : ".");
#else
	ssize_t n = readlink("/proc/self/exe", sHere, sizeof sHere - 1);
	sHere[n > 0 ? n : 0] = 0;
	char *s = strrchr(sHere, '/');
	if (s) *s = 0;
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && *xdg) snprintf(sUser, sizeof sUser, "%s/onslaught-ce", xdg);
	else snprintf(sUser, sizeof sUser, "%s/.local/share/onslaught-ce", home ? home : ".");
#endif
	const char *user = getenv("BEA_USER_DIR");
	if (user && *user) snprintf(sUser, sizeof sUser, "%s", user);
	snprintf(sIni, sizeof sIni, "%s" SEP "ce.ini", sUser);
	return ce_make_settings_dirs(sUser);
}


static void json_string(FILE *f, const char *v)
{
	fputc('"', f);
	for (const unsigned char *p = (const unsigned char *)v; *p; p++)
		if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
		else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
		else fputc(*p, f);
	fputc('"', f);
}

static void json_base64(FILE *f, const unsigned char *p, size_t n)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	fputc('"', f);
	for (size_t i = 0; i < n; i += 3)
	{
		unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		fputc(b64[v >> 18 & 63], f);
		fputc(b64[v >> 12 & 63], f);
		fputc(i + 1 < n ? b64[v >> 6 & 63] : '=', f);
		fputc(i + 2 < n ? b64[v & 63] : '=', f);
	}
	fputc('"', f);
}

/* The user folder's game files as the session starts (options, careers, the game's own text files), so a replay can
 * start from the same state: "relative/path": base64, in the .json. The launcher's files (ce.ini, the logs, update/,
 * tapes/) stay out, and so does any file over 1 MB. */
#define START_CAP (4u << 20)
static void json_start_files(FILE *f, const char *dir, const char *rel, int *first, size_t *total)
{
	char names[256][256];
	int count = 0;
#ifdef _WIN32
	char pattern[PATHN];
	snprintf(pattern, sizeof pattern, "%s\\*", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do
		if (count < 256 && strlen(fd.cFileName) < 256) snprintf(names[count++], 256, "%s", fd.cFileName);
	while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)))
		if (count < 256 && strlen(e->d_name) < 256) snprintf(names[count++], 256, "%s", e->d_name);
	closedir(d);
#endif
	qsort(names, (size_t)count, sizeof names[0], (int (*)(const void *, const void *))strcmp);
	for (int i = 0; i < count; i++)
	{
		const char *nm = names[i];
		if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
		if (!*rel && (!strcmp(nm, "ce.ini") || log_artifact_name(nm) ||
					  !strcmp(nm, "update") || !strcmp(nm, "tapes")))
			continue;
		char path[PATHN], sub[PATHN];
		snprintf(path, sizeof path, "%s" SEP "%s", dir, nm);
		snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", nm);
#ifdef _WIN32
		DWORD attr = GetFileAttributesA(path);
		int isdir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
		struct stat st;
		int isdir = !lstat(path, &st) && S_ISDIR(st.st_mode);
		if (!isdir && (lstat(path, &st) || !S_ISREG(st.st_mode))) continue;
#endif
		if (isdir)
		{
			if (strchr(rel, '/') == NULL) json_start_files(f, path, sub, first, total);	/* two levels at most */
			continue;
		}
		size_t n = 0;
		char *data = read_all(path, &n);
		if (!data) continue;
		if (n <= (1u << 20) && *total + n <= START_CAP)
		{
			fprintf(f, "%s\n    ", *first ? "" : ",");
			json_string(f, sub);
			fprintf(f, ": ");
			json_base64(f, (const unsigned char *)data, n);
			*first = 0;
			*total += n;
		}
		free(data);
	}
}

/* Any directory entry, including a dangling link, occupies a recording name. An inspection error is also occupied. */
static int recording_path_occupied(const char *path)
{
#ifdef _WIN32
	if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 1;
	DWORD error = GetLastError();
	return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
#else
	struct stat st;
	return !lstat(path, &st) || errno != ENOENT;
#endif
}

/* The JSON name reserves the whole pair. A collision is never opened for writing; retries also avoid an existing tape.
 * The recorder owns its separate incomplete stage and will refuse a final tape that appears after this reservation. */
static FILE *reserve_recording(const char *dir, const char *stamp, char *path, size_t n, char *json, size_t jn)
{
	static unsigned serial;
#ifdef _WIN32
	unsigned pid = (unsigned)GetCurrentProcessId();
#else
	unsigned pid = (unsigned)getpid();
#endif
	for (unsigned attempt = 0; attempt < 256; ++attempt)
	{
		int length = snprintf(path, n, "%s" SEP "%s-%08x-%08x.tape", dir, stamp, pid, serial++);
		if (length < 0 || (size_t)length >= n) return NULL;
		length = snprintf(json, jn, "%s.json", path);
		if (length < 0 || (size_t)length >= jn) return NULL;
		if (recording_path_occupied(path)) continue;
#ifdef _WIN32
		int fd = _open(json, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
		int fd = open(json, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#endif
		if (fd < 0) { if (errno == EEXIST) continue; return NULL; }
#ifdef _WIN32
		FILE *f = _fdopen(fd, "wb");
		if (!f) _close(fd);
#else
		FILE *f = fdopen(fd, "wb");
		if (!f) close(fd);
#endif
		if (!f) remove(json); /* only the metadata name we exclusively created */
		return f;
	}
	return NULL;
}

/* Session recording: the game's record mode writes the tape (replay.cpp); the .json beside it says what made it. */
static void start_recording(char **args, int nargs)
{
	const char *tape = getenv("BEA_PORT_TAPE"), *rec = getenv("BEA_PORT_RECORD");
	if ((tape && *tape) || (rec && *rec))
	{
		say("not recording this session: %s is set", tape && *tape ? "BEA_PORT_TAPE (a replay)" : "BEA_PORT_RECORD");
		return;
	}
	sRecordingRequested = 1;
	sRecordingPublished = sRecordingFailed = 0;
	sRecordingPath[0] = 0;
	sRecordingScanLen = 0;
	sRecordingScanOverflow = 0;
	char dir[PATHN], stamp[32], path[PATHN], json[PATHN + 8], os[256], date[64];
	time_t now = time(NULL);
	struct tm t;
#ifdef _WIN32
	localtime_s(&t, &now);
#else
	localtime_r(&now, &t);
#endif
	strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &t);
	int length = snprintf(dir, sizeof dir, "%s" SEP "tapes", sUser);
	if (length < 0 || (size_t)length >= sizeof dir)
	{
		say("recording failed: the tapes folder path is too long; starting without recording");
		return;
	}
	make_dirs(dir);
	const char *clock = getenv("BEA_PORT_CLOCK");
	int default_clock = !clock || !*clock;
	if (default_clock) clock = "shim";
	/* the virtual clock's mix, written only, to the sound device (dsound.cpp): the game never reads it back */
	const char *monitor = getenv("BEA_AUDIO_MONITOR");
	int default_monitor = !monitor || !*monitor;
	int monitor_on = !monitor || !*monitor || strcmp(monitor, "0") != 0;
	ce_os_name(os, sizeof os);
	utc_now(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ");
	FILE *f = reserve_recording(dir, stamp, path, sizeof path, json, sizeof json);
	if (f)
	{
		fprintf(f, "{\n  \"format\": \"onslaught-ce session tape 2\",\n  \"tape_lines\": [\"K\", \"M\", \"W\", \"W_CLIENT\", \"C\"],\n  \"tape\": ");
		json_string(f, strrchr(path, SEP[0]) + 1);
		fprintf(f, ",\n  \"ce_version\": \"%s\",\n  \"build\": \"%s\",\n  \"platform\": \"%s\",\n  \"os\": ", CE_VERSION, CE_COMMIT, PLATFORM);
		json_string(f, os);
		fprintf(f, ",\n  \"date_utc\": \"%s\",\n  \"clock\": ", date);
		json_string(f, clock);
		fprintf(f, ",\n  \"presents_per_second\": 30,\n  \"audio_monitor\": %s,\n  \"game_args\": [",
				monitor_on ? "true" : "false");
		for (int i = 0; i < nargs; i++)
		{
			fprintf(f, i ? ", " : "");
			json_string(f, args[i]);
		}
		fprintf(f, "],\n  \"start_files\": {");
		int first = 1;
		size_t total = 0;
		json_start_files(f, sUser, "", &first, &total);
		fprintf(f, "\n  }\n}\n");
		int ok = !ferror(f);
		if (fflush(f)) ok = 0;
		if (fclose(f)) ok = 0;
		if (!ok)
		{
			remove(json); /* a failed owned reservation is not a completed session description */
			say("recording failed: could not finish session metadata; starting without recording");
			return;
		}
	}
	else
	{
		say("recording failed: could not reserve session metadata; starting without recording");
		return;
	}
	/* No record-mode side effects until the complete metadata has been checked and closed. */
	if (default_clock) set_env("BEA_PORT_CLOCK", "shim"); /* 1/30 s a Present, paced to real time */
	if (default_monitor) set_env("BEA_AUDIO_MONITOR", "1");
	set_env("BEA_PORT_RECORD", path);
	snprintf(sRecordingPath, sizeof sRecordingPath, "%s", path);
	char shortpath[PATHN];
	ce_short_path(path, shortpath, sizeof shortpath);
	say("recording this session to %s: while recording the game runs at 30 frames a second; its menus take the "
		"keyboard and mouse", shortpath);
}

/* A normal game exit alone does not prove a capture. The exact child's acknowledgement and a nonlink regular final
 * tape are both required; a foreign final or the recorder's retained incomplete stage cannot count as success. */
static int finish_recording(int game_status)
{
	if (!sRecordingRequested) return game_status;
	int regular = 0;
#ifdef _WIN32
	DWORD attr = GetFileAttributesA(sRecordingPath);
	regular = attr != INVALID_FILE_ATTRIBUTES && !(attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
	struct stat st;
	regular = !lstat(sRecordingPath, &st) && S_ISREG(st.st_mode);
#endif
	if (sRecordingPublished && !sRecordingFailed && regular)
	{
		char shortpath[PATHN];
		ce_short_path(sRecordingPath, shortpath, sizeof shortpath);
		say("recording saved to %s", shortpath);
		return game_status;
	}
	say(game_status ? "recording failed or interrupted: no completed session tape was confirmed" :
		"game closed, recording failed: no completed session tape was confirmed");
	return game_status ? game_status : 3;
}

/* record_sessions=1: whether to record this start. The game's CE page, where the player switched recording on, is hidden
 * in a recorded session (its menus are the original game's, so the tape replays exactly), so the way to switch it off is
 * here. Returns 1 to record; "Stop recording" also sets record_sessions=0. No dialog (no display): record, as set. */
static int ask_record(void)
{
	const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Record"},
												{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not this time"},
												{0, 2, "Stop recording"}};
	const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE",
		"Session recording is on. A recording keeps your keyboard and mouse input, and a copy of your careers and "
		"settings as they were at the start, so a problem you report can be replayed exactly.\n\n"
		"While recording, the game runs at 30 frames a second with the original menus (keyboard and mouse), so "
		"the Onslaught CE menu and cheats are hidden.\n\n"
		"Record: record this session.\n"
		"Not this time: play without recording; you will be asked again next time.\n"
		"Stop recording: play without recording, and turn session recording off.", 3, buttons, NULL};
	int which = 1;
	const char *answer = getenv("BEA_RECORD_ANSWER");   /* tests: record, once or stop, without a dialog */
	if (answer && *answer) which = !strcmp(answer, "stop") ? 2 : !strcmp(answer, "once") ? 0 : 1;
	else if (!SDL_ShowMessageBox(&box, &which)) which = 1;
	if (which == 2)
	{
		if (ini_set("record_sessions", "0")) say("session recording switched off (record_sessions=0 in ce.ini)");
		return 0;
	}
	if (which == 0) say("not recording this session (chosen at start)");
	return which == 1;
}

static int start_game(char **args, int nargs, const char *game)
{
	char exe[PATHN];
	snprintf(exe, sizeof exe, "%s" SEP GAME_EXE, sHere);
	if (!sReplay)
	{
		/* Identifies a mixed manual install without scanning game data. A diagnostic only: unreadable executables
		 * can still be executable, so failure to hash must never prevent the ordinary spawn below. */
		uint8_t digest[32];
		if (sha256_file(exe, digest))
		{
			char hex[65];
			for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
			say("prelaunch game executable sha256=%s", hex);
		}
		else say("prelaunch game executable sha256=unavailable (launch will still be attempted)");
	}
	set_env("BEA_GAME_DIR", game);
	set_env("BEA_USER_DIR", sUser);
	if (!sReplay) set_env("BEA_CE_BUILD", CE_COMMIT);	/* the game's "Report a problem" page names it (ce.cpp) */
	char shortgame[PATHN];
	ce_short_path(game, shortgame, sizeof shortgame);
	say("game folder %s", shortgame);
#ifndef _WIN32
	/* The Linux entry point keeps this diagnostic for direct launches, but need not repeat our line. */
	set_env("BEA_CE_GAME_FOLDER_LOGGED", "1");
#endif
#ifdef _WIN32
	char cmd[8192];
	/* The game's legacy parser receives the same raw argument joining as before, now with a checked bound. */
	if (!ce_windows_command(cmd, sizeof cmd, exe, args, nargs, 1))
	{
		say("could not start the game: command line is too long");
		return 1;
	}
	/* The game's stdout and stderr come through a pipe, to the log and (from a console) the terminal. */
	SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
	HANDLE rd = NULL, wr = NULL;
	int piped = CreatePipe(&rd, &wr, &sa, 0) && SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOA si = {sizeof si};
	if (piped)
	{
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.hStdOutput = si.hStdError = wr;
	}
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(exe, cmd, NULL, NULL, piped, 0, NULL, sUser, &si, &pi))
	{
		say("could not start %s (error %lu)", exe, (unsigned long)GetLastError());
		return 1;
	}
	say("game process pid=%lu launcher_pid=%lu output_capture=%s", (unsigned long)pi.dwProcessId,
		(unsigned long)GetCurrentProcessId(), piped ? "pipe" : "unavailable");
	if (piped)
	{
		/* Read while the game runs, then whatever is still waiting, and stop: a program the game started (a browser
		 * opened for "Report a problem") may hold the pipe open long after the game has exited. */
		CloseHandle(wr);
		char buf[8192];
		int exited = 0;
		for (;;)
		{
			DWORD avail = 0, got = 0;
			if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;	/* every writer has closed it */
			if (avail)
			{
				if (!ReadFile(rd, buf, avail < sizeof buf ? avail : (DWORD)sizeof buf, &got, NULL) || !got) break;
				game_output(buf, got);
				continue;
			}
			if (exited) break;
			if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) exited = 1;	/* one more look for its last output */
		}
		CloseHandle(rd);
		if (sScanLen) game_output("\n", 1);
	}
	/* Wait for the game, so whatever started the launcher (Steam, a shortcut, a script) sees the game's run and exit code. */
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if (rc >= 0xC0000000u) say("the game stopped with exception %08lx", (unsigned long)rc);
	else if (rc == 0 || rc == GAME_QUIT_STATUS) { say("the game closed"); rc = 0; }
	else say("the game exited with status %lu", (unsigned long)rc);
	return (int)rc;
#else
	if (chdir(sUser) != 0) return 1;
	char **argv = calloc((size_t)nargs + 2, sizeof *argv);
	argv[0] = exe;
	for (int i = 0; i < nargs; i++) argv[i + 1] = args[i];
	/* The game's stdout and stderr come through one pipe (their order kept), to the terminal and the log. */
	int fd[2];
	if (pipe(fd) != 0)
	{
		if (sRecordingRequested)
		{
			say("could not start the recorded game: output capture is unavailable");
			return 3;
		}
		log_close();
		execv(exe, argv);
		return 1;
	}
	fcntl(fd[0], F_SETFD, FD_CLOEXEC);
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0) { say("could not start %s", exe); return 1; }
	if (pid == 0)
	{
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[0]);
		close(fd[1]);
		execv(exe, argv);
		_exit(127);
	}
	close(fd[1]);
	sChild = pid;
	say("game process pid=%lu launcher_pid=%lu output_capture=pipe", (unsigned long)pid, (unsigned long)getpid());
	/* Ctrl+C reaches the game as well (the terminal's whole process group): the launcher outlives it to finish the log.
	 * A SIGTERM or SIGHUP sent to the launcher alone goes on to the game. */
	signal(SIGINT, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, forward_signal);
	signal(SIGHUP, forward_signal);
	char buf[8192];
	int st = 0, exited = 0;
	for (;;)
	{
		struct pollfd pf = {fd[0], POLLIN, 0};
		int r = poll(&pf, 1, exited ? 0 : 250);
		if (r > 0)
		{
			ssize_t k = read(fd[0], buf, sizeof buf);
			if (k > 0) { game_output(buf, (size_t)k); continue; }
			if (k < 0 && errno == EINTR) continue;
			break;	/* end of output */
		}
		if (r < 0 && errno == EINTR) continue;
		if (exited) break;	/* the game has gone and nothing more is waiting (a child it left may hold the pipe) */
		if (waitpid(pid, &st, WNOHANG) == pid) exited = 1;
	}
	close(fd[0]);
	if (!exited)
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	if (sScanLen) game_output("\n", 1);
	if (WIFSIGNALED(st))
	{
		say("the game stopped on signal %d (%s)", WTERMSIG(st), strsignal(WTERMSIG(st)));
		return 128 + WTERMSIG(st);
	}
	if (WEXITSTATUS(st) == 0 || WEXITSTATUS(st) == GAME_QUIT_STATUS)
	{
		say("the game closed");
		return 0;
	}
	say("the game exited with status %d", WEXITSTATUS(st));
	return WEXITSTATUS(st);
#endif
}

#ifdef _WIN32
static int redirected_output(DWORD stream)
{
	HANDLE handle = GetStdHandle(stream);
	DWORD mode;
	/* Keep a caller's pipe, file or recognized device (including NUL). Console handles are reopened below, but replacing a
	 * redirected stream with CONOUT$ makes --version invisible to tools reading the launcher's output. */
	return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN &&
		!GetConsoleMode(handle, &mode);
}
#endif

int main(int argc, char **argv)
{
	int updateonly = 0, record = 0, norecord = 0, report = 0, first = argc;
#ifdef _WIN32
	/* Capture redirection before attaching: attachment can supply previously absent standard handles. */
	int out_redirected = redirected_output(STD_OUTPUT_HANDLE);
	int err_redirected = redirected_output(STD_ERROR_HANDLE);
	int console = AttachConsole(ATTACH_PARENT_PROCESS);
	if (console)
	{
		if (!out_redirected) freopen("CONOUT$", "w", stdout);
		if (!err_redirected) freopen("CONOUT$", "w", stderr);
	}
#endif
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--version")) { printf("Onslaught CE v%s" CE_VERSION_NOTE ", build %s\n", CE_VERSION, CE_COMMIT); return 0; }
		else if (!strcmp(argv[i], "--no-update-check")) {}   /* accepted for compatibility; no update check runs */
		else if (!strcmp(argv[i], "--update-only")) updateonly = 1;   /* disabled updater: guidance and exit 2, no game */
		else if (!strcmp(argv[i], "--record")) record = 1;
		else if (!strcmp(argv[i], "--no-record")) norecord = 1;   /* sets record_sessions=0 and starts unrecorded */
		else if (!strcmp(argv[i], "--report")) report = 1;
		else if (!strcmp(argv[i], "--")) { first = i + 1; break; }
		else if (!strcmp(argv[i], "--apply"))
		{
			fprintf(stderr, "Onslaught CE: automatic update installation is disabled; --apply was refused before accessing installation, user or update paths.\n");
			return 2;
		}
		else { first = i; break; }
	}
	if (updateonly) { manual_update_help(); return 2; }
	if (!init_paths())
	{
		say("could not prepare the settings folder; check its permissions and that no path component is a link");
		return 1;
	}
	if (report)
	{
		/* "Report a problem": the pre-filled issue page in the browser, and where the files to attach are. */
		static char url[32768];
		int opened = ce_open_report(CE_VERSION, CE_COMMIT, sUser, url, sizeof url);
		printf("Onslaught CE's logs and session tapes are in %s\n"
			   "%s\n%s\n", sUser, opened ? "Opened the problem report page in your browser:" :
			   "Open this page in your browser to report a problem:", url);
#ifdef _WIN32
		if (!console)
		{
			char text[PATHN + 256];
			snprintf(text, sizeof text, "%s\n\nPlease attach the logs that exist: onslaught-ce.log, onslaught-ce.old.log, "
				"onslaught-ce.concurrent-1.log and onslaught-ce.concurrent-2.log from this folder:\n%s",
					 opened ? "The problem report page is open in your browser." : "Your browser could not be opened.", sUser);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Onslaught CE", text, NULL);
		}
#endif
		return 0;
	}
	const char *tape = getenv("BEA_PORT_TAPE"), *rec = getenv("BEA_PORT_RECORD"), *cont = getenv(LOG_CONTINUE);
	sReplay = (tape && *tape) || (rec && *rec);
	int continuing = cont && *cont;
	unset_env(LOG_CONTINUE);
	if (!sReplay) log_open(continuing);
	char game[PATHN];
	int found = find_game(game, sizeof game);
	if (found <= 0)
	{
		say(found < 0 ? "could not find Battle Engine Aquila in Steam's libraries; set BEA_GAME_DIR to its folder" :
			"no supported Battle Engine Aquila install was chosen");
		log_close();
		return 1;
	}
	char opt[16];
	int ini_record = ini_get("record_sessions", opt, sizeof opt) && !strcmp(opt, "1");
	if (norecord)
	{
		if (ini_record && ini_set("record_sessions", "0")) say("session recording switched off (record_sessions=0 in ce.ini)");
	}
	else if (record || (ini_record && (sReplay || ask_record())))	/* a replay: start_recording says why not */
		start_recording(argv + first, argc - first);
	int rc = finish_recording(start_game(argv + first, argc - first, game));
	log_close();
	return rc;
}
