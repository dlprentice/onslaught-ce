#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native editor authoring state with authored objects, no game data or display.

This establishes collection, ambiguity, movement, undo and cursor-plane behavior;
it does not establish native menu, world, renderer or persistence integration.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def command(argv: list[str], owner: Path, name: str) -> int:
    result = subprocess.run(argv, capture_output=True, timeout=60, check=False)
    (owner / (name + '.stdout')).write_bytes(result.stdout)
    (owner / (name + '.stderr')).write_bytes(result.stderr)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = repo / 'local-data' / 'native-editor-tests'
    root.mkdir(parents=True, exist_ok=True)
    owner = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=root))
    owner.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('native editor checks require a C++ compiler')
    source = repo / 'port/game/ce_editor.cpp'
    header = repo / 'port/game/ce_editor.h'
    driver = repo / 'port/tools/tests/ce_editor_driver.cpp'
    exe = owner / 'state-check'
    options = [compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-I', str(header.parent), str(driver)]
    compiled = command(options + [str(source), '-o', str(exe)], owner, 'compile') == 0
    ran = compiled and command([str(exe)], owner, 'state') == 0
    body = source.read_text()
    variants = {
        'choose-first-duplicate': ('object.available = objects_[i].available = false;', 'object.available = false;'),
        'ignore-incomplete-collection': ('if (!collectionValid_) return false;', 'if (!collectionValid_) {}'),
        'save-during-drag': ('if (moving_) return;', 'if (false) return;'),
        'retain-redo-after-new-move': ('historySize_ = historyAt_;', '(void)historyAt_;'),
        'allow-behind-camera-plane': (' || distance < 0', ''),
    }
    controls = {}
    for name, (old, new) in variants.items():
        if body.count(old) != 1:
            raise RuntimeError('mutation anchor changed: ' + name)
        folder = owner / name
        folder.mkdir(exist_ok=False)
        candidate = folder / 'ce_editor.cpp'
        candidate.write_text(body.replace(old, new, 1))
        mutant = folder / 'control'
        built = command(options + [str(candidate), '-o', str(mutant)], folder, 'compile') == 0
        controls[name] = {'compiled': built,
                          'rejected_by_authored_checks': built and command([str(mutant)], folder, 'state') == 1}
    ok = ran and all(v['compiled'] and v['rejected_by_authored_checks'] for v in controls.values())
    result = {'passed': ok, 'scope': 'authored state and cursor-plane only; no native session or persistence proof',
              'compiler': compiler, 'baseline_compiled': compiled, 'baseline_passed': bool(ran),
              'controls': controls,
              'sha256': {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (source, header, driver, Path(__file__).resolve())}}
    (owner / 'RESULT.private.json').write_text(json.dumps(result, indent=2) + '\n')
    print('native editor state:', 'PASS' if ok else 'FAIL', '|', owner)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
