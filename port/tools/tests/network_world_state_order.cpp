// SPDX-License-Identifier: GPL-3.0-only
// Actual full scalar/reference validation and readiness; resource queries are a counted caller boundary.
#define main retained_codec_main
#include "network_world_driver.cpp"
#undef main
static unsigned resource_queries;
static bool resource_admitted=true;
static bool ResourceBoundary(const CE_WorldFrame &) { ++resource_queries;return resource_admitted; }
#include READINESS_CALLER
static CE_WorldFrame *InitialState() {
    CE_WorldFrame *f=Fixture();f->count=3;f->tick=60;f->sequence=2;f->lifecycle_baseline=1;f->lifecycle_watermark=0;
    for(unsigned i=0;i<2;++i)f->entities[i].engine.threat_count=0;
    return f;
}
static void CheckUnchanged(const uint32_t *before) {
    const uint32_t now[]={applied_tick,applied_sequence,life_committed_tick,life_applied,life_tick,life_baseline,life_baseline_tick,birth_applied,last_round_shot};
    Check(!memcmp(before,now,sizeof now)&&!applying&&!poisoned,"readiness classification changes no committed clock watermark or mutation state");
}
int main() {
    active=true;config={CE_WORLD_REPLICA,7,1,852};life_enabled=true;life_baseline=1;life_baseline_tick=60;
    applied_tick=60;applied_sequence=1;life_committed_tick=life_tick=61;life_applied=1;
    const uint32_t before[]={applied_tick,applied_sequence,life_committed_tick,life_applied,life_tick,life_baseline,life_baseline_tick,birth_applied,last_round_shot};
    CE_WorldFrame *f=InitialState();Check(CENetworkWorldAdapter::Validate(*f),"authored original prebirth state passes actual full validation");
    resource_admitted=false;int result=ProbeFrameReadiness(f);printf("LATE_STATE baseline=60 lifecycle=61 snapshot=60 sequence=2 watermark=0 readiness=%d resource_queries=%u\n",result,resource_queries);
    Check(result==CE_WORLD_STALE&&!resource_queries,"valid late prebirth STATE is stale instead of a session error");CheckUnchanged(before);
    f->entities[0].engine.camera.zoom=0;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"malformed stale scalar still refuses before stale classification");f->entities[0].engine.camera.zoom=1;
    f->tick=59;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"pre-baseline time cannot become an admitted stale state");f->tick=60;
    f->epoch=8;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"foreign epoch cannot hide behind old watermark");f->epoch=7;
    f->level=110;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"foreign level refuses before stale classification");f->level=852;
    f->lifecycle_baseline=9;f->sequence=10;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"foreign baseline with valid structure cannot hide behind stale watermark");f->lifecycle_baseline=1;f->sequence=2;
    f->entities[2].core.id=1;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"malformed stale entity identity refuses before drop");f->entities[2].core.id=3;
    f->lifecycle_watermark=1;f->sequence=3;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"new current-prefix state cannot rewind the committed lifecycle clock");
    f->lifecycle_watermark=2;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&!resource_queries,"new future-prefix state cannot defer a backward event clock");
    applied_sequence=3;f->sequence=2;f->lifecycle_watermark=1;Check(ProbeFrameReadiness(f)==CE_WORLD_STALE&&!resource_queries,"validated old state sequence drops before current clock and incarnation checks");applied_sequence=1;
    // A valid historical round need not resolve against today's already-despawned native objects/resources.
    CE_WorldFrame *historical=Fixture();historical->tick=61;historical->sequence=2;historical->lifecycle_baseline=1;historical->lifecycle_watermark=1;
    Entity &round=historical->entities[3];round.core.type=THING_TYPE_THING|THING_TYPE_COMPLEX_THING|THING_TYPE_ACTOR|THING_TYPE_AMMUNITION;round.core.flags=TF_IN_MAP_WHO;
    Name(round.round.definition,"Mech Pulse Bolt Medium");round.round.incarnation=1;round.round.schema=123;round.round.target=0;round.round.beam_start={{0,0,0}};
    life_applied=2;life_committed_tick=life_tick=62;Check(CENetworkWorldAdapter::Validate(*historical),"historical round state passes actual structural and reference validation");
    Check(ProbeFrameReadiness(historical)==CE_WORLD_STALE&&!resource_queries,"historical retired round never reaches current native resource resolution");
    life_applied=1;life_committed_tick=life_tick=61;CE_WorldFrameDestroy(historical);
    f->sequence=3;f->tick=61;f->lifecycle_watermark=1;
    Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&resource_queries==1,"new current frame still validates present native resources");
    resource_admitted=true;Check(ProbeFrameReadiness(f)==CE_WORLD_READY&&resource_queries==2,"new valid equal-prefix frame remains ready");
    f->tick=62;f->lifecycle_watermark=2;Check(ProbeFrameReadiness(f)==CE_WORLD_DEFER&&resource_queries==3,"new forward future-prefix frame still defers after resource preflight");
    resource_admitted=false;Check(ProbeFrameReadiness(f)==CE_WORLD_INVALID&&resource_queries==4,"malformed current resources still refuse a deferred future frame");
    CheckUnchanged(before);CE_WorldFrameDestroy(f);printf("PASS state readiness order %u checks; resource boundary only, no native Apply or actors\n",checks);return 0;
}
