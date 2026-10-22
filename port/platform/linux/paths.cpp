// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/paths.cpp: the game's Windows-style relative paths on Linux. Two roots: the player's game folder
// (BEA_GAME_DIR, default Steam's), read only, and the port's writable folder (BEA_USER_DIR, default
// $XDG_DATA_HOME/onslaught-ce, Onslaught CE's user folder), which shadows it. Reads take the writable copy when there is one; writes always land in the
// writable folder, copying a game file there first when it is opened for update, so the install is never modified.
// Every path component matches case-insensitively, as on Windows.
#include "handles.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char sGameDir[4096];
static char sUserDir[4096];

const char *PortGameDir() { return sGameDir; }
const char *PortUserDir() { return sUserDir; }

static void MakeDirs(const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++)
		if (*p == '/')
		{
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	mkdir(tmp, 0755);
}

void PortPathsInit()
{
	const char *game = getenv("BEA_GAME_DIR");
	const char *home = getenv("HOME");
	if (game && *game)
		snprintf(sGameDir, sizeof(sGameDir), "%s", game);
	else
		snprintf(sGameDir, sizeof(sGameDir), "%s/.local/share/Steam/steamapps/common/Battle Engine Aquila", home ? home : "");
	const char *user = getenv("BEA_USER_DIR");
	const char *xdg = getenv("XDG_DATA_HOME");
	if (user && *user)
		snprintf(sUserDir, sizeof(sUserDir), "%s", user);
	else if (xdg && *xdg)
		snprintf(sUserDir, sizeof(sUserDir), "%s/onslaught-ce", xdg);
	else
		snprintf(sUserDir, sizeof(sUserDir), "%s/.local/share/onslaught-ce", home ? home : "");
	MakeDirs(sUserDir);
}

// Finds `name` in directory `dir` ignoring case; copies the real spelling into out. Exact spelling is tried first.
static bool FindEntry(const char *dir, const char *name, char *out, size_t n)
{
	char probe[4096];
	struct stat st;
	snprintf(probe, sizeof(probe), "%s/%s", dir, name);
	if (lstat(probe, &st) == 0)
	{
		snprintf(out, n, "%s", name);
		return true;
	}
	DIR *d = opendir(dir);
	if (!d)
		return false;
	bool found = false;
	while (dirent *e = readdir(d))
		if (strcasecmp(e->d_name, name) == 0)
		{
			snprintf(out, n, "%s", e->d_name);
			found = true;
			break;
		}
	closedir(d);
	return found;
}

// Splits a game path into components: backslashes and slashes separate, "." and empty parts drop, a drive prefix drops.
static int Components(const char *game_path, char parts[][256], int max)
{
	const char *p = game_path;
	if (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':')
		p += 2;
	int count = 0;
	char cur[256];
	size_t len = 0;
	for (;; p++)
	{
		if (*p == '\\' || *p == '/' || *p == 0)
		{
			cur[len] = 0;
			if (len && strcmp(cur, ".") != 0)
			{
				if (strcmp(cur, "..") == 0)
				{
					if (count)
						count--;
				}
				else if (count < max)
					snprintf(parts[count++], 256, "%s", cur);
			}
			len = 0;
			if (*p == 0)
				break;
		}
		else if (len < sizeof(cur) - 1)
			cur[len++] = *p;
	}
	return count;
}

// Resolves components under root, ignoring case; `missing` = how many trailing components did not exist.
static void ResolveUnder(const char *root, char parts[][256], int count, char *out, size_t n, int *missing)
{
	snprintf(out, n, "%s", root);
	*missing = 0;
	for (int i = 0; i < count; i++)
	{
		char real[256];
		size_t len = strlen(out);
		if (*missing == 0 && FindEntry(out, parts[i], real, sizeof(real)))
			snprintf(out + len, n - len, "/%s", real);
		else
		{
			snprintf(out + len, n - len, "/%s", parts[i]);
			(*missing)++;
		}
	}
}

static bool Exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

int PortResolvePath(const char *game_path, PortPathMode mode, char *out, size_t n)
{
	if (!game_path || !*game_path)
		return -1;
	// An absolute path (/..., or a drive letter) is read as given, and never written: writes stay in the port's folder.
	bool drive = ((game_path[0] | 0x20) >= 'a' && (game_path[0] | 0x20) <= 'z') && game_path[1] == ':';
	if (game_path[0] == '/' || game_path[0] == '\\' || drive)
	{
		if (mode != PORT_PATH_READ)
			return -1;
		if (game_path[0] == '/')
		{
			snprintf(out, n, "%s", game_path);
			return 0;
		}
	}
	char parts[64][256];
	int count = Components(game_path, parts, 64);
	char user[4096], game[4096];
	int user_missing, game_missing;
	ResolveUnder(sUserDir, parts, count, user, sizeof(user), &user_missing);
	ResolveUnder(sGameDir, parts, count, game, sizeof(game), &game_missing);
	if (mode == PORT_PATH_READ)
	{
		snprintf(out, n, "%s", user_missing == 0 ? user : game);
		return 0;
	}
	if (mode == PORT_PATH_SHADOW)
	{
		snprintf(out, n, "%s", user);
		return 0;
	}
	// Writing: the writable folder, creating the parent folders the game folder also has (savegames\, ...).
	if (user_missing > 1)
	{
		if (game_missing > 1)
		{
			snprintf(out, n, "%s", user);
			return -1;
		}
		// Rebuild the user path with the game folder's spelling for the existing parents.
		const char *rel = game + strlen(sGameDir);
		snprintf(user, sizeof(user), "%s%s", sUserDir, rel);
		char parent[4096];
		snprintf(parent, sizeof(parent), "%s", user);
		char *slash = strrchr(parent, '/');
		if (slash)
		{
			*slash = 0;
			MakeDirs(parent);
		}
	}
	snprintf(out, n, "%s", user);
	return 0;
}

// Copies a game-folder file to the writable folder before it is opened for update.
int PortCopyOnWrite(const char *game_path, char *out, size_t n)
{
	char read[4096];
	PortResolvePath(game_path, PORT_PATH_READ, read, sizeof(read));
	if (PortResolvePath(game_path, PORT_PATH_WRITE, out, n) != 0)
		return -1;
	if (strcmp(read, out) == 0 || Exists(out) || !Exists(read))
		return 0;
	int in = open(read, O_RDONLY), dst = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (in >= 0 && dst >= 0)
	{
		char buf[65536];
		ssize_t r;
		while ((r = ::read(in, buf, sizeof(buf))) > 0)
			if (write(dst, buf, r) != r)
				break;
	}
	if (in >= 0)
		close(in);
	if (dst >= 0)
		close(dst);
	return 0;
}
