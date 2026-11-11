// SPDX-License-Identifier: GPL-3.0-only
// The complete three patched card methods and the options writer are included by the authored test. The
// surrounding game and platform APIs below are in-memory fakes; none creates or opens a file.
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef wchar_t WCHAR;
typedef int BOOL;
typedef unsigned long DWORD;
enum { MAX_PATH = 260, MCE_SUCCESS = 0, MCE_FAILURE = 1, MCE_FILEEXISTS = 2, ERROR_FILE_EXISTS = 3,
       ERROR_ALREADY_EXISTS = 4, ERROR_FILE_NOT_FOUND = 5 };
static const char SAVE_DIRECTORY[] = "authored/";
static const char SAVE_EXTENSION[] = ".bes";
static int boot_requested, lockstep_requested, creates, writes, deletes, options, option_failure, lookups, operation;
static long sFindHandle = -1;
struct FindData { char name[256]; } sFindData;

extern "C" int CE_BootRequested() { return boot_requested; }
extern "C" int CE_LockstepRequested() { return lockstep_requested; }
// The CE project loader's storage guard (patch 0139) follows the network guard in these methods. No project is selected
// in this fixture, so it admits every call and the network guard alone decides.
static bool PortCeProjectCareerAllowed() { return true; }
static char *PortSaveFile(WCHAR *) { static char name[] = "authored"; return name; }
static WCHAR *PortSaveWide(char *) { static WCHAR name[] = L"authored"; return name; }
static WCHAR *ToWCHAR(char *) { static WCHAR name[] = L"authored"; return name; }
static int _wcsicmp(const WCHAR *, const WCHAR *) { return 0; }
static int _findclose(long) { return 0; }
static int _mkdir(const char *) { ++creates; return 0; }
static void SetLastError(DWORD) {}
extern "C" unsigned PortSaveLookupUtf16(const void *, int *index)
{
    ++lookups;
    if (index) *index = operation == 1 ? -1 : 0;
    return 0;
}
extern "C" unsigned PortSavePathUtf8(const void *, char *path, unsigned capacity)
{
    if (capacity < 9) return 1;
    std::strcpy(path, "authored");
    return 0;
}
extern "C" unsigned PortSaveReserveUtf8(const char *) { ++creates; return 0; }
static FILE *FakeOpen(const char *, const char *) { ++creates; return NULL; }
static int FakeClose(FILE *) { return 0; }
static int FakeDelete(const char *) { ++deletes; return 1; }
#define fopen FakeOpen
#define fclose FakeClose
#define DeleteFile FakeDelete
extern "C" int PortReplaceUserFile(const char *path, const void *, unsigned, const char *what)
{
    if (what && std::strcmp(what, "career") == 0) ++writes;
    else if (path && std::strcmp(path, "defaultoptions.bea") == 0) ++options;
    else return 1;
    return 0;
}
void PortCeOptionsWriteFailed() { ++option_failure; }
struct History { void AddMessage(const char *) { ++option_failure; } } SETUP_HISTORY;
class CPCMemoryCard {
public:
#include "career_declarations.inc"
    int FirstSave() { return 0; }
    int NextSave() { return 0; }
};
class CFEPOptions { public: static void WriteDefaultOptionsFile(char *, int); };
#include "career_full_methods.inc"
#include "options_writer.inc"

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    boot_requested = std::atoi(argv[1]);
    lockstep_requested = std::atoi(argv[2]);
    const bool blocked =
#ifdef CE_NETWORK_OFF
        false;
#else
        boot_requested || lockstep_requested;
#endif
    CPCMemoryCard card;
    WCHAR name[] = L"authored";
    char content[] = "authored";
    int save = 77, save_new = 77;
    operation = 1; int c = card.CreateSave(0, name, &save, 1);
    operation = 1; int c_new = card.CreateSave(0, name, &save_new, 0);
    operation = 1; int c_null = blocked ? card.CreateSave(0, name, NULL, 1) : 1;
    operation = 2; int w = card.WriteSave(0, 0, name, content, sizeof content);
    operation = 3; int d = card.DeleteSave(0, 0, name);
    CFEPOptions::WriteDefaultOptionsFile(content, sizeof content);
    bool ok = blocked ? c == 1 && c_new == 1 && c_null == 1 && w == 1 && d == 1 && save == -1 && save_new == -1 &&
                        creates == 0 && writes == 0 && deletes == 0 &&
                        options == 1 && option_failure == 0 :
                        creates >= 2 && writes == 1 && deletes == 1 && options == 1 && option_failure == 0;
    if (!ok) std::fprintf(stderr, "authored counters: creates=%d writes=%d deletes=%d options=%d lookups=%d\n",
                          creates, writes, deletes, options, lookups);
    return ok ? 0 : 1;
}
