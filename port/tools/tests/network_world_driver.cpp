// SPDX-License-Identifier: GPL-3.0-only
// Compile the production adapter against real game headers. No actor, renderer, device or retail data is fabricated.
// These checks exercise typed codecs, reference validation and bounded rejection, not game initialization/playability.
#include CE_WORLD_SOURCE
#undef printf
#include <assert.h>

// The lockstep co-op layer (ce_network_lockstep.cpp) and the game objects the world module reads while it is active are outside this fixture:
// the answers of a game that is not in a lockstep session, and storage and typeinfo symbols for the camera test it makes there.
extern "C" {
int CE_LockstepActive(void) { return 0; }
int CE_LockstepTicking(void) { return 0; }
uint32_t CE_LockstepLocalPlayer(void) { return 0; }
int CE_LockstepControllerFlush(uint32_t) { return 0; }
int CE_LockstepInvertPitch(uint32_t, int, int original) { return original; }
int CE_LockstepWalkerAny(uint32_t, int original_type) { return original_type; }
int CE_LockstepCoopPlayers(void) { return 0; }
char GameStorage[sizeof(CGame)] __asm__("GAME") __attribute__((aligned(16)));	// the game object is never constructed here
char CameraTypeInfo[16] __asm__("_ZTI7CCamera");
char PanCameraTypeInfo[16] __asm__("_ZTI10CPanCamera");
}
CCamera *CGame::GetCamera(int) { return NULL; }

static unsigned checks;
static void Check(bool ok,const char *what) {
    ++checks; if(!ok) { fprintf(stderr,"FAIL %s (%s)\n",what,CE_WorldError()); exit(2); }
}
static CE_WorldFrame *Fixture() {
    CE_WorldFrame *f=CE_WorldFrameCreate(); Check(f!=NULL,"allocate frame");
    f->epoch=7; f->tick=140; f->sequence=1; f->level=852; f->phase=GAME_STATE_PLAYING; f->end_count=-1.f; f->count=7; f->lives[0]=f->lives[1]=2;
    for(unsigned i=0;i<f->count;++i) for(unsigned j=0;j<9;++j) f->entities[i].core.orientation.v[j]=f->entities[i].core.old_orientation.v[j]=(j%4==0?1.f:0.f);
    for(unsigned i=0;i<2;++i) {
        Entity &s=f->entities[i]; s.core.id=i+1; s.core.kind=ENGINE_THING; s.core.player=i+1; s.core.initial_number=i+10;
        Name(s.core.class_name,"CBattleEngine"); Name(s.core.mesh,"test-engine.msh");
        s.core.pos={{1.f+i,2.f,3.f}}; s.core.old_pos={{.5f+i,2.f,3.f}};
        s.engine.hit_decay=10; s.engine.part_count=1; s.engine.mState=BATTLE_ENGINE_STATE_WALKER; s.engine.view=s.engine.preferred_view=PLAYER_FP_VIEW;
        s.engine.camera.zoom=s.engine.camera.old_zoom=1.f; s.engine.camera.hud=1;
        Name(s.engine.cockpit.mesh,"test-cockpit.msh"); s.engine.cockpit.schema=123456+i;
        s.engine.cockpit.should_render=1; s.engine.cockpit.mode=1001; s.engine.cockpit.index=1;
        s.engine.cockpit.shake={{.001f,.002f,.003f}}; s.engine.cockpit.old_shake={{-.001f,-.002f,-.003f}};
        s.engine.cockpit.frame=.75f; s.engine.cockpit.old_frame=.25f;
        for(unsigned j=0;j<9;++j) s.engine.cockpit.orientation.v[j]=s.engine.cockpit.old_orientation.v[j]=j%4==0?1.f:0.f;
        s.engine.radar_present=1; s.engine.new_threat=1; s.engine.new_threat_time=1.25f;
        s.engine.threat_count=1; s.engine.threats[0]={4,1,.5f,12.f,1.1f};
        s.engine.flash_count=1; s.engine.flashes[0]={.25f,1.3f};
        s.engine.crosshair=i?1:2; s.engine.auto_aim=i?1:2;
        s.engine.weapon_count=1; Name(s.engine.weapons[0].name,"test weapon"); s.engine.weapons[0].target=i?1:2;
        s.engine.lock_count=1; s.engine.locks[0].target=i?1:2;
        s.engine.locks[0].locked=1; s.engine.locks[0].start=2.f; s.engine.locks[0].finish=3.f;
        s.engine.parts[0].pos={{.1f,.2f,.3f}}; s.engine.parts[0].old_pos={{.2f,.4f,.6f}};
        s.engine.parts[0].frame=.2f; s.engine.parts[0].old_frame=.1f;
        for(unsigned j=0;j<9;++j) s.engine.parts[0].orientation.v[j]=s.engine.parts[0].old_orientation.v[j]=
            s.engine.camera.orientation.v[j]=s.engine.camera.old_orientation.v[j]=(j%4==0?1.f:0.f);
        float value=1.f;
#define VALUE_FLOAT(name) s.engine.name=value; value+=.125f;
        ENGINE_FLOATS(VALUE_FLOAT)
#undef VALUE_FLOAT
    }
    Entity &point=f->entities[2]; point.core.id=3; point.core.kind=STATIC_THING; point.core.initial_number=30; Name(point.core.class_name,"CStart");
    point.core.flags=TF_MARKED_OBJECTIVE;
    Entity &round=f->entities[3]; round.core.id=4; round.core.kind=ROUND_THING; Name(round.core.class_name,"CRound");
    Name(round.round.definition,"test round"); round.round.launcher=1; round.round.target=2; round.round.start=1.5f;
    round.round.beam_start={{1.f,2.f,3.f}}; round.round.destination={{4.f,5.f,6.f}};
    Entity &cannon=f->entities[4]; cannon.core.id=5; cannon.core.kind=CANNON_THING; cannon.core.initial_number=31;
    Name(cannon.core.class_name,"CCannon"); Name(cannon.core.mesh,"test-cannon.msh");
    Name(cannon.cannon.unit.definition,"test cannon"); cannon.cannon.unit.mesh_schema=1234;
    cannon.cannon.unit.life=50.f; cannon.cannon.unit.initial_life=100.f; cannon.cannon.unit.hit=-3; cannon.cannon.unit.hit_decay=10;
    cannon.cannon.unit.turret[0]=1.5f; cannon.cannon.unit.turret[1]=.5f;
    cannon.cannon.drop=3.f; cannon.cannon.old_drop=1.f; cannon.cannon.state=3;
    Entity &feature=f->entities[5]; feature.core.id=6; feature.core.kind=FEATURE_THING; feature.core.initial_number=32;
    Name(feature.core.class_name,"CFeature"); Name(feature.core.mesh,"test-feature.msh");
    Name(feature.feature.definition,"test feature"); feature.feature.mesh_schema=5678;
    feature.feature.life=0.f; feature.feature.initial_life=20.f; feature.feature.invincible=1;
    Entity &building=f->entities[6]; building.core.id=7; building.core.kind=BUILDING_THING; building.core.initial_number=33;
    Name(building.core.class_name,"CBuilding"); Name(building.core.mesh,"test-building.msh");
    building.building.unit=cannon.cannon.unit; Name(building.building.unit.definition,"test building");
    BuildingState &b=building.building; b.door=3; b.shut=3; b.door_time=-1.f; b.last_hit=-100.f;
    SegmentGraph &g=b.graph; g.schema=6789; g.count=4; g.slots=6; g.core=1; g.core_count=2; g.primary_visible=1;
    g.total_health=131.f; g.total_size=22.f; g.fall_threshold=2.f;
    uint32_t alias[]={1,2,3,4,3,0}; memcpy(g.alias,alias,sizeof alias);
    g.nodes[0].type=DST_CORE; g.nodes[0].part=0; g.nodes[0].number=1; // native primary core legitimately has zero health/max
    g.nodes[1].type=DST_CORE; g.nodes[1].part=1; g.nodes[1].number=2; g.nodes[1].parent=1; g.nodes[1].total_time=.25f;
    g.nodes[2].type=DST_SWAP; g.nodes[2].part=4; g.nodes[2].parent=2; g.nodes[2].stages=1; g.nodes[2].health=5.f; g.nodes[2].max_health=10.f;
    g.nodes[3].type=DST_EXTRA; g.nodes[3].part=3; g.nodes[3].parent=2; g.nodes[3].hit=50;
    f->counters_a[0]=11; f->counters_b[0]=9;
    f->primary[0]={MOS_COMPLETE,123456}; f->primary[MAX_PRIMARY_OBJECTIVES-1]={MOS_FAILED,-1};
    f->secondary[0]={MOS_FAILED,INT32_MIN}; f->secondary[MAX_SECONDARY_OBJECTIVES-1]={MOS_COMPLETE,INT32_MAX};
    return f;
}
int main() {
    CE_WorldEnd(); Check(!CE_WorldActive()&&!CE_WorldReplica(),"offline admission");
    Check(CE_WorldViewPlayer(3)==3&&CE_WorldViewAspect(.5f)==.5f,"offline presentation untouched");
    CE_WorldFrame *f=Fixture(),*copy=CE_WorldFrameCreate(); uint8_t bytes[CE_NET_MAX_MESSAGE+1],again[CE_NET_MAX_MESSAGE]; uint32_t size=0,n=0;
    Check(CE_WorldEncode(f,bytes,CE_NET_MAX_MESSAGE,&size)&&size>1000,"encode complete typed world");
    Check(bytes[0]=='C'&&bytes[1]=='E'&&bytes[2]=='W'&&bytes[3]=='1',"wire magic");
    Check(CE_WorldDecode(copy,bytes,size),"decode complete typed world");
    active=true; config={CE_WORLD_REPLICA,7,1,852};
    CE_WorldBirthAck decoded_ack={91,92,93};
    Check(!CE_WorldAppliedBirths(&decoded_ack)&&decoded_ack.epoch==91&&decoded_ack.serial==92&&decoded_ack.tick==93,"successful Decode cannot advertise unapplied birth progress");
    CE_WorldEnd();
    Check(CE_WorldEncode(copy,again,sizeof again,&n)&&n==size&&!memcmp(bytes,again,size),"all fields round trip identically");
    Check(copy->entities[0].core.old_pos.v[0]==.5f&&copy->entities[0].engine.parts[0].old_frame==.1f,"old actor/pose fields retained");
    Check(copy->entities[3].round.launcher==1&&copy->entities[3].round.target==2,"ID references retained");
    Check(copy->entities[4].cannon.unit.turret[0]==1.5f&&copy->entities[4].cannon.unit.turret[1]==.5f&&copy->entities[4].cannon.drop==3.f&&copy->entities[4].cannon.old_drop==1.f&&copy->entities[4].cannon.unit.hit==-3,"typed turret drop and transient hit inputs round trip without evaluated poses");
    Check(copy->entities[5].feature.life==0.f&&!(copy->entities[5].core.flags&TF_DYING)&&copy->entities[5].feature.invincible,"feature zero life is not fabricated into death");
    Check(copy->counters_a[0]==11&&copy->counters_b[0]==9,"authoritative world counters survive snapshot");
    Check((copy->entities[2].core.flags&TF_MARKED_OBJECTIVE)!=0,"arena objective marker survives the actual core codec");
    Check(copy->primary[0].state==MOS_COMPLETE&&copy->primary[0].string_id==123456&&
          copy->primary[MAX_PRIMARY_OBJECTIVES-1].state==MOS_FAILED&&copy->primary[MAX_PRIMARY_OBJECTIVES-1].string_id==-1&&
          copy->secondary[0].state==MOS_FAILED&&copy->secondary[0].string_id==INT32_MIN&&
          copy->secondary[MAX_SECONDARY_OBJECTIVES-1].state==MOS_COMPLETE&&copy->secondary[MAX_SECONDARY_OBJECTIVES-1].string_id==INT32_MAX,
          "both complete objective arrays preserve state and signed ID bits, including endpoint slots");
    Check(copy->entities[6].building.graph.count==4&&copy->entities[6].building.graph.alias[2]==3&&copy->entities[6].building.graph.alias[4]==3&&copy->entities[6].building.graph.alias[5]==0,"typed building graph preserves unique segment IDs, shared slots and nulls");
    Check(copy->entities[6].building.graph.nodes[0].health==0.f&&!copy->entities[6].building.graph.nodes[0].destroyed&&copy->entities[6].building.graph.nodes[1].total_time==.25f,"zero-health live primary core and independent nonprimary core fall survive");
    Check(copy->entities[6].building.graph.total_health!=copy->entities[6].building.unit.life&&copy->entities[6].building.door_time==-1.f&&copy->entities[6].building.last_hit==-100.f,"native building health/timer sentinels remain distinct");
    SegmentGraph graph=f->entities[6].building.graph;
    graph.nodes[1].parent=3; Check(!CENetworkWorldAdapter::SegmentBound(graph),"segment parent cycle refused"); graph=f->entities[6].building.graph;
    graph.alias[0]=kParts+1; Check(!CENetworkWorldAdapter::SegmentBound(graph),"foreign segment alias refused"); graph=f->entities[6].building.graph;
    graph.nodes[3].type=DST_COMPONENT; Check(!CENetworkWorldAdapter::SegmentBound(graph),"unimplemented component segment refused"); graph=f->entities[6].building.graph;
    graph.nodes[3].part=6; Check(!CENetworkWorldAdapter::SegmentBound(graph),"spare slot is not a renderable segment part"); graph=f->entities[6].building.graph;
    graph.nodes[2].max_health=1.e-38f; Check(!CENetworkWorldAdapter::SegmentBound(graph),"finite swap fields cannot overflow native floor-to-int conversion"); graph=f->entities[6].building.graph;
    graph.nodes[3].direction.v[0]=1.f; Check(!CENetworkWorldAdapter::SegmentBound(graph),"inactive wire direction must be canonical");
    FVector unused_direction(1.e30f,2.e30f,3.e30f); Vec ignored=ExtraDirection(false,&unused_direction);
    Check(ignored.v[0]==0.f&&ignored.v[1]==0.f&&ignored.v[2]==0.f,"inactive native extra direction is ignored before numeric validation");
    Check(SegmentHit(2.f,0,2.f,.5f)==50&&SegmentHit(2.5f,0,2.f,.5f)==0&&SegmentHit(3.f,0,2.f,1.f)==0,"segment flash is captured without scheduling a client decay event");
    Check(copy->entities[0].engine.cockpit.old_shake.v[0]==-.001f&&copy->entities[0].engine.cockpit.shake.v[0]==.001f&&copy->entities[0].engine.cockpit.old_frame==.25f&&copy->entities[0].engine.cockpit.frame==.75f,"cockpit current and old presentation inputs retain independent host values");
    CockpitState cockpit=f->entities[0].engine.cockpit;
    cockpit.old_orientation.v[8]=0.f; Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"unprepared singular old cockpit orientation refused"); cockpit=f->entities[0].engine.cockpit;
    cockpit.old_shake.v[0]=__builtin_inff(); Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"nonfinite old cockpit shake refused"); cockpit=f->entities[0].engine.cockpit;
    cockpit.shake.v[0]=17.f; Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"cockpit position offset bounds checked before native readers"); cockpit=f->entities[0].engine.cockpit;
    cockpit.should_render=2; Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"unknown cockpit visibility refused"); cockpit=f->entities[0].engine.cockpit;
    cockpit.frame=1.1f; Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"cockpit normalized frame bounds checked"); cockpit=f->entities[0].engine.cockpit;
    cockpit.index=(uint32_t)-2; Check(!CENetworkWorldAdapter::CockpitBound(cockpit),"invalid negative cockpit animation index refused");
    Check(copy->end_count==-1.f,"native inactive result counter survives ordinary playing snapshot");
    Check(EndCountBound(GAME_STATE_PRE_RUNNING,-1.f)&&EndCountBound(GAME_STATE_PANNING,-1.f)&&EndCountBound(GAME_STATE_PLAYING,-1.f),"native constructor and LoadLevel sentinel admitted through all nonterminal phases");
    Check(!EndCountBound(GAME_STATE_PLAYING,0.f)&&!EndCountBound(GAME_STATE_PLAYING,-2.f)&&!EndCountBound(GAME_STATE_PLAYER_1_WON,-1.f)&&EndCountBound(GAME_STATE_PLAYER_1_WON,5.f)&&EndCountBound(GAME_STATE_GAME_DRAWN,2.f),"result counter sentinel and active phases cannot be confused");
    unsigned cuts[]={0,1,4,7,8,19,22,35,36,37,size/2,size-1};
    for(unsigned cut:cuts) {
        copy->tick=999; Check(!CE_WorldDecode(copy,bytes,cut)&&copy->tick==999,"truncated frame leaves destination unchanged");
    }
    uint8_t current_schema=bytes[7];
    const uint8_t old_world_schemas[]={4,5,6,7,8,9,10,12};
    for(uint8_t previous : old_world_schemas) {
        bytes[7]=previous; copy->tick=999;
        Check(!CE_WorldDecode(copy,bytes,size)&&copy->tick==999,"foreign world schema cannot reinterpret objective and entity fields");
    }
    bytes[7]=current_schema;
    bytes[size]=0; copy->tick=999; Check(!CE_WorldDecode(copy,bytes,size+1)&&copy->tick==999,"trailing payload refused atomically");
    f->primary[0].state=-1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"negative objective state refused"); f->primary[0].state=MOS_COMPLETE;
    f->secondary[MAX_SECONDARY_OBJECTIVES-1].state=MOS_FAILED+1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"unknown objective state refused"); f->secondary[MAX_SECONDARY_OBJECTIVES-1].state=MOS_COMPLETE;
    const uint32_t objective_start=58+8*NUM_WORLD_COUNTERS;
    CE_NetWriter objective_corrupt; CE_NetWriterInit(&objective_corrupt,bytes+objective_start,4); CE_NetWriteU32(&objective_corrupt,3);
    copy->tick=999; Check(!CE_WorldDecode(copy,bytes,size)&&copy->tick==999,"invalid objective state refuses the whole frame before replacement");
    Check(CE_WorldEncode(f,bytes,CE_NET_MAX_MESSAGE,&size),"restore frame after invalid objective wire control");
    uint32_t save=f->entities[1].core.id; f->entities[1].core.id=1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"duplicate IDs refused"); f->entities[1].core.id=save;
    f->entities[0].engine.crosshair=999; Check(!CE_WorldEncode(f,again,sizeof again,&n),"unresolved target refused"); f->entities[0].engine.crosshair=2;
    f->entities[0].engine.crosshair=3; Check(!CE_WorldEncode(f,again,sizeof again,&n),"unit-only target cannot name a static thing"); f->entities[0].engine.crosshair=2;
    f->entities[0].engine.crosshair=5; Check(CE_WorldEncode(f,again,sizeof again,&n),"unit-only target may name an admitted genuine cannon kind"); f->entities[0].engine.crosshair=2;
    f->entities[3].round.launcher=5; Check(CE_WorldEncode(f,again,sizeof again,&n),"cannon-launched projectile carries a canonical unit reference"); f->entities[3].round.launcher=1;
    f->entities[4].cannon.state=4; Check(!CE_WorldEncode(f,again,sizeof again,&n),"unknown cannon state refused"); f->entities[4].cannon.state=3;
    f->entities[5].feature.life=-1.f; Check(CE_WorldEncode(f,again,sizeof again,&n),"negative feature life is retained independently of explicit flags"); f->entities[5].feature.life=0.f;
    f->entities[0].engine.part_count=kParts+1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"pose capacity refused"); f->entities[0].engine.part_count=1;
    f->entities[0].engine.weapon_count=kWeapons+1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"weapon capacity refused"); f->entities[0].engine.weapon_count=1;
    Name(f->entities[2].core.class_name,"CUnsupportedUnit"); Check(!CE_WorldEncode(f,again,sizeof again,&n),"unsupported class refused"); Name(f->entities[2].core.class_name,"CStart");
    f->entities[1].core.player=1; Check(!CE_WorldEncode(f,again,sizeof again,&n),"duplicate player identity refused"); f->entities[1].core.player=2;
    f->entities[0].core.pos.v[0]=__builtin_inff(); Check(!CE_WorldEncode(f,again,sizeof again,&n),"nonfinite state refused"); f->entities[0].core.pos.v[0]=1.f;
    memset(again,0xa5,64); Check(!CE_WorldEncode(f,again,16,&n)&&n==0,"small output capacity refused");
    for(unsigned i=16;i<64;++i) Check(again[i]==0xa5,"writer output canary");
    CE_WorldInput input={}; input.epoch=7; input.sequence=1; input.tick=141; input.player=1; input.count=3;
    input.mouse_ground_aim=1; input.ground_yaw=.125f; input.ground_pitch=-.25f;
    input.actions[0]={BUTTON_MECH_YAW_RIGHT,1,.375f}; input.actions[1]={BUTTON_MECH_CHARGE_GUN_POD,0,1.f}; input.actions[2]={BUTTON_MECH_FIRE_GUN_POD,0,1.f};
    uint8_t small[CE_NET_MAX_INPUT]; Check(CE_WorldEncodeInput(&input,small,sizeof small,&n),"encode ordered semantic input");
    input.last_applied_birth_serial=23; Check(CE_WorldEncodeInput(&input,small,sizeof small,&n),"input carries applied birth watermark");
    CE_WorldInput decoded={}; Check(CE_WorldDecodeInput(&decoded,small,n),"decode ordered semantic input");
    Check(decoded.last_applied_birth_serial==23,"birth acknowledgement travels in immutable semantic input");
    Check(decoded.actions[0].button==27&&decoded.actions[1].button==19&&decoded.actions[2].button==18&&decoded.actions[0].source==1&&decoded.actions[0].value==.375f&&decoded.ground_pitch==-.25f,"order analogues and aim retained");
    CE_WorldInput gain=input; gain.mouse_ground_aim=0; gain.ground_yaw=gain.ground_pitch=0.f; gain.walker_mouse_any=1;
    Check(CE_WorldEncodeInput(&gain,small,sizeof small,&n)&&CE_WorldDecodeInput(&decoded,small,n)&&decoded.walker_mouse_any==1,"guest ANY binding category travels with the native sample");
    gain.walker_mouse_any=2; Check(!CE_WorldEncodeInput(&gain,small,sizeof small,&n),"unknown walker binding category refused");
    gain.walker_mouse_any=1; gain.mouse_ground_aim=1; Check(!CE_WorldEncodeInput(&gain,small,sizeof small,&n),"plain ground mouse and ANY binding phases cannot coexist");
    Check(CE_WorldEncodeInput(&input,small,sizeof small,&n),"restore native plain mouse sample");
    const uint8_t foreign_input_schemas[]={3,4,6};
    for(uint8_t previous : foreign_input_schemas) {
        small[7]=previous; decoded.tick=999;
        Check(!CE_WorldDecodeInput(&decoded,small,n)&&decoded.tick==999,"foreign semantic input schema cannot reinterpret the birth acknowledgement");
    }
    Check(CE_WorldEncodeInput(&input,small,sizeof small,&n),"restore current semantic input schema");
    decoded.tick=999; Check(!CE_WorldDecodeInput(&decoded,small,n-1)&&decoded.tick==999,"input decode atomic on truncation");
    input.actions[0].button=BUTTON_WIN_LEVEL; Check(!CE_WorldEncodeInput(&input,small,sizeof small,&n),"remote mission/debug command refused");
    input.actions[0].button=BUTTON_PAUSE; Check(!CE_WorldEncodeInput(&input,small,sizeof small,&n),"pause is not a client game mutation");
    input.actions[0].button=BUTTON_MECH_YAW_RIGHT; input.player=0; Check(!CE_WorldEncodeInput(&input,small,sizeof small,&n),"input cannot impersonate host player"); input.player=1;
    input.count=CE_WORLD_MAX_ACTIONS+1; Check(!CE_WorldEncodeInput(&input,small,sizeof small,&n),"input action bound"); input.count=3;
    input.ground_yaw=__builtin_nanf(""); Check(!CE_WorldEncodeInput(&input,small,sizeof small,&n),"nonfinite aim refused");
    input.ground_yaw=.125f;
    CE_WorldInput impossible=input; impossible.count=32;
    for(unsigned i=0;i<32;++i) impossible.actions[i]={19,0,1.f};
    Check(!ValidInput(impossible),"32 charge calls cannot impersonate one genuine native sample");
    impossible=input; impossible.actions[1]=impossible.actions[0]; Check(!ValidInput(impossible),"duplicated movement action cannot multiply one tick");
    impossible=input; impossible.actions[0]={22,0,1.f}; Check(!ValidInput(impossible),"unmapped enum action is not admitted");
    impossible=input; impossible.actions[0]={18,0,1.f}; impossible.actions[2]={27,0,1.f}; Check(!ValidInput(impossible),"reordered native mapping phases are refused");
    InputQueue queue={}; CE_WorldInput due={};
    Check(queue.Push(input,7,140)==1&&queue.consumed==0,"receipt is not consumption acknowledgement");
    Check(!queue.Peek(140,due)&&queue.Peek(141,due)&&due.sequence==1,"host assigns no earlier than next tick");
    queue.Consumed(); Check(queue.consumed==1,"acknowledgement follows actual consumption");
    Check(queue.Push(input,7,150)==1&&queue.count==0,"consumed duplicate is idempotent");
    input.sequence=2; input.tick=100; input.count=1; input.actions[0]={BUTTON_MECH_FIRE_GUN_POD,0,1.f};
    Check(queue.Push(input,7,150)==1&&!queue.Peek(150,due)&&queue.Peek(151,due)&&due.actions[0].button==18,"late release retained for next host tick");
    input.sequence=4; Check(queue.Push(input,7,150)==-1&&queue.count==1,"sequence gap refuses without consuming queued release");
    input.sequence=3; Check(queue.Push(input,7,150)==1,"burst batches queue in sequence"); queue.Consumed();
    Check(!queue.Peek(151,due)&&queue.Peek(152,due)&&due.sequence==3,"burst arrival applies at most one batch per host tick"); queue.Consumed();
    InputQueue full={}; input.tick=200;
    for(unsigned i=1;i<kInputs;++i) { input.sequence=i; Check(full.Push(input,7,200)==1,"bounded ordered backlog"); }
    input.sequence=kInputs; Check(full.Push(input,7,200)==0&&full.accepted==kInputs-1,"backpressure retains sequence for caller retry");
    full.Consumed(); Check(full.Push(input,7,201)==1&&full.accepted==kInputs,"same exact unsent batch retries after capacity opens");
    const unsigned render_rates[]={64,144};
    for(unsigned rate:render_rates) {
        CE_WorldEnd(); active=true; config.role=CE_WORLD_REPLICA;
        LocalClock clock={}; uint32_t samples=0,consumed=0,charge_calls=0,max_pending=0;
        for(unsigned render=0;render<rate;++render) {
            uint64_t now=render*1000/rate;
            if(clock.Ticket(now)) {
                CE_WorldInput batch={}; batch.epoch=7; batch.sequence=++samples; batch.tick=140+samples; batch.player=1;
                batch.invert_ground=samples%2; batch.mouse_ground_aim=samples%2;
                batch.ground_yaw=batch.mouse_ground_aim?.125f:0.f;
                batch.count=sizeof native_actions/sizeof *native_actions;
                for(unsigned action=0;action<batch.count;++action) batch.actions[action]={native_actions[action],0,1.f};
                Check(ValidInput(batch)&&local_input.Poll(batch),"native tick batch fits bounded FIFO during300ms send stall");
                if(local_input.count>max_pending) max_pending=local_input.count;
            }
            CE_WorldInput front={},again={}; int ready=CE_WorldSampleInput(&front);
            if(ready==1) {
                Check(CE_WorldSampleInput(&again)==1&&!memcmp(&front,&again,sizeof front),"repeated render polls lease exactly the same front");
                if(now>=300) do {
                    Check(front.sequence==consumed+1&&front.count==sizeof native_actions/sizeof *native_actions,"each historical native tick retains every action in sequence");
                    Check(front.invert_ground==front.sequence%2&&front.mouse_ground_aim==front.sequence%2&&front.ground_yaw==(front.mouse_ground_aim?.125f:0.f),"pose mode preferences and ground delta stay with their sampled tick");
                    for(unsigned action=0;action<front.count;++action) {
                        Check(front.actions[action].button==native_actions[action],"native charge release chronology retained within a tick");
                        if(front.actions[action].button==19) ++charge_calls;
                    }
                    Check(CE_WorldInputSent(front.sequence+1)==-1&&local_input.count>0,"wrong send acknowledgement cannot pop a sampled tick");
                    Check(CE_WorldInputSent(front.sequence)==1,"only accepted exact front retires its native tick"); ++consumed;
                } while(CE_WorldSampleInput(&front)==1);
            }
        }
        Check(samples==20&&consumed==20&&charge_calls==20&&max_pending>=6,"64/144Hz render polling retains20Hz batches and all charge calls through300ms backpressure");
        CE_WorldEnd();
    }
    LocalInput full_local={}; CE_WorldInput batch={}; batch.count=sizeof native_actions/sizeof *native_actions;
    for(unsigned i=0;i<kInputs;++i) { batch.sequence=i+1; Check(full_local.Poll(batch),"fixed native input FIFO accepts16 complete ticks"); }
    batch.sequence=kInputs+1; Check(!full_local.Poll(batch)&&full_local.failed&&full_local.count==kInputs,"seventeenth native tick refuses without discarding the16 retained batches");
    CE_WorldEnd(); active=true; config.role=CE_WORLD_REPLICA; applied_tick=100; sampled_tick=99;
    generated_sequence=8; sent_sequence=2; replica_ack=1;
    for(unsigned i=3;i<=8;++i) { batch.sequence=i; Check(local_input.Poll(batch),"suspension fixture retains six sampled ticks"); }
    CE_WorldInput leased={},after_wait={}; Check(CE_WorldSampleInput(&leased)==1,"lease front before shared host wait");
    Check(CE_WorldSetInputSuspended(CE_WORLD_INPUT_HOST_WAIT)==1&&input_suspension==CE_WORLD_INPUT_HOST_WAIT,"known shared-host reason suspends fresh sampling");
    Check(CE_WorldSetInputSuspended(2)==-1&&input_suspension==CE_WORLD_INPUT_HOST_WAIT,"unknown suspension bit cannot replace active wait reason");
    CE_WorldRebaseLocalSampler();
    Check(sampled_tick==applied_tick&&!FreshTick(applied_tick,sampled_tick)&&!local_clock.started,"resume rebase consumes current tick credit and resets wall ticket");
    Check(local_input.count==6&&CE_WorldSampleInput(&after_wait)==1&&!memcmp(&leased,&after_wait,sizeof leased),"suspension and rebase preserve exact leased front and backlog");
    Check(CE_WorldLastGeneratedSequence()==8&&sent_sequence==2&&CE_WorldConsumedSequence()==1,"suspension does not change generation send or host-consumed watermarks");
    Check(CE_WorldSetInputSuspended(0)==1&&!input_suspension&&!FreshTick(100,sampled_tick)&&FreshTick(101,sampled_tick),"resume grants no phantom input before a genuinely new host tick");
    sampled_tick=1000; Check(!FreshTick(1000,sampled_tick)&&FreshTick(1001,sampled_tick),"latest tick credit does not accumulate catch-up samples");
    CE_WorldEnd(); Check(CE_WorldSetInputSuspended(1)==1&&CE_WorldLastGeneratedSequence()==0&&!input_suspension,"inactive suspension API leaves ordinary paths untouched");
    float *invalid[]={&f->entities[0].core.pos.v[0],&f->entities[0].engine.euler.v[0],&f->entities[0].engine.threats[0].yaw,&f->entities[0].engine.flashes[0].yaw};
    for(float *value:invalid) { float original=*value; *value=3.402823466e38f; Check(!CENetworkWorldAdapter::Validate(*f),"huge finite native-consumer value rejected before apply"); *value=original; }
    f->entities[0].engine.parts[0].frame=-1.f; Check(!CENetworkWorldAdapter::Validate(*f),"negative active pose frame rejected before mutation"); f->entities[0].engine.parts[0].frame=.2f;
    Check(FrameBound(3.f,3)&&!FrameBound(-1.f,3)&&!FrameBound(4.f,3)&&!FrameBound(0.f,0),"asset frame gate handles native upper clamp and negative indices");
    Check(AnimationIndexBound(0,1)&&AnimationIndexBound(UINT32_MAX,0)&&!AnimationIndexBound(1,1)&&!AnimationIndexBound(UINT32_MAX-1,1),"mesh animation index admits only native sentinel or in-range table entry");
    f->entities[0].core.orientation.v[0]=0.f; Check(!CENetworkWorldAdapter::Validate(*f),"singular native inverse refused"); f->entities[0].core.orientation.v[0]=1.f;
    Check(CE_WorldEncode(f,bytes,CE_NET_MAX_MESSAGE,&size),"encode bounded state before adverse wire mutation");
    // Objective arrays follow the native world counters before the entity records.
    uint32_t position=59+8*NUM_WORLD_COUNTERS+8*(MAX_PRIMARY_OBJECTIVES+MAX_SECONDARY_OBJECTIVES)+16+1+strlen(f->entities[0].core.class_name)+1+strlen(f->entities[0].core.mesh);
    CE_NetReader location; CE_NetReaderInit(&location,bytes+position,4);
    Check(CE_NetReadF32(&location)==1.f,"wire mutation selects the native world X field");
    CE_NetWriter corrupt; CE_NetWriterInit(&corrupt,bytes+position,4); CE_NetWriteF32(&corrupt,3.402823466e38f);
    copy->tick=999; Check(!CE_WorldDecode(copy,bytes,size)&&copy->tick==999,"finite out-of-range payload leaves destination unchanged");
    poisoned=false; { ApplyScope scope; }
    Check(!applying&&poisoned,"failed mutation scope retires applying and requires teardown");
    poisoned=false; { ApplyScope scope; scope.committed=true; }
    Check(!applying&&!poisoned,"committed mutation scope retires applying without poisoning");
    poisoned=false;
    active=true; config.role=CE_WORLD_REPLICA; applied_tick=0;
    Check(!CE_WorldCanRender(),"unprepared replica cannot render native pose arrays");
    applied_tick=140; Check(CE_WorldCanRender(),"successful baseline enables native render");
    poisoned=true; Check(!CE_WorldCanRender(),"poisoned world cannot render a partial apply");
    binding_count=1; CE_WorldEnd(); Check(CE_WorldActive()&&poisoned,"poisoned live world retains event fence until teardown");
    binding_count=0; CE_WorldEnd(); Check(!CE_WorldActive()&&!poisoned&&CE_WorldCanRender(),"completed teardown clears poison and restores offline rendering");
    Vec unused=BeamStart(false,NULL); Check(unused.v[0]==0.f&&unused.v[1]==0.f&&unused.v[2]==0.f,"non-beam capture never reads unused native field");
    active=true; config.role=CE_WORLD_HOST; remote_invert_ground=0; remote_invert_jet=1;
    Check(CE_WorldInvertPitch(1,1,1)==0&&CE_WorldInvertPitch(1,0,0)==1,"guest walk and jet inversion override different host preferences once");
    Check(CE_WorldInvertPitch(0,1,1)==1,"host local inversion unchanged"); CE_WorldEnd();
    active=true; config.role=CE_WORLD_HOST; remote_walker_any=0;
    Check(CE_WorldWalkerBindingType(1,MOUSE_AXIS_PLUS_ANY)==MOUSE_AXIS_PLUS,"guest non-ANY preference overrides differing host slot1 ANY binding");
    remote_walker_any=1; Check(CE_WorldWalkerBindingType(1,KEY_ON)==MOUSE_AXIS_PLUS_ANY,"guest ANY preference overrides host slot1 keyboard binding");
    Check(CE_WorldWalkerBindingType(0,KEY_ON)==KEY_ON,"host local walker binding unchanged"); CE_WorldEnd();
    Check(CE_WorldWalkerBindingType(1,MOUSE_AXIS_MINUS_ANY)==MOUSE_AXIS_MINUS_ANY,"offline walker binding unchanged");
    CE_WorldInfo info={}; Check(CE_WorldFrameInfo(f,&info)&&info.frame_sequence==1&&info.tick==140,"explicit frame metadata");
    f->phase=GAME_STATE_PLAYER_2_WON; f->paused=1; f->sequence=2; f->end_count=5.f; f->alpha=160;
    Check(CE_WorldEncode(f,bytes,CE_NET_MAX_MESSAGE,&size)&&CE_WorldDecode(copy,bytes,size)&&copy->end_count==5.f&&copy->alpha==160,"terminal presentation advances sequence with simulation tick held");
    CE_WorldFrame *events=Fixture(); events->birth_count=2;
    ExplosionBirth &born=events->births[0]; born.serial=1; born.id=100; born.tick=140; born.ready=1; born.definition=12; born.schema=4321;
    born.radius=2.f; born.allegiance=2; born.map_who=1; born.mask=THING_TYPE_EXPLOSION; born.originator=1; Name(born.name,"test plain explosion");
    for(unsigned j=0;j<9;++j) born.orientation.v[j]=j%4==0?1.f:0.f;
    ExplosionBirth &deep=events->births[1]; deep.serial=2; deep.id=101; deep.tick=139; deep.deep=deep.ready=1; deep.pos={{0.f,0.f,100.f}};
    deep.orientation=born.orientation;
    Check(CE_WorldEncode(events,bytes,CE_NET_MAX_MESSAGE,&size)&&CE_WorldDecode(copy,bytes,size)&&copy->birth_count==2&&copy->births[0].radius==2.f&&copy->births[1].deep,"plain and separate deep-water birth records round trip");
    Check(copy->entities[0].engine.cockpit.old_shake.v[0]==-.001f&&copy->entities[0].engine.cockpit.old_frame==.25f&&copy->entities[1].engine.cockpit.schema==123457,"birth journal retains both genuine owner-bound cockpit cohorts");
    Check(CE_WorldEncode(copy,again,sizeof again,&n)&&n==size&&!memcmp(bytes,again,size),"birth journal explicit fields retain canonical bytes");
    // Maximum independently admitted field counts can exceed the transport's aggregate bound. This is wire data,
    // not a fabricated native world: refusal must still retain the output canary and return no partial frame.
    CE_WorldFrame *maximum=Fixture(); maximum->birth_count=kBirths;
    char longest[kName]; memset(longest,'x',sizeof longest-1); longest[sizeof longest-1]=0;
    for(unsigned i=0;i<kBirths;++i) {
        maximum->births[i]=born; maximum->births[i].serial=i+1; maximum->births[i].id=1000+i;
        Name(maximum->births[i].name,longest);
    }
    for(unsigned i=maximum->count;i<kEntities;++i) {
        maximum->entities[i].core=maximum->entities[2].core;
        maximum->entities[i].core.id=i+1; maximum->entities[i].core.initial_number=i+40;
        Name(maximum->entities[i].core.mesh,longest);
    }
    maximum->count=kEntities;
    for(unsigned p=0;p<2;++p) {
        EngineState &e=maximum->entities[p].engine; e.part_count=kParts; e.weapon_count=kWeapons;
        for(unsigned i=1;i<kParts;++i) e.parts[i]=e.parts[0];
        for(unsigned i=0;i<kWeapons;++i) {
            e.weapons[i]=e.weapons[0]; Name(e.weapons[i].name,longest); Name(e.weapons[i].mode,longest);
        }
    }
    Check(CENetworkWorldAdapter::Validate(*maximum),"independent maximum field counts remain structurally valid before aggregate encoding");
    bytes[CE_NET_MAX_MESSAGE]=0xa5;
    Check(!CE_WorldEncode(maximum,bytes,CE_NET_MAX_MESSAGE,&n)&&!n&&bytes[CE_NET_MAX_MESSAGE]==0xa5,"composed cockpit and birth frame cannot exceed transport64KiB or expose partial output");
    CE_WorldFrameDestroy(maximum);
    uint32_t planned=0; Check(CENetworkWorldAdapter::PlanBirths(*events,planned)&&planned==2&&birth_applied==0,"planning births cannot acknowledge or execute them");
    birth_applied=2; events->tick=200; Check(CENetworkWorldAdapter::PlanBirths(*events,planned)&&planned==2,"already-applied old journal duplicates cannot replay native Init");
    birth_applied=0; events->tick=142; Check(!CENetworkWorldAdapter::PlanBirths(*events,planned),"unapplied old birth refuses before native Init at foreground age limit");
    events->tick=140; born.serial=2; deep.serial=3; Check(!CENetworkWorldAdapter::PlanBirths(*events,planned),"missing birth serial refuses before native Init"); born.serial=1; deep.serial=2;
    events->birth_count=kBirths+1; Check(!CE_WorldEncode(events,again,sizeof again,&n),"journal64 capacity refuses explicitly"); events->birth_count=2;
    active=true; config={CE_WORLD_HOST,7,0,852}; captured_sequence=10; captured_tick=140; birth_generated=2;
    Check(CE_WorldEncode(events,bytes,sizeof bytes-1,&n)&&birth_published==0,"capture and encode do not publish a birth watermark");
    Check(CE_WorldFramePublished(events)&&birth_published==2&&published_frame==1,"only explicit successful-send publication admits included birth serials");
    deep.serial=3; born.serial=2; events->sequence=2; birth_generated=3;
    Check(CE_WorldEncode(events,bytes,sizeof bytes-1,&n)&&birth_published==2,"Send0 caller omission leaves newer birth unpublished");
    CE_WorldInput ack={}; ack.last_applied_birth_serial=3;
    Check(!ValidBirthAck(ack,false),"ACK cannot retire a newer replacement before actual transport publication");
    Check(CE_WorldFramePublished(events)&&birth_published==3,"newer accepted replacement publishes its included maximum");
    events->sequence=1; Check(!CE_WorldFramePublished(events)&&published_frame==2&&birth_published==3,"stale publication cannot roll back accepted frame or birth watermarks"); events->sequence=2;
    ack.last_applied_birth_serial=4; Check(!ValidBirthAck(ack,false),"future birth ACK refused");
    birth_ack=2; ack.last_applied_birth_serial=1; Check(ValidBirthAck(ack,false)&&ValidBirthAck(ack,true),"independent cumulative ACK may overtake fresh or duplicate immutable input");
    birth_ack=0;
    birth_count=3; for(unsigned i=0;i<3;++i) birth_journal[i].serial=i+1;
    RetireBirths(2); Check(birth_count==1&&birth_journal[0].serial==3&&birth_ack==2,"admitted ACK retires only its prefix without waiting for gameplay consumption");
    RetireBirths(1); Check(birth_count==1&&birth_journal[0].serial==3&&birth_ack==2,"older gameplay ACK cannot roll back independent cumulative retirement");
    events->birth_count=0; events->sequence=3; Check(CE_WorldFramePublished(events)&&birth_published==3,"empty replacement cannot invent publication of generated but absent births");
    config.role=CE_WORLD_REPLICA; Check(!CE_WorldFramePublished(events),"replica cannot publish host history");
    applied_tick=140; Check(CE_WorldCanRender(),"prepared replica may render before an unsupported native allocation attempt");
    Check(!CE_WorldExplosionSpawnAllowed(),"uncontrolled replica explosion factory fenced before allocation");
    Check(poisoned&&!CE_WorldCanRender(),"unrequested replica explosion cannot be silently discarded and then rendered");
    creating_explosion=true; Check(!CE_WorldExplosionSpawnAllowed(),"poisoned replica cannot reopen the controlled factory window"); creating_explosion=false;
    CE_WorldEnd(); active=true; config={CE_WORLD_REPLICA,7,1,852};
    creating_explosion=true; Check(CE_WorldExplosionSpawnAllowed(),"fresh controlled authoritative birth opens only the native factory call"); creating_explosion=false;
    CE_WorldEnd(); Check(CE_WorldExplosionSpawnAllowed(),"offline explosion factory remains native");
    CE_WorldFrameDestroy(events);
    CE_WorldFrameDestroy(copy); CE_WorldFrameDestroy(f);
    printf("{\"checks\":%u,\"game_initialization_tested\":false,\"playability_tested\":false}\n",checks);
}
