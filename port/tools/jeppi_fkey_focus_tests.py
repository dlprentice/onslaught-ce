#!/usr/bin/env python3
"""Check F1-F3 key release across focus changes in the synthetic input fixture.

This does not launch the game or exercise its pause menu or camera controls.
The mismatched-release case must be rejected by the fixture.
"""

import ast
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER_RELATIVE = 'port/tools/input_lifetime_tests.py'
FIXTURE_RELATIVE = 'port/tools/tests/input_lifetime_driver.cpp'
DRIVER = ROOT / DRIVER_RELATIVE
FIXTURE = ROOT / FIXTURE_RELATIVE
CPP_NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
PASS = re.compile(r'\bPASS(?:ED)?\b', re.I)
FAIL = re.compile(r'\bFAIL(?:ED|URE)?\b', re.I)
SKIP = re.compile(r'\bSKIP(?:PED)?\b', re.I)


def fixture_for(first, second):
    source = FIXTURE.read_text(encoding='utf-8')
    masked = CPP_NON_CODE.sub(
        lambda match: ''.join('\n' if c == '\n' else ' ' for c in match.group()),
        source,
    )
    events = list(re.finditer(r'\bSDL_SCANCODE_A\b', masked))
    if len(events) != 2:
        raise RuntimeError('input fixture key scenario changed')
    first_context = masked[max(0, events[0].start() - 250):events[0].start()]
    second_context = masked[max(0, events[1].start() - 250):events[1].start()]
    between = masked[events[0].start():events[1].start()]
    lost = between.find('SDL_EVENT_WINDOW_FOCUS_LOST')
    gained = between.find('SDL_EVENT_WINDOW_FOCUS_GAINED')
    if ('SDL_EVENT_KEY_DOWN' not in first_context or
            'SDL_EVENT_KEY_UP' not in second_context or lost < 0 or gained <= lost or
            between.count('SDL_EVENT_WINDOW_FOCUS_LOST') != 1 or
            between.count('SDL_EVENT_WINDOW_FOCUS_GAINED') != 1 or
            between.count('SDL_PushEvent') < 2):
        raise RuntimeError('input fixture no longer has the focus-change key scenario')
    for event, key in reversed(list(zip(events, (first, second)))):
        source = source[:event.start()] + key + source[event.end():]
    return source


def run_driver(fixture):
    tree = ast.parse(DRIVER.read_text(encoding='utf-8'), filename=str(DRIVER))

    class SelectFixture(ast.NodeTransformer):
        count = 0

        def visit_Constant(self, node):
            if isinstance(node.value, str) and node.value == FIXTURE_RELATIVE:
                self.count += 1
                return ast.copy_location(ast.Constant(str(fixture)), node)
            return node

    selector = SelectFixture()
    tree = selector.visit(tree)
    if selector.count != 1:
        raise RuntimeError('input driver fixture selection changed')
    ast.fix_missing_locations(tree)
    sys.argv = [str(DRIVER)]
    namespace = {'__name__': '__main__', '__file__': str(DRIVER)}
    exec(compile(tree, str(DRIVER), 'exec'), namespace)


def run_case(directory, label, first, second, negative=False):
    fixture = directory / (label + '.cpp')
    fixture.write_text(fixture_for(first, second), encoding='utf-8')
    home = directory / (label + '-home')
    home.mkdir(mode=0o700)
    if os.name == 'posix':
        home.chmod(0o700)
    environment = os.environ.copy()
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        environment.pop(key, None)
    environment.update({
        'HOME': str(home),
        'XDG_CONFIG_HOME': str(home / 'config'),
        'XDG_DATA_HOME': str(home / 'data'),
        'XDG_CACHE_HOME': str(home / 'cache'),
        'XDG_RUNTIME_DIR': str(home),
        'BEA_USER_DIR': str(home / 'ce-user'),
        'BEA_REPORT_NO_OPEN': '1',
        'BROWSER': '/bin/true',
        'SDL_VIDEODRIVER': 'dummy',
        'SDL_AUDIODRIVER': 'dummy',
    })
    stdout_path = directory / (label + '.stdout')
    stderr_path = directory / (label + '.stderr')
    try:
        with stdout_path.open('wb') as stdout, stderr_path.open('wb') as stderr:
            result = subprocess.run(
                [sys.executable, str(Path(__file__).resolve()), '--fixture', str(fixture)],
                cwd=ROOT, env=environment, stdout=stdout, stderr=stderr,
                timeout=90, check=False,
            )
    except subprocess.TimeoutExpired:
        print('FAIL: ' + label + ' test timed out')
        return False
    if stdout_path.stat().st_size + stderr_path.stat().st_size > 1024 * 1024:
        print('FAIL: ' + label + ' produced excessive output')
        return False
    lines = (stdout_path.read_text(encoding='utf-8', errors='replace') + '\n' +
             stderr_path.read_text(encoding='utf-8', errors='replace')).splitlines()
    passed = sum(bool(PASS.search(line)) for line in lines)
    failed = [line for line in lines if FAIL.search(line)]
    skipped = any(SKIP.search(line) for line in lines)
    if negative:
        ok = (result.returncode != 0 and passed > 0 and not skipped and
              any('release' in line.lower() for line in failed))
    else:
        ok = result.returncode == 0 and passed > 0 and not failed and not skipped
    print(('PASS: ' if ok else 'FAIL: ') + label)
    return ok


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--fixture':
        run_driver(Path(sys.argv[2]))
        return 0
    if len(sys.argv) != 1:
        print('Usage: python3 jeppi_fkey_focus_tests.py')
        return 2
    with tempfile.TemporaryDirectory(prefix='ce-fkey-focus-') as temp:
        directory = Path(temp)
        cases = (
            ('F1 focus-change release', 'SDL_SCANCODE_F1', 'SDL_SCANCODE_F1', False),
            ('F2 focus-change release', 'SDL_SCANCODE_F2', 'SDL_SCANCODE_F2', False),
            ('F3 focus-change release', 'SDL_SCANCODE_F3', 'SDL_SCANCODE_F3', False),
            ('mismatched release is rejected', 'SDL_SCANCODE_F1', 'SDL_SCANCODE_F2', True),
        )
        for case in cases:
            if not run_case(directory, *case):
                return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
