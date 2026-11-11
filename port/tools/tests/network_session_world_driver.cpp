// SPDX-License-Identifier: GPL-3.0-only
// Actual Session, World lifecycle/native Init functions and codec, with bounded in-memory transport and test-only
// complete-frame/platform boundaries. No UDP, device, game, real-time delay, actor ABI or visual proof.
#include SESSION_SOURCE
#include <deque>
#include <string>
#include <vector>
extern "C" void FixtureTick(uint32_t);
extern "C" int FixtureSpawn();
extern "C" void FixtureDespawn();
extern "C" void FixtureFailFactory(int);
extern "C" uint32_t FixtureRead(unsigned);
extern "C" int FixtureReadinessProbe(unsigned);
struct Packet {CE_NetMessage meta;std::vector<uint8_t> bytes;};
struct CE_NetSession {CE_NetStatus status;std::deque<Packet> incoming;uint8_t context[32]{};
    CE_StateReceiver*receiver=nullptr;CE_StateSender*sender=nullptr;};
static CE_NetSession *peer;
static uint64_t now=100;
static unsigned checks,quit,life_send0,life_ack_sends;
static constexpr unsigned fixture_lifecycle_applied=15;
static std::vector<uint8_t> baseline,actual_ack;
static std::vector<Packet> operations,states,raw_states;
static void Check(bool yes,const char*why){++checks;if(!yes){fprintf(stderr,"FAIL %s (%s)\n",why,CE_SessionError());exit(2);}}
static void In(uint32_t kind,uint32_t tick,const std::vector<uint8_t>&data){peer->incoming.push_back({{kind,2,tick,(uint32_t)data.size()},data});}
static void ControlIn(unsigned kind,uint32_t sequence=0,uint32_t tick=0){
    uint8_t bytes[24];CE_NetWriter w;CE_NetWriterInit(&w,bytes,sizeof bytes);for(uint32_t x:{kControlMagic,kControlSchema,kind,852u,sequence,tick})CE_NetWriteU32(&w,x);
    peer->incoming.push_back({{CE_NET_CONTROL,peer->status.epoch,tick,w.size},std::vector<uint8_t>(bytes,bytes+w.size)});
}
extern "C" int CE_BootRequested(){return getenv("BEA_NET_HOST")||getenv("BEA_NET_JOIN");}
extern "C" int CE_CompatibilityPrepared(CE_CompatibilityResult*out,char*,size_t){*out={};return Hex("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",out->digest);}
extern "C" int CE_BootPrepared(CE_CompatibilityResult*out,char*e,size_t n){return CE_CompatibilityPrepared(out,e,n);}
extern "C" int CE_CompatibilityCheckBoundary(char*,size_t){return 1;}
extern "C" void CE_CompatibilityRetire(){}
extern "C" int CE_NetLoadedAdmissionValidate(){return 1;}
extern "C" const char*CE_NetLoadedAdmissionError(){return "controlled loaded-world boundary";}
// The session reaches the lockstep co-op prototype's hooks; this driver starts no lockstep session, so each answers "not requested" and a load,
// a tick or a teardown passes through unchanged.
extern "C" int CE_LockstepRequested(void){return 0;}
extern "C" int CE_LockstepActive(void){return 0;}
extern "C" int CE_LockstepConfigureLevel(uint32_t){return 0;}
extern "C" int CE_LockstepAfterLoad(void){return 1;}
extern "C" int CE_LockstepPreparePlay(void){return 1;}
extern "C" int CE_LockstepBeforeUpdate(void){return 1;}
extern "C" void CE_LockstepAfterUpdate(void){}
extern "C" void CE_LockstepBeforeRender(void){}
extern "C" void CE_LockstepAfterWorldShutdown(void){}
extern "C" int CE_LockstepBeginTeardown(int quit){return quit;}
extern "C" int CE_LockstepExitCode(int native_code,int){return native_code;}
extern "C" const char *CE_LockstepNotice(void){return "";}
extern "C" const char *CE_LockstepError(void){return "";}
static CE_StateImage DeliveryImage(const uint8_t*bytes,uint32_t size,uint32_t&tick){
    CE_NetReader r;CE_NetReaderInit(&r,bytes,size);CE_StateImage image{};memcpy(image.identity.context,peer->context,32);
    image.identity.epoch=CE_NetReadU32(&r);CE_NetReadU32(&r);tick=CE_NetReadU32(&r);image.identity.sequence=CE_NetReadU32(&r);
    Check(!r.error&&image.identity.epoch==2&&image.identity.sequence&&tick,"controlled full-frame boundary carries exact actual delivery identity");
    image.data=bytes;image.size=size;return image;
}
static void DeliveryControlIn(const CE_StateDeliveryControl&c){
    uint8_t bytes[184];Check(CE_StateDeliveryEncodeControl(&c,bytes,sizeof bytes)==CE_DELIVERY_OK,"actual canonical opposite delivery control");
    In(CE_NET_STATE_CONTROL,c.key.tick,std::vector<uint8_t>(bytes,bytes+sizeof bytes));
}
extern "C" CE_NetSession *CE_NetCreate(const CE_NetConfig*c){peer=new CE_NetSession;peer->status={CE_NET_ACTIVE,0,c->role,0,1,c->port,0,0,0};memcpy(peer->context,c->compatibility,32);
    if(c->role==CE_NET_HOST)peer->receiver=CE_StateReceiverCreate(peer->context,2);else peer->sender=CE_StateSenderCreate(peer->context,2);return peer;}
extern "C" void CE_NetDestroy(CE_NetSession*p){if(p){CE_StateSenderDestroy(p->sender);CE_StateReceiverDestroy(p->receiver);}delete p;if(peer==p)peer=nullptr;}
extern "C" void CE_NetGetStatus(const CE_NetSession*p,CE_NetStatus*out){*out=p->status;}
extern "C" int CE_NetPeerVersion(const CE_NetSession*,char*,uint32_t){return 0;}
extern "C" uint32_t CE_NetQuietMs(const CE_NetSession*,uint64_t){return 0;}
extern "C" uint64_t CE_NetNowMs(){return now;}
extern "C" void CE_NetPoll(CE_NetSession*,uint64_t,unsigned){}
extern "C" int CE_NetBeginEpoch(CE_NetSession*p,uint32_t epoch){p->status.epoch=epoch;p->incoming.clear();return 1;}
extern "C" void CE_NetClose(CE_NetSession*p){if(p)p->status.phase=CE_NET_CLOSED;}
extern "C" int CE_NetReceive(CE_NetSession*p,CE_NetMessage*m,uint8_t*out,uint32_t capacity){
    if(p->incoming.empty())return 0;Packet q=p->incoming.front();if(q.bytes.size()>capacity)return -1;p->incoming.pop_front();*m=q.meta;memcpy(out,q.bytes.data(),q.bytes.size());return 1;
}
extern "C" int CE_NetSend(CE_NetSession*p,uint32_t kind,uint32_t tick,const uint8_t*bytes,uint32_t size){
    Check(p->status.phase==CE_NET_ACTIVE,"no send after close");std::vector<uint8_t> data(bytes,bytes+size);
    if(kind==CE_NET_CONTROL){CE_NetReader r;CE_NetReaderInit(&r,bytes,size);CE_NetReadU32(&r);Check(CE_NetReadU32(&r)==kControlSchema,"actual selected control schema");
        unsigned control=CE_NetReadU32(&r);CE_NetReadU32(&r);uint32_t sequence=CE_NetReadU32(&r);CE_NetReadU32(&r);
        if(control==PEER_LOADED){ControlIn(PEER_LOADED);if(!s.host&&p->status.epoch==1){p->incoming.clear();p->status.epoch=2;}
            else if(!s.host&&p->status.epoch==2){uint32_t bt=0;auto image=DeliveryImage(baseline.data(),baseline.size(),bt);
                Check(CE_StateSenderBaselineSent(p->sender,&image,bt,1,now)==CE_DELIVERY_OK,"opposite actual initial baseline Send1");In(CE_NET_BASELINE,60,baseline);}}
        else if(control==START)ControlIn(START_APPLIED,sequence,tick);
        else if(control==BASELINE_APPLIED)ControlIn(START,sequence,tick);
        else if(control==fixture_lifecycle_applied){Check(size==28&&FixtureRead(0)>0&&!FixtureRead(6),"real whole-Apply proof precedes lifecycle ACK");actual_ack=data;++life_ack_sends;}
    }else if(kind==CE_NET_STATE_CONTROL){CE_StateDeliveryControl c{};Check(CE_StateDeliveryDecodeControl(bytes,size,&c)==CE_DELIVERY_OK&&c.key.tick==tick,"Session canonical delivery control");
        int result=p->sender?CE_StateSenderControl(p->sender,&c,now,0):CE_StateReceiverControl(p->receiver,&c,now,0);
        Check(result==CE_DELIVERY_OK||result==CE_DELIVERY_STALE,"opposite actual delivery owner accepts exact proof");
    }else if(kind==CE_NET_BASELINE){baseline=data;uint32_t bt=0;auto image=DeliveryImage(bytes,size,bt);
        Check(bt==tick&&CE_StateReceiverBaselineApplied(p->receiver,&image,tick,now)==CE_DELIVERY_OK,"controlled opposite whole initial baseline Apply");
        CE_StateDeliveryControl ack{};Check(CE_StateReceiverPoll(p->receiver,now,&ack)==CE_DELIVERY_OK&&
            CE_StateReceiverControlQueued(p->receiver,&ack,1,now)==CE_DELIVERY_OK,"opposite exact whole baseline ACK Send1");
        DeliveryControlIn(ack);ControlIn(BASELINE_APPLIED,1,tick);}
    else if(kind==CE_NET_LIFECYCLE){if(life_send0){--life_send0;return 0;}operations.push_back({{kind,2,tick,size},data});}
    else if(kind==CE_NET_STATE){CE_StateDeliveryTicket ticket{};
        Check(CE_StateReceiverPrepare(p->receiver,bytes,size,now,&ticket)==CE_DELIVERY_OK,"actual CSD1 output decodes against opposite retained baseline");
        CE_StateImage raw{};Check(CE_StateReceiverImage(p->receiver,&ticket,&raw)==CE_DELIVERY_OK,"actual decoded full frame retained under delivery ticket");
        raw_states.push_back({{kind,2,tick,raw.size},std::vector<uint8_t>(raw.data,raw.data+raw.size)});
        states.push_back({{kind,2,tick,size},data});}
    return 1;
}
bool PortClockVirtual(){return false;}
extern "C" uint32_t CE_SessionWindowStatus(){return 0;}
extern "C" void CE_SessionGamePreRun(){FixtureTick(60);}
extern "C" int CE_SessionGamePump(){++now;return 0;}
extern "C" void CE_SessionGameIdle(){++now;}
extern "C" int CE_SessionGameQuitCode(){return quit;}
extern "C" void CE_SessionGameSetQuit(int value){quit=value;}
extern "C" uint32_t CE_SessionGameTick(){return FixtureRead(10);}
extern "C" void CE_SessionGameRebaseRenderClock(){}
static void Start(bool host){
    quit=0;unsetenv("BEA_NET_HOST");unsetenv("BEA_NET_JOIN");setenv(host?"BEA_NET_HOST":"BEA_NET_JOIN","127.0.0.1:27652",1);
    setenv("BEA_NET_SECRET","0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",1);
    setenv("BEA_NET_COMPAT","abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",1);
    Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad()&&CE_SessionPreparePlay(),"actual Session/World Start barrier completes");
    Check(FixtureRead(8),"actual World exact initial byte proof admits lifecycle");
}
static void End(){CE_SessionBeginTeardown(1);CE_SessionAfterWorldShutdown();Check(!s.requested&&!peer,"native test-owned teardown retires Session");}
static void Source(){
    Start(true);now+=50;CE_SessionBeforeRender();Check(states.size()==1,"source produces genuine pre-event fullSTATE");
    FixtureTick(61);Check(FixtureSpawn(),"actual source native factory/Init appends birth");life_send0=1;
    CE_SessionAfterFlush();Check(FixtureRead(9)==0&&FixtureRead(1)==1&&states.size()==1,"Send0 preserves actual World queue and blocks fullSTATE capture");
    Check(CE_SessionBeforeUpdate()&&FixtureRead(9)==1&&FixtureRead(1)==1,"Send1 publishes actual World front but retains application obligation");
    CE_SessionAfterFlush();Check(states.size()==2,"fullSTATE capture follows published lifecycle prefix");
    FixtureTick(62);FixtureDespawn();CE_SessionAfterFlush();Check(operations.size()==2&&FixtureRead(9)==2&&FixtureRead(1)==2,"native Shutdown emits separate immutable reliable despawn");
}
int main(int argc,char**argv){
    if(argc!=2)return 1;std::string mode=argv[1];Source();auto source_ops=operations;auto source_states=states;End();
    Start(false);
    if(mode=="late-state") {
        In(CE_NET_LIFECYCLE,61,source_ops[0].bytes);CE_SessionBeforeRender();Check(FixtureRead(0)==1&&FixtureRead(5)==60,"native lifecycle commit preserves prior full-frame clock");
        for(unsigned i=1;i<=5;++i)Check(FixtureReadinessProbe(i)==CE_WORLD_INVALID,"invalid scalar preflight/foreign epoch/baseline/prebaseline tick/new-prefix clock rewind refuse before stale classification");
        CE_WorldFrame *f=CE_WorldFrameCreate();Check(CE_WorldDecode(f,raw_states[0].bytes.data(),raw_states[0].bytes.size()),"late STATE bytes came from the earlier actual CSD1 decoder and Session capture");
        CE_WorldInfo info{};Check(CE_WorldFrameInfo(f,&info)&&info.epoch==2&&info.tick==60&&info.frame_sequence==2&&info.lifecycle_baseline==1&&!info.lifecycle_watermark,"late cross-channel frame retains its exact produced epoch/tick/sequence/baseline/watermark");
        fprintf(stderr,"READINESS epoch=%u tick=%u sequence=%u baseline=%u watermark=%u committed_lifecycle=%u applied_tick=%u result=%d\n",info.epoch,info.tick,info.frame_sequence,info.lifecycle_baseline,info.lifecycle_watermark,FixtureRead(0),FixtureRead(5),CE_WorldFrameReadiness(f));CE_WorldFrameDestroy(f);
        In(CE_NET_STATE,60,source_states[0].bytes);CE_SessionBeforeRender();
        Check(s.stage==RUNNING&&FixtureRead(0)==1&&FixtureRead(5)==60&&!s.have_frame,"genuine late older cross-channel STATE is validated stale rather than a protocol failure");End();
        printf("{\"case\":\"late-state\",\"checks\":%u}\n",checks);return 0;
    }
    In(CE_NET_STATE,61,source_states[1].bytes);CE_SessionBeforeRender();
    Check(s.have_frame&&FixtureRead(0)==0&&FixtureRead(5)==60&&!life_ack_sends,"actual readiness defers STATE ahead of lifecycle without clock/native effect");
    In(CE_NET_LIFECYCLE,61,source_ops[0].bytes);
    if(mode=="gap") {peer->incoming.back().bytes=source_ops[1].bytes;peer->incoming.back().meta.tick=62;peer->incoming.back().meta.size=source_ops[1].bytes.size();}
    if(mode=="factory-failure")FixtureFailFactory(1);
    int service=CE_SessionSystemService(1,1);
    if(mode=="gap") {Check(service==CE_SESSION_EXIT&&s.stage==FAILED&&FixtureRead(0)==0&&!life_ack_sends,"real ordered prefix gap refuses during staging before factory");End();printf("{\"case\":\"gap\",\"checks\":%u}\n",checks);return 0;}
    Check(service==CE_SESSION_NATIVE&&FixtureRead(0)==0&&FixtureRead(3)==0&&!life_ack_sends,"actual window service stages codec packet with no factory or ACK");
    CE_SessionBeforeRender();
    if(mode=="factory-failure"||mode=="gap") {
        Check(s.stage==FAILED&&FixtureRead(0)==0&&FixtureRead(5)==60&&!life_ack_sends,"real factory or ordered-prefix failure grants no ACK/clock/fullSTATE credit");
        if(mode=="factory-failure")Check(FixtureRead(6)&&!FixtureRead(3)&&strstr(CE_SessionError(),"allocation refused"),"actual low-heap native factory path refuses and poisons before allocating or ACK");End();
    }else{
        Check(s.stage==RUNNING&&FixtureRead(0)==1&&FixtureRead(3)==1&&FixtureRead(4)==1&&FixtureRead(7)==1&&FixtureRead(5)==61,"actual native Init commits once before dependent fullSTATE");
        Check(life_ack_sends==1&&actual_ack.size()==28,"real World applied lifecycle emits exact ACK");auto birth_ack=actual_ack;
        In(CE_NET_LIFECYCLE,62,source_ops[1].bytes);CE_SessionBeforeRender();Check(FixtureRead(0)==2&&FixtureRead(7)==0&&FixtureRead(4)==1&&life_ack_sends==2,"real native despawn commits without replaying birth effects");
        auto cumulative_ack=actual_ack;End();operations.clear();states.clear();Source();
        In(CE_NET_CONTROL,62,cumulative_ack);Check(CE_SessionBeforeUpdate()&&FixtureRead(1)==0&&FixtureRead(2)==2,"actual guest wholeApply ACK retires exact real host published prefix");
        In(CE_NET_CONTROL,61,birth_ack);Check(CE_SessionBeforeUpdate()&&FixtureRead(2)==2,"exact delayed duplicate cannot roll back retired prefix");End();
    }
    printf("{\"case\":\"%s\",\"checks\":%u,\"source_exact_world_lifecycle\":true,\"full_frame_and_geometry_boundaries\":true,\"real_network_or_game\":false}\n",argv[1],checks);return 0;
}
