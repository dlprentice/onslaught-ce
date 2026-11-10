#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual game-owner text roundtrip, then critical lockstep completion on loopback."""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
import os
import re
from pathlib import Path
import resource
import subprocess
import tempfile
import network_lockstep_transport_tests as fixture
from network_world_tests import actual_body
import deps

ROOT = Path(__file__).resolve().parents[2]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def limits() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (20, 20))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--transport-receipt', type=Path, required=True,
                        help='passing local transport receipt whose authored dependency objects are reused')
    parser.add_argument('--word-size', type=int, choices=(32, 64), action='append')
    args = parser.parse_args()
    receipt = args.transport_receipt.resolve()
    prior = json.loads(receipt.read_text())
    if prior.get('passed') is not True:
        parser.error('transport receipt did not pass')
    for name, digest in prior['dependency_sha256'].items():
        if sha(Path(name)) != digest:
            parser.error('dependency inputs changed since transport receipt')
    enet = deps.DATA / 'src/enet-5a9c537fd464b3c6d3c55e1d3bd47588faf71b42'
    mono = deps.DATA / 'src/monocypher-4.0.2/src'
    sources = [
        'port/tools/tests/network_chat_owner_driver.cpp',
        'port/network/transport.cpp', 'port/network/codec.cpp',
        'port/network/prerun_flags.cpp', 'port/game/ce_network_campaign.cpp',
        'port/game/ce_network_result_exchange.cpp', 'port/network/optional/text_chat.cpp',
        'port/network/optional/text_chat_owner.cpp',
        'port/network/optional/media_profile.cpp',
    ]
    observed = sources + ['port/game/ce_network_lockstep.cpp',
        'src/game.cpp', 'port/tools/tests/network_lockstep_terminal_driver.cpp',
        'port/game/ce_network_chat_ui.h', 'port/network/network.h',
        'port/network/text_chat_owner.h', 'port/network/text_chat.h',
        'port/network/media_profile.h', 'port/game/ce_network_world.cpp',
        'port/tools/network_chat_owner_integration_tests.py',
        'port/tools/network_lockstep_transport_tests.py']
    source_hashes = {s: sha(ROOT / s) for s in observed}
    base = ROOT / 'local-data/port/network-chat-owner-tests'
    base.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    validator = run / 'world_validator.inc'
    world = (ROOT / 'port/game/ce_network_world.cpp').read_text()
    a = world.index('const uint32_t native_actions[]=')
    b = world.index(';', a) + 1
    validator.write_text(world[a:b] + '\n' + actual_body(world, 'unsigned ActionRank(') + '\n' +
                         actual_body(world, 'bool ValidInput(') + '\nextern "C" ' +
                         actual_body(world, 'int CE_WorldLockstepValidInput(') + '\n')
    getter = run / 'native_slot_getter.inc'
    native_getter = actual_body((ROOT / 'src/game.cpp').read_text(), 'BOOL\tCGame::GetSlot(')
    getter.write_text(native_getter.replace('CGame::GetSlot', 'NativeGame::GetSlot', 1) + '\n')
    cases = []
    artifacts = {}
    for bits, mode in itertools.product(args.word_size or (32, 64), ('real', 'mock')):
        objects = receipt.parent / ('deps-' + str(bits))
        library = objects / 'libenet.a'
        crypto = receipt.parent / ('monocypher-' + str(bits) + '.o')
        for path in (library, crypto):
            if not path.is_file():
                parser.error('transport dependency artifact is missing')
            artifacts[str(path)] = sha(path)
        exe = run / ('chat-owner-' + mode + '-' + str(bits))
        selected = [s for s in sources if mode != 'mock' or s != 'port/network/transport.cpp']
        command = ['clang++', '-std=gnu++17', '-fms-extensions', '-fno-autolink',
                   '-fno-strict-aliasing', '-fwrapv', '-mms-bitfields', '-fshort-wchar',
                   '-O1', '-g0', '-DCE_NETWORK_TEXT=1',
                   '-DLOCKSTEP_VALIDATOR_INC="' + str(validator) + '"',
                   '-DNATIVE_SLOT_GETTER_INC="' + str(getter) + '"',
                   *(['-DCE_CHAT_OWNER_MOCK=1'] if mode == 'mock' else []),
                   '-I' + str(ROOT / 'port/tools/tests/lockstep_stubs'),
                   '-I' + str(enet / 'include'), '-I' + str(mono),
                   '-m' + str(bits), *[str(ROOT / s) for s in selected],
                   str(crypto), str(library),
                   *(['-Wl,--wrap=CE_NetSend', '-Wl,--wrap=CE_NetGetStatus',
                      '-Wl,--wrap=CE_NetReceive', '-Wl,--wrap=CE_NetPoll'] if mode == 'real' else []),
                   '-pthread', '-no-pie', '-o', str(exe)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=90,
                               preexec_fn=limits, env=dict(os.environ, TMPDIR=str(run)))
        log = run / ('build-' + mode + '-' + str(bits) + '.private.log')
        log.write_text(built.stdout + built.stderr)
        case = {'word_size': bits, 'mode': mode, 'compiled': built.returncode == 0,
                'compile_exit': built.returncode, 'build_log': str(log), 'passed': False}
        if built.returncode == 0 and mode == 'mock':
            tested = subprocess.run([str(exe)], capture_output=True, text=True,
                                    timeout=10, preexec_fn=limits)
            test_log = run / ('mock-' + str(bits) + '.private.log')
            test_log.write_text(tested.stdout + tested.stderr)
            case.update(run_log=str(test_log), run_exit=tested.returncode,
                        passed=tested.returncode == 0 and 'CHAT mock_checks failures=0' in tested.stdout)
        if built.returncode == 0 and mode == 'real':
            pair = fixture.pair(exe, None)
            trace = [re.findall(r'^CONFIRMED tick=([0-9]+) hash=([0-9a-f]{16})$', pair[role]['out'], re.M) for role in ('host', 'guest')]
            checks = {
                'two_owner_histories': all('CHAT owner_roundtrip' in pair[r]['out'] for r in ('host', 'guest')),
                'critical_completion': all(pair[r]['rc'] == 0 for r in ('host', 'guest')),
                'matching_nonempty_critical_confirmation': bool(trace[0]) and trace[0] == trace[1],
                'both_critical_control_flows': all(fixture.trace(pair[r]) for r in ('host', 'guest')),
            }
            pair_log = run / ('pair-' + str(bits) + '.private.json')
            pair_log.write_text(json.dumps(pair, indent=2) + '\n')
            case.update(checks=checks, pair_log=str(pair_log), passed=all(checks.values()))
        cases.append(case)
    unchanged = all(sha(ROOT / p) == digest for p, digest in source_hashes.items())
    unchanged = unchanged and all(sha(Path(p)) == digest for p, digest in artifacts.items())
    result = {'schema': 1, 'source_sha256': source_hashes,
              'dependency_receipt': str(receipt), 'dependency_artifact_sha256': artifacts,
              'cases': cases, 'unchanged_inputs': unchanged,
              'passed': unchanged and all(c['passed'] for c in cases),
              'limits': ['invented local fixtures only; no retail runtime',
                         'no visual or Windows acceptance', 'best effort local transport acceptance']}
    path = run / 'result.private.json'
    path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'passed': result['passed'], 'cases': len(cases), 'receipt': str(path)}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
