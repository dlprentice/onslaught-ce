// SPDX-License-Identifier: GPL-3.0-only
// Only the caller-created synthetic test root is writable in this driver.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602	// the loader's FILE_ID_INFO, before windows.h is first included
#endif
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdexcept>

static FARPROC WINAPI TestGetProcAddress(HMODULE module, LPCSTR name);
#define GetProcAddress TestGetProcAddress
#include CE_WINDOWS_MODS_SOURCE
#undef GetProcAddress

static std::wstring testRootWide;
static std::string testRoot;
static unsigned userDirectoryCalls;
static unsigned modReadCalls;
static HANDLE lastSnapshot;
static bool forceMmioFailure;
static unsigned decompressionCalls;
static bool selectorEnabled = true;
static bool lastMmioWasBasePath;
static unsigned relativeOpens;
static unsigned invalidDirectoryOptions;
static unsigned missingNoReparse;
static char gGameDir[MAX_PATH];
static const char fixture[] = "synthetic-mod-snapshot";
static const char baseFixture[] = "synthetic-base-content";
static const char unlistedFixture[] = "synthetic-base-unlisted";

typedef NTSTATUS (NTAPI *NativeCreateFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
    PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
static NativeCreateFile nativeCreateFile;

static NTSTATUS NTAPI ContractCreateFile(PHANDLE out, ACCESS_MASK access,
    POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK io, PLARGE_INTEGER size,
    ULONG fileAttributes, ULONG share, ULONG disposition, ULONG options,
    PVOID ea, ULONG eaSize)
{
    ++relativeOpens;
    const ULONG directoryOptions = FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT |
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH |
        FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_BY_FILE_ID;
    if ((options & FILE_DIRECTORY_FILE) && (options & ~directoryOptions))
    {
        ++invalidDirectoryOptions;
        return static_cast<NTSTATUS>(-1);
    }
    if (!(attributes->Attributes & OBJ_DONT_REPARSE))
    {
        ++missingNoReparse;
        return static_cast<NTSTATUS>(-1);
    }
    return nativeCreateFile(out, access, attributes, io, size, fileAttributes,
        share, disposition, options, ea, eaSize);
}

static FARPROC WINAPI TestGetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC proc = GetProcAddress(module, name);
    if (strcmp(name, "NtCreateFile")) return proc;
    nativeCreateFile = reinterpret_cast<NativeCreateFile>(proc);
    return proc ? reinterpret_cast<FARPROC>(ContractCreateFile) : NULL;
}

static void Require(bool ok, const char *label)
{
    if (!ok) throw std::runtime_error(label);
}

static std::string Utf8(const std::wstring &text)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    Require(size > 0, "utf8-conversion");
    std::string out(size, '\0');
    Require(WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), &out[0], size, NULL, NULL) == size,
        "utf8-conversion");
    return out;
}

static void NewFixtureFile(const std::wstring &path, const std::string &bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    Require(h != INVALID_HANDLE_VALUE, "new-fixture-file");
    DWORD count = 0;
    bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &count, NULL) &&
        count == bytes.size();
    CloseHandle(h);
    Require(ok, "new-fixture-file-write");
}

static void MakeFixture(const std::wstring &root)
{
    for (const wchar_t *part : {L"mods", L"mods\\fixture", L"mods\\fixture\\data",
                               L"game", L"game\\data"})
        Require(CreateDirectoryW((root + L"\\" + part).c_str(), NULL),
            "new-fixture-directory");
    NewFixtureFile(root + L"\\mods\\fixture\\data\\item.bin", fixture);
    NewFixtureFile(root + L"\\game\\data\\item.bin", baseFixture);
    NewFixtureFile(root + L"\\game\\data\\unlisted.bin", unlistedFixture);
    SHA256_CTX hash;
    unsigned char digest[32];
    sha256_init(&hash);
    sha256_update(&hash, reinterpret_cast<const unsigned char *>(fixture), sizeof(fixture) - 1);
    sha256_final(&hash, digest);
    std::string hex;
    for (unsigned char byte : digest)
    {
        char text[3];
        snprintf(text, sizeof(text), "%02x", byte);
        hex += text;
    }
    std::string manifest = "{\"schema\":1,\"id\":\"fixture\",\"name\":\"Synthetic fixture\","
        "\"version\":\"1.0.0\",\"author\":\"Synthetic control\",\"files\":[{"
        "\"path\":\"data/item.bin\",\"size\":" + std::to_string(sizeof(fixture) - 1) +
        ",\"sha256\":\"" + hex + "\"}]}";
    NewFixtureFile(root + L"\\mods\\fixture\\mod.json", manifest);
    NewFixtureFile(root + L"\\synthetic-only.marker", "synthetic-only\n");
}

int CE_SettingGetText(const char *, char *out, int n)
{
    if (!selectorEnabled)
    {
        if (n > 0) out[0] = 0;
        return 0;
    }
    return snprintf(out, n, "%s", "fixture") < n;
}

// Every fixture is an ordinary file. A packed-archive path is a test failure,
// rather than a stand-in decompressor that could admit unverified content.
int uncompress2(unsigned char *, unsigned long *, const unsigned char *, unsigned long *)
{
    ++decompressionCalls;
    return -1;
}

const char *PortUserDir()
{
    ++userDirectoryCalls;
    return testRoot.c_str();
}

bool PortGameHasFile(const char *path)
{
    return !strcmp(path, "data/item.bin");
}

static std::wstring Item()
{
    return testRootWide + L"\\mods\\fixture\\data\\item.bin";
}

static HANDLE OpenFixture(DWORD access, DWORD disposition = OPEN_EXISTING)
{
    HANDLE h = CreateFileW(Item().c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    Require(h != INVALID_HANDLE_VALUE, "synthetic-file-open");
    return h;
}

static void Rewrite(const char *text, const FILETIME *written = NULL)
{
    HANDLE h = OpenFixture(GENERIC_WRITE, CREATE_ALWAYS);
    DWORD count = 0;
    bool ok = WriteFile(h, text, static_cast<DWORD>(strlen(text)), &count, NULL) &&
        count == strlen(text);
    if (written) ok = ok && SetFileTime(h, NULL, NULL, written);
    CloseHandle(h);
    Require(ok, "synthetic-file-rewrite");
}

static std::string ReadHandle(HANDLE h)
{
    char buffer[128];
    DWORD count = 0;
    Require(ReadFile(h, buffer, sizeof(buffer), &count, NULL), "snapshot-read");
    return std::string(buffer, count);
}

static std::wstring SnapshotName(HANDLE h)
{
    std::vector<wchar_t> path(32768);
    DWORD size = GetFinalPathNameByHandleW(h, path.data(),
        static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED);
    Require(size && size < path.size(), "snapshot-name");
    return std::wstring(path.data(), size);
}

#define ModRead ProductionModRead
#include CE_WINDOWS_MODS_READER_HELPERS
#undef ModRead

static HANDLE ModRead(const char *name, const char *resolved)
{
    ++modReadCalls;
    lastSnapshot = ProductionModRead(name, resolved);
    return lastSnapshot;
}
enum { READ, WRITE };
static int Resolve(const char *name, int write, char *path, size_t n)
{
    if (write) return -1;
    char relative[MAX_PATH];
    if (!Relative(name, relative, sizeof(relative))) return -1;
    return snprintf(path, n, "%s\\%s", gGameDir, relative) < static_cast<int>(n) ? 0 : -1;
}

static HMMIO TestMmioOpen(LPSTR name, LPMMIOINFO info, DWORD flags)
{
    lastMmioWasBasePath = name != NULL;
    if (forceMmioFailure)
    {
        if (info) info->wErrorRet = MMIOERR_CANNOTOPEN;
        return NULL;
    }
    return mmioOpenA(name, info, flags);
}

#define mmioOpenA TestMmioOpen
#include CE_WINDOWS_MODS_MMIO_WRAPPER
#undef mmioOpenA

static void Loaded()
{
    PortModsInit();
    Require(PortModsActive() == 1, "unicode-mod-load");
    Require(PortModHas("data/item.bin"), "selected-file-visible");
}

static void SnapshotControl()
{
    Loaded();
    HANDLE h = static_cast<HANDLE>(PortModOpenRead("data/item.bin"));
    Require(h && h != INVALID_HANDLE_VALUE, "snapshot-open");
    std::wstring name = SnapshotName(h);
    Rewrite("synthetic-mod-changed!");
    Require(ReadHandle(h) == fixture, "snapshot-retains-verified-content");
    DWORD written = 0;
    Require(!WriteFile(h, "x", 1, &written, NULL), "snapshot-has-no-write-right");
    HANDLE writer = CreateFileW(name.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, 0, NULL);
    Require(writer == INVALID_HANDLE_VALUE, "snapshot-reopen-for-write-refused");
    CloseHandle(h);
    Require(GetFileAttributesW(name.c_str()) == INVALID_FILE_ATTRIBUTES,
        "snapshot-deleted-on-close");
}

static void HashControl()
{
    Loaded();
    HANDLE h = OpenFixture(GENERIC_READ);
    FILETIME written;
    Require(GetFileTime(h, NULL, NULL, &written), "synthetic-file-time");
    CloseHandle(h);
    Rewrite("synthetic-mod-changed!", &written);
    Require(!PortModOpenRead("data/item.bin"), "changed-content-refused");
}

static void IdentityControl()
{
    Loaded();
    Require(DeleteFileW(Item().c_str()), "synthetic-file-delete");
    Rewrite(fixture);
    Require(!PortModOpenRead("data/item.bin"), "replaced-file-refused");
    Require(!PortModHas("data/item.bin"), "replaced-file-not-advertised");
}

static void RenameControl()
{
    Loaded();
    std::wstring selected = testRootWide + L"\\mods\\fixture";
    Require(!MoveFileExW(selected.c_str(), (selected + L"-retained").c_str(), 0),
        "held-directory-cannot-be-renamed");
    std::wstring data = selected + L"\\data";
    Require(MoveFileExW(data.c_str(), (data + L"-retained").c_str(), 0),
        "synthetic-directory-rename");
    Require(CreateDirectoryW(data.c_str(), NULL), "synthetic-directory-create");
    Rewrite(fixture);
    Require(!PortModOpenRead("data/item.bin"), "look-alike-below-held-directory-refused");
}

static void LinkControl(bool directory, bool hard)
{
    std::wstring selected = testRootWide + L"\\mods\\fixture";
    std::wstring target = testRootWide + L"\\outside.bin";
    if (directory)
    {
        std::wstring data = selected + L"\\data";
        Require(MoveFileExW(data.c_str(), (data + L"-retained").c_str(), 0),
            "synthetic-directory-rename");
        Require(CreateSymbolicLinkW(data.c_str(), (data + L"-retained").c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE),
            "directory-reparse-fixture");
        Require(GetFileAttributesW(data.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT,
            "directory-reparse-fixture-visible");
    }
    else
    {
        Require(MoveFileExW(Item().c_str(), target.c_str(), 0), "synthetic-file-move");
        if (hard)
            Require(CreateHardLinkW(Item().c_str(), target.c_str(), NULL), "hardlink-fixture");
        else
        {
            Require(CreateSymbolicLinkW(Item().c_str(), target.c_str(),
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE), "file-reparse-fixture");
            Require(GetFileAttributesW(Item().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT,
                "file-reparse-fixture-visible");
        }
    }
    PortModsInit();
    Require(!PortModsActive(), "linked-mod-refused");
    Require(!PortModOpenRead("data/item.bin"), "linked-file-not-served");
}

static void DirectoryTypeControl()
{
    std::wstring data = testRootWide + L"\\mods\\fixture\\data";
    Require(MoveFileExW(data.c_str(), (data + L"-retained").c_str(), 0),
        "synthetic-directory-rename");
    HANDLE file = CreateFileW(data.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    Require(file != INVALID_HANDLE_VALUE, "synthetic-nondirectory-create");
    CloseHandle(file);
    HANDLE user = CreateFileW(testRootWide.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    Require(user != INVALID_HANDLE_VALUE, "synthetic-user-root");
    HANDLE mods = OpenChild(user, "mods", true);
    CloseHandle(user);
    Require(mods != INVALID_HANDLE_VALUE, "synthetic-mods-root");
    HANDLE selected = OpenChild(mods, "fixture", true);
    CloseHandle(mods);
    Require(selected != INVALID_HANDLE_VALUE, "synthetic-selected-root");
    HANDLE wrongType = OpenChild(selected, "data", true);
    CloseHandle(selected);
    Require(wrongType != INVALID_HANDLE_VALUE, "type-neutral-open");
    Require(Verify(wrongType, true, NULL) != NULL, "directory-type-checked-on-handle");
    CloseHandle(wrongType);
    PortModsInit();
    Require(!PortModsActive(), "nondirectory-component-refuses-mod");
}

static void MmioControl(bool fail, bool suppliedInfo)
{
    Loaded();
    forceMmioFailure = fail;
    MMIOINFO info = {};
    info.cchBuffer = 1024;
    char name[] = "data/item.bin";
    HMMIO io = PortMmioOpenA(name, suppliedInfo ? &info : NULL,
        MMIO_READ | MMIO_ALLOCBUF);
    Require(modReadCalls == 1 && lastSnapshot, "mmio-consumes-mod-snapshot");
    Require(!lastMmioWasBasePath, "mmio-selected-reader-uses-handle");
    if (fail)
    {
        Require(!io, "mmio-open-failure-propagated");
        BY_HANDLE_FILE_INFORMATION fileInfo;
        Require(!GetFileInformationByHandle(lastSnapshot, &fileInfo),
            "mmio-failure-closes-snapshot");
        return;
    }
    Require(io != NULL, "mmio-open-from-standard-handle");
    std::wstring snapshot = SnapshotName(lastSnapshot);
    Rewrite("synthetic-mod-changed!");
    char bytes[128];
    LONG count = mmioRead(io, bytes, sizeof(bytes));
    Require(count >= 0 && std::string(bytes, count) == fixture,
        "mmio-reads-verified-snapshot");
    Require(mmioClose(io, 0) == MMSYSERR_NOERROR, "mmio-close");
    Require(GetFileAttributesW(snapshot.c_str()) == INVALID_FILE_ATTRIBUTES,
        "mmio-close-deletes-snapshot");
}

static void MmioFallbackControl(bool noSelector)
{
    selectorEnabled = !noSelector;
    PortModsInit();
    Require(PortModsActive() == (noSelector ? 0 : 1), "mmio-selector-state");
    MMIOINFO info = {};
    char listed[] = "data/item.bin";
    char unlisted[] = "data/unlisted.bin";
    HMMIO io = PortMmioOpenA(noSelector ? listed : unlisted, &info,
        MMIO_READ | MMIO_ALLOCBUF);
    Require(io && lastMmioWasBasePath && !lastSnapshot, "mmio-base-fallback-open");
    Require(modReadCalls == (noSelector ? 0u : 1u), "mmio-real-selector-control");
    char bytes[128];
    LONG count = mmioRead(io, bytes, sizeof(bytes));
    Require(count >= 0 && std::string(bytes, count) ==
        (noSelector ? baseFixture : unlistedFixture), "mmio-base-fallback-content");
    Require(mmioClose(io, 0) == MMSYSERR_NOERROR, "mmio-base-fallback-close");
}

int wmain(int argc, wchar_t **argv)
{
    try
    {
        Require(argc >= 3 && argc <= 5, "synthetic-arguments");
        testRootWide = argv[1];
        bool makeFixture = false;
        bool unicodeChild = false;
        for (int i = 3; i < argc; ++i)
        {
            if (!wcscmp(argv[i], L"make-fixture") && !makeFixture) makeFixture = true;
            else if (!wcscmp(argv[i], L"unicode-child") && !unicodeChild) unicodeChild = true;
            else Require(false, "synthetic-fixture-option");
        }
        if (makeFixture)
            Require(CreateDirectoryW(testRootWide.c_str(), NULL), "fresh-owned-fixture-root");
        if (unicodeChild)
        {
            testRootWide += L"\\user-\u00e9\U0001f98a";
            if (makeFixture)
                Require(CreateDirectoryW(testRootWide.c_str(), NULL), "fresh-unicode-fixture-root");
        }
        if (makeFixture) MakeFixture(testRootWide);
        Require(GetFileAttributesW((testRootWide + L"\\synthetic-only.marker").c_str()) !=
            INVALID_FILE_ATTRIBUTES, "synthetic-root-marker");
        testRoot = Utf8(testRootWide);
        std::string base = Utf8(testRootWide + L"\\game");
        Require(snprintf(gGameDir, sizeof(gGameDir), "%s", base.c_str()) <
            static_cast<int>(sizeof(gGameDir)), "synthetic-base-path");
        std::wstring mode = argv[2];
        if (mode == L"fence")
        {
            PortModsInit();
            Require(!PortModsActive() && !userDirectoryCalls,
                "production-windows-fence-touches-no-user-directory");
        }
        else if (mode == L"load") Loaded();
        else if (mode == L"snapshot") SnapshotControl();
        else if (mode == L"hash") HashControl();
        else if (mode == L"identity") IdentityControl();
        else if (mode == L"rename") RenameControl();
        else if (mode == L"file-link") LinkControl(false, false);
        else if (mode == L"directory-link") LinkControl(true, false);
        else if (mode == L"hardlink") LinkControl(false, true);
        else if (mode == L"directory-type") DirectoryTypeControl();
        else if (mode == L"mmio") MmioControl(false, false);
        else if (mode == L"mmio-info") MmioControl(false, true);
        else if (mode == L"mmio-failure") MmioControl(true, true);
        else if (mode == L"mmio-no-selector") MmioFallbackControl(true);
        else if (mode == L"mmio-unlisted") MmioFallbackControl(false);
        else Require(false, "unknown-synthetic-case");
        Require(!decompressionCalls, "ordinary-fixtures-do-not-decompress");
        Require(!invalidDirectoryOptions, "documented-directory-open-options");
        Require(!missingNoReparse, "atomic-no-reparse-required");
        printf("{\"synthetic_control_pass\":true,\"relative_opens\":%u,"
            "\"invalid_directory_options\":%u,\"missing_no_reparse\":%u}\n",
            relativeOpens, invalidDirectoryOptions, missingNoReparse);
        return 0;
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "synthetic-control-fail:%s\n", error.what());
        printf("{\"synthetic_control_pass\":false,\"relative_opens\":%u,"
            "\"invalid_directory_options\":%u,\"missing_no_reparse\":%u}\n",
            relativeOpens, invalidDirectoryOptions, missingNoReparse);
        return 1;
    }
}
