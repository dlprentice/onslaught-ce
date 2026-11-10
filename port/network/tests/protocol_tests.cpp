// SPDX-License-Identifier: GPL-3.0-only
// Authored white-box adversarial controls supplement the actual-process tests (ENet may discard old UDP frames first).
#include "../transport.cpp"
#include "fixture_guard.h"
#include <cassert>
#include <memory>
#include <thread>

using Session = std::unique_ptr<CE_NetSession>;
static Session Active(uint32_t role = CE_NET_HOST)
{
    Session s(new CE_NetSession);
    s->status.role = role; s->status.phase = CE_NET_ACTIVE; s->status.epoch = 1;
    for (int i = 0; i < 32; ++i) { s->key[i] = uint8_t(i + 1); s->secret[i] = uint8_t(i + 3); }
    for (int i = 0; i < 16; ++i) s->session[i] = uint8_t(i + 8);
    return s;
}
static Header App(CE_NetSession *s, uint32_t kind, uint32_t seq = 1, uint32_t total = 3, uint32_t part = 0)
{
    Header h{};
    h.kind = kind; h.channel = Channel(kind); h.epoch = 1; h.sequence = seq; h.tick = 7; h.message = 1;
    h.total = total; h.part = part; h.parts = std::max(1u, (total + kChunk - 1) / kChunk);
    std::memcpy(h.session, s->session, 16);
    return h;
}
static std::array<uint8_t, kWire> Seal(const Header &h, const uint8_t *key, bool fromHost, uint32_t &size)
{
    std::array<uint8_t, kWire> wire{}, plain{};
    uint32_t n = std::min(kChunk, h.total - h.part * kChunk);
    for (uint32_t i = 0; i < n; ++i) plain[i] = uint8_t((h.part * kChunk + i) & 255);
    WriteHeader(wire.data(), h);
    uint8_t nonce[24]; Nonce(nonce, h, fromHost);
    crypto_aead_lock(wire.data() + kHeader, wire.data() + kHeader + n, key, nonce, wire.data(), kHeader, plain.data(), n);
    size = kHeader + n + kTag;
    return wire;
}
static void Feed(CE_NetSession *s, Header h, uint64_t now = 100)
{
    uint32_t size;
    auto wire = Seal(h, s->key, s->status.role != CE_NET_HOST, size);
    ReceivePacket(s, wire.data(), size, h.channel, now);
}
static void ReplayAndNonce()
{
    Replay r;
    assert(!r.Add(0) && r.Add(129) && r.Add(2) && !r.Add(1) && !r.Add(129) && !r.Add(2));
    assert(r.Add(500) && r.Add(499) && !r.Add(128));
    auto s = Active();
    Header h = App(s.get(), CE_NET_INPUT);
    uint8_t nonces[4][24];
    Nonce(nonces[0], h, true); Nonce(nonces[1], h, false);
    ++h.sequence; Nonce(nonces[2], h, true); ++h.channel; Nonce(nonces[3], h, true);
    for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) assert(std::memcmp(nonces[i], nonces[j], 24));
    h = App(s.get(), CE_NET_INPUT);
    Feed(s.get(), h); assert(s->count == 1 && s->lastAuthenticated == 100);
    Feed(s.get(), h, 200); assert(s->count == 1 && s->status.rejected_packets == 1 && s->lastAuthenticated == 100);
    ++h.sequence;
    uint32_t size;
    auto reflected = Seal(h, s->key, true, size); // wrong peer direction, otherwise valid header/key
    ReceivePacket(s.get(), reflected.data(), size, 1, 100);
    assert(s->count == 1 && s->status.rejected_packets == 2);
    h.epoch = 0; Feed(s.get(), h, 200); assert(s->count == 1 && s->status.rejected_packets == 3 && s->lastAuthenticated == 100);
    ++h.sequence; h.epoch = 1; h.session[0] ^= 1;
    Feed(s.get(), h); assert(s->count == 1 && s->status.rejected_packets == 4);
    assert(s->status.phase == CE_NET_ACTIVE);
    // An authenticated but illegal role/message cannot keep an otherwise silent session alive.
    h = App(s.get(), CE_NET_STATE, 1); Feed(s.get(), h, 200); assert(s->lastAuthenticated == 100);
}
static void Malformed()
{
    auto s = Active();
    Header h = App(s.get(), CE_NET_INPUT);
    uint32_t size; auto good = Seal(h, s->key, false, size);
    for (uint32_t n = 0; n < size; ++n) ReceivePacket(s.get(), good.data(), n, 1, 100);
    assert(s->count == 0);
    for (uint32_t pos : {0u, 4u, 6u, 7u, 15u, 27u, 29u, 31u, 32u, 48u, size - 1}) {
        auto broken = good; broken[pos] ^= 0x80;
        ReceivePacket(s.get(), broken.data(), size, 1, 100);
        assert(s->count == 0 && s->status.phase == CE_NET_ACTIVE);
    }
    ReceivePacket(s.get(), good.data(), size, 0, 100); assert(s->count == 0);
    h = App(s.get(), CE_NET_INPUT, 1, 1001); Feed(s.get(), h); assert(s->count == 0);
    // Correct AEAD from the peer still cannot reverse host authority.
    h = App(s.get(), CE_NET_STATE); Feed(s.get(), h); assert(s->count == 0);
    h = App(s.get(), CE_NET_INPUT); Feed(s.get(), h); assert(s->count == 1);
}
static void Reassembly()
{
    auto s = Active(CE_NET_GUEST);
    Header h = App(s.get(), CE_NET_STATE, 2, 1001, 1);
    Feed(s.get(), h); assert(s->count == 0 && s->assembly[2].received == 1);
    h.sequence = 1; h.part = 0; Feed(s.get(), h);
    assert(s->count == 1 && s->queue[0].meta.size == 1001);
    for (uint32_t i = 0; i < 1001; ++i) assert(s->queue[0].data[i] == uint8_t(i & 255));
    CE_NetMessage message{}; uint8_t tiny[1];
    assert(CE_NetReceive(s.get(), &message, tiny, sizeof(tiny)) == -1 && s->count == 1);
    auto mismatch = Active(CE_NET_GUEST);
    h = App(mismatch.get(), CE_NET_STATE, 1, 1001); Feed(mismatch.get(), h);
    h.sequence = 2; h.part = 1; ++h.tick; Feed(mismatch.get(), h);
    assert(mismatch->status.phase == CE_NET_FAILED && mismatch->status.error == CE_NET_PROTOCOL);
    auto expired = Active(CE_NET_GUEST);
    h = App(expired.get(), CE_NET_BASELINE, 1, CE_NET_MAX_MESSAGE); Feed(expired.get(), h, 100);
    ExpireAssemblies(expired.get(), 100 + expired->silenceMs);
    assert(expired->status.phase == CE_NET_FAILED && expired->status.error == CE_NET_TIMEOUT);
    auto large = Active(CE_NET_GUEST);
    for (uint32_t i = 0; i < kParts; ++i) {
        h = App(large.get(), CE_NET_BASELINE, i + 1, CE_NET_MAX_MESSAGE, i); Feed(large.get(), h);
        assert(large->count == (i == kParts - 1 ? 1u : 0u));
    }
    auto queue = Active();
    for (uint32_t i = 0; i < kQueue + 1; ++i) {
        h = App(queue.get(), CE_NET_CONTROL, i + 1); h.message = i + 1; Feed(queue.get(), h);
    }
    assert(queue->status.phase == CE_NET_FAILED && queue->status.error == CE_NET_CAPACITY && queue->count == 0);
}
static void SupersedableState()
{
    auto s=Active(CE_NET_GUEST);
    Header h=App(s.get(),CE_NET_STATE,1,2501); Feed(s.get(),h,100);
    h.sequence=2;h.part=1;Feed(s.get(),h,349);
    assert(s->count==0&&s->assembly[2].started==100&&s->assembly[2].received==2);
    ExpireAssemblies(s.get(),349);assert(s->assembly[2].message==1);
    ExpireAssemblies(s.get(),350);
    assert(s->status.phase==CE_NET_ACTIVE&&s->retiredState==1&&s->assembly[2].message==0&&s->count==0);
    h.sequence=3;h.part=2;Feed(s.get(),h,351);
    assert(s->assembly[2].message==0&&s->count==0&&s->lastAuthenticated==349);
    uint32_t sequence=4;
    auto piece=[&](uint32_t message,uint32_t part,uint64_t now){Header x=App(s.get(),CE_NET_STATE,sequence++,2501,part);x.message=message;x.tick=message;Feed(s.get(),x,now);};
    piece(2,2,400);piece(2,0,401);assert(s->count==0);piece(2,1,402);
    assert(s->count==1&&s->complete[2]==2&&s->retiredState==2&&s->queue[0].meta.tick==2);
    piece(3,0,410);assert(s->queue[0].meta.tick==2);piece(4,2,411);
    assert(s->retiredState==3&&s->assembly[2].message==4&&s->queue[0].meta.tick==2);
    piece(3,1,412);piece(3,2,413);assert(s->assembly[2].message==4&&s->assembly[2].received==1);
    piece(4,0,414);piece(4,1,415);
    assert(s->count==1&&s->queue[0].meta.tick==4&&s->retiredState==4);
    piece(3,0,416);piece(3,1,417);piece(3,2,418);
    assert(s->count==1&&s->queue[0].meta.tick==4&&s->assembly[2].message==0);
    for(uint32_t i=0;i<2501;++i)assert(s->queue[0].data[i]==uint8_t(i&255));
    piece(5,0,500);ExpireAssemblies(s.get(),750);
    assert(s->retiredState==5&&s->count==1&&s->queue[0].meta.tick==4&&s->status.phase==CE_NET_ACTIVE);
    s->sendSequence[2]=55;s->replay[2].highest=99;ClearMessages(s.get());
    assert(!s->retiredState&&!s->count&&!s->assembly[2].message&&s->sendSequence[2]==55&&s->replay[2].highest==99);

    auto full=Active(CE_NET_GUEST);
    for(uint32_t i=0;i<kQueue;++i){Header x=App(full.get(),CE_NET_CONTROL,i+1);x.message=i+1;Feed(full.get(),x,100);}
    assert(full->count==kQueue);
    for(uint32_t i=0;i<3;++i){Header x=App(full.get(),CE_NET_STATE,i+1,2501,i);Feed(full.get(),x,101);}
    assert(full->status.phase==CE_NET_ACTIVE&&full->retiredState==1&&!full->assembly[2].message);
    CE_NetMessage m{};uint8_t data[2501];while(CE_NetReceive(full.get(),&m,data,sizeof(data))==1){}
    for(uint32_t i=0;i<3;++i){Header x=App(full.get(),CE_NET_STATE,i+4,2501,i);Feed(full.get(),x,102);}
    assert(!full->count&&!full->assembly[2].message);
    for(uint32_t i=0;i<3;++i){Header x=App(full.get(),CE_NET_STATE,i+7,2501,i);x.message=2;Feed(full.get(),x,103);}
    assert(full->count==1&&full->retiredState==2);
}
static void LifecycleFraming()
{
    static_assert(CE_NET_CONTROL == 16 && CE_NET_INPUT == 17 && CE_NET_STATE == 18 &&
        CE_NET_BASELINE == 19 && CE_NET_LIFECYCLE == 20 && CE_NET_MAX_LIFECYCLE == 1000);
    assert(Channel(CE_NET_LIFECYCLE) == Channel(CE_NET_BASELINE) && Channel(CE_NET_LIFECYCLE) == 3);
    auto guest = Active(CE_NET_GUEST);
    Header h = App(guest.get(), CE_NET_LIFECYCLE, 1, CE_NET_MAX_LIFECYCLE);
    Feed(guest.get(), h);
    CE_NetMessage message{}; uint8_t data[CE_NET_MAX_LIFECYCLE];
    assert(guest->count == 1 && CE_NetReceive(guest.get(), &message, data, sizeof(data) - 1) == -1);
    assert(CE_NetReceive(guest.get(), &message, data, sizeof(data)) == 1 && message.kind == CE_NET_LIFECYCLE &&
        message.epoch == 1 && message.tick == 7 && message.size == sizeof(data));
    for (uint32_t i = 0; i < sizeof(data); ++i) assert(data[i] == uint8_t(i & 255));
    Feed(guest.get(), h, 200);
    assert(!guest->count && guest->status.rejected_packets == 1 && guest->lastAuthenticated == 100);

    // Every malformed form has a real peer MAC; channel/size/part bounds are not delegated to a payload codec.
    for (uint32_t total : {1001u, CE_NET_MAX_MESSAGE}) {
        auto s = Active(CE_NET_GUEST);
        for (uint32_t part : {0u, 1u}) Feed(s.get(), App(s.get(), CE_NET_LIFECYCLE, part + 1, total, part));
        assert(!s->count && s->status.rejected_packets == 2 && !s->lastAuthenticated);
    }
    for (uint32_t badParts : {0u, 2u}) {
        auto s = Active(CE_NET_GUEST); h = App(s.get(), CE_NET_LIFECYCLE); h.parts = badParts;
        Feed(s.get(), h); assert(!s->count && s->status.rejected_packets == 1);
    }
    for (uint32_t channel : {0u, 1u, 2u}) {
        auto s = Active(CE_NET_GUEST); h = App(s.get(), CE_NET_LIFECYCLE);
        uint32_t size; auto wire = Seal(h, s->key, true, size);
        ReceivePacket(s.get(), wire.data(), size, channel, 100);
        h.channel = channel; Feed(s.get(), h);
        assert(!s->count && s->status.rejected_packets == 2 && !s->lastAuthenticated);
    }
    for (uint32_t epoch : {0u, 2u}) {
        auto s = Active(CE_NET_GUEST); h = App(s.get(), CE_NET_LIFECYCLE); h.epoch = epoch;
        Feed(s.get(), h); assert(!s->count && s->status.rejected_packets == 1 && !s->lastAuthenticated);
    }
    auto host = Active(); h = App(host.get(), CE_NET_LIFECYCLE);
    Feed(host.get(), h); // Valid guest AEAD cannot reverse host authority or refresh silence.
    assert(!host->count && host->status.rejected_packets == 1 && !host->lastAuthenticated);
    host->status.phase = CE_NET_AUTHENTICATING; host->stage = kAcceptSent;
    ++h.sequence; Feed(host.get(), h);
    assert(host->status.phase == CE_NET_AUTHENTICATING && !host->count && !host->lastAuthenticated);
    auto syncing = Active(CE_NET_GUEST); syncing->status.phase = CE_NET_SYNCING;
    Feed(syncing.get(), App(syncing.get(), CE_NET_LIFECYCLE));
    assert(!syncing->count && syncing->status.rejected_packets == 1 && !syncing->lastAuthenticated);
    auto authenticated = Active(CE_NET_GUEST); h = App(authenticated.get(), CE_NET_LIFECYCLE);
    uint32_t size; auto wire = Seal(h, authenticated->key, false, size); // reflected direction
    ReceivePacket(authenticated.get(), wire.data(), size, 3, 100);
    wire = Seal(h, authenticated->key, true, size); wire[kHeader] ^= 1;
    ReceivePacket(authenticated.get(), wire.data(), size, 3, 100);
    assert(!authenticated->count && authenticated->status.rejected_packets == 2 && !authenticated->lastAuthenticated);
    std::puts("lifecycle: authenticated role/channel/size/part/epoch/replay/reflection/tamper controls passed");
}
static void StateRepairFraming()
{
    static_assert(CE_NET_CONTROL == 16 && CE_NET_INPUT == 17 && CE_NET_STATE == 18 && CE_NET_BASELINE == 19 &&
        CE_NET_LIFECYCLE == 20 && CE_NET_STATE_CONTROL == 21 && CE_NET_STATE_REPAIR == 22 &&
        CE_NET_STATE_CONTROL_BYTES == 184 && CE_NET_MAX_MESSAGE == 65536);
    assert(Channel(CE_NET_STATE_CONTROL) == 0 && Channel(CE_NET_STATE_REPAIR) == 3);
    for (uint32_t role : {uint32_t(CE_NET_HOST), uint32_t(CE_NET_GUEST)}) {
        auto s = Active(role); Header h = App(s.get(), CE_NET_STATE_CONTROL, 1, CE_NET_STATE_CONTROL_BYTES);
        Feed(s.get(), h); assert(s->count == 1 && s->queue[0].meta.kind == CE_NET_STATE_CONTROL);
        Feed(s.get(), h, 200); assert(s->count == 1 && s->status.rejected_packets == 1 && s->lastAuthenticated == 100);
        for (uint32_t size : {0u, 1u, 183u, 185u, 1000u, 1001u}) {
            auto bad = Active(role); Feed(bad.get(), App(bad.get(), CE_NET_STATE_CONTROL, 1, size));
            assert(!bad->count && bad->status.rejected_packets == 1 && !bad->lastAuthenticated);
        }
    }
    for (uint32_t kind : {uint32_t(CE_NET_STATE_CONTROL), uint32_t(CE_NET_STATE_REPAIR)}) {
        uint32_t total = kind == CE_NET_STATE_CONTROL ? CE_NET_STATE_CONTROL_BYTES : 1001;
        for (uint32_t channel = 0; channel < kChannels; ++channel) if (channel != Channel(kind)) {
            auto bad = Active(CE_NET_GUEST); Header h = App(bad.get(), kind, 1, total); h.channel = channel;
            Feed(bad.get(), h); assert(!bad->count && bad->status.rejected_packets == 1 && !bad->lastAuthenticated);
        }
        for (uint32_t epoch : {0u, 2u}) {
            auto bad = Active(CE_NET_GUEST); Header h = App(bad.get(), kind, 1, total); h.epoch = epoch;
            Feed(bad.get(), h); assert(!bad->count && bad->status.rejected_packets == 1 && !bad->lastAuthenticated);
        }
        auto bad = Active(CE_NET_GUEST); Header h = App(bad.get(), kind, 1, total); uint32_t size;
        auto wire = Seal(h, bad->key, false, size); ReceivePacket(bad.get(), wire.data(), size, h.channel, 100);
        wire = Seal(h, bad->key, true, size); wire[kHeader] ^= 1;
        ReceivePacket(bad.get(), wire.data(), size, h.channel, 100);
        assert(!bad->count && bad->status.rejected_packets == 2 && !bad->lastAuthenticated);
        h = App(bad.get(), kind, 1, total); h.parts = 0; Feed(bad.get(), h);
        h.parts = 2; h.total = kind == CE_NET_STATE_CONTROL ? CE_NET_STATE_CONTROL_BYTES : 1; Feed(bad.get(), h);
        assert(!bad->count && bad->status.rejected_packets == 4);
    }
    for (uint32_t size : {0u, 65537u}) {
        auto bad = Active(CE_NET_GUEST); Feed(bad.get(), App(bad.get(), CE_NET_STATE_REPAIR, 1, size));
        assert(!bad->count && bad->status.rejected_packets == 1 && !bad->lastAuthenticated);
    }
    auto wrongRole = Active(); Feed(wrongRole.get(), App(wrongRole.get(), CE_NET_STATE_REPAIR, 1, 1));
    assert(!wrongRole->count && wrongRole->status.rejected_packets == 1 && !wrongRole->lastAuthenticated);
    // A reliable application control is not a handshake proof or an epoch acknowledgement.
    for (uint32_t phase : {uint32_t(CE_NET_AUTHENTICATING), uint32_t(CE_NET_SYNCING)}) {
        auto s = Active(); s->status.phase = phase; s->stage = kAcceptSent;
        Feed(s.get(), App(s.get(), CE_NET_STATE_CONTROL, 1, CE_NET_STATE_CONTROL_BYTES));
        assert(s->status.phase == phase && !s->count && !s->lastAuthenticated);
    }
    auto small = Active(CE_NET_GUEST); Feed(small.get(), App(small.get(), CE_NET_STATE_REPAIR, 1, 1));
    assert(small->count == 1 && small->queue[0].meta.size == 1);
    auto large = Active(CE_NET_GUEST);
    for (uint32_t i = 0; i < kParts; ++i) {
        Feed(large.get(), App(large.get(), CE_NET_STATE_REPAIR, i + 1, CE_NET_MAX_MESSAGE, i));
        assert(large->count == (i == kParts - 1 ? 1u : 0u));
    }
    for (uint32_t i = 0; i < CE_NET_MAX_MESSAGE; ++i) assert(large->queue[0].data[i] == uint8_t(i & 255));
    auto partial = Active(CE_NET_GUEST);
    Feed(partial.get(), App(partial.get(), CE_NET_STATE_REPAIR, 1, 1001), 100);
    Feed(partial.get(), App(partial.get(), CE_NET_STATE, 1, 1001), 100);
    ExpireAssemblies(partial.get(), 350);
    assert(partial->status.phase == CE_NET_ACTIVE && partial->assembly[3].received == 1 &&
        !partial->assembly[2].message && !partial->count);
    Feed(partial.get(), App(partial.get(), CE_NET_STATE_REPAIR, 2, 1001, 1), 400);
    assert(partial->count == 1 && partial->queue[0].meta.kind == CE_NET_STATE_REPAIR);
    auto expired = Active(CE_NET_GUEST);
    Feed(expired.get(), App(expired.get(), CE_NET_STATE_REPAIR, 1, 1001), 100);
    ExpireAssemblies(expired.get(), 100 + expired->silenceMs);
    assert(expired->status.phase == CE_NET_FAILED && expired->status.error == CE_NET_TIMEOUT && !expired->count);
    std::puts("state repair: authenticated sizes/roles/channels/parts/epochs, complete65536 and reliable deadline passed");
}
static void OutgoingStateFlags()
{
    CE_NetConfig c{};c.role=CE_NET_HOST;c.ipv4="127.0.0.1";std::memset(c.secret,0x35,32);
    CE_NetSession*s=CE_NetCreate(&c);assert(s);
    ENetAddress address{};assert(enet_address_set_host_ip(&address,"127.0.0.1")==0);address.port=9;
    s->peer=enet_host_connect(s->host,&address,kChannels,kVersion);assert(s->peer);s->peer->state=ENET_PEER_STATE_CONNECTED;
    uint8_t data[kChunk]{};
    for(uint32_t kind:{uint32_t(CE_NET_STATE),uint32_t(CE_NET_BASELINE),uint32_t(CE_NET_INPUT),uint32_t(CE_NET_CONTROL),uint32_t(CE_NET_LIFECYCLE),uint32_t(CE_NET_STATE_CONTROL),uint32_t(CE_NET_STATE_REPAIR)}){
        uint32_t total=kind==CE_NET_LIFECYCLE?CE_NET_MAX_LIFECYCLE:kind==CE_NET_STATE_CONTROL?CE_NET_STATE_CONTROL_BYTES:
            kind==CE_NET_STATE||kind==CE_NET_BASELINE||kind==CE_NET_STATE_REPAIR?2501:3;
        Header h=App(s,kind,1,total);
        for(uint32_t part=0;part<h.parts;++part){h.part=part;assert(QueuePacket(s,h,data,std::min(kChunk,h.total-part*kChunk),s->key));}
    }
    unsigned states=0,reliable=0,lifecycle=0,controls=0,repairs=0;
    for(ENetListIterator i=enet_list_begin(&s->peer->outgoingCommands);i!=enet_list_end(&s->peer->outgoingCommands);i=enet_list_next(i)){
        auto*command=(ENetOutgoingCommand*)i;if(!command->packet)continue;
        uint8_t kind=command->packet->data[6];
        if(kind==CE_NET_STATE){assert(command->packet->flags&ENET_PACKET_FLAG_UNSEQUENCED);assert(!(command->packet->flags&ENET_PACKET_FLAG_RELIABLE));assert((command->command.header.command&ENET_PROTOCOL_COMMAND_MASK)==ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED);++states;}
    }
    for(ENetListIterator i=enet_list_begin(&s->peer->outgoingSendReliableCommands);i!=enet_list_end(&s->peer->outgoingSendReliableCommands);i=enet_list_next(i)){
        auto*command=(ENetOutgoingCommand*)i;if(!command->packet)continue;
        assert(command->packet->data[6]!=CE_NET_STATE&&(command->packet->flags&ENET_PACKET_FLAG_RELIABLE));++reliable;
        if(command->packet->data[6]==CE_NET_LIFECYCLE){
            assert(command->command.header.channelID==3&&
                (command->command.header.command&ENET_PROTOCOL_COMMAND_MASK)==ENET_PROTOCOL_COMMAND_SEND_RELIABLE);
            Header h{};uint32_t n;assert(ReadHeader(command->packet->data,uint32_t(command->packet->dataLength),3,h,n)&&
                h.parts==1&&h.part==0&&n==CE_NET_MAX_LIFECYCLE);++lifecycle;
        }
        if(command->packet->data[6]==CE_NET_STATE_CONTROL||command->packet->data[6]==CE_NET_STATE_REPAIR){
            uint32_t channel=command->packet->data[6]==CE_NET_STATE_CONTROL?0:3;Header h{};uint32_t n;
            assert(command->command.header.channelID==channel&&
                (command->command.header.command&ENET_PROTOCOL_COMMAND_MASK)==ENET_PROTOCOL_COMMAND_SEND_RELIABLE&&
                ReadHeader(command->packet->data,uint32_t(command->packet->dataLength),channel,h,n));
            if(h.kind==CE_NET_STATE_CONTROL){assert(h.parts==1&&n==CE_NET_STATE_CONTROL_BYTES);++controls;}
            else {assert(h.parts==3);++repairs;}
        }
    }
    assert(states==3&&reliable==10&&lifecycle==1&&controls==1&&repairs==3);
    s->status.phase=CE_NET_ACTIVE;s->status.epoch=1;s->peer->mtu=576;
    assert(CE_NetSend(s,CE_NET_STATE,1,data,sizeof(data))==0&&s->status.phase==CE_NET_FAILED&&s->status.error==CE_NET_PROTOCOL);
    CE_NetDestroy(s);
}
static void LifecycleLoopback()
{
    CE_NetConfig c{}; c.role = CE_NET_HOST; c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0x35, 32);
    CE_NetSession *host = CE_NetCreate(&c); assert(host); c.role = CE_NET_GUEST; c.port = host->status.port;
    CE_NetSession *guest = CE_NetCreate(&c); assert(guest);
    auto settle = [&]() {
        uint64_t deadline = CE_NetNowMs() + 2000;
        while ((host->status.phase != CE_NET_ACTIVE || guest->status.phase != CE_NET_ACTIVE) && CE_NetNowMs() < deadline) {
            uint64_t now = CE_NetNowMs(); CE_NetPoll(host, now, 32); CE_NetPoll(guest, now, 32);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(host->status.phase == CE_NET_ACTIVE && guest->status.phase == CE_NET_ACTIVE);
    };
    settle();
    uint8_t data[1001]; for (uint32_t i = 0; i < sizeof(data); ++i) data[i] = uint8_t(i & 255);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 0, data, sizeof(data)) == -1);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 0, nullptr, 1) == -1);
    for (uint32_t size : {0u, 1000u, 1001u}) assert(CE_NetSend(guest, CE_NET_LIFECYCLE, 0, data, size) == -1);
    assert(!host->status.sent_messages && !guest->status.sent_messages);
    assert(CE_NetSend(host, CE_NET_BASELINE, 11, data, sizeof(data)) == 1);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 12, data, CE_NET_MAX_LIFECYCLE) == 1);
    assert(CE_NetSend(host, CE_NET_BASELINE, 13, data, 7) == 1);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 14, nullptr, 0) == 1);
    uint32_t received = 0; uint64_t deadline = CE_NetNowMs() + 2000;
    while (received < 4 && CE_NetNowMs() < deadline) {
        CE_NetPoll(host, CE_NetNowMs(), 32); CE_NetPoll(guest, CE_NetNowMs(), 32);
        CE_NetMessage m{}; uint8_t bytes[1001];
        while (CE_NetReceive(guest, &m, bytes, sizeof(bytes)) == 1) {
            assert(received < 4 && m.tick == 11 + received && m.epoch == 1);
            assert(m.kind == (received % 2 ? CE_NET_LIFECYCLE : CE_NET_BASELINE));
            uint32_t expected[] = {1001, 1000, 7, 0}; assert(m.size == expected[received]);
            for (uint32_t i = 0; i < m.size; ++i) assert(bytes[i] == data[i]);
            ++received;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received == 4 && host->messageSequence[3] == 4 && host->sendSequence[3] == 5);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 20, data, CE_NET_MAX_LIFECYCLE) == 1);
    assert(CE_NetBeginEpoch(host, 2) == 1);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 21, data, CE_NET_MAX_LIFECYCLE) == 0);
    settle(); assert(host->status.epoch == 2 && guest->status.epoch == 2 && !guest->count);
    assert(CE_NetSend(host, CE_NET_LIFECYCLE, 22, data, CE_NET_MAX_LIFECYCLE) == 1);
    deadline = CE_NetNowMs() + 2000; received = 0;
    while (!received && CE_NetNowMs() < deadline) {
        CE_NetPoll(host, CE_NetNowMs(), 32); CE_NetPoll(guest, CE_NetNowMs(), 32);
        CE_NetMessage m{}; uint8_t bytes[1000];
        if (CE_NetReceive(guest, &m, bytes, sizeof(bytes)) == 1) {
            assert(m.kind == CE_NET_LIFECYCLE && m.epoch == 2 && m.tick == 22 && m.size == sizeof(bytes));
            assert(!std::memcmp(bytes, data, sizeof(bytes))); ++received;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received == 1 && !guest->count);
    CE_NetDestroy(guest); CE_NetDestroy(host);
    std::puts("lifecycle: public send bounds, reliable BASELINE ordering and epoch loopback passed");
}
static void StateRepairLoopback()
{
    CE_NetConfig c{}; c.role = CE_NET_HOST; c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0x35, 32);
    CE_NetSession *host = CE_NetCreate(&c); assert(host); c.role = CE_NET_GUEST; c.port = host->status.port;
    CE_NetSession *guest = CE_NetCreate(&c); assert(guest);
    auto settle = [&]() {
        uint64_t deadline = CE_NetNowMs() + 2000;
        while ((host->status.phase != CE_NET_ACTIVE || guest->status.phase != CE_NET_ACTIVE) && CE_NetNowMs() < deadline) {
            uint64_t now = CE_NetNowMs(); CE_NetPoll(host, now, 32); CE_NetPoll(guest, now, 32);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(host->status.phase == CE_NET_ACTIVE && guest->status.phase == CE_NET_ACTIVE);
    };
    settle(); uint8_t data[CE_NET_MAX_MESSAGE + 1];
    for (uint32_t i = 0; i < sizeof(data); ++i) data[i] = uint8_t(i & 255);
    for (auto *s : {host, guest}) {
        for (uint32_t size : {0u, 183u, 185u, 1001u}) assert(CE_NetSend(s, CE_NET_STATE_CONTROL, 0, data, size) == -1);
        assert(CE_NetSend(s, CE_NET_STATE_CONTROL, 0, nullptr, CE_NET_STATE_CONTROL_BYTES) == -1);
    }
    assert(CE_NetSend(host, CE_NET_STATE_REPAIR, 0, data, 0) == -1);
    assert(CE_NetSend(host, CE_NET_STATE_REPAIR, 0, data, sizeof(data)) == -1);
    for (uint32_t size : {1u, CE_NET_MAX_MESSAGE}) assert(CE_NetSend(guest, CE_NET_STATE_REPAIR, 0, data, size) == -1);
    assert(!host->status.sent_messages && !guest->status.sent_messages);
    assert(CE_NetSend(host, CE_NET_STATE_CONTROL, 20, data, CE_NET_STATE_CONTROL_BYTES) == 1);
    assert(CE_NetSend(guest, CE_NET_STATE_CONTROL, 21, data, CE_NET_STATE_CONTROL_BYTES) == 1);
    const uint32_t kinds[] = {CE_NET_BASELINE, CE_NET_LIFECYCLE, CE_NET_STATE_REPAIR, CE_NET_BASELINE,
        CE_NET_STATE_REPAIR, CE_NET_LIFECYCLE};
    const uint32_t sizes[] = {7, 1000, CE_NET_MAX_MESSAGE, 1001, 1, 0};
    for (uint32_t i = 0; i < 6; ++i) assert(CE_NetSend(host, kinds[i], 100 + i, data, sizes[i]) == 1);
    uint32_t received = 0, controls = 0; uint64_t deadline = CE_NetNowMs() + 3000;
    while ((received < 6 || controls < 2) && CE_NetNowMs() < deadline) {
        CE_NetPoll(host, CE_NetNowMs(), 32); CE_NetPoll(guest, CE_NetNowMs(), 32);
        for (auto *s : {host, guest}) {
            CE_NetMessage m{}; uint8_t bytes[CE_NET_MAX_MESSAGE];
            while (CE_NetReceive(s, &m, bytes, sizeof(bytes)) == 1) {
                assert(m.epoch == 1);
                if (m.kind == CE_NET_STATE_CONTROL) {
                    assert(m.tick == (s == host ? 21u : 20u) && m.size == CE_NET_STATE_CONTROL_BYTES); ++controls;
                } else {
                    assert(s == guest && received < 6 && m.kind == kinds[received] && m.tick == 100 + received &&
                        m.size == sizes[received]); ++received;
                }
                assert(!std::memcmp(bytes, data, m.size));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received == 6 && controls == 2 && host->messageSequence[3] == 6 && host->sendSequence[3] == 72);
    // Actually deliver only the first application part, then enter the existing public epoch barrier.
    assert(CE_NetSend(host, CE_NET_STATE_REPAIR, 300, data, 1001) == 1);
    deadline = CE_NetNowMs() + 2000;
    while (!guest->assembly[3].message && CE_NetNowMs() < deadline) {
        CE_NetPoll(guest, CE_NetNowMs(), 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(guest->assembly[3].kind == CE_NET_STATE_REPAIR && guest->assembly[3].received == 1 && !guest->count);
    assert(CE_NetBeginEpoch(host, 2) == 1);
    assert(CE_NetSend(host, CE_NET_STATE_REPAIR, 301, data, 1) == 0);
    assert(CE_NetSend(host, CE_NET_STATE_CONTROL, 301, data, CE_NET_STATE_CONTROL_BYTES) == 0);
    settle(); assert(guest->status.epoch == 2 && !guest->count && !guest->assembly[3].message);
    Header late = App(guest, CE_NET_STATE_REPAIR, guest->replay[3].highest + 1, 1001, 1);
    late.message = 7; late.epoch = 1; late.tick = 300;
    uint64_t authenticated = guest->lastAuthenticated; uint32_t rejected = guest->status.rejected_packets;
    Feed(guest, late, authenticated + 1); // Fresh authenticated packet sequence: epoch, not replay, must reject it.
    assert(!guest->count && !guest->assembly[3].message && guest->lastAuthenticated == authenticated &&
        guest->status.rejected_packets == rejected + 1 && guest->status.phase == CE_NET_ACTIVE);
    CE_NetDestroy(guest); CE_NetDestroy(host);
    std::puts("state repair: public184 bidirection, complete65536 mixed FIFO, partial epoch and late fragment passed");
}
static void StateControlReserve()
{
    CE_NetConfig c{}; c.role = CE_NET_HOST; c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0x35, 32);
    CE_NetSession *s = CE_NetCreate(&c); assert(s);
    ENetAddress address{}; assert(enet_address_set_host_ip(&address, "127.0.0.1") == 0); address.port = 9;
    s->peer = enet_host_connect(s->host, &address, kChannels, kVersion); assert(s->peer);
    s->peer->state = ENET_PEER_STATE_CONNECTED; s->status.phase = CE_NET_ACTIVE; s->status.epoch = 1;
    uint8_t control[CE_NET_STATE_CONTROL_BYTES]{}; uint32_t queued = 0;
    while (CE_NetSend(s, CE_NET_STATE_CONTROL, queued, control, sizeof(control)) == 1) assert(++queued < kPendingPackets);
    assert(queued && s->pendingPackets == kPendingPackets - kControlReserve && s->status.phase == CE_NET_ACTIVE);
    uint32_t before = s->pendingPackets; assert(SendInternal(s, kPing) && s->pendingPackets == before + 1);
    CE_NetDestroy(s);
}
static void CallerDrainBudget()
{
    // A recovered reliable burst can exceed a caller's64-message queue when it polls128 events before draining.
    // Keep that real capacity refusal, and verify the native Session's32-event poll drains the same80 inputs.
    for(uint32_t budget:{128u,32u}){
        CE_NetConfig c{};c.role=CE_NET_HOST;c.ipv4="127.0.0.1";std::memset(c.secret,0x35,32);
        CE_NetSession*host=CE_NetCreate(&c);assert(host);c.role=CE_NET_GUEST;c.port=host->status.port;
        CE_NetSession*guest=CE_NetCreate(&c);assert(guest);uint64_t deadline=CE_NetNowMs()+2000;
        while((host->status.phase!=CE_NET_ACTIVE||guest->status.phase!=CE_NET_ACTIVE)&&CE_NetNowMs()<deadline){uint64_t now=CE_NetNowMs();CE_NetPoll(host,now,32);CE_NetPoll(guest,now,32);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(host->status.phase==CE_NET_ACTIVE&&guest->status.phase==CE_NET_ACTIVE);
        for(uint32_t i=1;i<=80;++i){uint8_t data[4];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof(data));CE_NetWriteU32(&w,i);assert(CE_NetSend(guest,CE_NET_INPUT,i,data,sizeof(data))==1);}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        uint32_t received=0;deadline=CE_NetNowMs()+1000;
        do{
            CE_NetPoll(host,CE_NetNowMs(),budget);
            if(host->status.phase==CE_NET_FAILED)break;
            uint8_t data[4];CE_NetMessage message{};
            while(CE_NetReceive(host,&message,data,sizeof(data))==1){CE_NetReader r;CE_NetReaderInit(&r,data,sizeof(data));assert(message.kind==CE_NET_INPUT&&message.tick==received+1&&CE_NetReadU32(&r)==received+1&&CE_NetReadDone(&r));++received;}
        }while(received<80&&CE_NetNowMs()<deadline);
        if(budget==128)assert(host->status.phase==CE_NET_FAILED&&host->status.error==CE_NET_CAPACITY&&!host->interceptError&&host->status.received_messages==kQueue);
        else assert(host->status.phase==CE_NET_ACTIVE&&received==80);
        CE_NetDestroy(guest);CE_NetDestroy(host);
    }
}
static void HandshakeReplayAndRestart()
{
    auto guest = Active(CE_NET_GUEST);
    guest->status.phase = CE_NET_AUTHENTICATING; guest->stage = kHelloSent;
    std::memset(guest->clientNonce, 0x61, 32);
    Header h = App(guest.get(), kChallenge, 1, 96);
    h.epoch = h.tick = 0;
    uint8_t plain[96]{}; // old challenge has the wrong fresh client nonce, even with a valid invite MAC
    std::memcpy(plain + 64, guest->compatibility, 32);
    std::array<uint8_t, kWire> wire{};
    WriteHeader(wire.data(), h);
    uint8_t nonce[24]; Nonce(nonce, h, true);
    crypto_aead_lock(wire.data() + kHeader, wire.data() + kHeader + 96, guest->secret, nonce,
                     wire.data(), kHeader, plain, sizeof(plain));
    ReceivePacket(guest.get(), wire.data(), kHeader + 96 + kTag, 0, 100);
    assert(guest->stage == kHelloSent && guest->status.phase == CE_NET_AUTHENTICATING && guest->status.rejected_packets == 1);
    // Refusal is also bound to the fresh client nonce, so replaying a prior one cannot abort this join.
    h.kind = kRefuse; h.total = kHelloBytes;
    WriteHeader(wire.data(), h); Nonce(nonce, h, true);
    crypto_aead_lock(wire.data() + kHeader, wire.data() + kHeader + kHelloBytes, guest->secret, nonce,
                     wire.data(), kHeader, plain, kHelloBytes);
    ReceivePacket(guest.get(), wire.data(), kHeader + kHelloBytes + kTag, 0, 100);
    assert(guest->status.phase == CE_NET_AUTHENTICATING && guest->status.rejected_packets == 2);
    // The genuine refusal for this join names the host's version; control bytes are not passed through.
    std::memcpy(plain, guest->clientNonce, 32); std::memcpy(plain + 32, guest->compatibility, 32);
    std::memset(plain + 64, 0, CE_NET_VERSION_BYTES); std::memcpy(plain + 64, "0.1.9\x07x", 7);
    h.sequence = 2; WriteHeader(wire.data(), h); Nonce(nonce, h, true);
    crypto_aead_lock(wire.data() + kHeader, wire.data() + kHeader + kHelloBytes, guest->secret, nonce,
                     wire.data(), kHeader, plain, kHelloBytes);
    ReceivePacket(guest.get(), wire.data(), kHeader + kHelloBytes + kTag, 0, 100);
    char label[CE_NET_VERSION_BYTES + 1] = {};
    assert(guest->status.phase == CE_NET_FAILED && guest->status.error == CE_NET_INCOMPATIBLE);
    assert(CE_NetPeerVersion(guest.get(), label, sizeof label) == 1 && !std::strcmp(label, "0.1.9?x"));
    // A duplicated HELLO cannot reset an existing authenticated session or its traffic key.
    auto host = Active();
    h = App(host.get(), kHello, 1, kHelloBytes); h.epoch = h.tick = 0; std::memset(h.session, 0, 16);
    WriteHeader(wire.data(), h); std::memset(wire.data() + kHeader, 0, kHelloBytes + kTag);
    ReceivePacket(host.get(), wire.data(), kHeader + kHelloBytes + kTag, 0, 100);
    assert(host->status.phase == CE_NET_ACTIVE && host->count == 0);
    // Even a fresh authenticated sequence carrying an old handshake kind cannot roll epoch/state back.
    auto activeGuest = Active(CE_NET_GUEST);
    activeGuest->status.epoch = 2; activeGuest->stage = kProofSent;
    h = App(activeGuest.get(), kAccept, 1, 0); h.epoch = h.tick = 0;
    Feed(activeGuest.get(), h, 200);
    assert(activeGuest->status.epoch == 2 && activeGuest->status.phase == CE_NET_ACTIVE && activeGuest->lastAuthenticated == 0);
    host->status.epoch = 2; host->stage = kAcceptSent;
    h = App(host.get(), kReady, 1, 0); h.epoch = h.tick = 0;
    Feed(host.get(), h, 200);
    assert(host->status.epoch == 2 && host->lastAuthenticated == 0);
    auto beforeProof = Active();
    beforeProof->status.phase = CE_NET_AUTHENTICATING; beforeProof->stage = 0;
    h = App(beforeProof.get(), kReady, UINT32_MAX, 0); h.epoch = h.tick = 0;
    Feed(beforeProof.get(), h, 200);
    assert(beforeProof->status.phase == CE_NET_AUTHENTICATING && beforeProof->replay[0].highest == 0);
    auto overtaking = Active(); overtaking->status.phase = CE_NET_AUTHENTICATING;
    overtaking->status.epoch = 0; overtaking->stage = kAcceptSent;
    h = App(overtaking.get(), CE_NET_INPUT, 1); Feed(overtaking.get(), h, 200);
    assert(overtaking->status.phase == CE_NET_ACTIVE && overtaking->status.epoch == 1 && overtaking->count == 1);
    auto barrier = Active(); barrier->status.phase = CE_NET_SYNCING; barrier->status.epoch = 2;
    barrier->silenceMs = 300; barrier->syncStarted = 100;
    h = App(barrier.get(), kPing, 1, 0); h.epoch = 2;
    for (uint32_t i = 0; i < 5; ++i) { h.sequence = i + 1; Feed(barrier.get(), h, 100 + i * 50); }
    assert(barrier->lastAuthenticated == 300);
    ExpireBarrier(barrier.get(), 400);
    assert(barrier->status.phase == CE_NET_FAILED && barrier->status.error == CE_NET_TIMEOUT);
    auto epochOvertake = Active(); epochOvertake->status.phase = CE_NET_SYNCING; epochOvertake->status.epoch = 2;
    h = App(epochOvertake.get(), CE_NET_INPUT, 1); h.epoch = 2; Feed(epochOvertake.get(), h, 200);
    assert(epochOvertake->status.phase == CE_NET_ACTIVE && epochOvertake->count == 1 && epochOvertake->queue[0].meta.epoch == 2);
    CE_NetConfig c{}; c.role = CE_NET_HOST; c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0x35, 32);
    CE_NetSession *a = CE_NetCreate(&c), *b = CE_NetCreate(&c);
    assert(a && b && std::memcmp(a->session, b->session, 16) && std::memcmp(a->clientNonce, b->clientNonce, 32));
    CE_NetDestroy(a); CE_NetDestroy(b);
    c.ipv4 = "localhost"; assert(CE_NetCreate(&c) == nullptr);
    c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0, 32); assert(CE_NetCreate(&c) == nullptr);
}
static void IngressBounds()
{
    uint8_t ping[] = {0,0, uint8_t(ENET_PROTOCOL_COMMAND_PING | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE), 0xff, 0,1};
    assert(DatagramCommands(ping, sizeof(ping)) == 1);
    std::array<uint8_t, CE_NET_MAX_DATAGRAM + 1> raw{};
    assert(!DatagramCommands(raw.data(), raw.size()));
    for (uint32_t i = 0; i < 33; ++i) raw[2 + i * 4] = ENET_PROTOCOL_COMMAND_PING;
    assert(!DatagramCommands(raw.data(), 2 + 33 * 4));
    auto rate = Active();
    ENetHost empty{}; empty.receivedData = ping; empty.receivedDataLength = sizeof(ping); rate->ingressAt = 100;
    for (uint32_t i = 0; i < kPacketBurst; ++i) assert(Ingress(rate.get(), &empty, 100) == 0);
    assert(Ingress(rate.get(), &empty, 100) == 1);
    assert(Ingress(rate.get(), &empty, 1100) == 0);
    raw.fill(0); raw[2] = ENET_PROTOCOL_COMMAND_SEND_RELIABLE;
    raw[6] = uint8_t((1200 - 8) >> 8); raw[7] = uint8_t(1200 - 8);
    empty.receivedData = raw.data(); empty.receivedDataLength = 1200;
    auto bytes = Active(); bytes->ingressAt = 100;
    uint32_t accepted = 0;
    while (Ingress(bytes.get(), &empty, 100) == 0) ++accepted;
    assert(accepted == kByteBurst / 1200 && accepted < kPacketBurst);

    // Reproduce ENet's zero-payload gap queue using its real allocator. Its payload counter remains zero.
    CE_NetConfig c{}; c.role = CE_NET_HOST; c.ipv4 = "127.0.0.1"; std::memset(c.secret, 0x35, 32);
    CE_NetSession *s = CE_NetCreate(&c); assert(s);
    ENetAddress address{}; assert(enet_address_set_host_ip(&address, "127.0.0.1") == 0); address.port = 9;
    ENetPeer *p = enet_host_connect(s->host, &address, kChannels, kVersion); assert(p);
    ENetProtocol command{}; command.header.channelID = 0;
    command.header.command = ENET_PROTOCOL_COMMAND_SEND_RELIABLE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    for (uint32_t i = 0; i < kIncomingMetadata; ++i) {
        command.header.reliableSequenceNumber = uint16_t(i + 2); // missing sequence one keeps all commands queued
        assert(enet_peer_queue_incoming_command(p, &command, nullptr, 0, ENET_PACKET_FLAG_RELIABLE, 0));
    }
    assert(p->totalWaitingData == 0 && IncomingMetadata(s->host) == kIncomingMetadata);
    s->host->receivedData = ping; s->host->receivedDataLength = sizeof(ping);
    assert(Ingress(s, s->host, s->ingressAt) == -1 && s->interceptError == CE_NET_CAPACITY);
    CE_NetDestroy(s);
}
int main()
{
    if (!FixtureGuard()) return 2;
    ReplayAndNonce(); Malformed(); Reassembly(); SupersedableState(); LifecycleFraming(); StateRepairFraming();
    OutgoingStateFlags(); LifecycleLoopback(); StateRepairLoopback(); StateControlReserve(); CallerDrainBudget();
    HandshakeReplayAndRestart(); IngressBounds();
    std::puts("protocol: malformed/replay/reflection/session/epoch/chunk/expiry/queue/auth and raw ingress byte/packet/metadata bounds passed");
}
