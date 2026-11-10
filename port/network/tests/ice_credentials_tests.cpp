// SPDX-License-Identifier: GPL-3.0-only
// Invented credentials only. Never print grant bodies or credential values.
#include "ice_credentials.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static unsigned checks = 0;
static void Check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
static const uint8_t* Context() {
    static uint8_t value[32] = {};
    for (unsigned i = 0; i < 32; ++i) value[i] = static_cast<uint8_t>(i);
    return value;
}
static std::string ContextText() {
    const char* digits = "0123456789abcdef";
    std::string text;
    for (unsigned i = 0; i < 32; ++i) {
        text += digits[Context()[i] >> 4]; text += digits[Context()[i] & 15];
    }
    return text;
}
static std::string ProviderUrls() {
    return "\"turn:turn.cloudflare.com:3478?transport=udp\",\"turn:turn.cloudflare.com:443?transport=udp\","
           "\"turn:turn.cloudflare.com:3478?transport=tcp\",\"turn:turn.cloudflare.com:80?transport=tcp\","
           "\"turns:turn.cloudflare.com:5349?transport=tcp\",\"turns:turn.cloudflare.com:443?transport=tcp\"";
}
static std::string Grant(size_t user = 120, size_t password = 128,
                         const std::string& urls = ProviderUrls(), const std::string& role = "host") {
    return "{\"schema\":1,\"role\":\"" + role + "\",\"context\":\"" + ContextText() +
           "\",\"issuedAt\":100,\"expiresAt\":700,\"iceServers\":[{\"urls\":[\"stun:stun.cloudflare.com:3478\",\"stun:stun.cloudflare.com:443\"]},"
           "{\"urls\":[" + urls + "],\"username\":\"" + std::string(user, 'u') +
           "\",\"credential\":\"" + std::string(password, 'p') + "\"}]}";
}
static std::string Replace(std::string text, const std::string& from, const std::string& to) {
    size_t at = text.find(from);
    if (at == std::string::npos) throw std::runtime_error("fixture replacement anchor missing");
    text.replace(at, from.size(), to); return text;
}
static bool Parse(CeIceCredentials& value, const std::string& text, uint64_t now = 100,
                  uint32_t role = 1, CeIceCredentials::Mode mode = CeIceCredentials::Mode::Cloudflare) {
    return value.Parse(text.data(), text.size(), role, Context(), now, mode);
}
static void ValidityDiagnostics() {
    const char future[] = "relay grant is not yet valid; check this PC's and the issuer's date and time. For this pilot, relaunch both games with fresh private lobby and game folders.";
    const char expired[] = "relay grant expired; for this manual pilot, prepare fresh private lobby and game folders, then relaunch both games.";
    static_assert(sizeof future <= 160, "future grant advice exceeds the existing diagnostic buffer");
    static_assert(sizeof expired <= 160, "expired grant advice exceeds the existing diagnostic buffer");
    CeIceCredentials value;
    juice_config_t config = {};
    Check(!Parse(value, Grant(), 99), "future-issued grant accepted");
    Check(std::strcmp(value.Error(), future) == 0, "future grant lost its fixed clock synchronization advice");
    Check(value.ExpiresAt() == 0 && !value.IsCurrent(99), "future grant refusal retained usable state");
    Check(!Parse(value, Grant(), 69) && std::strcmp(value.Error(), future) == 0,
          "more distant future grant changed its refusal diagnostic");

    Check(Parse(value, Grant(), 100) && value.Configure(config, 100), "exact issuance instant rejected");
    Check(!value.Configure(config, 99) && !config.turn_servers && config.turn_servers_count == 0,
          "future configuration retained relay pointers");
    Check(std::strcmp(value.Error(), future) == 0, "future configuration lost clock synchronization advice");
    Check(value.IsCurrent(100) && value.Configure(config, 100), "diagnostic changed the loaded grant validity");

    Check(Parse(value, Grant(), 699) && value.Configure(config, 699), "last valid instant rejected");
    Check(!value.Configure(config, 700) && !config.turn_servers && config.turn_servers_count == 0,
          "exact-expiry configuration retained relay pointers");
    Check(std::strcmp(value.Error(), expired) == 0, "expired configuration lost its distinct diagnostic");
    Check(!Parse(value, Grant(), 700) && std::strcmp(value.Error(), expired) == 0,
          "exact expiry changed acceptance or its diagnostic");
    Check(!Parse(value, Grant(), 701) && std::strcmp(value.Error(), expired) == 0,
          "past expiry changed acceptance or its diagnostic");

    Check(Parse(value, Grant(), 150) && !value.IsCurrent(149) && !value.Configure(config, 149),
          "loaded grant clock rollback accepted");
    Check(std::strcmp(value.Error(), "relay grant expired or not yet valid") == 0,
          "clock rollback was mislabeled as grant expiry");
    Check(!Parse(value, Grant(), 99, 2) && std::strcmp(value.Error(), "relay grant role mismatch") == 0,
          "future timestamp changed role validation precedence");
    Check(!Parse(value, Replace(Grant(), ContextText(), std::string(64, 'a')), 99) &&
          std::strcmp(value.Error(), "relay grant context mismatch") == 0,
          "future timestamp changed context validation precedence");
    Check(!Parse(value, Replace(Grant(), "\"expiresAt\":700", "\"expiresAt\":100"), 99) &&
          std::strcmp(value.Error(), "invalid relay grant lifetime") == 0,
          "future timestamp changed lifetime validation precedence");
}
static void Parsing() {
    CeIceCredentials value;
    juice_config_t config = {};
    std::string text = Grant();
    Check(Parse(value, text), "documented provider shape rejected");
    Check(value.Configure(config, 100), "fresh configuration rejected");
    Check(config.turn_servers_count == 2 && config.stun_server_host == nullptr, "UDP selection failed");
    Check(config.turn_servers[0].port == 3478 && config.turn_servers[1].port == 443, "documented UDP ports lost");
    Check(std::strcmp(config.turn_servers[0].host, "turn.cloudflare.com") == 0, "fixed provider host lost");
    Check(std::strlen(config.turn_servers[0].username) == 120 && std::strlen(config.turn_servers[0].password) == 128,
          "long invented credential truncated");
    text.assign(text.size(), 'x');
    Check(std::strlen(config.turn_servers[0].username) == 120, "configuration borrowed input memory");
    Check(Parse(value, Grant(64, 64)) && value.Configure(config, 100) && config.turn_servers_count == 2 &&
          config.stun_server_host == nullptr, "eight-URL provider shape with ignored STUN443 rejected");
    Check(!Parse(value, Replace(Grant(), "stun:stun.cloudflare.com:443", "stun:stun.cloudflare.com:53")),
          "unrecognized STUN endpoint accepted");
    Check(Parse(value, Grant()) && value.Configure(config, 100), "provider fixture restore failed");
    Check(value.ExpiresAt() == 700 && value.IsCurrent(699) && !value.IsCurrent(700), "exclusive expiry failed");
    Check(!value.Configure(config, 700) && !config.turn_servers && config.turn_servers_count == 0,
          "expired configuration retained pointers");
    Check(Parse(value, Grant(513, 513)) && value.Configure(config, 100), "libjuice length boundary rejected");
    Check(std::strlen(config.turn_servers[0].username) == 513 && std::strlen(config.turn_servers[0].password) == 513,
          "maximum credential truncated");
    Check(!Parse(value, Grant(514, 128)) && !Parse(value, Grant(120, 514)), "overlength credential accepted");
    Check(!value.Configure(config, 100) && !config.turn_servers, "failed parse retained old configuration");
    Check(!Parse(value, Grant(0, 128)) && !Parse(value, Grant(120, 0)), "empty credential accepted");
    Check(Parse(value, Grant(), 699) && !value.IsCurrent(698), "clock rollback accepted");
    Check(!Parse(value, Grant(), 99) && !Parse(value, Grant(), 700), "grant validity boundary failed");
    Check(!Parse(value, Grant(), 100, 2), "wrong role accepted");
    Check(Parse(value, Grant(120, 128, ProviderUrls(), "guest"), 100, 2), "guest role rejected");
    Check(!Parse(value, Replace(Grant(), ContextText(), std::string(64, 'a'))), "wrong context accepted");
    Check(Parse(value, Replace(Grant(), "\"expiresAt\":700", "\"expiresAt\":7300")) &&
          value.IsCurrent(7299) && !value.IsCurrent(7300), "two-hour grant expiry failed");
    Check(Parse(value, Replace(Grant(), "\"expiresAt\":700", "\"expiresAt\":14500")) &&
          value.IsCurrent(14499) && !value.IsCurrent(14500), "four-hour grant boundary failed");
    Check(!Parse(value, Replace(Grant(), "\"expiresAt\":700", "\"expiresAt\":14501")), "excess lifetime accepted");
    Check(!Parse(value, Replace(Grant(), "\"expiresAt\":700", "\"expiresAt\":100")), "zero lifetime accepted");
    Check(!Parse(value, Replace(Grant(), "\"schema\":1", "\"schema\":2")), "unknown schema accepted");
    const std::string bad_urls[] = {
        "\"turn:other.invalid:3478?transport=udp\"", "\"turn:turn.cloudflare.com:9999?transport=udp\"",
        "\"turn:turn.cloudflare.com:3478\"", "\"turn:turn.cloudflare.com:3478?transport=udp&extra=1\"",
        "\"turn:127.0.0.1:30000?transport=udp\"", "\"https://turn.cloudflare.com/\"",
        "\"turn:turn.cloudflare.com:3478?transport=tcp\"",
        "\"turn:turn.cloudflare.com:3478?transport=udp\",\"turn:turn.cloudflare.com:3478?transport=udp\"",
    };
    for (const auto& urls : bad_urls) Check(!Parse(value, Grant(120, 128, urls)), "unsupported destination accepted");
    auto fixture = CeIceCredentials::Mode::AuthoredLoopback;
    Check(Parse(value, Grant(120, 128, "\"turn:127.0.0.1:30000?transport=udp\""), 100, 1, fixture), "fixture relay rejected");
    Check(value.Configure(config, 100) && config.turn_servers[0].port == 30000 &&
          std::strcmp(config.turn_servers[0].host, "127.0.0.1") == 0, "fixture destination changed");
    Check(!Parse(value, Grant(), 100, 1, fixture), "fixture accepted external relay");
    for (const char* port : {"0", "03000", "65536", "999999999999", "x"})
        Check(!Parse(value, Grant(120, 128, std::string("\"turn:127.0.0.1:") + port + "?transport=udp\""), 100, 1, fixture),
              "invalid fixture port accepted");
    const std::string invalid_json[] = {
        Grant() + "x", "x" + Grant(), Replace(Grant(), "\"schema\":1", "\"schema\":1,\"schema\":1"),
        Replace(Grant(), "\"schema\":1", "\"schema\":1,\"unknown\":1"),
        Replace(Grant(), "\"schema\":1", "\"schema\":true"),
        Replace(Grant(), "\"issuedAt\":100", "\"issuedAt\":0100"),
        Replace(Grant(), "\"issuedAt\":100", "\"issuedAt\":-1"),
        Replace(Grant(), "\"issuedAt\":100", "\"issuedAt\":18446744073709551616"),
        Replace(Grant(), "\"issuedAt\":100", "\"issuedAt\":1.0"),
        Replace(Grant(), "\"username\":\"", "\"username\":\"\\u0000"),
        Replace(Grant(), "\"credential\":\"", "\"credential\":\"\\n"),
        Replace(Grant(), "\"username\":\"", "\"username\":\" "),
        Replace(Grant(), "\"username\":\"", "\"username\":\"\\ud800"),
        Replace(Grant(), "\"urls\":[", "\"urls\":[],\"urls\":["),
    };
    for (const auto& invalid : invalid_json) Check(!Parse(value, invalid), "invalid JSON grant accepted");
    text = Grant(); text.append(CeIceCredentials::MaxGrantBytes - text.size(), ' ');
    Check(Parse(value, text), "bounded whitespace rejected"); text += ' ';
    Check(!Parse(value, text), "oversize grant accepted");
    text = Replace(Grant(), "\"schema\"", "\"sch\\u0065ma\"");
    Check(Parse(value, text), "valid ASCII JSON escape rejected");
    Check(!value.Parse(nullptr, 1, 1, Context(), 100), "null buffer accepted");
    Check(!value.Parse("{}", 2, 1, nullptr, 100), "null context accepted");
}
#if !defined(_WIN32)
static void Write(const std::string& path, const std::string& text) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    Check(fd >= 0, "fixture create failed");
    size_t offset = 0;
    while (offset < text.size()) {
        ssize_t n = write(fd, text.data() + offset, text.size() - offset);
        if (n <= 0) { close(fd); throw std::runtime_error("fixture write failed"); }
        offset += static_cast<size_t>(n);
    }
    close(fd);
}
static std::string Directory(const std::string& root, const char* name) {
    std::string path = root + "/" + name;
    Check(mkdir(path.c_str(), 0700) == 0, "fixture directory create failed"); return path;
}
static void DirectoryIdentity(const std::string& root) {
    const auto a = Directory(root, "identity-a"), b = Directory(root, "identity-b");
    CeIceCredentials value;
    Check(Parse(value, Grant()), "identity fixture grant rejected");
    juice_config_t config = {};
    Check(value.Configure(config, 100), "identity fixture configuration rejected");
    const auto turns = config.turn_servers;
    const char *username = turns[0].username, *password = turns[0].password;
    Check(value.RequireDistinctDirectories(a.c_str(), b.c_str()), "valid distinct private directories rejected");
    Check(value.RequireDistinctDirectories(a.c_str(), (b + "/").c_str()), "valid distinct directory spelling rejected");
    Check(!value.RequireDistinctDirectories(a.c_str(), a.c_str()), "same directory accepted twice");
    Check(!value.RequireDistinctDirectories(a.c_str(), (a + "/").c_str()), "trailing-slash physical alias accepted");
    Check(!value.RequireDistinctDirectories(a.c_str(), (a + "/./").c_str()), "dot directory alias accepted");
    Check(!value.RequireDistinctDirectories(nullptr, b.c_str()) && !value.RequireDistinctDirectories(a.c_str(), nullptr),
          "missing directory pair member accepted");
    const auto link = root + "/identity-link";
    Check(symlink(a.c_str(), link.c_str()) == 0, "identity symlink fixture failed");
    Check(!value.RequireDistinctDirectories(link.c_str(), b.c_str()) && !value.RequireDistinctDirectories(a.c_str(), link.c_str()),
          "directory identity check followed a symlink");
    Check(chmod(b.c_str(), 0755) == 0, "identity permission fixture failed");
    Check(!value.RequireDistinctDirectories(a.c_str(), b.c_str()) && !value.RequireDistinctDirectories(b.c_str(), a.c_str()),
          "directory identity check admitted public permissions");
    Check(chmod(b.c_str(), 0700) == 0, "identity permission fixture restore failed");
    Check(value.IsCurrent(100) && config.turn_servers == turns && turns[0].username == username && turns[0].password == password &&
          std::strlen(username) == 120 && std::strlen(password) == 128,
          "directory identity check invalidated owned grant strings or configuration");
    struct stat created = {};
    Check(lstat((a + "/relay-host.request.json").c_str(), &created) != 0 &&
          lstat((b + "/relay-host.request.json").c_str(), &created) != 0, "identity check published a request");
}
static void Files(const std::string& root) {
    CeIceCredentials value;
    std::string pending = Directory(root, "pending");
    using State = CeIceCredentials::LoadState;
    Check(value.TryOpen(pending.c_str(), 1, Context(), 100) == State::Pending, "missing private grant not pending");
    Check(value.PrepareRequest(pending.c_str(), 1, Context(), 100), "preparation request rejected");
    std::ifstream request_file(pending + "/relay-host.request.json");
    std::string request{std::istreambuf_iterator<char>(request_file), std::istreambuf_iterator<char>()};
    Check(request.find("\"requestExpiresAt\":280") != std::string::npos &&
          request.find("\"grantExpiresAt\":7300") != std::string::npos,
          "default request must ask for two hours without extending preparation");
    struct stat request_info = {};
    Check(stat((pending + "/relay-host.request.json").c_str(), &request_info) == 0 &&
          (request_info.st_mode & 0777) == 0600 && request_info.st_nlink == 1, "request publication privacy failed");
    Check(!value.PrepareRequest(pending.c_str(), 1, Context(), 101), "existing request overwritten");
    Check(value.TryOpen(pending.c_str(), 1, Context(), 100) == State::Pending, "request mistaken for grant");
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), 100, 0), "zero preparation window accepted");
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), 100, 181), "excess preparation window accepted");
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), 100, 180, 179), "grant shorter than request window accepted");
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), 100, 180, 14401), "excess requested grant lifetime accepted");
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), std::numeric_limits<uint64_t>::max()), "preparation overflow accepted");
    Check(value.PrepareRequest(pending.c_str(), 2, Context(), 100, 180, 14400), "four-hour requested lifetime rejected");
    std::ifstream longer_file(pending + "/relay-guest.request.json");
    std::string longer{std::istreambuf_iterator<char>(longer_file), std::istreambuf_iterator<char>()};
    Check(longer.find("\"requestExpiresAt\":280") != std::string::npos &&
          longer.find("\"grantExpiresAt\":14500") != std::string::npos,
          "explicit four-hour request changed the negotiation window");
    std::string invalid = Directory(root, "invalid-present");
    Write(invalid + "/relay-host.json", "{}");
    Check(value.TryOpen(invalid.c_str(), 1, Context(), 100) == State::Error, "invalid present grant not terminal");
    std::string stale = Directory(root, "stale-present");
    Write(stale + "/relay-host.json", Grant());
    Check(value.TryOpen(stale.c_str(), 1, Context(), 700) == State::Error, "expired present grant not terminal");
    chmod(pending.c_str(), 0755);
    Check(!value.PrepareRequest(pending.c_str(), 2, Context(), 100), "public request directory accepted");
    Check(value.TryOpen(pending.c_str(), 2, Context(), 100) == State::Error, "public directory treated as pending");
    chmod(pending.c_str(), 0700);
    std::string normal = Directory(root, "normal");
    Write(normal + "/relay-host.json", Grant());
    Write(normal + "/relay-guest.json", Grant(120, 128, ProviderUrls(), "guest"));
    Check(value.Open(normal.c_str(), 1, Context(), 100), "private host file rejected");
    Check(value.Open(normal.c_str(), 2, Context(), 100), "private guest file rejected");
    Check(chmod(normal.c_str(), 0755) == 0, "fixture chmod failed");
    Check(!value.Open(normal.c_str(), 1, Context(), 100), "public directory accepted");
    chmod(normal.c_str(), 0700); chmod((normal + "/relay-host.json").c_str(), 0644);
    Check(!value.Open(normal.c_str(), 1, Context(), 100), "public grant file accepted");
    chmod((normal + "/relay-host.json").c_str(), 0600);
    std::string file_link = Directory(root, "file-link");
    Check(symlink((normal + "/relay-host.json").c_str(), (file_link + "/relay-host.json").c_str()) == 0, "fixture symlink failed");
    Check(!value.Open(file_link.c_str(), 1, Context(), 100), "symlink grant accepted");
    std::string alias = root + "/directory-link";
    Check(symlink(normal.c_str(), alias.c_str()) == 0, "fixture directory symlink failed");
    Check(!value.Open(alias.c_str(), 1, Context(), 100), "symlink directory accepted");
    Check(!value.Open((alias + "/../normal").c_str(), 1, Context(), 100), "symlink ancestor accepted");
    std::string hard_link = Directory(root, "hard-link");
    Check(link((normal + "/relay-host.json").c_str(), (hard_link + "/relay-host.json").c_str()) == 0, "fixture hardlink failed");
    Check(!value.Open(hard_link.c_str(), 1, Context(), 100), "hardlink grant accepted");
    std::string empty = Directory(root, "empty"); Write(empty + "/relay-host.json", "");
    Check(!value.Open(empty.c_str(), 1, Context(), 100), "empty file accepted");
    std::string large = Directory(root, "large"); Write(large + "/relay-host.json", std::string(CeIceCredentials::MaxGrantBytes + 1, ' '));
    Check(!value.Open(large.c_str(), 1, Context(), 100), "oversize file accepted");
    std::string fifo = Directory(root, "fifo");
    Check(mkfifo((fifo + "/relay-host.json").c_str(), 0600) == 0, "fixture FIFO failed");
    Check(!value.Open(fifo.c_str(), 1, Context(), 100), "nonregular file accepted");
    Check(!value.Open("relative", 1, Context(), 100), "relative directory accepted");
}
#endif
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--prepare-operator-fixture") {
            CeIceCredentials value;
            Check(value.PrepareRequest(argv[2], 1, Context(), 100), "operator preparation failed");
            Check(value.TryOpen(argv[2], 1, Context(), 100) == CeIceCredentials::LoadState::Pending,
                  "operator preparation did not remain pending");
            std::printf("operator preparation checks passed: %u\n", checks);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--operator-fixture") {
            CeIceCredentials value;
            juice_config_t config = {};
            Check(value.Open(argv[2], 1, Context(), 100), "operator fixture grant rejected");
            Check(value.Configure(config, 100) && config.turn_servers_count == 2, "operator fixture configuration rejected");
            Check(std::strlen(config.turn_servers[0].username) == 120 && std::strlen(config.turn_servers[0].password) == 128,
                  "operator fixture credential length changed");
            std::printf("operator fixture checks passed: %u\n", checks);
            return 0;
        }
        ValidityDiagnostics();
        Parsing();
#if !defined(_WIN32)
        if (argc != 2) throw std::runtime_error("fixture root missing");
        DirectoryIdentity(argv[1]);
        Files(argv[1]);
#else
        (void)argc; (void)argv;
        CeIceCredentials value;
        Check(!value.RequireDistinctDirectories(nullptr, nullptr), "unverified Windows directory identity check enabled");
#endif
        std::printf("ice credential checks passed: %u\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ice credential check failed: %s\n", error.what());
        return 1;
    }
}
