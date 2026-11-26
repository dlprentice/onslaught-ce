// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/registry.cpp: the game's registry calls (PCPlatform.cpp writes one volatile value under
// HKEY_CURRENT_USER\Software\Lost Toys\Battle Engine Aquila), on both targets, as an ini file in the port's writable
// folder (registry.ini), so the port writes nothing outside it. port.h renames the game's Reg* calls to these.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef ERROR_WRITE_FAULT
#define ERROR_WRITE_FAULT 29L
#endif
#ifndef ERROR_INVALID_PARAMETER
#define ERROR_INVALID_PARAMETER 87L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#define strncasecmp _strnicmp
#endif

#include "platform.h"
#if !defined(_WIN32)
#include "../linux/handles.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
struct RegKey
{
	char path[256];
};

bool ReadFullLine(FILE *f, std::string *line)
{
	line->clear();
	char chunk[4096];
	while (fgets(chunk, sizeof(chunk), f))
	{
		line->append(chunk);
		if (!line->empty() && line->back() == '\n')
			return true;
	}
	return !line->empty();
}

#if defined(_WIN32)
bool RegUserDirWide(std::wstring *out)
{
	const char *user = PortUserDir();
	if (!user || !*user)
		return false;
	int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, user, -1, NULL, 0);
	if (needed <= 1)
		return false;
	out->resize((size_t)needed - 1);
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, user, -1, &(*out)[0], needed) == needed;
}

FILE *RegOpenRead(bool *missing)
{
	if (missing) *missing = false;
	std::wstring dir;
	if (!RegUserDirWide(&dir))
		return NULL;
	std::wstring path = dir + L"\\registry.ini";
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
						   FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		DWORD err = GetLastError();
		if (missing && (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND))
			*missing = true;
		return NULL;
	}
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{
		CloseHandle(h);
		return NULL;
	}
	int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
	if (fd < 0)
	{
		CloseHandle(h);
		return NULL;
	}
	FILE *f = _fdopen(fd, "rb");
	if (!f)
		_close(fd);
	return f;
}

bool RegWriteAtomic(const char *data, size_t size)
{
	std::wstring dir;
	if (!RegUserDirWide(&dir))
		return false;
	std::wstring dst = dir + L"\\registry.ini";
	DWORD dstAttr = GetFileAttributesW(dst.c_str());
	if (dstAttr != INVALID_FILE_ATTRIBUTES && (dstAttr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
		return false;
	static unsigned long serial = 0;
	HANDLE h = INVALID_HANDLE_VALUE;
	std::wstring tmp;
	for (int attempt = 0; attempt < 16; ++attempt)
	{
		wchar_t suffix[64];
		_snwprintf(suffix, 64, L"\\registry.ini.tmp.%lu.%lu", (unsigned long)GetCurrentProcessId(), ++serial);
		suffix[63] = 0;
		tmp = dir + suffix;
		h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
						FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
		if (h != INVALID_HANDLE_VALUE)
			break;
		if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
	}
	if (h == INVALID_HANDLE_VALUE)
		return false;
	size_t writtenTotal = 0;
	bool ok = true;
	while (writtenTotal < size)
	{
		size_t left = size - writtenTotal;
		DWORD chunk = left > 0x40000000u ? 0x40000000u : (DWORD)left;
		DWORD wrote = 0;
		if (!WriteFile(h, data + writtenTotal, chunk, &wrote, NULL) || wrote != chunk)
		{
			ok = false;
			break;
		}
		writtenTotal += wrote;
	}
	if (ok && !FlushFileBuffers(h))
		ok = false;
	if (!CloseHandle(h))
		ok = false;
	if (!ok || !MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(tmp.c_str());
		return false;
	}
	return true;
}
#else
FILE *RegOpenRead(bool *missing)
{
	if (missing) *missing = false;
	// Reading uses the user-only identity, including its spelling, and never falls back to the game folder.
	int fd = PortOpenUserFile("registry.ini", O_RDONLY, false);
	if (fd < 0)
	{
		if (missing && errno == ENOENT)
			*missing = true;
		return NULL;
	}
	FILE *f = fdopen(fd, "rb");
	if (!f)
		close(fd);
	return f;
}

bool RegWriteAtomic(const char *data, size_t size)
{
	char leaf[256];
	int dir = PortUserParent("registry.ini", leaf, sizeof(leaf), true);
	if (dir < 0)
		return false;
	struct stat existing;
	if (fstatat(dir, leaf, &existing, AT_SYMLINK_NOFOLLOW) == 0)
	{
		if (!S_ISREG(existing.st_mode))
		{
			close(dir);
			return false;
		}
	}
	else if (errno != ENOENT)
	{
		close(dir);
		return false;
	}
	static unsigned long serial = 0;
	char tmp[256];
	int fd = -1;
	for (int attempt = 0; attempt < 16; ++attempt)
	{
		snprintf(tmp, sizeof(tmp), ".registry.ini.tmp.%ld.%lu", (long)getpid(), ++serial);
		fd = openat(dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd >= 0)
			break;
		if (errno != EEXIST)
		{
			close(dir);
			return false;
		}
	}
	if (fd < 0)
	{
		close(dir);
		return false;
	}
	size_t writtenTotal = 0;
	bool ok = true;
	while (writtenTotal < size)
	{
		ssize_t n = write(fd, data + writtenTotal, size - writtenTotal);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			ok = false;
			break;
		}
		writtenTotal += (size_t)n;
	}
	if (ok && fsync(fd) != 0)
		ok = false;
	if (close(fd) != 0)
		ok = false;
	if (ok && renameat(dir, tmp, dir, leaf) != 0)
		ok = false;
	if (!ok)
		unlinkat(dir, tmp, 0);
	else
	{
		int syncDir = openat(dir, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (syncDir >= 0)
		{
			fsync(syncDir);
			close(syncDir);
		}
	}
	close(dir);
	return ok;
}
#endif
}

extern "C" LONG WINAPI PortRegCreateKeyExA(HKEY, LPCSTR sub, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY result,
									   LPDWORD disposition)
{
	if (!result)
		return ERROR_INVALID_PARAMETER;
	RegKey *k = (RegKey *)calloc(1, sizeof(RegKey));
	if (!k)
		return ERROR_NOT_ENOUGH_MEMORY;
	snprintf(k->path, sizeof(k->path), "%s", sub ? sub : "");
	*result = (HKEY)k;
	if (disposition)
		*disposition = REG_CREATED_NEW_KEY;
	return ERROR_SUCCESS;
}

extern "C" LONG WINAPI PortRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD, REGSAM sam, PHKEY result)
{
	return PortRegCreateKeyExA(root, sub, 0, NULL, 0, sam, NULL, result, NULL);
}

extern "C" LONG WINAPI PortRegOpenKeyA(HKEY root, LPCSTR sub, PHKEY result) { return PortRegOpenKeyExA(root, sub, 0, 0, result); }

extern "C" LONG WINAPI PortRegCloseKey(HKEY k)
{
	free((RegKey *)k);
	return ERROR_SUCCESS;
}

// Values are stored as "key\name=type:hex bytes" lines; a write replaces the line atomically.
extern "C" LONG WINAPI PortRegSetValueExA(HKEY key, LPCSTR name, DWORD, DWORD type, const BYTE *data, DWORD size)
{
	RegKey *k = (RegKey *)key;
	if (!k || (size > 0 && !data))
		return ERROR_INVALID_PARAMETER;
	char prefix[512];
	snprintf(prefix, sizeof(prefix), "%s\\%s=", k->path, name ? name : "");
	const size_t prefixLen = strlen(prefix);
	std::string out;
	bool missing = false;
	if (FILE *f = RegOpenRead(&missing))
	{
		std::string line;
		while (ReadFullLine(f, &line))
		{
			if (line.size() < prefixLen || strncasecmp(line.c_str(), prefix, prefixLen) != 0)
			{
				out.append(line);
				if (out.back() != '\n')
					out.push_back('\n');
			}
		}
		bool failed = ferror(f) != 0;
		if (fclose(f) != 0 || failed)
			return ERROR_ACCESS_DENIED;
	}
	else if (!missing)
		return ERROR_ACCESS_DENIED;
	char hdr[544];
	int hdrLen = snprintf(hdr, sizeof(hdr), "%s%lu:", prefix, (unsigned long)type);
	if (hdrLen <= 0 || (size_t)hdrLen >= sizeof(hdr))
		return ERROR_INVALID_PARAMETER;
	out.append(hdr, (size_t)hdrLen);
	static const char kHex[] = "0123456789abcdef";
	out.reserve(out.size() + (size_t)size * 2 + 1);
	for (DWORD i = 0; i < size; i++)
	{
		out.push_back(kHex[data[i] >> 4]);
		out.push_back(kHex[data[i] & 0x0f]);
	}
	out.push_back('\n');
	return RegWriteAtomic(out.data(), out.size()) ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}

extern "C" LONG WINAPI PortRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size)
{
	RegKey *k = (RegKey *)key;
	if (!k)
		return ERROR_INVALID_PARAMETER;
	char prefix[512];
	snprintf(prefix, sizeof(prefix), "%s\\%s=", k->path, name ? name : "");
	const size_t prefixLen = strlen(prefix);
	FILE *f = RegOpenRead(NULL);
	if (!f)
		return ERROR_FILE_NOT_FOUND;
	std::string line;
	LONG result = ERROR_FILE_NOT_FOUND;
	while (ReadFullLine(f, &line))
		if (line.size() >= prefixLen && strncasecmp(line.c_str(), prefix, prefixLen) == 0)
		{
			const char *p = line.c_str() + prefixLen;
			char *end = NULL;
			unsigned long t = strtoul(p, &end, 10);
			p = end ? end : p;
			if (*p == ':')
				p++;
			DWORD n = 0;
			while (p[0] && p[1] && p[0] != '\n' && p[0] != '\r')
			{
				unsigned int b = 0;
				if (sscanf(p, "%2x", &b) != 1)
					break;
				if (data && size && n < *size)
					data[n] = (BYTE)b;
				n++;
				p += 2;
			}
			if (type)
				*type = (DWORD)t;
			result = data && size && n > *size ? 234 /* ERROR_MORE_DATA */ : ERROR_SUCCESS;
			if (size)
				*size = n;
			break;
		}
	fclose(f);
	return result;
}
