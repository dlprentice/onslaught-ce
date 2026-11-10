// SPDX-License-Identifier: GPL-3.0-only
// Actual admission and Linux overlay, with authored data and explicit reader-hook boundaries only.
#define _FILE_OFFSET_BITS 64
#define NOMINMAX
#include "compatibility.h"
#include "handles.h"
#include "fixture_guard.h"
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

PortHandle *PortHandleNew(PortHandleKind kind, size_t size) {
    PortHandle *p = static_cast<PortHandle *>(std::calloc(1, size)); if (p) p->kind = kind; return p;
}
void PortHandleFree(PortHandle *p) { std::free(p); }
PortHandle *PortCheck(HANDLE h, int kind) { auto p = static_cast<PortHandle *>(h); return p && (!kind || p->kind == kind) ? p : nullptr; }
static DWORD last_file_error;
void PortSetLastError(DWORD error) { last_file_error = error; }
static unsigned checks;
static std::atomic<unsigned> reader_opens{}, hook_refusals{};
static char error[160];
static CE_CompatibilityResult initial{};
static bool deny_read_open;
extern "C" FILE *port_fopen(const char *, const char *);
extern "C" FILE *bea_crt_fopen(const char *, const char *);
extern "C" int PortCachedContentRoots(char *, size_t, char *, size_t);
static void Check(bool ok, const char *message) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL %s (%s)\n", message, error); std::exit(2); } }
static void Write(const std::string &path, const char *text) {
    FILE *f = std::fopen(path.c_str(), "wb"); Check(f, "authored sentinel create");
    Check(std::fwrite(text, 1, std::strlen(text), f) == std::strlen(text) && !std::fclose(f), "authored sentinel write");
}
static bool Zero(const CE_CompatibilityResult &result) { CE_CompatibilityResult zero{}; return !std::memcmp(&zero, &result, sizeof zero); }
extern "C" FILE *__real_port_fopen(const char *, const char *);
extern "C" int __real_CE_CompatibilityAuthoredOpenFailed(const char *, char *, size_t);
extern "C" int __wrap_CE_CompatibilityAuthoredOpenFailed(const char *name, char *error, size_t size) {
    int result = __real_CE_CompatibilityAuthoredOpenFailed(name, error, size);
    // State changes come only from the real helper. Force an unrelated side effect afterwards
    // so equality assertions actually test the production reader's preservation of its error.
    errno = ERANGE;
    return result;
}
extern "C" FILE *__real_fopen(const char *, const char *);
extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (deny_read_open && std::strstr(path, "default physics.dat")) { errno = EACCES; return nullptr; }
    return __real_fopen(path, mode);
}
extern "C" int __real_open(const char *, int, ...);
extern "C" int __wrap_open(const char *path, int flags, ...) {
    if (deny_read_open && std::strstr(path, "default physics.dat")) { errno = EACCES; return -1; }
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}
extern "C" FILE *__wrap_port_fopen(const char *name, const char *mode) {
    char why[160]; bool writes = mode[0] == 'w' || mode[0] == 'a' || std::strchr(mode, '+');
    if (!CE_CompatibilityObserveAuthoredRead(name, writes, why, sizeof why)) { ++hook_refusals; return nullptr; }
    ++reader_opens; FILE *f = __real_port_fopen(name, mode);
    if (f && !CE_CompatibilityValidateOpenedFile(name, f, why, sizeof why)) { ++hook_refusals; std::fclose(f); return nullptr; }
    return f;
}
extern "C" ssize_t __real_pread64(int, void *, size_t, off_t);
extern "C" ssize_t __wrap_pread64(int fd, void *out, size_t size, off_t offset) {
    if (std::getenv("ADMISSION_SHORT_NATIVE")) { unsetenv("ADMISSION_SHORT_NATIVE"); return 0; }
    ssize_t n = __real_pread64(fd, out, size, offset);
    if (n > 0 && std::getenv("ADMISSION_BAD_NATIVE")) {
        unsetenv("ADMISSION_BAD_NATIVE"); static_cast<unsigned char *>(out)[0] ^= 1;
    }
    return n;
}
// The contract under test: level 852's by default, the lockstep co-op's at COMPAT_TEST_LEVEL when COMPAT_TEST_LOCKSTEP is set.
static CE_CompatibilityContract TestContract() { return std::getenv("COMPAT_TEST_LOCKSTEP") ? CE_COMPAT_LOCKSTEP_PROTOCOL1 : CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5; }
static uint32_t TestLevel() { const char *level = std::getenv("COMPAT_TEST_LEVEL"); return level ? static_cast<uint32_t>(std::atoi(level)) : 852; }
static void Prepare() { Check(CE_CompatibilityPrepare(TestContract(), TestLevel(), &initial, error, sizeof error), "fresh actual-reader preflight"); }
static void Failed() {
    CE_CompatibilityResult after; std::memset(&after, 0xab, sizeof after);
    Check(!CE_CompatibilityPrepared(&after, error, sizeof error) && Zero(after), "failure cannot return context");
    Check(!CE_CompatibilityCheckBoundary(error, sizeof error), "failure stays sticky at boundary");
    CE_CompatibilityRetire();
    Check(!CE_CompatibilityPrepare(CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5, 852, &after, error, sizeof error) && Zero(after), "retirement cannot rearm");
}
int main(int argc, char **argv) {
    if (!FixtureGuard() || argc != 2) return 1;
    const std::string mode = argv[1], game = std::getenv("BEA_GAME_DIR"), user = std::getenv("BEA_USER_DIR");
    if (mode.rfind("rootguard", 0) == 0) {
        if (mode == "rootguard-absent") setenv("BEA_USER_DIR", (user + "/absent-user").c_str(), 1);
        if (mode == "rootguard-relative") setenv("BEA_GAME_DIR", "relative-game", 1);
        if (mode == "rootguard-same") setenv("BEA_USER_DIR", game.c_str(), 1);
        char g[4096], u[4096];
        Check(CE_CompatibilityValidateLauncherRoots(error, sizeof error) == (mode == "rootguard"), "pre-init root guard admits only existing distinct absolute roots");
        Check(!PortCachedContentRoots(g, sizeof g, u, sizeof u) && !reader_opens, "root guard cannot initialize or open through the game reader");
        Check(access((user + "/absent-user").c_str(), F_OK) != 0, "pre-init guard creates no user directory");
        std::printf("PASS %s %u none 0 0\n", argv[1], checks); return 0;
    }
    if (mode != "uninitialized") PortPathsInit();
    if (mode == "wrong-contract" || mode == "old-world-contract" || mode == "wrong-level") {
        Check(!CE_CompatibilityPrepare(mode == "wrong-contract" ? CE_COMPAT_LEVEL852_WORLD9_INPUT5_CONTROL4 : mode == "old-world-contract" ? CE_COMPAT_LEVEL852_WORLD10_INPUT5_CONTROL5 : CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5,
                mode == "wrong-level" ? 100 : 852, &initial, error, sizeof error) && Zero(initial), "unsupported schema or level refuses without context");
        Failed(); std::printf("PASS %s %u none 0 0\n", argv[1], checks); return 0;
    }
    if (mode == "prepare-refuses") {
        Check(!CE_CompatibilityPrepare(TestContract(), TestLevel(), &initial, error, sizeof error) && Zero(initial), "an unsupported level, contract or incomplete content refuses without context");
        Failed(); std::printf("PASS %s %u none 0 0\n", argv[1], checks); return 0;
    }
    if (mode == "prior-read") { FILE *f = port_fopen("data/default physics.dat", "rb"); Check(f, "ordinary prior offline read"); std::fclose(f); }
    if (mode == "prior-console" || mode == "prior-variable" || mode == "prior-script")
        Check(CE_CompatibilityConsoleOperation(mode == "prior-console" ? CE_COMPAT_CONSOLE_EXECUTE : mode == "prior-script" ? CE_COMPAT_CONSOLE_SCRIPT : CE_COMPAT_CONSOLE_SET_VARIABLE,
                "g_framelength 3", 15, error, sizeof error), "ordinary offline console operation");
    if (mode == "prior-runtime") Check(CE_CompatibilityRuntimeMutation(error, sizeof error), "ordinary offline runtime mutation");
    if (mode == "prior-unsupported") Check(CE_CompatibilityUnsupportedReader(error, sizeof error), "ordinary offline unsupported reader");
    if (mode == "prior-read" || mode == "prior-console" || mode == "prior-variable" || mode == "prior-script" || mode == "prior-runtime" || mode == "prior-unsupported" || mode == "uninitialized" || mode == "undefined-id") {
        Check(!CE_CompatibilityPrepare(CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5, 852, &initial, error, sizeof error) && Zero(initial), "unproven history or roots cannot prepare"); Failed();
    } else {
        Check(CE_CompatibilityConsoleOperation(CE_COMPAT_CONSOLE_EXECUTE, "", 0, error, sizeof error), "empty command does not dirty initial process");
        if (mode == "raw-overlay-miss") {
            Check(!mkdir((user + "/data").c_str(), 0700), "create authored overlay directory");
            Write(user + "/data/overlay-only.asset", "authored overlay only");
        }
        Prepare();
        if (mode == "golden" || mode == "empty-script") {
            Check(CE_CompatibilityCheckBoundary(error, sizeof error), "actual-reader boundary unchanged");
            CE_CompatibilityResult copied{}; Check(CE_CompatibilityPrepared(&copied, error, sizeof error) && !std::memcmp(&copied, &initial, sizeof initial), "immutable prepared context");
            Check(!hook_refusals && reader_opens >= 10, "module-owned scan reenters real hooks without bypass setting");
        } else if (mode == "file-offset") {
            FILE *f = port_fopen("data/default physics.dat", "rb"); Check(f, "actual buffered file open");
            Check(std::fgetc(f) == 'p' && std::fgetc(f) == 'h', "buffered prefix read");
            long offset = std::ftell(f); Check(offset == 2, "buffered prefix offset");
            Check(CE_CompatibilityValidateOpenedFile(".\\DaTa\\default physics.dat", f, error, sizeof error) && std::ftell(f) == offset && std::fgetc(f) == 'y', "FILE validation preserves buffered offset");
            Check(std::ungetc('Q', f) == 'Q' && CE_CompatibilityValidateOpenedFile("data/default physics.dat", f, error, sizeof error) && std::fgetc(f) == 'Q', "FILE validation preserves ungetc state");
            while (std::fgetc(f) != EOF) {} offset = std::ftell(f);
            Check(std::feof(f) && !std::ferror(f) && CE_CompatibilityValidateOpenedFile("data/default physics.dat", f, error, sizeof error) && std::feof(f) && !std::ferror(f) && std::ftell(f) == offset, "FILE validation preserves EOF and error indicators");
            Check(!std::fclose(f), "caller closes own FILE");
        } else if (mode == "native-offset" || mode == "native-short" || mode == "native-bad-digest") {
            HANDLE h = CreateFileA("data/default physics.dat", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(h != INVALID_HANDLE_VALUE, "actual Linux native reader open"); auto *f = static_cast<PortFile *>(h); char bytes[3];
            Check(read(f->fd, bytes, 3) == 3 && lseek(f->fd, 0, SEEK_CUR) == 3, "native prefix consumed");
            if (mode == "native-short") setenv("ADMISSION_SHORT_NATIVE", "1", 1);
            if (mode == "native-bad-digest") setenv("ADMISSION_BAD_NATIVE", "1", 1);
            int admitted = CE_CompatibilityValidateOpenedNative("data/default physics.dat", f->fd, error, sizeof error);
            Check(lseek(f->fd, 0, SEEK_CUR) == 3 && read(f->fd, bytes, 1) == 1 && bytes[0] == 's', "native proof never moves or closes caller fd");
            Check(admitted == (mode == "native-offset"), "native complete digest required"); close(f->fd); PortHandleFree(&f->h);
            if (!admitted) Failed();
        } else if (mode == "wrong-handle") {
            int fd = open((game + "/data/worldheaders.dat").c_str(), O_RDONLY); Check(fd >= 0, "wrong real native handle");
            Check(!CE_CompatibilityValidateOpenedNative("data/default physics.dat", fd, error, sizeof error), "wrong opened identity refuses"); close(fd); Failed();
        } else if (mode == "write" || mode == "traversal" || mode == "absolute" || mode == "unknown-reader" || mode == "repeated-dot" || mode == "embedded-dot") {
            const char *logical = mode == "traversal" ? "data/../outside" : mode == "absolute" ? "/data/file" : mode == "unknown-reader" ? "elsewhere/custom.dat" : mode == "repeated-dot" ? "././data/file" : mode == "embedded-dot" ? "data/./file" : "data/default physics.dat";
            Check(!CE_CompatibilityObserveAuthoredRead(logical, mode == "write", error, sizeof error), "preopen fence refuses before any side effect"); Failed();
        } else if (mode == "missing-probe") {
            Check(!port_fopen(".\\data\\optional-missing.asset", "rb") && !hook_refusals && CE_CompatibilityCheckBoundary(error, sizeof error), "normal missing optional probe stays valid");
        } else if (mode == "raw-overlay-miss") {
            Check(!chdir(game.c_str()), "raw CRT probe uses authored game cwd");
            Check(!bea_crt_fopen("data/overlay-only.asset", "rb"), "raw CRT miss is distinct from effective overlay absence");
            CE_CompatibilityResult copied{};
            Check(CE_CompatibilityPrepared(&copied, error, sizeof error), "raw overlay miss does not falsely revoke effective admission");
            FILE *file = __real_port_fopen("data/overlay-only.asset", "rb");
            Check(file, "real effective reader still opens admitted overlay input"); std::fclose(file);
        } else if (mode == "absent-file" || mode == "absent-native" || mode == "disappeared-file" ||
                   mode == "disappeared-native" || mode == "unreadable-file" || mode == "unreadable-native") {
            const bool absent = mode.rfind("absent-", 0) == 0, native = mode.find("native") != std::string::npos;
            const bool denied = mode.rfind("unreadable-", 0) == 0;
            const char *logical = absent ? ".\\data\\optional-missing.asset" : "data/default physics.dat";
            if (!absent && !denied) Check(!unlink((game + "/data/default physics.dat").c_str()), "remove only authored admitted fixture input");
            deny_read_open = denied;
            errno = 0;
            if (native) {
                Check(CreateFileA(logical, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) == INVALID_HANDLE_VALUE,
                      "real native reader returns its failed open");
                Check(last_file_error == (denied ? ERROR_ACCESS_DENIED : ERROR_FILE_NOT_FOUND), "native failed open preserves original mapped error");
                Check(errno == (denied ? EACCES : ENOENT), "native failed open also preserves original errno");
            } else {
                // Bypass __wrap_port_fopen: only the production reader can latch this failure.
                Check(!__real_port_fopen(logical, "rb"), "real stdio reader returns its failed open");
                Check(errno == (denied ? EACCES : ENOENT), "stdio failed open preserves original errno");
            }
            deny_read_open = false;
            Check(!hook_refusals, "fixture wrapper did not supply a compatibility failure");
            CE_CompatibilityResult copied; std::memset(&copied, 0xab, sizeof copied);
            if (absent) {
                Check(CE_CompatibilityPrepared(&copied, error, sizeof error) && !std::memcmp(&copied, &initial, sizeof copied), "never-present probe retains prepared context");
                Check(CE_CompatibilityCheckBoundary(error, sizeof error), "never-present probe keeps namespace valid");
            } else {
                Check(!CE_CompatibilityPrepared(&copied, error, sizeof error) && Zero(copied), "real failed open immediately revokes context before any boundary rescan");
                Failed();
            }
        } else if (mode == "late-file" || mode == "changed-file") {
            Write(game + (mode == "late-file" ? "/data/late.asset" : "/data/default physics.dat"), "replacement sentinel");
            // The production reader now owns the refusal before returning to this fixture wrapper.
            unsigned attempted = reader_opens;
            FILE *blocked = port_fopen(mode == "late-file" ? "data/late.asset" : "data/default physics.dat", "rb");
            Check(!blocked && reader_opens == attempted + 1 && hook_refusals == 0,
                  "real reader refuses changed successful open before wrapper receives bytes"); Failed();
        } else if (mode == "boundary-change" || mode == "boundary-shadow" || mode == "boundary-script") {
            if (mode == "boundary-shadow") { Check(!mkdir((user + "/data").c_str(), 0700), "create authored shadow directory"); Write(user + "/data/default physics.dat", "shadow"); }
            else Write(mode == "boundary-script" ? user + "/autoexec.con" : game + "/data/default physics.dat", "changed");
            Check(!CE_CompatibilityCheckBoundary(error, sizeof error), "late full effective namespace change refuses boundary"); Failed();
        } else if (mode == "console" || mode == "variable" || mode == "script" || mode == "runtime" || mode == "unsupported") {
            int result = mode == "unsupported" ? CE_CompatibilityUnsupportedReader(error, sizeof error) : mode == "runtime" ? CE_CompatibilityRuntimeMutation(error, sizeof error) :
                CE_CompatibilityConsoleOperation(mode == "console" ? CE_COMPAT_CONSOLE_EXECUTE : mode == "script" ? CE_COMPAT_CONSOLE_SCRIPT : CE_COMPAT_CONSOLE_SET_VARIABLE, "set x 3", 7, error, sizeof error);
            Check(!result, "armed command or unsupported reader refuses before effects"); Failed();
        } else if (mode == "empty-command") {
            Check(CE_CompatibilityConsoleOperation(CE_COMPAT_CONSOLE_EXECUTE, "", 0, error, sizeof error) && CE_CompatibilityCheckBoundary(error, sizeof error), "empty automatic command remains no-op");
        } else if (mode == "changed-roots") {
            setenv("BEA_USER_DIR", game.c_str(), 1); CE_CompatibilityResult copied;
            Check(!CE_CompatibilityPrepared(&copied, error, sizeof error) && Zero(copied), "changed environment cannot replace cached admission"); Failed();
        } else if (mode == "retire" || mode == "retire-offline") {
            CE_CompatibilityRetire();
            if (mode == "retire-offline") {
                Check(CE_CompatibilityObserveAuthoredRead("offline/file", 1, error, sizeof error), "offline work after completed teardown allowed");
                Check(CE_CompatibilityConsoleOperation(CE_COMPAT_CONSOLE_EXECUTE, "echo ok", 7, error, sizeof error), "offline command after teardown allowed");
            }
            CE_CompatibilityResult copied; Check(!CE_CompatibilityPrepare(CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5, 852, &copied, error, sizeof error) && Zero(copied), "retired context cannot be resurrected");
        } else if (mode == "concurrent") {
            std::atomic<unsigned> good{}; std::vector<std::thread> threads;
            for (unsigned i = 0; i < 3; ++i) threads.emplace_back([&] {
                char why[160]; for (unsigned n = 0; n < 4; ++n) {
                    FILE *f = port_fopen("data/default physics.dat", "rb"); if (!f) return;
                    if (std::fgetc(f) == 'p' && CE_CompatibilityValidateOpenedFile("data/default physics.dat", f, why, sizeof why)) ++good;
                    std::fclose(f);
                }
            });
            Check(CE_CompatibilityCheckBoundary(error, sizeof error), "boundary serializes with media validation");
            for (auto &thread : threads) thread.join(); Check(good == 12 && !hook_refusals, "concurrent callbacks retain valid process-owned context");
        } else return 3;
    }
    std::printf("PASS %s %u ", argv[1], checks); for (uint8_t c : initial.digest) std::printf("%02x", c);
    std::printf(" %u %llu\n", initial.files, static_cast<unsigned long long>(initial.bytes)); return 0;
}
