// SPDX-License-Identifier: GPL-3.0-only
// Reuse the existing authored fixture without starting the game.
#define main ce_existing_netcode_driver_main
#include "ce_netcode_driver.cpp"
#undef main

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

static const char *test_path;
static bool inject_failure;
static bool replace_during_write;
static bool inject_short_write;
static bool inject_truncate_failure;
static bool inject_close_failure;
static bool injected;
static int writer_fd = -1;

extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" int __real_ftruncate(int, off_t);
extern "C" int __real_close(int);

extern "C" ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    writer_fd = fd;
    if (inject_short_write) {
        inject_short_write = false;
        const size_t short_size = size > 4 ? 4 : size;
        const ssize_t result = __real_write(fd, data, short_size);
        if (!inject_truncate_failure && result > 0)
            injected = true;
        return result;
    }
    if (!inject_failure)
        return __real_write(fd, data, size);
    inject_failure = false;
    if (replace_during_write) {
        if (::unlink(test_path) != 0)
            return -1;
        const int replacement = ::open(test_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (replacement < 0)
            return -1;
        static const char contents[] = "authored replacement fixture";
        const bool wrote = __real_write(replacement, contents, sizeof(contents) - 1) ==
                           static_cast<ssize_t>(sizeof(contents) - 1);
        const bool closed = ::close(replacement) == 0;
        if (!wrote || !closed)
            return -1;
    }
    injected = true;
    errno = EIO;
    return -1;
}

extern "C" int __wrap_ftruncate(int fd, off_t length)
{
    if (inject_truncate_failure && fd == writer_fd) {
        inject_truncate_failure = false;
        injected = true;
        errno = EIO;
        return -1;
    }
    return __real_ftruncate(fd, length);
}

extern "C" int __wrap_close(int fd)
{
    if (inject_close_failure && fd == writer_fd) {
        inject_close_failure = false;
        const int result = __real_close(fd);
        if (result == 0) {
            injected = true;
            errno = EIO;
            return -1;
        }
        return result;
    }
    return __real_close(fd);
}

int main(int argc, char **argv)
{
    char start;
    if (argc != 3 || ::read(0, &start, 1) != 1)
        return 2;
    const int64_t now = CE_HandoffNow();
    const std::string text = launchText(now, "");
    CE_Handoff handoff;
    const char *error = NULL;
    if (!CE_HandoffParse(text.data(), text.size(), now, &handoff, &error))
        return 3;
    const std::string mode(argv[1]);
    test_path = argv[2];
    inject_failure = mode == "incomplete" || mode == "swap";
    replace_during_write = mode == "swap";
    inject_short_write = mode == "short" || mode == "truncate-fail";
    inject_truncate_failure = mode == "truncate-fail";
    inject_close_failure = mode == "close-fail";
    const bool ok = CE_HandoffWrite(test_path, &handoff) != 0;
    if ((mode == "incomplete" || mode == "swap" || mode == "short" ||
         mode == "truncate-fail" || mode == "close-fail") && !injected)
        return 4;
    return ok ? 0 : 1;
}
