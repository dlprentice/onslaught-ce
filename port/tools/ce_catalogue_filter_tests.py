#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Player-facing catalogue search on an authored installation; no game assets."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main():
    parent = ROOT / 'local-data' / 'catalogue-filter-tests'
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as temp:
        out = Path(temp)
        meshes = out / 'data/resources/meshes'
        textures = out / 'data/resources/dxtntextures'
        interface = out / 'data/resources/textures'
        for path in (meshes, textures, interface):
            path.mkdir(parents=True)
        for name in ('m_Scout.msh.aya', 'm_Ferry.msh.aya', 'm_Observer.msh.aya'):
            (meshes / name).write_bytes(b'owned fixture')
        for i in range(36):
            (textures / f'mustbe_Panel_{i:02}.tga(0).aya').write_bytes(b'owned fixture')
        for name in ('mustbe_Reticle.tga(0).aya', 'mustbe_Reticle.tga(1).aya', 'mustbe_Reticle.tga(2).aya'):
            (interface / name).write_bytes(b'owned fixture')
        inputs = ['port/game/ce_asset_catalog.cpp', 'port/game/ce_catalogue_menu.cpp', 'port/game/ce_catalogue_sounds.cpp', 'port/tools/tests/ce_catalogue_sounds_none.cpp',
                  'port/tools/tests/ce_catalogue_filter_driver.cpp']
        for sanitized in (False, True):
            binary = out / ('sanitized' if sanitized else 'normal')
            cmd = [os.getenv('CXX', 'clang++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DCE_VERSION',
                   '-I', str(ROOT / 'port/game'), *map(lambda p: str(ROOT / p), inputs), '-lz', '-o', str(binary)]
            if sanitized:
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(cmd, check=True)
            subprocess.run([str(binary), str(out)], check=True)

if __name__ == '__main__':
    main()
