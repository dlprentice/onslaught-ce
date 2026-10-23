/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_launch.c: Onslaught CE's launcher (port/updater/README.md). Finds the player's Battle Engine Aquila
 * install, checks the signed release feed for an update, and starts the game from this folder with the game folder and
 * a per-user writable folder. It never writes into the game folder.
 *
 *   onslaught-ce [--version] [--no-update-check] [--update-only] [--record] [--no-record] [--report] [-- GAME ARGUMENTS]
 *
 * The log: the game's output and the launcher's own messages go to <user folder>/onslaught-ce.log (the run before is
 * onslaught-ce.old.log; each stops at 4 MB) and still to the terminal. Its first lines name the version, the build, the
 * OS and the GPU (the game's "onslaught-ce: adapter" line). It never names the player's account: home is written ~ (Windows
 * %APPDATA% and the like) and the account name <user>. An update's restart continues the same log. A replay
 * (BEA_PORT_TAPE) or a harness recording (BEA_PORT_RECORD) gets no log, no update check and nothing else of the launcher's.
 * Session recording (--record, or record_sessions=1 in ce.ini): the game records its input to <user folder>/tapes/
 * YYYYMMDD-HHMMSS.tape (port/platform/common/replay.cpp's record mode, on the shim clock at 30 Presents a second) with a
 * .json beside it naming the version, build, OS, date and game arguments; never when a tape is being replayed. While
 * record_sessions=1, each start asks first (Record, Not this time, Stop recording), because a recorded session hides the
 * game's CE page where the setting was switched on; --no-record also sets record_sessions=0 and starts unrecorded.
 * --report opens the pre-filled "Report a problem" page (ce_report.h) and prints the log folder.
 *
 * Update rule: the feed (latest.json and its Ed25519 signature) is verified against the public key below before
 * anything is read from it; the archive's SHA-256 from the verified feed is checked before anything is unpacked; the
 * new version is unpacked beside this folder and swapped in by renaming folders, so a failure at any step leaves the
 * installed version as it was. No telemetry: the only requests are two public GETs (the feed) and, if the player agrees,
 * the archive. Opt out with BEA_NO_UPDATE_CHECK=1 or update_check=0 in ce.ini. */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "monocypher-ed25519.h"
#include "sha256.h"
#include "ce_report.h"

/* Quitting from the game's main menu ends WinMain with the WM_QUIT message's wParam, which the game posts as 13: an
 * ordinary close, not an error, so the launcher reports it as one and exits 0. */
#define GAME_QUIT_STATUS 13

#ifndef CE_VERSION
#define CE_VERSION "dev"
#endif
#ifndef CE_COMMIT
#define CE_COMMIT "unknown"		/* build.py: the source commit (+dirty when the tree had changes) */
#endif
#define FEED "https://github.com/dlprentice/onslaught-ce/releases/latest/download/latest.json"
/* The only place an update archive may come from, even under a valid signature (a leaked key or a mistaken feed cannot
 * send launchers elsewhere); curl -L still follows GitHub's redirect to its download host. */
#define ASSETS "https://github.com/dlprentice/onslaught-ce/releases/download/"
/* The release key's public half (the private half never leaves the release machine). */
static const char PUBLIC_KEY_HEX[] = "b6cf39b727c92cd8a1682c31f315a5d38427dce912e406ccbd4802f0f7634510";

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#define PLATFORM "windows-i686"
#define SEP "\\"
#define GAME_EXE "bea.exe"
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PLATFORM "linux-i686"
#define SEP "/"
#define GAME_EXE "bea"
#endif

#define PATHN 4096
static char sHere[PATHN], sUser[PATHN], sIni[PATHN], sCache[PATHN];
/* A replay (BEA_PORT_TAPE) or a recording someone else set up (BEA_PORT_RECORD, the test harness) as the launcher
 * started: the launcher then adds nothing of its own (no log, no update check, no build name for the CE layer). */
static int sReplay;

/* ---- the log ---------------------------------------------------------------------------------------------------- */

#define LOG_CAP (4u << 20)		/* each log file stops at 4 MB */
#define HOLD_CAP (1u << 20)
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
	if (!sLog || sLogFull) return;
	if (sLogBytes + n + sizeof note > LOG_CAP)
	{
		size_t room = LOG_CAP - sizeof note > sLogBytes ? LOG_CAP - sizeof note - sLogBytes : 0;
		fwrite(p, 1, room < n ? room : n, sLog);
		fwrite(note, 1, sizeof note - 1, sLog);
		sLogFull = 1;
	}
	else
	{
		fwrite(p, 1, n, sLog);
		sLogBytes += n;
	}
	fflush(sLog);
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

/* The game's output: to the terminal as it comes, and to the log a line at a time. */
static void game_output(const char *p, size_t n)
{
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

/* Starts this run's log: the last run's becomes onslaught-ce.old.log. `cont`: the launcher was restarted by an update
 * (BEA_CE_LOG_CONTINUE), so the log goes on in the same file and the run before it (perhaps the one being reported)
 * stays as onslaught-ce.old.log. */
static void log_open(int cont)
{
	char log[PATHN], old[PATHN], os[256], date[64], head[1024];
	snprintf(log, sizeof log, "%s" SEP "onslaught-ce.log", sUser);
	snprintf(old, sizeof old, "%s" SEP "onslaught-ce.old.log", sUser);
	if (cont && (sLog = fopen(log, "ab")))
	{
		fseek(sLog, 0, SEEK_END);
		long at = ftell(sLog);
		sLogBytes = at > 0 ? (size_t)at : 0;
		sLogFull = sLogBytes >= LOG_CAP;
		log_file("\n", 1);
	}
	else
	{
		remove(old);
		rename(log, old);
		sLogBytes = 0;
		sLogFull = 0;
		if (!(sLog = fopen(log, "wb"))) return;
	}
	ce_os_name(os, sizeof os);
	utc_now(date, sizeof date, "%Y-%m-%d %H:%M:%S UTC");
	int k = snprintf(head, sizeof head, "Onslaught CE v%s" CE_VERSION_NOTE ", build %s (" PLATFORM ")\nOS: %s\n%s: %s\n", CE_VERSION,
					 CE_COMMIT, os, cont ? "Restarted after an update" : "Started", date);
	log_file(head, (size_t)k < sizeof head ? (size_t)k : sizeof head - 1);
	sHolding = 1;
}

static void log_close(void)
{
	if (!sLog) return;
	log_release("not reported (the game did not reach its Direct3D device)");
	fclose(sLog);
	sLog = NULL;
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
	fclose(f);
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

/* Runs a program (no shell) and waits; returns its exit status, -1 when it could not start. */
static int run(char *const argv[])
{
#ifdef _WIN32
	char cmd[8192] = "";
	for (int i = 0; argv[i]; i++)
	{
		strcat(cmd, i ? " \"" : "\"");
		strcat(cmd, argv[i]);
		strcat(cmd, "\"");
	}
	STARTUPINFOA si = {sizeof si};
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(argv[0], cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (int)rc;
#else
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0)
	{
		execvp(argv[0], argv);
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static const char *tool(const char *name)
{
#ifdef _WIN32
	/* The system's own curl.exe and tar.exe (Windows 10 1803 and later), never a same-named file found on PATH. */
	static char p[2][MAX_PATH];
	static int i;
	char sys[MAX_PATH];
	GetSystemDirectoryA(sys, sizeof sys);
	snprintf(p[i & 1], MAX_PATH, "%s\\%s.exe", sys, name);
	return p[i++ & 1];
#else
	return name;
#endif
}

/* Tests point the feed elsewhere (BEA_UPDATE_FEED, which may then be a file:// URL); the signature and hash rules are
 * the same. Players' runs fetch only https. */
static const char *feed_url(void)
{
	const char *f = getenv("BEA_UPDATE_FEED");
	return f && *f ? f : FEED;
}

static int fetch(const char *url, const char *to, int seconds)
{
	char t[16];
	snprintf(t, sizeof t, "%d", seconds);
	const char *protos = getenv("BEA_UPDATE_FEED") ? "=https,file" : "=https";
	char *argv[] = {(char *)tool("curl"), "-fsL", "--proto", (char *)protos, "--max-time", t, "-o", (char *)to, (char *)url, NULL};
	return run(argv) == 0;
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

/* ---- updates -------------------------------------------------------------------------------------------------- */

static int json_str(const char *json, const char *key, const char *from, char *out, size_t n)
{
	char k[64];
	snprintf(k, sizeof k, "\"%s\"", key);
	const char *p = strstr(from ? from : json, k);
	if (!p || !(p = strchr(p + strlen(k), ':')) || !(p = strchr(p, '"'))) return 0;
	const char *e = strchr(p + 1, '"');
	if (!e || (size_t)(e - p - 1) >= n) return 0;
	snprintf(out, n, "%.*s", (int)(e - p - 1), p + 1);
	return 1;
}

/* Dotted numeric versions: -1, 0 or 1. "dev" is older than any release. */
static int version_cmp(const char *a, const char *b)
{
	while (*a || *b)
	{
		long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
		if (x != y) return x < y ? -1 : 1;
		if (*a == '.') a++;
		if (*b == '.') b++;
		if (!(*a >= '0' && *a <= '9') && !(*b >= '0' && *b <= '9')) break;
	}
	return 0;
}

static void remove_tree(const char *path);

/* The swap: everything verified and unpacked at `fresh`; `here` becomes here.old-<version>, fresh becomes here. */
static int swap_in(const char *fresh)
{
	char old[PATHN];
	snprintf(old, sizeof old, "%s.old-%s", sHere, CE_VERSION);
	remove_tree(old);
	if (rename(sHere, old) != 0) return 0;
	if (rename(fresh, sHere) != 0)
	{
		rename(old, sHere);
		return 0;
	}
	return 1;
}

static void relaunch(int argc, char **argv);

static int apply_update(const char *version, const char *url, const char *sha_hex, int argc, char **argv)
{
	char archive[PATHN], fresh[PATHN], unpack[PATHN];
	uint8_t want[32], got[32];
	if (!hex_to(sha_hex, want, 32)) return 0;
	/* Beside the install, on the same file system, so the swap is a rename. */
	snprintf(archive, sizeof archive, "%s.download-%s", sHere, version);
	snprintf(unpack, sizeof unpack, "%s.unpack-%s", sHere, version);
	if (!fetch(url, archive, 1800) || !sha256_file(archive, got) || memcmp(got, want, 32))
	{
		remove(archive);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE",
			"The update could not be downloaded, or the download did not match the signed release. Nothing was changed: "
			"this version starts now, and you will be asked again next time.", NULL);
		return 0;
	}
	remove_tree(unpack);
	make_dirs(unpack);
	char *tar[] = {(char *)tool("tar"), "-xf", archive, "-C", unpack, NULL};
	int ok = run(tar) == 0;
	remove(archive);
	/* The archive holds one folder, onslaught-ce-<version>-<platform>. */
	snprintf(fresh, sizeof fresh, "%s" SEP "onslaught-ce-%s-%s", unpack, version, PLATFORM);
	char probe[PATHN];
	snprintf(probe, sizeof probe, "%s" SEP GAME_EXE, fresh);
	if (!ok || !is_file(probe))
	{
		remove_tree(unpack);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be unpacked. Nothing was changed: this version starts now, and you will be asked again next time.", NULL);
		return 0;
	}
#ifdef _WIN32
	/* Windows cannot rename a folder holding a running program: a copy of this launcher in %TEMP% does the swap after
	 * this process exits (--apply FRESH HERE PID). */
	char temp[MAX_PATH], self[MAX_PATH], copy[MAX_PATH], cmd[8192];
	GetTempPathA(sizeof temp, temp);
	GetModuleFileNameA(NULL, self, sizeof self);
	snprintf(copy, sizeof copy, "%sonslaught-ce-update.exe", temp);
	char sdl[MAX_PATH], sdlcopy[MAX_PATH];
	snprintf(sdl, sizeof sdl, "%s\\SDL3.dll", sHere);
	snprintf(sdlcopy, sizeof sdlcopy, "%sSDL3.dll", temp);
	if (!CopyFileA(self, copy, FALSE) || !CopyFileA(sdl, sdlcopy, FALSE)) { remove_tree(unpack); return 0; }
	snprintf(cmd, sizeof cmd, "\"%s\" --apply \"%s\" \"%s\" %lu", copy, fresh, sHere, (unsigned long)GetCurrentProcessId());
	STARTUPINFOA si = {sizeof si};
	PROCESS_INFORMATION pi;
	set_env(LOG_CONTINUE, "1");	/* through the copy to the new launcher: it continues this log */
	if (!CreateProcessA(copy, cmd, NULL, NULL, FALSE, 0, NULL, temp, &si, &pi)) { unset_env(LOG_CONTINUE); remove_tree(unpack); return 0; }
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	say("updating to v%s: the update finishes after this launcher exits", version);
	log_close();
	exit(0);
#else
	if (!swap_in(fresh))
	{
		remove_tree(unpack);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be installed. Nothing was changed: "
								 "this version starts now, and you will be asked again next time.", NULL);
		return 0;
	}
	remove_tree(unpack);
	relaunch(argc, argv);
	return 1;
#endif
}

static void check_for_update(int argc, char **argv)
{
	char opt[16];
	const char *env = getenv("BEA_NO_UPDATE_CHECK");
	if ((env && *env && strcmp(env, "0")) || (ini_get("update_check", opt, sizeof opt) && !strcmp(opt, "0"))) return;
	char feed[PATHN], sig[PATHN], sigurl[1024];
	snprintf(feed, sizeof feed, "%s" SEP "latest.json", sCache);
	snprintf(sig, sizeof sig, "%s" SEP "latest.json.sig", sCache);
	snprintf(sigurl, sizeof sigurl, "%s.sig", feed_url());
	remove(feed);
	remove(sig);
	if (!fetch(feed_url(), feed, 10) || !fetch(sigurl, sig, 10)) return;   /* offline or no release yet: say nothing */
	size_t n = 0;
	char *json = read_all(feed, &n), *sighex = read_all(sig, NULL);
	uint8_t key[32], signature[64];
	int verified = json && sighex && hex_to(PUBLIC_KEY_HEX, key, 32) && strlen(sighex) >= 128 && hex_to(sighex, signature, 64) &&
				   crypto_ed25519_check(signature, key, (const uint8_t *)json, n) == 0;
	char version[64], skip[64], url[1024], sha[80];
	const char *plat = verified ? strstr(json, "\"" PLATFORM "\"") : NULL;
	if (plat && json_str(json, "version", NULL, version, sizeof version) && version_cmp(version, CE_VERSION) > 0 &&
		!(ini_get("skip_version", skip, sizeof skip) && !strcmp(skip, version)) &&
		json_str(json, "url", plat, url, sizeof url) && json_str(json, "sha256", plat, sha, sizeof sha) && strlen(sha) == 64 &&
		(!strncmp(url, ASSETS, strlen(ASSETS)) || getenv("BEA_UPDATE_FEED")))   /* tests' feeds may name file:// archives */
	{
		char text[1024];
		snprintf(text, sizeof text, "Onslaught CE v%s is available (you have v%s).\n\n"
				 "Update: download it, check its signature, replace this copy and start the new version. Your careers and "
				 "settings are kept.\nLater: start this version; you will be asked again next time.\n"
				 "Skip this version: start this version and do not ask about v%s again.", version, CE_VERSION, version);
		const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Update"},
													{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later"}, {0, 2, "Skip this version"}};
		const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE", text, 3, buttons, NULL};
		int which = 0;
		const char *answer = getenv("BEA_UPDATE_ANSWER");   /* tests: update, later or skip, without a dialog */
		int asked = answer && *answer ? (which = !strcmp(answer, "update") ? 1 : !strcmp(answer, "skip") ? 2 : 0, 1)
									  : SDL_ShowMessageBox(&box, &which);
		if (asked)
		{
			if (which == 1) apply_update(version, url, sha, argc, argv);
			else if (which == 2) ini_set("skip_version", version);
		}
	}
	free(json);
	free(sighex);
}

/* ---- folders, launch ------------------------------------------------------------------------------------------ */

static void remove_tree(const char *path)
{
#ifdef _WIN32
	char from[PATHN + 2] = {0};
	snprintf(from, PATHN, "%s", path);
	SHFILEOPSTRUCTA op = {0};
	op.wFunc = FO_DELETE;
	op.pFrom = from;
	op.fFlags = FOF_NO_UI;
	SHFileOperationA(&op);
#else
	DIR *d = opendir(path);
	if (!d) { remove(path); return; }
	struct dirent *e;
	char p[PATHN];
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
		{
			snprintf(p, sizeof p, "%s/%s", path, e->d_name);
			struct stat st;
			if (!lstat(p, &st) && S_ISDIR(st.st_mode)) remove_tree(p);
			else remove(p);
		}
	closedir(d);
	rmdir(path);
#endif
}

/* Old installs left by an update (here.old-*) are removed on the next start. */
static void tidy_old(void)
{
#ifndef _WIN32
	char parent[PATHN], *slash;
	snprintf(parent, sizeof parent, "%s", sHere);
	if (!(slash = strrchr(parent, '/'))) return;
	const char *base = slash + 1;
	char prefix[PATHN];
	snprintf(prefix, sizeof prefix, "%s.old-", base);
	*slash = 0;
	DIR *d = opendir(parent);
	if (!d) return;
	struct dirent *e;
	char p[PATHN];
	while ((e = readdir(d)))
		if (!strncmp(e->d_name, prefix, strlen(prefix)))
		{
			snprintf(p, sizeof p, "%s/%s", parent, e->d_name);
			remove_tree(p);
		}
	closedir(d);
#else
	char pattern[PATHN];
	snprintf(pattern, sizeof pattern, "%s.old-*", sHere);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	char parent[PATHN], *slash;
	snprintf(parent, sizeof parent, "%s", sHere);
	if ((slash = strrchr(parent, '\\'))) *slash = 0;
	do
	{
		char p[PATHN];
		snprintf(p, sizeof p, "%s\\%s", parent, fd.cFileName);
		remove_tree(p);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#endif
}

static void init_paths(void)
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
	snprintf(sCache, sizeof sCache, "%s" SEP "update", sUser);
	make_dirs(sUser);
	make_dirs(sCache);
}

static void relaunch(int argc, char **argv)
{
	say("updated: starting the new version");
	log_close();
	set_env(LOG_CONTINUE, "1");
	char self[PATHN];
	snprintf(self, sizeof self, "%s" SEP "onslaught-ce-launch", sHere);
#ifdef _WIN32
	snprintf(self, sizeof self, "%s\\onslaught-ce.exe", sHere);
	STARTUPINFOA si = {sizeof si};
	PROCESS_INFORMATION pi;
	char cmd[8192];
	snprintf(cmd, sizeof cmd, "\"%s\"", self);
	for (int i = 1; i < argc; i++) { strcat(cmd, " \""); strcat(cmd, argv[i]); strcat(cmd, "\""); }
	if (CreateProcessA(self, cmd, NULL, NULL, FALSE, 0, NULL, sHere, &si, &pi)) exit(0);
#else
	(void)argc;
	argv[0] = self;
	execv(self, argv);
#endif
	/* The new version did not start: this launcher carries on, in the same log. */
	unset_env(LOG_CONTINUE);
	if (sUser[0])
	{
		log_open(1);
		say("the new version could not be started");
	}
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
		if (!*rel && (!strcmp(nm, "ce.ini") || !strcmp(nm, "onslaught-ce.log") || !strcmp(nm, "onslaught-ce.old.log") ||
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

/* Session recording: the game's record mode writes the tape (replay.cpp); the .json beside it says what made it. */
static void start_recording(char **args, int nargs)
{
	const char *tape = getenv("BEA_PORT_TAPE"), *rec = getenv("BEA_PORT_RECORD");
	if ((tape && *tape) || (rec && *rec))
	{
		say("not recording this session: %s is set", tape && *tape ? "BEA_PORT_TAPE (a replay)" : "BEA_PORT_RECORD");
		return;
	}
	char dir[PATHN], stamp[32], path[PATHN], json[PATHN + 8], os[256], date[64];
	time_t now = time(NULL);
	struct tm t;
#ifdef _WIN32
	localtime_s(&t, &now);
#else
	localtime_r(&now, &t);
#endif
	strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &t);
	snprintf(dir, sizeof dir, "%s" SEP "tapes", sUser);
	make_dirs(dir);
	snprintf(path, sizeof path, "%s" SEP "%s.tape", dir, stamp);
	snprintf(json, sizeof json, "%s.json", path);
	const char *clock = getenv("BEA_PORT_CLOCK");
	if (!clock || !*clock) set_env("BEA_PORT_CLOCK", "shim");	/* record mode's clock: 1/30 s a Present, paced to real time */
	set_env("BEA_PORT_RECORD", path);
	ce_os_name(os, sizeof os);
	utc_now(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ");
	FILE *f = fopen(json, "wb");
	if (f)
	{
		fprintf(f, "{\n  \"format\": \"onslaught-ce session tape 1\",\n  \"tape\": ");
		json_string(f, strrchr(path, SEP[0]) + 1);
		fprintf(f, ",\n  \"ce_version\": \"%s\",\n  \"build\": \"%s\",\n  \"platform\": \"%s\",\n  \"os\": ", CE_VERSION, CE_COMMIT, PLATFORM);
		json_string(f, os);
		fprintf(f, ",\n  \"date_utc\": \"%s\",\n  \"clock\": ", date);
		json_string(f, getenv("BEA_PORT_CLOCK"));
		fprintf(f, ",\n  \"presents_per_second\": 30,\n  \"game_args\": [");
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
		fclose(f);
	}
	char shortpath[PATHN];
	ce_short_path(path, shortpath, sizeof shortpath);
	say("recording this session to %s: while recording the game runs at 30 frames a second, and its menus take the "
		"keyboard only", shortpath);
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
		"While recording, the game runs at 30 frames a second with the original menus (use the keyboard in them), so "
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
	set_env("BEA_GAME_DIR", game);
	set_env("BEA_USER_DIR", sUser);
	if (!sReplay) set_env("BEA_CE_BUILD", CE_COMMIT);	/* the game's "Report a problem" page names it (ce.cpp) */
	char shortgame[PATHN];
	ce_short_path(game, shortgame, sizeof shortgame);
	say("game folder %s", shortgame);
#ifdef _WIN32
	char cmd[8192];
	snprintf(cmd, sizeof cmd, "\"%s\"", exe);
	for (int i = 0; i < nargs; i++) { strcat(cmd, " "); strcat(cmd, args[i]); }
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

int main(int argc, char **argv)
{
	int nocheck = 0, updateonly = 0, record = 0, norecord = 0, report = 0, first = argc;
#ifdef _WIN32
	/* A windowed program has no terminal: started from one, write to it (the log is written either way). */
	int console = AttachConsole(ATTACH_PARENT_PROCESS);
	if (console)
	{
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
#endif
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--version")) { printf("Onslaught CE v%s" CE_VERSION_NOTE ", build %s\n", CE_VERSION, CE_COMMIT); return 0; }
		else if (!strcmp(argv[i], "--no-update-check")) nocheck = 1;
		else if (!strcmp(argv[i], "--update-only")) updateonly = 1;   /* check (and update), then exit: no game */
		else if (!strcmp(argv[i], "--record")) record = 1;
		else if (!strcmp(argv[i], "--no-record")) norecord = 1;   /* sets record_sessions=0 and starts unrecorded */
		else if (!strcmp(argv[i], "--report")) report = 1;
		else if (!strcmp(argv[i], "--")) { first = i + 1; break; }
#ifdef _WIN32
		else if (!strcmp(argv[i], "--apply") && i + 3 < argc)
		{
			/* Running from %TEMP%: wait for the old launcher to exit, then swap and start the new one. */
			HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)strtoul(argv[i + 3], NULL, 10));
			if (h) { WaitForSingleObject(h, 30000); CloseHandle(h); }
			snprintf(sHere, sizeof sHere, "%s", argv[i + 2]);
			char unpack[PATHN];
			snprintf(unpack, sizeof unpack, "%s", argv[i + 1]);
			char *s = strrchr(unpack, '\\');
			int ok = swap_in(argv[i + 1]);
			if (s) { *s = 0; remove_tree(unpack); }
			if (!ok)
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be installed. Nothing was "
										 "changed: Onslaught CE starts as it was.", NULL);
			char *none[] = {argv[0], NULL};
			relaunch(1, none);
			return ok ? 0 : 1;
		}
#endif
		else { first = i; break; }
	}
	init_paths();
	if (report)
	{
		/* "Report a problem": the pre-filled issue page in the browser, and where the files to attach are. */
		static char url[32768];
		int opened = ce_open_report(CE_VERSION, CE_COMMIT, sUser, url, sizeof url);
		printf("Onslaught CE's log and session tapes are in %s\n"
			   "%s\n%s\n", sUser, opened ? "Opened the problem report page in your browser:" :
			   "Open this page in your browser to report a problem:", url);
#ifdef _WIN32
		if (!console)
		{
			char text[PATHN + 256];
			snprintf(text, sizeof text, "%s\n\nPlease attach onslaught-ce.log from this folder:\n%s",
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
	tidy_old();
	if (!nocheck && !sReplay) check_for_update(argc, argv);
	if (updateonly) { log_close(); return 0; }
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
	int rc = start_game(argv + first, argc - first, game);
	log_close();
	return rc;
}
