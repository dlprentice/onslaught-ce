// SPDX-License-Identifier: GPL-3.0-only
#include "online_profile.h"

#include "monocypher.h"
#include "optional/monocypher-ed25519.h"
extern "C" {
#include "../updater/sha256.h"
}

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include "windows_private_acl.h"
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr size_t kMaxIniBytes = 8192;
constexpr size_t kMaxLobbyUrlBytes = 512;
constexpr char kKeyFileName[] = "online-profile.key";
constexpr char kIniFileName[] = "online-profile.ini";
constexpr char kAuthDomain[] = "onslaught-ce-lobby-auth-v1";

void SetError(std::string* out, const char* message) {
    if (out) *out = message ? message : "";
}

bool AllZero(const uint8_t* data, size_t len) {
    uint8_t acc = 0;
    for (size_t i = 0; i < len; ++i) acc |= data[i];
    return acc == 0;
}

std::string HexLower(const uint8_t* data, size_t len) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0Fu]);
    }
    return out;
}

std::string HexUpper(const uint8_t* data, size_t len) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0Fu]);
    }
    return out;
}

bool UnhexLower(const std::string& hex, uint8_t* out, size_t out_len) {
    if (hex.size() != out_len * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < out_len; ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::array<uint8_t, 32> Sha256Bytes(const uint8_t* data, size_t len) {
    SHA256_CTX ctx{};
    std::array<uint8_t, 32> out{};
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out.data());
    crypto_wipe(&ctx, sizeof(ctx));
    return out;
}

bool GenerateRandomSeed(uint8_t out[CeOnlineProfile::kSeedBytes]) {
    if (!out) return false;
    for (int attempt = 0; attempt < 4; ++attempt) {
#ifdef _WIN32
        if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(CeOnlineProfile::kSeedBytes),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            crypto_wipe(out, CeOnlineProfile::kSeedBytes);
            return false;
        }
#else
        const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            crypto_wipe(out, CeOnlineProfile::kSeedBytes);
            return false;
        }
        size_t got = 0;
        bool ok = true;
        while (got < CeOnlineProfile::kSeedBytes) {
            const ssize_t n = read(fd, out + got, CeOnlineProfile::kSeedBytes - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                ok = false;
                break;
            }
            got += static_cast<size_t>(n);
        }
        close(fd);
        if (!ok) {
            crypto_wipe(out, CeOnlineProfile::kSeedBytes);
            return false;
        }
#endif
        if (!AllZero(out, CeOnlineProfile::kSeedBytes)) return true;
    }
    crypto_wipe(out, CeOnlineProfile::kSeedBytes);
    return false;
}

bool IsCallsignChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == ' ' || c == '_' || c == '-';
}

bool ValidLobbyUrlValue(const std::string& url) {
    if (url.empty()) return true;
    if (url.size() > kMaxLobbyUrlBytes) return false;
    for (unsigned char c : url) {
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

bool ParseUint32Strict(const std::string& text, uint32_t& out) {
    if (text.empty() || text.size() > 10) return false;
    uint64_t val = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        val = val * 10u + static_cast<uint64_t>(c - '0');
        if (val > std::numeric_limits<uint32_t>::max()) return false;
    }
    out = static_cast<uint32_t>(val);
    return true;
}

std::string TrimAscii(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
        --end;
    return s.substr(start, end - start);
}

#ifdef _WIN32

bool EnsureWindowsUserDir(const char* user_dir, std::wstring& out_wide, std::string* error_out) {
    if (!CeWinAcl::NormalizeAbsolutePath(user_dir, out_wide)) {
        SetError(error_out, "invalid user directory path");
        return false;
    }
    bool already_exists = false;
    if (!CeWinAcl::CreatePrivateDirectory(out_wide, &already_exists)) {
        if (!already_exists) {
            SetError(error_out, "failed to create private user directory");
            return false;
        }
    }
    if (!CeWinAcl::CheckPathComponentsNoReparse(out_wide)) {
        SetError(error_out, "user directory path contains reparse point or is not a directory");
        return false;
    }
    return true;
}

bool WritePrivateFileReplaceWin(const std::wstring& dir_wide, const std::string& leaf_ascii,
                                const void* data, size_t size) {
    std::wstring final_leaf(leaf_ascii.begin(), leaf_ascii.end());
    if (!CeWinAcl::ValidLeafName(final_leaf) || (!data && size > 0)) return false;
    static std::atomic<uint64_t> seq{0};
    const std::string temp_ascii = "." + leaf_ascii + "." +
        std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) + "." +
        std::to_string(seq.fetch_add(1)) + ".tmp";
    std::wstring temp_leaf(temp_ascii.begin(), temp_ascii.end());
    if (!CeWinAcl::ValidLeafName(temp_leaf)) return false;
    CeWinAcl::OwnerOnlySecurity sec;
    if (!sec.Init(false)) return false;
    const std::wstring temp_path = dir_wide + L"\\" + temp_leaf;
    const std::wstring final_path = dir_wide + L"\\" + final_leaf;
    CeWinAcl::ScopedHandle file(CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, &sec.sa, CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.Good()) return false;
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    bool ok = true;
    while (offset < size) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(size - offset);
        if (!WriteFile(file.value, bytes + offset, chunk, &wrote, nullptr) || wrote == 0) {
            ok = false;
            break;
        }
        offset += wrote;
    }
    if (ok) ok = FlushFileBuffers(file.value) != 0;
    file.Reset();
    if (ok) {
        ok = MoveFileExW(temp_path.c_str(), final_path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!ok) DeleteFileW(temp_path.c_str());
    return ok;
}

bool ReadTextFileWin(const std::wstring& dir_wide, const std::string& leaf_ascii,
                     std::string& out_text, bool& out_not_found) {
    out_text.clear();
    out_not_found = false;
    std::wstring leaf(leaf_ascii.begin(), leaf_ascii.end());
    if (!CeWinAcl::ValidLeafName(leaf)) return false;
    const std::wstring full = dir_wide + L"\\" + leaf;
    CeWinAcl::ScopedHandle file(CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.Good()) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) out_not_found = true;
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info))
        return false;
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0 ||
        info.nFileSizeHigh != 0 || info.nFileSizeLow > kMaxIniBytes)
        return false;
    out_text.resize(info.nFileSizeLow);
    size_t offset = 0;
    while (offset < out_text.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(out_text.size() - offset);
        if (!ReadFile(file.value, &out_text[offset], want, &got, nullptr) || got == 0) {
            out_text.clear();
            return false;
        }
        offset += got;
    }
    return true;
}

#else

struct ScopedFd {
    int fd = -1;
    ScopedFd() = default;
    explicit ScopedFd(int v) : fd(v) {}
    ~ScopedFd() { if (fd >= 0) close(fd); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    bool Good() const { return fd >= 0; }
};

bool OpenOrCreatePosixUserDir(const char* user_dir, ScopedFd& out_fd, std::string* error_out) {
    if (!user_dir || !*user_dir || std::strlen(user_dir) > 4096) {
        SetError(error_out, "invalid user directory path");
        return false;
    }
    if (mkdir(user_dir, 0700) != 0 && errno != EEXIST) {
        SetError(error_out, "failed to create user directory");
        return false;
    }
    out_fd.fd = open(user_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (out_fd.fd < 0) {
        SetError(error_out, "failed to open user directory");
        return false;
    }
    struct stat st{};
    if (fstat(out_fd.fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
        SetError(error_out, "user directory is not owned by current user");
        return false;
    }
    return true;
}

// Returns 1 if read cleanly, 0 if ENOENT, -1 on permission/format/IO error.
int ReadStrictKeyPosix(int dir_fd, uint8_t out_seed[CeOnlineProfile::kSeedBytes]) {
    ScopedFd f(openat(dir_fd, kKeyFileName, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!f.Good()) return errno == ENOENT ? 0 : -1;
    struct stat before{}, after{};
    if (fstat(f.fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_uid != geteuid() || (before.st_mode & 0077) != 0 ||
        before.st_nlink != 1 || before.st_size != static_cast<off_t>(CeOnlineProfile::kSeedBytes))
        return -1;
    size_t pos = 0;
    while (pos < CeOnlineProfile::kSeedBytes) {
        const ssize_t n = read(f.fd, out_seed + pos, CeOnlineProfile::kSeedBytes - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            crypto_wipe(out_seed, CeOnlineProfile::kSeedBytes);
            return -1;
        }
        pos += static_cast<size_t>(n);
    }
    uint8_t extra = 0;
    if (read(f.fd, &extra, 1) != 0 || fstat(f.fd, &after) != 0 ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_size != after.st_size) {
        crypto_wipe(out_seed, CeOnlineProfile::kSeedBytes);
        return -1;
    }
    return 1;
}

bool PublishPrivateFilePosix(int dir_fd, const char* leaf, const void* data, size_t size, bool replace) {
    static std::atomic<uint64_t> seq{0};
    const std::string temp = std::string(".") + leaf + "." +
        std::to_string(static_cast<unsigned long>(getpid())) + "." +
        std::to_string(seq.fetch_add(1)) + ".tmp";
    ScopedFd f(openat(dir_fd, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (!f.Good()) return false;
    bool ok = fchmod(f.fd, 0600) == 0;
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t pos = 0;
    while (ok && pos < size) {
        const ssize_t n = write(f.fd, bytes + pos, size - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        pos += static_cast<size_t>(n);
    }
    if (ok) ok = fsync(f.fd) == 0;
    const int fd_close = f.fd;
    f.fd = -1;
    if (close(fd_close) != 0) ok = false;
    if (ok) {
        if (replace) {
            ok = renameat(dir_fd, temp.c_str(), dir_fd, leaf) == 0;
        } else {
            ok = linkat(dir_fd, temp.c_str(), dir_fd, leaf, 0) == 0;
            unlinkat(dir_fd, temp.c_str(), 0);
        }
        if (ok) (void)fsync(dir_fd);
    }
    if (!ok) unlinkat(dir_fd, temp.c_str(), 0);
    return ok;
}

bool ReadTextFilePosix(int dir_fd, const char* leaf, std::string& out_text, bool& out_not_found) {
    out_text.clear();
    out_not_found = false;
    ScopedFd f(openat(dir_fd, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!f.Good()) {
        if (errno == ENOENT) out_not_found = true;
        return false;
    }
    struct stat st{};
    if (fstat(f.fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<size_t>(st.st_size) > kMaxIniBytes)
        return false;
    out_text.resize(static_cast<size_t>(st.st_size));
    size_t pos = 0;
    while (pos < out_text.size()) {
        const ssize_t n = read(f.fd, &out_text[pos], out_text.size() - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            out_text.clear();
            return false;
        }
        pos += static_cast<size_t>(n);
    }
    return true;
}

#endif

} // namespace

CeOnlineProfile::~CeOnlineProfile() {
    Clear();
}

CeOnlineProfile::CeOnlineProfile(CeOnlineProfile&& other) noexcept
    : loaded_(other.loaded_),
      user_dir_(std::move(other.user_dir_)),
      secret_key_(other.secret_key_),
      public_key_(other.public_key_),
      pubkey_hex_(std::move(other.pubkey_hex_)),
      player_id_(std::move(other.player_id_)),
      tag_(std::move(other.tag_)),
      settings_(std::move(other.settings_)) {
    other.Clear();
}

CeOnlineProfile& CeOnlineProfile::operator=(CeOnlineProfile&& other) noexcept {
    if (this != &other) {
        Clear();
        loaded_ = other.loaded_;
        user_dir_ = std::move(other.user_dir_);
        secret_key_ = other.secret_key_;
        public_key_ = other.public_key_;
        pubkey_hex_ = std::move(other.pubkey_hex_);
        player_id_ = std::move(other.player_id_);
        tag_ = std::move(other.tag_);
        settings_ = std::move(other.settings_);
        other.Clear();
    }
    return *this;
}

void CeOnlineProfile::Clear() noexcept {
    crypto_wipe(secret_key_.data(), secret_key_.size());
    public_key_.fill(0);
    loaded_ = false;
    user_dir_.clear();
    pubkey_hex_.clear();
    player_id_.clear();
    tag_.clear();
    settings_ = CeOnlineProfileSettings{};
}

std::string CeOnlineProfile::EncodeCrockfordBase32Lower(const uint8_t* data, size_t len) {
    static constexpr char kAlphabet[33] = "0123456789abcdefghjkmnpqrstvwxyz";
    if (!data || len == 0) return {};
    std::string out;
    out.reserve((len * 8 + 4) / 5);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        acc = (acc << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(kAlphabet[(acc >> bits) & 31u]);
        }
    }
    if (bits > 0) {
        out.push_back(kAlphabet[(acc << (5 - bits)) & 31u]);
    }
    return out;
}

std::string CeOnlineProfile::DerivePlayerIdFromPubkey(const uint8_t pubkey[kPublicKeyBytes]) {
    if (!pubkey) return {};
    const auto digest = Sha256Bytes(pubkey, kPublicKeyBytes);
    return EncodeCrockfordBase32Lower(digest.data(), 16);
}

std::string CeOnlineProfile::DeriveTagFromPubkey(const uint8_t pubkey[kPublicKeyBytes]) {
    if (!pubkey) return {};
    const auto digest = Sha256Bytes(pubkey, kPublicKeyBytes);
    return HexUpper(digest.data(), 2);
}

bool CeOnlineProfile::IsValidCallsign(const std::string& text) {
    if (text.size() < kMinCallsignLen || text.size() > kMaxCallsignLen) return false;
    if (text.front() == ' ' || text.back() == ' ') return false;
    bool prev_space = false;
    for (char c : text) {
        if (!IsCallsignChar(c)) return false;
        if (c == ' ') {
            if (prev_space) return false;
            prev_space = true;
        } else {
            prev_space = false;
        }
    }
    return true;
}

std::string CeOnlineProfile::SanitizeCallsign(const std::string& raw, const std::string& fallback) {
    std::string clean;
    clean.reserve(std::min<size_t>(raw.size(), kMaxCallsignLen));
    bool prev_space = true; // strips leading spaces
    for (char c : raw) {
        if (!IsCallsignChar(c)) continue;
        if (c == ' ') {
            if (prev_space) continue;
            prev_space = true;
        } else {
            prev_space = false;
        }
        if (clean.size() < kMaxCallsignLen) clean.push_back(c);
    }
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    if (clean.size() >= kMinCallsignLen) return clean;
    if (IsValidCallsign(fallback)) return fallback;
    return {};
}

bool CeOnlineProfile::IsValidNonceHex(const std::string& nonce_hex) {
    if (nonce_hex.size() != 32) return false;
    for (char c : nonce_hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string CeOnlineProfile::BuildAuthMessage(const std::string& nonce_hex, uint64_t issued_at,
                                              const std::string& pubkey_hex, const std::string& callsign) {
    return std::string(kAuthDomain) + "\n" + nonce_hex + "\n" + std::to_string(issued_at) + "\n" +
           pubkey_hex + "\n" + callsign;
}

void CeOnlineProfile::DeriveIdentityFromKeypair() {
    pubkey_hex_ = HexLower(public_key_.data(), public_key_.size());
    player_id_ = DerivePlayerIdFromPubkey(public_key_.data());
    tag_ = DeriveTagFromPubkey(public_key_.data());
}

bool CeOnlineProfile::InitFromSeed(const uint8_t seed[kSeedBytes], const std::string& callsign) {
    Clear();
    if (!seed || AllZero(seed, kSeedBytes)) return false;
    uint8_t seed_copy[kSeedBytes];
    std::memcpy(seed_copy, seed, kSeedBytes);
    crypto_ed25519_key_pair(secret_key_.data(), public_key_.data(), seed_copy);
    crypto_wipe(seed_copy, sizeof(seed_copy));
    DeriveIdentityFromKeypair();
    const std::string default_cs = "Pilot" + tag_;
    settings_.callsign = SanitizeCallsign(callsign, default_cs);
    if (!IsValidCallsign(settings_.callsign)) settings_.callsign = default_cs;
    settings_.matches_played = 0;
    settings_.matches_won = 0;
    settings_.preferred_side = "forseti";
    settings_.lobby_url.clear();
    loaded_ = true;
    return true;
}

bool CeOnlineProfile::LoadSettingsFile(std::string* error_out) {
    std::string text;
    bool not_found = false;
#ifdef _WIN32
    std::wstring wide_dir;
    if (!CeWinAcl::NormalizeAbsolutePath(user_dir_.c_str(), wide_dir) ||
        !CeWinAcl::CheckPathComponentsNoReparse(wide_dir)) {
        SetError(error_out, "invalid user directory");
        return false;
    }
    if (!ReadTextFileWin(wide_dir, kIniFileName, text, not_found)) {
        if (not_found) return SaveSettings(error_out);
        SetError(error_out, "failed to read online-profile.ini");
        return false;
    }
#else
    ScopedFd dir_fd;
    if (!OpenOrCreatePosixUserDir(user_dir_.c_str(), dir_fd, error_out)) return false;
    if (!ReadTextFilePosix(dir_fd.fd, kIniFileName, text, not_found)) {
        if (not_found) return SaveSettings(error_out);
        SetError(error_out, "failed to read online-profile.ini");
        return false;
    }
#endif
    const std::string default_cs = "Pilot" + tag_;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string key = TrimAscii(trimmed.substr(0, eq));
        const std::string val = TrimAscii(trimmed.substr(eq + 1));
        if (key == "callsign") {
            const std::string clean = SanitizeCallsign(val, default_cs);
            if (IsValidCallsign(clean)) settings_.callsign = clean;
        } else if (key == "matches_played") {
            uint32_t v = 0;
            if (ParseUint32Strict(val, v)) settings_.matches_played = v;
        } else if (key == "matches_won") {
            uint32_t v = 0;
            if (ParseUint32Strict(val, v)) settings_.matches_won = v;
        } else if (key == "preferred_side") {
            if (val == "forseti" || val == "muspell") settings_.preferred_side = val;
        } else if (key == "lobby_url" || key == "lobby_url_override") {
            if (ValidLobbyUrlValue(val)) settings_.lobby_url = val;
        }
    }
    if (settings_.matches_won > settings_.matches_played)
        settings_.matches_won = settings_.matches_played;
    return true;
}

bool CeOnlineProfile::LoadOrCreate(const char* user_dir, std::string* error_out) {
    Clear();
    if (!user_dir || !*user_dir) {
        SetError(error_out, "missing user directory");
        return false;
    }
    uint8_t seed[kSeedBytes]{};
    struct SeedWiper {
        uint8_t* ptr;
        ~SeedWiper() { crypto_wipe(ptr, kSeedBytes); }
    } wiper{seed};

#ifdef _WIN32
    std::wstring wide_dir;
    if (!EnsureWindowsUserDir(user_dir, wide_dir, error_out)) return false;
    std::vector<uint8_t> key_bytes;
    struct VecWiper {
        std::vector<uint8_t>& v;
        ~VecWiper() { if (!v.empty()) crypto_wipe(v.data(), v.size()); }
    } vec_wiper{key_bytes};
    bool not_found = false;
    if (CeWinAcl::ReadPrivateFile(wide_dir, kKeyFileName, kSeedBytes, kSeedBytes, key_bytes, &not_found)) {
        std::memcpy(seed, key_bytes.data(), kSeedBytes);
        if (AllZero(seed, kSeedBytes)) {
            SetError(error_out, "online-profile.key is all zeros");
            return false;
        }
    } else if (not_found) {
        if (!GenerateRandomSeed(seed)) {
            SetError(error_out, "random seed generation failed");
            return false;
        }
        static std::atomic<uint64_t> key_seq{0};
        const std::string temp_name = ".online-profile.key." +
            std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) + "." +
            std::to_string(key_seq.fetch_add(1)) + ".tmp";
        if (!CeWinAcl::PublishPrivateFileNoReplace(wide_dir, temp_name, kKeyFileName, seed, kSeedBytes)) {
            SetError(error_out, "failed to publish private online-profile.key");
            return false;
        }
    } else {
        SetError(error_out, "online-profile.key is insecure, unreadable, or corrupt");
        return false;
    }
#else
    ScopedFd dir_fd;
    if (!OpenOrCreatePosixUserDir(user_dir, dir_fd, error_out)) return false;
    const int status = ReadStrictKeyPosix(dir_fd.fd, seed);
    if (status < 0) {
        SetError(error_out, "online-profile.key is insecure, unreadable, or corrupt");
        return false;
    }
    if (status == 0) {
        if (!GenerateRandomSeed(seed)) {
            SetError(error_out, "random seed generation failed");
            return false;
        }
        if (!PublishPrivateFilePosix(dir_fd.fd, kKeyFileName, seed, kSeedBytes, false)) {
            SetError(error_out, "failed to publish private online-profile.key");
            return false;
        }
    } else if (AllZero(seed, kSeedBytes)) {
        SetError(error_out, "online-profile.key is all zeros");
        return false;
    }
#endif

    uint8_t seed_copy[kSeedBytes];
    std::memcpy(seed_copy, seed, kSeedBytes);
    crypto_ed25519_key_pair(secret_key_.data(), public_key_.data(), seed_copy);
    crypto_wipe(seed_copy, sizeof(seed_copy));

    user_dir_ = user_dir;
    DeriveIdentityFromKeypair();
    settings_.callsign = "Pilot" + tag_;
    settings_.matches_played = 0;
    settings_.matches_won = 0;
    settings_.preferred_side = "forseti";
    settings_.lobby_url.clear();
    loaded_ = true;

    if (!LoadSettingsFile(error_out)) {
        Clear();
        return false;
    }
    return true;
}

bool CeOnlineProfile::SaveSettings(std::string* error_out) const {
    if (!loaded_ || user_dir_.empty()) {
        SetError(error_out, "profile has no backing user directory");
        return false;
    }
    std::string data;
    data.reserve(256);
    data += "# Onslaught CE Online Profile\n";
    data += "callsign=" + settings_.callsign + "\n";
    data += "matches_played=" + std::to_string(settings_.matches_played) + "\n";
    data += "matches_won=" + std::to_string(settings_.matches_won) + "\n";
    data += "preferred_side=" + settings_.preferred_side + "\n";
    data += "lobby_url=" + settings_.lobby_url + "\n";

#ifdef _WIN32
    std::wstring wide_dir;
    if (!CeWinAcl::NormalizeAbsolutePath(user_dir_.c_str(), wide_dir) ||
        !CeWinAcl::CheckPathComponentsNoReparse(wide_dir)) {
        SetError(error_out, "invalid user directory");
        return false;
    }
    if (!WritePrivateFileReplaceWin(wide_dir, kIniFileName, data.data(), data.size())) {
        SetError(error_out, "failed to write online-profile.ini");
        return false;
    }
    return true;
#else
    ScopedFd dir_fd;
    if (!OpenOrCreatePosixUserDir(user_dir_.c_str(), dir_fd, error_out)) return false;
    if (!PublishPrivateFilePosix(dir_fd.fd, kIniFileName, data.data(), data.size(), true)) {
        SetError(error_out, "failed to write online-profile.ini");
        return false;
    }
    return true;
#endif
}

bool CeOnlineProfile::SetCallsign(const std::string& raw, bool save, std::string* error_out) {
    if (!loaded_) {
        SetError(error_out, "profile not loaded");
        return false;
    }
    const std::string clean = SanitizeCallsign(raw, "");
    if (!IsValidCallsign(clean)) {
        SetError(error_out, "callsign must contain 3 to 20 safe ASCII characters");
        return false;
    }
    settings_.callsign = clean;
    if (save && !user_dir_.empty()) return SaveSettings(error_out);
    return true;
}

bool CeOnlineProfile::RecordMatchResult(bool won, bool save, std::string* error_out) {
    if (!loaded_) {
        SetError(error_out, "profile not loaded");
        return false;
    }
    if (settings_.matches_played < std::numeric_limits<uint32_t>::max())
        ++settings_.matches_played;
    if (won && settings_.matches_won < settings_.matches_played)
        ++settings_.matches_won;
    if (save && !user_dir_.empty()) return SaveSettings(error_out);
    return true;
}

bool CeOnlineProfile::SetPreferredSide(const std::string& side, bool save, std::string* error_out) {
    if (!loaded_) {
        SetError(error_out, "profile not loaded");
        return false;
    }
    if (side != "forseti" && side != "muspell") {
        SetError(error_out, "preferred_side must be forseti or muspell");
        return false;
    }
    settings_.preferred_side = side;
    if (save && !user_dir_.empty()) return SaveSettings(error_out);
    return true;
}

bool CeOnlineProfile::SetLobbyUrl(const std::string& url, bool save, std::string* error_out) {
    if (!loaded_) {
        SetError(error_out, "profile not loaded");
        return false;
    }
    if (!ValidLobbyUrlValue(url)) {
        SetError(error_out, "invalid lobby_url");
        return false;
    }
    settings_.lobby_url = url;
    if (save && !user_dir_.empty()) return SaveSettings(error_out);
    return true;
}

bool CeOnlineProfile::SignAuthChallenge(const std::string& nonce_hex, uint64_t issued_at,
                                        const std::string& callsign,
                                        std::string& out_signature_hex) const {
    out_signature_hex.clear();
    if (!loaded_ || !IsValidNonceHex(nonce_hex) || issued_at == 0 || !IsValidCallsign(callsign))
        return false;
    const std::string msg = BuildAuthMessage(nonce_hex, issued_at, pubkey_hex_, callsign);
    uint8_t sig[kSignatureBytes]{};
    crypto_ed25519_sign(sig, secret_key_.data(),
                        reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    out_signature_hex = HexLower(sig, sizeof(sig));
    return true;
}

bool CeOnlineProfile::VerifyAuthChallenge(const std::string& pubkey_hex, const std::string& nonce_hex,
                                          uint64_t issued_at, const std::string& callsign,
                                          const std::string& signature_hex) {
    if (!IsValidNonceHex(nonce_hex) || issued_at == 0 || !IsValidCallsign(callsign)) return false;
    uint8_t pubkey[kPublicKeyBytes]{};
    uint8_t sig[kSignatureBytes]{};
    if (!UnhexLower(pubkey_hex, pubkey, sizeof(pubkey)) ||
        !UnhexLower(signature_hex, sig, sizeof(sig)))
        return false;
    const std::string msg = BuildAuthMessage(nonce_hex, issued_at, pubkey_hex, callsign);
    return crypto_ed25519_check(sig, pubkey,
                                reinterpret_cast<const uint8_t*>(msg.data()), msg.size()) == 0;
}
