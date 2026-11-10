// SPDX-License-Identifier: GPL-3.0-only
// Authored loopback STATE carrier; byte-exact payloads and local monotonic age, never game objects or invites.
#include "../transport.cpp"
#include "fixture_guard.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>

static constexpr uint32_t kBaseAck=1000,kDone=1001,kDoneAck=1002,kConfirm=1003,kControls=20;
static void Pattern(uint8_t*p,uint32_t n,uint32_t seed){for(uint32_t i=0;i<n;++i)p[i]=uint8_t((i*37+seed)^(i>>8));}
static bool PatternEqual(const uint8_t*p,uint32_t n,uint32_t seed,uint32_t start=0){for(uint32_t i=start;i<n;++i)if(p[i]!=uint8_t((i*37+seed)^(i>>8)))return false;return true;}
static void State(uint8_t*p,uint32_t n,uint32_t tick,uint64_t now){Pattern(p,n,tick);CE_NetWriter w;CE_NetWriterInit(&w,p,16);CE_NetWriteU32(&w,0x53544154);CE_NetWriteU32(&w,tick);CE_NetWriteU64(&w,now);}
int main(int argc,char**argv)
{
    if(!FixtureGuard()||(argc!=5&&argc!=6))return 2;
    bool host=!std::strcmp(argv[1],"host"),gap=!std::strcmp(argv[3],"gap"),late=!std::strcmp(argv[3],"late"),raw=!std::strcmp(argv[3],"raw");
    uint32_t size=uint32_t(std::strtoul(argv[4],nullptr,10));if(size<16||size>CE_NET_MAX_MESSAGE)return 2;
    uint32_t pollBudget=argc==6?uint32_t(std::strtoul(argv[5],nullptr,10)):32;
    if(!pollBudget||pollBudget>512)return 2;
    CE_NetConfig c{};c.role=host?CE_NET_HOST:CE_NET_GUEST;c.ipv4="127.0.0.1";c.port=uint16_t(std::strtoul(argv[2],nullptr,10));c.handshake_ms=5000;c.silence_ms=10000;
    for(unsigned i=0;i<32;++i){c.secret[i]=uint8_t(i+1);c.compatibility[i]=uint8_t(i^0xa5);}
    CE_NetSession*s=CE_NetCreate(&c);if(!s)return 3;CE_NetStatus status{};CE_NetGetStatus(s,&status);std::printf("BOUND %u\n",unsigned(status.port));std::fflush(stdout);
    uint64_t begin=CE_NetNowMs(),started=0,active=0,nextStateAt=0,nextInputAt=0,completeAt=0;
    uint32_t nextState=1,states=0,lastState=0,controls=0,sentControls=0,inputs=0,sentInputs=0,sentStates=0,pressure=0,target=0;
    uint64_t maximumAge=0,newerAfterGapAge=0;bool baselineSent=false,baseline=false,echoed=false,done=false,ack=false,confirmed=false,complete=false;
    uint32_t peakBatch=0,peakQueue=0,peakMetadata=0,peakPending=0,peakBytes=0,peakReliable=0,window=0,minThrottle=ENET_PEER_PACKET_THROTTLE_SCALE;
    auto observe=[&](){peakQueue=std::max(peakQueue,s->count);peakMetadata=std::max(peakMetadata,IncomingMetadata(s->host));peakPending=std::max(peakPending,s->pendingPackets);peakBytes=std::max(peakBytes,s->pendingBytes);if(s->peer){peakReliable=std::max(peakReliable,s->peer->reliableDataInTransit);window=s->peer->windowSize;minThrottle=std::min(minThrottle,s->peer->packetThrottle);}};
    uint8_t data[CE_NET_MAX_MESSAGE];int rc=4;const char*failure="deadline";
    while(CE_NetNowMs()-begin<18000){
        uint64_t now=CE_NetNowMs();observe();uint32_t before=s->status.received_messages;CE_NetPoll(s,now,pollBudget);observe();peakBatch=std::max(peakBatch,s->status.received_messages-before);CE_NetGetStatus(s,&status);
        if(status.phase==CE_NET_ACTIVE&&!active)active=now;
        CE_NetMessage m{};
        while(CE_NetReceive(s,&m,data,sizeof(data))==1){
            if(m.epoch!=1){failure="epoch";rc=5;goto finish;}
            if(m.kind==CE_NET_BASELINE){
                if(host||m.size!=4097||!PatternEqual(data,m.size,99)){failure="baseline bytes";rc=5;goto finish;}
                baseline=true;uint8_t b=1;if(CE_NetSend(s,CE_NET_CONTROL,kBaseAck,&b,1)!=1){failure="baseline ACK";rc=5;goto finish;}
            }else if(m.kind==CE_NET_STATE){
                CE_NetReader r;CE_NetReaderInit(&r,data,16);uint32_t magic=CE_NetReadU32(&r),tick=CE_NetReadU32(&r);uint64_t stamp=CE_NetReadU64(&r);
                if(host||m.size!=size||magic!=0x53544154||tick!=m.tick||tick<=lastState||stamp>now||!PatternEqual(data,m.size,tick,16)){failure="partial/corrupt/older STATE";rc=5;goto finish;}
                if((gap||late)&&tick==3){failure="retired STATE exposed";rc=5;goto finish;}
                lastState=tick;++states;maximumAge=std::max(maximumAge,now-stamp);if((gap||late)&&tick>=4&&!newerAfterGapAge)newerAfterGapAge=now-stamp;
            }else if(m.kind==CE_NET_INPUT){
                CE_NetReader r;CE_NetReaderInit(&r,data,m.size);uint32_t n=CE_NetReadU32(&r);
                if(!host||!CE_NetReadDone(&r)||n!=inputs+1||m.tick!=n){failure="ordered INPUT";rc=5;goto finish;}++inputs;
            }else if(m.kind==CE_NET_CONTROL){
                if(host&&m.tick==kBaseAck){echoed=true;started=now;nextStateAt=now;}
                else if(!host&&m.tick>=1&&m.tick<=kControls){if(m.tick!=controls+1||m.size!=1||data[0]!=m.tick){failure="ordered CONTROL";rc=5;goto finish;}++controls;}
                else if(!host&&m.tick==kDone){CE_NetReader r;CE_NetReaderInit(&r,data,m.size);target=CE_NetReadU32(&r);if(!CE_NetReadDone(&r)){failure="done bytes";rc=5;goto finish;}done=true;}
                else if(host&&m.tick==kDoneAck){CE_NetReader r;CE_NetReaderInit(&r,data,m.size);uint32_t got=CE_NetReadU32(&r);if(!CE_NetReadDone(&r)||!done||got<target){failure="completion ACK";rc=5;goto finish;}ack=true;}
                else if(!host&&m.tick==kConfirm){confirmed=true;complete=true;completeAt=now;}
                else {failure="control identity";rc=5;goto finish;}
            }else {failure="message kind";rc=5;goto finish;}
        }
        if(status.phase==CE_NET_FAILED){failure="transport failure";rc=6;goto finish;}
        if(status.phase==CE_NET_CLOSED){if(complete&&status.error==CE_NET_PEER_LEFT){rc=0;failure="none";goto finish;}failure="premature close";rc=6;goto finish;}
        if(complete&&now-completeAt>=500){rc=0;failure="none";break;}
        if(status.phase!=CE_NET_ACTIVE){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(host){
            if(!baselineSent){Pattern(data,4097,99);if(CE_NetSend(s,CE_NET_BASELINE,0,data,4097)==1)baselineSent=true;}
            if(echoed){
                uint64_t elapsed=now-started;
                if(sentControls<kControls&&elapsed>=sentControls*50){uint8_t b=uint8_t(sentControls+1);if(CE_NetSend(s,CE_NET_CONTROL,sentControls+1,&b,1)==1)++sentControls;}
                bool due=now>=nextStateAt;
                if((gap||late)&&nextState==4&&elapsed<700)due=false;
                if(due&&!complete){State(data,size,nextState,now);int n=CE_NetSend(s,CE_NET_STATE,nextState,data,size);if(n<0){failure="STATE argument";rc=7;goto finish;}if(n)++sentStates;else ++pressure;++nextState;nextStateAt=now+(raw&&elapsed>=3000?500:50);}
                uint64_t duration=gap||late?1500:raw?8000:4000;
                if(!done&&elapsed>=duration&&sentControls==kControls){target=nextState-1;CE_NetWriter w;CE_NetWriterInit(&w,data,4);CE_NetWriteU32(&w,target);if(CE_NetSend(s,CE_NET_CONTROL,kDone,data,4)==1)done=true;}
                if(ack&&!confirmed){if(CE_NetSend(s,CE_NET_CONTROL,kConfirm,nullptr,0)==1){confirmed=true;complete=true;completeAt=now;}}
                if((gap||late)&&elapsed>=3500&&!complete){failure="newer STATE delayed by old fragment";rc=8;goto finish;}
            }
        }else{
            if(now>=nextInputAt&&!complete){CE_NetWriter w;CE_NetWriterInit(&w,data,4);CE_NetWriteU32(&w,sentInputs+1);int n=CE_NetSend(s,CE_NET_INPUT,sentInputs+1,data,4);if(n<0){failure="INPUT argument";rc=7;goto finish;}if(n)++sentInputs;nextInputAt=now+20;}
            if(done&&!ack&&lastState>=target&&controls==kControls&&baseline){CE_NetWriter w;CE_NetWriterInit(&w,data,4);CE_NetWriteU32(&w,lastState);if(CE_NetSend(s,CE_NET_CONTROL,kDoneAck,data,4)==1)ack=true;}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
finish:
    CE_NetGetStatus(s,&status);std::printf("RESULT %d role=%u failure=%s states=%u last=%u sent=%u pressure=%u inputs=%u input_sent=%u controls=%u max_age_ms=%llu newer_gap_age_ms=%llu phase=%u error=%u intercept=%u peak_batch=%u peak_queue=%u peak_metadata=%u peak_pending=%u peak_bytes=%u peak_reliable=%u window=%u min_throttle=%u poll_budget=%u\n",rc,c.role,failure,states,lastState,sentStates,pressure,inputs,sentInputs,controls,(unsigned long long)maximumAge,(unsigned long long)newerAfterGapAge,status.phase,status.error,s->interceptError,peakBatch,peakQueue,peakMetadata,peakPending,peakBytes,peakReliable,window,minThrottle,pollBudget);
    CE_NetClose(s);CE_NetDestroy(s);return rc;
}
