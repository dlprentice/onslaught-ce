"""Exercise the conformance runner's output guard without running a game."""

import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


_SCRIPT = Path(__file__).with_name("cross_platform_conformance.py")
_SPEC = importlib.util.spec_from_file_location("conformance_guard_target", _SCRIPT)
_CONFORMANCE = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = _CONFORMANCE
_SPEC.loader.exec_module(_CONFORMANCE)


class _ReachedOutputCreation(BaseException):
    """Stop before any conformance-runner output is created."""


class ConformanceOutputGuardTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "checkout"
        self.allowed = self.root / "local-data"
        self.allowed.mkdir(parents=True)
        self.outside = self.base / "outside"
        self.outside.mkdir()
        (self.allowed / "nested").mkdir()
        self.sibling = self.root / "local-data-other"
        self.sibling.mkdir()
        (self.root / "other").mkdir()
        self.synthetic_input = self.base / "synthetic-input"
        self.synthetic_input.write_text("synthetic input\n")
        self.synthetic_input.chmod(0o700)

    def _check(self, root, output, allowed, probe_existing=False):
        placeholder = str(self.synthetic_input)
        argv = [str(_SCRIPT)]
        for name in (
            "--crypto-source", "--enet-include", "--linux-enet", "--native-cc",
            "--native-cxx", "--windows-cc", "--windows-cxx", "--windows-enet",
            "--wine",
        ):
            argv.extend((name, str(self.base) if name == "--enet-include" else placeholder))
        argv.extend(("--output", str(output)))
        text = io.StringIO()
        reached = False
        result = None
        original_mkdir = Path.mkdir

        def stop_at_output_creation(path, *args, **kwargs):
            if probe_existing:
                original_mkdir(path, *args, **kwargs)
            raise _ReachedOutputCreation
        with mock.patch.object(_CONFORMANCE, "ROOT", root), \
             mock.patch.object(sys, "argv", argv), \
             mock.patch.object(Path, "mkdir", autospec=True, side_effect=stop_at_output_creation), \
             mock.patch.object(_CONFORMANCE, 'source_file', side_effect=lambda value, label: Path(value).absolute()), \
             mock.patch("subprocess.run", side_effect=AssertionError("unexpected subprocess")), \
             contextlib.redirect_stdout(text), contextlib.redirect_stderr(text):
            try:
                result = _CONFORMANCE.main()
            except _ReachedOutputCreation:
                reached = True
        if allowed:
            self.assertTrue(reached, "accepted path must reach output creation")
        else:
            self.assertFalse(reached, "rejected path must not create output")
            self.assertIsInstance(result, int)
            self.assertNotEqual(0, result)
            self.assertIn("failed", text.getvalue().lower())

    def test_local_data_parent_and_descendant_are_allowed(self):
        for output in (self.allowed / "result", self.allowed / "nested" / "result"):
            with self.subTest():
                self._check(self.root, output, True)

    def test_outside_prefix_and_parent_traversal_are_rejected(self):
        for output in (self.outside / "result", self.sibling / "result", self.allowed / ".." / "other" / "result"):
            with self.subTest():
                self._check(self.root, output, False)

    def test_missing_output_parent_is_rejected(self):
        self._check(self.root, self.allowed / "missing-parent" / "result", False)

    def test_symlink_escape_is_rejected(self):
        link = self.allowed / "escape"
        link.symlink_to(self.outside, target_is_directory=True)
        self._check(self.root, link / "result", False)

    def test_symlink_inside_local_data_is_allowed(self):
        target = self.allowed / "target"
        target.mkdir()
        link = self.allowed / "inside"
        link.symlink_to(target, target_is_directory=True)
        self._check(self.root, link / "result", True)

    def test_existing_output_symlink_is_rejected(self):
        marker = self.outside / "untouched"
        marker.write_text("synthetic marker")
        link = self.allowed / "existing-link"
        link.symlink_to(self.outside, target_is_directory=True)
        self._check(self.root, link, False, probe_existing=True)
        self.assertEqual("synthetic marker", marker.read_text())

    def test_existing_output_directory_is_rejected(self):
        existing = self.allowed / "existing-output"
        existing.mkdir()
        self._check(self.root, existing, False, probe_existing=True)

    def test_current_checkout_local_data_is_allowed(self):
        self._check(_CONFORMANCE.ROOT, _CONFORMANCE.ROOT / "local-data" / "synthetic-output", True)

    def test_help_documents_local_data_and_bind_mount(self):
        text = io.StringIO()
        with mock.patch.object(sys, "argv", [str(_SCRIPT), "--help"]), \
             contextlib.redirect_stdout(text), contextlib.redirect_stderr(text):
            with self.assertRaises(SystemExit) as raised:
                _CONFORMANCE.main()
        self.assertEqual(0, raised.exception.code)
        self.assertIn("local-data", text.getvalue())
        self.assertIn("bind mount", text.getvalue())


if __name__ == "__main__":
    unittest.main()
