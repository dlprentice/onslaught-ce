// SPDX-License-Identifier: GPL-3.0-only
// Complete production FindScan; authored native enumeration and strict WinAPI-conversion contracts backed by iconv.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iconv.h>
#include <string>
#include <vector>
#include <stdint.h>
#include "save_names.h"
using DWORD = uint32_t; using WCHAR = uint16_t; using HANDLE = void *;
enum : unsigned { MAX_PATH = 260, CP_UTF8 = 65001, MB_ERR_INVALID_CHARS = 8, WC_ERR_INVALID_CHARS = 128,
	ERROR_FILENAME_EXCED_RANGE = 206 };
enum : unsigned { ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_NO_MORE_FILES = 18 };
enum : unsigned { ERROR_GEN_FAILURE = 31, ERROR_INVALID_NAME = 123, ERROR_NO_UNICODE_TRANSLATION = 1113 };
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
struct FILETIME { DWORD low, high; };
template<class T> struct FindData
{
	DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
	DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1; T cFileName[260], cAlternateFileName[14];
};
using WIN32_FIND_DATAA = FindData<char>; using WIN32_FIND_DATAW = FindData<WCHAR>;
struct Find { WIN32_FIND_DATAA *entries; int count, next; bool saveNames; DWORD error; int sourceFirst; };
static DWORD error;
static unsigned wideCalls, ansiCalls, decodeCalls, closes;
static bool saveSearch;
static std::string fault;
static std::vector<std::vector<WCHAR>> nativeNames;
static std::vector<std::vector<WCHAR>> gameNativeNames, scanNames;
static bool splitRoots, strictWildcard;
static std::vector<std::vector<WCHAR>> nativePatterns;
static std::vector<WIN32_FIND_DATAA> delivered;
static size_t nextNative;
static DWORD fixtureAttributes = 32;
static int failures;
static void Check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "%s\n", message); failures++; } }
DWORD GetLastError() { return error; }
#ifndef SAVE_NAMESPACE_SOURCE
enum SaveDirectoryCase { SAVE_CASE_QUERY_UNKNOWN, SAVE_CASE_KNOWN_INSENSITIVE };
static DWORD SaveDirectoryCasePolicy(const WCHAR *, SaveDirectoryCase *policy = nullptr)
{ if (policy) *policy = SAVE_CASE_KNOWN_INSENSITIVE; return fault == "case-sensitive" ? 50 : 0; }
#endif
static size_t WideLen(const WCHAR *p) { size_t n = 0; while (p[n]) n++; return n; }
#ifndef SAVE_NAMESPACE_SOURCE
static WCHAR *TestWideLast(const WCHAR *p, WCHAR c) { WCHAR *last=nullptr;for(;*p;p++)if(*p==c)last=const_cast<WCHAR *>(p);return last; }
#define wcsrchr TestWideLast
#endif

static int Convert(const char *from, const char *to, const void *input, size_t bytes, void *output, size_t capacity, unsigned unit)
{
	char buffer[8192]; char *dest = buffer, *source = (char *)input;
	size_t left = sizeof(buffer), inputLeft = bytes;
	iconv_t codec = iconv_open(to, from);
	if (codec == (iconv_t)-1) { error = 87; return 0; }
	size_t rc = iconv(codec, &source, &inputLeft, &dest, &left);
	iconv_close(codec);
	if (rc == (size_t)-1) { error = 1113; return 0; }
	unsigned needed = unsigned((sizeof(buffer) - left) / unit);
	if (output)
	{
		if (capacity < needed) { error = 122; return 0; }
		memcpy(output, buffer, needed * unit);
	}
	return (int)needed;
}
static int MultiByteToWideChar(unsigned cp, unsigned flags, const char *input, int length, WCHAR *output, int capacity)
{
	decodeCalls++;
	Check(cp == CP_UTF8 && flags == MB_ERR_INVALID_CHARS && length == -1, "UTF8 decode flags/length changed");
	if ((fault=="identity-convert-zero-1" && decodeCalls==1) || (fault=="identity-convert-zero-2" && decodeCalls==2)) { error=0;return 0; }
	if ((!output && fault == "decode-query") || (output && fault == "decode-copy")) { error = output ? 122 : 1113; return 0; }
	return Convert("UTF-8", "UTF-16LE", input, strlen(input) + 1, output, capacity, 2);
}
static int WideCharToMultiByte(unsigned cp, unsigned flags, const WCHAR *input, int length, char *output, int capacity,
							  const char *replacement, void *usedReplacement)
{
	Check(cp == CP_UTF8 && flags == WC_ERR_INVALID_CHARS && length == -1 && !replacement && !usedReplacement,
		"UTF8 encode flags/default replacement changed");
	if ((!output && fault == "encode-query") || (output && fault == "encode-copy")) { error = output ? 122 : 1113; return 0; }
	return Convert("UTF-16LE", "UTF-8", input, (WideLen(input) + 1) * 2, output, capacity, 1);
}
static void Fill(WIN32_FIND_DATAW *data)
{
	memset(data, 0, sizeof(*data));
	const auto &name = scanNames[nextNative++];
	memcpy(data->cFileName, name.data(), name.size() * 2);
	data->dwFileAttributes = fixtureAttributes; data->nFileSizeLow = 0x12345678;
	data->nFileSizeHigh = 7; data->dwReserved0 = 11; data->dwReserved1 = 13;
	data->ftCreationTime = {17, 19}; data->ftLastAccessTime = {23, 29}; data->ftLastWriteTime = {31, 37};
}
static HANDLE FindFirstFileW(const WCHAR *spec, WIN32_FIND_DATAW *data)
{
	wideCalls++; nextNative = 0;
	const WCHAR *pattern=spec;for(const WCHAR *p=spec;*p;p++)if(*p=='\\')pattern=p+1;
	nativePatterns.emplace_back(pattern,pattern+WideLen(pattern));
	scanNames=splitRoots && spec[0]=='g'?gameNativeNames:nativeNames;
	if(strictWildcard && WideLen(pattern)!=1)
	{
		std::vector<std::vector<WCHAR>> matched;
		for(const auto &name:scanNames) { size_t n=name.size();if(n>=5 && name[n-5]=='.' && name[n-4]=='b' && name[n-3]=='e' && name[n-2]=='s')matched.push_back(name); }
		scanNames=matched;
	}
	if (fault == "native-open") { error = 5; return INVALID_HANDLE_VALUE; }
	if (fault == "native-open-zero") { error = 0; return INVALID_HANDLE_VALUE; }
	if (scanNames.empty()) { error = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
	Fill(data); return (HANDLE)1;
}
static bool FindNextFileW(HANDLE, WIN32_FIND_DATAW *data)
{
	if (fault == "native-next") { error = 5; return false; }
	if (fault == "native-next-zero") { error = 0; return false; }
	if (nextNative == scanNames.size()) { error = ERROR_NO_MORE_FILES; return false; }
	Fill(data); return true;
}
static HANDLE FindFirstFileA(const char *, WIN32_FIND_DATAA *data)
{
	ansiCalls++; memset(data, 0, sizeof(*data)); strcpy(data->cFileName, saveSearch ? "caf\xc3\xa9.bes" : "caf\xc3\xa9.ogg"); return (HANDLE)1;
}
static bool FindNextFileA(HANDLE, WIN32_FIND_DATAA *) { return false; }
static bool FindClose(HANDLE) { closes++;if(fault=="native-close-zero" || fault=="native-close-denied") {error=fault=="native-close-zero"?0:5;return false;}return true; }
#ifdef SAVE_NAMESPACE_SOURCE
#include SAVE_NAMESPACE_BOUNDARY
#else
static void FindAdd(Find *f, const WIN32_FIND_DATAA *data) { f->count++; delivered.push_back(*data); }
#endif
#include FIND_SCAN_SOURCE

static void Add(std::vector<WCHAR> name)
{
	name.insert(name.end(), {'.','b','e','s',0}); nativeNames.push_back(name);
}
int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	std::string test = argv[1]; Find f = {}; f.saveNames = test != "resource"; saveSearch = f.saveNames;
	if (test == "valid")
	{
		Add({'A'}); Add({0xe9}); Add({0xc3,0xa9}); Add({0x141}); Add({0xd83d,0xde00});
		Add(std::vector<WCHAR>(31, 0xe9)); Add(std::vector<WCHAR>(32, 'x')); Add({0xd800});
		FindScan(&f, "C:\\caf\xc3\xa9\\savegames\\*.bes");
		Check(f.count == 6 && wideCalls == 1 && ansiCalls == 0 && closes == 1 && !f.error, "wide SAVE enumeration/filter/close");
		const char *expected[] = {"A.bes", "\xc3\xa9.bes", "\xc3\x83\xc2\xa9.bes", "\xc5\x81.bes", "\xf0\x9f\x98\x80.bes"};
		for (size_t i = 0; i < delivered.size(); i++)
		{
			const auto &d = delivered[i];
			if (i < 5) Check(!strcmp(d.cFileName, expected[i]), "native filename replaced/shortened/double-encoded");
			Check(d.dwFileAttributes == 32 && d.nFileSizeLow == 0x12345678 && d.nFileSizeHigh == 7 && d.dwReserved0 == 11 &&
				d.dwReserved1 == 13 && d.ftCreationTime.low == 17 && d.ftLastAccessTime.high == 29 && d.ftLastWriteTime.high == 37,
				"native metadata changed");
		}
	}
	else if (test == "resource")
	{
		FindScan(&f, "C:\\caf\xc3\xa9\\*.ogg");
		Check(f.count == 1 && ansiCalls == 1 && !wideCalls && !decodeCalls && !strcmp(delivered[0].cFileName, "caf\xc3\xa9.ogg"),
			"resource search encoding/path changed");
	}
	else if (test == "invalid-spec" || test == "long-spec" || test == "decode-query" || test == "decode-copy")
	{
		fault = test;
		f.count = 7; // A failing second overlay root must also refuse an earlier partial listing.
		std::string spec = test == "invalid-spec" ? "C:\\\xe9\\*.bes" : test == "long-spec" ? std::string(521, 'x') : "C:\\savegames\\*.bes";
		FindScan(&f, spec.c_str());
		DWORD expected = test == "long-spec" ? 206 : test == "decode-copy" ? 122 : 1113;
		Check(f.count == 0 && !wideCalls && !ansiCalls && f.error == expected, "invalid/overlong spec reached enumeration or lost conversion error");
	}
	else if (test == "encode-query" || test == "encode-copy")
	{
		fault = test; Add({0xe9}); FindScan(&f, "C:\\savegames\\*.bes");
		Check(f.count == 0 && wideCalls == 1 && closes == 1, "failed native filename conversion exposed an alias/leaked a search");
	}
	else if (test == "case-sensitive")
	{
		fault = test; Add({'A'}); f.count = 7; FindScan(&f, "C:\\savegames\\*.bes");
		Check(f.count == 0 && f.error == 50 && !wideCalls, "unsupported case-sensitive namespace was admitted");
	}
	else if (test == "native-open" || test == "native-next")
	{
		fault = test; Add({'A'}); FindScan(&f, "C:\\savegames\\*.bes");
		Check(f.count == 0 && f.error == 5 && wideCalls == 1 && closes == (test == "native-next" ? 1u : 0u),
			"native enumeration failure exposed a partial listing/lost its error");
	}
	else return 2;
	return failures ? 1 : 0;
}
