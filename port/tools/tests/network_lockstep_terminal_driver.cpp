// SPDX-License-Identifier: GPL-3.0-only
// Compiles the real lockstep controller with only world/transport boundaries stubbed. Not a game run.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
#define CE_NET_TEST_AUTOMATION 1
#ifdef CE_LOCKSTEP_REAL_ADMISSION
extern "C" FILE *port_fopen(const char *, const char *);
#define fopen port_fopen // the production GameOpen owner goes through the effective reader
#endif
#include "../../game/ce_network_lockstep.cpp"
#include LOCKSTEP_VALIDATOR_INC

MockGame GAME;
MockEventManager EVENT_MANAGER;
// The native getter is extracted from src/game.cpp by the test runner; only its logger and storage are stubbed.
using BOOL=int;
enum { FALSE=0 };
struct NativeGame { int mSlots[MAX_CAREER_SLOTS]={}; int GetSlot(int); };
struct MockLog { int calls=0; void AddMessage(const char *,int){++calls;} } LOG;
#include NATIVE_SLOT_GETTER_INC
struct CE_NetSession {};
static CE_NetSession mock_net;
static int quit_code=0;
static bool fake_prerun_clear=false,fake_prerun_quit=false;static unsigned fake_prerun_word=0;
static bool fake_prerun_mutate_career=false;static unsigned fake_career_word=0,fake_prerun_calls=0;
static bool fake_prerun_admitted=false, fake_mutate_rng=false;
static CE_CampaignLaunch fake_campaign_launch = {};
static int fake_campaign_host = 0;
static bool fake_campaign_admitted = false;
static unsigned fake_campaign_clears = 0;
static unsigned fake_calls=0,fake_hold=1,prerun_calls_seen=0,prerun_hold_seen=0;
static int fake_rng_value=1,fake_rng_seed=2,prerun_value_seen=0,prerun_seed_seen=0;
static uint64_t now_ms=100;
static CE_NetStatus mock_status={CE_NET_ACTIVE,CE_NET_OK,CE_NET_HOST,0,1,0,0,0,0};
struct Packet { uint32_t type,tick; uint64_t value; uint64_t second=0; uint32_t size=0; uint32_t envelope=UINT32_MAX; std::vector<uint8_t> raw; };
static std::deque<Packet> inbound;
static std::vector<Packet> sent;
static unsigned closes=0, destroys=0, graceful_leaves=0, desync_attempts=0;
static bool close_saw_desync=false;
static uint64_t fake_world_value=0x12345678;
static uint64_t fake_gate_value=0x42; static bool fake_gate_valid=true;
static bool mutate_on_placement=false;
enum { IDLE_NONE, IDLE_MATCH, IDLE_MISMATCH, IDLE_TIMEOUT, IDLE_CLOSE, IDLE_FAIL, IDLE_DELAY_READY, IDLE_FAIL_UNREADY, IDLE_CANCEL,
       IDLE_PREHASH, IDLE_ACK_DELAY, IDLE_ACK_TIMEOUT, IDLE_ACK_CLOSE, IDLE_ACK_MISMATCH, IDLE_ACK_DUP,
       IDLE_ACK_EARLY, IDLE_ACK_SAMEPOLL, IDLE_ACK_DUP_LATE, IDLE_ACK_BAD_TICK, IDLE_ACK_BAD_ENVELOPE, IDLE_ACK_SHORT,
       IDLE_PRERUN_CLOSE, IDLE_PRERUN_TIMEOUT, IDLE_PRERUN_CHANGE, IDLE_PRERUN_CHANGE_EARLY };
static int fake_idle_mode=IDLE_NONE,fake_idle_count=0,fake_pump_count=0,fake_created=0;
static bool fake_reentrant_blocked=true,fake_retry=false;
static bool fake_sent_ack_before_match=false;
static void ready_packet();
static void pre_ack_packet();
static void start_init(bool host,int idle_mode);
static unsigned reject_send_type=0;
static CE_WorldInput fake_sample={},last_applied={};
static int applied_count=0;static uint32_t applied_player=99;
extern "C" {
void (*gPortCrtTrace)(void *)=nullptr;
void PortCrtRandState(unsigned *c,unsigned *h){*c=fake_calls;*h=fake_hold;}
void PortCrtRandSet(unsigned c,unsigned h){fake_calls=c;fake_hold=h;}
int CE_CompatibilityLockstepDigest(uint32_t,uint8_t out[32]){memset(out,1,32);return 1;}
static int fake_boot_lockstep=0;	// 1: this process was started by the lobby and admitted by the boot (the shipped route)
int CE_BootLockstep(void){return fake_boot_lockstep;}
#ifdef CE_LOCKSTEP_REAL_ADMISSION
int CE_BootPrepared(CE_CompatibilityResult *result,char *error,size_t size){return CE_CompatibilityPrepared(result,error,size);}
#else
int CE_BootPrepared(CE_CompatibilityResult *,char *,size_t){return 0;}	// ordinary terminal fixtures use only the test-automation route
int CE_CompatibilityAdmissionLive(char *,size_t){return 0;}
#endif
void CE_WorldLockstepSample(uint32_t,CE_WorldInput *out){*out=fake_sample;}
void CE_WorldLockstepApply(uint32_t p,const CE_WorldInput *in){last_applied=*in;applied_player=p;++applied_count;}
void CE_WorldLockstepRandom(int save,int *v,int *s){if(save){*v=fake_rng_value;*s=fake_rng_seed;}else{fake_rng_value=*v;fake_rng_seed=*s;}}
uint64_t CE_WorldLockstepThings(FILE *,uint32_t *c){*c=1;return fake_world_value;}
int CE_WorldLockstepGates(uint64_t *out){if(!fake_gate_valid||!out)return 0;*out=fake_gate_value;return 1;}
void CE_WorldLockstepPartnerStart(void *){}
void CE_WorldLockstepPlaceEmitters(void *const *,const int *,unsigned){if(mutate_on_placement)fake_world_value^=0xffff;}
int CE_WorldLockstepEndTick(){return 1;}
void CE_WorldLockstepBeginTick(){}
void CE_SessionGamePreRun(){++fake_prerun_calls;fake_prerun_admitted=L.stage==PRERUN&&L.peer_ready&&L.prerun_ack_sent&&L.prerun_ack_received&&CE_LockstepBeforeUpdate()==1;prerun_calls_seen=fake_calls;prerun_hold_seen=fake_hold;prerun_value_seen=fake_rng_value;prerun_seed_seen=fake_rng_seed;if(fake_prerun_mutate_career)fake_career_word^=1u;if(fake_prerun_clear)GAME.slot_words[fake_prerun_word]=0;if(fake_prerun_quit)quit_code=QT_QUIT_TO_SYSTEM;}
int CE_SessionGamePump(){if(fake_idle_mode!=IDLE_NONE){++fake_pump_count;if(CE_LockstepBeforeUpdate()!=0)fake_reentrant_blocked=false;if(fake_mutate_rng){++fake_calls;++fake_hold;++fake_rng_value;++fake_rng_seed;}if(fake_idle_mode==IDLE_CANCEL)quit_code=QT_QUIT_TO_SYSTEM;}return 0;}
void CE_SessionGameIdle(){
 if(fake_idle_mode==IDLE_NONE)return;
 ++fake_idle_count;now_ms+=(fake_idle_mode==IDLE_TIMEOUT||(fake_idle_mode==IDLE_ACK_TIMEOUT&&fake_idle_count>=3))?30001:(fake_idle_mode==IDLE_PRERUN_TIMEOUT?60001:1);
 if(fake_idle_mode==IDLE_DELAY_READY && fake_idle_count==2)ready_packet();
 if((fake_idle_count==2 && (fake_idle_mode==IDLE_MATCH||fake_idle_mode==IDLE_MISMATCH||fake_idle_mode==IDLE_ACK_DELAY||
    fake_idle_mode==IDLE_ACK_TIMEOUT||fake_idle_mode==IDLE_ACK_CLOSE||fake_idle_mode==IDLE_ACK_MISMATCH||
    fake_idle_mode==IDLE_ACK_DUP||fake_idle_mode==IDLE_ACK_SAMEPOLL||fake_idle_mode==IDLE_ACK_DUP_LATE||
    fake_idle_mode==IDLE_ACK_BAD_TICK||fake_idle_mode==IDLE_ACK_BAD_ENVELOPE||fake_idle_mode==IDLE_ACK_SHORT)) ||
    (fake_idle_count==4 && fake_idle_mode==IDLE_DELAY_READY) ||
    (fake_idle_count==3 && fake_idle_mode==IDLE_PRERUN_CHANGE)){
  Packet p{};p.type=MSG_HASH;p.tick=0;p.value=StateHash(0)^(fake_idle_mode==IDLE_MISMATCH?1ull:0ull);inbound.push_back(p);
 }
 bool ack=(fake_idle_count==4 && (fake_idle_mode==IDLE_MATCH||fake_idle_mode==IDLE_ACK_MISMATCH||
           fake_idle_mode==IDLE_ACK_DUP||fake_idle_mode==IDLE_ACK_DUP_LATE||fake_idle_mode==IDLE_ACK_BAD_TICK||
           fake_idle_mode==IDLE_ACK_BAD_ENVELOPE||fake_idle_mode==IDLE_ACK_SHORT)) ||
          (fake_idle_count==6 && (fake_idle_mode==IDLE_DELAY_READY||fake_idle_mode==IDLE_ACK_DELAY)) ||
          (fake_idle_count==3 && fake_idle_mode==IDLE_PREHASH) ||
          (fake_idle_count==2 && (fake_idle_mode==IDLE_ACK_EARLY||fake_idle_mode==IDLE_ACK_SAMEPOLL)) ||
          (fake_idle_count==5 && (fake_idle_mode==IDLE_ACK_DUP_LATE||fake_idle_mode==IDLE_PRERUN_CHANGE));
 if(ack){
  Packet p{};p.type=12;p.tick=fake_idle_mode==IDLE_ACK_BAD_TICK?1:0;p.envelope=fake_idle_mode==IDLE_ACK_BAD_ENVELOPE?1:(fake_idle_mode==IDLE_ACK_BAD_TICK?0:UINT32_MAX);
  p.value=StateHash(0)^(fake_idle_mode==IDLE_ACK_MISMATCH?1ull:0ull);
  if(fake_idle_mode==IDLE_ACK_SHORT){uint8_t data[16];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof data);CE_NetWriteU32(&w,kMagic);CE_NetWriteU8(&w,12);CE_NetWriteU32(&w,0);p.raw.assign(data,data+w.size);}
  inbound.push_back(p);if(fake_idle_mode==IDLE_ACK_DUP)inbound.push_back(p);
 }
 if(fake_idle_count==2 && fake_idle_mode==IDLE_CLOSE){mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;}
 if(fake_idle_count==2 && fake_idle_mode==IDLE_FAIL){mock_status.phase=CE_NET_FAILED;mock_status.error=CE_NET_TIMEOUT;}
 if(fake_idle_count==2 && fake_idle_mode==IDLE_FAIL_UNREADY){mock_status.phase=CE_NET_FAILED;mock_status.error=CE_NET_TIMEOUT;}
 if(fake_idle_count==2 && fake_idle_mode==IDLE_PRERUN_CLOSE){mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;}
 if(fake_idle_count==2 && fake_idle_mode==IDLE_PRERUN_CHANGE){GAME.slot_words[0]^=1u;pre_ack_packet();}
 if(fake_idle_count==1 && fake_idle_mode==IDLE_PRERUN_CHANGE_EARLY)GAME.slot_words[0]^=1u;
 if(fake_idle_count==4 && fake_idle_mode==IDLE_ACK_CLOSE){mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;}
}
int CE_SessionGameQuitCode(){return quit_code;}
void CE_SessionGameSetQuit(int v){quit_code=v;}
void CE_SessionGameRebaseRenderClock(){}
uint64_t CE_NetNowMs(){return now_ms;}
CE_NetSession *CE_NetCreate(const CE_NetConfig *){++fake_created;if(fake_retry&&fake_created==2){mock_status.phase=CE_NET_ACTIVE;mock_status.error=CE_NET_OK;}return &mock_net;}
void CE_NetDestroy(CE_NetSession *s){if(s)++destroys;}
void CE_NetClose(CE_NetSession *){++closes;if(mock_status.phase==CE_NET_ACTIVE||mock_status.phase==CE_NET_SYNCING){++graceful_leaves;for(const auto &p:sent)if(p.type==MSG_DESYNC)close_saw_desync=true;}}
void CE_NetPoll(CE_NetSession *,uint64_t,uint32_t){}
void CE_NetGetStatus(const CE_NetSession *,CE_NetStatus *out){*out=mock_status;}
int CE_NetPeerVersion(const CE_NetSession *,char *,uint32_t){return 0;}
int CE_NetSend(CE_NetSession *,uint32_t,uint32_t,const uint8_t *data,uint32_t size){
 if(mock_status.phase!=CE_NET_ACTIVE)return 0; // the real transport refuses sends after peer close
 CE_NetReader r;CE_NetReaderInit(&r,data,size);CE_NetReadU32(&r);Packet p{};p.type=CE_NetReadU8(&r);if(p.type==MSG_DESYNC)++desync_attempts;if(p.type==reject_send_type)return 0;p.tick=CE_NetReadU32(&r);if(size>=17)p.value=CE_NetReadU64(&r);if(size>=25)p.second=CE_NetReadU64(&r);p.size=size;
 p.raw.assign(data,data+size);
 if(p.type==13&&fake_idle_mode==IDLE_PREHASH){pre_ack_packet();Packet h{};h.type=MSG_HASH;h.tick=0;h.value=StateHash(0);inbound.push_back(h);}
 if(p.type==12&&(!L.hashes[0].local||!L.hashes[0].remote||L.hashes[0].mine!=L.hashes[0].theirs))fake_sent_ack_before_match=true;
 if (p.type == MSG_CAMPAIGN_RESULT) p.raw.assign(data, data + size);
    sent.push_back(p);return 1;
}
int CE_NetReceive(CE_NetSession *,CE_NetMessage *m,uint8_t *out,uint32_t cap){
 if(inbound.empty())return 0;Packet p=inbound.front();inbound.pop_front();CE_NetWriter w;CE_NetWriterInit(&w,out,cap);
 if(!p.raw.empty()){if(p.raw.size()>cap)return -1;memcpy(out,p.raw.data(),p.raw.size());w.size=(uint32_t)p.raw.size();}
 else {CE_NetWriteU32(&w,0x43454c31u);CE_NetWriteU8(&w,p.type);CE_NetWriteU32(&w,p.tick);CE_NetWriteU64(&w,p.value);}
 m->kind=CE_NET_CONTROL;m->tick=p.envelope==UINT32_MAX?p.tick:p.envelope;m->size=w.size;return 1;
}
}
static int failures=0;
static void check(bool ok,const char *what){std::printf("%s %s\n",ok?"PASS":"FAIL",what);failures+=!ok;}
static bool has(unsigned type){for(auto &p:sent)if(p.type==type)return true;return false;}
static uint64_t hash(){for(auto &p:sent)if(p.type==3&&p.tick==1)return p.value;return 0;}
static void init(bool host){
    ce_campaign_return_clear();
    fake_campaign_launch = {};
    fake_campaign_host = 0;
    fake_campaign_admitted = false;
    fake_campaign_clears = 0;
 L=Lockstep();L.stage=RUNNING;L.host=host;L.level=856;L.hash_every=1;L.hashes_compared=1;L.delay=3;L.quit_at=1;L.ran=true;L.sampled=true;L.net=&mock_net;
 quit_code=0;now_ms=100;fake_sample={};last_applied={};applied_count=0;applied_player=99;fake_world_value=0x12345678;fake_gate_value=0x42;fake_gate_valid=true;mutate_on_placement=false;reject_send_type=0;deferred_count=0;mock_status={CE_NET_ACTIVE,CE_NET_OK,host?CE_NET_HOST:CE_NET_GUEST,0,1,0,0,0,0};inbound.clear();sent.clear();closes=0;destroys=0;graceful_leaves=0;desync_attempts=0;close_saw_desync=false;GAME.state=GAME_STATE_PLAYING;
 fake_idle_mode=IDLE_NONE;fake_idle_count=0;fake_pump_count=0;fake_reentrant_blocked=true;fake_created=0;fake_retry=false;fake_sent_ack_before_match=false;
 fake_prerun_mutate_career=false;fake_career_word=0;fake_prerun_calls=0;fake_prerun_clear=false;fake_prerun_quit=false;fake_prerun_word=0;
 fake_prerun_admitted=false;fake_mutate_rng=false;fake_calls=0;fake_hold=1;fake_rng_value=1;fake_rng_seed=2;
 prerun_calls_seen=prerun_hold_seen=0;prerun_value_seen=prerun_seed_seen=0;
 for(auto &word:GAME.slot_words)word=0;CapturePreRunSlots();
}
static void in(unsigned type,uint64_t value,uint32_t tick=1){inbound.push_back({type,tick,value});}
static void reach(){CE_LockstepAfterUpdate();check(!quit_code,"no success before peer terminal confirmation");check(has(3),"normal final scheduled hash sent");check(has(5),"separate terminal state hash sent");}
static void peer_final(uint64_t h){in(3,h);in(5,h);CE_LockstepBeforeUpdate();}
static void peer_ack(uint64_t h){in(6,h);CE_LockstepBeforeUpdate();}
static void host(){init(true);reach();auto h=hash();peer_final(h);check(!quit_code&&has(6),"host compares final and acknowledges before exit");peer_ack(h);check(!quit_code&&has(7),"host sends release only after peer acknowledgement");in(8,h);CE_LockstepBeforeUpdate();check(!quit_code&&has(10),"host waits after sending DONE");in(11,h);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM,"host exits after delivery acknowledgement");check(CE_LockstepExitCode(37,0)==0,"successful test endpoint returns zero");}
static void guest(){init(false);reach();auto h=hash();peer_final(h);peer_ack(h);check(!quit_code,"guest waits for host release");in(7,h);CE_LockstepBeforeUpdate();check(!quit_code&&has(8),"guest confirms release and waits for DONE");in(10,h);CE_LockstepBeforeUpdate();check(!quit_code&&has(11),"synthetic guest sends DONE acknowledgement and waits for peer close");mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(38,0)==0,"guest completes only after confirmed peer close");}
static void mismatch(){init(true);reach();auto h=hash();in(3,h);in(5,h^1);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"terminal hash mismatch fails even when periodic hash matched");check(!has(6)&&has(4),"mismatched hash sends a desync and is not acknowledged");bool pair=false;for(auto &p:sent)if(p.type==4&&p.tick==1&&p.size==25&&p.value==h&&p.second==(h^1))pair=true;check(pair,"terminal desync includes both compared hash values");}
static void truncated(){init(true);reach();auto h=hash();in(5,h);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"missing scheduled final hash cannot pass");}
static void shipped_leaves_for_the_front_end(){
 // a shipped game goes back to the front end (the lobby tells the player); a test-automation pair, which has no lobby, quits to the system as it always did
 fake_boot_lockstep=1;
 last_error[0]=0;init(true);reach();auto h=hash();in(3,h);in(5,h^1);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&quit_code==QT_QUIT_TO_FRONTEND&&strstr(CE_LockstepError(),"final co-op states differ")!=nullptr,"a shipped game leaves for the front end after a desync, with the reason for the lobby");
 last_error[0]=0;init(true);L.quit_at=0;mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();
 check(L.outcome_error&&quit_code==QT_QUIT_TO_FRONTEND&&!strcmp(CE_LockstepError(),"the other player left the co-op game")&&CE_LockstepExitCode(0,0)!=0,"a shipped game that is left fails with the reason and leaves for the front end");
 fake_boot_lockstep=0;
 last_error[0]=0;init(true);reach();h=hash();in(3,h);in(5,h^1);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&quit_code==QT_QUIT_TO_SYSTEM,"a test-automation pair still quits to the system after a desync");
 last_error[0]=0;init(true);L.quit_at=0;mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();
 check(!L.outcome_error&&quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepError()[0]==0,"a test-automation pair that is left still ends quietly with an exit");
 last_error[0]=0;
}
static void early(){init(true);reach();mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"early peer exit is a failure");}
static void premature_ack(){init(true);reach();in(6,hash());CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"ACK before final hash fails");}
static void timeout_case(){init(true);reach();now_ms+=31000;CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"terminal wait times out as failure");}
static void remote_first(){init(true);SaveRandom();auto h=StateHash(1);in(3,h);in(5,h);CE_LockstepBeforeUpdate();check(!quit_code,"early remote final waits for local endpoint");reach();check(!quit_code&&has(6),"local endpoint processes retained peer final");peer_ack(h);in(8,h);CE_LockstepBeforeUpdate();in(11,h);CE_LockstepBeforeUpdate();check(CE_LockstepExitCode(37,0)==0&&quit_code==QT_QUIT_TO_SYSTEM,"peer-first exchange completes");}
static void wrong_tick(){init(true);reach();in(5,hash(),2);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"wrong final tick is refused");}
static void duplicate(){init(true);reach();auto h=hash();in(3,h);in(5,h);in(5,h);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"duplicate final is refused");}
static void bad_receipt(){init(true);reach();auto h=hash();peer_final(h);in(8,h);CE_LockstepBeforeUpdate();check(quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"receipt before release is refused");}
static void unaligned(){init(true);L.tick=1;L.quit_at=2;L.hash_every=3;SaveRandom();auto before=StateHash(2);mutate_on_placement=true;deferred_count=1;CE_LockstepAfterUpdate();uint64_t final=0;for(auto &p:sent)if(p.type==5)final=p.value;check(fake_world_value!=0x12345678,"deferred placement mutation executed");check(final==before,"unaligned final hash precedes deferred presentation placement");}
static void hash_window(){
 init(true);L.quit_at=0;L.tick=16;L.hash_every=20;in(3,7,20);check(Receive()&&L.stage==RUNNING,"hash up to delay plus one ticks ahead is admitted");
 init(true);L.quit_at=0;L.tick=1;L.hash_every=1;in(3,7,1000000);check(!Receive()&&L.outcome_error,"far-future hash is refused");
 init(true);L.quit_at=0;L.tick=19;L.hash_every=20;Packet bad={3,20,7};bad.envelope=21;inbound.push_back(bad);check(!Receive()&&L.outcome_error,"hash transport tick must equal payload tick");
 init(true);L.quit_at=0;L.tick=1280;L.hash_every=20;in(3,7,0);check(!Receive()&&L.outcome_error,"hash at the expired ring boundary is refused");
 init(true);L.quit_at=0;L.tick=1280;L.hash_every=20;L.hashes[0]={0,true,false,3,0};in(3,7,1280);check(!Receive()&&L.outcome_error,"remote hash cannot overwrite an unmatched ring slot");
 init(true);L.quit_at=0;L.tick=1280;L.hash_every=20;L.hashes[0]={0,true,true,3,3};in(3,7,1280);check(Receive()&&L.stage==RUNNING,"a completed ring slot can be reused");
 init(true);L.quit_at=0;L.tick=1279;L.hash_every=20;L.hashes[0]={0,false,true,0,3};CE_LockstepAfterUpdate();check(L.outcome_error,"local hash cannot overwrite an unmatched ring slot");
}
static void early_local_teardown(){init(true);L.tick=100;L.quit_at=5200;CE_LockstepBeginTeardown(QT_QUIT_TO_SYSTEM);check(L.outcome_error&&CE_LockstepExitCode(0,0)!=0,"local test teardown before endpoint is incomplete");}
static void peer_final_local_teardown(){
 init(true);L.quit_at=0;in(9,42,1);CE_LockstepBeforeUpdate();CE_LockstepBeginTeardown(QT_QUIT_TO_SYSTEM);
 check(L.outcome_error&&CE_LockstepExitCode(0,0)!=0,"local teardown after peer final is incomplete");
 init(true);L.quit_at=0;CE_LockstepBeginTeardown(QT_QUIT_TO_SYSTEM);
 check(!L.outcome_error&&!L.final_confirmed,"ordinary cancel before either final is unconfirmed, not a terminal failure");
}
static uint64_t mission_hash(){for(auto &p:sent)if(p.type==9&&p.tick==1)return p.value;return 0;}
static void natural_begin(bool host){init(host);L.quit_at=0;L.hash_every=2;GAME.state=GAME_STATE_LEVEL_WON;CE_LockstepAfterUpdate();check(L.stage==STOPPING&&!quit_code&&has(9),"natural end waits and publishes a separate final hash");}
static void natural_aligned(){init(true);L.quit_at=0;L.hash_every=1;GAME.state=GAME_STATE_LEVEL_WON;CE_LockstepAfterUpdate();check(has(3)&&has(9)&&mission_hash()==hash()&&mission_hash()!=0,"natural aligned final hash equals scheduled hash");}
static void natural_deferred(){init(true);L.quit_at=0;L.hash_every=2;GAME.state=GAME_STATE_LEVEL_WON;SaveRandom();uint64_t before=StateHash(1);mutate_on_placement=true;deferred_count=1;CE_LockstepAfterUpdate();check(mission_hash()==before&&fake_world_value!=0x12345678&&deferred_count==0,"natural end preserves pre-placement hash and completes deferred placement");}
static void natural_peer_first(){
 init(true);L.quit_at=0;L.hash_every=2;GAME.state=GAME_STATE_LEVEL_LOST;SaveRandom();auto h=StateHash(1);
 in(9,h);CE_LockstepBeforeUpdate();check(L.final_peer&&L.stage==RUNNING&&!quit_code,"peer-first natural final waits for local simulation");
 CE_LockstepAfterUpdate();check(L.stage==STOPPING&&L.final_ack_sent&&mission_hash()==h,"peer-first natural ending reaches the same tick and acknowledges");
 in(6,h);CE_LockstepBeforeUpdate();in(8,h);CE_LockstepBeforeUpdate();in(11,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"peer-first natural loss completes host handshake");
}
static void natural_aligned_exchange(bool host){
 init(host);L.quit_at=0;L.hash_every=1;GAME.state=GAME_STATE_LEVEL_LOST;CE_LockstepAfterUpdate();auto h=hash();
 in(3,h);in(9,h);CE_LockstepBeforeUpdate();check(L.final_ack_sent&&L.hashes_compared==2,"aligned natural loss compares scheduled hash before final acknowledgement");
 in(6,h);if(!host)in(7,h);CE_LockstepBeforeUpdate();if(host)in(8,h);else in(10,h);CE_LockstepBeforeUpdate();if(host){in(11,h);CE_LockstepBeforeUpdate();}
 check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,host?"aligned natural loss completes host":"aligned natural loss completes guest");
}
static void natural_outcome_mismatch(){
 init(true);L.quit_at=0;L.hash_every=2;GAME.state=GAME_STATE_LEVEL_WON;SaveRandom();auto won=StateHash(1);
 GAME.state=GAME_STATE_LEVEL_LOST;CE_LockstepAfterUpdate();check(mission_hash()!=won,"win/loss changes selected terminal hash");
 in(9,won);CE_LockstepBeforeUpdate();check(L.outcome_error&&!L.final_confirmed,"different win/loss outcomes cannot complete the handshake");
}
static void native_quit_error(){
 init(true);L.outcome_error=true;
 check(CE_LockstepExitCode(0,1)!=0,"outcome error overrides a zero native WM_QUIT code");
 check(CE_LockstepExitCode(17,1)==17,"nonzero native WM_QUIT code is preserved");
 L.outcome_error=false;check(CE_LockstepExitCode(0,1)==0,"successful zero native WM_QUIT code is preserved");
}
static void natural_host(){natural_begin(true);auto h=mission_hash();in(9,h);CE_LockstepBeforeUpdate();check(has(6)&&!quit_code,"natural host acknowledges peer final state");in(6,h);CE_LockstepBeforeUpdate();check(has(7)&&L.stage==STOPPING,"natural host awaits guest receipt");in(8,h);CE_LockstepBeforeUpdate();check(has(10)&&L.stage==STOPPING&&!L.final_confirmed,"natural host sends DONE but waits for delivery acknowledgement");in(11,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"natural host completes after delivery acknowledgement");}
static void natural_guest(){natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();check(has(8)&&L.stage==STOPPING&&!quit_code,"natural guest sends receipt but waits for host completion");in(10,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"natural guest reaches native end screen after host completion");}
static void natural_guest_done_lost(){natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();check(L.outcome_error&&!L.final_confirmed&&CE_LockstepExitCode(0,1)!=0,"guest refuses close after receipt when mission DONE is missing");}
static void natural_guest_done_timeout(){natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();now_ms+=31000;CE_LockstepBeforeUpdate();check(L.outcome_error&&!L.final_confirmed&&CE_LockstepExitCode(0,1)!=0,"guest times out after receipt when mission DONE is missing");}
static void natural_guest_done_and_close(){natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();in(10,h);mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();check(L.outcome_error&&!L.final_confirmed,"guest refuses same-poll DONE and close without a sendable acknowledgement");}
static void natural_completed_near_timeout(){natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();now_ms+=31000;in(11,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"completed mission does not fail the incomplete-wait timeout");}
static void natural_divergent(){natural_begin(true);auto h=mission_hash();in(9,h^1);CE_LockstepBeforeUpdate();check(L.outcome_error&&quit_code==QT_QUIT_TO_SYSTEM&&CE_LockstepExitCode(0,0)!=0,"natural end state mismatch fails");}
static void natural_scheduled_mismatch(){init(true);L.quit_at=0;L.hash_every=1;GAME.state=GAME_STATE_LEVEL_WON;CE_LockstepAfterUpdate();auto h=hash();in(3,h^1);CE_LockstepBeforeUpdate();check(L.outcome_error&&quit_code==QT_QUIT_TO_SYSTEM,"natural end does not discard a divergent final scheduled hash");}
static void natural_wrong_tick(){natural_begin(true);auto h=mission_hash();in(9,h,2);CE_LockstepBeforeUpdate();check(L.outcome_error,"peers ending the mission on different ticks refuse confirmation");}
static void natural_truncated(){natural_begin(true);mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();check(L.outcome_error&&CE_LockstepExitCode(0,0)!=0,"peer departure before natural final proof is incomplete");}
static void natural_late_error(){natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);in(8,h);in(11,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED,"natural host entered end screen");in(3,h^1,2);int may_update=CE_LockstepBeforeUpdate();check(L.outcome_error,"natural end does not silently discard later invalid messages");check(!may_update,"invalid late message stops the end-screen update");}
static void natural_host_delivery_ack(){
 natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();
 check(has(10)&&L.stage==STOPPING&&!L.final_confirmed&&!quit_code,"natural host waits for guest DONE acknowledgement");
 check(closes==0&&L.net==&mock_net,"host keeps transport active while awaiting DONE acknowledgement");
 in(11,h);CE_LockstepBeforeUpdate();check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"natural host completes after guest DONE acknowledgement");
}
static void natural_host_ack_missing(bool disconnect){
 natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();
 if(disconnect){mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;}else now_ms+=31000;
 CE_LockstepBeforeUpdate();check(L.outcome_error&&!L.final_confirmed&&CE_LockstepExitCode(0,1)!=0,disconnect?"natural host reports missing DONE acknowledgement on disconnect":"natural host reports missing DONE acknowledgement on timeout");
}
static void natural_host_ack_and_close(){
 natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();in(11,h);mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();
 check(L.stage==FINISHED&&L.final_confirmed&&!quit_code,"host preserves delivered acknowledgement with same-poll peer close");
}
static void invalid_delivery_ack(){
 natural_begin(true);auto h=mission_hash();in(9,h);in(6,h);CE_LockstepBeforeUpdate();in(11,h);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed,"host refuses DONE acknowledgement before sending DONE");
 natural_begin(true);h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();in(11,h^1);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed,"host refuses DONE acknowledgement with wrong hash");
 natural_begin(true);h=mission_hash();in(9,h);in(6,h);in(8,h);CE_LockstepBeforeUpdate();in(11,h,2);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed,"host refuses DONE acknowledgement with wrong tick");
 natural_begin(false);h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();in(11,h);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed,"guest refuses host-only DONE acknowledgement");
}
static void natural_guest_delivery_ack(){
 natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();in(10,h);CE_LockstepBeforeUpdate();
 check(has(11)&&L.stage==FINISHED&&L.final_confirmed,"natural guest acknowledges receiving DONE before completing");
}
static void natural_guest_unsendable_ack(){
 natural_begin(false);auto h=mission_hash();in(9,h);in(6,h);in(7,h);CE_LockstepBeforeUpdate();reject_send_type=11;in(10,h);CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed,"guest fails if DONE acknowledgement cannot be queued");
}
static void synthetic_host_delivery_ack(){
 init(true);reach();auto h=hash();peer_final(h);peer_ack(h);in(8,h);CE_LockstepBeforeUpdate();
 check(has(10)&&L.stage==STOPPING&&!L.final_confirmed&&!quit_code,"synthetic host waits for guest DONE acknowledgement");
 in(11,h);CE_LockstepBeforeUpdate();check(L.final_confirmed&&quit_code==QT_QUIT_TO_SYSTEM,"synthetic host completes after guest DONE acknowledgement");
}
static void synthetic_guest_no_done_close(){
 init(false);reach();auto h=hash();peer_final(h);peer_ack(h);in(7,h);CE_LockstepBeforeUpdate();mock_status.phase=CE_NET_CLOSED;mock_status.error=CE_NET_PEER_LEFT;CE_LockstepBeforeUpdate();
 check(L.outcome_error&&!L.final_confirmed&&CE_LockstepExitCode(0,1)!=0,"synthetic guest refuses peer close before DONE");
}
static void peer_ends_early(){init(true);L.quit_at=0;L.hash_every=2;GAME.state=GAME_STATE_PLAYING;in(9,42,1);CE_LockstepBeforeUpdate();CE_LockstepAfterUpdate();check(L.outcome_error,"peer mission end is refused when local state keeps playing at that tick");}
static void input(uint32_t tick,const CE_WorldInput &sample,uint32_t envelope=UINT32_MAX){
 uint8_t data[CE_NET_MAX_INPUT];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof data);CE_NetWriteU32(&w,0x43454c31u);CE_NetWriteU8(&w,2);CE_NetWriteU32(&w,tick);
 CE_NetWriteU8(&w,(uint8_t)sample.invert_ground);CE_NetWriteU8(&w,(uint8_t)sample.invert_jet);CE_NetWriteU8(&w,(uint8_t)sample.walker_mouse_any);CE_NetWriteU8(&w,(uint8_t)sample.mouse_ground_aim);
 CE_NetWriteF32(&w,sample.ground_yaw);CE_NetWriteF32(&w,sample.ground_pitch);CE_NetWriteU8(&w,(uint8_t)sample.count);
 for(unsigned i=0;i<sample.count;++i){CE_NetWriteU8(&w,(uint8_t)sample.actions[i].button);CE_NetWriteU8(&w,(uint8_t)sample.actions[i].source);CE_NetWriteF32(&w,sample.actions[i].value);}
 Packet p={2,tick,0};p.envelope=envelope;p.raw.assign(data,data+w.size);inbound.push_back(p);
}
static void input_controls(){
 CE_WorldInput sample={};sample.count=1;sample.actions[0]={25,CE_WORLD_ACTION_BINDING,-1.f};
 init(true);L.quit_at=0;L.tick=3;L.ran=false;input(3,sample);check(CE_LockstepBeforeUpdate()==1&&!L.outcome_error,"allowed input passes packet admission");CE_LockstepControllerFlush(1);check(applied_count==1&&applied_player==1&&last_applied.actions[0].button==25&&last_applied.actions[0].value==-1.f,"accepted remote action reaches the apply boundary unchanged");
 init(true);L.quit_at=0;sample.actions[0]={56,CE_WORLD_ACTION_BINDING,1.f};input(3,sample);check(!Receive()&&L.outcome_error,"remote pause action is refused");
 init(true);L.quit_at=0;sample.actions[0]={25,2,0.f};input(3,sample);check(!Receive()&&L.outcome_error,"remote action source is bounded");
 init(true);L.quit_at=0;sample.actions[0]={25,CE_WORLD_ACTION_BINDING,1.01f};input(3,sample);check(!Receive()&&L.outcome_error,"remote action magnitude is bounded");
 init(true);L.quit_at=0;sample.actions[0]={25,CE_WORLD_ACTION_BINDING,-1.f};sample.ground_yaw=.5f;input(3,sample);check(!Receive()&&L.outcome_error,"mouse angle without ground aim is refused");
 init(true);L.quit_at=0;sample.ground_yaw=0;sample.count=2;sample.actions[0]={27,CE_WORLD_ACTION_BINDING,1.f};sample.actions[1]={25,CE_WORLD_ACTION_BINDING,-1.f};input(3,sample);check(!Receive()&&L.outcome_error,"remote actions must retain native order");
 init(true);L.quit_at=0;sample.count=1;sample.actions[0]={25,CE_WORLD_ACTION_BINDING,-1.f};input(3,sample,4);check(!Receive()&&L.outcome_error,"input transport tick must equal payload tick");
 init(true);L.quit_at=0;L.sampled=false;fake_sample={};fake_sample.count=1;fake_sample.actions[0]={25,CE_WORLD_ACTION_BINDING,-1.f};CE_LockstepControllerFlush(0);check(!L.outcome_error&&has(2)&&L.mine[3].present&&L.mine[3].input.actions[0].value==-1.f,"valid local sample is queued and sent unchanged");
 init(true);L.quit_at=0;L.sampled=false;fake_sample={};fake_sample.count=1;fake_sample.actions[0]={56,CE_WORLD_ACTION_BINDING,1.f};CE_LockstepControllerFlush(0);check(L.outcome_error&&!has(2),"invalid local sample is not queued or sent");
 // CE's centered mouse and gamepad default can both contribute to one binding. The actual encoder and receiver must
 // preserve both actions, in order, for either peer; only the transport and final world application are stubbed here.
 for(bool host:{true,false}){
  init(host);L.quit_at=0;L.sampled=false;fake_sample={};fake_sample.count=2;
  fake_sample.actions[0]={25,CE_WORLD_ACTION_MOUSE_AXIS,0.f};fake_sample.actions[1]={25,CE_WORLD_ACTION_BINDING,-.75f};
  CE_LockstepControllerFlush(0);Packet wire{};bool found=false;
  for(const auto &p:sent)if(p.type==2){wire=p;found=true;break;}
  check(!L.outcome_error&&found&&L.mine[3].present&&L.mine[3].input.count==2,"local mouse and gamepad pair is queued and encoded");
  init(!host);L.quit_at=0;L.tick=3;if(found)inbound.push_back(wire);
  bool received=found&&Receive()&&!L.outcome_error;CE_LockstepControllerFlush(host?0:1);
  check(received&&applied_count==1&&applied_player==(host?0u:1u)&&last_applied.count==2&&
        last_applied.actions[0].button==25&&last_applied.actions[0].source==CE_WORLD_ACTION_MOUSE_AXIS&&last_applied.actions[0].value==0.f&&
        last_applied.actions[1].button==25&&last_applied.actions[1].source==CE_WORLD_ACTION_BINDING&&last_applied.actions[1].value==-.75f,
        "remote mouse and gamepad pair reaches the apply boundary in order");
 }
}
static void gate_hash_controls(){
 init(true);auto before=StateHash(0);fake_gate_value^=1;check(before!=StateHash(0),"selected player gate digest changes state hash");
 init(true);fake_gate_valid=false;CE_LockstepAfterUpdate();check(L.outcome_error&&quit_code&&!has(3),"invalid player gates stop before any hash is sent");
}
static void slot_hash_controls(){
 static_assert(sizeof(unsigned)==4 && sizeof(int)==4,"fixture requires 32-bit slot words");
 init(true);NativeGame native;bool native_matches=true,hashes_differ=true;
 for(int i=0;i<MAX_CAREER_SLOTS*8;++i){
  unsigned word=(unsigned)i>>5,bit=1u<<(i&31);native.mSlots[word]=(int)bit;GAME.slot_words[word]=bit;
  for(int j=0;j<MAX_CAREER_SLOTS*8;++j)if(native.GetSlot(j)!=(j==i) || GAME.GetSlot(j)!=(j==i))native_matches=false;
  auto changed=StateHash(0);GAME.slot_words[word]=0;
  if(changed==StateHash(0))hashes_differ=false;
  native.mSlots[word]=0;
 }
 check(native_matches,"native and mock getters address all 256 script-visible slot bits");
 check(hashes_differ,"all 256 script-visible career slots change selected hash");
 auto before=StateHash(0);GAME.slot_words[8]=1;check(before==StateHash(0),"non-script-visible storage words do not affect selected hash");GAME.slot_words[8]=0;
 LOG.calls=0;check(!native.GetSlot(-1)&&!native.GetSlot(256)&&LOG.calls==2,"native getter refuses out-of-range slots");
}
static bool all_converged_preruns_called=true;
static uint64_t converging_prerun(unsigned slot,bool flag){
 start_init(true,IDLE_MATCH);fake_prerun_word=slot>>5;GAME.slot_words[fake_prerun_word]=flag?(1u<<(slot&31)):0;fake_prerun_clear=true;fake_prerun_quit=true;inbound.clear();ready_packet();
 CE_LockstepPreparePlay();if(fake_prerun_calls!=1)all_converged_preruns_called=false;fake_prerun_clear=false;fake_prerun_quit=false;quit_code=0;
 return StateHash(0);
}
static void prerun_controls(){
 bool different=true;all_converged_preruns_called=true;
 for(unsigned slot=0;slot<MAX_CAREER_SLOTS*8;++slot){auto a=converging_prerun(slot,false);auto b=converging_prerun(slot,true);if(a==b)different=false;}
 check(different&&all_converged_preruns_called,"pre-run slot divergence survives converging current slots");
 init(true);L.pre_run_slots_captured=false;CE_LockstepAfterUpdate();
 check(L.outcome_error&&quit_code&&!has(3),"missing pre-run slot capture refuses state hash");
}
static void ready_packet(){
 uint8_t data[64];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof data);CE_NetWriteU32(&w,kMagic);CE_NetWriteU8(&w,MSG_READY);CE_NetWriteU32(&w,0);
 CE_NetWriteU32(&w,L.level);CE_NetWriteU32(&w,L.delay);CE_NetWriteU32(&w,L.hash_every);CE_NetWriteU32(&w,L.quit_at);
 uint8_t flags[256],digest[32];for(unsigned i=0;i<256;++i)flags[i]=(uint8_t)(GAME.GetSlot(i)!=0);
 if(!CE_NetPreRunFlagsDigest(flags,sizeof flags,digest)){std::abort();}CE_NetWriteBytes(&w,digest,sizeof digest);
 Packet p{};p.type=MSG_READY;p.tick=0;p.raw.assign(data,data+w.size);inbound.push_back(p);
 if(fake_idle_mode!=IDLE_PREHASH&&fake_idle_mode!=IDLE_PRERUN_CLOSE&&fake_idle_mode!=IDLE_PRERUN_TIMEOUT&&fake_idle_mode!=IDLE_PRERUN_CHANGE)pre_ack_packet();
}
static void pre_ack_packet(){uint8_t data[16];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof data);CE_NetWriteU32(&w,kMagic);CE_NetWriteU8(&w,MSG_PRERUN_ACK);CE_NetWriteU32(&w,0);Packet p{};p.type=MSG_PRERUN_ACK;p.tick=0;p.raw.assign(data,data+w.size);inbound.push_back(p);}
static void start_init(bool host,int idle_mode){
 init(host);L.stage=LOADED;L.hash_every=20;L.hashes_compared=0;L.quit_at=0;L.ran=false;L.net=nullptr;L.peer_ready=false;
 fake_idle_mode=idle_mode;fake_idle_count=0;if(idle_mode!=IDLE_DELAY_READY&&idle_mode!=IDLE_FAIL_UNREADY)ready_packet();
}
static void start_controls(){
 start_init(true,IDLE_MATCH);int result=CE_LockstepPreparePlay();
 check(result&&fake_idle_count>=2&&L.stage==RUNNING&&L.hashes[0].remote&&L.hashes_compared==1,"start waits for delayed matching hash");
 int allowed=result?CE_LockstepBeforeUpdate():0;
 check(result&&allowed==1&&L.hashes[0].remote&&L.hashes_compared==1,"first update follows compared initial hash");
 start_init(true,IDLE_MISMATCH);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown&&L.tick==0,"start refuses delayed mismatched hash");
 start_init(true,IDLE_TIMEOUT);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown&&L.tick==0,"missing start hash times out before gameplay");
 start_init(true,IDLE_CLOSE);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&CE_LockstepExitCode(0,0)!=0&&!L.shown&&L.tick==0,"peer close before start hash stops gameplay");
 start_init(false,IDLE_FAIL);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&fake_idle_count==2&&!L.shown,"guest transport failure after readiness stops promptly");
 start_init(false,IDLE_FAIL_UNREADY);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&fake_idle_count==2&&!L.shown,"guest failure after connection before readiness stops promptly");
 start_init(true,IDLE_DELAY_READY);result=CE_LockstepPreparePlay();
 check(result&&fake_idle_count>=4&&L.hashes_compared==1,"delayed readiness and delayed start hash complete once");
 check(fake_pump_count>0&&fake_reentrant_blocked,"message-pump reentry cannot advance a waiting game");
 start_init(false,IDLE_MATCH);fake_retry=true;mock_status.phase=CE_NET_FAILED;mock_status.error=CE_NET_TIMEOUT;result=CE_LockstepPreparePlay();
 check(result&&fake_created==2&&L.hashes[0].remote&&L.hashes_compared==1,"guest retry before readiness still starts");
 start_init(true,IDLE_CANCEL);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==STARTING&&!L.shown&&quit_code,"local cancellation does not start a waiting game");
 CE_LockstepBeginTeardown(quit_code);check(L.stage==ENDED&&closes==1&&!L.outcome_error,"local cancellation closes the waiting transport");
 start_init(true,IDLE_PREHASH);CapturePreRunSlots();SaveRandom();
 result=CE_LockstepPreparePlay();check(result&&L.stage==RUNNING&&L.hashes_compared==1,"peer hash before local capture is compared once");
 fake_idle_mode=IDLE_NONE;
}
static void start_ack_controls(){
 start_init(true,IDLE_ACK_DELAY);int result=CE_LockstepPreparePlay();
 check(result&&fake_idle_count>=6&&has(12)&&L.hashes_compared==1,"start waits for delayed peer comparison acknowledgement");
 check(result&&!fake_sent_ack_before_match,"local acknowledgement follows matching peer hash");
 bool bound=false;for(const auto &p:sent)if(p.type==12&&p.tick==0&&p.size==17&&p.value==L.hashes[0].mine)bound=true;
 check(result&&bound,"local start acknowledgement binds the compared hash");
 start_init(true,IDLE_ACK_TIMEOUT);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"lost start acknowledgement times out without gameplay");
 start_init(true,IDLE_ACK_CLOSE);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"disconnect before start acknowledgement fails");
 start_init(true,IDLE_ACK_MISMATCH);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"incorrect start acknowledgement hash is refused");
 start_init(true,IDLE_ACK_DUP);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"duplicate start acknowledgement is refused");
 start_init(true,IDLE_ACK_EARLY);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown&&fake_idle_count==2,"start acknowledgement before peer hash is refused");
 start_init(true,IDLE_ACK_BAD_TICK);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"start acknowledgement payload tick is checked");
 start_init(true,IDLE_ACK_BAD_ENVELOPE);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"start acknowledgement transport tick is checked");
 start_init(true,IDLE_ACK_SHORT);result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"truncated start acknowledgement is refused");
 init(true);L.stage=STARTING;L.hashes[0]={0,true,true,0,0};L.hashes_compared=1;
 {uint8_t data[16];CE_NetWriter w;CE_NetWriterInit(&w,data,sizeof data);CE_NetWriteU32(&w,kMagic);CE_NetWriteU8(&w,12);CE_NetWriteU32(&w,0);Packet p{};p.type=12;p.tick=0;p.raw.assign(data,data+w.size);inbound.push_back(p);}
 check(!Receive()&&L.outcome_error,"truncated acknowledgement fails even when hash is zero");
 start_init(true,IDLE_ACK_SAMEPOLL);result=CE_LockstepPreparePlay();
 check(result&&L.stage==RUNNING&&L.hashes_compared==1&&has(12),"peer hash and acknowledgement in one poll start once");
 start_init(true,IDLE_ACK_DUP_LATE);result=CE_LockstepPreparePlay();int may_update=result?CE_LockstepBeforeUpdate():0;
 check(result&&!may_update&&L.stage==FAILED&&L.outcome_error,"late duplicate acknowledgement stops before update");
 init(true);L.start_ack_received=false;L.hashes[0]={0,true,true,123,123};L.hashes_compared=1;Packet p{};p.type=12;p.tick=0;p.value=123;inbound.push_back(p);
 check(!Receive()&&L.outcome_error,"start acknowledgement outside start stage is refused");
 start_init(true,IDLE_MATCH);reject_send_type=12;result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"unsendable start acknowledgement fails before gameplay");
 start_init(true,IDLE_ACK_SAMEPOLL);reject_send_type=12;result=CE_LockstepPreparePlay();
 check(!result&&L.stage==FAILED&&L.outcome_error&&!L.shown,"unsendable acknowledgement fails after early peer ack");
 fake_idle_mode=IDLE_NONE;
}
static void prerun_admission_controls(){
 start_init(false,IDLE_FAIL_UNREADY);fake_prerun_mutate_career=true;fake_career_word=0x11u;
 int result=CE_LockstepPreparePlay();
 check(!result&&fake_prerun_calls==0&&fake_career_word==0x11u,"failed peer readiness cannot execute prerun or change local career");
 start_init(true,IDLE_MATCH);fake_mutate_rng=true;fake_calls=7;fake_hold=11;fake_rng_value=13;fake_rng_seed=17;
 result=CE_LockstepPreparePlay();
 check(result&&fake_prerun_calls==1&&fake_prerun_admitted,"matching flags and peer acknowledgement admit one prerun");
 check(result&&prerun_calls_seen==7&&prerun_hold_seen==11&&prerun_value_seen==13&&prerun_seed_seen==17,"message-pump random changes are restored before prerun");
 bool digest_only=false;for(const auto &p:sent)if(p.type==MSG_READY&&p.size==57){uint8_t flags[256]={},digest[32];CE_NetPreRunFlagsDigest(flags,sizeof flags,digest);digest_only=std::memcmp(p.raw.data()+25,digest,32)==0;}
 check(digest_only,"ready sends the 32-byte digest instead of raw slot flags");
 bool all_refused=true;
 for(unsigned slot=0;slot<256;++slot){
  start_init(true,IDLE_MATCH);inbound.clear();GAME.slot_words[slot>>5]^=1u<<(slot&31);ready_packet();GAME.slot_words[slot>>5]^=1u<<(slot&31);
  fake_prerun_mutate_career=true;fake_career_word=0x11u;result=CE_LockstepPreparePlay();
  if(result||!L.outcome_error||fake_prerun_calls||fake_career_word!=0x11u)all_refused=false;
 }
 check(all_refused,"each single-slot peer mismatch stops before prerun and modeled career mutation");
 start_init(true,IDLE_PRERUN_CLOSE);fake_prerun_mutate_career=true;fake_career_word=0x11u;result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls&&fake_career_word==0x11u,"disconnect before pre-run acknowledgement leaves prerun uncalled");
 start_init(true,IDLE_PRERUN_TIMEOUT);result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls,"missing pre-run acknowledgement times out before prerun");
 start_init(true,IDLE_PRERUN_CHANGE_EARLY);result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls&&!has(MSG_PRERUN_ACK),"local slot change before acknowledgement fails closed");
 start_init(true,IDLE_PRERUN_CHANGE);result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls&&has(MSG_PRERUN_ACK),"local slot change while awaiting acknowledgement fails closed");
 start_init(true,IDLE_MATCH);reject_send_type=MSG_PRERUN_ACK;result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls,"unsendable pre-run acknowledgement stops before prerun");
 start_init(true,IDLE_MATCH);inbound.front().raw.pop_back();result=CE_LockstepPreparePlay();
 check(!result&&L.outcome_error&&!fake_prerun_calls,"truncated ready digest stops before prerun");
 init(true);L.stage=STARTING;pre_ack_packet();check(!Receive()&&L.outcome_error,"pre-run acknowledgement before ready is refused");
 init(true);L.stage=STARTING;L.peer_ready=true;pre_ack_packet();pre_ack_packet();check(!Receive()&&L.outcome_error,"duplicate pre-run acknowledgement is refused");
 init(true);L.stage=STARTING;L.peer_ready=true;pre_ack_packet();inbound.front().raw[8]=1;check(!Receive()&&L.outcome_error,"pre-run acknowledgement payload tick is checked");
 init(true);L.stage=STARTING;L.peer_ready=true;pre_ack_packet();inbound.front().envelope=1;check(!Receive()&&L.outcome_error,"pre-run acknowledgement transport tick is checked");
 init(true);L.stage=STARTING;L.peer_ready=true;pre_ack_packet();inbound.front().raw.push_back(0);check(!Receive()&&L.outcome_error,"pre-run acknowledgement with extra payload is refused");
 init(true);L.stage=STARTING;L.peer_ready=true;L.prerun_finished=true;pre_ack_packet();check(!Receive()&&L.outcome_error,"late pre-run acknowledgement is refused");
 init(true);L.stage=STARTING;in(MSG_HASH,0,0);check(!Receive()&&L.outcome_error,"start hash before pre-run acknowledgement is refused");
 init(true);L.stage=LOADED;bool loaded=CE_LockstepBeforeUpdate()==0;L.stage=STARTING;bool waiting=CE_LockstepBeforeUpdate()==0;L.stage=PRERUN;bool allowed=CE_LockstepBeforeUpdate()==1;
 check(loaded&&waiting&&allowed,"only admitted prerun permits a simulation update before running");
 fake_idle_mode=IDLE_NONE;
}

void failure_teardown_controls(){
 fake_boot_lockstep=true; init(true); last_error[0]=0;
 check(!Fail("synthetic local failure"),"local failure terminates the session");
 check(L.stage==FAILED&&L.outcome_error&&!L.net&&quit_code==QT_QUIT_TO_FRONTEND,"local failure keeps its error outcome");
 check(closes==0&&destroys==1,"local failure aborts without an authenticated voluntary leave");
 check(!strcmp(CE_LockstepError(),"synthetic local failure"),"local failure keeps its notice");
 init(true); last_error[0]=0; mock_status.phase=CE_NET_FAILED; mock_status.error=CE_NET_TIMEOUT;
 check(!Poll()&&L.outcome_error&&quit_code==QT_QUIT_TO_FRONTEND,"remote transport failure exits the session");
 check(!strcmp(CE_LockstepError(),"the other player stopped responding"),"remote transport failure keeps the failure notice");
 check(graceful_leaves==0&&destroys==1,"remote transport failure does not send a voluntary leave");
 init(true); L.quit_at=0; last_error[0]=0; mock_status.phase=CE_NET_CLOSED; mock_status.error=CE_NET_PEER_LEFT;
 check(!Poll()&&L.outcome_error,"remote voluntary leave exits the session");
 check(!strcmp(CE_LockstepError(),"the other player left the co-op game"),"remote voluntary leave keeps its distinct notice");
 init(true); L.quit_at=0; CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND);
 check(closes==1&&!L.outcome_error,"a voluntary local leave still closes gracefully");
 fake_boot_lockstep=false; init(true); last_error[0]=0; reach(); auto h=hash(); in(MSG_HASH,h^1); CE_LockstepBeforeUpdate();
 check(desync_attempts==1&&has(MSG_DESYNC)&&close_saw_desync&&closes==1&&destroys==1,"queued state divergence is flushed before graceful close");
 last_error[0]=0; natural_divergent();
 check(has(MSG_DESYNC)&&close_saw_desync&&closes==1&&destroys==1,"queued mission divergence is flushed before graceful close");
 init(true); last_error[0]=0; reach(); h=hash(); in(MSG_HASH,h^1); reject_send_type=MSG_DESYNC; CE_LockstepBeforeUpdate();
 check(desync_attempts==1,"state divergence attempts a desync notice");
 check(L.outcome_error&&!has(MSG_DESYNC)&&closes==0&&destroys==1,"unqueued divergence aborts without a voluntary leave");
 last_error[0]=0; natural_begin(true); h=mission_hash(); reject_send_type=MSG_DESYNC; in(MSG_MISSION_FINAL,h^1); CE_LockstepBeforeUpdate();
 check(desync_attempts==1,"mission divergence attempts a desync notice");
 check(L.outcome_error&&!has(MSG_DESYNC)&&closes==0&&destroys==1,"unqueued mission divergence aborts without a voluntary leave");
 fake_boot_lockstep=true; init(true); L.quit_at=0; last_error[0]=0;
 uint8_t bytes[CE_NET_MAX_INPUT]; uint32_t length; Encode(bytes,length,MSG_DESYNC,L.tick);
 CE_NetWriter writer; CE_NetWriterInit(&writer,bytes,sizeof bytes); writer.size=length; CE_NetWriteU64(&writer,1); CE_NetWriteU64(&writer,2);
 Packet pending{}; pending.type=MSG_DESYNC; pending.tick=L.tick; pending.raw.assign(bytes,bytes+writer.size); inbound.push_back(pending);
 mock_status.phase=CE_NET_CLOSED; mock_status.error=CE_NET_PEER_LEFT;
 check(!Poll()&&L.outcome_error&&strstr(CE_LockstepError(),"went out of step")!=nullptr,"queued remote divergence wins over later leave");
 fake_boot_lockstep=false;
}
static void campaign_configure_fixture(bool host) {
    init(host);
    last_error[0] = 0;
    L.stage = LOADED;
    L.connection_established = false;
    L.quit_at = 0;
    fake_campaign_launch = {1, 17, 29, L.level};
    fake_campaign_host = host ? 1 : 0;
    fake_campaign_admitted = true;
    CeNetworkCampaign previous = {};
    previous.identity = {41, 53, L.level};
    previous.phase = CE_CAMPAIGN_AGREED_WIN;
    previous.host_outcome = previous.guest_outcome = CE_CAMPAIGN_OUTCOME_WIN;
    check(ce_campaign_return_capture(&previous, true, false),
          "configure fixture starts with a previous agreed return");
}

static void campaign_configure_controls() {
    for (bool host : {false, true}) {
        campaign_configure_fixture(host);
        check(CampaignConfigure() && L.campaign && L.stage == LOADED &&
              L.campaign_identity.session == fake_campaign_launch.session &&
              L.campaign_identity.attempt == fake_campaign_launch.attempt &&
              L.campaign_identity.mission == fake_campaign_launch.mission,
              "matching admitted launch configures the exact loaded campaign identity");
        check(L.campaign_state.phase == CE_CAMPAIGN_IDLE && !CampaignReady() &&
              !CE_LockstepCampaignActive() && !CE_LockstepCampaignAgreedWin() &&
              sent.empty() && fake_campaign_clears == 0,
              "launch configuration alone neither begins gameplay nor agrees progress");
        CeCampaignReturn returned = {};
        check(!ce_campaign_return_get(&returned),
              "accepted campaign launch clears the previous mission return");

        const char *refusals[] = {
            "unknown campaign launch version refuses before gameplay",
            "campaign launch for another loaded mission refuses before gameplay",
            "campaign launch for the other player role refuses before gameplay",
            "campaign launch without a session refuses before gameplay",
            "campaign launch without an attempt refuses before gameplay",
        };
        for (unsigned invalid = 0; invalid != sizeof refusals / sizeof refusals[0]; ++invalid) {
            campaign_configure_fixture(host);
            switch (invalid) {
            case 0: fake_campaign_launch.version = 2; break;
            case 1: ++fake_campaign_launch.mission; break;
            case 2: fake_campaign_host = host ? 0 : 1; break;
            case 3: fake_campaign_launch.session = 0; break;
            case 4: fake_campaign_launch.attempt = 0; break;
            }
            check(!CampaignConfigure() && L.stage == FAILED && L.outcome_error &&
                  !L.campaign && strstr(CE_LockstepError(),
                      "campaign launch does not match the loaded session") != nullptr,
                  refusals[invalid]);
            check(!fake_campaign_admitted && fake_campaign_clears == 1,
                  "refused campaign launch retires its boot admission");
            check(!ce_campaign_return_get(&returned),
                  "refused campaign launch cannot keep the previous mission return");
            const uint32_t stopped_tick = L.tick;
            check(!CE_LockstepBeforeUpdate() && L.stage == FAILED && L.tick == stopped_tick &&
                  !CE_LockstepCampaignActive() && !CE_LockstepCampaignAgreedWin(),
                  "refused campaign launch cannot advance a later simulation tick");
        }

        campaign_configure_fixture(host);
        fake_campaign_admitted = false;
        check(CampaignConfigure() && !L.campaign && L.stage == LOADED &&
              !L.outcome_error && !ce_campaign_return_get(&returned) && sent.empty(),
              "ordinary launch stays outside campaign and clears the prior return");
        CE_BootCampaignClear();
    }
    last_error[0] = 0;
}

static void campaign_begin(bool is_host, CeCampaignOutcome outcome) {
    init(is_host);
    L.quit_at = 0;
    L.hash_every = 1;
    L.campaign = true;
    L.connection_established = true;
    L.campaign_identity = {17, 29, L.level};
    ce_network_campaign_init(&L.campaign_state);
    ce_result_exchange_init(&L.campaign_result);
    check(ce_network_campaign_begin(&L.campaign_state, L.campaign_identity) == CE_CAMPAIGN_ACCEPTED,
          "campaign fixture begins");
    if (outcome != CE_CAMPAIGN_OUTCOME_UNKNOWN) CE_LockstepCampaignOutcome(outcome);
    GAME.state = outcome == CE_CAMPAIGN_OUTCOME_LOSS ? GAME_STATE_LEVEL_LOST : GAME_STATE_LEVEL_WON;
    CE_LockstepAfterUpdate();
}

static bool campaign_in(const CeResultMessage &message) {
    uint8_t buffer[192];
    uint32_t size = 0;
    Encode(buffer, size, MSG_CAMPAIGN_RESULT, uint32_t(message.terminal_tick));
    size_t written = 0;
    check(ce_result_message_encode(&message, buffer + size, sizeof(buffer) - size, &written) != 0,
          "peer result encodes");
    Packet packet = {};
    packet.type = MSG_CAMPAIGN_RESULT;
    packet.tick = uint32_t(message.terminal_tick);
    packet.envelope = packet.tick;
    packet.raw.assign(buffer, buffer + size + written);
    packet.size = uint32_t(packet.raw.size());
    inbound.push_back(packet);
    return Receive();
}

static void campaign_exchange(bool is_host, CeCampaignOutcome outcome) {
    campaign_begin(is_host, outcome);
    check(L.final_local && L.campaign_result_started && L.stage == STOPPING,
          "observed terminal starts result protocol");
    FinalComplete();
    check(!L.final_confirmed && !CE_LockstepCampaignAgreedWin(), "unagreed result cannot complete");
    in(MSG_HASH, L.final_hash, L.final_tick);
    check(Receive() && L.hashes_compared, "scheduled terminal hash is agreed before completion");
    in(MSG_MISSION_FINAL, L.final_hash, L.final_tick);
    check(Receive(), "peer terminal accepted");
    check(!has(MSG_FINAL_ACK), "old terminal acknowledgement waits for result agreement");
    CeResultExchange peer;
    ce_result_exchange_init(&peer);
    check(ce_result_exchange_begin(&peer, L.campaign_identity,
        is_host ? CE_CAMPAIGN_GUEST : CE_CAMPAIGN_HOST, outcome, L.final_tick, L.final_hash) == CE_RESULT_ACCEPTED,
        "peer observes the same terminal result");
    size_t next = 0;
    unsigned last_peer_kind = 0;
    for (unsigned round = 0; round != 8; ++round) {
        while (next != sent.size()) {
            Packet packet = sent[next++];
            if (packet.type != MSG_CAMPAIGN_RESULT) continue;
            CE_NetReader reader;
            CE_NetReaderInit(&reader, packet.raw.data(), uint32_t(packet.raw.size()));
            uint8_t type = 0;
            uint32_t tick = 0;
            CE_NetReadU32(&reader);
            type = CE_NetReadU8(&reader);
            tick = CE_NetReadU32(&reader);
            CeResultMessage message;
            bool decoded = type == MSG_CAMPAIGN_RESULT && tick == L.final_tick &&
                ce_result_message_decode(&message, reader.data + reader.position, reader.size - reader.position);
            check(decoded, "actual emitted result decodes at peer");
            if (!decoded) return;
            CeResultAction action = ce_result_exchange_receive(&peer, &message);
            check(action == CE_RESULT_ACCEPTED || action == CE_RESULT_DUPLICATE, "peer accepts emitted result");
        }
        CeResultMessage message;
        if (ce_result_exchange_pending(&peer, &message) && message.kind != last_peer_kind) {
            last_peer_kind = message.kind;
            check(campaign_in(message), "runtime receives authenticated peer result");
        }
    }
    check(CampaignReady() && has(MSG_FINAL_ACK), "matching result releases normal final barrier");
    check(!CE_LockstepCampaignAgreedWin(), "result agreement alone does not complete mission");
    CeCampaignReturn returned = {};
    CampaignCaptureReturn();
    check(!ce_campaign_return_get(&returned), "result agreement without terminal confirmation is not returned");

    in(MSG_FINAL_ACK, L.final_hash, L.final_tick);
    check(Receive(), "ordinary final acknowledgement retained");
    if (is_host) {
        in(MSG_FINAL_RECEIPT, L.final_hash, L.final_tick);
        check(Receive(), "ordinary final receipt retained");
        in(MSG_FINAL_DONE_ACK, L.final_hash, L.final_tick);
        check(Receive(), "ordinary final delivery acknowledgement retained");
    } else {
        in(MSG_FINAL_RELEASE, L.final_hash, L.final_tick);
        check(Receive(), "ordinary final release retained");
        in(MSG_FINAL_DONE, L.final_hash, L.final_tick);
        check(Receive(), "ordinary final completion retained");
        mock_status.phase = CE_NET_CLOSED;
        Poll();
    }
    check(L.final_confirmed, "campaign completes existing terminal handshake");
    check(bool(CE_LockstepCampaignAgreedWin()) == (outcome == CE_CAMPAIGN_OUTCOME_WIN),
          "only agreed observed win permits campaign progression");

    const CeCampaignIdentity confirmed_identity = L.campaign_identity;
    L.outcome_error = true;
    CampaignCaptureReturn();
    check(!ce_campaign_return_get(&returned), "runtime error suppresses confirmed campaign return");
    L.outcome_error = false;
    CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND);
    check(ce_campaign_return_get(&returned) &&
        returned.identity.session == confirmed_identity.session &&
        returned.identity.attempt == confirmed_identity.attempt &&
        returned.identity.mission == confirmed_identity.mission && returned.outcome == outcome,
        "confirmed campaign teardown retains exact identity and result");
    CampaignStop(CE_CAMPAIGN_STOP_CANCEL);
    CE_LockstepBeginTeardown(QT_QUIT_TO_FRONTEND);
    check(ce_campaign_return_get(&returned) && returned.outcome == outcome,
        "duplicate teardown preserves latched campaign result");
    CE_LockstepAfterWorldShutdown();
    check(!CE_LockstepCampaignActive() && !CE_LockstepCampaignAgreedWin(),
          "world teardown clears campaign ownership");

    CE_LockstepAfterWorldShutdown();
    check(ce_campaign_return_get(&returned) && returned.outcome == outcome,
        "world shutdown preserves latched campaign return");
    check(CampaignConfigure() && !ce_campaign_return_get(&returned),
        "new campaign configuration clears previous return");
}

static void campaign_controls() {
    campaign_exchange(true, CE_CAMPAIGN_OUTCOME_WIN);
    campaign_exchange(false, CE_CAMPAIGN_OUTCOME_WIN);
    campaign_exchange(true, CE_CAMPAIGN_OUTCOME_LOSS);
    campaign_exchange(false, CE_CAMPAIGN_OUTCOME_LOSS);
    campaign_begin(true, CE_CAMPAIGN_OUTCOME_UNKNOWN);
    check(L.stage == FAILED && !CE_LockstepCampaignAgreedWin(),
          "terminal flag never fabricates a win");
    campaign_begin(true, CE_CAMPAIGN_OUTCOME_WIN);
    CeResultMessage wrong = {};
    wrong.identity = L.campaign_identity;
    wrong.outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    wrong.terminal_tick = L.final_tick;
    wrong.terminal_fingerprint = L.final_hash;
    wrong.kind = CE_RESULT_ACCEPT;
    wrong.sender = CE_CAMPAIGN_GUEST;
    check(!campaign_in(wrong) && L.stage == FAILED && !CE_LockstepCampaignAgreedWin(),
          "conflicting peer outcome closes campaign");
    campaign_begin(true, CE_CAMPAIGN_OUTCOME_WIN);
    wrong.identity = L.campaign_identity;
    ++wrong.identity.attempt;
    wrong.outcome = CE_CAMPAIGN_OUTCOME_WIN;
    wrong.terminal_tick = L.final_tick;
    wrong.terminal_fingerprint = L.final_hash;
    check(!campaign_in(wrong) && !CE_LockstepCampaignAgreedWin(),
          "stale result identity closes campaign");
    campaign_begin(true, CE_CAMPAIGN_OUTCOME_WIN);
    CE_LockstepBeginTeardown(0);
    check(!CE_LockstepCampaignAgreedWin() && L.campaign_state.phase == CE_CAMPAIGN_CANCELLED,
          "cancelled attempt cannot progress");
    init(true);
    CE_LockstepCampaignOutcome(CE_CAMPAIGN_OUTCOME_WIN);
    check(!CE_LockstepCampaignActive() && !CE_LockstepCampaignAgreedWin(),
          "ordinary gameplay remains outside campaign state");
}

static void content_failure_peer_close_controls() {
    for(bool host:{false,true}) for(Stage stage:{RUNNING,STOPPING}) {
        init(host); fake_boot_lockstep=1; L.quit_at=0; L.stage=stage;
        L.final_mission=stage==STOPPING; L.final_local=L.final_mission;
        // This peer has no local content fault: exercise only the remote close path.
        L.admission_required=false;
        mock_status={CE_NET_CLOSED,CE_NET_PEER_LEFT,host?CE_NET_HOST:CE_NET_GUEST};
        const auto stopped_tick=L.tick;
        check(!CE_LockstepBeforeUpdate()&&L.stage==FAILED&&L.outcome_error&&L.tick==stopped_tick,
              "an uncompleted peer stops before another tick when the content-fault peer closes");
        const auto stopped_closes=closes;
        check(!CE_LockstepBeforeUpdate()&&L.stage==FAILED&&L.outcome_error&&L.tick==stopped_tick&&closes==stopped_closes,
              "a content-fault peer close cannot resurrect or close twice");
    }
}

#ifndef CE_LOCKSTEP_REAL_ADMISSION
int main(){
    content_failure_peer_close_controls();
    setvbuf(stdout, NULL, _IONBF, 0);
    campaign_configure_controls();
    campaign_controls();failure_teardown_controls();shipped_leaves_for_the_front_end();prerun_admission_controls();start_ack_controls();start_controls();prerun_controls();slot_hash_controls();gate_hash_controls();host();guest();mismatch();truncated();early();premature_ack();timeout_case();remote_first();wrong_tick();duplicate();bad_receipt();unaligned();hash_window();early_local_teardown();peer_final_local_teardown();natural_aligned();natural_deferred();natural_peer_first();natural_aligned_exchange(true);natural_aligned_exchange(false);natural_outcome_mismatch();native_quit_error();natural_host();natural_guest();natural_guest_done_lost();natural_guest_done_timeout();natural_guest_done_and_close();natural_completed_near_timeout();natural_divergent();natural_scheduled_mismatch();natural_wrong_tick();natural_truncated();natural_late_error();natural_host_delivery_ack();natural_host_ack_missing(true);natural_host_ack_missing(false);natural_host_ack_and_close();invalid_delivery_ack();natural_guest_delivery_ack();natural_guest_unsendable_ack();synthetic_host_delivery_ack();synthetic_guest_no_done_close();peer_ends_early();input_controls();return failures?1:0;}

#endif
extern "C" int CE_BootCampaignGet(CE_CampaignLaunch *out, int *host) {
    if (out) *out = {};
    if (host) *host = 0;
    if (!fake_campaign_admitted) return 0;
    if (out) *out = fake_campaign_launch;
    if (host) *host = fake_campaign_host;
    return 1;
}
extern "C" void CE_BootCampaignClear(void) {
    fake_campaign_launch = {};
    fake_campaign_host = 0;
    fake_campaign_admitted = false;
    ++fake_campaign_clears;
}
