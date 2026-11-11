#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production editor lifecycle and source-bound hooks on authored engine boundaries.

The fixture executes the exact entry, world observation, park, input ownership,
teardown and return functions, the exact draw-translation query and the exact text the
patched actor render-position getter calls it with. Engine objects and storage are authored stand-ins.
Actual world rendering/culling and usable pointer interaction need native runs.
"""
from __future__ import annotations
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

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port'))
import build
import quiet_crash


def definition(text, name):
    pattern = r'(?m)^[A-Za-z_][^\n;{}]*\b' + re.escape(name) + r'\s*\('
    match = re.search(pattern, text)
    if not match:
        raise ValueError('Missing definition: ' + name)
    first = text.index('{', match.end())
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)
    end = first + 1
    depth = 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return text[match.start():end]


MASK = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def mirror_guarded(body, call):
    """True when CALL is made once in BODY, inside the braced block of an `if (!gMirrorPass)`: drawn in an ordinary pass and
    never into the mirrored pass's reflection texture, like every other overlay at the end of CDXEngine::PostRender."""
    masked = MASK.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), body)
    if masked.count(call) != 1:
        return False
    k, depth = masked.index(call), 0
    while k > 0:
        k -= 1
        if masked[k] == '}':
            depth += 1
        elif masked[k] == '{':
            if not depth:
                break
            depth -= 1
    return re.search(r'\bif\s*\(\s*!\s*gMirrorPass\s*\)\s*$', masked[:k]) is not None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    # The driver may crash on purpose in a control: it runs through quiet_crash (no core file and no coredumpctl record).
    parent = ROOT / 'local-data/native-editor-lifecycle-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    source_path = ROOT / 'port/game/ce_editor.cpp'
    source = source_path.read_text()
    start = source.index('struct EditorGameAccess : CGame')
    end = source.index('bool EditorTrap(WORD key, int event);', start) + len('bool EditorTrap(WORD key, int event);')
    declarations = source[start:end]
    names = ('SourceName', 'EditorNote', 'Point', 'Vector', 'NativePosition', 'LiveActor', 'ApplyOne', 'EditorExit',
             'PortCeEditorActive', 'PortCeEditorWorldViewToken', 'PortCeEditorMessage', 'PortCeEditorMousePixels',
             'PortCeEditorSuppressFlightHud', 'PortCeEditorDrawPosition', 'PortCeEditorBegin', 'PortCeEditorBeginWorld',
             'PortCeEditorObserveWorldObject', 'PortCeEditorEndWorld', 'PortCeEditorAfterLoad', 'PortCeEditorFrame',
             'PortCeEditorLevelEnd', 'PortCeEditorCancel', 'PortCeEditorTakeReturn', 'PortCeEditorMenuReturn')
    functions = '\n'.join(definition(source, name) for name in names)
    texts = build.patched_text(('game.cpp', 'FrontEnd.cpp', 'actor.cpp', 'DXEngine.cpp'))
    # The same early guard the production game compiles. No generated game body is published.
    pre_run = definition(texts['game.cpp'], 'CGame::PreRun')
    entry = pre_run[pre_run.index('{') + 1:]
    guard = 'if (PortCeEditorActive()) return;'
    # The guard may share its block with other CE tools, but only as extra OR-alternatives: an '&&' or any other term could
    # disable it, so nothing but '|| Name()' is accepted after the editor test.
    guard_re = re.escape('if (PortCeEditorActive()') + r'(?:\s*\|\|\s*[A-Za-z_][A-Za-z_0-9]*\(\))*\) return;'
    if not re.match(r'\s*#ifdef CE_VERSION\s*' + guard_re, entry):
        raise ValueError('Editor warmup fence is absent or after work.')
    for name, required in (('CGame::MainLoop', 'PortCeEditorFrame();'),
                           ('CGame::ShutdownRestartLoop', 'PortCeEditorLevelEnd();'),
                           ('CGame::RunIntroFMV', guard), ('CGame::RunOutroFMV', guard)):
        body = definition(texts['game.cpp'], name)
        pat = guard_re if required == guard else re.escape(required)
        if not re.match(r'\s*#ifdef CE_VERSION\s*' + pat, body[body.index('{') + 1:]):
            raise ValueError('Editor lifecycle hook is absent or late: ' + name)
    post = definition(texts['game.cpp'], 'CGame::PostLoadProcess')
    # The parking check is followed only by further early FALSE returns, the end of the CE block and the single success return.
    if not re.search(r'if \(PortCeEditorActive\(\) && !PortCeEditorAfterLoad\(\)\) return FALSE;(\s*if \([^;{}]*\) return FALSE;)*\s*#endif\s*return TRUE;', post):
        raise ValueError('Successful world loading does not require editor parking.')
    frontend = definition(texts['FrontEnd.cpp'], 'CFrontEnd::Init')
    if frontend.count('PortCeEditorTakeReturn()') != 1 or frontend.index('PortCeEditorTakeReturn()') > frontend.index('mGoStraightToDeviceSelectScreen'):
        raise ValueError('Tool return is absent or comes after ordinary ending/save routing.')
    automatic = definition(texts['game.cpp'], 'CGame::RestartLoopRunLevel')
    if automatic.count('CONSOLE.Exec(') != 2 or automatic.count('(ceEditorAuthoring ? (void)0 : CONSOLE.Exec(') != 2:
        raise ValueError('An automatic startup command can run during authoring.')
    # The actor's render-position getter answers with the current translation for bound placements: one guarded block that is the
    # getter's first statement, composed exactly as the mirror holds it. Every consumer of the getter (the pose renderer, a
    # component placed from its parent) then sees the same translation.
    actor = texts['actor.cpp']
    block = re.compile(r'#ifdef CE_VERSION[ \t]*\n[ \t]*// CE_EDITOR_CURRENT_TRANSLATION_BEGIN\n.*?// CE_EDITOR_CURRENT_TRANSLATION_END\n#endif\n', re.S)
    found = block.findall(actor)
    if len(found) != 1 or actor.count('CE_EDITOR_CURRENT_TRANSLATION_BEGIN') != 1 or actor.count('CE_EDITOR_CURRENT_TRANSLATION_END') != 1:
        raise ValueError('The draw translation block is absent or repeated.')
    getter = definition(actor, 'CActor::GetRenderPos')
    if getter[getter.index('{') + 1:].lstrip().find(found[0]) != 0:
        raise ValueError('The draw translation is not the first statement of the actor render-position getter.')
    if actor.count('#ifdef CE_VERSION\n#include "ce_editor.h"\n#endif') != 1:
        raise ValueError('The actor source does not include the editor interface under the CE guard.')
    # The overlay draws over the finished picture of an ordinary pass only. Patch 0206 once added its call after the commander
    # map's unbraced `if (!gMirrorPass)`, indented as if guarded, so it also drew into the mirrored pass; the call now shares
    # the condition's braced body. The two controls are that old shape and a call after the block.
    post_render = definition(texts['DXEngine.cpp'], 'CDXEngine::PostRender')
    overlay = 'PortCeEditorRender();'
    if not mirror_guarded(post_render, overlay):
        raise ValueError('The editor overlay is drawn outside the mirror-pass condition.')
    braced = re.search(r'if \(!gMirrorPass\)\n\t\{\n(\t\tPortCeCmdRender\(\);[^\n]*\n)\t\tPortCeEditorRender\(\);\n\t\}\n', post_render)
    if not braced:
        raise ValueError('Overlay control anchor changed.')
    unbraced = post_render.replace(braced[0], 'if (!gMirrorPass)\n' + braced[1] + '\t\tPortCeEditorRender();\n', 1)
    after = post_render.replace(braced[0], 'if (!gMirrorPass)\n\t{\n' + braced[1] + '\t}\n\tPortCeEditorRender();\n', 1)
    if mirror_guarded(unbraced, overlay) or mirror_guarded(after, overlay):
        raise ValueError('The mirror-pass overlay check accepts an unguarded call.')
    translation = found[0]
    (out / 'editor-render-translation.private.inc').write_text(translation)
    (out / 'editor-declarations.private.inc').write_text(declarations + '\n')
    (out / 'editor-lifecycle.private.inc').write_text(functions + '\n')
    (out / 'editor-prerun.private.inc').write_text(guard + '\n')
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++ compiler is required.')
    driver = ROOT / 'port/tools/tests/ce_editor_lifecycle_driver.cpp'
    project = ROOT / 'port/game/ce_project.cpp'
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'ONSLAUGHT_'))}

    def compile_run(folder, project_source, snippet_ce=True):
        # snippet_ce: the driver defines CE_VERSION around the renderer text only (the editor source itself stays unguarded).
        binary = folder / 'lifecycle-check'
        command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-Wno-unused-function',
                   *(['-DDRIVER_CE_SNIPPET'] if snippet_ce else []),
                   '-I' + str(folder), '-I' + str(ROOT / 'port/game'), str(driver), str(source_path), str(project_source), '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, timeout=90, check=False)
        (folder / 'compile.stdout').write_bytes(built.stdout)
        (folder / 'compile.stderr').write_bytes(built.stderr)
        if built.returncode:
            return {'compiled': False, 'exit': None}
        ran = quiet_crash.run([str(binary)], env=environment, capture_output=True, timeout=10, check=False)
        (folder / 'run.stdout').write_bytes(ran.stdout)
        (folder / 'run.stderr').write_bytes(ran.stderr)
        project_environment = dict(environment, BEA_CE_PROJECT='authored-startup-intent.ceproject')
        project_run = quiet_crash.run([str(binary), 'project-start'], env=project_environment,
                                      capture_output=True, timeout=10, check=False)
        (folder / 'project-start.stdout').write_bytes(project_run.stdout)
        (folder / 'project-start.stderr').write_bytes(project_run.stderr)
        return {'compiled': True, 'exit': ran.returncode, 'startup_project_exit': project_run.returncode}

    baseline = compile_run(out, project)
    ordinary_folder = out / 'default-text'
    ordinary_folder.mkdir()
    for path in out.glob('*.inc'):
        shutil.copyfile(path, ordinary_folder / path.name)
    ordinary = compile_run(ordinary_folder, project, snippet_ce=False)
    query_gate = 'if (!thing || !position || !gEditorRequested || !PortCeEditorWorldViewToken() || !GAME.IsPaused()) return false;'
    variants = {
        'warmup-unguarded': ('editor-prerun.private.inc', guard, '(void)0;'),
        'career-snapshot-omitted': ('editor-lifecycle.private.inc', 'memcpy(gEditorCareer, &CAREER, sizeof(CCareer));', '(void)0;'),
        'career-restoration-omitted': ('editor-lifecycle.private.inc', 'if (gEditorCareer) memcpy(&CAREER, gEditorCareer, sizeof(CCareer));', '(void)0;'),
        'camera-restores-over-new-owner': ('editor-lifecycle.private.inc', 'gEditorCamera && GAME.GetCurrentCamera(0) == gEditorCamera && gEditorPreviousCamera', 'gEditorPreviousCamera && GAME.GetCurrentCamera(0) != gEditorPreviousCamera'),
        'teardown-releases-career-too-soon': ('editor-lifecycle.private.inc', 'gEditorReady = false;', 'gEditorRequested=false;gEditorReady = false;'),
        'init-offset-lost': ('editor-lifecycle.private.inc', 'actor.x + object->position.x - initial.x', 'object->position.x'),
        # The draw correction. The first two are the behaviour before it: the actor's ordinary render position for a moved actor.
        'draw-hook-disabled': ('editor-render-translation.private.inc', 'PortCeEditorDrawPosition(this, &ceEditorPosition)', 'false'),
        'draw-hook-result-ignored': ('editor-render-translation.private.inc', 'return ceEditorPosition;', '(void)ceEditorPosition;'),
        'draw-query-returns-wrong-translation': ('editor-lifecycle.private.inc', '*position = actor->GetPos();', '*position = FVector();'),
        'draw-ignores-pause': ('editor-lifecycle.private.inc', '|| !GAME.IsPaused())', ')'),
        'draw-ignores-session': ('editor-lifecycle.private.inc', '!gEditorRequested || !PortCeEditorWorldViewToken()', '!PortCeEditorWorldViewToken()'),
        'draw-ignores-camera-owner': ('editor-lifecycle.private.inc', '!PortCeEditorWorldViewToken() || !GAME.IsPaused()', '!GAME.IsPaused()'),
        'draw-compares-raw-pointers': ('editor-lifecycle.private.inc', 'static_cast<const IRenderableThing *>(gEditorActors[i]) != thing',
                                       'static_cast<const void *>(gEditorActors[i]) != static_cast<const void *>(thing)'),
        'draw-matches-any-thing': ('editor-lifecycle.private.inc', 'if (static_cast<const IRenderableThing *>(gEditorActors[i]) != thing) continue;', ''),
        'draw-skips-liveness': ('editor-lifecycle.private.inc', 'CThing *actor = LiveActor(i);', 'CThing *actor = gEditorActors[i];'),
        'draw-skips-validity': ('editor-lifecycle.private.inc', 'if (!actor || !CeEditorState::Valid(Point(actor->GetPos()))) return false;',
                                'if (!actor) return false;'),
        'draw-writes-actor-state': ('editor-lifecycle.private.inc', '*position = actor->GetPos();',
                                    'actor->MoveTo(actor->GetPos()); *position = actor->GetPos();'),
        # The answer does not depend on the editor's own state: a move in progress and a saved session draw at the current translation too.
        'draw-off-during-move': ('editor-lifecycle.private.inc', query_gate, query_gate + ' if (gEditorState.Moving()) return false;'),
        'draw-off-in-saved-session': ('editor-lifecycle.private.inc', query_gate, query_gate + ' if (!gEditorState.Dirty()) return false;'),
    }
    default_text_variants = {
        # The default build's text must never consult the editor: lifting the guard must be caught by the ordinary compile.
        'default-text-unguarded': ('editor-render-translation.private.inc', '#ifdef CE_VERSION', '#if 1'),
    }
    project_text = project.read_text()
    for symbol in ('PortCeNetActive', 'CE_BootRequested', 'CE_SessionActive', 'CE_LockstepRequested', 'PortModsActive'):
        variants['ignore-' + symbol] = ('ce_project.cpp', '(' + symbol + ' && ' + symbol + '())', 'false')
    controls = {}
    for snippet_ce, group in ((True, variants), (False, default_text_variants)):
        for name, (filename, before, after) in group.items():
            folder = out / name
            folder.mkdir()
            for path in out.glob('*.inc'):
                shutil.copyfile(path, folder / path.name)
            candidate = folder / filename
            text = project_text if filename == 'ce_project.cpp' else candidate.read_text()
            if text.count(before) != 1:
                raise ValueError('Lifecycle mutation anchor changed: ' + name)
            candidate.write_text(text.replace(before, after, 1))
            result = compile_run(folder, candidate if filename == 'ce_project.cpp' else project, snippet_ce)
            controls[name] = result | {'rejected': result['compiled'] and result['exit'] == 1 and result.get('startup_project_exit') == 0}
    passed = baseline['compiled'] and baseline['exit'] == 0 and baseline.get('startup_project_exit') == 0 and \
        ordinary['compiled'] and ordinary['exit'] == 0 and ordinary.get('startup_project_exit') == 0 and \
        all(v['rejected'] for v in controls.values())
    inputs = [source_path, ROOT / 'port/game/ce_editor.h', project, driver, Path(__file__).resolve(),
              ROOT / 'port/patches/0206-native-editor-lifecycle.patch']
    receipt = {'passed': passed, 'scope': __doc__, 'baseline': baseline, 'default_text': ordinary, 'causal_controls': controls,
               'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
               'resolved_hooks': {name: hashlib.sha256(value.encode('latin1')).hexdigest() for name, value in texts.items()}}
    (out / 'RESULT.private.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('native editor lifecycle:', 'PASS' if passed else 'FAIL', '|', out)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
