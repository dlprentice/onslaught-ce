#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual loaded-admission/native getter/query and owner-configuration controls, no assets or game.

Prepared loaded-object/OS boundaries are controlled. Target objects compile against the actual selected
native headers. Host controls do not establish native actor initialization, gameplay, file provenance,
Session ordering, visual/audio behavior or native Windows execution.
"""
import difflib
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile
from network_world_tests import actual_body
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
import build

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def body(s,sig):return actual_body(s,sig)
def old(path):return subprocess.check_output(['git','show','5294f6f6:'+path],cwd=ROOT,text=True)
def main():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    outroot=ROOT/'local-data/port/network-loaded-admission-tests';outroot.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=outroot));mirror=out/'mirror';mirror.mkdir()
    result={'passed':False,'limits':__doc__,'stages':[]}
    def run(name,cmd,expected=0,needle=None,timeout=60):
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=timeout);(out/(name+'.stdout')).write_text(p.stdout);(out/(name+'.stderr')).write_text(p.stderr)
        okay=p.returncode==expected and (needle is None or needle in p.stdout+p.stderr)
        result['stages'].append({'name':name,'command':cmd,'exit':p.returncode,'expected':expected,'needle':needle,'passed':okay,'stdout':p.stdout,'stderr':p.stderr})
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(('PASS ' if okay else 'FAIL ')+name,flush=True)
        return okay
    def finish(ok):
        result['passed']=ok;(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(out/'result.json',flush=True);return 0 if ok else 1
    # A fresh current header closure, ordinary normalization and every selected native patch; no game data.
    sources=[p for p in (ROOT/'src').rglob('*') if p.is_file()]
    index=build.CaseIndex([mirror/p.relative_to(ROOT/'src') for p in sources]);build.SYSTEM_NAMES=build.system_header_names()
    selected=[p for p in sources if p.suffix.lower() in ('.h','.hpp','.inl') or p.name in ('game.cpp','world.cpp','BattleEngineConfigurations.cpp','FEPSaveGame.cpp')]
    for p in selected:
        target=mirror/p.relative_to(ROOT/'src');target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(build.normalise(p.read_text(),mirror,index,[mirror,mirror/'MissionScript']))
    names={str(p.relative_to(ROOT/'src')) for p in selected};result['patches']={}
    for patch in sorted((ROOT/'port/patches').glob('*.patch')):
        sections=re.split(r'(?=^--- a/)',patch.read_text(),flags=re.M)
        chosen=''.join(s for s in sections if s.startswith('--- a/') and s.splitlines()[0][6:] in names)
        if chosen:
            p=subprocess.run(['patch','--batch','--fuzz=0','-p1'],input=chosen,capture_output=True,text=True,cwd=mirror,timeout=15)
            result['patches'][patch.name]={'sha256':sha(patch),'exit':p.returncode,'log':p.stdout+p.stderr}
            if p.returncode:return finish(False)
    prod=out/'production';(prod/'game').mkdir(parents=True);(prod/'platform/common').mkdir(parents=True)
    for name in ('ce_network_loaded_admission.cpp','ce_network_loaded_admission.h'):(prod/'game'/name).write_bytes((ROOT/'port/game'/name).read_bytes())
    result['production_sha256']={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'port/game/ce_network_loaded_admission.cpp',ROOT/'port/game/ce_network_loaded_admission.h',ROOT/'port/patches/0083-loaded-admission.patch',ROOT/'port/platform/common/window.cpp',ROOT/'port/platform/common/replay.cpp',ROOT/'port/platform/common/clock.cpp',ROOT/'port/game/updatedump.cpp']}
    result['native_sha256']={str(p.relative_to(mirror)):sha(p) for p in mirror.rglob('*') if p.is_file()}
    source=(prod/'game/ce_network_loaded_admission.cpp').read_text();validator=source[source.index('namespace {'):]
    flags=list(dict.fromkeys(re.findall(r'CLIPARAMS\.(m\w+)',source)))
    query=(mirror/'FEPSaveGame.cpp').read_text();native=(mirror/'BattleEngineConfigurations.cpp').read_text()
    replacements={'/* RAW_ACCESSOR */':body((mirror/'BattleEngineConfigurations.h').read_text(),'static const char* NetworkConfigurationName('),
                  '/* NATIVE_FALLBACK */':body(native,'CBattleEngineData* UBattleEngineConfigurations::GetConfiguration('),
                  '/* NATIVE_GAME_MP */':body((mirror/'game.cpp').read_text(),'BOOL CGame::IsMultiplayer()'),
                  '/* NATIVE_WORLD_MP */':body((mirror/'world.cpp').read_text(),'BOOL CWorld::IsMultiplayer()'),
                  '/* NATIVE_CHEAT_TABLE */':query[query.index('char cheatname[6][256]'):query.index('extern char gAllCheatsEnabled')],
                  '/* NATIVE_CHEAT_QUERY */':body(query,'BOOL CFEPSaveGame::IsCheatActive('),
                  '/* ACTUAL_VALIDATOR */':validator,'/* CLI_FLAGS */':'\n'.join('FLAG('+f+')' for f in flags),
                  '/* FLAG_MEMBERS */':','.join('&CCLIParams::'+f for f in flags)}
    template=(ROOT/'port/tools/tests/network_loaded_admission_driver.cpp.in').read_text()
    for key,value in replacements.items():assert template.count(key)==1,key;template=template.replace(key,value)
    cpp=out/'admission.cpp';cpp.write_text(template)
    host=['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-parentheses','-DCE_VERSION="fixture"','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    okay=run('loaded-build',[*host,str(cpp),'-o',str(out/'loaded')])
    if okay:okay=run('loaded-controls',[str(out/'loaded')])
    # (the network flavour's text of these files: the guards that keep them main's in a default build are no-ops there, and the pieces cut out below
    # would otherwise carry half of a conditional)
    net=lambda path:build.network_view(path.read_text(),off=False)
    window=net(ROOT/'port/platform/common/window.cpp');update=net(ROOT/'port/game/updatedump.cpp');replay=net(ROOT/'port/platform/common/replay.cpp');oldwindow=old('port/platform/common/window.cpp');oldupdate=old('port/game/updatedump.cpp')
    def window_prefix(s):
        begin=s.index('{',s.index('void Pump()'));end=s.index('\n\tif (fakeKeys && *fakeKeys)',begin)
        return s[begin:end]+'\n\t(void)fakeKeys;\n}\n'
    def update_prefix(s):
        begin=s.index('\tif (!gInit)',s.index('void PortUpdateHook()'));end=s.index('\n\tgUpdate++;',begin);return s[begin:end]
    start=window.index('bool gWindowAutomationReady');end=window.index('\nvoid Pump()',start)
    # (FeelScriptedKeys, the frame-feel script's key pump, stands between the configuration and Pump: it pushes SDL key events and is no admission owner, so the slice stops before it)
    feel_keys=window.find('// Authored keys on a wall-clock timeline',start)
    if 0<=feel_keys<end:end=feel_keys
    start_update=update.index('static bool gDumpConfigReady');end_update=update.index('\nextern "C" void CE_NetUpdateAutomationPrepare',start_update)
    pace_start=replay.index('\tif (!gPacingObserved)',replay.index('void PortReplayFrame('));pace_end=replay.index('\n\tif (gRecording)',pace_start)
    clock=net(ROOT/'port/platform/common/clock.cpp')
    owners={'/* CLOCK_MODE */':body(clock,'int Mode()'),'/* CLOCK_IDLE */':body(clock,'extern "C" int CE_NetClockOrdinary('),'/* WINDOW_STATE_AND_PREP */':window[start:end], '/* WINDOW_IDLE */':body(window,'extern "C" int CE_NetWindowAutomationIdle('),
            '/* WINDOW_PREFIX */':window_prefix(window),'/* ORIGINAL_WINDOW_PREFIX */':window_prefix(oldwindow),
            '/* UPDATE_STATE_AND_PREP */':update[start_update:end_update],'/* UPDATE_IDLE */':body(update,'extern "C" int CE_NetUpdateAutomationIdle('),
            '/* UPDATE_PREFIX */':update_prefix(update),'/* ORIGINAL_UPDATE_PREFIX */':update_prefix(oldupdate),
            '/* REPLAY_IDLE */':body(replay,'extern "C" int CE_NetAutomationIdle('),'/* REPLAY_PACING */':replay[pace_start:pace_end]}
    owner=(ROOT/'port/tools/tests/network_loaded_admission_owner.cpp.in').read_text()
    for key,value in owners.items():assert owner.count(key)==1,key;owner=owner.replace(key,value)
    owner_cpp=out/'owners.cpp';owner_cpp.write_text(owner)
    owner_built=run('owner-build',[*host,str(owner_cpp),'-o',str(out/'owners')]);okay=okay and owner_built
    if owner_built:
        for case in ('clock-cold','clock-real','clock-virtual','cold','off-owner','prepare-idle','disabled-sticky','keys-history','click-history','dump-history','dump-range','replay-present','replay-pacing','replay-modes'):
            okay=run('owner-'+case,[str(out/'owners'),case]) and okay
    # Exact source mutations must make their designated assertions fail.
    mutants=[('engine-id',template,'engine->mConfigurationId != 0 || ','','invalid engine ID'),
             ('engine-pointer',template,'engine->mConfiguration != data ||','','foreign same-name'),
             ('nonunique',template,'matches != 1','matches == 0','duplicate named'),
             ('start-id',template,'start->mInit.mConfigurationId != 0 || ','','start raw index'),
             ('cold-cheat',template,'if (CE_CheatCachedStatus(&cheats) != 1)','if (CE_CheatCachedStatus(&cheats) < 0)','unknown cheat cache'),
             ('frame-clock',template,'GAME.mFrameLength != 0.05f || ','','native frame clock'),
             ('god-layer',template,'CAREER.GetIsGod(i) || ','','career god'),
             ('window-history',owner,'!gWindowAutomationConfigured && ','','exhausted keys history'),
             ('dump-disabled',owner,'if (gDumpRequested)','if (true)','cached disabled choice'),
             ('clock-lazy',owner,'const int mode = gMode.load();','const int mode = Mode();','cold clock query'),
             ('replay-observation',owner,'if (!gInit || !gPacingObserved)','if (!gInit)','Init alone lacks pacing')]
    result['mutants']=[]
    for name,text,before,after,needle in mutants:
        assert text.count(before)==1,(name,text.count(before));candidate=out/(name+'.cpp');candidate.write_text(text.replace(before,after,1));binary=out/(name+'-mutant')
        built=run('mutant-build-'+name,[*host,str(candidate),'-o',str(binary)])
        args=[]
        if text is owner:args=[{'window-history':'keys-history','dump-disabled':'disabled-sticky','replay-observation':'replay-present','clock-lazy':'clock-cold'}[name]]
        passed=built and run('mutant-'+name,[str(binary),*args],2,needle);okay=passed and okay;result['mutants'].append({'name':name,'passed':passed})
    # Compile genuine module/owner source with the selected native headers and each pinned target ABI.
    for platform,target in build.TARGETS.items():
        includes=[ROOT/'port/d3dx/include',ROOT/'port/include',ROOT/'port/game',*target['includes'],mirror,mirror/'MissionScript']
        common=[*target['cxx'],'-std=gnu++14',*build.MSCXX,*build.COMMON,*build.X87,*build.NO_MATH_BUILTINS,'-DCE_VERSION="fixture"','-include',str(ROOT/'port/include/port.h'),*['-I'+str(p) for p in includes]]
        okay=run(platform+'-module-object',[*common,'-c',str(ROOT/'port/game/ce_network_loaded_admission.cpp'),'-o',str(out/(platform+'-loaded.o'))]) and okay
        okay=run(platform+'-update-object',[*common,'-c',str(ROOT/'port/game/updatedump.cpp'),'-o',str(out/(platform+'-update.o'))]) and okay
        sdl=build.deps.PREFIX if platform=='linux' else build.deps.PREFIX_WIN
        native=[*target['cxx'],'-std=gnu++17','-O2','-g','-fshort-wchar','-DNOMINMAX','-DCE_VERSION="fixture"',*['-I'+str(p) for p in [ROOT/'port/include',ROOT/'port/d3dx/include',*target['includes'],sdl/'include']]]
        for name in ('replay','window','clock'):
            okay=run(platform+'-'+name+'-object',[*native,'-c',str(ROOT/'port/platform/common'/(name+'.cpp')),'-o',str(out/(platform+'-'+name+'.o'))]) and okay
    result['generated_sha256']={p.name:sha(p) for p in (cpp,owner_cpp)}
    return finish(okay)
if __name__=='__main__':raise SystemExit(main())
