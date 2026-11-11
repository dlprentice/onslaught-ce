// SPDX-License-Identifier: GPL-3.0-only
// Linked only into network_career_fence_tests.py's authored Linux boundary executable. Any would-be career
// mutation is recorded and refused; options still go to the real atomic writer in the disposable user root.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

extern "C" void NetworkTestMarkCareerBoundary()
{
    const char *path = std::getenv("NETWORK_TEST_MARKER");
    if (!path) return;
    FILE *file = std::fopen(path, "ab");
    if (file) { std::fputc('x', file); std::fclose(file); }
}

extern "C" FILE *__real_bea_crt_fopen(const char *, const char *);
extern "C" FILE *__wrap_bea_crt_fopen(const char *name, const char *mode)
{
    if (mode && (mode[0] == 'w' || mode[0] == 'a' || std::strchr(mode, '+'))) {
        NetworkTestMarkCareerBoundary();
        errno = EACCES;
        return NULL;
    }
    return __real_bea_crt_fopen(name, mode);
}

extern "C" FILE *__real_bea_crt_port_fopen(const char *, const char *);
extern "C" FILE *__wrap_bea_crt_port_fopen(const char *name, const char *mode)
{
    if (mode && (mode[0] == 'w' || mode[0] == 'a' || std::strchr(mode, '+'))) {
        NetworkTestMarkCareerBoundary();
        errno = EACCES;
        return NULL;
    }
    return __real_bea_crt_port_fopen(name, mode);
}

extern "C" int __real_PortReplaceUserFile(const char *, const void *, unsigned, const char *);
extern "C" int __wrap_PortReplaceUserFile(const char *path, const void *bytes, unsigned size, const char *what)
{
    if (what && std::strcmp(what, "career") == 0) { NetworkTestMarkCareerBoundary(); return 1; }
    return __real_PortReplaceUserFile(path, bytes, size, what);
}

extern "C" int __wrap_unlinkat(int, const char *, int)
{
    NetworkTestMarkCareerBoundary();
    errno = EACCES;
    return -1;
}

extern "C" BOOL WINAPI __wrap_DeleteFileA(LPCSTR)
{
    NetworkTestMarkCareerBoundary();
    return FALSE;
}
