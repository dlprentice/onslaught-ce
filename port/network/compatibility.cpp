// SPDX-License-Identifier: GPL-3.0-only
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
// FileIdInfo is a Windows 8+ API. CE already targets newer Windows; do not compile a weaker fallback.
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#elif _WIN32_WINNT < 0x0602
#error Network content identity requires the Windows FileIdInfo API
#endif
#endif
#include "compatibility.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <monocypher.h>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
FILE *port_fopen(const char *, const char *); // The game's real user-shadow/read-fallback reader.
int PortCachedContentRoots(char *, size_t, char *, size_t);
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include "native_posix_stat.h"
#include <unistd.h>
#endif

#ifndef CE_NETWORK_SIMULATION_ID
#define CE_NETWORK_SIMULATION_ID ""
#endif

namespace {
namespace fs = std::filesystem;
constexpr unsigned kFiles = 65536, kDepth = 32, kLogical = 240;
constexpr uint64_t kFileBytes = uint64_t(4) * 1024 * 1024 * 1024;
constexpr uint64_t kTotalBytes = uint64_t(16) * 1024 * 1024 * 1024;

// Only these scoped module-owned reader calls can reenter the platform hooks while the admission
// mutex is held. There is no exported setter, token, environment variable or general caller bypass.
thread_local unsigned internal_reads;
struct InternalRead {
    InternalRead() { ++internal_reads; }
    ~InternalRead() { --internal_reads; }
};

using HASH_CTX = crypto_blake2b_ctx;
void HashInit(HASH_CTX *hash) { crypto_blake2b_init(hash, 32); }
void HashUpdate(HASH_CTX *hash, const void *data, size_t size) {
    crypto_blake2b_update(hash, static_cast<const uint8_t *>(data), size);
}
void HashFinal(HASH_CTX *hash, uint8_t out[32]) { crypto_blake2b_final(hash, out); }

struct Stamp {
    uint64_t device, identity, identity_high, bytes, modified, changed;
    bool operator==(const Stamp &b) const {
        return device == b.device && identity == b.identity && identity_high == b.identity_high && bytes == b.bytes &&
               modified == b.modified && changed == b.changed;
    }
};
struct Entry {
    fs::path path;
    bool directory;
    Stamp stamp;
};
using Tree = std::map<std::string, Entry>;

bool Refuse(char *error, size_t size, const char *message) {
    if (error && size) std::snprintf(error, size, "%s", message);
    return false;
}
bool ReaderRootsMatch(const char *game, const char *user) {
    char actual_game[4096], actual_user[4096];
    return PortCachedContentRoots(actual_game, sizeof actual_game, actual_user, sizeof actual_user) &&
           !std::strcmp(game, actual_game) && !std::strcmp(user, actual_user);
}
bool Canonical(const std::string &name, std::string &out) {
    if (name.empty() || name == "." || name == "..") return false;
    out.clear();
    for (unsigned char c : name) {
        // Resource paths are ASCII. Reject host-dependent names, separators and NTFS aliases/streams.
        if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') return false;
        out.push_back(c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : char(c));
    }
    std::string stem = out.substr(0, out.find('.'));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
         stem[3] >= '1' && stem[3] <= '9')) return false;
    return name.back() != ' ' && name.back() != '.';
}
bool PrivateName(const std::string &name) {
    if (name[0] == '.' || name == "savegames" || name == "recordings" || name == "logs" || name == "reports") return true;
    const char *suffix[] = {".bes", ".bea", ".ini", ".log", ".dmp", ".key", ".pem"};
    for (const char *s : suffix) {
        size_t n = std::strlen(s);
        if (name.size() >= n && name.compare(name.size() - n, n, s) == 0) return true;
    }
    return false;
}

#if defined(_WIN32)
bool HandleStamp(HANDLE h, Stamp &out) {
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_ID_INFO file_id{};
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileInformationByHandleEx(h, FileIdInfo, &file_id, sizeof file_id)) return false;
    // Legacy FileIndex64 is not unique on every filesystem. Require the full supported identity;
    // an unavailable or unknown ID must refuse admission instead of falling back to the short form.
    bool zero = true, ones = true;
    for (unsigned char byte : file_id.FileId.Identifier) {
        zero = zero && byte == 0;
        ones = ones && byte == 0xff;
    }
    if (zero || ones || !file_id.VolumeSerialNumber || file_id.VolumeSerialNumber == UINT64_MAX) return false;
    uint64_t low, high;
    std::memcpy(&low, file_id.FileId.Identifier, sizeof low);
    std::memcpy(&high, file_id.FileId.Identifier + sizeof low, sizeof high);
    out = {file_id.VolumeSerialNumber, low, high,
           (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
           (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime,
           (uint64_t(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime};
    return true;
}
bool PathStamp(const fs::path &path, Stamp &out) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = HandleStamp(h, out);
    if (!CloseHandle(h)) ok = false;
    return ok;
}
bool FileStamp(FILE *file, Stamp &out) {
    return HandleStamp(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file))), out);
}
bool WindowsLauncherRoot(const char *root) {
    // The legacy reader caches each root in char[MAX_PATH] and calls MakeDirs during Init.
    // Check the exact original UTF-8 bytes before it can truncate a root and create another path.
    if (!root) return false;
    size_t bytes = std::strlen(root);
    if (bytes < 3 || bytes >= MAX_PATH || GetACP() != CP_UTF8 ||
        !((root[0] >= 'a' && root[0] <= 'z') || (root[0] >= 'A' && root[0] <= 'Z')) ||
        root[1] != ':' || (root[2] != '\\' && root[2] != '/')) return false;
    wchar_t wide[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, root, -1, wide, MAX_PATH)) return false;
    if (bytes == 3) return true;
    size_t start = 3;
    for (size_t end = 3; end <= bytes; ++end) {
        if (end != bytes && root[end] != '/' && root[end] != '\\') {
            unsigned char c = static_cast<unsigned char>(root[end]);
            if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
            continue;
        }
        size_t size = end - start;
        if (!size || (size == 1 && root[start] == '.') || (size == 2 && root[start] == '.' && root[start + 1] == '.') ||
            root[end - 1] == '.' || root[end - 1] == ' ') return false;
        start = end + 1;
    }
    return true;
}
#else
bool NativeStamp(const uint64_t values[6], Stamp &out) {
    out = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return true;
}
bool PathStamp(const fs::path &path, Stamp &out) {
    uint64_t values[6];
    return CE_NativePosixPathStamp(path.c_str(), values) && NativeStamp(values, out);
}
bool FileStamp(FILE *file, Stamp &out) {
    uint64_t values[6];
    return CE_NativePosixFdStamp(fileno(file), values) && NativeStamp(values, out);
}
#endif

bool Scan(const fs::path &directory, const std::string &logical, unsigned depth, Tree &tree,
          char *error, size_t size) {
    if (depth > kDepth) return Refuse(error, size, "content directory depth exceeds admission bound");
    std::error_code ec;
    fs::directory_iterator it(directory, ec), end;
    if (ec) return Refuse(error, size, "content directory cannot be enumerated");
    std::map<std::string, bool> names;
    for (; it != end; it.increment(ec)) {
        if (ec) return Refuse(error, size, "content directory enumeration failed");
        std::string name;
        if (!Canonical(it->path().filename().u8string(), name)) return Refuse(error, size, "content has a nonportable name");
        if (!names.emplace(name, true).second) return Refuse(error, size, "content has case-equivalent aliases in one root");
        if (PrivateName(name)) return Refuse(error, size, "private save, settings or log content is not admitted inside data");
        std::string child = logical + "/" + name;
        if (child.size() > kLogical) return Refuse(error, size, "content logical path exceeds admission bound");
        auto status = it->symlink_status(ec);
        if (ec || fs::is_symlink(status)) return Refuse(error, size, "content links or unreadable entries are not admitted");
#if defined(_WIN32)
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Refuse(error, size, "content reparse entries are not admitted");
#endif
        bool isdir = fs::is_directory(status);
        Stamp stamp{};
        if (!isdir && (!fs::is_regular_file(status) || !PathStamp(it->path(), stamp)))
#if defined(_WIN32)
            return Refuse(error, size, "content entry is not a readable regular file with a file identity (under Wine keep the game folder on drive C:)");
#else
            return Refuse(error, size, "content entry is not a readable regular file");
#endif
        if (stamp.bytes > kFileBytes) {
            // Only already-canonical data-relative names and counts; no absolute roots or file contents.
            if (error && size) std::snprintf(error, size,
                "content file exceeds admission bound: observed=%llu limit=%llu path=%.72s%s",
                static_cast<unsigned long long>(stamp.bytes), static_cast<unsigned long long>(kFileBytes),
                child.c_str(), child.size() > 72 ? "..." : "");
            return false;
        }
        tree.emplace(child, Entry{it->path(), isdir, stamp});
        if (tree.size() > kFiles * 2u) return Refuse(error, size, "content inventory exceeds admission bound");
        if (isdir && !Scan(it->path(), child, depth + 1, tree, error, size)) return false;
    }
    return !ec || Refuse(error, size, "content directory enumeration failed");
}

bool RootEntries(const fs::path &root, fs::path &data, fs::path &autoexec, char *error, size_t size) {
    std::error_code ec;
    if (!root.is_absolute() || !fs::is_directory(root, ec) || ec)
        return Refuse(error, size, "launcher content roots must be absolute existing directories");
    fs::directory_iterator it(root, ec), end;
    if (ec) return Refuse(error, size, "launcher content root cannot be enumerated");
    for (; it != end; it.increment(ec)) {
        if (ec) return Refuse(error, size, "launcher root enumeration failed");
        std::string name;
        if (!Canonical(it->path().filename().u8string(), name)) continue; // Unrelated root names are not content.
        fs::path *target = name == "data" ? &data : name == "autoexec.con" ? &autoexec : nullptr;
        if (!target) continue;
        if (!target->empty()) return Refuse(error, size, "launcher root has ambiguous content aliases");
        auto status = it->symlink_status(ec);
        if (ec || fs::is_symlink(status)) return Refuse(error, size, "content entry links are not admitted");
#if defined(_WIN32)
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Refuse(error, size, "content reparse entries are not admitted");
#endif
        if (name == "data" ? !fs::is_directory(status) : !fs::is_regular_file(status))
            return Refuse(error, size, "launcher content entry has an unsupported type");
        *target = it->path();
    }
    return !ec || Refuse(error, size, "launcher root enumeration failed");
}

bool Inventory(const fs::path &game, const fs::path &user, Tree &effective, Stamp &script,
               bool &has_script, char *error, size_t size) {
    fs::path gd, ga, ud, ua;
    if (!RootEntries(game, gd, ga, error, size) || !RootEntries(user, ud, ua, error, size)) return false;
    if (gd.empty()) return Refuse(error, size, "game data directory is missing");
    Tree authored, shadow;
    if (!Scan(gd, "data", 0, authored, error, size) ||
        (!ud.empty() && !Scan(ud, "data", 0, shadow, error, size))) return false;
    effective = std::move(authored);
    for (const auto &item : shadow) {
        auto old = effective.find(item.first);
        if (old != effective.end() && old->second.directory != item.second.directory)
            return Refuse(error, size, "user content changes a file into a directory or conversely");
        effective[item.first] = item.second;
    }
    auto path = !ua.empty() ? ua : ga;
    has_script = !path.empty();
    if (has_script && (!PathStamp(path, script) || script.bytes))
        return Refuse(error, size, "network content admission requires absent or empty autoexec.con");
    return true;
}

void Word(HASH_CTX &hash, uint32_t value) {
    uint8_t data[4] = {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
    HashUpdate(&hash, data, sizeof data);
}
void Size(HASH_CTX &hash, uint64_t value) {
    Word(hash, uint32_t(value >> 32)); Word(hash, uint32_t(value));
}
void Text(HASH_CTX &hash, const std::string &value) {
    Word(hash, uint32_t(value.size())); HashUpdate(&hash, value.data(), value.size());
}
bool Read(const std::string &logical, const Entry &entry, uint8_t digest[32], char *error, size_t size) {
    InternalRead internal;
    FILE *file = port_fopen(logical.c_str(), "rb");
    if (!file) return Refuse(error, size, "effective content cannot be opened by the game reader");
    Stamp before{}, after{}, path{};
    bool ok = FileStamp(file, before) && before == entry.stamp && PathStamp(entry.path, path) && path == before;
    HASH_CTX hash; HashInit(&hash);
    uint8_t bytes[65536]; uint64_t total = 0;
    while (ok) {
        size_t n = std::fread(bytes, 1, sizeof bytes, file);
        if (n) {
            total += n;
            if (total > before.bytes) { ok = false; break; }
            HashUpdate(&hash, bytes, n);
        }
        if (n < sizeof bytes) { if (std::ferror(file) || !std::feof(file)) ok = false; break; }
    }
    ok = ok && total == before.bytes && FileStamp(file, after) && after == before &&
         PathStamp(entry.path, path) && path == before;
    if (std::fclose(file) != 0) ok = false;
    if (!ok) return Refuse(error, size, "effective content identity changed or a content read failed");
    HashFinal(&hash, digest); return true;
}
bool SameInventory(const Tree &a, const Tree &b) {
    if (a.size() != b.size()) return false;
    auto other = b.begin();
    for (const auto &item : a) {
        if (item.first != other->first || item.second.path != other->second.path ||
            item.second.directory != other->second.directory || !(item.second.stamp == other->second.stamp)) return false;
        ++other;
    }
    return true;
}
bool Preset(const CE_CompatibilityPreset &preset) {
    if (preset.index >= 20) return false;
    const char *end = static_cast<const char *>(std::memchr(preset.name, 0, sizeof preset.name));
    if (!end || end == preset.name) return false;
    for (const char *p = preset.name; p != end; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p >= 127 || *p == '/' || *p == '\\' || *p == ':') return false;
    return true;
}
} // namespace

extern "C" int CE_CompatibilityNormalizeDataPath(const char *path, size_t path_size,
                                                char *normalized, size_t normalized_size) {
    if (normalized && normalized_size) normalized[0] = 0;
    if (!path || !path_size || path_size > kLogical || !normalized || !normalized_size ||
        std::memchr(path, 0, path_size)) return 0;
    // The stock sample-bank root is ".", producing .\data\sounds\... . Admit this one
    // initial spelling only; embedded dots, repeated prefixes and traversal still fail Canonical.
    if (path_size >= 2 && path[0] == '.' && (path[1] == '/' || path[1] == '\\')) {
        path += 2; path_size -= 2;
    }
    try {
        std::string result;
        size_t start = 0;
        unsigned components = 0;
        for (size_t end = 0; end <= path_size; ++end) {
            if (end != path_size && path[end] != '/' && path[end] != '\\') continue;
            std::string part;
            if (!Canonical(std::string(path + start, end - start), part) || PrivateName(part)) return 0;
            if (!components && part != "data") return 0;
            if (components++) result += '/';
            result += part;
            start = end + 1;
        }
        if (components < 2 || result.size() + 1 > normalized_size) return 0;
        std::memcpy(normalized, result.c_str(), result.size() + 1);
        return 1;
    } catch (const std::exception &) {
        return 0;
    }
}

extern "C" int CE_CompatibilityCompute(const CE_CompatibilityRequest *request, CE_CompatibilityResult *result,
                                       char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (result) std::memset(result, 0, sizeof *result);
    if (!request || !result || request->contract != CE_COMPAT_LEVEL852_WORLD9_INPUT5_CONTROL4 || request->level != 852 ||
        !Preset(request->players[0]) || !Preset(request->players[1]))
        return Refuse(error, error_size, "unsupported level, schema contract or authored preset selection");
    const char simulation[] = CE_NETWORK_SIMULATION_ID;
    if (sizeof simulation < 2 || sizeof simulation > 129)
        return Refuse(error, error_size, "this build has no admitted common simulation identity");
    for (const char *p = simulation; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
            return Refuse(error, error_size, "common simulation identity is not an admitted build token");
    const char *game = std::getenv("BEA_GAME_DIR"), *user = std::getenv("BEA_USER_DIR");
    if (!game || !*game || !user || !*user)
        return Refuse(error, error_size, "explicit launcher content roots are required");
#if defined(_WIN32)
    if (!WindowsLauncherRoot(game) || !WindowsLauncherRoot(user))
        return Refuse(error, error_size, "Windows launcher roots must be bounded normal UTF-8 drive paths with the UTF-8 code page");
#endif
    if (!ReaderRootsMatch(game, user))
        return Refuse(error, error_size, "game reader roots are uninitialized or changed; restart before networking");
    try {
        // The Windows A reader uses the process UTF-8 manifest; make the inventory use UTF-8 explicitly too.
        const fs::path game_root = fs::u8path(game), user_root = fs::u8path(user);
        std::error_code roots_error;
        bool same_root = fs::equivalent(game_root, user_root, roots_error);
        if (roots_error || same_root)
            return Refuse(error, error_size, "launcher game and user content roots must be distinct readable directories");
        Tree tree; Stamp script{}; bool has_script = false;
        if (!Inventory(game_root, user_root, tree, script, has_script, error, error_size)) return 0;
        const char *required[] = {"data/resources/base_res_pc.aya", "data/resources/852_res_pc.aya", "data/default physics.dat",
                                  "data/battle engine configurations.dat", "data/worldheaders.dat"};
        for (const char *path : required) {
            auto file = tree.find(path);
            if (file == tree.end() || file->second.directory)
                return Refuse(error, error_size, "required PC mission archive or common configuration input is missing");
        }
        if (has_script) {
            // Confirm the real reader sees the same empty script, not a differently resolved alias.
            if (!ReaderRootsMatch(game, user))
                return Refuse(error, error_size, "game reader roots changed; restart before networking");
            InternalRead internal;
            FILE *f = port_fopen("autoexec.con", "rb"); Stamp opened{};
            bool ok = f && FileStamp(f, opened) && opened == script && std::fgetc(f) == EOF && !std::ferror(f);
            if (f && std::fclose(f) != 0) ok = false;
            if (!ok) return Refuse(error, error_size, "effective startup script differs from the admitted empty script");
        }
        HASH_CTX hash; HashInit(&hash);
        const char domain[] = "Onslaught CE BLAKE2b-256 effective content preflight v1";
        HashUpdate(&hash, domain, sizeof domain);
        Word(hash, 1); Text(hash, simulation); Word(hash, 852);
        Word(hash, 1); Word(hash, 9); Word(hash, 5); Word(hash, 4); // Wire, world, input, control tuple.
        for (const auto &player : request->players) { Word(hash, player.index); Text(hash, player.name); }
        CE_CompatibilityResult found{};
        for (const auto &item : tree) {
            // Empty authored directories are also part of the effective namespace.
            Word(hash, item.second.directory ? 0 : 1); Text(hash, item.first);
            if (item.second.directory) continue;
            if (++found.files > kFiles || item.second.stamp.bytes > kTotalBytes - found.bytes)
                return Refuse(error, error_size, "effective content count or total bytes exceed admission bound");
            found.bytes += item.second.stamp.bytes;
            uint8_t file_digest[32];
            if (!ReaderRootsMatch(game, user))
                return Refuse(error, error_size, "game reader roots changed; restart before networking");
            if (!Read(item.first, item.second, file_digest, error, error_size)) return 0;
            Size(hash, item.second.stamp.bytes); HashUpdate(&hash, file_digest, sizeof file_digest);
        }
        Word(hash, found.files); Size(hash, found.bytes);
        Tree after; Stamp after_script{}; bool after_has_script = false;
        if (!Inventory(game_root, user_root, after, after_script, after_has_script, error, error_size)) return 0;
        if (!ReaderRootsMatch(game, user) || !SameInventory(tree, after) || after_has_script != has_script ||
            (has_script && !(after_script == script)))
            return Refuse(error, error_size, "content inventory changed during compatibility preflight");
        HashFinal(&hash, found.digest); *result = found; return 1;
    } catch (const std::exception &) {
        return Refuse(error, error_size, "content preflight could not establish a complete bounded inventory");
    }
}

namespace {
using Digest = std::array<uint8_t, 32>;
struct Snapshot {
    std::string game, user;
    Tree tree;
    std::map<std::string, Digest> digests;
    Stamp script{};
    bool has_script = false;
};
enum AdmissionPhase { FRESH, PREPARING, PREPARED, FAILED, RETIRED };
struct Admission {
    std::mutex mutex;
    AdmissionPhase phase = FRESH;
    bool prior_history = false;
    Snapshot snapshot;
    CE_CompatibilityResult result{};
    CE_CompatibilityContract contract = CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5;
    uint32_t level = 852;
    char failure[160]{};
};
Admission &ProcessAdmission() {
    // A static Bink worker can outlive ordinary C++ static destruction ordering. Deliberately
    // retain this one process-owned allocation until OS process teardown; never destroy its mutex
    // or maps beneath a late media callback. Retire changes policy, not storage lifetime.
    static Admission *state = new Admission;
    return *state;
}
void ClearError(char *error, size_t size) { if (error && size) error[0] = 0; }
int Sticky(Admission &state, char *error, size_t size, const char *reason) {
    if (state.phase != FAILED) std::snprintf(state.failure, sizeof state.failure, "%s", reason);
    state.phase = FAILED;
    return Refuse(error, size, state.failure);
}
bool CurrentRoots(const Snapshot &snapshot) {
    const char *game = std::getenv("BEA_GAME_DIR"), *user = std::getenv("BEA_USER_DIR");
    return game && user && snapshot.game == game && snapshot.user == user && ReaderRootsMatch(game, user);
}
bool LauncherRoots(std::string &game, std::string &user, char *error, size_t size) {
    const char *g = std::getenv("BEA_GAME_DIR"), *u = std::getenv("BEA_USER_DIR");
    if (!g || !*g || !u || !*u) return Refuse(error, size, "explicit launcher content roots are required");
#if defined(_WIN32)
    if (!WindowsLauncherRoot(g) || !WindowsLauncherRoot(u))
        return Refuse(error, size, "Windows launcher roots must be bounded normal UTF-8 drive paths with the UTF-8 code page");
#endif
    game = g; user = u;
    const fs::path gp = fs::u8path(game), up = fs::u8path(user);
    std::error_code ec;
    if (!gp.is_absolute() || !up.is_absolute() || !fs::is_directory(gp, ec) || ec || !fs::is_directory(up, ec) || ec)
        return Refuse(error, size, "launcher roots must be absolute existing readable directories");
    bool same = fs::equivalent(gp, up, ec);
    return (!ec && !same) || Refuse(error, size, "launcher game and user content roots must be distinct readable directories");
}
bool CaptureSnapshot(Snapshot &snapshot, CE_CompatibilityResult &result, char *error, size_t size,
                     CE_CompatibilityContract contract = CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5, uint32_t level = 852) {
    const char simulation[] = CE_NETWORK_SIMULATION_ID;
    if (sizeof simulation < 2 || sizeof simulation > 129)
        return Refuse(error, size, "this build has no admitted common simulation identity");
    for (const char *p = simulation; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.'))
            return Refuse(error, size, "common simulation identity is not an admitted build token");
    if (!LauncherRoots(snapshot.game, snapshot.user, error, size)) return false;
    if (!CurrentRoots(snapshot)) return Refuse(error, size, "game reader roots are uninitialized or changed; restart before networking");
    const fs::path game_root = fs::u8path(snapshot.game), user_root = fs::u8path(snapshot.user);
    if (!Inventory(game_root, user_root, snapshot.tree, snapshot.script, snapshot.has_script, error, size)) return false;
    const std::string mission = "data/resources/" + std::to_string(level) + "_res_pc.aya";
    const char *required[] = {"data/resources/base_res_pc.aya", mission.c_str(), "data/default physics.dat",
                             "data/battle engine configurations.dat", "data/worldheaders.dat"};
    for (const char *name : required) {
        auto found = snapshot.tree.find(name);
        if (found == snapshot.tree.end() || found->second.directory)
            return Refuse(error, size, "required PC mission archive or common configuration input is missing");
    }
    if (snapshot.has_script) {
        InternalRead internal;
        FILE *file = port_fopen("autoexec.con", "rb"); Stamp opened{};
        bool ok = file && FileStamp(file, opened) && opened == snapshot.script &&
                  std::fgetc(file) == EOF && !std::ferror(file);
        if (file && std::fclose(file)) ok = false;
        if (!ok) return Refuse(error, size, "effective startup script differs from the admitted empty script");
    }
    HASH_CTX hash; HashInit(&hash);
    if (contract == CE_COMPAT_LOCKSTEP_PROTOCOL1) {
        // The lockstep co-op: its own domain, so that no lockstep admission can equal a level-852 one; the level and protocol version are in the context.
        const char domain[] = "Onslaught CE BLAKE2b-256 lockstep admission v1";
        HashUpdate(&hash, domain, sizeof domain);
        Word(hash, 1); Text(hash, simulation); Word(hash, level); Word(hash, 1);
    } else {
    const char domain[] = "Onslaught CE BLAKE2b-256 fresh process admission v2";
    HashUpdate(&hash, domain, sizeof domain);
    Word(hash, 2); Text(hash, simulation); Word(hash, 852);
    Word(hash, 1); Word(hash, 11); Word(hash, 5); Word(hash, 5);
    Word(hash, 1); Word(hash, 0); Word(hash, 0); Word(hash, 0);
    }
    // Admission policy1: genuine P1/P2 configuration indices0 and uninverted sides. The game
    // bridge must verify these actual loaded facts; no caller preset name enters this context.
    for (const auto &item : snapshot.tree) {
        Word(hash, item.second.directory ? 0 : 1); Text(hash, item.first);
        if (item.second.directory) continue;
        if (++result.files > kFiles || item.second.stamp.bytes > kTotalBytes - result.bytes)
            return Refuse(error, size, "effective content count or total bytes exceed admission bound");
        result.bytes += item.second.stamp.bytes;
        Digest digest{};
        if (!CurrentRoots(snapshot) || !Read(item.first, item.second, digest.data(), error, size))
            return Refuse(error, size, "content roots changed or effective content could not be verified");
        snapshot.digests.emplace(item.first, digest);
        Size(hash, item.second.stamp.bytes); HashUpdate(&hash, digest.data(), digest.size());
    }
    Word(hash, result.files); Size(hash, result.bytes);
    Tree after; Stamp script{}; bool has_script = false;
    if (!Inventory(game_root, user_root, after, script, has_script, error, size) || !CurrentRoots(snapshot) ||
        !SameInventory(snapshot.tree, after) || has_script != snapshot.has_script ||
        (has_script && !(script == snapshot.script)))
        return Refuse(error, size, "effective inputs changed during network admission");
    HashFinal(&hash, result.digest); return true;
}
bool SameSnapshot(const Snapshot &a, const Snapshot &b) {
    return a.game == b.game && a.user == b.user && a.has_script == b.has_script &&
           (!a.has_script || a.script == b.script) && SameInventory(a.tree, b.tree) && a.digests == b.digests;
}
bool Logical(const char *logical, std::string &normalized) {
    if (!logical) return false;
    size_t n = 0; while (n <= kLogical && logical[n]) ++n;
    char path[kLogical + 1];
    if (!CE_CompatibilityNormalizeDataPath(logical, n, path, sizeof path)) return false;
    normalized = path; return true;
}
bool NativeStamp(intptr_t native, Stamp &stamp) {
#if defined(_WIN32)
    return HandleStamp(reinterpret_cast<HANDLE>(native), stamp);
#else
    if (native < 0 || native > INT_MAX) return false;
    uint64_t values[6];
    return CE_NativePosixFdStamp(static_cast<int>(native), values) && NativeStamp(values, stamp);
#endif
}
bool NativeDigest(intptr_t native, const Stamp &expected, Digest &digest) {
    Stamp original{}, after{};
    if (!NativeStamp(native, original) || !(original == expected)) return false;
    HASH_CTX hash; HashInit(&hash); uint8_t bytes[65536]; uint64_t total = 0;
    bool ok = true;
#if defined(_WIN32)
    // ReOpenFile names the already-open object, never its possibly replaced logical pathname.
    // Unlike DuplicateHandle, this owned handle can be positioned/read independently. Clearing
    // NO_BUFFERING also avoids requiring the caller's alignment, sector size or OVERLAPPED state.
    HANDLE read = ReOpenFile(reinterpret_cast<HANDLE>(native), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    Stamp reopened{}; LARGE_INTEGER zero{};
    if (read == INVALID_HANDLE_VALUE) return false;
    ok = HandleStamp(read, reopened) && reopened == original && SetFilePointerEx(read, zero, nullptr, FILE_BEGIN);
    while (ok) {
        DWORD n = 0;
        if (!ReadFile(read, bytes, sizeof bytes, &n, nullptr)) { ok = false; break; }
        if (!n) break;
        total += n;
        if (total > expected.bytes) { ok = false; break; }
        HashUpdate(&hash, bytes, n);
    }
    ok = ok && HandleStamp(read, reopened) && reopened == original;
    if (!CloseHandle(read)) ok = false;
#else
    while (ok) {
        off_t offset = static_cast<off_t>(total);
        if (offset < 0 || uint64_t(offset) != total) { ok = false; break; }
        ssize_t n;
        do { n = pread(static_cast<int>(native), bytes, sizeof bytes, offset); } while (n < 0 && errno == EINTR);
        if (n < 0) { ok = false; break; }
        if (!n) break;
        total += static_cast<uint64_t>(n);
        if (total > expected.bytes) { ok = false; break; }
        HashUpdate(&hash, bytes, static_cast<size_t>(n));
    }
#endif
    if (!ok || total != expected.bytes || !NativeStamp(native, after) || !(after == original)) return false;
    HashFinal(&hash, digest.data()); return true;
}
int ValidateNativeLocked(Admission &state, const char *logical, intptr_t native, char *error, size_t size) {
    if (state.phase == FRESH || state.phase == RETIRED) { state.prior_history = true; return 1; }
    if (state.phase != PREPARED) return Sticky(state, error, size, "network admission is not available; restart required");
    std::string name;
    if (!Logical(logical, name)) return Sticky(state, error, size, "authored read is outside the admitted data namespace");
    auto entry = state.snapshot.tree.find(name); auto digest = state.snapshot.digests.find(name);
    if (entry == state.snapshot.tree.end() || entry->second.directory || digest == state.snapshot.digests.end())
        return Sticky(state, error, size, "opened content was absent from the admitted snapshot");
    Digest actual{}; Stamp path{};
    if (!CurrentRoots(state.snapshot) || !PathStamp(entry->second.path, path) || !(path == entry->second.stamp) ||
        !NativeDigest(native, entry->second.stamp, actual) || actual != digest->second ||
        !PathStamp(entry->second.path, path) || !(path == entry->second.stamp) || !CurrentRoots(state.snapshot))
        return Sticky(state, error, size, "opened content identity or bytes differ from the admitted snapshot");
    return 1;
}
} // namespace

extern "C" int CE_CompatibilityValidateLauncherRoots(char *error, size_t size) {
    ClearError(error, size);
    try {
        std::string game, user;
        return LauncherRoots(game, user, error, size);
    } catch (const std::exception &) {
        return Refuse(error, size, "launcher roots could not be validated without initialization");
    }
}
extern "C" int CE_CompatibilityPrepare(CE_CompatibilityContract contract, uint32_t level,
                                        CE_CompatibilityResult *result, char *error, size_t size) {
    ClearError(error, size); if (result) std::memset(result, 0, sizeof *result);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (state.phase != FRESH || state.prior_history)
        return Sticky(state, error, size, "networking requires a fresh process before authored reads or console use");
    state.phase = PREPARING;
    if (!result || !((contract == CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5 && level == 852) ||
                     (contract == CE_COMPAT_LOCKSTEP_PROTOCOL1 && CE_CompatibilityLockstepLevel(level))))
        return Sticky(state, error, size, "unsupported fresh-process network contract or level");
    try {
        Snapshot snapshot; CE_CompatibilityResult found{}; char detail[160]{};
        if (!CaptureSnapshot(snapshot, found, detail, sizeof detail, contract, level)) return Sticky(state, error, size, detail);
        state.snapshot = std::move(snapshot); state.result = found; state.contract = contract; state.level = level; state.phase = PREPARED;
        *result = found; return 1;
    } catch (const std::exception &) {
        return Sticky(state, error, size, "network admission could not retain a complete bounded snapshot");
    }
}
extern "C" int CE_CompatibilityPrepared(CE_CompatibilityResult *result, char *error, size_t size) {
    ClearError(error, size); if (result) std::memset(result, 0, sizeof *result);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (!result || state.phase != PREPARED)
        return Refuse(error, size, state.phase == FAILED ? state.failure : "no prepared network admission");
    if (!CurrentRoots(state.snapshot)) return Sticky(state, error, size, "content roots changed after admission; restart required");
    *result = state.result; return 1;
}
extern "C" int CE_CompatibilityAdmissionLive(char *error, size_t size) {
    ClearError(error, size);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    // In-tick observation only. The session still checks roots, roles and its retained digest at
    // startup/update/poll barriers; this neither scans inputs nor substitutes for those checks.
    return state.phase == PREPARED || Refuse(error, size,
        state.phase == FAILED ? state.failure : "no prepared network admission");
}
extern "C" int CE_CompatibilityCheckBoundary(char *error, size_t size) {
    ClearError(error, size);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (state.phase != PREPARED) return Sticky(state, error, size, "network boundary has no live prepared admission");
    try {
        Snapshot current; CE_CompatibilityResult result{}; char detail[160]{};
        if (!CurrentRoots(state.snapshot) || !CaptureSnapshot(current, result, detail, sizeof detail, state.contract, state.level) ||
            !SameSnapshot(state.snapshot, current) || std::memcmp(result.digest, state.result.digest, sizeof result.digest))
            return Sticky(state, error, size, "loaded inputs or roots changed after admission; restart required");
        return 1;
    } catch (const std::exception &) {
        return Sticky(state, error, size, "network boundary could not verify retained inputs");
    }
}
extern "C" int CE_CompatibilityObserveAuthoredRead(const char *logical, int for_write, char *error, size_t size) {
    ClearError(error, size); if (internal_reads) return 1;
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (state.phase == FRESH || state.phase == RETIRED) { state.prior_history = true; return 1; }
    if (state.phase != PREPARED) return Sticky(state, error, size, "network authored read has no live admission");
    try {
        std::string name;
        if (for_write || !Logical(logical, name))
            return Sticky(state, error, size, "authored writes or reads outside admitted data are refused");
        if (!CurrentRoots(state.snapshot)) return Sticky(state, error, size, "content roots changed after admission; restart required");
        auto entry = state.snapshot.tree.find(name);
        if (entry != state.snapshot.tree.end() && entry->second.directory)
            return Sticky(state, error, size, "authored read names an admitted directory instead of a file");
        return 1; // An absent optional probe may fail normally; a successful open must still validate.
    } catch (const std::exception &) {
        return Sticky(state, error, size, "authored path admission could not be established");
    }
}
extern "C" int CE_CompatibilityAuthoredOpenFailed(const char *logical, char *error, size_t size) {
    ClearError(error, size); if (internal_reads) return 1;
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (state.phase == FRESH || state.phase == RETIRED) { state.prior_history = true; return 1; }
    if (state.phase != PREPARED) return Sticky(state, error, size, "network authored read has no live admission");
    try {
        std::string name;
        if (!Logical(logical, name)) return Sticky(state, error, size, "authored read is outside the admitted data namespace");
        if (!CurrentRoots(state.snapshot)) return Sticky(state, error, size, "content roots changed after admission; restart required");
        // The retained inventory, not the filesystem after a failure, distinguishes a harmless
        // never-present probe from an admitted input that could no longer be opened. Call only
        // from effective-content readers: a raw CRT path can legitimately miss an overlay file.
        if (state.snapshot.tree.find(name) != state.snapshot.tree.end())
            return Sticky(state, error, size, "admitted content could not be opened");
        return 1;
    } catch (const std::exception &) {
        return Sticky(state, error, size, "failed authored open could not be checked against admission");
    }
}
extern "C" int CE_CompatibilityValidateOpenedNative(const char *logical, intptr_t native, char *error, size_t size) {
    ClearError(error, size); if (internal_reads) return 1;
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    try { return ValidateNativeLocked(state, logical, native, error, size); }
    catch (const std::exception &) { return Sticky(state, error, size, "opened content proof could not be established"); }
}
extern "C" int CE_CompatibilityUnsupportedReader(char *error, size_t size) {
    ClearError(error, size);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    state.prior_history = true;
    if (state.phase == FRESH || state.phase == RETIRED) return 1;
    return Sticky(state, error, size, "unsupported authored reader cannot establish network compatibility");
}
extern "C" int CE_CompatibilityValidateOpenedFile(const char *logical, FILE *file, char *error, size_t size) {
    ClearError(error, size); if (internal_reads) return 1;
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (state.phase == FRESH || state.phase == RETIRED) { state.prior_history = true; return 1; }
    if (!file) return Sticky(state, error, size, "opened content has no readable file handle");
#if defined(_WIN32)
    intptr_t native = _get_osfhandle(_fileno(file));
#else
    intptr_t native = fileno(file);
#endif
    try { return ValidateNativeLocked(state, logical, native, error, size); }
    catch (const std::exception &) { return Sticky(state, error, size, "opened content proof could not be established"); }
}
extern "C" int CE_CompatibilityConsoleOperation(CE_CompatibilityConsoleKind kind, const char *text, size_t count,
                                                char *error, size_t size) {
    ClearError(error, size);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    if (kind != CE_COMPAT_CONSOLE_EXECUTE && kind != CE_COMPAT_CONSOLE_SET_VARIABLE && kind != CE_COMPAT_CONSOLE_SCRIPT)
        return Sticky(state, error, size, "unknown console operation cannot enter network admission");
    if (!text || count > 4096 || std::memchr(text, 0, count))
        return Sticky(state, error, size, "console operation is not a bounded command");
    if (state.phase == FAILED) return Refuse(error, size, state.failure);
    if (kind == CE_COMPAT_CONSOLE_EXECUTE && !count) return 1;
    state.prior_history = true;
    if (state.phase == FRESH || state.phase == RETIRED) return 1;
    return Sticky(state, error, size, "console mutation is unavailable during network admission; restart required");
}
extern "C" int CE_CompatibilityRuntimeMutation(char *error, size_t size) {
    ClearError(error, size);
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    state.prior_history = true;
    if (state.phase == FRESH || state.phase == RETIRED) return 1;
    return Sticky(state, error, size, "runtime mutation is unavailable during network admission; restart required");
}
extern "C" void CE_CompatibilityRetire(void) {
    Admission &state = ProcessAdmission(); std::lock_guard<std::mutex> hold(state.mutex);
    state.phase = RETIRED; state.prior_history = true;
}
#ifdef CE_NET_TEST_AUTOMATION
// Lockstep prototype admission: both peers must be this exact simulation build and the same level. The genuine
// content files are not yet part of it (the prototype's peers read one game copy); a release digest must add them.
extern "C" int CE_CompatibilityLockstepDigest(uint32_t level, uint8_t out[32]) {
    const char simulation[] = CE_NETWORK_SIMULATION_ID;
    if (!simulation[0]) return 0;
    HASH_CTX hash; HashInit(&hash);
    const char domain[] = "onslaught-ce lockstep prototype 1";
    HashUpdate(&hash, domain, sizeof domain); HashUpdate(&hash, simulation, sizeof simulation);
    uint8_t word[4] = {uint8_t(level >> 24), uint8_t(level >> 16), uint8_t(level >> 8), uint8_t(level)};
    HashUpdate(&hash, word, sizeof word); HashFinal(&hash, out); return 1;
}
#endif
