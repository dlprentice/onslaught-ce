// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ice_signaling.h"
#include "signaling_https.h"

// Private operator-selected admission binding. This contains no CE join secret
// and is never part of the frontend View or a stage request.
struct CeIceSignalPairBinding {
    char approval[33]{};
    CE_NetRole role{};
    uint64_t expiresAt=0;
};
class CeIceSignalAuthorityAccess;
class CeIceSignalAuthority {
public:
    virtual ~CeIceSignalAuthority()=default;
    virtual bool ExpectedPair(CeIceSignalPairBinding* out) const noexcept=0;
    // Trusted authenticated START owner binds every immutable game request
    // field and selected directory identity. No caller/menu boolean suffices.
    virtual bool AllowsGame(const CeIceSignalRequest& request) const noexcept=0;
private:
    friend class CeIceSignalAuthorityAccess;
    // Only the provider access helper can call this. The owning implementation
    // overrides privately and keeps bytes out of ordinary files/argv/env/logs.
    virtual bool CopyBearer(char* bytes,size_t capacity,size_t* size) const noexcept=0;
};
// Neither factory makes a request. Production constructs the platform HTTPS
// transport. The explicit transport overload is the trusted authored-test seam.
std::shared_ptr<CeIceSignalProvider> CeCreateSignalingProvider(
    const char* explicitHttpsOrigin,std::shared_ptr<const CeIceSignalAuthority> authority) noexcept;
std::shared_ptr<CeIceSignalProvider> CeCreateSignalingProviderWithTransport(
    std::unique_ptr<CeSignalHttps> transport,
    std::shared_ptr<const CeIceSignalAuthority> authority) noexcept;
