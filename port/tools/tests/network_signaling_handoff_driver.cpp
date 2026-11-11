// SPDX-License-Identifier: GPL-3.0-only
#include "game/ce_network_boot_authority.h"
#include "network/ice_exchange.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// The shared derivation has separate bridge controls. This adapter verifies use of both
// original context inputs through that seam; it implements no game KDF.
bool CeIceExchange::ContextForConfig(const CE_NetConfig* config, uint8_t out[32]) noexcept {
    if (!config || !out || (config->role != CE_NET_HOST && config->role != CE_NET_GUEST)) return false;
    for (size_t i = 0; i < 32; ++i) out[i] = config->secret[i] ^ config->compatibility[i];
    return true;
}
struct CeLobbySignalingTransition {
    static bool Authenticate(CeBootSignalAuthority& owner, CE_NetRole role, uint32_t level,
        const uint8_t secret[32], uint64_t utc, uint64_t mono) {
        CeAuthenticatedLobbyStart start(role, level, secret, utc, mono);
        return owner.Authorize(start);
    }
};
class CeIceSignalAuthorityAccess {
public:
    static bool Read(const CeIceSignalAuthority& authority, char* out, size_t capacity, size_t* size) {
        return authority.CopyBearer(out, capacity, size);
    }
};
namespace {
constexpr uint64_t Utc = 1000, Mono = 1000000;
constexpr uint32_t Level = 7;
const char Bearer[] = "fixture-memory-authority";
int checks = 0, failures = 0;
void Check(bool good, const char* name) {
    ++checks; if (!good) { ++failures; fprintf(stderr, "FAIL %s\n", name); }
}
std::array<uint8_t, 32> Secret() {
    std::array<uint8_t, 32> out{}; for (size_t i = 0; i < out.size(); ++i) out[i] = uint8_t(i + 3); return out;
}
CeIceSignalPairBinding Pair() {
    CeIceSignalPairBinding pair{}; std::fill(pair.approval, pair.approval + 32, 'a');
    pair.role = CE_NET_HOST; pair.expiresAt = Utc + 3600; return pair;
}
std::shared_ptr<CeBootSignalAuthority> Owner(const std::string& directory) {
    return CeBootSignalAuthority::Create("https://fixture.invalid", Bearer, strlen(Bearer), Pair(), directory.c_str());
}
bool Authenticate(CeBootSignalAuthority& owner, CE_NetRole role = CE_NET_HOST) {
    auto secret = Secret(); return CeLobbySignalingTransition::Authenticate(owner, role, Level, secret.data(), Utc, Mono);
}
CE_NetConfig Config() {
    CE_NetConfig config{}; config.role = CE_NET_HOST; auto secret = Secret();
    memcpy(config.secret, secret.data(), secret.size());
    for (size_t i = 0; i < sizeof config.compatibility; ++i) config.compatibility[i] = uint8_t(i * 3);
    return config;
}
CeIceSignalRequest Request(const CeIceSignalGamePlan& plan, const std::string& directory) {
    CeIceSignalRequest request{}; request.stage = CeIceSignalStageKind::Game; request.role = plan.role;
    memcpy(request.context, plan.context, sizeof request.context); request.directory = directory.c_str();
    request.requestedAt = plan.requestedAt; request.requestExpiresAt = plan.requestExpiresAt;
    request.grantExpiresAt = plan.grantExpiresAt; request.setupDeadlineMs = plan.setupDeadlineMs; return request;
}
int Child(int descriptor, const std::string& directory) {
    auto owner = CeBootSignalAuthority::Consume(descriptor, Utc + 1, Mono + 100);
    if (!owner) return 2;
    CeIceSignalGamePlan plan{}; CE_NetConfig config = Config();
    if (!owner->BindBoot(config, Level, Utc + 1, Mono + 100, &plan) || !owner->AllowsGame(Request(plan, directory))) return 3;
    char token[64]{}; size_t size = 0;
    if (!CeIceSignalAuthorityAccess::Read(*owner, token, sizeof token, &size) || size != strlen(Bearer) || memcmp(token, Bearer, size)) return 4;
    return CeBootSignalAuthority::Consume(descriptor, Utc + 1, Mono + 100) ? 5 : 0;
}
int MemoryFile(const void* bytes, size_t size, bool seal) {
    int fd = int(syscall(SYS_memfd_create, "authored-handoff-control", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (fd < 0) return -1;
    if (write(fd, bytes, size) != ssize_t(size) || lseek(fd, 0, SEEK_SET) != 0 ||
        (seal && fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_WRITE) < 0)) {
        close(fd); return -1;
    }
    return fd;
}
} // namespace

void EraseFixture(void* bytes, size_t size) {
    volatile uint8_t* cursor = static_cast<volatile uint8_t*>(bytes);
    while (size--) *cursor++ = 0;
}

void LaunchSnapshot(const std::string& directory) {
    constexpr uint64_t utc = 1000, mono = 10000;
    constexpr uint32_t level = 41;
    auto pair = Pair(); pair.expiresAt = utc + 7200;
    auto owner = CeBootSignalAuthority::Create("https://signaling.example.invalid", "fixture-token", 13, pair, directory.c_str());
    auto config = Config(); config.role = CE_NET_HOST;
    Check(owner && CeLobbySignalingTransition::Authenticate(*owner, CE_NET_HOST, level, config.secret, utc, mono), "snapshot authenticated start");
    if (!owner) return;
    CE_Handoff launch{}; launch.kind = CE_HANDOFF_LAUNCH; launch.role = 1; launch.level = level; launch.created = int64_t(utc);
    std::memcpy(launch.secret, config.secret, sizeof launch.secret);
    std::strcpy(launch.args, "-fixture");
    CE_Handoff altered = launch; ++altered.created;
    Check(!owner->SetLaunch(altered), "snapshot cannot change authenticated start time");
    altered = launch; altered.secret[0] ^= 1;
    Check(!owner->SetLaunch(altered), "snapshot cannot replace session secret");
    altered = launch; altered.role = 2;
    Check(!owner->SetLaunch(altered), "snapshot cannot replace authenticated role");
    Check(owner->SetLaunch(launch) && !owner->SetLaunch(launch), "launch snapshot is owned once");
    auto descriptor = owner->Seal(utc, mono);
    auto restored = CeBootSignalAuthority::Consume(descriptor.Release(), utc + 1, mono + 1);
    Check(bool(restored), "launch snapshot survives protected handle consumption");
    if (!restored) return;
    CE_Handoff copy{};
    Check(restored->CopyLaunch(&copy) && std::memcmp(&copy, &launch, sizeof launch) == 0, "restored launch metadata and secret match");
    uint8_t secret[32]; std::memset(secret, 0xa5, sizeof secret);
    Check(!restored->CopyGameSecret(CE_NET_HOST, level, secret) && std::all_of(secret, secret + 32, [](uint8_t x) { return x == 0; }), "secret unavailable before boot binding");
    CeIceSignalGamePlan plan{};
    Check(restored->BindBoot(config, level, utc + 1, mono + 1, &plan), "snapshot binds actual boot configuration");
    Check(restored->CopyGameSecret(CE_NET_HOST, level, secret) && std::memcmp(secret, config.secret, 32) == 0, "typed secret available only for bound game");
    Check(!restored->CopyGameSecret(CE_NET_HOST, level + 1, secret) && std::all_of(secret, secret + 32, [](uint8_t x) { return x == 0; }), "wrong level cannot read secret");
    restored->Revoke();
    Check(!restored->CopyLaunch(&copy) && !restored->CopyGameSecret(CE_NET_HOST, level, secret), "terminal revoke removes launch and game secret");
    EraseFixture(&launch, sizeof launch); EraseFixture(&copy, sizeof copy); EraseFixture(&altered, sizeof altered);
    EraseFixture(&config, sizeof config); EraseFixture(secret, sizeof secret);
}

int main(int argc, char** argv) {
    if (argc == 4 && !strcmp(argv[1], "--child")) return Child(atoi(argv[2]), argv[3]);
    if (argc != 2) return 2;
    const std::string root = argv[1], directory = root + "/selected";
    if (mkdir(directory.c_str(), 0700) != 0) return 2;
    LaunchSnapshot(directory);
    auto owner = Owner(directory); Check(bool(owner), "memory owner accepts bounded authored inputs");
    if (!owner) return 1;
    Check(!owner->Seal(Utc, Mono), "selection alone cannot seal START authority");
    CeIceSignalGamePlan empty{}; Check(!owner->AllowsGame(Request(empty, directory)), "no game preparation before authenticated START");
    Check(!Authenticate(*owner, CE_NET_GUEST), "authenticated role must match expected pair");
    Check(Authenticate(*owner), "authenticated START creates one fixed plan");
    Check(!Authenticate(*owner), "a second START cannot replace or refresh the plan");
    auto secret = Secret();
    Check(owner->MatchesStart(CE_NET_HOST, Level, secret.data()) && !owner->MatchesStart(CE_NET_GUEST, Level, secret.data()) &&
        !owner->MatchesStart(CE_NET_HOST, Level + 1, secret.data()), "launch role and level bind the authenticated START");
    secret[0]++;
    Check(!owner->MatchesStart(CE_NET_HOST, Level, secret.data()), "another session secret cannot use START authority");
    CeSignalAuthorityFd sealed = owner->Seal(Utc, Mono); Check(bool(sealed), "authenticated plan seals to an owned anonymous descriptor");
    Check(!owner->Seal(Utc, Mono), "the owner cannot mint a second handoff descriptor");
    int descriptor = sealed.PrepareExec();
    Check(descriptor >= 3 && !(fcntl(descriptor, F_GETFD) & FD_CLOEXEC), "only the intended descriptor is enabled for exec inheritance");
    struct stat imageInfo{};
    if (fstat(descriptor, &imageInfo) != 0) return 2;
    std::vector<char> validImage(size_t(imageInfo.st_size));
    if (pread(descriptor, validImage.data(), validImage.size(), 0) != ssize_t(validImage.size())) return 2;
    int duplicate = dup(descriptor);
    auto restored = CeBootSignalAuthority::Consume(sealed.Release(), Utc + 1, Mono + 100);
    Check(bool(restored), "sealed authority restores after bounded elapsed time");
    Check(!CeBootSignalAuthority::Consume(duplicate, Utc + 1, Mono + 100), "duplicate descriptors cannot replay the consumed read");
    if (!restored) return 1;
    CeIceSignalPairBinding pair{};
    Check(restored->ExpectedPair(&pair) && pair.role == CE_NET_HOST && pair.expiresAt == Pair().expiresAt &&
        !memcmp(pair.approval, Pair().approval, sizeof pair.approval), "restart preserves expected approval role and expiry");
    char token[64]{}; size_t tokenSize = 0;
    Check(CeIceSignalAuthorityAccess::Read(*restored, token, sizeof token, &tokenSize) && tokenSize == strlen(Bearer) &&
        !memcmp(token, Bearer, tokenSize), "provider-only access reads the same bounded authority");
    CE_NetConfig config = Config(), wrong = config; wrong.secret[0]++;
    CeIceSignalGamePlan plan{};
    Check(!restored->BindBoot(wrong, Level, Utc + 1, Mono + 100, &plan), "boot cannot bind a different START secret");
    Check(restored->BindBoot(config, Level, Utc + 1, Mono + 100, &plan), "boot binds shared game context once");
    Check(plan.requestedAt == Utc && plan.requestExpiresAt == Utc + 120 && plan.grantExpiresAt == Pair().expiresAt &&
        plan.setupDeadlineMs == Mono + 120000, "restart never refreshes request grant or setup deadlines");
    Check(!restored->BindBoot(config, Level, Utc + 1, Mono + 100, &empty), "restored authority cannot rebind a second plan");
    auto request = Request(plan, directory); Check(restored->AllowsGame(request), "exact request admitted after boot binding");
    for (int change = 0; change < 7; ++change) {
        auto other = request;
        if (change == 0) other.stage = CeIceSignalStageKind::Lobby;
        if (change == 1) other.role = CE_NET_GUEST;
        if (change == 2) other.context[0]++;
        if (change == 3) other.requestedAt++;
        if (change == 4) other.requestExpiresAt++;
        if (change == 5) other.grantExpiresAt++;
        if (change == 6) other.setupDeadlineMs++;
        Check(!restored->AllowsGame(other), "every immutable request field rejects a mismatch");
    }
    const std::string otherDirectory = root + "/other"; mkdir(otherDirectory.c_str(), 0700);
    request.directory = otherDirectory.c_str(); Check(!restored->AllowsGame(request), "different directory refused");
    restored->Revoke(); request = Request(plan, directory);
    Check(!restored->AllowsGame(request) && !restored->ExpectedPair(&pair) &&
        !CeIceSignalAuthorityAccess::Read(*restored, token, sizeof token, &tokenSize) && tokenSize == 0,
        "terminal revocation refuses pair game and bearer access");
    auto expired = Owner(directory); Authenticate(*expired); auto late = expired->Seal(Utc, Mono);
    Check(!CeBootSignalAuthority::Consume(late.Release(), Utc + 120, Mono + 120000), "expired launch refused without extending setup");
    auto rollback = Owner(directory); Authenticate(*rollback); auto early = rollback->Seal(Utc, Mono);
    Check(!CeBootSignalAuthority::Consume(early.Release(), Utc - 1, Mono + 1), "rolled-back UTC refused");
    const char malformed[] = "authored malformed authority";
    Check(!CeBootSignalAuthority::Consume(MemoryFile(validImage.data(), validImage.size(), false), Utc, Mono), "otherwise-valid unsealed memory object refused");
    Check(!CeBootSignalAuthority::Consume(MemoryFile(malformed, sizeof malformed, true), Utc, Mono), "sealed malformed object refused");
    int ordinary = open(root.c_str(), O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
    if (ordinary >= 0) write(ordinary, validImage.data(), validImage.size());
    Check(ordinary >= 0 && !CeBootSignalAuthority::Consume(ordinary, Utc, Mono), "ordinary unlinked file cannot impersonate memory handle");
    std::vector<char> oversized(16385, 'x');
    Check(!CeBootSignalAuthority::Consume(MemoryFile(oversized.data(), oversized.size(), true), Utc, Mono), "oversized object refused before parsing");
    Check(!CeBootSignalAuthority::Create("https://fixture.invalid", "bad token", 9, Pair(), directory.c_str()), "opaque token rejects whitespace");
    std::vector<char> bigToken(4097, 'x');
    Check(!CeBootSignalAuthority::Create("https://fixture.invalid", bigToken.data(), bigToken.size(), Pair(), directory.c_str()), "opaque token allocation bounded");
    auto badPair = Pair(); badPair.approval[0] = 'g';
    Check(!CeBootSignalAuthority::Create("https://fixture.invalid", Bearer, strlen(Bearer), badPair, directory.c_str()), "malformed expected approval refused");
    auto shortPair = Pair(); shortPair.expiresAt = Utc + 119;
    auto shortOwner = CeBootSignalAuthority::Create("https://fixture.invalid", Bearer, strlen(Bearer), shortPair, directory.c_str());
    Check(shortOwner && !Authenticate(*shortOwner), "short pair lifetime cannot authorize refreshed window");
    auto forExec = Owner(directory); Authenticate(*forExec); auto inherited = forExec->Seal(Utc, Mono);
    int inheritedNumber = inherited.PrepareExec(); pid_t child = fork();
    if (child == 0) {
        std::string number = std::to_string(inheritedNumber);
        execl("/proc/self/exe", argv[0], "--child", number.c_str(), directory.c_str(), static_cast<char*>(nullptr)); _exit(127);
    }
    int status = -1; if (child > 0) waitpid(child, &status, 0);
    Check(child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "authority survives real exec without credential arguments or files");
    Check(!CeBootSignalAuthority::Consume(inherited.Release(), Utc + 1, Mono + 100), "child consumption exhausts parent duplicate position");
    auto replaced = Owner(directory); Authenticate(*replaced); auto pending = replaced->Seal(Utc, Mono);
    const std::string kept = root + "/retained-selected";
    bool moved = rename(directory.c_str(), kept.c_str()) == 0 && mkdir(directory.c_str(), 0700) == 0;
    Check(moved && !CeBootSignalAuthority::Consume(pending.Release(), Utc + 1, Mono + 100), "directory replacement before boot fails closed");
    printf("SIGNAL_HANDOFF_TESTS checks=%d failures=%d\n", checks, failures); return failures ? 1 : 0;
}
