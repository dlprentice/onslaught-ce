#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure native x86 atomic representation and protect networking STL headers.

Uses the selected compiler in the game's Microsoft compatibility mode. All
outputs and fixed diagnostic logs remain private, with no game or service run.
"""
from pathlib import Path
import argparse,hashlib,json,os,resource,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
MICRO='#include <cstdio>\n#define min(a,b) 71\n#define max(a,b) 72\n#define new(...) legacy_new(__VA_ARGS__)\n#ifdef __GCC_ATOMIC_TEST_AND_SET_TRUEVAL\n#define CE_HAD_ATOMIC_VALUE 1\n#else\n#define CE_HAD_ATOMIC_VALUE 0\n#endif\n#include "ce_network_stl.h"\n#if !defined(min) || !defined(max) || !defined(new)\n#error legacy macros were not restored\n#endif\n#if !CE_HAD_ATOMIC_VALUE && defined(__GCC_ATOMIC_TEST_AND_SET_TRUEVAL)\n#error temporary atomic constant escaped the header boundary\n#endif\nint main(){\n unsigned char value=0;const bool before=__atomic_test_and_set(&value,__ATOMIC_SEQ_CST);\n std::atomic_flag flag=ATOMIC_FLAG_INIT;const bool first=flag.test_and_set(),second=flag.test_and_set();\n std::atomic_flag initiallyTrue(true);const unsigned char standardTrue=*reinterpret_cast<const unsigned char*>(&initiallyTrue);\n const bool ok=standardTrue==value&&!before&&value==1&&!first&&second&&min(1,2)==71&&max(1,2)==72&&(std::max)(1,2)==2&&(std::numeric_limits<unsigned>::max)()>0;\n std::printf("atomic_true_byte=%u macro_restore=%u flag_transition=%u\\n",unsigned(value),unsigned(min(1,2)==71&&max(1,2)==72),unsigned(!first&&second));return ok?0:1;\n}\n'

def bounds():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_CPU,(30,30))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++')
    args=parser.parse_args()
    compiler=Path(shutil.which(args.compiler) or args.compiler).resolve()
    os.umask(0o077)
    base=ROOT/'local-data/port/network-chat-stl-tests';base.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='run-',dir=base));(work/'home').mkdir()
    env={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'HOME':str(work/'home'),'TMPDIR':str(work),'LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
    header=ROOT/'port/game/ce_network_stl.h';original=header.read_text()
    variants={
        'positive':(original,MICRO,'run'),
        'macro-shield-removed':(original.replace('#undef min\n#undef max\n#undef new\n',''),MICRO,'compile-reject'),
        'atomic-compat-removed':(original.replace('#define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1',''),MICRO,'compile-reject'),
        'atomic-value-wrong':(original.replace('#define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1','#define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 2'),MICRO,'runtime-reject'),
        'max-call-unprotected':(original,MICRO.replace('(std::numeric_limits<unsigned>::max)()','std::numeric_limits<unsigned>::max()'),'compile-reject'),
    }
    cases=[]
    for label,(text,source,expect) in variants.items():
        target=work/label;target.mkdir();(target/'ce_network_stl.h').write_text(text)
        cpp=target/'probe.cpp';cpp.write_text(source);exe=target/'probe';log=target/'result.private.log'
        command=[str(compiler),'--target=i686-pc-linux-gnu','-m32','-std=gnu++14','-fms-extensions','-fms-compatibility','-fms-compatibility-version=19.20','-nostdlib++','-I'+str(target),str(cpp),'-o',str(exe)]
        with log.open('w') as output:
            built=subprocess.run(command,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=45,preexec_fn=bounds)
            row={'label':label,'compiled':built.returncode==0,'compile_exit':built.returncode,'passed':False,'log':str(log)}
            if built.returncode==0:
                tested=subprocess.run([str(exe)],env=env,stdout=output,stderr=subprocess.STDOUT,timeout=5,preexec_fn=bounds)
                row['run_exit']=tested.returncode
                row['passed']=tested.returncode==(1 if expect=='runtime-reject' else 0) and expect!='compile-reject'
            elif expect=='compile-reject':row['passed']=True
        cases.append(row)
        if label=='positive' and not row['passed']:break
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    result={'schema':1,'compiler':str(compiler),'compiler_sha256':digest(compiler),'header_sha256':digest(header),'runner_sha256':digest(Path(__file__)),'target':'i686-pc-linux-gnu','mode':'gnu++14 Microsoft compatibility19.20','cases':cases,'passed':len(cases)==len(variants) and all(c['passed'] for c in cases),'limits':['native x86 header/runtime microcheck only; production game objects/link are separate gates']}
    path=work/'result.private.json';path.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'passed':result['passed'],'cases':len(cases),'receipt':str(path)}))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
