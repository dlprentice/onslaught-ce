// SPDX-License-Identifier: GPL-3.0-only
#include "star_transport.h"

#include <enet/enet.h>
#include <monocypher.h>
#if defined(CE_NETWORK_ICE)
#include "ice_bridge.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr uint32_t kOuterMagic = 0x43455331u; // "CES1"
constexpr size_t kOuterHeaderBytes = 28;    // magic(4) + flags(1) + chan(1) + slot(2) + seq(4) + session_id(16)
constexpr size_t kAeadTagBytes = 16;
constexpr size_t kMaxWirePacketBytes = kOuterHeaderBytes + CeStarWire::kFrameHeaderBytes + CE_STAR_MAX_FRAME_PAYLOAD + kAeadTagBytes;
constexpr size_t kMaxEventQueue = 512;
constexpr size_t kMaxChatEventQueue = 64;
constexpr size_t kMaxAuxEventQueue = 8;

constexpr uint8_t kFlagHandshake = 0x01u;
constexpr uint8_t kFlagFromHost = 0x02u;

constexpr uint16_t kTlvVersion = 1;

std::atomic<unsigned> gEnetUsers{0};

void AcquireEnet(bool& ok) {
    ok = false;
    unsigned prev = gEnetUsers.fetch_add(1);
    if (prev == 0) {
        if (enet_initialize() != 0) {
            gEnetUsers.fetch_sub(1);
            return;
        }
    }
    ok = true;
}

void ReleaseEnet(bool& owned) {
    if (!owned) return;
    owned = false;
    if (gEnetUsers.fetch_sub(1) == 1) {
        enet_deinitialize();
    }
}

bool AllZero(const uint8_t* p, size_t n) noexcept {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= p[i];
    return acc == 0;
}

bool FillRandom(uint8_t* out, size_t len) noexcept {
    if (!out || len == 0) return false;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        return true;
#else
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t got = 0;
        bool ok = true;
        while (got < len) {
            const ssize_t n = read(fd, out + got, len - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = false; break; }
            got += static_cast<size_t>(n);
        }
        close(fd);
        if (ok) return true;
    }
#endif
    crypto_wipe(out, len);
    return false;
}

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void PutU64(std::vector<uint8_t>& out, uint64_t v) {
    PutU32(out, static_cast<uint32_t>(v >> 32));
    PutU32(out, static_cast<uint32_t>(v & 0xFFFFFFFFu));
}

void PutBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    if (len > 0) out.insert(out.end(), data, data + len);
}

bool GetU8(const uint8_t* data, size_t size, size_t& pos, uint8_t& out) noexcept {
    if (pos + 1 > size) return false;
    out = data[pos++];
    return true;
}

bool GetU16(const uint8_t* data, size_t size, size_t& pos, uint16_t& out) noexcept {
    if (pos + 2 > size) return false;
    out = static_cast<uint16_t>((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
    pos += 2;
    return true;
}

bool GetU32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) noexcept {
    if (pos + 4 > size) return false;
    out = (static_cast<uint32_t>(data[pos]) << 24) |
          (static_cast<uint32_t>(data[pos + 1]) << 16) |
          (static_cast<uint32_t>(data[pos + 2]) << 8) |
          static_cast<uint32_t>(data[pos + 3]);
    pos += 4;
    return true;
}

bool GetU64(const uint8_t* data, size_t size, size_t& pos, uint64_t& out) noexcept {
    uint32_t hi = 0, lo = 0;
    if (!GetU32(data, size, pos, hi) || !GetU32(data, size, pos, lo)) return false;
    out = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

bool GetBytes(const uint8_t* data, size_t size, size_t& pos, uint8_t* out, size_t len) noexcept {
    if (len > size - pos) return false;
    if (len > 0) std::memcpy(out, data + pos, len);
    pos += len;
    return true;
}

bool IsPrintableAsciiText(const std::string& text, size_t min_len, size_t max_len) noexcept {
    if (text.size() < min_len || text.size() > max_len) return false;
    for (unsigned char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

void BuildAeadNonce(uint8_t nonce[24], const uint8_t session_id[16],
                    uint8_t flags, uint8_t channel, uint16_t wire_slot, uint32_t seq) noexcept {
    std::memcpy(nonce, session_id, 16);
    nonce[16] = flags;
    nonce[17] = channel;
    nonce[18] = static_cast<uint8_t>((wire_slot >> 8) & 0xFFu);
    nonce[19] = static_cast<uint8_t>(wire_slot & 0xFFu);
    nonce[20] = static_cast<uint8_t>((seq >> 24) & 0xFFu);
    nonce[21] = static_cast<uint8_t>((seq >> 16) & 0xFFu);
    nonce[22] = static_cast<uint8_t>((seq >> 8) & 0xFFu);
    nonce[23] = static_cast<uint8_t>(seq & 0xFFu);
}

void WriteOuterHeader(uint8_t out[kOuterHeaderBytes], uint8_t flags, uint8_t channel,
                      uint16_t wire_slot, uint32_t seq, const uint8_t session_id[16]) noexcept {
    out[0] = static_cast<uint8_t>((kOuterMagic >> 24) & 0xFFu);
    out[1] = static_cast<uint8_t>((kOuterMagic >> 16) & 0xFFu);
    out[2] = static_cast<uint8_t>((kOuterMagic >> 8) & 0xFFu);
    out[3] = static_cast<uint8_t>(kOuterMagic & 0xFFu);
    out[4] = flags;
    out[5] = channel;
    out[6] = static_cast<uint8_t>((wire_slot >> 8) & 0xFFu);
    out[7] = static_cast<uint8_t>(wire_slot & 0xFFu);
    out[8] = static_cast<uint8_t>((seq >> 24) & 0xFFu);
    out[9] = static_cast<uint8_t>((seq >> 16) & 0xFFu);
    out[10] = static_cast<uint8_t>((seq >> 8) & 0xFFu);
    out[11] = static_cast<uint8_t>(seq & 0xFFu);
    std::memcpy(out + 12, session_id, 16);
}

bool ValidMsgType(uint16_t raw) noexcept {
    switch (static_cast<CeStarMsgType>(raw)) {
    case CeStarMsgType::Hello:
    case CeStarMsgType::Accept:
    case CeStarMsgType::InputGuest:
    case CeStarMsgType::InputBundle:
    case CeStarMsgType::Hash:
    case CeStarMsgType::Desync:
    case CeStarMsgType::Chat:
    case CeStarMsgType::PingReport:
    case CeStarMsgType::Keepalive:
    case CeStarMsgType::Leave:
        return true;
    default:
        return false;
    }
}

} // namespace

CeStarBitmask CeStarBitmask::FromMask8(uint8_t mask, uint16_t width) noexcept {
    CeStarBitmask bm;
    bm.nbits = std::min<uint16_t>(std::max<uint16_t>(width, 1), 8);
    const uint64_t clamp = (bm.nbits == 64) ? ~0ULL : ((1ULL << bm.nbits) - 1ULL);
    bm.bits = static_cast<uint64_t>(mask) & clamp;
    return bm;
}

CeStarBitmask CeStarBitmask::FromMask64(uint64_t mask, uint16_t width) noexcept {
    CeStarBitmask bm;
    bm.nbits = std::min<uint16_t>(std::max<uint16_t>(width, 1), CE_STAR_MAX_WIRE_SLOTS);
    const uint64_t clamp = (bm.nbits == 64) ? ~0ULL : ((1ULL << bm.nbits) - 1ULL);
    bm.bits = mask & clamp;
    return bm;
}

bool CeStarBitmask::Encode(std::vector<uint8_t>& out) const {
    if (nbits == 0 || nbits > CE_STAR_MAX_WIRE_SLOTS) return false;
    PutU16(out, nbits);
    const size_t nbytes = (static_cast<size_t>(nbits) + 7u) / 8u;
    const uint64_t clamp = (nbits == 64) ? ~0ULL : ((1ULL << nbits) - 1ULL);
    const uint64_t masked = bits & clamp;
    for (size_t i = 0; i < nbytes; ++i) {
        out.push_back(static_cast<uint8_t>((masked >> (i * 8u)) & 0xFFu));
    }
    return true;
}

bool CeStarBitmask::Decode(const uint8_t* data, size_t size, size_t& pos, CeStarBitmask& out) {
    out = CeStarBitmask{};
    uint16_t nbits = 0;
    if (!GetU16(data, size, pos, nbits) || nbits == 0 || nbits > CE_STAR_MAX_WIRE_SLOTS)
        return false;
    const size_t nbytes = (static_cast<size_t>(nbits) + 7u) / 8u;
    if (nbytes > size - pos) return false;
    uint64_t bits = 0;
    for (size_t i = 0; i < nbytes; ++i) {
        bits |= static_cast<uint64_t>(data[pos + i]) << (i * 8u);
    }
    pos += nbytes;
    const uint64_t clamp = (nbits == 64) ? ~0ULL : ((1ULL << nbits) - 1ULL);
    if ((bits & ~clamp) != 0ULL) return false; // unused padding bits must be zero
    out.nbits = nbits;
    out.bits = bits;
    return true;
}

namespace CeStarWire {

bool EncodeFrame(CeStarMsgType msg_type, const uint8_t* payload, size_t payload_len,
                 std::vector<uint8_t>& out_frame) {
    out_frame.clear();
    if (payload_len > CE_STAR_MAX_FRAME_PAYLOAD || (!payload && payload_len > 0)) return false;
    out_frame.reserve(kFrameHeaderBytes + payload_len);
    PutU16(out_frame, CE_STAR_PROTO_VERSION);
    PutU16(out_frame, static_cast<uint16_t>(msg_type));
    PutU16(out_frame, static_cast<uint16_t>(payload_len));
    PutBytes(out_frame, payload, payload_len);
    return true;
}

bool DecodeFrame(const uint8_t* data, size_t size, uint16_t& out_proto,
                 CeStarMsgType& out_msg_type, const uint8_t*& out_payload, uint16_t& out_len) {
    if (!data || size < kFrameHeaderBytes) return false;
    size_t pos = 0;
    uint16_t proto = 0, raw_msg = 0, len = 0;
    if (!GetU16(data, size, pos, proto) || !GetU16(data, size, pos, raw_msg) ||
        !GetU16(data, size, pos, len))
        return false;
    if (proto != CE_STAR_PROTO_VERSION || !ValidMsgType(raw_msg)) return false;
    if (static_cast<size_t>(len) != size - kFrameHeaderBytes || len > CE_STAR_MAX_FRAME_PAYLOAD)
        return false;
    out_proto = proto;
    out_msg_type = static_cast<CeStarMsgType>(raw_msg);
    out_payload = data + kFrameHeaderBytes;
    out_len = len;
    return true;
}

bool SkipTrailingTlvs(const uint8_t* data, size_t size, size_t& pos) {
    while (pos < size) {
        uint16_t tag = 0, len = 0;
        if (!GetU16(data, size, pos, tag) || !GetU16(data, size, pos, len)) return false;
        (void)tag;
        if (len > size - pos) return false;
        pos += len;
    }
    return pos == size;
}

bool AppendTlv(std::vector<uint8_t>& out, uint16_t tag, const uint8_t* value, uint16_t len) {
    if (!value && len > 0) return false;
    PutU16(out, tag);
    PutU16(out, len);
    PutBytes(out, value, len);
    return true;
}

} // namespace CeStarWire

bool CeStarTransport::ReplayWindow::Accept(uint32_t seq) noexcept {
    if (seq == 0) return false;
    if (seq > highest) {
        const uint32_t diff = seq - highest;
        if (diff >= 128u) {
            low = 1ULL;
            high = 0ULL;
        } else if (diff >= 64u) {
            high = low << (diff - 64u);
            low = 1ULL;
        } else {
            high = (high << diff) | (low >> (64u - diff));
            low = (low << diff) | 1ULL;
        }
        highest = seq;
        return true;
    }
    const uint32_t diff = highest - seq;
    if (diff >= 128u) return false;
    const uint64_t bit = 1ULL << (diff & 63u);
    uint64_t& word = (diff < 64u) ? low : high;
    if ((word & bit) != 0ULL) return false;
    word |= bit;
    return true;
}

bool CeStarTransport::ChatRateWindow::Allow(uint64_t now_ms) noexcept {
    while (count > 0) {
        const uint64_t oldest = timestamps[head];
        if (now_ms >= oldest && now_ms - oldest >= kWindowMs) {
            head = (head + 1u) % kMaxBurst;
            --count;
        } else {
            break;
        }
    }
    if (count >= kMaxBurst) return false;
    const size_t idx = (head + count) % kMaxBurst;
    timestamps[idx] = now_ms;
    ++count;
    return true;
}

void CeStarTransport::PeerSlot::WipeCrypto() noexcept {
    crypto_wipe(session_id, sizeof(session_id));
    crypto_wipe(client_nonce, sizeof(client_nonce));
    crypto_wipe(host_nonce, sizeof(host_nonce));
    crypto_wipe(key, sizeof(key));
}

void CeStarTransport::PeerSlot::ResetLink() noexcept {
    peer = nullptr;
    connected = false;
    authenticated = false;
    WipeCrypto();
    for (size_t c = 0; c < kChannelCount; ++c) {
        send_seq[c] = 0;
        recv_replay[c].Reset();
    }
    chat_rate.Reset();
    rtt_ms = 0;
    route = CeStarRoute::Unknown;
    last_rx_ms = 0;
    connected_at_ms = 0;
}

std::unique_ptr<CeStarTransport> CeStarTransport::Create(const CeStarConfig& config,
                                                         CeStarError* out_error) {
    std::unique_ptr<CeStarTransport> self(new CeStarTransport());
    if (!self->Init(config, out_error)) return nullptr;
    return self;
}

CeStarTransport::~CeStarTransport() {
    // Destroy all CeIceBridge instances BEFORE destroying the ENetHost!
#if defined(CE_NETWORK_ICE)
    for (auto& slot : slots_) slot.ice.reset();
#endif
    for (auto& slot : slots_) slot.WipeCrypto();
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    crypto_wipe(config_.match_secret.data(), config_.match_secret.size());
    ReleaseEnet(enet_inited_);
}

bool CeStarTransport::Init(const CeStarConfig& config, CeStarError* out_error) {
    auto fail = [&](CeStarError err) {
        last_error_ = err;
        if (out_error) *out_error = err;
        return false;
    };

    if ((config.role != CeStarRole::Host && config.role != CeStarRole::Guest) ||
        config.max_guests < 1 || config.max_guests >= CE_STAR_MAX_WIRE_SLOTS ||
        AllZero(config.match_secret.data(), config.match_secret.size()) ||
        config.handshake_timeout_ms < 200 || config.handshake_timeout_ms > 180000 ||
        config.silence_timeout_ms < 500 || config.silence_timeout_ms > 180000)
        return fail(CeStarError::BadConfig);

    if (config.role == CeStarRole::Host) {
        if (config.local_sim_slot != 0) return fail(CeStarError::BadConfig);
        if (config.bind_ipv4.empty()) return fail(CeStarError::BadConfig);
    } else {
        if (config.local_sim_slot < 1 || config.local_sim_slot > config.max_guests)
            return fail(CeStarError::BadConfig);
        if (!config.use_ice && (config.remote_ipv4.empty() || config.remote_port == 0))
            return fail(CeStarError::BadConfig);
    }

    config_ = config;
    AcquireEnet(enet_inited_);
    if (!enet_inited_) return fail(CeStarError::Io);

    if (config_.role == CeStarRole::Host) {
        ENetAddress bind_addr{};
        if (enet_address_set_host_ip(&bind_addr, config_.bind_ipv4.c_str()) != 0)
            return fail(CeStarError::BadConfig);
        bind_addr.port = config_.bind_port;
        host_ = enet_host_create(&bind_addr, config_.max_guests, kChannelCount, 0, 0);
        if (!host_) return fail(CeStarError::Io);
        ENetAddress actual{};
        if (enet_socket_get_address(host_->socket, &actual) != 0 || actual.port == 0)
            return fail(CeStarError::Io);
        local_port_ = actual.port;

        slots_.resize(static_cast<size_t>(config_.max_guests) + 1u);
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            slots_[s].sim_slot = s;
            slots_[s].expected_seat = s;
        }
        for (const auto& eg : config_.expected_guests) {
            if (eg.sim_slot < 1 || eg.sim_slot > config_.max_guests)
                return fail(CeStarError::BadConfig);
            slots_[eg.sim_slot].expected_seat = eg.seat;
        }
    } else {
        if (config_.use_ice) {
            ENetAddress bind_addr{};
            if (enet_address_set_host_ip(&bind_addr, "127.0.0.1") != 0)
                return fail(CeStarError::BadConfig);
            bind_addr.port = 0;
            host_ = enet_host_create(&bind_addr, 1, kChannelCount, 0, 0);
            if (!host_) return fail(CeStarError::Io);
            ENetAddress actual{};
            if (enet_socket_get_address(host_->socket, &actual) != 0 || actual.port == 0)
                return fail(CeStarError::Io);
            local_port_ = actual.port;
        } else {
            host_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
            if (!host_) return fail(CeStarError::Io);
        }

        slots_.resize(1);
        slots_[0].sim_slot = 0;
        slots_[0].expected_seat = 0;

        if (!config_.use_ice) {
            ENetAddress remote{};
            if (enet_address_set_host_ip(&remote, config_.remote_ipv4.c_str()) != 0)
                return fail(CeStarError::BadConfig);
            remote.port = config_.remote_port;
            ENetPeer* p = enet_host_connect(host_, &remote, kChannelCount, 0);
            if (!p) return fail(CeStarError::Io);
            enet_peer_ping_interval(p, 500);
            slots_[0].peer = p;
            slots_[0].route = CeStarRoute::Direct;
            guest_connect_started_ = true;
        }
    }

    if (out_error) *out_error = CeStarError::Ok;
    return true;
}

void CeStarTransport::RejectPacket() noexcept {
    if (rejected_packets_ != std::numeric_limits<uint32_t>::max()) ++rejected_packets_;
}

void CeStarTransport::PushEvent(CeStarEvent&& ev) {
    if (ev.kind == CeStarEventKind::Chat) {
        if (chat_events_.size() >= kMaxChatEventQueue) chat_events_.pop_front();
        chat_events_.push_back(std::move(ev));
        return;
    }
    if (ev.kind == CeStarEventKind::PingReport) {
        if (aux_events_.size() >= kMaxAuxEventQueue) aux_events_.pop_front();
        aux_events_.push_back(std::move(ev));
        return;
    }
    if (critical_overflow_) return;
    if (events_.size() >= kMaxEventQueue) {
        critical_overflow_ = true;
        last_error_ = CeStarError::Capacity;
        if (config_.role == CeStarRole::Guest) {
            host_lost_ = true;
            if (!slots_.empty() && slots_[0].peer) {
                _ENetPeer* p = slots_[0].peer;
                slots_[0].ResetLink();
                enet_peer_reset(p);
            }
        } else {
            for (uint16_t s = 1; s <= config_.max_guests && s < slots_.size(); ++s) {
                if (slots_[s].peer) {
                    _ENetPeer* p = slots_[s].peer;
                    p->data = nullptr;
                    slots_[s].ResetLink();
                    enet_peer_reset(p);
                }
            }
        }
        CeStarEvent fault;
        fault.kind = CeStarEventKind::Error;
        fault.error = CeStarError::Capacity;
        events_.push_back(std::move(fault));
        return;
    }
    events_.push_back(std::move(ev));
}

bool CeStarTransport::PopEvent(CeStarEvent& out) {
    if (!events_.empty()) {
        out = std::move(events_.front());
        events_.pop_front();
        return true;
    }
    if (!chat_events_.empty()) {
        out = std::move(chat_events_.front());
        chat_events_.pop_front();
        return true;
    }
    if (!aux_events_.empty()) {
        out = std::move(aux_events_.front());
        aux_events_.pop_front();
        return true;
    }
    return false;
}

bool CeStarTransport::SeenHelloNonce(const uint8_t nonce[16]) noexcept {
    for (size_t i = 0; i < seen_hello_count_; ++i) {
        if (crypto_verify16(seen_hello_nonces_[i].data(), nonce) == 0) return true;
    }
    return false;
}

void CeStarTransport::RecordHelloNonce(const uint8_t nonce[16]) noexcept {
    std::memcpy(seen_hello_nonces_[seen_hello_next_].data(), nonce, 16);
    seen_hello_next_ = (seen_hello_next_ + 1u) % seen_hello_nonces_.size();
    if (seen_hello_count_ < seen_hello_nonces_.size()) ++seen_hello_count_;
}

void CeStarTransport::DerivePeerKey(const uint8_t session_id[16], const uint8_t client_nonce[32],
                                    const uint8_t host_nonce[32], uint16_t seat, uint16_t sim_slot,
                                    uint8_t out_key[32]) const noexcept {
    static constexpr char kDomain[] = "Onslaught CE star traffic v1";
    std::array<uint8_t, sizeof(kDomain) + 16 + 32 + 32 + 32 + 4> transcript{};
    size_t pos = 0;
    std::memcpy(transcript.data() + pos, kDomain, sizeof(kDomain)); pos += sizeof(kDomain);
    std::memcpy(transcript.data() + pos, session_id, 16); pos += 16;
    std::memcpy(transcript.data() + pos, client_nonce, 32); pos += 32;
    std::memcpy(transcript.data() + pos, host_nonce, 32); pos += 32;
    std::memcpy(transcript.data() + pos, config_.compatibility.data(), 32); pos += 32;
    transcript[pos++] = static_cast<uint8_t>((seat >> 8) & 0xFFu);
    transcript[pos++] = static_cast<uint8_t>(seat & 0xFFu);
    transcript[pos++] = static_cast<uint8_t>((sim_slot >> 8) & 0xFFu);
    transcript[pos++] = static_cast<uint8_t>(sim_slot & 0xFFu);
    crypto_blake2b_keyed(out_key, 32, config_.match_secret.data(), 32, transcript.data(), pos);
    crypto_wipe(transcript.data(), transcript.size());
}

bool CeStarTransport::SendEncrypted(_ENetPeer* peer, uint8_t channel, uint8_t flags,
                                    uint16_t wire_slot, uint32_t seq, const uint8_t session_id[16],
                                    const uint8_t key[32], const uint8_t* plaintext,
                                    size_t plaintext_len, bool reliable) {
    if (!peer || channel >= kChannelCount || plaintext_len < CeStarWire::kFrameHeaderBytes ||
        plaintext_len > CeStarWire::kFrameHeaderBytes + CE_STAR_MAX_FRAME_PAYLOAD)
        return false;
    std::vector<uint8_t> wire(kOuterHeaderBytes + plaintext_len + kAeadTagBytes);
    WriteOuterHeader(wire.data(), flags, channel, wire_slot, seq, session_id);
    uint8_t nonce[24];
    BuildAeadNonce(nonce, session_id, flags, channel, wire_slot, seq);
    crypto_aead_lock(wire.data() + kOuterHeaderBytes,
                     wire.data() + kOuterHeaderBytes + plaintext_len,
                     key, nonce, wire.data(), kOuterHeaderBytes, plaintext, plaintext_len);
    crypto_wipe(nonce, sizeof(nonce));
    const enet_uint32 enet_flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* pkt = enet_packet_create(wire.data(), wire.size(), enet_flags);
    if (!pkt) return false;
    if (enet_peer_send(peer, channel, pkt) != 0) {
        enet_packet_destroy(pkt);
        return false;
    }
    return true;
}

bool CeStarTransport::SendFrameToSlot(PeerSlot& slot, uint8_t channel, CeStarMsgType msg_type,
                                      const uint8_t* payload, size_t payload_len, bool reliable) {
    if (!slot.peer || !slot.authenticated || channel >= kChannelCount) return false;
    if (slot.send_seq[channel] == std::numeric_limits<uint32_t>::max()) return false;
    std::vector<uint8_t> frame;
    if (!CeStarWire::EncodeFrame(msg_type, payload, payload_len, frame)) return false;
    const uint32_t seq = ++slot.send_seq[channel];
    const uint8_t flags = (config_.role == CeStarRole::Host) ? kFlagFromHost : 0u;
    const uint16_t wire_slot = (config_.role == CeStarRole::Host) ? 0u : config_.local_sim_slot;
    return SendEncrypted(slot.peer, channel, flags, wire_slot, seq, slot.session_id, slot.key,
                         frame.data(), frame.size(), reliable);
}

void CeStarTransport::SendGuestHello(uint64_t now_ms) {
    if (config_.role != CeStarRole::Guest || guest_hello_sent_ || !slots_[0].peer) return;
    uint8_t hello_nonce[16]{};
    if (!FillRandom(hello_nonce, sizeof(hello_nonce)) ||
        !FillRandom(slots_[0].client_nonce, sizeof(slots_[0].client_nonce))) {
        last_error_ = CeStarError::Io;
        return;
    }
    std::vector<uint8_t> body;
    body.reserve(128);
    PutBytes(body, slots_[0].client_nonce, 32);
    PutBytes(body, config_.compatibility.data(), 32);
    PutBytes(body, config_.roster_hash.data(), 32);
    PutU16(body, config_.local_seat);
    PutU16(body, config_.local_sim_slot);
    if (!config_.version.empty() && config_.version.size() <= 64) {
        CeStarWire::AppendTlv(body, kTlvVersion,
                              reinterpret_cast<const uint8_t*>(config_.version.data()),
                              static_cast<uint16_t>(config_.version.size()));
    }
    std::vector<uint8_t> frame;
    if (!CeStarWire::EncodeFrame(CeStarMsgType::Hello, body.data(), body.size(), frame)) return;
    if (SendEncrypted(slots_[0].peer, kChannelControl, kFlagHandshake, config_.local_sim_slot, 1u,
                      hello_nonce, config_.match_secret.data(), frame.data(), frame.size(), true)) {
        guest_hello_sent_ = true;
        slots_[0].connected_at_ms = now_ms ? now_ms : 1u;
        enet_host_flush(host_);
    }
}

void CeStarTransport::CleanupPendingPeer(_ENetPeer* peer) noexcept {
    pending_peers_.erase(
        std::remove_if(pending_peers_.begin(), pending_peers_.end(),
                       [peer](const PendingPeer& p) { return p.peer == peer; }),
        pending_peers_.end());
}

void CeStarTransport::HandleHostHello(_ENetPeer* peer, const uint8_t hello_nonce[16],
                                      uint16_t claimed_slot, const uint8_t* body,
                                      size_t body_len, uint64_t now_ms) {
    if (SeenHelloNonce(hello_nonce)) { RejectPacket(); return; }
    if (body_len < 32 + 32 + 32 + 4) { RejectPacket(); return; }

    size_t pos = 0;
    uint8_t client_nonce[32]{}, compat[32]{}, roster[32]{};
    uint16_t seat = 0, sim_slot = 0;
    if (!GetBytes(body, body_len, pos, client_nonce, 32) ||
        !GetBytes(body, body_len, pos, compat, 32) ||
        !GetBytes(body, body_len, pos, roster, 32) ||
        !GetU16(body, body_len, pos, seat) ||
        !GetU16(body, body_len, pos, sim_slot) ||
        !CeStarWire::SkipTrailingTlvs(body, body_len, pos)) {
        RejectPacket();
        return;
    }
    if (sim_slot != claimed_slot || sim_slot < 1 || sim_slot > config_.max_guests) {
        RejectPacket();
        return;
    }

    PeerSlot& slot = slots_[sim_slot];
    if (slot.authenticated) {
        RejectPacket();
        return;
    }

#if defined(CE_NETWORK_ICE)
    if (config_.use_ice) {
        if (!slot.ice) { RejectPacket(); return; }
        const ENetAddress& expected_addr = slot.ice->LocalAddress();
        if (peer->address.host != expected_addr.host || peer->address.port != expected_addr.port) {
            RejectPacket();
            return;
        }
    }
#endif

    auto send_reject = [&](uint8_t status_code) {
        uint8_t resp_sid[16]{};
        if (!FillRandom(resp_sid, sizeof(resp_sid))) return;
        std::vector<uint8_t> reply;
        reply.reserve(112);
        reply.push_back(status_code);
        reply.push_back(0u);
        PutU16(reply, seat);
        PutU16(reply, sim_slot);
        PutBytes(reply, client_nonce, 32);
        uint8_t zero32[32]{};
        PutBytes(reply, zero32, 32);
        PutBytes(reply, config_.compatibility.data(), 32);
        CeStarBitmask::FromMask64(1ULL, static_cast<uint16_t>(config_.max_guests + 1u)).Encode(reply);
        std::vector<uint8_t> frame;
        if (CeStarWire::EncodeFrame(CeStarMsgType::Accept, reply.data(), reply.size(), frame)) {
            SendEncrypted(peer, kChannelControl, kFlagHandshake | kFlagFromHost, 0u, 1u,
                          resp_sid, config_.match_secret.data(), frame.data(), frame.size(), true);
            enet_host_flush(host_);
        }
        enet_peer_disconnect_later(peer, 0);
        for (auto& pp : pending_peers_) {
            if (pp.peer == peer) {
                pp.closing = true;
                pp.closing_since_ms = now_ms;
            }
        }
    };

    RecordHelloNonce(hello_nonce);

    if (seat != slot.expected_seat) {
        RejectPacket();
        send_reject(2u); // slot rejected
        return;
    }
    const bool check_roster = !AllZero(config_.roster_hash.data(), config_.roster_hash.size());
    if (crypto_verify32(compat, config_.compatibility.data()) != 0 ||
        (check_roster && crypto_verify32(roster, config_.roster_hash.data()) != 0)) {
        RejectPacket();
        send_reject(1u); // incompatible
        return;
    }

    uint8_t sid[16]{}, hnonce[32]{};
    if (!FillRandom(sid, sizeof(sid)) || !FillRandom(hnonce, sizeof(hnonce))) {
        RejectPacket();
        return;
    }

    slot.ResetLink();
    slot.peer = peer;
    slot.connected = true;
    slot.authenticated = true;
    std::memcpy(slot.session_id, sid, 16);
    std::memcpy(slot.client_nonce, client_nonce, 32);
    std::memcpy(slot.host_nonce, hnonce, 32);
    DerivePeerKey(slot.session_id, slot.client_nonce, slot.host_nonce, seat, sim_slot, slot.key);
    slot.last_rx_ms = now_ms;
    slot.connected_at_ms = now_ms;
    slot.rtt_ms = peer->roundTripTime;
    slot.route = CeStarRoute::Direct;
#if defined(CE_NETWORK_ICE)
    if (config_.use_ice && slot.ice) {
        slot.route = slot.ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
    }
#endif
    peer->data = &slot;
    enet_peer_ping_interval(peer, 500);
    CleanupPendingPeer(peer);

    std::vector<uint8_t> accept;
    accept.reserve(128);
    accept.push_back(0u); // status OK
    accept.push_back(0u);
    PutU16(accept, seat);
    PutU16(accept, sim_slot);
    PutBytes(accept, client_nonce, 32);
    PutBytes(accept, hnonce, 32);
    PutBytes(accept, config_.compatibility.data(), 32);
    ActiveMask().Encode(accept);
    std::vector<uint8_t> frame;
    if (CeStarWire::EncodeFrame(CeStarMsgType::Accept, accept.data(), accept.size(), frame)) {
        SendEncrypted(peer, kChannelControl, kFlagHandshake | kFlagFromHost, 0u, 1u,
                      slot.session_id, config_.match_secret.data(), frame.data(), frame.size(), true);
        enet_host_flush(host_);
    }

    CeStarEvent ev;
    ev.kind = CeStarEventKind::PeerAuthenticated;
    ev.sim_slot = sim_slot;
    ev.seat = seat;
    PushEvent(std::move(ev));
}

void CeStarTransport::HandleGuestAccept(const uint8_t session_id[16], const uint8_t* body,
                                        size_t body_len, uint64_t now_ms) {
    if (slots_[0].authenticated) { RejectPacket(); return; }
    size_t pos = 0;
    uint8_t status = 0, reserved = 0;
    uint16_t seat = 0, sim_slot = 0;
    uint8_t echo_cnonce[32]{}, hnonce[32]{}, host_compat[32]{};
    CeStarBitmask mask;
    if (!GetU8(body, body_len, pos, status) ||
        !GetU8(body, body_len, pos, reserved) || reserved != 0u ||
        !GetU16(body, body_len, pos, seat) ||
        !GetU16(body, body_len, pos, sim_slot) ||
        !GetBytes(body, body_len, pos, echo_cnonce, 32) ||
        !GetBytes(body, body_len, pos, hnonce, 32) ||
        !GetBytes(body, body_len, pos, host_compat, 32) ||
        !CeStarBitmask::Decode(body, body_len, pos, mask) ||
        !CeStarWire::SkipTrailingTlvs(body, body_len, pos)) {
        RejectPacket();
        return;
    }
    if (crypto_verify32(echo_cnonce, slots_[0].client_nonce) != 0 ||
        seat != config_.local_seat || sim_slot != config_.local_sim_slot) {
        RejectPacket();
        return;
    }
    if (status != 0u) {
        const CeStarError err = (status == 1u) ? CeStarError::Incompatible : CeStarError::SlotRejected;
        last_error_ = err;
        host_lost_ = true;
        if (slots_[0].peer) enet_peer_disconnect_now(slots_[0].peer, 0);
        slots_[0].ResetLink();
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Error;
        ev.error = err;
        PushEvent(std::move(ev));
        return;
    }
    if (crypto_verify32(host_compat, config_.compatibility.data()) != 0 ||
        AllZero(session_id, 16)) {
        last_error_ = CeStarError::Incompatible;
        host_lost_ = true;
        if (slots_[0].peer) enet_peer_reset(slots_[0].peer);
        slots_[0].ResetLink();
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Error;
        ev.error = CeStarError::Incompatible;
        PushEvent(std::move(ev));
        return;
    }

    std::memcpy(slots_[0].session_id, session_id, 16);
    std::memcpy(slots_[0].host_nonce, hnonce, 32);
    DerivePeerKey(slots_[0].session_id, slots_[0].client_nonce, slots_[0].host_nonce,
                  config_.local_seat, config_.local_sim_slot, slots_[0].key);
    slots_[0].authenticated = true;
    slots_[0].last_rx_ms = now_ms;
    slots_[0].rtt_ms = slots_[0].peer ? slots_[0].peer->roundTripTime : 0u;
    slots_[0].route = CeStarRoute::Direct;
#if defined(CE_NETWORK_ICE)
    if (config_.use_ice && slots_[0].ice) {
        slots_[0].route = slots_[0].ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
    }
#endif

    CeStarEvent ev;
    ev.kind = CeStarEventKind::HostAuthenticated;
    ev.sim_slot = config_.local_sim_slot;
    ev.seat = config_.local_seat;
    ev.mask = mask;
    PushEvent(std::move(ev));
}

void CeStarTransport::HandleAuthenticatedFrame(PeerSlot& slot, uint8_t channel,
                                               CeStarMsgType msg_type, const uint8_t* body,
                                               size_t body_len, uint64_t now_ms) {
    slot.last_rx_ms = now_ms;
    size_t pos = 0;

    switch (msg_type) {
    case CeStarMsgType::InputGuest: {
        if (config_.role != CeStarRole::Host || channel != kChannelInput) { RejectPacket(); return; }
        uint16_t claimed_slot = 0, len = 0;
        uint32_t tick = 0;
        if (!GetU16(body, body_len, pos, claimed_slot) || claimed_slot != slot.sim_slot ||
            !GetU32(body, body_len, pos, tick) ||
            !GetU16(body, body_len, pos, len) || len > body_len - pos) {
            RejectPacket();
            return;
        }
        std::vector<uint8_t> data(body + pos, body + pos + len);
        pos += len;
        if (!CeStarWire::SkipTrailingTlvs(body, body_len, pos)) { RejectPacket(); return; }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::InputGuest;
        ev.sim_slot = slot.sim_slot;
        ev.seat = slot.expected_seat;
        ev.tick = tick;
        ev.payload = std::move(data);
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::InputBundle: {
        if (config_.role != CeStarRole::Guest || channel != kChannelInput) { RejectPacket(); return; }
        uint32_t tick = 0;
        CeStarBitmask mask;
        uint16_t len = 0;
        if (!GetU32(body, body_len, pos, tick) ||
            !CeStarBitmask::Decode(body, body_len, pos, mask) ||
            !GetU16(body, body_len, pos, len) || len > body_len - pos) {
            RejectPacket();
            return;
        }
        std::vector<uint8_t> data(body + pos, body + pos + len);
        pos += len;
        if (!CeStarWire::SkipTrailingTlvs(body, body_len, pos)) { RejectPacket(); return; }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::InputBundle;
        ev.sim_slot = 0;
        ev.tick = tick;
        ev.mask = mask;
        ev.payload = std::move(data);
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::Hash: {
        if (channel != kChannelControl) { RejectPacket(); return; }
        uint16_t from_slot = 0;
        uint32_t tick = 0;
        uint64_t h = 0;
        if (!GetU16(body, body_len, pos, from_slot) ||
            !GetU32(body, body_len, pos, tick) ||
            !GetU64(body, body_len, pos, h) ||
            !CeStarWire::SkipTrailingTlvs(body, body_len, pos)) {
            RejectPacket();
            return;
        }
        if (config_.role == CeStarRole::Host && from_slot != slot.sim_slot) {
            RejectPacket();
            return;
        }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Hash;
        ev.sim_slot = (config_.role == CeStarRole::Host) ? slot.sim_slot : from_slot;
        ev.tick = tick;
        ev.hash = h;
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::Desync: {
        if (config_.role != CeStarRole::Guest || channel != kChannelControl) { RejectPacket(); return; }
        uint32_t tick = 0;
        uint16_t ref_slot = 0, div_slot = 0;
        CeStarBitmask div_mask;
        uint64_t expected_h = 0, actual_h = 0;
        if (!GetU32(body, body_len, pos, tick) ||
            !GetU16(body, body_len, pos, ref_slot) ||
            !GetU16(body, body_len, pos, div_slot) ||
            !CeStarBitmask::Decode(body, body_len, pos, div_mask) ||
            !GetU64(body, body_len, pos, expected_h) ||
            !GetU64(body, body_len, pos, actual_h) ||
            !CeStarWire::SkipTrailingTlvs(body, body_len, pos)) {
            RejectPacket();
            return;
        }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Desync;
        ev.tick = tick;
        ev.ref_slot = ref_slot;
        ev.diverging_slot = div_slot;
        ev.mask = div_mask;
        ev.expected_hash = expected_h;
        ev.actual_hash = actual_h;
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::Chat: {
        if (channel != kChannelAux) { RejectPacket(); return; }
        uint16_t from_seat = 0, from_slot = 0, text_len = 0;
        uint32_t seq = 0;
        if (!GetU16(body, body_len, pos, from_seat) ||
            !GetU16(body, body_len, pos, from_slot) ||
            !GetU32(body, body_len, pos, seq) ||
            !GetU16(body, body_len, pos, text_len) ||
            text_len == 0 || text_len > CE_STAR_MAX_CHAT_BYTES || text_len > body_len - pos) {
            RejectPacket();
            return;
        }
        std::string text(reinterpret_cast<const char*>(body + pos), text_len);
        pos += text_len;
        if (!CeStarWire::SkipTrailingTlvs(body, body_len, pos) ||
            !IsPrintableAsciiText(text, 1, CE_STAR_MAX_CHAT_BYTES)) {
            RejectPacket();
            return;
        }
        if (config_.role == CeStarRole::Host) {
            if (!slot.chat_rate.Allow(now_ms)) {
                RejectPacket();
                return;
            }
            // Authoritatively stamp the authenticated guest's seat/sim_slot and relay to other guests.
            from_seat = slot.expected_seat;
            from_slot = slot.sim_slot;
            std::vector<uint8_t> relay;
            relay.reserve(16 + text.size());
            PutU16(relay, from_seat);
            PutU16(relay, from_slot);
            PutU32(relay, seq);
            PutU16(relay, static_cast<uint16_t>(text.size()));
            PutBytes(relay, reinterpret_cast<const uint8_t*>(text.data()), text.size());
            for (uint16_t s = 1; s <= config_.max_guests; ++s) {
                if (s != slot.sim_slot && slots_[s].authenticated) {
                    SendFrameToSlot(slots_[s], kChannelAux, CeStarMsgType::Chat,
                                    relay.data(), relay.size(), true);
                }
            }
        } else {
            if (from_slot > config_.max_guests || !guest_chat_rates_[from_slot].Allow(now_ms)) {
                RejectPacket();
                return;
            }
        }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Chat;
        ev.seat = from_seat;
        ev.sim_slot = from_slot;
        ev.seq = seq;
        ev.text = std::move(text);
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::PingReport: {
        if (config_.role != CeStarRole::Guest || channel != kChannelAux) { RejectPacket(); return; }
        uint16_t count = 0;
        if (!GetU16(body, body_len, pos, count) || count > CE_STAR_MAX_WIRE_SLOTS) {
            RejectPacket();
            return;
        }
        std::vector<CeStarPingEntry> entries;
        entries.reserve(count);
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t s = 0, rtt = 0;
            uint8_t rt = 0, rsv = 0;
            if (!GetU16(body, body_len, pos, s) || !GetU16(body, body_len, pos, rtt) ||
                !GetU8(body, body_len, pos, rt) || !GetU8(body, body_len, pos, rsv) ||
                rt > 2u || rsv != 0u) {
                RejectPacket();
                return;
            }
            entries.push_back(CeStarPingEntry{s, rtt, static_cast<CeStarRoute>(rt)});
        }
        if (!CeStarWire::SkipTrailingTlvs(body, body_len, pos)) { RejectPacket(); return; }
        last_ping_report_ = entries;
        CeStarEvent ev;
        ev.kind = CeStarEventKind::PingReport;
        ev.pings = std::move(entries);
        PushEvent(std::move(ev));
        return;
    }

    case CeStarMsgType::Keepalive: {
        if (channel != kChannelAux) { RejectPacket(); return; }
        if (!CeStarWire::SkipTrailingTlvs(body, body_len, pos)) { RejectPacket(); return; }
        return;
    }

    case CeStarMsgType::Leave: {
        if (channel != kChannelControl) { RejectPacket(); return; }
        uint16_t leaving_slot = 0, reason = 0;
        if (!GetU16(body, body_len, pos, leaving_slot) ||
            !GetU16(body, body_len, pos, reason) ||
            !CeStarWire::SkipTrailingTlvs(body, body_len, pos)) {
            RejectPacket();
            return;
        }
        if (config_.role == CeStarRole::Host) {
            if (leaving_slot != slot.sim_slot) { RejectPacket(); return; }
            _ENetPeer* p = slot.peer;
            HandleDisconnectPeer(p, reason);
            if (p) enet_peer_reset(p);
        } else {
            _ENetPeer* p = slots_[0].peer;
            HandleDisconnectPeer(p, reason);
            if (p) enet_peer_reset(p);
        }
        return;
    }

    case CeStarMsgType::Hello:
    case CeStarMsgType::Accept:
    default:
        RejectPacket();
        return;
    }
}

void CeStarTransport::HandlePacket(_ENetPeer* peer, uint8_t channel, const uint8_t* data,
                                   size_t size, uint64_t now_ms) {
    if (!peer || !data || channel >= kChannelCount ||
        size < kOuterHeaderBytes + CeStarWire::kFrameHeaderBytes + kAeadTagBytes ||
        size > kMaxWirePacketBytes) {
        RejectPacket();
        return;
    }
    size_t hpos = 0;
    uint32_t magic = 0, seq = 0;
    uint8_t flags = 0, hdr_chan = 0;
    uint16_t wire_slot = 0;
    uint8_t session_id[16]{};
    if (!GetU32(data, size, hpos, magic) || magic != kOuterMagic ||
        !GetU8(data, size, hpos, flags) ||
        !GetU8(data, size, hpos, hdr_chan) || hdr_chan != channel ||
        !GetU16(data, size, hpos, wire_slot) ||
        !GetU32(data, size, hpos, seq) || seq == 0 ||
        !GetBytes(data, size, hpos, session_id, 16)) {
        RejectPacket();
        return;
    }
    if ((flags & ~(kFlagHandshake | kFlagFromHost)) != 0u) {
        RejectPacket();
        return;
    }
    const bool from_host = (flags & kFlagFromHost) != 0u;
    const bool is_handshake = (flags & kFlagHandshake) != 0u;
    // Direction check prevents packet reflection.
    if (config_.role == CeStarRole::Host && from_host) { RejectPacket(); return; }
    if (config_.role == CeStarRole::Guest && !from_host) { RejectPacket(); return; }

    const size_t pt_len = size - kOuterHeaderBytes - kAeadTagBytes;
    std::vector<uint8_t> plain(pt_len);
    uint8_t nonce[24];
    BuildAeadNonce(nonce, session_id, flags, channel, wire_slot, seq);

    if (is_handshake) {
        if (channel != kChannelControl || seq != 1u) {
            crypto_wipe(nonce, sizeof(nonce));
            RejectPacket();
            return;
        }
        if (crypto_aead_unlock(plain.data(), data + kOuterHeaderBytes + pt_len,
                               config_.match_secret.data(), nonce,
                               data, kOuterHeaderBytes, data + kOuterHeaderBytes, pt_len) != 0) {
            crypto_wipe(nonce, sizeof(nonce));
            RejectPacket();
            if (config_.role == CeStarRole::Host && peer->data == nullptr) {
                CleanupPendingPeer(peer);
                enet_peer_reset(peer);
            }
            return;
        }
        crypto_wipe(nonce, sizeof(nonce));
        uint16_t proto = 0, body_len = 0;
        CeStarMsgType msg_type{};
        const uint8_t* body = nullptr;
        if (!CeStarWire::DecodeFrame(plain.data(), plain.size(), proto, msg_type, body, body_len)) {
            RejectPacket();
            return;
        }
        if (config_.role == CeStarRole::Host && msg_type == CeStarMsgType::Hello) {
            HandleHostHello(peer, session_id, wire_slot, body, body_len, now_ms);
        } else if (config_.role == CeStarRole::Guest && msg_type == CeStarMsgType::Accept) {
            HandleGuestAccept(session_id, body, body_len, now_ms);
        } else {
            RejectPacket();
        }
        return;
    }

    PeerSlot* slot = nullptr;
    if (config_.role == CeStarRole::Host) {
        slot = static_cast<PeerSlot*>(peer->data);
        if (!slot || !slot->authenticated || slot->peer != peer || wire_slot != slot->sim_slot) {
            crypto_wipe(nonce, sizeof(nonce));
            RejectPacket();
            return;
        }
    } else {
        slot = &slots_[0];
        if (!slot->authenticated || slot->peer != peer || wire_slot != 0u) {
            crypto_wipe(nonce, sizeof(nonce));
            RejectPacket();
            return;
        }
    }

    if (crypto_verify16(session_id, slot->session_id) != 0 ||
        crypto_aead_unlock(plain.data(), data + kOuterHeaderBytes + pt_len,
                           slot->key, nonce,
                           data, kOuterHeaderBytes, data + kOuterHeaderBytes, pt_len) != 0) {
        crypto_wipe(nonce, sizeof(nonce));
        RejectPacket();
        return;
    }
    crypto_wipe(nonce, sizeof(nonce));

    if (!slot->recv_replay[channel].Accept(seq)) {
        RejectPacket();
        return;
    }

    uint16_t proto = 0, body_len = 0;
    CeStarMsgType msg_type{};
    const uint8_t* body = nullptr;
    if (!CeStarWire::DecodeFrame(plain.data(), plain.size(), proto, msg_type, body, body_len)) {
        RejectPacket();
        return;
    }
    HandleAuthenticatedFrame(*slot, channel, msg_type, body, body_len, now_ms);
}

#if defined(CE_NETWORK_ICE)
bool CeStarTransport::TryRetryGuestOverRelay() noexcept {
    if (config_.role != CeStarRole::Guest || !config_.use_ice || slots_.empty()) return false;
    auto& slot = slots_[0];
    if (slot.authenticated || slot.ice_require_relay || host_lost_ || !slot.ice) return false;
    slot.ice_failed = true;
    slot.ice.reset();
    if (slot.peer) {
        _ENetPeer* p = slot.peer;
        slot.peer = nullptr;
        enet_peer_reset(p);
    }
    slot.ResetLink();
    for (auto& r : guest_chat_rates_) r.Reset();
    guest_connect_started_ = false;
    guest_hello_sent_ = false;
    last_keepalive_tx_ms_ = 0;
    created_at_ms_ = 0;
    return true;
}
#endif

void CeStarTransport::HandleDisconnectPeer(_ENetPeer* peer, uint16_t reason) {
    if (!peer) return;
    if (config_.role == CeStarRole::Host) {
        CleanupPendingPeer(peer);
        auto* slot = static_cast<PeerSlot*>(peer->data);
        peer->data = nullptr;
        if (slot && slot->peer == peer) {
            const bool was_auth = slot->authenticated;
            const uint16_t s = slot->sim_slot;
            const uint16_t seat = slot->expected_seat;
            slot->ResetLink();
#if defined(CE_NETWORK_ICE)
            slot->ice.reset();
#endif
            if (was_auth) {
                CeStarEvent ev;
                ev.kind = CeStarEventKind::PeerDisconnected;
                ev.sim_slot = s;
                ev.seat = seat;
                ev.reason = reason;
                PushEvent(std::move(ev));
            }
        }
    } else {
        if (slots_[0].peer == peer) {
#if defined(CE_NETWORK_ICE)
            if (TryRetryGuestOverRelay()) {
                return;
            }
#endif
            const bool was_auth = slots_[0].authenticated;
            slots_[0].ResetLink();
            host_lost_ = true;
            if (last_error_ == CeStarError::Ok) last_error_ = CeStarError::HostLost;
            CeStarEvent ev;
            ev.kind = was_auth ? CeStarEventKind::HostDisconnected : CeStarEventKind::Error;
            ev.error = last_error_;
            ev.reason = reason;
            PushEvent(std::move(ev));
        }
    }
}

void CeStarTransport::PumpIceBridges(uint64_t now_ms) {
#if defined(CE_NETWORK_ICE)
    if (!config_.use_ice) return;
    if (config_.role == CeStarRole::Host) {
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            auto& slot = slots_[s];
            if (!slot.ice) continue;
            if (!slot.ice->Pump()) {
                slot.ice_failed = true;
                slot.ice.reset();
                if (slot.peer) {
                    _ENetPeer* p = slot.peer;
                    HandleDisconnectPeer(p, 0);
                    enet_peer_reset(p);
                }
            } else if (slot.authenticated && slot.ice->Ready()) {
                slot.route = slot.ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
            }
        }
    } else {
        auto& slot = slots_[0];
        if (!slot.ice) return;
        if (!slot.ice->Pump()) {
            if (TryRetryGuestOverRelay()) {
                return;
            }
            slot.ice_failed = true;
            slot.ice.reset();
            last_error_ = CeStarError::Io;
            host_lost_ = true;
            if (slot.peer) {
                _ENetPeer* p = slot.peer;
                HandleDisconnectPeer(p, 0);
                enet_peer_reset(p);
            } else {
                CeStarEvent ev;
                ev.kind = CeStarEventKind::Error;
                ev.error = CeStarError::Io;
                PushEvent(std::move(ev));
            }
            return;
        }
        if (slot.ice->Ready()) {
            slot.route = slot.ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
            if (!guest_connect_started_ && !host_lost_) {
                ENetPeer* p = enet_host_connect(host_, &slot.ice->LocalAddress(), kChannelCount, 0);
                if (p) {
                    enet_peer_ping_interval(p, 500);
                    slot.peer = p;
                    slot.connected_at_ms = now_ms ? now_ms : 1u;
                    guest_connect_started_ = true;
                } else if (!TryRetryGuestOverRelay()) {
                    last_error_ = CeStarError::Io;
                    host_lost_ = true;
                    CeStarEvent ev;
                    ev.kind = CeStarEventKind::Error;
                    ev.error = CeStarError::Io;
                    PushEvent(std::move(ev));
                }
            }
        }
    }
#else
    (void)now_ms;
#endif
}

void CeStarTransport::Poll(uint64_t now_ms, uint32_t event_budget) {
    if (!host_) return;
    if (created_at_ms_ == 0) created_at_ms_ = now_ms;

    PumpIceBridges(now_ms);

    ENetEvent ev{};
    uint32_t processed = 0;
    while (processed < event_budget && enet_host_service(host_, &ev, 0) > 0) {
        ++processed;
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (config_.role == CeStarRole::Host) {
                bool addr_ok = true;
#if defined(CE_NETWORK_ICE)
                if (config_.use_ice) {
                    addr_ok = false;
                    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
                        if (slots_[s].ice) {
                            const ENetAddress& a = slots_[s].ice->LocalAddress();
                            if (ev.peer->address.host == a.host && ev.peer->address.port == a.port) {
                                addr_ok = true;
                                break;
                            }
                        }
                    }
                }
#endif
                if (!addr_ok) {
                    RejectPacket();
                    enet_peer_reset(ev.peer);
                } else {
                    ev.peer->data = nullptr;
                    pending_peers_.push_back(PendingPeer{ev.peer, now_ms, 0, false});
                }
            } else {
                if (ev.peer == slots_[0].peer) {
                    slots_[0].connected = true;
                    slots_[0].last_rx_ms = now_ms;
                    SendGuestHello(now_ms);
                }
            }
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            HandlePacket(ev.peer, ev.channelID, ev.packet->data, ev.packet->dataLength, now_ms);
            enet_packet_destroy(ev.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            HandleDisconnectPeer(ev.peer, 0);
            break;

        case ENET_EVENT_TYPE_NONE:
        default:
            break;
        }
    }

    // Timeouts, live RTT updates, periodic keepalives, and ping reports.
    const uint32_t keepalive_ms = (config_.silence_timeout_ms > 0)
        ? std::max<uint32_t>(50u, std::min<uint32_t>(2000u, config_.silence_timeout_ms / 4u))
        : 0u;
    if (config_.role == CeStarRole::Host) {
        // Expire unauthenticated pending peers (and promptly reap rejected closing peers).
        for (size_t i = 0; i < pending_peers_.size();) {
            const PendingPeer& pp = pending_peers_[i];
            const bool close_expired = pp.closing && now_ms >= pp.closing_since_ms &&
                                       now_ms - pp.closing_since_ms >= 200u;
            const bool hs_expired = now_ms >= pp.connected_at_ms &&
                                    now_ms - pp.connected_at_ms >= config_.handshake_timeout_ms;
            if (close_expired || hs_expired) {
                _ENetPeer* p = pp.peer;
                pending_peers_.erase(pending_peers_.begin() + static_cast<std::ptrdiff_t>(i));
                if (p) enet_peer_reset(p);
            } else {
                ++i;
            }
        }
        // Update RTT and expire silent authenticated guests without affecting other peers.
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            auto& slot = slots_[s];
            if (!slot.authenticated || !slot.peer) continue;
            slot.rtt_ms = slot.peer->roundTripTime;
            if (now_ms >= slot.last_rx_ms &&
                now_ms - slot.last_rx_ms >= config_.silence_timeout_ms) {
                _ENetPeer* p = slot.peer;
                HandleDisconnectPeer(p, static_cast<uint16_t>(CeStarError::Timeout));
                enet_peer_reset(p);
            }
        }
        if (config_.ping_report_interval_ms > 0 &&
            (last_ping_broadcast_ms_ == 0 ||
             (now_ms >= last_ping_broadcast_ms_ &&
              now_ms - last_ping_broadcast_ms_ >= config_.ping_report_interval_ms))) {
            if (ActiveGuestCount() > 0) {
                BroadcastPingReport();
                last_ping_broadcast_ms_ = now_ms;
            }
        }
        if (keepalive_ms > 0 &&
            (last_keepalive_tx_ms_ == 0 ||
             (now_ms >= last_keepalive_tx_ms_ &&
              now_ms - last_keepalive_tx_ms_ >= keepalive_ms))) {
            bool sent_any = false;
            for (uint16_t s = 1; s <= config_.max_guests; ++s) {
                if (!slots_[s].authenticated) continue;
                if (SendFrameToSlot(slots_[s], kChannelAux, CeStarMsgType::Keepalive, nullptr, 0, false))
                    sent_any = true;
            }
            if (sent_any) last_keepalive_tx_ms_ = now_ms;
        }
    } else {
        auto& slot = slots_[0];
        if (slot.authenticated && slot.peer) {
            slot.rtt_ms = slot.peer->roundTripTime;
            if (now_ms >= slot.last_rx_ms &&
                now_ms - slot.last_rx_ms >= config_.silence_timeout_ms) {
                last_error_ = CeStarError::Timeout;
                _ENetPeer* p = slot.peer;
                HandleDisconnectPeer(p, static_cast<uint16_t>(CeStarError::Timeout));
                enet_peer_reset(p);
            } else if (keepalive_ms > 0 &&
                       (last_keepalive_tx_ms_ == 0 ||
                        (now_ms >= last_keepalive_tx_ms_ &&
                         now_ms - last_keepalive_tx_ms_ >= keepalive_ms))) {
                if (SendFrameToSlot(slot, kChannelAux, CeStarMsgType::Keepalive, nullptr, 0, false))
                    last_keepalive_tx_ms_ = now_ms;
            }
        } else if (guest_connect_started_ && !host_lost_ && slot.peer) {
            const uint64_t ref = slot.connected_at_ms ? slot.connected_at_ms : created_at_ms_;
            if (now_ms >= ref && now_ms - ref >= config_.handshake_timeout_ms) {
#if defined(CE_NETWORK_ICE)
                if (!TryRetryGuestOverRelay())
#endif
                {
                    last_error_ = CeStarError::Timeout;
                    _ENetPeer* p = slot.peer;
                    HandleDisconnectPeer(p, static_cast<uint16_t>(CeStarError::Timeout));
                    enet_peer_reset(p);
                }
            }
        }
    }

    enet_host_flush(host_);
    PumpIceBridges(now_ms);
}

bool CeStarTransport::SendGuestInput(uint32_t tick, const uint8_t* data, size_t size) {
    if (config_.role != CeStarRole::Guest || !slots_[0].authenticated ||
        (!data && size > 0) || size > CE_STAR_MAX_FRAME_PAYLOAD - 16)
        return false;
    std::vector<uint8_t> body;
    body.reserve(8 + size);
    PutU16(body, config_.local_sim_slot);
    PutU32(body, tick);
    PutU16(body, static_cast<uint16_t>(size));
    PutBytes(body, data, size);
    const bool ok = SendFrameToSlot(slots_[0], kChannelInput, CeStarMsgType::InputGuest,
                                    body.data(), body.size(), true);
    if (ok) enet_host_flush(host_);
    return ok;
}

bool CeStarTransport::BroadcastInputBundle(uint32_t tick, const CeStarBitmask& active_mask,
                                           const uint8_t* data, size_t size) {
    if (config_.role != CeStarRole::Host || (!data && size > 0) ||
        size > CE_STAR_MAX_FRAME_PAYLOAD - 32)
        return false;
    std::vector<uint8_t> body;
    body.reserve(16 + size);
    PutU32(body, tick);
    if (!active_mask.Encode(body)) return false;
    PutU16(body, static_cast<uint16_t>(size));
    PutBytes(body, data, size);
    bool any_ok = false;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated) continue;
        if (SendFrameToSlot(slots_[s], kChannelInput, CeStarMsgType::InputBundle,
                            body.data(), body.size(), true)) {
            any_ok = true;
        } else if (slots_[s].peer) {
            // Drop only the failing/closing guest slot instead of aborting the whole 3P/4P match.
            _ENetPeer* p = slots_[s].peer;
            HandleDisconnectPeer(p, static_cast<uint16_t>(CeStarError::Io));
            enet_peer_reset(p);
        }
    }
    if (any_ok) enet_host_flush(host_);
    return any_ok;
}

bool CeStarTransport::SendHash(uint32_t tick, uint64_t state_hash) {
    std::vector<uint8_t> body;
    body.reserve(14);
    PutU16(body, config_.local_sim_slot);
    PutU32(body, tick);
    PutU64(body, state_hash);
    if (config_.role == CeStarRole::Guest) {
        const bool ok = SendFrameToSlot(slots_[0], kChannelControl, CeStarMsgType::Hash,
                                        body.data(), body.size(), true);
        if (ok) enet_host_flush(host_);
        return ok;
    }
    bool any_ok = false;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated) continue;
        if (SendFrameToSlot(slots_[s], kChannelControl, CeStarMsgType::Hash,
                            body.data(), body.size(), true)) {
            any_ok = true;
        } else if (slots_[s].peer) {
            _ENetPeer* p = slots_[s].peer;
            HandleDisconnectPeer(p, static_cast<uint16_t>(CeStarError::Io));
            enet_peer_reset(p);
        }
    }
    if (any_ok) enet_host_flush(host_);
    return any_ok;
}

bool CeStarTransport::BroadcastDesync(uint32_t tick, uint16_t ref_slot, uint16_t diverging_slot,
                                      const CeStarBitmask& diverging_mask, uint64_t expected_hash,
                                      uint64_t actual_hash) {
    if (config_.role != CeStarRole::Host) return false;
    std::vector<uint8_t> body;
    body.reserve(40);
    PutU32(body, tick);
    PutU16(body, ref_slot);
    PutU16(body, diverging_slot);
    if (!diverging_mask.Encode(body)) return false;
    PutU64(body, expected_hash);
    PutU64(body, actual_hash);
    bool any = false, all_ok = true;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated) continue;
        any = true;
        if (!SendFrameToSlot(slots_[s], kChannelControl, CeStarMsgType::Desync,
                             body.data(), body.size(), true))
            all_ok = false;
    }
    if (any) enet_host_flush(host_);
    return any && all_ok;
}

bool CeStarTransport::SendChat(const std::string& text) {
    if (!IsPrintableAsciiText(text, 1, CE_STAR_MAX_CHAT_BYTES)) return false;
    std::vector<uint8_t> body;
    body.reserve(12 + text.size());
    PutU16(body, config_.local_seat);
    PutU16(body, config_.local_sim_slot);
    PutU32(body, ++chat_send_seq_);
    PutU16(body, static_cast<uint16_t>(text.size()));
    PutBytes(body, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    if (config_.role == CeStarRole::Guest) {
        const bool ok = SendFrameToSlot(slots_[0], kChannelAux, CeStarMsgType::Chat,
                                        body.data(), body.size(), true);
        if (ok) enet_host_flush(host_);
        return ok;
    }
    bool any = false, all_ok = true;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated) continue;
        any = true;
        if (!SendFrameToSlot(slots_[s], kChannelAux, CeStarMsgType::Chat,
                             body.data(), body.size(), true))
            all_ok = false;
    }
    if (any) enet_host_flush(host_);
    return any && all_ok;
}

bool CeStarTransport::BroadcastPingReport() {
    if (config_.role != CeStarRole::Host) return false;
    std::vector<CeStarPingEntry> entries;
    entries.push_back(CeStarPingEntry{0u, 0u, CeStarRoute::Direct});
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated || !slots_[s].peer) continue;
        const uint16_t rtt = static_cast<uint16_t>(std::min<uint32_t>(slots_[s].peer->roundTripTime, 9999u));
        slots_[s].rtt_ms = rtt;
        entries.push_back(CeStarPingEntry{s, rtt, slots_[s].route});
    }
    last_ping_report_ = entries;
    std::vector<uint8_t> body;
    body.reserve(2 + entries.size() * 6);
    PutU16(body, static_cast<uint16_t>(entries.size()));
    for (const auto& e : entries) {
        PutU16(body, e.sim_slot);
        PutU16(body, e.rtt_ms);
        body.push_back(static_cast<uint8_t>(e.route));
        body.push_back(0u);
    }
    bool any = false, all_ok = true;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (!slots_[s].authenticated) continue;
        any = true;
        if (!SendFrameToSlot(slots_[s], kChannelAux, CeStarMsgType::PingReport,
                             body.data(), body.size(), false))
            all_ok = false;
    }
    if (any) enet_host_flush(host_);
    return any && all_ok;
}

bool CeStarTransport::DisconnectGuest(uint16_t sim_slot, uint16_t reason) {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests)
        return false;
    auto& slot = slots_[sim_slot];
    if (!slot.peer) return false;
    if (slot.authenticated) {
        std::vector<uint8_t> body;
        PutU16(body, sim_slot);
        PutU16(body, reason);
        SendFrameToSlot(slot, kChannelControl, CeStarMsgType::Leave, body.data(), body.size(), true);
        enet_host_flush(host_);
    }
    _ENetPeer* p = slot.peer;
    HandleDisconnectPeer(p, reason);
    enet_peer_disconnect_later(p, 0);
    return true;
}

void CeStarTransport::Leave(uint16_t reason) {
    if (!host_) return;
    std::vector<uint8_t> body;
    PutU16(body, config_.local_sim_slot);
    PutU16(body, reason);
    if (config_.role == CeStarRole::Guest) {
        if (slots_[0].authenticated && slots_[0].peer) {
            SendFrameToSlot(slots_[0], kChannelControl, CeStarMsgType::Leave,
                            body.data(), body.size(), true);
            enet_host_flush(host_);
#if defined(CE_NETWORK_ICE)
            if (slots_[0].ice) slots_[0].ice->Pump();
#endif
            enet_peer_disconnect_later(slots_[0].peer, 0);
        }
    } else {
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            if (slots_[s].authenticated && slots_[s].peer) {
                SendFrameToSlot(slots_[s], kChannelControl, CeStarMsgType::Leave,
                                body.data(), body.size(), true);
            }
        }
        enet_host_flush(host_);
#if defined(CE_NETWORK_ICE)
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            if (slots_[s].ice) slots_[s].ice->Pump();
        }
#endif
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            if (slots_[s].peer) enet_peer_disconnect_later(slots_[s].peer, 0);
        }
    }
}

void CeStarTransport::FailConnection(CeStarError err) {
    last_error_ = (err == CeStarError::Ok) ? CeStarError::Io : err;
    if (config_.role == CeStarRole::Guest) {
        host_lost_ = true;
        if (!slots_.empty() && slots_[0].peer) {
            _ENetPeer* p = slots_[0].peer;
            enet_peer_reset(p);
            slots_[0].ResetLink();
        }
        CeStarEvent ev;
        ev.kind = CeStarEventKind::HostDisconnected;
        ev.error = last_error_;
        PushEvent(std::move(ev));
    } else {
        CeStarEvent ev;
        ev.kind = CeStarEventKind::Error;
        ev.error = last_error_;
        PushEvent(std::move(ev));
    }
}

bool CeStarTransport::IsAuthenticated() const noexcept {
    if (config_.role == CeStarRole::Host) return host_ != nullptr;
    return !slots_.empty() && slots_[0].authenticated;
}

uint16_t CeStarTransport::ActiveGuestCount() const noexcept {
    if (config_.role != CeStarRole::Host) return IsAuthenticated() ? 1u : 0u;
    uint16_t count = 0;
    for (uint16_t s = 1; s <= config_.max_guests; ++s) {
        if (slots_[s].authenticated) ++count;
    }
    return count;
}

CeStarBitmask CeStarTransport::ActiveMask() const noexcept {
    const uint16_t width = static_cast<uint16_t>(config_.max_guests + 1u);
    CeStarBitmask bm = CeStarBitmask::FromMask64(0ULL, width);
    if (config_.role == CeStarRole::Host) {
        bm.Set(0);
        for (uint16_t s = 1; s <= config_.max_guests; ++s) {
            if (slots_[s].authenticated) bm.Set(s);
        }
    } else if (IsAuthenticated()) {
        bm.Set(0);
        bm.Set(config_.local_sim_slot);
    }
    return bm;
}

bool CeStarTransport::GetPeerInfo(uint16_t sim_slot, CeStarPeerInfo& out) const noexcept {
    out = CeStarPeerInfo{};
    if (config_.role == CeStarRole::Host) {
        if (sim_slot < 1 || sim_slot > config_.max_guests) return false;
        const auto& s = slots_[sim_slot];
        out.seat = s.expected_seat;
        out.sim_slot = s.sim_slot;
        out.connected = s.connected;
        out.authenticated = s.authenticated;
        out.rtt_ms = s.peer ? s.peer->roundTripTime : s.rtt_ms;
        out.route = s.route;
        out.last_rx_ms = s.last_rx_ms;
        return true;
    }
    if (sim_slot != 0 || slots_.empty()) return false;
    const auto& s = slots_[0];
    out.seat = 0;
    out.sim_slot = 0;
    out.connected = s.connected;
    out.authenticated = s.authenticated;
    out.rtt_ms = s.peer ? s.peer->roundTripTime : s.rtt_ms;
    out.route = s.route;
    out.last_rx_ms = s.last_rx_ms;
    return true;
}

#if defined(CE_NETWORK_ICE)
bool CeStarTransport::AttachGuestIceBridge(uint16_t sim_slot, const juice_config_t& config,
                                           bool require_relay) {
    if (config_.role != CeStarRole::Host || !config_.use_ice || !host_ ||
        sim_slot < 1 || sim_slot > config_.max_guests || slots_[sim_slot].ice)
        return false;
    auto bridge = CeIceBridge::Create(host_, config, require_relay);
    if (!bridge) return false;
    slots_[sim_slot].ice = std::move(bridge);
    slots_[sim_slot].ice_failed = false;
    slots_[sim_slot].ice_require_relay = require_relay;
    return true;
}

bool CeStarTransport::GetGuestLocalIceDescription(uint16_t sim_slot, std::string& out_sdp) const {
    out_sdp.clear();
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice)
        return false;
    return slots_[sim_slot].ice->LocalDescription(out_sdp);
}

bool CeStarTransport::GetGuestEarlyRelayIceDescription(uint16_t sim_slot, std::string& out_sdp) const {
    out_sdp.clear();
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice)
        return false;
    return slots_[sim_slot].ice->EarlyRelayDescription(out_sdp);
}

bool CeStarTransport::IsGuestIceGatheringDone(uint16_t sim_slot) const {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice)
        return false;
    return slots_[sim_slot].ice->GatheringDone();
}

bool CeStarTransport::SetGuestRemoteIceDescription(uint16_t sim_slot, const std::string& remote_sdp) {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice)
        return false;
    return slots_[sim_slot].ice->SetRemoteDescription(remote_sdp);
}

bool CeStarTransport::IsGuestIceReady(uint16_t sim_slot) const {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice)
        return false;
    return slots_[sim_slot].ice->Ready();
}

bool CeStarTransport::IsGuestIceFailed(uint16_t sim_slot) const {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests)
        return false;
    return slots_[sim_slot].ice_failed || (slots_[sim_slot].ice && slots_[sim_slot].ice->Failed());
}

CeStarRoute CeStarTransport::GuestIceRoute(uint16_t sim_slot) const {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests ||
        !slots_[sim_slot].ice || !slots_[sim_slot].ice->Ready())
        return CeStarRoute::Unknown;
    return slots_[sim_slot].ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
}

void CeStarTransport::DetachGuestIceBridge(uint16_t sim_slot) {
    if (config_.role != CeStarRole::Host || sim_slot < 1 || sim_slot > config_.max_guests) return;
    slots_[sim_slot].ice.reset();
    slots_[sim_slot].ice_failed = false;
    slots_[sim_slot].ice_require_relay = false;
}

bool CeStarTransport::AttachHostIceBridge(const juice_config_t& config, bool require_relay) {
    if (config_.role != CeStarRole::Guest || !config_.use_ice || !host_ ||
        slots_.empty() || slots_[0].ice)
        return false;
    auto bridge = CeIceBridge::Create(host_, config, require_relay);
    if (!bridge) return false;
    slots_[0].ice = std::move(bridge);
    slots_[0].ice_failed = false;
    slots_[0].ice_require_relay = require_relay;
    return true;
}

bool CeStarTransport::GetLocalIceDescription(std::string& out_sdp) const {
    out_sdp.clear();
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice) return false;
    return slots_[0].ice->LocalDescription(out_sdp);
}

bool CeStarTransport::GetEarlyRelayIceDescription(std::string& out_sdp) const {
    out_sdp.clear();
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice) return false;
    return slots_[0].ice->EarlyRelayDescription(out_sdp);
}

bool CeStarTransport::IsIceGatheringDone() const {
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice) return false;
    return slots_[0].ice->GatheringDone();
}

bool CeStarTransport::SetRemoteIceDescription(const std::string& remote_sdp) {
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice) return false;
    return slots_[0].ice->SetRemoteDescription(remote_sdp);
}

bool CeStarTransport::IsIceReady() const {
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice) return false;
    return slots_[0].ice->Ready();
}

bool CeStarTransport::IsIceFailed() const {
    if (config_.role != CeStarRole::Guest || slots_.empty()) return false;
    return slots_[0].ice_failed || (slots_[0].ice && slots_[0].ice->Failed());
}

CeStarRoute CeStarTransport::HostIceRoute() const {
    if (config_.role != CeStarRole::Guest || slots_.empty() || !slots_[0].ice || !slots_[0].ice->Ready())
        return CeStarRoute::Unknown;
    return slots_[0].ice->UsesRelay() ? CeStarRoute::Relay : CeStarRoute::Direct;
}

void CeStarTransport::DetachHostIceBridge() {
    if (config_.role != CeStarRole::Guest || slots_.empty()) return;
    slots_[0].ice.reset();
    slots_[0].ice_failed = false;
    slots_[0].ice_require_relay = false;
    if (!slots_[0].authenticated) {
        if (slots_[0].peer) {
            enet_peer_reset(slots_[0].peer);
        }
        slots_[0].ResetLink();
        for (auto& r : guest_chat_rates_) r.Reset();
        guest_connect_started_ = false;
        guest_hello_sent_ = false;
        last_keepalive_tx_ms_ = 0;
        created_at_ms_ = 0;
        host_lost_ = false;
        last_error_ = CeStarError::Ok;
    }
}
#endif
