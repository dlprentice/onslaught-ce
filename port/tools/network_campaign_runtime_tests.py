#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual campaign handoff and boot admission with authored inputs only."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def extract(text, name):
    clean = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                   lambda m: re.sub('[^\n]', ' ', m.group()), text, flags=re.S)
    match = re.search(r'(?m)^(?:int|void) ' + name + r'\([^;{}]*\)\s*\{', clean)
    if not match:
        raise RuntimeError('campaign callable contract not found')
    end = clean.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    return text[match.start():end]


def main():
    parent = ROOT / 'local-data/port/network-campaign-runtime-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    codec = (ROOT / 'port/platform/common/ce_netcode.cpp').read_text()
    boot_source = (ROOT / 'port/game/ce_network_boot.cpp').read_text()
    globals_match = re.search(r'namespace\s*\{\s*CE_CampaignLaunch campaign_launch.*?\n\}', boot_source, re.S)
    if not globals_match:
        raise RuntimeError('typed boot state not found')
    boot = '#include "ce_network_boot.h"\n#include "network.h"\n#include <string.h>\n'
    boot += globals_match.group() + '\n'
    boot += '\n'.join(extract(boot_source, name) for name in
                      ('CE_BootCampaignClear', 'CE_BootCampaignAdmit', 'CE_BootCampaignGet'))
    variants = [
        ('actual', codec, boot, True),
        ('missing-mission-binding', codec.replace('campaign.mission != handoff->level || ', '', 1), boot, False),
        ('missing-canonical-check', codec.replace('if (!same) return CampaignParseRefuse', 'if (false && !same) return CampaignParseRefuse', 1), boot, False),
        ('missing-admission-clear', codec, boot.replace('    CE_BootCampaignClear();\n', '', 1), False),
    ]
    digest_object = out / 'sha256.o'
    with (out / 'digest-build.private.log').open('wb') as log:
        digest = subprocess.run(['cc', '-O1', '-c', str(ROOT / 'port/updater/sha256.c'),
                                 '-o', str(digest_object)], stdout=log, stderr=subprocess.STDOUT, timeout=30)
    if digest.returncode:
        raise RuntimeError('digest fixture compilation failed; private evidence: ' + str(out))
    results = []
    for label, trial_codec, trial_boot, expected in variants:
        trial = out / label
        trial.mkdir()
        (trial / 'codec.cpp').write_text(trial_codec)
        (trial / 'boot.cpp').write_text(trial_boot)
        executable = trial / 'driver'
        command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'port/platform/common'),
                   '-I' + str(ROOT / 'port/game'), '-I' + str(ROOT / 'port/network'),
                   str(trial / 'codec.cpp'), str(trial / 'boot.cpp'),
                   str(ROOT / 'port/tools/tests/network_campaign_runtime_driver.cpp'),
                   str(digest_object), '-o', str(executable)]
        with (trial / 'build.private.log').open('wb') as log:
            built = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=90)
        if built.returncode:
            raise RuntimeError('campaign fixture compilation failed; private evidence: ' + str(trial))
        with (trial / 'run.private.log').open('wb') as log:
            ran = subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, timeout=20)
        passed = ran.returncode == 0
        results.append({'case': label, 'compiled': True, 'passed': passed, 'expected_pass': expected})
        if passed != expected:
            raise RuntimeError('campaign fixture result differed; private evidence: ' + str(trial))
        if expected:
            handoff = trial / 'owned-launch'
            if handoff.exists():
                raise RuntimeError('fixture launch path unexpectedly exists')
            with (trial / 'restart.private.log').open('wb') as log:
                for mode in ('--write', '--read'):
                    process = subprocess.run([str(executable), mode, str(handoff)],
                                             stdout=log, stderr=subprocess.STDOUT, timeout=20)
                    if process.returncode:
                        raise RuntimeError('process restart fixture failed; private evidence: ' + str(trial))
            if handoff.exists():
                raise RuntimeError('consumed fixture launch remained on disk')
    record = {'results': results,
              'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (ROOT / 'port/platform/common/ce_netcode.cpp',
                                          ROOT / 'port/game/ce_network_boot.cpp')},
              'process_restart': 'Owned descriptor written and consumed by separate fixture processes.',
              'limits': 'Authored fixture and private temporary handoff only; no game, sockets or player acceptance.'}
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print('campaign handoff and boot: actual pass; three compiling controls rejected')
    print('private evidence: ' + str(out))


if __name__ == '__main__':
    main()
