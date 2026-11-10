// SPDX-License-Identifier: GPL-3.0-only
// Strict local grant reader. This does not issue or authenticate credentials.
#ifndef CE_ICE_CREDENTIALS_H
#define CE_ICE_CREDENTIALS_H

#include <juice/juice.h>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

class CeIceCredentials {
public:
    enum class Mode { Cloudflare, AuthoredLoopback };
    enum class LoadState { Pending, Ready, Error };
    enum { MaxCredentialBytes = 513, MaxGrantBytes = 32768,
           DefaultLifetimeSeconds = 7200, MaxLifetimeSeconds = 14400 };
    CeIceCredentials() = default;
    ~CeIceCredentials() { Clear(); }
    CeIceCredentials(const CeIceCredentials&) = delete;
    CeIceCredentials& operator=(const CeIceCredentials&) = delete;

    // Check the held directory identities through the same private-path owner.
    // This check never reads/writes grants or invalidates already-owned strings.
    bool RequireDistinctDirectories(const char* first, const char* second) {
#if defined(__linux__)
        if (!first || !second) { error_ = "invalid relay directory pair"; return false; }
        Fd a(OpenDirectory(first)), b(OpenDirectory(second));
        struct stat left = {}, right = {};
        if (a.value < 0 || b.value < 0 || fstat(a.value, &left) != 0 || fstat(b.value, &right) != 0 ||
            !S_ISDIR(left.st_mode) || !S_ISDIR(right.st_mode) || left.st_uid != geteuid() || right.st_uid != geteuid() ||
            (left.st_mode & 0077) != 0 || (right.st_mode & 0077) != 0) {
            error_ = "relay directories must be private"; return false;
        }
        if (left.st_dev == right.st_dev && left.st_ino == right.st_ino) {
            error_ = "relay lobby and game directories must be distinct"; return false;
        }
        error_ = ""; return true;
#else
        (void)first; (void)second;
        error_ = "private relay directory identity checks unavailable"; return false;
#endif
    }

    LoadState TryOpen(const char* directory, uint32_t role, const uint8_t* context,
                      uint64_t now, Mode mode = Mode::Cloudflare) {
        if (Open(directory, role, context, now, mode)) return LoadState::Ready;
        return pending_ ? LoadState::Pending : LoadState::Error;
    }

    bool PrepareRequest(const char* directory, uint32_t role, const uint8_t* context,
                        uint64_t now, uint32_t request_seconds = 180,
                        uint32_t grant_seconds = DefaultLifetimeSeconds) {
        if (loaded_) { error_ = "relay preparation already complete"; return false; }
        if (!directory || !context || (role != 1 && role != 2) || request_seconds < 1 || request_seconds > 180 ||
            grant_seconds < request_seconds || grant_seconds > MaxLifetimeSeconds ||
            now > std::numeric_limits<uint64_t>::max() - grant_seconds)
            return Fail("invalid relay preparation request");
        const char* digits = "0123456789abcdef";
        std::string binding;
        for (size_t i = 0; i < 32; ++i) {
            binding += digits[context[i] >> 4]; binding += digits[context[i] & 15];
        }
        std::string data = std::string("{\"schema\":1,\"role\":\"") + (role == 1 ? "host" : "guest") +
            "\",\"context\":\"" + binding + "\",\"requestedAt\":" + std::to_string(now) +
            ",\"requestExpiresAt\":" + std::to_string(now + request_seconds) +
            ",\"grantExpiresAt\":" + std::to_string(now + grant_seconds) + "}\n";
#if defined(__linux__)
        Fd parent(OpenDirectory(directory));
        if (parent.value < 0) return Fail("relay grant directory is not private");
        const char* name = role == 1 ? "relay-host.request.json" : "relay-guest.request.json";
        if (!PublishRequest(parent.value, name, data)) return Fail("relay preparation publication failed");
        error_ = ""; return true;
#else
        (void)data;
        return Fail("private relay request publication unavailable");
#endif
    }

    bool Open(const char* directory, uint32_t role, const uint8_t* context,
              uint64_t now, Mode mode = Mode::Cloudflare) {
        Clear();
#if defined(_WIN32)
        (void)directory; (void)role; (void)context; (void)now; (void)mode;
        return Fail("private relay file checks unavailable");
#else
        if (!directory || !context || (role != 1 && role != 2)) return Fail("invalid relay grant request");
        Fd parent(OpenDirectory(directory));
        if (parent.value < 0) return Fail("relay grant directory is not private");
        Fd file(openat(parent.value, role == 1 ? "relay-host.json" : "relay-guest.json",
                       O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (file.value < 0 && errno == ENOENT) {
            pending_ = true; error_ = "waiting for protected relay grant"; return false;
        }
        struct stat before = {}, after = {};
        if (file.value < 0 || fstat(file.value, &before) != 0 || !S_ISREG(before.st_mode) ||
            before.st_uid != geteuid() || (before.st_mode & 0077) != 0 || before.st_nlink != 1 ||
            before.st_size < 1 || before.st_size > MaxGrantBytes) return Fail("relay grant file is not private");
        SensitiveBytes bytes;
        bytes.value.resize(static_cast<size_t>(before.st_size));
        size_t offset = 0;
        while (offset < bytes.value.size()) {
            ssize_t count = read(file.value, bytes.value.data() + offset, bytes.value.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return Fail("relay grant read failed");
            offset += static_cast<size_t>(count);
        }
        char extra;
        if (read(file.value, &extra, 1) != 0 || fstat(file.value, &after) != 0 ||
            before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
            before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) return Fail("relay grant changed while reading");
        return Parse(bytes.value.data(), bytes.value.size(), role, context, now, mode);
#endif
    }

    bool Parse(const char* bytes, size_t size, uint32_t role, const uint8_t* context,
               uint64_t now, Mode mode = Mode::Cloudflare) {
        Clear();
        try {
            if (!bytes || !context || size == 0 || size > MaxGrantBytes || (role != 1 && role != 2))
                return Fail("invalid relay grant request");
            Grant grant;
            Parser parser(bytes, size);
            if (!parser.ReadGrant(grant)) return Fail("invalid relay grant");
            if (grant.schema != 1 || grant.role != (role == 1 ? "host" : "guest")) return Fail("relay grant role mismatch");
            if (!MatchesContext(grant.context, context)) return Fail("relay grant context mismatch");
            if (grant.expires <= grant.issued || grant.expires - grant.issued > MaxLifetimeSeconds)
                return Fail("invalid relay grant lifetime");
            if (grant.issued > now) return Fail(FutureGrantProblem());
            if (grant.expires <= now) return Fail(ExpiredGrantProblem());
            for (const Server& server : grant.servers) {
                bool has_relay = false, has_stun = false;
                for (const std::string& url : server.urls) {
                    std::string host;
                    uint16_t port = 0;
                    UrlKind kind = ClassifyUrl(url, mode, host, port);
                    if (kind == UrlKind::Invalid) return Fail("unsupported relay destination");
                    if (kind == UrlKind::Stun) has_stun = true; else has_relay = true;
                    if (kind != UrlKind::UdpRelay) continue;
                    if (!Secret(server.username) || !Secret(server.credential))
                        return Fail("invalid relay credential length or encoding");
                    for (const Relay& existing : relays_)
                        if (existing.host == host && existing.port == port) return Fail("duplicate relay destination");
                    if (relays_.size() >= 2) return Fail("too many relay destinations");
                    Relay relay;
                    relay.host = host; relay.port = port;
                    relay.username = server.username; relay.credential = server.credential;
                    relays_.push_back(std::move(relay));
                }
                if ((has_stun && has_relay) || (has_stun && (server.has_username || server.has_credential)) ||
                    (has_relay && (!server.has_username || !server.has_credential ||
                                   !Secret(server.username) || !Secret(server.credential))))
                    return Fail("invalid relay server entry");
            }
            if (relays_.empty()) return Fail("no supported UDP relay destination");
            issued_ = grant.issued; expires_ = grant.expires; opened_ = now; loaded_ = true; error_ = "";
            return true;
        } catch (...) { return Fail("relay grant rejected"); }
    }

    bool IsCurrent(uint64_t now) const { return loaded_ && now >= opened_ && now >= issued_ && now < expires_; }
    bool Configure(juice_config_t& config, uint64_t now) {
        config.turn_servers = nullptr; config.turn_servers_count = 0;
        config.stun_server_host = nullptr; config.stun_server_port = 0;
        if (!IsCurrent(now)) {
            if (loaded_ && now < issued_) error_ = FutureGrantProblem();
            else if (loaded_ && now >= expires_) error_ = ExpiredGrantProblem();
            else error_ = "relay grant expired or not yet valid";
            return false;
        }
        turns_.resize(relays_.size());
        for (size_t i = 0; i < relays_.size(); ++i) {
            turns_[i] = {};
            turns_[i].host = relays_[i].host.c_str(); turns_[i].port = relays_[i].port;
            turns_[i].username = relays_[i].username.c_str(); turns_[i].password = relays_[i].credential.c_str();
        }
        config.turn_servers = turns_.data(); config.turn_servers_count = static_cast<int>(turns_.size());
        return true;
    }
    uint64_t ExpiresAt() const { return expires_; }
    const char* Error() const { return error_; }

private:
    static const char* FutureGrantProblem() {
        return "relay grant is not yet valid; check this PC's and the issuer's date and time. For this pilot, relaunch both games with fresh private lobby and game folders.";
    }
    static const char* ExpiredGrantProblem() {
        return "relay grant expired; for this manual pilot, prepare fresh private lobby and game folders, then relaunch both games.";
    }
    static void Wipe(std::string& value) {
        if (!value.empty()) { volatile char* p = &value[0]; for (size_t i = 0; i < value.size(); ++i) p[i] = 0; }
        value.clear();
    }
    struct Server {
        std::vector<std::string> urls;
        std::string username, credential;
        bool has_username = false, has_credential = false;
        ~Server() { Wipe(username); Wipe(credential); }
    };
    struct Grant {
        uint64_t schema = 0, issued = 0, expires = 0;
        std::string role, context;
        std::vector<Server> servers;
    };
    struct Relay {
        std::string host, username, credential;
        uint16_t port = 0;
        ~Relay() { Wipe(username); Wipe(credential); }
    };
    enum class UrlKind { Invalid, Stun, UdpRelay, UnsupportedRelay };

    class Parser {
    public:
        Parser(const char* bytes, size_t size) : cursor_(bytes), end_(bytes + size) {}
        bool ReadGrant(Grant& grant) {
            if (!Take('{')) return false;
            unsigned fields = 0;
            do {
                std::string key;
                if (!String(key) || !Take(':')) return false;
                unsigned bit = 0;
                if (key == "schema") { bit = 1; if (!Number(grant.schema)) return false; }
                else if (key == "role") { bit = 2; if (!String(grant.role)) return false; }
                else if (key == "context") { bit = 4; if (!String(grant.context)) return false; }
                else if (key == "issuedAt") { bit = 8; if (!Number(grant.issued)) return false; }
                else if (key == "expiresAt") { bit = 16; if (!Number(grant.expires)) return false; }
                else if (key == "iceServers") { bit = 32; if (!Servers(grant.servers)) return false; }
                else return false;
                if (fields & bit) return false;
                fields |= bit;
            } while (Take(','));
            return Take('}') && fields == 63 && (Skip(), cursor_ == end_);
        }
    private:
        void Skip() { while (cursor_ < end_ && (*cursor_ == ' ' || *cursor_ == '\t' || *cursor_ == '\r' || *cursor_ == '\n')) ++cursor_; }
        bool Take(char c) { Skip(); if (cursor_ == end_ || *cursor_ != c) return false; ++cursor_; return true; }
        bool String(std::string& value) {
            if (!Take('"')) return false;
            value.clear();
            while (cursor_ < end_) {
                unsigned char c = static_cast<unsigned char>(*cursor_++);
                if (c == '"') return true;
                if (c == '\\') {
                    if (cursor_ == end_) return false;
                    c = static_cast<unsigned char>(*cursor_++);
                    if (c == 'u') {
                        unsigned decoded = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (cursor_ == end_) return false;
                            unsigned digit = Hex(*cursor_++);
                            if (digit > 15) return false;
                            decoded = decoded * 16 + digit;
                        }
                        if (decoded < 32 || decoded > 126) return false;
                        c = static_cast<unsigned char>(decoded);
                    } else if (c != '"' && c != '\\' && c != '/') return false;
                }
                if (c < 32 || c > 126 || value.size() >= MaxCredentialBytes) return false;
                value.push_back(static_cast<char>(c));
            }
            return false;
        }
        bool Number(uint64_t& value) {
            Skip();
            if (cursor_ == end_ || *cursor_ < '0' || *cursor_ > '9') return false;
            bool zero = *cursor_ == '0'; value = static_cast<unsigned>(*cursor_++ - '0');
            while (cursor_ < end_ && *cursor_ >= '0' && *cursor_ <= '9') {
                unsigned digit = static_cast<unsigned>(*cursor_++ - '0');
                if (zero || value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
                value = value * 10 + digit;
            }
            return true;
        }
        bool Urls(std::vector<std::string>& urls) {
            if (!Take('[')) return false;
            do {
                if (urls.size() >= 8) return false;
                std::string url;
                if (!String(url)) return false;
                urls.push_back(std::move(url));
            } while (Take(','));
            return Take(']');
        }
        bool Servers(std::vector<Server>& servers) {
            if (!Take('[')) return false;
            do {
                if (servers.size() >= 8 || !Take('{')) return false;
                Server server; unsigned fields = 0;
                do {
                    std::string key;
                    if (!String(key) || !Take(':')) return false;
                    unsigned bit = 0;
                    if (key == "urls") { bit = 1; if (!Urls(server.urls)) return false; }
                    else if (key == "username") { bit = 2; server.has_username = true; if (!String(server.username)) return false; }
                    else if (key == "credential") { bit = 4; server.has_credential = true; if (!String(server.credential)) return false; }
                    else return false;
                    if (fields & bit) return false;
                    fields |= bit;
                } while (Take(','));
                if (!Take('}') || !(fields & 1)) return false;
                servers.push_back(std::move(server));
            } while (Take(','));
            return Take(']');
        }
        const char* cursor_;
        const char* end_;
    };
    static unsigned Hex(char c) {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        return 16;
    }
    static bool MatchesContext(const std::string& value, const uint8_t* context) {
        if (value.size() != 64) return false;
        unsigned difference = 0;
        for (size_t i = 0; i < 32; ++i) {
            char a = value[2 * i], b = value[2 * i + 1];
            if ((a >= 'A' && a <= 'F') || (b >= 'A' && b <= 'F')) return false;
            unsigned high = Hex(a), low = Hex(b);
            if (high > 15 || low > 15) return false;
            difference |= (high * 16 + low) ^ context[i];
        }
        return difference == 0;
    }
    static bool Secret(const std::string& value) {
        if (value.empty() || value.size() > MaxCredentialBytes) return false;
        for (unsigned char c : value) if (c < 33 || c > 126) return false;
        return true;
    }
    static UrlKind ClassifyUrl(const std::string& url, Mode mode, std::string& host, uint16_t& port) {
        if (url == "stun:stun.cloudflare.com:3478" || url == "stun:stun.cloudflare.com:443") return UrlKind::Stun;
        if (url == "turn:turn.cloudflare.com:3478?transport=udp" || url == "turn:turn.cloudflare.com:443?transport=udp") {
            if (mode != Mode::Cloudflare) return UrlKind::Invalid;
            host = "turn.cloudflare.com"; port = url == "turn:turn.cloudflare.com:3478?transport=udp" ? 3478 : 443;
            return UrlKind::UdpRelay;
        }
        if (url == "turn:turn.cloudflare.com:3478?transport=tcp" || url == "turn:turn.cloudflare.com:80?transport=tcp" ||
            url == "turns:turn.cloudflare.com:5349?transport=tcp" || url == "turns:turn.cloudflare.com:443?transport=tcp")
            return mode == Mode::Cloudflare ? UrlKind::UnsupportedRelay : UrlKind::Invalid;
        const std::string prefix = "turn:127.0.0.1:", suffix = "?transport=udp";
        if (mode != Mode::AuthoredLoopback || url.compare(0, prefix.size(), prefix) != 0 ||
            url.size() <= prefix.size() + suffix.size() || url.compare(url.size() - suffix.size(), suffix.size(), suffix) != 0)
            return UrlKind::Invalid;
        unsigned value = 0; size_t end = url.size() - suffix.size();
        if (url[prefix.size()] == '0') return UrlKind::Invalid;
        for (size_t i = prefix.size(); i < end; ++i) {
            if (url[i] < '0' || url[i] > '9' || value > 6553) return UrlKind::Invalid;
            value = value * 10 + static_cast<unsigned>(url[i] - '0');
            if (value > 65535) return UrlKind::Invalid;
        }
        if (!value) return UrlKind::Invalid;
        host = "127.0.0.1"; port = static_cast<uint16_t>(value); return UrlKind::UdpRelay;
    }
#if !defined(_WIN32)
    struct Fd { int value; explicit Fd(int v) : value(v) {} ~Fd() { if (value >= 0) close(value); } };
    struct SensitiveBytes {
        std::vector<char> value;
        ~SensitiveBytes() { volatile char* p = value.data(); for (size_t i = 0; i < value.size(); ++i) p[i] = 0; }
    };
    static int OpenDirectory(const char* directory) {
        std::string path(directory);
        if (path.empty() || path.size() > 4096 || path[0] != '/') return -1;
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (current < 0) return -1;
        size_t offset = 1;
        while (offset < path.size()) {
            size_t end = path.find('/', offset);
            if (end == std::string::npos) end = path.size();
            std::string part = path.substr(offset, end - offset);
            if (part.empty() || part == "." || part == "..") { close(current); return -1; }
            int next = openat(current, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(current);
            if (next < 0) return -1;
            current = next; offset = end + 1;
        }
        struct stat info = {};
        if (fstat(current, &info) != 0 || info.st_uid != geteuid() || (info.st_mode & 0077) != 0) { close(current); return -1; }
        return current;
    }
#if defined(__linux__)
    static bool PublishRequest(int directory, const char* name, const std::string& data) {
        static std::atomic<uint64_t> sequence{0};
        std::string temporary = ".relay-request-" + std::to_string(static_cast<unsigned long>(getpid())) +
            "-" + std::to_string(sequence.fetch_add(1));
        Fd file(openat(directory, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        if (file.value < 0) return false;
        size_t offset = 0;
        while (offset < data.size()) {
            ssize_t count = write(file.value, data.data() + offset, data.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            offset += static_cast<size_t>(count);
        }
        return fsync(file.value) == 0 &&
            syscall(SYS_renameat2, directory, temporary.c_str(), directory, name, 1) == 0 && fsync(directory) == 0;
    }
#endif
#endif
    bool Fail(const char* error) { Clear(); error_ = error; return false; }
    void Clear() { relays_.clear(); turns_.clear(); issued_ = expires_ = opened_ = 0; loaded_ = false; pending_ = false; error_ = "relay grant unavailable"; }
    std::vector<Relay> relays_;
    std::vector<juice_turn_server_t> turns_;
    uint64_t issued_ = 0, expires_ = 0, opened_ = 0;
    bool loaded_ = false;
    bool pending_ = false;
    const char* error_ = "relay grant unavailable";
};
#endif
