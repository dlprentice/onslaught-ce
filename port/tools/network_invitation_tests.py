#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise read-only invitation import with invented private files.

The chooser and lobby wrapper are authored fakes. The protected native reader,
pure origin validator, typed authority constructor and signaling provider run
as production code. Its transport counts calls without contacting a service.
"""
from __future__ import annotations
import argparse,hashlib,json,os,re,resource,shlex,shutil,subprocess,tempfile
from pathlib import Path
import deps
ROOT=Path(__file__).resolve().parents[2]
SOURCES=['port/game/ce_network_invitation.cpp','port/game/ce_network_invitation.h',
 'port/game/ce_network_stl.h','port/game/ce_network_boot_authority.cpp','port/game/ce_network_boot_authority.h',
 'port/network/signaling_provider.cpp','port/network/signaling_https.cpp','port/updater/sha256.h','port/updater/sha256.c',
    "port/platform/common/private_ui_input.cpp", "port/platform/common/private_ui_input.h",
    "port/tools/tests/private_ui_input_driver.cpp",
    "port/platform/common/ce_netcode.h",
 'port/tools/tests/network_invitation_driver.cpp','port/tools/network_invitation_tests.py','port/platform/common/ce_netlobby.h']
SDL='''#pragma once
#define SDLCALL
struct SDL_Window;
using SDL_DialogFileCallback=void (*)(void*,const char* const*,int);
SDL_Window* SDL_GetKeyboardFocus();
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback,void*,SDL_Window*,const char*,bool);
'''
PLATFORM='#pragma once\nconst char* PortUserDir();\n'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def bounds():
 resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_CPU,(60,60))
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--no-controls',action='store_true')
 parser.add_argument('--word-size',choices=('32','64','both'),default='64')
 args=parser.parse_args();os.umask(0o077)
 base=ROOT/'local-data/port/network-invitation-tests';base.mkdir(parents=True,exist_ok=True)
 work=Path(tempfile.mkdtemp(prefix='run-',dir=base));(work/'home').mkdir()
 env={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'HOME':str(work/'home'),'TMPDIR':str(work),'LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
 mono=deps.DATA/'src/monocypher-4.0.2/src'
 if not (mono/'monocypher.c').is_file():parser.error('existing pinned Monocypher source required')
 curl=shlex.split(subprocess.run(['pkg-config','--cflags','--libs','libcurl'],capture_output=True,text=True,check=True).stdout)
 inputs=SOURCES+[str(p.relative_to(ROOT)) for p in (ROOT/'port/network').glob('*.h')]
 hashes={s:sha(ROOT/s) for s in inputs}
 dependency={str(mono/'monocypher.c'):sha(mono/'monocypher.c'),str(mono/'monocypher.h'):sha(mono/'monocypher.h')}
 cases=[];controls=[];original=(ROOT/'port/game/ce_network_invitation.cpp').read_text()
 mutations={
  'origin-check-removed':('||!CeSignalHttpsOriginValid(origin)',''),
  'preview-reads-capability':('else if(directory->Read("pair-admission.json",bytes.value)!=1','else if((directory->Read("identity-token",bytes.value),directory->Read("pair-admission.json",bytes.value))!=1'),
  'expiry-boundary-ignored':('utc&&expires>utc&&expires-utc<=kPairLifetimeSeconds','utc&&expires-utc<=kPairLifetimeSeconds'),
  'actual-select-removed':('CE_LobbySignalingSelect(std::move(authority))','bool(authority)'),
  'import-folder-used-for-exchange':('NewExchangeDirectory(&exchange)','(exchange=s.chosen.data(),true)'),
  'selected-source-replaced':('    if(CE_LobbySignalingSelected()){Refuse(s,CE_INVITATION_ERROR_SELECTION);return 0;}\n',''),
 }
 variants={'positive':original}
 variants["private-channel"] = original
 if not args.no_controls:
  for name,(before,after) in mutations.items():
   if original.count(before)!=1:parser.error('causal control anchor is not unique')
   variants[name]=original.replace(before,after,1)
 widths=('32','64') if args.word_size=='both' else (args.word_size,)
 for bits in widths:
  crypto=work/('monocypher-'+bits+'.o');crypto_log=work/('crypto-'+bits+'.private.log')
  with crypto_log.open('w') as log:
   built=subprocess.run(['clang','-std=c99','-m'+bits,'-O1','-g0','-c',str(mono/'monocypher.c'),'-o',str(crypto)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,preexec_fn=bounds)
  if built.returncode:cases.append({'label':'dependency','word_size':bits,'passed':False,'log':str(crypto_log)});continue
  sha_object=work/('sha256-'+bits+'.o')
  with crypto_log.open('a') as log:
   built=subprocess.run(['clang','-std=c99','-m'+bits,'-O1','-g0','-c',str(ROOT/'port/updater/sha256.c'),'-o',str(sha_object)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,preexec_fn=bounds)
  if built.returncode:cases.append({'label':'sha-dependency','word_size':bits,'passed':False,'log':str(crypto_log)});continue
  for label,text in variants.items():
   view=work/(label+'-'+bits);game=view/'port/game';network=view/'port/network';common=view/'port/platform/common';stubs=view/'stubs/SDL3';driver=view/'port/tools/tests';updater=view/'port/updater'
   for p in (game,network,common,stubs,driver,updater):p.mkdir(parents=True,exist_ok=True)
   for name in ('ce_network_invitation.h','ce_network_stl.h','ce_network_boot_authority.h','ce_network_boot_authority.cpp'):shutil.copyfile(ROOT/'port/game'/name,game/name)
   (game/'ce_network_invitation.cpp').write_text(text)
   shutil.copyfile(ROOT/'port/updater/sha256.h',updater/'sha256.h')
   for p in (ROOT/'port/network').glob('*.h'):shutil.copyfile(p,network/p.name)
   for name in ('signaling_provider.cpp','signaling_https.cpp'):shutil.copyfile(ROOT/'port/network'/name,network/name)
   shutil.copyfile(ROOT/'port/tools/tests/network_invitation_driver.cpp',driver/'network_invitation_driver.cpp')
   shutil.copyfile(ROOT/'port/platform/common/ce_netlobby.h',common/'ce_netlobby.h');(common/'platform.h').write_text(PLATFORM);(stubs/'SDL.h').write_text(SDL)
   exe=view/'test';log_path=view/'fixture.private.log';data=view/'owned-fixture';data.mkdir(mode=0o700)
   for private_name in ("private_ui_input.cpp", "private_ui_input.h", "ce_netcode.h"):
    (common/private_name).write_bytes((ROOT/"port/platform/common"/private_name).read_bytes())
   if label == "private-channel":
    (driver/"private_ui_input_driver.cpp").write_bytes((ROOT/"port/tools/tests/private_ui_input_driver.cpp").read_bytes())
   command=['clang++','-std=c++17','-DCE_NETWORK_ICE=1','-O1','-g0','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-function','-m'+bits,'-pthread','-I'+str(view/'stubs'),'-I'+str(game),'-I'+str(mono),str(driver/'network_invitation_driver.cpp'),str(game/'ce_network_invitation.cpp'),str(game/'ce_network_boot_authority.cpp'),str(network/'signaling_provider.cpp'),str(network/'signaling_https.cpp'),str(crypto),str(sha_object),*curl,'-Wl,--wrap=openat','-o',str(exe)]
   command += ["-I",str(view/"port/platform/common"),str(view/"port/platform/common/private_ui_input.cpp")]
   if label == "private-channel":
    command[command.index(str(driver/"network_invitation_driver.cpp"))] = str(driver/"private_ui_input_driver.cpp")
    command += ["-DPRIVATE_UI_INVITATION"]
   row={'label':label,'word_size':bits,'compiled':False,'executed':False,'passed':False,'log':str(log_path)}
   with log_path.open('w') as log:
    p=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,preexec_fn=bounds)
    row['compile_exit']=p.returncode;row['compiled']=p.returncode==0
    if row['compiled']:
     p=subprocess.run([str(exe),str(data)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=15,preexec_fn=bounds)
     row['run_exit']=p.returncode;row['executed']=True
   match=re.search(r'^checks=(\d+) failures=(\d+)$',log_path.read_text(),re.M)
   if match:
    row['checks'],row['failures']=map(int,match.groups())
    row['passed']=row.get('run_exit')==(0 if label in ("positive", "private-channel") else 1) and (row['failures']==0 if label in ("positive", "private-channel") else row['failures']>0)
   (cases if label in ("positive", "private-channel") else controls).append(row)
   if label in ("positive", "private-channel") and not row['passed']:break
 unchanged=all(sha(ROOT/s)==h for s,h in hashes.items())
 result={'schema':1,'source_sha256':hashes,'dependency_sha256':dependency,'cases':cases,'controls':controls,'unchanged_inputs':unchanged,'passed':unchanged and len(cases)==2*len(widths) and all(r['passed'] for r in cases+controls),'limits':['fake SDL chooser and lobby wrapper; actual protected file reader, authority constructor, origin parser and provider','invented files and counter-only provider transport; no credentials, service, real UI or game acceptance']}
 path=work/'result.private.json';path.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({'passed':result['passed'],'cases':len(cases),'controls':len(controls),'receipt':str(path)}))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
