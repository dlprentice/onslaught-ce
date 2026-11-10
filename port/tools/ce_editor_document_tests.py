#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored native-editor document transactions using the production project reader.

The supported v2 document is preserved in full. Unknown properties are rejected,
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
        'wrong-version': lambda v: v.update(version=3),
    }
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
