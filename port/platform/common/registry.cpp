// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/registry.cpp: the game's registry calls (PCPlatform.cpp writes one volatile value under
// HKEY_CURRENT_USER\Software\Lost Toys\Battle Engine Aquila), on both targets, as an ini file in the port's writable
// folder (registry.ini), so the port writes nothing outside it. port.h renames the game's Reg* calls to these.
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define strncasecmp _strnicmp
#endif

#include "platform.h"
#if !defined(_WIN32)
#include "../linux/handles.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
struct RegKey
{
	char path[256];
};
}

static void RegFile(char *out, size_t n) { snprintf(out, n, "%s/registry.ini", PortUserDir()); }

static FILE *RegOpen(const char *mode)
{
#if defined(_WIN32)
	char file[4096];
	RegFile(file, sizeof(file));
	return fopen(file, mode);
#else
	// Both operations use the same user-only identity, including its spelling. Reading must not fall back to the game.
	int flags = mode[0] == 'r' ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
	int fd = PortOpenUserFile("registry.ini", flags, false);
	if (fd < 0)
		return NULL;
	FILE *f = fdopen(fd, mode);
	if (!f)
		close(fd);
	return f;
#endif
}

extern "C" LONG WINAPI PortRegCreateKeyExA(HKEY, LPCSTR sub, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY result,
									   LPDWORD disposition)
{
	RegKey *k = (RegKey *)calloc(1, sizeof(RegKey));
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

// Values are stored as "key\name=type:hex bytes" lines; a write replaces the line.
extern "C" LONG WINAPI PortRegSetValueExA(HKEY key, LPCSTR name, DWORD, DWORD type, const BYTE *data, DWORD size)
{
	RegKey *k = (RegKey *)key;
	char prefix[512];
	snprintf(prefix, sizeof(prefix), "%s\\%s=", k->path, name ? name : "");
	char *kept = NULL;
	size_t kept_len = 0;
	if (FILE *f = RegOpen("r"))
	{
		char line[8192];
		while (fgets(line, sizeof(line), f))
			if (strncasecmp(line, prefix, strlen(prefix)) != 0)
			{
				size_t l = strlen(line);
				kept = (char *)realloc(kept, kept_len + l + 1);
				memcpy(kept + kept_len, line, l + 1);
				kept_len += l;
			}
		bool failed = ferror(f) != 0;
		if (fclose(f) != 0 || failed)
		{
			free(kept);
			return ERROR_ACCESS_DENIED;
		}
	}
#if !defined(_WIN32)
	else if (errno != ENOENT)
		return ERROR_ACCESS_DENIED;
#endif
	FILE *f = RegOpen("w");
	if (!f)
	{
		free(kept);
		return ERROR_ACCESS_DENIED;
	}
	if (kept)
		fputs(kept, f);
	fprintf(f, "%s%lu:", prefix, (unsigned long)type);
	for (DWORD i = 0; i < size; i++)
		fprintf(f, "%02x", data[i]);
	fputc('\n', f);
	fclose(f);
	free(kept);
	return ERROR_SUCCESS;
}

extern "C" LONG WINAPI PortRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size)
{
	RegKey *k = (RegKey *)key;
	char prefix[512];
	snprintf(prefix, sizeof(prefix), "%s\\%s=", k->path, name ? name : "");
	FILE *f = RegOpen("r");
	if (!f)
		return ERROR_FILE_NOT_FOUND;
	char line[8192];
	LONG result = ERROR_FILE_NOT_FOUND;
	while (fgets(line, sizeof(line), f))
		if (strncasecmp(line, prefix, strlen(prefix)) == 0)
		{
			char *p = line + strlen(prefix);
			unsigned long t = strtoul(p, &p, 10);
			if (*p == ':')
				p++;
			DWORD n = 0;
			while (p[0] && p[1] && p[0] != '\n')
			{
				unsigned int b;
				sscanf(p, "%2x", &b);
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
