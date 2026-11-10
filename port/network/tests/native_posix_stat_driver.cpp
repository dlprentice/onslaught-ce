// SPDX-License-Identifier: GPL-3.0-only
// Actual compatibility PathStamp/FileStamp/Scan with MS-layout callers and a separately native bridge.
#include "../compatibility.cpp"
#include <cstddef>
#include <fcntl.h>
#include <sys/stat.h>
static_assert(sizeof(CE_CompatibilityPreset) == 100 && offsetof(CE_CompatibilityPreset, index) == 0 &&
              offsetof(CE_CompatibilityPreset, name) == 4, "preset API changed");
static_assert(sizeof(CE_CompatibilityRequest) == 208 && offsetof(CE_CompatibilityRequest, contract) == 0 &&
              offsetof(CE_CompatibilityRequest, level) == 4 && offsetof(CE_CompatibilityRequest, players) == 8, "request API changed");
static_assert(sizeof(CE_CompatibilityResult) == 48 && offsetof(CE_CompatibilityResult, digest) == 0 &&
              offsetof(CE_CompatibilityResult, files) == 32 && offsetof(CE_CompatibilityResult, bytes) == 40, "result API changed");
extern "C" FILE *port_fopen(const char *, const char *) { std::abort(); }
extern "C" int PortCachedContentRoots(char *, size_t, char *, size_t) { std::abort(); }
void Print(const Stamp &s) {
    std::printf("[%llu,%llu,%llu,%llu,%llu,%llu]", (unsigned long long)s.device, (unsigned long long)s.identity,
        (unsigned long long)s.identity_high, (unsigned long long)s.bytes, (unsigned long long)s.modified,
        (unsigned long long)s.changed);
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    if (!std::strcmp(argv[1], "legacy")) {
        struct stat st{};
        if (lstat(argv[2], &st)) return 3;
        std::printf("%llu\n", (unsigned long long)st.st_size);
        return 0;
    }
    if (!std::strcmp(argv[1], "stamp")) {
        Stamp path{}, file{}, native{};
        FILE *f = std::fopen(argv[2], "rb"); if (!f || std::fseek(f, 3, SEEK_SET)) return 4;
        long before = std::ftell(f);
        if (!PathStamp(fs::path(argv[2]), path) || !FileStamp(f, file) || before != std::ftell(f)) return 5;
        int fd = fileno(f); if (!NativeStamp(intptr_t(fd), native) || !(native == path)) return 12;
        uint64_t guarded[8] = {11,22,33,44,55,66,77,88};
        if (!CE_NativePosixFdStamp(fd, guarded + 1) || guarded[0] != 11 || guarded[7] != 88) return 6;
        std::fclose(f);
        std::printf("{\"path\":"); Print(path); std::printf(",\"fd\":"); Print(file);
        std::printf(",\"offset_preserved\":true,\"api\":[100,208,48,40]}\n"); return 0;
    }
    if (!std::strcmp(argv[1], "refuse")) {
        uint64_t out[6] = {1,2,3,4,5,6}, before[6]; std::memcpy(before, out, sizeof out);
        if (CE_NativePosixPathStamp(argv[2], out) || std::memcmp(before, out, sizeof out) ||
            CE_NativePosixFdStamp(-1, out) || std::memcmp(before, out, sizeof out) ||
            CE_NativePosixPathStamp(nullptr, out) || CE_NativePosixFdStamp(-1, nullptr)) return 7;
        return 0;
    }
    if (!std::strcmp(argv[1], "scan")) {
        Tree tree; char error[192] = {};
        if (!Scan(fs::path(argv[2]) / "data", "data", 1, tree, error, sizeof error)) return 8;
        std::printf("%zu\n", tree.size()); return 0;
    }
    if (!std::strcmp(argv[1], "bound")) {
        static_assert(kFileBytes == uint64_t(4)*1024*1024*1024 && kTotalBytes == uint64_t(16)*1024*1024*1024, "bounds changed");
        for (size_t size : {size_t(1), size_t(8), size_t(192)}) {
            unsigned char bytes[194]; std::memset(bytes, 0xa5, sizeof bytes); Tree tree;
            if (Scan(fs::path(argv[2]) / "data", "data", 1, tree, reinterpret_cast<char *>(bytes + 1), size)) return 9;
            if (bytes[0] != 0xa5 || bytes[size+1] != 0xa5 || !std::memchr(bytes + 1, 0, size)) return 10;
            if (size == 192) std::printf("%s\n", bytes + 1);
        }
        return 0;
    }
    return 11;
}
