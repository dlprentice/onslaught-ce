// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_CHAT_OWNER_MOCK)
#define main ExistingTerminalFixtureMain
#include "network_lockstep_terminal_driver.cpp"
#undef main

namespace {
struct ChatIncoming { uint32_t epoch; CeTextChat::Frame frame; };
std::deque<ChatIncoming> chat_incoming;
uint32_t chat_limit=CE_NET_TEXT_MAX_FRAME;
bool chat_blocked=false;
unsigned chat_accepts=0,chat_receives=0;
}
extern "C" int PortCeChatCapturingInput(void){return 0;}
extern "C" uint32_t CE_NetTextMaxFrame(const CE_NetSession *){return chat_limit;}
extern "C" uint32_t CE_NetTextSend(CE_NetSession *,uint32_t epoch,const void *,uint32_t size,uint64_t){
    if(mock_status.phase!=CE_NET_ACTIVE||epoch!=mock_status.epoch)return CE_NET_TEXT_CLOSED;
    if(size>chat_limit)return CE_NET_TEXT_TOO_LARGE;
    if(chat_blocked)return CE_NET_TEXT_WOULD_BLOCK;
    ++chat_accepts;return CE_NET_TEXT_OK;
}
extern "C" uint32_t CE_NetTextReceive(CE_NetSession *,uint32_t *epoch,void *data,uint32_t capacity,uint32_t *size){
    if(epoch)*epoch=0;if(size)*size=0;
    if(chat_incoming.empty())return CE_NET_TEXT_EMPTY;
    const auto &item=chat_incoming.front();
    if(!data||!epoch||!size||capacity<item.frame.size)return CE_NET_TEXT_TOO_LARGE;
    *epoch=item.epoch;*size=item.frame.size;std::memcpy(data,item.frame.bytes,*size);
    chat_incoming.pop_front();++chat_receives;return CE_NET_TEXT_OK;
}
extern "C" void CE_NetTextGetInfo(const CE_NetSession *,CE_NetTextInfo *out){if(out)*out={};}

static void ChatInit(){
    ChatRetire();init(true);mock_status.phase=CE_NET_ACTIVE;mock_status.epoch=1;mock_status.local_player=0;
    now_ms=1000;chat_incoming.clear();chat_limit=CE_NET_TEXT_MAX_FRAME;
    chat_blocked=false;chat_accepts=chat_receives=0;
    check(Poll(),"chat-owner-poll-active");
    check(CE_LockstepChatAvailable(),"chat-owner-open");
}
static CE_ChatView ChatView(){CE_ChatView view{};CE_LockstepChatCopyView(&view);return view;}
static void ChatCompose(const char *text){
    check(CE_LockstepChatBegin()==CE_CHAT_OK,"chat-owner-compose");
    check(CE_LockstepChatAppend(reinterpret_cast<const uint8_t *>(text),uint32_t(std::strlen(text)))==CE_CHAT_OK,
          "chat-owner-append");
}
static void ChatInbound(uint64_t sequence){
    CeTextChat::Frame frame{};const uint8_t text[]={'p','e','e','r'};
    check(CeTextChat::Encode(text,sizeof(text),sequence,frame)==CeTextChat::Result::Ok,"chat-owner-encode-remote");
    chat_incoming.push_back({mock_status.epoch,frame});
}
int main(){
    ChatInit();chat_blocked=true;ChatCompose("first");
    check(CE_LockstepChatSubmit()==CE_CHAT_OK,"chat-owner-submit-local-queue");
    check(ChatView().queued==1&&ChatView().history_count==0,"chat-owner-no-premature-echo");
    check(Poll()&&ChatView().queued==1&&ChatView().history_count==0,"chat-owner-backpressure-retains-head");
    chat_blocked=false;
    check(Poll()&&chat_accepts==1&&ChatView().queued==0&&ChatView().history_count==1,
          "chat-owner-exact-commit-echo");
    check(Poll()&&chat_accepts==1,"chat-owner-no-double-commit");
    ChatCompose("long-enough");chat_limit=CeTextChat::HeaderBytes+3;
    check(CE_LockstepChatSubmit()==CE_CHAT_TOO_LARGE&&ChatView().draft_size==11&&ChatView().composing,
          "chat-owner-mtu-refusal-retains-draft");
    CE_LockstepChatCancel();chat_limit=CE_NET_TEXT_MAX_FRAME;
    ChatInit();chat_blocked=true;
    for(unsigned i=0;i<4;++i){ChatCompose("quota");check(CE_LockstepChatSubmit()==CE_CHAT_OK,"chat-owner-burst-submit");}
    ++mock_status.epoch;check(Poll(),"chat-owner-next-epoch");
    check(ChatView().queued==0&&ChatView().history_count==0&&!ChatView().composing,"chat-owner-epoch-clears-owned-text");
    ChatCompose("quota");check(CE_LockstepChatSubmit()==CE_CHAT_RATE_LIMITED,"chat-owner-epoch-preserves-burst");
    CE_LockstepChatCancel();
    ChatInit();L.tick=40;L.ran=false;L.sampled=false;L.waiting_since=0;
    bool saw_remote=false,saw_wait=false;
    for(unsigned i=0;i<70&&L.stage!=FAILED;++i){
        ChatInbound(i+1);saw_wait=CE_LockstepBeforeUpdate()==0||saw_wait;
        saw_remote=ChatView().history_count>0||saw_remote;now_ms+=500;
    }
    check(saw_wait&&saw_remote&&chat_receives>=4,"chat-owner-valid-chat-while-input-missing");
    check(L.stage==FAILED&&std::strstr(CE_LockstepError(),"stopped sending controls"),
          "chat-owner-chat-does-not-reset-input-timeout");
    check(!CE_LockstepChatAvailable()&&ChatView().draft_size==0&&ChatView().history_count==0,
          "chat-owner-failure-retires-private-text");
    ChatInit();ChatCompose("replacement");CE_LockstepAfterWorldShutdown();
    check(!CE_LockstepChatAvailable()&&ChatView().draft_size==0,"chat-owner-world-retirement");
    std::printf("CHAT mock_checks failures=%d\n",failures);return failures?1:0;
}
#else
// SPDX-License-Identifier: GPL-3.0-only
// Actual lockstep controller + actual linked transport. Game/world and clocked simulation inputs are stubs.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#define CE_NET_TEST_AUTOMATION 1
#include "../../game/ce_network_lockstep.cpp"
#include LOCKSTEP_VALIDATOR_INC
#include "../../network/tests/fixture_guard.h"

MockGame GAME;
MockEventManager EVENT_MANAGER;
static int quit_code;
static bool fixture_host,stall_after_done,early_close,guest_poll_delayed;
static bool fixture_shipped,fixture_reject_desync;
static uint32_t observed_phase;
extern "C" {
void (*gPortCrtTrace)(void *)=nullptr;
void PortCrtRandState(unsigned *calls,unsigned *hold){*calls=0;*hold=1;}
void PortCrtRandSet(unsigned,unsigned){}
int CE_CompatibilityLockstepDigest(uint32_t,uint8_t out[32]){std::memset(out,1,32);return 1;}
int CE_BootLockstep(void){return fixture_shipped?1:0;}
int CE_BootPrepared(CE_CompatibilityResult *,char *,size_t){return 0;}	// the admission is the lobby's: these fixtures drive the controller from the test-automation variables
int CE_CompatibilityAdmissionLive(char *,size_t){return 0;}	// this transport fixture has no genuine admission
void CE_WorldLockstepSample(uint32_t,CE_WorldInput *out){*out=CE_WorldInput{};}
void CE_WorldLockstepApply(uint32_t,const CE_WorldInput *){}
void CE_WorldLockstepRandom(int,int *value,int *seed){*value=1;*seed=2;}
uint64_t CE_WorldLockstepThings(FILE *,uint32_t *count){*count=1;return 0x12345678;}
int CE_WorldLockstepGates(uint64_t *out){if(!out)return 0;*out=0x42;return 1;}
void CE_WorldLockstepPartnerStart(void *){}
void CE_WorldLockstepPlaceEmitters(void *const *,const int *,unsigned){}
int CE_WorldLockstepEndTick(){return 1;}
void CE_WorldLockstepBeginTick(){}
void CE_SessionGamePreRun(){}
int CE_SessionGamePump(){return 0;}
void CE_SessionGameIdle(){}
int CE_SessionGameQuitCode(){return quit_code;}
void CE_SessionGameSetQuit(int value){quit_code=value;}
void CE_SessionGameRebaseRenderClock(){}

int __real_CE_NetSend(CE_NetSession *,uint32_t,uint32_t,const uint8_t *,uint32_t);
void __real_CE_NetGetStatus(const CE_NetSession *,CE_NetStatus *);
int __real_CE_NetReceive(CE_NetSession *,CE_NetMessage *,uint8_t *,uint32_t);
void __real_CE_NetPoll(CE_NetSession *,uint64_t,uint32_t);
void __wrap_CE_NetPoll(CE_NetSession *net,uint64_t now,uint32_t budget){
 if(!fixture_host&&early_close&&L.final_receipt_sent&&!guest_poll_delayed){guest_poll_delayed=true;std::this_thread::sleep_for(std::chrono::milliseconds(500));now=CE_NetNowMs();}
 __real_CE_NetPoll(net,now,budget);
}
void __wrap_CE_NetGetStatus(const CE_NetSession *net,CE_NetStatus *out){__real_CE_NetGetStatus(net,out);observed_phase=out->phase;}
int __wrap_CE_NetReceive(CE_NetSession *net,CE_NetMessage *msg,uint8_t *data,uint32_t capacity){
 int result=__real_CE_NetReceive(net,msg,data,capacity);
 if(result==1&&msg->kind==CE_NET_CONTROL&&msg->size>=9){CE_NetReader r;CE_NetReaderInit(&r,data,msg->size);CE_NetReadU32(&r);unsigned type=CE_NetReadU8(&r);std::printf("RECEIVE type=%u phase=%u\n",type,observed_phase);std::fflush(stdout);}
 return result;
}
int __wrap_CE_NetSend(CE_NetSession *net,uint32_t kind,uint32_t tick,const uint8_t *data,uint32_t size){
 unsigned type=0;if(kind==CE_NET_CONTROL&&data&&size>=9){CE_NetReader r;CE_NetReaderInit(&r,data,size);CE_NetReadU32(&r);type=CE_NetReadU8(&r);}
 if(fixture_reject_desync&&type==MSG_DESYNC){std::puts("REJECTED_DESYNC");std::fflush(stdout);return 0;}
 int result=__real_CE_NetSend(net,kind,tick,data,size);
 if(type){std::printf("CONTROL type=%u tick=%u result=%d\n",type,tick,result);std::fflush(stdout);}
 if(result==1&&fixture_host&&stall_after_done&&type==10){CE_NetPoll(net,CE_NetNowMs(),0);std::this_thread::sleep_for(std::chrono::milliseconds(500));}
 if(result==1&&fixture_host&&early_close&&type==10)CE_NetClose(net); // adversarial fixture: close before the ACK
 return result;
}
}


// Exercise the production owner through two real private loopback transports.
extern "C" int PortCeChatCapturingInput(void){return 0;}
extern "C" void CE_BootCampaignClear(void){}
extern "C" int CE_BootCampaignGet(CE_CampaignLaunch *,int *){return 0;}
static bool ChatTwoPlayerRoundtrip(){
    const uint64_t started=CE_NetNowMs();
    bool submitted=false,local=false,remote=false;
    while(CE_NetNowMs()-started<2500){
        if(!Poll()){std::puts("CHAT poll_failed");return false;}
        if(!submitted&&CE_LockstepChatAvailable()){
            const char *text=L.host?"fixture host chat":"fixture guest chat";
            const auto begin_result=CE_LockstepChatBegin();
            const auto append_result=CE_LockstepChatAppend(reinterpret_cast<const uint8_t *>(text),uint32_t(std::strlen(text)));
            const auto submit_result=CE_LockstepChatSubmit();
            if(begin_result!=CE_CHAT_OK||append_result!=CE_CHAT_OK||submit_result!=CE_CHAT_OK){
                std::printf("CHAT compose_refused %u %u %u\n",begin_result,append_result,submit_result);
                return false;
            }
            submitted=true;
        }
        CE_ChatView view{};CE_LockstepChatCopyView(&view);
        for(uint32_t i=0;i<view.history_count;++i){
            if(view.history[i].local_transport_accepted)local=true;
            else remote=true;
        }
        if(submitted&&local&&remote){std::puts("CHAT owner_roundtrip");return true;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::puts("CHAT roundtrip_timeout");return false;
}

int main(int argc,char **argv){
 if(!FixtureGuard()||argc!=4)return 2;
 const bool host=std::strcmp(argv[1],"host")==0;
 fixture_host=host;stall_after_done=std::strcmp(argv[3],"same-poll")==0;early_close=std::strcmp(argv[3],"early-close")==0;
 CE_NetConfig config{};config.role=host?CE_NET_HOST:CE_NET_GUEST;config.ipv4="127.0.0.1";
 config.port=uint16_t(std::strtoul(argv[2],nullptr,10));
 for(unsigned i=0;i<32;++i){config.secret[i]=uint8_t(i+1);config.compatibility[i]=uint8_t(i^0xa5);}
 config.handshake_ms=4000;config.silence_ms=3000;std::snprintf(config.version,sizeof config.version,"fixture");
 CE_NetSession *net=CE_NetCreate(&config);if(!net)return 3;
 CE_NetStatus status{};CE_NetGetStatus(net,&status);std::printf("BOUND %u\n",unsigned(status.port));std::fflush(stdout);
 uint64_t began=CE_NetNowMs();
 while(CE_NetNowMs()-began<6000){
  CE_NetPoll(net,CE_NetNowMs(),64);CE_NetGetStatus(net,&status);
  if(status.phase==CE_NET_ACTIVE)break;
  if(status.phase==CE_NET_FAILED||status.phase==CE_NET_CLOSED){std::printf("CONNECT_FAILED %u %u\n",status.phase,status.error);CE_NetDestroy(net);return 4;}
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 if(status.phase!=CE_NET_ACTIVE){std::puts("CONNECT_TIMEOUT");CE_NetDestroy(net);return 5;}
 L=Lockstep();L.stage=RUNNING;L.host=host;L.level=856;L.hash_every=1;L.hashes_compared=1;
 L.delay=3;L.quit_at=0;L.ran=true;L.sampled=true;L.net=net;GAME.state=GAME_STATE_LEVEL_LOST;CapturePreRunSlots();
    if(!ChatTwoPlayerRoundtrip()){CE_NetDestroy(net);return 20;}
 const char *mode=argv[3];
 const bool fault=std::strncmp(mode,"fault-",6)==0;
 const bool voluntary=std::strncmp(mode,"voluntary-",10)==0;
 const bool diverge=std::strncmp(mode,"diverge-",8)==0;
 const bool rejected=std::strncmp(mode,"reject-",7)==0;
 const bool forced=std::strncmp(mode,"forced-",7)==0;
 if(fault||voluntary||diverge||rejected||forced){
  fixture_shipped=true;L.quit_at=0;const bool actor=host==(std::strstr(mode,"-host")!=nullptr);
  std::puts("ARMED");std::fflush(stdout);char line[8];if(!std::fgets(line,sizeof line,stdin))return 9;
  if(actor){
   if(fault)Fail("synthetic local failure");
   else if(voluntary)CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND);
   else if(forced){CE_NetSession *old=L.net;L.net=nullptr;L.stage=FAILED;L.outcome_error=true;CE_NetClose(old);CE_NetDestroy(old);}
   else {fixture_reject_desync=rejected;HashSlot h{};h.tick=L.tick;h.local=true;h.remote=true;h.mine=1;h.theirs=2;Compare(h);}
   const bool correct=voluntary?!L.outcome_error:L.outcome_error;std::puts("TRIGGER_DONE");std::fflush(stdout);CE_LockstepAfterWorldShutdown();return correct?0:10;
  }
  std::puts("RECEIVER_RUNNING");std::fflush(stdout);began=CE_NetNowMs();
  while(CE_NetNowMs()-began<12000){
   if(!Poll()){
    const char *message=CE_LockstepError();
    const bool left=std::strcmp(message,"the other player left the co-op game")==0;
    const bool failed=std::strcmp(message,"the other player stopped responding")==0||std::strcmp(message,"the connection to the other player failed")==0;
    const bool desync=std::strstr(message,"went out of step")!=nullptr;
    const bool expected=voluntary?left:(diverge?desync:failed);
    if(left)std::puts("REMOTE_LEFT");else if(desync)std::puts("REMOTE_DESYNC");else if(failed)std::puts("REMOTE_FAILED");else std::puts("REMOTE_OTHER");
    const bool correct=expected&&L.outcome_error&&quit_code==QT_QUIT_TO_FRONTEND;std::puts(correct?"REMOTE_EXPECTED":"REMOTE_UNEXPECTED");std::fflush(stdout);CE_LockstepAfterWorldShutdown();return correct?0:1;
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::puts("REMOTE_TIMEOUT");std::fflush(stdout);CE_LockstepAfterWorldShutdown();return 12;
 }
 CE_LockstepAfterUpdate();
 began=CE_NetNowMs();
 while(CE_NetNowMs()-began<10000){
  CE_LockstepBeforeUpdate();
  if(L.stage==FINISHED&&L.final_confirmed){
   std::printf("CONFIRMED tick=%u hash=%016llx\n",L.final_tick,(unsigned long long)L.final_hash);std::fflush(stdout);
   CE_LockstepBeginTeardown(QT_QUIT_TO_SYSTEM);CE_LockstepAfterWorldShutdown();return CE_LockstepExitCode(0,0);
  }
  if(L.stage==FAILED||L.outcome_error){
   std::puts("INCOMPLETE");std::fflush(stdout);CE_LockstepAfterWorldShutdown();return CE_LockstepExitCode(0,1);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 std::puts("INCOMPLETE fixture timeout");CE_LockstepBeginTeardown(QT_QUIT_TO_SYSTEM);CE_LockstepAfterWorldShutdown();return 6;
}

#endif
