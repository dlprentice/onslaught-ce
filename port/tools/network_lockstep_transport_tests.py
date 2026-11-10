#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual lockstep controller with stub world, linked to built transport objects, over loopback.

The test uses authored fixture values and local sockets only. It drops one reliable terminal datagram and
checks both application outcomes. It does not run the game, inspect payloads, or prove Internet behavior.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading

from network_world_tests import actual_body
import deps

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'local-data/port/lockstep-transport-tests'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Proxy:
    def __init__(self, host_port, drop_role, drop_retransmissions=False):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1',0));self.port=self.sock.getsockname()[1]
        self.host=('127.0.0.1',host_port);self.guest=None;self.drop_role=drop_role
        self.seen={'host':[],'guest':[]};self.count={'host':Counter(),'guest':Counter()}
        self.dropped=0;self.drop_id=None;self.dropped_copies=0;self.drop_retransmissions=drop_retransmissions;self.stop=threading.Event()
        self.thread=threading.Thread(target=self.loop,daemon=True);self.thread.start()

    def loop(self):
        while not self.stop.is_set():
            if not select.select([self.sock],[],[],.01)[0]:continue
            data,sender=self.sock.recvfrom(65536)
            if sender==self.host:
                if self.guest is None:continue
                role='host';dest=self.guest
            else:
                role='guest';self.guest=sender;dest=self.host
            ids=[];cursor=0
            while True:
                pos=data.find(b'CEN1',cursor)
                if pos<0 or pos+48>len(data):break
                cursor=pos+4
                version,kind,channel=struct.unpack_from('>HBB',data,pos+4)
                tick,message,total=struct.unpack_from('>III',data,pos+16)
                part,parts=struct.unpack_from('>HH',data,pos+28)
                if version!=1 or kind!=16 or channel!=0 or tick!=1 or parts!=1 or part!=0 or total>1000 or pos+48+total+16>len(data):continue
                ids.append(message);self.count[role][message]+=1
                if message not in self.seen[role]:self.seen[role].append(message)
            # This fixture emits exactly five application controls per side, checked against its source-level
            # send trace below. The fifth is host DONE or guest DONE_ACK; no encrypted payload is inspected.
            if role==self.drop_role and len(self.seen[role])>=5:
                target=self.seen[role][4]
                if target in ids and (not self.dropped or self.drop_retransmissions):
                    self.dropped=1;self.drop_id=target;self.dropped_copies+=ids.count(target);continue
            self.sock.sendto(data,dest)

    def close(self):
        self.stop.set();self.thread.join(timeout=2);self.sock.close()

    def stats(self):
        return {'dropped':self.dropped,'drop_role':self.drop_role,'drop_id':self.drop_id,
                'dropped_copies':self.dropped_copies,'drop_retransmissions':self.drop_retransmissions,
                'seen':self.seen,'observed_copies':{r:dict(self.count[r]) for r in ('host','guest')}}


def build(run, build_dir):
    build_dir=build_dir.resolve();built_root=build_dir.parents[3]
    for name in ('transport.cpp','codec.cpp','network.h','prerun_flags.cpp'):
        if (built_root/'port/network'/name).read_bytes()!=(ROOT/'port/network'/name).read_bytes():
            raise ValueError('built transport source differs from this checkout: '+name)
    inputs={'transport':build_dir/'port/network/transport.cpp.o','codec':build_dir/'port/network/codec.cpp.o',
            'prerun_flags':build_dir/'port/network/prerun_flags.cpp.o',
            'monocypher':build_dir/'port/third-party/monocypher.c.o','enet':deps.PREFIX/'lib/libenet.a'}
    copydir=run/'objects';copydir.mkdir();copies={};pins={}
    for name,path in inputs.items():
        pins[name]=sha(path);copy=copydir/(name+('.a' if name=='enet' else '.o'));shutil.copyfile(path,copy)
        if sha(copy)!=pins[name]:raise ValueError('object/library copy mismatch: '+name)
        copies[name]=copy
    world=(ROOT/'port/game/ce_network_world.cpp').read_text();a=world.index('const uint32_t native_actions[]=');b=world.index(';',a)+1
    validator=run/'world-input-validator.inc'
    validator.write_text(world[a:b]+'\n'+actual_body(world,'unsigned ActionRank(')+'\n'+actual_body(world,'bool ValidInput(')+'\nextern "C" '+actual_body(world,'int CE_WorldLockstepValidInput(')+'\n')
    exe=run/'lockstep-transport-peer';compiler=deps.DATA/'llvm/bin/clang++'
    command=[str(compiler),'--target=i686-pc-linux-gnu',*deps.sysroot_flags(),'-std=gnu++17',
             '-fms-extensions','-fno-autolink','-fno-strict-aliasing','-fwrapv','-mms-bitfields',
             '-fshort-wchar','-O2','-DLOCKSTEP_VALIDATOR_INC="'+str(validator)+'"',
             '-I'+str(ROOT/'port/tools/tests/lockstep_stubs'),str(ROOT/'port/tools/tests/network_lockstep_transport_peer.cpp'),
             str(ROOT/'port/game/ce_network_campaign.cpp'),str(ROOT/'port/game/ce_network_result_exchange.cpp'),
             str(copies['transport']),str(copies['codec']),str(copies['prerun_flags']),str(copies['monocypher']),str(copies['enet']),
             '-m32',*deps.sysroot_link_flags(),'-Wl,--wrap=CE_NetSend','-Wl,--wrap=CE_NetGetStatus',
             '-Wl,--wrap=CE_NetReceive','-Wl,--wrap=CE_NetPoll','-lpthread','-no-pie','-o',str(exe)]
    built=subprocess.run(command,capture_output=True,text=True,timeout=90)
    (run/'build.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError('fixture build failed; see '+str(run/'build.log'))
    for name,path in inputs.items():
        if sha(path)!=pins[name]:raise ValueError('built object/library changed during fixture build: '+name)
    return exe,{'source_root':str(built_root),'objects':{n:{'path':str(p),'sha256':pins[n]} for n,p in inputs.items()},
                'command':command,'executable_sha256':sha(exe)}


def pair(exe,drop_role,mode='normal',drop_retransmissions=False):
    host=subprocess.Popen([str(exe),'host','0',mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    first=host.stdout.readline()
    if not first.startswith('BOUND '):raise RuntimeError('host fixture did not bind: '+first)
    proxy=Proxy(int(first.split()[1]),drop_role,drop_retransmissions)
    try:
        guest=subprocess.Popen([str(exe),'guest',str(proxy.port),mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        gout,gerr=guest.communicate(timeout=16);hout,herr=host.communicate(timeout=16)
        return {'host':{'pid':host.pid,'rc':host.returncode,'out':first+hout,'err':herr},
                'guest':{'pid':guest.pid,'rc':guest.returncode,'out':gout,'err':gerr},'proxy':proxy.stats()}
    finally:proxy.close()


def trace(row):
    return [int(line.split('type=')[1].split()[0]) for line in row['out'].splitlines() if line.startswith('CONTROL type=')]


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',type=Path,required=True);args=parser.parse_args()
    WORK.mkdir(parents=True,exist_ok=True);run=Path(tempfile.mkdtemp(prefix='run-',dir=WORK))
    exe,provenance=build(run,args.build_dir)
    cases={'no_loss':pair(exe,None),'lost_done':pair(exe,'host'),'lost_done_ack':pair(exe,'guest',drop_retransmissions=True),
           'ack_and_leave_same_poll':pair(exe,None,'same-poll'),
           'done_and_early_leave_same_poll':pair(exe,None,'early-close')}
    (run/'result.json').write_text(json.dumps({'provenance':provenance,'cases':cases},indent=2)+'\n')
    host_trace=[3,9,6,7,10];guest_trace=[3,9,6,8,11]
    def complete(r):
        markers=[re.findall(r'^CONFIRMED tick=([0-9]+) hash=([0-9a-f]{16})$',r[role]['out'],re.M) for role in ('host','guest')]
        return (type(r['host']['rc']) is int and r['host']['rc']==0 and type(r['guest']['rc']) is int and r['guest']['rc']==0 and
                type(r['host']['pid']) is int and type(r['guest']['pid']) is int and r['host']['pid']!=r['guest']['pid'] and
                len(markers[0])==1 and len(markers[1])==1 and markers[0][0]==markers[1][0] and markers[0][0][0]=='1')
    normal=cases['no_loss'];done=cases['lost_done'];ack=cases['lost_done_ack']
    # Withhold every final ACK copy; a single dropped reliable datagram can recover.
    ack_copies=ack['proxy']['observed_copies']['guest'].get(ack['proxy']['drop_id'],0)
    assert ack_copies>0 and ack['proxy']['dropped_copies']==ack_copies, 'final ACK fault was not maintained'
    normal_ok=complete(normal) and trace(normal['host'])==host_trace and trace(normal['guest'])==guest_trace
    target=done['proxy']['drop_id'];copies=done['proxy']['observed_copies']['host']
    done_ok=(complete(done) and done['proxy']['dropped']==1 and target is not None and copies.get(target,0)>=2 and
             trace(done['host'])==host_trace and trace(done['guest'])==guest_trace)
    ack_ok=(ack['proxy']['dropped']==1 and ack['guest']['rc']==0 and 'CONFIRMED ' in ack['guest']['out'] and
            ack['host']['rc']!=0 and 'INCOMPLETE' in ack['host']['out'] and 'CONFIRMED ' not in ack['host']['out'] and
            trace(ack['host'])==host_trace and trace(ack['guest'])==guest_trace)
    same=cases['ack_and_leave_same_poll']
    same_ok=(complete(same) and 'RECEIVE type=11 phase=6' in same['host']['out'] and
             trace(same['host'])==host_trace and trace(same['guest'])==guest_trace)
    early=cases['done_and_early_leave_same_poll']
    early_ok=(early['host']['rc']!=0 and early['guest']['rc']!=0 and
              'RECEIVE type=10 phase=6' in early['guest']['out'] and
              'CONTROL type=11 tick=1 result=0' in early['guest']['out'] and
              'CONFIRMED ' not in early['host']['out'] and 'CONFIRMED ' not in early['guest']['out'])
    changed=copy.deepcopy(normal);changed['guest']['out']=re.sub(r'(CONFIRMED tick=1 hash=)[0-9a-f]{16}',r'\g<1>0000000000000000',changed['guest']['out'])
    mismatched_ok=not complete(changed)
    changed=copy.deepcopy(normal);changed['guest']['pid']=changed['host']['pid'];same_pid_ok=not complete(changed)
    changed=copy.deepcopy(normal);changed['guest']['rc']=False;bool_status_ok=not complete(changed)
    for label,ok in [('both confirm without loss',normal_ok),('lost DONE retransmits while host waits',done_ok),
                     ('lost final ACK leaves host incomplete',ack_ok),('ACK and leave survive the same real poll',same_ok),
                     ('DONE and early leave cannot produce a false guest confirmation',early_ok),
                     ('different final hashes are refused',mismatched_ok),('same PID is refused',same_pid_ok),
                     ('boolean exit status is refused',bool_status_ok)]:print(('PASS ' if ok else 'FAIL ')+label)
    print('evidence:',run/'result.json')
    return 0 if normal_ok and done_ok and ack_ok and same_ok and early_ok and mismatched_ok and same_pid_ok and bool_status_ok else 1


if __name__=='__main__':raise SystemExit(main())
