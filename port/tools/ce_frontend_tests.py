#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual CE menu/message/footer bodies with authored boundaries; no game or display.

An existing pre-0074 generated mirror supplies headers for both target syntax
checks. The host fixture executes exact production routing, bounded text wrapping
and patched footer bodies. It uses conservative glyph widths, not retail font
metrics: fresh 640x480 and widescreen captures are still required for visual QA.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
import build


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(source, pattern):
    match=re.search(pattern,source,re.M)
    if not match:
        raise ValueError('source definition missing: '+pattern)
    start=match.start();brace=source.index('{',match.end())
    # These selected definitions contain no braces in string literals.
    depth=1;end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        if source[end]=='}':depth-=1
        end+=1
    return source[start:end]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mirror',type=Path,required=True)
    ap.add_argument('--compiler',default='g++')
    args=ap.parse_args()
    parent=ROOT/'local-data/port/ce-frontend-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));mirror=out/'mirror';mirror.mkdir()
    pins={}
    for source in args.mirror.rglob('*'):
        if source.is_file() and (source.suffix.lower() in ('.h','.hpp','.inl','.stf') or source.name=='FEPMain.cpp'):
            rel=source.relative_to(args.mirror);target=mirror/rel;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,target);pins[str(rel)]=sha(target)
    patch=ROOT/'port/patches/0074-ce-footer.patch'
    applied=subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(patch)],cwd=mirror,capture_output=True,text=True,timeout=10)
    (out/'patch.log').write_text(applied.stdout+applied.stderr)
    if applied.returncode:raise RuntimeError('0074 did not apply exactly: '+str(out/'patch.log'))
    source=(ROOT/'port/game/ce_frontend.cpp').read_text();fep=(mirror/'FEPMain.cpp').read_text()
    functions='\n'.join(body(source,p) for p in (r'^bool\s+PortCeMenus\(',r'^EFrontEndPage\s+PortCeAsOptions\(',
        r'^int\s+PortCeMainAction\(',r'^int\s+PortCeMainRows\(',r'^static void\s+Widen\(',
        r'^static void\s+Extent\(',r'^static int\s+WrapLines\('))
    types=source[source.index('class CCeMenuItem'):source.index('// The options list\'s Cheats item')]
    types+='\n'+source[source.index('static const char kSpecialThanks'):source.index('CPauseMenu::CPauseMenu(EPortCeNoLists)')]
    types+='\n'+body(source,r'^CPauseMenu::CPauseMenu\(EPortCeNoLists\)')
    start=fep.index('\tfloat\tv = RangeTransition(transition, 0.75f, 1.f);');end=fep.index('\tgInFrontEnd = true;',start)+len('\tgInFrontEnd = true;')
    footer=fep[start:end]
    pieces={'menu_functions.inc':functions,'menu_types.inc':types,'message.inc':body(source,r'^static void\s+ShowMessage\('),
            'menu_methods.inc':'\n'.join(body(source,p) for p in (r'^\s+CCeMenu\(\)',r'^\s+virtual void\s+ReceiveButtonAction\(',r'^\s+void\s+Leave\(',r'^\s+void\s+Choose\(')),
            'footer.inc':footer,
            'font_colours.inc':'\n'.join((body((ROOT/'src/DXFont.cpp').read_text(),r'^inline DWORD\s+ModulateColour\('),
                body((ROOT/'port/platform/public/DXFont.cpp').read_text(),r'^DWORD\s+StageColour\('),
                body((ROOT/'src/ltshell.cpp').read_text(),r'^void PCLTShell::SetModulate\(')))}
    for n,s in pieces.items():(out/n).write_text(s+'\n')
    driver=ROOT/'port/tools/tests/ce_frontend_driver.cpp'
    cases=[];commands=[]
    def compile_case(folder,version,extra=()):
        binary=folder/'driver'
        cmd=[args.compiler,'-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(folder),
             *(['-DCE_VERSION="'+version+'"'] if version is not None else []),*extra,str(driver),'-o',str(binary)]
        result=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        (folder/'build.log').write_text(result.stdout+result.stderr);commands.append(cmd)
        if result.returncode:raise RuntimeError('host compile failed: '+str(folder/'build.log'))
        return binary
    def run(binary,mode,expected=0,why=''):
        p=subprocess.run([str(binary),mode],capture_output=True,text=True,timeout=5)
        good=p.returncode==expected and (not why or why in p.stderr)
        cases.append(dict(binary=str(binary),mode=mode,expected_exit=expected,exit=p.returncode,stdout=p.stdout,stderr=p.stderr,passed=good))
        print(('PASS' if good else 'FAIL')+' '+binary.parent.name+' '+mode)
    binary=compile_case(out,'0.1.6-dev')
    for mode in ('menu-on','footer-off','footer-demo','footer-autoconfig'):run(binary,mode)
    private=out/'private';private.mkdir()
    for p in out.glob('*.inc'):shutil.copyfile(p,private/p.name)
    run(compile_case(private,None),'footer-private')
    long_version=out/'long-version';long_version.mkdir()
    for p in out.glob('*.inc'):shutil.copyfile(p,long_version/p.name)
    run(compile_case(long_version,'0.1.6-'+('x'*150),('-Wno-format-truncation',)),'footer-on')
    negatives=(('old-faded-colour','footer.inc','versionColour = 0x3c4b4f','versionColour = 0x102025','footer-on','subdued teal'),
               ('near-white-double-colour','footer.inc','versionColour = 0x3c4b4f','versionColour = 0x78969e','footer-on','subdued teal'),
               ('opaque-intro','footer.inc','SINT(v * 255.f)','SINT(255.f + v * 0.f)','footer-on','intro alpha'),
               ('old-preview-text','footer.inc','Onslaught CE V%s','Onslaught CE v%s (preview)','footer-on','installedversion'),
               ('wrong-thanks-action','menu_methods.inc','L"Special Thanks", CEA_THANKS','L"Special Thanks", CEA_REPORT','menu-on','no settings or external action'),
               ('changed-faithful-main','menu_functions.inc','if (!PortCeMenus())\n\t\treturn row;','if (!PortCeMenus())\n\t\treturn 9;','footer-off','main actions'),
               ('public-font-skip-double','font_colours.inc','if (USE_MODULATE_2X.mValue)','if (true)','footer-on','actual public colour path'),
               ('retail-font-skip-double','font_colours.inc','if (!USE_MODULATE_2X.mValue)','if (false)','footer-on','actual retail colour path'))
    for label,name,old,new,mode,why in negatives:
        folder=out/label;folder.mkdir()
        for p in out.glob('*.inc'):shutil.copyfile(p,folder/p.name)
        path=folder/name;text=path.read_text()
        if label=='opaque-intro':assert text.count(old)==2
        else:assert text.count(old)==1,(label,text.count(old))
        path.write_text(text.replace(old,new));run(compile_case(folder,'0.1.6-dev'),mode,2,why)
    syntax=[]
    for platform in ('linux','windows'):
        target=build.TARGETS[platform]
        includes=[build.PORT/'d3dx/include',build.PORT/'include',*target['includes'],mirror,mirror/'MissionScript']
        for source_path in (ROOT/'port/game/ce_frontend.cpp',mirror/'FEPMain.cpp'):
            cmd=[*target['cxx'],'-std=gnu++14',*build.MSCXX,*build.COMMON,*build.X87,*build.NO_MATH_BUILTINS,
                 '-DCE_VERSION="0.1.6-dev"','-fsyntax-only','-include',str(build.PORT/'include/port.h'),
                 *['-I'+str(p) for p in includes],str(source_path)]
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
            (out/(platform+'-'+source_path.name+'.log')).write_text(p.stdout+p.stderr)
            syntax.append(dict(platform=platform,source=source_path.name,command=cmd,exit=p.returncode))
    receipt=dict(source_sha256=sha(ROOT/'port/game/ce_frontend.cpp'),patch_sha256=sha(patch),
        driver_sha256=sha(driver),inputs=pins,pieces={n:sha(out/n) for n in pieces},commands=commands,cases=cases,syntax=syntax,
        limits=['Authored glyph widths/menus/dialog boundaries; actual native font/graphics need fresh capture review.',
                'No game/display/audio/device opened; no settings or careers accessed.'])
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(out/'result.json')
    return 0 if all(c['passed'] for c in cases) and all(c['exit']==0 for c in syntax) else 1


if __name__=='__main__':raise SystemExit(main())
