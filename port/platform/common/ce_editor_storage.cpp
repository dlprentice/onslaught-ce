// SPDX-License-Identifier: GPL-3.0-only
// The native editor stores user-authored JSON references. It never writes the installed map or a career.
#include "../../updater/ce_report.h"
#include "../../game/ce_editor_storage.h"
const char *PortUserDir();
namespace {
const size_t DocumentLimit = 8u << 20;
const int EntryLimit = 2048;
void Error(char *out, size_t capacity, const char *text) {
    if (out && capacity) snprintf(out, capacity, "%s", text);
}
bool Leaf(const char *name) {
    if (!name || !*name || name[0] == '.') return false;
    size_t n = 0;
    for (; n < sizeof(CeEditorFile::name) && name[n]; ++n) {
        const unsigned char c = static_cast<unsigned char>(name[n]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '.' || c == ' '))
            return false;
    }
    return n > 10 && n < sizeof(CeEditorFile::name) && !strcmp(name + n - 10, ".ceproject");
}
bool Directory(CeSettingsDir *dir, bool create, bool *absent) {
    const char *root = PortUserDir();
    int gone = 0;
    if (absent) *absent = false;
    if (!root || !*root) return false;
    const bool result = ce_settings_subdir_open_ex(root, "projects", create ? 1 : 0, dir, &gone) != 0;
    if (absent) *absent = gone != 0;
    return result;
}
struct Folder {
    CeSettingsDir dir;
    bool opened;
    Folder() : opened(false) {}
    ~Folder() {
        if (opened) ce_settings_dir_close(&dir);
    }
};
int Compare(const void *a, const void *b) {
    return strcmp(static_cast<const CeEditorFile *>(a)->name, static_cast<const CeEditorFile *>(b)->name);
}
#if defined(_WIN32)
#include "ce_win_reparse.h"
bool Full(CeSettingsDir *dir, const char *leaf, char *out, size_t capacity) {
    return size_t(snprintf(out, capacity, "%s\\%s", dir->path, leaf)) < capacity;
}
bool FileInfo(HANDLE handle, BY_HANDLE_FILE_INFORMATION *info) {
    return GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, info) &&
           !(info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !ce_win_handle_is_link(handle, info->dwFileAttributes);
}
bool Same(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
           a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh &&
           a.nFileSizeLow == b.nFileSizeLow &&
           a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime &&
           a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime;
}
#else
bool Same(const struct stat &a, const struct stat &b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
           a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
#endif
// The highest number among this level's saves already in the folder (0 when there are none, or it cannot be listed), so a
// new save is numbered after every earlier one and the newest save always has the highest number, gaps or not.
// Also counts valid .ceproject files so saving and browsing share the same EntryLimit.
int Highest(CeSettingsDir *dir, int level, int *validProjects) {
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "level-%04d-", level);
    int highest = 0;
    int valid = 0;
    auto consider = [&](const char *leaf) {
        int serial = 0;
        char tail[16] = "";
        if (strncmp(leaf, prefix, 11) || sscanf(leaf + 11, "%6d%15s", &serial, tail) != 2 || strcmp(tail, ".ceproject"))
            return;
        if (serial > highest) highest = serial;
    };
#if defined(_WIN32)
    char pattern[4400];
    if (!Full(dir, "*.ceproject", pattern, sizeof(pattern))) {
        if (validProjects) *validProjects = 0;
        return 0;
    }
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) {
        if (validProjects) *validProjects = 0;
        return 0;
    }
    do {
        if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
        consider(found.cFileName);
        if (Leaf(found.cFileName) &&
            !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !ce_win_find_is_link(found.dwFileAttributes, found.dwReserved0) &&
            !found.nFileSizeHigh && found.nFileSizeLow && found.nFileSizeLow <= DocumentLimit)
            ++valid;
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
#else
    int fd = openat(dir->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *directory = fd >= 0 ? fdopendir(fd) : nullptr;
    if (!directory) {
        if (fd >= 0) close(fd);
        if (validProjects) *validProjects = 0;
        return 0;
    }
    for (struct dirent *row; (row = readdir(directory));) {
        if (!strcmp(row->d_name, ".") || !strcmp(row->d_name, "..")) continue;
        consider(row->d_name);
        if (!Leaf(row->d_name)) continue;
        struct stat info;
        if (fstatat(dir->fd, row->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
            info.st_size > 0 && static_cast<unsigned long long>(info.st_size) <= DocumentLimit)
            ++valid;
    }
    closedir(directory);
#endif
    if (validProjects) *validProjects = valid;
    return highest;
}
} // namespace
int PortCeEditorFiles(CeEditorFile *files, int capacity, char *error, size_t errorCapacity) noexcept {
    Error(error, errorCapacity, "");
    if (!files || capacity < 1 || capacity > EntryLimit) {
        Error(error, errorCapacity, "The project browser has no valid bounded list.");
        return -1;
    }
    Folder folder;
    bool absent = false;
    if (!(folder.opened = Directory(&folder.dir, false, &absent))) {
        if (absent) return 0;
        Error(error, errorCapacity, "The projects folder cannot be read safely.");
        return -1;
    }
    int count = 0, entries = 0;
    bool ok = true;
#if defined(_WIN32)
    char pattern[4400];
    if (!Full(&folder.dir, "*", pattern, sizeof(pattern))) return -1;
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 0;
        Error(error, errorCapacity, "The projects folder cannot be listed.");
        return -1;
    }
    do {
        if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
        if (!Leaf(found.cFileName) ||
            (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            ce_win_find_is_link(found.dwFileAttributes, found.dwReserved0) ||
            found.nFileSizeHigh || !found.nFileSizeLow || found.nFileSizeLow > DocumentLimit)
            continue;
        if (++entries > EntryLimit) {
            ok = false;
            break;
        }
        if (count == capacity) {
            ok = false;
            break;
        }
        snprintf(files[count].name, sizeof(files[count].name), "%s", found.cFileName);
        files[count++].size = found.nFileSizeLow;
    } while (FindNextFileA(handle, &found));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(handle);
#else
    int fd = openat(folder.dir.fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *directory = fd >= 0 ? fdopendir(fd) : nullptr;
    if (!directory) {
        if (fd >= 0) close(fd);
        Error(error, errorCapacity, "The projects folder cannot be listed.");
        return -1;
    }
    errno = 0;
    for (struct dirent *row; (row = readdir(directory)); errno = 0) {
        if (!strcmp(row->d_name, ".") || !strcmp(row->d_name, "..")) continue;
        if (!Leaf(row->d_name)) continue;
        struct stat info;
        if (fstatat(folder.dir.fd, row->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            ok = false;
            break;
        }
        if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
            static_cast<unsigned long long>(info.st_size) > DocumentLimit)
            continue;
        if (++entries > EntryLimit) {
            ok = false;
            break;
        }
        if (count == capacity) {
            ok = false;
            break;
        }
        snprintf(files[count].name, sizeof(files[count].name), "%s", row->d_name);
        files[count++].size = unsigned(info.st_size);
    }
    if (errno) ok = false;
    closedir(directory);
#endif
    if (!ok) {
        Error(error, errorCapacity, "The project list changed or exceeds the browser limit.");
        return -1;
    }
    qsort(files, size_t(count), sizeof(*files), Compare);
    return count;
}
char *PortCeEditorRead(const char *filename, size_t *size, char *error, size_t errorCapacity) noexcept {
    Error(error, errorCapacity, "");
    if (size) *size = 0;
    if (!size || !Leaf(filename)) {
        Error(error, errorCapacity, "Choose a project filename from the browser.");
        return nullptr;
    }
    Folder folder;
    if (!(folder.opened = Directory(&folder.dir, false, nullptr))) {
        Error(error, errorCapacity, "The projects folder cannot be opened safely.");
        return nullptr;
    }
    char *bytes = nullptr;
    size_t length = 0, at = 0;
    bool ok = false;
#if defined(_WIN32)
    char path[4400];
    if (!Full(&folder.dir, filename, path, sizeof(path))) return nullptr;
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        Error(error, errorCapacity, "The project could not be opened for reading.");
        return nullptr;
    }
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (FileInfo(handle, &before) &&
        ce_win_hydrate_handle_a(path, &handle, &before, GENERIC_READ, FILE_SHARE_READ, 0, 0) &&
        !before.nFileSizeHigh && before.nFileSizeLow > 0 &&
        before.nFileSizeLow <= DocumentLimit) {
        length = before.nFileSizeLow;
        bytes = static_cast<char *>(malloc(length + 1));
        ok = bytes != nullptr;
        while (ok && at < length) {
            DWORD got = 0;
            ok = ReadFile(handle, bytes + at, DWORD(length - at), &got, nullptr) && got > 0;
            at += got;
        }
        char extra;
        DWORD got = 0;
        ok = ok && ReadFile(handle, &extra, 1, &got, nullptr) && got == 0 && FileInfo(handle, &after) &&
             Same(before, after);
    }
    if (handle != INVALID_HANDLE_VALUE && !CloseHandle(handle)) ok = false;
#else
    int fd = openat(folder.dir.fd, filename, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        Error(error, errorCapacity, "The project could not be opened safely.");
        return nullptr;
    }
    struct stat before{}, after{};
    if (fstat(fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_size > 0 &&
        static_cast<unsigned long long>(before.st_size) <= DocumentLimit) {
        length = size_t(before.st_size);
        bytes = static_cast<char *>(malloc(length + 1));
        ok = bytes != nullptr;
        while (ok && at < length) {
            const ssize_t n = read(fd, bytes + at, length - at);
            if (n < 0 && errno == EINTR) continue;
            ok = n > 0;
            if (ok) at += size_t(n);
        }
        char extra;
        ssize_t tail = -1;
        if (ok) {
            do tail = read(fd, &extra, 1);
            while (tail < 0 && errno == EINTR);
        }
        ok = ok && tail == 0 && fstat(fd, &after) == 0 && Same(before, after);
    }
    if (close(fd) != 0) ok = false;
#endif
    if (!ok) {
        free(bytes);
        Error(error, errorCapacity, "The project is not a stable bounded regular file.");
        return nullptr;
    }
    bytes[length] = 0;
    *size = length;
    return bytes;
}
void PortCeEditorFileFree(char *bytes) noexcept { free(bytes); }
bool PortCeEditorSave(int level, const char *bytes, size_t size, char *filename, size_t filenameCapacity,
                      char *error, size_t errorCapacity) noexcept {
    Error(error, errorCapacity, "");
    if (filename && filenameCapacity) filename[0] = 0;
    if (level < 1 || level > 9999 || !bytes || !size || size > DocumentLimit || !filename ||
        filenameCapacity < 40) {
        Error(error, errorCapacity, "The project is not ready to save.");
        return false;
    }
    Folder folder;
    if (!(folder.opened = Directory(&folder.dir, true, nullptr))) {
        Error(error, errorCapacity, "The projects folder cannot be created safely.");
        return false;
    }
    char name[64];
    int validProjects = 0;
    const int first = Highest(&folder.dir, level, &validProjects) + 1;
    if (validProjects >= EntryLimit) {
        Error(error, errorCapacity, "This folder has reached the project browser limit.");
        return false;
    }
    for (int revision = first; revision < first + EntryLimit && revision <= 999999; ++revision) {
        snprintf(name, sizeof(name), "level-%04d-%06d.ceproject", level, revision);
        size_t at = 0;
        bool ok = true;
        char verify[4096];
#if defined(_WIN32)
        char path[4400];
        if (!Full(&folder.dir, name, path, sizeof(path))) return false;
        HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            Error(error, errorCapacity, "A new project revision could not be created.");
            return false;
        }
        while (ok && at < size) {
            DWORD wrote = 0;
            ok = WriteFile(handle, bytes + at, DWORD(size - at), &wrote, nullptr) && wrote > 0;
            at += wrote;
        }
        ok = ok && FlushFileBuffers(handle) &&
             SetFilePointer(handle, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
        at = 0;
        while (ok && at < size) {
            const DWORD chunk = DWORD(size - at < sizeof(verify) ? size - at : sizeof(verify));
            DWORD got = 0;
            ok = ReadFile(handle, verify, chunk, &got, nullptr) && got == chunk &&
                 !memcmp(verify, bytes + at, chunk);
            at += got;
        }
        if (!ok) {
            FILE_DISPOSITION_INFO remove{TRUE};
            (void)SetFileInformationByHandle(handle, FileDispositionInfo, &remove, sizeof(remove));
        }
        if (!CloseHandle(handle)) ok = false;
#else
        int fd = openat(folder.dir.fd, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            Error(error, errorCapacity, "A new project revision could not be created.");
            return false;
        }
        struct stat created{}, current{};
        if (fstat(fd, &created) != 0) ok = false;
        while (ok && at < size) {
            const ssize_t n = write(fd, bytes + at, size - at);
            if (n < 0 && errno == EINTR) continue;
            ok = n > 0;
            if (ok) at += size_t(n);
        }
        ok = ok && fsync(fd) == 0 && lseek(fd, 0, SEEK_SET) == 0;
        at = 0;
        while (ok && at < size) {
            const size_t chunk = size - at < sizeof(verify) ? size - at : sizeof(verify);
            const ssize_t n = read(fd, verify, chunk);
            if (n < 0 && errno == EINTR) continue;
            ok = n > 0 && !memcmp(verify, bytes + at, size_t(n));
            if (ok) at += size_t(n);
        }
        if (close(fd) != 0) ok = false;
        if (!ok && fstatat(folder.dir.fd, name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            current.st_dev == created.st_dev && current.st_ino == created.st_ino)
            (void)unlinkat(folder.dir.fd, name, 0);
#endif
        if (!ok) {
            Error(error, errorCapacity, "The new project revision could not be written and verified.");
            return false;
        }
        snprintf(filename, filenameCapacity, "%s", name);
        return true;
    }
    Error(error, errorCapacity, "This map has reached the project revision limit.");
    return false;
}
