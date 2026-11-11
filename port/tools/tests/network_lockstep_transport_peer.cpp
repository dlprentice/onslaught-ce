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

// This transport-only world has no queued campaign boot request.
extern "C" int CE_BootCampaignGet(CE_CampaignLaunch *, int *) { return 0; }
extern "C" void CE_BootCampaignClear(void) {}
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
