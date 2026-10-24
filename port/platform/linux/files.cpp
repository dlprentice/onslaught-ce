// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/files.cpp: the game's file calls on Linux (CreateFile/ReadFile/..., FindFirstFile, the CRT's
// fopen/_access/_findfirst/_mkdir) through the path layer (paths.cpp).
#include "handles.h"
#include <io.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static DWORD ErrnoToWin32(int e)
{
	switch (e)
	{
	case ENOENT: return ERROR_FILE_NOT_FOUND;
	case ENOTDIR: return ERROR_PATH_NOT_FOUND;
	case EACCES:
	case EPERM: return ERROR_ACCESS_DENIED;
	case EEXIST: return ERROR_ALREADY_EXISTS;
	default: return ERROR_GEN_FAILURE;
	}
}

// ---- CRT ------------------------------------------------------------------------------------------------------------
extern "C" FILE *port_fopen(const char *name, const char *mode)
{
	char path[4096];
	// The first character selects the operation. LoadCardId uses "ra", which the CRT accepts as a read mode.
	bool writes = mode[0] == 'w' || mode[0] == 'a' || strchr(mode, '+');
	if (writes)
	{
		int flags = strchr(mode, '+') ? O_RDWR : O_WRONLY;
		if (mode[0] == 'w') flags |= O_CREAT | O_TRUNC;
		if (mode[0] == 'a') flags |= O_CREAT | O_APPEND;
		if (strchr(mode, 'x')) flags |= O_EXCL;
		int fd = PortOpenUserFile(name, flags, mode[0] == 'r' || mode[0] == 'a');
		if (fd < 0)
			return NULL;
		FILE *f = fdopen(fd, mode);
		if (!f)
			close(fd);
		return f;
	}
	if (PortResolvePath(name, PORT_PATH_READ, path, sizeof(path)) != 0)
	{
		errno = ENOENT;
		return NULL;
	}
	return fopen(path, mode);
}

extern "C" int port_access(const char *name, int mode)
{
	char path[4096];
	if (PortResolvePath(name, PORT_PATH_READ, path, sizeof(path)) != 0)
		return -1;
	// MSVC's modes: 0 exists, 2 write, 4 read, 6 both. Anything readable here is writable through the overlay.
	return access(path, F_OK);
}



extern "C" int _mkdir(const char *name)
{
	return PortMakeUserDirectory(name);
}

extern "C" int _chdir(const char *) { return 0; }
extern "C" char *_getcwd(char *buf, int n)
{
	if (buf && n > 1)
		snprintf(buf, n, ".");
	return buf;
}

// ---- searches -------------------------------------------------------------------------------------------------------
// A search merges the writable folder and the game folder (writable copies first, names unique ignoring case) and lists
// the result in name order, as FindFirstFile does on NTFS, where the original ran: names compared code unit by code unit
// after upper-casing (FindNameOrder). readdir's order is the filesystem's (on btrfs, creation order),
// and the game shows what it finds in the order it finds it: the career list (CPCMemoryCard::GetSaveName counts entries
// from _findfirst), so the same keys picked a different career on a folder whose saves were made in another order.
struct PortFind
{
	PortHandle h;
	char **names;
	unsigned char *isdir;
	unsigned *sizes;
	int count;
	int next;
};

static void FindAdd(PortFind *f, const char *name, bool dir, unsigned size)
{
	for (int i = 0; i < f->count; i++)
		if (strcasecmp(f->names[i], name) == 0)
			return;
	f->names = (char **)realloc(f->names, sizeof(char *) * (f->count + 1));
	f->isdir = (unsigned char *)realloc(f->isdir, f->count + 1);
	f->sizes = (unsigned *)realloc(f->sizes, sizeof(unsigned) * (f->count + 1));
	f->names[f->count] = strdup(name);
	f->isdir[f->count] = dir;
	f->sizes[f->count] = size;
	f->count++;
}

// NTFS order for the names the game uses: "." and ".." first, then ASCII letters upper-cased and every other byte
// compared as itself.
static int FindNameOrder(const char *a, const char *b)
{
	int dots_a = !strcmp(a, ".") ? 1 : !strcmp(a, "..") ? 2 : 3, dots_b = !strcmp(b, ".") ? 1 : !strcmp(b, "..") ? 2 : 3;
	if (dots_a != 3 || dots_b != 3)
		return dots_a - dots_b;
	for (;; a++, b++)
	{
		unsigned ca = (unsigned char)*a, cb = (unsigned char)*b;
		if (ca >= 'a' && ca <= 'z')
			ca -= 'a' - 'A';
		if (cb >= 'a' && cb <= 'z')
			cb -= 'a' - 'A';
		if (ca != cb || !ca)
			return (int)ca - (int)cb;
	}
}

static void FindSort(PortFind *f)
{
	for (int i = 1; i < f->count; i++)
		for (int j = i; j > 0 && FindNameOrder(f->names[j - 1], f->names[j]) > 0; j--)
		{
			char *n = f->names[j];
			f->names[j] = f->names[j - 1];
			f->names[j - 1] = n;
			unsigned char d = f->isdir[j];
			f->isdir[j] = f->isdir[j - 1];
			f->isdir[j - 1] = d;
			unsigned z = f->sizes[j];
			f->sizes[j] = f->sizes[j - 1];
			f->sizes[j - 1] = z;
		}
}

static void FindScan(PortFind *f, const char *dir, const char *pattern, const char *game_dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	while (dirent *e = readdir(d))
	{
		if (fnmatch(pattern, e->d_name, FNM_CASEFOLD) != 0)
			continue;
		char full[4096];
		struct stat st;
		snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
		if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
		{
			char logical[4096], selected[4096];
			snprintf(logical, sizeof(logical), "%s/%s", game_dir, e->d_name);
			// Use the load/save identity when listing too. In particular, two case-equivalent career files in one root
			// are both refused; an ambiguous writable identity must not uncover a different original career.
			if (PortResolvePath(logical, PORT_PATH_READ, selected, sizeof(selected)) != 0 || strcmp(selected, full) != 0)
				continue;
		}
		if (stat(full, &st) == 0)
			FindAdd(f, e->d_name, S_ISDIR(st.st_mode), (unsigned)st.st_size);
	}
	closedir(d);
}

static PortFind *FindStart(const char *spec)
{
	char dirpart[4096], pattern[256];
	snprintf(dirpart, sizeof(dirpart), "%s", spec);
	char *cut = NULL;
	for (char *p = dirpart; *p; p++)
		if (*p == '\\' || *p == '/')
			cut = p;
	if (cut)
	{
		snprintf(pattern, sizeof(pattern), "%s", cut + 1);
		*cut = 0;
	}
	else
	{
		snprintf(pattern, sizeof(pattern), "%s", dirpart);
		snprintf(dirpart, sizeof(dirpart), ".");
	}
	if (strcmp(pattern, "*.*") == 0)
		snprintf(pattern, sizeof(pattern), "*");
	PortFind *f = (PortFind *)PortHandleNew(PORT_FIND, sizeof(PortFind));
	char user[4096], game[4096];
	// An ambiguous writable directory (savegames and SAVEGAMES) blocks the logical search, including its fallback.
	if (PortResolvePath(dirpart, PORT_PATH_READ, game, sizeof(game)) != 0)
		return f;
	bool have_user = PortResolvePath(dirpart, PORT_PATH_SHADOW, user, sizeof(user)) == 0;
	if (have_user)
		FindScan(f, user, pattern, dirpart);
	// A normal read of the directory picks the writable directory when it exists, hiding the game folder's entries.
	if (PortResolvePath(dirpart, PORT_PATH_GAME, game, sizeof(game)) == 0 && (!have_user || strcmp(game, user) != 0))
		FindScan(f, game, pattern, dirpart);
	FindSort(f);
	return f;
}

static void FindFree(PortFind *f)
{
	for (int i = 0; i < f->count; i++)
		free(f->names[i]);
	free(f->names);
	free(f->isdir);
	free(f->sizes);
}

static void FillFindData(PortFind *f, int i, LPWIN32_FIND_DATAA data)
{
	memset(data, 0, sizeof(*data));
	data->dwFileAttributes = f->isdir[i] ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	data->nFileSizeLow = f->sizes[i];
	snprintf(data->cFileName, sizeof(data->cFileName), "%s", f->names[i]);
}

extern "C" HANDLE WINAPI FindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA data)
{
	PortFind *f = FindStart(spec);
	if (f->count == 0)
	{
		FindFree(f);
		PortHandleFree(&f->h);
		PortSetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	FillFindData(f, f->next++, data);
	return f;
}

extern "C" BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data)
{
	PortFind *f = (PortFind *)PortCheck(h, PORT_FIND);
	if (!f)
		return FALSE;
	if (f->next >= f->count)
	{
		PortSetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	FillFindData(f, f->next++, data);
	return TRUE;
}

extern "C" BOOL WINAPI FindClose(HANDLE h)
{
	PortFind *f = (PortFind *)PortCheck(h, PORT_FIND);
	if (!f)
		return FALSE;
	FindFree(f);
	PortHandleFree(&f->h);
	return TRUE;
}

static void FillFindT(PortFind *f, int i, struct _finddata_t *d)
{
	memset(d, 0, sizeof(*d));
	d->attrib = f->isdir[i] ? _A_SUBDIR : _A_NORMAL;
	d->size = f->sizes[i];
	snprintf(d->name, sizeof(d->name), "%s", f->names[i]);
}

extern "C" long _findfirst(const char *spec, struct _finddata_t *d)
{
	PortFind *f = FindStart(spec);
	if (f->count == 0)
	{
		FindFree(f);
		PortHandleFree(&f->h);
		errno = ENOENT;
		return -1;
	}
	FillFindT(f, f->next++, d);
	return (long)(intptr_t)f;
}

extern "C" int _findnext(long h, struct _finddata_t *d)
{
	PortFind *f = (PortFind *)PortCheck((HANDLE)(intptr_t)h, PORT_FIND);
	if (!f || f->next >= f->count)
	{
		errno = ENOENT;
		return -1;
	}
	FillFindT(f, f->next++, d);
	return 0;
}

extern "C" int _findclose(long h)
{
	FindClose((HANDLE)(intptr_t)h);
	return 0;
}

// ---- Win32 files ----------------------------------------------------------------------------------------------------
void PortFileClose(PortHandle *h)
{
	PortFile *f = (PortFile *)h;
	if (f->fd >= 0)
		close(f->fd);
}

extern "C" HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD,
									 HANDLE)
{
	char path[4096];
	bool write = (access & GENERIC_WRITE) != 0;
	// Creation and truncation cannot use a read-only path into the original folder.
	if (!write && disposition != OPEN_EXISTING)
	{
		PortSetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}
	int flags = write ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
	switch (disposition)
	{
	case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
	case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
	case OPEN_ALWAYS: flags |= O_CREAT; break;
	case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
	default: break;
	}
	int fd = -1;
	if (write)
		fd = PortOpenUserFile(name, flags, disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS);
	else if (PortResolvePath(name, PORT_PATH_READ, path, sizeof(path)) == 0)
		fd = open(path, flags, 0644);
	if (fd < 0)
	{
		PortSetLastError(ErrnoToWin32(errno));
		return INVALID_HANDLE_VALUE;
	}
	PortFile *f = (PortFile *)PortHandleNew(PORT_FILE, sizeof(PortFile));
	f->fd = fd;
	return f;
}

extern "C" HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
									 DWORD flags, HANDLE templ)
{
	char narrow[4096];
	WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), NULL, NULL);
	return CreateFileA(narrow, access, share, sa, disposition, flags, templ);
}

extern "C" BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED)
{
	PortFile *f = (PortFile *)PortCheck(h, PORT_FILE);
	if (!f)
		return FALSE;
	ssize_t total = 0;
	while ((DWORD)total < n)
	{
		ssize_t r = read(f->fd, (char *)buf + total, n - total);
		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0)
		{
			PortSetLastError(ErrnoToWin32(errno));
			if (got)
				*got = (DWORD)total;
			return FALSE;
		}
		if (r == 0)
			break;
		total += r;
	}
	if (got)
		*got = (DWORD)total;
	return TRUE;
}

extern "C" BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED)
{
	PortFile *f = (PortFile *)PortCheck(h, PORT_FILE);
	if (!f)
		return FALSE;
	ssize_t total = 0;
	while ((DWORD)total < n)
	{
		ssize_t r = write(f->fd, (const char *)buf + total, n - total);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
		{
			PortSetLastError(ErrnoToWin32(errno));
			if (put)
				*put = (DWORD)total;
			return FALSE;
		}
		total += r;
	}
	if (put)
		*put = (DWORD)total;
	return TRUE;
}

extern "C" DWORD WINAPI GetFileSize(HANDLE h, LPDWORD high)
{
	PortFile *f = (PortFile *)PortCheck(h, PORT_FILE);
	if (!f)
		return INVALID_FILE_SIZE;
	struct stat st;
	if (fstat(f->fd, &st) != 0)
		return INVALID_FILE_SIZE;
	if (high)
		*high = 0;
	return (DWORD)st.st_size;
}

extern "C" DWORD WINAPI SetFilePointer(HANDLE h, LONG distance, PLONG high, DWORD method)
{
	PortFile *f = (PortFile *)PortCheck(h, PORT_FILE);
	if (!f)
		return INVALID_SET_FILE_POINTER;
	int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : SEEK_END;
	off_t r = lseek(f->fd, (off_t)distance, whence);
	if (high)
		*high = 0;
	return r < 0 ? INVALID_SET_FILE_POINTER : (DWORD)r;
}

extern "C" BOOL WINAPI GetOverlappedResult(HANDLE, LPOVERLAPPED, LPDWORD n, BOOL)
{
	if (n)
		*n = 0;
	return TRUE;
}

extern "C" BOOL WINAPI DeleteFileA(LPCSTR name)
{
	// Only the port's writable copy can go; a file that exists only in the game folder stays.
	if (PortDeleteUserFile(name) != 0)
	{
		PortSetLastError(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}
	return TRUE;
}

extern "C" BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES)
{
	if (PortMakeUserDirectory(name) != 0)
	{
		PortSetLastError(ErrnoToWin32(errno));
		return FALSE;
	}
	return TRUE;
}

extern "C" DWORD WINAPI GetFileAttributesA(LPCSTR name)
{
	char path[4096];
	struct stat st;
	if (PortResolvePath(name, PORT_PATH_READ, path, sizeof(path)) != 0 || stat(path, &st) != 0)
		return INVALID_FILE_ATTRIBUTES;
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

extern "C" DWORD WINAPI GetCurrentDirectoryA(DWORD n, LPSTR buf)
{
	if (buf && n > 1)
		snprintf(buf, n, ".");
	return 1;
}
extern "C" BOOL WINAPI SetCurrentDirectoryA(LPCSTR) { return TRUE; }
