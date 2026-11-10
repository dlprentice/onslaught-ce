#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test actual merged menu and restart-scrub functions with an authored Linux process environment.

No game, saved career, transport, process restart or network peer is run by these controls.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from network_lobby_restart_handoff_tests import function

ROOT=Path(__file__).resolve().parents[2]
files={'offer':ROOT/'port/game/ce_netpages.cpp','scrub':ROOT/'port/platform/common/ce_netlobby.cpp',
       'driver':ROOT/'port/tools/tests/network_hostjoin_lockstep_driver.cpp',
       'extractor':ROOT/'port/tools/network_lobby_restart_handoff_tests.py',
       'owner_header':ROOT/'port/platform/common/ce_netcode.h'}


def definition(text, name):
    start,end=function(text,name)  # skips declarations instead of taking the next unrelated body
    return text[start:end]


sources={'offer':definition(files['offer'].read_text(),'PortCeNetOffered'),
         'setenv':definition(files['scrub'].read_text(),'SetEnv'),
         'scrub':definition(files['scrub'].read_text(),'ScrubEnvironment'),
         'clear_pending':definition(files['scrub'].read_text(),'ClearPendingHandoff'),
         'userdir':definition(files['scrub'].read_text(),'UserDir'),
         'available':definition(files['scrub'].read_text(),'CE_LobbyAvailable'),
         'lobbyshell':definition(files['scrub'].read_text(),'CE_LobbyShellExit'),
         'portshell':definition(files['scrub'].read_text(),'PortLobbyShellExit'),
         'portquit':definition(files['scrub'].read_text(),'PortLobbyLevelQuit'),
         'afterexit':definition(files['scrub'].read_text(),'CE_LobbyAfterExit')}
os.umask(0o077)
parent=ROOT/'local-data/port/network-hostjoin-lockstep-tests';parent.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));compiler=shutil.which('clang++')
if not compiler:raise RuntimeError('clang++ is required; no downloads')
variants=[('actual',None,None,None,None)]
for key in ('BEA_LOCKSTEP_HOST','BEA_LOCKSTEP_JOIN'):
    variants.append(('offer-'+key,'offer','"'+key+'"','"REMOVED_'+key+'"','configured '+key+' hides the network entry'))
    variants.append(('admission-'+key,'available','"'+key+'"','"REMOVED_'+key+'"','configured '+key+' refuses direct lobby admission'))
for key in ('BEA_LOCKSTEP_HOST','BEA_LOCKSTEP_JOIN','BEA_LOCKSTEP_SECRET','BEA_LOCKSTEP_DELAY','BEA_LOCKSTEP_HASH_EVERY',
            'BEA_LOCKSTEP_SKEW_AT','BEA_LOCKSTEP_DUMP','BEA_LOCKSTEP_QUIT_AT','BEA_LOCKSTEP_STALL_EVERY','BEA_LOCKSTEP_STALL_AT',
            'BEA_LOCKSTEP_STALL_MS','BEA_LOCKSTEP_NO_LOSS','BEA_LOCKSTEP_NO_PARTICLES','BEA_LOCKSTEP_EXTRA_RENDERS',
            'BEA_LOCKSTEP_STALL_FROM','BEA_LOCKSTEP_FLIP_CW_AT','BEA_LOCKSTEP_NO_CW_FENCE','BEA_LOCKSTEP_POISON_STACK','BEA_LOCKSTEP_TEST_UNINIT_READ'):
    variants.append(('scrub-'+key,'scrub','"'+key+'"','"REMOVED_'+key+'"','restart removes '+key))
variants += [
    ('change-native-return','portshell','return code;','return 0;','shell wrapper preserves return code and records the native quit'),
    ('change-level-return','portquit','return quit;','return 0;','level wrapper preserves ordinary quit'),
    ('restart-singleplayer','afterexit','if (!gLaunched)\n\t\treturn;','if (false) return;','ordinary single-player exit does not restart or hand off'),
    ('omit-failed-launch-clear','afterexit','    ClearPendingHandoff();','    (void)0;',
     'failed launch dispatch clears the pending owned record'),
    ('omit-owned-clear','clear_pending','CE_HandoffOwnedClear(gHandoffPath, &gHandoffOwner, NULL);','(void)0;',
     'failed launch dispatch clears the pending owned record'),
    ('omit-receipt-wipe','clear_pending','CE_CodeWipe(gHandoffReceipt, sizeof(gHandoffReceipt));','(void)0;',
     'failed launch clears pending owner and receipt buffers'),
    ('omit-file-publication','scrub','SetEnv("BEA_NET_HANDOFF_FILE", gHandoffName[0] ? gHandoffName : NULL);',
     'SetEnv("BEA_NET_HANDOFF_FILE", NULL);','restart publishes the pending handoff name'),
    ('omit-owner-publication','scrub','SetEnv("BEA_NET_HANDOFF_OWNER", gHandoffReceipt[0] ? gHandoffReceipt : NULL);',
     'SetEnv("BEA_NET_HANDOFF_OWNER", NULL);','restart publishes the pending handoff owner'),
]
results=[]
for label,which,needle,replacement,expected in variants:
    directory=out/label;directory.mkdir();copy=dict(sources)
    if needle:
        if copy[which].count(needle)!=1:raise RuntimeError('mutation is not unique: '+label)
        copy[which]=copy[which].replace(needle,replacement,1)
    defs=[]
    for part in sources:
        path=directory/(part+'.inc');path.write_text(copy[part]+'\n');defs.append('-DACTUAL_'+part.upper()+'="'+str(path)+'"')
    exe=directory/'test'
    built=subprocess.run([compiler,'-std=c++17','-O1',*defs,str(files['driver']),'-o',str(exe)],capture_output=True,text=True,timeout=30)
    (directory/'build.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(label+' did not compile; see '+str(directory/'build.log'))
    trials=[]
    for mode in ('ordinary','failed-launch'):
        trial=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=10,
                             env={'PATH':os.environ.get('PATH','/usr/bin'),'LANG':'C'})
        (directory/(mode+'.log')).write_text(trial.stdout+trial.stderr)
        trials.append(trial)
    output='\n'.join(trial.stdout for trial in trials)
    failed=any(trial.returncode!=0 for trial in trials)
    ok=not failed if expected is None else failed and ('FAIL '+expected) in output
    results.append({'name':label,'passed':ok,'returncodes':[trial.returncode for trial in trials],
                    'expected_failure':expected})
    print(('PASS ' if ok else 'FAIL ')+label,flush=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt={'scope':'actual extracted menu, restart scrub and pending-clear callers on Linux with authored environment and observed fake owned-clear backend; no files, game, live peers, real user state or process restart',
         'inputs':{str(path.relative_to(ROOT)):sha(path) for path in files.values()},'results':results}
(out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('evidence:',out)
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
