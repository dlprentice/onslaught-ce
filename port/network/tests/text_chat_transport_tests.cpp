// SPDX-License-Identifier: GPL-3.0-only
// Actual transport and ENet, invented data and owned loopback sockets only.
#include "../transport.cpp"
#include <string>
#include <thread>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}

struct Pair {
    CE_NetSession *a = nullptr, *b = nullptr;
    CE_NetConfig base{};
    uint64_t now = 0;
    Pair(bool minimum = false, bool checksum = false) {
        base.role = CE_NET_HOST; base.ipv4 = "127.0.0.1";
        base.silence_ms = 120000; base.handshake_ms = 5000;
        std::strcpy(base.version, "text-fixture");
        for (unsigned i = 0; i != 32; ++i) {
            base.secret[i] = static_cast<uint8_t>(i + 1);
            base.compatibility[i] = static_cast<uint8_t>(i + 33);
        }
        a = CE_NetCreate(&base);
        if (!a) return;
        // Factory settings before any peer is connected. Never overwrite a
        // negotiated peer MTU, window, throttle or reliable-flight counter.
        if (minimum) a->host->mtu = 576;
        if (checksum) a->host->checksum = enet_crc32;
        CE_NetStatus status{}; CE_NetGetStatus(a, &status);
        CE_NetConfig guest = base; guest.role = CE_NET_GUEST; guest.port = status.port;
        b = CE_NetCreate(&guest);
        if (b && checksum) b->host->checksum = enet_crc32;
        crypto_wipe(guest.secret, sizeof(guest.secret));
    }
    ~Pair() { CE_NetDestroy(b); CE_NetDestroy(a); crypto_wipe(&base, sizeof(base)); }
    void Step(bool poll_a = true, bool poll_b = true, uint64_t at = 0) {
        now = at ? at : CE_NetNowMs();
        if (poll_a && a) CE_NetPoll(a, now, 64);
        if (poll_b && b) CE_NetPoll(b, now, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bool Ready() {
        if (!a || !b) return false;
        bool epoch_started = false;
        for (unsigned i = 0; i != 4000; ++i) {
            Step();
            if (a->status.phase == CE_NET_FAILED || b->status.phase == CE_NET_FAILED)
                return false;
            if (!epoch_started && a->status.phase == CE_NET_ACTIVE &&
                b->status.phase == CE_NET_ACTIVE) {
                if (a->status.epoch == 0) {
                    if (!CE_NetBeginEpoch(a, 1)) return false;
                    epoch_started = true;
                } else epoch_started = true;
            }
            if (epoch_started && a->status.phase == CE_NET_ACTIVE &&
                b->status.phase == CE_NET_ACTIVE && a->status.epoch == b->status.epoch) {
                for (unsigned j = 0; j != 30; ++j) Step();
                return true;
            }
        }
        return false;
    }
};

CeTextChat::Frame Frame(uint64_t sequence, std::size_t bytes = 3) {
    std::string text;
    for (std::size_t i = 0; i != bytes / 2; ++i) text += u8"é";
    if (bytes % 2) text += 'x';
    CeTextChat::Frame frame{};
    Check(CeTextChat::Encode(reinterpret_cast<const uint8_t*>(text.data()),
                            text.size(), sequence, frame) == CeTextChat::Result::Ok,
          "authored-frame-valid");
    return frame;
}

unsigned DrainCritical(CE_NetSession* session) {
    unsigned count = 0; CE_NetMessage message{}; uint8_t bytes[2048];
    while (CE_NetReceive(session, &message, bytes, sizeof(bytes))) {
        Check(message.kind == CE_NET_INPUT || message.kind == CE_NET_CONTROL,
              "text-never-enters-gameplay-storage");
        ++count;
    }
    return count;
}

unsigned DrainText(CE_NetSession* session) {
    unsigned count = 0; uint32_t epoch = 0, size = 0;
    uint8_t bytes[CE_NET_TEXT_MAX_FRAME]{};
    while (CE_NetTextReceive(session, &epoch, bytes, sizeof(bytes), &size) == CE_NET_TEXT_OK) {
        CeTextChat::Decoded decoded{};
        Check(CeTextChat::Decode(bytes, size, decoded) == CeTextChat::Result::Ok,
              "received-text-is-valid");
        Check(epoch == session->status.epoch, "received-epoch-current");
        CeTextChat::Clear(decoded); ++count;
    }
    crypto_wipe(bytes, sizeof(bytes));
    return count;
}

void BidirectionalProgress() {
    Pair pair;
    Check(pair.Ready(), "paired-authenticated-active"); if (!pair.a || !pair.b ||
        pair.a->status.phase != CE_NET_ACTIVE || pair.b->status.phase != CE_NET_ACTIVE) return;
    uint8_t derived[32]{}; const CE_MediaProfile profile{1, 1, 0, 0};
    Check(CE_MediaProfileDeriveIdentity(derived, pair.base.compatibility, &profile) ==
          CE_MEDIA_PROFILE_TEXT, "profile-oracle");
    Check(std::memcmp(pair.a->compatibility, derived, sizeof(derived)) == 0 &&
          std::memcmp(pair.base.compatibility, derived, sizeof(derived)) != 0,
          "derived-owned-handshake-base-retained");
    crypto_wipe(derived, sizeof(derived));
    auto frame = Frame(1);
    Check(CE_NetSend(pair.a, CE_NET_TEXT, 0, frame.bytes, frame.size) == 0,
          "generic-send-cannot-bypass-text-admission");
    unsigned a_critical = 0, b_critical = 0, a_text = 0, b_text = 0;
    for (unsigned i = 0; i != 40; ++i) {
        const uint8_t value = static_cast<uint8_t>(i);
        Check(CE_NetSend(pair.a, CE_NET_CONTROL, i, &value, 1) != 0, "forward-control-accepted");
        Check(CE_NetSend(pair.b, CE_NET_INPUT, i, &value, 1) != 0, "reverse-input-accepted");
        pair.Step();
        a_critical += DrainCritical(pair.a); b_critical += DrainCritical(pair.b);
        if (i == 10 || i == 25) {
            frame = Frame(i == 10 ? 1 : 2);
            Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                                pair.now) == CE_NET_TEXT_OK, "forward-text-accepted");
            Check(CE_NetTextSend(pair.b, pair.b->status.epoch, frame.bytes, frame.size,
                                pair.now) == CE_NET_TEXT_OK, "reverse-text-accepted");
        }
        pair.Step(); a_text += DrainText(pair.a); b_text += DrainText(pair.b);
    }
    for (unsigned i = 0; i != 40; ++i) {
        pair.Step(); a_critical += DrainCritical(pair.a); b_critical += DrainCritical(pair.b);
        a_text += DrainText(pair.a); b_text += DrainText(pair.b);
    }
    Check(a_critical == 40 && b_critical == 40, "all-critical-progresses-with-text");
    Check(a_text == 2 && b_text == 2, "nonzero-bidirectional-text-delivery");
    std::printf("OBS {\"case\":\"bidirectional\",\"a_critical\":%u,\"b_critical\":%u,\"a_text\":%u,\"b_text\":%u,\"a_rejected\":%u,\"b_rejected\":%u,\"a_text_received\":%llu,\"b_text_received\":%llu}\n",
                a_critical, b_critical, a_text, b_text, pair.a->status.rejected_packets,
                pair.b->status.rejected_packets,
                static_cast<unsigned long long>(pair.a->textInfo.received),
                static_cast<unsigned long long>(pair.b->textInfo.received));
    Check(TextPacketFlags(CE_NET_TEXT, ENET_PACKET_FLAG_RELIABLE) ==
          ENET_PACKET_FLAG_UNSEQUENCED, "text-is-never-reliable-or-fragmenting");
    CeTextChat::Clear(frame);
}

void HeldAcknowledgements() {
    Pair pair; Check(pair.Ready(), "held-ack-pair");
    if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
    const uint8_t value = 1;
    Check(CE_NetSend(pair.a, CE_NET_CONTROL, 1, &value, 1) != 0, "held-ack-critical-send");
    for (unsigned i = 0; i != 5; ++i) pair.Step(false, true);
    std::printf("OBS {\"case\":\"held_ack\",\"pending\":%u,\"flight\":%u,\"unsent\":%u,\"b_queued\":%u,\"b_rejected\":%u}\n",
                pair.a->pendingPackets, pair.a->peer->reliableDataInTransit,
                static_cast<unsigned>(enet_list_size(&pair.a->peer->outgoingCommands) +
                                      enet_list_size(&pair.a->peer->outgoingSendReliableCommands)),
                pair.b->count, pair.b->status.rejected_packets);
    Check(DrainCritical(pair.b) == 1 && pair.a->peer->reliableDataInTransit > 0,
          "actual-critical-flight-without-ack-processing");
    const auto flight = pair.a->peer->reliableDataInTransit;
    auto frame = Frame(1);
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        CE_NetNowMs()) == CE_NET_TEXT_OK, "held-acks-do-not-block-text");
    Check(pair.a->peer->reliableDataInTransit == flight, "text-consumes-no-reliable-flight");
    for (unsigned i = 0; i != 10; ++i) pair.Step(false, true);
    Check(DrainText(pair.b) == 1, "text-received-while-critical-ack-held");
    CeTextChat::Clear(frame);
}

void UnsentPriorityAndReserve() {
    {
        Pair pair; Check(pair.Ready(), "unsent-priority-pair");
        if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
        uint8_t payload[1000]{};
        for (unsigned i = 0; i != 20; ++i) {
            Check(CE_NetSend(pair.a, CE_NET_CONTROL, i, payload, sizeof(payload)) != 0,
                  "critical-window-fill");
            pair.Step(false, true); DrainCritical(pair.b);
        }
        Check(!enet_list_empty(&pair.a->peer->outgoingCommands) ||
              !enet_list_empty(&pair.a->peer->outgoingSendReliableCommands),
              "real-unsent-critical-queue-established");
        auto frame = Frame(1); const auto before = pair.a->sendSequence[CE_NET_TEXT_CHANNEL];
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            CE_NetNowMs()) == CE_NET_TEXT_WOULD_BLOCK,
              "text-defers-behind-unsent-critical");
        Check(pair.a->sendSequence[CE_NET_TEXT_CHANNEL] == before &&
              pair.a->textSendBudget.count == 0, "priority-refusal-consumes-no-text-state");
        CeTextChat::Clear(frame);
    }
    {
        Pair pair; Check(pair.Ready(), "reserve-pair");
        if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
        const uint8_t payload = 1;
        for (unsigned i = 0; i != kPendingPackets - kControlReserve - 1; ++i) {
            Check(CE_NetSend(pair.a, CE_NET_CONTROL, i, &payload, 1) != 0,
                  "actual-small-critical-fill");
            pair.Step(false, true); DrainCritical(pair.b);
        }
        Check(pair.a->pendingPackets == kPendingPackets - kControlReserve - 1 &&
              enet_list_empty(&pair.a->peer->outgoingCommands) &&
              enet_list_empty(&pair.a->peer->outgoingSendReliableCommands),
              "reserve-case-is-sent-flight-not-unsent-priority");
        auto frame = Frame(1);
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            CE_NetNowMs()) == CE_NET_TEXT_WOULD_BLOCK,
              "text-retains-internal-plus-critical-reserve");
        Check(CE_NetSend(pair.a, CE_NET_CONTROL, 999, &payload, 1) != 0,
              "one-more-critical-packet-still-accepted");
        pair.Step(false, true); Check(DrainCritical(pair.b) == 1, "reserved-critical-progresses");
        CeTextChat::Clear(frame);
    }
}

void NegotiatedMtu(bool checksum) {
    Pair pair(true, checksum); Check(pair.Ready(), "minimum-mtu-pair");
    if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
    Check(pair.a->peer->mtu == 576 && pair.b->peer->mtu == 576,
          "minimum-mtu-genuinely-negotiated");
    const uint32_t maximum = CE_NetTextMaxFrame(pair.a);
    Check(maximum == (checksum ? 480u : 484u), "negotiated-frame-limit-includes-overhead");
    auto too_large = Frame(1, maximum - CeTextChat::HeaderBytes + 1);
    const auto sequence = pair.a->sendSequence[CE_NET_TEXT_CHANNEL];
    const auto message = pair.a->messageSequence[CE_NET_TEXT_CHANNEL];
    const auto pending = pair.a->pendingPackets;
    for (unsigned i = 0; i != 8; ++i)
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, too_large.bytes, too_large.size,
                            CE_NetNowMs()) == CE_NET_TEXT_TOO_LARGE,
              "one-byte-over-refused-before-admission");
    Check(pair.a->sendSequence[CE_NET_TEXT_CHANNEL] == sequence &&
          pair.a->messageSequence[CE_NET_TEXT_CHANNEL] == message &&
          pair.a->pendingPackets == pending && pair.a->textSendBudget.count == 0 &&
          pair.a->textLastSent == 0, "mtu-refusal-leaves-sequence-quota-enet-unchanged");
    auto exact = Frame(1, maximum - CeTextChat::HeaderBytes);
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, exact.bytes, exact.size,
                        CE_NetNowMs()) == CE_NET_TEXT_OK, "exact-fit-text-accepted");
    for (unsigned i = 0; i != 20; ++i) pair.Step();
    Check(DrainText(pair.b) == 1, "exact-fit-text-received");
    CeTextChat::Clear(exact); CeTextChat::Clear(too_large);
}

void ActualSendBudgetAndEpoch() {
    Pair pair; Check(pair.Ready(), "budget-pair");
    if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
    const uint64_t start = CE_NetNowMs();
    for (uint64_t i = 1; i != 5; ++i) {
        auto frame = Frame(i);
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            start) == CE_NET_TEXT_OK, "actual-send-burst-accepted");
        pair.Step(true, true, start); DrainText(pair.b); CeTextChat::Clear(frame);
    }
    auto frame = Frame(5);
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        start) == CE_NET_TEXT_RATE_LIMITED,
          "backlog-cannot-bypass-actual-send-budget");
    const auto old_epoch = pair.a->status.epoch;
    Check(CE_NetBeginEpoch(pair.a, old_epoch + 1) != 0, "epoch-started");
    for (unsigned i = 0; i != 30; ++i) pair.Step(true, true, start + 1);
    Check(pair.a->status.phase == CE_NET_ACTIVE && pair.b->status.phase == CE_NET_ACTIVE,
          "epoch-active-again");
    frame = Frame(1);
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        start + 1) == CE_NET_TEXT_RATE_LIMITED,
          "epoch-does-not-refill-transport-rate-budget");
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        start + 2101) == CE_NET_TEXT_OK, "transport-budget-expires-normally");
    pair.Step(true, true, start + 2101); DrainText(pair.b);
    for (uint64_t sequence = 2; sequence != 5; ++sequence) {
        frame = Frame(sequence);
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            start + 2101) == CE_NET_TEXT_OK, "second-transport-burst");
        pair.Step(true, true, start + 2101); DrainText(pair.b);
    }
    for (uint64_t sequence = 5; sequence != 9; ++sequence) {
        frame = Frame(sequence);
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            start + 4202) == CE_NET_TEXT_OK, "third-transport-burst");
        pair.Step(true, true, start + 4202); DrainText(pair.b);
    }
    frame = Frame(9);
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        start + 6503) == CE_NET_TEXT_RATE_LIMITED,
          "actual-send-long-window-budget");
    Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                        start + 4201) == CE_NET_TEXT_CLOCK_REGRESSION,
          "actual-send-backward-clock-refused");
    CeTextChat::Clear(frame);
}

void UnreadAndMalformed() {
    Pair pair; Check(pair.Ready(), "unread-pair");
    if (!pair.a || !pair.b || pair.a->status.phase != CE_NET_ACTIVE) return;
    const uint64_t start = CE_NetNowMs();
    for (uint64_t i = 1; i != 18; ++i) {
        auto frame = Frame(i);
        const auto now = start + i * 3000;
        Check(CE_NetTextSend(pair.a, pair.a->status.epoch, frame.bytes, frame.size,
                            now) == CE_NET_TEXT_OK, "paced-unread-send");
        for (unsigned j = 0; j != 2; ++j) pair.Step(true, true, now);
        CeTextChat::Clear(frame);
    }
    Check(pair.b->textCount == CE_NET_TEXT_RECEIVE_CAPACITY &&
          pair.b->textInfo.dropped_full == 1 && pair.b->count == 0,
          "private-unread-storage-bounded-without-gameplay-queue");
    std::printf("OBS {\"case\":\"unread\",\"queued\":%u,\"received\":%llu,\"full\":%llu,\"rejected\":%u,\"phase\":%u}\n",
                pair.b->textCount, static_cast<unsigned long long>(pair.b->textInfo.received),
                static_cast<unsigned long long>(pair.b->textInfo.dropped_full),
                pair.b->status.rejected_packets, pair.b->status.phase);
    auto malformed = Frame(18); malformed.bytes[4] = 2;
    Header h{}; h.kind = CE_NET_TEXT; h.channel = CE_NET_TEXT_CHANNEL;
    std::memcpy(h.session, pair.a->session, sizeof(h.session));
    h.epoch = pair.a->status.epoch; h.message = ++pair.a->messageSequence[CE_NET_TEXT_CHANNEL];
    h.total = malformed.size; h.parts = 1;
    Check(QueuePacket(pair.a, h, malformed.bytes, malformed.size, pair.a->key),
          "authored-authenticated-malformation-injected");
    enet_host_flush(pair.a->host);
    for (unsigned i = 0; i != 10; ++i) pair.Step(true, true, start + 52000);
    Check(pair.b->status.phase == CE_NET_FAILED && pair.b->status.error == CE_NET_INCOMPATIBLE,
          "full-text-queue-does-not-hide-schema-failure");
    CeTextChat::Clear(malformed);
}
} // namespace


void TextPacketOwnership()
{
    Pair pair;
    Check(pair.Ready(), "ownership-pair-active");
    for (unsigned i=0;i<40&&pair.a->pendingPackets;++i) pair.Step();
    Check(pair.a->pendingPackets==0, "ownership-clean-start");
    auto packetAtHead=[&]()->ENetPacket*{
        ENetList *commands=&pair.a->peer->outgoingCommands;
        if(enet_list_empty(commands))return nullptr;
        return reinterpret_cast<ENetOutgoingCommand *>(enet_list_begin(commands))->packet;
    };
    Header state{};state.kind=CE_NET_STATE;state.epoch=pair.a->status.epoch;
    state.parts=1;state.total=1;std::memcpy(state.session,pair.a->session,sizeof(state.session));
    const uint8_t byte=1;
    Check(QueuePacket(pair.a,state,&byte,1,pair.a->key), "ownership-state-queued");
    ENetPacket *state_packet=packetAtHead();
    const auto frame=Frame(1);
    Header text{};text.kind=CE_NET_TEXT;text.epoch=pair.a->status.epoch;
    text.parts=1;text.total=frame.size;std::memcpy(text.session,pair.a->session,sizeof(text.session));
    Check(QueuePacket(pair.a,text,frame.bytes,frame.size,pair.a->key), "ownership-text-queued");
    Check(state_packet&&state_packet->freeCallback&&pair.a->textPending==1&&pair.a->pendingPackets==2,
          "ownership-both-pending");
    if(state_packet&&state_packet->freeCallback){
        // Model STATE's callback running first without invalidating ENet's held packet.
        state_packet->freeCallback(state_packet);state_packet->freeCallback=nullptr;
    }
    Check(pair.a->textPending==1&&pair.a->pendingPackets==1,
          "state-free-preserves-text-ownership");
}

void CloseRetiresOnlyText()
{
    CE_NetClose(nullptr);CloseTransport(nullptr,true);
    Check(true, "null-close-is-inert");
    Pair pair;
    Check(pair.Ready(), "close-text-pair-active");
    const uint8_t byte=1;
    Check(CE_NetSend(pair.a,CE_NET_CONTROL,1,&byte,1)!=0, "close-critical-queued");
    const auto frame=Frame(1);
    uint32_t accepted=CE_NET_TEXT_WOULD_BLOCK;
    for(unsigned i=0;i<100&&accepted!=CE_NET_TEXT_OK;++i){
        pair.Step();accepted=CE_NetTextSend(pair.a,pair.a->status.epoch,frame.bytes,frame.size,CE_NetNowMs());
    }
    Check(accepted==CE_NET_TEXT_OK, "close-text-accepted");
    for(unsigned i=0;i<100&&(!pair.b->textCount||!pair.b->count);++i)pair.Step();
    Check(pair.b->textCount==1&&pair.b->count==1, "close-both-unread");
    const auto receive_budget=pair.b->textReceiveBudget;
    CloseTransport(pair.b,true);
    Check(pair.b->status.phase==CE_NET_CLOSED&&pair.b->textCount==0&&pair.b->textHead==0,
          "close-wipes-private-text-ring");
    bool zero=true;
    for(const auto &entry:pair.b->textQueue)
        for(const auto byte_value:entry.bytes)zero=zero&&byte_value==0;
    Check(zero, "close-wipes-text-bytes");
    bool retained_critical=false;
    CE_NetMessage message{};uint8_t bytes[4096]{};
    while(CE_NetReceive(pair.b,&message,bytes,sizeof(bytes)))
        retained_critical=retained_critical||(message.kind==CE_NET_CONTROL&&message.tick==1&&message.size==1&&bytes[0]==byte);
    Check(retained_critical, "close-preserves-completed-critical");
    Check(std::memcmp(&receive_budget,&pair.b->textReceiveBudget,sizeof(receive_budget))==0,
          "close-preserves-text-rate-window");
    pair.b->textCount=1;pair.b->textQueue[0].bytes[0]=1;
    CloseTransport(pair.b,true);
    Check(pair.b->textCount==0&&pair.b->textQueue[0].bytes[0]==0,
          "already-closed-wipes-private-text-ring");
}

int main() {
    BidirectionalProgress(); HeldAcknowledgements(); UnsentPriorityAndReserve();
    NegotiatedMtu(false);
    Check(TextFrameLimit(576, true) == 480, "checksum-overhead-arithmetic");
    ActualSendBudgetAndEpoch();
    UnreadAndMalformed();
    TextPacketOwnership();
    CloseRetiresOnlyText();
    std::printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
