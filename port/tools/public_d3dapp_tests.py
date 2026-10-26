#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the public graphics host with synthetic D3D/window controls, without game files or a display.

--baseline-source PATH additionally compares complete caller-visible output with the selected source's former
graphics host, using its actual function bodies and its widescreen patches. That source is optional and is never
copied into a tracked test. --sanitizers instruments the new host's synthetic executable with ASan/UBSan.
--syntax-targets checks the public implementation against both installed target header
sets and compilers; it does not fetch dependencies, compile a game or link an executable. Scratch stays local-data/.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SUPPORT = PORT / "tools/tests"
PUBLIC = PORT / "platform/public"
CASES = ("boot", "wide", "wide-only-on", "wide-only-off", "no-modes", "mixed", "saved", "saved-msaa", "preset-index", "windowed",
         "depth", "depth16-fallback", "depth-stencil", "depth32", "vp-fallback", "msaa-fallback", "lost-once", "fallback",
         "reset", "reset-retry", "query-reset", "paused-reset", "close", "minimize", "pause", "resize", "cleanup", "picker")
OWNERSHIP = ("owned-reset-ref", "owned-query-reset-ref", "owned-reset-ref-fail", "owned-failed-create")
MAPPING = ("physical-primary", "physical-secondary", "physical-recovery")
WINDOWED = ("desktop-only", "desktop-secondary", "explicit-client", "fullscreen-only", "window-rejected",
            "desktop-depth-rejected", "msaa-colour-rejected", "msaa-depth-rejected", "window-vp",
            "mode-transitions", "vp-transitions", "vp-incompatible", "window-resize", "client-width-limit",
            "client-height-limit", "client-zero", "client-negative", "desktop-unavailable", "window-only-escape",
            "ref-window-only", "physical-fullscreen", "window-rebuild", "failed-hal-slot", "owner-reuse", "mode-capacity", "client-shape", "resize-shape")
PUBLICATION = ("query", "caps", "depth", "size", "pending-index", "reset", "fullscreen-reset", "create", "finish", "init", "resize-reset", "resize-restore")


def command(args, **kwargs):
    def bounded_core(): resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True, timeout=60, preexec_fn=bounded_core, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{shlex.join([str(a) for a in args])}\n{result.stdout}{result.stderr}")
    return result.stdout


def function(text, expression):
    """Find one complete definition, balancing braces outside strings and comments."""
    match = re.search(expression, text, re.M)
    if not match:
        raise RuntimeError(f"missing baseline definition: {expression}")
    start = match.start()
    opening = text.index("{", match.end())
    depth, state, quote, i = 1, "code", "", opening + 1
    while i < len(text):
        c, nxt = text[i], text[i:i + 2]
        if state == "line":
            if c == "\n": state = "code"
        elif state == "block":
            if nxt == "*/": state = "code"; i += 1
        elif state == "string":
            if c == "\\": i += 1
            elif c == quote: state = "code"
        elif nxt == "//": state = "line"; i += 1
        elif nxt == "/*": state = "block"; i += 1
        elif c in "\"'": state = "string"; quote = c
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if not depth: return text[start:i + 1]
        i += 1
    raise RuntimeError("unterminated baseline function")


def baseline_source(root, scratch):
    selected = scratch / "baseline"
    selected.mkdir(exist_ok=True)
    for name in ("d3dapp.cpp", "Career.cpp"):
        (selected / name).write_bytes((root / "src" / name).read_bytes())
    for name in ("0041-widescreen-screen-shape.patch", "0045-widescreen-mode-gate.patch", "0046-widescreen-native-default.patch"):
        patch = root / "port/patches" / name
        command(["patch", "-p1", "-d", selected, "-i", patch, "--forward", "-r", "-"])
    text = (selected / "d3dapp.cpp").read_text()
    parts = ['#include "public_d3dapp_support.h"', '#define DX_SCREEN_WIDTH 640\n#define DX_SCREEN_HEIGHT 480',
             'static CD3DApplication *g_pD3DApp;\nstatic IDirect3DQuery9 *gResourceQuery;']
    parts += re.findall(r'^CTweakSLONG\s+[^\n]+;', text, re.M)
    helpers = ("Is32BitFormat", "FindMode", "FormatName", "LogDisplayMode", "DeviceTypeName", "SortModesCallback", "MakeDisplayModeKey", "WndProc")
    for name in helpers:
        parts.append(function(text, rf'^(?:static inline |LRESULT CALLBACK |int )[^\n]*\b{re.escape(name)}\s*\('))
    methods = ("CD3DApplication", "Create", "BuildDeviceList", "FindDepthStencilFormat", "MsgProc", "Initialize3DEnvironment",
               "Resize3DEnvironment", "ToggleFullscreen", "ForceWindowed", "AdjustWindowForChange", "Reset3DEnvironment",
               "Pause", "Cleanup3DEnvironment", "SetResolution", "UpdateScreenShape")
    for name in methods:
        body = function(text, rf'^(?:[A-Za-z_][\w *]*\s+)?CD3DApplication::{name}\s*\(')
        if name == "Reset3DEnvironment": body = body.replace("SelectDeviceProc", "PublicTestPicker")
        parts.append(body)
    parts.append(function(text, r'^D3DDeviceInfo::D3DDeviceInfo\s*\('))
    parts.append('HRESULT CD3DApplication::DisplayErrorMsg(HRESULT h, DWORD) { return h; }')
    out = selected / "baseline.cpp"
    out.write_text("\n\n".join(parts) + "\n")
    return out


def recovery_source(scratch, public):
    """Compile the actual active recovery caller block, with its normal member names bound to the test owner."""
    scratch.mkdir(parents=True, exist_ok=True)
    shell = scratch / "ltshell.cpp"
    shell.write_bytes((ROOT / "src/ltshell.cpp").read_bytes())
    if public:
        command(["patch", "-p1", "-d", scratch, "-i", PORT / "patches/0069-public-adapter-ordinal.patch", "--forward", "-r", "-"])
    text = shell.read_text()
    query = text.index("m_pD3D->GetAdapterDisplayMode( ")
    start = text.rfind("if( m_bWindowed )", 0, query)
    block = function(text[start:], r'^if\(\s*m_bWindowed\s*\)')
    aliases = "\n".join(f"    auto &{name} = app.{name};" for name in
                         ("m_bWindowed", "m_Adapters", "m_dwAdapter", "m_pD3D", "m_d3dpp"))
    (scratch / "public_d3dapp_recovery.h").write_text(
        "template<class T> void ActualRecoveryDisplayMode(T &app) {\n" + aliases + "\n" + block + "\n}\n")
    career = (ROOT / "src/Career.cpp").read_text()
    colour = function(career, r'^static inline bool\s+Is32BitFormat\s*\(')
    key = function(career, r'^void\s+CDisplayModeKey::Set\s*\(')
    adapter = function(career, r'^D3DAdapterInfo\s*\*\s*CD3DApplication::GetAdapterInfo\s*\(')
    write = function(career, r'^char\s*\*CCareer::WriteOptions\s*\(')
    write = write[write.index('D3DAdapterInfo\t*adapter'):write.index('memcpy(dest,')]
    read = function(career, r'^char\s*\*CCareer::ReadOptions\s*\(')
    read = function(read, r'^\s*if \(!\(gDisplayModeKey == old_key\)\)')
    read = read.replace('((FCareerApp &)LT)', 'LT')
    pause = function((ROOT / "src/PauseMenu.cpp").read_text(), r'^void\s+CSCModeMenuItem::SetValue\s*\(')
    pause = pause[pause.index('{'):]
    (scratch / "public_d3dapp_callers.h").write_text(colour + '\n' + key + '\n' + adapter + '\n' +
        'void ActualWriteKey(CD3DApplication &LT) {\n' + write + '\n}\n' +
        'void ActualReadKey(CD3DApplication &LT) { bool display_changed=false; CDisplayModeKey old_key;\n'
        'auto *current=LT.GetCurrentDevice(); old_key.Set(&current->modes[current->dwCurrentMode]);\n' + read + '\n}\n' +
        'HRESULT ActualSelectMode(CD3DApplication &LT, int value) { HRESULT result=E_FAIL;\n'
        'auto DisplaySettingChanged=[&] {result=LT.Reset3DEnvironment(TRUE,false);};\n' + pause + '\nreturn result;}\n')
    flip = function((ROOT / 'src/Platform.cpp').read_text(), r'^void\s+CPlatform::Flip\s*\(')
    (scratch / 'public_d3dapp_flip.h').write_text(flip + '\n')


def target_syntax(scratch):
    spec = importlib.util.spec_from_file_location("public_d3dapp_build", PORT / "build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    headers = scratch / "headers"
    paths = [p for p in (ROOT / "src").rglob("*") if p.is_file() and p.suffix.lower() in (".h", ".hpp", ".inc", ".inl")]
    targets = [headers / p.relative_to(ROOT / "src") for p in paths]
    index = build.CaseIndex(targets)
    build.SYSTEM_NAMES.update(build.system_header_names())
    for source, target in zip(paths, targets):
        target.parent.mkdir(parents=True, exist_ok=True)
        text = (PUBLIC / "d3dapp.h" if source.name == "d3dapp.h" else source).read_bytes().decode("latin-1")
        target.write_bytes(build.normalise(text, target.parent, index, [headers, headers / "MissionScript"]).encode("latin-1"))
    # The exported tree omits the old header, so also install the public header in that case.
    if not (headers / "d3dapp.h").exists(): (headers / "d3dapp.h").write_bytes((PUBLIC / "d3dapp.h").read_bytes())
    command(["patch", "-p1", "-d", headers, "-i", PORT / "patches/0002-global-operator-new.patch", "--forward", "-r", "-"])
    source = headers / "public-d3dapp.cpp"
    text = build.normalise((PUBLIC / "d3dapp.cpp").read_text(), headers, index, [headers])
    # window.cpp's optional fake-key diagnostic reads this one field by offset; check the actual target layout.
    text += '\nstatic_assert(__builtin_offsetof(PCLTShell, KeyDown) == 0x330e4, "platform input diagnostic layout");\n'
    source.write_bytes(text.encode("latin-1"))
    for name in ("linux", "windows"):
        target = build.TARGETS[name]
        if not Path(target["cxx"][0]).is_file(): raise RuntimeError(f"installed {name} compiler missing; no download attempted")
        inc = [PORT / "d3dx/include", PORT / "include"] + target["includes"] + [headers, headers / "MissionScript"]
        args = target["cxx"] + ["-std=gnu++14"] + build.MSCXX + build.COMMON + build.X87 + ["-fsyntax-only", "-include", PORT / "include/port.h"]
        args += [item for path in inc for item in ("-I", path)] + [source]
        command(args)
        print(f"{name}: public implementation and actual consumer headers compile")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-source", type=Path)
    parser.add_argument("--syntax-targets", action="store_true")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--before-ref", help="also prove ownership/mapping negatives from this frozen public-host commit")
    parser.add_argument("--window-before-ref", help="prove windowed admission/persistence negatives from this frozen public host")
    parser.add_argument("--publication-before-ref", help="prove failed publication persistence negatives from this frozen public host")
    args = parser.parse_args()
    scratch = ROOT / "local-data/public-d3dapp-tests"
    scratch.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get("CXX", "clang++"))
    common = compiler + ["-std=c++14", "-DBEA_PUBLIC_D3DAPP_TEST", "-DBEA_PUBLIC_GRAPHICS_HOST=1", "-I", str(SUPPORT)]
    recovery_source(scratch / "recovery", True)
    binary = scratch / "public-driver"
    instrumentation = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitizers else []
    command(common + instrumentation + ["-I", scratch / "recovery", PUBLIC / "d3dapp.cpp", SUPPORT / "public_d3dapp_driver.cpp", "-o", binary])
    results = {name: command([binary, name]) for name in CASES}
    expected = {"boot": ("size=640x480", "modes=3"), "wide": ("size=1920x1080", "presets=4,4,4"),
                "saved": ("size=1024x768",), "depth": ("depth=77",), "fallback": ("creates=2",),
                "reset": ("events=release-progress,reset,init,",), "close": ("quits=1", "events=quit,"),
                "minimize": ("minimized=0", "active=1"), "pause": ("nested-ready=0", "events=timer-stop,timer-start,"),
                "resize": ("size=900x700", "events=timer-start,invalidate,reset,restore,"),
                "cleanup": ("active=0 ready=0", "events=release,device-release,d3d-release,final,")}
    expected.update({"wide-only-off": ("result=-2113929213 adapters=0",), "no-modes": ("result=-2113929213 adapters=0",),
                     "wide-only-on": ("size=1920x1080", "modes=1"), "mixed": ("presets=0,1,2",),
                     "saved-msaa": ("samples=4",), "preset-index": ("selected=2", "size=1024x768"),
                     "depth16-fallback": ("depth=80",), "depth-stencil": ("depth=75",), "depth32": ("depth=71",),
                     "vp-fallback": ("flags=128",), "msaa-fallback": ("creates=2", "samples=0"), "lost-once": ("creates=2",),
                     "reset-retry": ("events=release-progress,reset,device-release,create,init,",),
                     "query-reset": ("events=release,query-release,reset,query-create,init,",),
                     "paused-reset": ("events=release,reset,init,timer-start,timer-stop,",),
                     "picker": ("result=-2147467263", "events=\n")})
    for name, checks in expected.items():
        for check in checks:
            if check not in results[name]: raise AssertionError(f"{name}: expected {check!r}\n{results[name]}")
    print(f"public host: {len(CASES)} synthetic scenarios passed")
    for name in OWNERSHIP:
        output = command([binary, name])
        if "device-refs=0 query-refs=0 factory-refs=0 owned-output-writes=0" not in output:
            raise AssertionError(f"{name}: unbalanced ownership\n{output}")
        order = {"owned-reset-ref": "events=release-progress,reset,device-release,create,init,release,device-release,d3d-release,final,",
                 "owned-query-reset-ref": "events=release-progress,query-release,reset,device-release,create,query-create,init,release,query-release,device-release,d3d-release,final,",
                 "owned-reset-ref-fail": "events=release-progress,reset,device-release,create,device-release,d3d-release,final,",
                 "owned-failed-create": "events=create,device-release,create,init,timer-start,release,device-release,d3d-release,final,"}
        if order[name] not in output: raise AssertionError(f"{name}: wrong release/callback order\n{output}")
        if name == "owned-reset-ref-fail" and "failed-ready=0" not in output:
            raise AssertionError(f"{name}: failed switch left rendering admitted\n{output}")
    for name in MAPPING:
        output = command([binary, name])
        ordinal = 0 if name == "physical-primary" else 1
        if f"result=0 logical=0 creates={ordinal}," not in output or "invalid-map=19" not in output:
            raise AssertionError(f"{name}: wrong physical adapter\n{output}")
        if name == "physical-recovery" and "recovery=1" not in output:
            raise AssertionError(f"{name}: actual recovery caller used the logical ordinal\n{output}")
    print("ownership/mapping: 7 counted-reference and actual recovery-caller controls passed")
    windowed = {name: command([binary, 'window-control', name]) for name in WINDOWED}
    print(f"windowed: {len(WINDOWED)} independent desktop/fullscreen, real options/menu, transition and bounds controls passed")
    publication = {name: command([binary, 'publication-control', name]) for name in PUBLICATION}
    print(f"publication: {len(PUBLICATION)} actual Flip/key-writer failure and stage-rollback controls passed")
    receipt = {"host_sha256": hashlib.sha256((PUBLIC / 'd3dapp.cpp').read_bytes()).hexdigest(),
               "driver_sha256": hashlib.sha256((SUPPORT / 'public_d3dapp_driver.cpp').read_bytes()).hexdigest(),
               "caller_sha256": hashlib.sha256((scratch / 'recovery/public_d3dapp_callers.h').read_bytes()).hexdigest(),
               "windowed": windowed, "publication": publication,
               "flip_sha256": hashlib.sha256((scratch / 'recovery/public_d3dapp_flip.h').read_bytes()).hexdigest(),
               "limits": "Actual production host/caller snippets; synthetic D3D/Win32. No game, GPU, native Windows or monitor claim."}
    if args.publication_before_ref:
        before = scratch / 'publication-before'
        recovery_source(before, True)
        source = before / 'd3dapp.cpp'
        source.write_text(command(['git', '-C', ROOT, 'show', f'{args.publication_before_ref}:port/platform/public/d3dapp.cpp']))
        negative = before / 'driver'
        command(common + ['-I', before, source, SUPPORT / 'public_d3dapp_driver.cpp', '-o', negative])
        receipt['publication_before_ref'] = args.publication_before_ref
        receipt['publication_negatives'] = {}
        for name in PUBLICATION:
            proc = subprocess.run([str(negative), 'publication-control', name], text=True, capture_output=True, timeout=10)
            if proc.returncode != 1 or 'FAIL: failed change preserves last successful client key' not in proc.stdout:
                raise AssertionError(f'{name}: exact failed-publication negative missing\n{proc.stdout}{proc.stderr}')
            receipt['publication_negatives'][name] = proc.stdout
        print(f'frozen {args.publication_before_ref}: all {len(PUBLICATION)} failed-publication/key negatives reproduced')
    if args.window_before_ref:
        before = scratch / 'window-before'
        recovery_source(before, True)
        source = before / 'd3dapp.cpp'
        source.write_text(command(['git', '-C', ROOT, 'show', f'{args.window_before_ref}:port/platform/public/d3dapp.cpp']))
        negative = before / 'driver'
        command(common + ['-I', before, source, SUPPORT / 'public_d3dapp_driver.cpp', '-o', negative])
        receipt['window_before_ref'] = args.window_before_ref
        receipt['negatives'] = {}
        for name in ('desktop-only', 'desktop-secondary', 'window-rejected', 'mode-transitions', 'window-resize', 'msaa-colour-rejected', 'msaa-depth-rejected', 'resize-shape'):
            proc = subprocess.run([str(negative), 'window-control', name], text=True, capture_output=True, timeout=10)
            if proc.returncode != 1 or 'FAIL:' not in proc.stdout:
                raise AssertionError(f'{name}: expected bounded old negative\n{proc.stdout}{proc.stderr}')
            if name == 'resize-shape' and 'FAIL: resize refreshes faithful projection compensation' not in proc.stdout:
                raise AssertionError(f'exact old aspect negative missing\n{proc.stdout}')
            if name.startswith('msaa-') and 'FAIL: published MSAA bits use both windowed colour and depth' not in proc.stdout:
                raise AssertionError(f'exact old MSAA domain negative missing\n{proc.stdout}')
            receipt['negatives'][name] = proc.stdout
        print(f"frozen {args.window_before_ref}: all 8 windowed admission/caller/projection negatives reproduced")
    (scratch / 'windowed-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if args.baseline_source:
        source = baseline_source(args.baseline_source.resolve(), scratch)
        legacy = scratch / "baseline-driver"
        recovery_source(scratch / "baseline-recovery", False)
        command(common + ["-DPUBLIC_GRAPHICS_BEFORE=1", "-I", scratch / "baseline-recovery", source, SUPPORT / "public_d3dapp_driver.cpp", "-o", legacy])
        for name in CASES:
            if name == "picker": continue # deliberately unsupported; the selected callers never request it
            old = command([legacy, name])
            current = results[name]
            if name == "reset-retry": current = current.replace("reset,device-release,create,", "reset,create,")
            if name == "resize":
                if 'aspect=1\n' not in old or 'aspect=1.03704\n' not in current:
                    raise AssertionError(f'expected exact resize projection correction\nOLD {old}\nNEW {current}')
                current = current.replace('aspect=1.03704\n', 'aspect=1\n')
            if old != current: raise AssertionError(f"{name}: caller-visible baseline difference\nOLD {old}\nNEW {results[name]}")
        digest = hashlib.sha256((args.baseline_source / "src/d3dapp.cpp").read_bytes()).hexdigest()
        print(f"baseline: {len(CASES)-3} complete caller observations equal; reset-retry releases old device; resize corrects aspect 1 to 1.03704 with all other output equal; selected source SHA-256 {digest}")
    if args.before_ref:
        before = scratch / "before"
        recovery_source(before, False)
        source = before / "d3dapp-before.cpp"
        source.write_text(command(["git", "-C", ROOT, "show", f"{args.before_ref}:port/platform/public/d3dapp.cpp"]))
        negative = before / "driver"
        command(common + ["-DPUBLIC_GRAPHICS_BEFORE=1", "-I", before, source, SUPPORT / "public_d3dapp_driver.cpp", "-o", negative])
        for name in OWNERSHIP:
            output = command([negative, name])
            if "device-refs=1" not in output: raise AssertionError(f"{name}: frozen ownership negative did not fail\n{output}")
        output = command([negative, "physical-secondary"])
        if "result=0" in output or "creates=0," not in output:
            raise AssertionError(f"frozen physical startup negative did not fail\n{output}")
        output = command([negative, "physical-recovery"])
        if "recovery=0" not in output: raise AssertionError(f"frozen actual recovery negative did not fail\n{output}")
        print(f"frozen {args.before_ref}: all 6 ownership/startup/recovery negatives reproduced")
    if args.syntax_targets: target_syntax(scratch)


if __name__ == "__main__": main()
