#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Couple actual compact lifecycle/API/codec bodies to native explosion Init/Hit/grow/factory/Shutdown bodies.

The controlled base geometry, renderer, particle and sound interfaces expose call order and lifetime only.
This gate does not establish native actor ABI, live rendering/audio, resource-pool behavior or FX equivalence.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def body(text,sig):
    a=text.index(sig); b=text.index('{',a); depth=0
    while ';' in text[a:b]:
        a=text.index(sig,a+len(sig)); b=text.index('{',a)
    for i in range(b,len(text)):
        if text[i]=='{': depth+=1
        elif text[i]=='}':
            depth-=1
            if not depth: return text[a:i+1]
    raise ValueError(sig)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--fixture',required=True,type=Path);args=ap.parse_args()
    fixture=args.fixture.resolve();source=(fixture/'production/game/ce_network_world.cpp').read_text();mirror=fixture/'mirror'
    parent=ROOT/'local-data/port/network-lifecycle-native';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    template=(ROOT/'port/tools/tests/network_world_lifecycle_native.cpp.in').read_text()
    pins={}
    def take(text,sig):
        code=body(text,sig);pins[sig]=hashlib.sha256(code.encode()).hexdigest();return code
    types='\n'.join(take(source,'struct '+name)+';' for name in ('Vec','Mat','Animation','Core','CockpitState','RoundState','RoundBirth','ExplosionBirth','ExplosionState','Binding'))
    globals=source[source.index('const unsigned kLifeOps'):source.index('struct Binding')]
    helper_sigs=('int Fail(const char *message)','int FailKind(const char *what, const char *name)','bool Name(char *out, const char *in)',
                 'Vec GetVec(const FVector &v)','void PutVec(FVector &out, const Vec &v)','Mat GetMat(const FMatrix &m)','void PutMat(FMatrix &out, const Mat &m)',
                 'Binding *ByThing(const CThing *thing)','Binding *ById(uint32_t id)','uint32_t Id(CThing *thing)','CThing *Resolve(uint32_t id)',
                 'bool Range(float value,float low,float high)','bool VecBound(const Vec &v,float limit=1048576.f)','bool MatBound(const Mat &m)','bool TimeBound(float time)',
                 'bool AnimationBound(const Animation &a)','Vec BeamStart(bool beam,const FVector *position)','struct ApplyScope')
    helpers='\n'.join(take(source,sig)+(';' if sig.startswith('struct ') else '') for sig in helper_sigs)
    methods=('    struct MeshSchema','    static bool ExplosionDefinition(','    static bool BirthBound(','    static void BirthFailure(',
             '    static CWeapon *WeaponAt(','    static bool RoundDefinition(','    static bool RoundEntryValues(','    static bool PulseMode(','    static uint32_t ShotBegin(','    static void ShotEnd(','    static void RoundEntry(','    static void RoundComplete(','    static void RoundShutdown(','    static bool RoundCapture(','    static void RoundApply(',
             '    static bool LifeDefinition(','    static bool LifeEntry(','    static void LifeComplete(','    static void LifeForget(',
             '    static void ExplosionBirthEntry(','    static void ExplosionReady(','    static bool ExplosionCapture(',
             '    static bool BirthCompatible(','    static void ExplosionApply(','    static bool LifeCompatible(','    static bool LifeOwner(','    static bool InitialLifecycleContext(','    static bool FrameResources(','    static bool LifeMutate(')
    adapter='\n'.join(take(source,sig)+(';' if sig.startswith('    struct ') else '') for sig in methods)
    core=take(source,'    static bool CoreCapture(').replace('CoreCapture(','BoundaryCoreCapture(',1)
    adapter+='\n'+core+'\n static bool CoreCapture(CThing*t,Core&out){++core_captures;return BoundaryCoreCapture(t,out);}\n'+take(source,'    static void CoreApply(')
    compatible=take(source,'    static bool Compatible(')
    expl=take(compatible,'        if(s.core.kind==EXPLOSION_THING)')+'\n'+take(compatible,'        if(s.core.kind==ROUND_THING)')
    adapter+='\n static bool Validate(const CE_WorldFrame&f){return f.valid;}\n static bool Apply(const CE_WorldFrame&f){if(poisoned)return false;if(!f.commit){poisoned=f.partial;return false;}applied_tick=f.tick;applied_sequence=f.sequence;return true;}\n static CThing*Baseline(const Core&c){for(unsigned i=0;i<binding_count;++i)if(c.initial_number&&bindings[i].initial_number==c.initial_number)return bindings[i].thing;return nullptr;}\n static bool Compatible(CThing*thing,const Entity&s){if(compatibility_refusal)return false;if(!thing||ThingKind(thing)!=s.core.kind||strcmp(thing->_GetClassName(),s.core.class_name))return false;if(thing->mThingType!=s.core.type)return false;if(((uint32_t)(unsigned short)thing->mFlags^s.core.flags)&(TF_IN_MAP_WHO|TF_IS_BIG_THING))return false;\n'+expl+'\n return true;}\n'
    codecs=('void WriteName(','void ReadName(','void WriteVec(','void ReadVec(','void WriteMat(','void ReadMat(',
            'void WriteCore(','void ReadCore(','void WriteRound(','void ReadRound(','void WriteRoundBirth(','void ReadRoundBirth(','bool RoundRecordBound(','void WriteBirth(','void ReadBirth(','void WriteExplosion(','void ReadExplosion(',
            'void LifeReset(','void LifeInvalidateProof(','bool LifeRecordProof(','bool LifeSame(','bool LifeRecordBound(','bool LifeDependency(','bool EncodeLife(const LifeRecord &r,','bool DecodeLife(const uint8_t *bytes,',
            'bool LifeKeyFor(','bool LifeSourceHeader(LifeRecord &r,','bool LifeAppend(LifeRecord &r)','bool LifeRetire(')
    codec='\n'.join(take(source,sig) for sig in codecs)
    api='\n'.join(take(source,sig) for sig in ('int CE_WorldFramePublished(','int CE_WorldApply(','int CE_WorldLifecycleStart(','int CE_WorldPeekLifecycle(','int CE_WorldLifecyclePublished(','int CE_WorldInspectLifecycle(',
          'int CE_WorldApplyLifecycle(','int CE_WorldAppliedLifecycle(','int CE_WorldAcknowledgeLifecycle('))
    wrappers='\n'.join(take(source,sig) for sig in ('void CE_WorldExplosionInitialized(','void CE_WorldLifecycleForget(','void CE_WorldLifecycleDestroyed(','uint32_t CE_WorldShotBegin(','void CE_WorldShotEnd(','void CE_WorldRoundBirth(','void CE_WorldRoundInitialized(','void CE_WorldRoundShutdown(','int CE_WorldActorInitPhase('))
    native='\n'.join(take((mirror/'thing.cpp').read_text(),sig) for sig in ('CThing::CThing()','CComplexThing::CComplexThing()',
                    'void\tCThing::AddShutdownEvent()','void\tCComplexThing::AddShutdownEvent()','void CThing::Shutdown()','CThing::~CThing()'))
    native+='\n'+take((ROOT/'src/InitThing.cpp').read_text(),'CInitThing::CInitThing() :')
    native+='\n'+'\n'.join(take((mirror/'explosion.cpp').read_text(),sig) for sig in ('void\tCExplosion::Init(CInitThing *init)',
                    'void\tCExplosion::Hit(CThing *other_thing, CCollisionReport *report)','void\tCExplosion::Unknown66()',
                    'void\tCExplosion::HandleEvent(CEvent *event)','float\tCExplosion::GetRadius()','float\tCExplosion::Unknown67()',
                    'void\tCExplosion::SetThingType(ULONG t)'))
    native+='\n'+take((mirror/'WorldPhysicsManager.cpp').read_text(),'CExplosion *UPhysicsManager::SpawnExplosion(SINT index)')
    round_native=(mirror/'Round.cpp').read_text(); actor_native=(mirror/'actor.cpp').read_text()
    native+='\n'+'\n'.join(take(round_native,sig) for sig in ('CRound::CRound(CRoundData *data)','void CRound::Init(CInitThing *init)','void CRound::Shutdown()','float CRound::Gravity()','inline BOOL CRound::ClipToGround()','inline BOOL CRound::CanGoUnderWater()'))
    native+='\n'+take(actor_native,'void\tCActor::Init(CInitThing* init)')+'\n'+take(actor_native,'void\tCActor::AddMoveEvent(CEvent* event)')
    native+='\n'+take((mirror/'WorldPhysicsManager.cpp').read_text(),'CRound *UPhysicsManager::SpawnRound(CRoundData *data)')
    native+='\n'+take((mirror/'thing.cpp').read_text(),'void CThing::Init(CInitThing* init)').replace('void CThing::Init(','void CThing::RoundThingInit(',1)
    native+='\n'+take((mirror/'thing.cpp').read_text(),'void\tCThing::InitCollisionSeekingThing(CInitCSThing *init)')
    collision=(mirror/'collisionseekingthing.cpp').read_text()
    native+='\n'+take(collision,'void CCSPersistentThing::Init(CInitCSThing *init)')+'\n'+take(collision,'void CCSPersistentThing::HandleEvent(CEvent *event)')
    dispatch=take(collision,'void CCollisionSeekingThing::Unknown6(CCollisionSeekingThing *other)')
    stop=dispatch.index('return;',dispatch.index('if (!(mCollisionFlags & 0x400))'))+len('return;')
    # Exact entry-admission guards, followed by an instrumented collision-interface continuation, not geometry.
    native+='\n'+dispatch[:stop].replace('CCollisionSeekingThing *other','')+'\n ++collision_continuations;\n}'
    # Exact selected early-return prefix; seek!=0 is refused before Init, so the unavailable seeking family is not modeled.
    seek=take(round_native,'void CRound::Unknown4dac90(CScheduledEvent *reuse)')
    stop=seek.index('return;',seek.index('if (mData->mUnknown48 != 1)'))+len('return;')
    native+='\n'+seek[:stop]+'\n assert(false && "unadmitted seeking family");\n}'
    boundaries=(ROOT/'port/tools/tests/network_world_round_boundaries.cpp.in').read_text()
    physics=(mirror/'WorldPhysicsManager.h').read_text()
    round_fields=physics[physics.index('\tCParticleDescriptor\t*mParticleA;',physics.index('class CRoundData\n')):physics.index('\n};',physics.index('class CRoundData\n'))]
    mode_fields=physics[physics.index('\tchar\t\t\t\t*mClipName;',physics.index('class CWeaponModeData')):physics.index('\n};',physics.index('class CWeaponModeData'))]
    boundaries=boundaries.replace('/* ROUND_DATA_FIELDS */','struct CRoundData{'+round_fields+'\n};').replace('/* MODE_DATA_FIELDS */','struct CWeaponModeData{'+mode_fields+'\n};')
    boundaries=boundaries.replace('/* ROUND_INIT_CLASS */',take((mirror/'InitThing.h').read_text(),'class CRoundInitThing : public CInitThing')+';')
    replacements={'/* VECTOR_MAGNITUDE */':take((ROOT/'src/maths.h').read_text(),'float\tMagnitude() const'),
                  '/* GAME_STATE_ENUM */':take((mirror/'game.h').read_text(),'enum EGameState')+';',
                  '/* INIT_DERIVED_CLASS */':take((mirror/'InitThing.h').read_text(),'class CExplosionInitThing : public CInitThing')+';',
                  '/* PROTOCOL_STRUCTS */':types,'/* LIFECYCLE_GLOBALS */':globals,'/* HELPERS */':helpers,
                  '/* BIND */':take(source,'Binding *Bind(CThing *thing, uint32_t id, uint32_t initial)'),
                  '/* ADAPTER */':adapter,'/* SPAWN_ALLOWED */':take(source,'int CE_WorldExplosionSpawnAllowed()')+'\n'+take(source,'int CE_WorldRoundSpawnAllowed()'),
                  '/* LIFECYCLE_WRAPPERS */':wrappers,'/* LIFECYCLE_CODEC */':codec,'/* LIFECYCLE_API */':api,
                  '/* ACTUAL_NATIVE */':native,'/* TESTS */':(ROOT/'port/tools/tests/network_world_lifecycle_native_cases.cpp.in').read_text(),
                  '/* ROUND_BOUNDARY_TYPES */':boundaries, '/* ROUND_TESTS */':(ROOT/'port/tools/tests/network_world_round_native_cases.cpp.in').read_text()}
    readiness=take(source,'int CE_WorldFrameReadiness(')
    assert readiness.index('LifeReadiness(*frame)')<readiness.index('FrameResources(*frame)')
    text=template
    for marker,value in replacements.items():
        assert text.count(marker)==1,marker
        text=text.replace(marker,value)
    cpp=out/'witness.cpp';cpp.write_text(text);(out/'source-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
    defs=['-DCE_WORLD_HEADER="'+str(fixture/'production/game/ce_network_world.h')+'"','-DCE_NET_HEADER="'+str(fixture/'production/network/network.h')+'"','-DNATIVE_THING_TYPES="'+str(mirror/'ThingType.h')+'"']
    cmd=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*defs,str(cpp),str(fixture/'production/network/codec.cpp'),'-o',str(out/'witness')]
    spawn=take((mirror/'weapon.cpp').read_text(),'BOOL CWeapon::SpawnRound()')
    stages=('CE_WorldRoundSpawnAllowed()','WeaponFired(this)','CE_WorldShotBegin(this,mode)','SOUND.PlayEffect','UPhysicsManager::SpawnRound','GetLaunchPosition','GAME.FloatRandom()','round->mUnknownEC = GetAttachedTo()','round->Init(&init)','if (WeaponMode_mParticleA(mode))','if (mode->mClipName)','RecoilWeapon(this)','CE_WorldShotEnd(this,ceShot)')
    positions=[spawn.index(stage) for stage in stages];assert positions==sorted(positions),positions
    fire=take((mirror/'weapon.cpp').read_text(),'BOOL CWeapon::Fire()');assert fire.index('CE_WorldRoundSpawnAllowed()')<fire.index('mCharge = 0')
    (out/'source-order.json').write_text(json.dumps({'spawn_sha256':hashlib.sha256(spawn.encode()).hexdigest(),'fire_sha256':hashlib.sha256(fire.encode()).hexdigest(),'stages':list(zip(stages,positions)),'limits':'first-hand actual source positions, not executed native weapon Fire/muzzle/sound'},indent=2)+'\n')
    result={'fixture':str(fixture),'fixture_receipt_sha256':sha(fixture/'result.json'),'compile':cmd,'source_pins_sha256':sha(out/'source-pins.json'),
            'limits':__doc__}
    proc=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'build.log').write_text(proc.stdout+proc.stderr);result['compile_exit']=proc.returncode
    if not proc.returncode:
        proc=subprocess.run([str(out/'witness')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True,timeout=10)
        result.update(exit=proc.returncode,stdout=proc.stdout,stderr=proc.stderr,binary_sha256=sha(out/'witness'))
        print(proc.stdout+proc.stderr,end='')
    result['native_positive_passed']=not result['compile_exit'] and result.get('exit')==0 and not result.get('stderr')
    result['passed']=False
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['native_positive_passed']:
        def mutate(sig,old,new):
            function=body(text,sig)
            assert function.count(old)==1,(sig,old)
            return text.replace(function,function.replace(old,new,1),1)
        variants=[
            ('missing-common-completion',mutate('void\tCExplosion::Init(CInitThing *init)','CE_WorldExplosionInitialized(this,init);',''),
             'actual common Init completion produces exactly one capsule'),
            ('missing-ready',mutate('void\tCExplosion::Init(CInitThing *init)','CE_WorldExplosionReady(this,init);',''),
             'actual common Init completion produces exactly one capsule'),
            ('unfenced-hit',mutate('void\tCExplosion::Hit(CThing *other_thing, CCollisionReport *report)','if (CE_WorldReplica()) return;',''),
             'actual direct Hit/grow guards block authoritative callbacks'),
            ('unfenced-grow',mutate('void\tCExplosion::Unknown66()','if (CE_WorldReplica()) return;',''),
             'actual replica grow guard preserves authoritative radius'),
            ('missing-pending-token',mutate('    static void LifeComplete(',
             'if(life_failed||life_pending!=explosion) { BirthFailure("compact Init completion token was invalidated"); return; }',''),
             'repeated completion cannot capture or append again'),
            ('missing-dependency-bytes',mutate('bool LifeDependency(',
             'r.dependency.size==life_keys[i].size&&!memcmp(r.dependency.bytes,life_keys[i].bytes,r.dependency.size)','r.dependency.size==life_keys[i].size'),
             'byte-exact baseline dependency refusal precedes irreversible Init'),
            ('early-application-proof',mutate('int CE_WorldApplyLifecycle(',
             'if(!CENetworkWorldAdapter::LifeMutate(r)) return -1;','life_applied=r.info.operation; if(!CENetworkWorldAdapter::LifeMutate(r)) return -1;'),
             'partial native Apply poisons without watermark advance'),
            ('unpublished-ACK',mutate('int CE_WorldAcknowledgeLifecycle(',
             'ack->operation>life_published','ack->operation>life_generated').replace('if(!slot.published) break;','',1),
             'unpublished ACK cannot retire generated source history'),
            ('lower-publication-ceiling',mutate('int CE_WorldLifecyclePublished(',
             'slot.birth_serial>birth_published','true'),
             'older despawn cannot lower legacy published birth ceiling'),
            ('relabel-old-source-tick',mutate('int CE_WorldApplyLifecycle(',
             'if(r.info.source_tick<life_committed_tick||r.info.source_tick<applied_tick)','if(false)'),
             'unseen old event refuses before factory instead of relabeling source age'),
            ('unrequested-factory',mutate('CExplosion *UPhysicsManager::SpawnExplosion(SINT index)',
             'if (!CE_WorldExplosionSpawnAllowed()) return NULL;',''),
             'unrequested native factory refuses before allocation or heap read'),
            ('defer-known-identity-conflict',mutate('int CE_WorldApplyLifecycle(',
             'Binding *known=ById(r.info.id);','if(r.info.operation!=life_applied+1) return 0; Binding *known=ById(r.info.id);'),
             'future-order defer cannot hide baseline-owner destruction'),
            ('defer-unknown-round-definition',mutate('    static bool FrameResources(',
             'if(!RoundDefinition(data,schema)||strcmp(s.core.class_name,"CRound")||schema!=s.round.schema||!s.round.incarnation||\n                   s.round.incarnation<=life_applied||s.round.incarnation>frame.lifecycle_watermark)','if(false)'),
             'malformed deferred round definition refuses before waiting'),
            ('missing-initial-byte-comparison',mutate('int CE_WorldLifecycleStart(',
             '||size!=life_baseline_size||memcmp(bytes,life_baseline_bytes,size)',''),
             'same epoch sequence tick changed Core pose refuses prior-proof mismatch'),
            ('missing-publication-proof',mutate('int CE_WorldFramePublished(',
             'if(!LifeRecordProof(*frame)) return Fail("published frame cannot retain its exact local baseline proof");',''),
             'exact successful publication retains candidate byte proof'),
            ('replace-live-owned-baseline',mutate('bool LifeRecordProof(',
             'if(life_enabled) return true;',''),
             'lifecycle-enabled later publication cannot overwrite initial owned baseline proof'),
            ('ignore-failed-full-Apply',mutate('int CE_WorldApply(',
             'if(!applied||poisoned)','if(poisoned)'),
             'failed Apply invalidates prior candidate and cannot mint a replacement'),
            ('scope-keeps-baseline-proof',mutate('struct ApplyScope',
             'LifeInvalidateProof();',''),
             'failed mutation scope invalidates prior byte proof without clearing owned baseline'),
            ('ignore-failed-birth-admission',mutate('int CE_WorldLifecycleStart(',
             '||birth_failed',''),
             'failed birth latch refuses without erasing prior byte proof'),
            ('admit-wrong-loaded-level',mutate('    static bool InitialLifecycleContext(',
             'GAME.GetCurrentLevel()==(int)config.level&&',''),
             'current loaded level mismatch refuses initial admission'),
            ('admit-wrong-player-count',mutate('    static bool InitialLifecycleContext(',
             'GAME.GetNPlayers()==2&&',''),
             'current player count mismatch refuses initial admission'),
            ('admit-terminal-native-phase',mutate('    static bool InitialLifecycleContext(',
             'GAME.GetGameState()<=GAME_STATE_PLAYING&&',''),
             'terminal current phase refuses initial admission'),
            ('admit-quit',mutate('    static bool InitialLifecycleContext(',
             'GAME.mQuit==QT_NONE&&',''),
             'pending quit refuses initial admission'),
            ('admit-invalid-event-epoch',mutate('    static bool InitialLifecycleContext(',
             'EVENT_MANAGER.IsValid()&&',''),
             'invalid event epoch refuses initial admission'),
        ]
        variants.extend([
            ('round-entry-hook',mutate('void CRound::Init(CInitThing *init)','CE_WorldRoundBirth(this,init);',''),
             'actual round Init common completion commits one immutable birth'),
            ('round-completion-hook',mutate('void CRound::Init(CInitThing *init)','CE_WorldRoundInitialized(this,init);',''),
             'actual round Init common completion commits one immutable birth'),
            ('round-endpoint-hook',mutate('void CRound::Shutdown()','CE_WorldRoundShutdown(this);',''),
             'native round Shutdown journals endpoint before killing its owned trail'),
            ('round-phase-random-rerun',mutate('void\tCActor::Init(CInitThing* init)','if (!CE_WorldActorInitPhase(this,r,&mDoFullMoveCount))','if (true)'),
             'controlled replica phase does not rerun RNG schedule gameplay or rewrite clocks'),
            ('missing-delayed-collision',mutate('void CCSPersistentThing::Init(CInitCSThing *init)','mCollisionFlags &= ~0x400;',''),
             'actual persistent Init clears collision admission before synchronous detector scan'),
            ('round-unrequested-factory',mutate('CRound *UPhysicsManager::SpawnRound(CRoundData *data)','if (!CE_WorldRoundSpawnAllowed()) return NULL;',''),
             'unrequested replica round allocation refuses before heap or constructor'),
            ('round-snapshot-incarnation',mutate('static bool Compatible(','binding->round_incarnation!=s.round.incarnation','false'),
             'same live object cannot adopt a different lifecycle incarnation through snapshot compatibility'),
            ('round-duplicate-shot',mutate('int CE_WorldApplyLifecycle(','r.round_birth.shot<=last_round_shot','false'),
             'reused source ShotID refuses before another native round Init'),
            ('round-inflates-explosion-publication',mutate('int CE_WorldLifecyclePublished(',
             '(info->kind==CE_WORLD_LIFECYCLE_BIRTH||info->kind==CE_WORLD_LIFECYCLE_DESPAWN)&&',''),
             'round publication changes no legacy explosion serial and cannot retire pending native birth'),
            ('round-old-nonzero-component-guard',mutate('    static bool RoundEntryValues(',
             'return std::isfinite(magnitude)&&magnitude>0.f;',
             'return birth.velocity.v[0]!=0.f||birth.velocity.v[1]!=0.f||birth.velocity.v[2]!=0.f;'),
             'native norm admission matches computed positive finite magnitude'),
            ('round-arbitrary-norm-epsilon',mutate('    static bool RoundEntryValues(',
             'magnitude>0.f','magnitude>0.000001f'),
             'native norm admission matches computed positive finite magnitude'),
            ('round-native-work-bound',mutate('    static bool LifeCompatible(',
             'if(!RoundEntryValues(data,record.round_birth))','if(false)'),
             'foreign finite lifespan refuses before native line-query work'),
            ('round-destroys-token',mutate('    static void LifeForget(',
             'if(round_pending==pointer) { round_pending=NULL; if(config.role==CE_WORLD_REPLICA) poisoned=true; BirthFailure("round destroyed before Init completion"); return; }',''),
             'heap-use-after-free'),
        ])
        result['mutants']=[]
        for name,code,expected in variants:
            folder=out/name;folder.mkdir(); mutant=folder/'witness.cpp';mutant.write_text(code)
            command=[str(mutant) if x==str(cpp) else str(folder/'witness') if x==str(out/'witness') else x for x in cmd]
            compiled=subprocess.run(command,capture_output=True,text=True,timeout=60);(folder/'build.log').write_text(compiled.stdout+compiled.stderr)
            ran=subprocess.run([str(folder/'witness')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True,timeout=10) if not compiled.returncode else None
            passed=not compiled.returncode and ran is not None and ran.returncode==(1 if expected=='heap-use-after-free' else 2) and expected in ran.stderr
            row={'name':name,'command':command,'compile_exit':compiled.returncode,'exit':ran.returncode if ran else None,'expected':expected,'passed':passed}
            if ran: (folder/'stdout.log').write_text(ran.stdout);(folder/'stderr.log').write_text(ran.stderr)
            result['mutants'].append(row);print(('PASS' if passed else 'FAIL')+' causal native control '+name,flush=True)
        result['passed']=all(x['passed'] for x in result['mutants'])
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True)
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
