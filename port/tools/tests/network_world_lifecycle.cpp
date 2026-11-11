// SPDX-License-Identifier: GPL-3.0-only
// Actual World record codecs/queue at authored scalar boundaries. No native Init, clocks, effects or actors.
#define main existing_world_codec_main
#include "network_world_driver.cpp"
#undef main
static LifeRecord Birth(uint32_t operation,uint32_t id,uint32_t tick=141) {
    LifeRecord r={}; r.info={7,1,operation,tick,1,CE_WORLD_LIFECYCLE_BIRTH,id,0}; r.birth_serial=(operation+1)/2;
    r.definition=78; r.definition_schema=1234; Name(r.definition_name,"Mech Pulse Hit Large");
    ExplosionBirth &b=r.birth; b.serial=r.birth_serial; b.id=id; b.tick=tick; b.definition=r.definition; b.schema=r.definition_schema;
    Name(b.name,r.definition_name); b.ready=1; b.orientation_type=EULER_ANGLES; b.orientation={{1,0,0,0,1,0,0,0,1}};
    b.pos={{1,2,3}}; b.radius=1; b.allegiance=2; b.mask=THING_TYPE_EXPLOSION; b.map_who=1;
    r.core.id=id; r.core.kind=EXPLOSION_THING; r.core.type=THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_EXPLOSION|THING_TYPE_CAN_DESTROY_TREES; r.core.flags=TF_IN_MAP_WHO|TF_DECLARED_SHUTDOWN;
    Name(r.core.class_name,"CExplosion"); r.core.pos=r.core.old_pos=b.pos; r.core.orientation=r.core.old_orientation=b.orientation;
    r.explosion={b.serial,b.definition,0,0,b.allegiance,b.schema,b.radius,b.radius}; return r;
}
static LifeRecord RoundEvent(uint32_t operation,uint32_t id,const LifeKey &key) {
    LifeRecord r={};r.info={7,1,operation,141,1,CE_WORLD_LIFECYCLE_ROUND_BIRTH,id,0};r.birth_serial=operation;
    Core &c=r.core;c.id=id;c.kind=ROUND_THING;c.type=THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_ACTOR|THING_TYPE_AMMUNITION;c.flags=TF_IN_MAP_WHO;
    Name(c.class_name,"CRound");c.pos=c.old_pos={{1,2,3}};c.velocity={{0,1.75f,0}};c.orientation=c.old_orientation={{1,0,0,0,1,0,0,0,1}};c.last_move=5.f;
    RoundState &round=r.round;Name(round.definition,"Mech Pulse Bolt Medium");round.incarnation=operation;round.schema=123;round.launcher=key.id;round.allegiance=2;round.start=5.f;round.destination={{9,8,7}};
    RoundBirth &b=r.round_birth;b.shot=1;b.schema=123;b.mode_schema=234;b.actor_phase=1;b.allegiance=2;Name(b.weapon,"Pulse Cannon Pod");Name(b.mode,"Mech Pulse Cannon Charged");
    b.pos=c.pos;b.velocity=c.velocity;b.orientation=c.orientation;b.destination=round.destination;b.lifespan=6;r.dependency=key;return r;
}
static void RoundCodecs(const LifeKey &key) {
    uint8_t bytes[1001],again[1000];uint32_t size=0,n=0;LifeRecord round=RoundEvent(1,100,key),copy={};
    Check(EncodeLife(round,bytes,size)&&size<=1000,"complete round birth fits one packet");unsigned birth_size=size;
    Check(DecodeLife(bytes,size,copy)&&EncodeLife(copy,again,n)&&n==size&&!memcmp(bytes,again,size),"round entry state ShotID and post Init canonical roundtrip");
    bytes[7]=1;Check(!DecodeLife(bytes,size,copy),"old lifecycle1 packet refuses without coercion");bytes[7]=2;
    for(unsigned cut=0;cut<size;++cut)Check(!DecodeLife(bytes,cut,copy),"every round record truncation refuses before native resources");
    bytes[size]=0;Check(!DecodeLife(bytes,size+1,copy),"round record trailing bytes refuse");
    LifeRecord bad=round;bad.core.old_pos.v[0]=2;Check(!EncodeLife(bad,bytes,n),"impossible selected pre-clamp old position refuses");
    bad=round;bad.round.incarnation=2;Check(!EncodeLife(bad,bytes,n),"round incarnation must name its exact birth operation");
    bad=round;bad.round_birth.shot=0;Check(!EncodeLife(bad,bytes,n),"round birth needs nonzero source shot context");
    bad=round;bad.round_birth.actor_phase=0;Check(!EncodeLife(bad,bytes,n),"pre AddMove phase is not accepted as initialized state");
    bad=round;bad.round.target=key.id;Check(!EncodeLife(bad,bytes,n),"unadmitted selected seeking target refuses");
    bad=round;Name(bad.core.class_name,"CMissile");Check(!EncodeLife(bad,bytes,n),"missile class cannot hide in pulse capsule");
    round.info.kind=CE_WORLD_LIFECYCLE_ROUND_DESPAWN;round.info.operation=2;round.core.pos.v[0]=5;
    Check(EncodeLife(round,bytes,size)&&DecodeLife(bytes,size,copy)&&copy.core.pos.v[0]==5&&copy.round.incarnation==1,"explicit round endpoint keeps source incarnation after motion");
    printf("ROUND_CODEC_SIZES birth=%u despawn=%u bytes\n",birth_size,size);
}
// Authored virtual-time service model, not an ENet/World-Apply observation. Actual immutable serializer/queue/
// publication/retirement code surrounds an explicitly modeled successful sink commit and cumulative ACK.
static void ServiceModel(unsigned rtt,unsigned stall,bool expect_overflow) {
    LifeReset();active=true;config={CE_WORLD_HOST,7,0,852};life_enabled=true;life_baseline=1;life_baseline_tick=life_tick=100;
    CE_WorldFrame *modelBase=Fixture();LifeKey key={};Check(LifeKeyFor(modelBase->entities[1],key),"model exact encoded baseline dependency");CE_WorldFrameDestroy(modelBase);life_keys[0]=key;life_key_count=1;
    struct Packet { bool used,arrived,committed;uint32_t delivered,acknowledged,wire;CE_WorldLifecycleInfo info;uint8_t bytes[1000]; } packets[64]={};
    unsigned maximum=0,send0=0,produced=0,received=0,wire_inflight=0,budget=0;bool overflow=false;uint32_t receiver=0;unsigned born=0;
    uint8_t bytes[1000],peek[1000];CE_WorldLifecycleInfo info{},again{};
    // 20 births/s + their next-tick despawns; modeled transport service32000B/s,16KiB reliable window,
    // four bounded sends per native20Hz cadence, deterministic up-to39ms jitter and one retry every11th op.
    for(unsigned ms=0;ms<9000;++ms) {
        budget=budget+32>16384?16384:budget+32;
        if(ms<=4000&&ms%50==0&&!overflow) {
            uint32_t tick=101+ms/50;
            if(born) { LifeRecord d={};d.info={7,1,life_generated+1,tick,1,CE_WORLD_LIFECYCLE_DESPAWN,1000+born,0};d.birth_serial=born;
                if(!LifeAppend(d)) overflow=true;else ++produced; }
            if(ms<4000&&!overflow) { ++born;LifeRecord b=Birth(life_generated+1,1000+born,tick);b.birth_serial=b.birth.serial=b.explosion.serial=born;b.info.ordinal=ms?2:1;b.dependency=key;b.birth.originator=b.explosion.originator=key.id;
                if(!LifeAppend(b)) overflow=true;else ++produced; }
        }
        if(life_count>maximum) maximum=life_count;
        if(ms%50==0&&!overflow) for(unsigned attempt=0;attempt<4;++attempt) {
            int available=CE_WorldPeekLifecycle(&info,bytes,sizeof bytes);Check(available>=0,"model host queue remains valid");if(!available)break;
            Check(CE_WorldPeekLifecycle(&again,peek,sizeof peek)==1&&LifeSame(info,again)&&!memcmp(bytes,peek,info.size),"modeled Send0 retains exact front bytes");
            unsigned wire=info.size+128;
            if(ms<stall||budget<wire||wire_inflight+wire>16384) {++send0;break;}
            unsigned slot=0;while(slot<64&&packets[slot].used)++slot;Check(slot<64,"modeled reliable pending bound");
            Packet &p=packets[slot];p={};p.used=true;p.info=info;p.wire=wire;memcpy(p.bytes,bytes,info.size);
            p.delivered=ms+rtt/2+(info.operation*17%40)+(info.operation%11==0?rtt:0);p.acknowledged=p.delivered+rtt/2;
            Check(CE_WorldLifecyclePublished(&info)==1,"modeled Send1 publishes exact immutable operation");budget-=wire;wire_inflight+=wire;
        }
        for(Packet &p:packets)if(p.used&&p.delivered<=ms)p.arrived=true;
        bool progress=true;
        while(progress){progress=false;for(Packet&p:packets)if(p.used&&p.arrived&&!p.committed&&p.info.operation==receiver+1){
            LifeRecord record={};Check(DecodeLife(p.bytes,p.info.size,record)&&record.info.operation==receiver+1,"modeled sink fully decodes next contiguous capsule");
            // This stage explicitly models WHOLE successful Apply; Decode itself never emits production ACK.
            p.committed=true;receiver=p.info.operation;++received;progress=true;}}
        for(Packet&p:packets)if(p.used&&p.committed&&p.acknowledged<=ms){
            if(p.info.operation>life_ack)Check(LifeRetire(p.info.operation,p.info.source_tick),"modeled whole-Apply proof retires exact published prefix");
            wire_inflight-=p.wire;p.used=false;}
    }
    Check(overflow==expect_overflow,"declared service/stall capacity outcome");
    if(!overflow)Check(produced==160&&received==160&&!life_count&&!wire_inflight&&life_ack==160,"100-300ms jitter/retry model drains every immutable source operation");
    else Check(life_count<=64&&life_generated==produced,"overload refuses explicitly without silently dropping generated history");
    printf("MODEL rtt=%u stall=%u rate=32000B/s births=20Hz maxqueue=%u Send0=%u produced=%u applied-model=%u overflow=%u\n",rtt,stall,maximum,send0,produced,received,(unsigned)overflow);
    binding_count=0;CE_WorldEnd();
}

// Authored complete selected pulse closure: round birth, impact birth, round endpoint, impact despawn.
// The sink commit and service are models; no actor, ENet, native flight/FX or measured runtime rate is claimed.
static void PulseServiceModel(unsigned rtt,unsigned stall,unsigned period,unsigned duration,bool expect_overflow) {
    LifeReset();active=true;config={CE_WORLD_HOST,7,0,852};life_enabled=true;life_baseline=1;life_baseline_tick=life_tick=100;
    CE_WorldFrame*base=Fixture();LifeKey key={};Check(LifeKeyFor(base->entities[1],key),"pulse service model baseline dependency");CE_WorldFrameDestroy(base);life_keys[0]=key;life_key_count=1;
    struct Shot{bool used;unsigned at;uint32_t id,impact,incarnation,serial,shot;} shots[4]={};
    struct Packet{bool used,arrived,committed;unsigned delivered,acknowledged,wire;CE_WorldLifecycleInfo info;uint8_t bytes[1000];} packets[64]={};
    uint32_t identity=1000,serial=0,sourceShots=0,receiver=0;unsigned produced=0,received=0,sourceWire=0,maximum=0,send0=0,inflight=0,budget=0;bool overflow=false;
    auto append=[&](LifeRecord &record,unsigned tick){record.info.operation=life_generated+1;record.info.source_tick=tick;record.info.ordinal=tick==life_tick?life_ordinal+1:1;
        if(!LifeAppend(record)){overflow=true;return false;}++produced;sourceWire+=life_journal[(life_first+life_count-1)%64].info.size+128;return true;};
    for(unsigned ms=0;ms<duration+5000;++ms){budget=budget+32>16384?16384:budget+32;
        if(ms%50==0&&!overflow){unsigned tick=101+ms/50;
            for(Shot&s:shots)if(s.used){
                if(ms==s.at+50){s.impact=++identity;s.serial=++serial;LifeRecord impact=Birth(life_generated+1,s.impact,tick);impact.birth_serial=impact.birth.serial=impact.explosion.serial=s.serial;impact.dependency=key;impact.birth.originator=impact.explosion.originator=key.id;if(!append(impact,tick))break;}
                if(ms==s.at+100){LifeRecord death=RoundEvent(life_generated+1,s.id,key);death.info.kind=CE_WORLD_LIFECYCLE_ROUND_DESPAWN;death.birth_serial=death.round.incarnation=s.incarnation;if(!append(death,tick))break;
                    LifeRecord gone={};gone.info={7,1,0,tick,0,CE_WORLD_LIFECYCLE_DESPAWN,s.impact,0};gone.birth_serial=s.serial;if(!append(gone,tick))break;s.used=false;}
            }
            if(ms<duration&&ms%period==0&&!overflow){Shot*slot=nullptr;for(Shot&s:shots)if(!s.used){slot=&s;break;}Check(slot!=nullptr,"source model has bounded three overlapping shot cohorts");
                *slot={true,ms,++identity,0,life_generated+1,0,++sourceShots};LifeRecord round=RoundEvent(life_generated+1,slot->id,key);round.round_birth.shot=slot->shot;if(!append(round,tick))overflow=true;}
        }
        if(life_count>maximum)maximum=life_count;
        if(ms%50==0&&!overflow)for(unsigned attempt=0;attempt<4;++attempt){uint8_t bytes[1000],again[1000];CE_WorldLifecycleInfo info{},same{};int available=CE_WorldPeekLifecycle(&info,bytes,sizeof bytes);Check(available>=0,"pulse model bounded host journal");if(!available)break;
            Check(CE_WorldPeekLifecycle(&same,again,sizeof again)==1&&LifeSame(info,same)&&!memcmp(bytes,again,info.size),"pulse model Send0 preserves exact immutable capsule");unsigned wire=info.size+128;
            if(ms<stall||budget<wire||inflight+wire>16384){++send0;break;}Packet*slot=nullptr;for(Packet&p:packets)if(!p.used){slot=&p;break;}Check(slot!=nullptr,"pulse model pending wire-slot bound");
            *slot={};slot->used=true;slot->info=info;slot->wire=wire;memcpy(slot->bytes,bytes,info.size);slot->delivered=ms+rtt/2+(info.operation*17%40)+(info.operation%11==0?rtt:0);slot->acknowledged=slot->delivered+rtt/2;
            Check(CE_WorldLifecyclePublished(&info)==1,"pulse model successful reliable enqueue is publication only");budget-=wire;inflight+=wire;
        }
        for(Packet&p:packets)if(p.used&&p.delivered<=ms)p.arrived=true;
        bool progress=true;while(progress){progress=false;for(Packet&p:packets)if(p.used&&p.arrived&&!p.committed&&p.info.operation==receiver+1){LifeRecord record{};Check(DecodeLife(p.bytes,p.info.size,record),"pulse sink fully decodes original ordered capsule");p.committed=true;receiver=p.info.operation;++received;progress=true;}}
        for(Packet&p:packets)if(p.used&&p.committed&&p.acknowledged<=ms){if(p.info.operation>life_ack)Check(LifeRetire(p.info.operation,p.info.source_tick),"pulse model only cumulative successful sink proof retires");inflight-=p.wire;p.used=false;}
    }
    Check(overflow==expect_overflow,"pulse closure declared sustainable or bounded-refusal outcome");
    if(!overflow)Check(produced==sourceShots*4&&received==produced&&!life_count&&!inflight&&life_ack==produced,"pulse closure model delivers every birth and explicit endpoint");
    else Check(life_count<=64&&produced==life_generated,"overload keeps bounded retained source operations without silent loss");
    printf("PULSE_MODEL rtt=%u stall=%u period=%u duration=%u service=32000B/s source-wire=%u shots=%u operations=%u maxqueue=%u Send0=%u overflow=%u\n",rtt,stall,period,duration,sourceWire,sourceShots,produced,maximum,send0,unsigned(overflow));
    binding_count=0;CE_WorldEnd();
}

int main() {
    CE_WorldEnd(); CE_WorldFrame *f=Fixture(); LifeKey key={};
    Check(LifeKeyFor(f->entities[1],key)&&key.size&&key.size<=kLifeKeyBytes,"canonical baseline engine dependency encodes within fixed bound");
    RoundCodecs(key);
    uint8_t bytes[CE_WORLD_LIFECYCLE_MAX_BYTES+1],again[CE_WORLD_LIFECYCLE_MAX_BYTES]; uint32_t size=0,n=0;
    LifeRecord normal=Birth(1,100),copy={}; normal.dependency=key; normal.birth.originator=key.id; normal.explosion.originator=key.id;
    Check(EncodeLife(normal,bytes,size)&&size<=1000,"complete authored normal capsule fits one packet");
    const uint32_t normal_size=size;
    Check(DecodeLife(bytes,size,copy)&&EncodeLife(copy,again,n)&&n==size&&!memcmp(bytes,again,n),"complete normal capsule exact canonical roundtrip");
    Check(copy.birth.tick==141&&copy.core.pos.v[2]==3&&copy.dependency.size==key.size,"entry and initialized source fields retained");
    for(unsigned i=0;i<size;++i) { copy.info.operation=999; Check(!DecodeLife(bytes,i,copy)&&copy.info.operation==999,"every truncated record rejected unchanged"); }
    bytes[size]=0; Check(!DecodeLife(bytes,size+1,copy),"trailing bytes cannot hide a second operation");
    for(unsigned offset=0;offset<40;offset+=4) {
        uint8_t saved[4]; memcpy(saved,bytes+offset,4); memset(bytes+offset,0,4);
        Check(!DecodeLife(bytes,size,copy),"invalid zero identity/header word refuses"); memcpy(bytes+offset,saved,4);
    }
    LifeRecord deep=Birth(1,100); deep.birth={}; deep.birth.id=100; deep.birth.serial=1; deep.birth.tick=141; deep.birth.deep=deep.birth.ready=1;
    deep.birth.pos={{1,2,100}}; deep.core.type=THING_TYPE_NONE; deep.core.pos=deep.core.old_pos={{0,0,0}};
    deep.explosion={1,0,1,0,0,0,0,0};
    Check(EncodeLife(deep,bytes,size)&&DecodeLife(bytes,size,copy),"deep capsule avoids unused native own fields"); uint32_t deep_size=size;
    LifeRecord invalid=normal; invalid.birth.attached=5; Check(!EncodeLife(invalid,bytes,size),"unsupported attachment refuses before native calls");
    invalid=normal; invalid.core.orientation.v[0]=3.402823466e38f; Check(!EncodeLife(invalid,bytes,size),"finite huge transform refuses");
    invalid=normal; Name(invalid.definition_name,"guessed explosion"); Check(!EncodeLife(invalid,bytes,size),"unknown family refuses");
    LifeRecord gone={}; gone.info={7,1,2,142,1,CE_WORLD_LIFECYCLE_DESPAWN,100,0}; gone.birth_serial=1;
    Check(EncodeLife(gone,bytes,size)&&size==40&&DecodeLife(bytes,size,copy),"despawn records original identity/time without FX expiry");
    active=true; config={CE_WORLD_HOST,7,0,852}; life_enabled=true; life_baseline=1; life_baseline_tick=life_tick=140;
    life_keys[0]=key; life_key_count=1;
    Check(LifeAppend(normal)&&life_generated==1&&life_count==1,"source completion appends immutable birth");
    CE_WorldLifecycleInfo info={},second={};
    Check(CE_WorldPeekLifecycle(&info,bytes,sizeof bytes)==1&&info.size==normal_size,"peek first unpublished birth");
    Check(CE_WorldPeekLifecycle(&second,again,sizeof again)==1&&LifeSame(info,second)&&!memcmp(bytes,again,info.size)&&!life_published,"Send0 retains exact bytes and no publication");
    CE_WorldLifecycleInfo wrong=info; ++wrong.source_tick; Check(!CE_WorldLifecyclePublished(&wrong)&&!life_published,"altered publication tuple refused");
    Check(CE_WorldLifecyclePublished(&info)&&CE_WorldLifecyclePublished(&info)&&life_published==1&&life_count==1,"Send1 publishes exactly once and never retires");
    Check(LifeAppend(gone)&&life_count==2,"source destruction is a separate ordered operation");
    normal.info.operation=3; normal.info.id=normal.birth.id=normal.core.id=101; normal.birth_serial=normal.birth.serial=normal.explosion.serial=2; normal.info.source_tick=normal.birth.tick=143;
    Check(LifeAppend(normal),"next incarnation is distinct after ordered despawn");
    // Immutable serialization accepts the last64 operations then refuses without modifying the retained front.
    for(unsigned i=3;i<kLifeOps;++i) { gone.info.operation=i+1; gone.info.source_tick=142+i; Check(LifeAppend(gone),"bounded queue admits next ordered source operation"); }
    gone.info.operation=65; Check(!LifeAppend(gone)&&life_count==64&&life_generated==64,"65th operation explicit bounded refusal");
    config.role=CE_WORLD_REPLICA; applied_tick=life_committed_tick=140; life_applied=0; life_failed=false;
    f->lifecycle_baseline=1; f->entities[3].round.incarnation=1; f->entities[3].round.schema=1234; f->entities[3].round.target=0; f->sequence=2; f->lifecycle_watermark=1;
    Check(LifeReadiness(*f)==CE_WORLD_DEFER,"newer state defers behind missing lifecycle record");
    f->entities[0].engine.camera.zoom=0; Check(LifeReadiness(*f)==CE_WORLD_INVALID,"malformed deferred state scalar validation precedes defer"); f->entities[0].engine.camera.zoom=1;
    life_applied=2; Check(LifeReadiness(*f)==CE_WORLD_STALE,"older watermark cannot resurrect removed actors");
    f->lifecycle_watermark=2; Check(LifeReadiness(*f)==CE_WORLD_READY,"equal committed watermark permits next complete state");
    f->tick=139; Check(LifeReadiness(*f)==CE_WORLD_INVALID,"state cannot rewind lifecycle committed clock");
    life_failed=true; CE_WorldLifecycleAck ack={91,92,93,94}; Check(!CE_WorldAppliedLifecycle(&ack)&&ack.epoch==91,"failed stream cannot advertise application proof");
    binding_count=0; CE_WorldEnd(); Check(!life_enabled&&!life_count&&!life_applied,"epoch end retires compact local ownership after teardown");
    PulseServiceModel(100,300,100,4000,false);PulseServiceModel(200,300,100,4000,false);PulseServiceModel(300,300,100,4000,false);PulseServiceModel(300,2000,100,4000,true);PulseServiceModel(300,300,50,40000,true);
    ServiceModel(100,300,false);ServiceModel(200,300,false);ServiceModel(300,300,false);ServiceModel(300,2000,true);
    CE_WorldFrameDestroy(f); printf("PASS compact lifecycle codec/queue: %u checks; normal=%u deep=%u despawn=40 bytes\n",checks,normal_size,deep_size); return 0;
}
