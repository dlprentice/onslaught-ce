#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual chat UI with authored SDL/font/backend controls, without a window."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile


GAME = r'''
#ifndef CE_CHAT_FAKE_GAME
#define CE_CHAT_FAKE_GAME
#include <cstdint>
#include <string>
#include <vector>
using WCHAR = uint16_t;
using DWORD = uint32_t;
struct SIZE { long cx, cy; };
enum { FONT_SMALL = 0 };
extern std::vector<std::u16string> drawn;
struct CDXBitmapFont {
 bool CanDisplay(WCHAR c) { return c >= 32 && c < 127; }
 int GetHeight() { return 16; }
 void GetTextExtent(WCHAR* text, SIZE* size) {
  unsigned count=0; while(text[count]) ++count; size->cx=count*8; size->cy=16;
 }
 void DrawTextWithShadow(float,float,DWORD,WCHAR* text,int,float,float,float) {
  std::u16string copy; while(*text) copy+=static_cast<char16_t>(*text++); drawn.push_back(copy);
 }
};
struct FakePlatform {
 CDXBitmapFont font;
 CDXBitmapFont* Font(int) { return &font; }
 int GetWindowWidth() { return 1280; }
 int GetWindowHeight() { return 720; }
};
struct FakeFrontend { bool running=false; bool IsRunning() { return running; } };
extern FakePlatform PLATFORM;
extern FakeFrontend FRONTEND;
#endif
'''

SDL = r'''
#ifndef CE_CHAT_FAKE_SDL
#define CE_CHAT_FAKE_SDL
#include <cstdint>
using SDL_WindowID=uint32_t;
enum SDL_Scancode { SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER,
 SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_A };
enum { SDL_EVENT_QUIT=256, SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_DESTROYED,
 SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_TEXT_INPUT };
struct SDL_Window { SDL_WindowID id; };
struct SDL_KeyboardEvent { uint32_t type; SDL_WindowID windowID; SDL_Scancode scancode; bool repeat; };
struct SDL_TextInputEvent { uint32_t type; SDL_WindowID windowID; const char* text; };
union SDL_Event { uint32_t type; SDL_KeyboardEvent key; SDL_TextInputEvent text; };
extern "C" SDL_Window* SDL_GetWindowFromID(SDL_WindowID);
extern "C" bool SDL_StartTextInput(SDL_Window*);
extern "C" bool SDL_StopTextInput(SDL_Window*);
#endif
'''


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_AS, (2*1024**3, 2*1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (30, 30))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--word-size",choices=("32","64","both"),default="both")
    parser.add_argument("--no-controls",action="store_true")
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[2]
    sys.path.insert(0,str(repo/'port/tools')); import deps
    base=repo/'local-data/port/network-chat-ui-tests'; base.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='run-',dir=base)); work.chmod(0o700)
    game=work/'game-headers';game.mkdir();sdl=work/'sdl-headers/SDL3';sdl.mkdir(parents=True)
    (game/'common.h').write_text(GAME)
    for name in ('game.h','frontend.h','Platform.h'):(game/name).write_text('#include "common.h"\n')
    (sdl/'SDL.h').write_text(SDL)
    env={'PATH':os.environ.get('PATH','/usr/bin:/bin'),'HOME':str(work),'TMPDIR':str(work),
         'LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
    ui=repo/'port/game/ce_network_chat_ui.cpp';driver=repo/'port/tools/tests/network_chat_ui_driver.cpp'
    sources=[repo/'port/network/optional/text_chat.cpp',repo/'port/network/optional/text_chat_owner.cpp']
    inputs=[ui,ui.with_suffix('.h'),driver,*sources,repo/'port/network/text_chat.h',
            repo/'port/network/text_chat_owner.h',Path(__file__).resolve()]
    receipt={'source_sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in inputs},'cases':[],'controls':[],'sdk_syntax':[],
             'limits':['actual UI against authored backend/font/SDL stubs',
                       'no window, real input device, game or visual acceptance']}
    widths=('32','64') if args.word_size=='both' else (args.word_size,)
    def run(label,width,source,enabled=True,control=False):
        log=work/(label+'-'+width+'.log');exe=work/(label+'-'+width)
        argv=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-O0','-g0','-m'+width,
              '-I'+str(game),'-I'+str(sdl.parent),str(source),str(driver),
              *(str(p) for p in sources),'-o',str(exe)]
        if enabled:argv.insert(1,'-DCE_NETWORK_TEXT=1')
        result={'label':label,'width':width,'compiled':False,'passed':False,'log':str(log)}
        try:
            with log.open('wb') as out:
                built=subprocess.run(argv,env=env,stdout=out,stderr=out,timeout=45,preexec_fn=limits)
                result['compiled']=built.returncode==0
                if built.returncode:return result
                executed=subprocess.run([str(exe)],env=env,stdout=out,stderr=out,
                                         timeout=10,preexec_fn=limits)
                result['run_exit']=executed.returncode
            match=re.search(r'^checks=(\d+) failures=(\d+)$',log.read_text(),re.M)
            if match:
                result['checks'],result['failures']=map(int,match.groups())
                result['passed']=(executed.returncode==(1 if control else 0) and
                    (result['failures']>0 if control else result['checks']>0 and result['failures']==0))
        except subprocess.TimeoutExpired:result['timeout']=True
        except OSError:result['launch_error']=True
        return result
    for width in widths:
        for enabled in (False,True):
            receipt['cases'].append(run('enabled' if enabled else 'disabled',width,ui,enabled))
        sdk=deps.DATA/'src/SDL3-3.4.16/include'
        log=work/('sdk-'+width+'.log')
        argv=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-m'+width,
              '-DCE_NETWORK_TEXT=1','-I'+str(game),'-I'+str(sdk),'-fsyntax-only',str(ui)]
        with log.open('wb') as out:
            built=subprocess.run(argv,env=env,stdout=out,stderr=out,timeout=45,preexec_fn=limits)
        receipt['sdk_syntax'].append({'width':width,'passed':built.returncode==0,'log':str(log)})
    if not args.no_controls and all(c['passed'] for c in receipt['cases']):
        original=ui.read_text()
        mutants={
         'repeat-submits':('if (!event->key.repeat) {\n            last_result = CE_LockstepChatSubmit();',
                           'if (true) {\n            last_result = CE_LockstepChatSubmit();'),
         'menu-captures':('return CE_LockstepChatAvailable() && !FRONTEND.IsRunning();',
                         'return CE_LockstepChatAvailable();'),
         'focus-retains-draft':('CE_LockstepChatCancel(); StopText(); release_key = SDL_SCANCODE_UNKNOWN;',
                                'StopText(); release_key = SDL_SCANCODE_UNKNOWN;'),
         'keyup-fence-removed':('return CanCompose() && (CE_LockstepChatComposing() || release_key != SDL_SCANCODE_UNKNOWN);',
                               'return CanCompose() && CE_LockstepChatComposing();')}
        for label,(before,after) in mutants.items():
            if original.count(before)!=1:
                receipt['controls'].append({'label':label,'passed':False,'anchor_error':True});continue
            folder=work/label;folder.mkdir();source=folder/ui.name
            source.write_text(original.replace(before,after,1));(folder/ui.with_suffix('.h').name).write_bytes(ui.with_suffix('.h').read_bytes())
            (folder/'ce_network_stl.h').write_bytes((ui.parent/'ce_network_stl.h').read_bytes())   # the UI source includes it by its own folder
            for width in widths:receipt['controls'].append(run(label,width,source,True,True))
    receipt['passed']=all(c['passed'] for group in ('cases','controls','sdk_syntax') for c in receipt[group])
    result=work/'result.private.json';result.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'passed':receipt['passed'],'cases':len(receipt['cases']),
                      'controls':len(receipt['controls']),'sdk_cases':len(receipt['sdk_syntax']),'receipt':str(result)}))
    return 0 if receipt['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
