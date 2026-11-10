#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fake host/guest orchestration only: no VM, SSH, game, provider or native process."""
from __future__ import annotations

import ast
import contextlib
import copy
import importlib.util
import json
import os
import sys
from pathlib import Path
import struct
import io
import tarfile
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace as NS
from unittest import mock
from unittest.mock import patch as owned_patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("ice_native_gate", HERE / "ice_windows_native_gate.py")
assert spec and spec.loader
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def metadata() -> dict:
    return {"schema": 1, "run_id": "ice-" + "1" * 12 + "-fixture", "candidate": {"commit": "1" * 40},
        "native_manifest_sha256": "2" * 64, "inactive_domain_sha256": "3" * 64, "guest_user": "fixture"}


def done(meta: dict) -> dict:
    return {"schema": 1, "run": meta["run_id"], "source_commit": meta["candidate"]["commit"],
        "manifest_sha256": meta["native_manifest_sha256"], "standard_user": True, "failure": 0, "inputs_unchanged": True,
        "ack_control": [], "cases": [{"name": name, "pid": 100 + i, "creation_time": 100000 + i, "exit": 0, "forced": False,
            "no_forbidden_output": True, "summary_valid": True, "identity_confirmed": True, "cleanup_confirmed": True, "output_complete": True,
                       "infrastructure_failure": 0, "checks": None if name == "closed" else 7, "stdout_bytes": 42, "stderr_bytes": 0,
            "stdout_sha256": "4" * 64, "stderr_sha256": gate.digest(b""),
            "job_completion": {"query_ok": True, "query_error": 0, "first_active_processes": 1, "active_processes": 0,
                               "queries": 2, "elapsed_ms": 31, "deadline_ms": 90000}} for i, name in enumerate(gate.ROLES)]}


def failed_stream(value: bytes) -> dict:
    return {"bytes": len(value), "sha256": gate.digest(value), "retained_bytes": min(len(value), gate.FAILED_OUTPUT_BYTES),
            "hex": value[-gate.FAILED_OUTPUT_BYTES:].hex()}


def failed_receipt(meta: dict, out: bytes, err: bytes) -> tuple[dict, dict]:
    """A grant failure and the matching evidence, as the supervisor writes them."""
    receipt = done(meta)
    receipt.update(failure=74, inputs_unchanged=False, cases=receipt["cases"][:2])
    receipt["cases"][1].update(exit=1, summary_valid=False, checks=None, stdout_bytes=len(out), stderr_bytes=len(err),
                               stdout_sha256=gate.digest(out), stderr_sha256=gate.digest(err))
    return receipt, {"schema": 1, "run": meta["run_id"], "manifest_sha256": meta["native_manifest_sha256"], "name": "grant",
                     "stdout": failed_stream(out), "stderr": failed_stream(err)}


class FakeBackend:
    def __init__(self, meta: dict, fail: str | None = None, native: dict | None = None, failed_case: dict | None = None):
        self.meta, self.fail, self.native, self.failed_case = meta, fail, native or done(meta), failed_case
        self.events: list[str] = []
        self.root_owned = False
        self.deadline = None

    def event(self, name: str):
        self.events.append(name)
        if self.fail == name:
            raise gate.GateError("fabricated-" + name)

    @contextlib.contextmanager
    def locks(self):
        acquired = []
        try:
            for name in ("heavy", "gpu", "vm"):
                self.event("lock-" + name); acquired.append(name)
            yield
        finally:
            for name in reversed(acquired): self.event("unlock-" + name)

    def preflight(self, meta): self.event("preflight")
    def start_vm(self): self.event("start-vm")
    def wait_guest(self, seconds):
        assert seconds == 180
        self.event("wait-guest")
    def stage(self, prepared, meta, receipts):
        self.event("stage")
        self.root_owned = True
    def ack_control(self): self.event("ack-control")
    def create_task(self): self.event("create-task")
    def start_task(self): self.event("start-task")
    def read_json(self, name, seconds):
        assert seconds == 2 or (seconds == (30 if name == "ready" else 390))
        self.event("read-" + name)
        if name == "ready":
            return {"schema": 1, "run": self.meta["run_id"], "manifest_sha256": self.meta["native_manifest_sha256"], "standard_user": True, "windows_build": 26100}
        if name == "failed-case":
            if self.failed_case is None: raise gate.GateError("native-receipt-deadline")
            return self.failed_case
        return self.native
    def acknowledge(self): self.event("ack")
    def wait_task_exit(self, seconds):
        assert seconds == 10
        self.event("task-exited")
    def readback(self, meta, *, best_effort=False):
        self.event("failure-readback" if best_effort else "readback")
        return {"complete": True, "files": []}
    def finish_task(self, force): self.event("end-task-forced" if force else "end-task-normal")
    def stop_vm(self, seconds):
        assert seconds == 90
        self.event("stop-vm")



def fake_toolchain(root):
    def pin(path):
        return {"path": str(path), "sha256": gate.digest(path.read_bytes())}
    manifest = root / "fake-toolchain-manifest.json"
    verifier = root / "fake-verifier.txt"
    receipt = root / "fake-toolchain-receipt.json"
    verifier.write_bytes(b"fabricated verifier identity, never executed")
    roots = [str(root / "fake-legacy"), str(root / "fake-canonical")]
    manifest.write_bytes(gate.json_bytes({"archive_sha256": "a" * 64, "roots": roots, "mismatches": [], "extras": [],
                         "entries": [{"installations": [{"matches_archive": True}, {"matches_archive": True}]}]}))
    receipt.write_bytes(gate.json_bytes({"schema": 1, "kind": "archive-installation-identity", "verified": True,
        "mismatch_count": 0, "extra_count": 0, "entry_count": 1, "roots": roots, "manifest": pin(manifest),
        "verifier": pin(verifier), "archive": {"sha256": "a" * 64}, "version": "fabricated-toolchain"}))
    return pin(receipt)


def fake_pe(subsystem: int = 3, imported: str | None = None) -> bytes:
    data = bytearray(1024)
    data[:2] = b"MZ"; struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"; struct.pack_into("<H", data, 132, 0x14C)
    struct.pack_into("<H", data, 134, 1); struct.pack_into("<H", data, 148, 224)
    struct.pack_into("<H", data, 152, 0x10B); struct.pack_into("<H", data, 220, subsystem)
    struct.pack_into("<I", data, 244, 16)
    struct.pack_into("<IIII", data, 384, 512, 4096, 512, 512)
    if imported:
        struct.pack_into("<I", data, 256, 4096)
        struct.pack_into("<I", data, 524, 4196)
        data[612:612 + len(imported) + 1] = imported.encode("ascii") + b"\0"
    return bytes(data)


class BoundedCommandOwnershipTests(unittest.TestCase):
    """Host-only ownership faults: each fake wait records whether the child was reaped."""

    def fixture(self, *, data=False):
        process = mock.Mock()
        process.returncode = None
        process.stdin = mock.Mock() if data else None
        process.stdout, process.stderr = mock.Mock(), mock.Mock()
        resources = [x for x in (process.stdin, process.stdout, process.stderr) if x is not None]
        for index, stream in enumerate(resources):
            stream.fileno.return_value = 100 + index
        def reaped(*, timeout):
            process.returncode = -15
            return -15
        process.wait.side_effect = reaped
        selector = mock.Mock()
        selector.get_map.return_value = {1: object()}
        selector.select.side_effect = OSError("authored poll refusal")
        return process, selector, resources

    @contextlib.contextmanager
    def installed(self, process, selector):
        with mock.patch.object(gate.subprocess, "Popen", return_value=process) as spawn, \
             mock.patch.object(gate.selectors, "DefaultSelector", return_value=selector) as construct, \
             mock.patch.object(gate.os, "set_blocking") as blocking:
            yield NS(spawn=spawn, construct=construct, blocking=blocking)

    def assert_stopped(self, process, resources):
        process.terminate.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=2)
        self.assertIsNotNone(process.returncode)
        process.kill.assert_not_called()
        for stream in resources:
            stream.close.assert_called_once_with()

    def test_setup_faults_reap_the_spawned_child(self):
        for phase in ("construct", "stdout", "stderr", "stdin-mode", "stdin-register"):
            with self.subTest(phase=phase):
                process, selector, streams = self.fixture(data=True)
                failure = OSError("authored setup refusal")
                with self.installed(process, selector) as owner:
                    if phase == "construct": owner.construct.side_effect = failure
                    elif phase == "stdout": selector.register.side_effect = failure
                    elif phase == "stderr": selector.register.side_effect = [None, failure]
                    elif phase == "stdin-mode": owner.blocking.side_effect = failure
                    else: selector.register.side_effect = [None, None, failure]
                    with self.assertRaises(OSError) as caught:
                        gate.bounded_command(["authored-child"], data=b"authored input")
                self.assertIs(caught.exception, failure)
                self.assert_stopped(process, streams)
                self.assertEqual(0 if phase == "construct" else 1, selector.close.call_count)

    def test_polling_faults_and_interrupts_reap_the_spawned_child(self):
        for kind in (OSError, ValueError, KeyboardInterrupt, SystemExit, MemoryError):
            with self.subTest(kind=kind.__name__):
                process, selector, streams = self.fixture()
                failure = kind("authored loop refusal")
                selector.select.side_effect = failure
                with self.installed(process, selector), self.assertRaises(kind) as caught:
                    gate.bounded_command(["authored-child"])
                self.assertIs(caught.exception, failure)
                self.assert_stopped(process, streams)
                selector.close.assert_called_once_with()

    def test_pipe_read_write_and_unregister_faults_reap(self):
        for operation in ("read", "write", "unregister"):
            with self.subTest(operation=operation):
                process, selector, streams = self.fixture(data=operation == "write")
                failure = OSError("authored pipe refusal")
                stream = process.stdin if operation == "write" else process.stdout
                key = NS(data=2 if operation == "write" else 0, fd=100, fileobj=stream)
                selector.select.side_effect = None
                selector.select.return_value = [(key, 1)]
                with self.installed(process, selector), \
                     mock.patch.object(gate.os, "read", return_value=b"") as read, \
                     mock.patch.object(gate.os, "write", return_value=1) as write:
                    if operation == "read": read.side_effect = failure
                    elif operation == "write": write.side_effect = failure
                    else: selector.unregister.side_effect = failure
                    with self.assertRaises(OSError) as caught:
                        gate.bounded_command(["authored-child"], data=b"a" if operation == "write" else None)
                self.assertIs(caught.exception, failure)
                self.assert_stopped(process, streams)

    def test_refusal_keeps_the_bounded_failure_reason(self):
        for reason in ("command deadline", "command cancelled", "command output limit"):
            with self.subTest(reason=reason):
                process, selector, streams = self.fixture()
                selector.select.side_effect = gate.GateError(reason)
                with self.installed(process, selector), self.assertRaises(gate.BoundedCommandFailure) as caught:
                    gate.bounded_command(["authored-child"])
                self.assertEqual(reason, caught.exception.reason)
                self.assertEqual((b"", b""), (caught.exception.stdout, caught.exception.stderr))
                self.assert_stopped(process, streams)

    def test_final_wait_failure_uses_the_same_cleanup(self):
        process, selector, streams = self.fixture()
        selector.get_map.return_value = {}
        failure = OSError("authored final wait refusal")
        reaped = process.wait.side_effect
        attempts = []
        def wait(*, timeout):
            attempts.append(timeout)
            if len(attempts) == 1: raise failure
            return reaped(timeout=timeout)
        process.wait.side_effect = wait
        with self.installed(process, selector), self.assertRaises(OSError) as caught:
            gate.bounded_command(["authored-child"])
        self.assertIs(caught.exception, failure)
        process.terminate.assert_called_once_with()
        self.assertEqual(2, len(attempts))
        self.assertEqual(2, attempts[-1])
        self.assertIsNotNone(process.returncode)
        for stream in streams: stream.close.assert_called_once_with()

    def test_a_failed_stop_still_attempts_kill_and_reap(self):
        for stop_error in (OSError, KeyboardInterrupt):
            with self.subTest(stop_error=stop_error.__name__):
                process, selector, streams = self.fixture()
                original = OSError("authored primary refusal")
                selector.select.side_effect = original
                process.terminate.side_effect = stop_error("authored terminate refusal")
                process.kill.side_effect = stop_error("authored kill refusal")
                attempts = []
                def wait(*, timeout):
                    attempts.append(timeout)
                    if len(attempts) == 1: raise stop_error("authored first wait refusal")
                    process.returncode = -9
                    return -9
                process.wait.side_effect = wait
                with self.installed(process, selector), self.assertRaises(OSError) as caught:
                    gate.bounded_command(["authored-child"])
                self.assertIs(caught.exception, original)
                self.assertEqual([2, 2], attempts)
                process.terminate.assert_called_once_with()
                process.kill.assert_called_once_with()
                self.assertEqual(-9, process.returncode)
                for stream in streams: stream.close.assert_called_once_with()

    def test_failed_reap_cannot_be_reported_as_a_stopped_child(self):
        process, selector, streams = self.fixture()
        original = selector.select.side_effect
        process.wait.side_effect = gate.subprocess.TimeoutExpired(["authored-child"], 2)
        with self.installed(process, selector), self.assertRaises(gate.BoundedCommandFailure) as caught:
            gate.bounded_command(["authored-child"])
        self.assertEqual("command cleanup unverified", caught.exception.reason)
        self.assertIsNone(caught.exception.code)
        self.assertIs(caught.exception.__cause__, original)
        self.assertEqual([mock.call(timeout=2), mock.call(timeout=2)], process.wait.call_args_list)
        process.terminate.assert_called_once_with()
        process.kill.assert_called_once_with()
        for stream in streams: stream.close.assert_called_once_with()

    def test_wait_without_an_observed_returncode_is_not_cleanup_proof(self):
        process, selector, streams = self.fixture()
        process.wait.side_effect = None
        process.wait.return_value = None
        with self.installed(process, selector), self.assertRaises(gate.BoundedCommandFailure) as caught:
            gate.bounded_command(["authored-child"])
        self.assertEqual("command cleanup unverified", caught.exception.reason)
        self.assertIsNone(process.returncode)

    def test_close_faults_do_not_skip_other_handles_or_hide_primary_failure(self):
        process, selector, streams = self.fixture(data=True)
        primary = selector.select.side_effect
        selector.close.side_effect = OSError("authored selector close refusal")
        streams[0].close.side_effect = KeyboardInterrupt()
        with self.installed(process, selector), self.assertRaises(OSError) as caught:
            gate.bounded_command(["authored-child"], data=b"authored input")
        self.assertIs(caught.exception, primary)
        self.assertTrue(caught.exception.pipe_cleanup_failed)
        self.assert_stopped(process, streams)
        selector.close.assert_called_once_with()

    def test_success_requires_reaped_child_and_closed_handles(self):
        for close_fault in (False, True):
            with self.subTest(close_fault=close_fault):
                process, selector, streams = self.fixture()
                selector.get_map.return_value = {}
                def reaped(*, timeout):
                    process.returncode = 0
                    return 0
                process.wait.side_effect = reaped
                if close_fault: selector.close.side_effect = OSError("authored close refusal")
                with self.installed(process, selector):
                    if close_fault:
                        with self.assertRaises(gate.GateError): gate.bounded_command(["authored-child"])
                    else: self.assertEqual((0, b"", b""), gate.bounded_command(["authored-child"]))
                process.terminate.assert_not_called()
                process.kill.assert_not_called()
                process.wait.assert_called_once()
                self.assertEqual(0, process.returncode)
                for stream in streams: stream.close.assert_called_once_with()

    def test_real_child_is_reaped_after_selector_construction_refusal(self):
        created = []
        original_spawn = gate.subprocess.Popen
        def spawn(*args, **kwargs):
            process = original_spawn(*args, **kwargs)
            created.append(process)
            return process
        failure = OSError("authored selector construction refusal")
        try:
            with mock.patch.object(gate.subprocess, "Popen", side_effect=spawn), \
                 mock.patch.object(gate.selectors, "DefaultSelector", side_effect=failure), \
                 self.assertRaises(OSError) as caught:
                gate.bounded_command([sys.executable, "-c", "import time; time.sleep(60)"])
            self.assertIs(caught.exception, failure)
            self.assertEqual(1, len(created))
            self.assertIsNotNone(created[0].poll())
            self.assertTrue(created[0].stdout.closed)
            self.assertTrue(created[0].stderr.closed)
        finally:
            # This also safely bounds a deliberate old-runner negative control.
            for process in created:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=2)
                for stream in (process.stdout, process.stderr):
                    if stream is not None: stream.close()


class GateTests(unittest.TestCase):
    def setUp(self):
        # Disk-backed, task-owned scratch. Tests never use the desktop or a live guest.
        root = HERE.parents[1] / "local-data" / "native-fixture-unit"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.scratch = tempfile.TemporaryDirectory(prefix="case-", dir=root)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        backend_patch = owned_patch.object(gate, "backend_binding", return_value={"commit": "authored-owned-backend", "files": {}, "base_declaration_sha256": "authored"})
        backend_patch.start()
        self.addCleanup(backend_patch.stop)

    def exercise(self, fail=None, native=None):
        meta = metadata(); backend = FakeBackend(meta, fail, native)
        result = gate.run_gate(self.root, meta, backend)
        self.assertEqual(json.loads((self.root / "receipts/result.json").read_text()), result)
        return result, backend.events

    def ack_backend(self, *, publish_failure=False, tail=b"\r\n"):
        backend = object.__new__(gate.RealBackend)
        backend.root = "C:\\ce-native-ice-authored"
        backend.run_id = "ice-authored"
        backend.sid = "S-1-5-21-1-2-3-1001"
        backend.receipts = self.root / "ack-receipts"; backend.receipts.mkdir()
        backend.deadline = None
        backend.root_owned = True
        backend.ack_report = {"control": None, "primary": None, "operations": []}
        files = {}; calls = []
        def transport(command, *, timeout, limit):
            calls.append((command, timeout))
            if " (type " in command:
                path = command.split("if exist ", 1)[1].split(" (type ", 1)[0]
                return (0, files[path], b"") if path in files else (76, b"", b"")
            if "&& ren " in command:
                root = command.split("if exist ", 1)[1].split("\\ack.txt", 1)[0]
                final = root + "\\ack.txt"
                if final in files: return 75, b"", b""
                if publish_failure:
                    files[root + "\\ack.tmp"] = backend.run_id.encode() + b"\r\n"
                    return 1, b"", b"Access is denied PRIVATE-AUTHORED-CANARY"
                files[final] = backend.run_id.encode() + tail
                return 0, b"", b""
            self.assertIn("mkdir ", command)
            return 0, b"", b""
        backend.guest_result = transport
        return backend, files, calls

    def test_ack_publisher_redirects_group_output_without_an_echo_suffix(self):
        command = gate.ack_command("C:\\ce-native-ice-authored", "ice-authored")
        self.assertIn("(echo ice-authored)>C:\\ce-native-ice-authored\\ack.tmp && ren ", command)
        self.assertNotIn("else (echo ice-authored>", command)
        self.assertNotEqual("0a563bc6ed92c426350704e86074da36a4eea9e8677330df4a43a65e4ea2fbbf", gate.digest(command.encode()))

    def test_real_ack_round_sets_temporary_owner_before_atomic_publication(self):
        backend, files, calls = self.ack_backend()
        transport = backend.guest_result
        published = []
        def owned_transport(command, *, timeout, limit):
            if "&& ren " in command:
                root = command.split("if exist ", 1)[1].split("\\ack.txt", 1)[0]
                owner = ' && icacls "' + root + '\\ack.tmp" /setowner "*' + backend.sid + '" /q'
                self.assertIn(owner, command)
                self.assertLess(command.index("(echo " + backend.run_id + ")>"), command.index(owner))
                self.assertLess(command.index(owner), command.index(" && ren "))
                self.assertIn("if exist " + root + "\\ack.tmp (exit /b 75)", command)
                self.assertNotIn("/grant", command)
                self.assertNotIn("/reset", command)
                self.assertNotIn(" /T", command)
                published.append(command)
            return transport(command, timeout=timeout, limit=limit)
        backend.guest_result = owned_transport
        backend.ack_control()
        backend.acknowledge()
        self.assertEqual(3, len(published))
        self.assertTrue(backend.ack_report["control"]["pass"])
        self.assertTrue(backend.ack_report["primary"]["final"]["matches_run"])

    def test_ack_owner_identity_refusal_precedes_transport(self):
        backend, files, calls = self.ack_backend()
        for sid in (None, "", "S-1-1-0", "S-1-5-21-1 & unwanted", 'S-1-5-21-1"'):
            backend.sid = sid
            with self.assertRaisesRegex(gate.GateError, "ack-owner-identity"):
                backend._ack_round(backend.root, "authored", float("inf"))
        self.assertEqual([], calls)
        self.assertEqual({}, files)

    def test_reparse_preparation_uses_selected_owned_executable_and_records_failure(self):
        backend, files, calls = self.ack_backend()
        meta = {"files": {"package/grant/authored.exe": {}}}
        def transport(command, *, timeout, limit):
            calls.append((command, timeout))
            self.assertEqual('cmd.exe /d /c "C:\\ce-native-ice-authored\\package\\grant\\authored.exe --prepare-native-reparse C:\\ce-native-ice-authored"', command)
            return 0, b"PASS prepared native reparse fixtures=6\r\n", b""
        backend.guest_result = transport
        backend.prepare_reparse(meta)
        report = json.loads((backend.receipts / "reparse-preparation.json").read_text())
        self.assertTrue(report["success_marker_matches"])
        self.assertEqual(6, report["declared_prepared_links"])
        self.assertLessEqual(calls[0][1], 30)
        backend.receipts = self.root / "old-count-preparation"; backend.receipts.mkdir()
        backend.guest_result = lambda *a, **k: (0, b"PASS prepared native reparse fixtures=4\r\n", b"")
        with self.assertRaisesRegex(gate.GateError, "native-reparse-preparation-failed"):
            backend.prepare_reparse(meta)
        backend.receipts = self.root / "refused-preparation"; backend.receipts.mkdir()
        backend.guest_result = lambda *a, **k: (1, b"", b"fixed authored capability failure")
        with self.assertRaisesRegex(gate.GateError, "native-reparse-preparation-failed"):
            backend.prepare_reparse(meta)
        report = json.loads((backend.receipts / "reparse-preparation.json").read_text())
        self.assertEqual(1, report["exit_code"])
        self.assertFalse(report["success_marker_matches"])

    def test_native_case_completion_rejects_missing_active_late_or_unqueried_jobs(self):
        for mutate in [lambda j: j.update(active_processes=1), lambda j: j.update(elapsed_ms=90001),
                       lambda j: j.update(query_ok=False, active_processes=None, query_error=5),
                       lambda j: j.update(first_active_processes=None)]:
            receipt = done(metadata()); mutate(receipt["cases"][0]["job_completion"])
            with self.assertRaises(gate.GateError): gate.validate_done(receipt, metadata())
        receipt = done(metadata()); receipt["cases"][0]["job_completion"] = None
        with self.assertRaises(gate.GateError): gate.validate_done(receipt, metadata())

    def test_actual_case_completion_waits_within_original_budget_and_pumps(self):
        import subprocess
        source = (Path(__file__).resolve().parents[1] / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        completion = source[source.index("struct CaseCompletion "):source.index("struct Attempt ")]
        harness = r'''
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
using DWORD=std::uint32_t;using ULONGLONG=std::uint64_t;using HANDLE=int;
constexpr DWORD kCaseMs=90000;constexpr int JobObjectBasicAccountingInformation=1;
struct Failure{unsigned code;};void Need(bool good,unsigned code){if(!good)throw Failure{code};}
struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION{DWORD ActiveProcesses=0;};
int mode=0,queries=0,sleeps=0;ULONGLONG ticks=0;DWORD last_error=0;
struct Log{int pumped=0;bool Pump(){++pumped;return mode!=5;}};
ULONGLONG GetTickCount64(){return ticks;}DWORD GetLastError(){return last_error;}
void Sleep(DWORD value){Need(value>0&&value<=10,200);ticks+=value;++sleeps;}
bool QueryInformationJobObject(HANDLE,int,JOBOBJECT_BASIC_ACCOUNTING_INFORMATION* value,std::size_t,void*){
    ++queries;++ticks;
    if(mode==3||(mode==7&&queries==2)){last_error=5;return false;}
    value->ActiveProcesses=mode==2||mode==4||mode==5||mode==7||(mode==1&&queries==1)?1:0;return true;
}
''' + completion + r'''
int main(){
    for(mode=0;mode<8;++mode){
        queries=sleeps=0;last_error=0;ticks=mode==4?89999:mode==6?90000:16;
        Log out,err;CaseCompletion value;bool completed=false;unsigned failure=0;
        try{completed=CompleteCaseJob(1,90000,0,out,err,value);}catch(const Failure& error){failure=error.code;}
        if(mode==0&&(!completed||failure||value.queries!=1||value.first_active||value.active))return 1;
        if(mode==1&&(!completed||failure||value.queries!=2||value.first_active!=1||value.active||out.pumped!=1||err.pumped!=1))return 1;
        if(mode==2&&(completed||failure||value.active!=1||value.elapsed_ms<90000||value.elapsed_ms>90001))return 2;
        if((mode==3||mode==7)&&(completed||failure!=60||value.query_ok||value.query_error!=5))return 3;
        if(mode==4&&(completed||failure||sleeps||value.queries!=1||value.elapsed_ms!=90000))return 4;
        if(mode==5&&(completed||failure||out.pumped!=1||sleeps))return 5;
        if(mode==6&&(completed||failure||value.active||value.elapsed_ms!=90001))return 6;
        if(mode==7&&(!value.first_ok||value.first_active!=1||value.queries!=2))return 7;
        const auto json=CaseCompletionJson(value);
        if(json.find("\"deadline_ms\":90000")==std::string::npos)return 8;
    }
    return 0;
}
'''
        generated = self.root / "case-completion-control.cpp"; executable = self.root / "case-completion-control"
        for broken, expected in ((False, 0), (True, 1)):
            code = harness
            if broken:
                code = code.replace('if (value.active == 0 && now <= deadline) return true;',
                                    'if (value.active == 0 && now <= deadline) return true;\n        if(value.active != 0)return false;', 1)
            generated.write_text(code)
            compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                                       str(generated), "-o", str(executable)], capture_output=True, timeout=30)
            self.assertEqual((0, b""), (compiled.returncode, compiled.stderr))
            ran = subprocess.run([str(executable)], capture_output=True, timeout=5)
            self.assertEqual((expected, b"", b""), (ran.returncode, ran.stdout, ran.stderr))

    def test_native_ack_sharing_policy_and_command_match_host_contract(self):
        import subprocess
        source = (Path(__file__).resolve().parents[1] / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        def function(marker):
            start = source.index(marker)
            return source[start:source.index("\n}\n", start) + 3]
        policy = function("DWORD ShareMode(")
        command = function("std::wstring AckCommand(")
        expected = gate.ack_command("C:\\ce-native-ice-authored", "ice-authored")
        harness = ('#include <string>\nusing DWORD=unsigned long;\n'
            'constexpr DWORD FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,FILE_SHARE_DELETE=4;\n'
            'std::wstring Wide(const std::string& s){return std::wstring(s.begin(),s.end());}\n' +
            policy + command + '\nint main(){\n'
            'if(ShareMode(false,false)!=FILE_SHARE_READ||ShareMode(false,true)!=FILE_SHARE_READ)return 1;\n'
            'if(ShareMode(true,false)!=FILE_SHARE_READ||ShareMode(true,true)!=(FILE_SHARE_READ|FILE_SHARE_WRITE))return 2;\n'
            'for(bool d:{false,true})for(bool p:{false,true})if(ShareMode(d,p)&FILE_SHARE_DELETE)return 3;\n'
            'return AckCommand(L"C:\\\\ce-native-ice-authored","ice-authored")==L' + json.dumps(expected) + '?0:4;\n}\n')
        generated = self.root / "ack-sharing-policy.cpp"; generated.write_text(harness)
        executable = self.root / "ack-sharing-policy"
        build = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)], capture_output=True, timeout=30)
        self.assertEqual(0, build.returncode)
        result = subprocess.run([str(executable)], capture_output=True, timeout=5)
        self.assertEqual(0, result.returncode)
        self.assertEqual(b"", result.stdout + result.stderr)

    def test_actual_native_ack_completion_distinguishes_each_failure(self):
        import subprocess
        source = (Path(__file__).resolve().parents[1] / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        diagnostics = source[source.index("struct AckCompletion {"):source.index("DWORD RunAckCommand(")]
        start = source.index("DWORD RunAckCommand(")
        completion = source[source.index('    completion.stage = "wait";', start):source.index("\n}\n", start)]
        harness = r"""
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
using DWORD=std::uint32_t; using ULONGLONG=std::uint64_t;
constexpr DWORD WAIT_OBJECT_0=0, WAIT_TIMEOUT=258, WAIT_FAILED=0xffffffff;
struct Failure { unsigned code; };
void Need(bool yes,unsigned code){if(!yes)throw Failure{code};}
struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {DWORD ActiveProcesses=0;};
constexpr int JobObjectBasicAccountingInformation=1;
struct Owned {int h;}; Owned process{1},job{2};
int scenario=0,waits=0,terminated=0,queries=0,sleeps=0; DWORD last_error=0; ULONGLONG ticks=0;
ULONGLONG GetTickCount64(){return ticks;}
DWORD GetLastError(){return last_error;}
DWORD WaitForSingleObject(int,DWORD){
    if(++waits>1)return WAIT_OBJECT_0;
    if(scenario==0){ticks+=5000;return WAIT_TIMEOUT;}
    if(scenario==1){++ticks;last_error=6;return WAIT_FAILED;}
    ticks+=scenario==7?4999:scenario==9?5000:16;
    return WAIT_OBJECT_0;
}
void Sleep(DWORD milliseconds){Need(milliseconds>0&&milliseconds<=10,200);ticks+=milliseconds;++sleeps;}
bool TerminateJobObject(int,DWORD){++terminated;return true;}
bool GetExitCodeProcess(int,DWORD* code){if(scenario==2){last_error=5;return false;}*code=0;return true;}
bool QueryInformationJobObject(int,int,JOBOBJECT_BASIC_ACCOUNTING_INFORMATION* value,std::size_t,void*){
    ++queries;++ticks;
    if(scenario==3||(scenario==8&&queries==2)){last_error=5;return false;}
    value->ActiveProcesses=scenario==4||scenario==7||scenario==8||(scenario==6&&queries<=2)?1:0;return true;
}
""" + diagnostics + "\nDWORD Complete(AckCompletion& completion){\n" + completion + r"""
}
int main(){
    for(scenario=0;scenario<10;++scenario){
        waits=0;terminated=0;queries=0;sleeps=0;last_error=0;ticks=0;
        AckCompletion completion;completion.publication=scenario%2!=0;unsigned failure=0;
        try{Complete(completion);}catch(const Failure& value){failure=value.code;}
        std::cout<<"{\"failure\":"<<failure<<",\"terminated\":"<<terminated<<",\"sleeps\":"<<sleeps<<",\"completion\":"<<AckCompletionJson(completion)<<"}\n";
    }
}
"""
        generated = self.root / "ack-completion-control.cpp"; generated.write_text(harness)
        executable = self.root / "ack-completion-control"
        compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)],
                                  capture_output=True, timeout=30)
        self.assertEqual((0, b""), (compiled.returncode, compiled.stderr))
        ran = subprocess.run([str(executable)], capture_output=True, timeout=5)
        self.assertEqual((0, b""), (ran.returncode, ran.stderr))
        rows = [json.loads(line) for line in ran.stdout.splitlines()]
        self.assertEqual([105, 105, 110, 111, 112, 0, 0, 112, 111, 112], [row["failure"] for row in rows])
        self.assertEqual([1, 1, 0, 0, 0, 0, 0, 0, 0, 0], [row["terminated"] for row in rows])
        self.assertEqual(["wait", "wait", "exit-query", "accounting-query", "active-processes", "complete", "complete",
                          "active-processes", "accounting-query", "active-processes"],
                         [row["completion"]["stage"] for row in rows])
        wait_ms = [5000, 1, 16, 16, 16, 16, 16, 4999, 16, 5000]
        for index, row in enumerate(rows):
            value = row["completion"]
            self.assertEqual("new" if index % 2 else "old", value["variant"])
            self.assertEqual({"result": (258 if index == 0 else 0xffffffff if index == 1 else 0),
                              "error": 6 if index == 1 else 0, "elapsed_ms": wait_ms[index]}, value["wait"])
            self.assertEqual(None if index < 2 else {"ok": index != 2, "code": None if index == 2 else 0,
                                                    "error": 5 if index == 2 else 0}, value["exit"])
            if index < 3: self.assertIsNone(value["accounting"])
        accounting = [row["completion"]["accounting"] for row in rows]
        self.assertEqual({"ok": False, "active_processes": None, "error": 5, "first_active_processes": None,
                          "queries": 1, "elapsed_ms": 17}, accounting[3])
        self.assertTrue(accounting[4]["ok"])
        self.assertEqual((1, 1), (accounting[4]["first_active_processes"], accounting[4]["active_processes"]))
        self.assertGreater(accounting[4]["queries"], 1)
        self.assertGreaterEqual(accounting[4]["elapsed_ms"], 5000)
        self.assertLessEqual(accounting[4]["elapsed_ms"], 5001)
        self.assertEqual({"ok": True, "active_processes": 0, "error": 0, "first_active_processes": 0,
                          "queries": 1, "elapsed_ms": 17}, accounting[5])
        self.assertEqual({"ok": True, "active_processes": 0, "error": 0, "first_active_processes": 1,
                          "queries": 3, "elapsed_ms": 39}, accounting[6])
        self.assertEqual({"ok": True, "active_processes": 1, "error": 0, "first_active_processes": 1,
                          "queries": 1, "elapsed_ms": 5000}, accounting[7])
        self.assertEqual(0, rows[7]["sleeps"])
        self.assertEqual({"ok": False, "active_processes": None, "error": 5, "first_active_processes": 1,
                          "queries": 2, "elapsed_ms": 28}, accounting[8])
        self.assertEqual({"ok": True, "active_processes": 0, "error": 0, "first_active_processes": 0,
                          "queries": 1, "elapsed_ms": 5001}, accounting[9])

    def test_native_ack_completion_receipt_has_bounded_schema(self):
        receipt = done(metadata())
        control = {"variant": "old", "stage": "wait", "wait": {"result": 258, "error": 0, "elapsed_ms": 5001},
                   "exit": None, "accounting": None}
        receipt.update(failure=105, inputs_unchanged=False, cases=[], ack_control=[control])
        gate.validate_native_receipt(receipt, metadata())
        accounting = {"ok": True, "active_processes": 0, "error": 0, "first_active_processes": 1, "queries": 3, "elapsed_ms": 39}
        observed = copy.deepcopy(receipt); observed["ack_control"][0].update(accounting=accounting)
        gate.validate_native_receipt(observed, metadata())
        invalid = [lambda r: r.pop("ack_control"), lambda r: r["ack_control"].extend([control, control]),
                   lambda r: r["ack_control"][0].update(variant="new"),
                   lambda r: r["ack_control"][0].update(stage="PRIVATE-AUTHORED-CANARY"),
                   lambda r: r["ack_control"][0]["wait"].update(result=True),
                   lambda r: r["ack_control"][0]["wait"].update(error=-1),
                   lambda r: r["ack_control"][0]["wait"].update(elapsed_ms=2**64),
                   lambda r: r["ack_control"][0].update(exit={"ok": False, "code": 0, "error": 5}),
                   lambda r: r["ack_control"][0].update(accounting={**accounting, "active_processes": 2**32}),
                   lambda r: r["ack_control"][0].update(accounting={**accounting, "first_active_processes": True}),
                   lambda r: r["ack_control"][0].update(accounting={**accounting, "queries": 0}),
                   lambda r: r["ack_control"][0].update(accounting={**accounting, "elapsed_ms": 2**64})]
        for mutate in invalid:
            value = copy.deepcopy(receipt); mutate(value)
            with self.assertRaises(gate.GateError): gate.validate_native_receipt(value, metadata())

    def test_ack_sharing_error_has_specific_sanitized_classification(self):
        value = b"The process cannot access the file because it is being used by another process. PRIVATE-AUTHORED-CANARY"
        self.assertEqual("sharing-violation", gate.ack_classification(1, b"", value))

    def test_ack_control_success_and_collision_preserve_files_and_private_receipts(self):
        backend, files, calls = self.ack_backend()
        backend.ack_control()
        report = backend.ack_report["control"]
        self.assertTrue(report["pass"])
        self.assertEqual(0, report["first"]["exit_code"])
        self.assertEqual(75, report["collision"]["exit_code"])
        self.assertEqual({backend.root + "\\ack-control\\ack.txt"}, set(files))
        self.assertTrue(all(0 < timeout <= 5 for _, timeout in calls))
        for path in backend.receipts.iterdir():
            self.assertEqual(0o600, path.stat().st_mode & 0o777)
            self.assertLessEqual(path.stat().st_size, 8192)

    def test_primary_ack_failure_preserves_error_and_readbacks_before_throw(self):
        backend, files, calls = self.ack_backend(publish_failure=True)
        with self.assertRaisesRegex(gate.GateError, "ack-publication-failed"):
            backend.acknowledge()
        report = backend.ack_report["primary"]
        self.assertEqual(1, report["exit_code"])
        self.assertEqual("access-denied", report["publication"]["classification"])
        self.assertEqual("present", report["temporary"]["status"])
        self.assertTrue(report["temporary"]["matches_run"])
        self.assertEqual("absent", report["final"]["status"])
        private = backend.receipts / report["publication"]["stderr"]["file"]
        self.assertIn(b"PRIVATE-AUTHORED-CANARY", private.read_bytes())
        self.assertNotIn("CANARY", json.dumps(backend.ack_report))
        self.assertEqual(1, sum("&& ren " in command for command, _ in calls))

    def test_ack_control_does_not_trim_protocol_spaces_or_retry_failed_publication(self):
        backend, _, calls = self.ack_backend(tail=b" \r\n")
        with self.assertRaisesRegex(gate.GateError, "ack-control-readback-failed"):
            backend.ack_control()
        self.assertFalse(backend.ack_report["control"]["first"]["final"]["matches_run"])
        self.assertEqual(1, sum("&& ren " in command for command, _ in calls))

    def test_observed_windows_trailing_space_cannot_pass_primary_ack_readback(self):
        backend, _, _ = self.ack_backend(tail=b" \r\n")
        with self.assertRaisesRegex(gate.GateError, "ack-readback-failed"):
            backend.acknowledge()
        report = backend.ack_report["primary"]
        self.assertEqual(0, report["exit_code"])
        self.assertEqual("absent", report["temporary"]["status"])
        self.assertEqual("present", report["final"]["status"])
        self.assertEqual(len(backend.run_id) + 3, report["final"]["bytes"])
        self.assertFalse(report["final"]["matches_run"])

    def test_ack_control_has_one_total_deadline(self):
        backend, _, _ = self.ack_backend()
        clock = [0.0]; calls = []
        def transport(command, *, timeout, limit):
            calls.append(command); clock[0] = 31.0
            return 0, b"", b""
        backend.guest_result = transport
        with mock.patch.object(gate.time, "monotonic", side_effect=lambda: clock[0]):
            with self.assertRaisesRegex(gate.GateError, "ack-control-publication-failed"):
                backend.ack_control()
        self.assertEqual(1, len(calls))
        self.assertEqual("unavailable", backend.ack_report["control"]["first"]["final"]["status"])

    def test_ack_private_stderr_retention_is_bounded(self):
        backend, _, _ = self.ack_backend()
        value = b"PRIVATE-AUTHORED-CANARY" * 500
        backend.guest_result = lambda *a, **k: (1, b"", value)
        _, _, row = backend._ack_step("authored", "authored", gate.time.monotonic() + 5)
        self.assertTrue(row["stderr"]["truncated"])
        self.assertEqual(8192, (backend.receipts / row["stderr"]["file"]).stat().st_size)
        self.assertNotIn("CANARY", json.dumps(row))

    def test_bounded_transport_retains_partial_private_stderr_on_timeout(self):
        import sys
        command = [sys.executable, "-c", 'import sys,time;sys.stderr.write("PRIVATE-AUTHORED-CANARY");sys.stderr.flush();time.sleep(5)']
        with self.assertRaises(gate.BoundedCommandFailure) as failure:
            gate.bounded_command(command, timeout=0.5)
        self.assertIn(b"PRIVATE-AUTHORED-CANARY", failure.exception.stderr)
        self.assertEqual("bounded-command-failed", str(failure.exception))

    def test_ack_unowned_root_cannot_publish_or_create_control(self):
        backend, _, calls = self.ack_backend(); backend.root_owned = False
        for operation in [backend.ack_control, backend.acknowledge]:
            with self.assertRaisesRegex(gate.GateError, "ack-root-not-owned"):
                operation()
        self.assertEqual([], calls)

    def test_ready_recovery_retains_initial_poll_timing(self):
        backend, _, _ = self.ack_backend(); calls = []
        def guest(command, **kwargs):
            calls.append(command)
            return 0, b'{"schema":1}'
        backend.guest = guest
        backend.read_json("ready", 30)
        backend.read_json("ready", 2)
        self.assertEqual([30, 2], [p["seconds_limit"] for p in backend.ack_report["ready_polls"]])
        self.assertEqual([1, 1], [p["poll_attempts"] for p in backend.ack_report["ready_polls"]])
        self.assertEqual(2, len(calls))

    def test_ack_control_refusal_prevents_task_creation_and_keeps_vm_cleanup(self):
        result, events = self.exercise("ack-control")
        self.assertFalse(result["pass"])
        self.assertFalse(result["task_created"])
        self.assertNotIn("create-task", events)
        self.assertIn("stop-vm", events)

    def test_failure_collection_retries_late_done_before_owned_cleanup(self):
        meta = metadata()
        class LateDone(FakeBackend):
            reads = 0
            def read_json(self, name, seconds):
                if name == "done":
                    self.reads += 1
                    if self.reads == 1:
                        self.event("done-unavailable")
                        raise gate.GateError("native-receipt-deadline")
                return super().read_json(name, seconds)
        backend = LateDone(meta)
        result = gate.run_gate(self.root, meta, backend)
        self.assertFalse(result["pass"])
        self.assertEqual(result["failure"], "native-receipt-deadline")
        self.assertEqual(backend.reads, 2)
        self.assertTrue((self.root / "receipts/recovered-done.json").is_file())
        self.assertFalse((self.root / "receipts/done.json").exists())
        self.assertTrue(result["failure_collection"]["readback"]["complete"])
        self.assertLess(backend.events.index("failure-readback"), backend.events.index("end-task-forced"))
        self.assertLess(backend.events.index("end-task-forced"), backend.events.index("stop-vm"))

    def test_failed_receipt_rechecks_inputs_without_reclassifying(self):
        receipt = done(metadata())
        receipt["failure"] = 74
        receipt["cases"][0]["exit"] = 1
        result, events = self.exercise(native=receipt)
        self.assertFalse(result["pass"])
        self.assertEqual(result["native"], receipt)
        self.assertIn("failure-readback", events)
        self.assertEqual(json.loads((self.root / "receipts/done.json").read_text()), receipt)
        self.assertTrue((self.root / "receipts/failure-readback.json").is_file())

    def test_failed_case_output_stays_private_and_only_numbers_reach_the_result(self):
        meta = metadata()
        out = b"Windows private-grant policy checks completed\r\nPRIVATE-AUTHORED-CANARY stdout\r\n"
        err = (b"Request outcome call=1 positive=1 expected=1 actual=0 fixed-error=relay request publication failed "
               b"step=publish native=1 ntstatus=0xC0000043 iostatus=0x00000000 lasterror=0\r\n"
               b"FAIL authored Windows grant check: PRIVATE-AUTHORED-CANARY\r\n")
        receipt, evidence = failed_receipt(meta, out, err)
        backend = FakeBackend(meta, native=receipt, failed_case=evidence)
        result = gate.run_gate(self.root, meta, backend)
        self.assertFalse(result["pass"])
        self.assertEqual("native-gate-failed", result["failure"])
        collected = result["failure_collection"]["failed_case"]
        self.assertEqual("preserved", collected["status"])
        self.assertEqual({"call": 1, "positive_call": 1, "expected_success": True, "actual_success": False,
                          "fixed_error_class": "request-publication-failed", "step": "publish", "native_status_returned": True,
                          "ntstatus": 0xC0000043, "io_status": 0, "last_error": 0}, collected["request_outcome"])
        for name, value in (("stdout", out), ("stderr", err)):
            row = collected[name]
            self.assertEqual({"bytes": len(value), "sha256": gate.digest(value), "retained_bytes": len(value),
                              "retained_sha256": gate.digest(value), "complete": True,
                              "file": "failed-grant." + name + ".private.bin"}, row)
            path = self.root / "receipts" / row["file"]
            self.assertEqual(value, path.read_bytes())
            self.assertEqual(0o600, path.stat().st_mode & 0o777)
        self.assertNotIn(b"CANARY", (self.root / "receipts/result.json").read_bytes())
        self.assertLess(backend.events.index("read-failed-case"), backend.events.index("failure-readback"))
        self.assertLess(backend.events.index("failure-readback"), backend.events.index("stop-vm"))

    def test_failed_case_evidence_must_match_the_case_receipt(self):
        meta = metadata(); out = b"authored progress\r\n"; err = b"FAIL authored Windows grant check\r\n"
        changes = [lambda e: e["stdout"].update(bytes=e["stdout"]["bytes"] + 1), lambda e: e["stderr"].update(sha256="5" * 64),
                   lambda e: e["stderr"].update(hex=b"FAIL authored Windows grant check?\r\n".hex()),
                   lambda e: e["stderr"].update(hex=e["stderr"]["hex"][:-2]), lambda e: e["stderr"].update(hex=e["stderr"]["hex"].upper()),
                   lambda e: e["stderr"].update(retained_bytes=1), lambda e: e["stderr"].update(retained_bytes=True),
                   lambda e: e["stderr"].update(extra=1), lambda e: e.update(extra=1), lambda e: e.update(name="closed"),
                   lambda e: e.update(run="old-run"), lambda e: e.update(manifest_sha256="6" * 64), lambda e: e.update(schema=True)]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                target = self.root / str(index); target.mkdir()
                receipt, evidence = failed_receipt(meta, out, err); change(evidence)
                result = gate.run_gate(target, meta, FakeBackend(meta, native=receipt, failed_case=evidence))
                self.assertFalse(result["pass"])
                self.assertEqual("unavailable-or-rejected", result["failure_collection"]["failed_case"]["status"])
                self.assertEqual([], list((target / "receipts").glob("failed-*")))
                self.assertEqual(receipt, result["native"])

    def test_failed_case_collection_never_changes_or_masks_the_gate_result(self):
        meta = metadata(); receipt, _ = failed_receipt(meta, b"a\n", b"b\n")
        result = gate.run_gate(self.root, meta, FakeBackend(meta, native=receipt))
        self.assertEqual("native-gate-failed", result["failure"])
        self.assertEqual("unavailable-or-rejected", result["failure_collection"]["failed_case"]["status"])
        self.assertTrue(result["failure_collection"]["readback"]["complete"])
        target = self.root / "clean"; target.mkdir()
        result = gate.run_gate(target, meta, FakeBackend(meta))
        self.assertTrue(result["pass"]); self.assertIsNone(result["failure_collection"])

    def test_failed_case_output_with_a_forbidden_token_is_never_fetched(self):
        meta = metadata(); receipt, evidence = failed_receipt(meta, b"a\n", b"b\n")
        receipt["cases"][1]["no_forbidden_output"] = False
        backend = FakeBackend(meta, native=receipt, failed_case=evidence)
        result = gate.run_gate(self.root, meta, backend)
        self.assertEqual("withheld-forbidden-output", result["failure_collection"]["failed_case"]["status"])
        self.assertNotIn("read-failed-case", backend.events)

    def test_passing_or_incomplete_last_case_has_no_failed_output(self):
        meta = metadata()
        late = done(meta); late.update(failure=75, inputs_unchanged=False)
        backend = FakeBackend(meta, native=late)
        result = gate.run_gate(self.root, meta, backend)
        self.assertEqual("not-applicable", result["failure_collection"]["failed_case"]["status"])
        self.assertNotIn("read-failed-case", backend.events)
        incomplete = done(meta); incomplete.update(failure=57, inputs_unchanged=False)
        incomplete["cases"] = incomplete["cases"][:1]
        incomplete["cases"][0].update({"exit": None, "forced": None, "cleanup_confirmed": False, "infrastructure_failure": 57,
            "output_complete": False, "no_forbidden_output": False, "summary_valid": False, "checks": None, "stdout_bytes": None,
            "stderr_bytes": None, "stdout_sha256": None, "stderr_sha256": None})
        target = self.root / "incomplete"; target.mkdir()
        result = gate.run_gate(target, meta, FakeBackend(meta, native=incomplete))
        self.assertEqual("not-applicable", result["failure_collection"]["failed_case"]["status"])

    def test_oversize_failed_output_keeps_a_marked_tail(self):
        meta = metadata(); out = b"x" * (gate.FAILED_OUTPUT_BYTES + 5) + b"LAST"; err = b"e\n"
        receipt, evidence = failed_receipt(meta, out, err)
        result = gate.run_gate(self.root, meta, FakeBackend(meta, native=receipt, failed_case=evidence))
        row = result["failure_collection"]["failed_case"]["stdout"]
        self.assertFalse(row["complete"])
        self.assertEqual((len(out), gate.FAILED_OUTPUT_BYTES), (row["bytes"], row["retained_bytes"]))
        retained = (self.root / "receipts" / row["file"]).read_bytes()
        self.assertEqual(out[-gate.FAILED_OUTPUT_BYTES:], retained)
        self.assertEqual(gate.digest(retained), row["retained_sha256"])

    def test_request_outcome_parses_only_the_fixed_line(self):
        line = (b"Request outcome call=7 positive=2 expected=0 actual=1 fixed-error=none step=none native=0 "
                b"ntstatus=0x00000000 iostatus=0x00000000 lasterror=32\n")
        value = gate.request_outcome(b"prefix\n" + line)
        self.assertEqual((7, 2, False, True, "none", "none", False, 32),
                         (value["call"], value["positive_call"], value["expected_success"], value["actual_success"],
                          value["fixed_error_class"], value["step"], value["native_status_returned"], value["last_error"]))
        self.assertEqual("unclassified", gate.request_outcome(line.replace(b"fixed-error=none", b"fixed-error=something else"))["fixed_error_class"])
        for text in (b"", b"FAIL authored Windows grant check\n", line.replace(b"0x00000000", b"0x0"), line.replace(b"call=7", b"call=x")):
            self.assertIsNone(gate.request_outcome(text))

    def test_actual_supervisor_evidence_passes_the_actual_host_validator(self):
        import subprocess
        code = (HERE.parent / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        hexed = code[code.index("std::string Hex("):code.index("bool HexString(")]
        evidence = code[code.index("std::string FailedStreamJson("):code.index("bool Summary(")]
        harness = r"""
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
using BYTE=unsigned char;
constexpr size_t kFailedOutputBytes=8192;
""" + hexed + evidence + r"""
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string out=argv[1][0]=='L'?std::string(9000,'x'):"authored output",err=argv[1][0]=='L'?"":"authored failure";
    std::fputs(FailedCaseJson("authored-run","manifest","grant",out,"out-hash",err,"err-hash").c_str(),stdout);
}
"""
        generated = self.root / "failed-evidence.cpp"; generated.write_text(harness)
        executable = self.root / "failed-evidence"
        compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)],
                                  capture_output=True, timeout=30)
        self.assertEqual((0, b""), (compiled.returncode, compiled.stderr))
        for mode, out, err in (("S", b"authored output", b"authored failure"), ("L", b"x" * 9000, b"")):
            ran = subprocess.run([str(executable), mode], capture_output=True, timeout=5)
            self.assertEqual((0, b""), (ran.returncode, ran.stderr))
            value = gate.read_json(ran.stdout)
            value["stdout"]["sha256"] = gate.digest(out); value["stderr"]["sha256"] = gate.digest(err)
            value["manifest_sha256"] = "2" * 64; value["run"] = metadata()["run_id"]
            case = {"name": "grant", "stdout_bytes": len(out), "stderr_bytes": len(err),
                    "stdout_sha256": gate.digest(out), "stderr_sha256": gate.digest(err)}
            streams = gate.validate_failed_case(value, case, metadata())
            self.assertEqual(out[-gate.FAILED_OUTPUT_BYTES:], streams["stdout"][0])
            self.assertEqual(err, streams["stderr"][0])
            self.assertEqual(mode == "S", streams["stdout"][1]["complete"])

    def test_actual_case_stream_refuses_any_disagreement(self):
        import subprocess
        code = (HERE.parent / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        stream = code[code.index("std::string CaseStream("):code.index("std::string FailedStreamJson(")]
        harness = r"""
#include <cstdint>
#include <string>
using DWORD=std::uint32_t;using HANDLE=int;using LONGLONG=long long;
union LARGE_INTEGER{LONGLONG QuadPart;};
struct Failure{unsigned code;};void Need(bool good,unsigned code){if(!good)throw Failure{code};}
int mode=0;std::string file="authored bytes";
bool GetFileSizeEx(HANDLE,LARGE_INTEGER* size){size->QuadPart=mode==1?1:static_cast<LONGLONG>(file.size());return mode!=2;}
std::string Read(HANDLE,DWORD limit){Need(limit==1024*1024,200);return mode==3?file+"x":file;}
std::string Digest(HANDLE){return "file-digest";}
std::string DigestBytes(const std::string&){return mode==4?"other-digest":"file-digest";}
""" + stream + r"""
int main(){
    const unsigned expected[]={0,93,93,94,95,93};
    for(mode=0;mode<6;++mode){
        std::string sha;unsigned failure=0;std::string text;
        try{text=CaseStream(1,mode==5?1:static_cast<DWORD>(file.size()),sha);}catch(const Failure& value){failure=value.code;}
        if(failure!=expected[mode])return 10+mode;
        if(!mode&&(text!=file||sha!="file-digest"))return 1;
    }
    return 0;
}
"""
        generated = self.root / "case-stream.cpp"; generated.write_text(harness)
        executable = self.root / "case-stream"
        compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)],
                                  capture_output=True, timeout=30)
        self.assertEqual((0, b""), (compiled.returncode, compiled.stderr))
        ran = subprocess.run([str(executable)], capture_output=True, timeout=5)
        self.assertEqual((0, b"", b""), (ran.returncode, ran.stdout, ran.stderr))

    def test_actual_output_open_does_not_follow_links_or_aliases(self):
        import subprocess
        code = (HERE.parent / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        handle = code[code.index("struct Handle {"):code.index("std::wstring Wide(")]
        opener = code[code.index("DWORD ShareMode("):code.index("std::string Read(HANDLE")]
        harness = r"""
#include <cstdint>
#include <string>
using DWORD=std::uint32_t;using PSID=void*;using HANDLE=void*;
constexpr DWORD kMaxFile=32*1024*1024;
constexpr DWORD FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,FILE_TRAVERSE=32,FILE_READ_DATA=1,FILE_READ_ATTRIBUTES=128,READ_CONTROL=0x20000;
constexpr DWORD OPEN_EXISTING=3,FILE_FLAG_OPEN_REPARSE_POINT=0x00200000,FILE_FLAG_BACKUP_SEMANTICS=0x02000000;
constexpr DWORD FILE_TYPE_DISK=1,FILE_ATTRIBUTE_REPARSE_POINT=1024,FILE_ATTRIBUTE_DEVICE=64,FILE_ATTRIBUTE_DIRECTORY=16;
static HANDLE const INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(-1);
struct BY_HANDLE_FILE_INFORMATION{DWORD dwFileAttributes=0,nNumberOfLinks=1,nFileSizeHigh=0,nFileSizeLow=64;};
struct Failure{unsigned code;};void Need(bool good,unsigned code){if(!good)throw Failure{code};}
int mode=0,opens=0;DWORD flags=0,share=0;
void CloseHandle(HANDLE){}
HANDLE CreateFileW(const wchar_t*,DWORD,DWORD s,void*,DWORD,DWORD f,void*){++opens;flags=f;share=s;return reinterpret_cast<HANDLE>(8);}
DWORD GetFileType(HANDLE){return mode==1?2:FILE_TYPE_DISK;}
bool GetFileInformationByHandle(HANDLE,BY_HANDLE_FILE_INFORMATION* value){
    if(mode==2)value->dwFileAttributes=FILE_ATTRIBUTE_REPARSE_POINT;
    if(mode==3)value->dwFileAttributes=FILE_ATTRIBUTE_DEVICE;
    if(mode==4)value->dwFileAttributes=FILE_ATTRIBUTE_DIRECTORY;
    if(mode==5)value->nNumberOfLinks=2;
    if(mode==6)value->nFileSizeHigh=1;
    if(mode==7)value->nFileSizeLow=kMaxFile+1;
    return true;
}
bool Private(HANDLE,PSID,bool){return mode!=8;}
""" + handle + opener + r"""
int main(){
    const unsigned expected[]={0,17,18,18,18,19,19,19,20};
    for(mode=0;mode<9;++mode){
        unsigned failure=0;
        try{Open(L"output",false,nullptr);}catch(const Failure& value){failure=value.code;}
        if(failure!=expected[mode])return 10+mode;
        if(!(flags&FILE_FLAG_OPEN_REPARSE_POINT)||(flags&FILE_FLAG_BACKUP_SEMANTICS)||share!=FILE_SHARE_READ)return 2;
    }
    return 0;
}
"""
        generated = self.root / "output-open.cpp"; generated.write_text(harness)
        executable = self.root / "output-open"
        compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)],
                                  capture_output=True, timeout=30)
        self.assertEqual((0, b""), (compiled.returncode, compiled.stderr))
        ran = subprocess.run([str(executable)], capture_output=True, timeout=5)
        self.assertEqual((0, b"", b""), (ran.returncode, ran.stdout, ran.stderr))

    def test_supervisor_gives_each_prepared_case_its_own_seed_folder(self):
        code = (HERE.parent / "network/tests/ice_windows_native_supervisor.cpp").read_text()
        self.assertIn('if (spec.name == "grant") command += L" --prepared-native-reparse " + Quote(root + L"\\\\prepared-reparse");', code)
        self.assertIn('if (spec.name == "directory") command += L" --prepared-native-reparse " + Quote(root + L"\\\\prepared-reparse-directory");', code)

    def test_failed_case_receipt_name_is_allowlisted(self):
        backend, _, _ = self.ack_backend()
        with self.assertRaisesRegex(gate.GateError, "receipt-name"):
            backend.read_json("..\\other", 1)
        commands = []
        def guest(command, **kwargs):
            commands.append(command)
            return 0, b'{"schema":1}'
        backend.guest = guest
        backend.read_json("failed-case", 2)
        self.assertEqual(['cmd.exe /d /c type "C:\\ce-native-ice-authored\\output\\failed-case.json"'], commands)

    def test_partial_owned_stage_collects_but_unowned_refusal_does_not(self):
        class PartialStage(FakeBackend):
            def stage(self, prepared, meta, receipts):
                self.root_owned = True
                raise gate.GateError("partial-stage")
        meta = metadata()
        backend = PartialStage(meta)
        result = gate.run_gate(self.root, meta, backend)
        self.assertFalse(result["pass"])
        self.assertTrue(result["failure_collection"]["attempted"])
        self.assertIn("failure-readback", backend.events)
        self.assertNotIn("create-task", backend.events)

    def test_unconfirmed_attempt_is_preserved_and_cannot_pass(self):
        meta = metadata()
        receipt = done(meta)
        receipt["failure"] = 57
        row = receipt["cases"][0]
        row.update({"exit": None, "forced": None, "cleanup_confirmed": False, "infrastructure_failure": 57,
                    "output_complete": False, "no_forbidden_output": False, "summary_valid": False, "checks": None,
                    "stdout_bytes": None, "stderr_bytes": None, "stdout_sha256": None, "stderr_sha256": None})
        receipt["cases"] = [row]
        gate.validate_native_receipt(receipt, meta)
        with self.assertRaises(gate.GateError):
            gate.validate_done(receipt, meta)
        result, _ = self.exercise(native=receipt)
        self.assertEqual(result["native"]["cases"][0]["pid"], row["pid"])
        self.assertFalse(result["pass"])

    def test_toolchain_and_progress_proof_are_required(self):
        pin = fake_toolchain(self.root)
        self.assertEqual(gate.toolchain_binding(pin)["archive_sha256"], "a" * 64)
        receipt_path = Path(pin["path"])
        receipt = json.loads(receipt_path.read_text())
        receipt["verified"] = False
        receipt_path.write_bytes(gate.json_bytes(receipt))
        pin["sha256"] = gate.digest(receipt_path.read_bytes())
        with self.assertRaises(gate.GateError):
            gate.toolchain_binding(pin)
        for progress in (["one"] * 4, ["one", "two", "three", "UNVERIFIED"], ["one", "two", "three"]):
            with self.assertRaises(gate.GateError):
                gate.grant_progress(progress)


    def test_actual_supervisor_summary_with_fabricated_output(self):
        import subprocess
        source = Path(__file__).resolve().parents[1] / "network/tests/ice_windows_native_supervisor.cpp"
        code = source.read_text()
        summary = code[code.index("bool Summary("):code.index("struct CaseCompletion ")]
        harness = r"""
int main() {
  const std::vector<std::string> progress = {"first progress", "second progress", "third progress", "fourth progress"};
  const std::string prefix = "first progress\r\nsecond progress\r\nthird progress\r\nfourth progress\r\n";
  const std::string grant = "PASS authored checks=657; unexercised-reparse=0; native privacy requires separate review\r\n";
  std::uint64_t checks = 0; bool present = false;
  if (!Summary("grant", prefix + grant, "", progress, checks, present) || !present || checks != 657) return 1;
  if (!Summary("directory", "PASS Windows private-directory checks=304; native acceptance remains separate\n", "", progress, checks, present) || !present || checks != 304) return 2;
  if (!Summary("protocol", "PASS 60 authored ICE exchange checks\n", "", progress, checks, present) || !present || checks != 60) return 3;
  if (!Summary("closed", "PASS Windows production exchange gate remains closed\n", "", progress, checks, present) || present) return 4;
  if (Summary("grant", grant, "", progress, checks, present)) return 5;
  if (Summary("grant", "arbitrary text\n" + prefix + grant, "", progress, checks, present)) return 6;
  if (Summary("grant", prefix + "UNVERIFIED\n" + grant, "", progress, checks, present)) return 7;
  if (Summary("grant", prefix + "UNEXERCISED\n" + grant, "", progress, checks, present)) return 8;
  if (Summary("grant", prefix + grant, "unexpected stderr", progress, checks, present)) return 9;
  if (Summary("grant", prefix + "PASS authored checks=657; unexercised-reparse=1; native privacy requires separate review\n", "", progress, checks, present)) return 10;
  if (Summary("protocol", "PASS 10000001 authored ICE exchange checks\n", "", progress, checks, present)) return 11;
  if (Summary("protocol", "PASS 0 authored ICE exchange checks\n", "", progress, checks, present)) return 12;
  return 0;
}
"""
        generated = self.root / "summary-control.cpp"
        executable = self.root / "summary-control"
        generated.write_text("#include <cstdint>\n#include <string>\n#include <vector>\n" + summary + harness)
        compiled = subprocess.run(["c++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        self.assertEqual(compiled.returncode, 0)
        executed = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
        self.assertEqual(executed.returncode, 0)
        self.assertEqual(executed.stdout + executed.stderr, b"")

    def test_virsh_failure_retains_bounded_reason_without_private_stderr(self):
        backend = object.__new__(gate.RealBackend)
        backend.diagnostics = []
        canary = b"Permission denied PRIVATE-AUTHORED-CANARY " * 300
        with mock.patch.object(gate, "bounded_command", return_value=(9, b"", canary)):
            with self.assertRaisesRegex(gate.GateError, "virsh-operation-failed"):
                backend.virsh("dominfo", gate.DOMAIN)
        self.assertEqual("permission-denied", backend.diagnostics[0]["reason"])
        self.assertEqual("dominfo", backend.diagnostics[0]["operation"])
        self.assertTrue(backend.diagnostics[0]["stderr_truncated"])
        self.assertNotIn("CANARY", json.dumps(backend.diagnostics))

    def test_owned_start_failure_uses_same_sanitized_diagnostic(self):
        backend = object.__new__(gate.RealBackend)
        backend.owned = mock.Mock()
        backend.diagnostics = []
        backend.owned.start.side_effect = RuntimeError("Permission denied PRIVATE-AUTHORED-CANARY")
        with self.assertRaisesRegex(gate.GateError, "owned-transient-start-failed"):
            backend.start_vm()
        self.assertEqual("permission-denied", backend.diagnostics[0]["reason"])
        self.assertNotIn("CANARY", json.dumps(backend.diagnostics))

    def test_owned_cleanup_false_becomes_gate_failure(self):
        backend = object.__new__(gate.RealBackend)
        backend.owned = mock.Mock()
        backend.owned.stop.return_value = False
        with self.assertRaisesRegex(gate.GateError, "owned-transient-cleanup-unverified"):
            backend.stop_vm(90)

    def test_complete_dry_path_and_lock_order(self):
        result, events = self.exercise()
        self.assertTrue(result["pass"])
        self.assertEqual(events, ["lock-heavy", "lock-gpu", "lock-vm", "preflight", "start-vm", "wait-guest", "stage", "ack-control",
            "create-task", "start-task", "read-ready", "ack", "read-done", "task-exited", "readback", "end-task-normal", "stop-vm",
            "unlock-vm", "unlock-gpu", "unlock-heavy"])

    def test_failed_lock_preserved_without_vm_actions(self):
        result, events = self.exercise("lock-gpu")
        self.assertFalse(result["pass"])
        self.assertEqual(events, ["lock-heavy", "lock-gpu", "unlock-heavy"])

    def test_preflight_refusal_never_stops_someone_elses_vm(self):
        result, events = self.exercise("preflight")
        self.assertFalse(result["vm_start_attempted"])
        self.assertNotIn("start-vm", events); self.assertNotIn("stop-vm", events)

    def test_ambiguous_start_failure_still_stops_owned_attempt(self):
        result, events = self.exercise("start-vm")
        self.assertTrue(result["vm_start_attempted"])
        self.assertFalse(result["vm_started"])
        self.assertIn("stop-vm", events); self.assertNotIn("end-task-forced", events)

    def test_boot_and_staging_failure_preserve_failure_and_owned_vm_cleanup(self):
        for point in ("wait-guest", "stage", "create-task"):
            with self.subTest(point=point):
                target = self.root / point; target.mkdir()
                backend = FakeBackend(metadata(), point)
                result = gate.run_gate(target, metadata(), backend)
                self.assertFalse(result["pass"]); self.assertIn("stop-vm", backend.events)
                self.assertNotIn("end-task-forced", backend.events)

    def test_task_failure_ends_only_created_task_before_vm_shutdown(self):
        result, events = self.exercise("start-task")
        self.assertFalse(result["pass"])
        self.assertLess(events.index("end-task-forced"), events.index("stop-vm"))

    def test_native_failure_receipt_is_not_reclassified_or_overwritten(self):
        receipt = done(metadata()); receipt["cases"][1]["exit"] = 7
        result, events = self.exercise(native=receipt)
        self.assertFalse(result["pass"])
        self.assertEqual(json.loads((self.root / "receipts/done.json").read_text()), receipt)
        self.assertIn("end-task-forced", events)
        with self.assertRaises(FileExistsError):
            gate.run_gate(self.root, metadata(), FakeBackend(metadata()))

    def test_cleanup_failures_cannot_pass(self):
        result, _ = self.exercise("stop-vm")
        self.assertFalse(result["pass"])
        self.assertEqual(result["cleanup_failures"], ["owned-vm-shutdown"])

    def test_stale_incomplete_forced_secret_and_elevated_receipts_fail(self):
        changes = [lambda r: r.update(run="old-run"), lambda r: r.update(standard_user=False),
            lambda r: r.update(inputs_unchanged=False), lambda r: r["cases"].pop(),
            lambda r: r["cases"].reverse(), lambda r: r["cases"][0].update(forced=True),
            lambda r: r["cases"][0].update(no_forbidden_output=False), lambda r: r["cases"][0].update(exit=False),
            lambda r: r["cases"][0].update(summary_valid=False),
            lambda r: r["cases"][0].update(stdout_bytes=1024 * 1024 + 1), lambda r: r.update(extra="untrusted-field")]
        for change in changes:
            receipt = done(metadata()); change(receipt)
            with self.assertRaises(gate.GateError): gate.validate_done(receipt, metadata())

    def test_pid_reuse_requires_distinct_creation_identity(self):
        receipt = done(metadata()); receipt["cases"][1]["pid"] = receipt["cases"][0]["pid"]
        gate.validate_done(receipt, metadata())
        receipt["cases"][1]["creation_time"] = receipt["cases"][0]["creation_time"]
        with self.assertRaises(gate.GateError): gate.validate_done(receipt, metadata())

    def test_path_and_regular_file_boundaries(self):
        for name in ("../outside", "/absolute", "a//b", "a/./b", "a/../b", "a\\b", "x:stream", "a/", "a."):
            self.assertFalse(gate.relative(name), name)
        source = self.root / "source"; source.write_bytes(b"fabricated")
        link = self.root / "link"; link.symlink_to(source)
        with self.assertRaises(gate.GateError): gate.plain_file(link)
        os.link(source, self.root / "twin")
        with self.assertRaises(gate.GateError): gate.plain_file(source)

    def test_pe_import_inventory_and_wrong_abi_refusal(self):
        self.assertEqual(gate.pe_imports(fake_pe(2, "KERNEL32.dll")), (2, {"kernel32.dll"}))
        value = bytearray(fake_pe()); struct.pack_into("<H", value, 132, 0x8664)
        with self.assertRaises(gate.GateError): gate.pe_imports(bytes(value))
        with self.assertRaises(gate.GateError): gate.pe_imports(b"untrusted non-executable")
        value = bytearray(fake_pe()); struct.pack_into("<I", value, 352, 4096)
        with self.assertRaises(gate.GateError): gate.pe_imports(bytes(value))

    def test_duplicate_receipt_fields_are_refused(self):
        with self.assertRaisesRegex(gate.GateError, "duplicate-json-field"):
            gate.read_json(b'{"pass":false,"pass":true}')
        with self.assertRaisesRegex(gate.GateError, "json-object-required"):
            gate.read_json(b'[]')

    def selection(self):
        def pin(name, value):
            path = self.root / name; path.write_bytes(value)
            return {"path": str(path), "sha256": gate.digest(value)}
        binding = {"repository": "unused-fake-repository", "commit": "1" * 40,
            "files": {"port/network/fabricated.cpp": "9" * 64}, "build_receipt": pin("receipt.json", b"{}\n")}
        return {"schema": 1, "run_id": metadata()["run_id"], "candidate": binding, "runner": copy.deepcopy(binding),
            "toolchain_identity": fake_toolchain(self.root), "grant_progress_lines": ["policy progress", "file progress", "request progress", "directory progress"], "supervisor": pin("supervisor.exe", fake_pe(2, "KERNEL32.dll")),
            "cases": [{"name": name, "exe": pin(name + ".exe", fake_pe(3, "KERNEL32.dll")), "dlls": [], "argument_kind": "root"} for name in gate.ROLES],
            "forbidden_output_tokens": ["fabricated-never-log-token"], "inactive_domain_sha256": "3" * 64, "guest_user": "fixture",
            "execution_paths": {"heavy_lock": str(self.root / "onslaught-heavy.lock"), "gpu_lock": str(self.root / "godot-gpu.lock"),
                "vm_home": str(self.root / "windows-vm"), "coordination_log": str(self.root / "COORDINATION.md")}, "owned_backend": {}}

    def test_preparation_is_offline_seals_bytes_and_refuses_changed_payload(self):
        selection = self.selection(); out = self.root / "prepared"
        binding = {"commit": "1" * 40, "files": selection["candidate"]["files"], "build_receipt_sha256": selection["candidate"]["build_receipt"]["sha256"]}
        with mock.patch.object(gate, "source_binding", return_value=binding), mock.patch.object(gate, "bounded_command", side_effect=AssertionError("no host/guest command expected")):
            sealed = gate.prepare(selection, out)
            self.assertEqual(gate.verify_prepared(out), sealed)
            self.assertNotIn(b"--wine-synthetic", (out / "payload/gate-plan.txt").read_bytes())
            with self.assertRaises(gate.GateError): gate.prepare(selection, out)
            (out / "payload/package/grant/grant.exe").write_bytes(fake_pe() + b"changed")
            with self.assertRaises(gate.GateError): gate.verify_prepared(out)

    def test_missing_dll_and_changed_artifact_fail_before_prepared_root_creation(self):
        selection = self.selection(); out = self.root / "prepared"
        value = fake_pe(3, "missing-runtime.dll"); path = Path(selection["cases"][0]["exe"]["path"])
        path.write_bytes(value); selection["cases"][0]["exe"]["sha256"] = gate.digest(value)
        with mock.patch.object(gate, "source_binding", return_value={"commit": "1" * 40}):
            with self.assertRaisesRegex(gate.GateError, "runtime-dll-closure-missing"): gate.prepare(selection, out)
            self.assertFalse(out.exists())
            selection["cases"][0]["exe"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(gate.GateError, "pin-mismatch"): gate.prepare(selection, out)
            self.assertFalse(out.exists())

    def test_frozen_source_binding_rejects_different_commit_bytes(self):
        selection = self.selection()
        with mock.patch.object(gate, "bounded_command", return_value=(0, b"different source", b"")):
            with self.assertRaisesRegex(gate.GateError, "frozen-source-mismatch"):
                gate.source_binding(selection["candidate"])

    def test_execution_paths_are_part_of_the_sealed_selection(self):
        selection = self.selection(); out = self.root / "prepared"
        binding = {"commit": "1" * 40, "files": selection["candidate"]["files"], "build_receipt_sha256": selection["candidate"]["build_receipt"]["sha256"]}
        with mock.patch.object(gate, "source_binding", return_value=binding):
            sealed = gate.prepare(selection, out)
            self.assertEqual(sealed["execution_paths"], selection["execution_paths"])
            sealed["execution_paths"]["vm_home"] = str(self.root / "elsewhere" / "windows-vm")
            (out / "sealed.json").write_bytes(gate.json_bytes(sealed))
            with self.assertRaisesRegex(gate.GateError, "execution-path-binding-changed"):
                gate.verify_prepared(out)
        for key in selection["execution_paths"]:
            paths = dict(selection["execution_paths"]); paths[key] = "relative"
            with self.assertRaises(gate.GateError): gate.execution_paths(paths)

    def test_task_is_hidden_least_privilege_and_bound_to_exact_manifest(self):
        value = gate.task_xml("C:\\ce-native-" + metadata()["run_id"], metadata()["run_id"], "2" * 64, "S-1-5-21-111-222-333-1001").decode("utf-16")
        self.assertIn("<RunLevel>LeastPrivilege</RunLevel>", value)
        self.assertIn("<LogonType>InteractiveToken</LogonType>", value)
        self.assertIn("<Hidden>true</Hidden>", value)
        self.assertIn("PT7M", value); self.assertIn("2" * 64, value)
        for forbidden in ("PowerShell", "HighestAvailable", "game.exe", "--wine-synthetic"):
            self.assertNotIn(forbidden, value)

    def test_real_command_generation_against_fake_guest(self):
        selection = self.selection(); prepared = self.root / "prepared"
        binding = {"commit": "1" * 40, "files": selection["candidate"]["files"], "build_receipt_sha256": selection["candidate"]["build_receipt"]["sha256"]}
        with mock.patch.object(gate, "source_binding", return_value=binding):
            meta = gate.prepare(selection, prepared)
        commands = []; uploaded = {}; ack_files = {}; acked = False; registered = False; started = False; prepared_links = False

        class Adapter(gate.RealBackend):
            @contextlib.contextmanager
            def locks(self):
                yield
            def preflight(self, metadata):
                self.uuid = "fabricated-domain-identity"
            def start_vm(self):
                commands.append("OWNED-VM-START")
            def stop_vm(self, seconds):
                commands.append("OWNED-VM-STOP")

        backend = Adapter(meta)
        def transport(argv, *, data=None, timeout=15, limit=131072):
            nonlocal acked, registered, started, prepared_links
            self.assertEqual(argv[0], "ssh")
            self.assertIn("StrictHostKeyChecking=yes", argv)
            self.assertIn("UpdateHostKeys=no", argv)
            command = argv[-1]; commands.append(command)
            if command.endswith("echo CE-ICE-READY"): return 0, b"CE-ICE-READY\r\n", b""
            if command == "whoami /user /fo csv /nh": return 0, b'"FAKE\\fixture","S-1-5-21-111-222-333-1001"\r\n', b""
            if command.startswith("tar -xf"):
                with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                    for item in archive:
                        self.assertTrue(item.isfile())
                        uploaded[item.name] = archive.extractfile(item).read()
                return 0, b"", b""
            if command.startswith("certutil "):
                path = command.split('"')[1]
                relative = path[len(backend.root) + 1:].replace("\\", "/")
                self.assertIn(relative, uploaded)
                return 0, (gate.digest(uploaded[relative]) + "\r\n").encode(), b""
            if " --prepare-native-reparse " in command:
                self.assertFalse(registered or started)
                self.assertIn("\\package\\grant\\", command)
                prepared_links = True
                return 0, b"PASS prepared native reparse fixtures=6\r\n", b""
            if command.startswith("schtasks /Create"):
                self.assertNotIn(" /F", command)
                self.assertFalse(registered); self.assertTrue(prepared_links); registered = True
                self.assertIn("LeastPrivilege", uploaded["task.xml"].decode("utf-16"))
                return 0, b"", b""
            if command.startswith("schtasks /Run"):
                self.assertTrue(registered); started = True
                return 0, b"", b""
            if command.startswith("schtasks /Query"):
                self.assertTrue(started)
                return 0, ('"TaskName","Status","Last Result"\r\n"\\' + backend.task + '","Ready","0"\r\n').encode(), b""
            if "\\output\\ready.json" in command:
                self.assertTrue(started); self.assertFalse(acked)
                return 0, gate.json_bytes({"schema": 1, "run": meta["run_id"], "manifest_sha256": meta["native_manifest_sha256"], "standard_user": True, "windows_build": 26100}), b""
            if "\\output\\done.json" in command:
                self.assertTrue(acked)
                return 0, gate.json_bytes(done(meta)), b""
            if "&& ren " in command:
                root = command.split("if exist ", 1)[1].split("\\ack.txt", 1)[0]
                path = root + "\\ack.txt"
                if path in ack_files: return 75, b"", b""
                if root == backend.root:
                    self.assertTrue(started); acked = True
                else: self.assertFalse(started)
                ack_files[path] = meta["run_id"].encode() + b"\r\n"
                self.assertIn("ack.tmp ack.txt", command)
                return 0, b"", b""
            if " (type " in command:
                path = command.split("if exist ", 1)[1].split(" (type ", 1)[0]
                return (0, ack_files[path], b"") if path in ack_files else (76, b"", b"")
            self.assertTrue(command.startswith(("cmd.exe /d /c ", "icacls ")), command)
            return 0, b"", b""

        with mock.patch.object(gate, "bounded_command", side_effect=transport):
            result = gate.run_gate(prepared, meta, backend)
        self.assertTrue(result["pass"])
        self.assertTrue(acked and registered and started)
        self.assertEqual(set(uploaded), set(meta["files"]) | {"task.xml"})
        transcript = "\n".join(commands).lower()
        for prohibited in ("snapshot", "vm-run-ce", "powershell", "steam", "savegames", "provider", "--wine-synthetic", " /delete", " /f "):
            self.assertNotIn(prohibited, transcript)
        self.assertEqual(commands[-1], "OWNED-VM-STOP")

    def test_verify_and_execute_require_the_sealed_runner_modules(self):
        files = {"port/tools/ice_windows_native_gate.py": gate.digest(gate.plain_file(Path(gate.__file__))),
                 "port/tools/ice_windows_owned_backend.py":
                     gate.digest(gate.plain_file(Path(gate.owned_backend.__file__)))}
        gate.require_sealed_runner_modules({"runner": {"files": dict(files, **{"port/other.cpp": "0" * 64})}})
        for name in files:
            for pins in (dict(files, **{name: "0" * 64}), {k: v for k, v in files.items() if k != name}):
                with self.subTest(name=name, pins=len(pins)), self.assertRaisesRegex(gate.GateError, "runner-module-drift"):
                    gate.require_sealed_runner_modules({"runner": {"files": pins}})
        stale = dict(files, **{"port/tools/ice_windows_owned_backend.py": "0" * 64})
        for pins, verified in ((stale, False), (files, True)):
            for action in (["verify", "prepared"], ["execute", "prepared", "--independent-review", "review.json"]):
                if verified and action[0] == "execute":
                    continue
                output = io.StringIO()
                with self.subTest(action=action[0], verified=verified), \
                        mock.patch.object(gate, "verify_prepared",
                                          return_value={"run_id": "fixture", "runner": {"files": pins}}), \
                        mock.patch.object(gate, "run_gate") as run, mock.patch.object(gate, "read_json") as review, \
                        mock.patch("sys.argv", ["gate", *action]), contextlib.redirect_stdout(output):
                    self.assertEqual(0 if verified else 1, gate.main())
                    self.assertEqual({"verified": True, "run_id": "fixture"} if verified else
                                     {"pass": False, "failure": "runner-module-drift"}, json.loads(output.getvalue()))
                    run.assert_not_called()
                    review.assert_not_called()


def tripped_machine(*, cleanup_verified=True):
    """The part of an owned machine the gate reads: its trip event, its guard and its report."""
    machine = NS(space_tripped=threading.Event(), report={"cleanup_verified": cleanup_verified})

    def guard():
        if machine.space_tripped.is_set():
            raise gate.owned_backend.OwnedError("owned-space-watchdog-tripped")
    machine.space_guard = guard
    return machine


class SpaceTripTests(unittest.TestCase):
    """A tripped space watchdog fails the run now, names itself as the cause and cannot be passed."""
    def setUp(self):
        root = HERE.parents[1] / "local-data" / "native-fixture-unit"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.scratch = tempfile.TemporaryDirectory(prefix="trip-", dir=root)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)

    def real_backend(self, machine):
        backend = object.__new__(gate.RealBackend)
        backend.owned, backend.deadline, backend.ssh = machine, None, ["ssh-authored"]
        backend.receipts, backend.root, backend.run_id = self.root, "C:\\ce-native-authored", "ice-authored"
        backend.task, backend.task_hash, backend.root_owned, backend.diagnostics = "CE-ICE-authored", None, True, []
        backend.ack_report = {"control": None, "primary": None, "operations": []}
        return backend

    def test_every_guest_step_and_wait_refuses_before_it_asks_the_guest_anything(self):
        machine = tripped_machine()
        machine.space_tripped.set()
        backend = self.real_backend(machine)
        steps = [lambda: backend.guest_result("whoami"), lambda: backend.guest("whoami"),
                 lambda: backend.wait_guest(180), lambda: backend.read_json("ready", 30),
                 lambda: backend.read_json("done", 390), lambda: backend.wait_task_exit(10),
                 lambda: backend.create_task(), lambda: backend.start_task(), lambda: backend.finish_task(force=True)]
        with mock.patch.object(gate, "bounded_command", side_effect=AssertionError("the guest was asked")), \
                mock.patch.object(gate.time, "sleep", side_effect=AssertionError("a wait went on after the trip")):
            for index, step in enumerate(steps):
                began = time.monotonic()
                with self.subTest(step=index), self.assertRaisesRegex(gate.GateError, "^owned-space-watchdog-tripped$"):
                    step()
                self.assertLess(time.monotonic() - began, 1.0)        # now, not at a 180 s or 390 s deadline

    def test_the_best_effort_readback_after_a_trip_collects_nothing_and_does_not_raise(self):
        machine = tripped_machine()
        machine.space_tripped.set()
        backend = self.real_backend(machine)
        with mock.patch.object(gate, "bounded_command", side_effect=AssertionError("the guest was asked")):
            report = backend.readback(metadata() | {"files": {"a.exe": {"sha256": "0" * 64, "size": 1}}}, best_effort=True)
        self.assertEqual((False, ["unavailable"]), (report["complete"], [row["status"] for row in report["files"]]))

    def test_a_quiet_guard_and_a_run_without_an_owned_machine_change_nothing(self):
        for owned_machine in (tripped_machine(), None):
            with self.subTest(owned=owned_machine is not None):
                backend = self.real_backend(owned_machine)
                with mock.patch.object(gate, "bounded_command", return_value=(0, b"CE-ICE-READY\r\n", b"")) as ask:
                    backend.wait_guest(180)
                    self.assertEqual((0, b"CE-ICE-READY\r\n"), backend.guest("echo", allow_failure=True))
                self.assertEqual(2, ask.call_count)
        bare = object.__new__(gate.RealBackend)                    # no owned attribute at all
        bare.deadline, bare.ssh = None, ["ssh-authored"]
        with mock.patch.object(gate, "bounded_command", return_value=(0, b"", b"")):
            bare.guest_result("echo")

    def test_a_command_in_flight_is_ended_at_the_trip(self):
        """A guest that is frozen but not yet destroyed keeps its ssh connection open: the call must not wait for
        its own deadline, and the child it started must be gone."""
        flag = threading.Event()
        timer = threading.Timer(0.3, flag.set)
        timer.start()
        self.addCleanup(timer.cancel)
        began = time.monotonic()
        with self.assertRaises(gate.BoundedCommandFailure) as caught:
            gate.bounded_command(["sleep", "30"], timeout=30, cancel=flag.is_set)
        self.assertEqual("command-cancelled", caught.exception.reason)
        self.assertLess(time.monotonic() - began, 3)
        self.assertIsNotNone(caught.exception.code)             # the child was waited for: it is not left running

    def test_in_flight_command_detects_stalled_sensor_before_any_trip_event(self):
        """Actual command cancellation plus the real adapter, authored sensor/provider, no SSH or VM."""
        import ice_windows_owned_backend_tests as fixture
        world = fixture.World(self.root / "machine")
        release, reading = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        original_statvfs = world.statvfs
        count = [0]
        def hung_sensor(path):
            count[0] += 1
            if count[0] > 2:
                reading.set()
                release.wait(10)
            return original_statvfs(path)
        world.statvfs = hung_sensor
        run = {"run_id": "authored-native-run", "owned_backend": {"run_id": "wg-20261102T1600Z-authored-01"},
               "execution_paths": {"vm_home": str(world.home)}, "files": fixture.STAGED["files"]}
        machine = gate.owned_backend.OwnedTransient(run, modules=world.modules(), **world.kwargs(poll_seconds=0.02))
        with mock.patch.object(gate.owned_backend, "version_probe", return_value="authored-version"), \
                mock.patch.object(gate.owned_backend, "cmdline", side_effect=lambda pid: b"owned\0" + world.uuid.encode() + b"\0"):
            machine.start(630)
        backend = self.real_backend(machine)
        backend.ssh = [sys.executable, "-c", "import time; time.sleep(10)"]
        real_command = gate.bounded_command
        def in_flight(argv, **kwargs):
            self.assertFalse(machine.space_tripped.is_set())
            world.advance(gate.owned_backend.STALE_SECONDS + 1)
            return real_command(argv, **kwargs)
        try:
            self.assertTrue(reading.wait(2))
            self.assertFalse(machine.space_tripped.is_set())
            began = time.monotonic()
            with mock.patch.object(gate, "bounded_command", side_effect=in_flight):
                with self.assertRaises(gate.BoundedCommandFailure) as caught:
                    backend.guest_result("authored-argument", timeout=2)
            self.assertEqual("command-cancelled", caught.exception.reason)
            self.assertIsNotNone(caught.exception.code)
            self.assertLess(time.monotonic() - began, 1)
            self.assertTrue(machine.space_tripped.is_set())
            self.assertEqual("space-watch-stalled", machine.report["space_watchdog"]["trip"])
        finally:
            release.set()
            machine.watch_stop.set()
            machine.watch_thread.join(2)
            if machine.trip_stop_thread is not None:
                machine.trip_stop_thread.join(2)
            machine.stop(1)

    def test_a_command_without_a_cancel_is_unchanged_and_a_quiet_cancel_changes_nothing(self):
        self.assertEqual((0, b"hello\n", b""), gate.bounded_command(["echo", "hello"]))
        self.assertEqual((0, b"hello\n", b""), gate.bounded_command(["echo", "hello"], cancel=lambda: False))
        with self.assertRaises(gate.BoundedCommandFailure) as caught:
            gate.bounded_command(["sleep", "30"], timeout=0.3, cancel=lambda: False)
        self.assertEqual("command-deadline", caught.exception.reason)

    def test_the_gate_hands_the_trip_to_every_guest_command_only_when_it_has_an_owned_machine(self):
        machine = tripped_machine()
        backend = self.real_backend(machine)
        seen = []
        with mock.patch.object(gate, "bounded_command", side_effect=lambda argv, **kwargs: (seen.append(kwargs), (0, b"", b""))[1]):
            backend.guest_result("echo")
            self.assertFalse(seen[0]["cancel"]())
            machine.space_tripped.set()
            self.assertTrue(seen[0]["cancel"]())
            seen.clear()
            backend.owned = None
            backend.guest_result("echo")
        self.assertNotIn("cancel", seen[0])

    def test_a_wait_notices_a_trip_that_happens_during_it(self):
        machine = tripped_machine()
        backend = self.real_backend(machine)
        calls = []

        def ask(*args, **kwargs):
            calls.append(args)
            if len(calls) == 2:
                machine.space_tripped.set()         # the watchdog stops the guest while the gate is waiting for it
            if len(calls) > 4:
                raise AssertionError("the wait went on after the trip")
            return 255, b"", b"connection reset"
        sleeps = []

        def sleep(seconds):
            sleeps.append(seconds)
            if len(sleeps) > 3:             # the right code sleeps twice before it sees the trip
                raise AssertionError("the wait went on after the trip")
        with mock.patch.object(gate, "bounded_command", side_effect=ask), mock.patch.object(gate.time, "sleep", sleep):
            with self.assertRaisesRegex(gate.GateError, "^owned-space-watchdog-tripped$"):
                backend.wait_guest(180)
        self.assertEqual((2, 2), (len(calls), len(sleeps)))

    def test_stop_vm_says_which_failed_the_trip_or_the_cleanup(self):
        for tripped, verified, expected in [(True, True, "owned-space-watchdog-tripped"),
                                            (True, False, "owned-transient-cleanup-unverified"),
                                            (False, True, "owned-transient-cleanup-unverified"),
                                            (False, False, "owned-transient-cleanup-unverified")]:
            with self.subTest(tripped=tripped, verified=verified):
                machine = tripped_machine(cleanup_verified=verified)
                if tripped:
                    machine.space_tripped.set()
                machine.stop = lambda seconds: False
                backend = self.real_backend(machine)
                with self.assertRaisesRegex(gate.GateError, "^" + expected + "$"):
                    backend.stop_vm(90)
        machine = tripped_machine()
        machine.stop = lambda seconds: True
        self.real_backend(machine).stop_vm(90)                       # a clean stop raises nothing

    def test_a_mock_machine_is_never_taken_for_a_tripped_one(self):
        backend = self.real_backend(mock.Mock())
        self.assertFalse(gate.space_tripped(backend))
        backend.owned.stop.return_value = False
        with self.assertRaisesRegex(gate.GateError, "^owned-transient-cleanup-unverified$"):
            backend.stop_vm(90)
        self.assertFalse(gate.space_tripped(object()))
        self.assertFalse(gate.space_tripped(NS(owned=None)))

    def exercise(self, backend):
        folder = Path(tempfile.mkdtemp(dir=self.root))
        result = gate.run_gate(folder, metadata(), backend)
        self.assertEqual(json.loads((folder / "receipts/result.json").read_text()), result)
        self.assertIn("stop-vm", backend.events)         # the owned machine is always stopped, tripped or not
        return result

    def tripping_backend(self, when, *, noticed_as="guest-operation-failed", stop_raises=None, finish_raises=None):
        meta = metadata()

        class Backend(FakeBackend):
            def event(this, name):
                if name == when:
                    this.owned.space_tripped.set()
                    if noticed_as is not None:
                        raise gate.GateError(noticed_as)
                super().event(name)

            def stop_vm(this, seconds):
                super().stop_vm(seconds)
                if stop_raises is not None:
                    raise gate.GateError(stop_raises)

            def finish_task(this, force):
                super().finish_task(force)
                if finish_raises is not None:
                    raise gate.GateError(finish_raises)
        backend = Backend(meta)
        backend.owned = tripped_machine()
        return backend

    def test_the_trip_is_the_failure_named_when_a_guest_step_noticed_the_dead_guest_first(self):
        for when in ("wait-guest", "stage", "start-task", "read-ready", "read-done"):
            with self.subTest(when=when):
                result = self.exercise(self.tripping_backend(when))
                self.assertFalse(result["pass"])
                self.assertEqual("owned-space-watchdog-tripped", result["failure"])
                self.assertEqual("guest-operation-failed", result["first_failure_seen"])
                self.assertEqual([], result["cleanup_failures"])

    def test_the_trip_is_the_failure_named_when_the_gate_itself_raised_it(self):
        result = self.exercise(self.tripping_backend("read-ready", noticed_as="owned-space-watchdog-tripped"))
        self.assertEqual(("owned-space-watchdog-tripped", False), (result["failure"], result["pass"]))
        self.assertNotIn("first_failure_seen", result)

    def test_a_trip_in_the_last_second_cannot_pass_a_run_whose_steps_all_passed(self):
        backend = self.tripping_backend("stop-vm", noticed_as=None, stop_raises="owned-space-watchdog-tripped")
        result = self.exercise(backend)
        self.assertFalse(result["pass"])
        self.assertEqual("owned-space-watchdog-tripped", result["failure"])
        self.assertEqual([], result["cleanup_failures"])         # the cleanup was verified; the run failed on the trip
        self.assertIn("read-done", backend.events)

    def test_a_trip_that_no_step_and_no_stop_noticed_still_fails_the_run(self):
        """The watchdog stopped the guest after the last guest step, and stop_vm saw nothing wrong: the run still cannot pass."""
        backend = self.tripping_backend("read-done", noticed_as=None)
        result = self.exercise(backend)
        self.assertEqual(("owned-space-watchdog-tripped", False, []), (result["failure"], result["pass"], result["cleanup_failures"]))
        self.assertNotIn("first_failure_seen", result)
        self.assertIn("task-exited", backend.events)             # every step really had passed

    def test_a_trip_with_a_cleanup_that_failed_reports_both(self):
        backend = self.tripping_backend("read-ready", stop_raises="owned-transient-cleanup-unverified")
        result = self.exercise(backend)
        self.assertEqual(("owned-space-watchdog-tripped", ["owned-vm-shutdown"], False),
                         (result["failure"], result["cleanup_failures"], result["pass"]))

    def test_ending_the_task_of_a_guest_stopped_for_space_is_not_a_cleanup_failure(self):
        result = self.exercise(self.tripping_backend("read-ready", finish_raises="owned-space-watchdog-tripped"))
        self.assertEqual([], result["cleanup_failures"])
        other = self.exercise(self.tripping_backend("read-ready", finish_raises="owned-task-end-unconfirmed"))
        self.assertEqual(["owned-task-cleanup"], other["cleanup_failures"])

    def test_a_run_whose_machine_never_tripped_is_unchanged(self):
        backend = self.tripping_backend("never", noticed_as=None)
        result = self.exercise(backend)
        self.assertTrue(result["pass"])
        self.assertIsNone(result["failure"])
        self.assertNotIn("first_failure_seen", result)


class GuestFailureTests(unittest.TestCase):
    """A guest command that fails says which step it was, with what exit code and what it printed (private files)."""
    def setUp(self):
        root = HERE.parents[1] / "local-data" / "native-fixture-unit"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.scratch = tempfile.TemporaryDirectory(prefix="guestfail-", dir=root)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)

    def backend(self, result=(1, b"", b""), receipts="here"):
        backend = object.__new__(gate.RealBackend)          # no __init__: only what these tests set
        backend.owned, backend.deadline, backend.ssh = None, None, ["ssh-authored"]
        backend.receipts = self.root if receipts == "here" else receipts
        backend.root, backend.run_id, backend.sid = "C:\\ce-native-ice-authored", "ice-authored", "S-1-5-21-1-2-3-1001"
        backend.task, backend.task_hash, backend.root_owned = "CE-ICE-ice-authored", None, True
        backend.ack_report = {"control": None, "primary": None, "operations": []}
        backend.script = []

        def transport(command, *, data=None, timeout=15, limit=131072):
            outcome = backend.script.pop(0) if backend.script else result
            if isinstance(outcome, BaseException):
                raise outcome
            return outcome
        backend.guest_result = transport
        return backend

    def private_files(self):
        return sorted(p.name for p in self.root.glob("guest-*.private.bin"))

    def test_a_failed_command_records_its_step_exit_code_and_bounded_private_output(self):
        out, err = b"stdout PRIVATE-OUT\r\n", b"Access is denied. PRIVATE-CANARY\r\n"
        backend = self.backend((7, out, err))
        with self.assertRaisesRegex(gate.GateError, "^guest-operation-failed$"):
            backend.guest("icacls authored", label="stage-grant-root")
        self.assertEqual(1, len(backend.diagnostics))
        row = backend.diagnostics[0]
        self.assertEqual(("guest", "stage-grant-root", 1, 7, "nonzero-exit", gate.digest(b"icacls authored")),
                         (row["stage"], row["operation"], row["index"], row["returncode"], row["reason"],
                          row["command_sha256"]))
        for name, value in (("stdout", out), ("stderr", err)):
            filename = f"guest-01-stage-grant-root.{name}.private.bin"
            self.assertEqual({"bytes": len(value), "sha256": gate.digest(value), "retained_bytes": len(value),
                              "truncated": False, "file": filename}, row[name])
            self.assertEqual(value, (self.root / filename).read_bytes())
            self.assertEqual(0o600, os.stat(self.root / filename).st_mode & 0o777)
        self.assertNotIn("PRIVATE", json.dumps(backend.diagnostics))        # sizes and hashes, never the text

    def test_output_beyond_the_bound_is_cut_hashed_whole_and_marked(self):
        err = bytes(range(256)) * 40 + b"tail"
        backend = self.backend((1, b"", err))
        with self.assertRaises(gate.GateError):
            backend.guest("x", label="create-task")
        row = backend.diagnostics[0]
        entry = row["stderr"]
        self.assertEqual((len(err), gate.digest(err), gate.GUEST_FAILURE_BYTES, True),
                         (entry["bytes"], entry["sha256"], entry["retained_bytes"], entry["truncated"]))
        self.assertEqual(err[:gate.GUEST_FAILURE_BYTES], (self.root / entry["file"]).read_bytes())
        self.assertEqual({"bytes": 0, "sha256": gate.digest(b""), "retained_bytes": 0, "truncated": False, "file": None},
                         row["stdout"])                                  # nothing printed: no file
        self.assertEqual(["guest-01-create-task.stderr.private.bin"], self.private_files())

    def test_a_transport_failure_is_recorded_with_its_reason_and_raised_unchanged(self):
        failure = gate.BoundedCommandFailure(None, b"partial", b"ssh: PRIVATE-CANARY timed out", "command-deadline")
        backend = self.backend()
        backend.script = [failure]
        with self.assertRaises(gate.BoundedCommandFailure) as caught:
            backend.guest("x", timeout=5, allow_failure=True, label="wait-guest")   # an allowed exit excuses no transport failure
        self.assertIs(failure, caught.exception)
        row = backend.diagnostics[0]
        self.assertEqual(("wait-guest", None, "command-deadline"), (row["operation"], row["returncode"], row["reason"]))
        self.assertEqual(b"ssh: PRIVATE-CANARY timed out", (self.root / row["stderr"]["file"]).read_bytes())
        self.assertNotIn("PRIVATE", json.dumps(backend.diagnostics))

    def test_an_allowed_nonzero_exit_is_returned_and_recorded_only_when_the_caller_explains_it(self):
        backend = self.backend((3, b"o", b"e"))
        self.assertEqual((3, b"o"), backend.guest("x", allow_failure=True, label="read-done"))
        self.assertEqual([], getattr(backend, "diagnostics", []))             # a poll that expects to fail is not recorded
        self.assertEqual((3, b"o"), backend.guest("x", allow_failure=True, explain=True, label="end-task"))
        self.assertEqual(["end-task"], [row["operation"] for row in backend.diagnostics])
        fine = self.backend((0, b"fine", b""))
        self.assertEqual((0, b"fine"), fine.guest("x", explain=True, label="stage-whoami"))
        self.assertEqual([], getattr(fine, "diagnostics", []))                # success is never a record

    def test_a_failure_before_the_receipts_folder_is_known_keeps_its_row_without_files(self):
        for case in ("none", "absent"):
            with self.subTest(receipts=case):
                backend = self.backend((1, b"o", b"e"), receipts=None)
                if case == "absent":
                    del backend.receipts
                with self.assertRaises(gate.GateError):
                    backend.guest("x", label="stage-whoami")
                row = backend.diagnostics[0]
                self.assertEqual(([None, None], (1, 1)), ([row["stdout"]["file"], row["stderr"]["file"]],
                                                          (row["stdout"]["bytes"], row["stderr"]["bytes"])))
                self.assertEqual([], self.private_files())

    def test_the_rows_are_capped_and_the_rest_counted_in_one_last_row(self):
        backend = self.backend((1, b"o", b"e"))
        for _ in range(gate.GUEST_FAILURE_RECORDS + 9):
            with self.assertRaises(gate.GateError):
                backend.guest("x", label="read-ready")
        rows = backend.diagnostics
        self.assertEqual(gate.GUEST_FAILURE_RECORDS + 1, len(rows))
        self.assertEqual(list(range(1, gate.GUEST_FAILURE_RECORDS + 1)), [row["index"] for row in rows[:-1]])
        self.assertEqual({"stage": "guest", "operation": "more-failures", "count": 9}, rows[-1])
        self.assertEqual(2 * gate.GUEST_FAILURE_RECORDS, len(self.private_files()))      # nothing written past the cap

    def test_a_receipts_folder_that_cannot_be_written_does_not_hide_the_failure(self):
        backend = self.backend((1, b"o", b"e"), receipts=self.root / "missing")
        with self.assertRaisesRegex(gate.GateError, "^guest-operation-failed$"):
            backend.guest("x", label="stage-extract")
        entry = backend.diagnostics[0]["stderr"]
        self.assertEqual((None, "FileNotFoundError"), (entry["file"], entry["error"]))

    def test_a_label_that_is_not_a_fixed_step_name_is_recorded_as_unlabelled(self):
        for bad in (None, "", "Has-Capitals", "x" * 49, "../escape", "with space", "-leading", "tail\n", 5, b"bytes"):
            with self.subTest(label=bad):
                backend = self.backend((1, b"o", b"e"))
                with self.assertRaises(gate.GateError):
                    backend.guest("x", label=bad)
                self.assertEqual("unlabelled", backend.diagnostics[0]["operation"])
                self.assertEqual(["guest-01-unlabelled.stderr.private.bin", "guest-01-unlabelled.stdout.private.bin"],
                                 self.private_files())
                for name in self.private_files():
                    (self.root / name).unlink()
        longest = "a" + "b" * 47                                          # exactly the longest allowed name
        backend = self.backend((1, b"", b"e"))
        with self.assertRaises(gate.GateError):
            backend.guest("x", label=longest)
        self.assertEqual(longest, backend.diagnostics[0]["operation"])

    def test_every_call_site_names_its_step(self):
        tree = ast.parse((HERE / "ice_windows_native_gate.py").read_text())
        sites = []
        for node in ast.walk(tree):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "guest" and
                    isinstance(node.func.value, ast.Name) and node.func.value.id == "self"):
                sites.append((node.lineno, next((k.value for k in node.keywords if k.arg == "label"), None)))
        self.assertGreaterEqual(len(sites), 14)
        for line, label in sites:
            with self.subTest(line=line):
                self.assertIsNotNone(label, "a guest() call without a label")
                if isinstance(label, ast.Constant):
                    self.assertIsNotNone(gate.STEP_LABEL.fullmatch(label.value))
                else:
                    self.assertEqual("'read-' + name", ast.unparse(label))     # read_json: a three-name allowlist

    def test_each_step_names_itself_when_its_guest_command_fails(self):
        cases = [("create_task", lambda b: b.create_task(), "create-task"),
                 ("start_task", lambda b: b.start_task(), "start-task"),
                 ("wait_task_exit", lambda b: b.wait_task_exit(5), "task-status"),
                 ("readback", lambda b: b.readback({"files": {"a.exe": {"sha256": "0" * 64, "size": 1}}}), "readback-hash")]
        for name, call, label in cases:
            with self.subTest(step=name):
                backend = self.backend((1, b"", b"Access is denied"))
                with self.assertRaises(gate.GateError):
                    call(backend)
                self.assertEqual((label, 1), (backend.diagnostics[0]["operation"], backend.diagnostics[0]["returncode"]))
        backend = self.backend()                                            # a poll's transport failure names its poll
        backend.script = [gate.BoundedCommandFailure(-15, b"", b"", "command-deadline")]
        with self.assertRaises(gate.BoundedCommandFailure):
            backend.read_json("done", 5)
        self.assertEqual("read-done", backend.diagnostics[0]["operation"])
        backend = self.backend((1, b"", b"No tasks are running"))
        with self.assertRaisesRegex(gate.GateError, "^owned-task-end-unconfirmed$"):
            backend.finish_task(force=True)                                 # the cleanup path explains itself too
        self.assertEqual(("end-task", 1), (backend.diagnostics[0]["operation"], backend.diagnostics[0]["returncode"]))

    def test_a_poll_that_expects_the_file_to_be_missing_records_nothing(self):
        clock = [0.0]
        backend = self.backend((1, b"", b"The system cannot find the file specified."))
        with mock.patch.object(gate.time, "monotonic", lambda: clock[0]), \
                mock.patch.object(gate.time, "sleep", lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            with self.assertRaisesRegex(gate.GateError, "^native-receipt-deadline$"):
                backend.read_json("ready", 3)
        self.assertEqual([], getattr(backend, "diagnostics", []))

    def stage_transport(self, backend, *, fail_at=None, good_hash=False, absent_exit=0):
        calls = []

        def transport(command, *, data=None, timeout=15, limit=131072):
            calls.append(command)
            if fail_at is not None and len(calls) == fail_at:
                return 1, b"", b"Access is denied PRIVATE-CANARY"
            if "whoami" in command:
                return 0, b'"HOST\\user","S-1-5-21-1-2-3-1001"\r\n', b""
            if "certutil" in command:
                return (0, backend.task_hash.encode() + b"\r\n", b"") if good_hash else (0, b"", b"")
            if "exit /b 74" in command:
                return absent_exit, b"", b"exists PRIVATE-CANARY" if absent_exit else b""
            return 0, b"", b""
        backend.guest_result = transport
        return calls

    def stage(self, backend):
        prepared = Path(tempfile.mkdtemp(dir=self.root))                    # stage() creates its files exclusively
        (prepared / "payload").mkdir()
        (prepared / "receipts").mkdir()
        backend.stage(prepared, metadata() | {"files": {}}, prepared / "receipts")

    def test_the_staging_commands_each_name_their_step(self):
        expected = ["stage-whoami", "stage-create-root", "stage-grant-root", "stage-owner-root", "stage-extract",
                    "stage-owner-tree"]
        for index, label in enumerate(expected):
            with self.subTest(label=label):
                backend = self.backend()
                calls = self.stage_transport(backend, fail_at=index + 1)
                with self.assertRaisesRegex(gate.GateError, "^guest-operation-failed$"):
                    self.stage(backend)
                self.assertEqual((label, index + 1), (backend.diagnostics[0]["operation"], len(calls)))

    def test_staging_names_its_absence_checks_and_an_unreadable_hash_is_not_a_command_failure(self):
        backend = self.backend()
        self.stage_transport(backend, good_hash=False)
        with self.assertRaisesRegex(gate.GateError, "^guest-file-readback$"):
            self.stage(backend)
        self.assertEqual([], getattr(backend, "diagnostics", []))             # the command succeeded; its answer was no hash
        backend = self.backend()
        self.stage_transport(backend, good_hash=True, absent_exit=74)
        with self.assertRaisesRegex(gate.GateError, "^guest-operation-failed$"):
            self.stage(backend)
        row = backend.diagnostics[0]
        self.assertEqual(("stage-absent-check", 74), (row["operation"], row["returncode"]))

    def test_run_gate_hands_the_receipts_folder_to_a_backend_that_wants_it_before_anything_runs(self):
        meta = metadata()

        class Attaching(FakeBackend):
            def attach_receipts(this, receipts):
                this.event("attach")
                this.received = receipts
        backend = Attaching(meta)
        folder = Path(tempfile.mkdtemp(dir=self.root))
        gate.run_gate(folder, meta, backend)
        self.assertEqual("attach", backend.events[0])
        self.assertEqual(folder / "receipts", backend.received)
        plain = FakeBackend(meta)                                           # no hook: unchanged
        gate.run_gate(Path(tempfile.mkdtemp(dir=self.root)), meta, plain)
        self.assertEqual("lock-heavy", plain.events[0])

    def test_the_result_carries_the_guest_failure_rows_and_never_their_text(self):
        meta = metadata()
        real = self.backend((1, b"o PRIVATE-OUT", b"e PRIVATE-CANARY"))
        real.diagnostics = []
        test = self

        class Failing(FakeBackend):
            def stage(this, prepared, meta, receipts):
                this.root_owned = True
                real.receipts = receipts
                with test.assertRaises(gate.GateError):
                    real.guest("x", label="stage-grant-root")
                raise gate.GateError("guest-operation-failed")
        failing = Failing(meta)
        failing.diagnostics = real.diagnostics
        folder = Path(tempfile.mkdtemp(dir=self.root))
        result = gate.run_gate(folder, meta, failing)
        self.assertFalse(result["pass"])
        self.assertEqual("guest-operation-failed", result["failure"])
        self.assertEqual(["stage-grant-root"], [row["operation"] for row in result["host_diagnostics"]])
        text = (folder / "receipts/result.json").read_text()
        self.assertNotIn("PRIVATE", text)
        self.assertEqual(b"e PRIVATE-CANARY", (folder / "receipts" / result["host_diagnostics"][0]["stderr"]["file"]).read_bytes())


class PreflightTests(unittest.TestCase):
    """The machine checks before a run: memory and root's free space, and no limit on CPU pressure."""
    XML = (b'<domain xmlns:qemu="http://libvirt.org/schemas/domain/qemu/1.0"><name>onslaught-ce-win11</name>'
           b'<uuid>00000000-0000-4000-8000-000000000001</uuid><devices><graphics type="vnc" listen="127.0.0.1"/></devices>'
           b'<qemu:commandline><qemu:arg value="user,restrict=on,hostfwd=tcp:127.0.0.1:2522-:22"/></qemu:commandline>'
           b'</domain>')

    def setUp(self):
        root = HERE.parents[1] / "local-data" / "native-fixture-unit"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.scratch = tempfile.TemporaryDirectory(prefix="preflight-", dir=root)
        self.addCleanup(self.scratch.cleanup)
        self.home = Path(self.scratch.name) / "windows-vm"
        (self.home / "ssh").mkdir(parents=True)
        (self.home / "net-mode").write_bytes(b"offline\n")
        for name in ("id_ed25519", "known_hosts"):
            (self.home / "ssh" / name).write_bytes(b"authored")
        os.chmod(self.home / "ssh" / "id_ed25519", 0o600)

    def run_preflight(self, *, available_gib=20, pressure="99.00", free_gib=200):
        backend = object.__new__(gate.RealBackend)
        backend.vm_home, backend.diagnostics = self.home, []
        answers = {"domstate": b"shut off\n", "dominfo": b"Name: x\nManaged save:   no\n", "dumpxml": self.XML}
        backend.virsh = lambda *args, timeout=15: answers[args[0]]
        reads, original, sentinel = [], gate.Path.read_text, object()

        def fake(path, *args, **kwargs):
            if str(path) == "/proc/meminfo":
                reads.append("meminfo")
                return "MemTotal: 33000000 kB\nMemAvailable: %d kB\n" % (available_gib * 1024 * 1024)
            if str(path) == "/proc/pressure/cpu":
                reads.append("pressure")
                return "some avg10=%s avg60=%s avg300=%s total=1\nfull avg10=0.00 avg60=0.00 avg300=0.00 total=0\n" % ((pressure,) * 3)
            return original(path, *args, **kwargs)
        with mock.patch.object(gate.Path, "read_text", autospec=True, side_effect=fake), \
                mock.patch.object(gate.shutil, "disk_usage", return_value=NS(free=free_gib * 1024 ** 3)), \
                mock.patch.object(gate.os, "nice"), \
                mock.patch.object(gate.owned_backend, "OwnedTransient", return_value=sentinel):
            backend.preflight({"inactive_domain_sha256": gate.digest(self.XML)})
        return backend, sentinel, reads

    def test_a_machine_under_heavy_cpu_pressure_is_not_refused_and_its_pressure_is_not_even_read(self):
        for pressure in ("0.00", "20.00", "20.01", "99.00"):
            with self.subTest(avg60=pressure):
                backend, sentinel, reads = self.run_preflight(pressure=pressure)
                self.assertIs(sentinel, backend.owned)
                self.assertEqual(["meminfo"], reads)

    def test_memory_and_free_space_still_refuse_exactly_at_their_lines(self):
        for name, kwargs in (("memory", {"available_gib": 11}), ("free space", {"free_gib": 50})):
            with self.subTest(under=name), self.assertRaisesRegex(gate.GateError, "^machine-capacity$"):
                self.run_preflight(**kwargs)
        backend, sentinel, _ = self.run_preflight(available_gib=12, free_gib=51)      # at the lines: admitted
        self.assertIs(sentinel, backend.owned)


if __name__ == "__main__":
    unittest.main()
