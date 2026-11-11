// SPDX-License-Identifier: GPL-3.0-only
// Actual Session, delivery owners and codecs. Native World effects and transport enqueue are named boundaries.
#define main existing_session_main
#include "network_session_driver.cpp"
#undef main

static void DeliveryStart(bool host) {
    Configure(host); Check(CE_SessionConfigureLevel(852)&&CE_SessionAfterLoad(),"delivery load barrier");
    if(host)host_tick=57;
    Check(CE_SessionPreparePlay()&&s.stage==RUNNING,"delivery initial whole-Apply and START barrier");
    Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE,"ordinary ready service warms only configuration proof");
    suppress_samples=true;
}
static void ControlPacket(CE_StateDeliveryControl c) {
    uint8_t bytes[CE_STATE_DELIVERY_CONTROL_BYTES];
    Check(CE_StateDeliveryEncodeControl(&c,bytes,sizeof bytes)==CE_DELIVERY_OK,"authored canonical state control");
    PacketIn(CE_NET_STATE_CONTROL,c.key.tick,bytes,sizeof bytes);
}
static void HostRepairBegin() {
    capture_bytes=65536; capture_seed=1; ++host_tick;
    unsigned before=state_sends; CE_SessionAfterFlush();
    Check(s.repair_phase==REPAIR_QUIESCE&&state_sends==before,"oversized real delta requests repair without a raw fallback send");
    Check(!CE_SessionBeforeUpdate(),"repair quiescence blocks native update");
    CE_SessionBeforeRender();
    Check(s.repair_phase==REPAIR_WAIT&&s.shared_wait,"repair acquires shared WAIT only after existing state and lifecycle obligations drain");
    Check(CE_SessionSystemService(1,1)==CE_SESSION_NATIVE&&s.wait_phase==WAIT_HELD,"ready acknowledged repair routes render while native update stays fenced");
    Check(!CE_SessionBeforeUpdate(),"held repair never releases native update");
}
static void HostRepairFinish() {
    for(unsigned i=0;i<10&&s.repair_phase!=REPAIR_OFF&&s.stage==RUNNING;++i){
        CE_SessionSystemService(1,1);CE_SessionBeforeRender();
    }
    Check(s.stage==RUNNING&&s.repair_phase==REPAIR_OFF&&!s.shared_wait&&repair_raw_sends==1,
          "one complete raw repair and whole-Apply ACK precede acknowledged normal resume");
    Check(CE_SessionBeforeUpdate(),"native update resumes only after exact whole-Apply and resume proof");
}
struct GuestRepairData { std::vector<uint8_t> raw; CE_StateDeliveryControl announcement{}; CE_StateDeliveryTicket ticket{}; };
static GuestRepairData GuestRepairBuild(bool queue_wait=true) {
    CE_WorldFrame f{};f.info={};f.info.epoch=2;f.info.level=852;f.info.tick=host_tick+1; CE_StateKey basis{};
    Check(CE_StateSenderBasis(peer->remote_sender,&basis)==CE_DELIVERY_OK,"opposite applied basis exists");f.info.frame_sequence=basis.sequence+1;
    f.info.lifecycle_baseline=1; f.bytes=65536;f.seed=1;
    GuestRepairData d;d.raw.resize(65536);uint32_t n=0;
    Check(CE_WorldEncode(&f,d.raw.data(),d.raw.size(),&n)&&n==65536,"authored full-size raw World boundary");
    uint32_t tick=0;auto image=RemoteView(d.raw.data(),n,tick);uint8_t wire[65536];CE_StateDeliveryTicket ignored{};uint32_t size=0;
    Check(CE_StateSenderPrepare(peer->remote_sender,&image,tick,now_ms,wire,sizeof wire,&size,&ignored)==CE_DELIVERY_REPAIR_REQUIRED,
          "opposite real sender requires repair for oversized delta");
    CE_StateRepairGate gate{1,1,0};
    Check(CE_StateSenderPrepareRepair(peer->remote_sender,&image,tick,&gate,now_ms,&d.announcement,&d.ticket)==CE_DELIVERY_OK,
          "opposite actual repair owner prepares canonical announcement");
    CE_StateDeliveryControl queued{};
    Check(CE_StateSenderPoll(peer->remote_sender,now_ms,&queued)==CE_DELIVERY_OK&&
          CE_StateSenderControlQueued(peer->remote_sender,&queued,1,now_ms)==CE_DELIVERY_OK,"opposite announcement Send1");
    if(queue_wait){ControlIn(HOST_WAIT,1,tick);CE_SessionSystemService(1,1);
        Check(s.shared_wait&&!s.wait_ack_pending&&s.authorized_repair_tick==tick,"guest actually queues exact quiet WAIT acknowledgement");}
    return d;
}
static void GuestAnnouncement(GuestRepairData &d) {
    ControlPacket(d.announcement); unsigned before=applied;CE_SessionSystemService(1,1);
    Check(s.stage==RUNNING&&s.repair_phase==REPAIR_WAIT&&!s.repair_installed&&applied==before,
          "announcement Poll validates without installing, applying or emitting READY");
}
static void GuestRaw(GuestRepairData &d) {
    uint32_t tick=0;auto image=RemoteView(d.raw.data(),d.raw.size(),tick);
    Check(CE_StateSenderValidate(peer->remote_sender,&d.ticket,&image,tick)==CE_DELIVERY_OK,"opposite exact READY permits raw image");
    PacketIn(CE_NET_STATE_REPAIR,tick,d.raw.data(),d.raw.size());
    Check(CE_StateSenderSent(peer->remote_sender,&d.ticket,&image,tick,1,now_ms)==CE_DELIVERY_OK,"opposite reliable raw Send1");
}
static void DeliveryCases(const std::string &mode) {
    if(mode=="delivery-boot-missing"||mode=="delivery-compat-missing") {
        Configure(true);boot_ready=mode!="delivery-boot-missing";compatibility_ready=mode!="delivery-compat-missing";
        Check(!CE_SessionConfigureLevel(852)&&!created,"caller-supplied compatibility label never replaces prepared boot proof");Finish();return;
    }
    if(mode=="delivery-context-override") {
        Configure(true);setenv("BEA_NET_COMPAT","invalid caller label",1);
        Check(CE_SessionConfigureLevel(852)&&created==1,"prepared boot context remains the sole admission owner");
        uint8_t context[32];FixtureContext(context);Check(!memcmp(context,peer->context,32),"environment compatibility label cannot replace prepared content digest");Finish();return;
    }
    if(mode.find("-admission-before-")!=std::string::npos) {
        bool host=mode.find("-host-")!=std::string::npos;Configure(host);
        Check(CE_SessionConfigureLevel(852),"admission boundary starts with valid boot proof");
        if(mode.find("prerun")!=std::string::npos){Check(CE_SessionAfterLoad(),"world began before the later loaded-state refusal");loaded_ready=false;
            Check(!CE_SessionPreparePlay()&&s.stage==FAILED&&!pre_runs&&!applied&&!captures,"loaded-state refusal prevents genuine PreRun or baseline effects");}
        else {loaded_ready=false;Check(!CE_SessionAfterLoad()&&s.stage==FAILED&&!begun&&!pre_runs,"loaded-state refusal precedes actual WorldBegin");}
        Finish();return;
    }
    const bool host=mode.find("guest")==std::string::npos; DeliveryStart(host);
    std::vector<uint8_t> pending_bytes;uint32_t pending_sequence=0;unsigned pending_samples=0;
    if(mode=="delivery-admission-world"||mode=="delivery-admission-poll"||mode=="delivery-admission-render") {
        unsigned before=captures,before_apply=applied;compatibility_ready=false;
        if(mode=="delivery-admission-world"){++host_tick;CE_SessionAfterFlush();}
        else if(mode=="delivery-admission-poll")CE_SessionBeforeUpdate();else CE_SessionBeforeRender();
        Check(s.stage==FAILED&&captures==before&&applied==before_apply,"lost prepared content proof stops before any native state effect");Finish();return;
    }
    if(host) {
        if(mode=="delivery-host-state-timeout"||mode=="delivery-host-late-proof") {
            remote_auto_apply=false;++host_tick;CE_SessionAfterFlush();uint64_t since=s.state_pending_since;
            Check(s.state_pending,"ordinary state owns unacknowledged application obligation");now_ms=since+29999;
            Check(CE_SessionBeforeUpdate()&&s.state_pending,"ordinary pending application survives29999ms without premature credit");
            if(mode=="delivery-host-late-proof")remote_auto_apply=true;now_ms=since+30000;
            Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&s.state_pending,"exact30000ms cannot be rescued by late valid whole-Apply proof");Finish();return;
        }
        if(mode=="delivery-host-delta") {
            ++host_tick;CE_SessionAfterFlush();Check(state_sends==1&&s.state_pending,"ordinary CSD1 Send1 retains application obligation");
            CE_SessionSystemService(1,1);Check(!s.state_pending,"only exact opposite whole-Apply releases state credit");Finish();return;
        }
        HostRepairBegin();
        if(mode=="delivery-host-shell-prefix") {
            ++s.lifecycle_published;unsigned before=captures,before_apply=remote_applies;
            Check(CE_SessionSystemService(1,1)==CE_SESSION_EXIT&&s.stage==FAILED&&captures==before&&remote_applies==before_apply,
                  "shell refuses a changed held repair prefix before reaching render");Finish();return;
        }
        if(mode=="delivery-host-gate-freeze")Check(s.repair_gate.wait_generation==s.wait_generation&&RepairHeldGate(),
                  "exact WAIT acknowledgement freezes repair generation before shell routing");
        if(mode=="delivery-host-focus"||mode=="delivery-host-device"||mode=="delivery-host-peer") {
            unsigned before=captures,before_apply=remote_applies;
            if(mode=="delivery-host-focus")window_reasons=CE_SESSION_UNFOCUSED;
            if(mode=="delivery-host-peer"){ControlIn(GUEST_BLOCK,1,host_tick);CE_SessionSystemService(1,1);}
            int result=CE_SessionSystemService(1,mode=="delivery-host-device"?2:1);
            Check(result==CE_SESSION_WAIT,"unavailable repair cannot bypass native shell wait");
            CE_SessionBeforeRender();Check(captures==before&&remote_applies==before_apply&&!repair_raw_sends,"unavailable repair cannot capture or apply");Finish();return;
        }
        if(mode=="delivery-host-timeout"||mode=="delivery-host-backwards") {
            uint64_t since=s.repair_since;now_ms=mode=="delivery-host-timeout"?since+29999:since-1;
            if(mode=="delivery-host-backwards")mute_peer=true; // preserve opposite-owner clock; exercise actual local expiry
            if(mode=="delivery-host-timeout"){Check(!CE_SessionBeforeUpdate()&&s.stage==RUNNING,"repair survives29999ms without granting native update");now_ms=since+30000;}
            Check(!CE_SessionBeforeUpdate()&&s.stage==FAILED&&!repair_raw_sends,"repair exact deadline or backward time fails before effects");Finish();return;
        }
        if(mode=="delivery-host-quiet-changed") {
            s.lifecycle_published=1;CE_SessionBeforeRender();Check(s.stage==FAILED&&!repair_raw_sends,"repair refuses a changed lifecycle watermark");Finish();return;
        }
        if(mode=="delivery-host-announcement-send0")local_state_control_send0=2;
        if(mode=="delivery-host-no-ready")remote_control_send0=100;
        CE_SessionBeforeRender();
        Check(s.repair_phase==REPAIR_PREPARED,"host prepares exact repair behind held WAIT");
        if(mode=="delivery-host-announcement-send0") {
            unsigned captured=captures;CE_SessionBeforeRender();Check(s.repair_phase==REPAIR_PREPARED&&captures==captured&&!repair_raw_sends,
                "announcement Send0 preserves raw frame and emits no unannounced repair");
        }
        if(mode=="delivery-host-no-ready") {
            for(unsigned i=0;i<3;++i)CE_SessionBeforeRender();Check(s.stage==RUNNING&&!repair_raw_sends&&s.repair_phase==REPAIR_PREPARED,
                "raw repair cannot precede opposite installed and queued READY");Finish();return;
        }
        if(mode=="delivery-host-mutated-frame") {
            ++s.capture->birth;CE_SessionBeforeRender();Check(s.stage==FAILED&&!repair_raw_sends,"changed retained repair frame refuses before raw send");Finish();return;
        }
        if(mode=="delivery-host-raw-send0") {
            world_send_backpressure=true;CE_SessionBeforeRender();auto ticket=s.repair_ticket;unsigned captured=captures;
            Check(s.stage==RUNNING&&s.repair_phase==REPAIR_PREPARED&&!repair_raw_sends&&!s.state_pending,"raw Send0 neither publishes nor invents an application obligation");
            CE_SessionBeforeRender();Check(captures==captured&&s.repair_ticket.generation==ticket.generation,"raw Send0 retains exact immutable image and ticket");world_send_backpressure=false;
        }
        if(mode=="delivery-host-apply-pending")remote_auto_apply=false;
        CE_SessionBeforeRender();
        if(mode=="delivery-host-apply-pending") {
            Check(s.repair_phase==REPAIR_SENT&&s.state_pending&&s.shared_wait,"raw Send1 is not whole-Apply proof");
            Check(!CE_SessionBeforeUpdate()&&s.repair_phase==REPAIR_SENT&&s.shared_wait,"no native update or resume before whole-Apply");
            remote_auto_apply=true;
        }
        if(mode=="delivery-host-close") {
            unsigned before=repair_raw_sends;LT.TriggerQuit();CE_SessionBeforeRender();Check(repair_raw_sends==before,"local close prevents remaining repair effects");Finish();return;
        }
        HostRepairFinish();Finish();return;
    }
    if(mode=="delivery-guest-delta") {
        FrameIn(CE_NET_STATE,61,2);unsigned before=applied;CE_SessionSystemService(1,1);
        Check(applied==before&&s.state_prepared,"delta Poll never grants native Apply");CE_SessionBeforeRender();
        Check(applied==before+1&&!s.state_prepared,"ready render performs exact whole delta Apply");Finish();return;
    }
    if(mode=="delivery-guest-offer-hold") {
        // The first image after the baseline is the sender's basis offer; a newer ordinary image follows in the same
        // poll. The offer must reach whole Apply (and its basis ACK) instead of being replaced.
        FrameIn(CE_NET_STATE,61,2);FrameIn(CE_NET_STATE,62,3);unsigned before=applied;CE_SessionSystemService(1,1);
        Check(s.state_prepared&&s.state_ticket.offer_generation&&s.state_ticket.key.sequence==2,"newer ordinary image cannot replace a held offer");
        CE_SessionBeforeRender();Check(applied==before+1&&!s.state_prepared,"held offer reaches whole Apply");
        CE_SessionSystemService(1,1);CE_StateKey basis{};
        Check(CE_StateSenderBasis(peer->remote_sender,&basis)==CE_DELIVERY_OK&&basis.sequence==2,"applied offer's ACK advances the sender basis");
        Finish();return;
    }
    if(mode=="delivery-guest-offer-expiry") {
        // An offer that cannot reach Apply within the hold does not freeze the guest: a newer image replaces it.
        FrameIn(CE_NET_STATE,61,2);CE_SessionSystemService(1,1);
        Check(s.state_prepared&&s.state_ticket.offer_generation&&s.state_ticket.key.sequence==2,"offer prepared");
        now_ms+=300;FrameIn(CE_NET_STATE,62,3);CE_SessionSystemService(1,1);
        Check(s.state_prepared&&s.state_ticket.key.sequence==3,"expired offer hold lets a newer image replace it");
        CE_SessionBeforeRender();Check(!s.state_prepared,"newer image reaches whole Apply");Finish();return;
    }
    if(mode=="delivery-guest-pending-input") {
        suppress_samples=false;FrameIn(CE_NET_STATE,61,2);send_backpressure=100;CE_SessionBeforeRender();
        Check(s.have_input&&leased_sequence,"pending ordinary input lease precedes repair");
        pending_bytes=attempted_input;pending_sequence=leased_sequence;pending_samples=sample_peeks;
    }
    auto d=GuestRepairBuild(mode!="delivery-guest-no-wait");
    if(mode=="delivery-guest-no-wait") {
        ControlPacket(d.announcement);CE_SessionBeforeRender();Check(s.stage==FAILED&&!s.repair_installed,"announcement cannot invent acknowledged WAIT");Finish();return;
    }
    if(mode=="delivery-guest-wrong-generation"||mode=="delivery-guest-wrong-watermark"||mode=="delivery-guest-wrong-tick") {
        if(mode=="delivery-guest-wrong-generation")++d.announcement.gate.wait_generation;
        if(mode=="delivery-guest-wrong-watermark")++d.announcement.gate.lifecycle_watermark;
        if(mode=="delivery-guest-wrong-tick")++d.announcement.key.tick;
        ControlPacket(d.announcement);CE_SessionBeforeRender();Check(s.stage==FAILED&&!s.repair_installed,"foreign repair tuple refuses before installation");Finish();return;
    }
    if(mode=="delivery-guest-terminal"||mode=="delivery-guest-terminal-bad") {
        if(mode=="delivery-guest-terminal-bad")d.announcement.active_base.digest[0]^=1;
        ControlIn(LEAVING);ControlPacket(d.announcement);unsigned before=applied;CE_SessionSystemService(1,1);
        Check((mode=="delivery-guest-terminal"?s.stage==DRAINING:s.stage==FAILED)&&!s.repair_installed&&applied==before,
              "terminal repair validates without installing, applying or promoting READY");
        CE_StateDeliveryControl proof{};Check(CE_StateReceiverPoll(s.state_receiver,now_ms,&proof)==CE_DELIVERY_RETRY,
            "terminal repair cannot install a cache announcement or queue READY");Finish();return;
    }
    GuestAnnouncement(d);
    if(mode=="delivery-guest-focus"||mode=="delivery-guest-device") {
        if(mode=="delivery-guest-focus")window_reasons=CE_SESSION_UNFOCUSED;
        CE_SessionSystemService(1,mode=="delivery-guest-device"?2:1);CE_SessionBeforeRender();
        Check(!s.repair_installed&&!s.repair_ready_sent,"unavailable guest cannot install repair or emit READY");Finish();return;
    }
    if(mode=="delivery-guest-ready-send0") {
        local_state_control_send0=2;CE_SessionBeforeRender();
        Check(s.repair_installed&&!s.repair_ready_sent,"READY Send0 retains installed transaction without claiming receipt");
        PacketIn(CE_NET_STATE_REPAIR,d.announcement.key.tick,d.raw.data(),d.raw.size());unsigned before=applied;CE_SessionBeforeRender();
        Check(s.stage==FAILED&&applied==before,"unannounced raw repair refuses before native Apply");Finish();return;
    }
    CE_SessionBeforeRender();Check(s.repair_installed&&s.repair_ready_sent,"ready guest installs before queuing READY");
    if(mode=="delivery-guest-early-resume") {
        ControlIn(HOST_RESUME,1,d.announcement.key.tick);CE_SessionSystemService(1,1);Check(s.stage==FAILED,"resume cannot precede whole repair Apply ACK");Finish();return;
    }
    if(mode=="delivery-guest-timeout") {
        now_ms=s.repair_since+29999;CE_SessionBeforeUpdate();Check(s.stage==RUNNING,"guest repair survives29999ms without application credit");
        now_ms=s.repair_since+30000;CE_SessionBeforeUpdate();Check(s.stage==FAILED,"guest repair fails at exact30000ms");Finish();return;
    }
    GuestRaw(d);
    if(mode=="delivery-guest-wrong-size")--peer->incoming.back().meta.size;
    if(mode=="delivery-guest-corrupt-raw")peer->incoming.back().bytes.back()^=1;
    if(mode=="delivery-guest-apply-fail")poison_apply=true;
    if(mode=="delivery-guest-admission-after-apply")compatibility_lost_on_apply=true;
    unsigned before=applied;CE_SessionBeforeRender();
    if(mode=="delivery-guest-admission-after-apply"){
        Check(s.stage==FAILED&&applied==before+1&&!s.repair_ack_sent&&s.state_prepared,
            "lost content proof after native Apply grants no delivery ACK or resume credit");Finish();return;
    }
    bool bad=mode=="delivery-guest-wrong-size"||mode=="delivery-guest-corrupt-raw"||mode=="delivery-guest-apply-fail";
    if(bad){Check(s.stage==FAILED&&applied==before,"invalid or failed repair never grants whole-Apply credit");Finish();return;}
    Check(applied==before+1&&s.repair_phase==REPAIR_APPLIED&&s.repair_ack_sent&&s.shared_wait,"repair Apply ACK does not independently release shared WAIT");
    if(mode=="delivery-guest-duplicate-raw") {
        PacketIn(CE_NET_STATE_REPAIR,d.announcement.key.tick,d.raw.data(),d.raw.size());CE_SessionBeforeRender();
        Check(s.stage==RUNNING&&applied==before+1,"exact duplicate raw repair never repeats native Apply");
    }
    if(mode=="delivery-guest-pending-input")Check(s.have_input&&leased_sequence==pending_sequence&&attempted_input==pending_bytes&&sample_peeks==pending_samples,
        "repair preserves exact leased input bytes and sequence without sampling");
    ControlIn(HOST_RESUME,1,d.announcement.key.tick);CE_SessionSystemService(1,1);
    Check(s.stage==RUNNING&&s.repair_phase==REPAIR_OFF&&!s.shared_wait,"guest resumes only after whole Apply ACK and host RESUME");
    if(mode=="delivery-guest-pending-input"){
        send_backpressure=0;CE_SessionBeforeRender();
        Check(!s.have_input&&!leased_sequence&&input_sent==1,"resume queues and retires the original leased input exactly once");
    }
    Finish();
}
int main(int argc,char**argv) {
    if(argc!=2)return 1;DeliveryCases(argv[1]);
    printf("{\"case\":\"%s\",\"checks\":%u,\"raw_repairs\":%u,\"state_sends\":%u,\"whole_applies\":%u,\"game_or_devices_tested\":false}\n",argv[1],checks,repair_raw_sends,state_sends,applied);return 0;
}
