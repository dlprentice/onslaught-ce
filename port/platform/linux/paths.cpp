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

static bool GameDirectory(int fd)
{
	struct stat game, here;
	return stat(sGameDir, &game) == 0 && fstat(fd, &here) == 0 && game.st_dev == here.st_dev && game.st_ino == here.st_ino;
}

// Each component is opened without following links. A held directory descriptor remains our parent even if its
// pathname is replaced. Refuse the known game root before any creation beneath it, including a user root inside it.
static int WalkDirectories(int fd, const char *path, bool create)
{
	char parts[4096];
	if (snprintf(parts, sizeof(parts), "%s", path) >= (int)sizeof(parts))
	{
		close(fd);
		errno = ENAMETOOLONG;
		return -1;
	}
	char *next;
	for (char *part = strtok_r(parts, "/", &next);; part = strtok_r(NULL, "/", &next))
	{
		if (GameDirectory(fd))
		{
			close(fd);
			errno = EACCES;
			return -1;
		}
		if (!part)
			return fd;
		if (!strcmp(part, "."))
			continue;
		int child = openat(fd, part, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (child < 0 && errno == ENOENT && create)
		{
			if (mkdirat(fd, part, 0755) == 0 || errno == EEXIST)
				child = openat(fd, part, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		}
		int error = errno;
		close(fd);
		if (child < 0)
		{
			errno = error;
			return -1;
		}
		fd = child;
	}
}

static int OpenUserRoot(bool create)
{
	char absolute[4096], cwd[4096];
	const char *path = sUserDir;
	if (sUserDir[0] != '/')
	{
		if (!getcwd(cwd, sizeof(cwd)) || snprintf(absolute, sizeof(absolute), "%s/%s", cwd, sUserDir) >= (int)sizeof(absolute))
			return -1;
		path = absolute;
	}
	int fd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
	return fd < 0 ? -1 : WalkDirectories(fd, path, create);
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
	int fd = OpenUserRoot(true);
	if (fd >= 0)
		close(fd);
}

// 1: found, 0: missing, -1: requested uniqueness cannot be established. Careers need a unique case-insensitive identity:
// the game drops the suffix while listing, then rebuilds it as .bes when loading, saving or deleting. Exact spelling
// cannot choose safely between Pair.BES and Pair.bes. Other resources retain their exact-spelling fast path.
static int FindEntry(const char *dir, const char *name, char *out, size_t n, bool unique)
{
	char probe[4096];
	struct stat st;
	snprintf(probe, sizeof(probe), "%s/%s", dir, name);
	if (!unique && lstat(probe, &st) == 0)
	{
		snprintf(out, n, "%s", name);
		return 1;
	}
	DIR *d = opendir(dir);
	if (!d)
		return !unique || errno == ENOENT || errno == ENOTDIR ? 0 : -1;
	bool found = false;
	errno = 0;
	while (dirent *e = readdir(d))
		if (strcasecmp(e->d_name, name) == 0)
		{
			if (found)
			{
				closedir(d);
				return -1;
			}
			snprintf(out, n, "%s", e->d_name);
			found = true;
			if (!unique)
				break;
		}
	int error = errno;
	closedir(d);
	return unique && error ? -1 : found ? 1 : 0;
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
// Returns -1 rather than selecting one spelling when a career component is ambiguous or cannot be inspected.
static int ResolveUnder(const char *root, char parts[][256], int count, char *out, size_t n, int *missing, bool unique)
{
	snprintf(out, n, "%s", root);
	*missing = 0;
	for (int i = 0; i < count; i++)
	{
		char real[256];
		size_t len = strlen(out);
		int found = *missing == 0 ? FindEntry(out, parts[i], real, sizeof(real), unique) : 0;
		if (found < 0)
			return -1;
		if (found)
			snprintf(out + len, n - len, "/%s", real);
		else
		{
			snprintf(out + len, n - len, "/%s", parts[i]);
			(*missing)++;
		}
	}
	return 0;
}

int PortResolvePath(const char *game_path, PortPathMode mode, char *out, size_t n)
{
	if (!game_path || !*game_path)
		return -1;
	// An absolute path (/..., or a drive letter) is read as given, and never written: writes stay in the port's folder.
	bool drive = ((game_path[0] | 0x20) >= 'a' && (game_path[0] | 0x20) <= 'z') && game_path[1] == ':';
	if (game_path[0] == '/' || game_path[0] == '\\' || drive)
	{
		if (mode != PORT_PATH_READ && mode != PORT_PATH_GAME)
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
	// Only the relative career namespace needs the stricter rule; absolute POSIX reads above remain exact host paths.
	bool unique = count > 0 && strcasecmp(parts[0], "savegames") == 0;
	if (mode == PORT_PATH_GAME)
	{
		if (ResolveUnder(sGameDir, parts, count, game, sizeof(game), &game_missing, unique) != 0)
			return -1;
		snprintf(out, n, "%s", game);
		return 0;
	}
	if (ResolveUnder(sUserDir, parts, count, user, sizeof(user), &user_missing, unique) != 0)
		return -1;
	// A complete, unique writable path wins without consulting a shadowed original. An ambiguous writable path never
	// falls back. New files also check the original identity before creating a shadow copy of it.
	if (mode == PORT_PATH_SHADOW || user_missing == 0)
	{
		snprintf(out, n, "%s", user);
		return 0;
	}
	if (ResolveUnder(sGameDir, parts, count, game, sizeof(game), &game_missing, unique) != 0)
		return -1;
	if (mode == PORT_PATH_READ)
	{
		snprintf(out, n, "%s", game);
		return 0;
	}
	// Writing: allow the parent folders the game folder also has. Only the descriptor walker creates them.
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
	}
	snprintf(out, n, "%s", user);
	return 0;
}

// Resolve spelling without creating anything, then open every parent through the protected descriptor walk. The result is an
// O_PATH descriptor of the folder that holds the file (see handles.h) and `leaf` receives the file's name in it.
int PortUserParent(const char *game_path, char *leaf, size_t n, bool create)
{
	char path[4096];
	if (PortResolvePath(game_path, PORT_PATH_WRITE, path, sizeof(path)) != 0)
		return -1;
	char *rel = path + strlen(sUserDir);
	if (*rel != '/')
	{
		errno = EACCES;
		return -1;
	}
	while (*rel == '/')
		rel++;
	char *slash = strrchr(rel, '/');
	const char *name = slash ? slash + 1 : rel;
	if (!*name || snprintf(leaf, n, "%s", name) >= (int)n)
	{
		errno = ENAMETOOLONG;
		return -1;
	}
	if (slash)
		*slash = 0;
	int fd = OpenUserRoot(create);
	return fd < 0 || !slash ? fd : WalkDirectories(fd, rel, create);
}

static int CheckWritable(int fd, int flags)
{
	if (fd < 0)
		return -1;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1)
	{
		close(fd);
		errno = EACCES;
		return -1;
	}
	if ((flags & O_TRUNC) && ftruncate(fd, 0) != 0)
	{
		int error = errno;
		close(fd);
		errno = error;
		return -1;
	}
	return fd;
}

int PortOpenUserFile(const char *game_path, int flags, bool copy_original)
{
	char source[4096], shadow[4096], leaf[256];
	int in = -1;
	struct stat before;
	if (copy_original)
	{
		if (PortResolvePath(game_path, PORT_PATH_READ, source, sizeof(source)) != 0 ||
			PortResolvePath(game_path, PORT_PATH_SHADOW, shadow, sizeof(shadow)) != 0)
			return -1;
		if (strcmp(source, shadow) != 0)
		{
			in = open(source, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
			if (in < 0 && errno != ENOENT)
				return -1;
			if (in >= 0 && (fstat(in, &before) != 0 || !S_ISREG(before.st_mode)))
			{
				close(in);
				errno = EACCES;
				return -1;
			}
		}
	}
	int parent = PortUserParent(game_path, leaf, sizeof(leaf), true);
	if (parent < 0)
	{
		if (in >= 0)
			close(in);
		return -1;
	}
	if (in < 0)
	{
		// Open without truncation, reject links and non-files through the opened descriptor, then truncate.
		int fd = openat(parent, leaf, (flags & ~O_TRUNC) | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0644);
		int error = errno;
		close(parent);
		errno = error;
		return CheckWritable(fd, flags);
	}

	// Stage the complete original privately, then publish without replacing a concurrently created user file.
	static unsigned sequence;
	char temp[80];
	int out = -1;
	for (int tries = 0; tries < 16 && out < 0; tries++)
	{
		snprintf(temp, sizeof(temp), ".ce-copy-%ld-%u", (long)getpid(), __sync_add_and_fetch(&sequence, 1));
		out = openat(parent, temp, (flags & O_ACCMODE) | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (out < 0 && errno != EEXIST)
			break;
	}
	bool ok = out >= 0;
	off_t total = 0;
	char buf[65536];
	while (ok)
	{
		ssize_t got = read(in, buf, sizeof(buf));
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
		{
			ok = got == 0;
			break;
		}
		total += got;
		for (ssize_t sent = 0; sent < got;)
		{
			ssize_t put = write(out, buf + sent, got - sent);
			if (put < 0 && errno == EINTR)
				continue;
			if (put <= 0)
			{
				ok = false;
				break;
			}
			sent += put;
		}
	}
	struct stat after;
	if (ok)
		ok = fstat(in, &after) == 0 && total == before.st_size && after.st_size == before.st_size &&
			 after.st_mtim.tv_sec == before.st_mtim.tv_sec && after.st_mtim.tv_nsec == before.st_mtim.tv_nsec &&
			 after.st_ctim.tv_sec == before.st_ctim.tv_sec && after.st_ctim.tv_nsec == before.st_ctim.tv_nsec;
	if (close(in) != 0)
		ok = false;
	if (ok)
		ok = fsync(out) == 0 && lseek(out, 0, SEEK_SET) == 0 && fcntl(out, F_SETFL, flags & O_APPEND) == 0;
	if (ok)
	{
		// Link the held file, not a temporary pathname another process could replace before publication.
		char fdpath[64];
		snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", out);
		ok = linkat(AT_FDCWD, fdpath, parent, leaf, AT_SYMLINK_FOLLOW) == 0;
	}
	if (out >= 0)
	{
		struct stat owned, named;
		bool removed = fstat(out, &owned) == 0;
		if (removed && fstatat(parent, temp, &named, AT_SYMLINK_NOFOLLOW) == 0)
		{
			if (owned.st_dev == named.st_dev && owned.st_ino == named.st_ino)
				removed = unlinkat(parent, temp, 0) == 0;
		}
		else if (errno != ENOENT)
			removed = false;
		if (!removed)
		{
			// A published copy is complete, but a retained second link prevents later writes. Preserve it and report
			// failure; rolling back the destination could remove a file someone else replaced after publication.
			fprintf(stderr, "onslaught-ce: copy temporary file could not be removed; retained in the writable folder\n");
			ok = false;
		}
	}
	close(parent);
	if (!ok)
	{
		if (out >= 0)
			close(out);
		errno = EIO;
		return -1;
	}
	return out;
}

int PortDeleteUserFile(const char *game_path)
{
	char leaf[256];
	int parent = PortUserParent(game_path, leaf, sizeof(leaf), false);
	if (parent < 0)
		return -1;
	struct stat st;
	int result = fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1
		? unlinkat(parent, leaf, 0) : -1;
	close(parent);
	return result;
}

int PortMakeUserDirectory(const char *game_path)
{
	char leaf[256];
	int parent = PortUserParent(game_path, leaf, sizeof(leaf), true);
	if (parent < 0)
		return -1;
	int result = mkdirat(parent, leaf, 0755), error = errno;
	close(parent);
	errno = error;
	return result;
}
