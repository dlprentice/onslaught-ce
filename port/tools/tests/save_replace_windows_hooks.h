// SPDX-License-Identifier: GPL-3.0-only
// Test-only fault injection for port/tools/save_replace_windows_tests.py. It is force-included (-include) when, and only when,
// port/platform/windows/save_replace.cpp is compiled into the test executable; no port build sees it, and the production file
// reads no fault variable. It redefines the Win32 calls that file makes (CreateFileW, WriteFile, FlushFileBuffers, ReadFile,
// CloseHandle for the temporary file, MoveFileExW, DeleteFileW, Sleep) as the Hook_* functions below. Each consults the fault
// plan in the environment variable SAVE_REPLACE_FAULTS and otherwise calls the real API.
//
//   SAVE_REPLACE_FAULTS = rule[;rule...]        rule = OP:WHEN:ACTION[:ARG]
//   OP     create write flush read close move delete sleep
//   WHEN   N (the Nth call of that OP in this process, counting from 1), N-M, N- (the Nth and every later one), or *
//   ACTION fail:CODE   the call fails with Win32 error CODE (decimal or 0x..) and does nothing (close really closes first)
//          crash       TerminateProcess(GetCurrentProcess(), 99) before the call
//          write only:  half (write half of the request, report that), limit:N (at most N bytes), zero (report success with
//                       0 bytes), drop (report all bytes written, write none), corrupt (write one byte of the chunk changed)
//          read only:   flip (the real read, then one byte of what it returned changed), eof (report success with 0 bytes)
//
// SaveReplaceHookReport() prints how often each OP was called (and the milliseconds slept), so a test can check, for example,
// that a refused rename was tried eight times with seven sleeps in between. It also reports the access, share mode and
// disposition of the last CreateFileW and the flags of the last MoveFileExW (and whether both its names are in one folder):
// what Wine cannot show the effect of (an exclusive CREATE_NEW temporary file, MOVEFILE_WRITE_THROUGH) is at least asserted.
#ifndef SAVE_REPLACE_WINDOWS_HOOKS_H
#define SAVE_REPLACE_WINDOWS_HOOKS_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace SaveReplaceHooks
{
enum Op { OP_CREATE, OP_WRITE, OP_FLUSH, OP_READ, OP_CLOSE, OP_MOVE, OP_DELETE, OP_SLEEP, OP_COUNT };
enum Action { ACT_FAIL, ACT_CRASH, ACT_HALF, ACT_LIMIT, ACT_ZERO, ACT_DROP, ACT_CORRUPT, ACT_FLIP, ACT_EOF };

struct Rule
{
	int op;
	unsigned from, to;	// to == 0: no upper bound
	int action;
	unsigned long arg;
};

Rule gRules[32];
int gRuleCount;
bool gLoaded;
volatile LONG gCalls[OP_COUNT];
volatile LONG gSleptMs;
HANDLE gTemp;	// the one file this translation unit creates
// The arguments of the last CreateFileW and MoveFileExW call, for the parts of the contract Wine cannot show the effect of.
DWORD gCreateAccess, gCreateShare, gCreateDisposition, gMoveFlags;
int gMoveSameFolder = -1;

void Die(const char *what, const char *rule)
{
	fprintf(stderr, "save_replace hooks: bad SAVE_REPLACE_FAULTS (%s): %s\n", what, rule);
	fflush(stderr);
	ExitProcess(98);
}

void Parse(char *rule)
{
	static const char *const ops[OP_COUNT] = {"create", "write", "flush", "read", "close", "move", "delete", "sleep"};
	static const char *const actions[] = {"fail", "crash", "half", "limit", "zero", "drop", "corrupt", "flip", "eof"};
	char copy[128];
	strncpy(copy, rule, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = 0;
	char *part[4] = {0, 0, 0, 0};
	int n = 0;
	for (char *p = rule; n < 4;)
	{
		part[n++] = p;
		char *colon = strchr(p, ':');
		if (!colon)
			break;
		*colon = 0;
		p = colon + 1;
	}
	if (n < 3 || gRuleCount >= (int)(sizeof(gRules) / sizeof(gRules[0])))
		Die("shape", copy);
	Rule r = {};
	r.op = -1;
	for (int i = 0; i < OP_COUNT; i++)
		if (!strcmp(part[0], ops[i]))
			r.op = i;
	r.action = -1;
	for (int i = 0; i < (int)(sizeof(actions) / sizeof(actions[0])); i++)
		if (!strcmp(part[2], actions[i]))
			r.action = i;
	if (r.op < 0 || r.action < 0)
		Die("operation or action", copy);
	if ((r.action == ACT_FAIL || r.action == ACT_LIMIT) && n < 4)
		Die("missing argument", copy);
	if (n == 4)
		r.arg = strtoul(part[3], NULL, 0);
	if (!strcmp(part[1], "*"))
		r.from = 1, r.to = 0;
	else
	{
		char *end = NULL;
		r.from = strtoul(part[1], &end, 10);
		if (end && *end == '-')
			r.to = end[1] ? strtoul(end + 1, NULL, 10) : 0;
		else
			r.to = r.from;
		if (!r.from)
			Die("call numbers count from 1", copy);
	}
	gRules[gRuleCount++] = r;
}

void Load()
{
	if (gLoaded)
		return;
	gLoaded = true;
	const char *text = getenv("SAVE_REPLACE_FAULTS");
	if (!text || !*text)
		return;
	char buffer[512];
	if (strlen(text) >= sizeof(buffer))
		Die("too long", text);
	strcpy(buffer, text);
	for (char *rule = buffer;;)
	{
		char *end = strchr(rule, ';');
		if (end)
			*end = 0;
		if (*rule)
			Parse(rule);
		if (!end)
			break;
		rule = end + 1;
	}
}

// Counts this call of op and returns the rule that applies to it, if any.
const Rule *Hit(int op)
{
	Load();
	unsigned n = (unsigned)InterlockedIncrement(&gCalls[op]);
	for (int i = 0; i < gRuleCount; i++)
		if (gRules[i].op == op && n >= gRules[i].from && (!gRules[i].to || n <= gRules[i].to))
			return &gRules[i];
	return NULL;
}

bool SameFolder(LPCWSTR a, LPCWSTR b)
{
	const WCHAR *sa = wcsrchr(a, L'\\'), *sb = wcsrchr(b, L'\\');
	return sa && sb && sa - a == sb - b && _wcsnicmp(a, b, (size_t)(sa - a)) == 0;
}

void Crash()
{
	TerminateProcess(GetCurrentProcess(), 99);
	ExitProcess(99);
}

HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
							   DWORD flags, HANDLE templ)
{
	const Rule *r = Hit(OP_CREATE);
	gCreateAccess = access, gCreateShare = share, gCreateDisposition = disposition;
	if (r && r->action == ACT_CRASH)
		Crash();
	if (r && r->action == ACT_FAIL)
	{
		SetLastError(r->arg);
		return INVALID_HANDLE_VALUE;
	}
	HANDLE h = CreateFileW(name, access, share, security, disposition, flags, templ);
	if (h != INVALID_HANDLE_VALUE)
		gTemp = h;
	return h;
}

BOOL WINAPI Hook_WriteFile(HANDLE h, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED overlapped)
{
	const Rule *r = Hit(OP_WRITE);
	if (!r)
		return WriteFile(h, data, size, written, overlapped);
	switch (r->action)
	{
	case ACT_CRASH:
		Crash();
		break;
	case ACT_FAIL:
		if (written)
			*written = 0;
		SetLastError(r->arg);
		return FALSE;
	case ACT_ZERO:
		if (written)
			*written = 0;
		return TRUE;
	case ACT_DROP:
		if (written)
			*written = size;
		return TRUE;
	case ACT_HALF:
		if (size > 1)
			size /= 2;
		break;
	case ACT_LIMIT:
		if (size > r->arg)
			size = r->arg;
		break;
	case ACT_CORRUPT:
		if (size)
		{
			unsigned char *copy = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, size);
			memcpy(copy, data, size);
			copy[size / 2] ^= 0xFF;
			BOOL ok = WriteFile(h, copy, size, written, overlapped);
			HeapFree(GetProcessHeap(), 0, copy);
			return ok;
		}
		break;
	}
	return WriteFile(h, data, size, written, overlapped);
}

BOOL WINAPI Hook_FlushFileBuffers(HANDLE h)
{
	const Rule *r = Hit(OP_FLUSH);
	if (r && r->action == ACT_CRASH)
		Crash();
	if (r && r->action == ACT_FAIL)
	{
		SetLastError(r->arg);
		return FALSE;
	}
	return FlushFileBuffers(h);
}

BOOL WINAPI Hook_ReadFile(HANDLE h, LPVOID buffer, DWORD size, LPDWORD got, LPOVERLAPPED overlapped)
{
	const Rule *r = Hit(OP_READ);
	if (r && r->action == ACT_CRASH)
		Crash();
	if (r && r->action == ACT_FAIL)
	{
		if (got)
			*got = 0;
		SetLastError(r->arg);
		return FALSE;
	}
	if (r && r->action == ACT_EOF)
	{
		if (got)
			*got = 0;
		return TRUE;
	}
	BOOL ok = ReadFile(h, buffer, size, got, overlapped);
	if (ok && r && r->action == ACT_FLIP && got && *got)
		((unsigned char *)buffer)[*got / 2] ^= 0x01;
	return ok;
}

// Only the temporary file's handle is ever faulted. A faulted close still really closes it, so the file can be deleted.
BOOL WINAPI Hook_CloseHandle(HANDLE h)
{
	if (!h || h != gTemp)
		return CloseHandle(h);
	const Rule *r = Hit(OP_CLOSE);
	if (r && r->action == ACT_CRASH)
		Crash();
	BOOL ok = CloseHandle(h);
	gTemp = NULL;
	if (r && r->action == ACT_FAIL)
	{
		SetLastError(r->arg);
		return FALSE;
	}
	return ok;
}

BOOL WINAPI Hook_MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
	const Rule *r = Hit(OP_MOVE);
	gMoveFlags = flags, gMoveSameFolder = SameFolder(from, to) ? 1 : 0;
	if (r && r->action == ACT_CRASH)
		Crash();
	if (r && r->action == ACT_FAIL)
	{
		SetLastError(r->arg);
		return FALSE;
	}
	return MoveFileExW(from, to, flags);
}

BOOL WINAPI Hook_DeleteFileW(LPCWSTR name)
{
	const Rule *r = Hit(OP_DELETE);
	if (r && r->action == ACT_CRASH)
		Crash();
	if (r && r->action == ACT_FAIL)
	{
		SetLastError(r->arg);
		return FALSE;
	}
	return DeleteFileW(name);
}

VOID WINAPI Hook_Sleep(DWORD ms)
{
	Hit(OP_SLEEP);
	InterlockedExchangeAdd(&gSleptMs, (LONG)ms);
	Sleep(ms);
}
}

extern "C" void SaveReplaceHookReport(char *out, size_t size)
{
	using namespace SaveReplaceHooks;
	snprintf(out, size,
			 "create=%ld write=%ld flush=%ld read=%ld close=%ld move=%ld delete=%ld sleep=%ld slept_ms=%ld create_access=%lu "
			 "create_share=%lu create_disposition=%lu move_flags=%lu move_same_folder=%d",
			 (long)gCalls[OP_CREATE], (long)gCalls[OP_WRITE], (long)gCalls[OP_FLUSH], (long)gCalls[OP_READ],
			 (long)gCalls[OP_CLOSE], (long)gCalls[OP_MOVE], (long)gCalls[OP_DELETE], (long)gCalls[OP_SLEEP], (long)gSleptMs,
			 (unsigned long)gCreateAccess, (unsigned long)gCreateShare, (unsigned long)gCreateDisposition,
			 (unsigned long)gMoveFlags, gMoveSameFolder);
}

// From here on, save_replace.cpp's calls land in the hooks above (they are defined first, so they reach the real functions).
#define CreateFileW SaveReplaceHooks::Hook_CreateFileW
#define WriteFile SaveReplaceHooks::Hook_WriteFile
#define FlushFileBuffers SaveReplaceHooks::Hook_FlushFileBuffers
#define ReadFile SaveReplaceHooks::Hook_ReadFile
#define CloseHandle SaveReplaceHooks::Hook_CloseHandle
#define MoveFileExW SaveReplaceHooks::Hook_MoveFileExW
#define DeleteFileW SaveReplaceHooks::Hook_DeleteFileW
#define Sleep SaveReplaceHooks::Hook_Sleep

#endif
