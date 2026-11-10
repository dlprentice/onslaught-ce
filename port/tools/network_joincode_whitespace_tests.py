# SPDX-License-Identifier: GPL-3.0-only
"""Exercise join-code whitespace and checksum properties using authored inputs."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix='onslaught-joincode-') as name:
        tmp = Path(name)
        commands = [
            ['gcc', '-c', '-O1', '-o', str(tmp / 'sha256.o'), str(ROOT / 'port/updater/sha256.c')],
            ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-I', str(ROOT / 'port/platform/common'), str(ROOT / 'port/tools/tests/network_joincode_whitespace_driver.cpp'), str(ROOT / 'port/platform/common/ce_netcode.cpp'), str(tmp / 'sha256.o'), '-o', str(tmp / 'whitespace-driver')],
        ]
        for command in commands:
            subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        result = subprocess.run([str(tmp / 'whitespace-driver')], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
        values = json.loads(result.stdout)
        if values['valid'] != 300 or not values['single_checks'] or not values['swap_checks']:
            raise AssertionError('the authored fixture did not exercise the required cases')
        print('Join-code whitespace and checksum properties passed')

if __name__ == '__main__':
    main()
