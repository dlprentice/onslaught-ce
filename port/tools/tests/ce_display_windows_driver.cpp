// SPDX-License-Identifier: GPL-3.0-only
// Execute the actual Win32 settings admission and boolean reader with authored Win32 handle boundaries on Linux.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#define _WIN32 1
using DWORD = unsigned long;
using HANDLE = intptr_t;
const HANDLE INVALID_HANDLE_VALUE = -1;
const DWORD FILE_READ_ATTRIBUTES = 1, GENERIC_READ = 2, FILE_SHARE_READ = 4, FILE_SHARE_WRITE = 8;
const DWORD OPEN_EXISTING = 3, FILE_FLAG_BACKUP_SEMANTICS = 0x100, FILE_FLAG_OPEN_REPARSE_POINT = 0x200;
const DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10, FILE_ATTRIBUTE_REPARSE_POINT = 0x400;
const DWORD ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_ACCESS_DENIED = 5, ERROR_ALREADY_EXISTS = 183;
const DWORD FILE_TYPE_DISK = 1, FILE_TYPE_PIPE = 3;
const int _O_RDONLY = 0, _O_BINARY = 0;
struct BY_HANDLE_FILE_INFORMATION { DWORD dwFileAttributes; };
struct FakeHandle { bool live, final; DWORD attributes, type; };
static FakeHandle handles[16];
static int next_handle, final_opens, final_reads, directory_creates, memfile;
static const char *scenario;
static void check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(2); } }
DWORD GetFullPathNameA(const char *path, DWORD n, char *out, char **)
{
    check(!strcmp(path, "C:\\fixture\\user"), "admitted input path");
    const char *full = "C:\\fixture\\user";
    if (n <= strlen(full)) return n;
    strcpy(out, full); return strlen(full);
}
// The folder-level failures of a scenario: gone (file or path not found) or refused (access denied).
static bool folder_scenario(const char *name) { return !strncmp(scenario, name, strlen(name)) && !scenario[strlen(name)]; }
DWORD GetLastError()
{
    if (folder_scenario("parent-path-missing")) return ERROR_PATH_NOT_FOUND;
    if (folder_scenario("parent-denied")) return ERROR_ACCESS_DENIED;
    return ERROR_FILE_NOT_FOUND;
}
HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *, DWORD disposition, DWORD flags, void *)
{
    bool final = strstr(path, "\\ce.ini") != nullptr;
    check(disposition == OPEN_EXISTING && (flags & FILE_FLAG_OPEN_REPARSE_POINT), "open-existing no-follow");
    if (final)
    {
        ++final_opens;
        check(access == GENERIC_READ && share == FILE_SHARE_READ && !(flags & FILE_FLAG_BACKUP_SEMANTICS),
              "final opened for read only");
        if (!strcmp(scenario, "missing")) return INVALID_HANDLE_VALUE;
    }
    else
    {
        check(access == FILE_READ_ATTRIBUTES && (flags & FILE_FLAG_BACKUP_SEMANTICS), "directory attribute access");
        if ((folder_scenario("parent-missing") || folder_scenario("parent-path-missing") || folder_scenario("parent-denied")) &&
            !strcmp(path, "C:\\fixture")) return INVALID_HANDLE_VALUE;
    }
    int i = ++next_handle; check(i < 16, "bounded handles");
    handles[i] = {true, final, final ? 0ul : FILE_ATTRIBUTE_DIRECTORY, FILE_TYPE_DISK};
    if (!final && !strcmp(scenario, "parent-link") && !strcmp(path, "C:\\fixture"))
        handles[i].attributes |= FILE_ATTRIBUTE_REPARSE_POINT;
    if (final && !strcmp(scenario, "reparse")) handles[i].attributes |= FILE_ATTRIBUTE_REPARSE_POINT;
    if (final && !strcmp(scenario, "directory")) handles[i].attributes |= FILE_ATTRIBUTE_DIRECTORY;
    if (final && !strcmp(scenario, "pipe")) handles[i].type = FILE_TYPE_PIPE;
    return i;
}
int CreateDirectoryA(const char *, void *) { ++directory_creates; return 0; }
int GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *out)
{
    check(h > 0 && handles[h].live, "information on live handle");
    if (handles[h].final && !strcmp(scenario, "info-fail")) return 0;
    out->dwFileAttributes = handles[h].attributes; return 1;
}
DWORD GetFileType(HANDLE h) { check(h > 0 && handles[h].live, "type on live handle"); return handles[h].type; }
int CloseHandle(HANDLE h) { check(h > 0 && handles[h].live, "single handle close"); handles[h].live = false; return 1; }
int _open_osfhandle(intptr_t h, int flags)
{
    check(h > 0 && handles[h].live && handles[h].final && flags == (_O_RDONLY | _O_BINARY), "admitted read handle");
    if (!strcmp(scenario, "fd-fail")) return -1;
    handles[h].live = false; // CRT takes ownership of the native handle.
    ++final_reads;
    int fd = dup(memfile); check(fd >= 0 && lseek(fd, 0, SEEK_SET) == 0, "memory-file duplicate"); return fd;
}
FILE *_fdopen(int fd, const char *mode)
{
    if (!strcmp(scenario, "stream-fail")) return nullptr;
    return fdopen(fd, mode);
}
int _close(int fd) { return close(fd); }
#include "native_windows_read.inc"
int main(int argc, char **argv)
{
    if (argc != 3) return 3;
    scenario = argv[1];
    setenv("BEA_USER_DIR", "C:\\fixture\\user", 1);
    memfile = memfd_create("authored-ce-ini", 0); check(memfile >= 0, "memory file");
    const char bytes[] = "unknown=other\nwindowed=1\nwide_mode_w=800tail\nwide_mode_w=640\n";
    check(write(memfile, bytes, sizeof(bytes) - 1) == sizeof(bytes) - 1, "fixture bytes");
    bool integer = !strcmp(argv[2], "integer"), checked = !strcmp(argv[2], "checked");
    check(integer || checked || !strcmp(argv[2], "boolean"), "reader selection");
    bool success = !strcmp(scenario, "regular");
    if (checked)
    {
        // The checked reader tells a settings file that is not there (or whose folder is not there yet) from a failed read.
        int out = -1, ok = CE_TrySettingGet("wide_mode_w", 7, &out);
        bool empty = !strcmp(scenario, "missing") || folder_scenario("parent-missing") || folder_scenario("parent-path-missing");
        check(ok == int(success || empty), "checked reader: success, empty file or failed read");
        check(!ok || out == (success ? 800 : 7), "checked reader result or default");
    }
    else
    {
        int value = integer ? CE_SettingGet("wide_mode_w", 7) : CE_SettingGetBool("windowed", 7);
        check(value == (success ? (integer ? 800 : 1) : 7), "reader result or default");
    }
    check(directory_creates == 0, "reader creates no parent");
    for (int i = 1; i <= next_handle; ++i) check(!handles[i].live, "all admitted handles released");
    bool parent_rejected = !strcmp(scenario, "parent-link") || folder_scenario("parent-missing") ||
                           folder_scenario("parent-path-missing") || folder_scenario("parent-denied");
    check(final_opens == (parent_rejected ? 0 : 1), "reject unsafe parent before final open");
    bool read_allowed = success || !strcmp(scenario, "stream-fail");
    check(final_reads == int(read_allowed), "read only an admitted regular disk file");
    close(memfile);
    printf("%s pass\n", scenario);
}
