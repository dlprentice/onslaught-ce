// SPDX-License-Identifier: GPL-3.0-only
// This translation unit uses native Linux layout, separately from the game's MS-layout callers.
#if !defined(_WIN32)
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "native_posix_stat.h"
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

// The pinned i686 glibc large-file ABI. Accidentally applying the game's -mms-bitfields must fail
// at compile time, rather than accepting corrupted sizes, identities and timestamps at runtime.
#if defined(__i386__) && defined(__GLIBC__)
static_assert(sizeof(struct stat) == 96 && offsetof(struct stat, st_size) == 44 &&
              offsetof(struct stat, st_ino) == 88 && offsetof(struct stat, st_mtim) == 72 &&
              offsetof(struct stat, st_ctim) == 80, "POSIX stat bridge requires native i686 large-file layout");
#endif
static_assert(sizeof(uint64_t) == 8, "metadata scalar slots must be 64 bits");

namespace {
int CopyStamp(const struct stat &st, uint64_t out[6]) {
    if (!out || !S_ISREG(st.st_mode) || st.st_size < 0) return 0;
    const uint64_t values[6] = {
        uint64_t(st.st_dev), uint64_t(st.st_ino), 0, uint64_t(st.st_size),
        (uint64_t(st.st_mtim.tv_sec) << 32) | uint32_t(st.st_mtim.tv_nsec),
        (uint64_t(st.st_ctim.tv_sec) << 32) | uint32_t(st.st_ctim.tv_nsec)};
    memcpy(out, values, sizeof values);
    return 1;
}
}
extern "C" int CE_NativePosixPathStamp(const char *path, uint64_t out[6]) {
    if (!path || !out) return 0;
    struct stat st{};
    return lstat(path, &st) == 0 && CopyStamp(st, out);
}
extern "C" int CE_NativePosixFdStamp(int fd, uint64_t out[6]) {
    if (!out) return 0;
    struct stat st{};
    return fstat(fd, &st) == 0 && CopyStamp(st, out);
}
#endif
