#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual public-host and shell-prefix recovery controls, with authored D3D/message boundaries.

No game files, desktop, GPU or Windows process is used. The generated shell prefix omits joystick polling but retains
the actual trailing network service boundary and actual session service/active bodies over authored dependencies.
Production message dispatch, cooperative-level and readiness branches remain intact. Bounded loss prefixes
do not establish the cause of a player's driver failure or impose a device-recovery timeout.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import resource
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
from build import network_view  # the patched shell as a build with the network sees it (CE_NETWORK_OFF resolved)
SUPPORT=ROOT/'port/tools/tests'
PUBLIC=ROOT/'port/platform/public'
CASES=('no-loss','race-before-reset','loss-1','loss-6','loss-7','loss-8','restore','repeated-restore',
       'minimize','resize','close','cleanup','permanent','cooperative-lost','cooperative-error','pause',
       'reentrant','reset-error','init-error','faithful','zero-elapsed','quit-message','cooperative-ok-pending',
       'presentation-caller-retry','presentation-caller-null-device')
NETWORK_CASES=('net-zero-ready','net-shared-wait','net-inactive','net-wmquit','net-admission-error',
               'net-recovery','net-close','net-device-sticky','initial-cooperative-error',
               'zero-healthy','initial-cooperative-lost-zero')

def run(args,**kwargs):
    def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    return subprocess.run([str(a) for a in args],text=True,capture_output=True,timeout=60,
                          preexec_fn=no_core,**kwargs)

def require_run(args,**kwargs):
    result=run(args,**kwargs)
    if result.returncode:raise RuntimeError(f'{args}\n{result.stdout}{result.stderr}')
    return result.stdout

def shell_prefix(out,helpers,omit_pending=False,omit_zero_guard=False,network_mutant=None,off=False,omit_wait=False):
    out.mkdir(parents=True,exist_ok=True)
    (out/'ltshell.cpp').write_bytes((ROOT/'src/ltshell.cpp').read_bytes())
    for path in sorted((ROOT/'port/patches').glob('*.patch')):
        for block in re.split(r'(?=^--- a/)',path.read_text(),flags=re.M):
            if block.startswith('--- a/ltshell.cpp\n'):
                require_run(['patch','--batch','--fuzz=0','-p1','-d',out,'--forward','-r','-'],input=block)
    # off=True is the shell as the default public build compiles it (CE_NETWORK_OFF resolved): the same recovery, with no session service
    # (no session_device_state, no CE_SessionActive) and the function's plain closing return.
    source=network_view((out/'ltshell.cpp').read_text(),off=off)
    if omit_wait:   # authored control: the lost-device wait of the recovery loop removed (the default shell's only wait there)
        anchor='if (recovery == D3DERR_DEVICELOST) Sleep(10);'
        if not off or source.count(anchor)!=1:raise AssertionError('wait mutation anchor')
        source=source.replace(anchor,'/* authored control: no wait */')
    definition=helpers.function(source,r'^BOOL PCLTShell::HandlingSystemStuff\s*\(')
    if off:
        if 'CE_SessionActive' in definition or 'session_device_state' in definition:raise AssertionError('the default shell still names the network session')
        footer='return cant_proc;\n'
        body=definition[definition.index('{')+1:definition.index('// process joystick')]+footer
    else:
        footer_start=definition.rfind('if (CE_SessionActive())')
        if footer_start<definition.index('// process joystick'):raise AssertionError('actual network service footer missing')
        footer=definition[footer_start:definition.rfind('}')]
        body=definition[definition.index('{')+1:definition.index('// process joystick')]+footer
    body=body.replace('LT.GetHWnd()','app.GetHWnd()')
    if omit_zero_guard:
        old='return cant_proc; // A zero clock delta must not turn unavailable resources into a runnable frame.'
        if body.count(old)!=1:raise AssertionError('zero-delta mutation anchor')
        body=body.replace(old,'return S_OK; // authored old zero-delta control')
    if omit_pending:
        start=body.index('#if defined(BEA_PUBLIC_GRAPHICS_HOST)')
        end=body.index('#endif',start)+len('#endif')
        body=body[:start]+body[end:]
    if network_mutant=='zero-delta-bypasses-service':
        old='&& m_bFrameMoving && !CE_SessionActive()'
        if body.count(old)!=1:raise AssertionError('network zero-delta mutation anchor')
        body=body.replace(old,'&& m_bFrameMoving')
    elif network_mutant=='omit-device-observation':
        old='session_device_state = SUCCEEDED(recovery) ? 1 : 2;'
        if body.count(old)!=1:raise AssertionError('device observation mutation anchor')
        body=body.replace(old,'/* authored control: leave pending observation unknown */')
    elif network_mutant=='omit-session-service':body=body.replace(footer,'return cant_proc;\n')
    aliases='\n'.join(f'auto &{name}=app.{name};' for name in
        ('m_bActive','m_bReady','m_bFrameMoving','m_bWindowed','m_Adapters','m_dwAdapter','m_pD3D',
         'm_d3dpp','m_pd3dDevice','m_fTime','m_fElapsedTime','m_bSingleStep','m_hWnd'))
    prefix=('static BOOL ActualRecoveryShell(CD3DApplication &app,bool ordinary) {\n'+aliases+
            '\nauto &mMsg=lastMessage; void *mhAccel=nullptr;\n'
            'const bool sPortQuitRequested=ordinary && quitCount;\n'
            'auto DeviceRecoveryPending=[&]{return app.DeviceRecoveryPending();};\n'
            'auto RecoverLostDevice=[&]{return app.RecoverLostDevice();};\n'+body+'\n}\n')
    (out/'public_device_recovery_shell.h').write_text(prefix)
    session=(ROOT/'port/game/ce_network_session.cpp').read_text()
    service=helpers.function(session,r'^int CE_SessionSystemService\s*\(').replace(
        'int CE_SessionSystemService(', 'int ActualSessionSystemService(',1)
    active=helpers.function(session,r'^int CE_SessionActive\s*\(')
    if network_mutant in ('session-always-active','session-always-offline'):
        active='int CE_SessionActive() { return '+('1' if network_mutant=='session-always-active' else '0')+'; }'
    (out/'public_device_recovery_service.h').write_text(service+'\n'+active+'\n')
    platform=(ROOT/'src/Platform.h').read_text()
    declarations=[]
    for name,text in (('Stage',session),('WaitPhase',session),('EQuitType',platform)):
        match=re.search(r'\benum '+name+r'\s*\{[^}]+\};',text,re.S)
        if not match:raise AssertionError(f'actual enum missing: {name}')
        declarations.append(match.group())
    (out/'public_device_recovery_session_types.h').write_text('\n'.join(declarations)+'\n')
    return hashlib.sha256(source.encode()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-ref',help='optional local frozen graphics-host commit for exact caller-output comparison')
    parser.add_argument('--mutants',action='store_true')
    parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--syntax-targets',action='store_true')
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('host_tests',ROOT/'port/tools/public_d3dapp_tests.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    out=ROOT/'local-data/public-device-recovery-tests'
    out.mkdir(parents=True,exist_ok=True)
    helpers.recovery_source(out/'recovery',True)
    shell_sha=shell_prefix(out/'shell',helpers)
    common=['clang++','-std=c++14','-DCE_VERSION=1','-DBEA_PUBLIC_D3DAPP_TEST',
            '-DBEA_PUBLIC_GRAPHICS_HOST=1','-I',SUPPORT,'-I',out/'recovery','-I',out/'shell']
    if args.sanitizers:common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    driver=SUPPORT/'public_device_recovery_driver.cpp'
    binary=out/'driver'
    require_run(common+[PUBLIC/'d3dapp.cpp',driver,'-o',binary])
    observations={name:require_run([binary,name]) for name in CASES+NETWORK_CASES}
    # The same recovery in the default public build (patch 0085 applies there too): the shell as CE_NETWORK_OFF resolves it, the same
    # d3dapp.cpp, the same cases. Its observations must be the network build's for every case that needs no session.
    shell_prefix(out/'shell-off',helpers,off=True)
    off_common=[item for item in common];off_common[off_common.index(out/'shell')]=out/'shell-off'
    off_binary=out/'driver-off'
    require_run(off_common+['-DCE_NETWORK_OFF=1',PUBLIC/'d3dapp.cpp',driver,'-o',off_binary])
    off_observations={name:require_run([off_binary,name]) for name in CASES}
    for name in CASES:
        if off_observations[name]!=observations[name]:raise AssertionError(f'default build observation differs from the network build: {name}')
    # Every pre-existing caller observation must remain exactly unchanged, including options/creation fallback.
    controls=[]
    for name in helpers.CASES+helpers.OWNERSHIP+helpers.MAPPING:controls.append([name])
    controls += [['window-control',name] for name in helpers.WINDOWED]
    controls += [['publication-control',name] for name in helpers.PUBLICATION]
    old_binary,new_binary=out/'old-controls',out/'new-controls'
    base_driver=SUPPORT/'public_d3dapp_driver.cpp'
    stubs=SUPPORT/'public_display_mode_stubs.cpp'    # this build is CE_VERSION: d3dapp.cpp's display-mode references
    require_run(common+[PUBLIC/'d3dapp.cpp',base_driver,stubs,'-o',new_binary])
    if args.before_ref:
        before=out/'before.cpp'
        before.write_text(require_run(['git','show',f'{args.before_ref}:port/platform/public/d3dapp.cpp']))
        require_run(common+[before,base_driver,stubs,'-o',old_binary])
    for case in controls:
        new=require_run([new_binary]+case)
        if args.before_ref:
            old=require_run([old_binary]+case)
            if old!=new:raise AssertionError(f'pre-existing output changed: {case}\nOLD {old}\nNEW {new}')
    mutations={}
    if args.mutants:
        text=(PUBLIC/'d3dapp.cpp').read_text()
        substitutions={
            'restore-blocking-retry':('if (result == D3DERR_DEVICELOST && resetting && recoveryAttempt) return result;',
                                      '/* authored control: retain old blocking retry */'),
            'omit-pending-readiness':('m_bReady = FALSE;\n        recoveryOwner = this;',
                                      'm_bReady = TRUE;\n        recoveryOwner = this;'),
            'pause-publishes-early':('m_bReady = pauseDepth == 0 && !DeviceRecoveryPending() && presentationFailureOwner != this;',
                                      'm_bReady = pauseDepth == 0;'),
            'admit-reentrant-recovery':('if (recoveryAttempt) return D3DERR_DEVICELOST;',
                                       '/* authored control: reentrant entry admitted */'),
            'hide-presentation-pending':('bool CD3DApplication::DeviceRecoveryPending() const { return recoveryOwner == this || presentationFailureOwner == this; }',
                                         'bool CD3DApplication::DeviceRecoveryPending() const { return recoveryOwner == this; }'),
        }
        selected={'restore-blocking-retry':'loss-7','omit-pending-readiness':'loss-1',
                  'pause-publishes-early':'pause','admit-reentrant-recovery':'reentrant',
                  'hide-presentation-pending':'presentation-caller-retry'}
        for name,(old,new) in substitutions.items():
            if text.count(old)!=1:raise AssertionError(f'mutation anchor {name}')
            source=out/f'{name}.cpp';source.write_text(text.replace(old,new))
            executable=out/name
            require_run(common+[source,driver,'-o',executable])
            observed=run([executable,selected[name]])
            if observed.returncode==0:raise AssertionError(f'mutant survived: {name}')
            mutations[name]={'case':selected[name],'exit':observed.returncode,
                             'stdout':observed.stdout,'stderr':observed.stderr}
        shell_prefix(out/'shell-no-pending',helpers,True)
        executable=out/'omit-pending-poll'
        alt=[item for item in common]
        alt[alt.index(out/'shell')]=out/'shell-no-pending'
        require_run(alt+[PUBLIC/'d3dapp.cpp',driver,'-o',executable])
        observed=run([executable,'loss-1'])
        if observed.returncode==0:raise AssertionError('mutant survived: omit-pending-poll')
        mutations['omit-pending-poll']={'case':'loss-1','exit':observed.returncode,
                                        'stdout':observed.stdout,'stderr':observed.stderr}
        # the default build's own shell: the two guards of patch 0085 removed one at a time must each be refused there as well
        for name,case,keywords in (('off-omit-pending-poll','loss-1',dict(omit_pending=True)),
                                   ('off-omit-zero-delta-guard','zero-elapsed',dict(omit_zero_guard=True)),
                                   ('off-omit-lost-device-wait','loss-7',dict(omit_wait=True))):
            folder=out/('shell-'+name);shell_prefix(folder,helpers,off=True,**keywords)
            executable=out/(name+'-driver')
            alt=[item for item in common];alt[alt.index(out/'shell')]=folder
            require_run(alt+['-DCE_NETWORK_OFF=1',PUBLIC/'d3dapp.cpp',driver,'-o',executable])
            observed=run([executable,case])
            if observed.returncode==0:raise AssertionError(f'mutant survived: {name}')
            mutations[name]={'case':case,'exit':observed.returncode,'stdout':observed.stdout,'stderr':observed.stderr}
        shell_prefix(out/'shell-zero-delta',helpers,omit_zero_guard=True)
        executable=out/'omit-zero-delta-guard'
        alt=[item for item in common];alt[alt.index(out/'shell')]=out/'shell-zero-delta'
        require_run(alt+[PUBLIC/'d3dapp.cpp',driver,'-o',executable])
        observed=run([executable,'zero-elapsed'])
        if observed.returncode==0:raise AssertionError('mutant survived: omit-zero-delta-guard')
        mutations['omit-zero-delta-guard']={'case':'zero-elapsed','exit':observed.returncode,
                                          'stdout':observed.stdout,'stderr':observed.stderr}
        for name,case in (('zero-delta-bypasses-service','net-zero-ready'),
                          ('omit-device-observation','net-device-sticky'),
                          ('omit-session-service','net-zero-ready'),
                          ('session-always-active','net-zero-ready'),
                          ('session-always-offline','net-zero-ready')):
            folder=out/name;shell_prefix(folder,helpers,network_mutant=name)
            executable=out/(name+'-driver')
            alt=[item for item in common];alt[alt.index(out/'shell')]=folder
            require_run(alt+[PUBLIC/'d3dapp.cpp',driver,'-o',executable])
            observed=run([executable,case])
            if observed.returncode==0:raise AssertionError(f'mutant survived: {name}')
            mutations[name]={'case':case,'exit':observed.returncode,'stdout':observed.stdout,'stderr':observed.stderr}
        source=out/'duplicate-network-yield.cpp'
        old=('// The shell owns yielding: active sessions must reach their one bounded service/yield boundary.\n'
             '    if (result == D3DERR_DEVICELOST) return result;')
        if text.count(old)!=1:raise AssertionError('duplicate yield mutation anchor')
        source.write_text(text.replace(old,old.replace('return result;','{ Sleep(10); return result; }')))
        executable=out/'duplicate-network-yield'
        require_run(common+[source,driver,'-o',executable])
        observed=run([executable,'net-recovery'])
        if observed.returncode==0:raise AssertionError('mutant survived: duplicate-network-yield')
        mutations['duplicate-network-yield']={'case':'net-recovery','exit':observed.returncode,
                                             'stdout':observed.stdout,'stderr':observed.stderr}
    if args.syntax_targets:
        # The existing helper materializes normalized real consumer headers, including the shell layout assertion.
        syntax=out/'syntax';helpers.target_syntax(syntax)
        build_spec=importlib.util.spec_from_file_location('recovery_target_build',ROOT/'port/build.py')
        build=importlib.util.module_from_spec(build_spec);build_spec.loader.exec_module(build)
        source=network_view((out/'shell/ltshell.cpp').read_text(),off=False)
        body=helpers.function(source,r'^BOOL PCLTShell::HandlingSystemStuff\s*\(')
        footer=body[body.rfind('if (CE_SessionActive())'):body.rfind('}')]
        body=body[:body.index('// process joystick')]+footer+'}\n'
        caller=syntax/'headers/public-recovery-caller.cpp'
        caller.write_text('#include "common.h"\n#include "ltshell.h"\n#include "DXUtil.h"\n#include "Platform.h"\n#include "ce_network_session.h"\n'
                          'static bool sPortQuitRequested;\n'+body)
        for name in ('linux','windows'):
            target=build.TARGETS[name]
            inc=[ROOT/'port/d3dx/include',ROOT/'port/include',ROOT/'port/game']+target['includes']+[syntax/'headers',syntax/'headers/MissionScript']
            command=target['cxx']+['-std=gnu++14']+build.MSCXX+build.COMMON+build.X87+[
                '-DCE_VERSION=1','-DBEA_PUBLIC_GRAPHICS_HOST=1','-fsyntax-only','-include',ROOT/'port/include/port.h']
            command += [item for path in inc for item in ('-I',path)]+[caller]
            require_run(command)
            print(f'{name}: actual patched recovery shell prefix compiles against real target headers')
    result={'kind':'AUTHORED_ACTUAL_SOURCE_CONTROLS_NOT_HARDWARE_ACCEPTANCE',
            'source_sha256':hashlib.sha256((PUBLIC/'d3dapp.cpp').read_bytes()).hexdigest(),
            'patched_shell_sha256':shell_sha,'cases':observations,'existing_observations':len(controls),
            'unchanged_old_observations':len(controls) if args.before_ref else None,
            'before_ref':args.before_ref,'causal_mutants':mutations,
            'network_service_sha256':hashlib.sha256((out/'shell/public_device_recovery_service.h').read_bytes()).hexdigest(),
            'network_cases':list(NETWORK_CASES),'default_build_cases':list(CASES),
            'default_build_shell_sha256':hashlib.sha256((out/'shell-off/public_device_recovery_shell.h').read_bytes()).hexdigest(),
            'sanitizers':args.sanitizers,'syntax_targets':args.syntax_targets,
            'limits':['No Windows/GPU/game execution','Pending messages and device losses are authored',
                      'No attribution to the player report','No timeout is inferred from bounded prefixes',
                      'Actual session service uses authored admission/transport/window dependencies, not peer networking',
                      'Initial non-LOST/non-NOTRESET cooperative errors retain their existing canceled loop']}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    history='unchanged old' if args.before_ref else 'current'
    print(f'default public build (CE_NETWORK_OFF): {len(off_observations)} recovery cases, observations equal the network build\'s')
    print(f'{len(CASES)} recovery + {len(NETWORK_CASES)} network/boundary cases; {len(controls)} {history} observations; {len(mutations)} causal refusals')

if __name__=='__main__':main()
