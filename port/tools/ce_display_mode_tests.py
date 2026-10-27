#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual settings, menu, parser and public host with authored files and fake device boundaries.

No game, user profile, window or GPU is opened. --compile-targets also compiles the actual affected units using both
pinned target compilers and this checkout's generated public mirror. Visual acceptance remains a separate game run.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'port'
sys.path.insert(0, str(PORT / 'tools'))
import cli_windowed_tests
import public_d3dapp_tests


def run(args, **kwargs):
    result = subprocess.run([str(x) for x in args], text=True, capture_output=True, timeout=90, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{args}\n{result.stdout}{result.stderr}")
    return result.stdout.strip()


def prepare(out):
    for name in ('CLIParams.cpp', 'PauseMenu.cpp', 'Career.cpp', 'CLIParams.h'):
        (out / name).write_bytes((ROOT / 'src' / name).read_bytes())
    for name in ('0046-widescreen-career-options.patch', '0071-force-windowed-cli.patch', '0086-ce-display-mode.patch'):
        run(['patch', '--batch', '--fuzz=0', '-p1', '-d', out, '-i', PORT / 'patches' / name, '--forward', '-r', '-'])
    parser = cli_windowed_tests.parser_source((out / 'CLIParams.cpp').read_text('latin-1'))
    prefix = ('extern int CE_DisplayModeStartupBridge(int,int);\n'
              'namespace ActualCLI { int CE_DisplayModeStartupBridge(int a,int b) '
              '{ return ::CE_DisplayModeStartupBridge(a,b); } }\n')
    (out / 'parser.cpp').write_text(prefix + parser)
    native = (ROOT / 'src/MenuItem.cpp').read_text('latin-1')
    signatures = (r'^CMenuMenuItem::CMenuMenuItem\(', r'^void\s+CMenuMenuItem::Refresh\(',
                  r'^void\s+CMenuMenuItem::Commit\(', r'^bool\s+CMenuMenuItem::HasPendingChange\(')
    (out / 'native_menu.inc').write_text('\n'.join(public_d3dapp_tests.function(native, p) for p in signatures) + '\n')
    (out / 'native_apply.inc').write_text(public_d3dapp_tests.function(
        native, r'^void\s+CApplyMenuItem::ReceiveButtonAction\(') + '\n')
    pause = (out / 'PauseMenu.cpp').read_text('latin-1')
    start = pause.index('#ifdef CE_VERSION\n\t// The CE choice')
    end = pause.index('#endif', start) + len('#endif')
    (out / 'native_row.inc').write_text(pause[start:end] + '\n')
    career = (out / 'Career.cpp').read_text('latin-1')
    key = career.index('memcpy(&gDisplayModeKey, source, sizeof(CDisplayModeKey));')
    start = career.index('\n\t{', key) + 1
    end = career.index('\n\tint\t\told = CLIPARAMS.mVSync;', start)
    (out / 'native_career_display.inc').write_text(career[start:end] + '\n')
    wide = (PORT / 'platform/common/widescreen.cpp').read_text()
    wide_signatures = (r'^int\s+PortWideDefaultSize\(', r'^int\s+PortWideDesktopSize\(',
                       r'^static int\s+WideStartSize\(', r'^int\s+PortWideStartSize\(',
                       r'^int\s+PortWideStartSizeOnDesktop\(')
    (out / 'native_wide.inc').write_text('\n'.join(public_d3dapp_tests.function(wide, p) for p in wide_signatures) + '\n')
    report = (PORT / 'updater/ce_report.h').read_text()
    start = report.index('typedef struct CeSettingsDir')
    end = report.index('} CeSettingsDir;', start) + len('} CeSettingsDir;')
    ce = (PORT / 'platform/common/ce.cpp').read_text()
    parts = [public_d3dapp_tests.function(report, r'^static inline void ce_user_dir\('), report[start:end],
             public_d3dapp_tests.function(report, r'^static inline void ce_settings_dir_close\('),
             public_d3dapp_tests.function(report, r'^static inline int ce_settings_dir_open\('),
             public_d3dapp_tests.function(ce, r'^static FILE \*ce_ini_open_regular\('),
             public_d3dapp_tests.function(ce, r'^static char \*ce_ini_read\('),
             public_d3dapp_tests.function(ce, r'^extern "C" int CE_SettingGet\('),
             public_d3dapp_tests.function(ce, r'^extern "C" int CE_SettingGetBool\(')]
    (out / 'native_windows_read.inc').write_text('\n'.join(parts) + '\n')
    for name in ('common.h', 'MenuItem.h', 'ltshell.h'):
        (out / name).write_text('// Test boundary definitions are in the driver.\n')
    public_d3dapp_tests.recovery_source(out, True)


def build_fixtures(out, overrides=None, targets=('ce', 'private', 'host', 'windows-read')):
    overrides = overrides or {}
    spec = importlib.util.spec_from_file_location('display_deps', PORT / 'tools/deps.py')
    deps = importlib.util.module_from_spec(spec); spec.loader.exec_module(deps)
    common = ['clang++', '-std=c++17', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
              '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Wl,--gc-sections',
              '-I', out, '-I', PORT / 'updater', '-I', PORT / 'platform/common', '-I', deps.PREFIX / 'include']
    product = [overrides.get('ce', PORT / 'platform/common/ce.cpp'),
               overrides.get('display', PORT / 'platform/common/display_mode.cpp'),
               overrides.get('parser', out / 'parser.cpp')]
    wrap = ['-Wl,--wrap=' + n for n in ('fopen', 'openat', 'fread', 'ferror', 'fwrite', 'fprintf', 'fflush', 'fsync', 'renameat')]
    for name, defines in (('ce', ['-DCE_VERSION="fixture"']), ('private', [])):
        if name not in targets: continue
        cmd = common + defines + ['-DVIDEO_SOURCE="' + str(overrides.get('video', PORT / 'game/ce_video_options.cpp')) + '"',
                                  *wrap, PORT / 'tools/tests/ce_display_mode_driver.cpp', *product, '-o', out / name]
        run(cmd)
    if 'windows-read' in targets:
        run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-I', out, PORT / 'tools/tests/ce_display_windows_driver.cpp', '-o', out / 'windows-read'])
    if 'host' not in targets: return
    host = r'''#define main ExistingHostDriverMain
#define PortWideDefaultSize DriverDefaultSize
#define PortWideStartSize DriverStartSize
#include "public_d3dapp_driver.cpp"
#undef PortWideDefaultSize
#undef PortWideStartSize
#undef main
// The actual widescreen default/start functions call SDL's primary before a window exists. Keep it independent
// of the D3D adapter's desktop so a secondary-monitor fallback cannot accidentally inherit the primary size.
using SDL_DisplayID=unsigned;
struct SDL_Window {};
struct SDL_DisplayMode { int w,h;float pixel_density; };
unsigned gTestW,gTestH;
void ReadTest(){} void PortVideoInit(){}
SDL_Window *PortMainSdlWindow(){return nullptr;}
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window*){return 2;}
SDL_DisplayID SDL_GetPrimaryDisplay(){return 1;}
SDL_DisplayMode primary={1920,1080,1.f};
const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID id){return id==1?&primary:nullptr;}
extern "C" int CE_SettingGet(const char *,int);
#define PORT_WIDE_KEY_W "wide_mode_w"
#define PORT_WIDE_KEY_H "wide_mode_h"
int PortWideDesktopSize(unsigned *,unsigned *);
#include "native_wide.inc"
extern "C" void ActualCLIProbe(const char *,int,unsigned *);
extern "C" int CE_DisplayModeUseSavedSize();
static void ActualCareerDisplayOptions(bool device_exists){
 struct { void *m_pd3dDevice; } LT{device_exists?reinterpret_cast<void *>(1):nullptr};
#include "native_career_display.inc"
}
int main(int argc,char **argv){
 if(argc!=3)return 3;
 unsigned v[15];ActualCLIProbe(argv[2],0,v);
 CLIPARAMS.mForceWindowed=v[0]; CLIPARAMS.mResWidth=v[2];
 wide=true;desktop={1920,1080,60,D3DFMT_X8R8G8B8};
 modes={{640,480,60,D3DFMT_X8R8G8B8},{800,600,60,D3DFMT_X8R8G8B8},
        {1024,768,60,D3DFMT_X8R8G8B8},{1920,1080,60,D3DFMT_X8R8G8B8}};
 if(!strcmp(argv[1],"secondary")||!strcmp(argv[1],"secondary-smaller")||
    !strcmp(argv[1],"no-saved-secondary")||!strcmp(argv[1],"sentinel-secondary")||
    !strcmp(argv[1],"remembered-secondary")||!strcmp(argv[1],"primary-smaller")||
    !strcmp(argv[1],"career-primary-smaller"))skipPrimary=true;
 if(!strcmp(argv[1],"primary-smaller")||!strcmp(argv[1],"career-primary-smaller")||
    !strncmp(argv[1],"career-inspect",14)){
  primary={1280,800,1.f};modes.push_back({1280,800,60,D3DFMT_X8R8G8B8});
 }
 if(!strcmp(argv[1],"no-modes"))modes.clear();
 if(!strcmp(argv[1],"smaller-monitor")||!strcmp(argv[1],"window-only-smaller")||
    !strcmp(argv[1],"secondary-smaller")||!strcmp(argv[1],"no-saved-secondary")||
    !strcmp(argv[1],"sentinel-secondary")||!strcmp(argv[1],"remembered-secondary")){
  desktop={1280,800,60,D3DFMT_X8R8G8B8};
  modes={{640,480,60,D3DFMT_X8R8G8B8},{800,600,60,D3DFMT_X8R8G8B8},
         {1024,768,60,D3DFMT_X8R8G8B8},{1280,800,60,D3DFMT_X8R8G8B8}};
  if(!strcmp(argv[1],"window-only-smaller"))modes.clear();
 }
 gDisplayModeKey.mWidth=800;gDisplayModeKey.mHeight=600;gDisplayModeKey.m32Bit=1;
 if(!strcmp(argv[1],"unavailable")||!strcmp(argv[1],"smaller-monitor")||
    !strcmp(argv[1],"window-only-smaller")||!strcmp(argv[1],"secondary-smaller")){
  gDisplayModeKey.mWidth=1600;gDisplayModeKey.mHeight=900;
 }
 if(!strcmp(argv[1],"no-saved-secondary")||!strcmp(argv[1],"primary-smaller")){
  gDisplayModeKey.mWidth=0;gDisplayModeKey.mHeight=0;
 }
 if(!strcmp(argv[1],"sentinel-secondary")||!strcmp(argv[1],"remembered-secondary")){
  gDisplayModeKey.mWidth=640;gDisplayModeKey.mHeight=480;
 }
 if(!strcmp(argv[1],"career-primary-smaller")||!strncmp(argv[1],"career-inspect",14)){
  gDisplayModeKey.mWidth=640;gDisplayModeKey.mHeight=480;
  if(!strcmp(argv[1],"career-inspect-saved")){gDisplayModeKey.mWidth=800;gDisplayModeKey.mHeight=600;}
  ActualCareerDisplayOptions(!strcmp(argv[1],"career-inspect-device"));
  if(!strncmp(argv[1],"career-inspect",14)){
   printf("%u %u %d\n",gDisplayModeKey.mWidth,gDisplayModeKey.mHeight,CE_DisplayModeUseSavedSize());return 0;
  }
 }
 if(!strncmp(argv[1],"wide-policy",11)){
  if(!strcmp(argv[1],"wide-policy-test")){gTestW=1600;gTestH=900;}
  if(!strcmp(argv[1],"wide-policy-disabled"))wide=false;
  unsigned sw=0,sh=0;
  if(!strcmp(argv[1],"wide-policy-sentinel")){sw=640;sh=480;}
  if(!strcmp(argv[1],"wide-policy-saved")){sw=800;sh=600;}
  unsigned a=0,b=0,c=0,d=0;
  int x=PortWideStartSize(sw,sh,1,&a,&b),y=PortWideStartSizeOnDesktop(sw,sh,1,1280,800,&c,&d);
  printf("%d %u %u %d %u %u\n",x,a,b,y,c,d);return 0;
 }
 Application app;app.SetResolution(v[4],v[5]);app.m_bUseDepthBuffer=TRUE;
 HRESULT h=app.Create(nullptr);
 printf("%d %u %d %u %u %u %d %u\n",int(h),v[0],app.m_bWindowed,app.m_d3dpp.BackBufferWidth,
        app.m_d3dpp.BackBufferHeight,createdAdapters.empty()?999:createdAdapters[0],CE_DisplayModeUseSavedSize(),resetCount);
 app.Cleanup3DEnvironment();return DeviceRefs()||QueryRefs()||d3d.refs?4:0;
}
'''
    (out / 'host.cpp').write_text(host)
    cmd = common + ['-DCE_VERSION="fixture"', '-DBEA_PUBLIC_D3DAPP_TEST', '-DBEA_PUBLIC_GRAPHICS_HOST=1',
                    '-I', PORT / 'tools/tests', out / 'host.cpp',
                    overrides.get('host', PORT / 'platform/public/d3dapp.cpp'), *product,
                    '-o', out / 'host']
    run(cmd)


def negative_controls(base):
    """Prove the selected caller assertions reject the corresponding source regressions."""
    cases = (
        ('replay-fence', 'display', PORT / 'platform/common/display_mode.cpp',
         'if (value && *value) return 0;', 'if (false && value && *value) return 0;', 'ce'),
        ('menu-rollback', 'video', PORT / 'game/ce_video_options.cpp',
         'if (failed) mSelection = mValue = GetValue();', '/* injected: retain the refused selection */', 'ce'),
        ('saved-size', 'host', PORT / 'platform/public/d3dapp.cpp',
         'm_bWindowed && !CLIPARAMS.mResWidth && CE_DisplayModeUseSavedSize()',
         'false && m_bWindowed && !CLIPARAMS.mResWidth && CE_DisplayModeUseSavedSize()', 'host'),
        ('empty-command', 'parser', None,
         'mForceWindowed = CE_DisplayModeStartupBridge(mForceWindowed, mAutoConfigTest || PLAYABLE_DEMO);',
         '/* injected: skip the empty-command preference */', 'ce'),
        ('binary-unknown', 'ce', PORT / 'platform/common/ce.cpp',
         'if (bad) { free(text); return def; }',
         "if (bad || memchr(text, '\\0', len)) { free(text); return def; }", 'ce'),
        ('fifo-block', 'ce', PORT / 'platform/common/ce.cpp',
         'O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC', 'O_RDONLY | O_NOFOLLOW | O_CLOEXEC', 'ce'),
        ('symlink-follow', 'ce', PORT / 'platform/common/ce.cpp',
         'O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC', 'O_RDONLY | O_NONBLOCK | O_CLOEXEC', 'ce'))
    results = {}
    for label, kind, source, old, new, target in cases:
        out = base / ('negative-' + label); out.mkdir()
        prepare(out)
        text = (source or out / 'parser.cpp').read_text()
        assert old in text, label
        text = text.replace(old, new, 1)
        if kind == 'ce':
            for relative, absolute in (('../../network/compatibility.h', PORT / 'network/compatibility.h'),
                                       ('../../updater/ce_report.h', PORT / 'updater/ce_report.h')):
                text = text.replace('"' + relative + '"', '"' + str(absolute) + '"')
        mutant = out / ('mutant-' + kind + '.cpp'); mutant.write_text(text)
        build_fixtures(out, {kind: mutant}, (target,))
        user = out / 'user'; user.mkdir()
        if label == 'fifo-block': os.mkfifo(user / 'ce.ini')
        elif label == 'symlink-follow':
            target_file = user / 'target'; target_file.write_bytes(b'windowed=1\n'); (user / 'ce.ini').symlink_to(target_file)
        else: (user / 'ce.ini').write_bytes(b'unknown=\x00\xff\nwindowed=1\n')
        env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_PORT_', 'BEA_USER_DIR', 'SDL_'))}
        env['BEA_USER_DIR'] = str(user)
        if label == 'replay-fence':
            env.update(BEA_PORT_CE_LAYER='1', BEA_PORT_TAPE='fixture')
            result = run([out / 'ce', 'cli', '0', '-level 110'], env=env)
            assert result != '0 0 0 0 640 480 0 0 0', (label, result)
        elif label == 'menu-rollback':
            (user / 'ce.ini').write_bytes(b'unknown=\x00\xff\nwindowed=0\n')
            p = subprocess.run([str(out / 'ce'), 'menu', 'short'], env=env, text=True, capture_output=True, timeout=10)
            assert p.returncode == 2 and 'native commit and refusal' in p.stderr, (label, p.returncode, p.stderr)
            result = 'refused selection assertion fails'
        elif label == 'saved-size':
            result = run([out / 'host', 'saved', ''], env=env)
            assert result == '0 1 1 640 480 0 1 0', (label, result)
            secondary = run([out / 'host', 'secondary-smaller', ''], env=env)
            assert secondary == '0 1 1 640 480 1 1 0', (label, secondary)
        elif label == 'empty-command':
            result = run([out / 'ce', 'cli', '0', ''], env=env)
            assert result == '0 0 0 0 640 480 0 1 0', (label, result)
        elif label == 'binary-unknown':
            result = run([out / 'ce', 'get', '7', 'none'], env=env)
            assert result == '7', (label, result)
        elif label == 'symlink-follow':
            result = run([out / 'ce', 'get', '7', 'none'], env=env)
            assert result == '1', (label, result)
        else:
            try:
                subprocess.run([str(out / 'ce'), 'cli', '0', ''], env=env, text=True,
                               capture_output=True, timeout=1)
            except subprocess.TimeoutExpired:
                result = 'unsafe FIFO open timed out'
            else: raise AssertionError((label, 'unsafe FIFO open unexpectedly returned'))
        results[label] = result
    out = base / 'negative-primary-default'; out.mkdir(); prepare(out)
    wide = out / 'native_wide.inc'; text = wide.read_text()
    old = 'if (!gTestW && desktopW && desktopH)'; assert text.count(old) == 1
    wide.write_text(text.replace(old, 'if (false && !gTestW && desktopW && desktopH)', 1))
    build_fixtures(out, targets=('host',))
    user = out / 'user'; user.mkdir(); (user / 'ce.ini').write_bytes(b'windowed=1\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_PORT_', 'BEA_USER_DIR', 'SDL_'))}
    env['BEA_USER_DIR'] = str(user)
    result = run([out / 'host', 'primary-smaller', ''], env=env)
    assert result == '0 1 1 1280 800 1 1 0', result
    results['primary-default'] = result
    out = base / 'negative-career-startup'; out.mkdir(); prepare(out)
    career = out / 'native_career_display.inc'; text = career.read_text()
    old = '(LT.m_pd3dDevice || !CE_DisplayModeUseSavedSizeBridge()) &&'; assert text.count(old) == 1
    career.write_text(text.replace(old, 'true &&', 1)); build_fixtures(out, targets=('host',))
    user = out / 'user'; user.mkdir(); (user / 'ce.ini').write_bytes(b'windowed=1\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_PORT_', 'BEA_USER_DIR', 'SDL_'))}
    env['BEA_USER_DIR'] = str(user)
    result = run([out / 'host', 'career-primary-smaller', ''], env=env)
    assert result == '0 1 1 1280 800 1 1 0', result
    results['career-startup'] = result
    for label, old, new, scenario in (
        ('windows-final-reparse', 'FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT',
         'FILE_ATTRIBUTE_DIRECTORY', 'reparse'),
        ('windows-parent-reparse', '(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)',
         'false', 'parent-link')):
        out = base / ('negative-' + label); out.mkdir(); prepare(out)
        path = out / 'native_windows_read.inc'; text = path.read_text()
        assert text.count(old) == 1, (label, text.count(old)); path.write_text(text.replace(old, new, 1))
        build_fixtures(out, targets=('windows-read',))
        p = subprocess.run([str(out / 'windows-read'), scenario, 'boolean'], text=True, capture_output=True, timeout=10)
        assert p.returncode == 2 and 'reader result or default' in p.stderr, (label, p.returncode, p.stderr)
        results[label] = 'reparse refusal assertion fails'
    return results


def compile_targets(out):
    sys.path.insert(0, str(PORT))
    import build
    build.PUBLIC = True
    build.build_mirror()
    results = {}
    for name in ('linux', 'windows'):
        t = build.TARGETS[name]
        game_inc = [PORT / 'd3dx/include', PORT / 'include', PORT / 'game', *t['includes'],
                    build.MIRROR, build.MIRROR / 'MissionScript']
        game_flags = [*t['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87,
                      *build.NO_MATH_BUILTINS, '-DCE_VERSION="fixture"', '-DBEA_PUBLIC_GRAPHICS_HOST=1',
                      '-c', '-include', PORT / 'include/port.h', *(f'-I{x}' for x in game_inc)]
        sdl = build.deps.PREFIX if name == 'linux' else build.deps.PREFIX_WIN
        platform_inc = [PORT / 'd3dx/include', PORT / 'include', *t['includes'], sdl / 'include']
        platform_flags = [*t['cxx'], '-std=gnu++17', '-DNOMINMAX', '-fms-extensions', '-fno-autolink',
                          '-fno-strict-aliasing', '-fwrapv', '-mms-bitfields', '-fshort-wchar', '-w',
                          '-DCE_VERSION="fixture"', '-c', *(f'-I{x}' for x in platform_inc)]
        units = [build.MIRROR / 'CLIParams.cpp', build.MIRROR / 'PauseMenu.cpp', build.MIRROR / 'Career.cpp',
                 PORT / 'game/ce_video_options.cpp', build.MIRROR / 'd3dapp.cpp']
        commands, outputs = [], []
        all_units = [(unit, game_flags) for unit in units] + [
            (PORT / 'platform/common' / unit, platform_flags) for unit in ('ce.cpp', 'display_mode.cpp', 'widescreen.cpp')]
        for unit, flags in all_units:
            obj = out / (name + '-' + unit.name + '.o')
            command = [*flags, unit, '-o', obj]
            run(command)
            blob = obj.read_bytes()
            if name == 'linux':
                assert blob[:5] == b'\x7fELF\x01' and blob[18:20] == b'\x03\x00'
            else:
                assert blob[:2] == b'\x4c\x01'
            commands.append(list(map(str, command)))
            outputs.append({'source': str(unit.relative_to(ROOT)), 'object_sha256': hashlib.sha256(blob).hexdigest()})
        results[name] = {'compiler': t['cxx'][0], 'commands': commands, 'objects': outputs}
    # The same patch queue must also compile with CE_VERSION absent and the private graphics header, preserving
    # the original options-loader path for faithful builds.
    build.PUBLIC = False
    build.build_mirror()
    for name in ('linux', 'windows'):
        t = build.TARGETS[name]
        inc = [PORT / 'd3dx/include', PORT / 'include', PORT / 'game', *t['includes'],
               build.MIRROR, build.MIRROR / 'MissionScript']
        unit = build.MIRROR / 'Career.cpp'; obj = out / (name + '-private-Career.cpp.o')
        command = [*t['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
                   '-c', '-include', PORT / 'include/port.h', *(f'-I{x}' for x in inc), unit, '-o', obj]
        run(command); blob = obj.read_bytes()
        if name == 'linux': assert blob[:5] == b'\x7fELF\x01' and blob[18:20] == b'\x03\x00'
        else: assert blob[:2] == b'\x4c\x01'
        results[name]['commands'].append(list(map(str, command)))
        results[name]['objects'].append({'source': str(unit.relative_to(ROOT)) + ' (private)',
                                         'object_sha256': hashlib.sha256(blob).hexdigest()})
    return results


class DisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        base = ROOT / 'local-data/ce-display-mode-tests'; base.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix='run-', dir=base))
        prepare(cls.out); build_fixtures(cls.out)

    def setUp(self):
        self.user = Path(tempfile.mkdtemp(prefix='user-', dir=self.out))
        self.ini = self.user / 'ce.ini'
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_PORT_', 'BEA_USER_DIR', 'SDL_'))}
        self.env['BEA_USER_DIR'] = str(self.user)

    def call(self, binary, *args, env=None):
        return run([self.out / binary, *args], env=env or self.env)

    def test_strict_boolean_and_binary_lines(self):
        self.assertEqual(self.call('ce', 'get', '7', 'none'), '7')
        cases = [(b'', 7), (b'windowed=0\n', 0), (b'windowed= \t1 \t\r\n', 1),
                 (b'windowed=1', 1), (b'other=\xff\x00\nwindowed=1\nmore=\x00\xfe\n', 1),
                 (b'windowed_extra=1\n', 7), (b'windowed=\n', 7), (b'windowed=2\n', 7),
                 (b'windowed=-1\n', 7), (b'windowed=01\n', 7), (b'windowed=1 comment\n', 7),
                 (b'windowed=1\x00\n', 7), (b'windowed=1\r\r\n', 7),
                 (b'windowed=1\nwindowed=1\n', 7), (b'windowed=bad\nwindowed=1\n', 7),
                 (b'windowed=1\nwindowed=bad\n', 7), (b'windowed=1\n' + b'x' * (1 << 20), 7)]
        for data, expected in cases:
            with self.subTest(data=data[:50]):
                self.ini.write_bytes(data)
                self.assertEqual(int(self.call('ce', 'get', '7', 'none')), expected)
        self.ini.write_bytes(b'windowed=1\n')
        self.assertEqual(self.call('ce', 'get', '7', 'read'), '7')

    def test_integer_reader_regular_file_compatibility(self):
        cases = [(b'', 7), (b'other=1\n', 7), (b'wide_mode_w=800tail\nwide_mode_w=640\n', 800),
                 (b'wide_mode_w= \t-42text\n', -42), (b'wide_mode_w=bad\n', 0),
                 (b'other=\x00bytes\nwide_mode_w=800\n', 7)]
        for data, expected in cases:
            with self.subTest(data=data):
                self.ini.write_bytes(data)
                self.assertEqual(int(self.call('ce', 'get-int', '7')), expected)
        self.ini.write_bytes(b'cheat_malloy=1\n'); self.assertEqual(self.call('ce', 'cheat'), '1')
        self.ini.write_bytes(b'cheat_malloy=0\n'); self.assertEqual(self.call('ce', 'cheat'), '0')

    def test_settings_reader_refuses_nonregular_and_links_without_blocking(self):
        missing = self.user / 'missing' / 'user'
        env = dict(self.env, BEA_USER_DIR=str(missing))
        self.assertEqual(self.call('ce', 'get', '7', 'none', env=env), '7')
        self.assertFalse(missing.exists())
        os.mkfifo(self.ini)
        for args, expected in ((('get', '7', 'none'), '7'), (('get-int', '7'), '7'), (('cheat',), '0'),
                               (('cli', '0', ''), '0 0 0 0 640 480 0 1 1'),
                               (('cli', '0', '-forcewindowed'), '1 0 0 0 640 480 0 1 0')):
            result = subprocess.run([str(self.out / 'ce'), *args], env=self.env, text=True,
                                    capture_output=True, timeout=2)
            self.assertEqual((result.returncode, result.stdout.strip()), (0, expected), result.stderr)
        for args, expected in ((('',), '0 0 0 640 480 1 0 0'),
                               (('-forcewindowed',), '0 1 1 640 480 1 0 0')):
            result = subprocess.run([str(self.out / 'host'), 'sentinel-secondary', *args], env=self.env,
                                    text=True, capture_output=True, timeout=2)
            self.assertEqual((result.returncode, result.stdout.strip()), (0, expected), result.stderr)
        self.assertTrue(self.ini.is_fifo())

        target = self.user / 'target'; target.write_bytes(b'windowed=1\nwide_mode_w=800\n')
        linked = self.user / 'linked'; linked.mkdir(); (linked / 'ce.ini').symlink_to(target)
        link_env = dict(self.env, BEA_USER_DIR=str(linked))
        self.assertEqual(self.call('ce', 'get', '7', 'none', env=link_env), '7')
        self.assertEqual(self.call('ce', 'get-int', '7', env=link_env), '7')
        self.assertEqual(target.read_bytes(), b'windowed=1\nwide_mode_w=800\n')
        ancestor = self.user / 'ancestor'; ancestor.symlink_to(linked, target_is_directory=True)
        self.assertEqual(self.call('ce', 'get', '7', 'none', env=dict(self.env, BEA_USER_DIR=str(ancestor))), '7')
        self.assertEqual(self.call('ce', 'get-int', '7', env=dict(self.env, BEA_USER_DIR=str(ancestor))), '7')
        nonregular = self.user / 'directory'; nonregular.mkdir(); (nonregular / 'ce.ini').mkdir()
        self.assertEqual(self.call('ce', 'get', '7', 'none', env=dict(self.env, BEA_USER_DIR=str(nonregular))), '7')

    def test_windows_reader_actual_source_with_win32_boundary_seams(self):
        for name in ('regular', 'missing', 'parent-missing', 'parent-link', 'reparse', 'directory', 'pipe',
                     'info-fail', 'fd-fail', 'stream-fail'):
            for reader in ('boolean', 'integer'):
                with self.subTest(name=name, reader=reader):
                    self.assertEqual(self.call('windows-read', name, reader), name + ' pass')

    def test_atomic_preservation_duplicate_repair_and_failures(self):
        original = b'other=\x00\xff\nwindowed=0\r\nkept=\xfe\nwindowed=1\nlast=ok\n'
        self.ini.write_bytes(original)
        self.assertEqual(self.call('ce', 'store', '1', 'none'), '0 1 1')
        self.assertEqual(self.ini.read_bytes(), b'other=\x00\xff\nwindowed=1\nkept=\xfe\nlast=ok\n')
        self.ini.write_bytes(b'no-newline=\x00\xff')
        self.assertEqual(self.call('ce', 'store', '1', 'none'), '0 1 1')
        self.assertEqual(self.ini.read_bytes(), b'no-newline=\x00\xff\nwindowed=1\n')
        for failure in ('read', 'short', 'format', 'flush', 'sync', 'rename'):
            with self.subTest(failure=failure):
                old = b'other=\x00\xff\nwindowed=0\nlast=\xfe\n'; self.ini.write_bytes(old)
                self.assertEqual(self.call('ce', 'store', '1', failure), '0 0 0')
                self.assertEqual(self.ini.read_bytes(), old)
                self.assertEqual(sorted(p.name for p in self.user.iterdir()), ['ce.ini'])
        old = b'unknown=\x00\xff\nwindowed=0\n'; self.ini.write_bytes(old)
        for invalid in ('-1', '2', '42'):
            self.assertEqual(self.call('ce', 'store', invalid, 'none'), '0 0 0')
            self.assertEqual(self.ini.read_bytes(), old)
        self.ini.unlink(); outside = self.user / 'outside'; outside.write_bytes(old); self.ini.symlink_to(outside)
        self.assertEqual(self.call('ce', 'store', '1', 'none'), '0 0 0')
        self.assertEqual(outside.read_bytes(), old); self.assertTrue(self.ini.is_symlink())

    def test_native_menu_commit_failure_and_retry(self):
        for failure in ('none', 'short', 'format', 'flush', 'sync', 'rename', 'read'):
            with self.subTest(failure=failure):
                self.ini.write_bytes(b'unknown=\x00\xff\nwindowed=0\n')
                self.assertEqual(self.call('ce', 'menu', failure), 'menu pass')
                self.assertEqual(self.ini.read_bytes(), b'unknown=\x00\xff\nwindowed=1\n')

    def test_cli_parser_replay_override_and_row_admission(self):
        self.ini.write_bytes(b'windowed=1\n')
        for joining in range(4):
            with self.subTest(joining=joining):
                self.assertEqual(self.call('ce', 'cli', str(joining), ''), '1 0 0 0 640 480 1 1 1')
                self.assertEqual(self.call('ce', 'cli', str(joining), '-forcewindowed -res 800 600'),
                                 '1 0 800 600 800 600 0 1 0')
                self.assertEqual(self.call('ce', 'cli', str(joining), '-res 800 600'),
                                 '1 0 800 600 800 600 1 1 1')
        for arg in ('-autoconfigtest', '-playabledemo'):
            v = self.call('ce', 'cli', '0', arg).split()
            self.assertEqual((v[0], v[6], v[8]), ('0', '0', '0'))
        for key in ('BEA_PORT_CLOCK', 'BEA_PORT_TAPE', 'BEA_PORT_PROBES', 'BEA_PORT_RECORD'):
            with self.subTest(key=key):
                env = dict(self.env, BEA_PORT_CE_LAYER='1', **{key: '1'})
                self.assertEqual(self.call('ce', 'cli', '0', '-level 110', env=env), '0 0 0 0 640 480 0 0 0')
                self.assertEqual(self.call('ce', 'cli', '0', '-forcewindowed', env=env), '1 0 0 0 640 480 0 0 0')
                self.assertEqual(self.call('ce', 'row', '1', env=env), '0 0')
                self.assertEqual(self.call('ce', 'store', '1', 'none', env=env), '0 0 0')
                self.assertEqual(self.ini.read_bytes(), b'windowed=1\n')
        off = dict(self.env, BEA_PORT_CE_LAYER='0')
        self.assertEqual(self.call('ce', 'row', '1', env=off), '0 0')
        self.assertEqual(self.call('ce', 'row', '0'), '0 0')
        self.assertEqual(self.call('ce', 'row', '1'), '1 1')
        self.assertEqual(self.call('private', 'cli', '0', '-level 110'), '0 0 0 0 640 480 0 0 0')
        self.assertEqual(self.call('private', 'row', '1'), '0 0')
        self.ini.write_bytes(b'windowed=0\n')
        self.assertEqual(self.call('ce', 'cli', '0', '-forcewindowed'), '1 0 0 0 640 480 0 1 0')

    def test_public_host_resolution_monitor_and_override(self):
        self.ini.write_bytes(b'windowed=1\n')
        cases = [('saved', '', '0 1 1 800 600 0 1 0'),
                 ('secondary', '', '0 1 1 800 600 1 1 0'),
                 ('unavailable', '', '0 1 1 1920 1080 0 1 0'),
                 ('no-modes', '', '0 1 1 800 600 0 1 0'),
                 ('smaller-monitor', '', '0 1 1 1280 800 0 1 0'),
                 ('window-only-smaller', '', '0 1 1 1280 800 0 1 0'),
                 ('secondary-smaller', '', '0 1 1 1280 800 1 1 0'),
                 ('no-saved-secondary', '', '0 1 1 1280 800 1 1 0'),
                 ('sentinel-secondary', '', '0 1 1 1280 800 1 1 0'),
                 ('primary-smaller', '', '0 1 1 1920 1080 1 1 0'),
                 ('saved', '-res 1024 768', '0 1 1 1024 768 0 1 0'),
                 ('saved', '-forcewindowed', '0 1 1 640 480 0 0 0'),
                 ('saved', '-forcewindowed -res 1024 768', '0 1 1 1024 768 0 0 0')]
        for name, args, expected in cases:
            with self.subTest(name=name, args=args): self.assertEqual(self.call('host', name, args), expected)
        self.ini.write_bytes(b'windowed=1\nwide_mode_w=800\nwide_mode_h=600\n')
        self.assertEqual(self.call('host', 'remembered-secondary', ''), '0 1 1 800 600 1 1 0')
        self.assertEqual(self.call('host', 'wide-policy', ''), '1 1920 1080 1 1280 800')
        self.assertEqual(self.call('host', 'wide-policy-test', ''), '1 1600 900 1 1600 900')
        self.assertEqual(self.call('host', 'wide-policy-disabled', ''), '0 0 0 0 0 0')
        self.assertEqual(self.call('host', 'wide-policy-saved', ''), '0 0 0 0 0 0')
        self.assertEqual(self.call('host', 'wide-policy-sentinel', ''), '1 800 600 1 800 600')
        self.ini.write_bytes(b'windowed=1\nwide_mode_w=640\nwide_mode_h=480\n')
        self.assertEqual(self.call('host', 'wide-policy-sentinel', ''), '0 0 0 0 0 0')
        self.ini.write_bytes(b'windowed=1\n')
        self.assertEqual(self.call('host', 'wide-policy-sentinel', ''), '1 1920 1080 1 1280 800')
        env = dict(self.env, BEA_PORT_CE_LAYER='1', BEA_PORT_TAPE='fixture')
        self.assertEqual(self.call('host', 'saved', '-forcewindowed', env=env), '0 1 1 640 480 0 0 0')
        self.ini.write_bytes(b'windowed=0\n')
        self.assertEqual(self.call('host', 'saved', ''), '0 0 0 800 600 0 0 0')

    def test_actual_options_load_block_before_selected_adapter(self):
        self.ini.write_bytes(b'windowed=1\n')
        cases = [('', '0 1 1 1920 1080 1 1 0'),
                 ('-forcewindowed', '0 1 1 640 480 1 0 0'),
                 ('-res 1024 768', '0 1 1 1024 768 1 1 0')]
        for args, expected in cases:
            with self.subTest(args=args):
                self.assertEqual(self.call('host', 'career-primary-smaller', args), expected)
        self.assertEqual(self.call('host', 'career-inspect', ''), '640 480 1')
        self.assertEqual(self.call('host', 'career-inspect', '-forcewindowed'), '1280 800 0')
        self.assertEqual(self.call('host', 'career-inspect', '-res 1024 768'), '640 480 1')
        self.assertEqual(self.call('host', 'career-inspect-device', ''), '1280 800 1')
        self.assertEqual(self.call('host', 'career-inspect-saved', ''), '800 600 1')
        replay = dict(self.env, BEA_PORT_CE_LAYER='1', BEA_PORT_TAPE='fixture')
        self.assertEqual(self.call('host', 'career-inspect', '', env=replay), '1280 800 0')
        self.ini.write_bytes(b'windowed=1\nwide_mode_w=640\nwide_mode_h=480\n')
        self.assertEqual(self.call('host', 'career-primary-smaller', ''), '0 1 1 640 480 1 1 0')
        self.ini.write_bytes(b'windowed=1\nwide_mode_w=800\nwide_mode_h=600\n')
        self.assertEqual(self.call('host', 'career-primary-smaller', ''), '0 1 1 800 600 1 1 0')
        self.ini.write_bytes(b'windowed=0\n')
        self.assertEqual(self.call('host', 'career-inspect', ''), '1280 800 0')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--compile-targets', action='store_true')
    ap.add_argument('--negative-controls', action='store_true'); args = ap.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DisplayTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful(): sys.exit(1)
    if args.compile_targets:
        result = compile_targets(DisplayTests.out)
        (DisplayTests.out / 'target-compiles.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'target_compiles': {name: len(r['objects']) for name, r in result.items()},
                          'receipt': str(DisplayTests.out / 'target-compiles.json')}))
    if args.negative_controls:
        result = negative_controls(DisplayTests.out)
        (DisplayTests.out / 'negative-controls.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'negative_controls': result}))
