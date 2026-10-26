#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile production 0070 observers/logger and real render-method predicates on authored host boundaries.

The water control slices the actual shader-selection assignment and three real Try/diagnostic branch entries;
rendering/API work is replaced by an explicit vertex-validation outcome. Complete renderer TUs require target
compilation separately. No game, GPU, device, retail asset, screenshot or sound is used here.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDL = '#pragma once\n#include <cstdint>\nusing Uint64=uint64_t;\nUint64 SDL_GetTicks();\n'


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/render-diagnostics-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    receipt = {'inputs': {}, 'commands': [], 'cases': [], 'passed': False,
               'limits': ['Host fake SDL, game fields and validation outcomes; no rendering/API success or native platform behavior measured.',
                          'Actual diagnostic functions/logger, render-method bodies and extracted selection/branch entries compile; renderer body is not run.']}
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_PORT_', 'TEST_CE_'))}
    env.update(ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:exitcode=65', TMPDIR=str(out))
    try:
        files = ['src/DXWater.cpp', 'src/DXEngine.cpp', 'src/DXKempyCube.cpp', 'src/RenderMethod.cpp',
                 'port/patches/0070-render-path-diagnostics.patch', 'port/platform/common/portlog.cpp',
                 'port/platform/common/portlog.h', 'port/tools/tests/render_diagnostics_driver.cpp',
                 'port/tools/render_diagnostics_tests.py', 'src/tweak.h', 'src/heightfield.h']
        for name in files:
            dest = out / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, dest)
            receipt['inputs'][name] = hashlib.sha256(dest.read_bytes()).hexdigest()
        result = subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', str(out / files[4])],
                                cwd=out / 'src', capture_output=True, text=True, timeout=10)
        (out / 'patch.log').write_text(result.stdout + result.stderr)
        if result.returncode or 'fuzz' in result.stdout: raise RuntimeError('0070 patch did not apply exactly')
        def function(raw, name):
            found = re.findall(r'^(?:static )?[^\n]*\b' + re.escape(name) + r'\([^\n]*\)[^\n]*\n\{\n.*?^\}', raw, re.M | re.S)
            if len(found) != 1: raise RuntimeError('expected one complete function: ' + name)
            return found[0] + '\n'
        water, engine, sky = [(out / 'src' / name).read_text() for name in ('DXWater.cpp', 'DXEngine.cpp', 'DXKempyCube.cpp')]
        helpers = '\n'.join(function(raw, name) for raw, name in ((water, 'PortWaterRenderPath'), (engine, 'PortRenderContext'), (sky, 'PortSkyCubeLoad')))
        (out / 'diagnostics.inc').write_text('#ifdef CE_VERSION\n' + helpers + '#endif\n')
        methods = (out / 'src/RenderMethod.cpp').read_text()
        (out / 'methods.inc').write_text('\n'.join(function(methods, 'CRenderMethod::' + name) for name in ('Try', 'Accept', 'BeginVBufCheck', 'CheckVBufValid')))
        selection = re.findall(r'^\tWATER_USE_SHADER = .*?;', water, re.S | re.M)
        if len(selection) != 1: raise RuntimeError('shader selection changed')
        branches = []
        for n in (2, 1, 0):
            pattern = r'\tif \(WATER_METHOD.Try\(' + str(n) + r'\)\)\n\t\{\n#ifdef CE_VERSION\n\t\tPortWaterRenderPath\(' + str(n) + r'\);\n#endif'
            found = re.findall(pattern, water)
            if len(found) != 1: raise RuntimeError('water observer is not bound to actual method branch: ' + str(n))
            branches.append(found[0] + '\n entered.push_back(' + str(n) + ');\n WATER_METHOD.BeginVBufCheck();\n CHECK(VBUFTEXTURE_VALIDATE == !WATER_METHOD.mAccepted.value);\n VBUFTEXTURE_VALIDATE_FAILED = ' + str(n) + ' > validMethod;\n if(WATER_METHOD.CheckVBufValid()) WATER_METHOD.Accept();\n }\n')
        if not re.search(r'wlevel = MAP.GetWaterLevel\(\);\n#ifdef CE_VERSION\n\tPortRenderContext\(campos, wlevel, viewpoint\);\n#endif', engine):
            raise RuntimeError('context observer lost the actual precomputed camera/water call site')
        if not re.search(r'void CDXKempyCube::Load\(UBYTE number\)[^\n]*\n\{\n#ifdef CE_VERSION\n\tPortSkyCubeLoad\(number\);\n#endif', sky):
            raise RuntimeError('sky observer lost the actual Load argument')
        (out / 'water.inc').write_text('static void RunWater(int validMethod) {\n' + selection[0] + '\n' + ''.join(branches) + '}\n')
        sdl = out / 'SDL3/SDL.h'; sdl.parent.mkdir(); sdl.write_text(SDL)
        driver = out / 'port/tools/tests/render_diagnostics_driver.cpp'
        command = [*shlex.split(os.environ.get('CXX', 'clang++')), '-std=c++17', '-O1', '-g', '-fsanitize=address', '-fno-omit-frame-pointer',
                   '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-I' + str(out),
                   '-DSESSION_LOGGER="' + str(out / 'port/platform/common/portlog.cpp') + '"',
                   '-DRENDER_METHOD_SOURCE="' + str(out / 'methods.inc') + '"',
                   '-DRENDER_DIAGNOSTICS_SOURCE="' + str(out / 'diagnostics.inc') + '"',
                   '-DWATER_SELECTION_SOURCE="' + str(out / 'water.inc') + '"', str(driver)]
        def build(label, extra=()):
            binary = out / label
            cmd = command + list(extra) + ['-o', str(binary)]
            receipt['commands'].append(cmd)
            result = subprocess.run(cmd, capture_output=True, env=env, timeout=90)
            (out / (label + '.build.log')).write_bytes(result.stdout + result.stderr)
            if result.returncode: raise RuntimeError(label + ' compile failed')
            return binary
        def run(binary, case, extra=None, expected=0, contains=(), absent=()):
            folder = out / (binary.name + '-' + case + '-' + str(len(receipt['cases'])))
            folder.mkdir()
            result = subprocess.run([str(binary), case], capture_output=True, env=dict(env, **(extra or {})), timeout=10)
            (folder / 'stdout.txt').write_bytes(result.stdout); (folder / 'stderr.txt').write_bytes(result.stderr)
            text = result.stderr.decode()
            passed = result.returncode == expected and all(x in text for x in contains) and not any(x in text for x in absent)
            if case in ('water-limit', 'context-limit', 'sky-limit'):
                lines = text.splitlines()
                passed = passed and len(lines) == 32 and all(
                    ('context_event=' + str(n) + ' context_limit=32') in line for n, line in enumerate(lines, 1))
                passed = passed and 'context_event=32 context_limit=32' in lines[-1]
            receipt['cases'].append({'binary': binary.name, 'case': case, 'environment': extra or {}, 'returncode': result.returncode, 'expected': expected, 'passed': passed})
            if not passed: raise RuntimeError('case failed: ' + folder.name)
        current = build('ce', ['-DCE_VERSION="test"'])
        private = build('private')
        run(private, 'disabled')
        for key in ('BEA_PORT_CLOCK', 'BEA_PORT_TAPE', 'BEA_PORT_PROBES', 'BEA_PORT_RECORD', 'TEST_CE_OFF'):
            run(current, 'disabled', {key: '1'})
        for case in ('water-shader', 'water-fixed', 'water-multi', 'water-f1', 'water-method1', 'water-method0', 'water-fallback', 'water-accepted', 'water-limit',
                     'context-bands', 'context-grid', 'context-mirror-view', 'context-nonfinite', 'context-change', 'context-limit', 'sky', 'sky-limit', 'shared-limit'):
            contains = []
            if case == 'water-shader': contains = ['branch_entered=2 shader_selected=1 accepted_before=0']
            if case in ('water-fixed', 'water-multi', 'water-f1'): contains = ['branch_entered=2 shader_selected=0']
            if case == 'water-method1': contains = ['branch_entered=1 shader_selected=0']
            if case == 'water-method0': contains = ['branch_entered=0 shader_selected=0']
            if case == 'water-fallback': contains = ['branch_entered=2 shader_selected=1', 'branch_entered=1 shader_selected=0', 'branch_entered=0 shader_selected=0']
            if case == 'context-bands': contains = ['water_height_band=' + str(n) for n in range(5)]
            if case == 'context-grid': contains = ['inside_grid=' + str(n) for n in (-1, 0, 1)]
            if case == 'context-nonfinite': contains = ['water_height_band=-1', 'inside_grid=-1']
            if case == 'sky': contains = ['cube_load=7 half_bits=45ebc724 top_bits=456bc724', 'cube_load=8']
            if case == 'shared-limit': contains = ['ordinary event limit reached']
            run(current, case, contains=contains)
        # Mutants are separate private inputs/binaries; controls must reject each incorrect observer contract.
        mutations = [('off-guard', 'diagnostics.inc', 'if (!PortSessionEnabled()) return;', 'if (false) return;', 'disabled', {'BEA_PORT_TAPE':'1'}),
                     ('water-method', 'water.inc', 'PortWaterRenderPath(2)', 'PortWaterRenderPath(1)', 'water-shader', {}),
                     ('water-shader', 'diagnostics.inc', 'method == 2 && WATER_USE_SHADER', 'method == 1 && WATER_USE_SHADER', 'water-shader', {}),
                     ('height-sign', 'diagnostics.inc', 'wlevel - campos.Z', 'campos.Z - wlevel', 'context-bands', {}),
                     ('sky-constant', 'port/tools/tests/render_diagnostics_driver.cpp', '#define KC_S 7544.89258f', '#define KC_S 1.0f', 'sky', {}),
                     ('local-limit', 'diagnostics.inc', 'events >= 32', 'events >= 320', 'water-limit', {})]
        for label, rel, before, after, case, extra in mutations:
            source = out / rel; saved = source.read_text()
            if before not in saved: raise RuntimeError('mutant input changed: ' + label)
            mutant_source = out / ('mutant-' + label + '.input'); mutant_source.write_text(saved.replace(before, after))
            # Compilers read only one job at a time; restore the frozen current input immediately afterwards.
            source.write_text(mutant_source.read_text())
            try:
                binary = build('mutant-' + label, ['-DCE_VERSION="test"'])
            finally:
                source.write_text(saved)
            # Output-field mutations are killed by the same required text controls used for the current binary.
            expected_text = {'water-method':'branch_entered=2 shader_selected=1', 'water-shader':'branch_entered=2 shader_selected=1',
                             'sky-constant':'half_bits=45ebc724 top_bits=456bc724'}.get(label)
            result = subprocess.run([str(binary), case], capture_output=True, env=dict(env, **extra), timeout=10)
            (out / ('mutant-' + label + '.stdout.txt')).write_bytes(result.stdout)
            (out / ('mutant-' + label + '.stderr.txt')).write_bytes(result.stderr)
            killed = result.returncode == 1 or (result.returncode == 0 and expected_text and expected_text not in result.stderr.decode())
            receipt['cases'].append({'binary': binary.name, 'case': case, 'returncode': result.returncode, 'mutant_killed': bool(killed), 'passed': bool(killed)})
            if not killed: raise RuntimeError('mutant survived: ' + label)
        receipt['generated'] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(out.rglob('*')) if p.is_file() and (p.suffix == '.inc' or p.suffix == '.input')}
        receipt['passed'] = True
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        receipt['error'] = str(error)
    (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(('PASS' if receipt['passed'] else 'FAIL') + ' ' + str(len(receipt['cases'])) + ' groups: ' + str(out / 'result.json'))
    if 'error' in receipt: print(receipt['error'])
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
