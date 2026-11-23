// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_archive_scanner.cpp: Crash-isolated --archive-scan quarantine scanner
// and deterministic cache reader/writer (archive-scan-v1.txt). Runs strictly in host
// memory using malloc/free + native OS file descriptors (zero D3D9/SDL/sound/retail
// heap init, zero operator new, zero fopen/fclose CRT table touches).
#include "ce_archive_scanner.h"
#include "ce_asset_catalog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Platform path helpers (strong C++ linkage from port/platform/{linux,windows}/paths.cpp).
void PortPathsNamesOnly();
const char *PortGameDir();
const char *PortUserDir();

namespace CeArchiveScanner {
namespace {

void SetErr(char *errBuf, size_t errCap, const char *msg) {
    if (errBuf && errCap) std::snprintf(errBuf, errCap, "%s", msg ? msg : "");
}

bool JoinPath(char *out, size_t outCap, const char *base, const char *rel) {
    if (!out || !outCap || !base || !*base || !rel || !*rel) return false;
    const size_t bl = std::strlen(base);
    const bool hasSlash = (base[bl - 1] == '/' || base[bl - 1] == '\\');
    const int n = std::snprintf(out, outCap, hasSlash ? "%s%s" : "%s/%s", base, rel);
    return n > 0 && static_cast<size_t>(n) < outCap;
}

// Host-malloc dynamically grown byte buffer (zero C++ operator new / STL).
struct ByteWriter {
    char *data;
    size_t len;
    size_t cap;
    bool oom;

    void Init() {
        data = NULL;
        len = 0;
        cap = 0;
        oom = false;
    }

    void Free() {
        if (data) std::free(data);
        data = NULL;
        len = 0;
        cap = 0;
        oom = false;
    }

    bool Reserve(size_t extra) {
        if (oom) return false;
        if (extra > (size_t(16u) << 20) || len > (size_t(16u) << 20) - extra) {
            oom = true;
            return false;
        }
        const size_t need = len + extra + 1u;
        if (need <= cap) return true;
        size_t next = cap ? cap * 2u : 8192u;
        while (next < need) next *= 2u;
        char *grown = static_cast<char *>(std::realloc(data, next));
        if (!grown) {
            oom = true;
            return false;
        }
        data = grown;
        cap = next;
        return true;
    }

    void Append(const char *s, size_t n) {
        if (!n || !Reserve(n)) return;
        std::memcpy(data + len, s, n);
        len += n;
        data[len] = '\0';
    }

    void AppendStr(const char *s) {
        if (s) Append(s, std::strlen(s));
    }
};

bool WriteWholeFile(const char *path, const char *bytes, size_t len) {
    if (!path || !*path || (!bytes && len > 0)) return false;
    char tmpPath[540];
    const int tn = std::snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    const bool useTmp = (tn > 0 && static_cast<size_t>(tn) < sizeof(tmpPath));
    const char *target = useTmp ? tmpPath : path;

#if defined(_WIN32)
    HANDLE h = CreateFileA(target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t writtenTotal = 0;
    bool ok = true;
    while (writtenTotal < len) {
        const size_t chunk = (len - writtenTotal) > 65536u ? 65536u : (len - writtenTotal);
        DWORD wrote = 0;
        if (!WriteFile(h, bytes + writtenTotal, static_cast<DWORD>(chunk), &wrote, NULL) || wrote == 0) {
            ok = false;
            break;
        }
        writtenTotal += static_cast<size_t>(wrote);
    }
    CloseHandle(h);
    if (!ok) {
        DeleteFileA(target);
        return false;
    }
    if (useTmp) {
        if (!MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileA(tmpPath);
            return false;
        }
    }
    return true;
#else
    const int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t writtenTotal = 0;
    bool ok = true;
    while (writtenTotal < len) {
        const ssize_t w = write(fd, bytes + writtenTotal, len - writtenTotal);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            ok = false;
            break;
        }
        writtenTotal += static_cast<size_t>(w);
    }
    if (close(fd) != 0) ok = false;
    if (!ok) {
        unlink(target);
        return false;
    }
    if (useTmp) {
        if (rename(tmpPath, path) != 0) {
            unlink(tmpPath);
            return false;
        }
    }
    return true;
#endif
}

bool ReadWholeFileMalloc(const char *path, char **outData, size_t *outSize, size_t maxBytes) {
    *outData = NULL;
    *outSize = 0;
    if (!path || !*path) return false;

#if defined(_WIN32)
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || static_cast<uint64_t>(sz.QuadPart) > maxBytes) {
        CloseHandle(h);
        return false;
    }
    const size_t n = static_cast<size_t>(sz.QuadPart);
    char *buf = static_cast<char *>(std::malloc(n + 1u));
    if (!buf) {
        CloseHandle(h);
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < n) {
        const size_t chunk = (n - done) > 65536u ? 65536u : (n - done);
        DWORD got = 0;
        if (!ReadFile(h, buf + done, static_cast<DWORD>(chunk), &got, NULL) || got == 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(got);
    }
    CloseHandle(h);
    if (!ok) {
        std::free(buf);
        return false;
    }
    buf[n] = '\0';
    *outData = buf;
    *outSize = n;
    return true;
#else
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st = {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<uint64_t>(st.st_size) > maxBytes) {
        close(fd);
        return false;
    }
    const size_t n = static_cast<size_t>(st.st_size);
    char *buf = static_cast<char *>(std::malloc(n + 1u));
    if (!buf) {
        close(fd);
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < n) {
        const ssize_t r = read(fd, buf + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(r);
    }
    close(fd);
    if (!ok) {
        std::free(buf);
        return false;
    }
    buf[n] = '\0';
    *outData = buf;
    *outSize = n;
    return true;
#endif
}

uint64_t ComputeCatalogFingerprint(const CeAssetCatalog *catalog) {
    uint64_t h = kFnv64Basis;
    const size_t count = PortCeAssetCatalogCount(catalog);
    uint64_t modelCount = 0;
    uint64_t texCount = 0;
    for (size_t i = 0; i < count; ++i) {
        const int kind = PortCeAssetCatalogKind(catalog, i);
        if (kind == CE_ASSET_MODEL) {
            ++modelCount;
            h = HashStringLower(h, PortCeAssetCatalogPath(catalog, i));
            h = HashU64(h, PortCeAssetCatalogSize(catalog, i));
        } else if (kind == CE_ASSET_TEXTURE) {
            ++texCount;
        }
    }
    h = HashU64(h, modelCount);
    h = HashU64(h, texCount);
    return h ? h : 1ULL;
}

} // namespace

bool ReadWholeFileIntoArena(const char *fullPath, SwapArena *arena,
                            unsigned char **outData, size_t *outSize, size_t maxBytes) {
    if (!fullPath || !*fullPath || !arena || !arena->base || !outData || !outSize) return false;
    *outData = NULL;
    *outSize = 0;
    const size_t mark = arena->used;

#if defined(_WIN32)
    HANDLE h = CreateFileA(fullPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || static_cast<uint64_t>(sz.QuadPart) > maxBytes) {
        CloseHandle(h);
        return false;
    }
    const size_t n = static_cast<size_t>(sz.QuadPart);
    unsigned char *buf = static_cast<unsigned char *>(arena->Alloc(n, 16u));
    if (!buf) {
        CloseHandle(h);
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < n) {
        const size_t chunk = (n - done) > 65536u ? 65536u : (n - done);
        DWORD got = 0;
        if (!ReadFile(h, buf + done, static_cast<DWORD>(chunk), &got, NULL) || got == 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(got);
    }
    CloseHandle(h);
    if (!ok) {
        arena->RewindTo(mark);
        return false;
    }
    *outData = buf;
    *outSize = n;
    return true;
#else
    const int fd = open(fullPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st = {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        static_cast<uint64_t>(st.st_size) > maxBytes) {
        close(fd);
        return false;
    }
    const size_t n = static_cast<size_t>(st.st_size);
    unsigned char *buf = static_cast<unsigned char *>(arena->Alloc(n, 16u));
    if (!buf) {
        close(fd);
        return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < n) {
        const ssize_t r = read(fd, buf + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(r);
    }
    close(fd);
    if (!ok) {
        arena->RewindTo(mark);
        return false;
    }
    *outData = buf;
    *outSize = n;
    return true;
#endif
}

bool LoadQuarantineCache(const char *cachePath, uint64_t expectedFingerprint,
                         QuarantineList *outList) {
    if (!cachePath || !*cachePath || !outList) return false;
    outList->Clear();
    char *raw = NULL;
    size_t rawLen = 0;
    if (!ReadWholeFileMalloc(cachePath, &raw, &rawLen, 2u * 1024u * 1024u))
        return false;
    const bool ok = ParseQuarantineCacheText(raw, rawLen, expectedFingerprint, outList);
    std::free(raw);
    return ok;
}

bool ScanGameDir(const char *gameDir, const char *cacheOutPath,
                 ScanSummary *outSummary, char *errBuf, size_t errCap) {
    SetErr(errBuf, errCap, "");
    if (outSummary) std::memset(outSummary, 0, sizeof(*outSummary));
    if (!gameDir || !*gameDir) {
        SetErr(errBuf, errCap, "game folder path is empty");
        return false;
    }

    char catErr[256] = {};
    CeAssetCatalog *catalog = PortCeAssetCatalogOpenKinds(gameDir, CE_ASSET_MODEL | CE_ASSET_TEXTURE,
                                                          catErr, sizeof(catErr));
    if (!catalog) {
        SetErr(errBuf, errCap, catErr[0] ? catErr : "failed to open asset catalog");
        return false;
    }

    SwapArena arena = {};
    if (!arena.Init(true)) {
        PortCeAssetCatalogClose(catalog);
        SetErr(errBuf, errCap, "failed to allocate 8 MiB swap arena");
        return false;
    }

    ScanSummary sum = {};
    sum.fingerprint = ComputeCatalogFingerprint(catalog);

    ByteWriter rows;
    rows.Init();

    const size_t totalAssets = PortCeAssetCatalogCount(catalog);
    for (size_t i = 0; i < totalAssets; ++i) {
        if (PortCeAssetCatalogKind(catalog, i) != CE_ASSET_MODEL) continue;
        const char *relPath = PortCeAssetCatalogPath(catalog, i);
        if (!relPath || !*relPath) continue;
        ++sum.total;

        arena.Reset();
        char fullPath[1024];
        if (!JoinPath(fullPath, sizeof(fullPath), gameDir, relPath)) {
            ++sum.quarantined;
            char line[384];
            std::snprintf(line, sizeof(line), "QUARANTINE\t%s\treason=path_too_long\n", relPath);
            rows.AppendStr(line);
            continue;
        }

        unsigned char *ayaData = NULL;
        size_t ayaSize = 0;
        if (!ReadWholeFileIntoArena(fullPath, &arena, &ayaData, &ayaSize, 2u * 1024u * 1024u)) {
            ++sum.quarantined;
            char line[384];
            std::snprintf(line, sizeof(line), "QUARANTINE\t%s\treason=read_failed\n", relPath);
            rows.AppendStr(line);
            continue;
        }

        ParsedMesh parsed = {};
        const ParseError perr = DecompressAndParseAyaMesh(ayaData, ayaSize, &arena, &parsed);
        if (perr != kParseOk || !parsed.bounds.valid) {
            ++sum.quarantined;
            char line[384];
            std::snprintf(line, sizeof(line), "QUARANTINE\t%s\treason=%s\n",
                          relPath, perr != kParseOk ? ParseErrorName(perr) : "invalid_bounds");
            rows.AppendStr(line);
            continue;
        }

        unsigned texOk = 0;
        for (uint32_t ti = 0; ti < parsed.textureCount; ++ti) {
            ++sum.texTotal;
            if (PortCeAssetCatalogFindTexture(catalog, parsed.textureNames[ti]) != SIZE_MAX) {
                ++texOk;
                ++sum.texResolved;
            }
        }

        ++sum.ok;
        if (parsed.arenaBytesUsed > sum.maxArenaBytes) sum.maxArenaBytes = parsed.arenaBytesUsed;
        char line[512];
        std::snprintf(line, sizeof(line),
                      "OK\t%s\tverts=%u\ttris=%u\tparts=%u\ttex=%u/%u\tspan=%.3f\tarena=%lu\n",
                      relPath, parsed.vertexCount, parsed.triangleCount, parsed.partCount,
                      texOk, parsed.textureCount, parsed.bounds.span,
                      static_cast<unsigned long>(parsed.arenaBytesUsed));
        rows.AppendStr(line);
    }

    if (arena.highWater > sum.maxArenaBytes) sum.maxArenaBytes = arena.highWater;
    arena.Shutdown();
    PortCeAssetCatalogClose(catalog);

    if (rows.oom) {
        rows.Free();
        SetErr(errBuf, errCap, "out of memory formatting scan cache");
        return false;
    }

    if (cacheOutPath && *cacheOutPath) {
        ByteWriter fileOut;
        fileOut.Init();
        char header[512];
        std::snprintf(header, sizeof(header),
                      "# Onslaught CE Archive Scan Cache v1\n"
                      "version=1\n"
                      "fingerprint=%016llx\n"
                      "total=%u\n"
                      "ok=%u\n"
                      "quarantined=%u\n"
                      "textures_resolved=%u\n"
                      "textures_total=%u\n"
                      "max_arena_bytes=%lu\n",
                      static_cast<unsigned long long>(sum.fingerprint),
                      sum.total, sum.ok, sum.quarantined,
                      sum.texResolved, sum.texTotal,
                      static_cast<unsigned long>(sum.maxArenaBytes));
        fileOut.AppendStr(header);
        if (rows.data && rows.len) fileOut.Append(rows.data, rows.len);
        const bool wrote = !fileOut.oom && WriteWholeFile(cacheOutPath, fileOut.data ? fileOut.data : "", fileOut.len);
        fileOut.Free();
        if (!wrote) {
            rows.Free();
            SetErr(errBuf, errCap, "failed to write quarantine cache file");
            return false;
        }
    }

    rows.Free();
    if (outSummary) *outSummary = sum;
    if (sum.total == 0u) {
        SetErr(errBuf, errCap, "no retail .msh.aya files found in game folder");
        return false;
    }
    return true;
}

} // namespace CeArchiveScanner

extern "C" int PortCeArchiveScanCli(int argc, char **argv) {
    const char *gameDir = NULL;
    const char *outPath = NULL;
    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) continue;
        if (std::strcmp(argv[i], "--game-dir") == 0 && i + 1 < argc) {
            gameDir = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            outPath = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("usage: bea --archive-scan [--game-dir DIR] [--out CACHE_FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "archive-scan: unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!gameDir || !*gameDir) gameDir = PortGameDir();
    char defaultOut[1024] = {};
    if (!outPath || !*outPath) {
        const char *userDir = PortUserDir();
        if (userDir && *userDir) {
            const size_t ul = std::strlen(userDir);
            const bool slash = (userDir[ul - 1] == '/' || userDir[ul - 1] == '\\');
            std::snprintf(defaultOut, sizeof(defaultOut),
                          slash ? "%sarchive-scan-v1.txt" : "%s/archive-scan-v1.txt", userDir);
            outPath = defaultOut;
        }
    }

    CeArchiveScanner::ScanSummary sum = {};
    char err[256] = {};
    if (!CeArchiveScanner::ScanGameDir(gameDir, outPath, &sum, err, sizeof(err))) {
        std::fprintf(stderr, "archive-scan: error: %s\n", err[0] ? err : "scan failed");
        return 1;
    }

    std::printf("archive-scan: %u/%u meshes ok, %u quarantined, textures %u/%u, peak_arena=%lu bytes (%.2f MiB), fingerprint=%016llx, cache=%s\n",
                sum.ok, sum.total, sum.quarantined,
                sum.texResolved, sum.texTotal,
                static_cast<unsigned long>(sum.maxArenaBytes),
                double(sum.maxArenaBytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(sum.fingerprint),
                (outPath && *outPath) ? outPath : "(none)");
    return 0;
}

#if defined(_WIN32) && defined(CE_VERSION)
// bea.exe --archive-scan: runs before WinMain with zero D3D9/window/sound/retail-heap init,
// matching the exact pre-WinMain pattern used by bea.exe --check-mods (mods.cpp).
namespace {
struct ArchiveScanCommand {
    ArchiveScanCommand() {
        if (__argc < 2 || std::strcmp(__argv[1], "--archive-scan") != 0)
            return;
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        const bool redirected = out && out != INVALID_HANDLE_VALUE &&
            GetFileType(out) != FILE_TYPE_UNKNOWN && !GetConsoleMode(out, &mode);
        if (AttachConsole(ATTACH_PARENT_PROCESS) && !redirected)
            (void)!freopen("CONOUT$", "w", stdout);
        PortPathsNamesOnly();
        const int status = PortCeArchiveScanCli(__argc - 2, __argv + 2);
        std::fflush(NULL);
        ExitProcess(static_cast<UINT>(status));
    }
} gArchiveScanCommand;
}
#endif
