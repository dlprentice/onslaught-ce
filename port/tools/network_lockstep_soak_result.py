#!/usr/bin/env python3
"""Strictly check one test-automation lockstep run; never infer a pass from missing errors."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile

MARKER=re.compile(r'onslaught-ce: co-op test confirmed final tick (\d+), terminal hash ([0-9a-f]{16}), (\d+) scheduled hashes compared')
BAD=('co-op session stopped:', 'out of step', 'stopped before final confirmation', 'the other player left the co-op game')

def evaluate(result,logs,tick,every):
    errors=[]; marks={};runs={}
    if not isinstance(result,dict) or type(tick) is not int or tick<=0 or type(every) is not int or every<=0:
        return ['invalid result, tick or hash interval']
    for role in ('host','guest'):
        row=result.get(role)
        if not isinstance(row,dict) or type(row.get('rc')) is not int or row['rc']!=0: errors.append(f'{role}: exit is not integer zero')
        if isinstance(row,dict):
            run=row.get('run')
            if not isinstance(run,str) or not run:errors.append(f'{role}: missing run identity')
            else:runs[role]=run
        log=logs.get(role)
        if not isinstance(log,str): errors.append(f'{role}: no log');continue
        if any(x in log for x in BAD): errors.append(f'{role}: error/departure marker')
        own='co-op started (you are player %d)' % (1 if role=='host' else 2)
        opposite='co-op started (you are player %d)' % (2 if role=='host' else 1)
        if log.count(own)!=1 or opposite in log: errors.append(f'{role}: role start missing, duplicate or mixed')
        hits=MARKER.findall(log)
        if len(hits)!=1: errors.append(f'{role}: expected one terminal confirmation, found {len(hits)}');continue
        end,digest,count=hits[0];marks[role]=digest
        if int(end)!=tick: errors.append(f'{role}: wrong terminal tick')
        if int(count)!=1+tick//every: errors.append(f'{role}: incomplete scheduled hash comparisons')
    if len(runs)==2 and runs['host']==runs['guest']: errors.append('runner identities are not distinct')
    if len(marks)==2 and marks['host']!=marks['guest']: errors.append('terminal hashes differ')
    return errors

def files(result_path,host_path,guest_path,tick,every):
    try:
        result=json.loads(result_path.read_text());paths={'host':host_path,'guest':guest_path}
        stats={role:path.stat() for role,path in paths.items()}
        logs={role:path.read_text(errors='replace') for role,path in paths.items()}
    except (OSError,ValueError) as e:return [f'missing or invalid evidence: {e}']
    errors=evaluate(result,logs,tick,every)
    pids={}
    for role in ('host','guest'):
        try:
            value=(result_path.parent/(role+'.pid')).read_text().strip()
            if not re.fullmatch(r'[1-9][0-9]*',value): raise ValueError('invalid PID')
            pids[role]=int(value)
        except (OSError,ValueError):errors.append(f'{role}: missing or invalid wrapper PID')
    if len(pids)==2 and pids['host']==pids['guest']:errors.append('wrapper PIDs are not distinct')
    if (stats['host'].st_dev,stats['host'].st_ino)==(stats['guest'].st_dev,stats['guest'].st_ino):errors.append('role logs are the same file or hardlink aliases')
    for role,path in paths.items():
        row=result.get(role) if isinstance(result,dict) else None
        if not isinstance(row,dict) or path.parent.name!=row.get('run'):errors.append(f'{role}: log directory does not match runner identity')
    return errors

def selftest():
    root=Path(__file__).resolve().parents[2]/'local-data/port/lockstep-soak-result-tests';root.mkdir(parents=True,exist_ok=True)
    case=Path(tempfile.mkdtemp(prefix='run-',dir=root))
    result={'host':{'rc':0,'run':'fixture-host'},'guest':{'rc':0,'run':'fixture-guest'}}
    def log(role,digest='0'*16,count=261):
        player=1 if role=='host' else 2
        return f'onslaught-ce: co-op started (you are player {player}); start state 1234\nonslaught-ce: co-op test confirmed final tick 5200, terminal hash {digest}, {count} scheduled hashes compared\n'
    host_dir=case/'fixture-host';guest_dir=case/'fixture-guest';host_dir.mkdir();guest_dir.mkdir()
    hp=host_dir/'stderr.log';gp=guest_dir/'wine.log';rp=case/'result.json'
    host_pid=case/'host.pid';guest_pid=case/'guest.pid';host_pid.write_text('123');guest_pid.write_text('124')
    def write(r,h,g):
        rp.write_text(json.dumps(r));hp.write_text(h);gp.write_text(g)
    def expect(label,want):
        errors=files(rp,hp,gp,5200,20);got=not errors;passed=got==want
        print(('PASS' if passed else 'FAIL'),label,('('+', '.join(errors)+')' if not passed else ''));return passed
    h=log('host');g=log('guest');ok=True
    write(result,h,g);ok=expect('valid',True) and ok
    write(result,h,'onslaught-ce: the other player left the co-op game\n');ok=expect('early-exit',False) and ok
    write(result,h,g.split('terminal hash')[0]);ok=expect('truncated',False) and ok
    write(result,h,log('guest','1'*16));ok=expect('different-hash',False) and ok
    write(result,h,log('guest',count=260));ok=expect('missing-compare',False) and ok
    write({'host':{'rc':37,'run':'fixture-host'},'guest':result['guest']},h,g);ok=expect('nonzero',False) and ok
    write({'host':{'rc':False,'run':'fixture-host'},'guest':result['guest']},h,g);ok=expect('boolean-is-not-exit-code',False) and ok
    write({'host':{'rc':0.0,'run':'fixture-host'},'guest':result['guest']},h,g);ok=expect('float-is-not-exit-code',False) and ok
    write({},h,g);ok=expect('missing-result',False) and ok
    write({'host':result['host'],'guest':{'rc':0,'run':'fixture-host'}},h,g);ok=expect('same-run-identity',False) and ok
    write(result,h,g);guest_pid.write_text('123');ok=expect('same-wrapper-pid',False) and ok;guest_pid.write_text('124')
    missing=case/'missing-pid';missing.mkdir();mh=missing/'fixture-host';mg=missing/'fixture-guest';mh.mkdir();mg.mkdir()
    (mh/'stderr.log').write_text(h);(mg/'wine.log').write_text(g);(missing/'result.json').write_text(json.dumps(result))
    (missing/'host.pid').write_text('123')
    missing_errors=files(missing/'result.json',mh/'stderr.log',mg/'wine.log',5200,20)
    missing_ok=any('guest: missing or invalid wrapper PID' in e for e in missing_errors)
    print(('PASS' if missing_ok else 'FAIL'),'missing-wrapper-pid');ok=missing_ok and ok
    combined=h+'onslaught-ce: co-op started (you are player 2); start state 1234\n'
    write(result,combined,combined);ok=expect('combined-log-two-roles-one-terminal',False) and ok
    # Fresh paths, never unlink any existing data; create an actual hardlink alias on the local filesystem.
    alias_case=case/'hardlink';alias_case.mkdir();ha=alias_case/'fixture-host';ga=alias_case/'fixture-guest';ha.mkdir();ga.mkdir()
    hard_host=ha/'stderr.log';hard_guest=ga/'wine.log';hard_host.write_text(combined);os.link(hard_host,hard_guest)
    rp.write_text(json.dumps(result));errors=files(rp,hard_host,hard_guest,5200,20)
    hard_ok=any('hardlink aliases' in e for e in errors);print(('PASS' if hard_ok else 'FAIL'),'hardlink-alias');ok=hard_ok and ok
    print('evidence:',case)
    return 0 if ok else 1

def main():
    p=argparse.ArgumentParser();p.add_argument('--self-test',action='store_true');p.add_argument('--result',type=Path);p.add_argument('--host-log',type=Path);p.add_argument('--guest-log',type=Path);p.add_argument('--tick',type=int,default=5200);p.add_argument('--every',type=int,default=20);a=p.parse_args()
    if a.self_test:return selftest()
    if not (a.result and a.host_log and a.guest_log):p.error('supply result and both logs')
    errors=files(a.result,a.host_log,a.guest_log,a.tick,a.every)
    if errors:
        for e in errors:print('FAIL',e)
        return 1
    print(f'PASS distinct role logs and runs: both peers confirmed tick {a.tick}, same terminal hash, {1+a.tick//a.every} scheduled comparisons and clean exits')
    return 0
if __name__=='__main__':raise SystemExit(main())
