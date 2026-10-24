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
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
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
			 "**Does the original game do the same?**\n\nYes, no, or not checked.\n\n"
			 "**How did you run it?**\n\nNative Linux, native Windows, or under Wine or Proton (which version?).\n\n"
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

/* The settings directory: no symlink/reparse-point component is followed. Linux keeps the final directory fd for
 * openat/renameat; Windows keeps each ancestor open without delete sharing while using the checked absolute path. */
typedef struct CeSettingsDir
{
#ifdef _WIN32
	HANDLE handles[128];
	int count;
	char path[4200];
#else
	int fd;
#endif
} CeSettingsDir;

static inline void ce_settings_dir_close(CeSettingsDir *dir)
{
#ifdef _WIN32
	while (dir->count) CloseHandle(dir->handles[--dir->count]);
#else
	if (dir->fd >= 0) close(dir->fd);
	dir->fd = -1;
#endif
}

static inline int ce_settings_dir_open(const char *path, int create, CeSettingsDir *dir)
{
#ifdef _WIN32
	dir->count = 0;
	DWORD n = GetFullPathNameA(path, sizeof dir->path, dir->path, NULL);
	if (!n || n >= sizeof dir->path) return 0;
	for (char *p = dir->path; *p; p++) if (*p == '/') *p = '\\';
	size_t root = 0;
	if (n >= 3 && dir->path[1] == ':' && dir->path[2] == '\\') root = 3;
	else if (n > 2 && dir->path[0] == '\\' && dir->path[1] == '\\' && dir->path[2] != '?' && dir->path[2] != '.')
	{
		char *server = strchr(dir->path + 2, '\\');
		char *share = server ? strchr(server + 1, '\\') : NULL;
		if (server && server[1]) root = share ? (size_t)(share - dir->path) : n;
	}
	if (!root) return 0; /* Device namespaces are not settings directories. */
	for (size_t end = root;;)
	{
		char saved = dir->path[end];
		dir->path[end] = 0;
		HANDLE h = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
		if (h == INVALID_HANDLE_VALUE && create && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
		{
			if (CreateDirectoryA(dir->path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
				h = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		}
		dir->path[end] = saved;
		BY_HANDLE_FILE_INFORMATION info;
		if (h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h, &info) ||
			!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
			dir->count == (int)(sizeof dir->handles / sizeof dir->handles[0]))
		{
			if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
			ce_settings_dir_close(dir);
			return 0;
		}
		dir->handles[dir->count++] = h;
		while (dir->path[end] == '\\') end++;
		if (!dir->path[end]) return 1;
		while (dir->path[end] && dir->path[end] != '\\') end++;
	}
#else
	dir->fd = -1;
	char copy[4200];
	if (!path || !*path || (size_t)snprintf(copy, sizeof copy, "%s", path) >= sizeof copy) return 0;
	/* Traversal needs search permission, not directory-list permission. O_PATH is Linux's stable 010000000 flag;
	 * some C feature profiles hide its spelling, while the kernel and the glibc-2.29-compatible *at calls support it. */
	int flags = O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#ifdef O_PATH
	flags |= O_PATH;
#else
	flags |= 010000000;
#endif
	int fd = open(path[0] == '/' ? "/" : ".", flags);
	if (fd < 0) return 0;
	for (char *part = copy; *part;)
	{
		while (*part == '/') part++;
		if (!*part) break;
		char *end = strchr(part, '/');
		if (end) *end = 0;
		if (!strcmp(part, "..")) { close(fd); return 0; }
		if (strcmp(part, "."))
		{
			int next = openat(fd, part, flags);
			if (next < 0 && errno == ENOENT && create && (mkdirat(fd, part, 0700) == 0 || errno == EEXIST))
				next = openat(fd, part, flags);
			close(fd);
			if (next < 0) return 0;
			fd = next;
		}
		if (!end) break;
		part = end + 1;
	}
	dir->fd = fd;
	return 1;
#endif
}

static inline int ce_make_settings_dirs(const char *path)
{
	CeSettingsDir dir;
	if (!ce_settings_dir_open(path, 1, &dir)) return 0;
	ce_settings_dir_close(&dir);
	return 1;
}

/* Sets key=value, keeping unrelated bytes (including non-text bytes) with the baseline's final-newline normalization.
 * A new, exclusive temporary file is flushed and closed before same-directory replacement. Existing unreadable,
 * non-regular or symlink/reparse-point settings files are refused. Failed reads/writes leave the original intact.
 * The caller owns admission of the selected user directory; this helper does not know the game's install directory. */
static inline int ce_ini_write(const char *ini, const char *key, const char *value)
{
	char parent[4200], name[256];
	if (!ini || !key || !*key || !value || (size_t)snprintf(parent, sizeof parent, "%s", ini) >= sizeof parent) return 0;
	char *slash = strrchr(parent, '/');
#ifdef _WIN32
	char *backslash = strrchr(parent, '\\');
	if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif
	const char *base = slash ? slash + 1 : parent;
	if (!*base || !strcmp(base, ".") || !strcmp(base, "..") ||
		(size_t)snprintf(name, sizeof name, "%s", base) >= sizeof name) return 0;
#ifdef _WIN32
	if (strchr(name, ':')) return 0;
#endif
	if (!slash) strcpy(parent, ".");
	else if (slash == parent) slash[1] = 0;
#ifdef _WIN32
	else if (slash == parent + 2 && parent[1] == ':') slash[1] = 0;
#endif
	else *slash = 0;
	CeSettingsDir dir;
	if (!ce_settings_dir_open(parent, 1, &dir)) return 0;
	char *t = NULL;
	size_t len = 0;
	FILE *f = NULL;
	int input = -1, absent = 0;
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir.path, name);
	HANDLE in = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (in == INVALID_HANDLE_VALUE) absent = GetLastError() == ERROR_FILE_NOT_FOUND;
	else
	{
		BY_HANDLE_FILE_INFORMATION info;
		if (GetFileType(in) == FILE_TYPE_DISK && GetFileInformationByHandle(in, &info) &&
			!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			input = _open_osfhandle((intptr_t)in, _O_RDONLY | _O_BINARY);
		if (input < 0) CloseHandle(in);
	}
	if (input >= 0) f = _fdopen(input, "rb");
#else
	input = openat(dir.fd, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
	if (input < 0) absent = errno == ENOENT;
	else
	{
		struct stat st;
		if (fstat(input, &st) != 0 || !S_ISREG(st.st_mode)) { close(input); input = -1; }
	}
	if (input >= 0) f = fdopen(input, "rb");
#endif
	if (!f && !absent)
	{
		if (input >= 0)
#ifdef _WIN32
			_close(input);
#else
			close(input);
#endif
		ce_settings_dir_close(&dir);
		return 0;
	}
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
		if (fclose(f) != 0) bad = 1;
		if (bad) { free(t); ce_settings_dir_close(&dir); return 0; }
	}
	char tmp[320];
	FILE *o = NULL;
#ifdef _WIN32
	HANDLE output = INVALID_HANDLE_VALUE;
	for (unsigned i = 0; i < 64; i++)
	{
		snprintf(tmp, sizeof tmp, "%s.tmp-%lu-%u", name, (unsigned long)GetCurrentProcessId(), i);
		snprintf(full, sizeof full, "%s\\%s", dir.path, tmp);
		output = CreateFileA(full, GENERIC_WRITE | DELETE, FILE_SHARE_READ, NULL, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (output != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
	}
	if (output != INVALID_HANDLE_VALUE)
	{
		HANDLE stream;
		if (DuplicateHandle(GetCurrentProcess(), output, GetCurrentProcess(), &stream, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			int fd = _open_osfhandle((intptr_t)stream, _O_WRONLY | _O_BINARY);
			if (fd < 0) CloseHandle(stream);
			else if (!(o = _fdopen(fd, "wb"))) _close(fd);
		}
	}
#else
	int output = -1;
	for (unsigned i = 0; i < 64; i++)
	{
		snprintf(tmp, sizeof tmp, "%s.tmp-%lu-%u", name, (unsigned long)getpid(), i);
		output = openat(dir.fd, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (output >= 0 || errno != EEXIST) break;
	}
	if (output >= 0)
	{
		int stream = dup(output);
		if (stream >= 0 && !(o = fdopen(stream, "wb"))) close(stream);
	}
#endif
	size_t k = strlen(key);
	int ok = o != NULL, written = 0;
	for (size_t at = 0; ok && at < len;)
	{
		char *l = t + at;
		char *nl = (char *)memchr(l, '\n', len - at);
		size_t n = nl ? (size_t)(nl - l + 1) : len - at;
		if (n > k && !memcmp(l, key, k) && l[k] == '=')
		{
			if (!written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
			written = 1;
		}
		else if (fwrite(l, 1, n, o) != n || (!nl && fputc('\n', o) == EOF))
			ok = 0;
		at += n;
	}
	if (ok && !written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
	if (o && (fflush(o) != 0 || ferror(o))) ok = 0;
#ifndef _WIN32
	if (ok && fsync(output) != 0) ok = 0;
#else
	if (ok && !FlushFileBuffers(output)) ok = 0;
#endif
	if (o && fclose(o) != 0) ok = 0;
	free(t);
#ifdef _WIN32
	if (ok)
	{
		WCHAR wide[4500];
		snprintf(full, sizeof full, "%s\\%s", dir.path, name);
		int n = MultiByteToWideChar(CP_ACP, 0, full, -1, wide, sizeof wide / sizeof wide[0]);
		size_t size = offsetof(FILE_RENAME_INFO, FileName) + (n > 0 ? (size_t)n * sizeof(WCHAR) : 0);
		FILE_RENAME_INFO *rename = n > 0 ? (FILE_RENAME_INFO *)calloc(1, size) : NULL;
		if (!rename) ok = 0;
		else
		{
			rename->ReplaceIfExists = TRUE;
			rename->RootDirectory = NULL;
			rename->FileNameLength = (DWORD)((n - 1) * sizeof(WCHAR));
			memcpy(rename->FileName, wide, (size_t)n * sizeof(WCHAR));
			if (!SetFileInformationByHandle(output, FileRenameInfo, rename, (DWORD)size)) ok = 0;
			free(rename);
		}
	}
	if (output != INVALID_HANDLE_VALUE)
	{
		if (!ok) { FILE_DISPOSITION_INFO remove = {TRUE}; SetFileInformationByHandle(output, FileDispositionInfo, &remove, sizeof remove); }
		CloseHandle(output);
	}
#else
	struct stat held, named;
	int same = output >= 0 && fstat(output, &held) == 0 && fstatat(dir.fd, tmp, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
		held.st_dev == named.st_dev && held.st_ino == named.st_ino;
	if (ok && (!same || renameat(dir.fd, tmp, dir.fd, name) != 0)) ok = 0;
	if (!ok && same) unlinkat(dir.fd, tmp, 0);
	if (output >= 0) close(output);
#endif
	ce_settings_dir_close(&dir);
	return ok;
}

#endif
