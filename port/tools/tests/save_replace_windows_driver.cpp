// SPDX-License-Identifier: GPL-3.0-only
// Driver for port/tools/save_replace_windows_tests.py: the real Windows path layer (files.cpp), save-name codec and atomic
// replace (save_replace.cpp), built for i686 Windows and run under Wine. Fault injection is not here: it lives in
// save_replace_windows_hooks.h, which is force-included only into save_replace.cpp. BEA_GAME_DIR and BEA_USER_DIR come from
// the environment, as for the game. Every command prints one "RESULT key=value ..." line (or a few) on standard output.
//
//   info                                          the roots files.cpp resolved, the process id and the ANSI code page
//   replace GAME_PATH PAYLOAD [WHAT]              PortReplaceUserFile over the payload file's bytes
//   collide GAME_PATH PAYLOAD N                   first plant N files with the temp names this process would pick, then replace
//   hold GAME_PATH PAYLOAD PROBE_DIR RELEASE_MS   hold the target open without FILE_SHARE_DELETE (closed after RELEASE_MS from
//                                                 another thread, or after the call when negative) while replacing it
//   writer GAME_PATH PAYLOAD_A PAYLOAD_B N        N replaces alternating B, A, B, ...
//   reader PATH PAYLOAD_A PAYLOAD_B READY STOP PAUSE_MS   open, read fully and classify PATH in a loop until the STOP file exists
//   read GAME_PATH OUT_FILE                       the file as the game reads it (port_fopen, both roots), copied to OUT_FILE
//   attrib NATIVE_PATH ro|rw|query                set, clear or read the read-only attribute of one native file
//   list SPEC                                     the game's own search (port_findfirst) over both roots
//   rawlist PATTERN                               the native FindFirstFileW over one folder
#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "save_replace.h"

extern "C" FILE *port_fopen(const char *name, const char *mode);
extern "C" intptr_t port_findfirst(const char *spec, struct _finddata_t *d);
extern "C" int port_findnext(intptr_t h, struct _finddata_t *d);
extern "C" int port_findclose(intptr_t h);
extern "C" void SaveReplaceHookReport(char *out, size_t size);
const char *PortGameDir();
const char *PortUserDir();

namespace
{
typedef std::vector<unsigned char> Bytes;

[[noreturn]] void Die(const char *what, DWORD error = 0)
{
	fprintf(stderr, "driver: %s (Windows error %lu)\n", what, (unsigned long)(error ? error : GetLastError()));
	fflush(stderr);
	ExitProcess(3);
}

std::string Utf8(const wchar_t *wide)
{
	int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
	if (n <= 0)
		Die("argument is not valid UTF-16");
	std::string out(n, 0);
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, &out[0], n, NULL, NULL);
	out.resize(n - 1);
	return out;
}

std::wstring Wide(const std::string &utf8)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, NULL, 0);
	if (n <= 0)
		Die("string is not valid UTF-8");
	std::wstring out(n, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, &out[0], n);
	out.resize(n - 1);
	return out;
}

bool Slurp(const std::wstring &path, Bytes &out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
						   FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size;
	if (!GetFileSizeEx(h, &size) || size.QuadPart > (1LL << 28))
	{
		CloseHandle(h);
		return false;
	}
	out.resize((size_t)size.QuadPart);
	for (size_t at = 0; at < out.size();)
	{
		DWORD got = 0;
		if (!ReadFile(h, &out[at], (DWORD)(out.size() - at), &got, NULL) || !got)
		{
			CloseHandle(h);
			return false;
		}
		at += got;
	}
	return CloseHandle(h) != 0;
}

// Writes a whole new file with the given disposition (CREATE_NEW or CREATE_ALWAYS).
bool Spit(const std::wstring &path, const void *data, size_t size, DWORD disposition)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	bool ok = true;
	for (size_t at = 0; ok && at < size;)
	{
		DWORD put = 0;
		ok = WriteFile(h, (const char *)data + at, (DWORD)(size - at), &put, NULL) && put;
		at += put;
	}
	return CloseHandle(h) && ok;
}

Bytes Load(const wchar_t *path)
{
	Bytes out;
	if (!Slurp(path, out))
		Die("cannot read a payload file");
	return out;
}

// Milliseconds on the performance counter (GetTickCount64 only moves every 15 ms or so).
double NowMs()
{
	static LARGE_INTEGER frequency;
	if (!frequency.QuadPart)
		QueryPerformanceFrequency(&frequency);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return 1000.0 * (double)now.QuadPart / (double)frequency.QuadPart;
}

std::string Hooks()
{
	char text[512];
	SaveReplaceHookReport(text, sizeof(text));
	return text;
}

int Replace(const std::string &gamePath, const Bytes &bytes, const char *what, const std::string &extra = std::string())
{
	static const unsigned char none = 0;
	double start = NowMs();
	int result = PortReplaceUserFile(gamePath.c_str(), bytes.empty() ? &none : bytes.data(), (unsigned)bytes.size(), what);
	DWORD error = GetLastError();
	long long elapsed = (long long)(NowMs() - start);
	printf("RESULT result=%d lasterror=%lu elapsed_ms=%lld pid=%lu %s%s%s\n", result, (unsigned long)error, elapsed,
		   (unsigned long)GetCurrentProcessId(), Hooks().c_str(), extra.empty() ? "" : " ", extra.c_str());
	fflush(stdout);
	return 0;
}

// The native path of the file a write to gamePath reaches (PortResolveUserPath is the production resolver).
std::wstring Native(const std::string &gamePath)
{
	char path[1040];
	if (PortResolveUserPath(gamePath.c_str(), path, sizeof(path)) != 0)
		Die("PortResolveUserPath refused the path", ERROR_INVALID_NAME);
	return Wide(path);
}

int Collide(const std::string &gamePath, const Bytes &bytes, unsigned count)
{
	std::wstring target = Native(gamePath);
	std::wstring folder = target.substr(0, target.rfind(L'\\') + 1);
	for (unsigned k = 1; k <= count; k++)
	{
		wchar_t name[64];
		_snwprintf(name, 64, L".ce-write-%lu-%u.tmp", (unsigned long)GetCurrentProcessId(), k);
		std::string content = "STALE-" + std::to_string(k);
		if (!Spit(folder + name, content.data(), content.size(), CREATE_NEW))
			Die("cannot plant a stale temporary file");
	}
	return Replace(gamePath, bytes, "career", "planted=" + std::to_string(count));
}

struct Release
{
	HANDLE handle;
	DWORD ms;
	double at;
};

DWORD WINAPI ReleaseThread(LPVOID parameter)
{
	Release *release = (Release *)parameter;
	Sleep(release->ms);
	release->at = NowMs();
	CloseHandle(release->handle);
	return 0;
}

// Does this system refuse to move a file over a destination that another handle holds open without FILE_SHARE_DELETE (as
// Windows does with ERROR_ACCESS_DENIED or ERROR_SHARING_VIOLATION)? Asked on two scratch files, never on the target.
const char *ProbeSharing(const std::wstring &folder, DWORD *error)
{
	std::wstring held = folder + L"\\probe-held.bin", fresh = folder + L"\\probe-new.bin";
	if (!Spit(held, "HELD", 4, CREATE_ALWAYS) || !Spit(fresh, "NEW", 3, CREATE_ALWAYS))
		Die("cannot write the sharing probe files");
	HANDLE h = CreateFileW(held.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
						   NULL);
	if (h == INVALID_HANDLE_VALUE)
		Die("cannot hold the sharing probe file");
	SetLastError(0);
	BOOL moved = MoveFileExW(fresh.c_str(), held.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	*error = moved ? 0 : GetLastError();
	CloseHandle(h);
	return moved ? "not-enforced" : "enforced";
}

int Hold(const std::string &gamePath, const Bytes &bytes, const std::wstring &probeFolder, long releaseMs)
{
	DWORD probeError = 0;
	const char *probe = ProbeSharing(probeFolder, &probeError);
	std::wstring target = Native(gamePath);
	HANDLE h = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
						   FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		Die("cannot hold the target open");
	Release release = {h, (DWORD)(releaseMs < 0 ? 0 : releaseMs), 0};
	HANDLE thread = NULL;
	double start = NowMs();
	if (releaseMs >= 0)
		thread = CreateThread(NULL, 0, ReleaseThread, &release, 0, NULL);
	if (releaseMs >= 0 && !thread)
		Die("cannot start the releasing thread");
	std::string extra = std::string("probe=") + probe + " probe_error=" + std::to_string(probeError);
	static const unsigned char none = 0;
	int result = PortReplaceUserFile(gamePath.c_str(), bytes.empty() ? &none : bytes.data(), (unsigned)bytes.size(), "career");
	DWORD error = GetLastError();
	long long elapsed = (long long)(NowMs() - start);
	if (thread)
	{
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
		extra += " released_at_ms=" + std::to_string((long long)(release.at - start));
	}
	else
	{
		CloseHandle(h);
		extra += " released_at_ms=-1";
	}
	printf("RESULT result=%d lasterror=%lu elapsed_ms=%lld pid=%lu %s %s\n", result, (unsigned long)error, elapsed,
		   (unsigned long)GetCurrentProcessId(), Hooks().c_str(), extra.c_str());
	fflush(stdout);
	return 0;
}

int Writer(const std::string &gamePath, const Bytes &a, const Bytes &b, unsigned count)
{
	unsigned ok = 0, publish = 0, other = 0;
	char last = '-';
	std::string codes;
	double start = NowMs();
	for (unsigned i = 0; i < count; i++)
	{
		const Bytes &payload = i % 2 ? a : b;
		int result = PortReplaceUserFile(gamePath.c_str(), payload.data(), (unsigned)payload.size(), "career");
		if (result == PORT_REPLACE_OK)
			ok++, last = i % 2 ? 'A' : 'B';
		else if (result == PORT_REPLACE_PUBLISH)
			publish++;
		else
			other++, codes += " code" + std::to_string(result);
	}
	printf("RESULT ok=%u publish=%u other=%u last_ok=%c elapsed_ms=%lld %s%s\n", ok, publish, other, last,
		   (long long)(NowMs() - start), Hooks().c_str(), codes.c_str());
	fflush(stdout);
	return 0;
}

int Reader(const std::wstring &path, const Bytes &a, const Bytes &b, const std::wstring &ready, const std::wstring &stop, DWORD pauseMs)
{
	unsigned long long iterations = 0, okA = 0, okB = 0, switches = 0, empty = 0, shorter = 0, mixed = 0, readErrors = 0, openFailed = 0;
	int last = 0;
	DWORD errorCode[8] = {};
	unsigned long long errorCount[8] = {};
	bool signalled = false, timedOut = false, announced = false;
	ULONGLONG start = GetTickCount64();
	const LONGLONG limit = (LONGLONG)(a.size() + b.size() + 4096);
	Bytes got;
	for (;;)
	{
		if ((iterations & 7) == 0 && GetFileAttributesW(stop.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			signalled = true;
			break;
		}
		if (GetTickCount64() - start > 60000)
		{
			timedOut = true;
			break;
		}
		iterations++;
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
							   FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
		{
			// Allowed while a replace is in flight; counted and reported, never an incomplete read.
			DWORD error = GetLastError();
			openFailed++;
			for (int i = 0; i < 8; i++)
				if (!errorCount[i] || errorCode[i] == error)
				{
					errorCode[i] = error;
					errorCount[i]++;
					break;
				}
			if (pauseMs)
				Sleep(pauseMs);
			continue;
		}
		LARGE_INTEGER size = {};
		bool sized = GetFileSizeEx(h, &size) != 0, complete = false, oversize = sized && size.QuadPart > limit;
		got.clear();
		if (sized && !oversize)
		{
			got.resize((size_t)size.QuadPart);
			size_t at = 0;
			while (at < got.size())
			{
				DWORD n = 0;
				if (!ReadFile(h, &got[at], (DWORD)(got.size() - at), &n, NULL) || !n)
					break;
				at += n;
			}
			complete = at == got.size();
			got.resize(at);
		}
		CloseHandle(h);
		if (oversize)
			mixed++;
		else if (!complete)
			readErrors++;
		else if (got.empty())
			empty++;
		else if (got == a)
			okA++, switches += last == 2, last = 1;
		else if (got == b)
			okB++, switches += last == 1, last = 2;
		else if ((got.size() < a.size() && !memcmp(got.data(), a.data(), got.size())) ||
				 (got.size() < b.size() && !memcmp(got.data(), b.data(), got.size())))
			shorter++;
		else
			mixed++;
		if (!announced && okA + okB)
		{
			announced = Spit(ready, "1", 1, CREATE_ALWAYS);
			if (!announced)
				Die("cannot write the ready flag");
		}
		if (pauseMs)
			Sleep(pauseMs);
	}
	std::string errors;
	for (int i = 0; i < 8 && errorCount[i]; i++)
		errors += (i ? "," : "") + std::to_string(errorCode[i]) + ":" + std::to_string(errorCount[i]);
	printf("RESULT iterations=%llu a=%llu b=%llu switches=%llu empty=%llu short=%llu mixed=%llu read_errors=%llu open_failed=%llu "
		   "open_errors=%s stopped_by=%s\n",
		   iterations, okA, okB, switches, empty, shorter, mixed, readErrors, openFailed, errors.empty() ? "-" : errors.c_str(),
		   signalled ? "flag" : timedOut ? "timeout" : "unknown");
	fflush(stdout);
	return 0;
}

int ReadThroughTheGame(const std::string &gamePath, const std::wstring &outFile)
{
	FILE *f = port_fopen(gamePath.c_str(), "rb");
	if (!f)
	{
		printf("RESULT read=failed\n");
		fflush(stdout);
		return 1;
	}
	Bytes bytes;
	unsigned char block[4096];
	size_t n;
	while ((n = fread(block, 1, sizeof(block), f)) != 0)
		bytes.insert(bytes.end(), block, block + n);
	fclose(f);
	if (!Spit(outFile, bytes.data(), bytes.size(), CREATE_ALWAYS))
		Die("cannot write the output file");
	printf("RESULT read=%llu\n", (unsigned long long)bytes.size());
	fflush(stdout);
	return 0;
}

int Attrib(const std::wstring &path, const std::wstring &mode)
{
	DWORD before = GetFileAttributesW(path.c_str());
	if (before == INVALID_FILE_ATTRIBUTES)
		Die("cannot read the attributes");
	if (mode == L"ro" || mode == L"rw")
	{
		DWORD next = mode == L"ro" ? (before | FILE_ATTRIBUTE_READONLY) : (before & ~(DWORD)FILE_ATTRIBUTE_READONLY);
		if (!SetFileAttributesW(path.c_str(), next ? next : FILE_ATTRIBUTE_NORMAL))
			Die("SetFileAttributesW failed");
	}
	else if (mode != L"query")
		return 2;
	DWORD after = GetFileAttributesW(path.c_str());
	printf("RESULT attributes=0x%lx readonly=%d\n", (unsigned long)after, (after & FILE_ATTRIBUTE_READONLY) != 0);
	fflush(stdout);
	return 0;
}

int List(const std::string &spec)
{
	struct _finddata_t d;
	intptr_t h = port_findfirst(spec.c_str(), &d);
	int count = 0;
	if (h != -1)
	{
		do
			printf("ENTRY name=%s attrib=%u size=%lu\n", d.name, (unsigned)d.attrib, (unsigned long)d.size), count++;
		while (port_findnext(h, &d) == 0);
		port_findclose(h);
	}
	printf("RESULT listed=%d\n", count);
	fflush(stdout);
	return 0;
}

int RawList(const std::wstring &pattern)
{
	WIN32_FIND_DATAW d;
	HANDLE h = FindFirstFileW(pattern.c_str(), &d);
	int count = 0;
	if (h != INVALID_HANDLE_VALUE)
	{
		do
			printf("ENTRY name=%s attrib=%lu\n", Utf8(d.cFileName).c_str(), (unsigned long)d.dwFileAttributes), count++;
		while (FindNextFileW(h, &d));
		FindClose(h);
	}
	printf("RESULT listed=%d\n", count);
	fflush(stdout);
	return 0;
}
}

int wmain(int argc, wchar_t **argv)
{
	if (argc < 2)
		return 2;
	std::wstring command = argv[1];
	if (command == L"info")
	{
		printf("RESULT pid=%lu acp=%u game=%s user=%s\n", (unsigned long)GetCurrentProcessId(), GetACP(), PortGameDir(), PortUserDir());
		fflush(stdout);
		return 0;
	}
	if (command == L"replace" && (argc == 4 || argc == 5))
		return Replace(Utf8(argv[2]), Load(argv[3]), argc == 5 ? Utf8(argv[4]).c_str() : "career");
	if (command == L"collide" && argc == 5)
		return Collide(Utf8(argv[2]), Load(argv[3]), (unsigned)_wtoi(argv[4]));
	if (command == L"hold" && argc == 6)
		return Hold(Utf8(argv[2]), Load(argv[3]), argv[4], _wtol(argv[5]));
	if (command == L"writer" && argc == 6)
		return Writer(Utf8(argv[2]), Load(argv[3]), Load(argv[4]), (unsigned)_wtoi(argv[5]));
	if (command == L"reader" && argc == 8)
		return Reader(argv[2], Load(argv[3]), Load(argv[4]), argv[5], argv[6], (DWORD)_wtoi(argv[7]));
	if (command == L"read" && argc == 4)
		return ReadThroughTheGame(Utf8(argv[2]), argv[3]);
	if (command == L"attrib" && argc == 4)
		return Attrib(argv[2], argv[3]);
	if (command == L"list" && argc == 3)
		return List(Utf8(argv[2]));
	if (command == L"rawlist" && argc == 3)
		return RawList(argv[2]);
	fprintf(stderr, "driver: unknown command or wrong argument count\n");
	return 2;
}
