// SPDX-License-Identifier: GPL-3.0-only
// Actual Session plus the existing caller/transport boundary. World native effects are controlled here;
// a separate source-coupled fixture measures the actual World codec/readiness/application functions.
#define SESSION_LIFECYCLE_TEST
#define main existing_session_main
#include "network_session_driver.cpp"
#undef main
#include <map>
static unsigned life_starts, life_peeks, life_publications, life_applies, life_ack_delegations, life_ack_sends, readiness_calls;
static unsigned life_send0, life_ack_send0;
static bool life_publish_failure, life_apply_failure, life_start_failure, life_ack_failure, life_send_failure;
static bool life_render_boundary, life_future, life_invalid_frame;
static uint32_t life_committed, life_state_sequence;
static std::deque<CE_WorldLifecycleInfo> life_source;
static std::vector<CE_WorldLifecycleInfo> life_sends;
static std::vector<uint8_t> life_attempt;
static std::map<uint32_t,uint32_t> frame_watermarks;
static std::vector<CE_WorldLifecycleAck> life_receipts;
static CE_WorldLifecycleInfo Info(uint32_t operation,uint32_t tick=61) {
    return {2,1,operation,tick,operation,CE_WORLD_LIFECYCLE_BIRTH,100+operation,32};
}
static std::vector<uint8_t> LifeBytes(const CE_WorldLifecycleInfo &i) {
    uint8_t bytes[32];CE_NetWriter w;CE_NetWriterInit(&w,bytes,sizeof bytes);
    CE_NetWriteU32(&w,i.epoch);CE_NetWriteU32(&w,i.baseline_sequence);CE_NetWriteU32(&w,i.operation);CE_NetWriteU32(&w,i.source_tick);
    CE_NetWriteU32(&w,i.ordinal);CE_NetWriteU32(&w,i.kind);CE_NetWriteU32(&w,i.id);CE_NetWriteU32(&w,i.size);
    return std::vector<uint8_t>(bytes,bytes+sizeof bytes);
}
static CE_WorldLifecycleInfo LifeDecode(const uint8_t *bytes,uint32_t size) {
    CE_NetReader r;CE_NetReaderInit(&r,bytes,size);CE_WorldLifecycleInfo i={};
    i.epoch=CE_NetReadU32(&r);i.baseline_sequence=CE_NetReadU32(&r);i.operation=CE_NetReadU32(&r);i.source_tick=CE_NetReadU32(&r);
    i.ordinal=CE_NetReadU32(&r);i.kind=CE_NetReadU32(&r);i.id=CE_NetReadU32(&r);i.size=CE_NetReadU32(&r);
    if(!CE_NetReadDone(&r)||!i.operation||!i.source_tick||!i.id||i.size!=size)return {};
    return i;
}
static void LifeIn(CE_WorldLifecycleInfo info) {auto bytes=LifeBytes(info);PacketIn(CE_NET_LIFECYCLE,info.source_tick,bytes.data(),bytes.size());}
static void LifeAckIn(uint32_t operation,uint32_t tick=61,uint32_t baseline=1,uint32_t schema=5,int trim=0) {
    if(peer)peer->remote_life=operation; // explicit authored opposite whole-lifecycle-Apply boundary
    uint8_t bytes[29];CE_NetWriter w;CE_NetWriterInit(&w,bytes,sizeof bytes);
    CE_NetWriteU32(&w,kControlMagic);CE_NetWriteU32(&w,schema);CE_NetWriteU32(&w,LIFECYCLE_APPLIED);CE_NetWriteU32(&w,852);
    CE_NetWriteU32(&w,operation);CE_NetWriteU32(&w,tick);CE_NetWriteU32(&w,baseline);
    if(trim>0)CE_NetWriteU8(&w,0);PacketIn(CE_NET_CONTROL,tick,bytes,w.size-(trim<0?1:0));
}
static void LifeFrame(uint32_t watermark,uint32_t sequence=2,uint32_t tick=61) {
    frame_watermarks[sequence]=watermark;FrameIn(CE_NET_STATE,tick,sequence);
}
static void LifecycleFrameInfo(CE_WorldFrame *f) {
    if(f->info.frame_sequence!=1)f->info.lifecycle_baseline=1;
    f->info.lifecycle_watermark=frame_watermarks[f->info.frame_sequence];
}
static void LifecycleCaptureInfo(CE_WorldFrame *f) {
    if(s.lifecycle_started){f->info.lifecycle_baseline=1;f->info.lifecycle_watermark=life_publications+life_source.size();}
}
extern "C" int CE_WorldLifecycleStart(const CE_WorldFrame *f) {
    ++life_starts;Check(f->info.epoch==2&&f->info.frame_sequence==1&&f->info.tick==60,"Start uses the exact initial baseline");
    Check(s.host?(s.baseline_applied&&publications==1):(s.start_seen&&applied==1),"Start follows actual baseline publication/application and exact peer proof");
    return life_start_failure?-1:1;
}
extern "C" int CE_WorldPeekLifecycle(CE_WorldLifecycleInfo *out,uint8_t *bytes,uint32_t capacity) {
    ++life_peeks;if(life_source.empty())return 0;*out=life_source.front();auto encoded=LifeBytes(*out);
    Check(capacity>=encoded.size(),"bounded lifecycle source capacity");memcpy(bytes,encoded.data(),encoded.size());return 1;
}
extern "C" int CE_WorldLifecyclePublished(const CE_WorldLifecycleInfo *info) {
    Check(life_sends.size()==life_publications+1,"lifecycle Published occurs only after Send1");
    Check(!life_source.empty()&&LifecycleTuple(life_source.front(),{info->epoch,info->baseline_sequence,info->operation,info->source_tick}),"Published retains the exact immutable source front");
    if(life_publish_failure)return -1;++life_publications;life_source.pop_front();return 1;
}
extern "C" int CE_WorldInspectLifecycle(const uint8_t *bytes,uint32_t size,CE_WorldLifecycleInfo *out) {
    *out=LifeDecode(bytes,size);return out->operation?1:-1;
}
extern "C" int CE_WorldApplyLifecycle(const uint8_t *bytes,uint32_t size) {
    Check(life_render_boundary&&RuntimeReady(),"no lifecycle native effects from Poll or while unavailable");
    ++life_applies;auto info=LifeDecode(bytes,size);
    if(life_apply_failure){poisoned=true;return -1;}if(life_future)return 0;
    Check(info.operation==life_committed+1,"whole Apply commits only the next source operation");life_committed=info.operation;return 1;
}
extern "C" int CE_WorldAppliedLifecycle(CE_WorldLifecycleAck *out) {
    if(poisoned||!life_committed)return 0;*out={2,1,life_committed,61};return 1;
}
extern "C" int CE_WorldAcknowledgeLifecycle(const CE_WorldLifecycleAck *ack) {
    ++life_ack_delegations;life_receipts.push_back(*ack);return life_ack_failure?-1:1;
}
extern "C" int CE_WorldFrameReadiness(const CE_WorldFrame *f) {
    ++readiness_calls;Check(life_render_boundary&&RuntimeReady(),"readiness precedes native state effects at eligible render boundary");
    if(life_invalid_frame)return CE_WORLD_INVALID;
    if(f->info.lifecycle_watermark>life_committed)return CE_WORLD_DEFER;
    if(f->info.lifecycle_watermark<life_committed||f->info.frame_sequence<=life_state_sequence)return CE_WORLD_STALE;
    life_state_sequence=f->info.frame_sequence;return CE_WORLD_READY;
}
static int LifecycleSend(uint32_t kind,uint32_t tick,const uint8_t *data,uint32_t size) {
    if(kind==CE_NET_LIFECYCLE) {
        auto info=LifeDecode(data,size);Check(info.operation&&tick==info.source_tick,"lifecycle send exact envelope and bytes");
        std::vector<uint8_t> current(data,data+size);if(life_attempt.empty())life_attempt=current;
        else Check(life_attempt==current,"Send0 retains immutable lifecycle bytes");
        if(life_send_failure)return -1;if(life_send0){--life_send0;return 0;}
        life_sends.push_back(info);life_attempt.clear();return 1;
    }
    Check(size==28&&applied&&life_committed&&!poisoned,"ACK only follows whole successful lifecycle Apply");
    CE_NetReader r;CE_NetReaderInit(&r,data,size);for(unsigned i=0;i<4;++i)CE_NetReadU32(&r);
    uint32_t op=CE_NetReadU32(&r),source_tick=CE_NetReadU32(&r),baseline=CE_NetReadU32(&r);
    Check(op==life_committed&&source_tick==tick&&source_tick==61&&baseline==1,"ACK retains exact lifecycle operation source tick and baseline");
    if(life_ack_send0){--life_ack_send0;return 0;}++life_ack_sends;return 1;
}
static void Start(bool host) {
    Configure(host);Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"lifecycle session actual load barrier");
    if(host)host_tick=57;
    Check(CE_SessionPreparePlay()&&s.lifecycle_started&&life_starts==1,"explicit lifecycle Start completes before RUNNING");
    suppress_samples=true;life_state_sequence=1;
}
static void Render() {life_render_boundary=true;CE_SessionBeforeRender();life_render_boundary=false;}
static void HostSource(unsigned count) {for(unsigned i=0;i<count;++i)life_source.push_back(Info(i+1));host_tick=61;}
static void Cases(const std::string &mode) {
    bool host=mode.rfind("life-host-",0)==0;
    if(mode=="life-host-start-fail"||mode=="life-guest-start-fail") {
        life_start_failure=true;Configure(host);Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"load before Start refusal");if(host)host_tick=57;
        Check(!CE_SessionPreparePlay()&&s.stage==FAILED&&!s.lifecycle_started,"failed Start never enters RUNNING");return;
    }
    Start(host);
    if(mode=="life-host-start"||mode=="life-guest-start")return;
    if(host) {
        if(mode=="life-host-unsent-ack") {LifeAckIn(1);Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&!life_ack_delegations,"unpublished ACK never delegates");return;}
        HostSource(mode=="life-host-window"?64:2);
        if(mode=="life-host-source-gap") {life_source.front().operation=2;CE_SessionAfterFlush();Check(s.stage==FAILED&&life_sends.empty()&&!life_publications,"gapped source prefix refuses before transport publication");return;}
        if(mode=="life-host-front-deadline") {
            life_send0=100;CE_SessionAfterFlush();uint64_t since=now_ms;now_ms=since+29999;InputIn(1,61);
            Check(!CE_SessionBeforeUpdate()&&s.stage==RUNNING&&s.lifecycle_front,"pending front survives29999ms without granting credit");
            Check(CE_SessionSystemService(0,2)==CE_SESSION_WAIT&&s.lifecycle_front,"focus wait does not discard pending source obligation");
            now_ms=since+30000;Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&!life_publications,"pending front fails at30000ms without publication or native update");return;
        }
        if(mode=="life-host-send0") {
            life_send0=3;unsigned before=captures;CE_SessionAfterFlush();unsigned peeks=life_peeks;
            Check(s.lifecycle_front&&!life_publications&&!life_sends.size()&&captures==before,"Send0 never publishes or captures state");
            Check(!CE_SessionBeforeUpdate()&&life_peeks==peeks,"Send0 fences next native update without re-peeking mutable source");
            now_ms+=60;Render();Check(captures==before&&!life_publications,"periodic render capture cannot overtake Send0");
            Check(CE_SessionBeforeUpdate()&&life_publications==2,"queued prefix releases next native update");return;
        }
        life_publish_failure=mode=="life-host-publication-fail";life_send_failure=mode=="life-host-send-fail";
        if(mode=="life-host-ack-deadline"||mode=="life-host-late-ack"||mode=="life-host-late-bad-ack"||mode=="life-host-partial-deadline")world_send_backpressure=true;
        CE_SessionAfterFlush();
        if(life_publish_failure||life_send_failure){Check(s.stage==FAILED&&!life_ack_delegations,"send/publication failure stops before further native progress");return;}
        Check(life_publications==(mode=="life-host-window"?64u:2u)&&!life_ack_delegations,"Send1 publishes all exact source operations but never retires them");
        if(mode=="life-host-ack-deadline"||mode=="life-host-late-ack"||mode=="life-host-late-bad-ack"||mode=="life-host-partial-deadline") {
            uint64_t since=now_ms;now_ms=since+29999;InputIn(1,61);
            if(mode=="life-host-partial-deadline")LifeAckIn(1);
            Check(CE_SessionBeforeUpdate()&&s.stage==RUNNING&&s.lifecycle_count==(mode=="life-host-partial-deadline"?1u:2u),"published obligation survives29999ms without implicit retirement");
            Check(CE_SessionSystemService(0,2)==CE_SESSION_WAIT,"focus/shared wait keeps original lifecycle deadline");
            unsigned before=life_ack_delegations;now_ms=since+30000;
            if(mode=="life-host-late-ack"||mode=="life-host-late-bad-ack")LifeAckIn(2,61,mode=="life-host-late-bad-ack"?2:1);
            Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&life_ack_delegations==before&&s.lifecycle_count>0,"30000ms terminal validation cannot promote late application credit");
            Check(strstr(CE_SessionError(),mode=="life-host-late-bad-ack"?"admitted baseline":"timed out")!=nullptr,"late malformed proof is validated before deadline classification");return;
        }
        if(mode=="life-host-valid-terminal") {
            LifeAckIn(2);ControlIn(LEAVING);Check(!CE_SessionBeforeUpdate()&&s.stage==DRAINING&&life_ack_delegations==1,"valid ACK before explicit Leave retains causal application proof");return;
        }
        if(mode=="life-host-valid-closed") {
            LifeAckIn(2);peer->status.phase=CE_NET_CLOSED;peer->status.error=CE_NET_PEER_LEFT;
            Check(!CE_SessionBeforeUpdate()&&s.stage==DRAINING&&!life_ack_delegations&&s.lifecycle_count==2,"frozen terminal queue validates exact proof without application promotion");return;
        }
        if(mode=="life-host-window") {
            Check(!CE_SessionBeforeUpdate()&&s.lifecycle_count==64,"full unacknowledged window fences native update");now_ms+=1000;
            Check(!CE_SessionBeforeUpdate()&&s.lifecycle_count==64&&!life_ack_delegations,"time is never application credit");
            LifeAckIn(64);Check(CE_SessionBeforeUpdate()&&s.lifecycle_count==0&&life_ack_delegations==1,"actual cumulative ACK releases full retained prefix");return;
        }
        if(mode=="life-host-ack") {
            LifeAckIn(2);Check(CE_SessionBeforeUpdate()&&s.lifecycle_count==0&&s.lifecycle_acked==2&&life_ack_delegations==1,"exact cumulative application proof retires prefix");
            LifeAckIn(1);LifeAckIn(2);Check(CE_SessionBeforeUpdate()&&life_ack_delegations==1,"exact retired duplicate ACK is harmless and never re-delegated");return;
        }
        if(mode=="life-host-availability") {
            ControlIn(GUEST_BLOCK,1,60);LifeAckIn(2);Check(!CE_SessionBeforeUpdate()&&s.shared_wait&&life_ack_delegations==1,"ACK cannot override guest unavailable shared barrier");return;
        }
        if(mode=="life-host-retired-tick") {LifeAckIn(2);Check(CE_SessionBeforeUpdate(),"retire known prefix before altered duplicate");LifeAckIn(1,62);}
        else {
            uint32_t op=mode=="life-host-future-ack"?3:2,tick=mode=="life-host-wrong-tick"?62:61,base=mode=="life-host-wrong-baseline"?2:1;
            if(mode=="life-host-leave-first")ControlIn(LEAVING);
            LifeAckIn(op,tick,base,mode=="life-host-schema4"?4:5,mode=="life-host-short"?-1:mode=="life-host-long"?1:0);
            if(mode=="life-host-wrong-epoch")peer->incoming.back().meta.epoch=1;
            if(mode=="life-host-envelope")++peer->incoming.back().meta.tick;
            if(mode=="life-host-leave-last")ControlIn(LEAVING);
            if(mode=="life-host-terminal") {peer->status.phase=CE_NET_CLOSED;peer->status.error=CE_NET_PEER_LEFT;}
            if(mode=="life-host-leave-first"||mode=="life-host-leave-last"||mode=="life-host-terminal") {
                // The bad tuple must not be hidden by the graceful departure marker on either side.
                auto &packet=peer->incoming[mode=="life-host-leave-first"?1:0];packet.bytes[24]=2;
            }
            life_ack_failure=mode=="life-host-world-ack-fail";
        }
        unsigned before=life_ack_delegations;Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED,"bad lifecycle receipt fails closed including terminal queue");
        Check(life_ack_delegations==before+(life_ack_failure?1:0),"missing foreign or changed proof never reaches World ACK");
    } else {
        if(mode=="life-guest-wrong-role-ack") {LifeAckIn(1);Render();Check(s.stage==FAILED&&!life_ack_sends,"guest refuses host-originated lifecycle ACK");return;}
        if(mode=="life-guest-invalid-frame") {LifeIn(Info(1));LifeFrame(1);life_invalid_frame=true;Render();Check(s.stage==FAILED&&applied==1&&!life_applies,"invalid deferred state fails before any native effect");return;}
        if(mode=="life-guest-gap") {LifeIn(Info(2));Render();Check(s.stage==FAILED&&!life_applies&&!life_ack_sends,"reliable ordered lifecycle gap is never skipped");return;}
        if(mode=="life-guest-wrong-baseline"||mode=="life-guest-wrong-epoch"||mode=="life-guest-envelope"||mode=="life-guest-duplicate") {
            auto info=Info(1);if(mode=="life-guest-wrong-baseline")info.baseline_sequence=2;
            if(mode=="life-guest-wrong-epoch")info.epoch=1;LifeIn(info);if(mode=="life-guest-envelope")++peer->incoming.back().meta.tick;
            if(mode=="life-guest-duplicate")LifeIn(info);Render();Check(s.stage==FAILED&&!life_applies&&!life_ack_sends,"foreign envelope or repeated application packet fails before effects");return;
        }
        if(mode=="life-guest-state-first") {LifeFrame(1);Render();Check(s.have_frame&&applied==1&&readiness_calls==1,"STATE before lifecycle defers without clock or native Apply");}
        if(mode=="life-guest-lease") {
            suppress_samples=false;send_backpressure=100;Render();auto original=attempted_input;unsigned generated=sampled;
            Check(s.have_input&&leased_sequence==1&&!input_acks,"input lease predates lifecycle source packet");
            LifeIn(Info(1));LifeFrame(1);Render();
            Check(life_ack_sends==1&&s.have_input&&attempted_input==original&&sampled==generated&&!input_acks,"lifecycle ACK overtakes exact pending input without resampling or changing bytes");
            send_backpressure=0;Render();Check(!s.have_input&&input_acks==1,"old input receives only its own Send1 acknowledgement");return;
        }
        if(mode=="life-guest-coalesce") {
            LifeIn(Info(1));LifeIn(Info(2));LifeFrame(2);Render();
            Check(life_applies==2&&life_committed==2&&life_ack_sends==1&&applied==2&&!s.have_frame,"cumulative ACK proves whole ordered prefix before dependent latest state");return;
        }
        LifeIn(Info(1));if(mode!="life-guest-state-first")LifeFrame(1);
        if(mode=="life-guest-window-service") {Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&s.lifecycle_in_count==1&&s.have_frame&&!life_applies&&applied==1,"window service stages only and cannot apply either stream");}
        if(mode=="life-guest-availability"||mode=="life-guest-wait") {
            if(mode=="life-guest-availability")window_reasons=CE_SESSION_MINIMIZED;else ControlIn(HOST_WAIT,1,60);
            Render();Check(!life_applies&&applied==1&&!life_ack_sends&&s.have_frame&&s.lifecycle_in_count==1,"unavailable guest retains staged lifecycle and state without native effects");return;
        }
        life_apply_failure=mode=="life-guest-factory-fail";life_future=mode=="life-guest-apply-gap";life_ack_send0=mode=="life-guest-ack-send0"?2:0;
        Render();
        if(life_apply_failure||life_future){Check(s.stage==FAILED&&applied==1&&!life_ack_sends&&!s.lifecycle_ack.operation,"failed or uncommitted Apply grants no ACK or full state");return;}
        Check(life_applies==1&&life_committed==1&&applied==2&&!s.have_frame&&readiness_calls>0,"lifecycle application precedes dependent full STATE application");
        if(mode=="life-guest-ack-send0") {
            Check(!life_ack_sends&&s.lifecycle_ack.operation==1,"Send0 retains exact committed lifecycle proof");
            Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&!life_ack_sends&&life_applies==1,"service retry neither loses proof nor replays native effects");
            Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&life_ack_sends==1&&life_applies==1,"next Send1 queues retained ACK exactly once");return;
        }
        Check(life_ack_sends==1,"whole lifecycle Apply queues exact schema5 application proof");
        if(mode=="life-guest-stale") {LifeFrame(0,3);Render();Check(applied==2&&!s.have_frame,"validated obsolete watermark is dropped without resurrection");}
    }
}
int main(int argc,char **argv) {
    if(argc!=2)return 1;Cases(argv[1]);printf("{\"case\":\"%s\",\"checks\":%u,\"life_published\":%u,\"life_applied\":%u,\"life_ack_sends\":%u,\"world_acks\":%u,\"real_network_or_native_actors\":false}\n",argv[1],checks,life_publications,life_applies,life_ack_sends,life_ack_delegations);
    if(s.requested)Finish();return 0;
}
