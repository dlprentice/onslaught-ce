// SPDX-License-Identifier: GPL-3.0-only
// Include the unchanged production owner definition so read-only calls can be checked byte for byte.
#include "state_delivery.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static unsigned checks=0;
static void Must(bool value,const char *why) {++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(2);}}
static const uint8_t context[32]={1,2,3,4};
struct Image {
    std::array<uint8_t,512> bytes{};uint32_t sequence,tick;
    Image(uint32_t s,uint32_t t):sequence(s),tick(t) {for(unsigned i=0;i<bytes.size();++i)bytes[i]=uint8_t(i+s);}
    CE_StateImage view() const {CE_StateImage v={};std::memcpy(v.identity.context,context,32);v.identity.epoch=7;v.identity.sequence=sequence;v.data=bytes.data();v.size=bytes.size();return v;}
};
static CE_StateKey Key(uint32_t sequence,uint32_t tick) {
    Image image(sequence,tick);auto view=image.view();CE_StateKey key={};
    Must(CE_StateDeliveryKey(&view,tick,&key)==0,"actual image key");return key;
}
struct Receiver {
    CE_StateReceiver *r;
    explicit Receiver(bool seeded=true,uint32_t sequence=1,uint32_t tick=140):r(CE_StateReceiverCreate(context,7)) {
        Must(r!=nullptr,"receiver allocation");
        if(seeded) {Image image(sequence,tick);auto view=image.view();Must(CE_StateReceiverBaselineApplied(r,&view,tick,100)==0,"actual initial baseline applied");}
    }
    ~Receiver(){CE_StateReceiverDestroy(r);}
};
static CE_StateDeliveryControl Announcement(uint32_t sequence=3,uint32_t tick=142,uint32_t offer=1) {
    CE_StateDeliveryControl c={};c.kind=CE_DELIVERY_REPAIR;c.offer_generation=offer;c.gate={3,1,7};c.key=Key(sequence,tick);c.active_base=Key(1,140);return c;
}
#ifndef CE_REPAIR_CHECK_LEGACY
static CE_StateDeliveryTicket Prepare(CE_StateReceiver *r,uint32_t sequence=2,uint32_t tick=141) {
    Image target(sequence,tick);auto image=target.view(),base=View(r->slots[r->b]);std::array<uint8_t,2048> wire{};uint32_t n=0;
    Must(CE_StateDeltaEncode(&base,&image,wire.data()+20,wire.size()-20,&n)==0,"actual delta encoding");
    std::memcpy(wire.data(),"CSD1",4);wire[5]=1;wire[7]=1;Put(wire.data()+8,tick);Put(wire.data()+12,r->slots[r->b].key.tick);Put(wire.data()+16,1);
    CE_StateDeliveryTicket ticket={};Must(CE_StateReceiverPrepare(r,wire.data(),n+20,100,&ticket)==0,"actual prepared image");return ticket;
}
static std::vector<uint8_t> Snapshot(const CE_StateReceiver *r) {
    const auto *bytes=reinterpret_cast<const uint8_t*>(r);return r?std::vector<uint8_t>(bytes,bytes+sizeof(*r)):std::vector<uint8_t>();
}
static void ReadOnly(const CE_StateReceiver *r,const CE_StateDeliveryControl *c,const CE_StateRepairGate *gate,uint64_t now,int expected) {
    const auto owner=Snapshot(r);CE_StateDeliveryControl old_control={};CE_StateRepairGate old_gate={};
    if(c)old_control=*c;if(gate)old_gate=*gate;
    int (*const check)(const CE_StateReceiver*,const CE_StateDeliveryControl*,const CE_StateRepairGate*,uint64_t)=CE_StateReceiverCheckRepair;
    Must(check(r,c,gate,now)==expected,"CheckRepair returns full shared admission verdict");
    Must(owner==Snapshot(r),"CheckRepair leaves every owner byte unchanged");
    Must(!c||!std::memcmp(c,&old_control,sizeof(*c)),"CheckRepair leaves announcement unchanged");
    Must(!gate||!std::memcmp(gate,&old_gate,sizeof(*gate)),"CheckRepair leaves gate unchanged");
}
static void CheckAdmission() {
    auto c=Announcement();Receiver f;auto gate=c.gate;
    ReadOnly(f.r,&c,&gate,100,CE_DELIVERY_OK);
    ReadOnly(f.r,&c,&gate,10000,CE_DELIVERY_OK);
    ReadOnly(f.r,&c,&gate,UINT64_MAX,CE_DELIVERY_OK);
    ReadOnly(f.r,&c,&gate,99,CE_DELIVERY_INVALID);
    ReadOnly(nullptr,&c,&gate,100,CE_DELIVERY_INVALID);
    ReadOnly(f.r,nullptr,&gate,100,CE_DELIVERY_INVALID);
    ReadOnly(f.r,&c,nullptr,100,CE_DELIVERY_INVALID);
    auto wrong_gate=gate;++wrong_gate.lifecycle_watermark;ReadOnly(f.r,&c,&wrong_gate,500,CE_DELIVERY_INVALID);
    auto bad=c;bad.kind=CE_DELIVERY_REPAIR_READY;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.key.context[0]^=1;bad.active_base.context[0]^=1;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;++bad.key.epoch;++bad.active_base.epoch;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.gate.wait_generation=0;ReadOnly(f.r,&bad,&bad.gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.active_base.digest[0]^=1;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.active_base.tick=bad.key.tick+1;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.active_base.sequence=bad.key.sequence;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;bad.active_base=Key(2,141);ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_OK);
    Receiver unseeded(false);ReadOnly(unseeded.r,&c,&gate,10000,CE_DELIVERY_INVALID);
    Image base(1,140);auto base_view=base.view();Must(CE_StateReceiverBaselineApplied(unseeded.r,&base_view,140,0)==0,"unseeded check did not advance clock before baseline");
    Receiver stale(true,3,143);bad=Announcement(2,144);ReadOnly(stale.r,&bad,&gate,500,CE_DELIVERY_STALE);
    Receiver retired;auto status=c;status.kind=CE_DELIVERY_STATUS;status.gate={};status.active_base={};status.key=Key(2,141);
    Must(CE_StateReceiverControl(retired.r,&status,100,0)==0,"actual retirement fence");ReadOnly(retired.r,&c,&gate,500,CE_DELIVERY_STALE);
    Receiver prepared;auto ticket=Prepare(prepared.r);ReadOnly(prepared.r,&c,&gate,500,CE_DELIVERY_INVALID);
    bad=Announcement(2,142,2);ReadOnly(prepared.r,&bad,&gate,500,CE_DELIVERY_STALE);
    Receiver late_tick;auto late=Prepare(late_tick.r,2,150);bad=Announcement(3,149,2);ReadOnly(late_tick.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    Must(CE_StateReceiverApplied(late_tick.r,&late,7,2,150,100)==0,"failed check retains prepared ticket and original time");
    Must(CE_StateReceiverExpectRepair(f.r,&c,&gate,100)==0,"actual expectation installation after checks");
    ReadOnly(f.r,&c,&gate,10000,CE_DELIVERY_OK);
    bad=c;bad.key.digest[0]^=1;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;++bad.offer_generation;ReadOnly(f.r,&bad,&gate,500,CE_DELIVERY_INVALID);
    bad=c;++bad.gate.wait_generation;ReadOnly(f.r,&bad,&bad.gate,500,CE_DELIVERY_INVALID);
    (void)ticket;
}
static void CheckOperability() {
    Receiver f;auto c=Announcement();auto gate=c.gate;CE_StateDeliveryControl response={};
    Must(CE_StateReceiverPoll(f.r,100,&response)==0&&response.kind==CE_DELIVERY_APPLIED&&CE_StateReceiverControlQueued(f.r,&response,1,100)==0,"initial ACK consumed");
    ReadOnly(f.r,&c,&gate,10000,CE_DELIVERY_OK);
    Must(CE_StateReceiverPoll(f.r,100,&response)==CE_DELIVERY_RETRY,"check did not queue readiness or advance clock");
    Image raw(3,142);CE_StateDeliveryTicket ticket={};ticket.generation=77;auto old=ticket;
    Must(CE_StateReceiverPrepareRepair(f.r,raw.bytes.data(),raw.bytes.size(),100,&ticket)==CE_DELIVERY_NOT_READY&&!std::memcmp(&ticket,&old,sizeof(ticket)),"check did not install repair or expose raw ticket");
    Must(CE_StateReceiverExpectRepair(f.r,&c,&gate,100)==0,"commit admits original time after future check");
    Must(CE_StateReceiverPoll(f.r,100,&response)==0&&response.kind==CE_DELIVERY_REPAIR_READY&&CE_StateReceiverControlQueued(f.r,&response,1,100)==0,"Expect still emits exact readiness");
    ReadOnly(f.r,&c,&gate,20000,CE_DELIVERY_OK);
    Must(CE_StateReceiverPoll(f.r,100,&response)==CE_DELIVERY_RETRY,"duplicate check does not recreate consumed response");
    Must(CE_StateReceiverExpectRepair(f.r,&c,&gate,200)==0&&f.r->repair_at==100,"duplicate Expect requeues without restarting transaction deadline");
    Must(CE_StateReceiverPoll(f.r,200,&response)==0&&response.kind==CE_DELIVERY_REPAIR_READY&&CE_StateReceiverControlQueued(f.r,&response,1,200)==0,"duplicate Expect response behavior retained");
    Must(CE_StateReceiverPrepareRepair(f.r,raw.bytes.data(),raw.bytes.size(),300,&ticket)==0,"actual raw prepare still works");
    CE_StateImage view={};Must(CE_StateReceiverImage(f.r,&ticket,&view)==0,"prepared repair view");std::vector<uint8_t> bytes(view.data,view.data+view.size);
    ReadOnly(f.r,&c,&gate,20000,CE_DELIVERY_OK);
    Must(CE_StateReceiverImage(f.r,&ticket,&view)==0&&bytes==std::vector<uint8_t>(view.data,view.data+view.size),"check preserves exact prepared raw bytes and ticket");
    Must(CE_StateReceiverApplied(f.r,&ticket,7,3,142,300)==0,"real application hook remains usable at original time");
    Must(CE_StateReceiverPoll(f.r,300,&response)==0&&response.kind==CE_DELIVERY_APPLIED&&response.key.sequence==3,"only actual whole-Apply hook emits application ACK");
    Receiver normal;auto normal_ticket=Prepare(normal.r);auto next=Announcement(3,142,2);
    ReadOnly(normal.r,&next,&gate,10000,CE_DELIVERY_OK);
    Must(CE_StateReceiverApplied(normal.r,&normal_ticket,7,2,141,100)==0,"ordinary prepared ticket survives repair preview");
    Receiver timeout;Must(CE_StateReceiverExpectRepair(timeout.r,&c,&gate,100)==0&&CE_StateReceiverExpectRepair(timeout.r,&c,&gate,2000)==0,"duplicate Expect accepted");
    ReadOnly(timeout.r,&c,&gate,UINT64_MAX,CE_DELIVERY_OK);
    Must(CE_StateReceiverPoll(timeout.r,30100,&response)==CE_DELIVERY_TIMEOUT,"checks and duplicate announcements never slide existing repair deadline");
}
#endif
static void ExpectTemporal() {
    std::printf("{\"case\":\"expect-temporal\",\"observations\":[");bool first=true;
    auto observe=[&](const char *name,CE_StateReceiver *r,CE_StateDeliveryControl c,CE_StateRepairGate gate,uint64_t now,int verdict,bool advances) {
        const uint64_t prior=r->now;const int result=CE_StateReceiverExpectRepair(r,&c,&gate,now);
        Must(result==verdict&&r->now==(advances?now:prior),"Expect preserves original admission and clock boundary");
        CE_StateDeliveryControl output={};const uint64_t follow=advances?now-1:prior;
        const int continuation=CE_StateReceiverPoll(r,follow,&output);
        Must(advances?continuation==CE_DELIVERY_INVALID:continuation!=CE_DELIVERY_INVALID,"temporal continuation observes original clock effects");
        if(!first)std::printf(",");first=false;
        std::printf("{\"name\":\"%s\",\"result\":%d,\"now\":%llu,\"continuation\":%d}",name,result,static_cast<unsigned long long>(r->now),continuation);
    };
    auto c=Announcement();auto gate=c.gate;
    {Receiver f;observe("valid",f.r,c,gate,500,CE_DELIVERY_OK,true);}
    {Receiver f(true,3,143);auto stale=Announcement(2,144);observe("stale",f.r,stale,gate,500,CE_DELIVERY_STALE,true);}
    {Receiver f;auto bad=c;bad.active_base.digest[0]^=1;observe("known-base-conflict",f.r,bad,gate,500,CE_DELIVERY_INVALID,true);}
    {Receiver f(false);observe("unseeded",f.r,c,gate,500,CE_DELIVERY_INVALID,true);}
    {Receiver f;auto bad=c;bad.kind=CE_DELIVERY_REPAIR_READY;observe("structural-kind",f.r,bad,gate,500,CE_DELIVERY_INVALID,false);}
    {Receiver f;auto bad=gate;++bad.wait_generation;observe("gate-mismatch",f.r,c,bad,500,CE_DELIVERY_INVALID,false);}
    {Receiver f;auto bad=c;bad.key.context[0]^=1;bad.active_base.context[0]^=1;observe("scope",f.r,bad,gate,500,CE_DELIVERY_INVALID,false);}
    {Receiver f;observe("backward",f.r,c,gate,99,CE_DELIVERY_INVALID,false);}
    {Receiver f;Must(CE_StateReceiverExpectRepair(f.r,&c,&gate,100)==0,"temporal duplicate installed");observe("duplicate",f.r,c,gate,500,CE_DELIVERY_OK,true);Must(f.r->repair_at==100,"duplicate never renews original repair timer");}
    std::printf("],\"checks\":%u,\"failed\":0}\n",checks);
}
int main(int argc,char **argv) {
    const std::string mode=argc==2?argv[1]:"";
    if(mode=="expect-temporal") {ExpectTemporal();return 0;}
#ifndef CE_REPAIR_CHECK_LEGACY
    if(mode=="check-admission")CheckAdmission();else if(mode=="check-operability")CheckOperability();else return 2;
    std::printf("{\"case\":\"%s\",\"checks\":%u,\"failed\":0}\n",mode.c_str(),checks);return 0;
#else
    return 2;
#endif
}
