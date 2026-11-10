#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline production lockstep/transport controls for failure and voluntary leave.

The peer is a synthetic world fixture. It uses the actual lockstep controller and
freshly built transport objects; it neither launches the game nor reads a save.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from network_lockstep_transport_tests import ROOT, WORK, build, sha


def wait_for(path, marker, child, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data = path.read_bytes()
        if len(data) > 131072:
            raise RuntimeError('synthetic peer output exceeded its bound')
        match = marker.search(data)
        if match:
            return match
        if child.poll() is not None:
            raise RuntimeError('synthetic peer exited before the expected marker')
        time.sleep(0.02)
    raise RuntimeError('synthetic peer did not reach the expected marker')


def stop_owned(child, exe):
    if child is None or child.poll() is not None:
        return False
    try:
        cmdline = (Path('/proc') / str(child.pid) / 'cmdline').read_bytes().split(b'\0')
    except OSError as exc:
        raise RuntimeError('cannot verify synthetic peer before cleanup') from exc
    if not cmdline or cmdline[0] != os.fsencode(str(exe)):
        raise RuntimeError('synthetic peer identity changed before cleanup')
    child.terminate()
    try:
        child.wait(timeout=2)
    except subprocess.TimeoutExpired:
        cmdline = (Path('/proc') / str(child.pid) / 'cmdline').read_bytes().split(b'\0')
        if not cmdline or cmdline[0] != os.fsencode(str(exe)):
            raise RuntimeError('synthetic peer identity changed before forced cleanup')
        child.kill()
        child.wait(timeout=2)
    return True


def pair(exe, root, mode, actor_role):
    case = root / (mode + '-' + actor_role)
    case.mkdir(mode=0o700)
    outputs = {role: case / (role + '.out') for role in ('host', 'guest')}
    errors = {role: case / (role + '.err') for role in ('host', 'guest')}
    children = {'host': None, 'guest': None}
    files = []
    forced = False
    try:
        for path in (*outputs.values(), *errors.values()):
            files.append(path.open('w+b'))
        host_out, guest_out, host_err, guest_err = files
        scenario = mode + '-' + actor_role
        children['host'] = subprocess.Popen([str(exe), 'host', '0', scenario], stdin=subprocess.PIPE, stdout=host_out, stderr=host_err)
        match = wait_for(outputs['host'], re.compile(rb'^BOUND (\d+)\s*$', re.M), children['host'])
        port = str(int(match.group(1)))
        children['guest'] = subprocess.Popen([str(exe), 'guest', port, scenario], stdin=subprocess.PIPE, stdout=guest_out, stderr=guest_err)
        for role in ('host', 'guest'):
            wait_for(outputs[role], re.compile(rb'^ARMED\s*$', re.M), children[role])
        other = 'guest' if actor_role == 'host' else 'host'
        children[other].stdin.write(b'go\n')
        children[other].stdin.flush()
        wait_for(outputs[other], re.compile(rb'^RECEIVER_RUNNING\s*$', re.M), children[other])
        children[actor_role].stdin.write(b'go\n')
        children[actor_role].stdin.flush()
        for role in ('host', 'guest'):
            children[role].wait(timeout=16)
        actor_text = outputs[actor_role].read_bytes()
        observer_text = outputs[other].read_bytes()
        if len(actor_text) > 131072 or len(observer_text) > 131072:
            raise RuntimeError('synthetic peer output exceeded its bound')
        expected_notice = {'fault': b'REMOTE_FAILED', 'voluntary': b'REMOTE_LEFT',
                           'diverge': b'REMOTE_DESYNC', 'reject': b'REMOTE_FAILED',
                           'forced': b'REMOTE_LEFT'}[mode]
        actor_ok = children[actor_role].returncode == 0 and b'TRIGGER_DONE' in actor_text
        observer_status = 1 if mode == 'forced' else 0
        observer_marker = b'REMOTE_UNEXPECTED' if mode == 'forced' else b'REMOTE_EXPECTED'
        observer_ok = (children[other].returncode == observer_status and expected_notice in observer_text
                       and observer_marker in observer_text)
        rejected = b'REJECTED_DESYNC' in actor_text
        rejection_ok = rejected if mode == 'reject' else not rejected
        return {'passed': actor_ok and observer_ok and rejection_ok,
                'actor_ok': actor_ok, 'observer_ok': observer_ok, 'rejection_ok': rejection_ok,
                'actor_exit': children[actor_role].returncode, 'observer_exit': children[other].returncode,
                'forced_cleanup': False}
    finally:
        for child in children.values():
            forced = stop_owned(child, exe) or forced
        for f in files:
            f.close()
        if forced:
            raise RuntimeError('a synthetic peer required forced cleanup')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    WORK.mkdir(mode=0o700, parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='failure-run-', dir=WORK))
    sources = (ROOT / 'port/game/ce_network_lockstep.cpp',
               ROOT / 'port/game/ce_network_world.cpp',
               ROOT / 'port/tools/tests/network_lockstep_transport_peer.cpp',
               ROOT / 'port/network/transport.cpp', ROOT / 'port/network/codec.cpp',
               ROOT / 'port/network/network.h', ROOT / 'port/network/prerun_flags.cpp')
    source_pins = {str(path): sha(path) for path in sources}
    exe, provenance = build(run, args.build_dir)
    observations = run / 'failure-observations'
    observations.mkdir(mode=0o700)
    cases = {}
    for mode in ('fault', 'voluntary', 'diverge', 'reject'):
        for role in ('host', 'guest'):
            name = mode + '-' + role
            cases[name] = pair(exe, observations, mode, role)
            print(('PASS ' if cases[name]['passed'] else 'FAIL ') + name, flush=True)
    # A deliberately graceful close models the old mistake and must be classified
    # as a voluntary leave; that is the negative control for the failure notice.
    for role in ('host', 'guest'):
        name = 'forced-' + role
        cases[name] = pair(exe, observations, 'forced', role)
        print(('PASS ' if cases[name]['passed'] else 'FAIL ') + 'negative-' + name, flush=True)
    sources_unchanged = all(sha(Path(path)) == digest for path, digest in source_pins.items())
    objects_unchanged = all(sha(Path(item['path'])) == item['sha256'] for item in provenance['objects'].values())
    print(('PASS ' if sources_unchanged else 'FAIL ') + 'source-inputs-unchanged', flush=True)
    print(('PASS ' if objects_unchanged else 'FAIL ') + 'linked-objects-unchanged', flush=True)
    receipt = {'provenance': provenance, 'source_sha256': source_pins, 'cases': cases,
               'sources_unchanged': sources_unchanged, 'objects_unchanged': objects_unchanged}
    (run / 'failure-result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return 0 if sources_unchanged and objects_unchanged and all(v['passed'] for v in cases.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
