// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#include "ce_network_stl.h"
#include "ce_network_boot_authority.h"
#include "../network/ice_exchange.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {
constexpr size_t BearerMax = 4096, OriginMax = 512, DirectoryMax = 4096, ImageMax = 16384;
constexpr uint64_t SetupSeconds = 120, GrantSeconds = 14400, SetupMs = 120000;
constexpr uint64_t LastUtc = uint64_t(INT64_MAX) - GrantSeconds;
void Wipe(void* data, size_t size) noexcept {
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(data);
    while (size--) *bytes++ = 0;
}
void Wipe(std::string& bytes) {
    if (!bytes.empty()) Wipe(&bytes[0], bytes.size());
}
bool Same(const void* a, const void* b, size_t size) noexcept {
    const auto* x = static_cast<const unsigned char*>(a);
    const auto* y = static_cast<const unsigned char*>(b);
    unsigned char difference = 0;
    for (size_t i = 0; i < size; ++i) difference |= x[i] ^ y[i];
    return difference == 0;
}
bool Role(CE_NetRole role) noexcept { return role == CE_NET_HOST || role == CE_NET_GUEST; }
bool Token(const char* bytes, size_t size) noexcept {
    if (!bytes || !size || size > BearerMax) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
            c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}
bool Pair(const CeIceSignalPairBinding& pair) noexcept {
    if (!Role(pair.role) || !pair.expiresAt || pair.expiresAt > LastUtc || pair.approval[32]) return false;
    for (size_t i = 0; i < 32; ++i)
        if (!(pair.approval[i] >= '0' && pair.approval[i] <= '9') && !(pair.approval[i] >= 'a' && pair.approval[i] <= 'f')) return false;
    return true;
}
bool BoundedText(const char* text, size_t max, std::string& out) {
    if (!text) return false;
    size_t size = 0;
    while (size <= max && text[size]) ++size;
    if (!size || size > max) return false;
    out.assign(text, size);
    return true;
}
struct Identity { uint64_t device = 0, inode = 0, user = 0; };
bool DirectoryIdentity(const char* path, Identity* out) noexcept {
#ifdef __linux__
    if (!path || path[0] != '/' || !out) return false;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat info{};
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == geteuid() && !(info.st_mode & 0022);
    close(fd);
    if (ok) *out = {uint64_t(info.st_dev), uint64_t(info.st_ino), uint64_t(info.st_uid)};
    return ok;
#else
    (void)path; (void)out; return false;
#endif
}
bool DirectoryMatches(const std::string& path, const Identity& expected) noexcept {
    Identity now;
    return DirectoryIdentity(path.c_str(), &now) && now.device == expected.device &&
        now.inode == expected.inode && now.user == expected.user;
}
bool Times(const CeIceSignalGamePlan& plan, uint64_t pairExpiry, uint64_t utc, uint64_t mono) noexcept {
    return Role(plan.role) && utc <= LastUtc && plan.requestedAt <= utc && plan.requestedAt <= LastUtc &&
        utc < plan.requestExpiresAt && plan.requestExpiresAt <= plan.grantExpiresAt && plan.grantExpiresAt <= pairExpiry &&
        plan.requestExpiresAt - plan.requestedAt <= SetupSeconds && plan.grantExpiresAt - plan.requestedAt <= GrantSeconds &&
        plan.grantExpiresAt - plan.requestedAt >= SetupSeconds && plan.setupDeadlineMs > mono &&
        plan.setupDeadlineMs - mono <= SetupMs;
}
struct Bytes {
    std::vector<uint8_t> data;
    Bytes() { data.reserve(ImageMax); }
    void Bound(size_t size) const { if (size > ImageMax - data.size()) throw std::length_error("sealed authority exceeds bound"); }
    ~Bytes() { Wipe(data.data(), data.size()); }
    void Number(uint64_t value) { Bound(sizeof value); for (unsigned i = 0; i < 8; ++i) data.push_back(uint8_t(value >> (i * 8))); }
    void Copy(const void* source, size_t size) { Bound(size);
        const auto* bytes = static_cast<const uint8_t*>(source); data.insert(data.end(), bytes, bytes + size);
    }
};
struct Reader {
    const std::vector<uint8_t>& bytes; size_t at = 0;
    bool Copy(void* out, size_t size) noexcept {
        if (at > bytes.size() || size > bytes.size() - at) return false;
        if (size) memcpy(out, bytes.data() + at, size);
        at += size; return true;
    }
    bool Number(uint64_t* out) noexcept {
        uint8_t raw[8]; if (!Copy(raw, sizeof raw)) return false;
        *out = 0; for (unsigned i = 0; i < 8; ++i) *out |= uint64_t(raw[i]) << (i * 8); return true;
    }
    bool Text(std::string& out, size_t max) {
        uint64_t size; if (!Number(&size) || !size || size > max || size > bytes.size() - at) return false;
        out.assign(reinterpret_cast<const char*>(bytes.data() + at), size_t(size)); at += size_t(size);
        return out.find('\0') == std::string::npos;
    }
};
#ifdef __linux__
constexpr int RequiredSeals = F_SEAL_SEAL | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_WRITE;
bool Sealed(int fd, struct stat* info) noexcept {
    int seals = fcntl(fd, F_GET_SEALS);
    return fd >= 3 && fstat(fd, info) == 0 && S_ISREG(info->st_mode) && info->st_nlink == 0 &&
        info->st_uid == geteuid() && info->st_size > 0 && uint64_t(info->st_size) <= ImageMax &&
        seals >= 0 && (seals & RequiredSeals) == RequiredSeals;
}
#endif
} // namespace

struct CeBootSignalAuthority::State {
    mutable std::mutex mutex;
    std::string origin, bearer, directory;
    CeIceSignalPairBinding pair{};
    Identity identity;
    CeIceSignalGamePlan plan{};
    uint32_t level = 0;
    std::array<uint8_t, 32> secret{};
    CE_Handoff launch{};
    bool authenticated = false, bound = false, sealed = false, terminal = false, hasLaunch = false;
    ~State() { Wipe(bearer); Wipe(secret.data(), secret.size()); Wipe(&launch, sizeof launch); }
};

CeAuthenticatedLobbyStart::CeAuthenticatedLobbyStart(CE_NetRole role, uint32_t level, const uint8_t secret[32],
    uint64_t utc, uint64_t mono) noexcept : role_(role), level_(level), utc_(utc), mono_(mono) {
    memcpy(secret_, secret, sizeof secret_);
}
CeAuthenticatedLobbyStart::~CeAuthenticatedLobbyStart() { Wipe(secret_, sizeof secret_); }
CeSignalAuthorityFd::CeSignalAuthorityFd(CeSignalAuthorityFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
CeSignalAuthorityFd& CeSignalAuthorityFd::operator=(CeSignalAuthorityFd&& other) noexcept {
    if (this != &other) { Reset(); fd_ = other.fd_; other.fd_ = -1; } return *this;
}
CeSignalAuthorityFd::~CeSignalAuthorityFd() { Reset(); }
void CeSignalAuthorityFd::Reset() noexcept {
#ifdef __linux__
    if (fd_ >= 0) close(fd_);
#endif
    fd_ = -1;
}
int CeSignalAuthorityFd::PrepareExec() noexcept {
#ifdef __linux__
    struct stat info{};
    if (!Sealed(fd_, &info)) return -1;
    int flags = fcntl(fd_, F_GETFD);
    if (flags < 0 || fcntl(fd_, F_SETFD, flags & ~FD_CLOEXEC) < 0) return -1;
    return fd_;
#else
    return -1;
#endif
}
CeBootSignalAuthority::CeBootSignalAuthority(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
CeBootSignalAuthority::~CeBootSignalAuthority() = default;
std::shared_ptr<CeBootSignalAuthority> CeBootSignalAuthority::Create(const char* origin, const char* bearer, size_t size,
    const CeIceSignalPairBinding& pair, const char* directory) noexcept {
    try {
        if (!Pair(pair) || !Token(bearer, size)) return {};
        auto state = std::unique_ptr<State>(new State);
        if (!BoundedText(origin, OriginMax, state->origin) || state->origin.compare(0, 8, "https://") ||
            !BoundedText(directory, DirectoryMax, state->directory) || !DirectoryIdentity(directory, &state->identity)) return {};
        state->bearer.assign(bearer, size); state->pair = pair;
        return std::shared_ptr<CeBootSignalAuthority>(new CeBootSignalAuthority(std::move(state)));
    } catch (...) { return {}; }
}
bool CeBootSignalAuthority::ExpectedPair(CeIceSignalPairBinding* out) const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!out || state_->terminal) return false;
    *out = state_->pair; return true;
}
bool CeBootSignalAuthority::CopyBearer(char* out, size_t capacity, size_t* size) const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (size) *size = 0;
    if (!out || !size || state_->terminal || state_->bearer.size() > capacity) return false;
    memcpy(out, state_->bearer.data(), state_->bearer.size()); *size = state_->bearer.size(); return true;
}
bool CeBootSignalAuthority::Authorize(const CeAuthenticatedLobbyStart& start) noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->terminal || state_->authenticated || start.role_ != state_->pair.role || !start.level_ ||
        start.utc_ > LastUtc || state_->pair.expiresAt < start.utc_ + SetupSeconds ||
        start.mono_ > std::numeric_limits<uint64_t>::max() - SetupMs ||
        !DirectoryMatches(state_->directory, state_->identity)) return false;
    state_->plan.role = start.role_; state_->plan.requestedAt = start.utc_;
    state_->plan.requestExpiresAt = start.utc_ + SetupSeconds;
    state_->plan.grantExpiresAt = (std::min)(state_->pair.expiresAt, start.utc_ + GrantSeconds);
    state_->plan.setupDeadlineMs = start.mono_ + SetupMs;
    memcpy(state_->secret.data(), start.secret_, state_->secret.size()); state_->level = start.level_;
    state_->authenticated = true; return true;
}
bool CeBootSignalAuthority::MatchesStart(CE_NetRole role, uint32_t level, const uint8_t secret[32]) const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return secret && state_->authenticated && !state_->terminal && role == state_->pair.role && level == state_->level &&
        Same(secret, state_->secret.data(), state_->secret.size());
}
bool CeBootSignalAuthority::SetLaunch(const CE_Handoff& launch) noexcept {
    static_assert(std::is_trivially_copyable<CE_Handoff>::value, "launch metadata must be trivially copyable");
    std::lock_guard<std::mutex> lock(state_->mutex);
    CE_NetRole role = launch.role == 1 ? CE_NET_HOST : CE_NET_GUEST;
    if (!state_->authenticated || state_->sealed || state_->terminal || state_->hasLaunch ||
        launch.created != int64_t(state_->plan.requestedAt) || launch.kind != CE_HANDOFF_LAUNCH || (launch.role != 1 && launch.role != 2) || role != state_->pair.role ||
        launch.level != state_->level || !Same(launch.secret, state_->secret.data(), state_->secret.size()) ||
        !memchr(launch.endpoint, 0, sizeof launch.endpoint) || !memchr(launch.args, 0, sizeof launch.args)) return false;
    state_->launch = launch; state_->hasLaunch = true; return true;
}
bool CeBootSignalAuthority::CopyLaunch(CE_Handoff* out) const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!out || !state_->authenticated || !state_->hasLaunch || state_->terminal) return false;
    *out = state_->launch; return true;
}
bool CeBootSignalAuthority::BindBoot(const CE_NetConfig& config, uint32_t level, uint64_t utc, uint64_t mono,
    CeIceSignalGamePlan* out) noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!out || !state_->authenticated || state_->bound || state_->terminal || level != state_->level ||
        config.role != state_->pair.role || !Same(config.secret, state_->secret.data(), state_->secret.size()) ||
        !Times(state_->plan, state_->pair.expiresAt, utc, mono) || !DirectoryMatches(state_->directory, state_->identity) ||
        !CeIceExchange::ContextForConfig(&config, state_->plan.context)) return false;
    state_->bound = true; *out = state_->plan; return true;
}
bool CeBootSignalAuthority::AllowsGame(const CeIceSignalRequest& request) const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->authenticated && state_->bound && !state_->terminal && request.stage == CeIceSignalStageKind::Game &&
        request.role == state_->pair.role && request.directory && state_->directory == request.directory &&
        Same(request.context, state_->plan.context, sizeof state_->plan.context) &&
        request.requestedAt == state_->plan.requestedAt && request.requestExpiresAt == state_->plan.requestExpiresAt &&
        request.grantExpiresAt == state_->plan.grantExpiresAt && request.setupDeadlineMs == state_->plan.setupDeadlineMs &&
        DirectoryMatches(state_->directory, state_->identity);
}
const char* CeBootSignalAuthority::Origin() const noexcept { return state_->origin.c_str(); }
const char* CeBootSignalAuthority::Directory() const noexcept { return state_->directory.c_str(); }
bool CeBootSignalAuthority::Started() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex); return state_->authenticated;
}
bool CeBootSignalAuthority::CopyGameSecret(CE_NetRole role, uint32_t level, uint8_t out[32]) const noexcept {
    if (!out) return false;
    Wipe(out, 32);
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->bound || state_->terminal || role != state_->pair.role || level != state_->level ||
        !DirectoryMatches(state_->directory, state_->identity)) return false;
    memcpy(out, state_->secret.data(), state_->secret.size()); return true;
}
void CeBootSignalAuthority::Revoke() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->terminal = true; Wipe(state_->bearer); state_->bearer.clear();
    Wipe(state_->secret.data(), state_->secret.size());
    Wipe(&state_->launch, sizeof state_->launch); state_->hasLaunch = false;
}

CeSignalAuthorityFd CeBootSignalAuthority::Seal(uint64_t utc, uint64_t mono) const noexcept {
#if defined(__linux__) && defined(SYS_memfd_create)
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->authenticated || state_->bound || state_->sealed || state_->terminal || !Times(state_->plan, state_->pair.expiresAt, utc, mono) ||
            !DirectoryMatches(state_->directory, state_->identity)) return {};
        Bytes image;
        image.Copy("CESIGFD1", 8); image.Number(2); image.Copy(state_->pair.approval, 32);
        image.Number(state_->pair.role); image.Number(state_->pair.expiresAt); image.Number(state_->level);
        image.Number(state_->plan.requestedAt); image.Number(state_->plan.requestExpiresAt);
        image.Number(state_->plan.grantExpiresAt); image.Number(state_->plan.setupDeadlineMs);
        image.Number(state_->identity.device); image.Number(state_->identity.inode); image.Number(state_->identity.user);
        image.Copy(state_->secret.data(), state_->secret.size());
        image.Number(state_->hasLaunch ? sizeof state_->launch : 0);
        if (state_->hasLaunch) image.Copy(&state_->launch, sizeof state_->launch);
        for (const auto* text : {&state_->origin, &state_->directory, &state_->bearer}) {
            image.Number(text->size()); image.Copy(text->data(), text->size());
        }
        if (image.data.size() > ImageMax) return {};
        CeSignalAuthorityFd fd(int(syscall(SYS_memfd_create, "ce-signaling-start", MFD_CLOEXEC | MFD_ALLOW_SEALING)));
        if (!fd) return {};
        ssize_t written;
        do { written = write(fd.fd_, image.data.data(), image.data.size()); } while (written < 0 && errno == EINTR);
        if (written != ssize_t(image.data.size()) || fcntl(fd.fd_, F_ADD_SEALS, RequiredSeals) < 0 || lseek(fd.fd_, 0, SEEK_SET) != 0) return {};
        struct stat info{}; if (!Sealed(fd.fd_, &info)) return {};
        state_->sealed = true;
        return fd;
    } catch (...) { return {}; }
#else
    (void)utc; (void)mono; return {};
#endif
}
std::shared_ptr<CeBootSignalAuthority> CeBootSignalAuthority::Consume(int descriptor, uint64_t utc, uint64_t mono) noexcept {
    CeSignalAuthorityFd owned(descriptor);
#ifdef __linux__
    try {
        struct stat before{};
        if (!Sealed(descriptor, &before) || lseek(descriptor, 0, SEEK_CUR) != 0) return {};
        Bytes image; image.data.resize(size_t(before.st_size));
        ssize_t count;
        do { count = read(descriptor, image.data.data(), image.data.size()); } while (count < 0 && errno == EINTR);
        struct stat after{};
        if (count != ssize_t(image.data.size()) || !Sealed(descriptor, &after) || before.st_dev != after.st_dev ||
            before.st_ino != after.st_ino || before.st_size != after.st_size) return {};
        auto state = std::unique_ptr<State>(new State);
        Reader read{image.data}; char magic[8]; uint64_t version, role, level;
        if (!read.Copy(magic, sizeof magic) || memcmp(magic, "CESIGFD1", sizeof magic) || !read.Number(&version) || version != 2 ||
            !read.Copy(state->pair.approval, 32) || !read.Number(&role) || role > uint64_t(INT_MAX) ||
            !read.Number(&state->pair.expiresAt) || !read.Number(&level) || level > UINT32_MAX || !level ||
            !read.Number(&state->plan.requestedAt) || !read.Number(&state->plan.requestExpiresAt) ||
            !read.Number(&state->plan.grantExpiresAt) || !read.Number(&state->plan.setupDeadlineMs) ||
            !read.Number(&state->identity.device) || !read.Number(&state->identity.inode) || !read.Number(&state->identity.user) ||
            !read.Copy(state->secret.data(), state->secret.size())) return {};
        state->pair.role = static_cast<CE_NetRole>(role); state->plan.role = state->pair.role; state->level = uint32_t(level);
        uint64_t launchSize = 0;
        if (!read.Number(&launchSize) || (launchSize && launchSize != sizeof state->launch) ||
            (launchSize && !read.Copy(&state->launch, sizeof state->launch)) ||
            !read.Text(state->origin, OriginMax) || !read.Text(state->directory, DirectoryMax) ||
            !read.Text(state->bearer, BearerMax) || read.at != image.data.size()) return {};
        state->hasLaunch = launchSize != 0;
        if (state->hasLaunch && (state->launch.created != int64_t(state->plan.requestedAt) || state->launch.kind != CE_HANDOFF_LAUNCH ||
            state->launch.role != (state->pair.role == CE_NET_HOST ? 1 : 2) || state->launch.level != state->level ||
            !Same(state->launch.secret, state->secret.data(), state->secret.size()) ||
            !memchr(state->launch.endpoint, 0, sizeof state->launch.endpoint) || !memchr(state->launch.args, 0, sizeof state->launch.args))) return {};
        if (!Pair(state->pair) || !Token(state->bearer.data(), state->bearer.size()) || state->origin.compare(0, 8, "https://") ||
            !Times(state->plan, state->pair.expiresAt, utc, mono) || !DirectoryMatches(state->directory, state->identity)) return {};
        state->authenticated = true; state->sealed = true;
        return std::shared_ptr<CeBootSignalAuthority>(new CeBootSignalAuthority(std::move(state)));
    } catch (...) { return {}; }
#else
    (void)utc; (void)mono; return {};
#endif
}
#endif
#endif
