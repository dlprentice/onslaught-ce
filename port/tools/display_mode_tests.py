#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual production mode functions on fake SDL/D3D boundaries; no GPU, game, VM or native ABI claim.

--before-ref optionally reproduces failures from a Git source revision. Both compiled inputs and all results stay in
ignored local-data. The Linux faithful 4:3 checks deliberately retain compatibility sizes; Windows keeps native lists.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('port/platform/common/d3d9hooks.cpp')
CASES=('adapters','formats','objects','single-native','native-larger-than-primary','fold-refresh',
       'faithful-missing-4by3','faithful-existing-4by3','nonwide-format','bounded-union','desktop-preserved','address-reuse','second-format-desktop','faithful-consumer','faithful-other-format','unsigned-index','desktop-unavailable','consumer-array-guard')

def sha(data): return hashlib.sha256(data).hexdigest()

def block(raw):
    text=raw.decode()
    start=text.index('// IDirect3D9::GetAdapterModeCount and EnumAdapterModes.')
    end=text.index('\nunsigned gFrames;',start)
    return text[start:end]+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',type=Path,default=ROOT)
    p.add_argument('--before-ref')
    p.add_argument('--regression-ref',help='optional first display fix to check the later adverse cases')
    p.add_argument('--consumer-source',type=Path,help='selected production d3dapp.cpp (private filter or public helper)')
    p.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    a=p.parse_args();resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    parent=ROOT/'local-data/port/display-mode-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));driver=(ROOT/'port/tools/tests/display_modes_driver.cpp').read_bytes()
    versions={'current':(a.source_root/SOURCE).read_bytes()}
    if a.before_ref: versions['before']=subprocess.check_output(['git','-C',str(a.source_root),'show',a.before_ref+':'+str(SOURCE)])
    if a.regression_ref: versions['regression']=subprocess.check_output(['git','-C',str(a.source_root),'show',a.regression_ref+':'+str(SOURCE)])
    consumer_path=a.consumer_source or a.source_root/'src/d3dapp.cpp'
    if not consumer_path.is_file() and not a.consumer_source:
        consumer_path=a.source_root/'port/platform/public/d3dapp.cpp'
    caller=consumer_path.read_text(encoding='latin-1')
    public=re.findall(r'^bool PublicDisplayModeAllowed\([^\n]+\)\n\{\n.*?^\}',caller,re.M|re.S)
    if public:
        if len(public)!=1: raise RuntimeError('ambiguous public display admission helper')
        consumer_helper=public[0]
        colour_helpers=re.findall(r'^bool Colour32\([^\n]+\)\n\{\n.*?^\}',caller,re.M|re.S)[0]
        consumer='if (!PublicDisplayModeAllowed(DisplayMode, PortWideModes(), ALLOW_WIDESCREEN_MODES.mValue)) continue;'
        begin=caller.index('D3DDISPLAYMODE available[')
        end=caller.index('for (unsigned i = 1; i < count;',begin)
        collection=caller[begin:end]
        capacity=int(re.search(r'available\[(\d+)\]',collection)[1])
        colours=re.search(r'const D3DFORMAT colours\[\] = \{[^;]+;',caller)[0]
        collection=re.sub(r'D3DDISPLAYMODE available\[\d+\];',f'ModeFrame<{capacity}> frame;auto &available=frame.modes;',collection,count=1)
        collection='IDirect3D9 *m_pD3D=&d;UINT physical=0;'+colours+'\n'+collection+f'\nreturn {{count,frame.intact(),{capacity},nullptr}};'
    else:
        consumer_helper=''
        colour_helpers=re.findall(r'^static inline bool Is32BitFormat\([^\n]+\)\n\{\n.*?^\}',caller,re.M|re.S)[0]
        begin=caller.index('// Filter out low-resolution modes')
        end=caller.index('// Check if the mode already exists',begin)
        consumer=caller[begin:end]
        begin=caller.index('D3DDISPLAYMODE modes[')
        end=caller.index('// Sort the list of display modes',begin)
        collection=caller[begin:end]
        capacity=int(re.search(r'modes\[(\d+)\]',collection)[1])
        collection=re.sub(r'D3DDISPLAYMODE modes\[\d+\];',f'ModeFrame<{capacity}> frame;auto &modes=frame.modes;',collection,count=1)
        collection='IDirect3D9 *m_pD3D=&d;UINT iAdapter=0;struct {D3DDISPLAYMODE d3ddmDesktop;} adapter{};auto *pAdapter=&adapter;d.GetAdapterDisplayMode(0,&adapter.d3ddmDesktop);\n'+collection+f'\nreturn {{dwNumModes,frame.intact(),{capacity},nullptr}};'
    compiler=subprocess.check_output([*shlex.split(a.cxx),'--version'],text=True)
    receipt={'compiler_version':compiler,'source_root':str(a.source_root.resolve()),'before_ref':a.before_ref,'regression_ref':a.regression_ref,'consumer_source':str(consumer_path.resolve()),'consumer_filter_sha256':sha((colour_helpers+consumer_helper+consumer+collection).encode()),'driver_sha256':sha(driver),'builds':[],
             'cases':[],'limits':['Actual extracted production mode functions; fake SDL/D3D boundaries.',
             'Actual factory lifetime and faithful consumer filter, with fake COM/OS calls. No game, GPU, Windows ABI, monitor/hotplug or native device creation.']}
    ok=True
    try:
        for version,raw in versions.items():
            (out/(version+'-d3d9hooks.cpp')).write_bytes(raw)
            for platform in ('windows','linux'):
                name=version+'-'+platform;source=out/(name+'.cpp');binary=out/name
                factory=re.findall(r'^extern "C" IDirect3D9 \*__stdcall PortDirect3DCreate9\(UINT sdk\)\n\{\n.*?^\}',raw.decode(),re.M|re.S)
                if len(factory)!=1: raise RuntimeError('expected one actual Direct3D factory body')
                source.write_text(driver.decode().replace('@PRODUCTION@',block(raw)).replace('@FACTORY@',factory[0]).replace('@CONSUMER_FILTER@',consumer).replace('@CONSUMER_HELPER@',consumer_helper).replace('@COLOUR_HELPERS@',colour_helpers).replace('@CONSUMER_COLLECTION@',collection))
                cmd=[*shlex.split(a.cxx),'-std=c++17','-O0','-g','-Wall','-Wextra','-Werror',
                     *(['-D_WIN32'] if platform=='windows' else []),str(source),'-o',str(binary)]
                proc=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
                receipt['builds'].append({'name':name,'source_sha256':sha(raw),'command':cmd,'exit':proc.returncode,
                    'stdout':proc.stdout,'stderr':proc.stderr})
                if proc.returncode: raise RuntimeError('compile failed: '+name+'\n'+proc.stderr)
                for case in CASES:
                    proc=subprocess.run([str(binary),case],capture_output=True,text=True,timeout=5)
                    actual=json.loads(proc.stdout)
                    passed=proc.returncode==0 and actual['failures']==0
                    if platform=='windows' and case=='faithful-consumer':
                        passed=passed and ('no native 4:3 fullscreen display modes' if version=='current' else 'no usable 4:3 display modes') in proc.stderr
                    if platform=='windows' and case=='faithful-other-format':
                        passed=passed and 'no native 4:3 fullscreen display modes' not in proc.stderr and 'no usable 4:3 display modes' not in proc.stderr
                    receipt['cases'].append({'version':version,'platform':platform,'case':case,'exit':proc.returncode,
                        'actual':actual,'pass':passed,'stderr':proc.stderr})
                    if version=='current':ok=ok and passed
        if 'before' in versions:
            negatives=[r for r in receipt['cases'] if r['version']=='before' and not r['pass']]
            receipt['old_negative_cases']=[r['platform']+'/'+r['case'] for r in negatives]
            # The required original failures must really fail in the same actual-function harness.
            required={'windows/adapters','windows/formats','windows/single-native'}
            ok=ok and required.issubset(set(receipt['old_negative_cases']))
            receipt['unchanged_controls']=[]
            controls={'linux':('faithful-missing-4by3','faithful-existing-4by3','nonwide-format','single-native','native-larger-than-primary','fold-refresh'),
                      'windows':('faithful-existing-4by3',)}
            for platform,cases in controls.items():
                for case in cases:
                    rows=[r for r in receipt['cases'] if r['platform']==platform and r['case']==case]
                    before=next(r for r in rows if r['version']=='before')
                    current=next(r for r in rows if r['version']=='current')
                    equal=before['actual']['queries']==current['actual']['queries']
                    receipt['unchanged_controls'].append({'platform':platform,'case':case,'all_mode_fields_equal':equal})
                    ok=ok and equal
        if 'regression' in versions:
            negatives={r['platform']+'/'+r['case'] for r in receipt['cases'] if r['version']=='regression' and not r['pass']}
            receipt['regression_negative_cases']=sorted(negatives)
            ok=ok and {'windows/address-reuse','linux/address-reuse','windows/second-format-desktop','windows/faithful-consumer'}.issubset(negatives)
        receipt['pass']=ok
    except Exception as e:
        receipt['error']=str(e);receipt['pass']=False;ok=False
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'pass':ok,'result':str(out/'result.json'),'old_negative_cases':receipt.get('old_negative_cases',[]),'regression_negative_cases':receipt.get('regression_negative_cases',[]),
                      'error':receipt.get('error')}))
    return 0 if ok else 1

if __name__=='__main__': raise SystemExit(main())
