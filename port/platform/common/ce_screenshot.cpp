// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_screenshot.cpp: CE photo mode's screenshot writer (port/game/ce_photo.cpp). A frame read back
// at the Present (d3d9hooks.cpp) is encoded as PNG by the pinned SDL library (SDL_SavePNG_IO, opaque RGBA) and written to
// <user folder>/screenshots/onslaught-ce-YYYYMMDD-HHMMSS.png in local time, or -2 ... -99 after it when that name is
// taken. The folder is reached as ce.ini's is (ce_settings_dir_open, port/updater/ce_report.h): no link or reparse point
// is followed, and it is made when missing. A file is only ever created new (O_EXCL, CREATE_NEW), never replaced, and
// one that could not be written whole is removed. Bounded: at most CE_SHOT_MAX_FILES entries in the folder,
// CE_SHOT_MAX_SIDE pixels either way, CE_SHOT_MAX_BYTES a file. Nothing here is game state.
#ifdef CE_VERSION

#include "../../updater/ce_report.h"
#include "../../game/ce_photo.h"

#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
// Direct3D 9 back buffer formats (d3d9types.h D3DFORMAT values).
const unsigned kA8R8G8B8 = 21, kX8R8G8B8 = 22, kR5G6B5 = 23, kX1R5G5B5 = 24, kA1R5G5B5 = 25;

SDL_PixelFormat Format(unsigned d3d)
{
	switch (d3d)
	{
	case kA8R8G8B8:		// the back buffer's alpha is not the picture's: saved opaque
	case kX8R8G8B8: return SDL_PIXELFORMAT_XRGB8888;
	case kR5G6B5: return SDL_PIXELFORMAT_RGB565;
	case kA1R5G5B5:
	case kX1R5G5B5: return SDL_PIXELFORMAT_XRGB1555;
	default: return SDL_PIXELFORMAT_UNKNOWN;
	}
}

// Entries in the folder (not "." or ".."), counted up to limit.
int Entries(CeSettingsDir *dir, int limit)
{
	int count = 0;
#ifdef _WIN32
	char pattern[4300];
	snprintf(pattern, sizeof pattern, "%s\\*", dir->path);
	WIN32_FIND_DATAA found;
	HANDLE h = FindFirstFileA(pattern, &found);
	if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : limit;
	do
		if (strcmp(found.cFileName, ".") && strcmp(found.cFileName, "..")) ++count;
	while (count < limit && FindNextFileA(h, &found));
	FindClose(h);
#else
	int fd = openat(dir->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
	if (!d)
	{
		if (fd >= 0) close(fd);
		return limit;		// unreadable: treated as full
	}
	for (struct dirent *e; count < limit && (e = readdir(d));)
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++count;
	closedir(d);
#endif
	return count;
}

// Writes data to a new file called name in the folder; CE_SHOT_SAVED, CE_SHOT_NAME when the name is taken, or
// CE_SHOT_WRITE (and nothing left behind).
int WriteNew(CeSettingsDir *dir, const char *name, const void *data, size_t size)
{
#ifdef _WIN32
	char full[4500];
	snprintf(full, sizeof full, "%s\\%s", dir->path, name);
	HANDLE h = CreateFileA(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_EXISTS ? CE_SHOT_NAME : CE_SHOT_WRITE;
	const char *at = (const char *)data;
	bool ok = true;
	for (size_t left = size; ok && left;)
	{
		DWORD chunk = left > (1u << 24) ? (1u << 24) : (DWORD)left, wrote = 0;
		ok = WriteFile(h, at, chunk, &wrote, NULL) && wrote == chunk;
		at += wrote;
		left -= wrote;
	}
	if (!CloseHandle(h)) ok = false;
	if (!ok) DeleteFileA(full);
	return ok ? CE_SHOT_SAVED : CE_SHOT_WRITE;
#else
	int fd = openat(dir->fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return errno == EEXIST ? CE_SHOT_NAME : CE_SHOT_WRITE;
	const char *at = (const char *)data;
	bool ok = true;
	for (size_t left = size; ok && left;)
	{
		ssize_t wrote = write(fd, at, left);
		if (wrote < 0 && errno == EINTR) continue;
		ok = wrote > 0;
		if (ok) { at += wrote; left -= (size_t)wrote; }
	}
	if (close(fd) != 0) ok = false;
	if (!ok) unlinkat(dir->fd, name, 0);
	return ok ? CE_SHOT_SAVED : CE_SHOT_WRITE;
#endif
}
}

extern "C" unsigned long long CE_ScreenshotTicks(void) { return SDL_GetTicks(); }

extern "C" int CE_ScreenshotSave(const void *pixels, int pitch, int width, int height, unsigned d3dFormat, char *name, int n)
{
	if (name && n > 0) name[0] = 0;
	const SDL_PixelFormat format = Format(d3dFormat);
	if (format == SDL_PIXELFORMAT_UNKNOWN) return CE_SHOT_FORMAT;
	if (!pixels || width <= 0 || height <= 0 || width > CE_SHOT_MAX_SIDE || height > CE_SHOT_MAX_SIDE ||
		pitch < width * (int)SDL_BYTESPERPIXEL(format))
		return CE_SHOT_SIZE;
	char user[4096], folder[4200];
	ce_user_dir(user, sizeof user);
#ifdef _WIN32
	if ((size_t)snprintf(folder, sizeof folder, "%s\\screenshots", user) >= sizeof folder) return CE_SHOT_FOLDER;
#else
	if ((size_t)snprintf(folder, sizeof folder, "%s/screenshots", user) >= sizeof folder) return CE_SHOT_FOLDER;
#endif
	CeSettingsDir dir;
	if (!ce_settings_dir_open(folder, 1, &dir)) return CE_SHOT_FOLDER;
	if (Entries(&dir, CE_SHOT_MAX_FILES) >= CE_SHOT_MAX_FILES)
	{
		ce_settings_dir_close(&dir);
		return CE_SHOT_FULL;
	}
	int status = CE_SHOT_ENCODE;
	SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, format, (void *)pixels, pitch);
	SDL_IOStream *io = surface ? SDL_IOFromDynamicMem() : NULL;
	if (io && SDL_SavePNG_IO(surface, io, false))
	{
		const Sint64 size = SDL_TellIO(io);
		const void *data = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
		if (data && size > 0 && size <= CE_SHOT_MAX_BYTES)
		{
			time_t now = time(NULL);
			struct tm local;
#ifdef _WIN32
			const bool dated = localtime_s(&local, &now) == 0;
#else
			const bool dated = localtime_r(&now, &local) != NULL;
#endif
			char stem[64], file[80];
			if (dated)
				snprintf(stem, sizeof stem, "onslaught-ce-%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
						 local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
			else
				snprintf(stem, sizeof stem, "onslaught-ce-%llu", (unsigned long long)SDL_GetTicks());
			status = CE_SHOT_NAME;
			for (int i = 1; i <= 99 && status == CE_SHOT_NAME; ++i)
			{
				if (i == 1) snprintf(file, sizeof file, "%s.png", stem);
				else snprintf(file, sizeof file, "%s-%d.png", stem, i);
				status = WriteNew(&dir, file, data, (size_t)size);
			}
			if (status == CE_SHOT_SAVED && name && n > 0) snprintf(name, n, "%s", file);
		}
	}
	if (io) SDL_CloseIO(io);
	if (surface) SDL_DestroySurface(surface);
	ce_settings_dir_close(&dir);
	return status;
}

#endif
