#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check fabricated fixture stdout without toolchains, subprocesses or a game."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


_SCRIPT = Path(__file__).with_name('cross_platform_conformance.py')
_SPEC = importlib.util.spec_from_file_location('conformance_completion_target', _SCRIPT)
_CONFORMANCE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_CONFORMANCE)

# These are the final lines emitted by the authored fixtures, including puts' newline.
_COMPLETION = {
    'codec': b'codec: golden endian/float bytes, every truncation, bounds, NaN/Inf and trailing data passed\n',
    'protocol': b'protocol: malformed/replay/reflection/session/epoch/chunk/expiry/queue/auth and raw ingress byte/packet/metadata bounds passed\n',
}
_PREFIX = b'fabricated preliminary check passed\n'


class ConformanceCompletionTests(unittest.TestCase):
    def _assert_invalid_output(self, fixture, invalid):
        complete = _PREFIX + _COMPLETION[fixture]
        for affected, linux, windows in (
            ('both', invalid, invalid),
            ('Linux', invalid, complete),
            ('Windows', complete, invalid),
        ):
            with self.subTest(fixture=fixture, affected=affected, invalid=invalid):
                with self.assertRaises(_CONFORMANCE.CheckFailure):
                    _CONFORMANCE.check_fixture_outcomes(fixture, linux, windows)

    def test_complete_lf_and_crlf_equivalent_outcomes_pass(self):
        for fixture, completion in _COMPLETION.items():
            for prefix in (b'', _PREFIX):
                lf = prefix + completion
                crlf = lf.replace(b'\n', b'\r\n')
                for linux, windows in ((lf, lf), (lf, crlf), (crlf, lf), (crlf, crlf)):
                    with self.subTest(fixture=fixture, linux=linux, windows=windows):
                        self.assertIsNone(_CONFORMANCE.check_fixture_outcomes(fixture, linux, windows))

    def test_identical_nonempty_incomplete_output_is_rejected(self):
        for fixture, completion in _COMPLETION.items():
            # Truncate partway through the final line, after earlier checks printed.
            partial = _PREFIX + completion[:len(completion) // 2]
            with self.subTest(fixture=fixture):
                with self.assertRaises(_CONFORMANCE.CheckFailure):
                    _CONFORMANCE.check_fixture_outcomes(fixture, partial, partial)

    def test_empty_output_is_rejected(self):
        for fixture in _COMPLETION:
            self._assert_invalid_output(fixture, b'')

    def test_missing_completion_marker_is_rejected(self):
        for fixture in _COMPLETION:
            self._assert_invalid_output(fixture, _PREFIX)

    def test_wrong_completion_marker_is_rejected(self):
        for fixture, completion in _COMPLETION.items():
            self._assert_invalid_output(fixture, _PREFIX + completion.replace(b' passed', b' failed'))
            other = 'protocol' if fixture == 'codec' else 'codec'
            self._assert_invalid_output(fixture, _PREFIX + _COMPLETION[other])

    def test_duplicate_completion_marker_is_rejected(self):
        for fixture, completion in _COMPLETION.items():
            self._assert_invalid_output(fixture, completion + _PREFIX + completion)
            self._assert_invalid_output(fixture, (completion + completion).replace(b'\n', b'\r\n'))

    def test_completion_marker_must_be_a_whole_terminated_line(self):
        for fixture, completion in _COMPLETION.items():
            for invalid in (
                _PREFIX + completion[:-1],
                _PREFIX + b'extra prefix: ' + completion,
                _PREFIX + completion[:-1] + b' extra suffix\n',
                completion + _PREFIX + b'extra prefix: ' + completion,
            ):
                self._assert_invalid_output(fixture, invalid)

    def test_completion_marker_must_be_the_final_line(self):
        for fixture, completion in _COMPLETION.items():
            self._assert_invalid_output(fixture, _PREFIX + completion + b'fabricated trailing output\n')
            self._assert_invalid_output(fixture, _PREFIX + completion + b'\n')

    def test_different_completed_platform_outcomes_are_rejected(self):
        for fixture, completion in _COMPLETION.items():
            for linux, windows in (
                (b'fabricated result A\n' + completion, b'fabricated result B\n' + completion),
                (_PREFIX + completion, _PREFIX + b'\n' + completion),
                (b'fabricated\rresult\n' + completion, b'fabricated\nresult\n' + completion),
            ):
                with self.subTest(fixture=fixture, linux=linux, windows=windows):
                    with self.assertRaises(_CONFORMANCE.CheckFailure):
                        _CONFORMANCE.check_fixture_outcomes(fixture, linux, windows)


class ConformanceMainCompletionTests(unittest.TestCase):
    def _run_main(self, outputs):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            allowed = root / 'local-data'
            allowed.mkdir()
            output = allowed / 'result'
            tool = root / 'fabricated-tool'
            tool.write_text('fabricated input\n')
            argv = [str(_SCRIPT)]
            for flag in ('native-cc', 'native-cxx', 'windows-cc', 'windows-cxx', 'wine',
                         'enet-include', 'linux-enet', 'windows-enet', 'crypto-source'):
                argv.extend(('--' + flag, str(tool)))
            argv.extend(('--output', str(output)))

            def run_step(out, env, receipt, label, command, timeout=90):
                receipt['steps'].append({'step': label, 'exit': 0, 'stderr_bytes': 0})
                return outputs.get(label, b'')

            with mock.patch.multiple(
                _CONFORMANCE,
                ROOT=root,
                executable=mock.Mock(return_value=str(tool)),
                source_file=mock.Mock(side_effect=lambda value, label: Path(value).absolute()),
                private_environment=mock.Mock(return_value={}),
                dependency_hashes=mock.Mock(return_value=[]),
                digest=mock.Mock(return_value='0' * 64),
                valid_linux_i386=mock.Mock(return_value=True),
                valid_windows_i386=mock.Mock(return_value=True),
                run_step=run_step,
            ), mock.patch.object(sys, 'argv', argv), \
                    mock.patch.object(_CONFORMANCE.os, 'umask'), \
                    mock.patch.object(_CONFORMANCE.resource, 'setrlimit'), \
                    mock.patch.object(_CONFORMANCE.subprocess, 'run',
                                      return_value=mock.Mock(returncode=0, stdout='fabricated-source-head')), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()):
                result = _CONFORMANCE.main()
            return result, json.loads((output / 'receipt.json').read_text())

    def _complete_outputs(self):
        return {
            'run-linux-codec': _COMPLETION['codec'],
            'run-win-codec': _COMPLETION['codec'].replace(b'\n', b'\r\n'),
            'run-linux-protocol': _PREFIX + _COMPLETION['protocol'],
            'run-win-protocol': (_PREFIX + _COMPLETION['protocol']).replace(b'\n', b'\r\n'),
        }

    def test_main_records_pass_for_complete_equivalent_fixture_output(self):
        result, receipt = self._run_main(self._complete_outputs())
        self.assertEqual(0, result)
        self.assertEqual('pass', receipt['status'])

    def test_main_keeps_receipt_nonpass_for_identical_incomplete_output(self):
        for fixture in _COMPLETION:
            outputs = self._complete_outputs()
            outputs['run-linux-' + fixture] = _PREFIX
            outputs['run-win-' + fixture] = _PREFIX
            with self.subTest(fixture=fixture):
                result, receipt = self._run_main(outputs)
                self.assertNotEqual(0, result)
                self.assertNotEqual('pass', receipt['status'])
                self.assertIn('completion', receipt['failure'])


if __name__ == '__main__':
    unittest.main()
