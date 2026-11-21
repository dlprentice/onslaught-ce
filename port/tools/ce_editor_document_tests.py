#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored native-editor document transactions using the production project reader.

Supported CE project metadata is preserved in full. Unknown properties are rejected,
including nested ones, rather than accepted and silently discarded. No game data,
display, native scene or actual career is used by these checks.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import ce_project_tests


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    parent = root / 'local-data/native-editor-document-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++ compiler is required.')
    owner = root / 'port/game/ce_project.cpp'
    header = owner.with_suffix('.h')
    driver = root / 'port/tools/tests/ce_editor_document_driver.cpp'
    binary = out / 'document-check'
    options = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I' + str(header.parent), str(driver)]

    def run(command, name):
        result = subprocess.run(command, capture_output=True, timeout=60, check=False)
        (out / (name + '.stdout')).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        return result.returncode

    if run(options + [str(owner), '-o', str(binary)], 'compile'):
        raise SystemExit('Document compilation failed; private output retained.')
    authored = ce_project_tests.authored()
    fixture = out / 'authored.ceproject'
    fixture.write_text(json.dumps(authored, ensure_ascii=False))
    updated = out / 'updated.ceproject'
    checks = {'document_transactions': run([str(binary), str(fixture), str(updated)], 'baseline') == 0}
    if checks['document_transactions']:
        result = json.loads(updated.read_text())
        with_goal = json.loads(updated.with_suffix(updated.suffix + '.goal').read_text())
        checks['goal_adds_just_version_and_objective'] = with_goal == dict(result, version=4, objective={
            'type': 'reach-point', 'position': [25.25, 70, -5]})
        with_route = json.loads(updated.with_suffix(updated.suffix + '.route').read_text())
        checks['ordered_route_preserves_other_metadata'] = with_route == dict(result, version=5, objectives=[
            with_goal['objective'], {'type': 'reach-point', 'position': [18, 32, 4]}])
        expected = copy.deepcopy(authored)
        expected['placements'][0]['position'] = [42.25, 11.5, -4]
        checks['all_original_properties_preserved'] = all(
            result[key] == value for key, value in expected.items() if key not in ('map', 'placements'))
        checks['original_binding_and_identity_preserved'] = (
            result['placements'][0] == expected['placements'][0]
            and result['map']['bindings'][0] == expected['map']['bindings'][0])
        checks['new_placement_linked'] = (
            len(result['placements']) == 2 and len(result['map']['bindings']) == 2
            and result['placements'][1]['definition'] == result['map']['bindings'][1]['definition']
            and result['map']['bindings'][1]['source_object'] == 'New object')

    mutations = {
        'unknown-root': lambda v: v.update(unsupported={'future': 1}),
        'unknown-map': lambda v: v['map'].update(unsupported={'future': 1}),
        'unknown-binding': lambda v: v['map']['bindings'][0].update(unsupported={'future': 1}),
        'unknown-placement': lambda v: v['placements'][0].update(unsupported={'future': 1}),
        'unknown-script': lambda v: v['scripts'][0].update(unsupported={'future': 1}),
        'side-on-v2': lambda v: v['placements'][0].update(side='muspell'),
        'side-outside-choice': lambda v: (v.update(version=3), v['placements'][0].update(side='unknown')),
        'side-not-a-string': lambda v: (v.update(version=3), v['placements'][0].update(side=1)),
        'v4-without-goal': lambda v: v.update(version=4),
        'v2-with-goal': lambda v: v.update(objective={'type':'reach-point','position':[3,4,5]}),
        'v3-with-goal': lambda v: v.update(version=3,objective={'type':'reach-point','position':[3,4,5]}),
        'unknown-rule': lambda v: v.update(version=4,objective={'type':'future','position':[3,4,5]}),
        'v5-without-stops': lambda v: v.update(version=5),
        'v5-single-not-sequence': lambda v: v.update(version=5, objectives=[{'type':'reach-point','position':[1,2,3]}]),
        'v5-third-stop': lambda v: v.update(version=5, objectives=[{'type':'reach-point','position':[1,2,3]}]*3),
        'v5-bad-second': lambda v: v.update(version=5, objectives=[{'type':'reach-point','position':[1,2,3]}, {'type':'future','position':[3,2,1]}]),
        'v5-conflicting-forms': lambda v: v.update(version=5, objective={'type':'reach-point','position':[1,2,3]}, objectives=[{'type':'reach-point','position':[1,2,3]}]*2),
        'v4-with-stops': lambda v: v.update(version=4, objectives=[{'type':'reach-point','position':[1,2,3]}]*2),
        'rule-no-height': lambda v: v.update(version=4,objective={'type':'reach-point','position':[3,4]}),
        'rule-out-of-range': lambda v: v.update(version=4,objective={'type':'reach-point','position':[3,2147483647,5]}),
        'rule-extra-field': lambda v: v.update(version=4,objective={'type':'reach-point','position':[3,4,5],'script':'test'}),
        'wrong-version': lambda v: v.update(version=5),
    }
    for label, version, extra in (
        ('hold-on-v5',5,{}), ('hold-negative-radius',6,{'radius':-5}), ('hold-zero-seconds',6,{'seconds':0}),
        ('hold-fractional-seconds',6,{'seconds':1.5}), ('hold-fractional-radius',6,{'radius':12.5}),
        ('hold-too-large',6,{'radius':121}), ('hold-too-long',6,{'seconds':31}), ('hold-extra-field',6,{'script':'text'})):
        mutations[label]=lambda v,ver=version,changes=extra: v.update(version=ver,objectives=[dict(type='hold-area',position=[1,2,3],radius=18,seconds=3)|changes])
    mutations['missing-hold-radius']=lambda v: v.update(version=6,objectives=[dict(type='hold-area',position=[1,2,3],seconds=3)])
    mutations['reach-only-v6']=lambda v: v.update(version=6,objectives=[dict(type='reach-point',position=[1,2,3])])
    for name, mutate in mutations.items():
        value = copy.deepcopy(authored)
        mutate(value)
        path = out / (name + '.ceproject')
        path.write_text(json.dumps(value))
        checks[name] = run([str(binary), str(path), '--reject'], name) == 0

    source = owner.read_text()
    variants = {
        'editor-career-guard-removed': (
            '!(PortCeEditorActive && PortCeEditorActive())', 'true'),
        'museum-career-guard-removed': (
            '!(PortCeMuseumVisitActive && PortCeMuseumVisitActive())', 'true'),
        'partial-update-published': ('Json root = document->root;', 'Json& root = document->root;'),
    }
    controls = {}
    for name, (before, after) in variants.items():
        if source.count(before) != 1:
            raise RuntimeError('Document control anchor changed: ' + name)
        candidate = out / (name + '.cpp')
        candidate.write_text(source.replace(before, after, 1))
        mutant = out / name
        built = run(options + [str(candidate), '-o', str(mutant)], name + '-compile') == 0
        rejected = built and run([str(mutant), str(fixture), str(out / (name + '.ceproject'))], name) == 1
        controls[name] = {'compiled': built, 'rejected': rejected}
    passed = all(checks.values()) and all(c['compiled'] and c['rejected'] for c in controls.values())
    receipt = {'passed': passed, 'scope': __doc__, 'checks': checks, 'causal_controls': controls,
               'sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (owner, header, driver, Path(__file__).resolve())}}
    (out / 'RESULT.private.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('native editor documents:', 'PASS' if passed else 'FAIL', '|', out)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
