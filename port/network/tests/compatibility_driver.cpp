// SPDX-License-Identifier: GPL-3.0-only
// Synthetic fixtures use the real Linux path and file layer. No game data or game process is needed.
#include "compatibility.h"
#include "handles.h"
#include "fixture_guard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" int PortCachedContentRoots(char *, size_t, char *, size_t);

PortHandle *PortHandleNew(PortHandleKind kind, size_t size) {
    PortHandle *p = static_cast<PortHandle *>(std::calloc(1, size));
    if (p) p->kind = kind;
    return p;
}
void PortHandleFree(PortHandle *p) { std::free(p); }
PortHandle *PortCheck(HANDLE h, int kind) {
    PortHandle *p = static_cast<PortHandle *>(h);
    return p && (!kind || p->kind == kind) ? p : nullptr;
}
void PortSetLastError(DWORD) {}

static unsigned preflight_reads;
extern "C" FILE *__real_port_fopen(const char *, const char *);
extern "C" FILE *__wrap_port_fopen(const char *name, const char *mode) {
    ++preflight_reads;
    return __real_port_fopen(name, mode);
}

extern "C" size_t __real_fread(void *, size_t, size_t, FILE *);
extern "C" size_t __wrap_fread(void *ptr, size_t size, size_t count, FILE *file) {
    if (std::getenv("COMPAT_TEST_SHORT_READ")) {
        unsetenv("COMPAT_TEST_SHORT_READ");
        return 0; // Neither EOF nor a successful complete read.
    }
    size_t n = __real_fread(ptr, size, count, file);
    if (const char *target = std::getenv("COMPAT_TEST_MUTATE")) {
        FILE *out = std::fopen(target, "wb");
        if (!out) std::abort();
        std::fputs("changed during the scan", out);
        if (std::fclose(out)) std::abort();
        unsetenv("COMPAT_TEST_MUTATE");
    }
    return n;
}
int main(int argc, char **argv) {
    if (!FixtureGuard()) return 8;
    if (argc == 2 && !std::strcmp(argv[1], "--path-selftests")) {
        struct Case { const char *in; const char *expected; } cases[] = {
            {"data/resources/meshes/m_authored.aya", "data/resources/meshes/m_authored.aya"},
            {"DaTa\\Resources\\852_res_PC.aya", "data/resources/852_res_pc.aya"},
            {"data/sounds/sounds.sfx", "data/sounds/sounds.sfx"},
            {"data/music/BEA 08(Master).wma", "data/music/bea 08(master).wma"},
            {"data\\resources/mesh.aya", "data/resources/mesh.aya"},
            {"../data/file", nullptr}, {"/data/file", nullptr}, {"\\data\\file", nullptr},
            {"C:\\data\\file", nullptr}, {"\\\\server\\data\\file", nullptr},
            {"data/../file", nullptr}, {"data/./file", nullptr}, {"data//file", nullptr},
            {"data/\\file", nullptr}, {"data/file/", nullptr}, {"data", nullptr},
            {"savegames/private.bes", nullptr}, {"data/private.bes", nullptr},
            {"data/file:stream", nullptr}, {"data/file.", nullptr}, {"data/CON.aya", nullptr},
            {"autoexec.con", nullptr}, {"data/.hidden", nullptr}, {"data/nonascii-\xc3\xa9", nullptr}
        };
        for (const auto &test : cases) {
            char out[241]; std::memset(out, 0xab, sizeof out);
            int ok = CE_CompatibilityNormalizeDataPath(test.in, std::strlen(test.in), out, sizeof out);
            if (ok != (test.expected != nullptr) || (ok ? std::strcmp(out, test.expected) != 0 : out[0] != 0)) return 4;
        }
        const char nul[] = {'d','a','t','a','/','f',0,'x'};
        char out[241];
        if (CE_CompatibilityNormalizeDataPath(nul, sizeof nul, out, sizeof out) || out[0]) return 5;
        if (CE_CompatibilityNormalizeDataPath("data/file", 9, out, 2) || out[0]) return 6;
        if (CE_CompatibilityNormalizeDataPath(nullptr, 9, out, sizeof out) || out[0]) return 7;
        std::puts("PASS path admission cases");
        return 0;
    }
    if (argc != 7) return 2;
    if (!std::getenv("COMPAT_TEST_UNINITIALIZED")) PortPathsInit();
    if (const char *alternative = std::getenv("COMPAT_TEST_ALTERNATIVE_GAME"))
        setenv("BEA_GAME_DIR", alternative, 1); // Cached real reader must not hash a different inventory silently.
    if (const char *alternative = std::getenv("COMPAT_TEST_ALTERNATIVE_USER"))
        setenv("BEA_USER_DIR", alternative, 1);
    if (std::getenv("COMPAT_TEST_CACHE_QUERY")) {
        char game[4096], user[4096];
        if (!PortCachedContentRoots(game, sizeof game, user, sizeof user) ||
            std::strcmp(game, std::getenv("BEA_GAME_DIR")) || std::strcmp(user, std::getenv("BEA_USER_DIR"))) return 9;
        char g[4096] = "sentinel", u[4096] = "sentinel";
        if (PortCachedContentRoots(g, 1, u, sizeof u) || g[0] || u[0]) return 10;
        if (PortCachedContentRoots(g, sizeof g, u, 1) || g[0] || u[0]) return 11;
        if (PortCachedContentRoots(nullptr, 0, u, sizeof u) || u[0]) return 12;
        if (!PortCachedContentRoots(g, sizeof g, u, sizeof u) || std::strcmp(g, game) || std::strcmp(u, user)) return 13;
    }
    CE_CompatibilityRequest request{};
    request.contract = static_cast<CE_CompatibilityContract>(std::strtoul(argv[1], nullptr, 10));
    request.level = std::strtoul(argv[2], nullptr, 10);
    for (int i = 0; i != 2; ++i) {
        request.players[i].index = std::strtoul(argv[3 + 2 * i], nullptr, 10);
        std::strncpy(request.players[i].name, argv[4 + 2 * i], sizeof request.players[i].name);
    }
    CE_CompatibilityResult result;
    std::memset(&result, 0xab, sizeof result);
    char error[160];
    if (!CE_CompatibilityCompute(&request, &result, error, sizeof error)) {
        const CE_CompatibilityResult zero{};
        if (std::memcmp(&result, &zero, sizeof result)) return 3;
        std::printf("REFUSED %s; reader opens=%u\n", error, preflight_reads);
        return 1;
    }
    for (uint8_t c : result.digest) std::printf("%02x", c);
    std::printf(" %u %llu\n", result.files, static_cast<unsigned long long>(result.bytes));
    return 0;
}
