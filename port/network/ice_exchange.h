// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_ICE_EXCHANGE_H
#define CE_ICE_EXCHANGE_H
#if defined(CE_NETWORK_ICE)
#include "network.h"
#include <cstddef>
#include <cstdint>
#include <memory>

enum class CeIceExchangeResult { Pending, Ready, Error };

// Local, per-session offer/answer exchange. The caller supplies wall-clock UTC
// seconds, an explicitly selected private directory and its existing CE config.
// Files contain authenticated ciphertext, never plaintext SDP or TURN secrets.
// A shared directory is sufficient for two local instances. Remote peers need
// an explicit file transfer; this class has no network or broker connection.
// Host attempts refuse existing same-context files; a fresh CE stage secret
// uses distinct filenames under the same directory. New records must arrive
// atomically and accepted records must remain unchanged for the object's life.
// The five-minute expiry bounds exchange, not an already established session.
// POSIX checks owner/private directory mode. Windows rejects reparse entries;
// the caller remains responsible for a private Windows directory and its ACL.
class CeIceExchange {
public:
    static constexpr size_t MaxRecordBytes = 4194;
    // Pure binding helper, identical to Open: keyed BLAKE2b-256 over the
    // context label and compatibility, keyed by the nonzero session secret.
    // The role is validated but is not part of the shared context.
    static bool ContextForConfig(const CE_NetConfig* config,
                                 uint8_t context[32]) noexcept;
    static std::unique_ptr<CeIceExchange> Open(const CE_NetConfig& config,
                                              const char* private_directory,
                                              uint64_t now_unix_seconds);
    ~CeIceExchange();
    CeIceExchange(const CeIceExchange&) = delete;
    CeIceExchange& operator=(const CeIceExchange&) = delete;
    CeIceExchangeResult PublishLocal(const char* sdp, size_t size, uint64_t now);
    CeIceExchangeResult PollRemote(char* sdp, size_t capacity, size_t* size, uint64_t now);
    // Bounded broker bridge over the existing authenticated format. Export is
    // Pending until PublishLocal succeeds; it never consumes the local record.
    // A small/null output buffer is refused without consuming or changing it.
    CeIceExchangeResult ExportLocalRecord(uint8_t* bytes, size_t capacity,
                                          size_t* size, uint64_t now);
    // Authenticates before no-replace publication; identical retries are Ready.
    // Does not consume SDP: PollRemote still delivers it once. A host returns
    // Pending until its own offer exists; callers must retain the pending input.
    CeIceExchangeResult ImportPeerRecord(const uint8_t* bytes, size_t size, uint64_t now);
    const char* Error() const;
    // Exactly 32 locally derived binding bytes; this is not account identity or
    // a credential-broker authorization. The pointer lasts until destruction.
    const uint8_t* Context() const;
    CE_NetRole Role() const;

private:
    struct Impl;
    explicit CeIceExchange(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
#endif
#endif
