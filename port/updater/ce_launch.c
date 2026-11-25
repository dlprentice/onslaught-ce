/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_launch.c: Onslaught CE's launcher (port/updater/README.md). Finds the player's Battle Engine Aquila
 * install and starts the game from this folder with the game folder and
 * a per-user writable folder. It never writes into the game folder.
 *
 *   onslaught-ce [--version] [--no-update-check] [--update-only] [--rollback] [--record] [--no-record] [--report]
 *                [-- GAME ARGUMENTS]
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
 * record_sessions=1, each start asks first (Record, Not this time, Turn recording off), because a recorded session hides the
 * game's CE page where the setting was switched on; --no-record also sets record_sessions=0 and starts unrecorded.
 * --report opens the pre-filled "Report a problem" page (ce_report.h) and prints the log folder.
 *
 * Updates (ce_update_v2.h, UPDATER-V2.md): a signed ce-update-v2.json feed, never the old latest.json; the player is
 * asked first; a new version is unpacked into <this folder>/versions/ and made active by one atomic state-file
 * replacement, and this folder's own files are never changed. Nothing outside this folder and the user folder's
 * update-v2/ is written, and nothing is deleted by name pattern. The base folder's launcher hands over to the active
 * version at start. --update-only checks now without starting the game; --rollback makes the previous version
 * active again. The historical --apply entry point refuses with exit 2. Old 0.1.0-0.1.6 launchers remain unsafe to
 * invoke; this successor does not repair an older launcher's behavior. */
#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#else
#define _FILE_OFFSET_BITS 64	/* the updater's file sizes and identities */
#endif
#include <SDL3/SDL.h>
#include <inttypes.h>
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
/* The release key's public half: the v2 feed's signature (ce_update_v2.h) and the signing tools. */
static const char PUBLIC_KEY_HEX[] = "b6cf39b727c92cd8a1682c31f315a5d38427dce912e406ccbd4802f0f7634510";

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
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
/* "1" when the player chose the user folder (BEA_USER_DIR before any launcher set it), else "0"; the game logs it. */
static const char *sUserCustom = "0";
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

#ifndef _WIN32
/* The question on the terminal, when stdin and stderr both are one: the text and the numbered buttons; an empty line
 * picks the Return-key default and end of input the Escape-key default. 0 when there is no terminal. */
static int ask_terminal(const SDL_MessageBoxData *box, int *which)
{
	if (!isatty(0) || !isatty(2)) return 0;
	int enter = -1, esc = -1;
	fprintf(stderr, "\n%s\n\n%s\n\n", box->title, box->message);
	for (int i = 0; i < box->numbuttons; i++)
	{
		if (box->buttons[i].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) enter = i;
		if (box->buttons[i].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT) esc = i;
		fprintf(stderr, "  %d. %s%s\n", i + 1, box->buttons[i].text, enter == i ? " (Enter)" : "");
	}
	for (;;)
	{
		char line[64];
		fprintf(stderr, "Choose 1-%d: ", box->numbuttons);
		fflush(stderr);
		int k = -1;
		if (!fgets(line, sizeof line, stdin)) k = esc;
		else if (line[0] == '\n') k = enter;
		else if (atoi(line) >= 1 && atoi(line) <= box->numbuttons) k = atoi(line) - 1;
		else continue;
		if (k < 0) return 0;
		*which = box->buttons[k].buttonID;
		return 1;
	}
}
#endif

/* A message box that cannot take the launcher down. On Linux SDL shows it with zenity or, without zenity, its own X11
 * dialog, and either can fail or crash on a desktop missing a library (0.1.8's and 0.1.9's crashed in SDL's X11 dialog
 * without the 32-bit libXrandr). So the box is shown by a child process; when that fails or crashes, or there is no
 * display, the question goes to the terminal if terfallback is set and there is one. Returns 1 with *which set, or 0
 * when nobody could be asked: callers then take their safe default (an update is never installed). */
static int ask_box(const SDL_MessageBoxData *box, int *which, int terfallback)
{
	/* SDL's dialogs never wrap their text, so it is wrapped here, once, for every box (ce_box_wrap); the terminal question keeps the text as written. */
	char wrapped[2048];
	SDL_MessageBoxData shown = *box;
	if (ce_box_wrap(box->message, wrapped, sizeof wrapped, CE_BOX_COLUMNS)) shown.message = wrapped;
#ifdef _WIN32
	(void)terfallback;
	return SDL_ShowMessageBox(&shown, which);
#else
	const char *x11 = getenv("DISPLAY"), *wayland = getenv("WAYLAND_DISPLAY");
	if ((x11 && *x11) || (wayland && *wayland))
	{
		fflush(NULL);
		pid_t pid = fork();
		if (pid == 0)
		{
			int w = -1;
			_exit(SDL_ShowMessageBox(&shown, &w) && w >= 0 && w < 64 ? w : 100);
		}
		int st = 0;
		if (pid > 0) while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
		if (pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) < 64) { *which = WEXITSTATUS(st); return 1; }
		if (pid > 0 && WIFSIGNALED(st)) say("the message box crashed (signal %d)", WTERMSIG(st));
		else say("the message box could not be shown");
	}
	if (terfallback && ask_terminal(box, which)) return 1;
	say("nobody could be asked (no working message box%s)", terfallback ? " or terminal" : "");
	return 0;
#endif
}

/* An information or error box with one OK button; with no working box the text stays in the log and on the terminal. */
static void tell_box(SDL_MessageBoxFlags flags, const char *text)
{
	const SDL_MessageBoxButtonData ok = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"};
	const SDL_MessageBoxData box = {flags, NULL, "Onslaught CE", text, 1, &ok, NULL};
	int which = 0;
	if (!ask_box(&box, &which, 0)) say("%s", text);
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

/* Set while an update restarts the launcher: the new launcher continues this log instead of rotating it away. */
#define LOG_CONTINUE "BEA_CE_LOG_CONTINUE"

static int sClosedLogValid, sClosedLogSlot = -1;
static uint64_t sClosedLogDev, sClosedLogIno;
static int sContinueTargetValid, sContinueTargetSlot = -1;
static uint64_t sContinueTargetDev, sContinueTargetIno;

static int log_stream_identity(FILE *f, uint64_t *dev, uint64_t *ino)
{
	if (!f) return 0;
#ifdef _WIN32
	int fd = _fileno(f);
	if (fd < 0) return 0;
	intptr_t osf = _get_osfhandle(fd);
	if (osf == -1) return 0;
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle((HANDLE)osf, &info)) return 0;
	*dev = (uint64_t)info.dwVolumeSerialNumber;
	*ino = ((uint64_t)info.nFileIndexHigh << 32) | (uint64_t)info.nFileIndexLow;
	return 1;
#else
	int fd = fileno(f);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0) return 0;
	*dev = (uint64_t)st.st_dev;
	*ino = (uint64_t)st.st_ino;
	return 1;
#endif
}

static void log_parse_continue_token(const char *val)
{
	sContinueTargetValid = 0;
	sContinueTargetSlot = -1;
	sContinueTargetDev = sContinueTargetIno = 0;
	if (!val || !*val) return;
	int slot = -1;
	uint64_t dev = 0, ino = 0;
	char extra = 0;
	if (sscanf(val, "1:%d:%" SCNu64 ":%" SCNu64 "%c", &slot, &dev, &ino, &extra) == 3 &&
		slot >= 0 && slot < CE_LOG_SLOT_COUNT)
	{
		sContinueTargetValid = 1;
		sContinueTargetSlot = slot;
		sContinueTargetDev = dev;
		sContinueTargetIno = ino;
	}
}

/* Starts this run's log: the last run's becomes onslaught-ce.old.log. `cont`: the launcher was restarted by an update
 * (BEA_CE_LOG_CONTINUE), so the log goes on in the same file and the run before it (perhaps the one being reported)
 * stays as onslaught-ce.old.log. */
static void log_open(int cont)
{
	char log[PATHN], os[256], date[64], head[1024];
	sLogBytes = 0; sLogFull = 0;
	int appended = 0;
	int has_target = 0, target_slot = -1;
	uint64_t target_dev = 0, target_ino = 0;
	if (cont)
	{
		if (!sContinueTargetValid)
			log_parse_continue_token(getenv(LOG_CONTINUE));
		has_target = sContinueTargetValid;
		target_slot = sContinueTargetSlot;
		target_dev = sContinueTargetDev;
		target_ino = sContinueTargetIno;
		sContinueTargetValid = 0;
	}
	if (cont && has_target)
	{
		unsigned long error = 0;
		int owned = log_acquire_lease(target_slot, &error);
		if (owned < 0)
		{
			say("log lease failed slot=%s error=%lu; output continues on the terminal only", sLogNames[target_slot], error);
			return;
		}
		if (owned > 0)
		{
			if (!log_path(log, sizeof log, sLogNames[target_slot])) error = log_invalid_file();
			else
			{
				sLog = log_open_file(log, LOG_APPEND, &error);
				if (sLog)
				{
					uint64_t cur_dev = 0, cur_ino = 0;
					long at = ftell(sLog);
					if (!log_stream_identity(sLog, &cur_dev, &cur_ino) || cur_dev != target_dev || cur_ino != target_ino)
					{
						error = log_invalid_file();
						fclose(sLog);
						sLog = NULL;
					}
					else if (at < 0)
					{
						error = log_native_error();
						fclose(sLog);
						sLog = NULL;
					}
					else
					{
						sLogBytes = (size_t)at;
						sLogFull = sLogBytes >= LOG_CAP;
						appended = 1;
					}
				}
				else if (log_missing(error))
					sLog = log_open_file(log, LOG_CREATE, &error);
			}
			if (!sLog)
			{
				say("log open/rotation refused slot=%s error=%lu; existing files were not truncated", sLogNames[target_slot], error);
				log_release_lease();
			}
		}
	}
	for (int slot = 0; !sLog && slot < CE_LOG_SLOT_COUNT; ++slot)
	{
		if (cont && has_target && slot == target_slot) continue;
		unsigned long error = 0;
		int owned = log_acquire_lease(slot, &error);
		if (!owned) continue;
		if (owned < 0) { say("log lease failed slot=%s error=%lu; output continues on the terminal only", sLogNames[slot], error); return; }
		if (!log_path(log, sizeof log, sLogNames[slot])) { error = log_invalid_file(); }
		else if (cont && !has_target && slot == 0)
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
	/* The lowercase "Onslaught CE v" stays: it is log_open_file's ownership magic, so earlier versions' logs still rotate. */
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
	sClosedLogValid = 0;
	sClosedLogSlot = -1;
	sClosedLogDev = sClosedLogIno = 0;
	if (sLog)
	{
		log_release("not reported (the game did not reach its Direct3D device)");
		if (sLog)
		{
			uint64_t dev = 0, ino = 0;
			int slot = sLogSlot;
			int have_id = slot >= 0 && slot < CE_LOG_SLOT_COUNT && log_stream_identity(sLog, &dev, &ino);
			FILE *file = sLog; sLog = NULL;
			if (fclose(file)) fprintf(stderr, "onslaught-ce: log close failed error=%lu\n", log_native_error());
			else if (have_id)
			{
				sClosedLogValid = 1;
				sClosedLogSlot = slot;
				sClosedLogDev = dev;
				sClosedLogIno = ino;
			}
		}
	}
	log_release_lease();
}

/* ---- small helpers ---------------------------------------------------------------------------------------------- */

static void set_env(const char *name, const char *value)
{
	char cont_buf[96];
	if (name && value && !strcmp(name, LOG_CONTINUE) && !strcmp(value, "1") && sClosedLogValid)
	{
		snprintf(cont_buf, sizeof cont_buf, "1:%d:%" PRIu64 ":%" PRIu64,
			sClosedLogSlot, sClosedLogDev, sClosedLogIno);
		value = cont_buf;
	}
#ifdef _WIN32
	SetEnvironmentVariableA(name, value);
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

static void unset_env(const char *name)
{
	if (name && !strcmp(name, LOG_CONTINUE))
		log_parse_continue_token(getenv(LOG_CONTINUE));
#ifdef _WIN32
	SetEnvironmentVariableA(name, NULL);
	_putenv_s(name, "");
#else
	unsetenv(name);
#endif
}

/* p names a regular file, whether or not it can be read. */
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

static char *read_stream_bounded(intptr_t raw, size_t max_bytes, size_t *n)
{
	size_t cap = 4096, len = 0;
	if (cap > max_bytes + 1) cap = max_bytes + 1;
	char *b = malloc(cap + 1);
	int bad = !b;
	while (!bad)
	{
		if (len == cap)
		{
			if (cap >= max_bytes + 1) { bad = 1; break; }
			size_t next_cap = cap * 2;
			if (next_cap > max_bytes + 1 || next_cap < cap) next_cap = max_bytes + 1;
			char *nb = realloc(b, next_cap + 1);
			if (!nb) { bad = 1; break; }
			b = nb;
			cap = next_cap;
		}
		size_t want = cap - len;
#ifdef _WIN32
		DWORD got = 0;
		if (!ReadFile((HANDLE)raw, b + len, (DWORD)want, &got, NULL)) { bad = 1; break; }
		size_t r = (size_t)got;
#else
		ssize_t got = read((int)raw, b + len, want);
		if (got < 0) { if (errno == EINTR) continue; bad = 1; break; }
		size_t r = (size_t)got;
#endif
		if (r == 0) break;
		len += r;
		if (len > max_bytes) { bad = 1; break; }
	}
#ifdef _WIN32
	if (!CloseHandle((HANDLE)raw)) bad = 1;
#else
	if (close((int)raw) != 0) bad = 1;
#endif
	if (bad) { free(b); return NULL; }
	b[len] = 0;
	if (n) *n = len;
	return b;
}

static char *read_regular_bounded(const char *p, size_t max_bytes, size_t *n)
{
	if (n) *n = 0;
	if (!p || !*p) return NULL;
#ifdef _WIN32
	HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
		info.nFileSizeHigh || info.nFileSizeLow > max_bytes)
	{
		CloseHandle(h);
		return NULL;
	}
	return read_stream_bounded((intptr_t)h, max_bytes, n);
#else
	int fd = open(p, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > (uint64_t)max_bytes)
	{
		close(fd);
		return NULL;
	}
	return read_stream_bounded((intptr_t)fd, max_bytes, n);
#endif
}

static char *read_all(const char *p, size_t *n)
{
	return read_regular_bounded(p, 4u * 1024u * 1024u, n);
}

static char *read_ini_bounded(const char *ini_path, size_t *n)
{
	if (n) *n = 0;
	char parent[4200], name[256];
	if (!ini_path || !*ini_path || (size_t)snprintf(parent, sizeof parent, "%s", ini_path) >= sizeof parent) return NULL;
	char *slash = strrchr(parent, '/');
#ifdef _WIN32
	char *backslash = strrchr(parent, '\\');
	if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif
	const char *base = slash ? slash + 1 : parent;
	if (!*base || !strcmp(base, ".") || !strcmp(base, "..") ||
		(size_t)snprintf(name, sizeof name, "%s", base) >= sizeof name) return NULL;
#ifdef _WIN32
	if (strchr(name, ':')) return NULL;
#endif
	if (!slash)
	{
#ifdef _WIN32
		DWORD cwd_len = GetCurrentDirectoryA(sizeof parent, parent);
		if (!cwd_len || cwd_len >= sizeof parent) return NULL;
#else
		if (!getcwd(parent, sizeof parent)) return NULL;
#endif
	}
	else if (slash == parent) slash[1] = 0;
#ifdef _WIN32
	else if (slash == parent + 2 && parent[1] == ':') slash[1] = 0;
#endif
	else *slash = 0;
	CeSettingsDir dir;
	if (!ce_settings_dir_open_ex(parent, 0, &dir, NULL)) return NULL;
#ifdef _WIN32
	HANDLE raw = INVALID_HANDLE_VALUE;
	char full[4500];
	if ((size_t)snprintf(full, sizeof full, "%s\\%s", dir.path, name) < sizeof full)
	{
		HANDLE in = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (in != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION info;
			if (GetFileType(in) == FILE_TYPE_DISK && GetFileInformationByHandle(in, &info) &&
				!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
				!info.nFileSizeHigh && info.nFileSizeLow <= CE_INI_MAX_BYTES)
				raw = in;
			else
				CloseHandle(in);
		}
	}
	ce_settings_dir_close(&dir);
	if (raw == INVALID_HANDLE_VALUE) return NULL;
	return read_stream_bounded((intptr_t)raw, CE_INI_MAX_BYTES, n);
#else
	int raw = -1;
	int input = openat(dir.fd, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
	if (input >= 0)
	{
		struct stat st;
		if (fstat(input, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > CE_INI_MAX_BYTES)
			close(input);
		else
			raw = input;
	}
	ce_settings_dir_close(&dir);
	if (raw < 0) return NULL;
	return read_stream_bounded((intptr_t)raw, CE_INI_MAX_BYTES, n);
#endif
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

/* ce.ini: key=value lines in the user folder (optional UTF-8 BOM, optional spaces or tabs before key, around '=', and after value). */
static int ini_get(const char *key, char *out, size_t n)
{
	if (out && n) out[0] = 0;
	if (!key || !*key || !out || !n || !*sIni) return 0;
	char *t = read_ini_bounded(sIni, NULL);
	if (!t) return 0;
	char *start = t;
	if ((unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF)
		start += 3;
	size_t k = strlen(key);
	int found = 0;
	for (char *l = start; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL)
	{
		char *p = l;
		while (*p == ' ' || *p == '\t') p++;
		if (!strncmp(p, key, k))
		{
			char *eq = p + k;
			while (*eq == ' ' || *eq == '\t') eq++;
			if (*eq == '=')
			{
				char *e = eq + 1;
				size_t m = strcspn(e, "\r\n");
				while (m > 0 && (*e == ' ' || *e == '\t')) { e++; m--; }
				while (m > 0 && (e[m - 1] == ' ' || e[m - 1] == '\t')) m--;
				if (m < n)
				{
					memcpy(out, e, m);
					out[m] = 0;
					found = 1;
				}
				break;
			}
		}
	}
	free(t);
	return found;
}

/* What the player is told when a choice made in one of the launcher's questions could not be kept in ce.ini: what will happen
 * (the question comes back), then why. A ce.ini its owner made read-only is refused (ce_report.h), where 0.1.13 on Linux replaced
 * it, and without these words "Skip this version" and "Turn recording off" came back at every start with no sign of the reason. */
#define CE_BOX_UNSAVED_SKIP "Onslaught CE could not remember to skip this version, so it will offer the update again the next time it starts."
#define CE_BOX_UNSAVED_RECORD "Session recording could not be turned off, so Onslaught CE will ask about it again the next time it starts."
#define CE_BOX_INI_READONLY \
	"The settings file ce.ini in your Onslaught CE user folder is read-only, and Onslaught CE does not change a read-only file. " \
	"To let it save choices like this one, make ce.ini writable again."
#define CE_BOX_INI_UNWRITABLE \
	"The settings file ce.ini in your Onslaught CE user folder could not be written. Check that the folder and the file can be " \
	"written and that the disk is not full. Starting Onslaught CE with --report shows where the folder is."

static int sIniQuiet;	/* the write was asked for by a command-line switch: no box, as nobody may be there to close it */

/* Says it once a start for each of the two choices, in a box; where the answer came from a switch or from a test's environment
 * variable, or no box can be shown, the same words go to the terminal and the log. Other keys (the game folder the launcher found by
 * itself) are no choice the player made at this start: they keep the one log line. */
static void ini_tell_unsaved(const char *key, int refusal)
{
	static int told_skip, told_record;
	int skip = !strcmp(key, "skip_version"), record = !strcmp(key, "record_sessions");
	if (!skip && !record) return;
	int *told = skip ? &told_skip : &told_record;
	if (*told) return;
	*told = 1;
	char text[1024];
	snprintf(text, sizeof text, "%s\n\n%s", skip ? CE_BOX_UNSAVED_SKIP : CE_BOX_UNSAVED_RECORD,
			 refusal == CE_INI_REFUSED_READONLY ? CE_BOX_INI_READONLY : CE_BOX_INI_UNWRITABLE);
	/* BEA_RECORD_ANSWER answers the recording question in every build (ask_record); BEA_UPDATE_ANSWER is honoured only in the
	 * updater tests' builds (ce_update_v2.h), so elsewhere it gives no answer and the player still gets the box. */
	const char *record_answer = getenv("BEA_RECORD_ANSWER"), *update_answer = NULL;
#ifdef CE_UPDATER_TESTING
	update_answer = getenv("BEA_UPDATE_ANSWER");
#endif
	if (sIniQuiet || (record_answer && *record_answer) || (update_answer && *update_answer)) say("%s", text);
	else tell_box(SDL_MESSAGEBOX_WARNING, text);
}

/* ce_ini_write_ex (ce_report.h): written whole beside ce.ini and renamed over it, so a failed write leaves it as it was. */
static int ini_set(const char *key, const char *value)
{
	int refusal = CE_INI_REFUSED_NONE;
	if (ce_ini_write_ex(sIni, key, value, &refusal)) return 1;
	say("could not write %s to ce.ini; it is unchanged%s", key,
		refusal == CE_INI_REFUSED_READONLY ? " (the file is read-only)" : "");
	ini_tell_unsaved(key, refusal);
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
			return snprintf(why, n, "its %s is a different edition from the one Onslaught CE supports", GAME_FILES[i].name), 1;
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
	static const struct { HKEY root; const char *subkey; const char *val; } keys[] = {
		{HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath"},
		{HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"},
		{HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath"},
	};
	for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
	{
		DWORD len = sizeof steam;
		if (RegGetValueA(keys[i].root, keys[i].subkey, keys[i].val, RRF_RT_REG_SZ, NULL, steam, &len) == ERROR_SUCCESS)
		{
			for (char *c = steam; *c; c++) if (*c == '/') *c = '\\';
			if (steam_libraries(steam, found, n)) return 1;
		}
	}
	static const char *const gog[] = {
		"C:\\GOG Games\\Battle Engine Aquila",
		"C:\\Program Files (x86)\\GOG Galaxy\\Games\\Battle Engine Aquila",
	};
	for (size_t i = 0; i < sizeof gog / sizeof *gog; i++)
		if (supported_install(gog[i])) { snprintf(found, n, "%s", gog[i]); return 1; }
	return 0;
#else
	const char *home = getenv("HOME");
	if (!home || !*home) return 0;
	const size_t hlen = ce_trim_sep_len(home, 1);
	const char *roots[] = {
		"/.local/share/Steam",
		"/.steam/steam",
		"/.steam/root",
		"/.var/app/com.valvesoftware.Steam/.local/share/Steam",
		"/snap/steam/common/.local/share/Steam",
	};
	char steam[PATHN];
	for (size_t i = 0; i < sizeof roots / sizeof *roots; i++)
	{
		snprintf(steam, sizeof steam, "%.*s%s", (int)hlen, home, roots[i]);
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
static char sPickError[256];
static SDL_AtomicInt sPickDone;

static void SDLCALL picked(void *userdata, const char *const *files, int filter)
{
	(void)userdata; (void)filter;
	if (!files)
	{
		const char *err = SDL_GetError();
		if (err && *err) snprintf(sPickError, sizeof sPickError, "%s", err);
	}
	else if (files[0])
	{
		snprintf(sPicked, sizeof sPicked, "%s", files[0]);
	}
	SDL_SetAtomicInt(&sPickDone, 1);
}

/* True when user is game or lies beneath it. Fails closed when paths cannot be inspected. */
static int user_inside_game(const char *user, const char *game)
{
	if (!user || !*user || !game || !*game) return 0;
#ifdef _WIN32
	WCHAR a[PATHN], b[PATHN], fa[PATHN], fb[PATHN];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, user, -1, a, PATHN) ||
		!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, game, -1, b, PATHN))
		return 1;
	DWORD la = GetFullPathNameW(a, PATHN, fa, NULL), lb = GetFullPathNameW(b, PATHN, fb, NULL);
	if (!la || la >= PATHN || !lb || lb >= PATHN) return 1;
	while (lb > 3 && fb[lb - 1] == L'\\') fb[--lb] = 0;
	if (la < lb || CompareStringOrdinal(fa, (int)lb, fb, (int)lb, TRUE) != CSTR_EQUAL) return 0;
	return fa[lb] == 0 || fa[lb] == L'\\' || fb[lb - 1] == L'\\';
#else
	struct stat gst;
	if (stat(game, &gst) != 0) return 0;
	int cur = open(user, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cur < 0) return 1;
	for (int depth = 0; depth < 512; ++depth)
	{
		struct stat here;
		if (fstat(cur, &here) != 0) { close(cur); return 1; }
		if (here.st_dev == gst.st_dev && here.st_ino == gst.st_ino) { close(cur); return 1; }
		int up = openat(cur, "..", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (up < 0) { close(cur); return 1; }
		struct stat parent;
		if (fstat(up, &parent) != 0) { close(cur); close(up); return 1; }
		close(cur);
		if (parent.st_dev == here.st_dev && parent.st_ino == here.st_ino) { close(up); return 0; }
		cur = up;
	}
	close(cur);
	return 1;
#endif
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
		const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE", CE_BOX_GAME_MISSING, 2, buttons, NULL};
		int which = 0;
		if (!ask_box(&box, &which, 0)) return -1;
		if (which != 1) return 0;
		SDL_SetAtomicInt(&sPickDone, 0);
		sPicked[0] = 0;
		sPickError[0] = 0;
		SDL_ClearError();
		SDL_ShowOpenFolderDialog(picked, NULL, NULL, NULL, false);
		while (!SDL_GetAtomicInt(&sPickDone))
		{
			SDL_PumpEvents();
			SDL_Delay(20);
		}
		if (!sPicked[0])
		{
			if (sPickError[0])
			{
				char text[640];
#ifdef _WIN32
				say("could not open the folder picker (%s); set BEA_GAME_DIR to your Battle Engine Aquila folder", sPickError);
#else
				say("could not open the folder picker (%s); install zenity or xdg-desktop-portal, or set BEA_GAME_DIR=/path/to/Battle Engine Aquila", sPickError);
#endif
				ce_box_text_picker_failed(text, sizeof text, sPickError);
				tell_box(SDL_MESSAGEBOX_WARNING, text);
			}
			return 0;
		}
		char why[256], text[512];
		if (!install_problem(sPicked, why, sizeof why)) { snprintf(found, n, "%s", sPicked); return 1; }
		say("the chosen folder is not a supported Battle Engine Aquila install: %s", why);
		ce_box_text_bad_folder(text, sizeof text, why);
		tell_box(SDL_MESSAGEBOX_WARNING, text);
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
	if (found > 0 && !user_inside_game(sUser, dir)) ini_set("gamedir", dir);
	return found;
}

/* ---- updates ------------------------------------------------------------------------------------------------- */

#include "ce_update_v2.h"

/* ---- folders, launch ------------------------------------------------------------------------------------------ */



/* This program's folder (sHere): the updater needs it before anything else runs. */
static void init_here(void)
{
#ifdef _WIN32
	GetModuleFileNameA(NULL, sHere, sizeof sHere);
	char *s = strrchr(sHere, '\\');
#else
	ssize_t n = readlink("/proc/self/exe", sHere, sizeof sHere - 1);
	sHere[n > 0 ? n : 0] = 0;
	char *s = strrchr(sHere, '/');
#endif
	if (s) *s = 0;
}

static int init_paths(void)
{
	init_here();
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");
	if (appdata && *appdata) snprintf(sUser, sizeof sUser, "%.*s\\Onslaught CE", (int)ce_trim_sep_len(appdata, 1), appdata);
	else snprintf(sUser, sizeof sUser, "Onslaught CE");
#else
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && *xdg) snprintf(sUser, sizeof sUser, "%.*s/onslaught-ce", (int)ce_trim_sep_len(xdg, 1), xdg);
	else if (home && *home) snprintf(sUser, sizeof sUser, "%.*s/.local/share/onslaught-ce", (int)ce_trim_sep_len(home, 1), home);
	else snprintf(sUser, sizeof sUser, ".local/share/onslaught-ce");
#endif
	const char *user = getenv("BEA_USER_DIR"), *custom = getenv("BEA_CE_CUSTOM_USER_DIR");
	if (user && *user) snprintf(sUser, sizeof sUser, "%.*s", (int)ce_trim_sep_len(user, 0), user);
	/* A launcher restarted by an update inherits BEA_USER_DIR from us: keep the first launcher's answer. */
	sUserCustom = custom && (!strcmp(custom, "0") || !strcmp(custom, "1")) ? custom : user && *user ? "1" : "0";
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

/* Explicit ASCII case-insensitive helpers for replay start_files allowlisting. */
static int ascii_ieq(const char *a, const char *b)
{
	while (*a && *b)
	{
		unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
		if (ca != cb) return 0;
	}
	return *a == 0 && *b == 0;
}

static int ascii_iends(const char *s, const char *suffix)
{
	size_t sl = strlen(s), xl = strlen(suffix);
	return sl > xl && ascii_ieq(s + sl - xl, suffix);
}

/* The user folder's retail state files as the session starts (defaultoptions.bea, registry.ini, savegames/<name>.bes),
 * so a replay can start from the same state: "relative/path": base64, in the .json. Using a strict allowlist ensures
 * private credentials (online-profile.key, online-profile.ini), ce.ini, logs, mods, backups, screenshots, and links
 * can never be exported into shareable session recordings. */
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
		char path[PATHN], sub[PATHN];
		snprintf(path, sizeof path, "%s" SEP "%s", dir, nm);
		snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", nm);
#ifdef _WIN32
		DWORD attr = GetFileAttributesA(path);
		if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
		int isdir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
		struct stat st;
		if (lstat(path, &st) != 0) continue;
		int isdir = S_ISDIR(st.st_mode);
		if (!isdir && !S_ISREG(st.st_mode)) continue;
#endif
		if (isdir)
		{
			if (!*rel && ascii_ieq(nm, "savegames"))
				json_start_files(f, path, sub, first, total);
			continue;
		}
		if (!*rel)
		{
			if (!ascii_ieq(nm, "defaultoptions.bea") && !ascii_ieq(nm, "registry.ini"))
				continue;
		}
		else if (ascii_ieq(rel, "savegames"))
		{
			if (!ascii_iends(nm, ".bes"))
				continue;
		}
		else
		{
			continue;
		}
		if (*total >= START_CAP) continue;
		size_t max_file = START_CAP - *total;
		if (max_file > (1u << 20)) max_file = (1u << 20);
		size_t n = 0;
		char *data = read_regular_bounded(path, max_file, &n);
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
 * in a recorded session (its menus are the original game's, so the tape can be replayed), so the way to switch it off is
 * here. Returns 1 to record; "Turn recording off" also sets record_sessions=0. No dialog (no display): record, as set. */
static int ask_record(void)
{
	const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Record"},
												{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not this time"},
												{0, 2, "Turn recording off"}};
	const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE", CE_BOX_RECORD, 3, buttons, NULL};
	int which = 1;
	const char *answer = getenv("BEA_RECORD_ANSWER");   /* tests: record, once or stop, without a dialog */
	if (answer && *answer) which = !strcmp(answer, "stop") ? 2 : !strcmp(answer, "once") ? 0 : 1;
	else if (!ask_box(&box, &which, 1)) which = 1;
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
	set_env("BEA_CE_CUSTOM_USER_DIR", sUserCustom);
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
	int piped = 0;
	if (CreatePipe(&rd, &wr, &sa, 0))
	{
		if (SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0))
			piped = 1;
		else
		{
			CloseHandle(rd);
			CloseHandle(wr);
			rd = wr = NULL;
		}
	}
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
		DWORD err = GetLastError();
		if (piped)
		{
			CloseHandle(rd);
			CloseHandle(wr);
		}
		say("could not start %s (error %lu)", exe, (unsigned long)err);
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
	if (!argv) return 1;
	argv[0] = exe;
	for (int i = 0; i < nargs; i++) argv[i + 1] = args[i];
	/* The game's stdout and stderr come through one pipe (their order kept), to the terminal and the log. */
	int fd[2];
	if (pipe(fd) != 0)
	{
		if (sRecordingRequested)
		{
			say("could not start the recorded game: output capture is unavailable");
			free(argv);
			return 3;
		}
		log_close();
		execv(exe, argv);
		free(argv);
		return 1;
	}
	fcntl(fd[0], F_SETFD, FD_CLOEXEC);
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0)
	{
		say("could not start %s", exe);
		close(fd[0]);
		close(fd[1]);
		free(argv);
		return 1;
	}
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
	free(argv);
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
	int updateonly = 0, rollback = 0, nocheck = 0, record = 0, norecord = 0, report = 0, first = argc;
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
	/* The historical in-place swap helper's entry point is refused before anything else, whatever version is active. */
	static const char *const options[] = {"--version", "--no-update-check", "--update-only", "--rollback", "--record",
		"--no-record", "--report"};
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--apply"))
		{
			fprintf(stderr, "Onslaught CE: --apply was refused before accessing installation, user or update paths.\n");
			return 2;
		}
		int known = 0;	/* the option parser below stops at the first other argument: game arguments start there */
		for (size_t k = 0; k < sizeof options / sizeof *options; ++k) known |= !strcmp(argv[i], options[k]);
		if (!known) break;
	}
	/* The base folder's launcher hands over to the active updated version (versions/, ce_update_v2.h). */
	init_here();
	upd_find_root();
	{
		int status = 0;
		if (upd_start_active(argc, argv, 0, &status)) return status;
	}
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--version"))
		{
			printf("Onslaught CE V%s" CE_VERSION_NOTE ", build %s%s\n", CE_VERSION, CE_COMMIT, UPD_TEST_NOTE);
			return 0;
		}
		else if (!strcmp(argv[i], "--no-update-check")) nocheck = 1;
		else if (!strcmp(argv[i], "--update-only")) updateonly = 1;   /* check and offer an update now; no game */
		else if (!strcmp(argv[i], "--rollback")) rollback = 1;        /* the previous version becomes active; no game */
		else if (!strcmp(argv[i], "--record")) record = 1;
		else if (!strcmp(argv[i], "--no-record")) norecord = 1;   /* sets record_sessions=0 and starts unrecorded */
		else if (!strcmp(argv[i], "--report")) report = 1;
		else if (!strcmp(argv[i], "--")) { first = i + 1; break; }
		else { first = i; break; }
	}
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
			/* Only the sentence is wrapped; the folder follows it whole, on a line of its own. Wrapping the whole text broke a folder
			 * longer than a line at one of its spaces (ce_box_wrap turns a space into a line feed), and this box is the one place a
			 * player without a console is shown the folder, to be read back exactly. */
			char text[PATHN + 256], shown[2 * PATHN + 512];
			ce_box_text_report(text, sizeof text, opened, "");
			if (!ce_box_wrap(text, shown, sizeof shown, CE_BOX_COLUMNS)) snprintf(shown, sizeof shown, "%s", text);
			snprintf(shown + strlen(shown), sizeof shown - strlen(shown), "%s", sUser);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Onslaught CE", shown, NULL);
		}
#endif
		return 0;
	}
	const char *tape = getenv("BEA_PORT_TAPE"), *rec = getenv("BEA_PORT_RECORD"), *cont = getenv(LOG_CONTINUE);
	const char *updated = getenv("BEA_CE_JUST_UPDATED");
	sReplay = (tape && *tape) || (rec && *rec);
	int continuing = cont && *cont, just_updated = updated && *updated;
	unset_env(LOG_CONTINUE);
	unset_env("BEA_CE_JUST_UPDATED");
	if (rollback || updateonly)
	{
		log_open(continuing);
		int rc;
		if (rollback) rc = upd_rollback();
		else if (nocheck) { say("update: --no-update-check: nothing checked"); rc = UPD_DISABLED; }	/* no request */
		else rc = upd_check(1);
		if (updateonly && rc == UPD_UPDATED) say("update: installed; the next start runs the new version");
		log_close();
		return rc;
	}
	if (!sReplay) log_open(continuing);
	if (!sReplay && !nocheck && !just_updated && upd_check(0) == UPD_UPDATED)
	{
		/* Start the new version with the same arguments; it continues this log and does not check again. */
		say("update: starting the new version");
		log_close();
		set_env(LOG_CONTINUE, "1");
		set_env("BEA_CE_JUST_UPDATED", "1");
		int status = 0;
		if (upd_start_active(argc, argv, 1, &status)) return status;
		unset_env(LOG_CONTINUE);
		unset_env("BEA_CE_JUST_UPDATED");
		log_open(1);
		say("update: the new version could not be started; this version starts instead");
	}
	char game[PATHN];
	int found = find_game(game, sizeof game);
	if (found <= 0)
	{
		say(found < 0 ? "could not find Battle Engine Aquila in Steam's libraries; set BEA_GAME_DIR to its folder" :
			"no supported Battle Engine Aquila install was chosen");
		log_close();
		return 1;
	}
	if (user_inside_game(sUser, game))
	{
		say("the user folder cannot be inside the game folder; set BEA_USER_DIR to a separate folder");
		log_close();
		return 1;
	}
	char opt[16];
	int ini_record = ini_get("record_sessions", opt, sizeof opt) && !strcmp(opt, "1");
	if (norecord)
	{
		sIniQuiet = 1;	/* --no-record: a failed write is said on the terminal and in the log, not in a box */
		if (ini_record && ini_set("record_sessions", "0")) say("session recording switched off (record_sessions=0 in ce.ini)");
		sIniQuiet = 0;
	}
	else if (record || (ini_record && (sReplay || ask_record())))	/* a replay: start_recording says why not */
	{
		start_recording(argv + first, argc - first);
		/* The explicit CLI path may be unattended even with a display; do not add a blocking dialog there. */
		if (*sRecordingPath)
			say("Direct gamepad and flight-stick controls are unavailable while recording; use the keyboard and mouse.");
	}
	int rc = finish_recording(start_game(argv + first, argc - first, game));
	log_close();
	return rc;
}
