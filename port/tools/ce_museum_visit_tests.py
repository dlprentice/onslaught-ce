#!/usr/bin/env python3
"""Authored museum cache/lifecycle and actual owned-reader controls.

No game or device runs. The career in the host fixture is an authored byte
container; separate target syntax checks use the real game headers. Extracted
reader definitions and all fixtures remain in ignored local-data.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port'))
import build


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def definition(text: str, name: str, owner: str = 'CDXMemBuffer') -> str:
    found = re.search(r'^[ \t]*(?:(?:SINT|BOOL|void|EQuitType|CMesh\s*\*)\s*)?' + re.escape(owner) + r'::' + re.escape(name) + r'\s*\([^)]*\)', text, re.M)
    if not found:
        raise ValueError('missing reader method ' + name)
    start = text.index('{', found.end())
    # Comments and strings cannot change the nesting of the selected body.
    token = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for match in token.finditer(text, start):
        if match.group() == '{': depth += 1
        elif match.group() == '}':
            depth -= 1
            if not depth: return text[found.start():match.end()]
    raise ValueError('unclosed reader method ' + name)


def fixture(folder: Path, change: str = '') -> tuple[Path, Path]:
    user, game = folder / 'user', folder / 'game'
    (user / 'museum').mkdir(parents=True)
    (game / 'data/resources').mkdir(parents=True)
    archive = b'authored museum carrier archive fixture'
    base, real = b'authored museum base bytes', b'authored museum exhibit bytes'
    h = lambda value: hashlib.sha256(value).hexdigest()
    text = ('Onslaught CE museum cache 1\n'
            f'carrier 73 {h(archive)}\n'
            f'base 21 {h(base)} {len(base)}\n'
            f'real 73 {h(real)} {len(real)}\n')
    (game / 'data/resources/073_res_PC.aya').write_bytes(archive)
    (game / 'data/resources/100_res_PC.aya').write_bytes(archive)
    (user / 'museum/world021.body').write_bytes(base)
    (user / 'museum/world073.body').write_bytes(real)
    if change == 'missing-cache': return user, game
    if change == 'version': text = text.replace('cache 1', 'cache 2')
    if change == 'duplicate-id': text = text.replace('base 21 ', 'base 73 ')
    if change == 'wrong-carrier': text = text.replace('carrier 73 ', 'carrier 74 ')
    if change == 'invalid-carrier': text = text.replace('carrier 73 ', 'carrier 999 ').replace('real 73 ', 'real 999 ')
    if change == 'bad-number': text = text.replace('base 21 ', 'base -21 ')
    if change == 'bad-hash': text = text.replace(h(base), 'g' * 64)
    if change == 'extra-row': text += 'unexpected\n'
    if change == 'extra-field': text = text.replace('base 21 ', 'base 21 trailing ')
    if change == 'control-character': text = text.replace('base ', 'base\x00 ')
    if change == 'oversize-manifest': text += ' ' * 512
    if change == 'zero-size': text = text.replace(f'{h(real)} {len(real)}', f'{h(real)} 0')
    if change == 'oversize-body': text = text.replace(f'{h(real)} {len(real)}', f'{h(real)} 16777217')
    if change == 'wrong-length': text = text.replace(f'{h(real)} {len(real)}', f'{h(real)} {len(real)+1}')
    if change == 'changed-body': (user / 'museum/world073.body').write_bytes(b'x' * len(real))
    if change == 'empty-body': (user / 'museum/world073.body').write_bytes(b'')
    if change == 'changed-carrier': (game / 'data/resources/073_res_PC.aya').write_bytes(b'other authored archive')
    if change == 'body-directory':
        # Build this distinct fixture without creating a body at the target.
        (user / 'museum/world073.body').rename(user / 'museum/retained-original.body')
        (user / 'museum/world073.body').mkdir()
    (user / 'museum/room.cache').write_text(text)
    return user, game


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--editor-lifecycle', type=Path,
                    help='explicit read-only 0206 dependency while checking an unmerged candidate')
    ap.add_argument('--world-codec-tools', type=Path,
                    help='optional public Toolkit tools directory for independent world decoding')
    args = ap.parse_args()
    parent = ROOT / 'local-data/port/ce-museum-visit-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    out = out.resolve(); out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()): raise SystemExit('test output must be new and empty')
    records: list[dict] = []

    def run(label: str, command: list[str], expected: int = 0, timeout: int = 120) -> bool:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=timeout)
        (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
        good = result.returncode == expected
        records.append(dict(label=label, command=command, exit=result.returncode, expected=expected, passed=good))
        return good

    headers = out / 'headers'; headers.mkdir()
    (headers / 'common.h').write_text('#pragma once\n#include "Career.h"\n')
    (headers / 'Career.h').write_text('#pragma once\nstruct CCareer { unsigned char authored[257]; };\nextern CCareer fixtureCareer;\n#define CAREER fixtureCareer\n')
    (headers / 'frontend.h').write_text('#pragma once\nstruct FixtureFrontend { bool running = true; bool IsRunning() const { return running; } };\nextern FixtureFrontend fixtureFrontend;\n#define FRONTEND fixtureFrontend\n')
    provider = ROOT / 'port/game/ce_museum_visit.cpp'
    engine = ROOT / 'port/game/ce_museum_visit_engine.cpp'
    generator = ROOT / 'port/game/ce_museum_visit_generate.cpp'
    driver = ROOT / 'port/tools/tests/ce_museum_visit_driver.cpp'
    hash_object = out / 'sha256.o'
    assert run('sha-compile', ['gcc', '-std=c99', '-O1', '-c', str(ROOT / 'port/updater/sha256.c'), '-o', str(hash_object)])

    def compile_host(folder: Path, version: bool = True, provider_path: Path = provider, engine_path: Path = engine) -> Path:
        folder.mkdir(exist_ok=True)
        binary = folder / 'driver'
        command = ['g++', '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-DCE_MUSEUM_VISIT_HOST_FIXTURE=1',
                   '-I' + str(headers), '-I' + str(ROOT / 'port/game'),
                   *(['-DCE_VERSION="museum-test"'] if version else []),
                   str(driver), str(provider_path), str(engine_path), str(hash_object), '-o', str(binary)]
        assert run(folder.name + '-compile', command)
        return binary

    ordinary = compile_host(out / 'public')
    default = compile_host(out / 'default', False)
    modes = ['valid', 'retained', 'restart', 'restart-after-failure', 'restart-unfinished', 'missing-world', 'wrong-world', 'alias-world', 'long-world', 'null-output', 'allocation-failures',
             'native', 'native-changed', 'native-refused',
             'guard-menus', 'guard-frontend', 'guard-policy', 'guard-editor', 'guard-gallery']
    for mode in modes:
        user, game = fixture(out / ('fixture-' + mode))
        run(mode, [str(ordinary), mode, str(user), str(game)])
    user, game = fixture(out / 'fixture-default')
    run('default-off', [str(default), 'default', str(user), str(game)])
    invalid = ['missing-cache', 'version', 'duplicate-id', 'wrong-carrier', 'invalid-carrier', 'bad-number', 'bad-hash',
               'extra-row', 'extra-field', 'control-character', 'oversize-manifest', 'zero-size', 'oversize-body',
               'wrong-length', 'changed-body', 'empty-body', 'changed-carrier', 'body-directory']
    for change in invalid:
        user, game = fixture(out / ('fixture-' + change), change)
        run('refuse-' + change, [str(ordinary), 'refuse', str(user), str(game)])

    mutations = [
        ('restore-lost', 'engine', 'std::memcpy(&CAREER, gMuseumCareer, sizeof(CCareer));', '(void)0;', 'valid', ''),
        ('guard-early-release', 'provider', 'if (gVisit.active) gVisit.ended = true;', 'if (gVisit.active) gVisit.active = false;', 'valid', ''),
        ('editor-exclusion-lost', 'engine', '(PortCeEditorActive && PortCeEditorActive())', 'false', 'guard-editor', ''),
        ('policy-exclusion-lost', 'engine', '!PortCeAuthoringSessionAllowed()', 'false', 'guard-policy', ''),
        ('gallery-exclusion-lost', 'engine', 'PortCeMuseumActive()', 'false', 'guard-gallery', ''),
        ('frontend-exclusion-lost', 'engine', '!FRONTEND.IsRunning()', 'false', 'guard-frontend', ''),
        ('body-hash-ignored', 'provider', 'actual != body->hash', 'false', 'refuse', 'changed-body'),
        ('failure-latch-lost', 'provider', 'if (!bytes || !length || gVisit.ended || gVisit.failed) {', 'if (!bytes || !length || gVisit.ended) {', 'restart-after-failure', ''),
        ('restart-unfinished-allowed', 'provider', 'if (gVisit.ended && gVisit.loaded) {', 'if (gVisit.ended) {', 'restart-unfinished', ''),
        ('restart-never-allowed', 'provider', 'if (gVisit.ended && gVisit.loaded) {', 'if (false) {', 'restart', ''),
        ('input-handle-leak', 'provider', 'if (value) std::fclose(value);', '(void)0;', 'allocation-failures', ''),
        ('native-cache-selected', 'engine', 'bool PortCeMuseumVisitBegin(void) { return Begin(false); }',
         'bool PortCeMuseumVisitBegin(void) { return Begin(true); }', 'native', ''),
        ('native-source-check-lost', 'provider', 'if (!CheckInstalledSource()) return false;', '(void)0;', 'native-changed', ''),
    ]
    for label, which, old, new, mode, changed in mutations:
        folder = out / label; folder.mkdir()
        source = engine if which == 'engine' else provider
        text = source.read_text(); assert text.count(old) == 1
        altered = folder / source.name
        # Keep the source's relative updater header valid without changing it.
        copied = folder / 'game'; copied.mkdir()
        altered = copied / source.name; altered.write_text(text.replace(old, new))
        (folder / 'updater').symlink_to(ROOT / 'port/updater', target_is_directory=True)
        binary = compile_host(folder, provider_path=altered if which == 'provider' else provider,
                              engine_path=altered if which == 'engine' else engine)
        user, game = fixture(out / ('fixture-mutant-' + label), changed)
        run(label, [str(binary), mode, str(user), str(game)], 2)

    # Generate a separate mirror. Never mutate an existing runtime or its inputs.
    # An unmerged museum candidate can name its exact editor-hook dependency;
    # the merged tree needs no external input. Snapshot before building so a
    # concurrent author cannot change the compilation's patch or header inputs.
    dependencies = {}
    if args.editor_lifecycle:
        dependency = args.editor_lifecycle.resolve()
        if dependency.name != '0206-native-editor-lifecycle.patch':
            raise SystemExit('expected the editor lifecycle dependency')
        port_view = out / 'port-view'; port_view.mkdir()
        for child in build.PORT.iterdir():
            if child.name not in ('patches', 'game'):
                (port_view / child.name).symlink_to(child, target_is_directory=child.is_dir())
        for directory in ('patches', 'game'):
            shutil.copytree(build.PORT / directory, port_view / directory)
        shutil.copy2(dependency, port_view / 'patches' / dependency.name)
        dependency_header = dependency.parents[1] / 'game/ce_editor.h'
        shutil.copy2(dependency_header, port_view / 'game/ce_editor.h')
        dependencies = {str(dependency): sha(port_view / 'patches' / dependency.name),
                        str(dependency_header): sha(port_view / 'game/ce_editor.h')}
        build.PORT = port_view
    build.MIRROR_ROOT = out / 'mirror'; build.MIRROR = build.MIRROR_ROOT / 'src'
    with (out / 'mirror-prepare.log').open('w') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
        build.build_mirror()
    text = (build.MIRROR / 'DXMemBuffer.cpp').read_text()
    reader = '\n\n'.join(definition(text, n) for n in ['CDXMemBuffer', 'Read', 'ReadString', 'Skip', 'Close', 'InitOwnedRead'])
    (out / 'museum_reader.inc').write_text(reader + '\n')
    # The recovered Read/Skip declarations retain their original return quirks;
    # callers here inspect byte bounds/state, never an unspecified return value.
    binary = out / 'reader-driver'
    assert run('reader-compile', ['g++', '-std=c++14', '-O0', '-g', '-fpermissive', '-Wno-return-type',
                                 '-fsanitize=address', '-DCE_VERSION="museum-test"', '-DCE_MUSEUM_READER_FIXTURE=1',
                                 '-I' + str(out), str(driver), '-o', str(binary)])
    run('reader', [str(binary)])

    mesh_query = definition((build.MIRROR / 'mesh.cpp').read_text(), 'FindLoadedUnique', 'CMesh')
    (out / 'museum_mesh_query.inc').write_text(mesh_query + '\n')
    mesh_binary = out / 'mesh-query-driver'
    mesh_command = ['g++', '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-DCE_MUSEUM_MESH_FIXTURE=1', '-I' + str(out), str(driver), '-o', str(mesh_binary)]
    assert run('mesh-query-compile', mesh_command)
    run('mesh-query', [str(mesh_binary)])
    for label, old in [('ambiguous-mesh-accepted', 'if (match) return NULL;'),
                       ('unterminated-mesh-accepted', 'if (!memchr(mesh->mName, 0, sizeof(mesh->mName))) return NULL;')]:
        folder = out / label; folder.mkdir()
        assert mesh_query.count(old) == 1
        (folder / 'museum_mesh_query.inc').write_text(mesh_query.replace(old, '(void)0;') + '\n')
        command = mesh_command.copy()
        command[command.index('-I' + str(out))] = '-I' + str(folder)
        command[-1] = str(folder / 'driver')
        assert run(label + '-compile', command)
        run(label, [str(folder / 'driver')], 2)

    generator_out = out / 'generated-room'; generator_out.mkdir()
    generator_binary = out / 'generator-driver'
    generator_command = ['g++', '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                         '-Wno-unused-function', '-DCE_MUSEUM_GENERATOR_FIXTURE=1',
                         '-I' + str(ROOT / 'port/game'), '-I' + str(ROOT / 'src'),
                         str(driver), str(generator), '-o', str(generator_binary)]
    assert run('generator-compile', generator_command)
    assert run('generator', [str(generator_binary), str(generator_out)])
    for label, old_text, new_text in [
            ('site-view-ignored', 'if (eyeZ + dz * t >= ground - 0.3f) return false;', 'if (eyeZ + dz * t >= ground - 0.3f && false) return false;'),
            ('site-ring-ignored', 'const int ring = kRings[tier];', 'const int ring = 0;'),
            ('site-visible-count-lost', 'if (visible < bestVisible || (visible == bestVisible && range >= bestRange)) continue;',
             'if (range >= bestRange) continue;')]:
        text = generator.read_text(); assert text.count(old_text) == 1
        folder = out / label; folder.mkdir()
        mutant = folder / 'ce_museum_visit_generate.cpp'; mutant.write_text(text.replace(old_text, new_text))
        command = generator_command.copy()
        command[command.index(str(generator))] = str(mutant)
        command[-1] = str(folder / 'driver')
        assert run(label + '-compile', command)
        run(label, [str(folder / 'driver'), str(folder)], 2)
    if args.world_codec_tools:
        codec_path = args.world_codec_tools.resolve()
        sys.path.insert(0, str(codec_path))
        import world_resources_encode as world_codec
        dependencies[str(codec_path / 'world_resources_encode.py')] = sha(codec_path / 'world_resources_encode.py')
        dependencies[str(codec_path / 'world_resources_decode.py')] = sha(codec_path / 'world_resources_decode.py')

        def decode_generated(folder: Path) -> bool:
            models = {role: world_codec.read_world_model((folder / (role + '.body')).read_bytes())
                      for role in ('base', 'real')}
            for role, model in models.items():
                if model['header']['battleEngineConfigurations'] != ['authored.player']: return False
                if model['mapNumber'] != 42 or model['baseLevel'] != (-1 if role == 'base' else 41): return False
                if model['header']['worldType'] != 0: return False
                if any(model[key] for key in ('scripts', 'namedMeshes', 'treeGroups', 'treeTypes', 'waypointPaths')): return False
                if model['influenceMap']['nodes'] or model['influenceMap']['links']: return False
                if any(grid['data'] for grid in model['occupancyGrids']): return False
                if any(model['timings'].values()) or model['levelStartTime']: return False
                if world_codec.write_world_body(model) != (folder / (role + '.body')).read_bytes(): return False
            if models['base']['things']: return False
            things = models['real']['things']
            if [item['kind'] for item in things] != ['start', 'feature', 'feature', 'feature']: return False
            if things[0]['init']['start'] != dict(planeMode=0, playerNumber=1): return False
            if things[0]['init']['position'] != [12.0, 24.0, -3.0]: return False
            for i, item in enumerate(things):
                init = item['init']
                if init['script'] or init['spawnScript'] or init['attachScriptsToUnits']: return False
                if i:
                    expected = 'authored.feature.b' if (i - 1) % 2 else 'authored.feature.a'
                    if item['definition'] != expected or item['diesOn']: return False
                    if init['position'] != [16.0 + (i - 1) * 4.0, 24.0, -3.0]: return False
            return True

        records.append(dict(label='generator-independent-codec', passed=decode_generated(generator_out)))
        mutant = out / 'generator-wrong-player.cpp'
        text = generator.read_text()
        old = 'w.u32(0); w.u32(1); // walker; primary player identity is one-based'
        assert text.count(old) == 1
        mutant.write_text(text.replace(old, 'w.u32(0); w.u32(0); // deliberate wrong player identity'))
        mutant_binary = out / 'generator-wrong-player'
        command = generator_command.copy()
        command[command.index(str(generator))] = str(mutant)
        command[-1] = str(mutant_binary)
        assert run('generator-wrong-player-compile', command)
        mutant_out = out / 'generated-wrong-player'; mutant_out.mkdir()
        assert run('generator-wrong-player', [str(mutant_binary), str(mutant_out)])
        records.append(dict(label='generator-wrong-player-rejected', passed=not decode_generated(mutant_out)))

    # Bind the lifecycle assertions to their real function scopes. Keeping the
    # museum out of the editor's pause/frame path is as important as suppressing
    # campaign films and automatic commands. Runtime acceptance is still needed.
    game_source = (build.MIRROR / 'game.cpp').read_text()
    frontend_source = (build.MIRROR / 'FrontEnd.cpp').read_text()
    scopes = {name: definition(game_source, name, 'CGame') for name in
              ('RunIntroFMV', 'RunOutroFMV', 'PostLoadProcess', 'ShutdownRestartLoop',
               'RestartLoopRunLevel', 'PreRun', 'MainLoop')}
    hook_checks = {
        'intro-suppressed': 'if (PortCeEditorActive() || PortCeMuseumVisitActive()) return;' in scopes['RunIntroFMV'],
        'outro-suppressed': 'if (PortCeEditorActive() || PortCeMuseumVisitActive()) return;' in scopes['RunOutroFMV'],
        'load-refusal': 'if (PortCeMuseumVisitActive() && !PortCeMuseumVisitAfterLoad()) return FALSE;' in scopes['PostLoadProcess'],
        'level-end-guard': 'PortCeMuseumVisitLevelEnd();' in scopes['ShutdownRestartLoop'],
        'automatic-commands-suppressed': 'const bool ceEditorAuthoring = PortCeEditorActive() || PortCeMuseumVisitActive();' in scopes['RestartLoopRunLevel'],
        'warmup-remains-enabled': 'PortCeMuseumVisit' not in scopes['PreRun'],
        'movement-remains-enabled': 'PortCeMuseumVisit' not in scopes['MainLoop'],
        'frontend-returns-before-progression': 'if (PortCeEditorTakeReturn() || PortCeMuseumVisitTakeReturn())' in frontend_source,
    }
    (out / 'lifecycle-hooks.private.json').write_text(json.dumps(hook_checks, indent=2) + '\n')
    for label, passed in hook_checks.items():
        records.append(dict(label='hook-' + label, passed=passed, kind='source-scope assertion'))

    for platform in ['linux', 'windows']:
        target = build.TARGETS[platform]
        includes = [build.PORT / 'game', build.PORT / 'd3dx/include', build.PORT / 'include',
                    *target['includes'], build.MIRROR, build.MIRROR / 'MissionScript']
        for source in [provider, engine, generator, build.MIRROR / 'DXMemBuffer.cpp', build.MIRROR / 'world.cpp',
                       build.MIRROR / 'game.cpp', build.MIRROR / 'FrontEnd.cpp', build.MIRROR / 'mesh.cpp']:
            command = [*target['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
                       '-DCE_VERSION="museum-test"', '-fsyntax-only', '-include', str(build.PORT / 'include/port.h'),
                       *['-I' + str(p) for p in includes], str(source)]
            run(platform + '-' + source.name, command)
    result = dict(passed=all(r['passed'] for r in records), checks=records,
                  dependencies=dependencies,
                  sources={str(p.relative_to(ROOT)): sha(p) for p in [provider, engine, generator, driver, ROOT / 'port/game/ce_museum_visit.h', Path(__file__).resolve(), ROOT / 'port/patches/0207-ce-museum-visit-lifecycle.patch']},
                  limits=['Authored cache/lifecycle fixtures; not installed room acceptance.',
                          'Real reader methods with authored allocation/file stand-ins; no retail content in fixture.',
                          'Target syntax compilation is not a Windows execution claim.',
                          'No menu launch, rendered room, first-use generation or game/save mutation exercised.'])
    (out / 'result.private.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'passed': result['passed'], 'checks': len(records), 'receipt': str(out / 'result.private.json')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
