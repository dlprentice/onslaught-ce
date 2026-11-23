// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Trusted transport boundary. Production uses the platform HTTPS factory;
// authored fixtures may inject this interface without opening sockets.
enum class CeSignalHttpsState { Pending, Ready, Failed };
struct CeSignalHttpsReply { unsigned code=0; std::string body; };
class CeSignalHttps {
public:
    virtual ~CeSignalHttps()=default;
    // Copies bounded inputs before returning. One POST at a time to one fixed
    // origin; deadline is absolute monotonic time. No ambient identity inputs.
    virtual bool Begin(const char* route,const char* bearer,size_t bearerSize,
                       const std::string& body,uint64_t nowMs,uint64_t deadlineMs) noexcept=0;
    // Single caller thread, nonblocking pump. Ready consumes one reply.
    virtual CeSignalHttpsState Poll(uint64_t nowMs,CeSignalHttpsReply* reply) noexcept=0;
    virtual void Cancel() noexcept=0;
};
// Pinned libcurl 7.74 requires initialization/cleanup while no other process
// threads are running. Startup/shutdown owns these calls, never the menu pump.
// Shutdown refuses live transports. Windows uses WinHTTP and these are no-ops.
bool CeInitializeSignalHttps() noexcept;
bool CeShutdownSignalHttps() noexcept;
// Pure syntax/bounds check shared with the factory. Requires no initialization
// and constructs no transport; it never performs IO or reads ambient state.
bool CeSignalHttpsOriginValid(const char* explicitOrigin) noexcept;
// Constructs but does not send. Only ASCII DNS HTTPS origins with optional port
// are accepted. No path, query, userinfo, proxy, redirects or insecure TLS mode.
// Requires successful explicit initialization on Linux.
std::unique_ptr<CeSignalHttps> CeCreateSignalHttps(const char* explicitOrigin) noexcept;

// Non-blocking HTTPS/JSON transport for the /ce-lobby/v1/ online lobby service.
// Supports GET and POST on strict /ce-lobby/v1/... paths with optional Bearer auth.
enum class CeOnlineHttpMethod { Get, Post };

class CeOnlineHttps {
public:
    virtual ~CeOnlineHttps() = default;
    // Copies bounded inputs before returning. One GET or POST at a time to one fixed
    // origin on /ce-lobby/v1/... paths; bearer is optional (nullptr/0 when unauthenticated).
    // Deadline is absolute monotonic time in milliseconds. No ambient identity or proxy inputs.
    virtual bool Begin(CeOnlineHttpMethod method, const char* pathAndQuery,
                       const char* bearer, size_t bearerSize,
                       const std::string& body, uint64_t nowMs, uint64_t deadlineMs) noexcept = 0;
    // Single caller thread, nonblocking pump. Ready consumes one reply.
    virtual CeSignalHttpsState Poll(uint64_t nowMs, CeSignalHttpsReply* reply) noexcept = 0;
    virtual void Cancel() noexcept = 0;
};

// Pure syntax/bounds checks shared with CeCreateOnlineHttps.
// When allowLoopbackHttp is true, http://127.0.0.1:<port> is accepted strictly for
// local loopback integration tests; never reads ambient environment variables.
bool CeOnlineHttpsOriginValid(const char* explicitOrigin, bool allowLoopbackHttp = false) noexcept;
bool CeOnlineHttpsPathValid(const char* pathAndQuery) noexcept;
std::unique_ptr<CeOnlineHttps> CeCreateOnlineHttps(const char* explicitOrigin,
                                                   bool allowLoopbackHttp = false) noexcept;
