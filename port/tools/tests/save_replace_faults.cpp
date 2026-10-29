// SPDX-License-Identifier: GPL-3.0-only
// Linker wrappers (-Wl,--wrap=...) for port/tools/save_replace_tests.py. They exist only in the test executable; the
// production code reads no fault variable. SAVE_TEST_FAULT selects one fault, which touches only the temporary file
// ".ce-write-*" that save_replace.cpp creates (or the folder it flushes), never the test's own bookkeeping.
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" ssize_t __real_pread(int, void *, size_t, off_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_openat(int, const char *, int, ...);
extern "C" int __real_renameat(int, const char *, int, const char *);
extern "C" int __real_fstatat(int, const char *, struct stat *, int);

// How often creating a temporary file ran into a name that was already taken (a real EEXIST, not an injected one).
static long gTempEexist;
extern "C" long SaveTestTempEexist() { return __sync_fetch_and_add(&gTempEexist, 0); }

static bool Fault(const char *name)
{
    const char *f = getenv("SAVE_TEST_FAULT");
    return f && !strcmp(f, name);
}

// The file behind a descriptor, as the kernel names it.
static bool LinkIs(int fd, const char *needle, bool directory = false)
{
    char proc[64], path[4096] = {0};
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    if (readlink(proc, path, sizeof(path) - 1) < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || S_ISDIR(st.st_mode) != directory) return false;
    return directory || strstr(path, needle) != NULL;
}
static bool IsTemp(int fd) { return LinkIs(fd, "/.ce-write-"); }
static bool IsFolder(int fd) { return LinkIs(fd, "", true); }

extern "C" ssize_t __wrap_write(int fd, const void *p, size_t n)
{
    if (!IsTemp(fd)) return __real_write(fd, p, n);
    static int calls;
    ++calls;
    if (Fault("write-eio")) { if (calls == 1) return __real_write(fd, p, n / 2); errno = EIO; return -1; }
    if (Fault("enospc")) { if (calls == 1) return __real_write(fd, p, n / 2); errno = ENOSPC; return -1; }
    if (Fault("crash-mid-write")) { __real_write(fd, p, n / 2); raise(SIGKILL); }
    if (Fault("write-eintr") && calls == 1) { errno = EINTR; return -1; }
    if (Fault("short-writes")) return __real_write(fd, p, n > 7 ? 7 : n);
    if (Fault("write-dropped")) return (ssize_t)n;      // claims success, writes nothing
    if (Fault("write-zero")) return 0;
    return __real_write(fd, p, n);
}

extern "C" ssize_t __wrap_pread(int fd, void *p, size_t n, off_t at)
{
    if (!IsTemp(fd)) return __real_pread(fd, p, n, at);
    if (Fault("readback-eio")) { errno = EIO; return -1; }
    if (Fault("readback-short")) return __real_pread(fd, p, n / 2, at);
    ssize_t got = __real_pread(fd, p, n, at);
    if (Fault("readback-flip") && got > 0) ((unsigned char *)p)[got / 2] ^= 0x01;
    return got;
}

extern "C" int __wrap_fsync(int fd)
{
    if (IsFolder(fd))
    {
        if (Fault("dirsync-eio")) { errno = EIO; return -1; }
        if (Fault("dirsync-einval")) { errno = EINVAL; return -1; }
        if (Fault("dirsync-enosys")) { errno = ENOSYS; return -1; }
        return __real_fsync(fd);
    }
    if (!IsTemp(fd)) return __real_fsync(fd);
    if (Fault("fsync-eio")) { errno = EIO; return -1; }
    int result = __real_fsync(fd);
    if (Fault("swap-temp"))
    {
        // Another process replaces the temporary name after the bytes are on disk: the original inode moves away and a
        // different file takes its name. save_replace must notice and publish nothing.
        char proc[64], path[4096] = {0}, moved[4200];
        snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
        if (readlink(proc, path, sizeof(path) - 1) > 0)
        {
            snprintf(moved, sizeof(moved), "%s.moved", path);
            rename(path, moved);
            int other = __real_openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_EXCL, 0644);
            if (other >= 0) { __real_write(other, "SWAPPED", 7); __real_close(other); }
        }
    }
    return result;
}

extern "C" int __wrap_close(int fd)
{
    bool temp = IsTemp(fd);
    int result = __real_close(fd);
    if (temp && Fault("close-eio")) { errno = EIO; return -1; }
    return result;
}

extern "C" int __wrap_openat(int parent, const char *name, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    if (!strncmp(name, ".ce-write-", 10))
    {
        if (Fault("temp-exists")) { errno = EEXIST; return -1; }
        if (Fault("temp-eacces")) { errno = EACCES; return -1; }
    }
    int fd = __real_openat(parent, name, flags, mode);
    if (fd < 0 && errno == EEXIST && !strncmp(name, ".ce-write-", 10))
    {
        int error = errno;
        __sync_add_and_fetch(&gTempEexist, 1);
        errno = error;
    }
    return fd;
}

extern "C" int __wrap_renameat(int from_dir, const char *from, int to_dir, const char *to)
{
    if (!strncmp(from, ".ce-write-", 10))
    {
        if (Fault("rename-eio")) { errno = EIO; return -1; }
        if (Fault("rename-exdev")) { errno = EXDEV; return -1; }
        if (Fault("crash-before-rename")) { raise(SIGKILL); }
    }
    int result = __real_renameat(from_dir, from, to_dir, to);
    if (result == 0 && Fault("crash-after-rename")) { raise(SIGKILL); }
    return result;
}

// "target-unlinked-once" / "target-unlinked-always": the career's name led to the previous file, which another process's
// rename had already unlinked when its attributes were read, so it shows no link at all (st_nlink 0). That is what a second
// writer sees when the first one's rename lands between its lookup and its check; the temporary files (".ce-") are not touched.
extern "C" int __wrap_fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    int result = __real_fstatat(dirfd, path, st, flags);
    bool once = Fault("target-unlinked-once"), always = Fault("target-unlinked-always");
    if (result == 0 && (once || always) && flags == AT_SYMLINK_NOFOLLOW && strncmp(path, ".ce-", 4) != 0 && S_ISREG(st->st_mode))
    {
        static bool done;
        if (always || !done)
            st->st_nlink = 0;
        done = true;
    }
    return result;
}
