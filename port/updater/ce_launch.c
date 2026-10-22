/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_launch.c: Onslaught CE's launcher (port/updater/README.md). Finds the player's Battle Engine Aquila
 * install, checks the signed release feed for an update, and starts the game from this folder with the game folder and
 * a per-user writable folder. It never writes into the game folder.
 *
 *   onslaught-ce [--version] [--no-update-check] [--update-only] [-- GAME ARGUMENTS]
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
#include "monocypher-ed25519.h"
#include "sha256.h"

#ifndef CE_VERSION
#define CE_VERSION "dev"
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PLATFORM "linux-i686"
#define SEP "/"
#define GAME_EXE "bea"
#endif

#define PATHN 4096
static char sHere[PATHN], sUser[PATHN], sIni[PATHN], sCache[PATHN];

/* ---- small helpers ---------------------------------------------------------------------------------------------- */

static int is_file(const char *p)
{
	FILE *f = fopen(p, "rb");
	if (f) fclose(f);
	return f != NULL;
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

static void ini_set(const char *key, const char *value)
{
	char *t = read_all(sIni, NULL);
	FILE *f = fopen(sIni, "wb");
	if (!f) { free(t); return; }
	size_t k = strlen(key);
	for (char *l = t; l && *l;)
	{
		char *nl = strchr(l, '\n');
		size_t len = nl ? (size_t)(nl - l + 1) : strlen(l);
		if (!(strncmp(l, key, k) == 0 && l[k] == '=')) fwrite(l, 1, len, f);
		l += len;
	}
	fprintf(f, "%s=%s\n", key, value);
	fclose(f);
	free(t);
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
static const struct { const char *path, *sha256; } GAME_FILES[] = {
	{"data" SEP "MissionScripts" SEP "onsldef.msl", "a1c9b9383ea7f9255de207a86c0e6c1885465309d50334344fd82696a1cf36c6"},
	{"data" SEP "MissionScripts" SEP "text" SEP "text.stf", "fd318d6c2304eb8ffcfa718357c1715aadad69915f39851b19f442d8263b56ae"},
};

/* Case-insensitive path lookup on Linux (the game's own folders vary in case); Windows is already case-insensitive. */
static int find_ci(const char *dir, const char *rel, char *out, size_t n)
{
#ifdef _WIN32
	snprintf(out, n, "%s\\%s", dir, rel);
	return is_file(out);
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
	return is_file(out);
#endif
}

static int supported_install(const char *dir)
{
	char p[PATHN];
	for (size_t i = 0; i < sizeof GAME_FILES / sizeof *GAME_FILES; i++)
	{
		uint8_t got[32], want[32];
		if (!find_ci(dir, GAME_FILES[i].path, p, sizeof p) || !sha256_file(p, got) || !hex_to(GAME_FILES[i].sha256, want, 32) ||
			memcmp(got, want, 32))
			return 0;
	}
	return 1;
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

static char sPicked[PATHN];
static volatile int sPickDone;

static void SDLCALL picked(void *userdata, const char *const *files, int filter)
{
	(void)userdata; (void)filter;
	if (files && files[0]) snprintf(sPicked, sizeof sPicked, "%s", files[0]);
	sPickDone = 1;
}

static int ask_for_install(char *found, size_t n)
{
	const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder..."},
												{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
	for (;;)
	{
		const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE",
			"Onslaught CE needs your own copy of Battle Engine Aquila and could not find it in Steam's libraries.\n\n"
			"Choose the folder that holds BEA.exe (your Steam or disc install). Onslaught CE only reads it.", 2, buttons, NULL};
		int which = 0;
		if (!SDL_ShowMessageBox(&box, &which) || which != 1) return 0;
		sPickDone = 0;
		sPicked[0] = 0;
		SDL_ShowOpenFolderDialog(picked, NULL, NULL, NULL, false);
		while (!sPickDone)
		{
			SDL_PumpEvents();
			SDL_Delay(20);
		}
		if (!sPicked[0]) return 0;
		if (supported_install(sPicked)) { snprintf(found, n, "%s", sPicked); return 1; }
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Onslaught CE",
								 "That folder is not a supported Battle Engine Aquila install (its mission data did not match).", NULL);
	}
}

static int find_game(char *dir, size_t n)
{
	const char *env = getenv("BEA_GAME_DIR");
	if (env && *env && supported_install(env)) { snprintf(dir, n, "%s", env); return 1; }
	char ini[PATHN];
	if (ini_get("gamedir", ini, sizeof ini) && supported_install(ini)) { snprintf(dir, n, "%s", ini); return 1; }
	if (find_steam_install(dir, n) || ask_for_install(dir, n))
	{
		ini_set("gamedir", dir);
		return 1;
	}
	return 0;
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
			"The update could not be downloaded or did not match the signed release. Nothing was changed.", NULL);
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
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be unpacked. Nothing was changed.", NULL);
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
	if (!CreateProcessA(copy, cmd, NULL, NULL, FALSE, 0, NULL, temp, &si, &pi)) { remove_tree(unpack); return 0; }
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exit(0);
#else
	if (!swap_in(fresh))
	{
		remove_tree(unpack);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be installed. Nothing was changed.", NULL);
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
		char text[512];
		snprintf(text, sizeof text, "Onslaught CE v%s is available (you have v%s).\n\nUpdate now?", version, CE_VERSION);
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
}

static int start_game(char **args, int nargs, const char *game)
{
	char exe[PATHN];
	snprintf(exe, sizeof exe, "%s" SEP GAME_EXE, sHere);
#ifdef _WIN32
	SetEnvironmentVariableA("BEA_GAME_DIR", game);
	SetEnvironmentVariableA("BEA_USER_DIR", sUser);
	char cmd[8192];
	snprintf(cmd, sizeof cmd, "\"%s\"", exe);
	for (int i = 0; i < nargs; i++) { strcat(cmd, " "); strcat(cmd, args[i]); }
	STARTUPINFOA si = {sizeof si};
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, sUser, &si, &pi)) return 1;
	/* Wait for the game, so whatever started the launcher (Steam, a shortcut, a script) sees the game's run and exit code. */
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (int)rc;
#else
	setenv("BEA_GAME_DIR", game, 1);
	setenv("BEA_USER_DIR", sUser, 1);
	if (chdir(sUser) != 0) return 1;
	char **argv = calloc((size_t)nargs + 2, sizeof *argv);
	argv[0] = exe;
	for (int i = 0; i < nargs; i++) argv[i + 1] = args[i];
	execv(exe, argv);
	return 1;
#endif
}

int main(int argc, char **argv)
{
	int nocheck = 0, updateonly = 0, first = argc;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--version")) { printf("Onslaught CE v%s\n", CE_VERSION); return 0; }
		else if (!strcmp(argv[i], "--no-update-check")) nocheck = 1;
		else if (!strcmp(argv[i], "--update-only")) updateonly = 1;   /* check (and update), then exit: no game */
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
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", "The update could not be installed. Nothing was changed.", NULL);
			char *none[] = {argv[0], NULL};
			relaunch(1, none);
			return ok ? 0 : 1;
		}
#endif
		else { first = i; break; }
	}
	init_paths();
	tidy_old();
	if (!nocheck) check_for_update(argc, argv);
	if (updateonly) return 0;
	char game[PATHN];
	if (!find_game(game, sizeof game)) return 1;
	return start_game(argv + first, argc - first, game);
}
