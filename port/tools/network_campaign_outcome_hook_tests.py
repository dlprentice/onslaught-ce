#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check authoritative outcome hook placement through the real port patch route."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = '0189-network-campaign-outcomes.patch'


def body(source, name):
    clean = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                   lambda m: re.sub('[^\n]', ' ', m.group()), source, flags=re.S)
    match = re.search(r'\b' + name + r'\s*\([^;{}]*\)\s*\{', clean)
    if not match:
        raise AssertionError('authoritative transition missing')
    start = clean.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    return source[start:end]


def valid(source):
    for name, terminal, outcome in (
            ('DeclareLevelWon', 'GAME_STATE_LEVEL_WON', 'CE_CAMPAIGN_OUTCOME_WIN'),
            ('DeclareLevelLost', 'GAME_STATE_LEVEL_LOST', 'CE_CAMPAIGN_OUTCOME_LOSS')):
        text = body(source, name)
        pattern = r'\b\w+\s*=\s*' + terminal + r'\s*;\s*CE_LockstepCampaignOutcome\(' + outcome + r'\);'
        if len(re.findall(pattern, text)) != 1:
            return False
    return source.count('CE_LockstepCampaignOutcome(') == 2


def main():
    spec = importlib.util.spec_from_file_location('campaign_port_build', ROOT / 'port/build.py')
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    # Later patches take this one's added lines as their context (0206's first game.cpp hunk), so they cannot apply without it: the
    # baseline is every patch before it, and no other patch file may carry the hook.
    patches = sorted((ROOT / 'port/patches').glob('*.patch'))
    baseline = build.patched_text(('game.cpp',), skip=tuple(p.name for p in patches if p.name >= PATCH), network=True)['game.cpp']
    carriers = [p.name for p in patches if p.name != PATCH and 'CE_LockstepCampaignOutcome(' in p.read_text(encoding='latin-1')]
    assert not carriers, 'hook exists outside its owning patch: ' + ', '.join(carriers)
    enabled = build.patched_text(('game.cpp',), network=True)['game.cpp']
    disabled = build.patched_text(('game.cpp',), network=False)['game.cpp']
    assert valid(enabled), 'actual transition hook placement failed'
    assert 'CE_LockstepCampaignOutcome(' not in baseline, 'hook exists outside its owning patch'
    assert 'CE_LockstepCampaignOutcome(' not in disabled, 'network-disabled game gained a runtime dependency'
    patch = (ROOT / 'port/patches' / PATCH).read_text()
    assert all(not line.startswith('-') or line.startswith('---') for line in patch.splitlines())
    assert all(not line.startswith(' ') for line in patch.splitlines()), 'original context leaked into authored patch'
    missing = enabled.replace('CE_LockstepCampaignOutcome(CE_CAMPAIGN_OUTCOME_WIN);', '', 1)
    assert not valid(missing), 'missing-notification control was not caught'
    pattern = r'(\b\w+\s*=\s*GAME_STATE_LEVEL_WON\s*;)\s*(CE_LockstepCampaignOutcome\(CE_CAMPAIGN_OUTCOME_WIN\);)'
    premature, count = re.subn(pattern, r'\2\n\1', enabled, count=1)
    assert count == 1 and not valid(premature), 'premature-notification control was not caught'
    parent = ROOT / 'local-data/port/network-campaign-outcome-hook-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    record = {'checks': 7, 'failed_controls_detected': 2,
              'patch_sha256': hashlib.sha256(patch.encode()).hexdigest(),
              'limits': 'Source placement and network-disabled guards; no running game acceptance.'}
    (out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print('campaign outcome hooks: seven checks and two negative controls passed')
    print('private evidence: ' + str(out))


if __name__ == '__main__':
    main()
