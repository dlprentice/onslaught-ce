/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_report.h: what the launcher (ce_launch.c) and the game (port/platform/common/ce.cpp) share for
 * "Report a problem": the per-user folder, the OS name, the newest session tape, and the pre-filled GitHub issue page;
 * and the one safe way to change a line of ce.ini (ce_ini_write).
 * Header only, valid C and C++; the includer has SDL3 (SDL_OpenURL). Nothing is sent: the page opens in the player's
 * browser with the text filled in, and the player decides whether to submit it. */
#ifndef CE_REPORT_H
#define CE_REPORT_H

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#define CE_ISSUES "https://github.com/dlprentice/onslaught-ce/issues/new"
/* After the version wherever it is shown (patch 0037's menu text says the same). */
#ifndef CE_VERSION_NOTE
#define CE_VERSION_NOTE " (preview)"
#endif

/* The per-user folder: BEA_USER_DIR, else Linux $XDG_DATA_HOME/onslaught-ce or ~/.local/share/onslaught-ce, Windows
 * %APPDATA%\Onslaught CE (the game's own default, port/platform/linux/paths.cpp and windows/files.cpp). */
static inline void ce_user_dir(char *out, size_t n)
{
	const char *user = getenv("BEA_USER_DIR");
	if (user && *user) { snprintf(out, n, "%s", user); return; }
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");
	snprintf(out, n, "%s\\Onslaught CE", appdata ? appdata : ".");
#else
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && *xdg) snprintf(out, n, "%s/onslaught-ce", xdg);
	else snprintf(out, n, "%s/.local/share/onslaught-ce", home ? home : ".");
#endif
}

/* "Linux: Arch Linux, kernel 6.10.2 x86_64" or "Windows 10.0 build 19045" ("under Wine 9.0" when it is). */
static inline void ce_os_name(char *out, size_t n)
{
#ifdef _WIN32
	typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
	typedef const char *(CDECL * WineVersionFn)(void);
	HMODULE nt = GetModuleHandleA("ntdll.dll");
	RtlGetVersionFn get = nt ? (RtlGetVersionFn)(void *)GetProcAddress(nt, "RtlGetVersion") : NULL;
	WineVersionFn wine = nt ? (WineVersionFn)(void *)GetProcAddress(nt, "wine_get_version") : NULL;
	OSVERSIONINFOW v;
	memset(&v, 0, sizeof v);
	v.dwOSVersionInfoSize = sizeof v;
	if (get && get(&v) == 0)
		snprintf(out, n, "Windows %lu.%lu build %lu", (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion,
				 (unsigned long)v.dwBuildNumber);
	else
		snprintf(out, n, "Windows");
	if (wine)
	{
		size_t l = strlen(out);
		snprintf(out + l, n - l, " under Wine %s", wine());
	}
#else
	char pretty[128] = "";
	FILE *f = fopen("/etc/os-release", "r");
	if (!f) f = fopen("/usr/lib/os-release", "r");
	if (f)
	{
		char line[256];
		while (fgets(line, sizeof line, f))
			if (!strncmp(line, "PRETTY_NAME=", 12))
			{
				char *v = line + 12, *e;
				if (*v == '"') v++;
				if ((e = strpbrk(v, "\"\r\n"))) *e = 0;
				snprintf(pretty, sizeof pretty, "%s", v);
				break;
			}
		fclose(f);
	}
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(out, n, "Linux: %s%skernel %s %s", pretty, *pretty ? ", " : "", u.release, u.machine);
	else
		snprintf(out, n, "Linux: %s", *pretty ? pretty : "unknown");
#endif
}

/* The newest session tape in <user>/tapes (named YYYYMMDD-HHMMSS.tape, so the greatest name is the newest); 0 if none. */
static inline int ce_latest_tape(const char *user, char *out, size_t n)
{
	char best[64] = "";
#ifdef _WIN32
	char pattern[4096];
	snprintf(pattern, sizeof pattern, "%s\\tapes\\*.tape", user);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do
		if (strlen(fd.cFileName) < sizeof best && strcmp(fd.cFileName, best) > 0) snprintf(best, sizeof best, "%s", fd.cFileName);
	while (FindNextFileA(h, &fd));
	FindClose(h);
	if (!*best) return 0;
	snprintf(out, n, "%s\\tapes\\%s", user, best);
#else
	char dir[4096];
	snprintf(dir, sizeof dir, "%s/tapes", user);
	DIR *d = opendir(dir);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir(d)))
	{
		size_t l = strlen(e->d_name);
		if (l > 5 && l < sizeof best && !strcmp(e->d_name + l - 5, ".tape") && strcmp(e->d_name, best) > 0)
			snprintf(best, sizeof best, "%s", e->d_name);
	}
	closedir(d);
	if (!*best) return 0;
	snprintf(out, n, "%s/%s", dir, best);
#endif
	return 1;
}

/* A path as the player would type it, without their account name: ~/... on Linux, %APPDATA%\... on Windows. */
static inline void ce_short_path(const char *path, char *out, size_t n)
{
#ifdef _WIN32
	const char *base = getenv("APPDATA"), *name = "%APPDATA%";
#else
	const char *base = getenv("HOME"), *name = "~";
#endif
	size_t l = base ? strlen(base) : 0;
	if (l && !strncmp(path, base, l) && (path[l] == '/' || path[l] == '\\'))
		snprintf(out, n, "%s%s", name, path + l);
	else
		snprintf(out, n, "%s", path);
}

static inline void ce_url_encode(const char *in, char *out, size_t n)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; p++)
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr("-_.~", *p))
			out[o++] = (char)*p;
		else
		{
			out[o++] = '%';
			out[o++] = hex[*p >> 4];
			out[o++] = hex[*p & 15];
		}
	out[o] = 0;
}

/* The pre-filled issue page (bug_report.md's sections), naming the version, the build, the OS and the files to attach. */
static inline void ce_report_url(const char *version, const char *build, const char *user, char *out, size_t n)
{
	char os[256], log[4096], tape[4096], shortlog[4096], shorttape[4096], body[8192], title[256];
	char ebody[3 * sizeof body], etitle[3 * sizeof title];
	ce_os_name(os, sizeof os);
	snprintf(log, sizeof log, "%s%sonslaught-ce.log", user,
#ifdef _WIN32
			 "\\"
#else
			 "/"
#endif
	);
	ce_short_path(log, shortlog, sizeof shortlog);
	int haveTape = ce_latest_tape(user, tape, sizeof tape);
	if (haveTape) ce_short_path(tape, shorttape, sizeof shorttape);
	snprintf(body, sizeof body,
			 "**What happened**\n\n\n\n"
			 "**Steps to reproduce**\n\n1. \n\n"
			 "**Files**\n\n"
			 "Please attach the log: drag `%s` into this box (`onslaught-ce.old.log` beside it is the run before).\n"
			 "%s%s%s\n"
			 "**System**\n\n"
			 "- Onslaught CE v%s" CE_VERSION_NOTE ", build %s\n"
			 "- %s\n",
			 shortlog, haveTape ? "Optional, if you recorded the session: the tape `" : "",
			 haveTape ? shorttape : "", haveTape ? "` and its `.json` (it lets us replay exactly what happened).\n" : "",
			 version, build && *build ? build : "unknown", os);
	snprintf(title, sizeof title, "Problem: (describe it in a few words) [v%s]", version);
	ce_url_encode(body, ebody, sizeof ebody);
	ce_url_encode(title, etitle, sizeof etitle);
	snprintf(out, n, CE_ISSUES "?template=bug_report.md&title=%s&body=%s", etitle, ebody);
}

/* Opens the page in the browser (unless BEA_REPORT_NO_OPEN is set: tests); 1 when the browser was asked to. */
static inline int ce_open_report(const char *version, const char *build, const char *user, char *url, size_t n)
{
	ce_report_url(version, build, user, url, n);
	const char *no = getenv("BEA_REPORT_NO_OPEN");
	if (no && *no && strcmp(no, "0")) return 0;
	return SDL_OpenURL(url) ? 1 : 0;
}

/* Sets key=value in the key=value file ini (ce.ini), replacing the key's line or adding one and keeping every other line
 * as it is. The whole file is written to ini.tmp, every write and the close are checked (and on Linux it is synced),
 * and only then is it renamed over ini, so a crash, a power cut or a full disk leaves the old ce.ini whole. A ce.ini
 * that exists but cannot be read is left alone. Returns 1 when ini now holds the line, 0 when nothing was changed. */
static inline int ce_ini_write(const char *ini, const char *key, const char *value)
{
	char *t = NULL;
	size_t len = 0;
	FILE *f = fopen(ini, "rb");
	if (!f && errno != ENOENT) return 0;
	if (f)
	{
		size_t cap = 0, got;
		char buf[4096];
		int bad = 0;
		while ((got = fread(buf, 1, sizeof buf, f)) > 0)
		{
			if (len + got + 1 > cap || !t)
			{
				char *m = (char *)realloc(t, cap = (len + got + 1) * 2);
				if (!m) { bad = 1; break; }
				t = m;
			}
			memcpy(t + len, buf, got);
			len += got;
			t[len] = 0;
			if (len > (1u << 20)) { bad = 1; break; }	/* not a ce.ini */
		}
		if (ferror(f)) bad = 1;
		fclose(f);
		if (bad) { free(t); return 0; }
	}
	char tmp[4200];
	if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", ini) >= sizeof tmp) { free(t); return 0; }
	FILE *o = fopen(tmp, "wb");
	if (!o) { free(t); return 0; }
	size_t k = strlen(key);
	int ok = 1, written = 0;
	for (char *l = t; l && *l;)
	{
		char *nl = strchr(l, '\n');
		size_t n = nl ? (size_t)(nl - l + 1) : strlen(l);
		if (!strncmp(l, key, k) && l[k] == '=')
		{
			if (!written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
			written = 1;
		}
		else if (fwrite(l, 1, n, o) != n || (!nl && fputc('\n', o) == EOF))
			ok = 0;
		l += n;
	}
	if (!written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
	if (fflush(o) != 0 || ferror(o)) ok = 0;
#ifndef _WIN32
	if (ok && fsync(fileno(o)) != 0) ok = 0;
#endif
	if (fclose(o) != 0) ok = 0;
	free(t);
#ifdef _WIN32
	if (ok && !MoveFileExA(tmp, ini, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = 0;
#else
	if (ok && rename(tmp, ini) != 0) ok = 0;
#endif
	if (!ok) remove(tmp);
	return ok;
}

#endif
