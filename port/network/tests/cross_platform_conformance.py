#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run authored network fixtures as Linux i386 and Windows i386 console programs.

This opt-in, Linux-hosted check needs existing local toolchains and libraries.
It creates a new private output directory, uses a private Wine prefix and requires
an isolated network namespace. It reads no installed game or user save. Equal
fixture outcomes do not establish live-game state parity or real Windows behavior.

The output directory's parent must already exist inside this checkout's local-data directory; a private bind mount there is supported.
Focused output checks: python3 -m unittest discover -s port/network/tests -p 'test_conformance_*.py'
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
INCLUDE = ROOT / 'port/network'
NET = ROOT / 'port' / 'network'
TESTS = NET / 'tests'
ISOLATED_EXEC = '''import os, subprocess, sys
result = subprocess.run([sys.argv[1], "link", "set", "lo", "up"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if result.returncode:
    raise SystemExit(125)
os.execvpe(sys.argv[2], sys.argv[2:], os.environ)
'''


class CheckFailure(Exception):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def executable(value: str, label: str) -> str:
    found = shutil.which(value) if '/' not in value else os.path.abspath(value)
    if not found or not Path(found).is_file() or not os.access(found, os.X_OK):
        raise CheckFailure(label + ' is unavailable')
    return found


def source_file(value: str, label: str) -> Path:
    path = Path(value).absolute()
    if not path.is_file():
        raise CheckFailure(label + ' is unavailable')
    return path


def valid_linux_i386(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open('rb') as f:
        head = f.read(20)
    return (len(head) == 20 and head[:4] == b'\x7fELF' and
            head[4] == 1 and head[5] == 1 and
            struct.unpack_from('<H', head, 18)[0] == 3)


def valid_windows_i386(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open('rb') as f:
        head = f.read(64)
        if len(head) < 64 or head[:2] != b'MZ':
            return False
        offset = struct.unpack_from('<I', head, 60)[0]
        if offset > 16 * 1024 * 1024:
            return False
        f.seek(offset)
        pe = f.read(6)
    return len(pe) == 6 and pe[:4] == b'PE\0\0' and struct.unpack_from('<H', pe, 4)[0] == 0x14c


def private_environment(out: Path) -> dict[str, str]:
    names = ('home', 'tmp', 'runtime', 'config', 'cache', 'data')
    for name in names:
        (out / name).mkdir(mode=0o700)
    return {
        'PATH': os.defpath,
        'HOME': str(out / 'home'),
        'TMPDIR': str(out / 'tmp'),
        'XDG_RUNTIME_DIR': str(out / 'runtime'),
        'XDG_CONFIG_HOME': str(out / 'config'),
        'XDG_CACHE_HOME': str(out / 'cache'),
        'XDG_DATA_HOME': str(out / 'data'),
        'WINEPREFIX': str(out / 'wine'),
        'WINEDEBUG': '-all',
        'WINEDLLOVERRIDES': 'mscoree,mshtml=',
        'SDL_AUDIO_DRIVER': 'dummy',
        'BEA_REPORT_NO_OPEN': '1',
        'BROWSER': '/bin/true',
        'LANG': 'C',
        'LC_ALL': 'C',
        'TZ': 'UTC',
    }


def run_step(out: Path, env: dict[str, str], receipt: dict, label: str,
             command: list[str], timeout: int = 90) -> bytes:
    stdout = out / (label + '.stdout')
    stderr = out / (label + '.stderr')
    with stdout.open('xb') as so, stderr.open('xb') as se:
        try:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=so,
                                    stderr=se, cwd=ROOT, env=env, timeout=timeout,
                                    check=False)
            status = result.returncode
        except (OSError, subprocess.TimeoutExpired):
            status = None
    receipt['steps'].append({'step': label, 'exit': status,
                             'stderr_bytes': stderr.stat().st_size})
    if status != 0:
        raise CheckFailure(label + ' failed')
    return stdout.read_bytes()


def dependency_hashes(out: Path, dep_labels: list[str]) -> list[dict[str, str]]:
    found: dict[str, dict[str, str]] = {}
    for label in dep_labels:
        text = (out / (label + '.stdout')).read_text(encoding='utf-8')
        if ':' not in text:
            raise CheckFailure('dependency output is invalid')
        fields = shlex.split(text.split(':', 1)[1].replace('\\\n', ' '))
        if not fields:
            raise CheckFailure('dependency output is empty')
        for field in fields:
            path = Path(field)
            if not path.is_absolute():
                path = ROOT / path
            path = path.resolve(strict=True)
            if not path.is_file():
                raise CheckFailure('a dependency is unavailable')
            try:
                label_path = 'repo/' + str(path.relative_to(ROOT))
            except ValueError:
                label_path = 'external/' + hashlib.sha256(str(path).encode()).hexdigest()
            found[label_path] = {'identity': label_path, 'sha256': digest(path)}
    return [found[k] for k in sorted(found)]


def normalized(data: bytes) -> bytes:
    return data.replace(b'\r\n', b'\n')


FIXTURE_COMPLETION_LINES = {
    'codec': b'codec: golden endian/float bytes, every truncation, bounds, NaN/Inf and trailing data passed',
    'protocol': b'protocol: malformed/replay/reflection/session/epoch/chunk/expiry/queue/auth and raw ingress byte/packet/metadata bounds passed',
}


def check_fixture_outcomes(fixture: str, linux: bytes, windows: bytes) -> None:
    """Require each fixture's final completion line before comparing outcomes."""
    completion = FIXTURE_COMPLETION_LINES[fixture]
    outcomes = []
    for platform, data in (('Linux', linux), ('Windows', windows)):
        outcome = normalized(data)
        lines = outcome.split(b'\n')
        if lines[-2:] != [completion, b''] or lines.count(completion) != 1:
            raise CheckFailure(f'{fixture} {platform} fixture completion output is invalid')
        outcomes.append(outcome)
    if outcomes[0] != outcomes[1]:
        raise CheckFailure(f'{fixture} fixture outcomes differ')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, epilog="The output directory's parent must already exist inside this checkout's local-data directory; a private bind mount there is supported.")
    for flag in ('native-cc', 'native-cxx', 'windows-cc', 'windows-cxx', 'wine',
                 'enet-include', 'linux-enet', 'windows-enet', 'crypto-source', 'output'):
        parser.add_argument('--' + flag, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024 * 1024, 64 * 1024 * 1024))
    try:
        native_cc = executable(args.native_cc, 'native C compiler')
        native_cxx = executable(args.native_cxx, 'native C++ compiler')
        windows_cc = executable(args.windows_cc, 'Windows C compiler')
        windows_cxx = executable(args.windows_cxx, 'Windows C++ compiler')
        wine = executable(args.wine, 'Wine')
        unshare = executable('unshare', 'network namespace support')
        ip = executable('ip', 'loopback setup')
        enet_include = Path(args.enet_include).absolute()
        source_file(str(enet_include / 'enet' / 'enet.h'), 'ENet header')
        linux_enet = source_file(args.linux_enet, 'Linux ENet archive')
        windows_enet = source_file(args.windows_enet, 'Windows ENet archive')
        crypto = source_file(args.crypto_source, 'crypto source')
        source_file(str(crypto.with_suffix('.h')), 'crypto header')
        for label, path in (('codec', NET / 'codec.cpp'),
                            ('codec fixture', TESTS / 'codec_tests.cpp'),
                            ('protocol fixture', TESTS / 'protocol_tests.cpp')):
            source_file(str(path), label)
        output = Path(args.output).absolute()
        parent = output.parent.resolve(strict=True)
        allowed = ((ROOT / 'local-data').resolve(),)
        if not any(parent == base or base in parent.parents for base in allowed):
            raise CheckFailure("The output directory's parent must already exist inside this checkout's local-data directory.")
        output.mkdir(mode=0o700)
    except (CheckFailure, OSError, RuntimeError):
        print('Input or output setup failed; no test was reported as passing.', file=sys.stderr)
        return 2

    try:
        env = private_environment(output)
    except OSError:
        print('Private test environment setup failed.', file=sys.stderr)
        return 2
    receipt: dict = {'format': 1, 'status': 'incomplete', 'steps': [],
                     'limits': ['synthetic fixtures', 'Wine is not native Windows',
                                'no gameplay or Internet session tested',
                                'system headers and runtimes are not exhaustively pinned']}
    headers = ['-I' + str(INCLUDE), '-I' + str(enet_include), '-I' + str(crypto.parent)]
    codec = str(NET / 'codec.cpp')
    codec_test = str(TESTS / 'codec_tests.cpp')
    protocol_test = str(TESTS / 'protocol_tests.cpp')
    cpp_flags = ['-std=c++17', '-O2']
    try:
        executables = {'native_cc': native_cc, 'native_cxx': native_cxx,
                       'windows_cc': windows_cc, 'windows_cxx': windows_cxx, 'wine': wine}
        digest_cache: dict[Path, str] = {}
        for value in executables.values():
            resolved = Path(value).resolve(strict=True)
            if resolved not in digest_cache:
                digest_cache[resolved] = digest(resolved)
        receipt['toolchain_sha256'] = {label: digest_cache[Path(value).resolve(strict=True)]
                                        for label, value in executables.items()}
        native_base = [native_cxx, '-m32', *cpp_flags, *headers]
        windows_base = [windows_cxx, *cpp_flags, *headers]
        for label, base, source in (
            ('deps-linux-codec', native_base, codec),
            ('deps-linux-codec-test', native_base, codec_test),
            ('deps-linux-protocol', native_base, protocol_test),
            ('deps-win-codec', windows_base, codec),
            ('deps-win-codec-test', windows_base, codec_test),
            ('deps-win-protocol', windows_base, protocol_test),
        ):
            run_step(output, env, receipt, label, [*base, '-MM', source])
        receipt['dependencies'] = dependency_hashes(
            output, ['deps-linux-codec', 'deps-linux-codec-test', 'deps-linux-protocol',
                     'deps-win-codec', 'deps-win-codec-test', 'deps-win-protocol'])
        receipt['external_inputs'] = {
            'linux_enet_sha256': digest(linux_enet),
            'windows_enet_sha256': digest(windows_enet),
            'crypto_source_sha256': digest(crypto),
            'crypto_header_sha256': digest(crypto.with_suffix('.h')),
        }
        head = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                              stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, text=True, check=False, env=env)
        if head.returncode == 0:
            receipt['source_head'] = head.stdout.strip()
        native_codec = output / 'codec-linux'
        win_codec = output / 'codec-windows.exe'
        native_protocol = output / 'protocol-linux'
        win_protocol = output / 'protocol-windows.exe'
        native_crypto = output / 'crypto-linux.o'
        win_crypto = output / 'crypto-windows.o'
        commands = (
            ('build-linux-codec', [*native_base, codec, codec_test, '-o', str(native_codec)]),
            ('build-win-codec', [*windows_base, '-static', codec, codec_test, '-o', str(win_codec)]),
            ('build-linux-crypto', [native_cc, '-m32', '-std=c99', '-O2', '-c', str(crypto), '-o', str(native_crypto)]),
            ('build-win-crypto', [windows_cc, '-std=c99', '-O2', '-c', str(crypto), '-o', str(win_crypto)]),
            ('build-linux-protocol', [*native_base, '-pthread', codec, protocol_test, str(native_crypto), str(linux_enet), '-o', str(native_protocol)]),
            ('build-win-protocol', [*windows_base, '-static', codec, protocol_test, str(win_crypto), str(windows_enet), '-lws2_32', '-lwinmm', '-lbcrypt', '-o', str(win_protocol)]),
        )
        for label, command in commands:
            run_step(output, env, receipt, label, command)
        if not (valid_linux_i386(native_codec) and valid_linux_i386(native_protocol) and
                valid_windows_i386(win_codec) and valid_windows_i386(win_protocol)):
            raise CheckFailure('a test binary has the wrong platform or architecture')
        receipt['binaries'] = {label: digest(path) for label, path in (
            ('linux_codec', native_codec), ('windows_codec', win_codec),
            ('linux_protocol', native_protocol), ('windows_protocol', win_protocol))}
        def isolated(command: list[str]) -> list[str]:
            return [unshare, '--user', '--map-root-user', '--net', sys.executable,
                    '-c', ISOLATED_EXEC, ip, *command]
        codec_linux = run_step(output, env, receipt, 'run-linux-codec', isolated([str(native_codec)]))
        codec_windows = run_step(output, env, receipt, 'run-win-codec', isolated([wine, str(win_codec)]))
        protocol_linux = run_step(output, env, receipt, 'run-linux-protocol', isolated([str(native_protocol)]))
        protocol_windows = run_step(output, env, receipt, 'run-win-protocol', isolated([wine, str(win_protocol)]))
        check_fixture_outcomes('codec', codec_linux, codec_windows)
        check_fixture_outcomes('protocol', protocol_linux, protocol_windows)
        receipt['status'] = 'pass'
    except (CheckFailure, OSError, UnicodeError, ValueError, subprocess.SubprocessError) as error:
        receipt['failure'] = str(error) if isinstance(error, CheckFailure) else 'test setup or execution failed'
    with (output / 'receipt.json').open('x', encoding='utf-8') as f:
        json.dump(receipt, f, indent=2, sort_keys=True)
        f.write('\n')
    if receipt['status'] != 'pass':
        print('Cross-platform fixture check failed; see the private receipt.', file=sys.stderr)
        return 1
    print('Synthetic Linux i386 and Windows i386 Wine fixtures passed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
