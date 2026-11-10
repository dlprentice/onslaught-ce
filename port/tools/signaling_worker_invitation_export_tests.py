#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored fake-HTTPS and real native metadata-parser controls for pair export."""
from __future__ import annotations

import argparse
from contextlib import ExitStack, redirect_stderr, redirect_stdout
import copy
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import stat
import subprocess
import unittest
from unittest.mock import patch

import signaling_api_contract as api
import signaling_api_contract_tests as http_fixture
import signaling_pair_bindings as bindings
import signaling_session_store as service
import signaling_worker_invitation_export as export
import network_invitation_tests as native_fixture


ROOT = Path(__file__).resolve().parents[2]
KEY = bytes([49]) * 32
NOW = 1000


class Clock:
    def __init__(self):
        self.now = NOW

    def __call__(self):
        return self.now


class CurrentOnly:
    def __init__(self, pair, clock):
        self.pair, self.clock = pair, clock
        self.verifier = bindings.PairCapabilityVerifier(KEY, maximum_seconds=900)
        self.calls = []
        self.overrides = {}
        self.after = None

    def dispatch(self, route, opaque_token, body):
        assert route == "pair/current"
        assert json.loads(body) == {}
        principal = self.verifier.verify(opaque_token, self.clock())
        role = "host" if principal.subject == self.pair["host"] else "guest"
        assert principal.subject == self.pair[role]
        self.calls.append(role)
        fields = dict(role=role, approval=self.pair["approval"], pair_expires_at=self.pair["expiresAt"])
        changes = self.overrides.get(role, {})
        status = changes.get("status", "awaiting_pair_consent")
        fields.update({k: v for k, v in changes.items() if k != "status"})
        reply = service.Reply(status, **fields)
        if self.after:
            self.after(role)
        return reply


class ExportTests(unittest.TestCase):
    output: Path
    native_parser: Path

    def setUp(self):
        self.folder = self.output / self._testMethodName
        self.folder.mkdir(mode=0o700)
        self.pair = dict(schema=1, approval="a" * 32, host="p_" + "b" * 32 + "_host",
                         guest="p_" + "c" * 32 + "_guest", issuedAt=NOW, expiresAt=NOW + 900)
        self.config = self.folder / "pair.json"
        self.key = self.folder / "admission.key"
        self.key.write_bytes(KEY); self.key.chmod(0o600)
        self.write_pair()
        self.clock = Clock()
        self.backend = CurrentOnly(self.pair, self.clock)
        self.factory = http_fixture.ConnectionFactory(api.Api(self.backend))
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(api.http.client, "HTTPSConnection", self.factory))
        self.stack.enter_context(patch.object(socket, "socket", side_effect=AssertionError("no real socket")))
        self.stack.enter_context(patch.object(socket, "create_connection", side_effect=AssertionError("no real network")))
        self.stack.enter_context(patch.object(socket, "getaddrinfo", side_effect=AssertionError("no real DNS")))
        for name in ("accept", "close_pair", "stage"):
            self.stack.enter_context(patch.object(api.Client, name, side_effect=AssertionError("Current only")))
        self.stack.enter_context(patch.object(Path, "home", side_effect=AssertionError("no home/profile lookup")))

    def write_pair(self, value=None):
        self.config.write_text(json.dumps(self.pair if value is None else value))
        self.config.chmod(0o600)

    def paths(self):
        parent = self.folder / "new-pair"
        return parent / "host", parent / "guest"

    def run_export(self, **changes):
        host, guest = self.paths()
        options = dict(pair_config=self.config, origin="https://authored.invalid", host_output=host,
                       guest_output=guest, admission_key=self.key, maximum_seconds=900, clock=self.clock)
        options.update(changes)
        export.export_worker(**options)

    def assert_unpublished(self):
        self.assertFalse((self.folder / "new-pair").exists())
        self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def assert_refused(self, **options):
        with self.assertRaises(Exception):
            self.run_export(**options)
        self.assert_unpublished()

    def parse_native(self, path, role):
        result = subprocess.run([str(self.native_parser), str(path), str(1 if role == "host" else 2)],
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=5, env={"PATH": os.environ.get("PATH", "/usr/bin"),
                                                "HOME": str(self.folder), "TMPDIR": str(self.folder)})
        return result.returncode

    def run_cli(self, host=None, guest=None):
        default_host, default_guest = self.paths()
        host, guest = host or default_host, guest or default_guest
        stdout, stderr = io.StringIO(), io.StringIO()
        original = export.export_worker
        with patch.object(export, "export_worker", side_effect=lambda *a, **kw: original(*a, **dict(kw, clock=self.clock))):
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = export.main(["--pair-config", str(self.config), "--service-origin", "https://authored.invalid",
                    "--host-output", str(host), "--guest-output", str(guest), "--admission-key", str(self.key),
                    "--maximum-seconds", "900"])
        return result, stdout.getvalue() + stderr.getvalue()

    def invitation_snapshot(self, directory):
        return {str(path.relative_to(directory)): path.read_bytes()
                for role in ("host", "guest") for path in (directory / role).iterdir()}

    def test_current_only_export_matches_real_native_parser_and_signed_claims(self):
        self.backend.after = lambda role: self.assert_unpublished()
        self.run_export()
        self.assertEqual(["host", "guest"], self.backend.calls)
        verifier = bindings.PairCapabilityVerifier(KEY, maximum_seconds=900)
        for role, directory in zip(("host", "guest"), self.paths()):
            self.assertEqual(0o700, stat.S_IMODE(directory.stat().st_mode))
            token_path, metadata_path = directory / export.TOKEN_FILE, directory / export.ADMISSION_FILE
            token = token_path.read_bytes()
            principal = verifier.verify(token, NOW)
            self.assertEqual(self.pair[role], principal.subject)
            self.assertEqual(NOW, principal.issued_at)
            self.assertEqual(self.pair["expiresAt"], principal.expires_at)
            self.assertFalse(token.endswith(b"\n"))
            expected = dict(schema=1, approval=self.pair["approval"], role=role, expiresAt=self.pair["expiresAt"])
            self.assertEqual(expected, json.loads(metadata_path.read_bytes()))
            self.assertEqual(0, self.parse_native(metadata_path, role))
            for path in (token_path, metadata_path):
                self.assertEqual(0o600, stat.S_IMODE(path.stat().st_mode))
            self.assertEqual(3, self.parse_native(metadata_path, "guest" if role == "host" else "host"))
        for connection in self.factory.connections:
            self.assertEqual("authored.invalid", connection.host)
            self.assertTrue(connection.options["context"].check_hostname)
            self.assertEqual(ssl.CERT_REQUIRED, connection.options["context"].verify_mode)
            self.assertEqual(api.PREFIX + "pair/current", connection.path)
            self.assertEqual("POST", connection.method)

    def test_old_five_field_export_is_rejected_by_the_native_parser(self):
        verifier = bindings.PairCapabilityVerifier(KEY, maximum_seconds=900)
        members = {}
        for role in ("host", "guest"):
            members[role] = bindings.RoleAdmission(self.pair["approval"], self.pair[role], role, NOW,
                self.pair["expiresAt"], verifier.mint(self.pair[role], role, NOW, self.pair["expiresAt"]))
        old = self.folder / "legacy-host"
        old.mkdir(mode=0o700)
        bindings.export_role(bindings.PairAdmission(members["host"], members["guest"]), "host", old, self.clock)
        metadata = old / bindings.ADMISSION_FILE
        self.assertEqual({"schema", "approval", "role", "issuedAt", "expiresAt"}, set(json.loads(metadata.read_bytes())))
        self.assertEqual(3, self.parse_native(metadata, "host"))
        self.run_export()
        self.assertEqual(0, self.parse_native(self.paths()[0] / export.ADMISSION_FILE, "host"))

    def test_second_current_mismatch_never_publishes_the_first_role(self):
        cases = (dict(approval="d" * 32), dict(role="host"), dict(pair_expires_at=NOW + 899),
                 dict(status="closed", approval=None, pair_expires_at=None),
                 dict(status="unavailable", approval=None, pair_expires_at=None))
        for changes in cases:
            with self.subTest(fields=sorted(changes)):
                self.backend.overrides = {"guest": changes}
                self.backend.calls.clear()
                self.assert_refused()
                self.assertEqual(["host", "guest"], self.backend.calls)

    def test_invalid_pair_fields_times_duplicates_and_private_modes_refuse_before_https(self):
        original = copy.deepcopy(self.pair)
        cases = [dict(original, schema=True), dict(original, approval="not-an-approval"),
                 dict(original, host=original["guest"]), dict(original, issuedAt=NOW + 1),
                 dict(original, expiresAt=NOW), dict(original, expiresAt=NOW + 901),
                 dict(original, expiresAt=True), dict(original, context="e" * 64),
                 {k: v for k, v in original.items() if k != "guest"}]
        for value in cases:
            with self.subTest(fields=sorted(value)):
                self.write_pair(value)
                self.assert_refused()
        self.config.write_text('{"schema":1,"schema":1}')
        self.assert_refused()
        self.write_pair(); self.config.chmod(0o644)
        self.assert_refused()
        self.config.chmod(0o600)
        for data in (b"", KEY[:-1], KEY + b"x", KEY.hex().encode()):
            self.key.write_bytes(data)
            self.assert_refused()
        self.key.write_bytes(KEY); self.key.chmod(0o644)
        self.assert_refused()
        self.assertEqual([], self.factory.connections)

    def test_fixed_origin_and_output_collision_refuse_without_connecting_or_overwriting(self):
        for origin in ("http://authored.invalid", "https://user:private-canary@authored.invalid",
                       "https://authored.invalid/path", "https://authored.invalid?query=1"):
            self.assert_refused(origin=origin)
        parent = self.folder / "new-pair"
        parent.mkdir(mode=0o700)
        keep = parent / "keep"; keep.write_bytes(b"authored retained file")
        with self.assertRaises(Exception):
            self.run_export()
        self.assertEqual(b"authored retained file", keep.read_bytes())
        self.assertEqual(["keep"], sorted(p.name for p in parent.iterdir()))
        self.assertEqual([], self.factory.connections)

    def test_no_partial_export_on_write_failure_or_commit_collision(self):
        original = export.write_private
        count = 0
        def fail_third(directory, name, data):
            nonlocal count
            count += 1
            if count == 3:
                raise OSError("authored write failure")
            original(directory, name, data)
        with patch.object(export, "write_private", side_effect=fail_third):
            self.assert_refused()
        self.assertEqual(3, count)
        def collide(base, temporary, final):
            os.mkdir(final, mode=0o700, dir_fd=base)
            (self.folder / final / "keep").write_bytes(b"authored collision sentinel")
            raise FileExistsError()
        with patch.object(export.wire.private, "_publish_no_replace", side_effect=collide):
            with self.assertRaises(Exception):
                self.run_export()
        self.assertEqual(["keep"], [p.name for p in (self.folder / "new-pair").iterdir()])
        self.assertEqual(b"authored collision sentinel", (self.folder / "new-pair/keep").read_bytes())
        self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def test_publish_then_raise_preserves_complete_output_and_fixed_diagnostics(self):
        original = export.wire.private._publish_no_replace
        for label, exception in (("runtime", RuntimeError), ("interrupt", KeyboardInterrupt)):
            with self.subTest(failure=label):
                parent = self.folder / ("published-" + label)
                expected = {}
                def publish_then_raise(base, temporary, final):
                    expected.update(self.invitation_snapshot(self.folder / temporary))
                    original(base, temporary, final)
                    raise exception("private-error-canary " + expected["host/identity-token"].decode())
                with patch.object(export.wire.private, "_publish_no_replace", side_effect=publish_then_raise):
                    result, diagnostics = self.run_cli(parent / "host", parent / "guest")
                self.assertEqual(1, result)
                self.assertIn("preserve", diagnostics)
                self.assertNotIn("private-error-canary", diagnostics)
                actual = self.invitation_snapshot(parent)
                self.assertEqual({"host/identity-token", "host/pair-admission.json", "guest/identity-token", "guest/pair-admission.json"}, set(actual))
                self.assertTrue(all(actual[name] == expected[name] for name in expected), "published files changed")
                self.assertFalse(any(data.decode() in diagnostics for name, data in expected.items() if name.endswith("identity-token")))
                self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def test_before_rename_exception_cleans_only_its_own_private_staging(self):
        sentinel = self.folder / "keep"
        sentinel.write_bytes(b"authored retained data")
        inputs = self.config.read_bytes(), self.key.read_bytes(), sentinel.read_bytes()
        with patch.object(export.wire.private, "_publish_no_replace", side_effect=RuntimeError("private-error-canary")):
            result, diagnostics = self.run_cli()
        self.assertEqual(1, result)
        self.assertNotIn("private-error-canary", diagnostics)
        self.assert_unpublished()
        self.assertTrue(inputs == (self.config.read_bytes(), self.key.read_bytes(), sentinel.read_bytes()))

    def test_mkdir_open_failures_leave_no_staging_and_close_owned_descriptors(self):
        sentinel = self.folder / "keep"
        sentinel.write_bytes(b"authored retained data")
        inputs = self.config.read_bytes(), self.key.read_bytes(), sentinel.read_bytes()
        original = os.open
        for phase in ("stage", "host", "guest"):
            with self.subTest(directory=phase):
                faulted = False
                before = len(os.listdir("/proc/self/fd"))
                def fail_open(path, flags, *args, **options):
                    nonlocal faulted
                    name = os.fspath(path)
                    selected = name.startswith(".worker-invitation-") if phase == "stage" else name == phase
                    if not faulted and options.get("dir_fd") is not None and flags & os.O_DIRECTORY and selected:
                        faulted = True
                        raise OSError(errno.EMFILE, "authored directory open failure")
                    return original(path, flags, *args, **options)
                with patch.object(export.os, "open", side_effect=fail_open):
                    result, _ = self.run_cli()
                self.assertTrue(faulted)
                self.assertEqual(1, result)
                self.assert_unpublished()
                self.assertEqual(before, len(os.listdir("/proc/self/fd")))
                self.assertTrue(inputs == (self.config.read_bytes(), self.key.read_bytes(), sentinel.read_bytes()))

    def test_moved_staging_with_unproven_destination_is_never_rolled_back(self):
        original = export.wire.private._publish_no_replace
        expected = {}
        def move_elsewhere(base, temporary, final):
            expected.update(self.invitation_snapshot(self.folder / temporary))
            original(base, temporary, "moved-pair")
            raise RuntimeError("private-error-canary")
        with patch.object(export.wire.private, "_publish_no_replace", side_effect=move_elsewhere):
            result, diagnostics = self.run_cli()
        self.assertEqual(1, result)
        self.assertIn("preserve", diagnostics)
        self.assertNotIn("private-error-canary", diagnostics)
        self.assertFalse((self.folder / "new-pair").exists())
        actual = self.invitation_snapshot(self.folder / "moved-pair")
        self.assertTrue(set(actual) == set(expected) and all(actual[name] == expected[name] for name in expected), "moved files changed")
        self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def test_delayed_current_expiry_and_backwards_clock_publish_nothing(self):
        for changed in (NOW + 900, NOW - 1):
            self.clock.now = NOW
            self.backend.after = lambda role, changed=changed: setattr(self.clock, "now", changed) if role == "guest" else None
            self.assert_refused()
        self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def test_expiry_during_staging_does_not_publish_either_role(self):
        original = export.write_private
        count = 0
        def expire_after_writes(directory, name, data):
            nonlocal count
            original(directory, name, data)
            count += 1
            if count == 4:
                self.clock.now = self.pair["expiresAt"]
        with patch.object(export, "write_private", side_effect=expire_after_writes):
            self.assert_refused()
        self.assertEqual(4, count)

    def test_postrename_sync_failure_preserves_both_complete_invitations(self):
        original = os.fsync
        identity = self.folder.stat()
        def fail_parent_sync(fd):
            selected = os.fstat(fd)
            if (selected.st_dev, selected.st_ino) == (identity.st_dev, identity.st_ino):
                raise OSError("authored parent sync failure")
            original(fd)
        with patch.object(export.os, "fsync", side_effect=fail_parent_sync):
            with self.assertRaises(export.PublicationUnverified):
                self.run_export()
        for directory in self.paths():
            self.assertEqual({export.TOKEN_FILE, export.ADMISSION_FILE}, {p.name for p in directory.iterdir()})
        self.assertFalse(list(self.folder.glob(".worker-invitation-*")))

    def test_private_key_pipe_is_supported_without_closing_the_callers_descriptor(self):
        read, write = os.pipe()
        try:
            os.write(write, KEY); os.close(write); write = -1
            self.run_export(admission_key=None, admission_key_fd=read)
            os.fstat(read)
            self.assertEqual(["host", "guest"], self.backend.calls)
        finally:
            os.close(read)
            if write >= 0:
                os.close(write)

    def test_cli_uses_only_explicit_private_inputs_and_logs_no_capabilities(self):
        host, guest = self.paths()
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(export, "export_worker", wraps=export.export_worker) as call:
            # Pass the clock explicitly through the real entrypoint's library call.
            real = call._mock_wraps
            call.side_effect = lambda *a, **kw: real(*a, **dict(kw, clock=self.clock))
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = export.main(["--pair-config", str(self.config), "--service-origin", "https://authored.invalid",
                    "--host-output", str(host), "--guest-output", str(guest), "--admission-key", str(self.key),
                    "--maximum-seconds", "900"])
        self.assertEqual(0, result)
        diagnostics = stdout.getvalue() + stderr.getvalue()
        self.assertNotIn(KEY.hex(), diagnostics)
        self.assertNotIn(self.pair["approval"], diagnostics)
        for directory in (host, guest):
            self.assertNotIn((directory / export.TOKEN_FILE).read_text(), diagnostics)
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            self.assertEqual(1, export.main(["--not-an-option", "private-canary-never-echo"]))
        self.assertNotIn("private-canary-never-echo", stdout.getvalue() + stderr.getvalue())


def native_parser(output: Path) -> Path:
    compiler = shutil.which("g++")
    if not compiler:
        raise RuntimeError("native consumer cross-format control requires g++")
    # Reuse the accepted component fixture's platform stubs; retain the real parser source.
    mirror = output / "native-source"
    game, network, common, stubs = (mirror / "game", mirror / "network", mirror / "platform/common", mirror / "stubs")
    for directory in (game, network, common, stubs):
        directory.mkdir(parents=True)
    shutil.copyfile(ROOT / "port/game/ce_network_invitation.cpp", game / "ce_network_invitation.cpp")
    for path in (ROOT / "port/game").glob("*.h"):
        shutil.copyfile(path, game / path.name)
    for path in (ROOT / "port/network").glob("*.h"):
        shutil.copyfile(path, network / path.name)
    for path in (ROOT / "port/platform/common").glob("*.h"):
        if path.name != "platform.h":
            shutil.copyfile(path, common / path.name)
    shutil.copyfile(ROOT / "port/platform/common/private_ui_input.cpp", common / "private_ui_input.cpp")
    (mirror / "updater").mkdir()
    shutil.copyfile(ROOT / "port/updater/sha256.h", mirror / "updater/sha256.h")
    (common / "platform.h").write_text(native_fixture.PLATFORM)
    (stubs / "SDL.h").write_text(native_fixture.SDL)
    source = output / "native-metadata.cpp"
    source.write_text("""// SPDX-License-Identifier: GPL-3.0-only
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
""" + '#include ' + json.dumps(str(game / "ce_network_invitation.cpp")) + "\n" + """
int main(int argc,char** argv) {
  if(argc!=3) return 2;
  std::ifstream input(argv[1],std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
  if(data.empty()) return 2;
  CeIceSignalPairBinding value={};
  return ParseMetadata(data.data(),data.size(),static_cast<uint32_t>(std::strtoul(argv[2],0,10)),&value)?0:3;
}
""")
    binary = output / "native-metadata"
    with (output / "native-compile.private.log").open("wb") as log:
        result = subprocess.run([compiler, "-std=c++17", "-DCE_NETWORK_ICE=1", "-I", str(stubs), "-I", str(common),
                                 "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                                 str(source), str(common / "private_ui_input.cpp"), "-o", str(binary)],
                                stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, timeout=60,
                                env={"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": str(output), "TMPDIR": str(output)})
    if result.returncode:
        raise RuntimeError("native consumer cross-format fixture did not compile; private log retained")
    return binary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    output = parser.parse_args().output.absolute()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    ExportTests.output, ExportTests.native_parser = output, native_parser(output)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ExportTests))
    receipt = dict(tests=result.testsRun, failures=len(result.failures), errors=len(result.errors),
                   skipped=len(result.skipped), real_network_calls=0, provider_calls=0, consent_calls=0,
                   stage_calls=0, real_keys=False, game_attestation=False,
                   native_consumer_sha256=hashlib.sha256((ROOT / "port/game/ce_network_invitation.cpp").read_bytes()).hexdigest(),
                   native_input_unit_sha256=hashlib.sha256((ROOT / "port/platform/common/private_ui_input.cpp").read_bytes()).hexdigest())
    (output / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if result.wasSuccessful() and not result.skipped else 1


if __name__ == "__main__":
    raise SystemExit(main())
