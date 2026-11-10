#!/usr/bin/env python3
"""Exercise lockstep voice lookup through the production path resolver.

The Ogg pages below are synthetic metadata, not audio.  The production voice
opener, loader and duration parser are extracted and compiled with the Linux
file layer.  Session activity and content-admission hooks use fixture stubs.
"""

import re
import struct
import unittest
from pathlib import Path

import files_tests


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_network_lockstep.cpp"


def definition(source, name):
    # Blank comments and literals without changing offsets, so braces in either
    # do not interfere with the small definition extractor.
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    clean = pattern.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), source)
    matches = list(re.finditer(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', clean))
    if len(matches) != 1:
        raise AssertionError(f"expected one definition of {name}")
    match = matches[0]
    start = clean.rfind('\n', 0, match.start()) + 1
    pos = clean.find('{', match.start())
    depth = 1
    end = pos + 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise AssertionError(f"unclosed definition of {name}")
    return source[start:end]


def page(payload, granule, sequence, flags):
    segments = [255] * (len(payload) // 255) + [len(payload) % 255]
    header = bytearray(struct.pack('<4sBBQIIIB', b'OggS', 0, flags, granule,
                                   7, sequence, 0, len(segments)))
    data = header + bytes(segments) + payload
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    data[22:26] = struct.pack('<I', crc)
    return bytes(data)


def synthetic_voice(seconds):
    sample_rate = 8000
    ident = (b'\x01vorbis' + struct.pack('<IBIiiiBB', 0, 1, sample_rate,
                                        0, 0, 0, 0xB8, 1))
    return (page(ident, 0, 0, 2) +
            page(b'\x00', int(sample_rate * seconds), 1, 4))


class VoicePath(files_tests.FileOverlay):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding='utf-8')
        injected = '\n'.join((
            '#include <stdint.h>',
            'static float voice_seconds;',
            'static int voice_test_active;',
            'static int CE_LockstepActive(void) { return voice_test_active; }',
            definition(source, 'GameOpen'),
            definition(source, 'VoiceLength'),
            definition(source, 'CE_LockstepVoiceFile'),
            definition(source, 'CE_LockstepVoiceSeconds'),
        ))
        driver = files_tests.DRIVER
        marker = re.search(r'\bint\s+main\s*\([^;{}]*\)\s*\{', driver)
        if marker is None:
            raise AssertionError('file driver entry point not found')
        driver = driver[:marker.start()] + injected + '\n' + driver[marker.start():]
        marker = re.search(r'\bint\s+main\s*\([^;{}]*\)\s*\{', driver)
        entry = '''
            if (argc >= 3 && (!strcmp(argv[1], "voice") ||
                              !strcmp(argv[1], "voice-inactive"))) {
                voice_test_active = !strcmp(argv[1], "voice");
                voice_seconds = 99.0f;
                CE_LockstepVoiceFile(argv[2]);
                printf("%.9g\\n", (double)CE_LockstepVoiceSeconds());
                return 0;
            }
        '''
        opens = list(re.finditer(r'\bport_fopen\s*\(', driver))
        if not opens or opens[-1].start() < marker.end():
            raise AssertionError('file driver open call not found')
        insert_at = driver.rfind('\n', 0, opens[-1].start()) + 1
        driver = driver[:insert_at] + entry + driver[insert_at:]
        original = files_tests.DRIVER
        files_tests.DRIVER = driver
        try:
            super().setUpClass()
        finally:
            files_tests.DRIVER = original

    def put_voice(self, root, seconds):
        path = root / 'Data' / 'Audio' / 'Voices' / 'LineA.ogg'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(synthetic_voice(seconds))
        if root == self.game:
            self.original = self.snapshot()

    def seconds(self, name, active=True):
        action = 'voice' if active else 'voice-inactive'
        return float(self.run_driver(action, name, cwd=self.game))

    def test_exact_path_reads_duration(self):
        self.put_voice(self.game, 1.5)
        self.assertAlmostEqual(self.seconds('Data/Audio/Voices/LineA.ogg'), 1.5)

    def test_windows_separators_and_case_read_duration(self):
        self.put_voice(self.game, 1.5)
        self.assertAlmostEqual(self.seconds(r'data\audio\voices\linea.OGG'), 1.5)

    def test_user_content_takes_precedence(self):
        self.put_voice(self.game, 1.5)
        self.put_voice(self.user, 2.5)
        self.assertAlmostEqual(self.seconds(r'data\audio\voices\linea.OGG'), 2.5)

    def test_missing_invalid_and_inactive_fall_back(self):
        self.assertEqual(self.seconds(r'data\audio\voices\missing.ogg'), 0.0)
        self.put_voice(self.game, 1.5)
        bad = self.game / 'Data' / 'Audio' / 'Voices' / 'LineA.ogg'
        bad.write_bytes(b'not an ogg stream')
        self.original = self.snapshot()
        self.assertEqual(self.seconds(r'data\audio\voices\linea.OGG'), 0.0)
        self.put_voice(self.game, 1.5)
        self.assertEqual(self.seconds('Data/Audio/Voices/LineA.ogg', active=False), 0.0)


if __name__ == '__main__':
    tests = [
        'test_exact_path_reads_duration',
        'test_windows_separators_and_case_read_duration',
        'test_user_content_takes_precedence',
        'test_missing_invalid_and_inactive_fall_back',
    ]
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.TestSuite(VoicePath(name) for name in tests))
    raise SystemExit(not result.wasSuccessful())
