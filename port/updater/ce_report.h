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
/* After the version wherever it is shown; empty since 0.1.8 ("Onslaught CE V0.1.8", as the menu footer, patch 0074). */
#ifndef CE_VERSION_NOTE
#define CE_VERSION_NOTE ""
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
			 "Please attach the logs that exist: `%s`, plus `onslaught-ce.old.log`, `onslaught-ce.concurrent-1.log` and "
			 "`onslaught-ce.concurrent-2.log` from the same folder. Drag them into this box.\n"
			 "%s%s%s\n"
			 "**System**\n\n"
			 "- Onslaught CE V%s" CE_VERSION_NOTE ", build %s\n"
			 "- %s\n",
			 shortlog, haveTape ? "Optional, if you recorded the session: the tape `" : "",
			 haveTape ? shorttape : "", haveTape ? "` and its `.json` (it can help us reproduce what happened).\n" : "",
			 version, build && *build ? build : "unknown", os);
	snprintf(title, sizeof title, "Problem: (describe it in a few words) [V%s]", version);
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

/* Opens the settings directory. OS/user ancestor folders above the target directory are canonicalized once so relocated
 * homes (/home -> /var/home, ~/.local/share, OneDrive Known Folder Move, junctioned C:\Users) work, while the target
 * directory itself and any created/traversed components below that ancestor refuse symlinks and reparse points.
 * With create == 0, a failed open also says in *absent (when given) whether the path simply does not exist yet: some
 * component is missing and nothing on the way is a file, a dangling/target link, a reparse point or otherwise refused.
 * The settings reader takes that as an empty file (the first write, create == 1, makes the folders); any other refusal
 * stays a failed read. */
static inline int ce_settings_dir_open_ex(const char *path, int create, CeSettingsDir *dir, int *absent)
{
	if (absent) *absent = 0;
#ifdef _WIN32
	dir->count = 0;
	if (!path || !*path) return 0;
	for (const char *p = path; *p;)
	{
		while (*p == '/' || *p == '\\') p++;
		const char *s = p;
		while (*p && *p != '/' && *p != '\\') p++;
		if (p - s == 2 && s[0] == '.' && s[1] == '.') return 0;
	}
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
	while (n > root && dir->path[n - 1] == '\\') dir->path[--n] = 0;
	size_t last_sep = n;
	while (last_sep > root && dir->path[last_sep - 1] != '\\') last_sep--;
	size_t anc_end = last_sep > root ? last_sep - 1 : root;
	HANDLE anc = INVALID_HANDLE_VALUE;
	DWORD anc_err = 0;
	for (;;)
	{
		char saved = dir->path[anc_end];
		dir->path[anc_end] = 0;
		anc = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		anc_err = anc == INVALID_HANDLE_VALUE ? GetLastError() : 0;
		if (anc != INVALID_HANDLE_VALUE)
		{
			dir->path[anc_end] = saved;
			break;
		}
		int anc_gone = anc_err == ERROR_FILE_NOT_FOUND || anc_err == ERROR_PATH_NOT_FOUND;
		if (anc_gone)
		{
			HANDLE probe = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			if (probe != INVALID_HANDLE_VALUE)
			{
				CloseHandle(probe);
				dir->path[anc_end] = saved;
				return 0;
			}
		}
		dir->path[anc_end] = saved;
		if (!anc_gone || anc_end <= root)
		{
			if (absent && !create && anc_gone) *absent = 1;
			return 0;
		}
		while (anc_end > root && dir->path[anc_end - 1] != '\\') anc_end--;
		if (anc_end > root) anc_end--;
	}
	BY_HANDLE_FILE_INFORMATION ainfo;
	if (!GetFileInformationByHandle(anc, &ainfo) || !(ainfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		CloseHandle(anc);
		return 0;
	}
	char canon[4200];
	DWORD clen = GetFinalPathNameByHandleA(anc, canon, sizeof canon, FILE_NAME_NORMALIZED);
	if (!clen || clen >= sizeof canon)
	{
		CloseHandle(anc);
		return 0;
	}
	const char *csrc = canon;
	char cbuf[4200];
	if (!strncmp(csrc, "\\\\?\\UNC\\", 8))
	{
		if ((size_t)snprintf(cbuf, sizeof cbuf, "\\\\%s", csrc + 8) >= sizeof cbuf) { CloseHandle(anc); return 0; }
		csrc = cbuf;
	}
	else if (!strncmp(csrc, "\\\\?\\", 4))
		csrc += 4;
	size_t csrc_len = strlen(csrc);
	while (csrc_len > 3 && csrc[csrc_len - 1] == '\\') csrc_len--;
	int need_sep = csrc_len > 0 && csrc[csrc_len - 1] != '\\';
	char tail[4200];
	const char *rest = dir->path + anc_end;
	while (*rest == '\\') rest++;
	if (*rest)
	{
		if ((size_t)snprintf(tail, sizeof tail, "%.*s%s%s", (int)csrc_len, csrc, need_sep ? "\\" : "", rest) >= sizeof tail)
		{
			CloseHandle(anc);
			return 0;
		}
	}
	else
	{
		if ((size_t)snprintf(tail, sizeof tail, "%.*s", (int)csrc_len, csrc) >= sizeof tail)
		{
			CloseHandle(anc);
			return 0;
		}
	}
	strcpy(dir->path, tail);
	dir->handles[dir->count++] = anc;
	if (!*rest) return 1;
	size_t end = csrc_len;
	while (dir->path[end] == '\\') end++;
	while (dir->path[end] && dir->path[end] != '\\') end++;
	for (;;)
	{
		char saved = dir->path[end];
		dir->path[end] = 0;
		HANDLE h = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
		int gone = h == INVALID_HANDLE_VALUE && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
		if (gone && create)
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
			if (absent && !create && gone) *absent = 1;
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
	for (const char *p = copy; *p;)
	{
		while (*p == '/') p++;
		const char *s = p;
		while (*p && *p != '/') p++;
		if (p - s == 2 && s[0] == '.' && s[1] == '.') return 0;
	}
	size_t len = strlen(copy);
	while (len > 1 && copy[len - 1] == '/') copy[--len] = 0;
	/* Traversal needs search permission, not directory-list permission. O_PATH is Linux's stable 010000000 flag;
	 * some C feature profiles hide its spelling, while the kernel and the glibc-2.29-compatible *at calls support it. */
	int leaf_flags = O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#ifdef O_PATH
	leaf_flags |= O_PATH;
#else
	leaf_flags |= 010000000;
#endif
	int anc_flags = leaf_flags & ~O_NOFOLLOW;
	if (len == 1 && copy[0] == '/')
	{
		dir->fd = open("/", anc_flags);
		return dir->fd >= 0 ? 1 : 0;
	}
	size_t anc_end = len;
	while (anc_end > 0 && copy[anc_end - 1] != '/') anc_end--;
	int fd = -1;
	const char *tail = NULL;
	if (anc_end == 0)
	{
		fd = open(".", anc_flags);
		if (fd < 0) return 0;
		tail = copy;
	}
	else
	{
		size_t cur = anc_end > 1 ? anc_end - 1 : 1;
		for (;;)
		{
			char saved = copy[cur];
			copy[cur] = 0;
			fd = open(copy, anc_flags);
			int why = fd < 0 ? errno : 0;
			if (fd >= 0)
			{
				copy[cur] = saved;
				tail = copy + cur;
				break;
			}
			if (why == ENOENT)
			{
				struct stat lst;
				if (lstat(copy, &lst) == 0 || errno != ENOENT)
				{
					copy[cur] = saved;
					return 0;
				}
			}
			copy[cur] = saved;
			if (why != ENOENT) return 0;
			if (cur <= 1)
			{
				if (copy[0] == '/')
				{
					if (absent && !create) *absent = 1;
					return 0;
				}
				fd = open(".", anc_flags);
				if (fd < 0) return 0;
				tail = copy;
				break;
			}
			while (cur > 0 && copy[cur - 1] != '/') cur--;
			if (cur == 0)
			{
				fd = open(".", anc_flags);
				if (fd < 0) return 0;
				tail = copy;
				break;
			}
			if (cur > 1) cur--;
		}
	}
	char tail_copy[4200];
	if ((size_t)snprintf(tail_copy, sizeof tail_copy, "%s", tail) >= sizeof tail_copy) { close(fd); return 0; }
	for (char *part = tail_copy; *part;)
	{
		while (*part == '/') part++;
		if (!*part) break;
		char *end = strchr(part, '/');
		if (end) *end = 0;
		if (strcmp(part, "."))
		{
			int next = openat(fd, part, leaf_flags), why = next < 0 ? errno : 0;
			if (next < 0 && why == ENOENT && create && (mkdirat(fd, part, 0700) == 0 || errno == EEXIST))
				next = openat(fd, part, leaf_flags);
			close(fd);
			if (next < 0)
			{
				if (absent && !create && why == ENOENT) *absent = 1;
				return 0;
			}
			fd = next;
		}
		if (!end) break;
		part = end + 1;
	}
	dir->fd = fd;
	return 1;
#endif
}

static inline int ce_settings_dir_open(const char *path, int create, CeSettingsDir *dir)
{
	return ce_settings_dir_open_ex(path, create, dir, NULL);
}

/* Opens a single child folder (sub) inside the CE-owned user directory (root). OS/user ancestors above root are
 * canonicalized by ce_settings_dir_open_ex, while BOTH root itself and sub refuse symlinks and reparse points. */
static inline int ce_settings_subdir_open_ex(const char *root, const char *sub, int create, CeSettingsDir *dir, int *absent)
{
	if (absent) *absent = 0;
	if (!root || !*root || !sub || !*sub || !strcmp(sub, ".") || !strcmp(sub, "..")) return 0;
	for (const char *p = sub; *p; p++)
		if (*p == '/' || *p == '\\') return 0;
	if (!ce_settings_dir_open_ex(root, create, dir, absent)) return 0;
#ifdef _WIN32
	size_t base = strlen(dir->path);
	int need_sep = base > 0 && dir->path[base - 1] != '\\';
	if (base + (size_t)need_sep + strlen(sub) >= sizeof dir->path)
	{
		ce_settings_dir_close(dir);
		return 0;
	}
	if (need_sep) dir->path[base++] = '\\';
	strcpy(dir->path + base, sub);
	HANDLE h = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	DWORD error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	int gone = h == INVALID_HANDLE_VALUE && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
	if (gone && create)
	{
		if (CreateDirectoryA(dir->path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
			h = CreateFileA(dir->path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	}
	BY_HANDLE_FILE_INFORMATION info;
	if (h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h, &info) ||
		!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
		dir->count == (int)(sizeof dir->handles / sizeof dir->handles[0]))
	{
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
		ce_settings_dir_close(dir);
		if (absent && !create && gone) *absent = 1;
		return 0;
	}
	dir->handles[dir->count++] = h;
	return 1;
#else
	int leaf_flags = O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
#ifdef O_PATH
	leaf_flags |= O_PATH;
#else
	leaf_flags |= 010000000;
#endif
	int next = openat(dir->fd, sub, leaf_flags), why = next < 0 ? errno : 0;
	if (next < 0 && why == ENOENT && create && (mkdirat(dir->fd, sub, 0700) == 0 || errno == EEXIST))
		next = openat(dir->fd, sub, leaf_flags);
	close(dir->fd);
	if (next < 0)
	{
		dir->fd = -1;
		if (absent && !create && why == ENOENT) *absent = 1;
		return 0;
	}
	dir->fd = next;
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

/* The header of the block that lists the unfinished features (ce_ini_add_section). Every reader of ce.ini takes lines that
 * set "key=" (optional spaces or tabs before key or "="), so this header and the ";" comment lines under it are skipped as
 * they always were. A key that is not in the file yet is added above the header, so the block stays at the end and the keys
 * under it are its own. */
#define CE_INI_EXPERIMENTAL_HEADER "[Experimental]"

/* The largest ce.ini the game's readers (ce.cpp) accept; they refuse a longer file and use their defaults. The writer never
 * produces a longer one. */
#define CE_INI_MAX_BYTES (1u << 20)

/* Whether the line at l (n bytes, without its newline) is exactly header, a final CR and trailing blanks aside. */
static inline int ce_ini_is_header(const char *l, size_t n, const char *header)
{
	size_t h = strlen(header);
	while (n > 0 && (l[n - 1] == '\r' || l[n - 1] == ' ' || l[n - 1] == '\t')) n--;
	return n == h && !memcmp(l, header, h);
}

/* Where the first line equal to header starts in t (len bytes), or len when there is none. */
static inline size_t ce_ini_find_header(const char *t, size_t len, const char *header)
{
	for (size_t at = 0; at < len;)
	{
		const char *nl = (const char *)memchr(t + at, '\n', len - at);
		size_t n = nl ? (size_t)(nl - (t + at)) : len - at;
		if (ce_ini_is_header(t + at, n, header)) return at;
		at += n + (nl ? 1 : 0);
	}
	return len;
}

/* Where a key that is not in the file yet goes when the file has the header at header_at: above the header, and above the one blank line
 * that sets the header apart from the lines before it, so the block keeps its blank line. header_at when there is no such line. */
static inline size_t ce_ini_insert_before(const char *t, size_t header_at)
{
	if (header_at >= 1 && t[header_at - 1] == '\n')
	{
		if (header_at == 1 || t[header_at - 2] == '\n') return header_at - 1;
		if (t[header_at - 2] == '\r' && (header_at == 2 || t[header_at - 3] == '\n')) return header_at - 2;
	}
	return header_at;
}

/* Whether the line at line (len bytes, without its newline) sets key (key_len bytes): optional leading spaces or tabs,
 * exact case-sensitive key, optional spaces or tabs, then '='. When eq_pos is given, *eq_pos receives the index of '='. */
static inline int ce_ini_match_key(const char *line, size_t len, const char *key, size_t key_len, size_t *eq_pos)
{
	if (!key || !key_len) return 0;
	size_t p = 0;
	while (p < len && (line[p] == ' ' || line[p] == '\t')) ++p;
	if (len - p <= key_len || memcmp(line + p, key, key_len) != 0) return 0;
	p += key_len;
	while (p < len && (line[p] == ' ' || line[p] == '\t')) ++p;
	if (p >= len || line[p] != '=') return 0;
	if (eq_pos) *eq_pos = p;
	return 1;
}

/* Whether some line of t sets key (optional spaces or tabs before key or "="). */
static inline int ce_ini_has_key(const char *t, size_t len, const char *key)
{
	size_t k = strlen(key);
	for (size_t at = 0; at < len;)
	{
		const char *nl = (const char *)memchr(t + at, '\n', len - at);
		size_t n = nl ? (size_t)(nl - (t + at)) : len - at;
		if (ce_ini_match_key(t + at, n, key, k, NULL)) return 1;
		at += n + (nl ? 1 : 0);
	}
	return 0;
}

/* Sets key=value, or (key NULL, section given) adds a block of settings, keeping unrelated bytes (including non-text bytes)
 * with the baseline's final-newline normalization. A new, exclusive temporary file is flushed and closed before
 * same-directory replacement. Existing unreadable, non-regular or symlink/reparse-point settings files are refused. Failed
 * reads/writes leave the original intact. The caller owns admission of the selected user directory; this helper does not
 * know the game's install directory.
 * A key that is not in the file is added above CE_INI_EXPERIMENTAL_HEADER (and the blank line before it) when the file has that line, else at the end.
 * The block (section_header, then the comment text section_note, whole lines each starting with ";", then each of
 * section_keys as key=0 unless the file already has a line for it somewhere) is written only when the file has no line equal
 * to section_header; it never changes a line that is there, and a file that already has it is not written at all (it keeps its
 * inode, its mode and its bytes, and a read-only file with the block is a success).
 * A write replaces the file with one that has the same permission bits (a new file is private to its owner), and refuses a file its
 * owner cannot write (no owner write bit; on Windows the read-only attribute): the player made it read-only, so it is left as it is
 * and the call returns 0. The result is never longer than CE_INI_MAX_BYTES, the size every reader of ce.ini accepts: a write that
 * would grow the file past it is refused and the file is left as it is.
 * After a call that returned 0, ce_ini_refusal says why, for a caller that tells the player: CE_INI_REFUSED_READONLY for the read-only
 * file above (the one refusal a player causes on purpose and can undo), CE_INI_REFUSED_OTHER for everything else. It is 0 after a call
 * that returned 1. Each program that includes this file has its own copy. */
enum { CE_INI_REFUSED_NONE = 0, CE_INI_REFUSED_READONLY = 1, CE_INI_REFUSED_OTHER = 2 };
static int ce_ini_refusal;

static inline int ce_ini_edit_file(const char *ini, const char *key, const char *value, const char *section_header,
	const char *section_note, const char *const *section_keys, int section_count);

/* Explicit refusal reporting via optional *refusal_out so callers across translation units never read a stale TU-local static. */
static inline int ce_ini_edit_ex(const char *ini, const char *key, const char *value, const char *section_header,
	const char *section_note, const char *const *section_keys, int section_count, int *refusal_out)
{
	ce_ini_refusal = CE_INI_REFUSED_OTHER;	/* until the edit says otherwise: the read-only refusal names itself */
	int ok = ce_ini_edit_file(ini, key, value, section_header, section_note, section_keys, section_count);
	if (ok) ce_ini_refusal = CE_INI_REFUSED_NONE;
	if (refusal_out) *refusal_out = ce_ini_refusal;
	return ok;
}

static inline int ce_ini_edit(const char *ini, const char *key, const char *value, const char *section_header,
	const char *section_note, const char *const *section_keys, int section_count)
{
	return ce_ini_edit_ex(ini, key, value, section_header, section_note, section_keys, section_count, NULL);
}

static inline int ce_ini_edit_file(const char *ini, const char *key, const char *value, const char *section_header,
	const char *section_note, const char *const *section_keys, int section_count)
{
	char parent[4200], name[256];
	if (!ini || (size_t)snprintf(parent, sizeof parent, "%s", ini) >= sizeof parent) return 0;
	if (key ? (!*key || !value) : (!section_header || !*section_header || section_count < 0 || (section_count && !section_keys))) return 0;
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
	int input = -1, absent = 0, readonly = 0;
#ifndef _WIN32
	unsigned keep_mode = 0600;	/* the permission bits the replacement gets: the old file's, else private */
#endif
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir.path, name);
	HANDLE in = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (in == INVALID_HANDLE_VALUE) absent = GetLastError() == ERROR_FILE_NOT_FOUND;
	else
	{
		BY_HANDLE_FILE_INFORMATION info;
		if (GetFileType(in) == FILE_TYPE_DISK && GetFileInformationByHandle(in, &info) &&
			!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
		{
			readonly = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
			input = _open_osfhandle((intptr_t)in, _O_RDONLY | _O_BINARY);
		}
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
		else
		{
			keep_mode = (unsigned)(st.st_mode & 0777);
			readonly = !(st.st_mode & S_IWUSR);
		}
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
			if (len > CE_INI_MAX_BYTES) { bad = 1; break; }	/* not a ce.ini */
		}
		if (ferror(f)) bad = 1;
		if (fclose(f) != 0) bad = 1;
		if (bad) { free(t); ce_settings_dir_close(&dir); return 0; }
		/* Strip a leading UTF-8 BOM (Notepad-saved ce.ini) so line 1 is never shadowed. */
		if (t && len >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF)
		{
			len -= 3;
			memmove(t, t + 3, len + 1);
		}
	}
	/* The block is only ever added: a file that has its header needs no write, so it is left whole (and keeps its inode and mode). */
	if (!key && t && ce_ini_find_header(t, len, section_header) < len) { free(t); ce_settings_dir_close(&dir); return 1; }
	/* A write replaces the file, which a read-only file's owner has asked us not to do. */
	if (readonly) { free(t); ce_settings_dir_close(&dir); ce_ini_refusal = CE_INI_REFUSED_READONLY; return 0; }
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
	size_t k = key ? strlen(key) : 0;
	int ok = o != NULL, written = key ? 0 : 1;
	size_t header_at = key && t ? ce_ini_find_header(t, len, CE_INI_EXPERIMENTAL_HEADER) : len;
	if (header_at < len) header_at = ce_ini_insert_before(t, header_at);
	int has_key = key && t ? ce_ini_has_key(t, len, key) : 0;
	for (size_t at = 0; ok && at < len;)
	{
		char *l = t + at;
		char *nl = (char *)memchr(l, '\n', len - at);
		size_t line_len = nl ? (size_t)(nl - l) : len - at;
		size_t n = line_len + (nl ? 1 : 0);
		if (key && !has_key && !written && at == header_at && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
		if (key && !has_key && at == header_at) written = 1;
		if (key && ce_ini_match_key(l, line_len, key, k, NULL))
		{
			if (!written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
			written = 1;
		}
		else if (fwrite(l, 1, n, o) != n || (!nl && fputc('\n', o) == EOF))
			ok = 0;
		at += n;
	}
	if (ok && !written && fprintf(o, "%s=%s\n", key, value) < 0) ok = 0;
	if (ok && !key)
	{
		const char *section_default = (value && *value) ? value : "0";
		if (len > 0 && fputc('\n', o) == EOF) ok = 0;
		if (ok && fprintf(o, "%s\n%s", section_header, section_note ? section_note : "") < 0) ok = 0;
		for (int i = 0; ok && i < section_count; i++)
			if ((!t || !ce_ini_has_key(t, len, section_keys[i])) && fprintf(o, "%s=%s\n", section_keys[i], section_default) < 0) ok = 0;
	}
	if (o && (fflush(o) != 0 || ferror(o))) ok = 0;
	if (ok && o)
	{
		long size = ftell(o);	/* what a reader would find: more than it accepts is refused here, not written */
		if (size < 0 || (unsigned long)size > CE_INI_MAX_BYTES) ok = 0;
	}
#ifndef _WIN32
	if (ok && fchmod(output, (mode_t)keep_mode) != 0) ok = 0;
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
			int renamed = 0;
			for (int attempt = 0; attempt < 5; ++attempt)
			{
				if (SetFileInformationByHandle(output, FileRenameInfo, rename, (DWORD)size)) { renamed = 1; break; }
				DWORD rerr = GetLastError();
				if (rerr != ERROR_SHARING_VIOLATION && rerr != ERROR_ACCESS_DENIED) break;
				if (attempt + 1 < 5) Sleep(20);
			}
			if (!renamed) ok = 0;
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


/* Sets key=value in the settings file ini, as ce_ini_edit_ex describes, reporting any refusal reason in *refusal_out. */
static inline int ce_ini_write_ex(const char *ini, const char *key, const char *value, int *refusal_out)
{
	if (!key)
	{
		ce_ini_refusal = CE_INI_REFUSED_OTHER;
		if (refusal_out) *refusal_out = CE_INI_REFUSED_OTHER;
		return 0;
	}
	return ce_ini_edit_ex(ini, key, value, NULL, NULL, NULL, 0, refusal_out);
}

/* Sets key=value in the settings file ini, as ce_ini_edit describes. */
static inline int ce_ini_write(const char *ini, const char *key, const char *value)
{
	return ce_ini_write_ex(ini, key, value, NULL);
}

/* Adds a section block with an explicit default value ("0" or "1") for any missing keys, reporting any refusal reason. */
static inline int ce_ini_add_section_default_ex(const char *ini, const char *header, const char *note,
	const char *const *keys, int count, const char *default_value, int *refusal_out)
{
	return ce_ini_edit_ex(ini, NULL, default_value, header, note, keys, count, refusal_out);
}

/* Adds the block that lists the unfinished features, reporting any refusal reason in *refusal_out. */
static inline int ce_ini_add_section_ex(const char *ini, const char *header, const char *note,
	const char *const *keys, int count, int *refusal_out)
{
	return ce_ini_edit_ex(ini, NULL, NULL, header, note, keys, count, refusal_out);
}

/* Adds the block that lists the unfinished features, if ini has no line equal to header (ce_ini_edit). 1 when the file now has it. */
static inline int ce_ini_add_section(const char *ini, const char *header, const char *note, const char *const *keys, int count)
{
	return ce_ini_add_section_ex(ini, header, note, keys, count, NULL);
}

#endif
