// SPDX-License-Identifier: GPL-3.0-only
//
// Installed-file conventions, titles, texture lookup and bank layout follow
// Onslaught Companion's AssetLibrary and XapBank (GPL-3.0-only). No game data
// is included here. Only the selected texture is read for inspection.
#include "ce_asset_catalog.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cwchar>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The port already links zlib for the native archive reader.
extern "C" int uncompress(unsigned char*, unsigned long*, const unsigned char*, unsigned long);

namespace {

constexpr size_t kMaxDirectoryEntries = 100000;
constexpr size_t kMaxAssets = 200000;
constexpr size_t kMaxDiagnostics = 1024;

struct Asset {
    int kind = 0;
    std::string title;
    std::string path;
    std::string group;
    std::string texture_key;
    uint64_t size = 0;
    int sample_index = -1;
};

struct Diagnostic {
    std::string path;
    std::string message;
};

unsigned char Lower(unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<unsigned char>(ch + ('a' - 'A')) : ch;
}

std::string LowerAscii(std::string value) {
    for (char& ch : value) ch = static_cast<char>(Lower(static_cast<unsigned char>(ch)));
    return value;
}

bool EndsWith(const std::string& text, const char* ending) {
    const size_t n = std::strlen(ending);
    if (text.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (Lower(static_cast<unsigned char>(text[text.size() - n + i])) !=
            Lower(static_cast<unsigned char>(ending[i]))) return false;
    return true;
}

bool StartsWith(const std::string& text, const char* start) {
    const size_t n = std::strlen(start);
    if (text.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (Lower(static_cast<unsigned char>(text[i])) !=
            Lower(static_cast<unsigned char>(start[i]))) return false;
    return true;
}

bool ContainsAscii(const std::string& value, const char* query) {
    if (!query || !*query) return true;
    const size_t n = std::strlen(query);
    if (n > value.size()) return false;
    for (size_t start = 0; start <= value.size() - n; ++start) {
        size_t i = 0;
        while (i < n && Lower(static_cast<unsigned char>(value[start + i])) ==
                        Lower(static_cast<unsigned char>(query[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

bool ValidUtf8(const std::string& value) {
    size_t i = 0;
    while (i < value.size()) {
        const unsigned char first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80) continue;
        unsigned count = 0;
        uint32_t code = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 0x07; }
        else return false;
        if (count > value.size() - i) return false;
        for (unsigned n = 0; n < count; ++n) {
            const unsigned char next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if ((count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
            (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return false;
    }
    return true;
}

uint32_t Little32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

std::string ModelTitle(const std::string& file) {
    std::string name = file;
    if (EndsWith(name, ".msh.aya")) name.resize(name.size() - 8);
    else if (EndsWith(name, ".aya")) name.resize(name.size() - 4);
    if (StartsWith(name, "m_")) name.erase(0, 2);
    return name;
}

std::string TextureTitle(const std::string& file) {
    std::string name = file;
    const size_t bracket = name.rfind('(');
    if (bracket != std::string::npos && bracket > 0) name.resize(bracket);
    if (EndsWith(name, ".tga")) name.resize(name.size() - 4);
    if (StartsWith(name, "mustbe_")) name.erase(0, 7);
    std::replace(name.begin(), name.end(), '%', '/');
    return name;
}

// A spoken line's file stem: the name the game's text table gives the line (MessageBox::StartMessage).
std::string SpeechTitle(const std::string& file) {
    std::string name = file;
    if (EndsWith(name, ".ogg")) name.resize(name.size() - 4);
    return name;
}

std::string TextureKey(const std::string& file) {
    const size_t bracket = file.rfind("(0)");
    return bracket == std::string::npos || bracket == 0 ? std::string() : LowerAscii(file.substr(0, bracket));
}

std::string BankGroup(const std::string& file) {
    std::string name = file;
    if (EndsWith(name, ".xap")) name.resize(name.size() - 4);
    if (StartsWith(name, "sounds_")) name.erase(0, 7);
    if (EndsWith(name, "_pc")) name.resize(name.size() - 3);
    if (name.empty()) return file;
    if (name[0] >= 'a' && name[0] <= 'z') name[0] -= 'a' - 'A';
    return name;
}

bool SoundTitle(const unsigned char* data, std::string& out) {
    for (size_t i = 0; i < 64 && data[i]; ++i) {
        const unsigned char ch = data[i];
        if (ch < 0x20 || (ch >= 0x7f && ch < 0xa0)) return false;
        if (ch == '\\') out += '/';
        else if (ch < 0x80) out += static_cast<char>(ch);
        else {
            out += static_cast<char>(0xc0 | (ch >> 6));
            out += static_cast<char>(0x80 | (ch & 0x3f));
        }
    }
    return !out.empty();
}

struct Name {
    std::string utf8;
#if defined(_WIN32)
    std::wstring native;
#endif
};

bool NameLess(const Name& a, const Name& b) {
    const std::string left = LowerAscii(a.utf8);
    const std::string right = LowerAscii(b.utf8);
    return left != right ? left < right : a.utf8 < b.utf8;
}

#if defined(_WIN32)

bool WideFromUtf8(const char* text, std::wstring& out) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (count <= 1) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(count));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, buffer.data(), count)) return false;
    out.assign(buffer.data(), static_cast<size_t>(count - 1));
    return true;
}

bool Utf8FromWide(const wchar_t* text, std::string& out) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return false;
    std::vector<char> buffer(static_cast<size_t>(count));
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, buffer.data(), count, nullptr, nullptr)) return false;
    out.assign(buffer.data(), static_cast<size_t>(count - 1));
    return true;
}

bool FinalPath(HANDLE handle, std::wstring& out) {
    const DWORD count = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count > 32768) return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(count) + 1);
    const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= buffer.size()) return false;
    out.assign(buffer.data(), written);
    return true;
}

bool InsideRoot(HANDLE handle, const std::wstring& root) {
    std::wstring resolved;
    if (!FinalPath(handle, resolved) || resolved.size() <= root.size()) return false;
    if (CompareStringOrdinal(resolved.c_str(), static_cast<int>(root.size()),
                             root.c_str(), static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) return false;
    return root.back() == L'\\' || resolved[root.size()] == L'\\';
}

struct Root {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring path;
    ~Root() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const char* selected) {
        std::wstring selected_path;
        if (!WideFromUtf8(selected, selected_path)) return false;
        while (selected_path.size() > 1 && (selected_path.back() == L'\\' || selected_path.back() == L'/') &&
               selected_path[selected_path.size() - 2] != L':') selected_path.pop_back();
        handle = CreateFileW(selected_path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info = {};
        if (handle == INVALID_HANDLE_VALUE || !(GetFileInformationByHandle(handle, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !FinalPath(handle, path)) return false;
        while (path.size() > 1 && path.back() == L'\\') path.pop_back();
        return true;
    }
};

struct Directory {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring path;
    ~Directory() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const Root& root, const char* relative) {
        path = root.path;
        const char* current = relative;
        while (*current) {
            const char* slash = std::strchr(current, '/');
            const std::string component(current, slash ? static_cast<size_t>(slash - current) : std::strlen(current));
            std::wstring wide;
            if (!WideFromUtf8(component.c_str(), wide)) return false;
            path += L'\\';
            path += wide;
            HANDLE next = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            BY_HANDLE_FILE_INFORMATION info = {};
            const bool valid = next != INVALID_HANDLE_VALUE && GetFileInformationByHandle(next, &info) &&
                               (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                               !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && InsideRoot(next, root.path);
            if (!valid) { if (next != INVALID_HANDLE_VALUE) CloseHandle(next); return false; }
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            handle = next;
            if (!slash) break;
            current = slash + 1;
        }
        return handle != INVALID_HANDLE_VALUE;
    }
    bool List(const char* extension, std::vector<Name>& names, bool& bad_name) const {
        WIN32_FIND_DATAW data = {};
        const std::wstring pattern = path + L"\\*";
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
        bool good = true;
        size_t visited = 0;
        do {
            if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0) continue;
            if (++visited > kMaxDirectoryEntries) { good = false; break; }
            Name name;
            if (!Utf8FromWide(data.cFileName, name.utf8)) { bad_name = true; continue; }
            if (!EndsWith(name.utf8, extension)) continue;
            name.native = data.cFileName;
            names.push_back(std::move(name));
        } while (FindNextFileW(find, &data));
        if (good && GetLastError() != ERROR_NO_MORE_FILES) good = false;
        FindClose(find);
        if (!good) names.clear();
        return good;
    }
    // Plain subfolders only: a reparse point (a link or junction) is never followed.
    bool ListFolders(std::vector<Name>& names, bool& bad_name) const {
        WIN32_FIND_DATAW data = {};
        const std::wstring pattern = path + L"\\*";
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
        bool good = true;
        size_t visited = 0;
        do {
            if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0) continue;
            if (++visited > kMaxDirectoryEntries) { good = false; break; }
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            Name name;
            if (!Utf8FromWide(data.cFileName, name.utf8)) { bad_name = true; continue; }
            name.native = data.cFileName;
            names.push_back(std::move(name));
        } while (FindNextFileW(find, &data));
        if (good && GetLastError() != ERROR_NO_MORE_FILES) good = false;
        FindClose(find);
        if (!good) names.clear();
        return good;
    }
};

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION before = {};
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const Root& root, const Directory& directory, const Name& name) {
        const std::wstring path = directory.path + L"\\" + name.native;
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_RANDOM_ACCESS, nullptr);
        return handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_DISK &&
               GetFileInformationByHandle(handle, &before) &&
               !(before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
               InsideRoot(handle, root.path);
    }
    uint64_t Size() const { return (static_cast<uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow; }
    bool Read(uint64_t offset, void* data, size_t size) const {
        if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()) || size > MAXDWORD) return false;
        LARGE_INTEGER position;
        position.QuadPart = static_cast<LONGLONG>(offset);
        DWORD read = 0;
        return SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) &&
               ReadFile(handle, data, static_cast<DWORD>(size), &read, nullptr) && read == size;
    }
    bool Unchanged() const {
        BY_HANDLE_FILE_INFORMATION after = {};
        return GetFileInformationByHandle(handle, &after) &&
               before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
               before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow &&
               before.nFileSizeHigh == after.nFileSizeHigh && before.nFileSizeLow == after.nFileSizeLow &&
               before.ftLastWriteTime.dwHighDateTime == after.ftLastWriteTime.dwHighDateTime &&
               before.ftLastWriteTime.dwLowDateTime == after.ftLastWriteTime.dwLowDateTime;
    }
};

#else

struct Root {
    int fd = -1;
    ~Root() { if (fd >= 0) close(fd); }
    bool Open(const char* selected) {
        std::string path(selected);
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        return fd >= 0;
    }
};

struct Directory {
    int fd = -1;
    ~Directory() { if (fd >= 0) close(fd); }
    bool Open(const Root& root, const char* relative) {
        fd = dup(root.fd);
        if (fd < 0) return false;
        const char* current = relative;
        while (*current) {
            const char* slash = std::strchr(current, '/');
            const std::string component(current, slash ? static_cast<size_t>(slash - current) : std::strlen(current));
            const int next = openat(fd, component.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (next < 0) return false;
            close(fd);
            fd = next;
            if (!slash) break;
            current = slash + 1;
        }
        return true;
    }
    bool List(const char* extension, std::vector<Name>& names, bool& bad_name) const {
        const int copy = dup(fd);
        if (copy < 0) return false;
        DIR* dir = fdopendir(copy);
        if (!dir) { close(copy); return false; }
        bool good = true;
        size_t visited = 0;
        errno = 0;
        while (dirent* entry = readdir(dir)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
            if (++visited > kMaxDirectoryEntries) { good = false; break; }
            Name name;
            name.utf8 = entry->d_name;
            if (!EndsWith(name.utf8, extension)) continue;
            if (!ValidUtf8(name.utf8)) { bad_name = true; continue; }
            names.push_back(std::move(name));
        }
        if (errno) good = false;
        closedir(dir);
        if (!good) names.clear();
        return good;
    }
    // Plain subfolders only: a symbolic link is never followed.
    bool ListFolders(std::vector<Name>& names, bool& bad_name) const {
        const int copy = dup(fd);
        if (copy < 0) return false;
        DIR* dir = fdopendir(copy);
        if (!dir) { close(copy); return false; }
        bool good = true;
        size_t visited = 0;
        errno = 0;
        while (dirent* entry = readdir(dir)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
            if (++visited > kMaxDirectoryEntries) { good = false; break; }
            struct stat info = {};
            if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(info.st_mode)) { errno = 0; continue; }
            Name name;
            name.utf8 = entry->d_name;
            if (!ValidUtf8(name.utf8)) { bad_name = true; continue; }
            names.push_back(std::move(name));
        }
        if (errno) good = false;
        closedir(dir);
        if (!good) names.clear();
        return good;
    }
};

struct File {
    int fd = -1;
    struct stat before = {};
    ~File() { if (fd >= 0) close(fd); }
    bool Open(const Root&, const Directory& directory, const Name& name) {
        struct stat candidate = {};
        if (fstatat(directory.fd, name.utf8.c_str(), &candidate, AT_SYMLINK_NOFOLLOW) ||
            !S_ISREG(candidate.st_mode)) return false;
        fd = openat(directory.fd, name.utf8.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        return fd >= 0 && !fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_size >= 0 &&
               before.st_dev == candidate.st_dev && before.st_ino == candidate.st_ino;
    }
    uint64_t Size() const { return static_cast<uint64_t>(before.st_size); }
    bool Read(uint64_t offset, void* data, size_t size) const {
        if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
        unsigned char* output = static_cast<unsigned char*>(data);
        size_t done = 0;
        while (done < size) {
            if (offset + done > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
            const ssize_t count = pread(fd, output + done, size - done, static_cast<off_t>(offset + done));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            done += static_cast<size_t>(count);
        }
        return true;
    }
    bool Unchanged() const {
        struct stat after = {};
        return !fstat(fd, &after) && before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
               before.st_size == after.st_size &&
               before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
               before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    }
};

#endif

} // namespace

struct CeAssetCatalog {
    Root root;
    std::vector<Asset> assets;
    std::vector<Diagnostic> diagnostics;
};

namespace {

void Report(CeAssetCatalog& catalog, const std::string& path, const char* message) {
    if (catalog.diagnostics.size() < kMaxDiagnostics) catalog.diagnostics.push_back({path, message});
    else if (catalog.diagnostics.size() == kMaxDiagnostics)
        catalog.diagnostics.push_back({"", "Further diagnostics were omitted."});
}

bool Bank(File& file, const std::string& relative, const std::string& filename,
          size_t budget, std::vector<Asset>& samples) {
    const uint64_t length = file.Size();
    unsigned char header[8];
    if (length < sizeof(header) || !file.Read(0, header, sizeof(header)) ||
        std::memcmp(header, "PCXP", 4) != 0) return false;
    const uint32_t count = Little32(header + 4);
    if (count > budget || count > (length - sizeof(header)) / 72) return false;
    const std::string group = BankGroup(filename);
    uint64_t position = sizeof(header);
    samples.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char record[72];
        if (position > length || length - position < sizeof(record) ||
            !file.Read(position, record, sizeof(record)) || std::memcmp(record, "PSMP", 4) != 0) return false;
        position += sizeof(record);
        const uint32_t decoded = Little32(record + 68);
        if (decoded > INT_MAX || (decoded & 1u)) return false;
        const uint64_t packed = (static_cast<uint64_t>(decoded) + 3u) / 4u;
        if (packed > length - position) return false;
        Asset entry;
        entry.kind = CE_ASSET_SOUND;
        if (!SoundTitle(record + 4, entry.title)) return false;
        entry.path = relative;
        entry.group = group;
        entry.size = decoded;
        entry.sample_index = static_cast<int>(i);
        samples.push_back(std::move(entry));
        position += packed;
    }
    return position == length && file.Unchanged();
}

void ScanFolder(CeAssetCatalog& catalog, const Root& root, const char* relative,
                const char* extension, int kind, const char* group) {
    Directory directory;
    if (!directory.Open(root, relative)) {
        Report(catalog, relative, "Folder is missing, linked or inaccessible.");
        return;
    }
    std::vector<Name> names;
    bool bad_name = false;
    if (!directory.List(extension, names, bad_name)) {
        Report(catalog, relative, "Folder could not be listed or has too many entries.");
        return;
    }
    if (bad_name) Report(catalog, relative, "A filename could not be represented as UTF-8.");
    std::sort(names.begin(), names.end(), NameLess);
    for (const Name& name : names) {
        const std::string path = std::string(relative) + "/" + name.utf8;
        File file;
        if (!file.Open(root, directory, name)) {
            Report(catalog, path, "File is linked, inaccessible or not a regular file.");
            continue;
        }
        if (catalog.assets.size() == kMaxAssets) {
            Report(catalog, relative, "The asset limit was reached.");
            return;
        }
        if (kind == CE_ASSET_SOUND) {
            std::vector<Asset> samples;
            if (!Bank(file, path, name.utf8, kMaxAssets - catalog.assets.size(), samples)) {
                Report(catalog, path, "Sound bank is unsupported, incomplete or changed during reading.");
                continue;
            }
            catalog.assets.insert(catalog.assets.end(), std::make_move_iterator(samples.begin()),
                                  std::make_move_iterator(samples.end()));
        } else {
            Asset entry;
            entry.kind = kind;
            entry.title = kind == CE_ASSET_MODEL ? ModelTitle(name.utf8) : kind == CE_ASSET_SPEECH ? SpeechTitle(name.utf8) : TextureTitle(name.utf8);
            entry.path = path;
            entry.group = group;
            entry.size = file.Size();
            if (kind == CE_ASSET_TEXTURE) entry.texture_key = TextureKey(name.utf8);
            catalog.assets.push_back(std::move(entry));
        }
    }
}

const Asset* At(const CeAssetCatalog* catalog, size_t index) {
    return catalog && index < catalog->assets.size() ? &catalog->assets[index] : nullptr;
}

uint32_t HeaderWord(const unsigned char* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

// AYA stores length-prefixed zlib members. Metadata needs only the first 128 decoded bytes.
bool ReadTextureBytes(File& file, bool full, std::vector<unsigned char>& decoded) {
    constexpr unsigned long blockLimit = 1ul << 20;
    constexpr uint32_t packedLimit = uint32_t(blockLimit + (blockLimit >> 12) + (blockLimit >> 14) + (blockLimit >> 25) + 13);
    constexpr size_t maxBytes = 32u << 20;
    if (full && file.Size() > maxBytes) return false;
    uint64_t pos = 0;
    std::vector<unsigned char> packed, block(blockLimit);
    unsigned members = 0;
    while (pos < file.Size() && (full || decoded.size() < 128)) {
        unsigned char size[4];
        if (++members > 512 || file.Size() - pos < 4 || !file.Read(pos, size, sizeof size)) return false;
        const uint32_t length = HeaderWord(size);
        pos += 4;
        if (!length || length > packedLimit || length > file.Size() - pos) return false;
        if (!full && pos + length > uint64_t(packedLimit) * 2 + 8) return false;
        packed.resize(length);
        if (!file.Read(pos, packed.data(), length)) return false;
        unsigned long bytes = blockLimit;
        if (uncompress(block.data(), &bytes, packed.data(), length) != 0 || !bytes) return false;
        size_t take = full ? size_t(bytes) : std::min<size_t>(bytes, 128 - decoded.size());
        if (take > maxBytes - decoded.size()) return false;
        decoded.insert(decoded.end(), block.data(), block.data() + take);
        pos += length;
    }
    return decoded.size() >= 128 && file.Unchanged();
}

bool TextureHeader(const unsigned char* h, CeAssetTextureHeader& out) {
    if (std::memcmp(h, "DDS ", 4) || HeaderWord(h + 4) != 124 || HeaderWord(h + 76) != 32) return false;
    const uint32_t flags = HeaderWord(h + 8), height = HeaderWord(h + 12), width = HeaderWord(h + 16);
    if ((flags & 0x1007) != 0x1007 || !width || !height || !(HeaderWord(h + 108) & 0x1000)) return false;
    const uint32_t levels = flags & 0x20000 ? HeaderWord(h + 28) : 1;
    uint32_t maxLevels = 1;
    for (uint32_t extent = std::max(width, height); extent > 1; extent >>= 1) ++maxLevels;
    if (!levels || levels > maxLevels) return false;
    char format[sizeof out.format] = {};
    const uint32_t pf = HeaderWord(h + 80), bits = HeaderWord(h + 88);
    if (pf & 4) {
        for (size_t i = 0; i < 4; ++i) if (h[84+i] < ' ' || h[84+i] > '~') return false;
        if (!std::memcmp(h + 84, "DX10", 4)) return false; // the layout lives in a separate extended header
        std::memcpy(format, h + 84, 4);
    } else if (pf & 0x40) {
        const uint32_t r = HeaderWord(h + 92), g = HeaderWord(h + 96), b = HeaderWord(h + 100), a = HeaderWord(h + 104);
        if (bits == 16 && r == 0x7c00 && g == 0x03e0 && b == 0x001f && a == 0x8000) std::strcpy(format, "A1R5G5B5");
        else if (bits == 32 && r == 0xff0000 && g == 0xff00 && b == 0xff && a == 0xff000000) std::strcpy(format, "A8R8G8B8");
        else if (bits && bits <= 64) std::snprintf(format, sizeof format, "%u-bit RGB", unsigned(bits));
        else return false;
    } else return false;
    out.width = width; out.height = height; out.levels = levels;
    std::memcpy(out.format, format, sizeof out.format);
    return true;
}

const Diagnostic* DiagnosticAt(const CeAssetCatalog* catalog, size_t index) {
    return catalog && index < catalog->diagnostics.size() ? &catalog->diagnostics[index] : nullptr;
}

void Error(char* out, size_t capacity, const char* message) {
    if (!out || !capacity) return;
    const size_t length = std::min(capacity - 1, std::strlen(message));
    std::memcpy(out, message, length);
    out[length] = '\0';
}

bool TextureMatches(const std::string& key, const char* requested) {
    if (key.empty() || !requested || !*requested) return false;
    size_t i = 0;
    while (i < key.size() && requested[i]) {
        unsigned char ch = static_cast<unsigned char>(requested[i]);
        if (ch == '/' || ch == '\\') ch = '%';
        if (static_cast<unsigned char>(key[i]) != Lower(ch)) return false;
        ++i;
    }
    return i == key.size() && requested[i] == '\0';
}

// The radio messages' spoken lines: data/sounds/<language>/MessageBox/<name>.ogg, the folder the game reads a
// line from (MessageBox::StartMessage builds data\\sounds\\<language>\\messagebox\\<name>.ogg). One group per
// language folder that has such a folder, named as on disk; the folder is matched without ASCII case, as the game's
// own open does, and two spellings of it in one language are refused rather than guessed between.
void ScanSpeech(CeAssetCatalog& catalog, const Root& root) {
    const std::string base = "data/sounds";
    Directory sounds;
    if (!sounds.Open(root, base.c_str())) { Report(catalog, base, "Folder is missing, linked or inaccessible."); return; }
    std::vector<Name> languages;
    bool bad_name = false;
    if (!sounds.ListFolders(languages, bad_name)) { Report(catalog, base, "Folder could not be listed or has too many entries."); return; }
    if (bad_name) Report(catalog, base, "A folder name could not be represented as UTF-8.");
    std::sort(languages.begin(), languages.end(), NameLess);
    for (const Name& language : languages) {
        const std::string folder = base + "/" + language.utf8;
        Directory directory;
        std::vector<Name> inside;
        bool bad = false;
        if (!directory.Open(root, folder.c_str()) || !directory.ListFolders(inside, bad)) {
            Report(catalog, folder, "Folder could not be listed or has too many entries.");
            continue;
        }
        const Name* messages = nullptr;
        bool twice = false;
        for (const Name& name : inside)
            if (LowerAscii(name.utf8) == "messagebox") { twice = messages != nullptr; if (!twice) messages = &name; else break; }
        if (twice) { Report(catalog, folder, "Two MessageBox folders differ only in letter case."); continue; }
        if (!messages) continue;
        const std::string speech = folder + "/" + messages->utf8;
        ScanFolder(catalog, root, speech.c_str(), ".ogg", CE_ASSET_SPEECH, language.utf8.c_str());
    }
}

} // namespace

extern "C" CeAssetCatalog* PortCeAssetCatalogOpen(const char* game_root, char* error, size_t error_capacity) noexcept {
    return PortCeAssetCatalogOpenKinds(game_root, CE_ASSET_ALL, error, error_capacity);
}

extern "C" CeAssetCatalog* PortCeAssetCatalogOpenKinds(const char* game_root, int kinds, char* error, size_t error_capacity) noexcept {
    Error(error, error_capacity, "");
    if (!game_root || !*game_root || !ValidUtf8(game_root)) {
        Error(error, error_capacity, "Select a game folder with a valid UTF-8 path.");
        return nullptr;
    }
    try {
        std::unique_ptr<CeAssetCatalog> catalog(new CeAssetCatalog());
        Root& root = catalog->root;
        if (!root.Open(game_root)) {
            Error(error, error_capacity, "The selected game folder cannot be opened.");
            return nullptr;
        }
        if (kinds & CE_ASSET_MODEL)
            ScanFolder(*catalog, root, "data/resources/meshes", ".aya", CE_ASSET_MODEL, "Models");
        if (kinds & CE_ASSET_TEXTURE) {
            ScanFolder(*catalog, root, "data/resources/dxtntextures", ".aya", CE_ASSET_TEXTURE, "Textures");
            ScanFolder(*catalog, root, "data/resources/textures", ".aya", CE_ASSET_TEXTURE, "Interface textures");
        }
        if (kinds & CE_ASSET_SOUND)
            ScanFolder(*catalog, root, "data/sounds", ".xap", CE_ASSET_SOUND, "");
        if (kinds & CE_ASSET_SPEECH)
            ScanSpeech(*catalog, root);
        return catalog.release();
    } catch (...) {
        Error(error, error_capacity, "The asset catalogue could not be created.");
        return nullptr;
    }
}

extern "C" void PortCeAssetCatalogClose(CeAssetCatalog* catalog) noexcept { delete catalog; }
extern "C" size_t PortCeAssetCatalogCount(const CeAssetCatalog* catalog) noexcept {
    return catalog ? catalog->assets.size() : 0;
}
extern "C" int PortCeAssetCatalogKind(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->kind : 0;
}
extern "C" const char* PortCeAssetCatalogTitle(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->title.c_str() : nullptr;
}
extern "C" const char* PortCeAssetCatalogPath(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->path.c_str() : nullptr;
}
extern "C" const char* PortCeAssetCatalogGroup(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->group.c_str() : nullptr;
}
extern "C" uint64_t PortCeAssetCatalogSize(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->size : 0;
}
extern "C" int PortCeAssetCatalogSampleIndex(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Asset* asset = At(catalog, index); return asset ? asset->sample_index : -1;
}
namespace {
bool OpenSelectedTexture(const CeAssetCatalog* catalog, size_t index, bool full,
                         std::vector<unsigned char>& bytes, CeAssetTextureHeader& header) {
    const Asset* asset = At(catalog, index);
    if (!asset || asset->kind != CE_ASSET_TEXTURE) return false;
    const size_t slash = asset->path.find_last_of('/');
    if (slash == std::string::npos) return false;
    Name name;
    name.utf8 = asset->path.substr(slash + 1);
#ifdef _WIN32
    if (!WideFromUtf8(name.utf8.c_str(), name.native)) return false;
#endif
    Directory dir; File file;
    return dir.Open(catalog->root, asset->path.substr(0, slash).c_str()) && file.Open(catalog->root, dir, name) &&
           file.Size() == asset->size && ReadTextureBytes(file, full, bytes) && TextureHeader(bytes.data(), header);
}
}
extern "C" int PortCeAssetCatalogTextureHeader(const CeAssetCatalog* catalog, size_t index, CeAssetTextureHeader* header) noexcept {
    if (!header) return 0;
    *header = {};
    try { std::vector<unsigned char> bytes; return OpenSelectedTexture(catalog, index, false, bytes, *header); }
    catch (...) { return 0; }
}

extern "C" int PortCeAssetCatalogReadTexture(const CeAssetCatalog* catalog, size_t index, CeAssetTextureReader reader, void* context) noexcept {
    if (!reader) return 0;
    try {
        std::vector<unsigned char> bytes; CeAssetTextureHeader header = {};
        if (!OpenSelectedTexture(catalog, index, true, bytes, header) || bytes.size() <= 128 ||
            header.width > 4096 || header.height > 4096 || uint64_t(header.width) * header.height > (4u << 20)) return 0;
        return reader(bytes.data(), bytes.size(), &header, context) != 0;
    } catch (...) { return 0; }
}

extern "C" size_t PortCeAssetCatalogNext(const CeAssetCatalog* catalog, size_t after,
                                         int kind_mask, const char* text) noexcept {
    if (!catalog) return SIZE_MAX;
    const size_t count = catalog->assets.size();
    const size_t first = after == SIZE_MAX ? 0 : after >= count ? count : after + 1;
    for (size_t i = first; i < count; ++i) {
        const Asset& asset = catalog->assets[i];
        if ((asset.kind & kind_mask) && (ContainsAscii(asset.title, text) ||
            ContainsAscii(asset.group, text) || ContainsAscii(asset.path, text))) return i;
    }
    return SIZE_MAX;
}
extern "C" size_t PortCeAssetCatalogFindTexture(const CeAssetCatalog* catalog, const char* requested) noexcept {
    if (!catalog) return SIZE_MAX;
    for (size_t i = 0; i < catalog->assets.size(); ++i)
        if (TextureMatches(catalog->assets[i].texture_key, requested)) return i;
    return SIZE_MAX;
}
extern "C" size_t PortCeAssetCatalogDiagnosticCount(const CeAssetCatalog* catalog) noexcept {
    return catalog ? catalog->diagnostics.size() : 0;
}
extern "C" const char* PortCeAssetCatalogDiagnosticPath(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Diagnostic* diagnostic = DiagnosticAt(catalog, index); return diagnostic ? diagnostic->path.c_str() : nullptr;
}
extern "C" const char* PortCeAssetCatalogDiagnosticMessage(const CeAssetCatalog* catalog, size_t index) noexcept {
    const Diagnostic* diagnostic = DiagnosticAt(catalog, index); return diagnostic ? diagnostic->message.c_str() : nullptr;
}
