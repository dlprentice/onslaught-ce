// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/files.cpp: the Windows build's file calls through the same two roots as Linux (paths.cpp):
// the player's game folder (BEA_GAME_DIR, default the folder of the port's executable), read only, and the port's
// writable folder (BEA_USER_DIR, default %APPDATA%\Onslaught CE, Onslaught CE's user folder), which shadows it. Reads take the writable copy when there
// is one; writes always land in the writable folder, copying a game file there first when it is opened for update; so
// the player's install is never modified. A write to an absolute path (a drive letter, \ or /) is refused. port.h
// renames the game's calls (fopen, _access, _mkdir, _findfirst/_findnext/_findclose, CreateFileA,
// FindFirstFileA/FindNextFileA/FindClose, DeleteFileA, CreateDirectoryA, GetFileAttributesA, mmioOpenA) to these.
// FileIdInfo is a Windows8+ query. Expose its SDK types locally; failed queries still refuse the operation.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <mmsystem.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/mods.h"
#include "../common/platform.h"
#include "../common/save_names.h"

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

// Whether a read of game_path that would take `resolved` reads the game folder's own copy: only then may an enabled
// mod's copy replace it (platform/common/mods.h); the writable folder's copy of a file still wins over a mod.
bool ModMayReplace(const char *game_path, const char *resolved)
{
	char rel[MAX_PATH], game[MAX_PATH * 2];
	if (!PortModsActive() || Absolute(game_path) || !Relative(game_path, rel, sizeof(rel)))
		return false;
	snprintf(game, sizeof(game), "%s\\%s", gGameDir, rel);
	return _stricmp(game, resolved) == 0;
}

// An enabled mod's verified copy of a game file for reading, or NULL.
HANDLE ModRead(const char *game_path, const char *resolved)
{
	return ModMayReplace(game_path, resolved) ? (HANDLE)PortModOpenRead(game_path) : NULL;
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
	bool saveNames;
	DWORD error;
	int sourceFirst;
};

DWORD SaveNativeSameFile(const char *a, const char *b)
{
	const char *paths[] = {a, b};
	const auto lastError = []() { DWORD error = GetLastError(); return error ? error : DWORD(ERROR_GEN_FAILURE); };
	// ReFS does not promise unique 64-bit IDs; compare the documented volume + full 128-bit identity.
	// https://learn.microsoft.com/windows/win32/api/fileapi/ns-fileapi-by_handle_file_information
	// https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-file_id_info
	FILE_ID_INFO info[2] = {};
	for (int i = 0; i < 2; i++)
	{
		WCHAR wide[MAX_PATH * 2];
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, paths[i], -1, wide, sizeof(wide) / sizeof(wide[0]))) return lastError();
		HANDLE h = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
		{
			DWORD error = lastError();
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ERROR_NOT_SUPPORTED : error;
		}
		// FileIdInfo requires Windows 8+. Unsupported filesystem/OS queries refuse this save operation.
		BOOL ok = GetFileInformationByHandleEx(h, FileIdInfo, &info[i], sizeof(info[i]));
		DWORD error = ok ? 0 : lastError();
		if (!CloseHandle(h) && !error) error = lastError();
		if (error) return error;
	}
	return info[0].VolumeSerialNumber == info[1].VolumeSerialNumber &&
		!memcmp(info[0].FileId.Identifier, info[1].FileId.Identifier, sizeof(info[0].FileId.Identifier)) ? 0 : ERROR_NOT_SUPPORTED;
}

void FindAdd(Find *f, const WIN32_FIND_DATAA *d)
{
	for (int i = 0; i < f->count; i++)
	{
		if (f->saveNames)
		{
			WCHAR a[32], b[32];
			DWORD error = PortSaveNameUtf16(f->entries[i].cFileName, a, 32);
			if (!error) error = PortSaveNameUtf16(d->cFileName, b, 32);
			int equal = error ? -1 : PortSaveNamesEqual(a, b);
			if (equal < 0) { f->error = error ? error : GetLastError(); f->count = 0; return; }
			if (equal)
			{
				// User entries shadow the game root. Two native entries in one root with the same save identity
				// are unsupported: selecting either would silently alias a distinct file.
				DWORD error = ERROR_DUP_NAME;
				if (i < f->sourceFirst)
				{
					char canonical[MAX_PATH * 2], alias[MAX_PATH * 2];
					snprintf(canonical, sizeof(canonical), "%s\\savegames\\%s", gUserDir, f->entries[i].cFileName);
					snprintf(alias, sizeof(alias), "%s\\savegames\\%s", gUserDir, d->cFileName);
					error = SaveNativeSameFile(canonical, alias);
				}
				if (error) { f->error = error; f->count = 0; }
				return;
			}
		}
		else if (_stricmp(f->entries[i].cFileName, d->cFileName) == 0) return;
	}
	void *grown = realloc(f->entries, sizeof(WIN32_FIND_DATAA) * (f->count + 1));
	if (!grown) { f->error = ERROR_NOT_ENOUGH_MEMORY; f->count = 0; return; }
	f->entries = (WIN32_FIND_DATAA *)grown;
	f->entries[f->count++] = *d;
}

enum SaveDirectoryCase { SAVE_CASE_QUERY_UNKNOWN, SAVE_CASE_KNOWN_INSENSITIVE };

DWORD SaveDirectoryCasePolicy(const WCHAR *spec, SaveDirectoryCase *policy)
{
	*policy = SAVE_CASE_QUERY_UNKNOWN;
	const auto lastError = []() { DWORD error = GetLastError(); return error ? error : DWORD(ERROR_GEN_FAILURE); };
	WCHAR directory[MAX_PATH * 2];
	size_t length = wcslen(spec);
	if (length >= sizeof(directory) / sizeof(directory[0])) return ERROR_FILENAME_EXCED_RANGE;
	memcpy(directory, spec, (length + 1) * sizeof(WCHAR));
	WCHAR *slash = wcsrchr(directory, L'\\');
	if (!slash) return ERROR_INVALID_NAME;
	*slash = 0;
	HANDLE h = CreateFileW(directory, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		DWORD error = lastError();
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : error;
	}
	FILE_CASE_SENSITIVE_INFO info = {};
	// FILE_INFO_BY_HANDLE_CLASS::FileCaseSensitiveInfo is 23; older SDK target macros hide its name.
	// Unsupported queries do not establish the directory's case behavior. Admit UNKNOWN only through the complete
	// save-name scan and native identity/absence checks below; never interpret a query failure as a clear flag.
	const FILE_INFO_BY_HANDLE_CLASS caseSensitiveInfo = static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
	BOOL ok = GetFileInformationByHandleEx(h, caseSensitiveInfo, &info, sizeof(info));
	DWORD error = ok ? (info.Flags ? ERROR_NOT_SUPPORTED : 0) : lastError();
	bool unknown = !ok && (error == ERROR_INVALID_PARAMETER || error == ERROR_CALL_NOT_IMPLEMENTED);
	if (unknown) error = 0;
	if (!CloseHandle(h) && !error) error = lastError();
	if (!error && ok) *policy = SAVE_CASE_KNOWN_INSENSITIVE;
	return error;
}

void FindScan(Find *f, const char *spec)
{
	f->sourceFirst = f->count;
	if (f->saveNames)
	{
		const auto lastError = []() { DWORD error = GetLastError(); return error ? error : DWORD(ERROR_GEN_FAILURE); };
		// Savecard names are UTF-8 after patch 0066. Enumerate UTF-16 first so an invalid native surrogate cannot
		// be replaced by an A API and exposed as a different, valid career name. Other searches keep their A path.
		WCHAR wideSpec[MAX_PATH * 2];
		int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, spec, -1, NULL, 0);
		if (!needed) { f->error = lastError(); f->count = 0; return; }
		if (needed > (int)(sizeof(wideSpec) / sizeof(wideSpec[0]))) { f->error = ERROR_FILENAME_EXCED_RANGE; f->count = 0; return; }
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, spec, -1, wideSpec, needed) != needed)
		{
			f->error = lastError(); f->count = 0; return;
		}
		SaveDirectoryCase policy;
		if (DWORD error = SaveDirectoryCasePolicy(wideSpec, &policy)) { f->error = error; f->count = 0; return; }
		// Both known-insensitive and query-unknown directories use the same full scan. A native *.bes wildcard can
		// miss upper-case extensions or match a short-name alias; only the strict long-name codec admits savecards.
		WCHAR *slash = wcsrchr(wideSpec, L'\\');
		if (!slash || slash + 2 >= wideSpec + sizeof(wideSpec) / sizeof(wideSpec[0]))
		{ f->error = ERROR_INVALID_NAME; f->count = 0; return; }
		slash[1] = L'*'; slash[2] = 0;
		WIN32_FIND_DATAW wide;
		HANDLE h = FindFirstFileW(wideSpec, &wide);
		if (h == INVALID_HANDLE_VALUE)
		{
			DWORD error = lastError();
			if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND && error != ERROR_NO_MORE_FILES)
			{
				f->error = error; f->count = 0;
			}
			return;
		}
		do
		{
			WIN32_FIND_DATAA entry = {};
			int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.cFileName, -1, NULL, 0, NULL, NULL);
			if (bytes <= 0)
			{
				DWORD error = lastError();
				if (error == ERROR_NO_UNICODE_TRANSLATION) continue; // an invalid native surrogate is not a save name
				f->error = error; f->count = 0; break;
			}
			if (bytes > (int)sizeof(entry.cFileName)) continue;
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.cFileName, -1, entry.cFileName, bytes, NULL, NULL) != bytes)
			{ f->error = lastError(); f->count = 0; break; }
			if (PortSaveNameUtf16(entry.cFileName, NULL, 0)) continue;
			entry.dwFileAttributes = wide.dwFileAttributes;
			entry.ftCreationTime = wide.ftCreationTime;
			entry.ftLastAccessTime = wide.ftLastAccessTime;
			entry.ftLastWriteTime = wide.ftLastWriteTime;
			entry.nFileSizeHigh = wide.nFileSizeHigh;
			entry.nFileSizeLow = wide.nFileSizeLow;
			entry.dwReserved0 = wide.dwReserved0;
			entry.dwReserved1 = wide.dwReserved1;
			FindAdd(f, &entry);
			if (f->error) break;
		} while (FindNextFileW(h, &wide));
		if (!f->error && GetLastError() != ERROR_NO_MORE_FILES) { f->error = lastError(); f->count = 0; }
		if (!FindClose(h) && !f->error) { f->error = lastError(); f->count = 0; }
		return;
	}
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
	f->saveNames = spec && (!_stricmp(spec, "savegames\\*.bes") || !_stricmp(spec, "savegames/*.bes"));
	char user[MAX_PATH * 2], game[MAX_PATH * 2];
	if (Resolve(spec, SHADOW, user, sizeof(user)) == 0)
		FindScan(f, user);
	if (f->error) return f;
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

DWORD SaveFoldedNameAbsent(const void *name)
{
	Find *f = FindStart("savegames\\*.bes");
	DWORD error = f->error;
	for (int i = 0; !error && i < f->count; i++)
	{
		// Hidden/system/directory aliases also block a reservation, even though the savecard UI does not list them.
		WCHAR listed[32];
		error = PortSaveNameUtf16(f->entries[i].cFileName, listed, 32);
		int equal = error ? -1 : PortSaveNamesEqual(name, listed);
		if (equal < 0) { if (!error) { error = GetLastError(); if (!error) error = ERROR_GEN_FAILURE; } }
		else if (equal) error = ERROR_FILE_EXISTS;
	}
	FindFree(f);
	return error;
}
}

// Whether the game folder itself has this regular file (mods.h: a mod may only replace the game's files).
bool PortGameHasFile(const char *rel)
{
	Init();
	char native[MAX_PATH], path[MAX_PATH * 2];
	if (!Relative(rel, native, sizeof(native)))
		return false;
	snprintf(path, sizeof(path), "%s\\%s", gGameDir, native);
	DWORD a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
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

// The writable-folder path (UTF-8) a write to game_path reaches, for save_replace.cpp. As every write does, it creates the
// parent folders the game folder also has; it never creates the file. 0, or a Win32 error.
unsigned PortResolveUserPath(const char *game_path, char *out, unsigned n)
{
	return Resolve(game_path, WRITE, out, n) == 0 ? 0 : ERROR_INVALID_NAME;
}

unsigned PortSaveLookupUtf16(const void *name, int *index)
{
	*index = -1;
	char path[MAX_PATH];
	DWORD error = PortSavePathUtf8(name, path, sizeof(path));
	if (error) return error;
	Find *f = FindStart("savegames\\*.bes");
	error = f->error;
	int admitted = 0;
	char canonicalPath[MAX_PATH] = {};
	for (int i = 0; !error && i < f->count; i++)
	{
		if (f->entries[i].dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY)) continue;
		WCHAR listed[32];
		error = PortSaveNameUtf16(f->entries[i].cFileName, listed, 32);
		int equal = error ? -1 : PortSaveNamesEqual(name, listed);
		if (equal < 0) { if (!error) error = GetLastError(); break; }
		if (equal)
		{
			*index = admitted;
			snprintf(canonicalPath, sizeof(canonicalPath), "savegames\\%s", f->entries[i].cFileName);
		}
		admitted++;
	}
	FindFree(f);
	if (!error && *index != -1)
	{
		char native[MAX_PATH * 2], canonicalNative[MAX_PATH * 2];
		// Bind the requested spelling to the admitted entry, even across volumes with differing case tables.
		if (Resolve(path, READ, native, sizeof(native)) || Resolve(canonicalPath, READ, canonicalNative, sizeof(canonicalNative)))
			error = ERROR_NOT_SUPPORTED;
		else error = SaveNativeSameFile(native, canonicalNative);
	}
	if (error) { *index = -1; SetLastError(error); }
	return error;
}

unsigned PortSaveReserveUtf8(const char *path)
{
	WCHAR name[32]; char checked[MAX_PATH], native[MAX_PATH * 2];
	if (!path || strncmp(path, "savegames\\", 10)) return ERROR_INVALID_NAME;
	DWORD error = PortSaveNameUtf16(path + 10, name, 32);
	if (!error) error = PortSavePathUtf8(name, checked, sizeof(checked));
	if (error) return error;
	if (strcmp(path, checked) || Resolve(path, WRITE, native, sizeof(native))) return ERROR_INVALID_NAME;
	WCHAR wide[MAX_PATH * 2];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, native, -1, wide, sizeof(wide) / sizeof(wide[0])))
	{ error = GetLastError(); return error ? error : DWORD(ERROR_GEN_FAILURE); }
	SaveDirectoryCase policy;
	if ((error = SaveDirectoryCasePolicy(wide, &policy))) return error;
	// CREATE_NEW protects only the literal native path. Re-prove folded absence in both roots immediately before
	// reserving, even for a valid canonical spelling and even when the case query succeeded.
	if ((error = SaveFoldedNameAbsent(name))) return error;
	HANDLE h = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE || !CloseHandle(h)) { error = GetLastError(); return error ? error : DWORD(ERROR_GEN_FAILURE); }
	// Patch0066 rechecks the namespace before WriteSave. A later different-case alias fails closed there, possibly
	// leaving this owned empty reservation. The subsequent pathname reopen is not an atomic save transaction.
	return 0;
}

int port_findnext(intptr_t h, struct _finddata_t *d);

FILE *port_fopen(const char *name, const char *mode)
{
	char path[MAX_PATH * 2];
	bool writes = strpbrk(mode, "wa+") != NULL;
	bool update = strchr(mode, 'a') || (strchr(mode, 'r') && strchr(mode, '+'));
	int rc = writes ? (update ? CopyOnWrite(name, path, sizeof(path)) : Resolve(name, WRITE, path, sizeof(path)))
					: Resolve(name, READ, path, sizeof(path));
	if (rc != 0)
		return NULL;
	if (HANDLE h = writes ? NULL : ModRead(name, path))
	{
		int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | (strchr(mode, 'b') ? _O_BINARY : _O_TEXT));
		if (fd < 0)
		{
			CloseHandle(h);
			return NULL;
		}
		FILE *f = _fdopen(fd, mode);
		if (!f)
			_close(fd);
		return f;
	}
	return fopen(path, mode);
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
		DWORD error = f->error;
		FindFree(f);
		if (error) SetLastError(error);
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
	if (HANDLE h = write ? NULL : ModRead(name, path))
		return h;
	return CreateFileA(path, access, share, sa, disposition, flags, templ);
}

// Search handles are the port's own (a merged listing); they never reach kernel32.
HANDLE WINAPI PortFindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA data)
{
	Find *f = FindStart(spec);
	if (!f->count)
	{
		DWORD error = f->error ? f->error : ERROR_FILE_NOT_FOUND;
		FindFree(f);
		SetLastError(error);
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
	bool write = (flags & (MMIO_WRITE | MMIO_READWRITE)) != 0;
	if (Resolve(name, write ? WRITE : READ, path, sizeof(path)) != 0)
		return NULL;
	char mod[MAX_PATH * 2];
	if (!write && ModMayReplace(name, path) && PortModPath(name, mod, sizeof(mod)))
		return mmioOpenA(mod, info, flags);
	return mmioOpenA(path, info, flags);
}
}
