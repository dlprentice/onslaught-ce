#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual public-host and whole-shell recovery controls, with authored D3D/message boundaries.

No game files, desktop, GPU or Windows process is used. The shell's message step (HandlingSystemStuff) is compiled whole,
as the patched ltshell.cpp has it, together with the text of the pad poll (UpdateJoystick), the release chain
(ReleaseDeviceObjects, InvalidateDeviceObjects, DeleteDeviceObjects, FreeDirectInput) and the statements of
InitDeviceObjects that make the pad state; the frame-end function (CPlatform::Flip) and the Window mode request
(ce_video_options.cpp) are the patched and real text too. A step therefore polls the pads over the state the release
frees, with fewer pads than slots, as a player's game does. Left out and authored instead: the rest of
InitDeviceObjects (the device-object walk and the pad enumeration), the wait screen's drawing, the pad devices and the
menu. The actual trailing network service boundary and session service/active bodies run over authored dependencies.
Bounded loss prefixes do not establish the cause of a player's driver failure or impose a device-recovery timeout.

--negative-ref REF builds the same driver over REF's patch queue, graphics host and Window mode request (the tracked
text of that commit) and requires each case listed in NEGATIVE to fail there: the cases that show the defects fixed after
the pre-freeze reading of 0.1.14. It needs the repository's history, so it is not part of the registered run.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
import build  # patched_text: the patched sources in the mirror's form; network_view: CE_NETWORK_OFF resolved either way
from build import network_view
SUPPORT=ROOT/'port/tools/tests'
PUBLIC=ROOT/'port/platform/public'
CASES=('no-loss','race-before-reset','loss-1','loss-6','loss-7','loss-8','restore','repeated-restore',
       'minimize','resize','close','cleanup','permanent','cooperative-lost','cooperative-error','pause',
       'reentrant','reset-error','init-error','faithful','zero-elapsed','quit-message','cooperative-ok-pending',
       'presentation-caller-retry','presentation-caller-null-device',
       # one case per reset caller whose reset fails with the device objects released, and the nested entries
       'options-reset-refused','options-reset-recovers','toggle-refused','window-switch-messages-first','resize-refused',
       'reset-reentrant','four-pads',
       # one Apply with the Window mode row and a row that asks for the options reset
       'apply-to-window','apply-to-fullscreen','apply-reset-refused')
# What each of these cases met at 3c2f5c40 (--negative-ref): a write or read through the freed pad state, a device used
# after it was dropped, a frame admitted on released objects, or a window switch inside the options reset.
NEGATIVE=('loss-1','loss-6','loss-7','loss-8','restore','repeated-restore','minimize','resize','close','cleanup','permanent',
          'cooperative-lost','cooperative-error','pause','reentrant','reset-error','init-error','faithful','quit-message',
          'cooperative-ok-pending','presentation-caller-null-device','options-reset-refused','options-reset-recovers',
          'toggle-refused','window-switch-messages-first','reset-reentrant','apply-to-window','apply-to-fullscreen',
          'apply-reset-refused')
NETWORK_CASES=('net-zero-ready','net-inactive','net-wmquit',
               'net-recovery','net-close','initial-cooperative-error',
               'zero-healthy','initial-cooperative-lost-zero',
               # the session's own FALSE returns (a WM_QUIT, the session's end) while the device objects are released
               'net-wmquit-released','net-exit-released')

def run(args,**kwargs):
    def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    return subprocess.run([str(a) for a in args],text=True,capture_output=True,timeout=60,
                          preexec_fn=no_core,**kwargs)

def require_run(args,**kwargs):
    result=run(args,**kwargs)
    if result.returncode:raise RuntimeError(f'{args}\n{result.stdout}{result.stderr}')
    return result.stdout

REAL=((r'^BOOL PCLTShell::HandlingSystemStuff\s*\(','BOOL PCLTShell::HandlingSystemStuff','BOOL ShellApplication::HandlingSystemStuff'),
      (r'^HRESULT PCLTShell::UpdateJoystick\s*\(','HRESULT PCLTShell::UpdateJoystick','HRESULT ShellApplication::UpdateJoystick'),
      (r'^VOID PCLTShell::FreeDirectInput\s*\(','VOID PCLTShell::FreeDirectInput','VOID ShellApplication::FreeDirectInput'),
      (r'^HRESULT PCLTShell::InvalidateDeviceObjects\s*\(','HRESULT PCLTShell::InvalidateDeviceObjects','HRESULT ShellApplication::RealInvalidateDeviceObjects'),
      (r'^HRESULT PCLTShell::DeleteDeviceObjects\s*\(','HRESULT PCLTShell::DeleteDeviceObjects','HRESULT ShellApplication::RealDeleteDeviceObjects'),
      (r'^HRESULT PCLTShell::ReleaseDeviceObjects\s*\(','HRESULT PCLTShell::ReleaseDeviceObjects','HRESULT ShellApplication::RealReleaseDeviceObjects'))

def patched(root,name,out):
    """src/NAME of the tree at root in the mirror's form (include names normalised, as the patches' context expects), with
    every section of that tree's patch queue for the file applied in order at fuzz 0."""
    out.mkdir(parents=True,exist_ok=True)
    if root==ROOT:text=build.patched_text([name])[name]
    else:
        (out/name).write_bytes(build.mirror_text(name,root=root))
        for path in sorted((root/'port/patches').glob('*.patch')):
            sections=[part for part in re.split(r'(?=^--- a/)',path.read_text(encoding='latin-1'),flags=re.M) if part.startswith(f'--- a/{name}\n')]
            if not sections:continue
            done=subprocess.run(['patch','--batch','--fuzz=0','-p1','-d',str(out)],input=''.join(sections).encode('latin-1'),capture_output=True)
            if done.returncode or b'fuzz' in done.stdout.lower():raise RuntimeError(f'{path.name} does not apply to {name} of {root}:\n{done.stdout.decode("latin-1")}')
        text=(out/name).read_bytes().decode('latin-1')
    (out/name).write_text(text,encoding='latin-1')
    return text

def reset_rows(out):
    """Every Video Options row that asks for the options reset, by the class whose SetValue asks. All of them ask through
    one function, which sets the request flag once per Apply, so the apply-* cases' one reset row stands for each of
    them. This fails when a row (or anything else) starts to ask another way."""
    pause=patched(ROOT,'PauseMenu.cpp',out)
    if len(re.findall(r'\bgDisplayResetState\s*=[^=]',pause))!=1 or 'inline void\tDisplaySettingChanged()' not in pause:
        raise AssertionError('PauseMenu.cpp: the options reset is no longer asked for in one place')
    rows=[]
    for match in re.finditer(r'^\tDisplaySettingChanged\(\);',pause,re.M):
        owner=re.findall(r'^void\s+(\w+)::SetValue\(',pause[:match.start()],re.M)
        between=pause[pause.rindex('::SetValue(',0,match.start()):match.start()]
        if not owner or '\n}\n' in between:raise AssertionError('a reset request outside a row\'s SetValue')
        rows.append(owner[-1])
    if len(rows)<7 or len(set(rows))!=len(rows):raise AssertionError(f'reset rows: {rows}')
    # the only other writers: the frame-end function clears the flag, and the options loader asks with 2
    writers={}
    for path in sorted((ROOT/'src').glob('*.cpp')):
        count=len(re.findall(r'\bgDisplayResetState\s*=[^=]',path.read_text(encoding='latin-1')))
        if count:writers[path.name]=count
    if writers!={'Career.cpp':1,'PauseMenu.cpp':1,'Platform.cpp':2,'ltshell.cpp':1}:raise AssertionError(f'writers of the reset request: {writers}')
    added=[path.name for path in sorted((ROOT/'port/patches').glob('*.patch'))
           if re.search(r'^\+.*\bgDisplayResetState\s*=[^=]',path.read_text(encoding='latin-1'),re.M)]
    removed=[path.name for path in sorted((ROOT/'port/patches').glob('*.patch'))
             if re.search(r'^-.*\bgDisplayResetState\s*=[^=]',path.read_text(encoding='latin-1'),re.M)]
    if added or removed!=['0057-pump-after-device-loss.patch']:raise AssertionError(f'patches that change the reset request: +{added} -{removed}')
    return rows

def shell_prefix(out,helpers,omit_pending=False,omit_zero_guard=False,network_mutant=None,off=False,omit_wait=False,
                 root=ROOT,shell_mutant=None):
    # off=True is the shell as the default public build compiles it (CE_NETWORK_OFF resolved): the same recovery, with no session service
    # (no session_device_state, no CE_SessionActive) and the function's plain closing return.
    source=network_view(patched(root,'ltshell.cpp',out),off=off)
    if omit_wait:   # authored control: the lost-device wait of the recovery loop removed (the default shell's only wait there)
        anchor='if (recovery == D3DERR_DEVICELOST) Sleep(10);'
        if not off or source.count(anchor)!=1:raise AssertionError('wait mutation anchor')
        source=source.replace(anchor,'/* authored control: no wait */')
    definition=helpers.function(source,r'^BOOL PCLTShell::HandlingSystemStuff\s*\(')
    whole=definition    # the function under test, all of it: every later edit is a named authored control
    if '// process joystick' not in definition or 'UpdateJoystick(i, first)' not in definition:raise AssertionError('the pad poll is not in the shell function')
    footer=None
    if off:
        if 'CE_SessionActive' in definition or 'session_device_state' in definition:raise AssertionError('the default shell still names the network session')
    else:
        footer_start=definition.rfind('if (CE_SessionActive())')
        if footer_start<definition.index('// process joystick'):raise AssertionError('actual network service footer missing')
        footer=definition[footer_start:definition.rfind('}')]
    if omit_zero_guard:
        old='return PORT_STEP_RESULT(cant_proc); // A zero clock delta must not turn unavailable resources into a runnable frame.'
        if definition.count(old)!=1:raise AssertionError('zero-delta mutation anchor')
        definition=definition.replace(old,'return S_OK; // authored old zero-delta control')
    if omit_pending:
        at=definition.index('HRESULT recovery = RecoverLostDevice();')
        start=definition.rindex('#if defined(BEA_PUBLIC_GRAPHICS_HOST)',0,at)
        end=definition.index('#endif',at)+len('#endif')
        definition=definition[:start]+definition[end:]
    if network_mutant=='zero-delta-bypasses-service':
        old='&& m_bFrameMoving && !CE_SessionActive()'
        if definition.count(old)!=1:raise AssertionError('network zero-delta mutation anchor')
        definition=definition.replace(old,'&& m_bFrameMoving')
    elif network_mutant=='omit-device-observation':
        old='session_device_state = SUCCEEDED(recovery) ? 1 : 2;'
        if definition.count(old)!=1:raise AssertionError('device observation mutation anchor')
        definition=definition.replace(old,'/* authored control: leave pending observation unknown */')
    elif network_mutant=='omit-session-service':definition=definition.replace(footer,'return cant_proc;\n')
    if shell_mutant=='pads-polled-while-released':
        old='if (mD3DActive)\n'
        if definition.count(old)!=1:raise AssertionError('pad poll guard anchor')
        definition=definition.replace(old,'if (true) /* authored control: poll whatever the release left */\n')
    elif shell_mutant=='no-idle-pads-for-the-last-pass':
        old='if (!mD3DActive)\n'
        if definition.count(old)!=1:raise AssertionError('idle pads anchor')
        definition=definition.replace(old,'if (false) /* authored control: the last pass reads what the release left */\n')
    elif shell_mutant:raise AssertionError(shell_mutant)
    pieces=[]
    for expression,old,new in REAL:
        text=definition if old.endswith('HandlingSystemStuff') else helpers.function(source,expression)
        if text.count(old)!=1:raise AssertionError(f'actual shell definition: {old}')
        pieces.append(text.replace(old,new))
    init=helpers.function(source,r'^HRESULT PCLTShell::InitDeviceObjects\s*\(')
    start=init.index('mD3DActive = TRUE;')
    end=init.index('}',init.index('mOldJoyState[i]',init.index('for (int i=0;i<MAX_JOYPADS;i++)',start)))+1
    made='\n'.join(line for line in init[start:end].splitlines() if not line.startswith('#line'))
    if 'mJoyState[i]' not in made or made.count('new(')!=2:raise AssertionError('the pad state statements of InitDeviceObjects')
    pieces.append('void ShellApplication::RealInitPadState()\n{\n\t'+made+'\n}')
    (out/'public_device_recovery_shell.h').write_text('\n\n'.join(pieces)+'\n')
    # The frame-end function as the build has it (patch 0205's request included). This folder comes before the host
    # tests' own in the include path, whose copy is the unpatched function.
    flip=helpers.function(patched(root,'Platform.cpp',out),r'^void\s+CPlatform::Flip\s*\(')
    (out/'public_d3dapp_flip.h').write_text('\n'.join(line for line in flip.splitlines() if not line.startswith('#line'))+'\n')
    for name in ('common.h','MenuItem.h','ltshell.h'):
        (out/name).write_text('// Test boundary definitions are in the driver.\n')
    session=(ROOT/'port/game/ce_network_lockstep.cpp').read_text()
    service=helpers.function(session,r'^int CE_SessionSystemService\s*\(').replace(
        'int CE_SessionSystemService(', 'int ActualSessionSystemService(',1)
    active=helpers.function(session,r'^int CE_SessionActive\s*\(')
    if network_mutant in ('session-always-active','session-always-offline'):
        active='int CE_SessionActive() { return '+('1' if network_mutant=='session-always-active' else '0')+'; }'
    (out/'public_device_recovery_service.h').write_text(service+'\n'+active+'\n')
    platform=(ROOT/'src/Platform.h').read_text()
    declarations=[]
    for name,text in (('EQuitType',platform),):
        match=re.search(r'\benum '+name+r'\s*\{[^}]+\};',text,re.S)
        if not match:raise AssertionError(f'actual enum missing: {name}')
        declarations.append(match.group())
    (out/'public_device_recovery_session_types.h').write_text('\n'.join(declarations)+'\n')
    return hashlib.sha256(whole.encode()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-ref',help='optional local frozen graphics-host commit for exact caller-output comparison')
    parser.add_argument('--mutants',action='store_true')
    parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--syntax-targets',action='store_true')
    parser.add_argument('--negative-ref',help='a commit whose shell patches, graphics host and Window mode request must fail the NEGATIVE cases')
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('host_tests',ROOT/'port/tools/public_d3dapp_tests.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    out=ROOT/'local-data/public-device-recovery-tests'
    out.mkdir(parents=True,exist_ok=True)
    helpers.recovery_source(out/'recovery',True)
    shell_sha=shell_prefix(out/'shell',helpers)
    rows=reset_rows(out/'rows')
    video=ROOT/'port/game/ce_video_options.cpp'
    # host: the graphics host's own controls (their frame-end function is the unpatched one of public_d3dapp_tests).
    # common: the whole shell; its folder comes first, so the patched frame-end function is the one compiled.
    host=['clang++','-std=c++14','-DCE_VERSION=1','-DBEA_PUBLIC_D3DAPP_TEST','-DBEA_PUBLIC_GRAPHICS_HOST=1','-I',SUPPORT,'-I',out/'recovery']
    common=host[:-2]+['-I',out/'shell','-I',out/'recovery',f'-DVIDEO_SOURCE="{video}"']
    if args.sanitizers:
        host+=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    driver=SUPPORT/'public_device_recovery_driver.cpp'
    binary=out/'driver'
    require_run(common+[PUBLIC/'d3dapp.cpp',driver,'-o',binary])
    observations={name:require_run([binary,name]) for name in CASES+NETWORK_CASES}
    negatives=None
    if args.negative_ref:
        # The same driver and cases over an older tree's text: the shell with that tree's patch queue, its graphics host
        # and its Window mode request. Each NEGATIVE case must fail there, and no other case.
        tree=out/'negative-tree'
        if tree.exists():shutil.rmtree(tree)
        tree.mkdir()
        archive=subprocess.run(['git','-C',str(ROOT),'archive',args.negative_ref,'src','port'],capture_output=True,check=True).stdout
        subprocess.run(['tar','-x','-C',str(tree)],input=archive,check=True)
        shell_prefix(out/'shell-negative',helpers,off=True,root=tree)
        old_video=tree/'port/game/ce_video_options.cpp'
        flags=[item for item in common if not str(item).startswith('-DVIDEO_SOURCE')]
        flags[flags.index(out/'shell')]=out/'shell-negative'
        old_binary=out/'driver-negative'
        require_run(flags+[f'-DVIDEO_SOURCE="{old_video}"','-DCE_NETWORK_OFF=1',tree/'port/platform/public/d3dapp.cpp',driver,'-o',old_binary])
        negatives={}
        for name in CASES:
            observed=run([old_binary,name])
            negatives[name]={'exit':observed.returncode,'stderr':observed.stderr.strip()[-200:]}
        failed={name for name,item in negatives.items() if item['exit']}
        if failed!=set(NEGATIVE):
            raise AssertionError(f'{args.negative_ref}: failing cases differ from NEGATIVE: only there {sorted(failed-set(NEGATIVE))}, '
                                 f'only listed {sorted(set(NEGATIVE)-failed)}')
        for name in CASES:
            print(f'  {args.negative_ref[:8]} {name}: exit {negatives[name]["exit"]} {negatives[name]["stderr"]}')
        print(f'{args.negative_ref}: {len(failed)} of {len(CASES)} cases fail on that text, as listed; the other {len(CASES)-len(failed)} pass there too')
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
    require_run(host+[PUBLIC/'d3dapp.cpp',base_driver,stubs,'-o',new_binary])
    if args.before_ref:
        before=out/'before.cpp'
        before.write_text(require_run(['git','show',f'{args.before_ref}:port/platform/public/d3dapp.cpp']))
        require_run(host+[before,base_driver,stubs,'-o',old_binary])
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
        substitutions.update({
            # the fixes that followed the pre-freeze reading of 0.1.14, one control each
            'refused-reset-not-owned':('        presentationFailureOwner = this;\n        presentationRecoveryReady = wasReady || pauseDepth != 0;\n    }\n    if (SUCCEEDED(result) && !m_bFrameMoving)',
                                       '    }\n    if (SUCCEEDED(result) && !m_bFrameMoving)'),
            'dropped-device-released-again':('HRESULT result = deviceObjectsLive ? ReleaseDeviceObjects(showProgress) : S_OK;',
                                             'HRESULT result = ReleaseDeviceObjects(showProgress);'),
            'nested-reset-admitted':('if (deviceChanging) return RefuseNestedChange("Reset3DEnvironment");','/* authored control: nested reset admitted */'),
            'nested-window-switch-admitted':('if (deviceChanging) return RefuseNestedChange("ForceWindowed");','/* authored control: nested switch admitted */'),
            'busy-host-reported-free':('return deviceChanging || presentationTransition || recoveryAttempt || recoveryOwner || presentationFailureOwner;',
                                       'return 0;'),
        })
        selected={'restore-blocking-retry':'loss-7','omit-pending-readiness':'loss-1',
                  'pause-publishes-early':'pause','admit-reentrant-recovery':'reentrant',
                  'hide-presentation-pending':'presentation-caller-retry',
                  'refused-reset-not-owned':'options-reset-refused','dropped-device-released-again':'presentation-caller-null-device',
                  'nested-reset-admitted':'reset-reentrant','nested-window-switch-admitted':'reset-reentrant',
                  'busy-host-reported-free':'apply-to-window'}
        for name,(old,new) in substitutions.items():
            if text.count(old)!=1:raise AssertionError(f'mutation anchor {name}')
            source=out/f'{name}.cpp';source.write_text(text.replace(old,new))
            executable=out/name
            require_run(common+[source,driver,'-o',executable])
            observed=run([executable,selected[name]])
            if observed.returncode==0:raise AssertionError(f'mutant survived: {name}')
            mutations[name]={'case':selected[name],'exit':observed.returncode,
                             'stdout':observed.stdout,'stderr':observed.stderr}
        # the shell's two pad guards of patch 0085, and the request's own question, removed one at a time
        # (the idle pads one in the network build by each of its three FALSE returns with the objects released)
        for name,cases,keywords in (('pads-polled-while-released',('loss-1',),dict(shell_mutant='pads-polled-while-released')),
                                    ('no-idle-pads-for-the-last-pass',('close','net-wmquit-released','net-exit-released'),
                                     dict(shell_mutant='no-idle-pads-for-the-last-pass'))):
            for off in (False,True):
                label=name+('-default' if off else '')
                folder=out/('shell-'+label);shell_prefix(folder,helpers,off=off,**keywords)
                executable=out/(label+'-driver')
                alt=[item for item in common];alt[alt.index(out/'shell')]=folder
                require_run(alt+(['-DCE_NETWORK_OFF=1'] if off else [])+[PUBLIC/'d3dapp.cpp',driver,'-o',executable])
                for case in cases:
                    if off and case.startswith('net-'):continue
                    observed=run([executable,case])
                    key=label if case==cases[0] else f'{label}:{case}'
                    if observed.returncode==0:raise AssertionError(f'mutant survived: {key}')
                    mutations[key]={'case':case,'exit':observed.returncode,'stdout':observed.stdout,'stderr':observed.stderr}
        anchor='if (pendingMode >= 0 && PortPublicDeviceBusy()) return;'
        text_video=video.read_text()
        if text_video.count(anchor)!=1:raise AssertionError('window request mutation anchor')
        mutant=out/'request-not-asking.cpp';mutant.write_text(text_video.replace(anchor,'/* authored control: the host is not asked */'))
        executable=out/'request-not-asking'
        alt=[item for item in common if not str(item).startswith('-DVIDEO_SOURCE')]
        require_run(alt+[f'-DVIDEO_SOURCE="{mutant}"',PUBLIC/'d3dapp.cpp',driver,'-o',executable])
        observed=run([executable,'apply-to-window'])
        if observed.returncode==0:raise AssertionError('mutant survived: request-not-asking')
        mutations['request-not-asking']={'case':'apply-to-window','exit':observed.returncode,'stdout':observed.stdout,'stderr':observed.stderr}
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
                          ('omit-device-observation','net-recovery'),
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
        source=network_view((out/'shell/ltshell.cpp').read_text(encoding='latin-1'),off=False)
        body=helpers.function(source,r'^BOOL PCLTShell::HandlingSystemStuff\s*\(')+'\n'    # the whole function, as the tests run it
        caller=syntax/'headers/public-recovery-caller.cpp'
        caller.write_text('#include "common.h"\n#include "ltshell.h"\n#include "DXUtil.h"\n#include "Platform.h"\n#include "ce_network_session.h"\n'
                          'static bool sPortQuitRequested;\nstatic int sKeysRead[32];\nstatic int *spKeysRead = sKeysRead;\n'+body)
        for name in ('linux','windows'):
            target=build.TARGETS[name]
            inc=[ROOT/'port/d3dx/include',ROOT/'port/include',ROOT/'port/game']+target['includes']+[syntax/'headers',syntax/'headers/MissionScript']
            command=target['cxx']+['-std=gnu++14']+build.MSCXX+build.COMMON+build.X87+[
                '-DCE_VERSION=1','-DBEA_PUBLIC_GRAPHICS_HOST=1','-fsyntax-only','-include',ROOT/'port/include/port.h']
            command += [item for path in inc for item in ('-I',path)]+[caller]
            require_run(command)
            print(f'{name}: actual patched whole shell function compiles against real target headers')
    result={'kind':'AUTHORED_ACTUAL_SOURCE_CONTROLS_NOT_HARDWARE_ACCEPTANCE',
            'source_sha256':hashlib.sha256((PUBLIC/'d3dapp.cpp').read_bytes()).hexdigest(),
            'patched_shell_sha256':shell_sha,'cases':observations,'existing_observations':len(controls),
            'unchanged_old_observations':len(controls) if args.before_ref else None,
            'before_ref':args.before_ref,'causal_mutants':mutations,
            'network_service_sha256':hashlib.sha256((out/'shell/public_device_recovery_service.h').read_bytes()).hexdigest(),
            'network_cases':list(NETWORK_CASES),'default_build_cases':list(CASES),
            'negative_ref':args.negative_ref,'negative_cases':negatives,'reset_rows':rows,
            'default_build_shell_sha256':hashlib.sha256((out/'shell-off/public_device_recovery_shell.h').read_bytes()).hexdigest(),
            'sanitizers':args.sanitizers,'syntax_targets':args.syntax_targets,
            'limits':['No Windows/GPU/game execution','Pending messages and device losses are authored',
                      'No attribution to the player report','No timeout is inferred from bounded prefixes',
                      'Actual session service uses authored admission/transport/window dependencies, not peer networking',
                      'Initial non-LOST/non-NOTRESET cooperative errors retain their existing canceled loop']}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    history='unchanged old' if args.before_ref else 'current'
    print(f'{len(rows)} rows ask for the options reset, all through the one request the apply cases make: {", ".join(rows)}')
    print(f'default public build (CE_NETWORK_OFF): {len(off_observations)} recovery cases, observations equal the network build\'s')
    print(f'{len(CASES)} recovery + {len(NETWORK_CASES)} network/boundary cases; {len(controls)} {history} observations; {len(mutations)} causal refusals')

if __name__=='__main__':main()
