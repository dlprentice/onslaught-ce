// SPDX-License-Identifier: GPL-3.0-only
// The production orchestrator with controlled transport/world/platform boundaries and the actual PreRun caller body.
// No engine, actor, retail data, SDL device or real network is fabricated or opened by these boundary tests.
#include <deque>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state_delivery.h"
#include "ce_network_boot.h"
#include "ce_network_loaded_admission.h"
#include SESSION_SOURCE
struct CE_WorldFrame { CE_WorldInfo info; uint32_t birth; uint32_t bytes=28, seed=0; };
#ifdef SESSION_LIFECYCLE_TEST
static void LifecycleFrameInfo(CE_WorldFrame *);
static void LifecycleCaptureInfo(CE_WorldFrame *);
#endif
struct Packet { CE_NetMessage meta; std::vector<uint8_t> bytes; };
struct CE_NetSession {
    CE_NetStatus status; std::deque<Packet> incoming; uint8_t context[32]={};
    CE_StateSender *remote_sender=nullptr; CE_StateReceiver *remote_receiver=nullptr;
    CE_StateDeliveryTicket remote_ticket={}; CE_StateDeliveryControl remote_announcement={};
    CE_StateRepairGate remote_gate={}; uint32_t remote_tick=0, remote_life=0, remote_base_seq=1;
    bool remote_baseline=false, remote_wait=false, remote_prepared=false, remote_ready_sent=false;
};
static uint64_t now_ms = 100;
static unsigned checks, created, destroyed, begun, ended, captures, applied, sampled, pre_runs, updates, state_sends;
static uint64_t state_bytes;
static unsigned input_attempts, input_sent, queued, queue_attempts, pumps, rebased;
static unsigned input_acks, ack_attempts, network_polls, sampler_rebases;
static unsigned publication_attempts, publications, accepted_world_sends;
static unsigned birth_getters, birth_ack_attempts, birth_send_backpressure, sample_peeks;
static bool birth_send_invalid, birth_getter_unavailable, birth_bad_epoch, birth_zero_tick, reject_birth_ack, suppress_samples, world_committed, inject_birth_baseline;
static uint32_t committed_birth, baseline_birth, leased_tick, leased_birth;
static std::vector<CE_WorldBirthAck> birth_attempts, birth_sends, delegated_birth_acks;
static std::vector<unsigned> control_order;
struct AvailabilityTuple { unsigned kind; uint32_t generation, tick; };
static std::vector<AvailabilityTuple> availability_attempts, availability_sends;
static unsigned availability_backpressure;
static bool availability_send_invalid;
static std::string handoff_case;
static Stage handoff_block_stage = OFF;
static bool handoff_block_start_queued, handoff_block_start_applied;
static unsigned terminal_send_attempts;
static unsigned automatic_execs;
static void HandoffQuit();
#ifdef SESSION_LIFECYCLE_TEST
static int LifecycleSend(uint32_t kind,uint32_t tick,const uint8_t *data,uint32_t size);
#endif
static std::vector<unsigned> birth_sample_peeks;
static bool reject_publication, world_send_backpressure;
static uint32_t window_reasons, input_suspended;
static bool auto_wait_ack = true;
static unsigned control_backpressure;
static std::vector<uint32_t> queued_sequences;
static uint32_t leased_sequence;
static bool world_live, world_replica, ticket, virtual_clock, mute_peer, close_window, corrupt_control, early_input;
static bool incompatible; // the transport refused the other build: the guest fails, the host keeps sending its refusal
static bool erase_loaded1, baseline_first;
static bool reject_input_ack;
static bool poison_apply, poisoned;
static unsigned rendered;
static bool flood_messages;
static bool refuse_net_allocation;
static unsigned frame_allocations, refuse_frame_allocation;
static unsigned dispatched;
static unsigned send_backpressure, queue_backpressure;
static uint32_t world_epoch, host_tick, frame_sequence, applied_tick, local_sequence;
static uint32_t capture_bytes=28,capture_seed=0;
static std::vector<uint8_t> attempted_input;
static CE_NetSession *peer;
static void Check(bool condition, const char *what) {
    ++checks; if (!condition) { fprintf(stderr, "FAIL %s (%s)\n", what, CE_SessionError()); exit(2); }
}
static void PacketIn(uint32_t kind, uint32_t tick, const uint8_t *data, uint32_t size) {
    Packet packet; packet.meta = {kind, peer->status.epoch, tick, size}; packet.bytes.assign(data,data+size); peer->incoming.push_back(packet);
}
static void ControlIn(unsigned kind,uint32_t sequence=0,uint32_t tick=0,uint32_t schema=kControlSchema,int extra=0) {
    uint8_t data[25]; CE_NetWriter w; CE_NetWriterInit(&w,data,sizeof data);
    CE_NetWriteU32(&w,kControlMagic); CE_NetWriteU32(&w,schema); CE_NetWriteU32(&w,kind);
    CE_NetWriteU32(&w,corrupt_control?100:852); CE_NetWriteU32(&w,sequence); CE_NetWriteU32(&w,tick);
    if(extra>0)CE_NetWriteU8(&w,0);PacketIn(CE_NET_CONTROL,tick,data,w.size-(extra<0?1:0));
}
static void FrameIn(uint32_t kind,uint32_t tick,uint32_t sequence,uint32_t birth=0);
#include "network_session_delivery_boundary.inc"
extern "C" uint64_t CE_NetNowMs() { return now_ms; }
extern "C" CE_NetSession *CE_NetCreate(const CE_NetConfig *c) {
    if(refuse_net_allocation)return NULL;
    ++created; peer=new CE_NetSession;memcpy(peer->context,c->compatibility,32); peer->status={CE_NET_CONNECTING,0,c->role,c->role==CE_NET_HOST?0u:1u,0,c->port,0,0,0}; return peer;
}
extern "C" void CE_NetDestroy(CE_NetSession *p) { if(p) { if(s.stage==FAILED)Check(!s.net,"failed session detaches transport before destruction");CE_StateSenderDestroy(p->remote_sender);CE_StateReceiverDestroy(p->remote_receiver); ++destroyed; delete p; peer=NULL; } }
extern "C" void CE_NetGetStatus(const CE_NetSession *p,CE_NetStatus *out) { *out=p->status; }
extern "C" int CE_NetPeerVersion(const CE_NetSession *,char *out,uint32_t cap) { if(!incompatible)return 0;snprintf(out,cap,"%s","0.1.9");return 1; }
static uint32_t quiet_ms;
extern "C" uint32_t CE_NetQuietMs(const CE_NetSession *,uint64_t) { return quiet_ms; }
extern "C" void CE_NetPoll(CE_NetSession *p,uint64_t,unsigned) {
    ++network_polls;
    if(mute_peer)return;
    if(incompatible&&p->status.phase==CE_NET_CONNECTING) { p->status.phase=p->status.role==CE_NET_GUEST?CE_NET_FAILED:CE_NET_AUTHENTICATING; p->status.error=CE_NET_INCOMPATIBLE; }
    if(incompatible)return;
    if(p->status.phase==CE_NET_CONNECTING) { p->status.phase=CE_NET_ACTIVE; p->status.epoch=1; }
    if(p->status.phase==CE_NET_SYNCING) p->status.phase=CE_NET_ACTIVE;
    RemotePump();
    if(erase_loaded1&&p->status.role==CE_NET_GUEST&&s.stage==LOADED&&p->status.epoch==1&&!p->incoming.empty()) {
        p->incoming.clear();p->status.epoch=2;erase_loaded1=false;
    }
}
extern "C" int CE_NetBeginEpoch(CE_NetSession *p,uint32_t e) { p->status.epoch=e; p->status.phase=CE_NET_SYNCING; p->incoming.clear(); return 1; }
extern "C" void CE_NetClose(CE_NetSession *p) { if(p) p->status.phase=CE_NET_CLOSED; }
extern "C" int CE_NetReceive(CE_NetSession *p,CE_NetMessage *meta,uint8_t *out,uint32_t cap) {
    if(p->incoming.empty())return 0; auto packet=p->incoming.front(); if(cap<packet.bytes.size())return -1;
    p->incoming.pop_front();*meta=packet.meta;memcpy(out,packet.bytes.data(),packet.bytes.size());
    if(!handoff_case.empty()&&meta->kind==CE_NET_CONTROL&&packet.bytes.size()==24) {
        CE_NetReader r;CE_NetReaderInit(&r,out,24);CE_NetReadU32(&r);CE_NetReadU32(&r);
        if(CE_NetReadU32(&r)==GUEST_BLOCK) {
            handoff_block_stage=s.stage;handoff_block_start_queued=s.start_queued;handoff_block_start_applied=s.start_applied;
            if(handoff_case=="quit")HandoffQuit();
        }
    }
    return 1;
}
extern "C" int CE_NetSend(CE_NetSession *p,uint32_t kind,uint32_t tick,const uint8_t *data,uint32_t size) {
    if(p->status.phase==CE_NET_CLOSED)++terminal_send_attempts;
    if(p->status.phase!=CE_NET_ACTIVE)return 0;
    if(kind==CE_NET_STATE_CONTROL)return RemoteControlSend(tick,data,size);
#ifdef SESSION_LIFECYCLE_TEST
    if(kind==CE_NET_LIFECYCLE)return LifecycleSend(kind,tick,data,size);
#endif
    if(kind==CE_NET_CONTROL) {
        CE_NetReader r;CE_NetReaderInit(&r,data,size);uint32_t magic=CE_NetReadU32(&r),schema=CE_NetReadU32(&r);
        unsigned control=CE_NetReadU32(&r);uint32_t level=CE_NetReadU32(&r),sequence=CE_NetReadU32(&r),payload_tick=CE_NetReadU32(&r);
        uint32_t baseline=control==LIFECYCLE_APPLIED?CE_NetReadU32(&r):0;
        Check(size==(control==LIFECYCLE_APPLIED?28u:24u)&&CE_NetReadDone(&r)&&magic==kControlMagic&&schema==5&&level==852&&payload_tick==tick,"control uses exact schema5 bounded payload and envelope");
        (void)baseline;
#ifdef SESSION_LIFECYCLE_TEST
        if(control==LIFECYCLE_APPLIED)return LifecycleSend(kind,tick,data,size);
#endif
        control_order.push_back(control);
        if(!handoff_case.empty()&&(control==HOST_WAIT||(control>=GUEST_BLOCK&&control<=GUEST_READY_ACK)))
            Check(s.stage==RUNNING,"handoff metadata never drives controls before RUNNING");
        if(control==START&&handoff_case=="unqueued") {
            ControlIn(START_APPLIED,sequence,tick);ControlIn(GUEST_BLOCK,1,tick);return 0;
        }
        if(control>=GUEST_BLOCK&&control<=GUEST_READY_ACK) {
            availability_attempts.push_back({control,sequence,tick});
            if(availability_send_invalid)return -1;
            if(availability_backpressure){--availability_backpressure;return 0;}
        }
        if(control==BIRTHS_APPLIED) {
            ++birth_ack_attempts;birth_attempts.push_back({p->status.epoch,sequence,tick});
            birth_sample_peeks.push_back(sample_peeks);
            Check(applied&&!poisoned&&sequence&&sequence<=committed_birth&&tick<=applied_tick,"birth proof follows a whole successful Apply");
            if(birth_send_invalid)return -1;
            if(birth_send_backpressure){--birth_send_backpressure;return 0;}
            birth_sends.push_back({p->status.epoch,sequence,tick});return 1;
        }
        if(control_backpressure) { --control_backpressure; return 0; }
        if(control>=GUEST_BLOCK&&control<=GUEST_READY_ACK)availability_sends.push_back({control,sequence,tick});
        if(control==PEER_LOADED) {
            if(baseline_first&&p->status.epoch==2&&p->status.role==CE_NET_GUEST)FrameIn(CE_NET_BASELINE,60,1,baseline_birth);
            ControlIn(PEER_LOADED);
            if(p->status.epoch==1&&p->status.role==CE_NET_GUEST) { /* emulate the authenticated host epoch transition */ }
            if(!baseline_first&&p->status.epoch==2&&p->status.role==CE_NET_GUEST) FrameIn(CE_NET_BASELINE,60,1,baseline_birth); // same receive pass as loaded2
        }
        if(control==HOST_WAIT && auto_wait_ack) ControlIn(HOST_WAIT_ACK,sequence,tick);
        if(control==HOST_RESUME && auto_wait_ack) ControlIn(HOST_RESUME_ACK,sequence,tick);
        if(control==HOST_WAIT&&auto_wait_ack){p->remote_wait=true;p->remote_tick=tick;p->remote_gate={sequence,p->remote_base_seq,p->remote_life};}
        if(control==HOST_RESUME&&auto_wait_ack)p->remote_wait=false;
        if(control==BASELINE_APPLIED){p->remote_baseline=true;RemoteStart();}
        if(control==START) {
            if(early_input) { CE_WorldInput input={}; input.epoch=2;input.tick=61;input.player=1;input.sequence=1;
                uint8_t bytes[1000];uint32_t n;CE_WorldEncodeInput(&input,bytes,sizeof bytes,&n);PacketIn(CE_NET_INPUT,61,bytes,n); }
            if(handoff_case=="before"||handoff_case=="terminal-before")ControlIn(GUEST_BLOCK,1,tick);
            ControlIn(START_APPLIED,sequence,tick);
            if(handoff_case=="leaving-first")ControlIn(LEAVING);
            if(!handoff_case.empty()&&handoff_case!="control"&&handoff_case!="before"&&handoff_case!="terminal-before") {
                ControlIn(GUEST_BLOCK,1,tick);
                if(handoff_case=="wrong-epoch")p->incoming.back().meta.epoch=1;
            }
            if(handoff_case=="leaving-last")ControlIn(LEAVING);
            if(handoff_case=="terminal"||handoff_case=="terminal-before") {p->status.phase=CE_NET_CLOSED;p->status.error=CE_NET_PEER_LEFT;}
        }
    } else if(kind==CE_NET_BASELINE) {if(world_send_backpressure)return 0;++accepted_world_sends;RemoteBaseline(data,size,tick);if(inject_birth_baseline)ControlIn(BIRTHS_APPLIED,0,tick);ControlIn(BASELINE_APPLIED,1,tick);}
    else if(kind==CE_NET_STATE) {if(world_send_backpressure)return 0;++accepted_world_sends;++state_sends;state_bytes+=size;RemoteState(data,size,tick,false);}
    else if(kind==CE_NET_STATE_REPAIR) {if(world_send_backpressure)return 0;++accepted_world_sends;RemoteState(data,size,tick,true);}
    else if(kind==CE_NET_INPUT) {
        ++input_attempts;
        std::vector<uint8_t> bytes(data,data+size);
        if(attempted_input.empty())attempted_input=bytes;else Check(attempted_input==bytes,"pending input bytes retained exactly");
        if(send_backpressure) {--send_backpressure;return 0;} ++input_sent; attempted_input.clear();
    }
    return 1;
}
extern "C" int CE_WorldBegin(const CE_WorldConfig *c) { ++begun;world_live=true;world_replica=c->role==CE_WORLD_REPLICA;world_epoch=c->epoch;world_committed=false;RemoteOwners(c->role==CE_WORLD_HOST);return 1; }
extern "C" void CE_WorldEnd() { if(!world_live) {++ended;world_replica=false;} }
extern "C" int CE_WorldActive() {return world_live;}
extern "C" int CE_WorldReplica() {return world_live&&world_replica;}
extern "C" int CE_WorldCanRender(){return !world_live||(!poisoned&&(!world_replica||applied>0));}
extern "C" const char *CE_WorldError(){return "controlled world refusal";}
extern "C" CE_WorldFrame *CE_WorldFrameCreate(){if(++frame_allocations==refuse_frame_allocation)return NULL;return new CE_WorldFrame{};}
extern "C" void CE_WorldFrameDestroy(CE_WorldFrame *f){delete f;}
extern "C" int CE_WorldFrameInfo(const CE_WorldFrame *f,CE_WorldInfo *out){*out=f->info;return 1;}
extern "C" int CE_WorldFramePublished(const CE_WorldFrame *f){
    ++publication_attempts;Check(publication_attempts<=accepted_world_sends,"publication follows an accepted transport enqueue");Check(f==s.capture,"publication receives the exact captured frame");
    if(reject_publication)return 0;++publications;return 1;
}
extern "C" int CE_WorldCapture(CE_WorldFrame *f){++captures;f->info={};f->info.epoch=world_epoch;f->info.level=852;f->info.tick=host_tick;f->info.frame_sequence=++frame_sequence;f->bytes=capture_bytes;f->seed=capture_seed;
#ifdef SESSION_LIFECYCLE_TEST
    LifecycleCaptureInfo(f);
#else
    if(s.lifecycle_started)f->info.lifecycle_baseline=s.baseline_sequence;
#endif
    return host_tick!=0;}
extern "C" int CE_WorldEncode(const CE_WorldFrame *f,uint8_t *out,uint32_t cap,uint32_t *size){
    CE_NetWriter w;CE_NetWriterInit(&w,out,cap);CE_NetWriteU32(&w,f->info.epoch);CE_NetWriteU32(&w,f->info.tick);CE_NetWriteU32(&w,f->info.frame_sequence);CE_NetWriteU32(&w,f->info.level);CE_NetWriteU32(&w,f->birth);
    CE_NetWriteU32(&w,f->info.lifecycle_baseline);CE_NetWriteU32(&w,f->info.lifecycle_watermark);
    for(uint32_t i=28;i<f->bytes;++i)CE_NetWriteU8(&w,uint8_t(i*31+f->seed));*size=w.size;return !w.error;
}
extern "C" int CE_WorldDecode(CE_WorldFrame *f,const uint8_t *data,uint32_t size){
    CE_NetReader r;CE_NetReaderInit(&r,data,size);f->info={};f->info.epoch=CE_NetReadU32(&r);f->info.tick=CE_NetReadU32(&r);f->info.frame_sequence=CE_NetReadU32(&r);f->info.level=CE_NetReadU32(&r);f->birth=CE_NetReadU32(&r);
#ifdef SESSION_LIFECYCLE_TEST
    LifecycleFrameInfo(f);
#endif
    f->info.lifecycle_baseline=CE_NetReadU32(&r);f->info.lifecycle_watermark=CE_NetReadU32(&r);f->bytes=size;
    while(r.position<r.size)CE_NetReadU8(&r);
    return CE_NetReadDone(&r);
}
extern "C" int CE_WorldApply(const CE_WorldFrame *f){if(poison_apply){poisoned=true;return 0;}++applied;ticket=f->info.tick!=applied_tick;host_tick=applied_tick=f->info.tick;committed_birth=f->birth;world_committed=true;if(compatibility_lost_on_apply)compatibility_ready=false;return 1;}
// Existing orchestration controls retain their authored legacy birth boundary. The separate lifecycle driver
// and real-World coupling exercise the new admitted stream, including the actual codec and readiness bodies.
#ifndef SESSION_LIFECYCLE_TEST
extern "C" int CE_WorldLifecycleStart(const CE_WorldFrame *f){Check(f->info.frame_sequence==s.baseline_sequence&&f->info.tick==s.baseline_tick,"lifecycle Start retains the exact admitted baseline tuple");Check(s.host?s.baseline_applied:s.start_seen,"lifecycle Start follows exact peer baseline/START proof");return 1;}
extern "C" int CE_WorldPeekLifecycle(CE_WorldLifecycleInfo*,uint8_t*,uint32_t){return 0;}
extern "C" int CE_WorldLifecyclePublished(const CE_WorldLifecycleInfo*){return -1;}
extern "C" int CE_WorldInspectLifecycle(const uint8_t*,uint32_t,CE_WorldLifecycleInfo*){return -1;}
extern "C" int CE_WorldApplyLifecycle(const uint8_t*,uint32_t){return -1;}
extern "C" int CE_WorldAppliedLifecycle(CE_WorldLifecycleAck*){return 0;}
extern "C" int CE_WorldAcknowledgeLifecycle(const CE_WorldLifecycleAck*){return -1;}
extern "C" int CE_WorldFrameReadiness(const CE_WorldFrame*){return CE_WORLD_READY;}
#endif
// The session reaches the lockstep co-op prototype's hooks. These suites start no lockstep session, so each answers "not requested" and
// a load, a tick or a teardown passes through unchanged.
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
extern "C" int CE_WorldAppliedBirths(CE_WorldBirthAck *out){
    ++birth_getters;if(!world_live||!world_replica||!world_committed||poisoned||birth_getter_unavailable)return 0;
    *out={birth_bad_epoch?world_epoch+1:world_epoch,committed_birth,birth_zero_tick?0:applied_tick};return 1;
}
extern "C" int CE_WorldAcknowledgeBirths(uint32_t epoch,uint32_t serial,uint32_t tick){
    delegated_birth_acks.push_back({epoch,serial,tick});return reject_birth_ack?-1:1;
}
extern "C" int CE_WorldSampleInput(CE_WorldInput *out){
    ++sample_peeks;if(suppress_samples)return 0;
    if(!ticket || (input_suspended && !leased_sequence))return 0;
    if(!leased_sequence){leased_sequence=++local_sequence;++sampled;leased_tick=applied_tick+1;leased_birth=committed_birth;}
    *out={};out->epoch=2;out->tick=leased_tick;out->player=1;out->sequence=leased_sequence;out->last_applied_birth_serial=leased_birth;return 1;
}
extern "C" int CE_WorldInputSent(uint32_t sequence){
    ++ack_attempts;if(reject_input_ack||!leased_sequence||sequence!=leased_sequence)return -1;
    leased_sequence=0;ticket=false;++input_acks;return 1;
}
extern "C" int CE_WorldEncodeInput(const CE_WorldInput *in,uint8_t *out,uint32_t cap,uint32_t *size){
    CE_NetWriter w;CE_NetWriterInit(&w,out,cap);CE_NetWriteU32(&w,in->epoch);CE_NetWriteU32(&w,in->tick);CE_NetWriteU32(&w,in->player);CE_NetWriteU32(&w,in->sequence);CE_NetWriteU32(&w,in->last_applied_birth_serial);*size=w.size;return !w.error;
}
extern "C" int CE_WorldDecodeInput(CE_WorldInput *out,const uint8_t *data,uint32_t size){
    CE_NetReader r;CE_NetReaderInit(&r,data,size);*out={};out->epoch=CE_NetReadU32(&r);out->tick=CE_NetReadU32(&r);out->player=CE_NetReadU32(&r);out->sequence=CE_NetReadU32(&r);out->last_applied_birth_serial=CE_NetReadU32(&r);return CE_NetReadDone(&r);
}
extern "C" int CE_WorldQueueRemoteInput(const CE_WorldInput *in){++queue_attempts;if(queue_backpressure){--queue_backpressure;return 0;}++queued;queued_sequences.push_back(in->sequence);return 1;}
extern "C" int CE_WorldSetInputSuspended(uint32_t mask){if(!handoff_case.empty())Check(s.stage==RUNNING,"handoff metadata never calls the world input gate before RUNNING");if(mask&~1u)return -1;input_suspended=mask;return 1;}
extern "C" void CE_WorldRebaseLocalSampler(){++sampler_rebases;if(!leased_sequence)ticket=false;}
extern "C" uint32_t CE_WorldLastGeneratedSequence(){return local_sequence;}
extern "C" uint32_t CE_SessionWindowStatus(){return window_reasons;}
bool PortClockVirtual(){return virtual_clock;}
static int game_phase=1;
struct {bool mBuildResources=false,mMusic=false,mSound=false;} CLIPARAMS;
using BOOL=int;
#define TRUE 1
#define FALSE 0
#define PC 1
#define PS2 2
#define XBOX 3
#define TARGET PC
#define PLAYABLE_DEMO false
static bool pause_for_showing_controls;
static unsigned init_failure, resource_touches, shutdowns;
static bool lifecycle_check, expected_requested, expected_mission_fence, shutdown_finished;
static std::vector<std::string> resource_order;
static void ResourceTouch(const char *name){
    if(!lifecycle_check)return;
    Check(CE_SessionActive()==expected_requested&&CE_SessionMissionVMBlocked()==expected_mission_fence,
          "native resource boundaries retain admitted mission fence");
    Check(!s.world_started&&!world_live,"Init failure boundary precedes any native world or adapter start");
    ++resource_touches;resource_order.push_back(name);
}
struct NativeResourceBoundary {
    const char *name; unsigned gate;
    bool Init(){ResourceTouch(name);return init_failure!=gate;}
    void Shutdown(){ResourceTouch(name);} void ShutDown(){ResourceTouch(name);}
    void Reset(){ResourceTouch(name);} void Initialise(){ResourceTouch(name);}
    void SetNumViewpoints(int){ResourceTouch(name);}
} MAP={"map",1},ENGINE={"engine",2},HUD={"hud",0},GAMEINTERFACE={"interface",0},PARTICLE_SET={"particle-set",0},STATICSHADOWS={"shadows",0},RENDERQUEUE={"render-queue",0};
struct CIMPOSTER {static bool InitAll(){ResourceTouch("imposter-init");return init_failure!=3;}static void ShutdownAll(){ResourceTouch("imposter-shutdown");}};
struct GenericSPtrSet {static void ClearAnyDynamicCreatedNodes(){ResourceTouch("list-cleanup");}};
struct CMESH {static void FreeLevelResources(){ResourceTouch("mesh");}};
struct CVBufTexture {static void FreeLevelResources(){ResourceTouch("vertex-buffer");}};
struct CTEXTURE {static void FreeLevelResources(){ResourceTouch("texture");}};
struct {void SetMerge(bool){ResourceTouch("merge");}void Cleanup(){ResourceTouch("heap-cleanup");}} MEM_MANAGER;
struct {void Stop(){ResourceTouch("music");}} MUSIC;
struct {void ReloadLanguageSampleBank(){ResourceTouch("sound-reload");}} SOUND;
enum {CVar_bool=1};
struct {
    void InitDefaultCommands(){ResourceTouch("console-init");}
    void RegisterVariable(const char*,const char*,int,bool*){ResourceTouch("console-register");}
    void SetLoading(bool){ResourceTouch("console-loading");}
    void StatusDone(const char*){ResourceTouch("console-status-done");}
    void Status(const char*){ResourceTouch("console-status");}
    void ClearCommandsAndVariables(){ResourceTouch("console-clear");shutdown_finished=true;++shutdowns;}
    void Exec(const char*name){Check(!strcmp(name,"autoexec.con"),"native automatic-script caller retains its literal filename");++automatic_execs;}
} CONSOLE;
bool ceEditorAuthoring=false;   // the map editor's flag (patch 0206) that the automatic-script call is gated on: never set here
enum EQuitType {QT_NONE=0,QT_QUIT_TO_FRONTEND=1,QT_QUIT_TO_SYSTEM=2,QT_LOAD_ERROR=3,QT_RESTART_LEVEL=4,QT_QUIT_TIMEOUT=5,QT_USER_QUIT_TO_FRONTEND=6};
enum {GAME_STATE_PRE_RUNNING=1,MAX_PLAYERS=4};
using SINT=int;
#define SAFE_DELETE(p) do {delete(p);(p)=NULL;} while(0)
struct CGame {
    EQuitType mQuit=QT_NONE;
    bool mPause=false,mRestarting=false,mFirstTimeRound=false,mShowMemDeltas=false,mShowDataSizes=false;
    int mCurrentlyRunningLevel=0,mRetryCount=0;
    float mBaseTime=0,mFrameTime=0,mFrameTime2=0;
    int *mPlayer[MAX_PLAYERS]={},*mCurrentCamera[MAX_PLAYERS]={};
    int GetGameState(){return game_phase;}
    void Update(){Check(s.peer_epoch_loaded,"host Update waits for both admitted worlds");++updates;++host_tick;if(updates==3)game_phase=2;}
    void SetQuit(EQuitType quit){mQuit=quit;game_phase=9;}
    void CalcRenderFrameFraction(){}
    void PreRun();
    BOOL Init(); void Shutdown(); EQuitType RunLevel(SINT level);
    void RunOutroFMV(){ResourceTouch("outro");}
    void Render(SINT num_renders);
} GAME;
struct {float GetSysTimeFloat(){return now_ms/1000.f;}} PLATFORM;
struct MSG {unsigned message;};
enum {PM_REMOVE=1,WM_QUIT=0x12};
static int PeekMessage(MSG *message,void *,unsigned,unsigned,unsigned){
    ++pumps;if(close_window){message->message=WM_QUIT;return 1;}message->message=0x400;return flood_messages?1:0;
}
static void TranslateMessage(MSG *){}
static void DispatchMessage(MSG *){++dispatched;}
struct PCLTShell {bool WantsToQuit(){return false;} void TriggerQuit();} LT;
struct {int quit=0;void SetQuit(int code){quit=code;}} FRONTEND;
#include TRIGGER_QUIT_SOURCE
static void HandoffQuit(){LT.TriggerQuit();}
#include PRERUN_SOURCE
#include BRIDGE_SOURCE
extern "C" void CE_SessionGamePreRun(){++pre_runs;CENetworkSession::PreRun();}
extern "C" void CE_SessionGameIdle(){now_ms+=100;if(peer&&peer->status.role==CE_NET_GUEST&&s.stage==LOADED&&s.peer_loaded&&peer->status.epoch==1)peer->status.epoch=2;}
extern "C" int CE_SessionGameQuitCode(){return CENetworkSession::QuitCode();}
extern "C" void CE_SessionGameSetQuit(int quit){GAME.SetQuit((EQuitType)quit);}
extern "C" uint32_t CE_SessionGameTick(){return host_tick;}
extern "C" void CE_SessionGameRebaseRenderClock(){++rebased;CENetworkSession::RebaseRenderClock();}
extern "C" int CE_SessionGameAbortLoad(){return CENetworkSession::AbortLoad();}
// The frame hooks the model and terrain quality patches put at the top of CGame::Render: this driver measures neither.
static void CeModelQualityBeginFrame(){}
struct CeTerrainRenderFrameScope{CeTerrainRenderFrameScope(){} ~CeTerrainRenderFrameScope(){}};
#include RENDER_ENTRY_SOURCE
#include INIT_ENTRY_SOURCE
static void Configure(bool host){
    setenv(host?"BEA_NET_HOST":"BEA_NET_JOIN","127.0.0.1:27652",1);
    setenv("BEA_NET_SECRET","0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",1);
    setenv("BEA_NET_COMPAT","abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",1);
}
static void Finish(){CE_SessionBeginTeardown(1);world_live=false;CE_SessionAfterWorldShutdown();Check(!s.requested&&!peer,"session and world retired");}
static void InitFailureCases(const std::string &mode){
    bool offline=mode.find("offline")!=std::string::npos,host=mode.find("host")!=std::string::npos;
    if(!offline)Configure(host);
    lifecycle_check=true;expected_requested=!offline;expected_mission_fence=!offline&&!host;
    init_failure=mode.find("map")!=std::string::npos?1:mode.find("engine")!=std::string::npos?2:mode.find("imposter")!=std::string::npos?3:0;
    // Execute the native RunLevel prefix, actual Init and actual Shutdown; hardware/resource methods are boundaries.
    int result=GAME.RunLevel(offline?100:852);
    Check(resource_touches>0&&!begun&&!world_live&&!pre_runs&&!updates,"early native caller has no world admission or gameplay effects");
    if(init_failure){
        Check(result==QT_LOAD_ERROR&&shutdowns==1&&shutdown_finished,"failed Init preserves native shutdown and load-error result");
        Check(!s.requested&&!peer&&!CE_SessionMissionVMBlocked()&&destroyed==created,"failed native Init retires pending session after native shutdown");
        lifecycle_check=false;
        Check(CE_SessionConfigureLevel(offline?100:852)&&CE_SessionMissionVMBlocked()==(!offline&&!host),"next valid level configuration is not blocked by failed Init");
        Finish();
    }else{
        Check(result==QT_NONE&&!shutdowns&&s.requested==!offline&&CE_SessionMissionVMBlocked()==(!offline&&!host),"successful Init prefix retains pending admission for native loading");
        GAME.Shutdown();lifecycle_check=false;Finish();
    }
    Check(resource_order.back()=="console-clear","native PC Shutdown completes all resource boundary calls before session retirement");
}
static void MissionFenceCases(const std::string &mode){
    Check(!CE_SessionMissionVMBlocked(),"fresh session permits ordinary mission VM");
    if(mode=="vm-fence-offline"){
        Check(CE_SessionConfigureLevel(100)&&!CE_SessionMissionVMBlocked()&&!created,"offline campaign does not latch replica role");return;
    }
    bool host=mode=="vm-fence-host";Configure(host);
    bool invalid=mode.find("invalid")!=std::string::npos;
    if(mode=="vm-fence-invalid-endpoint")setenv("BEA_NET_JOIN","localhost:1",1);
    if(mode=="vm-fence-invalid-secret")setenv("BEA_NET_SECRET","bad",1);
    if(mode=="vm-fence-invalid-disabled")setenv("BEA_PORT_TAPE","1",1);
    if(mode=="vm-fence-invalid-role")setenv("BEA_NET_HOST","127.0.0.1:27652",1);
    refuse_net_allocation=mode=="vm-fence-allocation-net";
    refuse_frame_allocation=mode=="vm-fence-allocation-first"?1:mode=="vm-fence-allocation-second"?2:0;
    bool rejected=invalid||refuse_net_allocation||refuse_frame_allocation;
    int configured=CE_SessionConfigureLevel(mode=="vm-fence-invalid-level"?100:852);
    if(rejected){Check(!configured&&!CE_SessionMissionVMBlocked(),"invalid or incomplete allocation cannot latch mission fence");Finish();return;}
    Check(configured&&CE_SessionMissionVMBlocked()==!host&&!world_live,"validated local role is latched before world load");
    Check(CE_SessionAfterLoad()&&CE_SessionMissionVMBlocked()==!host,"epoch and world admission preserve local role fence");
    if(host)host_tick=57;
    Check(CE_SessionPreparePlay()&&CE_SessionMissionVMBlocked()==!host,"baseline and start barriers preserve local role fence");
    if(!host){Fail("controlled post-admission peer failure");Check(CE_SessionMissionVMBlocked(),"failed connection cannot release mission fence around live guest world");}
    CE_SessionBeginTeardown(1);Check(CE_SessionMissionVMBlocked()==!host,"native world destruction retains admitted role fence");
    CE_SessionAfterWorldShutdown();Check(s.requested&&world_live&&CE_SessionMissionVMBlocked()==!host&&!peer,"incomplete native destruction cannot release local role fence");
    world_live=false;CE_SessionAfterWorldShutdown();Check(!CE_SessionMissionVMBlocked()&&!s.requested&&!peer,"final native world retirement releases cached fence");
    unsetenv("BEA_NET_HOST");unsetenv("BEA_NET_JOIN");Check(CE_SessionConfigureLevel(100)&&!CE_SessionMissionVMBlocked(),"replacement ordinary session cannot inherit old guest role");
}
static void InputIn(uint32_t sequence,uint32_t tick) {
    CE_WorldInput in={};in.epoch=2;in.tick=tick;in.player=1;in.sequence=sequence;
    uint8_t data[1000];uint32_t n;CE_WorldEncodeInput(&in,data,sizeof data,&n);PacketIn(CE_NET_INPUT,tick,data,n);
}
static void BirthCases(const std::string &mode) {
    bool host=mode.rfind("host-",0)==0;
    Configure(host);suppress_samples=true;
    bool pending=mode=="guest-birth-baseline"||mode=="guest-birth-backpressure"||mode=="guest-birth-coalesce"||
        mode=="guest-birth-service"||mode=="guest-birth-wait-order"||mode=="guest-birth-unavailable"||mode=="guest-birth-poison-pending"||mode=="guest-birth-reset"||mode=="guest-birth-close";
    if(pending){baseline_birth=2;if(mode!="guest-birth-baseline")birth_send_backpressure=100;}
    inject_birth_baseline=mode=="host-birth-baselined";
    Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"birth route uses actual load/epoch barrier");
    if(mode=="host-birth-wrong-stage") {
        ControlIn(BIRTHS_APPLIED,2,60);Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&delegated_birth_acks.empty(),"birth ACK refuses before host baseline stage");Finish();return;
    }
    if(host)host_tick=57;
    Check(CE_SessionPreparePlay()&&s.stage==RUNNING,"birth route uses actual whole baseline barrier");
    if(host) {
        Check(birth_getters==0&&birth_sends.empty(),"host never produces replica birth proof");
        if(mode=="host-birth-baselined")Check(delegated_birth_acks.size()==1&&delegated_birth_acks[0].serial==0&&delegated_birth_acks[0].tick==60,"host delegates complete proof during BASELINED barrier");
        else {
            if(mode=="host-birth-refused")reject_birth_ack=true;
            ControlIn(BIRTHS_APPLIED,reject_birth_ack?UINT32_MAX:3,reject_birth_ack?UINT32_MAX:60,mode=="host-birth-old-schema"?3:kControlSchema,mode=="host-birth-short"?-1:mode=="host-birth-long"?1:0);
            if(mode=="host-birth-wrong-epoch")peer->incoming.back().meta.epoch=1;
            if(mode=="host-birth-session-epoch"){peer->status.epoch=3;peer->incoming.back().meta.epoch=3;}
            if(mode=="host-birth-envelope")++peer->incoming.back().meta.tick;
            if(mode=="host-birth-wrong-level") {auto &bytes=peer->incoming.back().bytes;bytes[15]=100;}
            bool refusal=mode!="host-birth-accept";
            int service=CE_SessionSystemService(1,1);
            if(refusal) {
                Check(service==CE_SESSION_EXIT&&s.stage==FAILED&&GAME.mQuit==6,"invalid or world-refused birth ACK closes session");
                Check(delegated_birth_acks.size()==(mode=="host-birth-refused"?1u:0u),"malformed role/epoch/schema/envelope proof never reaches world admission");
                if(reject_birth_ack)Check(delegated_birth_acks[0].epoch==2&&delegated_birth_acks[0].serial==UINT32_MAX&&delegated_birth_acks[0].tick==UINT32_MAX,"future bounds are delegated without truncating the received tuple");
            } else {
                Check(service==CE_SESSION_NATIVE&&delegated_birth_acks.size()==1,"host delegates received birth application proof");
                auto proof=delegated_birth_acks.back();Check(proof.epoch==2&&proof.serial==3&&proof.tick==60,"world receives exact epoch serial and committed tick");
                ControlIn(BIRTHS_APPLIED,3,60);ControlIn(BIRTHS_APPLIED,1,59);InputIn(1,61);
                Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&delegated_birth_acks.size()==3&&queued_sequences==std::vector<uint32_t>({1}),"older duplicate proofs and an older immutable input retain session ordering");
            }
        }
        Finish();return;
    }
    Check(sampled==0&&sample_peeks==0,"baseline application ACK needs no gameplay sampling");
    if(mode=="guest-birth-baseline") {
        Check(applied==1&&birth_sends.size()==1&&s.birth_ack_sent==2,"whole baseline sends one cumulative application ACK");
        auto proof=birth_sends.back();Check(proof.epoch==2&&proof.serial==2&&proof.tick==60,"baseline ACK carries committed tuple and exact header");
    } else if(mode=="guest-birth-backpressure"||mode=="guest-birth-service"||mode=="guest-birth-wait-order") {
        Check(birth_sends.empty()&&s.birth_ack.serial==2&&s.birth_ack_sent==0,"Send0 keeps committed proof pending");
        unsigned attempts=birth_ack_attempts,peeks=sample_peeks;birth_send_backpressure=1;
        GAME.mPause=true;game_phase=9;
        if(mode=="guest-birth-wait-order")ControlIn(HOST_WAIT,1,applied_tick);
        control_order.clear();Check(CE_SessionSystemService(0,2)==CE_SESSION_WAIT,"unready paused menu route still services transport");
        Check(birth_ack_attempts==attempts+1&&birth_sends.empty()&&s.birth_ack_sent==0,"one service Poll retries Send0 once without marking sent");
        if(mode=="guest-birth-wait-order")Check(control_order==std::vector<unsigned>({GUEST_BLOCK,HOST_WAIT_ACK,BIRTHS_APPLIED}),"shared wait acknowledgement precedes birth proof");
        Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT&&birth_sends.size()==1&&s.birth_ack_sent==2,"service-only Send1 advances cumulative acknowledgement");
        attempts=birth_ack_attempts;
        for(unsigned i=0;i<100;++i)Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT,"unready service remains bounded");
        Check(birth_ack_attempts==attempts&&sample_peeks==peeks&&!sampled,"sent proof neither floods nor touches gameplay sampler");
    } else if(mode=="guest-birth-coalesce") {
        FrameIn(CE_NET_STATE,61,2,4);CE_SessionBeforeRender();FrameIn(CE_NET_STATE,62,3,7);CE_SessionBeforeRender();
        Check(s.birth_ack.serial==7&&s.birth_ack.tick==62&&birth_sends.empty(),"higher complete Apply supersedes unsent cumulative proof");
        birth_send_backpressure=0;CE_SessionSystemService(0,2);
        Check(birth_sends.size()==1&&birth_sends[0].serial==7&&birth_sends[0].tick==62,"only highest retained whole-Apply proof is sent");
    } else if(mode=="guest-birth-unavailable"||mode=="guest-birth-poison-pending") {
        unsigned attempts=birth_ack_attempts;birth_send_backpressure=0;
        if(mode=="guest-birth-unavailable")birth_getter_unavailable=true;else poisoned=true;
        CE_SessionSystemService(0,2);
        Check(birth_ack_attempts==attempts&&birth_sends.empty()&&s.birth_ack.serial==0,"unavailable or poisoned getter cancels stale pending proof");
    } else if(mode=="guest-birth-reset") {
        CE_SessionBeginTeardown(1);Check(!s.birth_ack.serial&&!s.birth_ack.tick&&!s.birth_ack_sent,"teardown clears all birth ACK state");
        world_live=false;CE_SessionAfterWorldShutdown();Check(!s.requested&&!peer,"old session is fully retired before replacement");
        baseline_birth=0;birth_send_backpressure=0;unsigned attempts=birth_ack_attempts;
        Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad()&&s.stage==PREPARING,"fresh session reenters genuine load barrier");
        Check(!s.birth_ack.serial&&!s.birth_ack_sent&&birth_ack_attempts==attempts,"new session cannot replay prior-epoch pending proof");
    } else if(mode=="guest-birth-lease") {
        suppress_samples=false;send_backpressure=100;CE_SessionBeforeRender();
        Check(s.have_input&&leased_sequence==1&&!input_acks,"old input lease remains with transport caller");
        auto bytes=attempted_input;uint32_t size=s.input_size;unsigned generated=sampled;
        FrameIn(CE_NET_STATE,61,2,3);CE_SessionBeforeRender();
        CE_WorldInput held={};Check(CE_WorldDecodeInput(&held,s.input,s.input_size)&&held.last_applied_birth_serial==0,"retained input keeps original earlier birth watermark");
        Check(birth_sends.size()==1&&birth_sends[0].serial==3&&s.have_input&&size==s.input_size&&attempted_input==bytes&&sampled==generated&&!input_acks,"birth control overtakes old exact input bytes without resampling or popping");
        send_backpressure=0;CE_SessionBeforeRender();Check(!s.have_input&&input_acks==1,"old immutable input later receives its own exact send acknowledgement");
    } else if(mode=="guest-birth-wrong-role") {
        ControlIn(BIRTHS_APPLIED,3,60);Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&delegated_birth_acks.empty(),"guest refuses host-originated birth application ACK");
    } else if(mode=="guest-birth-bad-epoch"||mode=="guest-birth-zero-tick") {
        birth_bad_epoch=mode=="guest-birth-bad-epoch";birth_zero_tick=mode=="guest-birth-zero-tick";
        Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&birth_sends.empty(),"invalid committed getter tuple fails before sending");
    } else if(mode=="guest-birth-zero") {
        unsigned attempts=birth_ack_attempts;for(unsigned i=0;i<100;++i)CE_SessionSystemService(1,1);
        Check(birth_ack_attempts==attempts&&birth_sends.empty()&&!s.birth_ack_sent,"serial zero never creates a birth ACK packet");
    } else if(mode=="guest-birth-close") {
        LT.TriggerQuit();unsigned getters=birth_getters,attempts=birth_ack_attempts;
        Check(CE_SessionSystemService(0,2)==CE_SESSION_EXIT,"native close owns pending birth proof exit");
        CE_SessionBeforeRender();Check(birth_getters==getters&&birth_ack_attempts==attempts,"known close performs no proof observation or send");
    } else {
        Check(mode=="guest-birth-decode"||mode=="guest-birth-apply-fail"||mode=="guest-birth-send-invalid","known birth control case");
        FrameIn(CE_NET_STATE,61,2,3);CE_SessionSystemService(0,2);
        Check(s.have_frame&&applied==1&&birth_sends.empty()&&!s.birth_ack_sent,"decoded incoming state never grants application proof");
        CE_SessionBeforeRender();
        Check(s.have_frame&&applied==1&&birth_sends.empty()&&!s.birth_ack_sent,"unavailable replica retains state without Apply or application proof");
        // Complete the genuine availability/wait protocol before asking these Apply-boundary negatives to run.
        ControlIn(HOST_WAIT,1,60);CE_SessionSystemService(1,1);
        ControlIn(GUEST_BLOCK_ACK,1,60);CE_SessionSystemService(1,1);
        ControlIn(GUEST_READY_ACK,2,60);ControlIn(HOST_RESUME,1,60);CE_SessionSystemService(1,1);
        Check(RuntimeReady(),"Apply-boundary control resumes only after exact availability and shared-wait receipts");
        poison_apply=mode=="guest-birth-apply-fail";birth_send_invalid=mode=="guest-birth-send-invalid";
        unsigned peeks=sample_peeks;CE_SessionBeforeRender();
        if(poison_apply)Check(s.stage==FAILED&&poisoned&&birth_ack_attempts==0&&!s.birth_ack_sent,"failed partial Apply cannot emit or commit birth ACK");
        else if(birth_send_invalid)Check(s.stage==FAILED&&birth_sends.empty()&&!s.birth_ack_sent,"invalid birth send fails without marking acknowledgement accepted");
        else Check(birth_sends.size()==1&&birth_sends[0].serial==3&&birth_sample_peeks.back()==peeks&&sample_peeks==peeks+1,"successful Apply sends proof before the gameplay input peek");
    }
    Finish();
}
static void StartHandoffCases(const std::string &mode) {
    handoff_case=mode.substr(strlen("start-handoff-"));Configure(true);suppress_samples=true;
    Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"handoff begins after genuine load and epoch barriers");
    host_tick=57;
    bool prepared=CE_SessionPreparePlay();
    bool early=handoff_case=="before"||handoff_case=="terminal-before"||handoff_case=="unqueued"||handoff_case=="wrong-epoch";
    bool departure=handoff_case=="terminal"||handoff_case=="leaving-first"||handoff_case=="leaving-last";
    Check(updates==3&&captures==1&&begun==1&&applied==0&&sample_peeks==0&&input_suspended==0&&sampler_rebases==0,
          "coalesced handoff retains metadata without extra native updates capture apply sampling or input callbacks");
    Check(availability_attempts.empty()&&!terminal_send_attempts,"handoff sends no availability receipt or post-close control before RUNNING");
    if(early) {
        Check(!prepared&&s.stage==FAILED&&s.peer_guest_generation==0,"BLOCK before queued START and exact START_APPLIED refuses without latching availability");
        if(handoff_case!="wrong-epoch")Check(handoff_block_stage==BASELINED,"early negative reaches actual BASELINED dispatcher");
        if(handoff_case=="before"||handoff_case=="terminal-before")Check(!handoff_block_start_applied,"early BLOCK has no preceding START_APPLIED");
        if(handoff_case=="unqueued")Check(handoff_block_start_applied&&!handoff_block_start_queued,"unqueued START remains outside the handoff despite a forged receipt");
    } else if(departure) {
        Check(!prepared&&s.stage==DRAINING&&GAME.mQuit==1&&exit_outcome==EXIT_GRACEFUL&&!CE_SessionError()[0],
              "ordered terminal handoff and peer departure drain gracefully without play admission");
        Check(s.start_applied&&s.peer_guest_generation==1&&!s.peer_guest_ack_pending&&rebased==0,
              "terminal receipt preserves validation order but cancels pending availability before any ready callback");
    } else if(handoff_case=="quit") {
        Check(!prepared&&GAME.mQuit==1&&FRONTEND.quit==-1&&s.stage==BASELINED&&!CE_SessionError()[0]&&rebased==0,
              "native quit during handoff cancels PreparePlay without RUNNING or peer failure");
        Check(s.peer_guest_blocked&&s.peer_guest_ack_pending,"cancelled handoff retains metadata until native teardown");
    } else if(handoff_case=="control") {
        Check(prepared&&s.stage==RUNNING&&!s.peer_guest_blocked&&!s.peer_guest_ack_pending,"ordinary startup is unchanged");
        if(CE_SessionBeforeUpdate())GAME.Update();
        Check(updates==4,"ordinary startup permits the next native update");
    } else {
        Check(prepared&&s.stage==RUNNING&&s.peer_guest_blocked&&s.peer_guest_ack_pending&&!s.shared_wait,
              "ordered post-START_APPLIED BLOCK is retained through the full PreparePlay handoff");
        Check(handoff_block_stage==BASELINED&&handoff_block_start_queued&&handoff_block_start_applied,
              "positive reaches exact acknowledged BASELINED handoff");
        if(handoff_case=="cancel") {
            LT.TriggerQuit();unsigned polled=network_polls;
            Check(!CE_SessionBeforeUpdate()&&network_polls==polled&&rebased==1&&!s.shared_wait,
                  "native close before first update prevents polling or driving retained handoff work");
            CE_SessionBeginTeardown(1);
            Check(!s.peer_guest_ack_pending&&s.stage==DRAINING,"teardown cancels retained handoff receipt");
        } else {
            if(CE_SessionBeforeUpdate())GAME.Update();
            Check(updates==3&&host_tick==60&&s.shared_wait&&input_suspended==1&&s.wait_generation==1,
                  "first BeforeUpdate freezes the acknowledged guest BLOCK before native advancement");
            CE_SessionAfterFlush();Check(captures==1,"frozen first handoff cannot publish a new world frame");
            Check(CE_SessionSystemService(1,1)==CE_SESSION_WAIT&&s.wait_phase==WAIT_HELD&&!s.peer_guest_ack_pending,
                  "retained handoff receipt follows the ordinary acknowledged shared wait");
            Check(availability_sends.size()==1&&availability_sends[0].kind==GUEST_BLOCK_ACK&&availability_sends[0].generation==1&&availability_sends[0].tick==60,
                  "handoff preserves exact source tick and generation through later receipt");
        }
    }
    Finish();
}
static void AvailabilityStart(bool host) {
    Configure(host);suppress_samples=true;
    Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"availability uses actual load and epoch barrier");
    if(host)host_tick=57;
    Check(CE_SessionPreparePlay()&&s.stage==RUNNING&&host_tick==60,"availability begins only after whole baseline and START");
    control_order.clear();
}
static void HostGuestBlocked() {
    ControlIn(GUEST_BLOCK,1,host_tick);
    Check(CE_SessionSystemService(1,1)==CE_SESSION_WAIT&&s.shared_wait&&s.peer_guest_blocked,
          "guest-only unavailability enters host shared wait");
    Check(CE_SessionSystemService(1,1)==CE_SESSION_WAIT&&s.wait_phase==WAIT_HELD,
          "host holds until explicit guest readiness after WAIT acknowledgement");
}
static void GuestBlockHeld() {
    window_reasons=CE_SESSION_UNFOCUSED;
    Check(CE_SessionSystemService(0,1)==CE_SESSION_WAIT&&s.guest_request_queued&&s.guest_generation==1,
          "guest unavailability queues one BLOCK request");
    ControlIn(GUEST_BLOCK_ACK,1,host_tick);ControlIn(HOST_WAIT,1,host_tick);
    Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT&&s.shared_wait&&s.guest_reported_blocked&&!s.wait_ack_pending,
          "guest admits exact receipt and acknowledges shared wait");
}
static void AvailabilityCases(const std::string &mode) {
    bool host=mode.rfind("host-",0)==0;AvailabilityStart(host);
    unsigned c=captures,a=applied,p=sample_peeks,r=rebased;
    if(host) {
        if(mode=="host-availability-invalid-send") {
            availability_send_invalid=true;ControlIn(GUEST_BLOCK,1,host_tick);
            CE_SessionSystemService(1,1);
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED,"invalid availability receipt send fails closed");
        } else if(mode=="host-availability-backpressure") {
            availability_backpressure=100;ControlIn(GUEST_BLOCK,1,host_tick);
            Check(CE_SessionSystemService(1,1)==CE_SESSION_WAIT&&s.peer_guest_ack_pending&&availability_sends.empty(),"host receipt retains Send0 without releasing wait");
            ControlIn(GUEST_BLOCK,1,host_tick);CE_SessionSystemService(1,1);
            Check(s.peer_guest_generation==1&&s.wait_generation==1,"exact duplicate BLOCK is idempotent while receipt is pending");
            availability_backpressure=0;CE_SessionSystemService(1,1);
            Check(!s.peer_guest_ack_pending&&availability_sends.size()==1,"host receipt advances only on Send1");
            for(auto t:availability_attempts)Check(t.kind==GUEST_BLOCK_ACK&&t.generation==1&&t.tick==60,"receipt backpressure retains exact generation and source tick");
        } else if(mode=="host-availability-timeout") {
            availability_backpressure=100;ControlIn(GUEST_BLOCK,1,host_tick);CE_SessionSystemService(1,1);now_ms+=30000;
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED,"host receipt backpressure has bounded deadline");
        } else if(mode=="host-availability-invalid-first") {
            ControlIn(GUEST_READY,1,host_tick);
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED,"READY cannot precede BLOCK and held WAIT");
        } else if(mode.find("host-availability-invalid-")==0) {
            bool after=mode=="host-availability-invalid-stale"||mode=="host-availability-invalid-conflict"||mode=="host-availability-invalid-tickback";
            if(after)HostGuestBlocked();
            unsigned kind=mode=="host-availability-invalid-role"?GUEST_BLOCK_ACK:GUEST_BLOCK;
            uint32_t gen=mode=="host-availability-invalid-gap"?2:mode=="host-availability-invalid-zero"?0:1;
            uint32_t tick=mode=="host-availability-invalid-future"?61:mode=="host-availability-invalid-tickzero"?0:mode=="host-availability-invalid-tickback"?59:60;
            if(mode=="host-availability-invalid-stale") {ControlIn(GUEST_READY,2,60);CE_SessionSystemService(1,1);}
            if(mode=="host-availability-invalid-conflict")kind=GUEST_READY;
            ControlIn(kind,gen,tick,mode=="host-availability-invalid-schema"?3:kControlSchema);
            if(mode=="host-availability-invalid-epoch")peer->incoming.back().meta.epoch=1;
            if(mode=="host-availability-invalid-level")peer->incoming.back().bytes[15]=100;
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED,"invalid availability role epoch schema generation or tuple fails closed");
        } else {
            HostGuestBlocked();
            if(mode=="host-availability-or") {
                window_reasons=CE_SESSION_MINIMIZED;CE_SessionSystemService(0,1);
                ControlIn(GUEST_READY,2,host_tick);CE_SessionSystemService(0,1);
                Check(!s.peer_guest_blocked&&s.shared_wait&&s.wait_phase==WAIT_HELD,"host local block survives admitted guest recovery");
                window_reasons=0;
            } else if(mode=="host-availability-reblock") {
                auto_wait_ack=false;ControlIn(GUEST_READY,2,host_tick);CE_SessionSystemService(1,1);
                Check(s.wait_phase==RESUME_ACK&&s.shared_wait,"host keeps wait through RESUME handshake");
                ControlIn(GUEST_BLOCK,3,host_tick);ControlIn(HOST_RESUME_ACK,1,host_tick);
                CE_SessionSystemService(1,1);
                Check(s.shared_wait&&s.completed_wait==0&&rebased==r&&s.wait_generation==2,"admitted reblock fences old resume ACK before release");
                Check(s.peer_guest_ack_pending&&availability_sends.back().generation==2,"BLOCK receipt cannot attest the previous held wait during RESUME");
                ControlIn(HOST_WAIT_ACK,2,host_tick);CE_SessionSystemService(1,1);
                Check(!s.peer_guest_ack_pending&&availability_sends.back().generation==3&&availability_sends.back().kind==GUEST_BLOCK_ACK,
                      "reblock receipt follows the newly acknowledged WAIT");
                Check(captures==c&&applied==a&&sample_peeks==p,"availability service never captures applies or samples");Finish();return;
            }
            if(s.peer_guest_blocked)ControlIn(GUEST_READY,2,host_tick);
            CE_SessionSystemService(1,1);CE_SessionSystemService(1,1);
            Check(!s.shared_wait&&s.completed_wait==1&&rebased==r+1,"host resumes only after guest READY receipt and RESUME acknowledgement");
        }
    } else if(mode=="guest-availability-unsent-ack"||mode=="guest-availability-invalid-role"||mode=="guest-availability-no-request") {
        if(mode=="guest-availability-unsent-ack") {availability_backpressure=100;window_reasons=1;CE_SessionSystemService(0,1);}
        ControlIn(mode=="guest-availability-invalid-role"?GUEST_BLOCK:GUEST_BLOCK_ACK,1,60);
        Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED,"receipt requires matching role and an exact Send1 request");
    } else if(mode=="guest-availability-invalid-send") {
        availability_send_invalid=true;window_reasons=1;
        Check(CE_SessionSystemService(0,1)==CE_SESSION_EXIT&&s.stage==FAILED,"invalid availability request send fails closed");
    } else if(mode=="guest-availability-timeout"||mode=="guest-availability-backwards"||mode=="guest-availability-overflow") {
        if(mode=="guest-availability-overflow")s.guest_generation=UINT32_MAX;
        window_reasons=1;CE_SessionSystemService(0,1);
        if(mode=="guest-availability-timeout")now_ms+=30000;
        if(mode=="guest-availability-backwards")now_ms=99;
        Check(CE_SessionSystemService(0,1)==CE_SESSION_EXIT&&s.stage==FAILED,"guest availability timeout backwards clock or generation overflow fails closed");
    } else if(mode=="guest-availability-backpressure") {
        availability_backpressure=100;window_reasons=1;
        CE_SessionSystemService(0,1);Check(s.guest_request_pending&&!s.guest_request_queued,"guest retains BLOCK after Send0");
        window_reasons=0;host_tick=61;CE_SessionSystemService(1,1);
        Check(s.guest_request_blocked&&s.guest_generation==1&&s.guest_request_tick==60,"rapid recovery cannot replace unsent BLOCK bytes");
        availability_backpressure=0;CE_SessionSystemService(1,1);
        Check(availability_sends.size()==1&&s.guest_request_pending&&s.guest_request_queued,"accepted request waits for host receipt");
        for(auto t:availability_attempts)Check(t.kind==GUEST_BLOCK&&t.generation==1&&t.tick==60,"request backpressure retains exact generation and source tick");
        ControlIn(GUEST_BLOCK_ACK,1,60);CE_SessionSystemService(1,1);
        Check(s.guest_reported_blocked&&!s.guest_request_pending&&availability_sends.size()==1,"rapid recovery cannot send READY before shared WAIT");
        ControlIn(HOST_WAIT,1,61);CE_SessionSystemService(1,1);CE_SessionSystemService(1,1);
        Check(s.guest_generation==2&&s.guest_request_queued&&!s.guest_request_blocked,"READY follows queued WAIT acknowledgement");
    } else {
        GuestBlockHeld();
        if(mode=="guest-availability-device") {
            window_reasons=0;CE_SessionSystemService(1,2);CE_SessionSystemService(1,0);
            Check(s.device_unready&&s.guest_generation==1&&availability_sends.size()==1,"unknown device probe cannot erase guest device loss");
        } else if(mode=="guest-availability-close") {
            CE_SessionBeginTeardown(1);
            Check(!s.guest_request_pending&&!s.peer_guest_ack_pending&&s.stage==DRAINING,"teardown cancels availability receipts without releasing world fence");
            Check(captures==c&&applied==a&&sample_peeks==p,"availability service never captures applies or samples");Finish();return;
        }
        window_reasons=0;CE_SessionSystemService(1,1);
        Check(s.guest_generation==2&&s.guest_request_queued&&!s.guest_request_blocked,"guest recovery queues READY after host wait barrier");
        if(mode=="guest-availability-reblock") {
            // Reblock is read by Poll itself, before a queued READY receipt and RESUME can be consumed.
            window_reasons=CE_SESSION_MINIMIZED;ControlIn(GUEST_READY_ACK,2,60);ControlIn(HOST_RESUME,1,60);
            Check(!CE_SessionBeforeUpdate()&&s.shared_wait&&s.guest_request_blocked&&s.guest_generation==3,
                  "reblock after READY Send1 suppresses RESUME ACK before Poll releases wait");
            Check(rebased==r&&s.completed_wait==0,"cancelled READY cannot rebase or complete shared wait");
            window_reasons=0;CE_SessionSystemService(1,1);
            Check(s.guest_generation==3&&s.guest_request_blocked&&s.guest_request_pending,
                  "recovery against an old held WAIT still awaits the new BLOCK receipt");
            ControlIn(HOST_WAIT,2,60);CE_SessionSystemService(1,1);
            Check(s.guest_generation==3&&!s.wait_ack_pending,"new WAIT acknowledgement cannot replace the pending BLOCK receipt");
            ControlIn(GUEST_BLOCK_ACK,3,60);CE_SessionSystemService(1,1);
            Check(s.guest_generation==4&&s.guest_request_queued&&!s.guest_request_blocked,"new recovery uses a new immutable generation");
            ControlIn(GUEST_READY_ACK,4,60);ControlIn(HOST_RESUME,2,60);
        } else {ControlIn(GUEST_READY_ACK,2,60);ControlIn(HOST_RESUME,1,60);}
        CE_SessionSystemService(1,1);
        Check(!s.shared_wait&&!s.guest_request_pending&&!s.guest_reported_blocked&&rebased==r+1,
              "guest resumes once only after exact READY receipt and host RESUME");
    }
    Check(captures==c&&applied==a&&sample_peeks==p,"availability service never captures applies or samples");
    Finish();
}
static void SharedWaitCases(const std::string &mode,bool host) {
    if(mode.find("wait")==std::string::npos)return;
    GAME.mPause=true;
    unsigned before_captures=captures,before_applied=applied,before_samples=sampled,before_rebase=rebased;
    if(host) {
        auto_wait_ack=mode!="host-wait-noack" && mode!="host-wait-backpressure";
        window_reasons=CE_SESSION_UNFOCUSED;
        if(mode=="host-wait-backpressure")control_backpressure=2;
        Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT,"inactive host is shared-waiting");
        Check(s.shared_wait&&s.wait_generation==1,"host wait generation is latched");
        Check(!CE_SessionBeforeUpdate(),"shared wait fences native model Update");
        CE_SessionAfterFlush();CE_SessionBeforeRender();
        Check(captures==before_captures&&applied==before_applied&&sampled==before_samples,"wait service has no capture apply or fresh sampling");
        if(mode=="host-wait-noack") {
            now_ms+=30001;Check(CE_SessionSystemService(0,0)==CE_SESSION_EXIT&&GAME.mQuit==6,"missing wait acknowledgement exits within deadline");return;
        }
        if(mode=="host-wait-backpressure") {
            CE_SessionSystemService(0,0);Check(s.wait_phase==WAIT_ACK,"WAIT exact control survives send backpressure");
            for(unsigned n=1;n<=3;++n)InputIn(n,host_tick+1);
            ControlIn(HOST_WAIT_ACK,1,s.wait_tick);
            Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT&&s.wait_phase==WAIT_HELD,"control behind inputs is consumed while suspended");
            Check(s.remote_count==3,"shared wait retains all deferred immutable input batches");
        }
        if(mode=="host-wait-overflow") {
            for(unsigned n=1;n<=32;++n)InputIn(n,host_tick+1);
            Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT&&s.remote_count==32,"deferred FIFO holds exactly32 complete input batches");
            InputIn(33,host_tick+1);
            Check(CE_SessionSystemService(0,0)==CE_SESSION_EXIT&&s.remote_count==32&&GAME.mQuit==6,"next batch fails closed without overwriting retained FIFO");return;
        }
        Check(s.wait_phase==WAIT_HELD,"peer acknowledges wait without releasing simulation");
        for(unsigned n=0;n<4000;++n) {now_ms+=10;Check(CE_SessionSystemService(0,0)==CE_SESSION_WAIT,"healthy forty-second suspension keeps servicing transport");}
        Check(captures==before_captures&&applied==before_applied&&network_polls>=4000,"long wait services transport without world capture or apply");
        window_reasons=0;
        Check(CE_SessionSystemService(1,2)==CE_SESSION_WAIT,"device loss remains suspended after focus restore");
        Check(CE_SessionSystemService(1,0)==CE_SESSION_WAIT,"unknown device probe cannot clear observed loss");
        auto_wait_ack=mode!="host-wait-resume-noack";
        if(!auto_wait_ack) {
            Check(CE_SessionSystemService(1,1)==CE_SESSION_WAIT&&s.wait_phase==RESUME_ACK,"host does not run ahead of RESUME acknowledgement");
            now_ms+=30001;Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&GAME.mQuit==6,"missing resume acknowledgement has bounded deadline");return;
        }
        for(unsigned n=0;n<3&&s.shared_wait;++n)CE_SessionSystemService(1,1);
        Check(!s.shared_wait&&s.completed_wait==1&&rebased==before_rebase+1&&sampler_rebases==1,"resume requires acknowledgement and rebases exactly once");
        Check(CE_SessionBeforeUpdate(),"native model resumes after explicit peer acknowledgement");
        if(mode=="host-wait-backpressure")Check(queued_sequences==std::vector<uint32_t>({1,2,3}),"deferred inputs drain in exact original sequence order");
        ControlIn(HOST_WAIT_ACK,1,s.wait_tick);ControlIn(HOST_RESUME_ACK,1,s.wait_tick);
        Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&rebased==before_rebase+1,"completed controls are idempotent without rebasing again");
    } else {
        uint32_t frozen=applied_tick;
        if(mode=="guest-wait-gap"||mode=="guest-wait-resume-first") {
            ControlIn(mode=="guest-wait-gap"?HOST_WAIT:HOST_RESUME,mode=="guest-wait-gap"?2:1,frozen);
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&GAME.mQuit==6,"future generation and resume-before-wait fail closed");return;
        }
        if(mode=="guest-wait-lease") {FrameIn(CE_NET_STATE,applied_tick+1,30);send_backpressure=100;CE_SessionBeforeRender();Check(s.have_input,"guest retains a leased front before shared wait");frozen=applied_tick;}
        unsigned wait_applied=applied;
        ControlIn(HOST_WAIT,1,frozen);
        Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&s.shared_wait&&input_suspended==1,"guest acknowledges shared wait without requiring lost device");
        Check(!CE_SessionBeforeUpdate(),"guest shared wait stops fresh native input sampling");
        unsigned wait_samples=sampled;auto retained=attempted_input;
        for(unsigned n=0;n<20;++n) {now_ms+=100;CE_SessionSystemService(1,1);}
        Check(sampled==wait_samples&&applied==wait_applied,"idle service never applies buffered state or samples");
        if(mode=="guest-wait-lease")Check(s.have_input&&attempted_input==retained,"idle wait preserves exact outstanding lease bytes");
        ControlIn(HOST_WAIT,1,frozen);CE_SessionSystemService(1,1);
        Check(s.shared_wait,"duplicate current WAIT does not resume");
        ControlIn(HOST_RESUME,1,frozen);CE_SessionSystemService(1,1);
        Check(!s.shared_wait&&input_suspended==0&&rebased==before_rebase+1,"guest resumes only after queuing generation ACK and rebases once");
        if(mode=="guest-wait-lease") {send_backpressure=0;CE_SessionBeforeRender();Check(!s.have_input,"retained lease resumes normal send acknowledgement");}
        ControlIn(HOST_WAIT,1,frozen);ControlIn(HOST_RESUME,1,frozen);CE_SessionSystemService(1,1);
        Check(!s.shared_wait&&rebased==before_rebase+1,"completed duplicate host controls do not restart a wait");
    }
    Check(GAME.mPause,"shared suspension never clears a pre-existing manual game pause");
}
int main(int argc,char **argv){
    if(argc!=2)return 1;const std::string mode=argv[1];
    if(mode.rfind("start-handoff-",0)==0){StartHandoffCases(mode);printf("{\"case\":\"%s\",\"checks\":%u,\"block_stage\":%u,\"block_start_queued\":%d,\"block_start_applied\":%d,\"terminal_send_attempts\":%u,\"game_or_devices_tested\":false}\n",argv[1],checks,unsigned(handoff_block_stage),int(handoff_block_start_queued),int(handoff_block_start_applied),terminal_send_attempts);return 0;}
    if(mode.find("-availability-")!=std::string::npos){AvailabilityCases(mode);printf("{\"case\":\"%s\",\"checks\":%u,\"availability_attempts\":%zu,\"availability_sends\":%zu,\"game_or_devices_tested\":false}\n",argv[1],checks,availability_attempts.size(),availability_sends.size());return 0;}
    if(mode.rfind("init-",0)==0){InitFailureCases(mode);printf("{\"case\":\"%s\",\"checks\":%u,\"resource_touches\":%u,\"real_resources_or_game_tested\":false}\n",argv[1],checks,resource_touches);return 0;}
    if(mode.rfind("vm-fence-",0)==0){MissionFenceCases(mode);printf("{\"case\":\"%s\",\"checks\":%u,\"game_or_devices_tested\":false}\n",argv[1],checks);return 0;}
    if(mode.find("-birth-")!=std::string::npos){BirthCases(mode);printf("{\"case\":\"%s\",\"checks\":%u,\"birth_ack_attempts\":%u,\"birth_ack_sends\":%zu,\"world_ack_delegations\":%zu,\"game_or_devices_tested\":false}\n",argv[1],checks,birth_ack_attempts,birth_sends.size(),delegated_birth_acks.size());return 0;}
    if(mode=="offline") {Check(CE_SessionConfigureLevel(100),"offline configure");Check(CE_SessionAfterLoad(),"offline loaded");s.peer_epoch_loaded=true;
        Check(CE_SessionPreparePlay()&&pre_runs==1&&updates==3,"offline genuine PreRun");Check(CE_SessionBeforeUpdate(),"offline update");CE_SessionAfterFlush();CE_SessionBeforeRender();Check(!created&&!CE_SessionActive()&&!birth_getters&&!birth_ack_attempts,"offline has no transport or birth proof observations");}
    else {
        bool host=mode.rfind("guest",0)!=0;Configure(host);
        erase_loaded1=mode=="guest-epoch-erase"||mode=="guest-both-races";
        baseline_first=mode=="guest-baseline-first"||mode=="guest-both-races";
        if(mode=="both")setenv("BEA_NET_JOIN","127.0.0.1:27652",1);
        if(mode=="bad-endpoint")setenv("BEA_NET_HOST","localhost:1",1);
        if(mode=="bad-secret")setenv("BEA_NET_SECRET","test",1);
        if(mode=="virtual")virtual_clock=true;
        if(mode.rfind("disabled:",0)==0)setenv(mode.substr(9).c_str(),"1",1);
        bool invalid=mode=="both"||mode=="bad-endpoint"||mode=="bad-secret"||mode=="virtual"||mode=="wrong-level"||mode.rfind("disabled:",0)==0;
        if(invalid){Check(!CE_SessionConfigureLevel(mode=="wrong-level"?100:852)&&!created,"invalid admission creates no socket");CE_SessionAfterWorldShutdown();}
        else {
            Check(CE_SessionConfigureLevel(852)&&created==1,"explicit transport created");mute_peer=mode=="timeout";close_window=mode=="close-wait";corrupt_control=mode=="corrupt";early_input=mode=="early-input";incompatible=mode=="incompatible"||mode=="guest-incompatible";
            if(mode=="pump-budget"){flood_messages=true;Check(!CE_SessionGamePump()&&pumps==64&&dispatched==64,"actual message pump bounds a continuous message flood");flood_messages=false;}
            if(mute_peer||close_window||corrupt_control||incompatible){Check(!CE_SessionAfterLoad(),"barrier fails closed");Check(!pre_runs,"no PreRun before load admission");
                if(incompatible){char named[160];snprintf(named,sizeof named,"different Onslaught CE version: yours is %s, theirs is 0.1.9",kVersionLabel);
                    Check(strstr(CE_SessionError(),named)!=NULL,"refusal names both versions");
                    if(host)Check(s.refused_at&&now_ms-s.refused_at>=kRefusalFlushMs,"refusing host keeps polling so its refusal leaves");
                    else Check(!s.refused_at,"refused guest stops at once");}
                if(close_window)Check(CE_SessionError()[0]==0&&s.stage!=FAILED,"actual load-wait WM_QUIT cancels without manufacturing a peer failure");
                else Check(CE_SessionError()[0]!=0&&s.stage==FAILED,"real timeout or corrupted admission remains a failure");
                int abort=CE_SessionGameAbortLoad();Check(abort==(close_window?QT_QUIT_TO_SYSTEM:QT_LOAD_ERROR),"actual abort bridge preserves close versus genuine load failure");
                Check(GAME.mQuit==abort,"caller returns the preserved quit result");Finish();}
            else {
                if(mode=="local-close-load") {
                    LT.TriggerQuit();unsigned polled=network_polls;
                    Check(!CE_SessionAfterLoad()&&s.stage!=FAILED&&CE_SessionError()[0]==0,"genuine close before load barrier is not a network failure");
                    Check(network_polls==polled&&GAME.mQuit==1&&FRONTEND.quit==-1,"local load close preserves native quit owners without transport work");
                    Check(CE_SessionGameAbortLoad()==1,"actual abort bridge preserves genuine close quit");
                    Finish();printf("{\"case\":\"%s\",\"checks\":%u}\n",argv[1],checks);return 0;
                }
                Check(CE_SessionAfterLoad()&&begun==1&&s.peer_epoch_loaded,"both loaded and epoch admitted");
                if(!host){GAME.Render(0);Check(rendered==0,"actual native render entry refuses a pre-baseline replica");}
                if(host)host_tick=57;
                if(mode=="local-close-prerun") {
                    LT.TriggerQuit();unsigned polled=network_polls;
                    Check(!CE_SessionPreparePlay()&&s.stage!=FAILED&&pre_runs==0,"genuine local close prevents new PreRun or network admission");
                    Check(network_polls==polled&&CE_SessionError()[0]==0,"local pre-run close performs no transport work or fake failure");
                    Check(CE_SessionGameAbortLoad()==1,"actual pre-run abort keeps close quit");
                    Finish();printf("{\"case\":\"%s\",\"checks\":%u}\n",argv[1],checks);return 0;
                }
                if(mode=="publish-baseline-send0") {
                    world_send_backpressure=true;
                    Check(!CE_SessionPreparePlay()&&s.stage==FAILED,"baseline queue backpressure fails admission");
                    Check(publication_attempts==0&&s.baseline_sequence==0,"baseline Send0 never publishes or advances bookkeeping");
                    Finish();printf("{\"case\":\"%s\",\"checks\":%u}\n",argv[1],checks);return 0;
                }
                if(mode=="publish-baseline-refused") {
                    reject_publication=true;
                    Check(!CE_SessionPreparePlay()&&s.stage==FAILED,"baseline publication refusal stops admission");
                    Check(publication_attempts==1&&publications==0&&s.baseline_sequence==0,"refused publication precedes baseline bookkeeping");
                    Finish();printf("{\"case\":\"%s\",\"checks\":%u}\n",argv[1],checks);return 0;
                }
                Check(CE_SessionPreparePlay()&&s.stage==RUNNING&&rebased==1,"baseline applied and start acknowledged");
                Check(pre_runs==(host?1u:0u),"only host genuine PreRun");
                if(host){Check(host_tick==60,"host baseline follows actual PreRun updates");unsigned before=state_sends;
                    CE_SessionAfterFlush();Check(state_sends==before,"no duplicate Flush tick capture");++host_tick;CE_SessionAfterFlush();Check(state_sends==before+1,"new Flush tick captured");
                    now_ms+=50;CE_SessionBeforeRender();Check(state_sends==before+2,"same-tick periodic terminal snapshot");
                    if(early_input)Check(queued==1,"cross-channel early input retained after start was sent");
                    if(mode=="queue-backpressure-control") {
                        InputIn(1,host_tick+1);ControlIn(LEAVING);queue_backpressure=100;
                        Check(!CE_SessionBeforeUpdate()&&s.stage==DRAINING&&GAME.mQuit==1&&!s.remote_count&&!queued,"control behind a blocked input is consumed rather than starved");
                    }
                    if(mode=="queue-backpressure"){CE_WorldInput in={};in.epoch=2;in.tick=62;in.player=1;in.sequence=1;uint8_t data[1000];uint32_t n;CE_WorldEncodeInput(&in,data,sizeof data,&n);PacketIn(CE_NET_INPUT,62,data,n);queue_backpressure=1;
                        Check(CE_SessionBeforeUpdate()&&s.remote_count,"host retains popped backpressured input");Check(CE_SessionBeforeUpdate()&&!s.remote_count&&queued==1,"host retries exactly one pending input");}
                } else {Check(applied==1&&applied_tick==60,"guest baseline applied before start");send_backpressure=mode=="guest-backpressure"?2:0;
                    reject_input_ack=mode=="guest-ack-refused";
                    CE_SessionBeforeRender();
                    if(send_backpressure)Check(input_acks==0&&ack_attempts==0&&s.have_input,"backpressure does not pop a leased input");
                    CE_SessionBeforeRender();CE_SessionBeforeRender();Check(sampled==1&&input_sent==1,"one exact semantic batch per host tick despite render calls");
                    if(reject_input_ack)Check(s.stage==FAILED&&s.have_input&&input_acks==0&&ack_attempts==1,"refused lease ack stops session without silently clearing input");
                    else {
                        Check(input_acks==1&&ack_attempts==1,"successful transport enqueue pops one exact lease");
                        if(mode=="guest-render-poison") {
                            GAME.Render(0);Check(rendered==1,"actual native entry renders an admitted baseline");
                            poison_apply=true;FrameIn(CE_NET_STATE,61,2);GAME.Render(0);
                            Check(poisoned&&rendered==1&&s.stage==FAILED,"same native entry applies then refuses a poisoned incoming frame");
                        } else {FrameIn(CE_NET_STATE,61,2);CE_SessionBeforeRender();Check(applied==2,"replica applies next state before render");Check(CE_SessionBeforeUpdate(),"replica genuine Update may poll its local sampler");}
                    }}
                if(mode.find("local-close")!=std::string::npos) {
                    unsigned polled=network_polls,captured=captures,applied_before=applied,sampled_before=sampled,drawn=rendered;
                    LT.TriggerQuit();Check(GAME.mQuit==1&&FRONTEND.quit==-1,"actual native TriggerQuit owns the close before later model work");
                    peer->status.phase=CE_NET_CLOSED; // peer-close race after this process's local native close
                    if(mode=="local-close-before-update")Check(!CE_SessionBeforeUpdate(),"known local close fences the pending native Update");
                    else if(mode=="local-close-after-flush"){++host_tick;CE_SessionAfterFlush();}
                    else GAME.Render(0);
                    Check(network_polls==polled&&captures==captured&&applied==applied_before&&sampled==sampled_before&&rendered==drawn,"no poll capture apply sample or draw follows known local close");
                    Check(s.stage!=FAILED&&CE_SessionError()[0]==0&&GAME.mQuit==1,"already known close does not manufacture a connection failure");
                    Check(CE_SessionSystemService(0,2)==CE_SESSION_EXIT,"system service shares the same close-first order");
                }
                if(mode=="state-pacing"&&host) {
                    // Every image differs over 30 KB: twenty a second would be ~600 KiB/s, far over ENet's cap.
                    capture_bytes=30028;const unsigned sends0=state_sends,pubs0=publications;const uint64_t bytes0=state_bytes;
                    const uint64_t start=now_ms;unsigned frames=0;
                    for(;frames<40;++frames){++host_tick;++capture_seed;now_ms+=50;CE_SessionAfterFlush();Check(s.stage==RUNNING,"paced host keeps running");}
                    const unsigned sent=state_sends-sends0;const uint64_t bytes=state_bytes-bytes0;
                    Check(bytes<=kStateBurstBytes+kStateBytesPerSecond*(now_ms-start)/1000,"state bytes stay within the budget");
                    Check(sent>=8&&sent<frames,"large images are paced, not stopped");
                    Check(publications-pubs0==sent,"only images actually queued are published");
                    capture_bytes=28;
                }
                if(mode=="quiet-notice") {
                    Check(!CE_SessionNotice()[0],"a talking peer gives no notice");
                    quiet_ms=1999;Check(CE_SessionBeforeUpdate()&&!CE_SessionNotice()[0],"under two seconds of silence is ordinary");
                    quiet_ms=2000;Check(CE_SessionBeforeUpdate()&&!strcmp(CE_SessionNotice(),"Connection lost, waiting... (28 s)"),"silence is announced with the time left");
                    quiet_ms=24500;Check(CE_SessionBeforeUpdate()&&!strcmp(CE_SessionNotice(),"Connection lost, waiting... (6 s)"),"the countdown follows the silence");
                    quiet_ms=0;Check(CE_SessionBeforeUpdate()&&!CE_SessionNotice()[0]&&s.stage==RUNNING,"traffic again clears the notice");
                }
                if(mode=="peer-disconnected"||mode=="peer-leaving"||mode=="peer-malformed") {
                    if(mode=="peer-disconnected")peer->status.phase=CE_NET_CLOSED;
                    else if(mode=="peer-leaving")ControlIn(LEAVING);
                    else {corrupt_control=true;ControlIn(LEAVING);}
                    if(mode=="peer-leaving") {
                        Check(!CE_SessionBeforeUpdate()&&s.stage==DRAINING&&GAME.mQuit==1,"validated peer departure returns through native frontend quit");
                        Check(!CE_SessionError()[0]&&CE_SessionExitCode(17,0)==0,"graceful departure has no failure and normal process outcome");
                    }else {
                        Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&GAME.mQuit==6,"unexpected disconnect or malformed packet remains a real failure");
                        Check(CE_SessionError()[0]!=0&&CE_SessionExitCode(0,0)==1,"unexpected peer failure retains its diagnostic and error outcome");
                    }
                }
                if(mode=="publish-positive")Check(publications==captures&&publications>=3,"each accepted baseline/state enqueue publishes exactly once");
                if(mode=="publish-send0") {
                    unsigned published=publications,attempted=publication_attempts;
                    world_send_backpressure=true;++host_tick;CE_SessionAfterFlush();
                    Check(s.stage==RUNNING&&publications==published&&publication_attempts==attempted,"world Send0 cannot advance publication watermark");
                    world_send_backpressure=false;++host_tick;CE_SessionAfterFlush();
                    Check(publications==published+1&&publication_attempts==attempted+1,"next accepted frame is published exactly once");
                }
                if(mode=="publish-refused") {
                    unsigned published=publications,last_tick=s.last_capture_tick;
                    reject_publication=true;++host_tick;CE_SessionAfterFlush();
                    Check(s.stage==FAILED&&GAME.mQuit==6,"publication refusal fails the running session closed");
                    Check(publications==published&&s.last_capture_tick==last_tick,"refused publication changes no accepted-frame bookkeeping");
                }
                SharedWaitCases(mode,host);
                Check(CE_SessionBeginTeardown(4)==6&&world_live,"retry leaves session while keeping world fence");CE_SessionBeforeRender();world_live=false;CE_SessionAfterWorldShutdown();Check(!peer&&!s.requested&&ended==1,"fence clears only after world shutdown");
            }
        }
    }
    printf("{\"case\":\"%s\",\"checks\":%u,\"captures\":%u,\"input_attempts\":%u,\"queue_attempts\":%u,\"pumps\":%u,\"destroyed\":%u,\"game_or_devices_tested\":false}\n",
        argv[1],checks,captures,input_attempts,queue_attempts,pumps,destroyed);
    return 0;
}
