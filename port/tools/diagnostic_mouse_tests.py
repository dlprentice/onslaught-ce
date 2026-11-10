#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise authored diagnostic mouse ingress through production shims and actual mouse poll callers.

SDL events are initialized without video/gamepads/windows/devices. Queue-only causality uses SDL's real cached
mouse query; other cases control only SDL focus/window/physical-state boundaries. Generated callers and receipts
stay in ignored local-data/port/. --source-root selects shim bytes, --expect-old reproduces the unchanged missing
sampler case. No game/retail inputs or graphics are used; this is not physical input or compositor proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile

import deps
import input_lifetime_tests as callers
import menu_driver_markers
from window_tests import private_ui_compile_args

ROOT=Path(__file__).resolve().parents[2]
CASES=("queue-causal","raw-queue-only","final-motion","additive-once","physical-hold-survives-release",
       "unset","unset-after-feed","focus","focus-race","focus-queued-epoch","destroy-epoch","wrong-event-window","queue-filter-failure","pair-bounds","reacquire","before-acquire","wrong-window","destroy",
       "replay","record","record-queue","replay-queue","queue-failure","overflow","sample-overflow","invalid-pairs","invalid-motion","button-map",
       "wrapped-button","malformed-head","malformed-motion","complete-token-bounds","coordinate-pair","legacy-menu-malformed","legacy-record-malformed")

def sha(raw):return hashlib.sha256(raw).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--case',choices=CASES,action='append')
    parser.add_argument('--expect-old',action='store_true')
    args=parser.parse_args();selected=args.case or CASES
    if args.expect_old and selected!=['queue-causal']:parser.error('--expect-old requires only --case queue-causal')
    parent=ROOT/'local-data/port/diagnostic-mouse-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));receipt={'source_root':str(args.source_root.resolve()),'patches':[],'cases':[],
       'limits':['Actual production window/dinput and full PollMouseState/ShutdownMouse/HandleMouseMessage bodies.',
                 'SDL event subsystem only, no video/gamepad/device/window/game/GPU initialization.',
                 'Queue causality uses real SDL_GetRelativeMouseState; other physical state/focus/window APIs are bounded fakes.',
                 'No physical mouse, game aim/fire, presentation or player-feel proof.']};passed=False
    try:
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        sources={name:(ROOT/'src'/name).read_bytes() for name in callers.SOURCES}
        receipt['caller_source_sha256']={n:sha(raw) for n,raw in sources.items()}
        for name,raw in sources.items():(out/name).write_bytes(raw)
        for patch in sorted((ROOT/'port/patches').glob('*.patch')):
            raw=patch.read_bytes();sections=[s for s in re.split(br'(?=^--- a/)',raw,flags=re.MULTILINE)
                if any(s.startswith(f'--- a/{name}\n+++ b/{name}\n'.encode()) for name in callers.SOURCES)]
            if not sections:continue
            result=subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-d',str(out),'-r','-'],input=b''.join(sections),capture_output=True,timeout=10)
            receipt['patches'].append({'name':patch.name,'sha256':sha(raw),'exit':result.returncode})
            if result.returncode:raise RuntimeError('actual caller patch failed: '+patch.name)
        bodies={name:callers.definition((out/file).read_text(encoding='latin-1'),signature) for name,(file,signature) in callers.FUNCTIONS.items()}
        receipt['functions_sha256']={name:sha(body.encode()) for name,body in bodies.items()};(out/'callers.inc').write_text('\n'.join(bodies.values()))
        for name in ('window.cpp','dinput.cpp','portlog.cpp','inventory.cpp','input_activity.h','portlog.h'):
            raw=(args.source_root/'port/platform/common'/name).read_bytes();(out/name).write_bytes(raw)
            receipt.setdefault('shims_sha256',{})[name]=sha(raw)
        (out/'ce_tutorial_pad.h').write_bytes((args.source_root/'port/include/ce_tutorial_pad.h').read_bytes())    # dinput.cpp's tutorial pad query types
        have_api=b'void PortDinputDiagnosticMouseEvent(' in (out/'dinput.cpp').read_bytes();receipt['diagnostic_API_present']=have_api
        header=(ROOT/'src/ltshell.h').read_text(encoding='latin-1');accessors=[]     # the shell's inline joystick accessors (JOY_BUTTON_ACCESSORS of the shared driver)
        for accessor in ('JoyButtonOnce','JoyButtonOn','JoyButtonRelease'):
            found=re.findall(r'\bBOOL\s+'+accessor+r'\([^)]*\)\s*\{[^}]*\}',header,re.DOTALL)
            if len(found)!=1:raise RuntimeError('expected one inline input accessor: '+accessor)
            accessors.append(found[0])
        support=menu_driver_markers.expand((ROOT/'port/tools/tests/input_lifetime_driver.cpp').read_text().replace('JOY_BUTTON_ACCESSORS','\n'.join(accessors)),ROOT).split('int main(int argc, char **argv)')[0]
        receipt['shared_fixture_source_sha256']=sha((ROOT/'port/tools/tests/input_lifetime_driver.cpp').read_bytes())
        support=support.replace('static bool focused = true, relativeMode, injectFocusLoss;',
            'static bool focused = true, relativeMode, injectFocusLoss, realRelativeCache, faithfulProfile; static unsigned testPresent;')
        support=support.replace('static SDL_MouseButtonFlags TestRelativeState(float *x, float *y) {',
            'static SDL_MouseButtonFlags TestRelativeState(float *x, float *y) { if(realRelativeCache) return SDL_GetRelativeMouseState(x,y);')
        support=support.replace('static SDL_WindowID TestWindowID(SDL_Window *) { return 17; }',
            'static SDL_WindowID TestWindowID(SDL_Window *w) { return w == testWindow ? 17 : 18; }\nstatic void TestDestroyWindow(SDL_Window *) {}\n#define SDL_DestroyWindow TestDestroyWindow')
        support=support.replace('unsigned PortPresentCount(void) { return 0; }','unsigned PortPresentCount(void) { return testPresent; }')
        support=support.replace('bool PortCeLayer() { return true; }','bool PortCeLayer() { return !faithfulProfile; }')
        for name,path in {'WINDOW_CPP':out/'window.cpp','DINPUT_CPP':out/'dinput.cpp','PORTLOG_CPP':out/'portlog.cpp','INVENTORY_CPP':out/'inventory.cpp','CALLERS_INC':out/'callers.inc'}.items():support=support.replace(name,str(path))
        (out/'support.inc').write_text(support)
        driver=(ROOT/'port/tools/tests/diagnostic_mouse_driver.cpp').read_text().replace('TEST_SUPPORT_INC',str(out/'support.inc'))
        (out/'driver.cpp').write_text(driver);receipt['driver_sha256']=sha(driver.encode());receipt['support_sha256']=sha(support.encode())
        binary=out/'driver';command=[str(deps.DATA/'llvm/bin/clang++'),'--target=i686-pc-linux-gnu','-m32','-std=c++17','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fshort-wchar','-fms-extensions','-fno-autolink','-w','-O0','-DCE_VERSION="diagnostic-mouse-test"',f'-DDIAGNOSTIC_API={int(have_api)}',f'-I{deps.PORT/"include/linux"}',f'-I{deps.DATA/"dxvk-native/usr/include/dxvk"}',f'-I{deps.PREFIX/"include"}',f'-I{ROOT/"port/platform/common"}',str(out/'driver.cpp'),f'-L{deps.PREFIX/"lib"}','-lSDL3',f'-Wl,-rpath,{deps.PREFIX/"lib"}','-o',str(binary)]
        result=subprocess.run(command + private_ui_compile_args(args.source_root / "port"),capture_output=True,text=True,timeout=60,env=dict(os.environ,TMPDIR=str(out)))
        receipt['compile']={'command':command,'exit':result.returncode,'output':result.stdout+result.stderr}
        if result.returncode:raise RuntimeError('diagnostic caller fixture did not compile')
        env={k:v for k,v in os.environ.items() if not k.startswith(('BEA_PORT_','SDL_'))};env['SDL_VIDEO_DRIVER']='dummy';env['SDL_AUDIO_DRIVER']='dummy'
        for profile in ('ce','faithful'):
            for case in selected:
                result=subprocess.run([str(binary),case,profile],capture_output=True,text=True,timeout=10,env=env)
                valid=result.returncode==(1 if args.expect_old else 0)
                if args.expect_old:valid=valid and '"relative_x":0,"relative_y":0,"held":false,"release":false' in result.stdout
                if case in ('queue-failure','queue-filter-failure','overflow','sample-overflow','invalid-pairs','wrapped-button','malformed-head','malformed-motion') and not args.expect_old:
                    valid=valid and result.stderr.count('diagnostic mouse fixture failed:')==1
                if case=='invalid-motion':valid=valid and result.stderr.count('diagnostic mouse fixture failed:')==4
                receipt['cases'].append({'case':case,'profile':profile,'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr,'pass':valid,'old_defect_expected':args.expect_old})
                print(('PASS ' if valid else 'FAIL ')+profile+' '+case)
        passed=bool(receipt['cases']) and all(r['pass'] for r in receipt['cases'])
    except (OSError,RuntimeError,subprocess.TimeoutExpired) as error:receipt['error']=str(error);print('FAIL '+str(error))
    receipt['pass']=passed;(out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(out/'result.json');return 0 if passed else 1

if __name__=='__main__':raise SystemExit(main())
