// SPDX-License-Identifier: GPL-3.0-only
// port/platform/windows/crashlog.cpp: the Windows build's crash report in onslaught-ce.log. Retail's own handler
// (ltshell.cpp ExceptionHandler, 00512040) only opens OnslaughtException.txt and returns, so a crash said nothing but
// its code. A vectored handler, installed before the game starts, sees each error-class exception (code 0xCxxxxxxx) on
// the thread that raised it, before any frame handler or the game's filter, and writes to the stderr handle the launcher
// copies into onslaught-ce.log: the code and its parameters (an access violation's read/write and address), the address
// as module+offset (and the nearest export below it, for system DLLs), the registers, the code bytes at the address
// (only when it lies in a loaded module's executable code), the frame-pointer chain and the stack words that point into
// a loaded module (a heuristic for frames built without a frame pointer). Players attach this log to public reports, so
// it never prints the game's data memory: what registers point at can hold paths with the account name (in ANSI or
// UTF-16, cut anywhere), which the launcher's line scrub cannot reliably find; only addresses, module names and code
// bytes are written. It returns EXCEPTION_CONTINUE_SEARCH, so the exception goes on exactly as before: a first-chance
// exception a frame handler takes (Direct3D and drivers may probe memory that way) is reported too and marked as
// first-chance; a report does not establish that the exception was fatal. At most kMaxReports are written, followed by
// one notice if more arrive. A later fatal exception can therefore be absent from this log.
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
const int kMaxReports = 8;
const int kChainFrames = 32;
const int kScanWords = 1024;		// 4 KB of stack from ESP
const int kScanHits = 48;
LONG gReports;

void Out(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	int n = _vsnprintf(line, sizeof line - 1, fmt, ap);
	va_end(ap);
	if (n < 0 || n > (int)sizeof line - 1)
		n = (int)strlen(line);
	DWORD written;
	HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
	if (err && err != INVALID_HANDLE_VALUE)
		WriteFile(err, line, (DWORD)n, &written, NULL);
}

bool Readable(const void *p, size_t size)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
		return false;
	if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
						 PAGE_EXECUTE_WRITECOPY)))
		return false;
	return (const char *)p + size <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

bool Executable(const void *p)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!p || !VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE)
		return false;
	return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// The nearest export at or below rva in the module's export directory, or NULL.
const char *NearestExport(HMODULE m, DWORD rva, DWORD *at)
{
	const BYTE *base = (const BYTE *)m;
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
	if (!Readable(dos, sizeof *dos) || dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
	if (!Readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE)
		return NULL;
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress || !dir.Size)
		return NULL;
	const IMAGE_EXPORT_DIRECTORY *ed = (const IMAGE_EXPORT_DIRECTORY *)(base + dir.VirtualAddress);
	if (!Readable(ed, sizeof *ed))
		return NULL;
	const DWORD *funcs = (const DWORD *)(base + ed->AddressOfFunctions);
	const DWORD *names = (const DWORD *)(base + ed->AddressOfNames);
	const WORD *ords = (const WORD *)(base + ed->AddressOfNameOrdinals);
	if (!Readable(funcs, ed->NumberOfFunctions * 4) || !Readable(names, ed->NumberOfNames * 4) ||
		!Readable(ords, ed->NumberOfNames * 2))
		return NULL;
	const char *best = NULL;
	DWORD bestRva = 0;
	for (DWORD i = 0; i < ed->NumberOfNames; i++)
	{
		if (ords[i] >= ed->NumberOfFunctions)
			continue;
		DWORD f = funcs[ords[i]];
		if (f <= rva && f >= bestRva && !(f >= dir.VirtualAddress && f < dir.VirtualAddress + dir.Size))
		{
			best = (const char *)(base + names[i]);
			bestRva = f;
		}
	}
	*at = bestRva;
	return best;
}

// "module+0xoffset [export+0xdelta] (base 0x...)" for an address, into buf.
void Describe(DWORD addr, char *buf, size_t size)
{
	HMODULE m = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							(LPCSTR)(UINT_PTR)addr, &m) || !m)
	{
		snprintf(buf, size, "%08lx (no module)", (unsigned long)addr);
		return;
	}
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(m, path, sizeof path);
	path[n < sizeof path ? n : sizeof path - 1] = 0;
	const char *name = strrchr(path, '\\');
	name = name ? name + 1 : path;
	DWORD rva = addr - (DWORD)(UINT_PTR)m, at = 0;
	const char *exp = NearestExport(m, rva, &at);
	if (exp && Readable(exp, 1))
		snprintf(buf, size, "%08lx %s+0x%lx [%.64s+0x%lx] (base %08lx)", (unsigned long)addr, name, (unsigned long)rva, exp,
				 (unsigned long)(rva - at), (unsigned long)(UINT_PTR)m);
	else
		snprintf(buf, size, "%08lx %s+0x%lx (base %08lx)", (unsigned long)addr, name, (unsigned long)rva,
				 (unsigned long)(UINT_PTR)m);
}

void Report(EXCEPTION_POINTERS *info, LONG k)
{
	const EXCEPTION_RECORD *r = info->ExceptionRecord;
	const CONTEXT *c = info->ContextRecord;
	char where[256];
	Describe((DWORD)(UINT_PTR)r->ExceptionAddress, where, sizeof where);
	Out("onslaught-ce: crash report %ld (first-chance; may be handled): exception "
		"%08lx thread %lu flags %lx\n", k, (unsigned long)r->ExceptionCode, (unsigned long)GetCurrentThreadId(),
		(unsigned long)r->ExceptionFlags);
	Out("onslaught-ce:   at %s\n", where);
	if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
		Out("onslaught-ce:   access violation: %s address %08lx\n",
			r->ExceptionInformation[0] == 0 ? "read" : r->ExceptionInformation[0] == 1 ? "write" : "execute",
			(unsigned long)r->ExceptionInformation[1]);
	else
		for (DWORD i = 0; i < r->NumberParameters && i < 4; i++)
			Out("onslaught-ce:   parameter %lu: %08lx\n", (unsigned long)i, (unsigned long)r->ExceptionInformation[i]);
	Out("onslaught-ce:   eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx\n", c->Eax, c->Ebx, c->Ecx, c->Edx,
		c->Esi, c->Edi);
	Out("onslaught-ce:   eip %08lx esp %08lx ebp %08lx eflags %08lx\n", c->Eip, c->Esp, c->Ebp, c->EFlags);
	{
		// The code bytes around the address, only from a module's executable code (never data: an execute fault through a
		// bad pointer can land in heap or stack memory).
		const BYTE *ip = (const BYTE *)(UINT_PTR)c->Eip;
		char hex[3 * 32 + 1];
		hex[0] = 0;
		if (Executable(ip - 16) && Executable(ip + 15) && Readable(ip - 16, 32))
		{
			for (int i = 0; i < 32; i++)
				snprintf(hex + 3 * i, 4, "%02x ", ip[i - 16]);
			Out("onslaught-ce:   code eip-16..eip+15: %s\n", hex);
		}
		else if (Executable(ip) && Executable(ip + 15) && Readable(ip, 16))
		{
			for (int i = 0; i < 16; i++)
				snprintf(hex + 3 * i, 4, "%02x ", ip[i]);
			Out("onslaught-ce:   code eip..eip+15: %s\n", hex);
		}
		else
			Out("onslaught-ce:   code: the address is not in a module's executable code\n");
	}
	NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
	DWORD lo = (DWORD)(UINT_PTR)tib->StackLimit, hi = (DWORD)(UINT_PTR)tib->StackBase;
	Out("onslaught-ce:   stack %08lx..%08lx; frame-pointer chain:\n", (unsigned long)lo, (unsigned long)hi);
	DWORD ebp = c->Ebp;
	for (int i = 0; i < kChainFrames; i++)
	{
		if (ebp < lo || ebp + 8 > hi || (ebp & 3) || !Readable((void *)(UINT_PTR)ebp, 8))
			break;
		DWORD *f = (DWORD *)(UINT_PTR)ebp;
		DWORD ret = f[1];
		if (!ret)
			break;
		Describe(ret, where, sizeof where);
		Out("onslaught-ce:     #%02d %s\n", i, where);
		if (f[0] <= ebp)
			break;
		ebp = f[0];
	}
	Out("onslaught-ce:   stack words pointing into code (from esp, heuristic):\n");
	int hits = 0;
	for (DWORD a = c->Esp & ~3u; a + 4 <= hi && a < (c->Esp & ~3u) + kScanWords * 4 && hits < kScanHits; a += 4)
	{
		if (a < lo || !Readable((void *)(UINT_PTR)a, 4))
			break;
		DWORD v = *(DWORD *)(UINT_PTR)a;
		if (!Executable((void *)(UINT_PTR)v))
			continue;
		Describe(v, where, sizeof where);
		Out("onslaught-ce:     [esp+%04lx] %s\n", (unsigned long)(a - c->Esp), where);
		hits++;
	}
}

LONG CALLBACK Handler(EXCEPTION_POINTERS *info)
{
	const EXCEPTION_RECORD *r = info->ExceptionRecord;
	if (((DWORD)r->ExceptionCode >> 28) != 0xC)
		return EXCEPTION_CONTINUE_SEARCH;		// informational, warning and C++ (0xE06D7363) exceptions pass silently
	LONG k = InterlockedIncrement(&gReports);
	if (k > kMaxReports)
	{
		if (k == kMaxReports + 1)
			Out("onslaught-ce: further first-chance exception reports suppressed after %d; a later fatal exception may not "
				"be reported\n", kMaxReports);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	Report(info, k);
	return EXCEPTION_CONTINUE_SEARCH;
}

struct Install
{
	Install() { AddVectoredExceptionHandler(1, Handler); }
} gInstall;
}
