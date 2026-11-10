// SPDX-License-Identifier: GPL-3.0-only
// Native libc metadata boundary. No C/C++ structure crosses the caller's MS-layout ABI.
#ifndef CE_NATIVE_POSIX_STAT_H
#define CE_NATIVE_POSIX_STAT_H
#include <stdint.h>
#if !defined(_WIN32)
#ifdef __cplusplus
extern "C" {
#endif
// Six uint64_t scalar slots: device, inode, reserved zero, bytes, modified, changed.
// Times retain compatibility's seconds<<32 | nanoseconds representation. Failure leaves out unchanged.
// Path uses lstat and rejects links/nonregular files; fd neither closes nor repositions the caller's handle.
int CE_NativePosixPathStamp(const char *path, uint64_t out[6]);
int CE_NativePosixFdStamp(int fd, uint64_t out[6]);
#ifdef __cplusplus
}
#endif
#endif
#endif
