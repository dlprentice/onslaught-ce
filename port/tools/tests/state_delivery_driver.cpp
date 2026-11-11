// SPDX-License-Identifier: GPL-3.0-only
// Authored byte images and a Session-shaped caller. Its whole-Apply boundary is explicitly controlled, not native game proof.
#ifdef CE_DELIVERY_WORLD_FIXTURE
#define main OriginalAuthoredWorldFixtureMain
#include CE_DELIVERY_WORLD_FIXTURE
#undef main
#undef new
#undef min
#undef max
#undef fopen
#endif
#include "state_delivery.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static unsigned verified=0;
static void Must(bool ok,const char *why) {++verified;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(2);}}
static void W32(uint8_t *p,uint32_t x) {p[0]=uint8_t(x>>24);p[1]=uint8_t(x>>16);p[2]=uint8_t(x>>8);p[3]=uint8_t(x);}
static uint32_t R32(const uint8_t *p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static uint8_t context[32]={1,2,3,4};
#ifndef CE_DELIVERY_WORLD_FIXTURE
struct Image {
    std::vector<uint8_t> bytes;uint32_t seq,tick,watermark;
    Image(uint32_t sequence,uint32_t source_tick,uint32_t life=0,uint32_t size=512):bytes(size),seq(sequence),tick(source_tick),watermark(life) {
        for(unsigned i=24;i<size;++i) bytes[i]=uint8_t(i%251+1);
        if(size>=24) {std::memcpy(bytes.data(),"AUT9",4);W32(bytes.data()+4,7);W32(bytes.data()+8,seq);W32(bytes.data()+12,tick);W32(bytes.data()+16,life);W32(bytes.data()+20,1);}
    }
    CE_StateImage view() const {CE_StateImage v={};std::memcpy(v.identity.context,context,32);v.identity.epoch=7;v.identity.sequence=seq;v.data=bytes.data();v.size=uint32_t(bytes.size());return v;}
};
static CE_StateDeliveryControl RoundControl(const CE_StateDeliveryControl &c) {
    uint8_t bytes[184];CE_StateDeliveryControl copy={};
    Must(CE_StateDeliveryEncodeControl(&c,bytes,sizeof bytes)==0&&CE_StateDeliveryDecodeControl(bytes,sizeof bytes,&copy)==0,"actual canonical control roundtrip");return copy;
}
struct Packet {Image image;std::vector<uint8_t> wire;CE_StateDeliveryTicket ticket={};Packet(uint32_t s,uint32_t t,uint32_t m=0):image(s,t,m),wire(65536,0x7b){}};
struct Pair {
    CE_StateSender *tx;CE_StateReceiver *rx;unsigned published=0,native_apply_calls=0;uint32_t actual_applied=1;
    Pair(bool acknowledge=true) {
        tx=CE_StateSenderCreate(context,7);rx=CE_StateReceiverCreate(context,7);Must(tx&&rx,"fixed-size owners created");
        Image base(1,140);auto v=base.view();
        Must(CE_StateSenderBaselineSent(tx,&v,140,1,0)==0,"initial reliable Send1 retained without basis promotion");
        CE_StateKey sentinel={};Must(CE_StateSenderBasis(tx,&sentinel)==CE_DELIVERY_NOT_READY,"Send1 baseline is not application ACK");
        // This test caller invokes BaselineApplied only after its explicitly successful whole-Apply boundary.
        Must(CE_StateReceiverBaselineApplied(rx,&v,140,0)==0,"modeled complete baseline Apply creates exact ACK");
        if(acknowledge) ack(0);
    }
    ~Pair(){CE_StateSenderDestroy(tx);CE_StateReceiverDestroy(rx);}
    CE_StateDeliveryControl pending(uint64_t now) {CE_StateDeliveryControl c={};Must(CE_StateReceiverPoll(rx,now,&c)==0,"receiver has bounded control");return RoundControl(c);}
    CE_StateDeliveryControl ack(uint64_t now,int queued=1,bool deliver=true) {
        auto c=pending(now);Must(c.kind==CE_DELIVERY_APPLIED,"only actual whole-Apply proof is ACK");
        Must(CE_StateReceiverControlQueued(rx,&c,queued,now)==(queued?0:CE_DELIVERY_RETRY),"ACK queue result respected");
        if(deliver) Must(CE_StateSenderControl(tx,&c,now,0)==0,"exact application ACK accepted");return c;
    }
    void prepare(Packet &p,uint64_t now,int sent=1) {
        auto v=p.image.view();uint32_t n=0;
        Must(CE_StateSenderPrepare(tx,&v,p.image.tick,now,p.wire.data(),65536,&n,&p.ticket)==0,"actual delta sender prepare");p.wire.resize(n);
        Must(CE_StateSenderValidate(tx,&p.ticket,&v,p.image.tick)==0,"pre-Send ticket validates exact image/current basis");
        Must(CE_StateSenderSent(tx,&p.ticket,&v,p.image.tick,sent,now)==(sent?0:CE_DELIVERY_RETRY),"Send result commits exact ticket");
        if(sent) {Must(v.identity.sequence==p.ticket.key.sequence&&p.image.tick==p.ticket.key.tick,"publication bound to matching captured FrameInfo");++published;}
    }
    CE_StateDeliveryTicket receive(const Packet &p,uint64_t now) {
        CE_StateDeliveryTicket ticket={};Must(CE_StateReceiverPrepare(rx,p.wire.data(),uint32_t(p.wire.size()),now,&ticket)==0,"actual codec reconstructs surviving raw image");return ticket;
    }
    int apply(const CE_StateDeliveryTicket &ticket,uint64_t now,uint32_t life=0,bool whole_success=true,bool header_matches=true) {
        CE_StateImage v={};Must(CE_StateReceiverImage(rx,&ticket,&v)==0,"exact prepared generation readable");
        // Matches Session's envelope/FrameInfo checks before readiness and whole Apply. No real native actors execute.
        if(v.size<24||std::memcmp(v.data,"AUT9",4)||R32(v.data+4)!=ticket.key.epoch||R32(v.data+8)!=ticket.key.sequence||
           R32(v.data+12)!=ticket.key.tick||!header_matches) return CE_DELIVERY_INVALID;
        const uint32_t watermark=R32(v.data+16);
        if(watermark>life) return CE_DELIVERY_RETRY;
        if(watermark<life) {Must(CE_StateReceiverDiscard(rx,&ticket)==0,"stale lifecycle frame discarded without ACK");return CE_DELIVERY_STALE;}
        ++native_apply_calls;if(!whole_success) return CE_DELIVERY_INVALID;
        int result=CE_StateReceiverApplied(rx,&ticket,ticket.key.epoch,ticket.key.sequence,ticket.key.tick,now);
        if(result==0) actual_applied=ticket.key.sequence;return result;
    }
};
static Packet Manual(const Image &base,const Image &target,uint32_t offer=0) {
    Packet p(target.seq,target.tick,target.watermark);p.image=target;auto b=base.view(),t=target.view();uint32_t n=0;
    Must(CE_StateDeltaEncode(&b,&t,p.wire.data()+20,65536-20,&n)==0,"independent actual-codec packet construction");
    std::memcpy(p.wire.data(),"CSD1",4);p.wire[4]=0;p.wire[5]=1;p.wire[6]=0;p.wire[7]=offer?1:0;
    W32(p.wire.data()+8,target.tick);W32(p.wire.data()+12,base.tick);W32(p.wire.data()+16,offer);p.wire.resize(n+20);return p;
}
static CE_StateDeliveryControl Status(Pair &p,uint64_t now) {
    CE_StateDeliveryControl q={};Must(CE_StateSenderPoll(p.tx,now,&q)==0&&q.kind==CE_DELIVERY_STATUS,"1s triggers STATUS, not eviction");
    q=RoundControl(q);Must(CE_StateSenderControlQueued(p.tx,&q,1,now)==0,"one reliable status request queued");return q;
}
static CE_StateDeliveryControl Respond(Pair &p,const CE_StateDeliveryControl &q,uint64_t now) {
    Must(CE_StateReceiverControl(p.rx,&q,now,0)==0,"receiver examines exact status identity");
    auto response=p.pending(now);Must(CE_StateReceiverControlQueued(p.rx,&response,1,now)==0,"bounded status response queued");return response;
}
static void Basic() {
    Pair p(false);Packet before(2,141);auto v=before.image.view();uint32_t n=77;CE_StateDeliveryTicket t={};
    Must(CE_StateSenderPrepare(p.tx,&v,141,0,before.wire.data(),65536,&n,&t)==CE_DELIVERY_NOT_READY&&n==77,"no STATE before strong baseline ACK");p.ack(0);
    Packet a(2,141);p.prepare(a,0);auto ticket=p.receive(a,0);
    Must(p.apply(ticket,0,0,false)==CE_DELIVERY_INVALID,"failed whole-Apply boundary cannot ACK");
    CE_StateDeliveryControl none={};Must(CE_StateReceiverPoll(p.rx,0,&none)==CE_DELIVERY_RETRY,"Decode/failed Apply grants no ACK");
    Must(p.apply(ticket,0)==0,"whole success commits exact generation");auto c=p.ack(0);
    CE_StateKey basis={};Must(CE_StateSenderBasis(p.tx,&basis)==0&&basis.sequence==2,"only actual ACK promotes basis");
    Must(CE_StateSenderControl(p.tx,&c,0,0)==CE_DELIVERY_STALE,"duplicate ACK benign");
    Packet b(3,142);p.prepare(b,1000);auto bt=p.receive(b,1000);Must(p.apply(bt,1000)==0,"new base confirms candidate before next offer");p.ack(1000);
    Must(CE_StateReceiverPrepare(p.rx,a.wire.data(),uint32_t(a.wire.size()),1000,&ticket)==CE_DELIVERY_STALE,"late old delta cannot resurrect retired basis");
}
static void SlowAck() {
    Pair p;Packet c(2,141);p.prepare(c,0);auto ct=p.receive(c,600);Must(p.apply(ct,600)==0,"healthy600ms one-way offer Apply");auto ack=p.ack(600,1,false);
    Packet ordinary(3,142);p.prepare(ordinary,1000);Must(ordinary.ticket.offer_generation==0,"healthy1.2s RTT does not abandon C at1s");
    Must(CE_StateSenderControl(p.tx,&ack,1200,0)==0,"late healthy ACK promotes original C");
    Packet next(4,143);p.prepare(next,1300);auto nt=p.receive(next,1300);Must(p.apply(nt,1300)==0,"two-slot handoff survives healthy slow ACK");p.ack(1300);
}
static void LostOffer(bool superseded) {
    Pair p;Packet lost(2,141);p.prepare(lost,0);
    if(superseded) {Packet later(3,142);p.prepare(later,50);auto t=p.receive(later,50);Must(p.apply(t,50)==0,"newer ordinary state does not need lost offer");}
    auto q=Status(p,1000);CE_StateDeliveryControl sentinel={};
    Must(CE_StateSenderPoll(p.tx,1100,&sentinel)==CE_DELIVERY_RETRY,"no unbounded outstanding status queue");
    auto response=Respond(p,q,1100);Must(response.kind==(superseded?CE_DELIVERY_SUPERSEDED:CE_DELIVERY_RETIRED),"retirement never falsely claims an evicted Applied image was absent");
    Must(p.actual_applied==(superseded?3u:1u),"offer retirement fence never advances World Applied floor");
    Must(CE_StateSenderControl(p.tx,&response,1200,0)==0,"explicit fenced negative resolves candidate");
    CE_StateDeliveryTicket late={};Must(CE_StateReceiverPrepare(p.rx,lost.wire.data(),uint32_t(lost.wire.size()),1200,&late)==CE_DELIVERY_STALE,"late retired offer cannot Apply/ACK");
    auto false_late=response;false_late.kind=CE_DELIVERY_APPLIED;Must(CE_StateSenderControl(p.tx,&false_late,1200,0)==CE_DELIVERY_STALE,"abandoned late ACK is benign, not promotion");
    Packet next(superseded?4:3,143);p.prepare(next,1200);auto t=p.receive(next,1200);Must(p.apply(t,1200)==0,"fresh offer recovers without old tick replay");p.ack(1200);
}
static void Deferred() {
    Pair p;Packet c(2,141,1);p.prepare(c,0);auto ticket=p.receive(c,0);Must(p.apply(ticket,0,0)==CE_DELIVERY_RETRY,"DEFER leaves offer unacknowledged");
    auto query=Status(p,1000);auto response=Respond(p,query,1000);Must(response.kind==CE_DELIVERY_PENDING,"prepared/deferred offer cannot be retired");
    Must(CE_StateSenderControl(p.tx,&response,1000,0)==0,"PENDING keeps immutable candidate");
    Must(p.apply(ticket,1200,1)==0,"whole success only after exact lifecycle watermark");auto ack=p.ack(1200,0,false);
    query=Status(p,2200);Must(CE_StateReceiverControl(p.rx,&query,2200,0)==0,"status queries retained actualApplied offer");
    // The unsent ordinary ACK remains first. It may be queued now; its status response is the same actual proof.
    Must(CE_StateReceiverControlQueued(p.rx,&ack,1,2200)==0,"old ACK Send0 retains exact proof");
    auto proof=p.pending(2200);Must(proof.kind==CE_DELIVERY_APPLIED,"actualApplied proof survives missing original ACK");
    Must(CE_StateReceiverControlQueued(p.rx,&proof,1,2200)==0&&CE_StateSenderControl(p.tx,&proof,2200,0)==0,"STATUS proof legitimately promotes candidate");
    Packet d(3,142,1);p.prepare(d,2300);auto dt=p.receive(d,2300);Must(p.apply(dt,2300,1)==0,"new offer confirms old candidate and keeps both useful bases");p.ack(2300);
}
static void PreservePrepared() {
    Pair p;Packet deferred(2,141,1);p.prepare(deferred,0);auto ticket=p.receive(deferred,0);CE_StateImage original={};Must(CE_StateReceiverImage(p.rx,&ticket,&original)==0,"deferred image view");
    std::vector<uint8_t> before(original.data,original.data+original.size);auto old_ticket=ticket;
    Image unknown_base(6,145),target(7,146,1);auto unknown=Manual(unknown_base,target);
    CE_StateDeliveryTicket sentinel={};sentinel.generation=77;auto old=sentinel;
    Must(CE_StateReceiverPrepare(p.rx,unknown.wire.data(),uint32_t(unknown.wire.size()),10,&sentinel)==CE_DELIVERY_REPAIR_REQUIRED&&sentinel.generation==77,"unknown basis refuses without changing output ticket");
    auto malformed=deferred.wire;malformed.back()^=1;
    // Use a newer header/image so this reaches actual digest validation, rather than only a duplicate filter.
    auto bad=Manual(Image(1,140),Image(8,147,1));bad.wire.back()^=1;
    Must(CE_StateReceiverPrepare(p.rx,bad.wire.data(),uint32_t(bad.wire.size()),10,&sentinel)==CE_DELIVERY_INVALID,"new malformed target fails actual codec digest");
    CE_StateImage retained={};Must(CE_StateReceiverImage(p.rx,&old_ticket,&retained)==0&&before==std::vector<uint8_t>(retained.data,retained.data+retained.size),"unknown/failed Prepare preserves exact deferred raw and generation");
    Must(p.apply(old_ticket,10,1)==0,"prior deferred target remains usable after refusals");
    (void)old;(void)malformed;
}
static void GenerationAndSend0() {
    Pair p;Packet zero(2,141);p.prepare(zero,0,0);Must(p.published==0,"Send0 never publishes or retains a basis offer");
    Packet c(3,142);p.prepare(c,10);auto ct=p.receive(c,10);Must(p.apply(ct,10)==0,"fresh post-Send0 offer Apply");auto ack=p.ack(10,1,false);
    Packet staged(4,143);auto v=staged.image.view();uint32_t n=0;
    Must(CE_StateSenderPrepare(p.tx,&v,143,20,staged.wire.data(),65536,&n,&staged.ticket)==0,"ordinary target prepared against old base");
    Must(CE_StateSenderControl(p.tx,&ack,20,0)==0,"intervening applied ACK changes basis generation");
    Must(CE_StateSenderValidate(p.tx,&staged.ticket,&v,143)==CE_DELIVERY_STALE&&CE_StateSenderSent(p.tx,&staged.ticket,&v,143,1,20)==CE_DELIVERY_STALE,"staged old-base wire cannot validate/commit after ACK");
    Packet mutated(5,144);v=mutated.image.view();Must(CE_StateSenderPrepare(p.tx,&v,144,30,mutated.wire.data(),65536,&n,&mutated.ticket)==0,"next exact source snapshot prepared");
    mutated.image.bytes.back()^=1;v=mutated.image.view();Must(CE_StateSenderValidate(p.tx,&mutated.ticket,&v,144)==CE_DELIVERY_INVALID,"overwritten raw/capture cannot publish a different frame under old ticket");
}
static void Repair(bool overtaken) {
    Pair p;Packet c(2,141);p.prepare(c,0);auto ct=p.receive(c,0);Must(p.apply(ct,0)==0,"offer C actualApply");auto c_ack=p.ack(0);
    // Receiver still has B1+C2 while sender has promoted C2: the original raw self-eviction counterexample.
    Image too_large(10,142,7,65536);std::fill(too_large.bytes.begin()+24,too_large.bytes.end(),0xfe);auto v=too_large.view();uint8_t output[65536];std::memset(output,0xa5,sizeof output);uint32_t n=77;CE_StateDeliveryTicket ignored={};
    Must(CE_StateSenderPrepare(p.tx,&v,142,10,output,sizeof output,&n,&ignored)==CE_DELIVERY_REPAIR_REQUIRED&&n==77&&output[0]==0xa5,"delta overflow explicitly requests repair, never truncates");
    Image raw(11,142,7,65536);std::fill(raw.bytes.begin()+24,raw.bytes.end(),0xfe);v=raw.view();CE_StateRepairGate gate={3,1,7};CE_StateDeliveryControl announce={};CE_StateDeliveryTicket ticket={};
    Must(CE_StateSenderPrepareRepair(p.tx,&v,142,&gate,10,&announce,&ticket)==0&&announce.active_base.sequence==2,"fresh frozen-tick repair names full sender active base");
    CE_StateImage held={};Must(CE_StateSenderRepairImage(p.tx,&ticket,&held)==CE_DELIVERY_NOT_READY,"cross-channel raw cannot precede exact announcement receipt");
    auto control=RoundControl(announce);Must(CE_StateSenderControlQueued(p.tx,&control,0,10)==CE_DELIVERY_RETRY,"announcement Send0 retains exact repair");
    auto ready=control;ready.kind=CE_DELIVERY_REPAIR_READY;Must(CE_StateSenderControl(p.tx,&ready,10,0)==CE_DELIVERY_STALE,"unsent announcement cannot acquire forged readiness");
    Must(CE_StateSenderControlQueued(p.tx,&control,1,10)==0,"repair announcement queued");
    auto bad_ready=ready;bad_ready.key.digest[0]^=1;
    Must(CE_StateSenderControl(p.tx,&bad_ready,10,0)==CE_DELIVERY_INVALID&&CE_StateSenderControl(p.tx,&bad_ready,10,1)==CE_DELIVERY_INVALID,"known repair-ready identity validates equally during terminal drain");
    bad_ready=ready;++bad_ready.gate.lifecycle_watermark;
    Must(CE_StateSenderControl(p.tx,&bad_ready,10,0)==CE_DELIVERY_INVALID&&CE_StateSenderControl(p.tx,&bad_ready,10,1)==CE_DELIVERY_INVALID,"known repair-ready gate validates before terminal no-op");
    auto wrong_gate=gate;++wrong_gate.lifecycle_watermark;Must(CE_StateReceiverExpectRepair(p.rx,&control,&wrong_gate,10)==CE_DELIVERY_INVALID,"quiet lifecycle gate must match announcement exactly");
    auto wrong_base=control;wrong_base.active_base.digest[0]^=1;Must(CE_StateReceiverExpectRepair(p.rx,&wrong_base,&gate,10)==CE_DELIVERY_INVALID,"known active base identity cannot be relabeled during raw repair");
    Must(CE_StateReceiverExpectRepair(p.rx,&control,&gate,10)==0,"caller-admitted WAIT/quiet-watermark announcement installed");
    auto future_status=control;future_status.kind=CE_DELIVERY_STATUS;future_status.gate={};future_status.active_base={};++future_status.offer_generation;++future_status.key.sequence;
    Must(CE_StateReceiverControl(p.rx,&future_status,10,0)==CE_DELIVERY_INVALID&&CE_StateReceiverControl(p.rx,&future_status,10,1)==CE_DELIVERY_INVALID,"future STATUS cannot bypass expected-repair phase during terminal drain");
    ready=p.pending(10);
    Must(CE_StateSenderControl(p.tx,&ready,10000,1)==CE_DELIVERY_STALE&&CE_StateSenderRepairImage(p.tx,&ticket,&held)==CE_DELIVERY_NOT_READY,"terminal valid repair-ready changes neither readiness nor clock");
    Must(ready.kind==CE_DELIVERY_REPAIR_READY&&CE_StateReceiverControlQueued(p.rx,&ready,1,10)==0&&CE_StateSenderControl(p.tx,&ready,10,0)==0,"exact receiver installation gates reliable raw send");
    Must(CE_StateSenderRepairImage(p.tx,&ticket,&held)==0&&held.size==65536&&!std::memcmp(held.data,raw.bytes.data(),65536),"full64KiB raw has no added header/cap increase");
    Must(CE_StateSenderSent(p.tx,&ticket,&held,142,0,10)==CE_DELIVERY_RETRY&&CE_StateSenderRepairImage(p.tx,&ticket,&held)==0,"raw Send0 retains exact bytes/generation and matching frame obligation");
    Must(CE_StateSenderSent(p.tx,&ticket,&held,142,1,20)==0,"one reliable repair image Send1");
    Packet forbidden(12,143,7);auto forbidden_view=forbidden.image.view();n=99;
    Must(CE_StateSenderPrepare(p.tx,&forbidden_view,143,30,forbidden.wire.data(),65536,&n,&ignored)==CE_DELIVERY_REPAIR_REQUIRED&&n==99,"repair quiescence cannot emit a newer STATE fragment that retires repair");
    CE_StateDeliveryTicket rt={};Must(CE_StateReceiverPrepareRepair(p.rx,held.data,held.size,300,&rt)==0,"reliable repair can arrive after250ms STATE TTL");
    auto old=Manual(Image(1,140),Image(9,141));Must(CE_StateReceiverPrepare(p.rx,old.wire.data(),uint32_t(old.wire.size()),300,&ignored)==CE_DELIVERY_STALE,"delayed old channel2 STATE cannot overwrite prepared channel3 repair");
    if(overtaken) {
        Must(p.apply(rt,300,8)==CE_DELIVERY_STALE,"future lifecycle overtaking makes old repair unapplyable; never rewind it");
        CE_StateDeliveryControl none={};Must(CE_StateReceiverPoll(p.rx,300,&none)==CE_DELIVERY_RETRY,"stale repair never ACKs");
        Must(CE_StateSenderPoll(p.tx,30020,&none)==CE_DELIVERY_TIMEOUT&&CE_StateReceiverPoll(p.rx,30010,&none)==CE_DELIVERY_TIMEOUT,"broken quiet-watermark transaction fails bounded, not false recovery");
    } else {
        Must(p.apply(rt,300,7)==0,"repair wholeApply only at exact drained lifecycle watermark");auto ack=p.ack(300,1,false);
        auto old_status=c_ack;old_status.kind=CE_DELIVERY_STATUS;
        Must(CE_StateReceiverControl(p.rx,&old_status,300,0)==0,"old active C2 remains retained while repair ACK is in flight");
        auto proof=p.pending(300);Must(proof.kind==CE_DELIVERY_APPLIED&&proof.key.sequence==2,"raw active-base identity conserves sender C2 before caching D");
        Must(CE_StateReceiverControlQueued(p.rx,&proof,1,300)==0&&CE_StateSenderControl(p.tx,&ack,300,0)==0,"repair ACK promotes D only after genuine whole Apply");
        CE_StateKey basis={};Must(CE_StateSenderBasis(p.tx,&basis)==0&&basis.sequence==11,"repair ACK alone promotes fresh basis");
        Packet next(12,143,7);p.prepare(next,1200);auto nt=p.receive(next,1200);Must(p.apply(nt,1200,7)==0,"B1/C2/D repair handoff conserves active base and resumes deltas");p.ack(1200);
        Must(CE_StateSenderControl(p.tx,&ack,1200,0)==CE_DELIVERY_STALE,"late repair ACK harmless after next promotion");
        Must(CE_StateReceiverControl(p.rx,&old_status,1200,0)==0,"old evicted actualApplied status handled");proof=p.pending(1200);
        Must(proof.kind==CE_DELIVERY_SUPERSEDED,"evicted previously applied C is SUPERSEDED, never falsely RETIRED");
        Must(CE_StateSenderBasis(p.tx,&basis)==0,"current basis for repeated repair rate test");
        CE_StateDeliveryControl missing={};missing.kind=CE_DELIVERY_MISSING_BASE;missing.key=basis;
        Must(CE_StateSenderControl(p.tx,&missing,1200,0)==CE_DELIVERY_REPAIR_REQUIRED,"new explicit repair request admitted without automatic reliable send");
        Image fresh(13,143,7);auto fresh_view=fresh.view();auto untouched=announce;auto old_ticket=ticket;++gate.wait_generation;
        Must(CE_StateSenderPrepareRepair(p.tx,&fresh_view,143,&gate,1200,&announce,&ticket)==CE_DELIVERY_RETRY&&
             !std::memcmp(&announce,&untouched,sizeof announce)&&!std::memcmp(&ticket,&old_ticket,sizeof ticket),"one reliable repair per30s with unchanged outputs on rate refusal");
    }
}
static void BadAndTerminal() {
    Pair p;Packet c(2,141);p.prepare(c,0);auto t=p.receive(c,0);Must(p.apply(t,0)==0,"candidate for control negatives");auto ack=p.ack(0,1,false);
    auto bad=ack;++bad.key.tick;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK tick bound");bad=ack;bad.key.digest[0]^=1;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK exact digest bound");
    bad=ack;++bad.key.epoch;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK epoch bound");
    bad=ack;bad.key.context[0]^=1;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK source context bound");
    bad=ack;++bad.key.sequence;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK target sequence bound");
    bad=ack;++bad.key.size;Must(CE_StateSenderControl(p.tx,&bad,0,0)==CE_DELIVERY_INVALID,"ACK full image size bound");
    Must(CE_StateSenderControl(p.tx,&ack,0,1)==CE_DELIVERY_STALE,"terminal validation cannot promote basis");CE_StateKey key={};Must(CE_StateSenderBasis(p.tx,&key)==0&&key.sequence==1,"terminal retains old actual basis");
    auto q=Status(p,1000);bad=q;bad.key.digest[0]^=1;Must(CE_StateReceiverControl(p.rx,&bad,1000,0)==CE_DELIVERY_INVALID,"STATUS cannot retire same-generation wrong identity");
    Must(CE_StateReceiverControl(p.rx,&q,1000,1)==CE_DELIVERY_STALE,"terminal status creates no response");CE_StateDeliveryControl none={};Must(CE_StateReceiverPoll(p.rx,1000,&none)==CE_DELIVERY_RETRY,"terminal has no response or repair action");
    Must(CE_StateSenderControl(p.tx,&ack,1000,0)==0,"nonterminal exact proof still valid");
    Must(CE_StateSenderPoll(p.tx,999,&none)==CE_DELIVERY_INVALID,"caller monotonic time cannot go backwards");
}
static void TerminalPairs() {
    Pair p;Packet c(2,141);p.prepare(c,0);auto t=p.receive(c,0);Must(p.apply(t,0)==0,"terminal paired fixture whole-Apply boundary");auto ack=p.ack(0,1,false);
    auto known_bad=[&](CE_StateDeliveryControl bad) {
        auto status=bad;status.kind=CE_DELIVERY_STATUS;
        for(int terminal=0;terminal!=2;++terminal) {
            Must(CE_StateSenderControl(p.tx,&bad,0,terminal)==CE_DELIVERY_INVALID,"sender normal/terminal known proof identity is invalid");
            Must(CE_StateReceiverControl(p.rx,&status,0,terminal)==CE_DELIVERY_INVALID,"receiver normal/terminal known STATUS identity is invalid");
        }
    };
    auto bad=ack;bad.key.digest[0]^=1;known_bad(bad);bad=ack;++bad.key.tick;known_bad(bad);
    bad=ack;++bad.key.sequence;known_bad(bad);bad=ack;++bad.key.size;known_bad(bad);bad=ack;++bad.offer_generation;known_bad(bad);
    for(unsigned kind:{unsigned(CE_DELIVERY_PENDING),unsigned(CE_DELIVERY_RETIRED),unsigned(CE_DELIVERY_SUPERSEDED)}) {
        bad=ack;bad.kind=kind;bad.key.digest[3]^=1;
        for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&bad,0,terminal)==CE_DELIVERY_INVALID,"all proof kinds validate known identity before terminal return");
    }
    bad=ack;bad.offer_generation=99;bad.key.sequence=99;bad.key.tick=999;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&bad,0,terminal)==CE_DELIVERY_INVALID,"impossible future proof is not benign stale");
    CE_StateKey basis={};Must(CE_StateSenderBasis(p.tx,&basis)==0&&basis.sequence==1,"invalid known controls preserve old acknowledged basis");auto baseline=basis;
    CE_StateDeliveryControl none={};Must(CE_StateReceiverPoll(p.rx,0,&none)==CE_DELIVERY_RETRY,"invalid STATUS creates no response or retirement");
    auto status=ack;status.kind=CE_DELIVERY_STATUS;
    Must(CE_StateSenderControl(p.tx,&ack,10000,1)==CE_DELIVERY_STALE&&CE_StateReceiverControl(p.rx,&status,10000,1)==CE_DELIVERY_STALE,"valid terminal proof/status remain inert");
    Must(CE_StateSenderPoll(p.tx,0,&none)==CE_DELIVERY_RETRY&&CE_StateReceiverPoll(p.rx,0,&none)==CE_DELIVERY_RETRY,"terminal dry calls do not change either monotonic clock");
    auto missing=ack;missing.kind=CE_DELIVERY_MISSING_BASE;missing.offer_generation=0;missing.key=basis;
    bad=missing;++bad.key.tick;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&bad,0,terminal)==CE_DELIVERY_INVALID,"missing-base known identity validates before terminal return");
    Must(CE_StateSenderControl(p.tx,&missing,10000,1)==CE_DELIVERY_STALE,"terminal missing-base proof cannot enter repair");
    Packet transient(3,142);p.prepare(transient,0,0);Must(p.published==1,"terminal missing-base neither quiesces STATE nor publishes Send0");
    Must(CE_StateSenderControl(p.tx,&ack,0,0)==0,"candidate retained exactly after invalid and dry controls");
    bad=ack;bad.key.digest[0]^=1;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&bad,0,terminal)==CE_DELIVERY_INVALID,"retained promoted basis still validates corrupt duplicate identity");
    Packet d(4,143);p.prepare(d,1000);auto dt=p.receive(d,1000);Must(p.apply(dt,1000)==0,"normal handoff still succeeds after terminal dry calls");p.ack(1000);
    bad=status;++bad.key.tick;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateReceiverControl(p.rx,&bad,1000,terminal)==CE_DELIVERY_INVALID,"retained receiver base validates identity after promotion");
    auto unknown=ack;unknown.key.digest[0]^=1;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&unknown,1000,terminal)==CE_DELIVERY_STALE,"unretained unknown past proof remains benign without unbounded history");
    unknown=ack;unknown.offer_generation=0;unknown.key=baseline;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateSenderControl(p.tx,&unknown,1000,terminal)==CE_DELIVERY_STALE,"genuine old baseline proof remains benign after eviction");
    Must(CE_StateSenderBasis(p.tx,&basis)==0&&basis.sequence==4&&p.native_apply_calls==2,"controls never introduce a World callback or change latest legitimate basis");
}
static void TerminalPrepared() {
    Pair p;Packet c(2,141,1);p.prepare(c,0);auto ticket=p.receive(c,0);CE_StateImage image={};Must(CE_StateReceiverImage(p.rx,&ticket,&image)==0,"prepared offer retained before terminal validation");
    const std::vector<uint8_t> bytes(image.data,image.data+image.size);auto query=Status(p,1000);auto future=query;++future.offer_generation;++future.key.sequence;++future.key.tick;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateReceiverControl(p.rx,&future,1000,terminal)==CE_DELIVERY_INVALID,"future STATUS cannot bypass an existing prepared-offer phase");
    auto bad=query;bad.key.digest[0]^=1;
    for(int terminal=0;terminal!=2;++terminal) Must(CE_StateReceiverControl(p.rx,&bad,1000,terminal)==CE_DELIVERY_INVALID,"prepared known STATUS identity validates equally");
    Must(CE_StateReceiverControl(p.rx,&query,10000,1)==CE_DELIVERY_STALE,"valid terminal prepared STATUS emits no PENDING");
    CE_StateDeliveryControl none={};Must(CE_StateReceiverPoll(p.rx,1000,&none)==CE_DELIVERY_RETRY,"terminal call neither changes clock nor queues response");
    Must(CE_StateReceiverImage(p.rx,&ticket,&image)==0&&bytes==std::vector<uint8_t>(image.data,image.data+image.size),"prepared raw and ticket unchanged by all controls");
    Must(p.apply(ticket,1000,1)==0,"original prepared target remains whole-Apply eligible");p.ack(1000);
    Packet next(3,142,1);p.prepare(next,1100);auto nt=p.receive(next,1100);Must(p.apply(nt,1100,1)==0,"terminal future status did not advance retirement fence");p.ack(1100);
}
static void LeaveOrders() {
    for(bool malformed:{false,true}) for(bool leave_first:{false,true}) for(bool receiver:{false,true}) {
        Pair p;Packet c(2,141);p.prepare(c,0);auto ticket=p.receive(c,0);Must(p.apply(ticket,0)==0,"leave-order fixture whole-Apply boundary");auto ack=p.ack(0,1,false);auto control=ack;
        if(receiver) control.kind=CE_DELIVERY_STATUS;if(malformed) control.key.digest[0]^=1;control=RoundControl(control);
        bool peer_leaving=false,error=false;
        auto consume=[&] {const int result=receiver?CE_StateReceiverControl(p.rx,&control,0,peer_leaving):CE_StateSenderControl(p.tx,&control,0,peer_leaving);
            if(result==CE_DELIVERY_INVALID) error=true;else Must(result==CE_DELIVERY_OK||result==CE_DELIVERY_STALE,"caller retains strict validated terminal outcome");};
        // Session-shaped LEAVING is sticky but never skips controls already queued behind it. Actual Session is not run.
        if(leave_first) peer_leaving=true;consume();if(!leave_first) peer_leaving=true;
        Must(peer_leaving&&error==malformed,"malformed-before-leave and leave-before-malformed both retain error; valid controls remain graceful");
        CE_StateKey basis={};Must(CE_StateSenderBasis(p.tx,&basis)==0,"basis remains inspectable during terminal drain");
        const uint32_t expected=malformed||receiver||leave_first?1:2;
        Must(basis.sequence==expected&&p.native_apply_calls==1,"leave ordering introduces no mutation beyond legitimate pre-leave ACK");
        if(malformed) {
            Must(CE_StateSenderControl(p.tx,&ack,0,0)==0,"malformed terminal control did not consume candidate");
            if(receiver) {auto valid=ack;valid.kind=CE_DELIVERY_STATUS;Must(CE_StateReceiverControl(p.rx,&valid,0,0)==0,"valid known STATUS still works after malformed leave drain");}
        }
    }
}
static void Wire() {
    Pair p;Packet a(2,141);p.prepare(a,0);auto t=p.receive(a,0);Must(p.apply(t,0)==0,"wire fixture actualApplied");auto control=p.pending(0);
    uint8_t bytes[184];Must(CE_StateDeliveryEncodeControl(&control,bytes,184)==0,"control encoded");
    for(unsigned n=0;n<184;++n) {CE_StateDeliveryControl out;std::memset(&out,0x87,sizeof out);auto old=out;Must(CE_StateDeliveryDecodeControl(bytes,n,&out)==CE_DELIVERY_INVALID&&!std::memcmp(&out,&old,sizeof out),"all truncated controls refuse unchanged");}
    for(unsigned i:{0u,4u,5u,6u,7u,12u,16u,20u,104u}) {auto old=bytes[i];bytes[i]^=0xff;CE_StateDeliveryControl out={};Must(CE_StateDeliveryDecodeControl(bytes,184,&out)==CE_DELIVERY_INVALID,"bad framing/unused fields canonical refusal");bytes[i]=old;}
    auto alias=control;Must(CE_StateDeliveryEncodeControl(&alias,reinterpret_cast<uint8_t*>(&alias),sizeof alias)==CE_DELIVERY_INVALID,"control descriptor output alias refused");
    auto v=a.image.view();Must(CE_StateDeliveryKey(&v,141,reinterpret_cast<CE_StateKey*>(&v))==CE_DELIVERY_INVALID,"key metadata cannot overwrite input descriptor");
}
static void LossSchedules() {
    unsigned schedules=0;
    for(unsigned mask=0;mask<32;++mask) {
        std::vector<unsigned> order;for(unsigned i=0;i<5;++i) if(mask&(1u<<i)) order.push_back(i);
        do {
            ++schedules;Pair p;std::vector<Packet> packets;
            for(unsigned i=0;i<5;++i) {packets.emplace_back(i+2,i+141);p.prepare(packets.back(),i*50);}
            uint32_t floor=1;uint64_t now=1000;
            for(auto index:order) {
                auto &packet=packets[index];CE_StateDeliveryTicket t={};int result=CE_StateReceiverPrepare(p.rx,packet.wire.data(),uint32_t(packet.wire.size()),now++,&t);
                if(packet.image.seq>floor) {Must(result==0&&p.apply(t,now++)==0,"ordered-subset actual whole-Apply gate accepts exact latest bytes");floor=packet.image.seq;}
                else Must(result==CE_DELIVERY_STALE,"reordered older state never overwrites actual Applied floor");
                Must(CE_StateReceiverPrepare(p.rx,packet.wire.data(),uint32_t(packet.wire.size()),now++,&t)==CE_DELIVERY_STALE,"duplicate cannot Apply/ACK again");
            }
            Must(p.actual_applied==floor,"loss schedule actual callback floor");auto query=Status(p,2000);auto answer=Respond(p,query,2000);
            Must(answer.kind==CE_DELIVERY_APPLIED||answer.kind==CE_DELIVERY_RETIRED||answer.kind==CE_DELIVERY_SUPERSEDED,"closed loss schedule has exact retained proof or fenced refusal");
            Must(CE_StateSenderControl(p.tx,&answer,2000,0)==0,"only actual proof/explicit retirement resolves candidate");
            Packet recovery(7,146);p.prepare(recovery,2500);auto t=p.receive(recovery,2500);Must(p.apply(t,2500)==0,"all finite ordered-subset schedules retain a useful basis for fresh data");p.ack(2500);
        } while(std::next_permutation(order.begin(),order.end()));
    }
    Must(schedules==326,"complete bounded five-target drop/reorder schedule space");
}
static void KeyVector() {
    Image image(9,151);auto v=image.view();CE_StateKey key={};uint8_t image_digest[32],wire[184];
    Must(CE_StateImageDigest(&v,image_digest)==0&&CE_StateDeliveryKey(&v,151,&key)==0,"actual image and delivery digests");
    CE_StateDeliveryControl c={};c.kind=CE_DELIVERY_APPLIED;c.offer_generation=3;c.key=key;
    Must(CE_StateDeliveryEncodeControl(&c,wire,sizeof wire)==0,"actual control encoding");
    std::printf("{\"image_digest\":\"");for(auto b:image_digest)std::printf("%02x",unsigned(b));
    std::printf("\",\"delivery_digest\":\"");for(auto b:key.digest)std::printf("%02x",unsigned(b));
    std::printf("\",\"wire\":\"");for(auto b:wire)std::printf("%02x",unsigned(b));std::printf("\"}\n");
}
static void Cadence() {
    // A lost offer is resolved one status period after its Send, and the next offer follows at once: the basis cannot
    // fall a second behind per lost image while the receiver keeps answering.
    Pair p;Packet lost(2,141);p.prepare(lost,0);Must(lost.ticket.offer_generation==1,"first image after the baseline ACK is an offer");
    CE_StateDeliveryControl none={};Must(CE_StateSenderPoll(p.tx,CE_STATE_DELIVERY_STATUS_MS-1,&none)==CE_DELIVERY_RETRY,"no status before one period");
    auto q=Status(p,CE_STATE_DELIVERY_STATUS_MS);auto response=Respond(p,q,CE_STATE_DELIVERY_STATUS_MS);
    Must(response.kind==CE_DELIVERY_RETIRED&&CE_StateSenderControl(p.tx,&response,CE_STATE_DELIVERY_STATUS_MS,0)==0,"never-arrived offer retired after one period");
    Packet next(3,142);p.prepare(next,CE_STATE_DELIVERY_OFFER_MS);Must(next.ticket.offer_generation==2,"a new offer follows the retirement at once");
    auto t=p.receive(next,CE_STATE_DELIVERY_OFFER_MS);Must(p.apply(t,CE_STATE_DELIVERY_OFFER_MS)==0,"replacement offer applies");p.ack(CE_STATE_DELIVERY_OFFER_MS);
    const uint64_t at=CE_STATE_DELIVERY_OFFER_MS;
    Packet early(4,143);p.prepare(early,2*at-1);Must(early.ticket.offer_generation==0,"no offer inside one offer period");
    Packet due(5,144);p.prepare(due,2*at);Must(due.ticket.offer_generation==3,"an offer each offer period while none is pending");
    Must(CE_STATE_DELIVERY_STATUS_MS<=250&&CE_STATE_DELIVERY_OFFER_MS<=250,"lost offers cost at most a quarter second");
}
#endif
#ifdef CE_DELIVERY_WORLD_FIXTURE
static void ActualWorldAdmission() {
    // Real frozen World9 serialization/readiness + actual delivery/codec. Native resource preflight/WorldApply do not run.
    auto *f=Fixture();auto *decoded=CE_WorldFrameCreate();Must(decoded!=nullptr,"actual frame allocation");
    CE_StateSender *tx=CE_StateSenderCreate(context,7);CE_StateReceiver *rx=CE_StateReceiverCreate(context,7);
    uint8_t bytes[65536],wire[65536];uint32_t n=0,wire_n=0;unsigned whole_callbacks=0;
    auto encode=[&](uint32_t seq,uint32_t mark) {
        f->sequence=seq;f->tick=140;f->lifecycle_baseline=seq==1?0:1;f->lifecycle_watermark=mark;
        Must(CE_WorldEncode(f,bytes,sizeof bytes,&n)==1,"actual authored7-record World9 Encode");
        CE_StateImage v={};std::memcpy(v.identity.context,context,32);v.identity.epoch=7;v.identity.sequence=seq;v.data=bytes;v.size=n;return v;
    };
    auto base=encode(1,0);Must(CE_WorldDecode(decoded,bytes,n)==1,"actual complete baseline Decode");
    Must(CE_StateSenderBaselineSent(tx,&base,140,1,0)==0&&CE_StateReceiverBaselineApplied(rx,&base,140,0)==0,"caller-modeled whole baseline success hooks");
    CE_StateDeliveryControl ack={};Must(CE_StateReceiverPoll(rx,0,&ack)==0&&CE_StateReceiverControlQueued(rx,&ack,1,0)==0&&CE_StateSenderControl(tx,&ack,0,0)==0,"strong initial baseline ACK");
    CE_StateDeliveryTicket ticket={},received={};auto target=encode(2,7);
    Must(CE_StateSenderPrepare(tx,&target,140,0,wire,sizeof wire,&wire_n,&ticket)==0&&CE_StateSenderSent(tx,&ticket,&target,140,1,0)==0,"actual World9 bytes staged/sent through real delta codec");
    Must(CE_StateReceiverPrepare(rx,wire,wire_n,0,&received)==0,"actual World9 reconstruction before caller Decode");
    CE_StateImage view={};Must(CE_StateReceiverImage(rx,&received,&view)==0&&CE_WorldDecode(decoded,view.data,view.size)==1,"complete actual World9 Decode of reconstructed bytes");
    CE_WorldInfo info={};Must(CE_WorldFrameInfo(decoded,&info)==1&&info.epoch==received.key.epoch&&info.frame_sequence==received.key.sequence&&info.tick==received.key.tick&&info.level==852,"actual Session-style envelope/FrameInfo admission");
    active=true;config={CE_WORLD_REPLICA,7,1,852};poisoned=life_failed=false;life_enabled=true;life_baseline=1;life_committed_tick=140;applied_sequence=1;
    life_applied=6;Must(LifeReadiness(*decoded)==CE_WORLD_DEFER,"actual missing lifecycle operation defers");
    Must(CE_StateReceiverPoll(rx,0,&ack)==CE_DELIVERY_RETRY,"real Decode+DEFER grants no application ACK");
    life_applied=7;Must(LifeReadiness(*decoded)==CE_WORLD_READY,"actual exact watermark is ready");
    Must(CE_StateReceiverPoll(rx,0,&ack)==CE_DELIVERY_RETRY,"READY metadata alone still grants no ACK");
    life_applied=8;Must(LifeReadiness(*decoded)==CE_WORLD_STALE,"actual future lifecycle overtakes unapplied target without rewind");
    Must(CE_StateReceiverDiscard(rx,&received)==0&&CE_StateReceiverPoll(rx,0,&ack)==CE_DELIVERY_RETRY&&whole_callbacks==0,"stale actual frame never reaches modeled whole-Apply callback or ACK");
    target=encode(3,8);Must(CE_StateSenderPrepare(tx,&target,140,10,wire,sizeof wire,&wire_n,&ticket)==0&&CE_StateSenderSent(tx,&ticket,&target,140,1,10)==0&&CE_StateReceiverPrepare(rx,wire,wire_n,10,&received)==0,"fresh same-source-tick capture with newer frame sequence");
    Must(CE_StateReceiverImage(rx,&received,&view)==0&&CE_WorldDecode(decoded,view.data,view.size)==1&&LifeReadiness(*decoded)==CE_WORLD_READY,"actual current-watermark frame admits before callback");
    ++whole_callbacks;Must(CE_StateReceiverApplied(rx,&received,7,3,140,10)==0,"only caller-modeled successful WHOLE Apply commits delivery");applied_sequence=3;
    Must(CE_StateReceiverPoll(rx,10,&ack)==CE_DELIVERY_RETRY,"non-offer whole Apply does not invent basis ACK");
    CE_StateDeliveryControl query={};Must(CE_StateSenderPoll(tx,1000,&query)==0&&CE_StateSenderControlQueued(tx,&query,1,1000)==0&&CE_StateReceiverControl(rx,&query,1000,0)==0,"status resolves coalesced stale offer");
    Must(CE_StateReceiverPoll(rx,1000,&ack)==0&&ack.kind==CE_DELIVERY_SUPERSEDED&&CE_StateReceiverControlQueued(rx,&ack,1,1000)==0&&CE_StateSenderControl(tx,&ack,1000,0)==0,"no false absent proof for superseded frame");
    target=encode(4,8);Must(CE_StateSenderPrepare(tx,&target,140,1200,wire,sizeof wire,&wire_n,&ticket)==0&&CE_StateSenderSent(tx,&ticket,&target,140,1,1200)==0&&CE_StateReceiverPrepare(rx,wire,wire_n,1200,&received)==0,"fresh actual encoded offer after explicit resolution");
    Must(CE_StateReceiverImage(rx,&received,&view)==0&&CE_WorldDecode(decoded,view.data,view.size)==1&&LifeReadiness(*decoded)==CE_WORLD_READY,"actual new offer readiness");
    ++whole_callbacks;Must(CE_StateReceiverApplied(rx,&received,7,4,140,1200)==0&&CE_StateReceiverPoll(rx,1200,&ack)==0&&ack.kind==CE_DELIVERY_APPLIED,"whole callback success creates exact offer ACK");
    Must(CE_StateReceiverControlQueued(rx,&ack,1,1200)==0&&CE_StateSenderControl(tx,&ack,1200,0)==0,"actual serialized image basis promoted after exact proof");
    decoded->tick=139;Must(LifeReadiness(*decoded)==CE_WORLD_INVALID,"actual committed source clock cannot rewind");
    Must(whole_callbacks==2,"only two authored successful whole-Apply boundaries executed; no native Apply claimed");
    CE_StateSenderDestroy(tx);CE_StateReceiverDestroy(rx);CE_WorldFrameDestroy(decoded);CE_WorldFrameDestroy(f);active=false;
}
#endif
int main(int argc,char **argv) {
#ifdef CE_DELIVERY_WORLD_FIXTURE
    if(argc!=2||std::strcmp(argv[1],"actual-world-admission")) return 2;
    ActualWorldAdmission();std::printf("{\"case\":\"actual-world-admission\",\"checks\":%u,\"failed\":0}\n",verified);return 0;
#else
    const std::string mode=argc==2?argv[1]:"";
    if(mode=="key-vector") {KeyVector();return 0;}
    if(mode=="basic") Basic();else if(mode=="slow-ack") SlowAck();else if(mode=="lost-offer") LostOffer(false);else if(mode=="cadence") Cadence();
    else if(mode=="coalesced-offer") LostOffer(true);else if(mode=="deferred") Deferred();else if(mode=="preserve") PreservePrepared();
    else if(mode=="generation-send0") GenerationAndSend0();else if(mode=="repair") Repair(false);else if(mode=="repair-lifecycle-overtake") Repair(true);
    else if(mode=="bad-terminal") BadAndTerminal();else if(mode=="wire") Wire();
    else if(mode=="terminal-pairs") TerminalPairs();else if(mode=="terminal-prepared") TerminalPrepared();else if(mode=="leave-orders") LeaveOrders();
    else if(mode=="loss-schedules") LossSchedules();
    else {std::fprintf(stderr,"unknown bounded case\n");return 2;}
    std::printf("{\"case\":\"%s\",\"checks\":%u,\"failed\":0}\n",mode.c_str(),verified);return 0;
#endif
}
