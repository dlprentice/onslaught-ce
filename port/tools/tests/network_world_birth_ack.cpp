// SPDX-License-Identifier: GPL-3.0-only
// Actual application-ACK, publication, input FIFO and final Apply-commit code at controlled clock/frame-validation
// boundaries. No actors, gameplay, transport, native Apply, event dispatch or renderer are constructed here.
#include CE_WORLD_HEADER
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
static unsigned checks;
static void Check(bool ok,const char *why) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(2); } }
constexpr unsigned kInputs=16,kName=96,kBirths=64;
#include NATIVE_TYPES
CE_WorldConfig config={};
bool active,poisoned,applying,creating_explosion,creating_round,life_enabled,life_proof_valid;
void *round_spawn=nullptr,*round_pending=nullptr;const void *applying_round=nullptr;
uint8_t life_baseline_bytes[65536];uint32_t life_baseline_size,life_proof_sequence,life_proof_tick;
uint32_t applied_tick,applied_sequence,replica_ack,birth_applied,birth_count,birth_generated,birth_published,birth_ack;
uint32_t captured_tick,captured_sequence,published_frame,published_tick;
ExplosionBirth birth_journal[kBirths]={};
const ExplosionBirth *applying_birth=nullptr; CExplosion *birth_spawn=nullptr;
char error_text[192]={};
struct ClockBoundary { uint32_t now=140; uint32_t GetFrameCount() const { return now; } } EVENT_MANAGER;
// Only the metadata read by FramePublished is supplied here. The real typed codec validates full frames separately.
struct CE_WorldFrame { uint32_t epoch,level,tick,sequence,birth_count,consumed_sequence; ExplosionBirth births[kBirths]; bool valid; };
struct CENetworkWorldAdapter { static bool Validate(const CE_WorldFrame &f) { return f.valid; } };
// Whole-frame encoding is a metadata-only boundary in this legacy ACK fixture; compact admission has its own gate.
int CE_WorldEncode(const CE_WorldFrame*f,uint8_t*out,uint32_t capacity,uint32_t*size){if(!f||!out||!size||!f->valid||capacity<4)return 0;out[0]=(uint8_t)(f->tick>>24);out[1]=(uint8_t)(f->tick>>16);out[2]=(uint8_t)(f->tick>>8);out[3]=(uint8_t)f->tick;*size=4;return 1;}
#include NATIVE_PROOF_HELPERS
#include NATIVE_HELPERS
#include NATIVE_LOCAL_INPUT
LocalInput local_input={};
#include NATIVE_INPUT_QUEUE
InputQueue commands={};
#include NATIVE_APPLY_SCOPE
#include NATIVE_REPLICA
#include NATIVE_GETTER
#include NATIVE_RETIRE
#include NATIVE_INPUT_ACK
#include NATIVE_ACK
#include NATIVE_QUEUE
#include NATIVE_PUBLISHED
static void Reset(uint32_t role,bool enabled=true) {
    config={role,7,role==CE_WORLD_REPLICA?1u:0u,852}; active=enabled; poisoned=applying=creating_explosion=creating_round=false;round_spawn=round_pending=nullptr;applying_round=nullptr;
    applied_tick=applied_sequence=replica_ack=birth_applied=birth_count=birth_generated=birth_published=birth_ack=0;
    captured_tick=published_frame=published_tick=0; captured_sequence=10;
    applying_birth=nullptr; birth_spawn=nullptr; EVENT_MANAGER.now=140;
    std::memset(birth_journal,0,sizeof birth_journal); std::memset(&commands,0,sizeof commands); std::memset(&local_input,0,sizeof local_input);
}
static void Journal(unsigned count) {
    birth_count=birth_generated=count;
    for(unsigned i=0;i<count;++i) { birth_journal[i]={}; birth_journal[i].serial=i+1; birth_journal[i].id=100+i; birth_journal[i].tick=138+i; }
}
static CE_WorldFrame Frame(unsigned count,uint32_t sequence=1,uint32_t tick=140) {
    CE_WorldFrame f={}; f.epoch=7; f.level=852; f.tick=tick; f.sequence=sequence; f.birth_count=count; f.valid=true;
    for(unsigned i=0;i<count;++i) f.births[i]=birth_journal[i];
    return f;
}
static void Unavailable(const char *why) {
    CE_WorldBirthAck out={91,92,93},before=out;
    Check(CE_WorldAppliedBirths(&out)==0&&!std::memcmp(&out,&before,sizeof out),why);
}
static bool FinishApply(bool commit,uint32_t serial) {
    ApplyScope scope;
    Unavailable("getter while applying cannot expose old or intermediate commit metadata");
    CE_WorldFrame f={}; f.tick=140; f.sequence=applied_sequence+1; f.consumed_sequence=4;
    uint32_t next_birth=serial;
    if(!commit) return false;
    // Extracted verbatim from the final successful Apply line; source-order checks reject earlier ACK writes.
#include NATIVE_APPLY_COMMIT
}
static CE_WorldInput Input(uint32_t sequence,uint32_t button) {
    CE_WorldInput in={}; in.epoch=7; in.player=1; in.sequence=sequence; in.tick=141; in.count=1;
    in.actions[0]={button,CE_WORLD_ACTION_BINDING,1.f}; return in;
}
int main() {
    static_assert(sizeof(CE_WorldBirthAck)==12,"three explicit C ABI words");
    Reset(CE_WORLD_REPLICA,false); Unavailable("offline getter leaves destination unchanged");
    Check(CE_WorldAcknowledgeBirths(7,0,140)==-1&&!birth_ack&&!birth_count,"offline ACK cannot change journal state");
    Reset(CE_WORLD_HOST); Unavailable("host cannot advertise replica Apply progress");
    Reset(CE_WORLD_REPLICA); Unavailable("unapplied incoming metadata cannot acknowledge a birth");
    applied_tick=140; birth_applied=2; Unavailable("tick without a successful Apply sequence is not a commit"); applied_tick=birth_applied=0;
    Check(!CE_WorldAppliedBirths(nullptr),"null getter destination is unavailable");
    Check(CE_WorldAcknowledgeBirths(7,0,140)==-1&&!birth_ack,"replica cannot retire host history");
    Check(FinishApply(true,0),"successful baseline commit without births");
    CE_WorldBirthAck out={}; Check(CE_WorldAppliedBirths(&out)==1&&out.epoch==7&&out.serial==0&&out.tick==140,"baseline serial zero is legitimate and needs no retirement");
    Check(FinishApply(true,2)&&CE_WorldAppliedBirths(&out)==1&&out.serial==2&&out.tick==140,"only final successful Apply commit exposes cumulative serial and tick");
    Check(!FinishApply(false,3)&&poisoned,"failed Apply scope is poisoned"); Unavailable("poisoned getter cannot ACK a failed Apply");

    Reset(CE_WORLD_HOST); Journal(3); captured_tick=140; CE_WorldFrame f=Frame(2);
    Check(!published_tick&&!birth_published,"capture metadata alone is not publication");
    f.tick=141; Check(!CE_WorldFramePublished(&f)&&!published_tick&&!birth_published,"publication beyond captured tick refuses without changing watermarks"); f.tick=140;
    f.valid=false; Check(!CE_WorldFramePublished(&f)&&!published_tick&&!birth_published,"failed frame validation does not publish ACK capacity"); f.valid=true;
    Check(CE_WorldFramePublished(&f)&&published_tick==140&&birth_published==2,"exact admitted publication exposes its tick and birth maximum");
    Check(CE_WorldAcknowledgeBirths(8,2,140)==-1&&birth_ack==0&&birth_count==3,"wrong epoch application ACK refuses unchanged");
    Check(CE_WorldAcknowledgeBirths(7,3,140)==-1&&birth_ack==0&&birth_count==3,"unpublished serial application ACK refuses unchanged");
    EVENT_MANAGER.now=142; Check(CE_WorldAcknowledgeBirths(7,2,141)==-1&&birth_ack==0,"ACK beyond published tick refuses even after authoritative time advances");
    EVENT_MANAGER.now=139; Check(CE_WorldAcknowledgeBirths(7,2,140)==-1&&birth_ack==0,"ACK beyond authoritative current tick refuses"); EVENT_MANAGER.now=140;
    Check(CE_WorldAcknowledgeBirths(7,2,0)==-1&&birth_ack==0,"zero tick is not a genuine post-PreRun baseline");
    Check(CE_WorldAcknowledgeBirths(7,2,138)==-1&&birth_ack==0,"fresh ACK cannot predate its retained birth");
    Check(CE_WorldAcknowledgeBirths(7,2,139)==1&&birth_ack==2&&birth_count==1&&birth_journal[0].serial==3&&!commands.count,"application ACK retires history without gameplay consumption");
    Check(CE_WorldAcknowledgeBirths(7,1,138)==1&&birth_ack==2&&birth_count==1,"older reordered serial and tick are idempotent without rollback");
    Check(CE_WorldAcknowledgeBirths(7,2,139)==1&&birth_ack==2&&birth_count==1,"duplicate cumulative ACK is idempotent");
    f=Frame(0,2,139); Check(CE_WorldFramePublished(&f)&&published_tick==140&&birth_published==2,"older publication cannot roll back maximum published tick or serial");

    Reset(CE_WORLD_HOST); Journal(3); captured_tick=140; f=Frame(3); Check(CE_WorldFramePublished(&f),"publish journal for input/control reordering");
    CE_WorldInput charge=Input(1,19),release=Input(2,18),leased={};
    Check(local_input.Poll(charge)&&local_input.Peek(leased)==1,"lease immutable pending input before an independent ACK"); CE_WorldInput original=leased;
    Check(CE_WorldQueueRemoteInput(&charge)==1&&commands.count==1&&!commands.consumed,"actual ordered queue admits first input without consuming it");
    Check(CE_WorldAcknowledgeBirths(7,2,140)==1&&birth_ack==2&&commands.count==1&&!commands.consumed,"CONTROL progress cannot consume queued gameplay");
    Check(local_input.Peek(leased)==1&&!std::memcmp(&leased,&original,sizeof leased)&&local_input.count==1&&local_input.leased,"independent ACK neither rewrites nor pops the immutable input lease");
    Check(CE_WorldQueueRemoteInput(&release)==1&&commands.count==2&&commands.accepted==2&&birth_ack==2&&birth_count==1,"fresh old-ACK input remains valid and cannot roll cumulative retirement back");
    Check(CE_WorldQueueRemoteInput(&charge)==1&&commands.count==2&&commands.accepted==2&&birth_ack==2,"duplicate old-ACK input remains idempotent");
    Check(CE_WorldAcknowledgeBirths(7,3,140)==1&&birth_count==0&&birth_ack==3&&commands.count==2,"control ACK can retire the final birth while inputs remain queued");
    CE_WorldInput queued={}; Check(commands.Peek(141,queued)&&queued.sequence==1&&queued.actions[0].button==19,"charge preserves original queue order"); commands.Consumed();
    Check(commands.Peek(142,queued)&&queued.sequence==2&&queued.actions[0].button==18,"release preserves original queue order"); commands.Consumed();
    Check(commands.consumed==2&&!commands.count&&local_input.count==1,"only actual queue consumption advances gameplay ACK");
    Check(local_input.Sent(99)==-1&&local_input.count==1&&local_input.Sent(1)==1&&!local_input.count,"only matching transport lease acknowledgement pops local input");
    CE_WorldInput again=Input(3,19); Check(CE_WorldQueueRemoteInput(&again)==1&&birth_ack==3&&!birth_count,"later fresh input carrying old serial zero stays admissible");

    Reset(CE_WORLD_HOST); Journal(kBirths); captured_tick=202; EVENT_MANAGER.now=202; f=Frame(kBirths,1,202);
    Check(CE_WorldFramePublished(&f)&&CE_WorldAcknowledgeBirths(7,kBirths,202)==1&&!birth_count&&birth_ack==kBirths&&!commands.count&&!local_input.count,"full journal can retire without any gameplay samples");
    Check(CE_WorldAcknowledgeBirths(7,0,140)==1&&birth_ack==kBirths,"old baseline ACK is a cumulative no-op");
    std::printf("{\"checks\":%u,\"native_apply_tested\":false,\"gameplay_tested\":false}\n",checks);
}
