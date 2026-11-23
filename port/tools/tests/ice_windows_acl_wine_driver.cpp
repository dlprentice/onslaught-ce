// SPDX-License-Identifier: GPL-3.0-only
// Windows NTFS private ACL, ICE credential, and sealed kernel-section handoff checks under Wine/Windows.
// Uses authored fixture data only; no game files, external servers, or real credentials.
#ifndef _WIN32
#error "ice_windows_acl_wine_driver.cpp is a Windows i686 test driver"
#endif

#include "network/windows_private_acl.h"
#include "network/ice_credentials.h"
#include "network/ice_exchange.h"
#include "game/ce_network_boot_authority.h"

#include <aclapi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

namespace {

constexpr uint64_t kUtc = 1700000000ULL;
constexpr uint64_t kMono = 1000000ULL;
constexpr uint32_t kLevel = 100;
const char kBearer[] = "fixture-win-sealed-authority-token";
const char kOrigin[] = "https://signaling.fixture.invalid";

int gChecks = 0;
int gFailures = 0;

void Check(bool ok, const char* label) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s (gle=%lu)\n", label, static_cast<unsigned long>(GetLastError()));
    }
}

std::wstring ToWide(const std::string& utf8) {
    std::wstring out;
    if (!CeWinAcl::NormalizeAbsolutePath(utf8.c_str(), out)) return {};
    return out;
}

bool GrantWorldRead(const std::wstring& path) {
    std::array<BYTE, SECURITY_MAX_SID_SIZE> world{};
    DWORD worldSize = static_cast<DWORD>(world.size());
    if (!CreateWellKnownSid(WinWorldSid, nullptr, world.data(), &worldSize)) return false;

    std::vector<BYTE> userSid;
    if (!CeWinAcl::CurrentUserSid(userSid)) return false;

    EXPLICIT_ACCESSW ea[2]{};
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(userSid.data());

    ea[1].grfAccessPermissions = GENERIC_READ;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(world.data());

    PACL newDacl = nullptr;
    if (SetEntriesInAclW(2, ea, nullptr, &newDacl) != ERROR_SUCCESS || !newDacl) return false;
    DWORD r = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    nullptr, nullptr, newDacl, nullptr);
    LocalFree(newDacl);
    return r == ERROR_SUCCESS;
}

std::string Hex32(const uint8_t bytes[32]) {
    const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (size_t i = 0; i < 32; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

std::array<uint8_t, 32> FixtureSecret() {
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(i + 9);
    return out;
}

CE_NetConfig FixtureConfig() {
    CE_NetConfig cfg{};
    cfg.role = CE_NET_HOST;
    auto sec = FixtureSecret();
    std::memcpy(cfg.secret, sec.data(), sec.size());
    for (size_t i = 0; i < sizeof cfg.compatibility; ++i) cfg.compatibility[i] = static_cast<uint8_t>(i * 5 + 1);
    return cfg;
}

CeIceSignalPairBinding FixturePair() {
    CeIceSignalPairBinding pair{};
    std::fill(pair.approval, pair.approval + 32, 'b');
    pair.role = CE_NET_HOST;
    pair.expiresAt = kUtc + 3600;
    return pair;
}

std::string MakeCloudflareGrant(const char* role, const uint8_t ctx[32], uint64_t issuedAt, uint64_t expiresAt) {
    return std::string("{\"schema\":1,\"role\":\"") + role +
        "\",\"context\":\"" + Hex32(ctx) +
        "\",\"issuedAt\":" + std::to_string(issuedAt) +
        ",\"expiresAt\":" + std::to_string(expiresAt) +
        ",\"iceServers\":[{\"urls\":[\"turn:turn.cloudflare.com:3478?transport=udp\"],"
        "\"username\":\"fixture-turn-user\",\"credential\":\"fixture-turn-pass\"}]}\n";
}

void TestWinAclPrimitives(const std::string& root) {
    const std::string privPath = root + "\\acl-private";
    const std::string pubPath = root + "\\acl-public";
    const std::string taintedPath = root + "\\acl-tainted";
    const std::wstring privWide = ToWide(privPath);
    const std::wstring pubWide = ToWide(pubPath);
    const std::wstring taintedWide = ToWide(taintedPath);

    Check(CeWinAcl::CreatePrivateDirectory(privWide), "CreatePrivateDirectory succeeds");
    bool already = false;
    Check(!CeWinAcl::CreatePrivateDirectory(privWide, &already) && already,
          "CreatePrivateDirectory reports ERROR_ALREADY_EXISTS");
    Check(CreateDirectoryW(pubWide.c_str(), nullptr) != 0, "CreateDirectoryW default dir created");
    Check(CeWinAcl::CreatePrivateDirectory(taintedWide), "CreatePrivateDirectory tainted created");
    Check(GrantWorldRead(taintedWide), "GrantWorldRead adds Everyone ACE");

    CeWinAcl::ScopedHandle h;
    Check(CeWinAcl::OpenPrivateDirectory(privPath.c_str(), h) && h.Good(),
          "OpenPrivateDirectory accepts owner-only private directory");
    h.Reset();
    Check(!CeWinAcl::OpenPrivateDirectory(taintedPath.c_str(), h),
          "OpenPrivateDirectory refuses Everyone-readable directory");
    Check(!CeWinAcl::OpenPrivateDirectory("relative\\dir", h),
          "OpenPrivateDirectory refuses relative path");
    Check(!CeWinAcl::OpenPrivateDirectory((root + "\\..\\acl-private").c_str(), h),
          "OpenPrivateDirectory refuses dot-dot traversal");
    Check(!CeWinAcl::OpenPrivateDirectory((privPath + ":stream").c_str(), h),
          "OpenPrivateDirectory refuses alternate data stream colon");
    Check(!CeWinAcl::OpenPrivateDirectory((root + "\\missing-dir").c_str(), h),
          "OpenPrivateDirectory refuses missing directory");

    const std::string payload = "owner-only-secret-bytes";
    Check(CeWinAcl::PublishPrivateFileNoReplace(privWide, ".tmp-1", "good.bin", payload.data(), payload.size()),
          "PublishPrivateFileNoReplace publishes private file");
    Check(!CeWinAcl::PublishPrivateFileNoReplace(privWide, ".tmp-2", "good.bin", payload.data(), payload.size()),
          "PublishPrivateFileNoReplace refuses replacing existing file");

    std::vector<uint8_t> readBack;
    bool notFound = false;
    Check(CeWinAcl::ReadPrivateFile(privWide, "good.bin", 1, 1024, readBack, &notFound) &&
          !notFound && std::string(readBack.begin(), readBack.end()) == payload,
          "ReadPrivateFile reads exact published private bytes");
    Check(!CeWinAcl::ReadPrivateFile(privWide, "absent.bin", 1, 1024, readBack, &notFound) && notFound,
          "ReadPrivateFile reports outNotFound for missing file");
    Check(!CeWinAcl::ReadPrivateFile(privWide, "good.bin", 1, 4, readBack, &notFound) && !notFound,
          "ReadPrivateFile enforces maxBytes bound");

    // Hard link refusal (nNumberOfLinks > 1)
    const std::string linkPayload = "linked-bytes";
    Check(CeWinAcl::PublishPrivateFileNoReplace(privWide, ".tmp-hl", "hardsrc.bin", linkPayload.data(), linkPayload.size()),
          "PublishPrivateFileNoReplace hard-link source");
    const std::wstring hardSrc = privWide + L"\\hardsrc.bin";
    const std::wstring hardDst = privWide + L"\\harddst.bin";
    if (CreateHardLinkW(hardDst.c_str(), hardSrc.c_str(), nullptr)) {
        Check(!CeWinAcl::ReadPrivateFile(privWide, "hardsrc.bin", 1, 1024, readBack, &notFound),
              "ReadPrivateFile refuses hard-linked file");
    }

    // World-readable file inside private directory must be refused
    const std::string openPayload = "exposed-file";
    Check(CeWinAcl::PublishPrivateFileNoReplace(privWide, ".tmp-open", "exposed.bin", openPayload.data(), openPayload.size()),
          "PublishPrivateFileNoReplace exposed fixture");
    Check(GrantWorldRead(privWide + L"\\exposed.bin"), "GrantWorldRead on file");
    Check(!CeWinAcl::ReadPrivateFile(privWide, "exposed.bin", 1, 1024, readBack, &notFound) && !notFound,
          "ReadPrivateFile refuses Everyone-readable file inside private directory");
}

void TestWinIceCredentials(const std::string& root) {
    const std::string dirA = root + "\\cred-a";
    const std::string dirB = root + "\\cred-b";
    const std::string dirBad = root + "\\cred-bad";
    Check(CeWinAcl::CreatePrivateDirectory(ToWide(dirA)), "cred-a created");
    Check(CeWinAcl::CreatePrivateDirectory(ToWide(dirB)), "cred-b created");
    Check(CeWinAcl::CreatePrivateDirectory(ToWide(dirBad)), "cred-bad created");
    Check(GrantWorldRead(ToWide(dirBad)), "cred-bad made world-readable");

    CeIceCredentials creds;
    Check(creds.RequireDistinctDirectories(dirA.c_str(), dirB.c_str()),
          "RequireDistinctDirectories accepts two distinct private dirs");
    Check(!creds.RequireDistinctDirectories(dirA.c_str(), dirA.c_str()),
          "RequireDistinctDirectories refuses identical directories");
    Check(!creds.RequireDistinctDirectories(dirA.c_str(), dirBad.c_str()),
          "RequireDistinctDirectories refuses world-readable directory");

    uint8_t ctx[32];
    for (size_t i = 0; i < 32; ++i) ctx[i] = static_cast<uint8_t>(0xA0u + i);

    Check(creds.PrepareRequest(dirA.c_str(), 1, ctx, kUtc),
          "PrepareRequest writes relay-host.request.json");
    Check(!creds.PrepareRequest(dirA.c_str(), 1, ctx, kUtc),
          "PrepareRequest refuses duplicate request overwrite");
    Check(!creds.PrepareRequest(dirBad.c_str(), 1, ctx, kUtc),
          "PrepareRequest refuses world-readable directory");

    Check(creds.TryOpen(dirA.c_str(), 1, ctx, kUtc) == CeIceCredentials::LoadState::Pending,
          "TryOpen returns Pending when grant absent");

    const std::string grant = MakeCloudflareGrant("host", ctx, kUtc, kUtc + 3600);
    Check(CeWinAcl::PublishPrivateFileNoReplace(ToWide(dirA), ".tmp-grant", "relay-host.json", grant.data(), grant.size()),
          "Published private Cloudflare grant");
    Check(creds.TryOpen(dirA.c_str(), 1, ctx, kUtc) == CeIceCredentials::LoadState::Ready,
          "TryOpen parses valid private Cloudflare grant on Windows");
    juice_config_t jcfg{};
    Check(creds.IsCurrent(kUtc) && creds.Configure(jcfg, kUtc) &&
          jcfg.turn_servers_count == 1 && jcfg.turn_servers != nullptr &&
          jcfg.turn_servers[0].port == 3478 &&
          std::strcmp(jcfg.turn_servers[0].host, "turn.cloudflare.com") == 0 &&
          std::strcmp(jcfg.turn_servers[0].username, "fixture-turn-user") == 0 &&
          std::strcmp(jcfg.turn_servers[0].password, "fixture-turn-pass") == 0,
          "Parsed Windows TURN grant fields match");

    Check(creds.TryOpen(dirA.c_str(), 1, ctx, kUtc + 4000) == CeIceCredentials::LoadState::Error,
          "TryOpen refuses expired grant");
    Check(creds.TryOpen(dirA.c_str(), 2, ctx, kUtc) == CeIceCredentials::LoadState::Pending,
          "TryOpen guest does not read host grant");
    uint8_t wrongCtx[32];
    std::memcpy(wrongCtx, ctx, 32);
    wrongCtx[0] ^= 1;
    Check(creds.TryOpen(dirA.c_str(), 1, wrongCtx, kUtc) == CeIceCredentials::LoadState::Error,
          "TryOpen refuses wrong context grant");

    // World-readable grant file in private dirB must fail closed
    Check(CeWinAcl::PublishPrivateFileNoReplace(ToWide(dirB), ".tmp-grant", "relay-host.json", grant.data(), grant.size()),
          "Published grant in dirB");
    Check(GrantWorldRead(ToWide(dirB) + L"\\relay-host.json"), "Made dirB grant world-readable");
    Check(creds.TryOpen(dirB.c_str(), 1, ctx, kUtc) == CeIceCredentials::LoadState::Error,
          "TryOpen refuses Everyone-readable grant file");
}

int RunChildConsume(const char* handleArg, const char* dirArg) {
    char* end = nullptr;
    const unsigned long val = std::strtoul(handleArg, &end, 10);
    if (!handleArg || !*handleArg || (end && *end)) {
        std::fprintf(stderr, "FAIL: child bad handle arg\n");
        return 2;
    }
    const int descriptor = static_cast<int>(val);
    auto consumed = CeBootSignalAuthority::Consume(descriptor, kUtc, kMono);
    Check(consumed != nullptr, "child CeBootSignalAuthority::Consume succeeds across CreateProcessW");
    if (consumed) {
        Check(std::strcmp(consumed->Origin(), kOrigin) == 0, "child origin preserved");
        Check(std::strcmp(consumed->Directory(), dirArg) == 0, "child directory preserved");
        CeIceSignalPairBinding pair{};
        Check(consumed->ExpectedPair(&pair) && pair.role == CE_NET_HOST && pair.expiresAt == kUtc + 3600,
              "child pair binding preserved");
        CE_Handoff handoff{};
        Check(consumed->CopyLaunch(&handoff) && handoff.role == CE_NET_HOST && handoff.level == kLevel,
              "child launch handoff preserved");
        CE_NetConfig cfg = FixtureConfig();
        CeIceSignalGamePlan plan{};
        Check(consumed->BindBoot(cfg, kLevel, kUtc + 1, kMono + 10, &plan),
              "child BindBoot succeeds");
        uint8_t sec[32]{};
        auto expectedSec = FixtureSecret();
        Check(consumed->CopyGameSecret(CE_NET_HOST, kLevel, sec) &&
              std::memcmp(sec, expectedSec.data(), 32) == 0,
              "child game secret preserved");
        CeIceSignalRequest req{};
        req.stage = CeIceSignalStageKind::Game;
        req.role = plan.role;
        std::memcpy(req.context, plan.context, 32);
        req.directory = dirArg;
        req.requestedAt = plan.requestedAt;
        req.requestExpiresAt = plan.requestExpiresAt;
        req.grantExpiresAt = plan.grantExpiresAt;
        req.setupDeadlineMs = plan.setupDeadlineMs;
        Check(consumed->AllowsGame(req), "child AllowsGame accepts bound request");
    }
    // Replay on closed/consumed handle must fail
    Check(!CeBootSignalAuthority::Consume(descriptor, kUtc, kMono),
          "child replay Consume refuses closed handle");
    std::printf("CHILD_CONSUME checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}

void TestWinSealedSectionHandoff(const std::string& exePath, const std::string& root) {
    const std::string sigDir = root + "\\signal-priv";
    const std::string badDir = root + "\\signal-pub";
    Check(CeWinAcl::CreatePrivateDirectory(ToWide(sigDir)), "signal-priv created");
    Check(CeWinAcl::CreatePrivateDirectory(ToWide(badDir)), "signal-pub created");
    Check(GrantWorldRead(ToWide(badDir)), "signal-pub made world-readable");

    Check(!CeBootSignalAuthority::Create(kOrigin, kBearer, std::strlen(kBearer), FixturePair(), badDir.c_str()),
          "CeBootSignalAuthority::Create refuses world-readable directory");

    auto owner = CeBootSignalAuthority::Create(kOrigin, kBearer, std::strlen(kBearer), FixturePair(), sigDir.c_str());
    Check(owner != nullptr, "CeBootSignalAuthority::Create accepts private directory");
    if (!owner) return;

    auto secret = FixtureSecret();
    Check(CeLobbySignalingTransition::Authenticate(*owner, CE_NET_HOST, kLevel, secret.data(), kUtc, kMono),
          "CeBootSignalAuthority::Authorize succeeds");

    CE_Handoff launch{};
    launch.created = static_cast<int64_t>(kUtc);
    launch.kind = CE_HANDOFF_LAUNCH;
    launch.role = CE_NET_HOST;
    launch.level = kLevel;
    std::memcpy(launch.secret, secret.data(), 32);
    Check(owner->SetLaunch(launch), "CeBootSignalAuthority::SetLaunch succeeds");

    CeSignalAuthorityFd sealed = owner->Seal(kUtc, kMono);
    Check(static_cast<bool>(sealed), "CeBootSignalAuthority::Seal produces Windows section handle");
    if (!sealed) return;

    const int rawFd = sealed.PrepareExec();
    Check(rawFd >= 3, "CeSignalAuthorityFd::PrepareExec validates and marks handle inheritable");

    // Verify parent cannot map the sealed handle for write!
    HANDLE sealedHandle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(rawFd));
    void* badWriteView = MapViewOfFile(sealedHandle, FILE_MAP_WRITE, 0, 0, 0);
    Check(badWriteView == nullptr, "sealed handle refuses FILE_MAP_WRITE");
    if (badWriteView) UnmapViewOfFile(badWriteView);

    // Snapshot valid serialized payload bytes so our forged writable section has an identical valid payload.
    std::vector<uint8_t> validSectionBytes;
    if (void* roView = MapViewOfFile(sealedHandle, FILE_MAP_READ, 0, 0, 0)) {
        const auto* hdr = static_cast<const uint8_t*>(roView);
        const uint32_t payloadLen = uint32_t(hdr[0]) | (uint32_t(hdr[1]) << 8) |
                                    (uint32_t(hdr[2]) << 16) | (uint32_t(hdr[3]) << 24);
        if (payloadLen > 0 && payloadLen <= 16384) {
            validSectionBytes.assign(hdr, hdr + 4 + payloadLen);
        }
        UnmapViewOfFile(roView);
    }
    Check(!validSectionBytes.empty(), "captured serialized sealed section bytes");

    // Spawn real Windows child process with bInheritHandles = TRUE!
    std::string cmd = "\"" + exePath + "\" --child " + std::to_string(static_cast<unsigned long>(static_cast<uint32_t>(rawFd))) +
                      " \"" + sigDir + "\"";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL spawned = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    Check(spawned != 0, "CreateProcessA with bInheritHandles=TRUE spawns child");
    if (spawned) {
        DWORD waitRc = WaitForSingleObject(pi.hProcess, 30000);
        DWORD exitCode = 99;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Check(waitRc == WAIT_OBJECT_0 && exitCode == 0, "child process consumed sealed authority cleanly");
    }
    sealed.Reset();

    // Forged WRITABLE anonymous section containing the exact valid payload must still be refused by Consume!
    HANDLE forged = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                       static_cast<DWORD>(validSectionBytes.size() ? validSectionBytes.size() : 4096), nullptr);
    Check(forged != nullptr, "forged writable section created for negative control");
    if (forged) {
        void* v = MapViewOfFile(forged, FILE_MAP_WRITE, 0, 0, 0);
        if (v && !validSectionBytes.empty()) {
            std::memcpy(v, validSectionBytes.data(), validSectionBytes.size());
            UnmapViewOfFile(v);
        }
        int forgedFd = static_cast<int>(reinterpret_cast<intptr_t>(forged));
        Check(!CeBootSignalAuthority::Consume(forgedFd, kUtc, kMono),
              "CeBootSignalAuthority::Consume refuses forged writable kernel section");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--child") == 0) {
        return RunChildConsume(argv[2], argv[3]);
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <C:\\\\path\\\\to\\\\dir>\n", argv[0]);
        return 2;
    }
    TestWinAclPrimitives(argv[1]);
    TestWinIceCredentials(argv[1]);
    TestWinSealedSectionHandoff(argv[0], argv[1]);
    std::printf("WINDOWS_ICE_ACL_TESTS checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
