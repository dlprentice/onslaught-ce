// SPDX-License-Identifier: GPL-3.0-only
// Opposite-peer delivery owners use the real codec/cache. Their whole-World-Apply callback, admission,
// window/device and transport remain explicit test boundaries; no game or file-layer admission is claimed.
static bool boot_ready=true,compatibility_ready=true,loaded_ready=true,boot_requested_override=false;
static bool compatibility_lost_on_apply=false;
static unsigned admission_copies,admission_boundaries,loaded_checks,admission_retirements;
static unsigned boundary_refuse_at,boot_refusals;
static int boundary_quit_on_refusal;
static bool remote_auto_apply=true,remote_controls_invalid=false;
static unsigned remote_control_send0,local_state_control_send0,remote_applies,repair_raw_sends;
static bool RemoteGateEqual(const CE_StateRepairGate&a,const CE_StateRepairGate&b){return a.wait_generation==b.wait_generation&&a.lifecycle_baseline==b.lifecycle_baseline&&a.lifecycle_watermark==b.lifecycle_watermark;}
static void FixtureContext(uint8_t out[32]) {
    Check(Hex("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",out),"authored admission context parses");
}
extern "C" int CE_BootRequested(){return boot_requested_override||EnvPresent("BEA_NET_HOST")||EnvPresent("BEA_NET_JOIN");}
extern "C" int CE_BootPrepared(CE_CompatibilityResult*out,char*error,size_t capacity){
    if(!boot_ready){*out={};if(capacity)snprintf(error,capacity,"controlled missing early boot");return 0;}
    return CE_CompatibilityPrepared(out,error,capacity);
}
extern "C" int CE_CompatibilityPrepared(CE_CompatibilityResult*out,char*error,size_t capacity){
    ++admission_copies;*out={};if(!compatibility_ready){if(capacity)snprintf(error,capacity,"controlled sticky refusal");return 0;}
    FixtureContext(out->digest);return 1;
}
extern "C" int CE_CompatibilityCheckBoundary(char*error,size_t capacity){
    ++admission_boundaries;
    if(boundary_refuse_at&&admission_boundaries==boundary_refuse_at){
        compatibility_ready=false;if(boundary_quit_on_refusal)CE_SessionGameSetQuit(boundary_quit_on_refusal);
        if(capacity)snprintf(error,capacity,"controlled boundary change");return 0;
    }
    CE_CompatibilityResult r{};return CE_CompatibilityPrepared(&r,error,capacity);
}
extern "C" int CE_CompatibilityRuntimeMutation(char*,size_t){++boot_refusals;compatibility_ready=false;return 0;}
extern "C" void CE_CompatibilityRetire(){++admission_retirements;Check(!world_live,"admission retirement follows native world teardown");}
extern "C" int CE_NetLoadedAdmissionValidate(){++loaded_checks;return loaded_ready?1:0;}
extern "C" const char*CE_NetLoadedAdmissionError(){return "controlled loaded-world refusal";}
static void RemoteOwners(bool local_host){
    if(local_host)peer->remote_receiver=CE_StateReceiverCreate(peer->context,2);
    else peer->remote_sender=CE_StateSenderCreate(peer->context,2);
    Check(local_host?peer->remote_receiver!=nullptr:peer->remote_sender!=nullptr,"opposite peer actual fixed-size delivery owner");
}
static CE_StateImage RemoteView(const uint8_t*bytes,uint32_t size,uint32_t &tick){
    CE_NetReader r;CE_NetReaderInit(&r,bytes,size);CE_StateImage image{};memcpy(image.identity.context,peer->context,32);
    image.identity.epoch=CE_NetReadU32(&r);tick=CE_NetReadU32(&r);image.identity.sequence=CE_NetReadU32(&r);
    Check(!r.error&&image.identity.epoch==2&&image.identity.sequence&&tick,"controlled World header belongs to opposite peer image");image.data=bytes;image.size=size;return image;
}
static void RemoteStart(){
    if(peer&&peer->remote_sender&&peer->remote_baseline){CE_StateKey basis{};if(CE_StateSenderBasis(peer->remote_sender,&basis)==CE_DELIVERY_OK){ControlIn(START,basis.sequence,basis.tick);peer->remote_baseline=false;}}
}
static void RemotePump();
static bool RemoteApply(){
    if(!peer||!peer->remote_receiver||!peer->remote_prepared||!remote_auto_apply)return false;
    if(peer->remote_wait&&!peer->remote_ticket.repair)return false;
    CE_StateImage image{};Check(CE_StateReceiverImage(peer->remote_receiver,&peer->remote_ticket,&image)==CE_DELIVERY_OK,"opposite peer surviving ticket image");
    CE_NetReader r;CE_NetReaderInit(&r,image.data,image.size);uint32_t epoch=CE_NetReadU32(&r),tick=CE_NetReadU32(&r),sequence=CE_NetReadU32(&r),level=CE_NetReadU32(&r);
    CE_NetReadU32(&r);uint32_t baseline=CE_NetReadU32(&r),watermark=CE_NetReadU32(&r);
    Check(!r.error&&epoch==2&&level==852&&sequence==peer->remote_ticket.key.sequence&&tick==peer->remote_ticket.key.tick,"controlled opposite whole-Apply validates actual delivery/header identity");
    if(watermark>peer->remote_life)return false;
    if(peer->remote_ticket.repair)Check(peer->remote_wait&&peer->remote_ready_sent&&baseline==peer->remote_gate.lifecycle_baseline&&watermark==peer->remote_gate.lifecycle_watermark&&tick==peer->remote_tick,"opposite repair whole-Apply uses exact held quiet gate");
    // This is the named controlled native boundary. Decode/Send1 itself never grants application credit.
    ++remote_applies;Check(CE_StateReceiverApplied(peer->remote_receiver,&peer->remote_ticket,epoch,sequence,tick,now_ms)==CE_DELIVERY_OK,"opposite whole success then actual delivery Applied");
    peer->remote_prepared=false;return true;
}
static void RemotePump(){
    if(!peer||peer->status.phase!=CE_NET_ACTIVE||!peer->remote_receiver)return;
    RemoteApply();CE_StateDeliveryControl c{};int p=CE_StateReceiverPoll(peer->remote_receiver,now_ms,&c);
    if(p==CE_DELIVERY_RETRY)return;Check(p==CE_DELIVERY_OK,"opposite receiver bounded control service");
    int sent=remote_control_send0?0:1;if(remote_control_send0)--remote_control_send0;
    Check(CE_StateReceiverControlQueued(peer->remote_receiver,&c,sent,now_ms)==(sent?CE_DELIVERY_OK:CE_DELIVERY_RETRY),"opposite control Send0 retains actual proof");
    if(sent){uint8_t bytes[184];Check(CE_StateDeliveryEncodeControl(&c,bytes,sizeof bytes)==CE_DELIVERY_OK,"opposite canonical control");PacketIn(CE_NET_STATE_CONTROL,c.key.tick,bytes,sizeof bytes);if(c.kind==CE_DELIVERY_REPAIR_READY)peer->remote_ready_sent=true;}
}
static int RemoteControlSend(uint32_t tick,const uint8_t*bytes,uint32_t size){
    CE_StateDeliveryControl c{};Check(size==184&&CE_StateDeliveryDecodeControl(bytes,size,&c)==CE_DELIVERY_OK&&c.key.tick==tick,"actual Session fixed184 canonical control and envelope");
    if(remote_controls_invalid)return -1;if(local_state_control_send0){--local_state_control_send0;return 0;}
    if(peer->remote_sender){int r=CE_StateSenderControl(peer->remote_sender,&c,now_ms,0);Check(r==CE_DELIVERY_OK||r==CE_DELIVERY_STALE||r==CE_DELIVERY_REPAIR_REQUIRED,"opposite actual sender admits whole-Apply/control proof");RemoteStart();}
    else if(c.kind==CE_DELIVERY_REPAIR){
        Check(peer->remote_wait&&RemoteGateEqual(c.gate,peer->remote_gate),"opposite announcement cannot invent its WAIT/prefix");
        Check(CE_StateReceiverCheckRepair(peer->remote_receiver,&c,&peer->remote_gate,now_ms)==CE_DELIVERY_OK&&CE_StateReceiverExpectRepair(peer->remote_receiver,&c,&peer->remote_gate,now_ms)==CE_DELIVERY_OK,"opposite receiver installs before READY");peer->remote_announcement=c;RemotePump();
    }else{int r=CE_StateReceiverControl(peer->remote_receiver,&c,now_ms,0);Check(r==CE_DELIVERY_OK||r==CE_DELIVERY_STALE,"opposite actual receiver status handling");RemotePump();}
    return 1;
}
static void RemoteBaseline(const uint8_t*bytes,uint32_t size,uint32_t tick){
    uint32_t header_tick=0;auto image=RemoteView(bytes,size,header_tick);Check(header_tick==tick,"opposite baseline exact source tick");
    ++remote_applies;Check(CE_StateReceiverBaselineApplied(peer->remote_receiver,&image,tick,now_ms)==CE_DELIVERY_OK,"controlled opposite whole baseline Apply then actual exact digest ACK");RemotePump();
}
static void RemoteState(const uint8_t*bytes,uint32_t size,uint32_t tick,bool repair){
    if(repair){++repair_raw_sends;Check(peer->remote_ready_sent&&peer->remote_wait,"raw repair follows opposite installed/queued READY");}
    CE_StateDeliveryTicket ticket{};int r=repair?CE_StateReceiverPrepareRepair(peer->remote_receiver,bytes,size,now_ms,&ticket):CE_StateReceiverPrepare(peer->remote_receiver,bytes,size,now_ms,&ticket);
    Check(r==CE_DELIVERY_OK||r==CE_DELIVERY_STALE||r==CE_DELIVERY_RETRY||r==CE_DELIVERY_REPAIR_REQUIRED,"opposite real delta/repair preparation");
    if(r==CE_DELIVERY_OK){Check(ticket.key.tick==tick,"opposite prepared key retains source tick");peer->remote_ticket=ticket;peer->remote_prepared=true;}
    RemotePump();
}
static void FrameIn(uint32_t kind,uint32_t tick,uint32_t sequence,uint32_t birth){
    CE_WorldFrame f{};f.info={};f.info.epoch=2;f.info.tick=tick;f.info.frame_sequence=sequence;f.info.level=852;f.birth=birth;
#ifdef SESSION_LIFECYCLE_TEST
    LifecycleFrameInfo(&f);
#else
    if(sequence!=1)f.info.lifecycle_baseline=1;
#endif
    uint8_t raw[65536];uint32_t n=0;Check(CE_WorldEncode(&f,raw,sizeof raw,&n),"authored opposite World encoder");uint32_t header_tick=0;auto image=RemoteView(raw,n,header_tick);
    if(kind==CE_NET_BASELINE){Check(CE_StateSenderBaselineSent(peer->remote_sender,&image,tick,1,now_ms)==CE_DELIVERY_OK,"opposite reliable baseline Send1 only seeds pending proof");PacketIn(kind,tick,raw,n);return;}
    uint8_t wire[65536];uint32_t size=0;CE_StateDeliveryTicket ticket{};
    Check(CE_StateSenderPrepare(peer->remote_sender,&image,tick,now_ms,wire,sizeof wire,&size,&ticket)==CE_DELIVERY_OK&&CE_StateSenderValidate(peer->remote_sender,&ticket,&image,tick)==CE_DELIVERY_OK,"opposite actual delta preparation and pre-Send validation");
    Check(CE_StateSenderSent(peer->remote_sender,&ticket,&image,tick,1,now_ms)==CE_DELIVERY_OK,"opposite actual delta Send1");PacketIn(kind,tick,wire,size);
}
