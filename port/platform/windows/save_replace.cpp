// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/save_replace.cpp (patch 0117): all-or-nothing replacement of one file in the port's writable folder
// (port/platform/common/save_replace.h). The new bytes go to a new file in the same folder, are flushed and read back, and
// MoveFileExW then puts them over the target in one step (write-through). The previous file is never opened for writing, so
// no failure or interruption can leave it short or empty. A crash between the write and the move leaves one ".ce-write-*.tmp"
// file, which is never listed as a career (careers are the *.bes entries).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../common/platform.h"
#include "../common/portlog.h"
#include "../common/save_replace.h"

namespace
{
const char *Reason(int step)
{
	switch (step)
	{
	case PORT_REPLACE_REFUSED: return "the file cannot be written";
	case PORT_REPLACE_CREATE: return "no temporary file";
	case PORT_REPLACE_WRITE: return "writing failed";
	case PORT_REPLACE_SYNC: return "flushing to disk failed";
	case PORT_REPLACE_VERIFY: return "the bytes read back differ";
	case PORT_REPLACE_PUBLISH: return "the move failed";
	}
	return "failed";
}

int Fail(int step, DWORD error, const char *what)
{
	fprintf(stderr, PORT_LOG_PREFIX "%s was not saved: %s (Windows error %lu); the previous file is unchanged\n", what ? what : "file",
			Reason(step), (unsigned long)error);
	SetLastError(error);
	return step;
}

DWORD Last(DWORD fallback)
{
	DWORD error = GetLastError();
	return error ? error : fallback;
}

// True when `inner` is `outer` or lies beneath it. Both are UTF-8 paths in any slash style and any case. A path that cannot be
// made absolute counts as inside, so the answer errs towards not writing. (Links and junctions are not followed.)
bool Inside(const char *inner, const char *outer)
{
	const DWORD room = MAX_PATH * 2;
	WCHAR a[room], b[room], fa[room], fb[room];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inner, -1, a, room) ||
		!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, outer, -1, b, room))
		return true;
	DWORD la = GetFullPathNameW(a, room, fa, NULL), lb = GetFullPathNameW(b, room, fb, NULL);
	if (!la || la >= room || !lb || lb >= room)
		return true;
	while (lb > 3 && fb[lb - 1] == L'\\')
		fb[--lb] = 0;
	if (la < lb || CompareStringOrdinal(fa, (int)lb, fb, (int)lb, TRUE) != CSTR_EQUAL)
		return false;
	return fa[lb] == 0 || fa[lb] == L'\\' || fb[lb - 1] == L'\\';
}

// The bytes of h, read again from its start, equal the buffer.
bool ReadsBack(HANDLE h, const unsigned char *bytes, unsigned size)
{
	LARGE_INTEGER length, start;
	start.QuadPart = 0;
	if (!GetFileSizeEx(h, &length) || length.QuadPart != (LONGLONG)size || !SetFilePointerEx(h, start, NULL, FILE_BEGIN))
		return false;
	unsigned char back[65536];
	for (unsigned at = 0; at < size;)
	{
		DWORD want = size - at < sizeof(back) ? size - at : (DWORD)sizeof(back), got = 0;
		if (!ReadFile(h, back, want, &got, NULL) || !got || memcmp(back, bytes + at, got) != 0)
			return false;
		at += got;
	}
	return true;
}
}

int PortReplaceUserFile(const char *game_path, const void *data, unsigned size, const char *what)
{
	const unsigned char *bytes = (const unsigned char *)data;
	char path[MAX_PATH * 2];
	WCHAR target[MAX_PATH * 2], temp[MAX_PATH * 2];
	// A career or an options file is never empty: replacing one with nothing could only be a caller's mistake. (Retail's
	// fwrite of zero bytes reported failure too.)
	if (!data || !size)
		return Fail(PORT_REPLACE_REFUSED, ERROR_INVALID_PARAMETER, what);
	// The player's game folder is never written: a writable folder that is, or lies inside, the game folder is refused (the Linux
	// path layer refuses the same). Everything else this file's callers write is checked by the path layer's own rules.
	if (Inside(PortUserDir(), PortGameDir()))
	{
		static bool said;
		if (!said)
			fprintf(stderr, PORT_LOG_PREFIX "the writable folder is, or is inside, the game folder; set BEA_USER_DIR to a separate folder\n");
		said = true;
		return Fail(PORT_REPLACE_REFUSED, ERROR_ACCESS_DENIED, what);
	}
	if (PortResolveUserPath(game_path, path, sizeof(path)) != 0)
		return Fail(PORT_REPLACE_REFUSED, ERROR_INVALID_NAME, what);
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, target, sizeof(target) / sizeof(target[0])))
		return Fail(PORT_REPLACE_REFUSED, Last(ERROR_INVALID_NAME), what);

	// An existing target must not be a folder, and a read-only career stays protected as it was when opening it for writing
	// failed. A link or placeholder file is replaced itself, never written through.
	DWORD attributes = GetFileAttributesW(target);
	if (attributes != INVALID_FILE_ATTRIBUTES)
	{
		if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))
			return Fail(PORT_REPLACE_REFUSED, ERROR_ACCESS_DENIED, what);
	}
	else
	{
		DWORD error = GetLastError();
		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			return Fail(PORT_REPLACE_REFUSED, error, what);
	}

	// The temporary file lives beside the target, so the move never crosses a volume.
	WCHAR *slash = wcsrchr(target, L'\\');
	size_t folder = slash ? (size_t)(slash - target) + 1 : 0;
	static unsigned sequence;
	HANDLE h = INVALID_HANDLE_VALUE;
	for (int tries = 0; tries < 16 && h == INVALID_HANDLE_VALUE; tries++)
	{
		WCHAR name[48];
		_snwprintf(name, sizeof(name) / sizeof(name[0]), L".ce-write-%lu-%u.tmp", (unsigned long)GetCurrentProcessId(),
				   (unsigned)InterlockedIncrement((volatile LONG *)&sequence));
		if (folder + wcslen(name) >= sizeof(temp) / sizeof(temp[0]))
			return Fail(PORT_REPLACE_CREATE, ERROR_FILENAME_EXCED_RANGE, what);
		memcpy(temp, target, folder * sizeof(WCHAR));
		wcscpy(temp + folder, name);
		h = CreateFileW(temp, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
			break;
	}
	if (h == INVALID_HANDLE_VALUE)
		return Fail(PORT_REPLACE_CREATE, Last(ERROR_GEN_FAILURE), what);

	int step = PORT_REPLACE_OK;
	DWORD error = 0;
	for (unsigned done = 0; done < size && !step;)
	{
		DWORD put = 0;
		if (!WriteFile(h, bytes + done, size - done, &put, NULL) || !put)
			step = PORT_REPLACE_WRITE, error = Last(ERROR_WRITE_FAULT);
		else
			done += put;
	}
	if (!step && !FlushFileBuffers(h))
		step = PORT_REPLACE_SYNC, error = Last(ERROR_GEN_FAILURE);
	if (!step && !ReadsBack(h, bytes, size))
		step = PORT_REPLACE_VERIFY, error = ERROR_CRC;
	// Close before the move: a rename needs the file free, and a failed close is a failed write.
	if (!CloseHandle(h) && !step)
		step = PORT_REPLACE_WRITE, error = Last(ERROR_GEN_FAILURE);
	// A virus scanner or indexer can hold the target for a moment; the move is retried briefly, never forced.
	for (int attempt = 0; !step; attempt++)
	{
		if (MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			break;
		error = Last(ERROR_GEN_FAILURE);
		if (attempt >= 7 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION))
		{
			step = PORT_REPLACE_PUBLISH;
			break;
		}
		Sleep(25);
	}
	if (step)
	{
		// The temporary file was created above with CREATE_NEW, so it is ours to remove, and only it.
		if (!DeleteFileW(temp))
			fprintf(stderr, PORT_LOG_PREFIX "a temporary file could not be removed from the writable folder\n");
		return Fail(step, error, what);
	}
	return PORT_REPLACE_OK;
}
