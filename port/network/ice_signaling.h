// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "network.h"
#include <cstddef>
#include <cstdint>
#include <memory>

// This boundary carries decoded bounded events between the native signaling
// adapter and the existing ICE owner. Wire parsing and bearer authority belong
// to the adapter; credential and CE record authentication belong to the owner.
constexpr size_t CE_ICE_SIGNAL_CONTEXT_SIZE = 32;
constexpr size_t CE_ICE_SIGNAL_DIGEST_SIZE = 32;
constexpr size_t CE_ICE_SIGNAL_GRANT_MAX = 32768;
constexpr size_t CE_ICE_SIGNAL_RECORD_MAX = 4194;

enum class CeIceSignalStageKind : uint32_t { Lobby, Game };
enum class CeIceSignalRead : uint32_t { Pending, Ready, Failed };

enum class CeIceSignalStatus : uint32_t {
    Unavailable,
    AuthenticationRequired,
    NotAdmitted,
    InvalidRequest,
    RateLimited,
    NeedsConsent,             // Local view only; never a wire status.
    AwaitingPairConsent,
    Admitted,
    AwaitingPeerRequest,
    AwaitingPeerRecord,       // Local view while a guest waits for the offer.
    Exchanging,
    ServiceDeliveryComplete, // Does not establish ICE or game readiness.
    Issuing,
    IssuanceUnknown,
    SetupExpired,
    GrantExpired,
    Closed,
    Expired
};

// The only player-facing view. No authority, approval, subject, credential,
// record, raw response or upstream error string is exposed here.
struct CeIceSignalView {
    CeIceSignalStatus status = CeIceSignalStatus::Unavailable;
    CE_NetRole role{};
    uint64_t setupExpiresAt = 0;
    uint64_t grantExpiresAt = 0;
    uint64_t pairExpiresAt = 0;
};

struct CeIceSignalRequest {
    CeIceSignalStageKind stage = CeIceSignalStageKind::Lobby;
    CE_NetRole role{};
    uint8_t context[CE_ICE_SIGNAL_CONTEXT_SIZE]{};
    // This selected private directory remains owned by CeIceExchange. Prepare
    // must copy the name if it retains it, and must never log or publish it.
    const char* directory = nullptr;
    uint64_t requestedAt = 0;
    uint64_t requestExpiresAt = 0;
    uint64_t grantExpiresAt = 0;
    // The earlier local monotonic limit is authoritative. Pumping, consent and
    // retries must never extend this deadline or replace the fixed request.
    uint64_t setupDeadlineMs = 0;
};

// Nonsecret fixed Game request values restored only after the native boot owner
// consumes its authenticated START authority handle. This plan is not authority:
// the provider independently checks its sealed pair and directory binding.
struct CeIceSignalGamePlan {
    CE_NetRole role{};
    uint8_t context[CE_ICE_SIGNAL_CONTEXT_SIZE]{};
    uint64_t requestedAt = 0;
    uint64_t requestExpiresAt = 0;
    uint64_t grantExpiresAt = 0;
    uint64_t setupDeadlineMs = 0;
};

class CeIceSignalStage {
public:
    // Destruction is local cancellation only. It must not close the pair;
    // authenticated START transfers that pair to a fresh game-stage owner.
    virtual ~CeIceSignalStage() = default;
    virtual bool Accept() noexcept = 0;
    virtual void Poll(uint64_t utcSeconds, uint64_t monotonicMs) noexcept = 0;
    virtual bool PublishLocal(const uint8_t* record, size_t size) noexcept = 0;
    // Ready consumes an event only after a successful bounded copy. Bytes need
    // not have a trailing NUL. Pending is not a failure or permission to remint.
    virtual CeIceSignalRead TakeOwnGrant(char* bytes, size_t capacity,
                                         size_t* size) noexcept = 0;
    virtual CeIceSignalRead TakePeerRecord(uint8_t* bytes, size_t capacity,
                                           size_t* size,
                                           uint8_t digest[CE_ICE_SIGNAL_DIGEST_SIZE]) noexcept = 0;
    // Called only after the existing CE owner has imported and authenticated
    // this record. Acknowledgement itself does not establish CE readiness.
    virtual bool AcknowledgePeer(const uint8_t digest[CE_ICE_SIGNAL_DIGEST_SIZE]) noexcept = 0;
    virtual CeIceSignalView View() const noexcept = 0;
    virtual void CancelLocal() noexcept = 0;
};

class CeIceSignalProvider {
public:
    virtual ~CeIceSignalProvider() = default;
    // Idempotent bounded pre-stage pump: bind pair/current and pump an explicit
    // Accept without a CE context or stage request. Never consent implicitly.
    virtual void Preflight(uint64_t utcSeconds, uint64_t monotonicMs) noexcept = 0;
    virtual CeIceSignalView View() const noexcept = 0;
    virtual bool Accept() noexcept = 0;
    // Requires the same admitted pair/role. Game authorization is checked from
    // the provider's typed authenticated-START state, never a caller boolean.
    // A null result is a refusal; the caller must not fall back to manual files.
    virtual std::unique_ptr<CeIceSignalStage> Prepare(
        const CeIceSignalRequest& request) noexcept = 0;
    // Terminal, idempotent best-effort pair close. This is separate from local
    // stage cancellation so an ordinary lobby teardown cannot end START.
    virtual void ClosePair() noexcept = 0;
    // Read-only pair binding is still in flight; this does not imply admission.
    virtual bool PreflightPending() const noexcept { return false; }
};
