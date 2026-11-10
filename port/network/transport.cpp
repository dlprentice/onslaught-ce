// SPDX-License-Identifier: GPL-3.0-only
// Direct IPv4 ENet transport, bounded application framing and invite-secret admission. No game objects live here.
#include "network.h"
#if defined(CE_NETWORK_TEXT)
#include "media_profile.h"
#include "text_chat.h"
#endif
#if defined(CE_NETWORK_ICE)
#include "ice_session.h"
#include "ice_bridge.h"
#endif
#include <enet/enet.h>
#include <monocypher.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
#if defined(CE_NETWORK_TEXT)
const uint32_t kTextChannelCount = 5;
#else
const uint32_t kTextChannelCount = 4;
#endif
constexpr uint32_t kMagic = 0x43454e31, kVersion = 1, kChannels = kTextChannelCount, kChunk = 1000;
constexpr uint32_t kHeader = 48, kTag = 16, kWire = kHeader + kChunk + kTag;
constexpr uint32_t kParts = (CE_NET_MAX_MESSAGE + kChunk - 1) / kChunk;
constexpr uint32_t kQueue = 64, kPendingPackets = 256, kPendingBytes = 256 * 1024;
constexpr uint32_t kControlReserve = 8;
// Five 20Hz snapshot intervals; age starts at the first authenticated fragment and never slides.
constexpr uint32_t kStateAssemblyMs = 250;
// ENet learns RTT from acknowledgements, so a peer blocked in a level load or pre-run looks like a slow link: a guest
// measured 1.6 s +- 1.4 s after the host's pre-run and waited ~7 s, then twice that, to resend one lost control.
// A receive silence this long (a local or remote stall, or an outage) ends with stall-inflated samples; the timing
// learnt before it is kept through a short grace instead. A genuinely slower link keeps its own estimate.
constexpr enet_uint32 kSilentGapMs = 500, kGapGraceMs = 250;
constexpr uint32_t kIncomingMetadata = 1024, kPacketBurst = 256, kPacketRate = 1024;
constexpr uint32_t kByteBurst = 128 * 1024, kByteRate = 512 * 1024;
constexpr uint32_t kHello = 1, kChallenge = 2, kProof = 3, kAccept = 4, kReady = 5;
constexpr uint32_t kPing = 6, kEpoch = 7, kEpochAck = 8, kLeave = 9, kRefuse = 10;
constexpr uint32_t kHelloSent = 1, kChallengeSent = 2, kProofSent = 3, kAcceptSent = 4;
// HELLO and REFUSE carry nonce, compatibility digest and the sender's version label.
constexpr uint32_t kHelloBytes = 64 + CE_NET_VERSION_BYTES;
unsigned gUsers;
CE_NetSession *gPolling;

struct Header {
    uint32_t kind, channel, epoch, sequence, tick, message, total, part, parts;
    uint8_t session[16];
};
struct Replay {
    uint32_t highest = 0;
    uint64_t low = 0, high = 0;
    bool Add(uint32_t seq)
    {
        if (!seq) return false;
        if (seq > highest) {
            uint32_t n = seq - highest;
            if (n >= 128) { low = 1; high = 0; }
            else if (n >= 64) { high = low << (n - 64); low = 1; }
            else { high = (high << n) | (low >> (64 - n)); low = (low << n) | 1; }
            highest = seq;
            return true;
        }
        uint32_t n = highest - seq;
        if (n >= 128) return false;
        uint64_t bit = uint64_t(1) << (n & 63);
        uint64_t &word = n < 64 ? low : high;
        if (word & bit) return false;
        word |= bit;
        return true;
    }
};
struct Assembly {
    uint32_t message = 0, kind = 0, epoch = 0, tick = 0, total = 0, parts = 0, received = 0;
    uint64_t started = 0;
    std::array<uint8_t, kParts> seen{};
    std::array<uint8_t, CE_NET_MAX_MESSAGE> data{};
};
struct Queued {
    CE_NetMessage meta{};
    std::vector<uint8_t> data;
};
uint32_t Channel(uint32_t kind)
{
#if defined(CE_NETWORK_TEXT)
    if (kind == CE_NET_TEXT) return CE_NET_TEXT_CHANNEL;
#endif

    switch (kind) {
    case CE_NET_INPUT: return 1;
    case CE_NET_STATE: return 2;
    case CE_NET_BASELINE: case CE_NET_LIFECYCLE: case CE_NET_STATE_REPAIR: return 3;
    case CE_NET_STATE_CONTROL: return 0;
    default: return 0;
    }
}
bool IsApp(uint32_t kind) {
#if defined(CE_NETWORK_TEXT)
    if (kind == CE_NET_TEXT) return true;
#endif
 return kind >= CE_NET_CONTROL && kind <= CE_NET_STATE_REPAIR; }
bool AllZero(const uint8_t *p, uint32_t n)
{ uint8_t x = 0; for (uint32_t i = 0; i < n; ++i) x |= p[i]; return x == 0; }

void WriteHeader(uint8_t *out, const Header &h)
{
    CE_NetWriter w;
    CE_NetWriterInit(&w, out, kHeader);
    CE_NetWriteU32(&w, kMagic); CE_NetWriteU16(&w, kVersion);
    CE_NetWriteU8(&w, uint8_t(h.kind)); CE_NetWriteU8(&w, uint8_t(h.channel));
    CE_NetWriteU32(&w, h.epoch); CE_NetWriteU32(&w, h.sequence); CE_NetWriteU32(&w, h.tick);
    CE_NetWriteU32(&w, h.message); CE_NetWriteU32(&w, h.total);
    CE_NetWriteU16(&w, uint16_t(h.part)); CE_NetWriteU16(&w, uint16_t(h.parts));
    CE_NetWriteBytes(&w, h.session, 16);
}
bool ReadHeader(const uint8_t *p, uint32_t size, uint32_t channel, Header &h, uint32_t &partSize)
{
    if (!p || size < kHeader + kTag || size > kWire || channel >= kChannels) return false;
    CE_NetReader r;
    CE_NetReaderInit(&r, p, kHeader);
    if (CE_NetReadU32(&r) != kMagic || CE_NetReadU16(&r) != kVersion) return false;
    h.kind = CE_NetReadU8(&r); h.channel = CE_NetReadU8(&r);
    h.epoch = CE_NetReadU32(&r); h.sequence = CE_NetReadU32(&r); h.tick = CE_NetReadU32(&r);
    h.message = CE_NetReadU32(&r); h.total = CE_NetReadU32(&r);
    h.part = CE_NetReadU16(&r); h.parts = CE_NetReadU16(&r);
    CE_NetReadBytes(&r, h.session, 16);
    if (!CE_NetReadDone(&r) || !h.sequence || !h.message || h.channel != channel ||
        h.kind == 0 || (h.kind > kRefuse && !IsApp(h.kind)) || Channel(h.kind) != channel ||
        h.total > CE_NET_MAX_MESSAGE || !h.parts || h.parts > kParts || h.part >= h.parts ||
        h.parts != std::max(1u, (h.total + kChunk - 1) / kChunk)) return false;
    if ((channel < 2 || !IsApp(h.kind)) && h.parts != 1) return false;
    if (h.kind == CE_NET_LIFECYCLE && (h.total > CE_NET_MAX_LIFECYCLE || h.parts != 1)) return false;
    if (h.kind == CE_NET_STATE_CONTROL && (h.total != CE_NET_STATE_CONTROL_BYTES || h.parts != 1)) return false;
    if (h.kind == CE_NET_STATE_REPAIR && !h.total) return false;
    partSize = h.total - h.part * kChunk;
    if (partSize > kChunk) partSize = kChunk;
    return size == kHeader + partSize + kTag;
}
void Nonce(uint8_t nonce[24], const Header &h, bool fromHost)
{
    std::memcpy(nonce, h.session, 16);
    nonce[16] = fromHost ? 1 : 2;
    nonce[17] = uint8_t(h.channel); nonce[18] = nonce[19] = 0;
    CE_NetWriter w;
    CE_NetWriterInit(&w, nonce + 20, 4); CE_NetWriteU32(&w, h.sequence);
}
}


#if defined(CE_NETWORK_TEXT)
struct CeTextBudget {
    uint64_t accepted[12]{};
    uint32_t count = 0;
    uint64_t last = 0;
    bool Clock(uint64_t now) const { return count == 0 || now >= last; }
    bool Allows(uint64_t now) const {
        if (!Clock(now)) return false;
        uint32_t burst = 0, window = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (now - accepted[i] < 2000) ++burst;
            if (now - accepted[i] < 10000) ++window;
        }
        return burst < 4 && window < 12;
    }
    void Add(uint64_t now) {
        uint32_t retained = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (now - accepted[i] < 10000) accepted[retained++] = accepted[i];
        accepted[retained++] = now; count = retained; last = now;
    }
};
struct CeTextQueued {
    uint32_t epoch = 0, size = 0;
    uint8_t bytes[CE_NET_TEXT_MAX_FRAME]{};
};
#endif
struct CE_NetSession {
    CE_NetStatus status{};
    ENetHost *host = nullptr;
    ENetPeer *peer = nullptr;
#if defined(CE_NETWORK_ICE)
    std::unique_ptr<CeIceBridge> ice;
    uint32_t iceTimeoutMs = 30000;
    bool iceWaiting = false;
    void *iceExchange = nullptr;
    CE_NetIceExchangeCallbacks iceCallbacks{};
    char iceProblem[160]{};
#endif
    uint8_t secret[32]{}, compatibility[32]{}, clientNonce[32]{}, hostNonce[32]{}, session[16]{}, key[32]{};
    char version[CE_NET_VERSION_BYTES]{}, peerVersion[CE_NET_VERSION_BYTES + 1]{};
    uint32_t handshakeMs = 5000, silenceMs = 10000, stage = 0;
    uint64_t started = 0, lastPoll = 0, lastAuthenticated = 0, lastPing = 0, syncStarted = 0;
    uint32_t sendSequence[kChannels]{}, messageSequence[kChannels]{}, complete[kChannels]{};
    uint32_t retiredState = 0;
    Replay replay[kChannels];
    Assembly assembly[kChannels];
    Queued queue[kQueue];
    uint32_t head = 0, count = 0, queuedBytes = 0, pendingPackets = 0, pendingBytes = 0;
    uint32_t interceptError = 0;
    bool gapResuming = false; uint64_t gapGraceUntil = 0; enet_uint32 rtt = 0, rttVariance = 0, throttle = 0;
    uint64_t ingressAt = 0, packetCredit = uint64_t(kPacketBurst) * 1000, byteCredit = uint64_t(kByteBurst) * 1000;

#if defined(CE_NETWORK_TEXT)
    CeTextQueued textQueue[CE_NET_TEXT_RECEIVE_CAPACITY]{};
    uint32_t textHead = 0, textCount = 0, textPending = 0;
    uint64_t textLastReceived = 0, textLastSent = 0;
    CeTextBudget textSendBudget, textReceiveBudget;
    CE_NetTextInfo textInfo{};
#endif
};

namespace {
void KeepPeerVersion(CE_NetSession *s, const uint8_t *label)
{
    uint32_t n = 0;
    for (; n < CE_NET_VERSION_BYTES && label[n]; ++n) s->peerVersion[n] = label[n] >= 0x20 && label[n] < 0x7f ? char(label[n]) : '?';
    s->peerVersion[n] = 0;
}
bool SilentGap(const ENetPeer *p)
{
    return p->lastReceiveTime && enet_uint32(enet_time_get() - p->lastReceiveTime) >= kSilentGapMs;
}
void TimingBeforeService(CE_NetSession *s, uint64_t now)
{
    ENetPeer *p = s->peer;
    if (!p || !p->lastReceiveTime) return;
    if (SilentGap(p)) s->gapResuming = s->rtt != 0;
    else if (!s->gapResuming && now >= s->gapGraceUntil) {
        s->rtt = p->roundTripTime; s->rttVariance = p->roundTripTimeVariance; s->throttle = p->packetThrottle;
    }
}
void TimingAfterService(CE_NetSession *s, uint64_t now)
{
    ENetPeer *p = s->peer;
    if (!p) return;
    if (s->gapResuming && !SilentGap(p)) { s->gapResuming = false; s->gapGraceUntil = now + std::max(kGapGraceMs, 2 * s->rtt); }
    if (s->gapResuming || now >= s->gapGraceUntil) return;
    p->roundTripTime = std::min(p->roundTripTime, s->rtt);
    p->roundTripTimeVariance = std::min(p->roundTripTimeVariance, s->rttVariance);
    p->packetThrottle = std::max(p->packetThrottle, s->throttle);
    const enet_uint32 timeout = p->roundTripTime + 4 * p->roundTripTimeVariance;
    ENetListIterator first = enet_list_begin(&p->sentReliableCommands);
    if (first == enet_list_end(&p->sentReliableCommands)) return;
    for (ENetListIterator i = first; i != enet_list_end(&p->sentReliableCommands); i = enet_list_next(i)) {
        auto *c = reinterpret_cast<ENetOutgoingCommand *>(i);
        c->roundTripTimeout = std::min(c->roundTripTimeout, timeout);
    }
    // ENet checks resends only from nextTimeout, which it derives from the oldest sent command.
    auto *oldest = reinterpret_cast<ENetOutgoingCommand *>(first);
    p->nextTimeout = oldest->sentTime + oldest->roundTripTimeout;
}
#if defined(CE_NETWORK_TEXT)
void ClearTextMessages(CE_NetSession *s){
    crypto_wipe(s->textQueue, sizeof(s->textQueue));
    s->textHead = s->textCount = 0;
    s->textLastReceived = s->textLastSent = 0;
    /* Pending callbacks and session rate budgets survive epoch transitions. */
}
#endif
void ClearMessages(CE_NetSession *s)
{
    s->head = s->count = s->queuedBytes = s->retiredState = 0;
    for (auto &q : s->queue) std::vector<uint8_t>().swap(q.data);
    for (auto &a : s->assembly) { a.message = a.received = 0; a.seen.fill(0); }
    std::fill(std::begin(s->complete), std::end(s->complete), 0);

#if defined(CE_NETWORK_TEXT)
    ClearTextMessages(s);
#endif
}
#if defined(CE_NETWORK_ICE)
void RetireIceExchange(CE_NetSession *s)
{
    void *context = s->iceExchange;
    s->iceExchange = nullptr;
    if (context) s->iceCallbacks.destroy(context);
}
#endif
void Fail(CE_NetSession *s, uint32_t error)
{
    if (s->peer) enet_peer_reset(s->peer);
    s->peer = nullptr;
#if defined(CE_NETWORK_ICE)
    s->ice.reset();
    RetireIceExchange(s);
#endif
    s->status.phase = CE_NET_FAILED; s->status.error = error;
    ClearMessages(s);
    crypto_wipe(s->key, sizeof(s->key));
}
void FreePacket(ENetPacket *p)
{
    auto *s = static_cast<CE_NetSession *>(p->userData);
    --s->pendingPackets; s->pendingBytes -= uint32_t(p->dataLength);


}
#if defined(CE_NETWORK_TEXT)
void FreeTextPacket(ENetPacket *p){
    CE_NetSession *s=static_cast<CE_NetSession *>(p->userData);
    if(s&&s->textPending)--s->textPending;
    FreePacket(p);
}
#endif


static enet_uint32 TextPacketFlags(uint32_t kind, enet_uint32 ordinary) {
#if defined(CE_NETWORK_TEXT)
    if (kind == CE_NET_TEXT) return ENET_PACKET_FLAG_UNSEQUENCED;
#else
    (void)kind;
#endif
    return ordinary;
}
bool QueuePacket(CE_NetSession *s, Header &h, const uint8_t *plain, uint32_t size, const uint8_t *key)
{
    if (!s->peer || size > kChunk || s->pendingPackets >= kPendingPackets ||
        s->pendingBytes > kPendingBytes - (kHeader + size + kTag)) return false;
    if (s->sendSequence[h.channel] == UINT32_MAX) { Fail(s, CE_NET_PROTOCOL); return false; }
    h.sequence = ++s->sendSequence[h.channel];
    uint8_t wire[kWire];
    WriteHeader(wire, h);
    if (key) {
        uint8_t nonce[24]; Nonce(nonce, h, s->status.role == CE_NET_HOST);
        crypto_aead_lock(wire + kHeader, wire + kHeader + size, key, nonce, wire, kHeader, plain, size);
    } else {
        if (size) std::memcpy(wire + kHeader, plain, size);
        std::memset(wire + kHeader + size, 0, kTag);
    }
    // Each STATE fragment is independently authenticated and supersedable. Other application traffic stays ordered.
    enet_uint32 flags = h.kind == CE_NET_STATE ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;
    ENetPacket *packet = enet_packet_create(wire, kHeader + size + kTag, TextPacketFlags(h.kind, flags));
    if (!packet) return false;
    if (enet_peer_send(s->peer, uint8_t(h.channel), packet) != 0) { enet_packet_destroy(packet); return false; }
    packet->userData = s;
#if defined(CE_NETWORK_TEXT)
    packet->freeCallback=h.kind==CE_NET_TEXT?FreeTextPacket:FreePacket;
#else
    packet->freeCallback=FreePacket;
#endif

    ++s->pendingPackets;
#if defined(CE_NETWORK_TEXT)
    if (h.kind == CE_NET_TEXT) ++s->textPending;
#endif
s->pendingBytes += uint32_t(packet->dataLength);
    return true;
}
bool SendInternal(CE_NetSession *s, uint32_t kind, const uint8_t *data = nullptr, uint32_t size = 0)
{
    if (s->messageSequence[0] == UINT32_MAX) { Fail(s, CE_NET_PROTOCOL); return false; }
    Header h{};
    h.kind = kind; h.channel = 0; h.epoch = IsApp(kind) ? s->status.epoch : (kind >= kPing ? s->status.epoch : 0);
    h.message = ++s->messageSequence[0]; h.total = size; h.parts = 1;
    if (kind != kHello) std::memcpy(h.session, s->session, 16);
    const uint8_t *key = kind == kHello ? nullptr : (kind == kChallenge || kind == kRefuse ? s->secret : s->key);
    return QueuePacket(s, h, data, size, key);
}
// Only a verified remote leave retains completed messages for the session's terminal control validation.
// No further ingress is serviced once CLOSED; the existing queue and byte caps remain unchanged.
void CloseTransport(CE_NetSession *s, bool preserveCompleted)
{
    if(!s)return;
#if defined(CE_NETWORK_TEXT)
    ClearTextMessages(s);
#endif

    if (!s || s->status.phase == CE_NET_FAILED) return;
    if (s->status.phase == CE_NET_CLOSED) {
        if (!preserveCompleted) ClearMessages(s);
        return;
    }
#if defined(CE_NETWORK_ICE)
    const bool gracefulIceClose = s->peer && (s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING);
#endif
    if (s->peer && (s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING)) {
        if (!SendInternal(s, kLeave)) {
            if (s->status.phase != CE_NET_FAILED) Fail(s, CE_NET_CAPACITY);
            return;
        }
        enet_host_flush(s->host); enet_peer_disconnect_later(s->peer, 0);
#if defined(CE_NETWORK_ICE)
        // Flush the local shuttle before closing it, just as the direct path flushes its host.
        if (s->ice && !s->ice->Pump()) { Fail(s, CE_NET_IO); return; }
#endif
    } else if (s->peer) {
        // Cancelling authentication is an abort, not an authenticated leave.
        // Notify the transport before retiring its local shuttle.
        enet_peer_disconnect_now(s->peer, 0);
        s->peer = nullptr;
#if defined(CE_NETWORK_ICE)
        if (s->ice) s->ice->Pump();
#endif
    }
    s->status.phase = CE_NET_CLOSED;
#if defined(CE_NETWORK_ICE)
    // An admitted close retains the bridge until Destroy, so bounded ENet
    // teardown can still carry the queued leave. A cancelled handshake stops now.
    if (!gracefulIceClose) s->ice.reset();
    RetireIceExchange(s);
#endif
    if (!preserveCompleted) ClearMessages(s);
    crypto_wipe(s->key, sizeof(s->key));
}
void DeriveKey(CE_NetSession *s)
{
    uint8_t transcript[sizeof("Onslaught CE network traffic 1") + 16 + 32 + 32 + 32];
    CE_NetWriter w;
    CE_NetWriterInit(&w, transcript, sizeof(transcript));
    const char domain[] = "Onslaught CE network traffic 1";
    CE_NetWriteBytes(&w, reinterpret_cast<const uint8_t *>(domain), sizeof(domain));
    CE_NetWriteBytes(&w, s->session, 16); CE_NetWriteBytes(&w, s->clientNonce, 32);
    CE_NetWriteBytes(&w, s->hostNonce, 32); CE_NetWriteBytes(&w, s->compatibility, 32);
    crypto_blake2b_keyed(s->key, 32, s->secret, 32, transcript, w.size);
    crypto_wipe(transcript, sizeof(transcript));
}
bool Decrypt(CE_NetSession *s, const Header &h, const uint8_t *wire, uint32_t n, uint8_t *plain, const uint8_t *key)
{
    uint8_t nonce[24]; Nonce(nonce, h, s->status.role != CE_NET_HOST);
    return crypto_aead_unlock(plain, wire + kHeader + n, key, nonce, wire, kHeader, wire + kHeader, n) == 0;
}
bool SameSession(const CE_NetSession *s, const Header &h) { return crypto_verify16(s->session, h.session) == 0; }
void Reject(CE_NetSession *s) { if (s->status.rejected_packets != UINT32_MAX) ++s->status.rejected_packets; }
uint32_t ReadBE16(const uint8_t *p) { return (uint32_t(p[0]) << 8) | p[1]; }
uint32_t DatagramCommands(const uint8_t *data, uint32_t size)
{
    if (!data || size < 2 || size > CE_NET_MAX_DATAGRAM) return 0;
    uint32_t flags = ReadBE16(data);
    // No compression or ENet checksum is enabled on this transport. Do not admit a decompression allocation path.
    if (flags & ENET_PROTOCOL_HEADER_FLAG_COMPRESSED) return 0;
    uint32_t at = (flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME) ? 4 : 2, count = 0;
    static const uint32_t sizes[] = {0, sizeof(ENetProtocolAcknowledge), sizeof(ENetProtocolConnect),
        sizeof(ENetProtocolVerifyConnect), sizeof(ENetProtocolDisconnect), sizeof(ENetProtocolPing),
        sizeof(ENetProtocolSendReliable), sizeof(ENetProtocolSendUnreliable), sizeof(ENetProtocolSendFragment),
        sizeof(ENetProtocolSendUnsequenced), sizeof(ENetProtocolBandwidthLimit), sizeof(ENetProtocolThrottleConfigure),
        sizeof(ENetProtocolSendFragment)};
    while (at < size) {
        if (size - at < sizeof(ENetProtocolCommandHeader) || ++count > ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS) return 0;
        uint32_t kind = data[at] & ENET_PROTOCOL_COMMAND_MASK;
        if (!kind || kind >= ENET_PROTOCOL_COMMAND_COUNT || sizes[kind] > size - at) return 0;
        uint32_t payload = 0;
        if (kind == ENET_PROTOCOL_COMMAND_SEND_RELIABLE) payload = ReadBE16(data + at + 4);
        else if (kind == ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE || kind == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED ||
                 kind == ENET_PROTOCOL_COMMAND_SEND_FRAGMENT || kind == ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT)
            payload = ReadBE16(data + at + 6);
        at += sizes[kind];
        if (payload > size - at) return 0;
        at += payload;
    }
    return at == size ? count : 0;
}
uint32_t ListCount(ENetList *list, uint32_t limit)
{
    uint32_t count = 0;
    for (auto i = enet_list_begin(list); i != enet_list_end(list); i = enet_list_next(i)) {
        if (++count > limit) break;
    }
    return count;
}
uint32_t IncomingMetadata(ENetHost *host)
{
    uint32_t count = 0;
    for (size_t i = 0; i < host->peerCount; ++i) {
        ENetPeer *p = &host->peers[i];
        count += ListCount(&p->acknowledgements, kIncomingMetadata);
        count += ListCount(&p->dispatchedCommands, kIncomingMetadata);
        for (size_t j = 0; j < p->channelCount; ++j) {
            count += ListCount(&p->channels[j].incomingReliableCommands, kIncomingMetadata);
            count += ListCount(&p->channels[j].incomingUnreliableCommands, kIncomingMetadata);
            if (count > kIncomingMetadata) return count;
        }
        if (count > kIncomingMetadata) return count;
    }
    return count;
}
int Ingress(CE_NetSession *s, ENetHost *host, uint64_t now)
{
    uint64_t elapsed = now >= s->ingressAt ? now - s->ingressAt : 0;
    s->ingressAt = now;
    s->packetCredit = elapsed >= 1000 ? uint64_t(kPacketBurst) * 1000 :
        std::min(uint64_t(kPacketBurst) * 1000, s->packetCredit + elapsed * kPacketRate);
    s->byteCredit = elapsed >= 1000 ? uint64_t(kByteBurst) * 1000 :
        std::min(uint64_t(kByteBurst) * 1000, s->byteCredit + elapsed * kByteRate);
    uint64_t bytes = host->receivedDataLength * 1000;
    if (s->packetCredit < 1000 || s->byteCredit < bytes) { Reject(s); return 1; }
    s->packetCredit -= 1000; s->byteCredit -= bytes;
    uint32_t commands = DatagramCommands(host->receivedData, uint32_t(host->receivedDataLength));
    if (!commands) { Reject(s); return 1; }
    // A command can allocate one incoming command/packet and one acknowledgement. Reserve both before ENet parses.
    // This bounds zero-payload out-of-order packets too; maximumWaitingData alone counts only payload bytes.
    uint32_t metadata = IncomingMetadata(host);
    if (metadata > kIncomingMetadata || commands * 2 > kIncomingMetadata - metadata) {
        s->interceptError = CE_NET_CAPACITY;
        return -1;
    }
    return 0;
}
int Intercept(ENetHost *host, ENetEvent *)
{
#if defined(CE_NETWORK_ICE)
    if (gPolling && gPolling->host == host && gPolling->ice) {
        const ENetAddress &source = gPolling->ice->LocalAddress();
        if (host->receivedAddress.host != source.host || host->receivedAddress.port != source.port) {
            Reject(gPolling);
            return 1;
        }
    }
#endif
    return gPolling && gPolling->host == host ? Ingress(gPolling, host, CE_NetNowMs()) : -1;
}
void RetireState(CE_NetSession *s)
{
    auto &a = s->assembly[Channel(CE_NET_STATE)];
    s->retiredState = std::max(s->retiredState, a.message);
    a.message = a.received = 0; a.seen.fill(0);
}
bool AssemblyExpired(const CE_NetSession *s, const Assembly &a, uint64_t now)
{
    uint32_t lifetime = a.kind == CE_NET_STATE ? std::min(kStateAssemblyMs, s->silenceMs) : s->silenceMs;
    return a.message && now >= a.started && now - a.started >= lifetime;
}
void ExpireAssemblies(CE_NetSession *s, uint64_t now)
{
    for (uint32_t i = 1; i < kChannels; ++i) {
        auto &a = s->assembly[i];
        if (AssemblyExpired(s, a, now)) {
            if (a.kind == CE_NET_STATE) RetireState(s);
            else { Fail(s, CE_NET_TIMEOUT); return; }
        }
    }
}
void ExpireBarrier(CE_NetSession *s, uint64_t now)
{
    if (s->status.phase == CE_NET_SYNCING && now >= s->syncStarted &&
        now - s->syncStarted >= std::min(s->handshakeMs, s->silenceMs)) Fail(s, CE_NET_TIMEOUT);
}

bool Assemble(CE_NetSession *s, const Header &h, const uint8_t *plain, uint32_t n, uint64_t now)
{
    if (h.epoch != s->status.epoch || h.message <= s->complete[h.channel] ||
        (h.kind == CE_NET_STATE && h.message <= s->retiredState)) { Reject(s); return false; }
    auto &a = s->assembly[h.channel];
    if (AssemblyExpired(s, a, now)) {
        if (a.kind != CE_NET_STATE) { Fail(s, CE_NET_TIMEOUT); return false; }
        RetireState(s);
        if (h.message <= s->retiredState) { Reject(s); return false; }
    }
    if (a.message && h.message < a.message) { Reject(s); return false; }
    if (!a.message || h.message > a.message) {
        if (h.kind == CE_NET_STATE && a.message) RetireState(s);
        a.message = h.message; a.kind = h.kind; a.epoch = h.epoch; a.tick = h.tick; a.total = h.total;
        a.parts = h.parts; a.received = 0; a.started = now; a.seen.fill(0);
    }
    if (a.kind != h.kind || a.epoch != h.epoch || a.tick != h.tick || a.total != h.total || a.parts != h.parts) {
        Fail(s, CE_NET_PROTOCOL); return false;
    }
    if (a.seen[h.part]) { Reject(s); return false; }
    if (n) std::memcpy(a.data.data() + h.part * kChunk, plain, n);
    a.seen[h.part] = 1; ++a.received;
    if (a.received != a.parts) return true;
    Queued *destination = nullptr;
    // Only completed STATE replaces an older completed snapshot; no partial state reaches the consumer.
    if (h.kind == CE_NET_STATE) {
        for (uint32_t i = 0; i < s->count; ++i) {
            Queued &q = s->queue[(s->head + i) % kQueue];
            if (q.meta.kind == h.kind) { destination = &q; break; }
        }
    }
    uint32_t replaced = destination ? destination->meta.size : 0;
    if ((!destination && s->count == kQueue) || h.total > kPendingBytes - (s->queuedBytes - replaced)) {
        if (h.kind == CE_NET_STATE) { RetireState(s); return true; }
        Fail(s, CE_NET_CAPACITY); return false;
    }
    if (!destination) { destination = &s->queue[(s->head + s->count) % kQueue]; ++s->count; }
    Queued &q = *destination;
    q.meta = {h.kind, h.epoch, h.tick, h.total};
    q.data.resize(h.total);
    if (h.total) std::memcpy(q.data.data(), a.data.data(), h.total);
    s->queuedBytes = s->queuedBytes - replaced + h.total;
    ++s->status.received_messages;
    s->complete[h.channel] = h.message;
    if (h.kind == CE_NET_STATE) RetireState(s); else a.message = 0;
    return true;
}

#if defined(CE_NETWORK_TEXT)
static void ReceiveText(CE_NetSession *s, const Header &h,
                        const uint8_t *plain, uint32_t n, uint64_t now) {
    if (s->status.phase != CE_NET_ACTIVE || h.epoch != s->status.epoch) {
        Reject(s); return;
    }
    CeTextChat::Decoded decoded{};
    const auto result = CeTextChat::Decode(plain, n, decoded);
    if (h.parts != 1 || h.part != 0 || h.total != n ||
        n > CE_NET_TEXT_MAX_FRAME || result != CeTextChat::Result::Ok) {
        CeTextChat::Clear(decoded);
        Fail(s, result == CeTextChat::Result::UpdateRequired ?
             CE_NET_INCOMPATIBLE : CE_NET_PROTOCOL);
        return;
    }
    if (decoded.sequence <= s->textLastReceived) {
        ++s->textInfo.dropped_duplicate; CeTextChat::Clear(decoded); return;
    }
    s->textLastReceived = decoded.sequence;
    CeTextChat::Clear(decoded);
    if (!s->textReceiveBudget.Allows(now)) { ++s->textInfo.dropped_rate; return; }
    if (s->textCount == CE_NET_TEXT_RECEIVE_CAPACITY) {
        ++s->textInfo.dropped_full; return;
    }
    CeTextQueued &slot = s->textQueue[(s->textHead + s->textCount) % CE_NET_TEXT_RECEIVE_CAPACITY];
    slot.epoch = h.epoch; slot.size = n; std::memcpy(slot.bytes, plain, n);
    ++s->textCount; ++s->textInfo.received; s->textReceiveBudget.Add(now);
}
#endif
void ReceivePacket(CE_NetSession *s, const uint8_t *wire, uint32_t size, uint32_t channel, uint64_t now)
{
    Header h{}; uint32_t n;
    if (!ReadHeader(wire, size, channel, h, n)) { Reject(s); return; }
    uint8_t plain[kChunk];
    if (h.kind == kHello && s->status.role == CE_NET_HOST && s->stage == 0 &&
        s->status.phase == CE_NET_AUTHENTICATING) {
        if (n != kHelloBytes || h.epoch || h.tick || !AllZero(h.session, 16) || !AllZero(wire + kHeader + n, kTag)) { Reject(s); return; }
        std::memcpy(s->clientNonce, wire + kHeader, 32);
        KeepPeerVersion(s, wire + kHeader + 64);
        if (crypto_verify32(wire + kHeader + 32, s->compatibility) != 0) {
            // Bind refusal to this fresh client nonce too: an old authenticated refusal cannot reject a new join.
            uint8_t refusal[kHelloBytes];
            std::memcpy(refusal, s->clientNonce, 32); std::memcpy(refusal + 32, s->compatibility, 32);
            std::memcpy(refusal + 64, s->version, CE_NET_VERSION_BYTES);
            if (!SendInternal(s, kRefuse, refusal, sizeof(refusal))) Fail(s, CE_NET_IO);
            // Remain bounded by handshake deadline; reliable refusal needs service calls to leave the queue.
            s->stage = kChallengeSent; s->status.error = CE_NET_INCOMPATIBLE;
            return;
        }
        uint8_t challenge[96];
        std::memcpy(challenge, s->clientNonce, 32); std::memcpy(challenge + 32, s->hostNonce, 32);
        std::memcpy(challenge + 64, s->compatibility, 32);
        DeriveKey(s);
        if (!SendInternal(s, kChallenge, challenge, sizeof(challenge))) { Fail(s, CE_NET_IO); return; }
        s->stage = kChallengeSent;
        return;
    }
    if ((h.kind == kChallenge || h.kind == kRefuse) && s->status.role == CE_NET_GUEST && s->stage == kHelloSent) {
        if (h.epoch || h.tick || AllZero(h.session, 16) ||
            !Decrypt(s, h, wire, n, plain, s->secret)) { Reject(s); return; }
        if (h.kind == kRefuse) {
            if (n == kHelloBytes && crypto_verify32(plain, s->clientNonce) == 0) { KeepPeerVersion(s, plain + 64); Fail(s, CE_NET_INCOMPATIBLE); }
            else Reject(s);
            return;
        }
        if (n != 96 || crypto_verify32(plain, s->clientNonce) != 0) { Reject(s); return; }
        if (crypto_verify32(plain + 64, s->compatibility) != 0) { Fail(s, CE_NET_INCOMPATIBLE); return; }
        std::memcpy(s->session, h.session, 16); std::memcpy(s->hostNonce, plain + 32, 32);
        DeriveKey(s);
        if (!s->replay[0].Add(h.sequence) || !SendInternal(s, kProof)) { Fail(s, CE_NET_IO); return; }
        s->stage = kProofSent; s->lastAuthenticated = now;
        return;
    }
    bool trafficReady = s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING ||
        (s->status.phase == CE_NET_AUTHENTICATING &&
         ((s->status.role == CE_NET_HOST && (s->stage == kAcceptSent ||
             (s->stage == kChallengeSent && s->status.error == CE_NET_OK))) ||
          (s->status.role == CE_NET_GUEST && s->stage == kProofSent)));
    if (!trafficReady || !SameSession(s, h) || !Decrypt(s, h, wire, n, plain, s->key)) { Reject(s); return; }
    if (!s->replay[channel].Add(h.sequence)) { Reject(s); return; }
    // Channels are independent: reliable INPUT can overtake reliable READY on channel zero. A valid first input
    // proves the already-PROOF-authenticated guest accepted the session too. Never acknowledge/drop that batch.
    if (s->status.phase == CE_NET_AUTHENTICATING && s->status.role == CE_NET_HOST && s->stage == kAcceptSent &&
        h.epoch == 1 && (h.kind == CE_NET_INPUT || h.kind == CE_NET_CONTROL)) {
        s->status.phase = CE_NET_ACTIVE; s->status.epoch = 1; s->lastPing = now;
    }
    // The same cross-channel race exists after an epoch barrier. A valid command tagged with our new epoch
    // acknowledges that transition; a heartbeat alone does not acknowledge it or extend the barrier deadline.
    if (s->status.phase == CE_NET_SYNCING && s->status.role == CE_NET_HOST && h.epoch == s->status.epoch &&
        (h.kind == CE_NET_INPUT || h.kind == CE_NET_CONTROL)) s->status.phase = CE_NET_ACTIVE;
    if (h.kind == kProof && s->status.phase == CE_NET_AUTHENTICATING && s->status.role == CE_NET_HOST &&
        s->stage == kChallengeSent && n == 0 &&
        h.epoch == 0 && s->status.error == CE_NET_OK) {
        if (!SendInternal(s, kAccept)) { Fail(s, CE_NET_IO); return; }
        s->stage = kAcceptSent; s->lastAuthenticated = now;
        return;
    }
    if (h.kind == kAccept && s->status.phase == CE_NET_AUTHENTICATING && s->status.role == CE_NET_GUEST &&
        s->stage == kProofSent && n == 0 && h.epoch == 0) {
        if (!SendInternal(s, kReady)) { Fail(s, CE_NET_IO); return; }
        s->status.phase = CE_NET_ACTIVE; s->status.epoch = 1; s->lastPing = s->lastAuthenticated = now;
        return;
    }
    if (h.kind == kReady && s->status.phase == CE_NET_AUTHENTICATING && s->status.role == CE_NET_HOST &&
        s->stage == kAcceptSent && n == 0 && h.epoch == 0) {
        s->status.phase = CE_NET_ACTIVE; s->status.epoch = 1; s->lastPing = s->lastAuthenticated = now;
        return;
    }
    if (s->status.phase != CE_NET_ACTIVE && s->status.phase != CE_NET_SYNCING) { Reject(s); return; }
    if (h.kind == kLeave && n == 0 && h.epoch == s->status.epoch) {
        CloseTransport(s, true);
        if (s->status.phase == CE_NET_CLOSED) s->status.error = CE_NET_PEER_LEFT;
        return;
    }
    if (h.kind == kEpoch && s->status.role == CE_NET_GUEST && n == 0 && s->status.epoch != UINT32_MAX &&
        h.epoch == s->status.epoch + 1) {
        ClearMessages(s); s->status.epoch = h.epoch; s->lastAuthenticated = now;
        if (!SendInternal(s, kEpochAck)) Fail(s, CE_NET_IO);
        return;
    }
    if (h.kind == kEpochAck && s->status.role == CE_NET_HOST && s->status.phase == CE_NET_SYNCING &&
        n == 0 && h.epoch == s->status.epoch) { s->status.phase = CE_NET_ACTIVE; s->lastAuthenticated = now; return; }
    if (h.kind == kPing && n == 0 && h.epoch == s->status.epoch) { s->lastAuthenticated = now; return; }
    if (!IsApp(h.kind) || s->status.phase != CE_NET_ACTIVE ||
        (s->status.role == CE_NET_HOST && (h.kind == CE_NET_STATE || h.kind == CE_NET_BASELINE ||
            h.kind == CE_NET_LIFECYCLE || h.kind == CE_NET_STATE_REPAIR)) ||
        (s->status.role == CE_NET_GUEST && h.kind == CE_NET_INPUT)) { Reject(s); return; }
#if defined(CE_NETWORK_TEXT)
    if (h.kind == CE_NET_TEXT) { ReceiveText(s, h, plain, n, now); return; }
#endif
    if (Assemble(s, h, plain, n, now)) s->lastAuthenticated = now;
}
}

uint64_t CE_NetNowMs(void)
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES])
{
    if (!out) return 0;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out, CE_NET_SECRET_BYTES, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) return 1;
#else
    FILE *f = std::fopen("/dev/urandom", "rb");
    if (f) { size_t n = std::fread(out, 1, CE_NET_SECRET_BYTES, f); std::fclose(f); if (n == CE_NET_SECRET_BYTES) return 1; }
#endif
    crypto_wipe(out, CE_NET_SECRET_BYTES);
    return 0;
}
#if defined(CE_NETWORK_ICE)
static CE_NetSession *CreateSession(const CE_NetConfig *c, bool ice)
#else
CE_NetSession *CE_NetCreate(const CE_NetConfig *c)
#endif
{
    if (!c || (c->role != CE_NET_HOST && c->role != CE_NET_GUEST) || !c->ipv4 || AllZero(c->secret, 32) ||
        (c->role == CE_NET_GUEST && !c->port
#if defined(CE_NETWORK_ICE)
         && !ice
#endif
        ) || (c->handshake_ms && (c->handshake_ms < 100 || c->handshake_ms > 180000)) ||
        (c->silence_ms && (c->silence_ms < 200 || c->silence_ms > 120000))) return nullptr;
    ENetAddress address{};
    if (enet_address_set_host_ip(&address, c->ipv4) != 0) return nullptr;
    if (gUsers == 0 && enet_initialize() != 0) return nullptr;
    ++gUsers;
    CE_NetSession *s = new(std::nothrow) CE_NetSession;
    if (!s) { if (!--gUsers) enet_deinitialize(); return nullptr; }
    s->status.role = c->role; s->status.local_player = c->role == CE_NET_HOST ? 0 : 1;
    s->status.phase = c->role == CE_NET_HOST ? CE_NET_WAITING : CE_NET_CONNECTING;
    std::memcpy(s->secret, c->secret, 32);
#if defined(CE_NETWORK_TEXT)
    const CE_MediaProfile textProfile{1, 1, 0, 0};
    if (CE_MediaProfileDeriveIdentity(s->compatibility, c->compatibility,
                                     &textProfile) != CE_MEDIA_PROFILE_TEXT) {
        CE_NetDestroy(s); return nullptr;
    }
#else
    std::memcpy(s->compatibility, c->compatibility, 32);
#endif
    for (uint32_t i = 0; i < CE_NET_VERSION_BYTES && c->version[i]; ++i) s->version[i] = c->version[i];
    if (c->handshake_ms) s->handshakeMs = c->handshake_ms;
    if (c->silence_ms) s->silenceMs = c->silence_ms;
    uint8_t random[32];
    if (!CE_NetGenerateSecret(s->clientNonce) || !CE_NetGenerateSecret(s->hostNonce) || !CE_NetGenerateSecret(random)) {
        CE_NetDestroy(s); return nullptr;
    }
    std::memcpy(s->session, random, 16); crypto_wipe(random, sizeof(random));
    if (c->role == CE_NET_GUEST) std::memset(s->session, 0, 16);
    address.port = c->port;
#if defined(CE_NETWORK_ICE)
    if (ice) s->host = enet_host_create(&address, 1, kChannels, 256 * 1024, 256 * 1024);
    else
#endif
    s->host = enet_host_create(c->role == CE_NET_HOST ? &address : nullptr, 1, kChannels, 256 * 1024, 256 * 1024);
    if (!s->host) { CE_NetDestroy(s); return nullptr; }
    s->host->mtu = CE_NET_MAX_DATAGRAM;
    s->host->maximumPacketSize = kWire;
    s->host->maximumWaitingData = kPendingBytes - kWire; // upstream checks before adding one last packet
    s->host->duplicatePeers = 1;
    s->host->intercept = Intercept;
    s->status.port = s->host->address.port;
    if (c->role == CE_NET_GUEST
#if defined(CE_NETWORK_ICE)
        && !ice
#endif
    ) {
        s->peer = enet_host_connect(s->host, &address, kChannels, kVersion);
        if (!s->peer) { CE_NetDestroy(s); return nullptr; }
    }
    s->started = s->lastPoll = s->lastAuthenticated = s->ingressAt = CE_NetNowMs();
    return s;
}
#if defined(CE_NETWORK_ICE)
CE_NetSession *CE_NetCreate(const CE_NetConfig *c)
{
    return CreateSession(c, false);
}
CE_NetSession *CE_NetCreateIcePending(const CE_NetConfig *c, uint32_t timeout)
{
    if (!c || (timeout && (timeout < 100 || timeout > 180000))) return nullptr;
    CE_NetConfig local = *c;
    local.ipv4 = "127.0.0.1"; local.port = 0;
    CE_NetSession *s = CreateSession(&local, true);
    crypto_wipe(local.secret, sizeof(local.secret));
    if (!s) return nullptr;
    s->iceWaiting = true;
    if (timeout) s->iceTimeoutMs = timeout;
    return s;
}
int CE_NetIceStart(CE_NetSession *s, const juice_config_t *ice)
{
    if (!s || !ice || !s->iceWaiting || s->ice || s->peer ||
        s->status.phase == CE_NET_CLOSED || s->status.phase == CE_NET_FAILED) return 0;
    const uint64_t now = CE_NetNowMs();
    const uint64_t elapsed = now - s->started;
    if (now < s->started || elapsed >= s->iceTimeoutMs) return 0;
    s->ice = CeIceBridge::Create(s->host, *ice);
    return s->ice ? 1 : 0;
}
CE_NetSession *CE_NetCreateIce(const CE_NetConfig *c, const juice_config_t *ice, uint32_t timeout)
{
    if (!ice) return nullptr;
    CE_NetSession *s = CE_NetCreateIcePending(c, timeout);
    if (!s) return nullptr;
    if (!CE_NetIceStart(s, ice)) { CE_NetDestroy(s); return nullptr; }
    return s;
}
int CE_NetIceLocalDescription(const CE_NetSession *s, char *out, uint32_t capacity)
{
    if (!out || !capacity) return -1;
    out[0] = 0;
    if (!s || !s->ice || s->status.phase == CE_NET_CLOSED || s->status.phase == CE_NET_FAILED) return 0;
    std::string description;
    if (!s->ice->LocalDescription(description)) return 0;
    if (description.size() >= capacity) return -1;
    std::memcpy(out, description.c_str(), description.size() + 1);
    return 1;
}
int CE_NetIceSetRemoteDescription(CE_NetSession *s, const char *description, uint32_t size)
{
    if (!s || !s->ice || !s->iceWaiting || s->status.phase == CE_NET_CLOSED ||
        s->status.phase == CE_NET_FAILED || !description || !size || size >= JUICE_MAX_SDP_STRING_LEN) return 0;
    return s->ice->SetRemoteDescription(std::string(description, size)) ? 1 : 0;
}
int CE_NetIceReady(const CE_NetSession *s)
{
    return s && s->ice && s->status.phase != CE_NET_CLOSED && s->status.phase != CE_NET_FAILED &&
        s->ice->Ready() ? 1 : 0;
}
int CE_NetIceUsesRelay(const CE_NetSession *s)
{
    return s && s->ice && s->status.phase != CE_NET_CLOSED && s->status.phase != CE_NET_FAILED &&
        s->ice->UsesRelay() ? 1 : 0;
}
int CE_NetIceProgress(const CE_NetSession *s, CE_NetIceProgressInfo *out)
{
    if (!out) return 0;
    *out = CE_NetIceProgressInfo{};
    if (!s || !s->ice || s->status.phase == CE_NET_CLOSED || s->status.phase == CE_NET_FAILED) return 0;
    out->gathered = s->ice->GatheringDone() ? 1 : 0;
    out->relay_candidates = s->ice->RelayCandidateCount();
    out->state = s->ice->StateIndex();
    return 1;
}
int CE_NetIceAttachExchange(CE_NetSession *s, void *context, const CE_NetIceExchangeCallbacks *callbacks)
{
    if (!s || !s->iceWaiting || s->iceExchange || !context || !callbacks ||
        !callbacks->poll || !callbacks->error || !callbacks->destroy ||
        s->status.phase == CE_NET_CLOSED || s->status.phase == CE_NET_FAILED) return 0;
    s->iceExchange = context;
    s->iceCallbacks = *callbacks;
    return 1;
}
const char *CE_NetIceProblem(const CE_NetSession *s)
{
    return s ? s->iceProblem : "";
}
#endif
// Bounded reliable close drain; no late application messages are dispatched.
static void DrainLocalClose(CE_NetSession *session)
{
    if (!session->host || !session->peer || session->status.phase != CE_NET_CLOSED ||
        session->status.error != CE_NET_OK) return;
    const ENetPeerState state = session->peer->state;
    if (state != ENET_PEER_STATE_CONNECTED && state != ENET_PEER_STATE_DISCONNECT_LATER &&
        state != ENET_PEER_STATE_DISCONNECTING) return;
    CE_NetSession *previous = gPolling; gPolling = session;
    enet_host_flush(session->host);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
#if defined(CE_NETWORK_ICE)
        if (session->ice && !session->ice->Pump()) break;
#endif
        ENetEvent event{};
        const int result = enet_host_service(session->host, &event, 10);
        if (result < 0) break;
        if (result > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            else if (event.type == ENET_EVENT_TYPE_DISCONNECT && event.peer == session->peer) break;
        }
        if (session->peer->state == ENET_PEER_STATE_DISCONNECTED ||
            session->peer->state == ENET_PEER_STATE_ZOMBIE) break;
    }
    gPolling = previous;
}
void CE_NetDestroy(CE_NetSession *s)
{
    if (!s) return;
    DrainLocalClose(s);
    // Destruction without Close is an abort, including cancellation during
    // connection/authentication. ENet loss notification carries no kLeave proof.
    if (s->peer && (s->status.phase == CE_NET_CONNECTING || s->status.phase == CE_NET_AUTHENTICATING ||
        s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING))
        enet_peer_disconnect_now(s->peer, 0);
#if defined(CE_NETWORK_ICE)
    if (s->ice) { s->ice->Pump(); s->ice.reset(); }
    RetireIceExchange(s);
#endif
    if (s->host) enet_host_destroy(s->host);
    crypto_wipe(s->secret, sizeof(s->secret)); crypto_wipe(s->key, sizeof(s->key));
    crypto_wipe(s->clientNonce, sizeof(s->clientNonce)); crypto_wipe(s->hostNonce, sizeof(s->hostNonce));
    delete s;
    if (!--gUsers) enet_deinitialize();
}
int CE_NetPeerVersion(const CE_NetSession *s, char *out, uint32_t capacity)
{
    if (!s || !out || !capacity || !s->peerVersion[0]) return 0;
    std::snprintf(out, capacity, "%s", s->peerVersion);
    return 1;
}
uint32_t CE_NetQuietMs(const CE_NetSession *s, uint64_t now)
{
    if (!s || (s->status.phase != CE_NET_ACTIVE && s->status.phase != CE_NET_SYNCING) || now < s->lastAuthenticated) return 0;
    return uint32_t(std::min<uint64_t>(now - s->lastAuthenticated, UINT32_MAX));
}
void CE_NetGetStatus(const CE_NetSession *s, CE_NetStatus *out)
{
    if (out) *out = s ? s->status : CE_NetStatus{CE_NET_FAILED, CE_NET_BAD_CONFIG, 0, 0, 0, 0, 0, 0, 0};
}
void CE_NetPoll(CE_NetSession *s, uint64_t now, uint32_t budget)
{
    if (!s || s->status.phase == CE_NET_CLOSED || s->status.phase == CE_NET_FAILED) return;
    if (now < s->lastPoll) { Fail(s, CE_NET_PROTOCOL); return; }
    s->lastPoll = now;
#if defined(CE_NETWORK_ICE)
    if (s->ice || s->iceWaiting) {
        if (s->iceWaiting && now - s->started >= s->iceTimeoutMs) {
            std::snprintf(s->iceProblem, sizeof(s->iceProblem), "%s",
                          "experimental relay preparation timed out; relaunch both games with fresh private lobby and game folders and repeat the private code and file exchange");
            Fail(s, CE_NET_TIMEOUT); return;
        }
        if (s->iceExchange) {
            uint32_t error = s->iceCallbacks.poll(s->iceExchange, s, now);
            if (error != CE_NET_OK) {
                const char *problem = s->iceCallbacks.error(s->iceExchange);
                std::snprintf(s->iceProblem, sizeof(s->iceProblem), "%s", problem ? problem : "relay exchange failed");
                Fail(s, error <= CE_NET_PEER_LEFT ? error : CE_NET_IO);
                return;
            }
        }
        // A pending grant owns no dependency agent and must not service ENet.
        if (!s->ice) return;
        if (!s->ice->Pump()) {
            std::snprintf(s->iceProblem, sizeof(s->iceProblem), "%s", "experimental relay connection failed");
            Fail(s, CE_NET_IO); return;
        }
        if (s->iceWaiting) {
            if (!s->ice->Ready()) return;
            if (s->status.role == CE_NET_GUEST) {
                s->peer = enet_host_connect(s->host, &s->ice->LocalAddress(), kChannels, kVersion);
                if (!s->peer) { Fail(s, CE_NET_IO); return; }
            }
            s->iceWaiting = false;
            s->started = s->lastAuthenticated = s->ingressAt = now;
        }
    }
#endif
    TimingBeforeService(s, now);
    ExpireBarrier(s, now);
    ExpireAssemblies(s, now);
    if (s->status.phase == CE_NET_FAILED) return;
    if (!budget) budget = 64;
    if (budget > 512) budget = 512;
    for (uint32_t i = 0; i < budget; ++i) {
        ENetEvent e{};
        CE_NetSession *previous = gPolling; gPolling = s;
        int result = enet_host_service(s->host, &e, 0);
        gPolling = previous;
        if (result < 0) { Fail(s, s->interceptError ? s->interceptError : CE_NET_IO); return; }
        if (result == 0) break;
        if (e.type == ENET_EVENT_TYPE_CONNECT) {
            s->peer = e.peer; s->status.phase = CE_NET_AUTHENTICATING; s->started = now;
            enet_peer_ping_interval(s->peer, 500);
            enet_peer_timeout(s->peer, 0, s->silenceMs, s->silenceMs);
            if (s->status.role == CE_NET_GUEST) {
                uint8_t hello[kHelloBytes]; std::memcpy(hello, s->clientNonce, 32); std::memcpy(hello + 32, s->compatibility, 32);
                std::memcpy(hello + 64, s->version, CE_NET_VERSION_BYTES);
                if (!SendInternal(s, kHello, hello, sizeof(hello))) { Fail(s, CE_NET_IO); return; }
                s->stage = kHelloSent;
            }
        } else if (e.type == ENET_EVENT_TYPE_RECEIVE) {
            if (e.peer == s->peer) ReceivePacket(s, e.packet->data, uint32_t(e.packet->dataLength), e.channelID, now);
            enet_packet_destroy(e.packet);
        } else if (e.type == ENET_EVENT_TYPE_DISCONNECT) { Fail(s, CE_NET_PEER_LEFT); return; }
        if (s->status.phase == CE_NET_FAILED || s->status.phase == CE_NET_CLOSED) return;
    }
    TimingAfterService(s, now);
    if (s->status.phase == CE_NET_WAITING) {
#if defined(CE_NETWORK_ICE)
        // A ready relay has an intended peer; it must also begin CE admission.
        if (s->ice && now - s->started >= s->handshakeMs) Fail(s, CE_NET_AUTH);
#endif
        return;
    }
    if (s->status.phase == CE_NET_CONNECTING || s->status.phase == CE_NET_AUTHENTICATING) {
        if (now - s->started >= s->handshakeMs) Fail(s, s->status.error ? s->status.error : CE_NET_AUTH);
        return;
    }
    if (now - s->lastAuthenticated >= s->silenceMs) { Fail(s, CE_NET_TIMEOUT); return; }
    if (now - s->lastPing >= std::min(1000u, s->silenceMs / 3)) {
        if (!SendInternal(s, kPing)) { Fail(s, CE_NET_CAPACITY); return; }
        s->lastPing = now;
    }
    ExpireAssemblies(s, now);
    if (s->status.phase == CE_NET_FAILED) return;
    enet_host_flush(s->host);
}
int CE_NetSend(CE_NetSession *s, uint32_t kind, uint32_t tick, const uint8_t *data, uint32_t size)
{
    if (kind == CE_NET_TEXT) return 0;

    if (!s || !IsApp(kind) || (!data && size) || size > CE_NET_MAX_MESSAGE ||
        ((kind == CE_NET_INPUT || kind == CE_NET_CONTROL) && size > kChunk) ||
        (kind == CE_NET_LIFECYCLE && size > CE_NET_MAX_LIFECYCLE) ||
        (kind == CE_NET_STATE_CONTROL && size != CE_NET_STATE_CONTROL_BYTES) ||
        (kind == CE_NET_STATE_REPAIR && !size) ||
        (s->status.role == CE_NET_HOST && kind == CE_NET_INPUT) ||
        (s->status.role == CE_NET_GUEST && (kind == CE_NET_STATE || kind == CE_NET_BASELINE ||
            kind == CE_NET_LIFECYCLE || kind == CE_NET_STATE_REPAIR))) return -1;
    if (s->status.phase != CE_NET_ACTIVE) return 0;
    // ENet promotes oversized UNSEQUENCED packets to reliable fragments. Never silently change STATE semantics.
    if (kind == CE_NET_STATE && s->peer) {
        size_t overhead = sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendFragment) +
            (s->host->checksum ? sizeof(enet_uint32) : 0);
        size_t largest = kHeader + std::min(kChunk, size) + kTag;
        if (s->peer->mtu < overhead || largest > s->peer->mtu - overhead) {
            Fail(s, CE_NET_PROTOCOL); return 0;
        }
    }
    uint32_t channel = Channel(kind), parts = std::max(1u, (size + kChunk - 1) / kChunk);
    uint32_t bytes = size + parts * (kHeader + kTag);
    uint32_t applicationBytes = kPendingBytes - kControlReserve * kWire;
    if (s->pendingPackets > kPendingPackets - kControlReserve - parts || s->pendingBytes > applicationBytes ||
        bytes > applicationBytes - s->pendingBytes) return 0;
    if (s->messageSequence[channel] == UINT32_MAX || s->sendSequence[channel] > UINT32_MAX - parts) {
        Fail(s, CE_NET_PROTOCOL); return 0;
    }
    Header h{};
    h.kind = kind; h.channel = channel; h.epoch = s->status.epoch; h.tick = tick;
    h.message = ++s->messageSequence[channel]; h.total = size; h.parts = parts;
    std::memcpy(h.session, s->session, 16);
    for (uint32_t i = 0; i < parts; ++i) {
        h.part = i;
        uint32_t n = std::min(kChunk, size - i * kChunk);
        if (!QueuePacket(s, h, data ? data + i * kChunk : nullptr, n, s->key)) { Fail(s, CE_NET_IO); return 0; }
    }
    ++s->status.sent_messages;
    enet_host_flush(s->host);
    return 1;
}
int CE_NetReceive(CE_NetSession *s, CE_NetMessage *message, uint8_t *out, uint32_t capacity)
{
    if (!s || !message || !s->count) return 0;
    Queued &q = s->queue[s->head];
    if (q.meta.size > capacity || (!out && q.meta.size)) return -1;
    *message = q.meta;
    if (q.meta.size) std::memcpy(out, q.data.data(), q.meta.size);
    s->queuedBytes -= q.meta.size;
    std::vector<uint8_t>().swap(q.data);
    s->head = (s->head + 1) % kQueue; --s->count;
    return 1;
}
int CE_NetBeginEpoch(CE_NetSession *s, uint32_t epoch)
{
    if (!s || s->status.role != CE_NET_HOST || s->status.phase != CE_NET_ACTIVE ||
        s->status.epoch == UINT32_MAX || epoch != s->status.epoch + 1) return 0;
    ClearMessages(s); s->status.epoch = epoch; s->status.phase = CE_NET_SYNCING; s->syncStarted = CE_NetNowMs();
    if (!SendInternal(s, kEpoch)) { Fail(s, CE_NET_IO); return 0; }
    return 1;
}
void CE_NetClose(CE_NetSession *s)
{
    CloseTransport(s, false);
}


#if defined(CE_NETWORK_TEXT)
static uint32_t TextFrameLimit(uint32_t mtu, bool checksum) {
    const uint32_t overhead = sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendFragment) +
                              (checksum ? sizeof(enet_uint32) : 0) + kHeader + kTag;
    return mtu > overhead ? std::min<uint32_t>(CE_NET_TEXT_MAX_FRAME, mtu - overhead) : 0;
}
#endif

uint32_t CE_NetTextMaxFrame(const CE_NetSession *s) {
#if defined(CE_NETWORK_TEXT)
    if (!s || !s->peer || !s->host || s->status.phase != CE_NET_ACTIVE) return 0;
    return TextFrameLimit(s->peer->mtu, s->host->checksum != nullptr);
#else
    (void)s; return 0;
#endif
}

uint32_t CE_NetTextSend(CE_NetSession *s, uint32_t epoch, const void *frame,
                       uint32_t size, uint64_t now) {
#if defined(CE_NETWORK_TEXT)
    if (!s || s->status.phase != CE_NET_ACTIVE) return CE_NET_TEXT_CLOSED;
    if (epoch != s->status.epoch) return CE_NET_TEXT_INVALID;
    const uint32_t maximum = CE_NetTextMaxFrame(s);
    if (size > maximum || size > CE_NET_TEXT_MAX_FRAME) return CE_NET_TEXT_TOO_LARGE;
    CeTextChat::Decoded decoded{};
    if (CeTextChat::Decode(static_cast<const uint8_t*>(frame), size, decoded) != CeTextChat::Result::Ok)
        return CE_NET_TEXT_INVALID;
    const uint64_t sequence = decoded.sequence; CeTextChat::Clear(decoded);
    if (sequence <= s->textLastSent) return CE_NET_TEXT_INVALID;
    const uint32_t wire = kHeader + size + kTag;
    const uint32_t reserve = kControlReserve + 1;
    if (s->textPending || s->count ||
        !enet_list_empty(&s->peer->outgoingCommands) ||
        !enet_list_empty(&s->peer->outgoingSendReliableCommands))
        return CE_NET_TEXT_WOULD_BLOCK;
    if (s->pendingPackets + 1 > kPendingPackets - reserve ||
        wire > kPendingBytes - reserve * kWire ||
        s->pendingBytes > kPendingBytes - reserve * kWire - wire)
        return CE_NET_TEXT_WOULD_BLOCK;
    if (!s->textSendBudget.Clock(now)) return CE_NET_TEXT_CLOCK_REGRESSION;
    if (!s->textSendBudget.Allows(now)) return CE_NET_TEXT_RATE_LIMITED;
    Header h{}; h.kind = CE_NET_TEXT; h.channel = CE_NET_TEXT_CHANNEL;
    std::memcpy(h.session, s->session, sizeof(h.session));
    h.epoch = epoch; h.message = ++s->messageSequence[CE_NET_TEXT_CHANNEL];
    h.total = size; h.parts = 1;
    if (!QueuePacket(s, h, static_cast<const uint8_t*>(frame), size, s->key))
        return CE_NET_TEXT_WOULD_BLOCK;
    s->textLastSent = sequence; s->textSendBudget.Add(now); ++s->textInfo.sent;
    enet_host_flush(s->host);
    return CE_NET_TEXT_OK;
#else
    (void)s; (void)epoch; (void)frame; (void)size; (void)now;
    return CE_NET_TEXT_DISABLED;
#endif
}

uint32_t CE_NetTextReceive(CE_NetSession *s, uint32_t *epoch, void *frame,
                          uint32_t capacity, uint32_t *size) {
    if (epoch) *epoch = 0;
    if (size) *size = 0;
    if (frame) std::memset(frame, 0, std::min<uint32_t>(capacity, CE_NET_TEXT_MAX_FRAME));
#if defined(CE_NETWORK_TEXT)
    if (!s || s->status.phase != CE_NET_ACTIVE) return CE_NET_TEXT_CLOSED;
    if (!epoch || !frame || !size) return CE_NET_TEXT_INVALID;
    if (!s->textCount) return CE_NET_TEXT_EMPTY;
    CeTextQueued &slot = s->textQueue[s->textHead];
    if (capacity < slot.size) return CE_NET_TEXT_TOO_LARGE;
    *epoch = slot.epoch; *size = slot.size; std::memcpy(frame, slot.bytes, slot.size);
    crypto_wipe(&slot, sizeof(slot));
    s->textHead = (s->textHead + 1) % CE_NET_TEXT_RECEIVE_CAPACITY; --s->textCount;
    return CE_NET_TEXT_OK;
#else
    (void)s; return CE_NET_TEXT_DISABLED;
#endif
}

void CE_NetTextGetInfo(const CE_NetSession *s, CE_NetTextInfo *out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out)); out->phase = CE_NET_CLOSED;
#if defined(CE_NETWORK_TEXT)
    if (!s) return;
    *out = s->textInfo; out->phase = s->status.phase;
    out->max_frame = CE_NetTextMaxFrame(s); out->queued = s->textCount; out->pending = s->textPending;
#else
    (void)s;
#endif
}
