#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Current editor draft -> playable preview -> paused editor on authored boundaries.

Executes the production editor key/toolbar, explicit save, preview, world placement, restart,
career exclusion and return owners. Engine, disk and actors are authored stand-ins;
native rendering and physical input acceptance need separate isolated runs.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile

from ce_editor_lifecycle_tests import definition

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port'))
import build


def default_jobs() -> int:
    env = os.environ.get('BEA_MUTANT_JOBS', '').strip()
    if env:
        value = int(env)
        if value < 1:
            raise ValueError(f'BEA_MUTANT_JOBS must be >= 1, got {value}')
        return value
    return min(6, os.cpu_count() or 1)


def fixture(out: Path):
    source_path = ROOT / 'port/game/ce_editor.cpp'
    project_path = ROOT / 'port/game/ce_project.cpp'
    source = source_path.read_text()
    start = source.index('struct EditorGameAccess : CGame')
    end = source.index('bool EditorTrap(WORD key, int event);', start) + len('bool EditorTrap(WORD key, int event);')
    (out / 'editor-declarations.private.inc').write_text(source[start:end] + '\n')
    names = (
        'SamePoint', 'EditorBounds', 'EditorFit', 'EditorStartView', 'SourceName', 'EditorNote', 'EditorNoticeSettle', 'EditorToolEnabled', 'EditorScrollFiles', 'Point', 'Vector', 'EditorStandAbove', 'EditorStartClearance', 'EditorPlayerStartFit', 'EditorPlayerStartSupported', 'EditorStartRefusal', 'NativePosition', 'LiveActor', 'ApplyOne', 'EditorStartStandsOn', 'EditorBeginMove',
        'EditorRefuse', 'EditorExit', 'EditorLeave', 'EditorParkRefusal', 'EditorCopyCareerOptions', 'EditorRestoreCareer', 'EditorPrepareReload', 'EditorReturnToAuthoring', 'EditorUpdateRevision', 'EditorSave', 'EditorApplyDocument',
        'EditorOpenFile', 'EditorStartPlaytest', 'EditorOwnSerial', 'EditorFileBefore', 'EditorBrowse', 'EditorProjectOpens', 'EditorFileGrey', 'EditorRequestOpen', 'EditorAnswer', 'EditorUndo',
        'EditorRouteChoices', 'EditorPlaceStop', 'EditorChooseRoute', 'EditorHoldMenuRows', 'EditorHoldInspect', 'EditorOpenHold', 'EditorHoldChoice', 'EditorToolbar', 'EditorTrap', 'EditorGameScreen', 'PortCeEditorActive', 'PortCeEditorFilterSave', 'PortCeEditorWorldViewToken', 'PortCeEditorMessage',
        'PortCeEditorPlaying', 'PortCeEditorMousePixels', 'PortCeEditorSuppressFlightHud', 'PortCeEditorResolveQuit',
        'PortCeEditorDrawPosition', 'PortCeEditorBegin', 'PortCeEditorBeginWorld', 'PortCeEditorApplyWorldObject',
        'PortCeEditorApplyPlayerStart', 'PortCeEditorObserveWorldObject', 'PortCeEditorEndWorld', 'PortCeEditorAfterLoad', 'EditorReachEntry', 'EditorUpdateReach', 'PortCeEditorFrame',
        'PortCeEditorKey', 'PortCeEditorLevelEnd', 'PortCeEditorCancel', 'PortCeEditorTakeReturn', 'PortCeEditorMenuReturn',
    )
    functions = [definition(source, name) for name in names]
    functions.sort(key=source.index)
    (out / 'editor-lifecycle.private.inc').write_text('\n'.join(functions) + '\n')
    texts = build.patched_text(('game.cpp', 'ltshell.cpp', 'actor.cpp'))
    pre_run = definition(texts['game.cpp'], 'CGame::PreRun')
    guard = 'if (PortCeEditorActive() && !PortCeEditorPlaying()) return;'
    if guard not in pre_run:
        raise ValueError('The play-test warmup path is absent from production.')
    automatic = definition(texts['game.cpp'], 'CGame::RestartLoopRunLevel')
    hook = 'mQuit = static_cast<EQuitType>(PortCeEditorResolveQuit(mQuit));'
    if automatic.count(hook) != 1 or automatic.rindex(hook) > automatic.rindex('return mQuit'):
        raise ValueError('The restart result is not resolved before returning to the level loop.')
    # Only the owned CE block is extracted; engine bodies stay in the private mirror.
    (out / 'editor-prerun.private.inc').write_text(guard + '\n')
    actor = texts['actor.cpp']
    a = actor.index('#ifdef CE_VERSION\n\t// CE_EDITOR_CURRENT_TRANSLATION_BEGIN')
    b = actor.index('#endif', a) + len('#endif')
    (out / 'editor-render-translation.private.inc').write_text(actor[a:b] + '\n')
    base = (ROOT / 'port/tools/tests/ce_editor_lifecycle_driver.cpp').read_text()
    engine, rest = base.split('bool EditorTrap(WORD,int){return true;}\n', 1)
    common = rest.split('#include "editor-lifecycle.private.inc"\n', 1)[1].split('int main(', 1)[0]
    (out / 'editor-playtest-engine.private.inc').write_text(engine)
    (out / 'editor-playtest-common.private.inc').write_text(common)
    return source_path, project_path, guard


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--causal-controls', action='store_true')
    parser.add_argument('-j', '--jobs', type=int, default=None, help='max parallel compiles/runs (default: BEA_MUTANT_JOBS or min(6, cpu_count))')
    args = parser.parse_args()
    jobs = default_jobs() if args.jobs is None else args.jobs
    if jobs < 1:
        raise SystemExit(f'--jobs must be >= 1, got {jobs}')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/native-editor-playtest-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    source_path, project_path, guard = fixture(out)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++ compiler is required.')
    driver = ROOT / 'port/tools/tests/ce_editor_playtest_driver.cpp'
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'ONSLAUGHT_'))}
    base_flags = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                  '-Wno-unused-function', '-DDRIVER_CE_SNIPPET', '-DDRIVER_EDITOR_VIEW']
    shared_dir = out / 'shared-objs'
    shared_dir.mkdir(exist_ok=True)
    editor_obj = shared_dir / 'ce_editor.o'
    project_obj = shared_dir / 'ce_project.o'
    modes_obj = shared_dir / 'ce_modes.o'
    driver_obj = shared_dir / 'driver.o'

    def compile_obj(src: Path, obj: Path, inc_dir: Path | None = None) -> subprocess.CompletedProcess:
        cmd = [*base_flags, *(['-I' + str(inc_dir)] if inc_dir else []), '-I' + str(ROOT / 'port/game'), '-c', str(src), '-o', str(obj)]
        return subprocess.run(cmd, capture_output=True, timeout=90, check=False)

    def link_and_run(folder: Path, inputs: list[str], inc_dir: Path | None = None, link_only: bool = False) -> dict:
        binary = folder / 'playtest-check'
        if link_only:
            command = [compiler, *inputs, '-o', str(binary)]
        else:
            command = [*base_flags, *(['-I' + str(inc_dir)] if inc_dir else []), '-I' + str(ROOT / 'port/game'), *inputs, '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, timeout=90, check=False)
        (folder / 'compile.stdout').write_bytes(built.stdout)
        (folder / 'compile.stderr').write_bytes(built.stderr)
        if built.returncode:
            return {'compiled': False, 'exit': None}
        result = subprocess.run([str(binary)], env=environment, capture_output=True, timeout=30, check=False)
        (folder / 'run.stdout').write_bytes(result.stdout)
        (folder / 'run.stderr').write_bytes(result.stderr)
        return {'compiled': True, 'exit': result.returncode}

    shared_tasks = [
        (source_path, editor_obj, None),
        (project_path, project_obj, None),
        (ROOT / 'port/game/ce_modes.cpp', modes_obj, None),
        (driver, driver_obj, out),
    ]
    if jobs == 1:
        shared_built = [compile_obj(*task) for task in shared_tasks]
    else:
        with ThreadPoolExecutor(max_workers=min(len(shared_tasks), jobs)) as pool:
            shared_built = list(pool.map(lambda t: compile_obj(*t), shared_tasks))
    if any(r.returncode for r in shared_built):
        (out / 'compile.stdout').write_bytes(b''.join(r.stdout for r in shared_built))
        (out / 'compile.stderr').write_bytes(b''.join(r.stderr for r in shared_built))
        baseline = {'compiled': False, 'exit': None}
    else:
        baseline = link_and_run(out, [str(driver_obj), str(editor_obj), str(project_obj), str(modes_obj)], link_only=True)
    controls = {}
    if args.causal_controls and baseline == {'compiled': True, 'exit': 0}:
        variants = {
            'preview-writes-project': ('editor-lifecycle.private.inc', '    CeProjectDocument *document = nullptr;\n    if (!EditorUpdateRevision', '    if (!EditorSave()) return false;\n    CeProjectDocument *document = nullptr;\n    if (!EditorUpdateRevision'),
            'preview-omits-draft': ('editor-lifecycle.private.inc', 'if (!EditorUpdateRevision(document, error, sizeof(error))) {', 'if (true) {'),
            'reset-escapes-to-editor': ('editor-lifecycle.private.inc', 'if (!repeat) GAME.SetQuit(QT_RESTART_LEVEL);', 'if (!repeat) EditorReturnToAuthoring();'),
            'return-replaces-named-document': ('editor-lifecycle.private.inc', 'if (!EditorApplyDocument(document, nullptr)) {', 'if (!EditorApplyDocument(document, gEditorFilename)) {'),
            'reset-repeats-while-held': ('editor-lifecycle.private.inc', 'if (!repeat) GAME.SetQuit(QT_RESTART_LEVEL);', 'GAME.SetQuit(QT_RESTART_LEVEL);'),
            'unchanged-save-writes-copy': ('editor-lifecycle.private.inc', 'if (gEditorFileCurrent && !gEditorState.Dirty()) {', 'if (false) {'),
            'refusal-fails-engine-load': ('editor-lifecycle.private.inc', 'gEditorPhase = Refused;\n    }\n    return true;', 'gEditorPhase = Refused;\n    }\n    return false;'),
            'load-during-play-accepted': ('editor-lifecycle.private.inc', '    if (gEditorPhase == Playing)\n        return EditorRefuse(', '    if (false)\n        return EditorRefuse('),
            'undo-refusal-silent': ('editor-lifecycle.private.inc', '    } else {\n        EditorCue(CueBack);\n        EditorNote(redo ?', '    } else {\n        EditorNote(redo ?'),
            'leave-question-on-yes': ('editor-lifecycle.private.inc', '            gEditorDialog = DialogDiscardExit;\n            gEditorButtonFocus = 0;', '            gEditorDialog = DialogDiscardExit;\n            gEditorButtonFocus = 1;'),
            'esc-answers-yes': ('editor-lifecycle.private.inc', '        if (key == DIK_ESCAPE)\n            EditorAnswer(false);', '        if (key == DIK_ESCAPE)\n            EditorAnswer(true);'),
            'tool-focus-silent': ('editor-lifecycle.private.inc', '        gEditorToolFocus = (gEditorToolFocus + (key == DIK_DOWN ? 1 : 9)) % 10;\n        EditorCue(CueMove);', '        gEditorToolFocus = (gEditorToolFocus + (key == DIK_DOWN ? 1 : 9)) % 10;'),
            'refusal-outlives-selection': ('editor-lifecycle.private.inc', 'if (covered || moved || reselected) {', 'if (covered || moved) {'),
            'move-notice-outlives-move': ('editor-lifecycle.private.inc', 'if (covered || moved || reselected) {', 'if (covered || reselected) {'),
            'notice-under-list': ('editor-lifecycle.private.inc', 'if (covered || moved || reselected) {', 'if (moved || reselected) {'),
            'list-oldest-first': ('editor-lifecycle.private.inc', 'return sa > sb;', 'return sa < sb && sa > 0;'),
            'list-in-folder-order': ('editor-lifecycle.private.inc', 'return sa > sb;', 'return false;'),
            'wheel-ignored-in-list': ('editor-lifecycle.private.inc', '            EditorScrollFiles(key == 3 ? -1 : 1);\n            return true;', '            return true;'),
            'asks-before-reading': ('editor-lifecycle.private.inc', 'if (!EditorProjectOpens(gEditorFileIndex)) {', 'if (false) {'),
            'window-close-returns-to-editor': ('editor-lifecycle.private.inc', 'quit == QT_QUIT_TO_FRONTEND && FRONTEND.mQuit == -1', 'false'),
            'leave-keeps-stale-note': ('editor-lifecycle.private.inc', '    gEditorNote[0] = 0;\n    EditorExit();', '    EditorExit();'),
            'unsaved-scene-played': ('project.cpp', '*x = placement.x; *y = placement.y; *z = placement.z;', '(void)0;'),
            'missing-binding-accepted': ('project.cpp', 'if (!placement.seen)', 'if (false && !placement.seen)'),
            'duplicate-binding-accepted': ('project.cpp', 'if (placement.seen)', 'if (false && placement.seen)'),
            'return-skips-reload': ('editor-lifecycle.private.inc', 'if (!EditorPrepareReload(ReturnToAuthoring))', 'if ((gEditorPhase = Authoring, false))'),
            'return-loses-saved-document': ('editor-lifecycle.private.inc', 'if (!EditorApplyDocument(document, nullptr)) {',
                                            'PortCeProjectDocumentDelete(document); if (false) {'),
            'normal-leave-escapes-editor': ('editor-lifecycle.private.inc', 'return EditorReturnToAuthoring() ? QT_RESTART_LEVEL : QT_USER_QUIT_TO_FRONTEND;', 'return quit;'),
            'career-memory-survives-return': ('editor-lifecycle.private.inc', '    EditorRestoreCareer();\n    gEditorPhase = phase;', '    gEditorPhase = phase;'),
            'test-steals-flight-keys': ('editor-lifecycle.private.inc', 'if (key != DIK_F5) return false;', 'if (key != DIK_F5) return true;'),
            'test-skips-warmup': ('editor-prerun.private.inc', guard, 'if (PortCeEditorActive()) return;'),
            'other-level-reaches-question': ('editor-lifecycle.private.inc', 'const bool level = PortCeProjectDocumentLevel(document) == gEditorLevel;', 'const bool level = true;'),
            'open-keeps-far-view': ('editor-lifecycle.private.inc', '    EditorStartView();\n    return true;', '    return true;'),
            'reopen-ignores-moved-objects': ('editor-lifecycle.private.inc', 'const bool moved = !SamePoint(object->position, gEditorSources[i].initial) || object->side >= 0;', 'const bool moved = false;'),
            'reopen-follows-vertical-outlier': ('editor-lifecycle.private.inc', 'if (!SamePoint(p, framed)) continue;', '(void)0;'),
            'reach-completes-from-anywhere': ('editor-lifecycle.private.inc', 'met = EditorReachEntry(from, pos, gEditorTestReach, entry);', 'met = true;'),
            'hold-leaving-keeps-time': ('editor-lifecycle.private.inc', 'if (!inside) gEditorHoldElapsed=0;', 'if (!inside) {}'),
        'hold-ignores-custom-radius': ('editor-lifecycle.private.inc', 'const bool inside=gEditorReachDistance<=radius;', 'const bool inside=gEditorReachDistance<=kEditorReachRadius;'),
        'hold-ignores-custom-duration': ('editor-lifecycle.private.inc', 'met=inside && gEditorHoldElapsed>=gEditorTestReach.hold.seconds;', 'met=inside && gEditorHoldElapsed>=3;'),
        'hold-counts-paused-time': ('editor-lifecycle.private.inc', 'gEditorHoldLastNs=0; // A paused interval', '/* retain prior clock */ // A paused interval'),
        'hold-inspector-forgets-radius': ('editor-lifecycle.private.inc', 'int &v=row==0?h.seconds:h.radius;', 'int &v=h.seconds;'),
        'reach-never-completes': ('editor-lifecycle.private.inc', 'else gEditorReached = true;', 'else gEditorReached = false;'),
            'reach-endpoint-only': ('editor-lifecycle.private.inc', 'met = EditorReachEntry(from, pos, gEditorTestReach, entry);', 'met = gEditorReachDistance <= kEditorReachRadius;'),
            'reach-connects-teleport': ('editor-lifecycle.private.inc', 'gEditorReachPilot == engine && !relocated', 'gEditorReachPilot == engine'),
            'reach-flat-flyover': ('editor-lifecycle.private.inc', 'radius2-(ex*ex+ey*ey+ez*ez)', 'radius2-(ex*ex+ey*ey)'),
            'route-uses-entire-segment-again': ('editor-lifecycle.private.inc', '&& secondEntry > entry', '&& secondEntry >= 0'),
            'route-drops-fast-second-crossing': ('editor-lifecycle.private.inc', 'if (!gEditorTestReach.hold.Enabled() && EditorReachEntry(from, pos, gEditorTestReach, secondEntry) && secondEntry > entry)', 'if (false)'),
            'route-credits-waiting-in-overlap': ('editor-lifecycle.private.inc', 'if (gEditorSecondNeedsExit) {', 'if (false) {'),
            'route-counts-simultaneous-entry': ('editor-lifecycle.private.inc', '&& secondEntry > entry', '&& secondEntry >= entry'),
            'route-ignores-immediate-next-sweep': ('editor-lifecycle.private.inc', '            double secondEntry = 0;', '            gEditorReachPlayerValid = false; double secondEntry = 0;'),
            'route-skip-first': ('editor-lifecycle.private.inc', 'gEditorTestReach=PortCeProjectDocumentReach(gEditorPlayDocument);', 'gEditorTestReach = PortCeProjectDocumentReach(gEditorPlayDocument, PortCeProjectDocumentReach(gEditorPlayDocument,1).enabled ? 1 : 0);'),
            'route-ends-at-first': ('editor-lifecycle.private.inc', 'gEditorReachStage = 1;', 'gEditorReachStage = 1; gEditorReached = true;'),
            'route-target-stays-first': ('editor-lifecycle.private.inc', 'gEditorTestReach = gEditorNextReach;', '(void)gEditorNextReach;'),
            'route-keeps-second-on-remove-first': ('editor-lifecycle.private.inc', 'const int index = action == RemoveFirst ? 0 : 1;', 'const int index = 1;'),
            'reach-lost-on-return': ('editor-lifecycle.private.inc', 'if (!candidate.RestoreReach(targets, targetCount, holdSettings))', 'if (!candidate.RestoreReach(nullptr, 0))'),
            'side-not-applied-before-init': ('project.cpp', 'if (placement.side >= 0) *unitSide = placement.side;', 'if (placement.side >= 0) *unitSide = 0;'),
            'side-lost-on-reopen': ('project.cpp', 'value.x,value.y,value.z,value.side', 'value.x,value.y,value.z,-1'),
            'reopen-uses-first-change': ('editor-lifecycle.private.inc', 'moved == nearestMoved && distance < nearestDistance', 'moved == nearestMoved && !moved && distance < nearestDistance'),
            'reopen-prefers-later-tie': ('editor-lifecycle.private.inc', 'moved == nearestMoved && distance < nearestDistance', 'moved == nearestMoved && distance <= nearestDistance'),
            'reopen-loses-selection': ('editor-lifecycle.private.inc', 'if (nearestMoved) gEditorState.Select(nearest);', '(void)0;'),
            'history-lost-in-play-test': ('editor-lifecycle.private.inc', 'if (!filename && !candidate.KeepHistory(gEditorKept)) {', 'if (false) {'),
            'unreadable-row-white': ('editor-lifecycle.private.inc', 'gEditorFileKinds[index] = !document ? FileUnreadable :', 'gEditorFileKinds[index] = !document ? FileOpens :'),
            'every-row-grey': ('editor-lifecycle.private.inc', 'return gEditorFileKinds[index] != FileOpens;', 'return true;'),
            'career-fence-released-in-test': ('editor-lifecycle.private.inc', 'return gEditorRequested;', 'return gEditorRequested && gEditorPhase != Playing;'),
            'return-keeps-stale-player-start-tool': ('editor-lifecycle.private.inc', '    gEditorChoosingReach=false;\n    gEditorChoosingPlayerStart=false;', '    if (filename) {\n        gEditorChoosingReach=false;\n        gEditorChoosingPlayerStart=false;\n    }'),
            'play-ignores-missing-start-support': ('editor-lifecycle.private.inc', 'if (fit != StartFits) {', 'if (false) {'),
            'covered-start-counts-as-support': ('editor-lifecycle.private.inc', '        over.mDistToImpact < above - 0.5f) return StartCovered;', '        over.mDistToImpact < above - 0.5f) return StartFits;'),
            'reload-ignores-missing-start-support': ('editor-lifecycle.private.inc', 'checked ? EditorPlayerStartFit(used, authored.enabled) : StartFits', 'checked && used.x != used.x && authored.enabled ? StartNoFooting : StartFits'),
            'reload-skips-map-start': ('editor-lifecycle.private.inc', 'authored.enabled || (gEditorNativeStartCount == 1 && !gEditorNativeStartFlying)', 'authored.enabled'),
            'crowded-start-fits': ('editor-lifecycle.private.inc', '                if (side.mHitType != kCollideNothing && side.mHitType != kCollideGround) return StartCrowded;', '                (void)side;'),
            'edge-start-fits': ('editor-lifecycle.private.inc', '    if (placed) {\n        static const float kFeet', '    if (false) {\n        static const float kFeet'),
            'slope-any-steepness': ('editor-lifecycle.private.inc', 'std::fabs(under - surface) > 1.f', 'std::fabs(under - surface) > 100.f'),
            'waterline-hull-is-a-deck': ('editor-lifecycle.private.inc', 'return hitType == kCollideGround ? 0.25f : 1.0f;', 'return hitType == kCollideGround ? 0.25f : -100.f;'),
            'refusal-overwritten-by-welcome': ('editor-lifecycle.private.inc', '    else\n        EditorNote(returning ?', '    EditorNote(returning ?'),
            'late-refusal-forgets-reason': ('editor-lifecycle.private.inc', 'EditorNote(EditorStartRefusal(EditorStartFit(gEditorStartRejectedFit)), NoticeRefusal);', 'EditorNote("Unsafe start.", NoticeRefusal);'),
        }
        project_text = project_path.read_text()
        inc_files = [(p.name, p.read_text()) for p in out.glob('*.inc')]
        prepared = []
        for name, (file, before, after) in variants.items():
            folder = out / name
            folder.mkdir()
            for inc_name, inc_text in inc_files:
                (folder / inc_name).write_text(inc_text)
            target = project_text if file == 'project.cpp' else (folder / file).read_text()
            if target.count(before) != 1:
                raise ValueError('Causal-control anchor changed: ' + name)
            if file == 'project.cpp':
                project = folder / file
                project.write_text(target.replace(before, after, 1))
                prepared.append((name, folder, [str(driver_obj), str(editor_obj), str(project), str(modes_obj)], None))
            else:
                (folder / file).write_text(target.replace(before, after, 1))
                prepared.append((name, folder, [str(driver), str(editor_obj), str(project_obj), str(modes_obj)], folder))

        def eval_variant(item):
            name, folder, inputs, inc_dir = item
            result = link_and_run(folder, inputs, inc_dir=inc_dir)
            return name, dict(result, rejected=result == {'compiled': True, 'exit': 1})

        if jobs == 1:
            for item in prepared:
                name, rec = eval_variant(item)
                controls[name] = rec
        else:
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                for name, rec in pool.map(eval_variant, prepared):
                    controls[name] = rec
    passed = baseline == {'compiled': True, 'exit': 0} and all(v['rejected'] for v in controls.values())
    receipt = {'passed': passed, 'scope': __doc__, 'baseline': baseline, 'causal_controls': controls,
               'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (source_path, source_path.with_suffix('.h'), project_path, project_path.with_suffix('.h'),
                                    driver, Path(__file__).resolve(), ROOT / 'port/patches/0206-native-editor-lifecycle.patch')}}
    (out / 'RESULT.private.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('native editor saved-revision play test:', 'PASS' if passed else 'FAIL', '|', out)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
