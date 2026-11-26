// SPDX-License-Identifier: GPL-3.0-only
// Authenticated 4-peer (64-ready wire schema) Star Host-Relay transport over ENet + optional multi-slot CeIceBridge.
#ifndef CE_STAR_TRANSPORT_H
#define CE_STAR_TRANSPORT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#if defined(CE_NETWORK_ICE)
#include <juice/juice.h>
#endif

struct _ENetHost;
struct _ENetPeer;
struct _ENetEvent;
class CeIceBridge;

constexpr uint16_t CE_STAR_PROTO_VERSION = 1;
constexpr uint16_t CE_STAR_MAX_WIRE_SLOTS = 64;
constexpr uint16_t CE_STAR_DEFAULT_MAX_GUESTS = 3;
constexpr size_t CE_STAR_MAX_FRAME_PAYLOAD = 4096;
constexpr size_t CE_STAR_MAX_CHAT_BYTES = 160;
constexpr size_t CE_STAR_SECRET_BYTES = 32;
constexpr size_t CE_STAR_DIGEST_BYTES = 32;

enum class CeStarRole : uint8_t {
    Host = 1,
    Guest = 2
};

enum class CeStarRoute : uint8_t {
    Unknown = 0,
    Direct = 1,
    Relay = 2
};

enum class CeStarError : uint16_t {
    Ok = 0,
    BadConfig = 1,
    Io = 2,
    AuthFailed = 3,
    Incompatible = 4,
    Protocol = 5,
    Timeout = 6,
    Capacity = 7,
    HostLost = 8,
    SlotRejected = 9
};

enum class CeStarMsgType : uint16_t {
    Hello = 1,
    Accept = 2,
    InputGuest = 10,
    InputBundle = 11,
    Hash = 12,
    Desync = 13,
    Chat = 20,
    PingReport = 21,
    Keepalive = 22,
    Leave = 30
};

// 64-ready wire bitmask: encoded as u16 nbits (1..64) + ceil(nbits/8) LSB-first bytes.
// Interoperates directly with CeNPeerLockstepRing's uint8_t masks for <= 8 peers.
struct CeStarBitmask {
    uint16_t nbits = 0;
    uint64_t bits = 0;

    static CeStarBitmask FromMask8(uint8_t mask, uint16_t width = 4) noexcept;
    static CeStarBitmask FromMask64(uint64_t mask, uint16_t width) noexcept;
    uint8_t ToMask8() const noexcept { return static_cast<uint8_t>(bits & 0xFFu); }
    bool Test(uint16_t bit) const noexcept { return bit < nbits && ((bits >> bit) & 1ULL) != 0ULL; }
    void Set(uint16_t bit) noexcept { if (bit < nbits) bits |= (1ULL << bit); }
    void Clear(uint16_t bit) noexcept { if (bit < nbits) bits &= ~(1ULL << bit); }

    bool Encode(std::vector<uint8_t>& out) const;
    static bool Decode(const uint8_t* data, size_t size, size_t& pos, CeStarBitmask& out);
};

// Extensible big-endian inner wire frame helpers (proto u16 = 1, msg u16, len u16 + body + optional TLVs).
namespace CeStarWire {

constexpr size_t kFrameHeaderBytes = 6;

bool EncodeFrame(CeStarMsgType msg_type, const uint8_t* payload, size_t payload_len,
                 std::vector<uint8_t>& out_frame);
bool DecodeFrame(const uint8_t* data, size_t size, uint16_t& out_proto,
                 CeStarMsgType& out_msg_type, const uint8_t*& out_payload, uint16_t& out_len);
// Validates and skips zero or more trailing TLV records (tag u16, len u16, val[len]).
bool SkipTrailingTlvs(const uint8_t* data, size_t size, size_t& pos);
bool AppendTlv(std::vector<uint8_t>& out, uint16_t tag, const uint8_t* value, uint16_t len);

} // namespace CeStarWire

struct CeStarExpectedGuest {
    uint16_t seat = 0;
    uint16_t sim_slot = 0; // 1..max_guests
};

struct CeStarConfig {
    CeStarRole role = CeStarRole::Host;
    std::string bind_ipv4 = "127.0.0.1";
    uint16_t bind_port = 0;
    std::string remote_ipv4;
    uint16_t remote_port = 0;
    bool use_ice = false;
    bool require_relay = false;
    uint16_t max_guests = CE_STAR_DEFAULT_MAX_GUESTS; // 1..63 (1..3 in 0.1.14)
    uint16_t local_seat = 0;
    uint16_t local_sim_slot = 0; // 0 for Host; 1..max_guests for Guest
    std::array<uint8_t, CE_STAR_SECRET_BYTES> match_secret{};
    std::array<uint8_t, CE_STAR_DIGEST_BYTES> compatibility{};
    std::array<uint8_t, CE_STAR_DIGEST_BYTES> roster_hash{};
    std::string version = "0.1.16";
    uint32_t handshake_timeout_ms = 10000;
    uint32_t silence_timeout_ms = 15000;
    uint32_t ping_report_interval_ms = 1000;
    // Host optional explicit seat <-> sim_slot mapping. If empty, defaults to seat == sim_slot.
    std::vector<CeStarExpectedGuest> expected_guests;
};

struct CeStarPeerInfo {
    uint16_t seat = 0;
    uint16_t sim_slot = 0;
    bool connected = false;
    bool authenticated = false;
    uint32_t rtt_ms = 0;
    CeStarRoute route = CeStarRoute::Unknown;
    uint64_t last_rx_ms = 0;
};

struct CeStarPingEntry {
    uint16_t sim_slot = 0;
    uint16_t rtt_ms = 0;
    CeStarRoute route = CeStarRoute::Unknown;
};

enum class CeStarEventKind : uint8_t {
    PeerAuthenticated = 1,
    PeerDisconnected = 2,
    HostAuthenticated = 3,
    HostDisconnected = 4,
    InputGuest = 5,
    InputBundle = 6,
    Hash = 7,
    Desync = 8,
    Chat = 9,
    PingReport = 10,
    Error = 11
};

struct CeStarEvent {
    CeStarEventKind kind = CeStarEventKind::Error;
    uint16_t sim_slot = 0;
    uint16_t seat = 0;
    uint16_t ref_slot = 0;
    uint16_t diverging_slot = 0;
    uint16_t reason = 0;
    CeStarError error = CeStarError::Ok;
    uint32_t tick = 0;
    uint32_t seq = 0;
    uint64_t hash = 0;
    uint64_t expected_hash = 0;
    uint64_t actual_hash = 0;
    CeStarBitmask mask;
    std::vector<uint8_t> payload;
    std::string text;
    std::vector<CeStarPingEntry> pings;
};

class CeStarTransport {
public:
    static std::unique_ptr<CeStarTransport> Create(const CeStarConfig& config,
                                                   CeStarError* out_error = nullptr);
    ~CeStarTransport();
    CeStarTransport(const CeStarTransport&) = delete;
    CeStarTransport& operator=(const CeStarTransport&) = delete;

    void Poll(uint64_t now_ms, uint32_t event_budget = 64);
    bool PopEvent(CeStarEvent& out);

    // Application message senders (Monocypher AEAD encrypted + replay protected).
    bool SendGuestInput(uint32_t tick, const uint8_t* data, size_t size);
    bool BroadcastInputBundle(uint32_t tick, const CeStarBitmask& active_mask,
                              const uint8_t* data, size_t size);
    bool SendHash(uint32_t tick, uint64_t state_hash);
    bool BroadcastDesync(uint32_t tick, uint16_t ref_slot, uint16_t diverging_slot,
                         const CeStarBitmask& diverging_mask, uint64_t expected_hash,
                         uint64_t actual_hash);
    bool SendChat(const std::string& text);
    bool BroadcastPingReport();
    bool DisconnectGuest(uint16_t sim_slot, uint16_t reason = 0);
    void Leave(uint16_t reason = 0);
    void FailConnection(CeStarError err);

    // Inspection & live telemetry.
    CeStarRole Role() const noexcept { return config_.role; }
    uint16_t LocalPort() const noexcept { return local_port_; }
    uint16_t MaxGuests() const noexcept { return config_.max_guests; }
    uint16_t LocalSimSlot() const noexcept { return config_.local_sim_slot; }
    bool IsAuthenticated() const noexcept;
    bool IsHostLost() const noexcept { return host_lost_; }
    CeStarError LastError() const noexcept { return last_error_; }
    uint16_t ActiveGuestCount() const noexcept;
    CeStarBitmask ActiveMask() const noexcept;
    bool GetPeerInfo(uint16_t sim_slot, CeStarPeerInfo& out) const noexcept;
    uint32_t RejectedPackets() const noexcept { return rejected_packets_; }
    const std::vector<CeStarPingEntry>& LastPingReport() const noexcept { return last_ping_report_; }

#if defined(CE_NETWORK_ICE)
    // Multi-slot Host ICE: attaches one independent CeIceBridge per guest sim_slot (1..max_guests)
    // onto the Host's single ENetHost, each bound to its own ephemeral 127.0.0.1:<port_i>.
    bool AttachGuestIceBridge(uint16_t sim_slot, const juice_config_t& config, bool require_relay);
    bool GetGuestLocalIceDescription(uint16_t sim_slot, std::string& out_sdp) const;
    bool GetGuestEarlyRelayIceDescription(uint16_t sim_slot, std::string& out_sdp) const;
    bool IsGuestIceGatheringDone(uint16_t sim_slot) const;
    bool SetGuestRemoteIceDescription(uint16_t sim_slot, const std::string& remote_sdp);
    bool IsGuestIceReady(uint16_t sim_slot) const;
    bool IsGuestIceFailed(uint16_t sim_slot) const;
    CeStarRoute GuestIceRoute(uint16_t sim_slot) const;
    void DetachGuestIceBridge(uint16_t sim_slot);

    // Single-slot Guest ICE: attaches one CeIceBridge onto the Guest's ENetHost.
    bool AttachHostIceBridge(const juice_config_t& config, bool require_relay);
    bool GetLocalIceDescription(std::string& out_sdp) const;
    bool GetEarlyRelayIceDescription(std::string& out_sdp) const;
    bool IsIceGatheringDone() const;
    bool SetRemoteIceDescription(const std::string& remote_sdp);
    bool IsIceReady() const;
    bool IsIceFailed() const;
    CeStarRoute HostIceRoute() const;
    void DetachHostIceBridge();
#endif

private:
    struct ReplayWindow {
        uint32_t highest = 0;
        uint64_t low = 0;
        uint64_t high = 0;
        void Reset() noexcept { highest = 0; low = 0; high = 0; }
        bool Accept(uint32_t seq) noexcept;
    };

    static constexpr uint8_t kChannelControl = 0;
    static constexpr uint8_t kChannelInput = 1;
    static constexpr uint8_t kChannelAux = 2;
    static constexpr size_t kChannelCount = 3;

    struct ChatRateWindow {
        static constexpr size_t kMaxBurst = 6;
        static constexpr uint64_t kWindowMs = 10000;
        std::array<uint64_t, kMaxBurst> timestamps{};
        size_t count = 0;
        size_t head = 0;
        void Reset() noexcept { timestamps.fill(0); count = 0; head = 0; }
        bool Allow(uint64_t now_ms) noexcept;
    };

    struct PeerSlot {
        uint16_t expected_seat = 0;
        uint16_t sim_slot = 0;
        _ENetPeer* peer = nullptr;
        bool connected = false;
        bool authenticated = false;
        uint8_t session_id[16]{};
        uint8_t client_nonce[32]{};
        uint8_t host_nonce[32]{};
        uint8_t key[32]{};
        uint32_t send_seq[kChannelCount]{};
        ReplayWindow recv_replay[kChannelCount]{};
        ChatRateWindow chat_rate{};
        uint32_t rtt_ms = 0;
        CeStarRoute route = CeStarRoute::Unknown;
        uint64_t last_rx_ms = 0;
        uint64_t connected_at_ms = 0;
#if defined(CE_NETWORK_ICE)
        std::unique_ptr<CeIceBridge> ice;
        bool ice_failed = false;
        bool ice_require_relay = false;
#endif
        void WipeCrypto() noexcept;
        void ResetLink() noexcept;
    };

    struct PendingPeer {
        _ENetPeer* peer = nullptr;
        uint64_t connected_at_ms = 0;
        uint64_t closing_since_ms = 0;
        bool closing = false;
    };

    CeStarTransport() = default;
    bool Init(const CeStarConfig& config, CeStarError* out_error);

    bool SendEncrypted(_ENetPeer* peer, uint8_t channel, uint8_t flags, uint16_t wire_slot,
                       uint32_t seq, const uint8_t session_id[16], const uint8_t key[32],
                       const uint8_t* plaintext, size_t plaintext_len, bool reliable);
    bool SendFrameToSlot(PeerSlot& slot, uint8_t channel, CeStarMsgType msg_type,
                         const uint8_t* payload, size_t payload_len, bool reliable);
    void HandlePacket(_ENetPeer* peer, uint8_t channel, const uint8_t* data, size_t size, uint64_t now_ms);
    void HandleHostHello(_ENetPeer* peer, const uint8_t hello_nonce[16], uint16_t claimed_slot,
                         const uint8_t* body, size_t body_len, uint64_t now_ms);
    void HandleGuestAccept(const uint8_t session_id[16], const uint8_t* body, size_t body_len, uint64_t now_ms);
    void HandleAuthenticatedFrame(PeerSlot& slot, uint8_t channel, CeStarMsgType msg_type,
                                  const uint8_t* body, size_t body_len, uint64_t now_ms);
    void HandleDisconnectPeer(_ENetPeer* peer, uint16_t reason);
    void PumpIceBridges(uint64_t now_ms);
    void PushEvent(CeStarEvent&& ev);
    void RejectPacket() noexcept;
    bool SeenHelloNonce(const uint8_t nonce[16]) noexcept;
    void RecordHelloNonce(const uint8_t nonce[16]) noexcept;
    void DerivePeerKey(const uint8_t session_id[16], const uint8_t client_nonce[32],
                       const uint8_t host_nonce[32], uint16_t seat, uint16_t sim_slot,
                       uint8_t out_key[32]) const noexcept;
    void SendGuestHello(uint64_t now_ms);
    void CleanupPendingPeer(_ENetPeer* peer) noexcept;
    void ConfigurePeerTimeouts(_ENetPeer* peer) noexcept;
    static int InterceptCallback(_ENetHost* host, _ENetEvent* event);
    int IngressCheck(_ENetHost* host, uint64_t now_ms) noexcept;
#if defined(CE_NETWORK_ICE)
    bool TryRetryGuestOverRelay() noexcept;
#endif

    CeStarConfig config_;
    _ENetHost* host_ = nullptr;
    uint16_t local_port_ = 0;
    bool enet_inited_ = false;
    bool guest_connect_started_ = false;
    bool guest_hello_sent_ = false;
    bool host_lost_ = false;
    bool critical_overflow_ = false;
    bool ingress_inited_ = false;
    CeStarError last_error_ = CeStarError::Ok;
    uint32_t rejected_packets_ = 0;
    uint32_t chat_send_seq_ = 0;
    uint64_t last_ping_broadcast_ms_ = 0;
    uint64_t last_keepalive_tx_ms_ = 0;
    uint64_t created_at_ms_ = 0;
    uint64_t ingress_at_ms_ = 0;
    uint64_t packet_credit_ = 0;
    uint64_t byte_credit_ = 0;
    uint64_t packet_burst_ = 0;
    uint64_t packet_rate_ = 0;
    uint64_t byte_burst_ = 0;
    uint64_t byte_rate_ = 0;
    uint32_t incoming_metadata_limit_ = 0;

    // Host uses slots_[1..max_guests] (slot 0 unused on Host). Guest uses slots_[0] for Host link.
    std::vector<PeerSlot> slots_;
    std::vector<PendingPeer> pending_peers_;
    std::array<ChatRateWindow, CE_STAR_MAX_WIRE_SLOTS> guest_chat_rates_{};
    std::array<std::array<uint8_t, 16>, 64> seen_hello_nonces_{};
    size_t seen_hello_count_ = 0;
    size_t seen_hello_next_ = 0;

    std::deque<CeStarEvent> events_;
    std::deque<CeStarEvent> chat_events_;
    std::deque<CeStarEvent> aux_events_;
    std::vector<CeStarPingEntry> last_ping_report_;
};

#endif // CE_STAR_TRANSPORT_H
