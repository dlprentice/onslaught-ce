// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_music_scan.cpp: the folder listing behind the CE music playlist (port/game/ce_music.cpp).
// It only reads: one folder, no subfolders, at most kScanEntries names looked at and the caller's own track limit, and
// only files whose first Ogg page opens a Vorbis stream of two channels at 44100 Hz, the one format the game's music
// stream plays (PCMusicStream.cpp: HasFormat, GetChannels() == 2, GetSampleRate() == 44100). A full path must fit the
// stream's track name (255 bytes). Nothing is written, created or followed into a subfolder.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
const int kScanEntries = 4096;		// names looked at in one folder; the rest of a huge folder is not read
const size_t kTrackPath = 255;		// PCMusicStream's sTrackName: _tcsncpy(sTrackName, filename, 255)

bool OggName(const char *name)
{
	size_t n = strlen(name);
	if (n < 5 || name[0] == '.') return false;
	const char *e = name + n - 4;
	return e[0] == '.' && (e[1] | 0x20) == 'o' && (e[2] | 0x20) == 'g' && (e[3] | 0x20) == 'g';
}
}

// Whether the first bytes of a file (n of them) open an Ogg Vorbis stream the game can play: the first page's capture
// pattern with its beginning-of-stream flag, then the identification packet (type 1, "vorbis", version 0) with two
// channels at 44100 Hz.
extern "C" int CE_MusicHeaderPlayable(const unsigned char *b, size_t n)
{
	if (n < 27 || memcmp(b, "OggS", 4) || b[4] != 0 || !(b[5] & 0x02)) return 0;
	size_t packet = 27 + (size_t)b[26];
	if (n < packet + 16) return 0;
	const unsigned char *p = b + packet;
	if (p[0] != 1 || memcmp(p + 1, "vorbis", 6) || p[7] || p[8] || p[9] || p[10]) return 0;
	unsigned rate = (unsigned)p[12] | ((unsigned)p[13] << 8) | ((unsigned)p[14] << 16) | ((unsigned)p[15] << 24);
	return p[11] == 2 && rate == 44100;
}

static int Playable(const char *path)
{
	unsigned char head[300];
	size_t got = 0;
#ifdef _WIN32
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD read = 0;
	if (GetFileType(h) == FILE_TYPE_DISK && ReadFile(h, head, sizeof(head), &read, NULL)) got = read;
	CloseHandle(h);
#else
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return 0;
	struct stat st;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
	{
		ssize_t r = read(fd, head, sizeof(head));
		got = r > 0 ? (size_t)r : 0;
	}
	close(fd);
#endif
	return CE_MusicHeaderPlayable(head, got);
}

// Lists dir's playable .ogg files, calling add(path) for each until it returns 0 (the caller's list is full).
// Returns the tracks added, or -1 when dir is not an absolute folder that can be listed. *skipped counts .ogg files
// left out (not playable, or a path too long); *truncated is 1 when the folder had more names than were looked at.
extern "C" int CE_MusicScan(const char *dir, int (*add)(const char *path, void *ctx), void *ctx, int *skipped,
							int *truncated)
{
	if (skipped) *skipped = 0;
	if (truncated) *truncated = 0;
	if (!dir || !*dir || !add) return -1;
	char base[1024];
	size_t n = strlen(dir);
	if (n >= sizeof(base)) return -1;
	memcpy(base, dir, n + 1);
#ifdef _WIN32
	const char sep = '\\';
	bool absolute = (((base[0] | 0x20) >= 'a' && (base[0] | 0x20) <= 'z') && base[1] == ':' &&
					 (base[2] == '\\' || base[2] == '/')) || (base[0] == '\\' && base[1] == '\\');
	for (char *c = base; *c; ++c) if (*c == '/') *c = '\\';
	while (n > 3 && base[n - 1] == '\\') base[--n] = 0;
#else
	const char sep = '/';
	bool absolute = base[0] == '/';
	while (n > 1 && base[n - 1] == '/') base[--n] = 0;
#endif
	if (!absolute) return -1;
	const bool rootSep = base[n - 1] == sep;
	int added = 0, looked = 0;
	char path[1300];
#ifdef _WIN32
	char spec[1100];
	snprintf(spec, sizeof(spec), "%s%s*", base, rootSep ? "" : "\\");
	WIN32_FIND_DATAA find;
	HANDLE h = FindFirstFileA(spec, &find);
	if (h == INVALID_HANDLE_VALUE) return -1;
	do
	{
		if (++looked > kScanEntries) { if (truncated) *truncated = 1; break; }
		if (find.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
		const char *name = find.cFileName;
#else
	DIR *d = opendir(base);
	if (!d) return -1;
	for (struct dirent *e; (e = readdir(d));)
	{
		if (++looked > kScanEntries) { if (truncated) *truncated = 1; break; }
		const char *name = e->d_name;
#endif
		if (!OggName(name)) continue;
		int len = snprintf(path, sizeof(path), "%s%s%s", base, rootSep ? "" : (sep == '/' ? "/" : "\\"), name);
		if (len < 0 || (size_t)len > kTrackPath || !Playable(path))
		{
			if (skipped) ++*skipped;
			continue;
		}
		if (!add(path, ctx)) break;
		++added;
#ifdef _WIN32
	} while (FindNextFileA(h, &find));
	FindClose(h);
#else
	}
	closedir(d);
#endif
	return added;
}

// A seed for the playlist's own shuffle (never the game's rand()): the wall clock, mixed.
extern "C" unsigned CE_MusicSeed(void)
{
	unsigned s = (unsigned)time(NULL) ^ (unsigned)clock() * 2654435761u;
	return s ? s : 0x9e3779b9u;
}
