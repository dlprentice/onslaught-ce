// SPDX-License-Identifier: GPL-3.0-only
// Actual transport + Session at controlled world/native-quit boundaries. Authored loopback peers only.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <sys/prctl.h>
#define CE_NetDestroy ActualTransportDestroy
#include TRANSPORT_SOURCE
#undef CE_NetDestroy
extern "C" void CE_NetDestroy(CE_NetSession*);
#include SESSION_SOURCE
static unsigned checks, gameplay_calls, metadata_acks, backpressure;
static bool reject_birth_ack;
static CE_NetSession *observed_receiver;
static CE_NetStatus destroyed_receiver_status;
static unsigned aborted_transports;
static void Check(bool value,const char *what){++checks;if(!value){fprintf(stderr,"FAIL %s\n",what);std::_Exit(2);}}
extern "C" void CE_NetDestroy(CE_NetSession *net){
 if(net&&s.stage==FAILED){Check(!s.net,"failed Session detaches transport before actual destruction");++aborted_transports;}
 if(net&&net==observed_receiver)CE_NetGetStatus(net,&destroyed_receiver_status);
 ActualTransportDestroy(net);
}
using EQuitType=int;using BOOL=int;
struct NativeQuit {
 int mQuit=0,mGameState=3;int *mPlayer[4]{},*mCurrentCamera[4]{};
 enum {QT_NONE=0,GAME_STATE_PLAYING=3,GAME_STATE_QUIT=9};
#include "set-quit.inc"
} game;
struct CE_WorldFrame {CE_WorldInfo info;};
extern "C" {
int CE_WorldBegin(const CE_WorldConfig*){++gameplay_calls;return 1;}
void CE_WorldEnd(){}
int CE_WorldActive(){return 0;}
int CE_WorldReplica(){return 0;}
int CE_WorldCanRender(){return 1;}
const char*CE_WorldError(){return "controlled world boundary";}
int CE_NetLoadedAdmissionValidate(){return 1;}
const char*CE_NetLoadedAdmissionError(){return "";}
// The lockstep, boot, compatibility and lifecycle boundaries the session now reaches: this driver starts no lockstep session and no
// boot step, so each answers "not requested" or "nothing to do" and a tick or a teardown passes through unchanged.
int CE_BootRequested(){return 0;}
static void AdmissionDigest(CE_CompatibilityResult*r){for(unsigned i=0;i<32;++i)r->digest[i]=uint8_t(i^0xa5);r->files=1;r->bytes=1;}	// the digest the sessions here are configured with
int CE_BootPrepared(CE_CompatibilityResult*r,char*,size_t){AdmissionDigest(r);return 1;}
int CE_CompatibilityCheckBoundary(char*,size_t){return 1;}
int CE_CompatibilityPrepared(CE_CompatibilityResult*r,char*,size_t){AdmissionDigest(r);return 1;}
void CE_CompatibilityRetire(){}
int CE_LockstepRequested(){return 0;}
int CE_LockstepActive(){return 0;}
int CE_LockstepConfigureLevel(uint32_t){return 0;}
int CE_LockstepAfterLoad(){return 1;}
int CE_LockstepPreparePlay(){return 1;}
int CE_LockstepBeforeUpdate(){return 1;}
void CE_LockstepAfterUpdate(){}
void CE_LockstepBeforeRender(){}
void CE_LockstepAfterWorldShutdown(){}
int CE_LockstepBeginTeardown(int quit){return quit;}
int CE_LockstepExitCode(int native_code,int){return native_code;}
const char*CE_LockstepNotice(){return "";}
const char*CE_LockstepError(){return "";}
int CE_WorldFrameReadiness(const CE_WorldFrame*){return 0;}
int CE_WorldLifecycleStart(const CE_WorldFrame*){return 0;}
int CE_WorldInspectLifecycle(const uint8_t*,uint32_t,CE_WorldLifecycleInfo*){return 0;}
int CE_WorldPeekLifecycle(CE_WorldLifecycleInfo*,uint8_t*,uint32_t){return 0;}
int CE_WorldLifecyclePublished(const CE_WorldLifecycleInfo*){return 0;}
int CE_WorldApplyLifecycle(const uint8_t*,uint32_t){return 0;}
int CE_WorldAppliedLifecycle(CE_WorldLifecycleAck*){return 0;}
int CE_WorldAcknowledgeLifecycle(const CE_WorldLifecycleAck*){return 0;}
CE_WorldFrame*CE_WorldFrameCreate(){return new CE_WorldFrame{};}
void CE_WorldFrameDestroy(CE_WorldFrame*p){delete p;}
int CE_WorldFrameInfo(const CE_WorldFrame*,CE_WorldInfo*){++gameplay_calls;return 0;}
int CE_WorldFramePublished(const CE_WorldFrame*){++gameplay_calls;return 0;}
int CE_WorldCapture(CE_WorldFrame*){++gameplay_calls;return 0;}
int CE_WorldEncode(const CE_WorldFrame*,uint8_t*,uint32_t,uint32_t*){++gameplay_calls;return 0;}
int CE_WorldDecode(CE_WorldFrame*,const uint8_t*,uint32_t){++gameplay_calls;return 0;}
int CE_WorldApply(const CE_WorldFrame*){++gameplay_calls;return 0;}
int CE_WorldAppliedBirths(CE_WorldBirthAck*){return 0;}
int CE_WorldAcknowledgeBirths(uint32_t,uint32_t,uint32_t){++metadata_acks;return reject_birth_ack?-1:1;}
int CE_WorldSampleInput(CE_WorldInput*){++gameplay_calls;return 0;}
int CE_WorldInputSent(uint32_t){++gameplay_calls;return -1;}
int CE_WorldEncodeInput(const CE_WorldInput*,uint8_t*,uint32_t,uint32_t*){++gameplay_calls;return 0;}
int CE_WorldDecodeInput(CE_WorldInput*,const uint8_t*,uint32_t){++gameplay_calls;return 0;}
int CE_WorldQueueRemoteInput(const CE_WorldInput*){++gameplay_calls;return -1;}
int CE_WorldSetInputSuspended(uint32_t){++gameplay_calls;return -1;}
void CE_WorldRebaseLocalSampler(){++gameplay_calls;}
uint32_t CE_WorldLastGeneratedSequence(){return 0;}
uint32_t CE_SessionWindowStatus(){return 0;}
void CE_SessionGamePreRun(){++gameplay_calls;}
int CE_SessionGamePump(){return 0;}
void CE_SessionGameIdle(){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
int CE_SessionGameQuitCode(){return game.mQuit;}
void CE_SessionGameSetQuit(int quit){game.SetQuit(quit);}
uint32_t CE_SessionGameTick(){return 60;}
void CE_SessionGameRebaseRenderClock(){++gameplay_calls;}
}
bool PortClockVirtual(){return false;}
int PortLobbyShellExit(int code,int){return code;}	// the shell's final return, as the ordinary exit answers it (patch 0140)
static void Service(CE_NetSession*a,CE_NetSession*b){CE_NetPoll(a,CE_NetNowMs(),32);CE_NetPoll(b,CE_NetNowMs(),32);std::this_thread::sleep_for(std::chrono::milliseconds(1));}

// Genuine final shell return/getters, with system initialization and window destruction kept at resource boundaries.
struct PCLTShell {
 struct {unsigned message=0x1c;int wParam=1;} mMsg;
 enum {WM_QUIT=0x12};
#include "shell-getters.inc"
 int MainLoop(); void *GetHWnd(){return nullptr;}void Cleanup3DEnvironment(){}
} LT;
struct {bool Init(){return false;}void Run(){}void Shutdown(){}} SYSTEM;
static void con_whatami(char*){}
struct {void RegisterCommand(const char*,const char*,void(*)(char*)){}void Print(const char*,...){}} CONSOLE;
struct {int GetMemorySize(){return 0;}} PLATFORM;
static void*GetMenu(void*){return nullptr;}static void DestroyMenu(void*){}static void DestroyWindow(void*){}
enum {QT_NONE=0,QT_LOAD_ERROR=3,MAX_PLAYERS=4};
#define SAFE_DELETE(p) do{delete(p);(p)=nullptr;}while(0)
static NativeQuit &GAME=game;
struct NativeAbort {
#include "abort-load.inc"
};
extern "C" int CE_SessionGameAbortLoad(){return NativeAbort::AbortLoad();}
static void PortSessionSummary(){}
#include "shell-main-loop.inc"
static void SendAuthoredControl(CE_NetSession *sender,unsigned kind=LEAVING,uint32_t sequence=0,uint32_t tick=0,bool malformed=false){
 uint8_t bytes[24];CE_NetWriter w;CE_NetWriterInit(&w,bytes,sizeof bytes);
 CE_NetWriteU32(&w,malformed?0:kControlMagic);CE_NetWriteU32(&w,kControlSchema);CE_NetWriteU32(&w,kind);
 CE_NetWriteU32(&w,852);CE_NetWriteU32(&w,sequence);CE_NetWriteU32(&w,tick);
 Check(CE_NetSend(sender,CE_NET_CONTROL,tick,bytes,sizeof bytes)==1,"actual authored control queued");
}
static void InvalidLeave(CE_NetSession *sender,const char *mode){
 Header h{};h.kind=kLeave;h.channel=0;h.epoch=1+(strcmp(mode,"wrong-epoch-leave")==0);
 h.message=++sender->messageSequence[0];h.parts=1;memcpy(h.session,sender->session,16);
 if(!strcmp(mode,"wrong-session-leave"))h.session[0]^=0x80;
 uint8_t wrong[32]{};
 Check(QueuePacket(sender,h,nullptr,0,!strcmp(mode,"unauthenticated-leave")?wrong:sender->key),"authored invalid proof queued at actual wire boundary");
 enet_host_flush(sender->host);
}
int main(int argc,char**argv){
 Check(prctl(PR_SET_DUMPABLE,0,0,0,0)==0,"owned fixture core suppression");Check(argc==2,"one case");std::string name=argv[1];const char*mode=argv[1];
 bool guest_receiver=name.rfind("guest-",0)==0;
 CE_NetConfig config{};config.role=CE_NET_HOST;config.ipv4="127.0.0.1";config.handshake_ms=1000;config.silence_ms=200;
 for(unsigned i=0;i<32;++i){config.secret[i]=uint8_t(i+1);config.compatibility[i]=uint8_t(i^0xa5);}
 auto host=CE_NetCreate(&config);Check(host!=nullptr,"actual host allocation");CE_NetStatus status{};CE_NetGetStatus(host,&status);config.port=status.port;config.role=CE_NET_GUEST;auto guest=CE_NetCreate(&config);Check(guest!=nullptr,"actual guest allocation");
 uint64_t start=CE_NetNowMs();while((host->status.phase!=CE_NET_ACTIVE||guest->status.phase!=CE_NET_ACTIVE)&&CE_NetNowMs()-start<1500)Service(host,guest);
 Check(host->status.phase==CE_NET_ACTIVE&&guest->status.phase==CE_NET_ACTIVE,"actual ENet/Monocypher authentication completed");
 auto receiver=guest_receiver?guest:host;auto sender=guest_receiver?host:guest;observed_receiver=receiver;
 // Only game/world admission is controlled. Peer transports, Session service and native quit/shell methods are actual.
 s.net=receiver;s.requested=true;s.host=!guest_receiver;s.level=852;s.epoch=1;s.stage=RUNNING;s.world_started=true;s.mission_vm_blocked=guest_receiver;
 for(unsigned i=0;i<32;++i)s.state_context[i]=uint8_t(i^0xa5);	// the content digest this session was admitted with (CE_BootPrepared's)
 if(name=="load-close"||name=="load-remote-error"){s.stage=LOADING;s.world_started=false;}
 if(name=="prepare-close"||name=="guest-baseline-close"||name=="prepare-remote-error"||name=="guest-baseline-remote-error"){s.stage=PREPARING;s.have_baseline=name.find("baseline")!=std::string::npos;}
 bool reject=name=="wrong-epoch-leave"||name=="wrong-session-leave"||name=="unauthenticated-leave";
 bool remote_error=name.find("remote-error")!=std::string::npos;
 bool error=remote_error||name=="guest-backpressure"||name=="error-reset-offline"||name=="malformed-control"||name=="malformed-then-close"||name=="leave-then-malformed"||name=="late-malformed"||name=="unexpected-then-close"||name=="bare-disconnect"||name=="timeout"||name=="invalid-leave-shape"||name=="invalid-wait-close"||name=="invalid-birth-close"||name=="queue-overflow"||name=="fail-then-leave";
 bool own=name=="own-quit-first";
 if(remote_error){
  // Two independently owned Session contexts are controlled here, with actual Fail and both live transports.
  Session receiver_context=s;s.net=sender;s.host=!receiver_context.host;s.mission_vm_blocked=!s.host;
  if(name=="guest-syncing-remote-error")Check(CE_NetBeginEpoch(sender,2)==1&&sender->status.phase==CE_NET_SYNCING,"actual sender epoch transition enters SYNCING");
  if(name=="remote-error-pending"){
   uint8_t bytes[32]={};for(unsigned i=0;i<16;++i)Check(CE_NetSend(sender,CE_NET_INPUT,61,bytes,sizeof bytes)==1,"actual pending outbound packet queued before sender failure");
   Check(sender->pendingPackets>0,"sender retains live FreePacket callback buffers before abort");
  }
  Fail("authored remote application failure");
  Check(!s.net&&s.stage==FAILED&&s.requested&&s.mission_vm_blocked==!s.host,"actual sender failure retains local world and mission fences after detach");
  Fail("repeated authored failure");Check(!s.net&&s.stage==FAILED,"repeated sender failure has NULL-safe destruction");
  CE_SessionBeginTeardown(6);Check(!s.net&&s.requested&&s.mission_vm_blocked==!s.host,"sender teardown retains world and mission ownership after abort");
  CE_SessionAfterWorldShutdown();Check(!s.requested&&!CE_SessionMissionVMBlocked(),"sender final world retirement is NULL-safe and clears ownership once");
  sender=nullptr;s=receiver_context;exit_outcome=EXIT_NONE;error_text[0]=0;
 }else if(name=="app-leaving")SendAuthoredControl(sender);
 else if(name=="both-leaving"||name=="reset-offline"||name=="reset-network"||name=="reset-invalid") {SendAuthoredControl(sender);CE_NetClose(sender);}
 else if(name=="malformed-control"||name=="fail-then-leave")SendAuthoredControl(sender,LEAVING,0,0,true);
 else if(name=="malformed-then-close"||name=="error-reset-offline"){SendAuthoredControl(sender,LEAVING,0,0,true);CE_NetClose(sender);}
 else if(name=="leave-then-malformed"){SendAuthoredControl(sender);SendAuthoredControl(sender,LEAVING,0,0,true);CE_NetClose(sender);}
 else if(name=="late-malformed"){for(unsigned i=0;i<40;++i)SendAuthoredControl(sender);SendAuthoredControl(sender,LEAVING,0,0,true);CE_NetClose(sender);}
 else if(name=="queue-overflow"){for(unsigned i=0;i<65;++i)SendAuthoredControl(sender);CE_NetClose(sender);}
 else if(name=="full-terminal-queue"){for(unsigned i=0;i<64;++i)SendAuthoredControl(sender);CE_NetClose(sender);}
 else if(name=="unexpected-then-close"){SendAuthoredControl(sender,99);CE_NetClose(sender);}
 else if(name=="invalid-leave-shape"){SendAuthoredControl(sender,LEAVING,1);CE_NetClose(sender);}
 else if(name=="bare-disconnect")enet_peer_disconnect_now(sender->peer,0);
 else if(reject)InvalidLeave(sender,mode);
 else if(name=="own-quit-first"){SendAuthoredControl(sender,LEAVING,0,0,true);CE_NetClose(sender);}
 else if(name=="terminal-input-fifo"){s.remote_count=1;uint8_t bytes[3]={};Check(CE_NetSend(sender,CE_NET_INPUT,61,bytes,3)==1,"actual terminal input queued");CE_NetClose(sender);}
 else if(name=="host-wait-ack"||name=="host-resume-ack"||name=="invalid-wait-close"){
  s.wait_generation=1;s.wait_tick=60;s.shared_wait=true;s.wait_phase=name=="host-wait-ack"?WAIT_ACK:RESUME_ACK;
  SendAuthoredControl(sender,name=="host-wait-ack"?HOST_WAIT_ACK:HOST_RESUME_ACK,name=="invalid-wait-close"?2:1,60);CE_NetClose(sender);
 }else if(name=="guest-wait"||name=="guest-resume"){
  if(name=="guest-resume"){s.wait_generation=1;s.wait_tick=60;s.shared_wait=true;s.wait_ack_pending=false;}
  SendAuthoredControl(sender,name=="guest-wait"?HOST_WAIT:HOST_RESUME,1,60);CE_NetClose(sender);
 }else if(name=="birth-close"||name=="invalid-birth-close"){
  reject_birth_ack=name=="invalid-birth-close";SendAuthoredControl(sender,BIRTHS_APPLIED,2,60);CE_NetClose(sender);
 }else if(name=="guest-backpressure"){
  // STATE is UNSEQUENCED and superseded, so it never fills the sender's queue; the reliable baseline channel does.
  uint8_t bytes[CE_NET_MAX_MESSAGE]={};while(CE_NetSend(sender,CE_NET_BASELINE,60,bytes,sizeof bytes)==1)++backpressure;
  Check(backpressure>0,"actual reliable baseline sender reaches bounded Send0 backpressure");SendAuthoredControl(sender);CE_NetClose(sender);
 }else if(name!="timeout")CE_NetClose(sender);
 // Coalesce actual authenticated packets before Session service to reproduce the entire frozen completed queue.
 if(name!="app-leaving"&&name!="malformed-control"&&name!="fail-then-leave"&&!reject&&name!="timeout"){
  start=CE_NetNowMs();while(receiver->status.phase==CE_NET_ACTIVE&&CE_NetNowMs()-start<(name=="guest-backpressure"?500u:100u)){CE_NetPoll(receiver,CE_NetNowMs(),512);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(name!="bare-disconnect")Check(receiver->status.phase==CE_NET_CLOSED||receiver->status.phase==CE_NET_FAILED,"actual terminal transport was observed before callback");
 }
 unsigned retained=receiver->count;Check(retained<=64&&receiver->queuedBytes<=256u*1024u,"frozen completed queue retains original message and byte caps");
 if(own){game.SetQuit(2);LT.mMsg.message=PCLTShell::WM_QUIT;LT.mMsg.wParam=7;}
 start=CE_NetNowMs();while(!game.mQuit&&CE_NetNowMs()-start<(reject?80u:1500u)){
  if(name!="timeout")CE_NetPoll(sender,CE_NetNowMs(),32);
  if(name=="load-close"||name=="load-remote-error"){if(!CE_SessionAfterLoad())CE_SessionGameAbortLoad();break;}
  else if(name=="prepare-close"||name=="guest-baseline-close"||name=="prepare-remote-error"||name=="guest-baseline-remote-error"){if(!CE_SessionPreparePlay())CE_SessionGameAbortLoad();break;}
  else if(name=="paused-service"||name=="device-unready")CE_SessionSystemService(name=="paused-service",name=="device-unready"?2:1);
  else if(name=="after-flush")CE_SessionAfterFlush();
  else if(name=="before-render")CE_SessionBeforeRender();
  else CE_SessionBeforeUpdate();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 Check(!gameplay_calls,"no world dispatch Apply Capture wait gate or clock rebase follows terminal intent");
 if(own){Check(!CE_SessionBeforeUpdate(),"prior native quit fences queued malformed departure");Check(!error_text[0]&&exit_outcome==EXIT_NONE,"known native quit keeps precedence without network failure");}
 else if(reject)Check(!game.mQuit&&s.stage==RUNNING&&receiver->status.phase==CE_NET_ACTIVE&&receiver->status.rejected_packets>0,"wrong epoch/session or invalid AEAD cannot prove departure");
 else if(error){bool native_abort=name=="load-remote-error"||name=="prepare-remote-error"||name=="guest-baseline-remote-error";Check(game.mQuit==(native_abort?3:6)&&s.stage==(native_abort?DRAINING:FAILED)&&exit_outcome==EXIT_ERROR&&error_text[0],"malformed loss and transport failures retain error outcome");}
 else {Check(game.mQuit==1&&game.mGameState==9&&s.stage==DRAINING&&!error_text[0]&&exit_outcome==EXIT_GRACEFUL,"authenticated intentional departure uses native QUIT non-WON frontend outcome");}
 if(name=="fail-then-leave"){SendAuthoredControl(sender);Check(!CE_SessionBeforeUpdate()&&exit_outcome==EXIT_ERROR,"later peer leave cannot downgrade a latched error");}
 Check(!gameplay_calls,"no world dispatch Apply Capture wait gate or clock rebase follows terminal intent");
 if(s.net)CE_NetGetStatus(s.net,&status);else status=destroyed_receiver_status;int expected=own?7:reject?1:error?1:0;
 if(remote_error)Check(aborted_transports==2,"actual sender and receiver aborts destroy each transport exactly once");
 CE_SessionBeginTeardown(game.mQuit?game.mQuit:1);CE_NetDestroy(sender);CE_SessionAfterWorldShutdown();Check(!s.requested&&!CE_SessionMissionVMBlocked(),"native world retirement releases Session and mission fence");
 Check(LT.MainLoop()==expected,"actual shell return maps retained outcome and preserves genuine WM_QUIT");
 Check(CE_SessionExitCode(23,1)==23,"genuine native WM_QUIT code always retains precedence");
 if(name=="reset-offline"||name=="error-reset-offline"){unsetenv("BEA_NET_HOST");unsetenv("BEA_NET_JOIN");Check(CE_SessionConfigureLevel(100)&&CE_SessionExitCode(17,0)==17,"successful ordinary configuration clears prior terminal outcome");}
 if(name=="reset-invalid"){setenv("BEA_NET_JOIN","bad",1);Check(!CE_SessionConfigureLevel(852)&&CE_SessionExitCode(0,0)==1,"failed new admission cannot inherit old graceful outcome");CE_SessionAfterWorldShutdown();}
 if(name=="reset-network"){
  char endpoint[32];snprintf(endpoint,sizeof endpoint,"127.0.0.1:%u",config.port);setenv("BEA_NET_HOST",endpoint,1);setenv("BEA_NET_SECRET","0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",1);
  // An authored nonzero digest; no actual invite or player data is read.
  setenv("BEA_NET_COMPAT","abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",1);
  Check(CE_SessionConfigureLevel(852)&&CE_SessionExitCode(17,0)==17,"successful new network configuration clears prior graceful outcome");CE_SessionBeginTeardown(1);CE_SessionAfterWorldShutdown();
 }
 printf("{\"case\":\"%s\",\"checks\":%u,\"transport_phase\":%u,\"transport_error\":%u,\"retained\":%u,\"received\":%u,\"native_quit\":%d,\"native_phase\":%d,\"shell_exit\":%d,\"aborted_transports\":%u,\"metadata_acks\":%u,\"backpressure_batches\":%u,\"real_authentication\":true,\"game_or_devices\":false}\n",mode,checks,status.phase,status.error,retained,status.received_messages,game.mQuit,game.mGameState,expected,aborted_transports,metadata_acks,backpressure);
 return 0;
}
