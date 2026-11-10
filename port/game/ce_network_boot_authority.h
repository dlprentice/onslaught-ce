// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#include "ce_network_stl.h"
#include "../network/signaling_provider.h"
#include "../platform/common/ce_netcode.h"
#include <cstddef>
#include <cstdint>
#include <memory>

// Construction belongs to the authenticated lobby transition, not a menu/consent flag.
struct CeLobbySignalingTransition;
class CeBootSignalAuthority;
class CeAuthenticatedLobbyStart final {
    friend struct CeLobbySignalingTransition;
    friend class CeBootSignalAuthority;
    CeAuthenticatedLobbyStart(CE_NetRole role, uint32_t level, const uint8_t secret[32],
                              uint64_t utcSeconds, uint64_t monotonicMs) noexcept;
    CE_NetRole role_;
    uint32_t level_;
    uint8_t secret_[32];
    uint64_t utc_, mono_;
public:
    CeAuthenticatedLobbyStart(const CeAuthenticatedLobbyStart&) = delete;
    CeAuthenticatedLobbyStart& operator=(const CeAuthenticatedLobbyStart&) = delete;
    ~CeAuthenticatedLobbyStart();
};

// Owned, never guessed; close-on-exec until the intended exec is about to start.
// Only the nonsecret descriptor number may cross in launch metadata.
class CeSignalAuthorityFd final {
    friend class CeBootSignalAuthority;
    explicit CeSignalAuthorityFd(int descriptor) noexcept : fd_(descriptor) {}
    int fd_ = -1;
public:
    CeSignalAuthorityFd() noexcept = default;
    CeSignalAuthorityFd(const CeSignalAuthorityFd&) = delete;
    CeSignalAuthorityFd& operator=(const CeSignalAuthorityFd&) = delete;
    CeSignalAuthorityFd(CeSignalAuthorityFd&&) noexcept;
    CeSignalAuthorityFd& operator=(CeSignalAuthorityFd&&) noexcept;
    ~CeSignalAuthorityFd();
    explicit operator bool() const noexcept { return fd_ >= 0; }
    int Release() noexcept { int descriptor = fd_; fd_ = -1; return descriptor; }
    int PrepareExec() noexcept;
    void Reset() noexcept;
};

// The ordinary launch handoff carries no signaling authority. This owner retains the bearer
// in memory; Linux exec transfer uses a sealed, unlinked kernel-memory object. Other platforms
// refuse until an equivalent owned native-handle path has been accepted.
class CeBootSignalAuthority final : public CeIceSignalAuthority {
    struct State;
    std::unique_ptr<State> state_;
    explicit CeBootSignalAuthority(std::unique_ptr<State>) noexcept;
    bool CopyBearer(char*, size_t, size_t*) const noexcept override;
public:
    static std::shared_ptr<CeBootSignalAuthority> Create(const char* httpsOrigin,
        const char* bearer, size_t bearerSize, const CeIceSignalPairBinding&,
        const char* selectedDirectory) noexcept;
    // Takes ownership even on refusal. Consumption is one bounded read, with no seek back;
    // duplicate descriptors share its position and cannot replay through this API.
    static std::shared_ptr<CeBootSignalAuthority> Consume(int descriptor,
        uint64_t utcSeconds, uint64_t monotonicMs) noexcept;
    ~CeBootSignalAuthority() override;
    bool ExpectedPair(CeIceSignalPairBinding*) const noexcept override;
    bool AllowsGame(const CeIceSignalRequest&) const noexcept override;
    bool Authorize(const CeAuthenticatedLobbyStart&) noexcept;
    bool SetLaunch(const CE_Handoff&) noexcept;
    bool CopyLaunch(CE_Handoff*) const noexcept;
    CeSignalAuthorityFd Seal(uint64_t utcSeconds, uint64_t monotonicMs) const noexcept;
    bool MatchesStart(CE_NetRole, uint32_t level, const uint8_t secret[32]) const noexcept;
    // Call only after the selected level passed normal boot compatibility preparation.
    bool BindBoot(const CE_NetConfig&, uint32_t level, uint64_t utcSeconds,
        uint64_t monotonicMs, CeIceSignalGamePlan*) noexcept;
    const char* Origin() const noexcept;
    const char* Directory() const noexcept;
    bool Started() const noexcept;
    bool CopyGameSecret(CE_NetRole, uint32_t level, uint8_t out[32]) const noexcept;
    void Revoke() noexcept;
};
#endif
#endif
