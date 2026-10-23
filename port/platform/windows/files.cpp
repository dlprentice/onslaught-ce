// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/files.cpp: the Windows build's file calls through the same two roots as Linux (paths.cpp):
// the player's game folder (BEA_GAME_DIR, default the folder of the port's executable), read only, and the port's
// writable folder (BEA_USER_DIR, default %APPDATA%\Onslaught CE, Onslaught CE's user folder), which shadows it. Reads take the writable copy when there
// is one; writes always land in the writable folder, copying a game file there first when it is opened for update; so
// the player's install is never modified. A write to an absolute path (a drive letter, \ or /) is refused. port.h
// renames the game's calls (fopen, _access, _mkdir, _findfirst/_findnext/_findclose, CreateFileA,
// FindFirstFileA/FindNextFileA/FindClose, DeleteFileA, CreateDirectoryA, GetFileAttributesA, mmioOpenA) to these.
#include <windows.h>
#include <mmsystem.h>
#include <direct.h>
#include <io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/platform.h"

namespace
{
char gGameDir[MAX_PATH];
char gUserDir[MAX_PATH];

bool Absolute(const char *p)
{
	return p[0] == '\\' || p[0] == '/' || (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':');
}

bool Exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

void MakeDirs(const char *path)
{
	char tmp[MAX_PATH];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 3; *p; p++)
		if (*p == '\\' || *p == '/')
		{
			char c = *p;
			*p = 0;
			CreateDirectoryA(tmp, NULL);
			*p = c;
		}
	CreateDirectoryA(tmp, NULL);
}

void Init()
{
	if (gGameDir[0])
		return;
	const char *game = getenv("BEA_GAME_DIR");
	if (game && *game)
		snprintf(gGameDir, sizeof(gGameDir), "%s", game);
	else
	{
		GetModuleFileNameA(NULL, gGameDir, sizeof(gGameDir));
		if (char *slash = strrchr(gGameDir, '\\'))
			*slash = 0;
	}
	const char *user = getenv("BEA_USER_DIR");
	const char *appdata = getenv("APPDATA");
	if (user && *user)
		snprintf(gUserDir, sizeof(gUserDir), "%s", user);
	else
		snprintf(gUserDir, sizeof(gUserDir), "%s\\Onslaught CE", appdata && *appdata ? appdata : ".");
	MakeDirs(gUserDir);
}

// The game path as components joined by \: separators of either kind, "." and empty parts dropped, ".." taking the
// previous component away and never climbing above the root (as the Linux path layer's Components()), so no name can
// reach outside the game folder or the port's writable folder. False (out empty) when a component or the whole result
// does not fit: a shortened name would be another file.
bool Relative(const char *game_path, char *out, size_t n)
{
	char parts[64][MAX_PATH];
	int count = 0;
	char cur[MAX_PATH];
	size_t len = 0;
	for (const char *p = game_path;; p++)
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
				else if (count < 64)
					snprintf(parts[count++], MAX_PATH, "%s", cur);
				else
					return out[0] = 0, false;
			}
			len = 0;
			if (*p == 0)
				break;
		}
		else if (len < sizeof(cur) - 1)
			cur[len++] = *p;
		else
			return out[0] = 0, false;
	}
	out[0] = 0;
	size_t used = 0;
	for (int i = 0; i < count; i++)
	{
		int w = snprintf(out + used, n - used, "%s%s", i ? "\\" : "", parts[i]);
		if (w < 0 || (size_t)w >= n - used)
			return out[0] = 0, false;
		used += (size_t)w;
	}
	return true;
}

enum Mode { READ, WRITE, SHADOW };

// 0 and a host path in out, or -1.
int Resolve(const char *game_path, Mode mode, char *out, size_t n)
{
	Init();
	if (!game_path || !*game_path)
		return -1;
	if (Absolute(game_path))
	{
		if (mode != READ)
			return -1;
		snprintf(out, n, "%s", game_path);
		return 0;
	}
	char rel[MAX_PATH], user[MAX_PATH * 2], game[MAX_PATH * 2];
	if (!Relative(game_path, rel, sizeof(rel)))
		return -1;
	snprintf(user, sizeof(user), "%s\\%s", gUserDir, rel);
	snprintf(game, sizeof(game), "%s\\%s", gGameDir, rel);
	if (mode == READ)
	{
		snprintf(out, n, "%s", Exists(user) ? user : game);
		return 0;
	}
	snprintf(out, n, "%s", user);
	if (mode == WRITE)
	{
		// Parent folders the game folder has are created in the writable folder (savegames\, ...).
		char parent[MAX_PATH * 2];
		snprintf(parent, sizeof(parent), "%s", rel);
		if (char *slash = strrchr(parent, '\\'))
		{
			*slash = 0;
			char gparent[MAX_PATH * 3], uparent[MAX_PATH * 3];
			snprintf(gparent, sizeof(gparent), "%s\\%s", gGameDir, parent);
			snprintf(uparent, sizeof(uparent), "%s\\%s", gUserDir, parent);
			if (!Exists(uparent) && Exists(gparent))
				MakeDirs(uparent);
		}
	}
	return 0;
}

int CopyOnWrite(const char *game_path, char *out, size_t n)
{
	char read[MAX_PATH * 2];
	Resolve(game_path, READ, read, sizeof(read));
	if (Resolve(game_path, WRITE, out, n) != 0)
		return -1;
	if (strcmp(read, out) != 0 && !Exists(out) && Exists(read))
		CopyFileA(read, out, TRUE);
	return 0;
}

// A merged search: the writable folder's entries first, names unique ignoring case, the whole list in NTFS name order
// (each folder alone already comes back in it on NTFS; the merge must keep it, as the Linux build's searches do):
// "." and ".." first, then ASCII letters upper-cased and every other byte compared as itself.
struct Find
{
	WIN32_FIND_DATAA *entries;
	int count;
	int next;
};

void FindAdd(Find *f, const WIN32_FIND_DATAA *d)
{
	for (int i = 0; i < f->count; i++)
		if (_stricmp(f->entries[i].cFileName, d->cFileName) == 0)
			return;
	f->entries = (WIN32_FIND_DATAA *)realloc(f->entries, sizeof(WIN32_FIND_DATAA) * (f->count + 1));
	f->entries[f->count++] = *d;
}

void FindScan(Find *f, const char *spec)
{
	WIN32_FIND_DATAA d;
	HANDLE h = FindFirstFileA(spec, &d);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do
		FindAdd(f, &d);
	while (FindNextFileA(h, &d));
	FindClose(h);
}

int FindNameOrder(const char *a, const char *b)
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

void FindSort(Find *f)
{
	for (int i = 1; i < f->count; i++)
		for (int j = i; j > 0 && FindNameOrder(f->entries[j - 1].cFileName, f->entries[j].cFileName) > 0; j--)
		{
			WIN32_FIND_DATAA e = f->entries[j];
			f->entries[j] = f->entries[j - 1];
			f->entries[j - 1] = e;
		}
}

Find *FindStart(const char *spec)
{
	Find *f = (Find *)calloc(1, sizeof(Find));
	char user[MAX_PATH * 2], game[MAX_PATH * 2];
	if (Resolve(spec, SHADOW, user, sizeof(user)) == 0)
		FindScan(f, user);
	Resolve(spec, READ, game, sizeof(game));
	if (_stricmp(game, user) != 0)
		FindScan(f, game);
	FindSort(f);
	return f;
}

void FindFree(Find *f)
{
	free(f->entries);
	free(f);
}
}

const char *PortGameDir()
{
	Init();
	return gGameDir;
}

const char *PortUserDir()
{
	Init();
	return gUserDir;
}

extern "C" {

int port_findnext(intptr_t h, struct _finddata_t *d);

FILE *port_fopen(const char *name, const char *mode)
{
	char path[MAX_PATH * 2];
	bool writes = strpbrk(mode, "wa+") != NULL;
	bool update = strchr(mode, 'a') || (strchr(mode, 'r') && strchr(mode, '+'));
	int rc = writes ? (update ? CopyOnWrite(name, path, sizeof(path)) : Resolve(name, WRITE, path, sizeof(path)))
					: Resolve(name, READ, path, sizeof(path));
	return rc == 0 ? fopen(path, mode) : NULL;
}

int port_access(const char *name, int mode)
{
	char path[MAX_PATH * 2];
	// Anything that exists can be written through the overlay, so every mode asks only whether it exists.
	(void)mode;
	return Resolve(name, READ, path, sizeof(path)) == 0 ? _access(path, 0) : -1;
}

int port_mkdir(const char *name)
{
	char path[MAX_PATH * 2];
	return Resolve(name, WRITE, path, sizeof(path)) == 0 ? _mkdir(path) : -1;
}

intptr_t port_findfirst(const char *spec, struct _finddata_t *d)
{
	Find *f = FindStart(spec);
	if (!f->count)
	{
		FindFree(f);
		return -1;
	}
	if (port_findnext((intptr_t)f, d) != 0)
		return -1;
	return (intptr_t)f;
}

int port_findnext(intptr_t h, struct _finddata_t *d)
{
	Find *f = (Find *)h;
	if (!f || f->next >= f->count)
		return -1;
	const WIN32_FIND_DATAA &e = f->entries[f->next++];
	memset(d, 0, sizeof(*d));
	d->attrib = e.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
									  FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
	d->size = e.nFileSizeLow;
	snprintf(d->name, sizeof(d->name), "%s", e.cFileName);
	return 0;
}

int port_findclose(intptr_t h)
{
	if (h != -1 && h)
		FindFree((Find *)h);
	return 0;
}

HANDLE WINAPI PortCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags,
							  HANDLE templ)
{
	char path[MAX_PATH * 2];
	bool write = (access & GENERIC_WRITE) != 0;
	int rc = !write ? Resolve(name, READ, path, sizeof(path))
					: (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS) ? CopyOnWrite(name, path, sizeof(path))
																				   : Resolve(name, WRITE, path, sizeof(path));
	if (rc != 0)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}
	return CreateFileA(path, access, share, sa, disposition, flags, templ);
}

// Search handles are the port's own (a merged listing); they never reach kernel32.
HANDLE WINAPI PortFindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA data)
{
	Find *f = FindStart(spec);
	if (!f->count)
	{
		FindFree(f);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	*data = f->entries[f->next++];
	return (HANDLE)f;
}

BOOL WINAPI PortFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data)
{
	Find *f = (Find *)h;
	if (!f || h == INVALID_HANDLE_VALUE || f->next >= f->count)
	{
		SetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	*data = f->entries[f->next++];
	return TRUE;
}

BOOL WINAPI PortFindClose(HANDLE h)
{
	if (!h || h == INVALID_HANDLE_VALUE)
		return FALSE;
	FindFree((Find *)h);
	return TRUE;
}

BOOL WINAPI PortDeleteFileA(LPCSTR name)
{
	char path[MAX_PATH * 2];
	return Resolve(name, SHADOW, path, sizeof(path)) == 0 && DeleteFileA(path);
}

BOOL WINAPI PortCreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES sa)
{
	char path[MAX_PATH * 2];
	return Resolve(name, WRITE, path, sizeof(path)) == 0 && CreateDirectoryA(path, sa);
}

DWORD WINAPI PortGetFileAttributesA(LPCSTR name)
{
	char path[MAX_PATH * 2];
	return Resolve(name, READ, path, sizeof(path)) == 0 ? GetFileAttributesA(path) : INVALID_FILE_ATTRIBUTES;
}

HMMIO WINAPI PortMmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags)
{
	char path[MAX_PATH * 2];
	if (Resolve(name, (flags & (MMIO_WRITE | MMIO_READWRITE)) ? WRITE : READ, path, sizeof(path)) != 0)
		return NULL;
	return mmioOpenA(path, info, flags);
}
}
