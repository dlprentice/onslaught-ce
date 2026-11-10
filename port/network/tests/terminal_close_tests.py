#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test immediate host close with and without a dropped terminal control, using loopback ENet only.

This tests transport behavior with authored fixture values. It does not launch the game, test the lockstep
controller, prove cross-machine behavior, or record packet contents or any real invitation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading

import run_tests as network_tests

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / 'local-data/network/terminal-close'


def verify_sources(enet):
    deps = network_tests.deps
    pins = deps.lock()
    items = [('enet', enet, ['callbacks.c', 'compress.c', 'host.c', 'list.c', 'packet.c',
                             'peer.c', 'protocol.c', 'unix.c']),
             ('monocypher', deps.SRC / 'monocypher-4.0.2', ['src/monocypher.c', 'src/monocypher.h'])]
    for name, source, selected in items:
        archive = pins[name]['file']
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != pins[name]['sha']:
            raise ValueError(f'{name}: dependency archive does not match deps.lock')
        with tarfile.open(archive) as tar:
            members = {'/'.join(Path(m.name).parts[1:]): m for m in tar.getmembers() if m.isfile()}
            if name == 'enet':
                selected += sorted(p for p in members if p.startswith('include/enet/') and p.endswith('.h'))
            for item in selected:
                if item not in members or tar.extractfile(members[item]).read() != (source / item).read_bytes():
                    raise ValueError(f'{name}: extracted source differs from pinned archive: {item}')
        print(f'{name}: pinned archive and {len(selected)} used source/header files verified', flush=True)


class LoopbackProxy:
    def __init__(self, host_port, drop_terminal):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.port = self.socket.getsockname()[1]
        self.host = ('127.0.0.1', host_port)
        self.guest = None
        self.drop_terminal = drop_terminal
        self.dropped = 0
        self.forwarded = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def _forward(self, data, address):
        sent = self.socket.sendto(data, address)
        self.forwarded += 1
        return sent

    def loop(self):
        while not self.stop.is_set():
            if not select.select([self.socket], [], [], 0.01)[0]:
                continue
            packet, sender = self.socket.recvfrom(65536)
            if sender == self.host:
                if self.guest is None:
                    continue
                dest = self.guest
                # Only the protocol's clear frame kind and test tick are read; the authenticated payload stays opaque.
                offsets = [i for i in range(len(packet) - 20) if packet[i:i + 4] == b'CEN1']
                terminal = any(packet[i + 6] == 16 and struct.unpack_from('>I', packet, i + 16)[0] == 4242 for i in offsets)
                if self.drop_terminal and terminal:
                    self.dropped += 1
                    continue
            else:
                self.guest = sender
                dest = self.host
            self._forward(packet, dest)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.socket.close()


def build(enet, run):
    network_tests.WORK = run / 'network-tests'
    network_tests.build(enet)
    deps = network_tests.deps
    objdir = network_tests.WORK / 'linux'
    objects = [objdir / (name + '.o') for name in
               ('callbacks', 'compress', 'host', 'list', 'packet', 'peer', 'protocol', 'unix', 'monocypher', 'codec', 'transport')]
    exe = run / 'terminal-close-peer'
    args = [str(deps.DATA / 'llvm/bin/clang++'), '-m32', *deps.sysroot_flags(), '-std=c++17', '-O2',
            '-I' + str(ROOT / 'port/network'), '-I' + str(ROOT / 'port/network/tests'),
            str(ROOT / 'port/network/tests/terminal_close_peer.cpp'), *map(str, objects),
            '-m32', *deps.sysroot_link_flags(), '-lpthread', '-o', str(exe)]
    subprocess.run(args, check=True)
    return exe


def pair(exe, drop_terminal):
    host = subprocess.Popen([str(exe), 'host', '0'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    first = host.stdout.readline()
    if not first.startswith('BOUND '):
        raise RuntimeError('host failed to bind: ' + first)
    proxy = LoopbackProxy(int(first.split()[1]), drop_terminal)
    guest = None
    try:
        guest = subprocess.Popen([str(exe), 'guest', str(proxy.port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        gout, gerr = guest.communicate(timeout=12)
        hout, herr = host.communicate(timeout=12)
        return {'host': {'rc': host.returncode, 'out': first + hout, 'err': herr},
                'guest': {'rc': guest.returncode, 'out': gout, 'err': gerr}, 'dropped': proxy.dropped, "forwarded": proxy.forwarded}
    finally:
        proxy.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--enet-source', type=Path, required=True)
    args = p.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='run-', dir=WORK))
    verify_sources(args.enet_source)
    exe = build(args.enet_source, run)
    normal = pair(exe, False)
    dropped = pair(exe, True)
    (run / 'result.json').write_text(json.dumps({'normal': normal, 'dropped': dropped}, indent=2) + '\n')
    normal_ok = (normal['host']['rc'] == 0 and normal['guest']['rc'] == 0 and
                 'HOST_TERMINAL_QUEUED_AND_CLOSED' in normal['host']['out'] and
                 'GUEST_TERMINAL_RECEIVED' in normal['guest']['out'] and normal['dropped'] == 0)
    drop_ok = (dropped['host']['rc'] == 0 and dropped['guest']['rc'] != 0 and dropped['dropped'] >= 1 and
               'INCOMPLETE' in dropped['guest']['out'] and 'GUEST_TERMINAL_RECEIVED' not in dropped['guest']['out'])
    print(('PASS' if normal_ok else 'FAIL') + ' delivered control before immediate host close')
    print(('PASS' if drop_ok else 'FAIL') + ' dropped control cannot be reported received after host close')
    print('evidence:', run / 'result.json')
    return 0 if normal_ok and drop_ok else 1


if __name__ == '__main__':
    sys.exit(main())
