#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored host controls only: no libvirt connection, VM, provider or game."""
from pathlib import Path
from types import SimpleNamespace as NS
import ast
import hashlib
import json
import os
import re
import shutil
import signal
import struct
import sys
import tempfile
import subprocess
import threading
import time
import unittest
import uuid
from unittest.mock import patch
from unittest.mock import create_autospec

import ice_windows_owned_backend as owned

REVIEWED_SOURCE = None
PREVIOUS_REVIEWED_COMMIT = "cc6f5e7cb39e4ccce50890c681650c61d2f63ce8"   # the closure before the room change
GIB = 1024 ** 3


def authored_qcow2(virtual=64 * GIB, cluster_bits=16, refcount_order=4, incompatible=0, version=3,
                   backing=0, crypt=0, magic=b"QFI\xfb", length=112):
    """An authored qcow2 header in the declared base's layout, unless a field is overridden."""
    head = struct.pack(">4sIQIIQIIQQIIQ", magic, version, backing, 0, cluster_bits, virtual, crypt,
                       128, 3 << 16, 1 << 16, 1, 1, 4 << 16)
    if version == 3:
        head += struct.pack(">QQQII", incompatible, 1, 0, refcount_order, length) + bytes(8)
    return head + b"authored qcow2 body"


# The declared base: a 22,424,649,728-byte qcow2 file with a 540,672-byte variable store.
DECLARED = {"files": {"disk": {"stat": {"size": 22424649728}}, "vars": {"stat": {"size": 540672}}}}
FROZEN_ROOM = NS(FLOOR_BYTES=50 * GIB, MARGIN_BYTES=5 * GIB, DISK_VIRTUAL_BYTES=64 * GIB)
STAGED = {"files": {"authored-payload": {"sha256": "0" * 64, "size": 6604253}}}
KEPT = 4257771520
LINE = 55 * GIB                                   # the frozen floor plus margin: where the watchdog stops
# This run's non-disk terms for STAGED: variables, TPM state, capture (twice the payload and staging, plus the
# session allowance) and records.
NON_DISK = 540672 + (1 << 20) + 2 * (6604253 + (1 << 20)) + (64 << 20) + (16 << 20)
# The declared disk's worst case: 1,048,576 data clusters of 64 KiB and 174 metadata clusters.
WORST = 64 * GIB + 174 * 65536
# The same virtual disk with the tiny authored header/body needs 164 metadata clusters.
AUTHORED_WORST = 64 * GIB + 164 * 65536


def wait_for(predicate, seconds=5.0):
    """Real time, for the real watch thread."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.005)
    return bool(predicate())


class FixtureWatchdog:
    """The frozen SpaceWatchdog's contract: below `line` it calls on_trip once and reports no room."""
    line = LINE

    def __init__(self, path, on_trip, statvfs=os.statvfs, events=None):
        self.path, self.on_trip, self.statvfs, self.events = Path(path), on_trip, statvfs, events
        self.tripped, self.minimum_free = False, None

    def check_once(self):
        st = self.statvfs(self.path)
        free = st.f_bavail * st.f_frsize
        if self.events is not None and isinstance(getattr(self.statvfs, "__self__", None), owned.FreeSpace):
            self.events.append("watch-check")
        self.minimum_free = free if self.minimum_free is None else min(self.minimum_free, free)
        if free < self.line:
            if not self.tripped:
                self.tripped = True
                self.on_trip(f"free space {free} bytes is below the floor plus the margin ({self.line})")
            return False
        return True


class FixtureLayout:
    def __init__(self, home, run):
        self.run_dir = home / "gate-runs" / run
        self.machine_dir = self.run_dir / "machine"
        self.disk = self.machine_dir / "disk"
        self.xml = self.run_dir / "domain.xml"
        self.domain_name = "owned-" + run


class FixtureRecorder:
    def __init__(self, root, run):
        self.root = root

    def create_run_dir(self):
        self.root.mkdir()

    def is_recorded(self, path):
        return Path(path).is_relative_to(self.root)

    def write_new(self, path, data, keep=False):
        with Path(path).open("xb") as stream:
            stream.write(data)


class World:
    def __init__(self, home, base=None, disk=None):
        self.home = home
        self.events = []
        self.base_name = "authored-base"
        self.base_state = "shut off"
        self.owned_state = "undefined"
        self.domain_owned = False
        self.persistent_existing = False
        self.refuse_clone = False
        self.refuse_xml = False
        self.refuse_qemu = False
        self.fail_create = False
        self.fail_shutdown = False
        self.fail_guard_after = False
        self.fail_guard_before = False
        self.fail_host_pin = False
        self.fail_connect = False
        self.fail_close = False
        self.on_create = None
        self.clock = 0
        self.free = [1 << 50]                 # what statvfs says; tests lower it to trip the watchdog
        self.free_queue = []                  # readings used up first, one per statvfs call
        self.hidden = [0]                     # data bytes btrfs has taken and statvfs does not show (reserved)
        self.destroy_failures = 0             # destroy raises this many times first
        self.destroy_leaves = 0               # destroy then returns this many times without the domain going
        self.fail_freezer = False             # no handle on the QEMU process
        self.fail_freeze_stop = None          # the exception freezing raises
        self.freezers = []
        self.uuid = ""
        self.base = base or NS(domain_xml=home / "domain.xml", disk=home / "disk/base",
                               vars=home / "nvram/base", tpm=home / "tpm")
        self.tpm_file = getattr(self.base, "tpm_state", self.base.tpm / "state")
        self.base.domain_xml.parent.mkdir(parents=True, exist_ok=True)
        self.base.domain_xml.write_text("authored template")
        for path in [self.base.disk, self.base.vars, self.tpm_file]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((disk or authored_qcow2()) if path == self.base.disk else
                             b"authored immutable machine input")
            path.chmod(0o444)
        self.before = self.snapshot()
        firmware = home / "authored-firmware"
        firmware.write_bytes(b"authored firmware fixture")
        self.host = {"machine_alias": "authored", "machine_type": "authored-pinned",
                     "domain_uuid": "authored-base-uuid", "firmware_path": str(firmware),
                     "emulator_path": "authored-emulator", "pci_reference": []}
        self.base_xml = "<domain><uuid>authored-base-uuid</uuid></domain>"
        self.host.update(firmware_sha256=owned.sha(firmware.read_bytes()),
                         qemu_version="authored-version", libvirt_version="authored-version",
                         swtpm_version="authored-version", persistent_xml_sha256=owned.sha(self.base_xml.encode()),
                         ssh_host_key_sha256=owned.sha(b"authored public key identity"))
        (home / "gate-runs").mkdir(parents=True)
        (home / "gate").mkdir(parents=True, exist_ok=True)
        files = {key: {"stat": {"size": path.stat().st_size}}
                 for key, path in [("disk", self.base.disk), ("vars", self.base.vars)]}
        (home / "gate/base.json").write_text(json.dumps({"host": self.host, "files": files}))

    def snapshot(self):
        return [(p, p.stat().st_mode, p.read_bytes()) for p in
                [self.base.disk, self.base.vars, self.tpm_file]]

    def statvfs(self, path):
        value = self.free_queue.pop(0) if self.free_queue else self.free[0]
        return NS(f_bavail=value, f_frsize=1, real=True)

    def unaccounted(self):
        self.events.append("sensor-counters")
        return self.hidden[0], 0

    def kwargs(self, **more):
        """What OwnedTransient takes beside the metadata and modules: this world's provider, clock, sensor and
        process handle."""
        return dict(provider_factory=self.provider, clock=lambda: self.clock, sleep=self.advance,
                    statvfs=self.statvfs, unaccounted=self.unaccounted, freezer_factory=self.freezer_factory,
                    **more)

    def freezer_factory(self, pid, run_uuid):
        world = self
        if world.fail_freezer:
            raise OSError("authored: no handle on the process")

        class Freezer:
            def stop(self):
                world.events.append("freeze-owned")
                if world.fail_freeze_stop is not None:
                    raise world.fail_freeze_stop
            def close(self):
                world.events.append("freezer-closed")
        freezer = Freezer()
        world.freezers.append((pid, run_uuid, freezer))
        return freezer

    def watchdog(self, path, on_trip, statvfs=os.statvfs):
        return FixtureWatchdog(path, on_trip, statvfs, self.events)

    def state(self, name):
        return self.base_state if name == self.base_name else self.owned_state

    def provider(self, layout, recorded):
        world = self

        class Provider:
            def connect(self):
                world.events.append("connect")
                if world.fail_connect: raise RuntimeError("authored connect refusal")
            def domain_state(self, name): return world.state(name)
            def domain_names(self): return [world.base_name]
            def to_native(self, xml): return "authored native arguments"
            def persistent_xml(self): return world.base_xml
            def versions(self): return {"libvirt": "authored-version", "qemu": "authored-version"}
            def machine_canonical(self, alias): return "authored-pinned"
            def create(self, xml):
                world.events.append("create-owned")
                world.owned_state = "running"
                world.domain_owned = True
                if world.on_create: world.on_create()
                if world.fail_create:
                    raise RuntimeError("authored private canary must not be emitted")
            def shutdown(self, name):
                if name == world.base_name: raise AssertionError("base shutdown")
                world.events.append("shutdown-owned")
                if not world.fail_shutdown: world.owned_state = "undefined"
            def destroy(self, name):
                if name == world.base_name: raise AssertionError("base destroy")
                world.events.append("destroy-owned")
                if world.destroy_failures:
                    world.destroy_failures -= 1
                    raise RuntimeError("authored libvirt destroy failure PRIVATE-CANARY")
                if world.destroy_leaves:
                    world.destroy_leaves -= 1
                    return
                world.owned_state = "undefined"
            def close(self):
                world.events.append("close-connection")
                if world.fail_close: raise RuntimeError("authored close refusal")
                if world.domain_owned: world.owned_state = "undefined"
        return Provider()

    def clone(self, ctx, states):
        self.events.append("clone-owned")
        if self.refuse_clone: return ["authored clone refusal"]
        ctx.layout.machine_dir.mkdir()
        for label, source in [("disk", self.base.disk), ("vars", self.base.vars),
                              ("tpm", self.tpm_file)]:
            target = ctx.layout.disk if label == "disk" else ctx.layout.machine_dir / label
            shutil.copyfile(source, target)
            target.chmod(0o600)
        return []

    def guard(self, ctx, phase, states):
        self.events.append(phase)
        if not ctx.layout.run_dir.is_dir():
            raise AssertionError("guard evidence requires the owned recorder directory")
        if self.fail_guard_before and "before" in phase:
            return ["authored changed base before clone"]
        return ["authored changed base"] if self.fail_guard_after and "after" in phase else []

    def xml(self, template, layout, restrict, pins, uuid, **kwargs):
        self.events.append("build-owned-xml")
        self.uuid = uuid
        if not restrict or kwargs != {"forwards": (), "tunnel_socket": None, "relay_forwards": ()}:
            raise AssertionError("network scope changed")
        return "authored owned XML"

    def advance(self, seconds): self.clock += seconds

    def modules(self):
        return {
            "wg_common": NS(BASE_DOMAIN=self.base_name, DOMAIN_PREFIX="owned-", SSH_PORT=2522,
                            FLOOR_BYTES=50 * GIB, MARGIN_BYTES=5 * GIB, DISK_VIRTUAL_BYTES=64 * GIB),
            "wg_files": NS(Recorder=FixtureRecorder, MARKER_NAME="BASE-SUSPECT",
                           marker_paths=lambda p: [], exclusive_bytes=lambda p: p.stat().st_size),
            "wg_base": NS(load_declaration=json.loads, HOST_KEYS=tuple(self.host),
                          pin_problems=lambda *a: ["host drift"] if self.fail_host_pin else []),
            "wg_runner": NS(Context=lambda *args: NS(layout=args[0], base=args[1],
                                hasher=lambda p: owned.sha(p.read_bytes())),
                            guard_base=self.guard, prepare_copies=self.clone),
            "wg_virt": NS(),
            "wg_machine": NS(RunLayout=FixtureLayout, BasePaths=lambda h: self.base,
                Pins=lambda *args: args, port_in_use=lambda p: False,
                FLOOR_BYTES=50 * GIB, MARGIN_BYTES=5 * GIB,
                room_required=lambda kept, disk=64 * GIB: 55 * GIB + disk + kept,
                check_admission=lambda *a, **k: [] if self.base_state == "shut off" else ["base running"],
                build_domain_xml=self.xml, precreate_check=lambda *a, **k: ["bad XML"] if self.refuse_xml else [],
                pci_problems=lambda *a: [], pci_devices=lambda x: [], memory_problems=lambda *a: [],
                meminfo_available_bytes=lambda x: 32 * 1024**3, guest_memory_bytes=lambda x: 6 * 1024**3,
                find_qemu_pid=lambda name: 12345, find_swtpm_pid=lambda layout: 12346,
                check_qemu_cmdline=lambda *a, **k: ["wrong owner"] if self.refuse_qemu else [],
                check_swtpm_cmdline=lambda *a: [], check_open_files=lambda *a: [], open_targets=lambda p: [],
                lower_priority=lambda *a: self.events.append("nice-owned"),
                SpaceWatchdog=self.watchdog),
        }


class OwnedTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-authored-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.world = World(self.root)
        self.metadata = {"run_id": "authored-native-run",
                         "owned_backend": {"run_id": "wg-20261102T1600Z-authored-01"},
                         "execution_paths": {"vm_home": str(self.root)},
                         "files": {"authored.exe": {"sha256": "0" * 64, "size": 4096}}}

    def machine(self, modules=None):
        return owned.OwnedTransient(self.metadata, modules=modules or self.world.modules(), **self.world.kwargs())

    def start(self, machine):
        with patch.object(owned, "version_probe", return_value="authored-version"), \
                patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
            machine.start(630)

    def test_owned_copy_and_connection_lifetime(self):
        machine = self.machine()
        self.start(machine)
        self.world.events.append("fixture-and-collection")
        self.assertNotIn("close-connection", self.world.events)
        self.assertTrue(machine.stop(90))
        self.assertLess(self.world.events.index("fixture-and-collection"), self.world.events.index("shutdown-owned"))
        self.assertLess(self.world.events.index("shutdown-owned"), self.world.events.index("close-connection"))
        self.assertEqual(self.world.before, self.world.snapshot())
        self.assertEqual(0o600, (machine.layout.machine_dir / "vars").stat().st_mode & 0o777)
        self.assertFalse(machine.report["base_started"])

    def test_running_base_refuses_before_copy_or_create(self):
        self.world.base_state = "running"
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "admission"):
            self.start(machine)
        machine.stop(90)
        self.assertNotIn("clone-owned", self.world.events)
        self.assertNotIn("create-owned", self.world.events)
        self.assertNotIn("shutdown-owned", self.world.events)

    def test_existing_run_is_not_reused_or_removed(self):
        machine = self.machine()
        machine.layout.run_dir.mkdir()
        sentinel = machine.layout.run_dir / "foreign"
        sentinel.write_bytes(b"preserve authored collision")
        with self.assertRaisesRegex(owned.OwnedError, "already-exists"):
            self.start(machine)
        machine.stop(90)
        self.assertEqual(b"preserve authored collision", sentinel.read_bytes())
        self.assertNotIn("create-owned", self.world.events)

    def test_changed_host_pin_refuses_before_copy_or_create(self):
        self.world.fail_host_pin = True
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "host-pin"):
            self.start(machine)
        machine.stop(90)
        self.assertNotIn("clone-owned", self.world.events)
        self.assertNotIn("create-owned", self.world.events)

    def test_bad_copy_or_xml_never_creates_domain(self):
        for fault in ["refuse_clone", "refuse_xml"]:
            with self.subTest(fault=fault), tempfile.TemporaryDirectory(dir=self.root) as temp:
                self.world = World(Path(temp))
                self.metadata["execution_paths"]["vm_home"] = temp
                setattr(self.world, fault, True)
                machine = self.machine()
                with self.assertRaises(owned.OwnedError): self.start(machine)
                machine.stop(90)
                self.assertNotIn("create-owned", self.world.events)
                self.assertEqual(self.world.before, self.world.snapshot())

    def test_partial_create_does_not_grant_destructive_ownership(self):
        self.world.fail_create = True
        machine = self.machine()
        with self.assertRaises(RuntimeError): self.start(machine)
        self.assertFalse(machine.stop(90))
        self.assertNotIn("shutdown-owned", self.world.events)
        self.assertNotIn("destroy-owned", self.world.events)
        self.assertIn("close-connection", self.world.events)
        self.assertEqual("undefined", self.world.owned_state)

    def test_existing_domain_is_not_destroyed_or_adopted(self):
        self.world.owned_state = "running"
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "domain-already-exists"):
            self.start(machine)
        machine.stop(90)
        self.assertEqual("running", self.world.owned_state)
        self.assertNotIn("shutdown-owned", self.world.events)
        self.assertNotIn("destroy-owned", self.world.events)
        self.assertNotIn("fixture-after-owned-stop", self.world.events)
        self.assertFalse(machine.layout.run_dir.exists())
        self.assertEqual("not-run-no-owned-files", machine.report["base_guard_after_stop"])

    def test_unknown_and_none_are_not_accepted_as_absence(self):
        for state in ["unknown", None]:
            with self.subTest(state=state):
                self.world.owned_state = state
                machine = self.machine()
                with self.assertRaisesRegex(owned.OwnedError, "domain-already-exists"):
                    self.start(machine)
                machine.stop(90)
                self.assertNotIn("clone-owned", self.world.events)
                self.assertNotIn("create-owned", self.world.events)

    def test_owned_recorder_precedes_before_guard_failure(self):
        self.world.fail_guard_before = True
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "immutable-base"):
            self.start(machine)
        self.assertTrue(machine.run_recorded)
        self.assertTrue(machine.layout.run_dir.is_dir())
        self.assertNotIn("clone-owned", self.world.events)
        self.assertEqual("fixture-before-clone", machine.report["base_guard_failure"]["phase"])
        self.assertEqual(1, machine.report["base_guard_failure"]["problem_count"])
        machine.stop(90)

    def test_partial_connection_is_closed_without_vm_actions(self):
        self.world.fail_connect = True
        machine = self.machine()
        with self.assertRaises(RuntimeError): self.start(machine)
        machine.stop(90)
        self.assertIn("close-connection", self.world.events)
        self.assertNotIn("create-owned", self.world.events)

    def test_close_failure_is_not_reported_as_verified(self):
        self.world.fail_close = True
        machine = self.machine()
        self.start(machine)
        self.assertFalse(machine.stop(90))
        self.assertFalse(machine.report["connection_closed"])

    def test_forced_stop_or_changed_base_cannot_pass(self):
        self.world.fail_shutdown = True
        machine = self.machine()
        self.start(machine)
        self.assertFalse(machine.stop(3))
        self.assertIn("destroy-owned", self.world.events)
        self.assertTrue(machine.report["forced"])

    def test_wrong_qemu_identity_is_not_reprioritized(self):
        self.world.refuse_qemu = True
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "qemu-identity"):
            self.start(machine)
        machine.stop(90)
        self.assertNotIn("nice-owned", self.world.events)

    def test_after_guard_failure_closes_connection_and_preserves_failure(self):
        machine = self.machine()
        self.start(machine)
        self.world.fail_guard_after = True
        self.assertFalse(machine.stop(90))
        self.assertIn("close-connection", self.world.events)
        self.assertEqual("failed", machine.report["base_guard_after_stop"])
        self.assertEqual("immutable-base-identity-changed", machine.report["cleanup_failure_code"])

    def test_diagnostic_never_serializes_private_stderr(self):
        canary = b"PRIVATE-AUTHORED-CANARY /private/canary secret=canary Permission denied " * 100
        result = owned.diagnostic("preflight", "domstate", 7, canary)
        self.assertEqual("permission-denied", result["reason"])
        self.assertEqual(7, result["returncode"])
        self.assertTrue(result["stderr_truncated"])
        self.assertNotIn("CANARY", json.dumps(result))
        self.assertNotIn("/private", json.dumps(result))

    def test_admission_reasons_are_bounded_without_original_private_text(self):
        modules = self.world.modules()
        problems = ["PRIVATE-AUTHORED-CANARY /private/path " + str(i) for i in range(17)]
        modules["wg_machine"].check_admission = lambda *a, **k: problems
        report = {}
        self.assertIs(problems, owned.observe_admission(modules,
            self.world.provider(None, None), [], 200 * 1024**3, 0, report, {"disk_bytes": 16 * GIB}))
        self.assertFalse(report["accepted"])
        self.assertEqual(17, report["reason_count"])
        self.assertEqual(16, len(report["reasons"]))
        self.assertTrue(report["reasons_truncated"])
        self.assertEqual("unclassified-admission-refusal", report["reasons"][0]["code"])
        self.assertEqual(owned.sha(problems[0].encode()), report["reasons"][0]["message_sha256"])
        self.assertNotIn("CANARY", json.dumps(report))
        self.assertNotIn("/private", json.dumps(report))

    def test_host_pin_receipt_names_field_and_redacts_private_identity(self):
        expected = {"qemu_version": "11.1.2", "firmware_path": "/private/PRIVATE-AUTHORED-CANARY"}
        observed = {"qemu_version": "11.1.2 (libvirt reports 11.1.2)",
                    "firmware_path": "/different/PRIVATE-AUTHORED-CANARY"}
        problem = "private mismatch PRIVATE-AUTHORED-CANARY"
        report = owned.host_pin_report(expected, observed, tuple(expected), [problem])
        self.assertFalse(report["accepted"])
        self.assertEqual(["qemu_version", "firmware_path"], report["mismatched_fields"])
        self.assertEqual(expected["qemu_version"], report["comparisons"][0]["declared"]["value"])
        self.assertEqual(observed["qemu_version"], report["comparisons"][0]["observed"]["value"])
        self.assertNotEqual(report["comparisons"][1]["declared"]["json_sha256"],
                            report["comparisons"][1]["observed"]["json_sha256"])
        self.assertEqual(owned.sha(problem.encode()), report["problem_identities"][0]["sha256"])
        self.assertNotIn("CANARY", json.dumps(report))
        self.assertNotIn("/private", json.dumps(report))

    def test_admission_receipt_states_the_room_rule(self):
        machine = self.machine()
        self.start(machine)
        self.assertTrue(machine.stop(90))
        admission, bound = machine.report["admission"], machine.report["disk_bound"]
        room = admission["room"]
        self.assertEqual(room["floor_bytes"] + room["margin_bytes"] + room["growth_bytes"] +
                         admission["retained_exclusive_bytes"], admission["required_free_bytes"])
        self.assertEqual(sum(room["growth"].values()), room["growth_bytes"])
        self.assertEqual(AUTHORED_WORST, room["growth"]["disk_bytes"])
        self.assertEqual(room["disk_bytes"], room["growth"]["disk_bytes"])
        declared = json.loads((self.root / "gate/base.json").read_text())   # the authored base's own size
        self.assertEqual({"basis": "worst-case", "disk_bytes": AUTHORED_WORST,
                          "worst_case_bytes": AUTHORED_WORST,
                          "measured_estimate_bytes": 16 * GIB, "stop_latency_bound_proven": False,
                          "measured_runs": len(owned.MEASURED_DISK_GROWTH),
                          "largest_measured_bytes": max(v for _, v in owned.MEASURED_DISK_GROWTH),
                          "factor": 2, "floor_bytes": 16 * GIB}, room["disk"])
        self.assertEqual(room["disk"]["worst_case_bytes"], bound["admitted_worst_case_bytes"])
        # The frozen predicate counted the same disk term: floor + disk + margin + kept.
        self.assertEqual(55 * GIB + AUTHORED_WORST, admission["frozen_required_free_bytes"])
        self.assertGreater(admission["required_free_bytes"], admission["frozen_required_free_bytes"])
        self.assertTrue(admission["space_watchdog_armed"])
        self.assertTrue(bound["within_admitted"])
        self.assertEqual(bound["admitted_worst_case_bytes"], bound["worst_case_bytes"])
        self.assertEqual(AUTHORED_WORST, bound["admitted_disk_bytes"])
        self.assertEqual((64 * GIB, 1), (bound["virtual_bytes"], bound["snapshots"]))
        json.dumps(machine.report)

    def test_admission_measures_free_space_where_the_machine_lives(self):
        retained = self.root / "gate-runs/wg-20261101T0000Z-kept-01/receipts/net.pcap"
        retained.parent.mkdir(parents=True)
        retained.write_bytes(bytes(1000))
        growth = owned.run_growth(FROZEN_ROOM, json.loads((self.root / "gate/base.json").read_text()),
                                  self.metadata)
        need = owned.run_room_required(FROZEN_ROOM, growth, 1000)
        asked = []
        real = self.world.statvfs
        self.world.statvfs = lambda path: (asked.append(Path(path)), real(path))[1]
        # statvfs alone would admit this run (a GiB over), but btrfs has taken 2 GiB it does not show yet.
        self.world.free[0] = need + GIB
        self.world.hidden[0] = 2 * GIB
        machine = self.machine()
        with patch.object(owned, "version_probe", return_value="authored-version"), \
                patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
            with self.assertRaisesRegex(owned.OwnedError, "admission-refused"):
                machine.start(630)
        machine.stop(90)
        self.assertEqual(self.root, asked[0])
        admission = machine.report["admission"]
        self.assertEqual((need - GIB, need, 1000), (admission["free_bytes"], admission["required_free_bytes"],
                                                    admission["retained_exclusive_bytes"]))
        self.assertEqual((need + GIB, 2 * GIB), (admission["free_statvfs_bytes"], admission["unaccounted_bytes"]))
        self.assertEqual(["insufficient-disk-space"], [r["code"] for r in admission["reasons"]])
        self.assertNotIn("clone-owned", self.world.events)
        self.assertNotIn("create-owned", self.world.events)

    def test_admission_line_is_exact_on_the_sensors_free_space(self):
        retained = self.root / "gate-runs/wg-20261101T0000Z-kept-01/receipts/net.pcap"
        retained.parent.mkdir(parents=True)
        retained.write_bytes(bytes(1000))
        growth = owned.run_growth(FROZEN_ROOM, json.loads((self.root / "gate/base.json").read_text()),
                                  self.metadata)
        need = owned.run_room_required(FROZEN_ROOM, growth, 1000)
        for free, admitted in [(need - 1, False), (need, True), (need + 1, True)]:
            with self.subTest(free=free):
                self.world = World(self.root / f"line-{free - need}")
                self.metadata["execution_paths"]["vm_home"] = str(self.world.home)
                (self.world.home / "gate-runs/wg-20261101T0000Z-kept-01/receipts").mkdir(parents=True)
                (self.world.home / "gate-runs/wg-20261101T0000Z-kept-01/receipts/net.pcap").write_bytes(bytes(1000))
                self.world.hidden[0] = 7 * GIB
                self.world.free[0] = free + 7 * GIB
                machine = self.machine()
                with patch.object(owned, "version_probe", return_value="authored-version"), \
                        patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
                    if admitted:
                        machine.start(630)
                    else:
                        with self.assertRaisesRegex(owned.OwnedError, "admission-refused"):
                            machine.start(630)
                machine.stop(90)
                self.assertEqual(admitted, machine.report["admission"]["accepted"])
                self.assertEqual(admitted, "create-owned" in self.world.events)

    def test_an_unreadable_sensor_refuses_admission_before_anything_is_copied_or_created(self):
        def broken():
            raise owned.OwnedError("owned-space-sensor-reading")
        self.world.unaccounted = broken
        machine = self.machine()
        with patch.object(owned, "version_probe", return_value="authored-version"), \
                patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
            with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-reading"):
                machine.start(630)
        machine.stop(90)
        admission = machine.report["admission"]
        self.assertEqual((False, False, "space-sensor-unreadable"),
                         (admission["accepted"], admission["space_sensor_read"], admission["reasons"][0]["code"]))
        for event in ("clone-owned", "build-owned-xml", "create-owned"):
            self.assertNotIn(event, self.world.events)
        self.assertFalse(machine.layout.run_dir.exists())


    def test_statvfs_oserror_has_a_bounded_admission_reason_before_copy(self):
        def broken(_path):
            raise OSError("authored private path must not enter the receipt")
        self.world.statvfs = broken
        machine = self.machine()
        with self.assertRaisesRegex(owned.OwnedError, "^owned-space-sensor-reading$"):
            self.start(machine)
        self.assertTrue(machine.stop(1))
        self.assertEqual({"accepted": False, "space_sensor_read": False, "reason_count": 1,
                          "reasons": [{"code": "space-sensor-unreadable"}]}, machine.report["admission"])
        self.assertNotIn("authored private path", json.dumps(machine.report))
        self.assertNotIn("clone-owned", self.world.events)
        self.assertNotIn("create-owned", self.world.events)

    def test_unknown_retained_extents_refuse_with_a_bounded_admission_reason(self):
        retained = self.root / "gate-runs/wg-20261101T0000Z-kept-01/receipts/net.pcap"
        retained.parent.mkdir(parents=True)
        retained.write_bytes(b"authored retained input")
        for value in (None, -1, True, 1.5):
            with self.subTest(value=value):
                modules = self.world.modules()
                modules["wg_files"].exclusive_bytes = lambda path, result=value: result
                machine = self.machine(modules)
                with self.assertRaisesRegex(owned.OwnedError, "^owned-kept-inventory-unreadable$"):
                    self.start(machine)
                self.assertTrue(machine.stop(1))
                self.assertEqual({"accepted": False, "kept_inventory_read": False, "reason_count": 1,
                                  "reasons": [{"code": "kept-inventory-unreadable"}]},
                                 machine.report["admission"])
                self.assertFalse(machine.layout.run_dir.exists())
                self.assertNotIn("clone-owned", self.world.events)
                self.assertNotIn("create-owned", self.world.events)
        self.assertEqual(b"authored retained input", retained.read_bytes())

    def test_retained_inventory_oserror_keeps_only_a_bounded_reason(self):
        retained = self.root / "gate-runs/wg-20261101T0000Z-kept-01/receipts/net.pcap"
        retained.parent.mkdir(parents=True)
        retained.write_bytes(b"authored retained input")
        modules = self.world.modules()
        def unreadable(_path):
            raise OSError("authored private path must not enter the receipt")
        modules["wg_files"].exclusive_bytes = unreadable
        machine = self.machine(modules)
        with self.assertRaisesRegex(owned.OwnedError, "^owned-kept-inventory-unreadable$"):
            self.start(machine)
        self.assertTrue(machine.stop(1))
        self.assertEqual({"accepted": False, "kept_inventory_read": False, "reason_count": 1,
                          "reasons": [{"code": "kept-inventory-unreadable"}]}, machine.report["admission"])
        self.assertNotIn("authored private path", json.dumps(machine.report))
        self.assertNotIn("clone-owned", self.world.events)
        self.assertNotIn("create-owned", self.world.events)


class RoomTests(unittest.TestCase):
    """free >= floor + margin + worst-case growth + kept, beside every frozen admission check."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-room-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.world = World(self.root)
        self.growth = owned.run_growth(FROZEN_ROOM, DECLARED, STAGED)
        self.need = owned.run_room_required(FROZEN_ROOM, self.growth, KEPT)

    def admission(self, free, frozen=(), growth=None):
        modules = self.world.modules()
        seen = []
        modules["wg_machine"].check_admission = lambda *a, **k: seen.append(k) or list(frozen)
        report = {}
        problems = owned.observe_admission(modules, self.world.provider(None, None), [], free, KEPT,
                                           report, self.growth if growth is None else growth)
        report["frozen_keywords"] = seen
        return problems, report

    def test_growth_reserves_worst_case_disk_and_every_other_part_of_the_run(self):
        self.assertEqual({"disk_bytes": WORST, "firmware_vars_bytes": 540672, "tpm_state_bytes": 1 << 20,
                          "network_capture_bytes": 2 * (6604253 + (1 << 20)) + (64 << 20),
                          "record_bytes": 16 << 20}, self.growth)
        self.assertEqual(WORST + NON_DISK, sum(self.growth.values()))
        self.assertEqual(LINE + WORST + NON_DISK + KEPT, self.need)
        problems, report = self.admission(self.need)
        self.assertEqual([], problems)
        self.assertEqual([{"disk_bytes": WORST}], report["frozen_keywords"])
        self.assertEqual(LINE + WORST + KEPT, report["frozen_required_free_bytes"])
        self.assertEqual(NON_DISK, self.need - report["frozen_required_free_bytes"])
        old_measured_line = LINE + 16 * GIB + NON_DISK + KEPT
        self.assertFalse(self.admission(old_measured_line)[1]["accepted"])

    def test_no_measured_runs_gives_the_worst_case_as_before(self):
        # 1,048,576 data clusters of 64 KiB; 128 L2 tables, one L1 cluster, the header, and 43 refcount
        # blocks plus one refcount-table cluster for the longest the 342,173-cluster file can become.
        growth = owned.run_growth(FROZEN_ROOM, DECLARED, STAGED, measured=())
        self.assertEqual(WORST, growth["disk_bytes"])
        self.assertEqual(68831660986, sum(growth.values()))
        self.assertEqual(132145232826, owned.run_room_required(FROZEN_ROOM, growth, KEPT))
        self.assertEqual({"basis": "worst-case", "disk_bytes": WORST, "worst_case_bytes": WORST,
                          "measured_estimate_bytes": None, "stop_latency_bound_proven": False,
                          "measured_runs": 0, "largest_measured_bytes": None, "factor": 2,
                          "floor_bytes": 16 * GIB}, owned.disk_terms(FROZEN_ROOM, DECLARED, ()))
        problems, report = self.admission(132145232826 - 1, growth=growth)
        self.assertEqual([{"disk_bytes": WORST}], report["frozen_keywords"])
        self.assertEqual(["insufficient-disk-space"], [r["code"] for r in report["reasons"]])
        self.assertTrue(self.admission(132145232826, growth=growth)[1]["accepted"])

    def test_measured_estimate_is_observation_only_and_never_reduces_admission(self):
        run = "wg-20261101T0000Z-authored-0"
        for measured, disk in [(((run + "1", 1),), 16 * GIB),
                               (((run + "1", 8 * GIB), (run + "2", 1)), 16 * GIB),
                               (((run + "1", 8 * GIB + 1),), 16 * GIB + 2),
                               (((run + "1", 3 * GIB), (run + "2", 20 * GIB)), 40 * GIB),
                               (((run + "1", WORST // 2 + 1),), WORST),
                               (((run + "1", WORST),), WORST)]:
            with self.subTest(measured=measured):
                terms = owned.disk_terms(FROZEN_ROOM, DECLARED, measured)
                self.assertEqual((WORST, "worst-case", disk, len(measured), max(v for _, v in measured)),
                                 (terms["disk_bytes"], terms["basis"], terms["measured_estimate_bytes"],
                                  terms["measured_runs"], terms["largest_measured_bytes"]))
                self.assertFalse(terms["stop_latency_bound_proven"])
                self.assertEqual(WORST, owned.run_growth(FROZEN_ROOM, DECLARED, STAGED, measured)["disk_bytes"])
        # Retain the pinned observations without treating them as upper bounds on a future run.
        table = owned.MEASURED_DISK_GROWTH
        self.assertEqual(6, len(table))
        self.assertTrue(all(owned.RUN_ID.fullmatch(name) for name, _ in table))
        self.assertLess(2 * max(v for _, v in table), 16 * GIB)
        self.assertEqual(WORST, owned.disk_terms(FROZEN_ROOM, DECLARED)["disk_bytes"])
        self.assertEqual(16 * GIB, owned.disk_terms(FROZEN_ROOM, DECLARED)["measured_estimate_bytes"])

    def test_negative_zero_absurd_or_malformed_measurements_refuse(self):
        run = "wg-20261101T0000Z-authored-01"
        for value in (-1, 0, WORST + 1, 1 << 60, True, 1.5, "1", None):
            with self.subTest(value=value), self.assertRaisesRegex(owned.OwnedError, "owned-room-measurements"):
                owned.disk_terms(FROZEN_ROOM, DECLARED, ((run, value),))
            with self.subTest(value=value), self.assertRaisesRegex(owned.OwnedError, "owned-room-measurements"):
                owned.run_growth(FROZEN_ROOM, DECLARED, STAGED, ((run, value),))
        for measured in [[(run, GIB)], ((run, GIB), (run, GIB)), ((run, GIB, 0),), (run,),
                         (("ice-authored-native-run", GIB),), (("", GIB),), ((None, GIB),), "measured"]:
            with self.subTest(measured=measured), self.assertRaisesRegex(owned.OwnedError, "owned-room-measurements"):
                owned.disk_terms(FROZEN_ROOM, DECLARED, measured)
        # One absurd entry refuses even beside sound ones: it never just loses to a larger maximum.
        with self.assertRaisesRegex(owned.OwnedError, "owned-room-measurements"):
            owned.disk_terms(FROZEN_ROOM, DECLARED, (("wg-20261101T0000Z-authored-02", GIB), (run, -GIB)))

    def test_room_line_refuses_one_byte_under_and_admits_at_and_over(self):
        problems, report = self.admission(self.need - 1)
        self.assertEqual(1, len(problems))
        self.assertEqual(["insufficient-disk-space"], [r["code"] for r in report["reasons"]])
        self.assertFalse(report["accepted"])
        self.assertEqual((self.need - 1, self.need, KEPT),
                         (report["free_bytes"], report["required_free_bytes"],
                          report["retained_exclusive_bytes"]))
        for free in (self.need, self.need + 1):
            with self.subTest(free=free):
                problems, report = self.admission(free)
                self.assertEqual([], problems)
                self.assertTrue(report["accepted"])

    def test_each_part_of_the_growth_is_counted(self):
        self.assertFalse(self.admission(LINE + 16 * GIB + KEPT)[1]["accepted"])     # the frozen line
        self.assertFalse(self.admission(self.need - KEPT)[1]["accepted"])          # without the kept bytes
        for part, size in self.growth.items():
            with self.subTest(part=part):
                self.assertGreater(size, 0)
                self.assertFalse(self.admission(self.need - size)[1]["accepted"])
                without = {k: v for k, v in self.growth.items() if k != part}
                if part == "disk_bytes":     # no disk term at all is no room rule
                    with self.assertRaisesRegex(owned.OwnedError, "owned-room-inputs"):
                        owned.run_room_required(FROZEN_ROOM, without, KEPT)
                    continue
                self.assertEqual(self.need - size, owned.run_room_required(FROZEN_ROOM, without, KEPT))
                self.assertTrue(self.admission(self.need - size, growth=without)[1]["accepted"])

    def test_every_frozen_admission_check_still_refuses(self):
        frozen = ["a base-suspect marker exists (1)", "the persistent domain authored is not shut off",
                  "a run domain already exists: [1]", "port 2522 is in use",
                  "UDP port 9 on 127.0.0.1 is in use", "free space 1 bytes is below the 2 bytes a run needs"]
        codes = ["base-suspect-marker", "base-not-stopped", "run-domain-exists", "ssh-port-in-use",
                 "udp-port-in-use", "insufficient-disk-space"]
        for free in (self.need + 1, self.need - 1):
            with self.subTest(free=free):
                problems, report = self.admission(free, frozen)
                self.assertEqual(frozen, problems[:len(frozen)])
                self.assertEqual(codes + (["insufficient-disk-space"] if free < self.need else []),
                                 [r["code"] for r in report["reasons"]])
                self.assertFalse(report["accepted"])

    def test_floor_below_fifty_gigabytes_or_a_lowered_margin_refuses(self):
        # 50 GB in either unit: 50 * 1000 ** 3 is 50 GB but under 50 GiB, so it refuses too.
        for floor, margin in [(50 * 1000 ** 3 - 1, 5 * GIB), (50 * 1000 ** 3, 5 * GIB), (50 * GIB - 1, 5 * GIB),
                              (50 * GIB, 5 * GIB - 1), (50 * GIB, 0), (50 * GIB, -1), (50.0 * GIB, 5 * GIB),
                              (50 * GIB, True)]:
            with self.subTest(floor=floor, margin=margin):
                with self.assertRaisesRegex(owned.OwnedError, "owned-room-floor"):
                    owned.run_room_required(NS(FLOOR_BYTES=floor, MARGIN_BYTES=margin), self.growth, 0)
                modules = self.world.modules()
                modules["wg_common"].FLOOR_BYTES, modules["wg_common"].MARGIN_BYTES = floor, margin
                with self.assertRaisesRegex(owned.OwnedError, "owned-room-floor"):
                    owned.observe_admission(modules, self.world.provider(None, None), [], 1 << 50, 0, {},
                                            self.growth)
        self.assertEqual(50 * GIB + 5 * GIB + 16 * GIB + 7, owned.run_room_required(
            NS(FLOOR_BYTES=50 * GIB, MARGIN_BYTES=5 * GIB), {"disk_bytes": 16 * GIB}, 7))
        self.assertEqual(60 * GIB + 6 * GIB + 16 * GIB, owned.run_room_required(
            NS(FLOOR_BYTES=60 * GIB, MARGIN_BYTES=6 * GIB), {"disk_bytes": 16 * GIB}, 0))

    def test_missing_or_malformed_room_inputs_refuse(self):
        for metadata in [{}, {"files": {}}, {"files": {"a": {"size": -1}}},
                         {"files": {"a": {"size": True}}}, {"files": {"a": 4096}}]:
            with self.subTest(metadata=metadata), self.assertRaisesRegex(owned.OwnedError, "room-inputs"):
                owned.run_growth(FROZEN_ROOM, DECLARED, metadata)
        for declaration in [{}, {"files": {"disk": {"stat": {"size": 1}}}},
                            {"files": {"disk": {"stat": {"size": "1"}}, "vars": {"stat": {"size": 1}}}},
                            {"files": {"disk": {"stat": {"size": 0}}, "vars": {"stat": {"size": 1}}}}]:
            with self.subTest(declaration=declaration), self.assertRaisesRegex(owned.OwnedError, "room-inputs"):
                owned.run_growth(FROZEN_ROOM, declaration, STAGED)
        for growth, kept in [({}, 0), ({"disk_bytes": -1}, 0), ({"disk_bytes": 0}, 0), ({"disk_bytes": 1}, -1),
                             ({"disk_bytes": 1.0}, 0), ({"disk_bytes": True}, 0), ({"disk_bytes": 1}, None),
                             ({"record_bytes": 1}, 0), ({"disk_bytes": 1, "record_bytes": -1}, 0)]:
            with self.subTest(growth=growth, kept=kept), self.assertRaisesRegex(owned.OwnedError, "room-inputs"):
                owned.run_room_required(FROZEN_ROOM, growth, kept)

    def test_qcow2_bound_matches_hand_counts(self):
        one = owned.qcow2_geometry(authored_qcow2(virtual=GIB))
        # 16,384 data clusters; 2 L2 tables, 1 L1 cluster, the header, 1 refcount block, 1 table cluster.
        self.assertEqual({"disk_data_bytes": GIB, "disk_metadata_bytes": 6 * 65536},
                         owned.qcow2_growth(one, 65536))
        wide = owned.qcow2_geometry(authored_qcow2(virtual=GIB, incompatible=0x10))
        self.assertEqual(16, wide["l2_entry_bytes"])                 # extended L2 entries: 4 tables
        self.assertEqual(8 * 65536, owned.qcow2_growth(wide, 65536)["disk_metadata_bytes"])
        two = owned.qcow2_geometry(authored_qcow2(version=2))
        self.assertEqual((16, 8), (two["refcount_bits"], two["l2_entry_bytes"]))
        self.assertEqual({"disk_data_bytes": 64 * GIB, "disk_metadata_bytes": 174 * 65536},
                         owned.qcow2_growth(owned.qcow2_geometry(authored_qcow2()), 22424649728))
        self.assertEqual(WORST, owned.disk_terms(FROZEN_ROOM, DECLARED)["worst_case_bytes"])
        with self.assertRaisesRegex(owned.OwnedError, "room-inputs"):
            owned.qcow2_growth(one, 0)

    def test_qcow2_layouts_outside_the_bound_refuse(self):
        self.assertEqual({"virtual_bytes": 64 * GIB, "cluster_bytes": 65536, "l2_entry_bytes": 8,
                          "refcount_bits": 16, "snapshots": 1}, owned.qcow2_geometry(authored_qcow2()))
        self.assertEqual(8, owned.qcow2_geometry(authored_qcow2(incompatible=0x9))["l2_entry_bytes"])
        refused = [authored_qcow2(magic=b"QFI\x00"), authored_qcow2(version=4), authored_qcow2(backing=4096),
                   authored_qcow2(crypt=1), authored_qcow2(incompatible=0x2), authored_qcow2(incompatible=0x4),
                   authored_qcow2(incompatible=0x20), authored_qcow2(cluster_bits=8),
                   authored_qcow2(cluster_bits=22), authored_qcow2(virtual=0), authored_qcow2(refcount_order=7),
                   authored_qcow2(length=100), authored_qcow2()[:71], authored_qcow2()[:103], "QFI"]
        for index, header in enumerate(refused):
            with self.subTest(index=index), self.assertRaisesRegex(owned.OwnedError, "owned-disk-format"):
                owned.qcow2_geometry(header)

    def test_copy_that_could_outgrow_admission_never_creates_domain(self):
        for name, disk, failure in [("larger", authored_qcow2(virtual=128 * GIB), "owned-disk-growth-bound"),
                                    ("backed", authored_qcow2(backing=4096), "owned-disk-format")]:
            with self.subTest(copy=name):
                home = self.root / name
                world = World(home, disk=disk)
                metadata = {"run_id": "authored-native-run",
                            "owned_backend": {"run_id": "wg-20261102T1600Z-authored-01"},
                            "execution_paths": {"vm_home": str(home)}, "files": STAGED["files"]}
                machine = owned.OwnedTransient(metadata, modules=world.modules(), **world.kwargs())
                with patch.object(owned, "version_probe", return_value="authored-version"), \
                        patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + world.uuid.encode() + b"\0"):
                    with self.assertRaisesRegex(owned.OwnedError, failure):
                        machine.start(630)
                self.assertTrue(machine.stop(90))
                self.assertIn("clone-owned", world.events)
                self.assertNotIn("build-owned-xml", world.events)
                self.assertNotIn("create-owned", world.events)
                self.assertEqual(world.before, world.snapshot())
                if name == "larger":
                    self.assertFalse(machine.report["disk_bound"]["within_admitted"])
                    self.assertEqual(128 * GIB, machine.report["disk_bound"]["virtual_bytes"])
                    declared = json.loads((home / "gate/base.json").read_text())
                    self.assertEqual(owned.disk_terms(FROZEN_ROOM, declared, ())["disk_bytes"],
                                     machine.report["disk_bound"]["admitted_worst_case_bytes"])
                    self.assertGreater(machine.report["disk_bound"]["worst_case_bytes"],
                                       machine.report["disk_bound"]["admitted_worst_case_bytes"])


class WatchdogTests(unittest.TestCase):
    """The watchdog is load-bearing: armed at floor + margin before any copy or guest, or admission refuses;
    a trip destroys only this run's own guest, and the run fails with cleanup as for any failed run."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-watch-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.world = World(self.root)
        self.metadata = {"run_id": "authored-native-run",
                         "owned_backend": {"run_id": "wg-20261102T1600Z-authored-01"},
                         "execution_paths": {"vm_home": str(self.root)}, "files": STAGED["files"]}

    def machine(self, modules=None, **more):
        more.setdefault("poll_seconds", 0.02)
        machine = owned.OwnedTransient(self.metadata, modules=modules or self.world.modules(),
                                       **self.world.kwargs(**more))
        self.addCleanup(machine.watch_stop.set)
        return machine

    def start(self, machine):
        with patch.object(owned, "version_probe", return_value="authored-version"), \
                patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
            machine.start(630)

    def refused_unarmed(self, modules=None):
        machine = self.machine(modules)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-unarmed"):
            self.start(machine)
        machine.stop(90)
        self.assertFalse(machine.report["space_watchdog"]["armed"])
        for event in ("clone-owned", "build-owned-xml", "create-owned"):
            self.assertNotIn(event, self.world.events)
        self.assertFalse(machine.layout.run_dir.exists())
        admission = machine.report["admission"]
        self.assertFalse(admission["accepted"])
        self.assertFalse(admission["space_watchdog_armed"])
        self.assertEqual("space-watchdog-unarmed", admission["reasons"][-1]["code"])
        self.assertFalse(machine.watch_thread is not None and machine.watch_thread.is_alive())
        return machine

    def test_watchdog_is_armed_at_floor_plus_margin_before_any_copy_or_guest(self):
        machine = self.machine()
        self.start(machine)
        events = self.world.events
        self.assertLess(events.index("watch-check"), events.index("clone-owned"))
        self.assertLess(events.index("clone-owned"), events.index("create-owned"))
        self.assertTrue(machine.watch_thread.is_alive())
        self.assertEqual({"armed": True, "stop_line_bytes": LINE, "first_free_bytes": 1 << 50},
                         {k: machine.report["space_watchdog"][k] for k in ("armed", "stop_line_bytes",
                                                                           "first_free_bytes")})
        self.assertTrue(machine.stop(90))
        self.assertFalse(machine.watch_thread.is_alive())
        self.assertEqual((False, None, False), tuple(machine.report["space_watchdog"][k]
                                                     for k in ("tripped", "trip", "stopped_guest")))
        self.assertNotIn("failure_cause", machine.report)

    def test_watchdog_not_armed_refuses_before_any_copy_or_guest(self):
        class Late(FixtureWatchdog):          # stops one page below the line: not floor + margin
            line = LINE - 4096

        class Early(FixtureWatchdog):         # stops above the line
            line = LINE + 1

        class Broken(FixtureWatchdog):
            def check_once(self): raise OSError("authored statvfs failure")

        class Silent(FixtureWatchdog):        # reports no room but never calls on_trip
            def check_once(self):
                st = self.statvfs(self.path)
                return st.f_bavail * st.f_frsize >= self.line

        for name, factory in [("late", Late), ("early", Early), ("broken", Broken), ("silent", Silent),
                              ("missing", None)]:
            with self.subTest(watchdog=name):
                self.world = World(self.root / name)
                self.metadata["execution_paths"]["vm_home"] = str(self.root / name)
                modules = self.world.modules()
                modules["wg_machine"].SpaceWatchdog = factory
                self.refused_unarmed(modules)
        for name, field in [("floor", "FLOOR_BYTES"), ("margin", "MARGIN_BYTES")]:
            with self.subTest(mismatch=name):        # the watchdog would read another line than admission
                self.world = World(self.root / name)
                self.metadata["execution_paths"]["vm_home"] = str(self.root / name)
                modules = self.world.modules()
                setattr(modules["wg_machine"], field, getattr(modules["wg_machine"], field) + GIB)
                self.refused_unarmed(modules)

    def test_watchdog_first_reading_under_the_line_or_a_dead_thread_refuses(self):
        self.world.free_queue = [1 << 50, LINE - 1]  # admission's own reading is ample; the watchdog's first is not
        machine = self.refused_unarmed()
        self.assertEqual("free-space-below-stop-line", machine.report["space_watchdog"]["trip"])
        self.assertEqual("owned-space-watchdog-tripped", machine.report["failure_cause"])
        self.world = World(self.root / "thread")
        self.metadata["execution_paths"]["vm_home"] = str(self.root / "thread")
        with patch.object(owned.threading.Thread, "start", lambda thread: None):
            self.refused_unarmed()

    def test_trip_after_arming_refuses_the_guest(self):
        clone, holder = self.world.clone, []

        def clone_then_fill(ctx, states):
            problems = clone(ctx, states)
            self.world.free[0] = LINE - 1
            holder[0].watchdog.check_once()         # the watch thread's next reading, taken now
            return problems
        self.world.clone = clone_then_fill
        machine = self.machine()
        holder.append(machine)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            self.start(machine)
        self.assertNotIn("create-owned", self.world.events)
        self.assertFalse(machine.stop(90))
        self.assertEqual((True, "free-space-below-stop-line", False),
                         tuple(machine.report["space_watchdog"][k] for k in ("tripped", "trip", "stopped_guest")))
        self.assertEqual("owned-space-watchdog-tripped", machine.report["failure_cause"])
        self.assertEqual(self.world.before, self.world.snapshot())

    def test_trip_during_the_run_stops_only_the_owned_guest_and_fails_cleanly(self):
        for path in ("direct", "thread"):
            with self.subTest(path=path):
                self.world = World(self.root / path)
                self.metadata["execution_paths"]["vm_home"] = str(self.root / path)
                machine = self.machine()
                self.start(machine)
                self.world.free[0] = LINE - 1
                if path == "direct":
                    machine.watchdog.check_once()
                else:                               # the real watch thread, polling once a second
                    for _ in range(100):
                        if "destroy-owned" in self.world.events:
                            break
                        owned.time.sleep(0.05)
                self.assertIn("destroy-owned", self.world.events)
                self.assertEqual("undefined", self.world.owned_state)
                self.assertFalse(machine.stop(90))          # Provider.destroy refuses the base's name
                self.assertNotIn("shutdown-owned", self.world.events)
                report = machine.report
                self.assertEqual((True, "free-space-below-stop-line", True, LINE - 1),
                                 tuple(report["space_watchdog"][k] for k in ("tripped", "trip", "stopped_guest",
                                                                              "minimum_free_bytes")))
                self.assertEqual("owned-space-watchdog-stopped-guest", report["failure_cause"])
                self.assertTrue(report["forced"])
                self.assertTrue(report["connection_closed"])
                self.assertEqual("verified", report["base_guard_after_stop"])
                self.assertTrue(report["copies_retained"])
                self.assertTrue(machine.layout.disk.exists())
                self.assertEqual(1, self.world.events.count("destroy-owned"))
                self.assertEqual(self.world.before, self.world.snapshot())

    def test_trip_racing_the_create_stops_the_guest_it_created(self):
        machine = self.machine()

        def fill():
            self.world.free[0] = LINE - 1
            machine.watchdog.check_once()           # trips while created is still False
        self.world.on_create = fill
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-stopped-guest"):
            self.start(machine)
        self.assertEqual(1, self.world.events.count("destroy-owned"))
        self.assertEqual("undefined", self.world.owned_state)
        self.assertFalse(machine.stop(90))
        self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])

    def test_a_failed_watch_trips_and_stops_the_guest(self):
        machine = self.machine()
        self.start(machine)
        machine.watchdog.check_once = lambda: (_ for _ in ()).throw(OSError("authored statvfs failure"))
        for _ in range(100):
            if "destroy-owned" in self.world.events:
                break
            owned.time.sleep(0.05)
        self.assertFalse(machine.stop(90))
        self.assertEqual(("space-watch-failed", True), (machine.report["space_watchdog"]["trip"],
                                                        machine.report["space_watchdog"]["stopped_guest"]))

    def trip_now(self, machine, free=LINE - 1):
        """Lower statvfs under the line and wait for the real watch thread to trip."""
        self.world.free[0] = free
        self.assertTrue(wait_for(lambda: machine.space_tripped.is_set()))

    def test_bytes_btrfs_has_taken_count_against_the_line_before_statvfs_shows_them(self):
        machine = self.machine()
        self.start(machine)
        self.world.free[0] = LINE + GIB                 # statvfs alone still shows a GiB of room ...
        self.world.hidden[0] = 2 * GIB                  # ... which btrfs has taken, and not yet shown
        self.assertTrue(wait_for(lambda: "destroy-owned" in self.world.events))
        self.assertFalse(machine.stop(90))
        watch = machine.report["space_watchdog"]
        self.assertEqual("free-space-below-stop-line", watch["trip"])
        self.assertEqual({"free": LINE - GIB, "statvfs_free": LINE + GIB, "unaccounted": 2 * GIB}, watch["trip_reading"])
        self.assertEqual((LINE - GIB, 2 * GIB), (watch["minimum_free_bytes"], watch["max_unaccounted_bytes"]))
        self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])

    def test_statvfs_alone_would_not_have_tripped_that_run(self):
        """The control for the test above: the same readings through the old sensor (statvfs only) never trip."""
        trips = []
        dog = FixtureWatchdog(self.root, trips.append, lambda path: NS(f_bavail=LINE + GIB, f_frsize=1))
        self.assertTrue(dog.check_once())
        self.assertEqual([], trips)

    def test_a_trip_destroys_again_until_the_guest_is_gone(self):
        self.world.destroy_failures = 3
        machine = self.machine()
        self.start(machine)
        self.trip_now(machine)
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        watch = machine.report["space_watchdog"]
        self.assertEqual(4, machine.stop_attempts)
        self.assertEqual(4, watch["stop_attempts"])
        self.assertEqual(["failed", "failed", "failed", "gone"], [row["outcome"] for row in watch["stop_log"]])
        self.assertEqual(["RuntimeError"] * 3 + [None], [row.get("error_class") for row in watch["stop_log"]])
        self.assertEqual(4, self.world.events.count("destroy-owned"))
        self.assertIsNotNone(watch["stop_seconds"])
        self.assertFalse(machine.stop(90))
        self.assertNotIn("shutdown-owned", self.world.events)
        self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])
        self.assertTrue(machine.report["cleanup_verified"])
        self.assertNotIn("CANARY", json.dumps(machine.report))        # no libvirt text in the receipt
        self.assertFalse(machine.watch_thread.is_alive())

    def test_a_destroy_that_returns_with_the_domain_still_there_is_not_a_stop(self):
        self.world.destroy_leaves = 2
        machine = self.machine()
        self.start(machine)
        self.trip_now(machine)
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertEqual(["still-present", "still-present", "gone"],
                         [row["outcome"] for row in machine.report["space_watchdog"]["stop_log"]])
        self.assertFalse(machine.stop(90))

    def test_a_destroy_that_never_works_is_bounded_logged_and_ended_by_closing_the_connection(self):
        self.world.destroy_failures = 10 ** 6
        with patch.object(owned, "STOP_ATTEMPTS_WATCH", 7), patch.object(owned, "STOP_ATTEMPTS_FINAL", 3), \
                patch.object(owned, "STOP_LOG_ENTRIES", 4):
            machine = self.machine()
            self.start(machine)
            self.trip_now(machine)
            self.assertTrue(wait_for(lambda: machine.report["space_watchdog"]["stop_gave_up"]))
            self.assertTrue(wait_for(lambda: not machine.watch_thread.is_alive()))
            self.assertEqual(7, machine.stop_attempts)
            self.assertFalse(machine.space_stopped)
            self.assertEqual("running", self.world.owned_state)
            self.assertFalse(machine.stop(90))
        watch = machine.report["space_watchdog"]
        self.assertEqual(10, watch["stop_attempts"])                    # the watch thread's 7 and stop()'s 3
        self.assertEqual(4, len(watch["stop_log"]))                    # the receipt keeps the first few; the count has all
        self.assertEqual(11, self.world.events.count("destroy-owned"))  # and stop()'s last forced destroy
        self.assertNotIn("shutdown-owned", self.world.events)           # no polite shutdown for a guest filling root
        self.assertEqual("owned-space-watchdog-tripped", machine.report["failure_cause"])
        self.assertFalse(machine.report["cleanup_verified"])
        self.assertTrue(machine.report["connection_closed"])           # AUTODESTROY is what ends it
        self.assertEqual("undefined", self.world.owned_state)

    def test_a_trip_requests_freeze_before_destroy_without_claiming_an_observed_stop(self):
        machine = self.machine()
        self.start(machine)
        self.assertEqual([(12345, self.world.uuid)], [(pid, run_uuid) for pid, run_uuid, _ in self.world.freezers])
        self.assertTrue(machine.report["space_watchdog"]["freeze_ready"])
        self.trip_now(machine)
        self.assertTrue(wait_for(lambda: "destroy-owned" in self.world.events))
        self.assertLess(self.world.events.index("freeze-owned"), self.world.events.index("destroy-owned"))
        self.assertEqual(1, self.world.events.count("freeze-owned"))
        self.assertFalse(machine.stop(90))
        watch = machine.report["space_watchdog"]
        self.assertEqual(("sent", True), (watch["freeze_status"], watch["freeze_ready"]))
        self.assertNotIn("frozen", watch)
        self.assertIsNotNone(watch["freeze_seconds"])
        self.assertIn("freezer-closed", self.world.events)

    def test_a_slow_destroy_at_a_trip_is_not_a_slow_poll_and_its_time_is_recorded(self):
        provider = self.world.provider

        def slow_provider(layout, recorded):
            inner = provider(layout, recorded)
            real_destroy = inner.destroy

            def destroy(name):
                self.world.clock += 5               # the injected clock moves only while libvirt is asked
                real_destroy(name)
            inner.destroy = destroy
            return inner
        self.world.provider = slow_provider
        machine = self.machine()
        self.start(machine)
        self.assertTrue(wait_for(lambda: machine.polls >= 2))
        self.trip_now(machine)
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertEqual(0, machine.max_gap)        # readings are timed from where they start
        watch = machine.report["space_watchdog"]
        self.assertEqual((5.0, 5.0), (watch["stop_seconds"], watch["stop_log"][0]["seconds"]))
        self.assertFalse(machine.stop(90))

    def test_a_handle_that_will_not_close_does_not_stop_the_run_ending(self):
        self.world.freezer_factory_real = self.world.freezer_factory

        def failing_close_factory(pid, run_uuid):
            freezer = self.world.freezer_factory_real(pid, run_uuid)
            freezer.close = lambda: (_ for _ in ()).throw(OSError("authored close failure"))
            return freezer
        self.world.freezer_factory = failing_close_factory
        machine = self.machine()
        self.start(machine)
        self.assertTrue(machine.stop(90))
        self.assertTrue(machine.report["connection_closed"])

    def test_a_guest_that_cannot_be_frozen_is_destroyed_all_the_same(self):
        for case in ("no-handle", "signal-fails", "process-gone"):
            with self.subTest(case=case):
                self.world = World(self.root / case)
                self.metadata["execution_paths"]["vm_home"] = str(self.root / case)
                self.world.fail_freezer = case == "no-handle"
                self.world.fail_freeze_stop = {"signal-fails": PermissionError("authored"),
                                               "process-gone": ProcessLookupError("authored")}.get(case)
                machine = self.machine()
                self.start(machine)
                self.assertEqual(case != "no-handle", machine.report["space_watchdog"]["freeze_ready"])
                self.trip_now(machine)
                self.assertTrue(wait_for(lambda: machine.space_stopped))
                self.assertFalse(machine.stop(90))
                watch = machine.report["space_watchdog"]
                self.assertEqual({"no-handle": "not-requested", "signal-fails": "failed",
                                  "process-gone": "already-exited"}[case], watch["freeze_status"])
                self.assertEqual("PermissionError" if case == "signal-fails" else None, watch.get("freeze_error_class"))
                self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])

    def test_a_trip_before_the_guest_exists_freezes_and_destroys_nothing(self):
        clone, holder = self.world.clone, []

        def clone_then_fill(ctx, states):
            problems = clone(ctx, states)
            self.world.free[0] = LINE - 1
            holder[0].watchdog.check_once()
            return problems
        self.world.clone = clone_then_fill
        machine = self.machine()
        holder.append(machine)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            self.start(machine)
        self.assertFalse(machine.stop(90))
        for event in ("create-owned", "freeze-owned", "destroy-owned"):
            self.assertNotIn(event, self.world.events)
        self.assertEqual([], self.world.freezers)

    def test_a_trip_racing_the_create_with_a_destroy_that_fails_does_not_claim_a_stop(self):
        self.world.destroy_failures = 10 ** 6
        machine = self.machine(poll_seconds=3600)       # the watch thread stays out of it: start() makes every attempt

        def fill():
            self.world.free[0] = LINE - 1
            machine.watchdog.check_once()
        self.world.on_create = fill
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            self.start(machine)
        self.assertFalse(machine.space_stopped)
        self.assertEqual(owned.STOP_ATTEMPTS_FINAL, self.world.events.count("destroy-owned"))
        self.assertEqual(["failed"] * owned.STOP_ATTEMPTS_FINAL,
                         [row["outcome"] for row in machine.report["space_watchdog"]["stop_log"]])
        self.assertFalse(machine.stop(90))
        self.assertEqual("owned-space-watchdog-tripped", machine.report["failure_cause"])

    def test_the_guard_is_quiet_while_the_watch_is_alive_and_raises_after_a_trip(self):
        machine = self.machine()
        machine.space_guard()                                           # nothing armed yet
        self.start(machine)
        machine.space_guard()
        self.trip_now(machine)
        for _ in range(2):
            with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
                machine.space_guard()
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertFalse(machine.stop(90))

    def test_the_guard_treats_a_watch_thread_that_has_died_as_a_trip(self):
        machine = self.machine()
        self.start(machine)
        machine.space_guard()
        real = machine.watch_thread
        machine.watch_thread = NS(is_alive=lambda: False, ident=1, join=lambda timeout=None: None)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            machine.space_guard()
        self.assertEqual("space-watch-failed", machine.report["space_watchdog"]["trip"])
        self.assertTrue(wait_for(lambda: "destroy-owned" in self.world.events))
        machine.watch_thread = real
        self.assertFalse(machine.stop(90))

    def test_the_guard_treats_a_watch_that_has_gone_quiet_as_a_trip(self):
        release, calls, real = threading.Event(), [0], self.world.statvfs

        def hanging(path):
            calls[0] += 1
            if calls[0] > 2:                    # admission and arming were answered; every later reading blocks
                release.wait(30)
            return real(path)
        self.world.statvfs = hanging
        self.addCleanup(release.set)
        machine = self.machine()
        self.start(machine)
        self.assertTrue(wait_for(lambda: calls[0] > 2))
        machine.space_guard()                                           # quiet, but not for STALE_SECONDS yet
        self.world.advance(owned.STALE_SECONDS + 1)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            machine.space_guard()
        self.assertEqual("space-watch-stalled", machine.report["space_watchdog"]["trip"])
        self.assertTrue(wait_for(lambda: "destroy-owned" in self.world.events))
        release.set()
        self.assertFalse(machine.stop(90))
        self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])

    def test_a_guard_after_the_run_has_stopped_is_quiet(self):
        machine = self.machine()
        self.start(machine)
        self.assertTrue(machine.stop(90))
        machine.space_guard()

    def test_the_report_records_what_the_watch_saw(self):
        machine = self.machine()
        self.start(machine)
        self.assertTrue(wait_for(lambda: machine.polls >= 3))
        self.world.hidden[0] = 123456
        self.assertTrue(wait_for(lambda: machine.sensor.maximum_unaccounted == 123456))
        self.assertTrue(machine.stop(90))
        watch = machine.report["space_watchdog"]
        self.assertGreaterEqual(watch["polls"], 3)
        self.assertEqual(("btrfs-statvfs-less-unaccounted", 0.02, 123456, False, "not-requested"),
                         (watch["sensor"], watch["poll_seconds"], watch["max_unaccounted_bytes"],
                          watch["stop_gave_up"], watch["freeze_status"]))
        self.assertTrue(machine.report["cleanup_verified"])
        json.dumps(machine.report)

    def test_the_watch_polls_every_tenth_of_a_second_by_default(self):
        self.assertEqual(0.1, owned.POLL_SECONDS)
        machine = owned.OwnedTransient(self.metadata, modules=self.world.modules(), **self.world.kwargs())
        self.addCleanup(machine.watch_stop.set)
        self.assertEqual(0.1, machine.poll_seconds)
        self.start(machine)
        first = machine.polls
        time.sleep(1.05)
        self.assertTrue(6 <= machine.polls - first <= 11, machine.polls - first)       # about ten a second, never one
        self.assertTrue(machine.stop(90))

    def test_admission_does_not_depend_on_measured_writer_or_stop_timings(self):
        expected = owned.run_growth(FROZEN_ROOM, DECLARED, STAGED)
        line = owned.run_room_required(FROZEN_ROOM, expected, KEPT)
        with patch.object(owned, "MEASURED_STOP_SECONDS", 10000), \
                patch.object(owned, "MEASURED_WRITE_BURST_BYTES", 1 << 50), \
                patch.object(owned, "MEASURED_SENSOR_MISS_BYTES", 1 << 50):
            self.assertEqual(expected, owned.run_growth(FROZEN_ROOM, DECLARED, STAGED))
            self.assertEqual(line, owned.run_room_required(FROZEN_ROOM, expected, KEPT))
        self.assertEqual(WORST, expected["disk_bytes"])

    def test_run_records_its_copys_unshared_bytes_at_the_end(self):
        machine = self.machine()
        self.start(machine)
        self.assertEqual("not-reached", machine.report["disk_growth"]["reason"])
        self.assertTrue(machine.stop(90))
        growth = machine.report["disk_growth"]
        size = machine.layout.disk.stat().st_size
        folder = sum(p.stat().st_size for p in machine.layout.run_dir.rglob("*") if p.is_file())
        self.assertEqual({"measured": True, "reason": None, "admitted_disk_bytes": AUTHORED_WORST,
                          "apparent_bytes": size, "allocated_bytes": machine.layout.disk.stat().st_blocks * 512,
                          "unshared_bytes": size, "run_folder_unshared_bytes": folder,
                          "within_admitted_disk_term": True}, growth)
        json.dumps(machine.report)

    def test_unmeasurable_or_oversized_copies_are_recorded_as_such(self):
        for case in ("unavailable", "oversized", "refused-before-copy"):
            with self.subTest(case=case):
                self.world = World(self.root / case)
                self.metadata["execution_paths"]["vm_home"] = str(self.root / case)
                modules = self.world.modules()
                if case == "unavailable":
                    modules["wg_files"].exclusive_bytes = lambda path: None
                if case == "oversized":
                    modules["wg_files"].exclusive_bytes = lambda path: AUTHORED_WORST + 1
                if case == "refused-before-copy":
                    self.world.fail_host_pin = True
                machine = self.machine(modules)
                if case == "refused-before-copy":
                    with self.assertRaisesRegex(owned.OwnedError, "host-pin"):
                        self.start(machine)
                    machine.stop(90)
                    self.assertEqual({"measured": False, "reason": "not-reached"}, machine.report["disk_growth"])
                    continue
                self.start(machine)
                self.assertTrue(machine.stop(90))
                growth = machine.report["disk_growth"]
                if case == "unavailable":
                    self.assertEqual((False, "extent-map-unavailable", None),
                                     (growth["measured"], growth["reason"], growth["unshared_bytes"]))
                else:   # recorded, not a cleanup failure: the table is re-derived from it
                    self.assertEqual((True, AUTHORED_WORST + 1, False),
                                     (growth["measured"], growth["unshared_bytes"],
                                      growth["within_admitted_disk_term"]))


class SensorTests(unittest.TestCase):
    """The free-space sensor: statvfs less the data bytes btrfs has taken and does not show yet."""
    def sensor(self, free, reserved=0, may_use=0):
        calls = []

        def statvfs(path):
            calls.append("statvfs")
            return NS(f_bavail=free, f_frsize=1)

        def unaccounted():
            calls.append("counters")
            return reserved, may_use
        return owned.FreeSpace(Path("/authored"), statvfs, unaccounted), calls

    def test_free_is_statvfs_less_reserved_and_may_use(self):
        sensor, _ = self.sensor(100 * GIB, 3 * GIB, 2 * GIB)
        self.assertEqual({"free": 95 * GIB, "statvfs_free": 100 * GIB, "unaccounted": 5 * GIB}, sensor.read())
        self.assertEqual((95 * GIB, 5 * GIB, 1), (sensor.minimum_free, sensor.maximum_unaccounted, sensor.polls))
        shaped = sensor.statvfs(Path("/anything"))          # the frozen watchdog's parameter: blocks of one byte
        self.assertEqual((95 * GIB, 1), (shaped.f_bavail, shaped.f_frsize))
        self.assertEqual(2, sensor.polls)

    def test_counters_are_read_before_statvfs(self):
        """A byte that moves from reserved to used between the two reads is then counted twice, never missed."""
        sensor, calls = self.sensor(10 * GIB, GIB)
        sensor.read()
        self.assertEqual(["counters", "statvfs"], calls)

    def test_may_use_to_reserved_transfer_between_reads_is_not_lost(self):
        pending = 128 << 20
        state = {"bytes_used": 0, "bytes_reserved": 0, "bytes_may_use": pending}
        changing, reads = [False], []
        def counter(_folder, name):
            value = state[name]
            if changing[0] and name in ("bytes_may_use", "bytes_reserved"):
                reads.append(name)
                if len(reads) == 1:
                    state["bytes_reserved"] += state["bytes_may_use"]
                    state["bytes_may_use"] = 0
            return value
        sensor = owned.FreeSpace(Path("/authored"),
                lambda _path: NS(f_blocks=1 << 40, f_bfree=1 << 39, f_bavail=1 << 39, f_frsize=1))
        with patch.object(owned, "btrfs_data_folder", return_value=Path("/authored/data")), \
                patch.object(owned, "space_counter", side_effect=counter):
            sensor._bind()
            changing[0] = True
            reading = sensor.read()
        self.assertEqual(["bytes_may_use", "bytes_reserved"], reads)
        self.assertGreaterEqual(reading["unaccounted"], pending)
        self.assertLessEqual(reading["free"], (1 << 39) - pending)

    def test_binding_statvfs_oserror_is_normalized_without_private_text(self):
        def broken(_path):
            raise OSError("authored private bind path")
        sensor = owned.FreeSpace(Path("/authored"), broken)
        with patch.object(owned, "btrfs_data_folder", return_value=Path("/authored/data")), \
                patch.object(owned, "space_counter", return_value=0):
            with self.assertRaisesRegex(owned.OwnedError, "^owned-space-sensor-reading$"):
                sensor.read()
        self.assertIsNone(sensor.last)
        self.assertEqual(0, sensor.polls)

    def test_more_taken_than_free_reads_as_nothing_free(self):
        sensor, _ = self.sensor(GIB, 5 * GIB, GIB)
        self.assertEqual(0, sensor.read()["free"])

    def test_the_minimum_and_the_largest_unaccounted_amount_are_kept(self):
        values = iter([(0, 0, 50 * GIB), (4 * GIB, 0, 40 * GIB), (0, 1, 60 * GIB)])
        state = {}
        sensor = owned.FreeSpace(Path("/authored"), lambda path: NS(f_bavail=state["free"], f_frsize=1),
                                 lambda: (state["reserved"], state["may_use"]))
        for state["reserved"], state["may_use"], state["free"] in values:
            sensor.read()
        self.assertEqual((36 * GIB, 4 * GIB, 3), (sensor.minimum_free, sensor.maximum_unaccounted, sensor.polls))

    def test_a_reading_that_is_not_a_whole_non_negative_number_of_bytes_refuses(self):
        class Int(int):
            pass
        for bad in (-1, True, 1.5, None, "1", Int(1), 1 << 60):
            for place in ("reserved", "may_use", "statvfs"):
                with self.subTest(bad=bad, place=place):
                    sensor, _ = self.sensor(bad if place == "statvfs" else 10 * GIB,
                                            bad if place == "reserved" else 0, bad if place == "may_use" else 0)
                    with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-reading"):
                        sensor.read()

    def test_a_failing_read_refuses_and_is_not_swallowed(self):
        def failing_statvfs(path):
            raise OSError("authored statvfs failure")
        sensor = owned.FreeSpace(Path("/authored"), failing_statvfs, lambda: (0, 0))
        with self.assertRaisesRegex(owned.OwnedError, "^owned-space-sensor-reading$"):
            sensor.read()
        self.assertEqual(0, sensor.polls)

    def test_a_counter_is_decimal_digits_and_nothing_else(self):
        with tempfile.TemporaryDirectory(prefix="ice-owned-counter-") as temp:
            folder = Path(temp)
            for text, value in [(b"123\n", 123), (b"0\n", 0), (b"7", 7), (b"9" * 18 + b"\n", 10 ** 18 - 1)]:
                (folder / "bytes_reserved").write_bytes(text)
                self.assertEqual(value, owned.space_counter(folder, "bytes_reserved"))
            for text in [b"", b"\n", b"-1\n", b"12 3\n", b"0x10\n", b"1e3\n", b"123\n\n", b" 123\n", b"1" * 19 + b"\n",
                         b"12\x00\n", b"\xff", b"1.5\n"]:
                with self.subTest(text=text):
                    (folder / "bytes_reserved").write_bytes(text)
                    with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-reading"):
                        owned.space_counter(folder, "bytes_reserved")
            with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-reading"):
                owned.space_counter(folder, "bytes_may_use")                 # not there

    def test_only_a_btrfs_folder_gives_counters(self):
        for path in ("/proc", "/sys", "/dev/shm"):          # procfs, sysfs and tmpfs, whatever TMPDIR is
            with self.subTest(path=path), self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-filesystem"):
                owned.btrfs_data_folder(path)
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-path"):
            owned.btrfs_data_folder("/authored-folder-that-is-not-there")
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-filesystem"):
            owned.FreeSpace(Path("/proc")).read()               # the real binding refuses too, with no fallback

    def test_a_sysfs_folder_that_cannot_be_looked_at_refuses(self):
        with patch.object(owned, "BTRFS_SYSFS", Path("/authored-sysfs-that-is-not-there")):
            with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-filesystem"):
                owned.btrfs_data_folder(LOCAL_DATA)
        with patch.object(owned.Path, "is_dir", side_effect=PermissionError("authored")):
            with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-filesystem"):
                owned.btrfs_data_folder(LOCAL_DATA)

    def test_the_counters_must_fit_what_statvfs_says_is_used(self):
        """The folder found is checked against the file system under the path: a counter that cannot be bytes of it refuses."""
        with tempfile.TemporaryDirectory(prefix="ice-owned-units-") as temp:
            folder = Path(temp) / "data"
            folder.mkdir()
            sensor = owned.FreeSpace(Path(temp), lambda path: NS(f_blocks=1000, f_bfree=900, f_bavail=900, f_frsize=1))
            # 1,000 bytes in all, 100 of them used (statvfs); the folder's bytes_used, then what it has reserved
            for used, reserved, ok in [(100, 0, True), (101, 0, False), (50, 951, False), (50, 950, True)]:
                with self.subTest(used=used, reserved=reserved):
                    (folder / "bytes_used").write_text(str(used))
                    (folder / "bytes_reserved").write_text(str(reserved))
                    (folder / "bytes_may_use").write_text("0")
                    with patch.object(owned, "btrfs_data_folder", return_value=folder):
                        sensor._unaccounted = None
                        if ok:
                            sensor._bind()
                        else:
                            with self.assertRaisesRegex(owned.OwnedError, "owned-space-sensor-units"):
                                sensor._bind()


LOCAL_DATA = Path(__file__).resolve().parents[2] / "local-data"


class RealRootSensorTests(unittest.TestCase):
    """The sensor against this machine's root file system (btrfs), in the repository's own ignored local-data."""
    def setUp(self):
        LOCAL_DATA.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="room-sensor-", dir=LOCAL_DATA)
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def test_the_sensor_finds_this_file_systems_counters_and_they_agree_with_btrfs(self):
        sensor = owned.FreeSpace(self.dir)
        reading = sensor.read()
        folder = owned.btrfs_data_folder(self.dir)
        self.assertEqual((Path("/sys/fs/btrfs"), "allocation", "data"), (folder.parents[2], folder.parent.name, folder.name))
        # An independent identification: btrfs-progs asks the file system itself (BTRFS_IOC_SPACE_INFO) for its data
        # bytes; the folder found by the file system's id holds the same number, give or take what runs meanwhile.
        out = subprocess.run(["btrfs", "filesystem", "df", "--raw", str(self.dir)], capture_output=True, text=True,
                             timeout=15, check=True).stdout
        used = int(re.search(r"^Data, [a-z0-9]+: total=[0-9]+, used=([0-9]+)$", out, re.M).group(1))
        self.assertLess(abs(used - owned.space_counter(folder, "bytes_used")), 2 * GIB)
        st = os.statvfs(self.dir)
        self.assertLess(abs(reading["statvfs_free"] - st.f_bavail * st.f_frsize), 4 * GIB)
        self.assertEqual(max(0, reading["statvfs_free"] - reading["unaccounted"]), reading["free"])
        self.assertGreaterEqual(reading["unaccounted"], 0)

    def test_a_buffered_write_is_in_the_reading_at_once(self):
        """128 MiB of incompressible data, written and not synced: the next reading is that much lower, whether or not
        statvfs has moved (it may not for half a minute)."""
        sensor = owned.FreeSpace(self.dir)
        size = 128 << 20
        data = os.urandom(size)
        drops = []
        for attempt in range(3):            # other work on the machine can free space between two readings
            before = sensor.read()
            with open(self.dir / f"written-{attempt}", "wb") as stream:
                stream.write(data)
            after = sensor.read()
            drops.append(before["free"] - after["free"])
            if drops[-1] >= 0.9 * size:
                break
        self.assertGreaterEqual(max(drops), 0.9 * size, drops)

    def test_a_small_buffered_write_waits_in_may_use_and_the_reading_counts_it_there(self):
        """16 MiB is under the dirty thresholds, so it stays in the page cache, reserved as bytes_may_use, for a
        while: the sensor must count that counter as well as bytes_reserved."""
        sensor = owned.FreeSpace(self.dir)
        folder = owned.btrfs_data_folder(self.dir)
        size = 16 << 20
        data = os.urandom(size)
        results = []
        for attempt in range(4):
            may_use = owned.space_counter(folder, "bytes_may_use")
            before = sensor.read()
            with open(self.dir / f"small-{attempt}", "wb") as stream:
                stream.write(data)
            after = sensor.read()
            results.append((owned.space_counter(folder, "bytes_may_use") - may_use, before["free"] - after["free"]))
            if results[-1][0] >= 0.5 * size and results[-1][1] >= 0.9 * size:
                break
        self.assertTrue(any(counter >= 0.5 * size and drop >= 0.9 * size for counter, drop in results), results)

    def test_the_gate_binds_the_real_sensor_by_default(self):
        self.assertIs(owned.os.statvfs, owned.OwnedTransient.__init__.__kwdefaults__["statvfs"])
        self.assertIsNone(owned.OwnedTransient.__init__.__kwdefaults__["unaccounted"])


class FreezerTests(unittest.TestCase):
    """The process handle: it names the run's QEMU by its pidfd and its UUID, and nothing else."""
    def setUp(self):
        self.children = []
        self.addCleanup(self.reap)

    def reap(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait()

    def child(self, *args):
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)", *args],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(child)
        # Popen returns when the child has exec'd, a moment before the kernel shows its command line: wait for it.
        self.assertTrue(wait_for(lambda: b"time.sleep(60)" in Path(f"/proc/{child.pid}/cmdline").read_bytes()))
        return child

    @staticmethod
    def state(pid):
        with open(f"/proc/{pid}/stat", "rb") as stream:
            return stream.read().rsplit(b") ", 1)[1][:1].decode()

    def test_it_halts_the_process_its_command_line_names_and_a_kill_still_ends_it(self):
        run_uuid = str(uuid.uuid4())
        child = self.child(run_uuid)
        freezer = owned.Freezer(child.pid, run_uuid)
        self.assertNotEqual("T", self.state(child.pid))
        freezer.stop()
        self.assertTrue(wait_for(lambda: self.state(child.pid) == "T"))
        child.kill()                                       # libvirt's SIGKILL ends a stopped process
        self.assertEqual(-signal.SIGKILL, child.wait(timeout=10))
        freezer.close()
        freezer.close()                                    # closing twice is harmless

    def test_it_refuses_a_process_that_does_not_carry_the_uuid_and_signals_nothing(self):
        run_uuid = str(uuid.uuid4())
        for case, args, uuid_asked in [("none", (), run_uuid), ("another uuid", (str(uuid.uuid4()),), run_uuid),
                                       ("a part of an argument", ("prefix-" + run_uuid,), run_uuid),
                                       ("the uuid of nobody", (run_uuid,), str(uuid.uuid4()))]:
            with self.subTest(case=case):
                child = self.child(*args)
                before = len(os.listdir("/proc/self/fd"))
                with self.assertRaisesRegex(owned.OwnedError, "owned-freeze-identity"):
                    owned.Freezer(child.pid, uuid_asked)
                self.assertEqual(before, len(os.listdir("/proc/self/fd")))      # no pidfd left open
                self.assertNotEqual("T", self.state(child.pid))

    def test_it_refuses_this_process_the_init_process_and_pids_that_cannot_be_one(self):
        for pid in (os.getpid(), 1, 0, -1, True, "1", None, 2 ** 22 + 12345):
            with self.subTest(pid=pid):
                with self.assertRaises((owned.OwnedError, OSError)):
                    owned.Freezer(pid, str(uuid.uuid4()))

    def test_a_process_that_has_already_gone_is_reported_as_gone_not_as_a_failure(self):
        run_uuid = str(uuid.uuid4())
        child = self.child(run_uuid)
        freezer = owned.Freezer(child.pid, run_uuid)
        child.kill()
        child.wait()
        with self.assertRaises(ProcessLookupError):
            freezer.stop()
        freezer.close()


class HostileSpaceTests(unittest.TestCase):
    """What the independent review of the first watchdog found, and what a hostile caller or machine can do to this one."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-hostile-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.world = World(self.root)
        self.metadata = {"run_id": "authored-native-run", "owned_backend": {"run_id": "wg-20261102T1600Z-authored-01"},
                         "execution_paths": {"vm_home": str(self.root)}, "files": STAGED["files"]}

    def machine(self, **more):
        more.setdefault("poll_seconds", 0.02)
        machine = owned.OwnedTransient(self.metadata, modules=self.world.modules(), **self.world.kwargs(**more))
        self.addCleanup(machine.watch_stop.set)
        return machine

    def start(self, machine):
        with patch.object(owned, "version_probe", return_value="authored-version"), \
                patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
            machine.start(630)

    def test_the_review_case_a_failed_destroy_is_retried_and_the_run_is_told(self):
        """Round 1: one destroy attempt, swallowed; the guest ran on until the gate's own stop; nothing told the run."""
        self.world.destroy_failures = 1
        machine = self.machine()
        self.start(machine)
        self.world.free[0] = LINE - 1
        self.assertTrue(wait_for(lambda: self.world.owned_state == "undefined"))
        self.assertEqual(2, self.world.events.count("destroy-owned"))
        self.world.free[0] = LINE - 10 * GIB                     # still falling: nothing more to destroy
        time.sleep(0.2)
        self.assertEqual(2, self.world.events.count("destroy-owned"))
        with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-tripped"):
            machine.space_guard()                                # what the gate's next step sees
        self.assertFalse(machine.stop(90))
        self.assertEqual("owned-space-watchdog-stopped-guest", machine.report["failure_cause"])

    def test_the_review_case_hidden_bytes_of_a_writer_the_poll_cannot_outrun(self):
        """Round 1: a once-a-second statvfs watchdog overshot its line by up to 6.96 GB. Here the readings come from the
        counters, so the same hidden amounts trip at once, whatever statvfs says."""
        for hidden in (GIB, 4 * GIB, 7 * GIB, 13 * GIB):
            with self.subTest(hidden=hidden):
                self.world = World(self.root / f"hidden-{hidden}")
                self.metadata["execution_paths"]["vm_home"] = str(self.world.home)
                machine = self.machine()
                self.start(machine)
                self.world.free[0] = LINE + 14 * GIB             # statvfs sees 14 GiB of room over the line
                self.world.hidden[0] = hidden                    # the writer has already taken `hidden` of it
                tripped = wait_for(lambda: machine.space_tripped.is_set(), 0.5)
                self.assertEqual(hidden > 14 * GIB, tripped)     # exactly when what is taken exceeds the room
                self.world.hidden[0] = 14 * GIB + 1
                self.assertTrue(wait_for(lambda: machine.space_stopped))
                self.assertFalse(machine.stop(90))

    def test_the_review_case_another_writer_fills_root_and_the_record_can_tell(self):
        machine = self.machine()
        self.start(machine)
        self.world.hidden[0] = 20 * GIB                         # someone else's writes, not the guest's
        self.world.free[0] = LINE + 5 * GIB
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertFalse(machine.stop(90))
        report = machine.report
        self.assertEqual("owned-space-watchdog-stopped-guest", report["failure_cause"])
        self.assertTrue(report["disk_growth"]["within_admitted_disk_term"])      # the copy itself stayed small
        self.assertEqual(20 * GIB, report["space_watchdog"]["trip_reading"]["unaccounted"])

    def test_a_sensor_that_fails_after_arming_trips_and_stops_the_guest(self):
        machine = self.machine()
        self.start(machine)

        def broken():
            raise owned.OwnedError("owned-space-sensor-reading")
        self.world.unaccounted = broken
        machine.sensor._unaccounted = broken
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertFalse(machine.stop(90))
        watch = machine.report["space_watchdog"]
        self.assertEqual(("space-watch-failed", "owned-space-sensor-reading"), (watch["trip"], watch["watch_error"]))

    def test_a_sensor_failure_with_a_private_message_records_only_the_class(self):
        machine = self.machine()
        self.start(machine)

        def broken():
            raise RuntimeError("PRIVATE-CANARY private-location")
        machine.sensor._unaccounted = broken
        self.assertTrue(wait_for(lambda: machine.space_stopped))
        self.assertFalse(machine.stop(90))
        self.assertEqual("RuntimeError", machine.report["space_watchdog"]["watch_error"])
        self.assertNotIn("CANARY", json.dumps(machine.report))

    def test_stale_reader_guard_returns_while_its_dispatched_destroy_is_pending(self):
        sensor_release, destroy_release = threading.Event(), threading.Event()
        sensor_entered, destroy_entered, guard_returned = (threading.Event() for _ in range(3))
        calls, original = [0], self.world.statvfs
        def sensor(path):
            calls[0] += 1
            if calls[0] > 2:
                sensor_entered.set()
                sensor_release.wait(10)
            return original(path)
        self.world.statvfs = sensor
        machine = self.machine()
        self.start(machine)
        original_destroy = machine.provider.destroy
        def destroy(name):
            destroy_entered.set()
            destroy_release.wait(10)
            original_destroy(name)
        machine.provider.destroy = destroy
        caught = []
        def guard():
            try:
                machine.space_guard()
            except owned.OwnedError as error:
                caught.append(str(error))
            finally:
                guard_returned.set()
        caller = None
        try:
            self.assertTrue(sensor_entered.wait(2))
            self.world.advance(owned.STALE_SECONDS + 1)
            caller = threading.Thread(target=guard, daemon=True)
            caller.start()
            self.assertTrue(guard_returned.wait(0.5), "guard must not wait for libvirt")
            self.assertEqual(["owned-space-watchdog-tripped"], caught)
            self.assertTrue(destroy_entered.wait(1))
            self.assertFalse(machine.space_stopped)
        finally:
            sensor_release.set(); destroy_release.set(); machine.watch_stop.set()
            if caller is not None:
                caller.join(2)
            machine.watch_thread.join(2)
            if machine.trip_stop_thread is not None:
                machine.trip_stop_thread.join(2)
            machine.stop(1)

    def test_final_stop_never_reenters_a_hung_watchdog_destroy_or_closes_its_connection(self):
        """A concurrent stop is not successful cleanup; no second provider operation is issued."""
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        machine = self.machine()
        self.start(machine)
        real_destroy = machine.provider.destroy
        operations = []
        def hanging(name):
            operations.append("destroy")
            entered.set()
            release.wait(10)
            real_destroy(name)
        machine.provider.destroy = hanging
        self.world.free[0] = LINE - 1
        self.assertTrue(entered.wait(2))
        try:
            with patch.object(machine.provider, "domain_state", side_effect=AssertionError("concurrent state")), \
                    patch.object(machine.provider, "close", side_effect=AssertionError("concurrent close")):
                began = time.monotonic()
                self.assertFalse(machine.stop(1))
                self.assertLess(time.monotonic() - began, 0.5)
            self.assertEqual(["destroy"], operations)
            self.assertFalse(machine.report["cleanup_verified"])
            self.assertFalse(machine.report["connection_closed"])
            self.assertEqual("owned-stop-in-flight", machine.report["cleanup_failure_code"])
            self.assertTrue(machine.report["connection_close_deferred"])
            self.assertTrue(machine.report["space_watchdog"]["tripped"])
            self.assertFalse(machine.report["space_watchdog"]["stopped_guest"])
            self.assertEqual(1, machine.report["space_watchdog"]["stop_attempts"])
            self.assertTrue(machine.report["forced"])
            self.assertEqual("not-run-stop-in-flight", machine.report["base_guard_after_stop"])
        finally:
            release.set()
            machine.watch_thread.join(2)
        self.assertFalse(machine.watch_thread.is_alive())
        self.assertFalse(machine.stop(1))
        self.assertTrue(machine.report["connection_closed"])
        self.assertTrue(machine.report["cleanup_verified"])

    def test_two_threads_never_destroy_at_once(self):
        active, worst, lock = [0], [0], threading.Lock()
        provider = self.world.provider

        def counting_provider(layout, recorded):
            inner = provider(layout, recorded)
            real_destroy = inner.destroy

            def destroy(name):
                with lock:
                    active[0] += 1
                    worst[0] = max(worst[0], active[0])
                time.sleep(0.05)
                with lock:
                    active[0] -= 1
                real_destroy(name)
            inner.destroy = destroy
            return inner
        self.world.provider = counting_provider
        self.world.destroy_leaves = 20
        machine = self.machine()
        self.start(machine)
        self.world.free[0] = LINE - 1
        callers = [threading.Thread(target=lambda: [machine._stop_for_space(1, 40) for _ in range(5)]) for _ in range(3)]
        self.assertTrue(wait_for(lambda: machine.space_tripped.is_set()))
        for caller in callers:
            caller.start()
        for caller in callers:
            caller.join()
        self.assertEqual(1, worst[0])
        self.assertFalse(machine.stop(90))

    def test_a_watch_thread_that_never_started_is_not_joined_and_the_connection_still_closes(self):
        with patch.object(owned.threading.Thread, "start", lambda thread: None):
            machine = self.machine()
            with patch.object(owned, "version_probe", return_value="authored-version"), \
                    patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + self.world.uuid.encode() + b"\0"):
                with self.assertRaisesRegex(owned.OwnedError, "owned-space-watchdog-unarmed"):
                    machine.start(630)
        machine.stop(90)
        self.assertTrue(machine.report["connection_closed"])

    def test_the_freeze_handle_is_closed_even_when_the_run_ends_by_a_failure(self):
        self.world.refuse_qemu = True
        machine = self.machine()
        with self.assertRaises(owned.OwnedError):
            self.start(machine)
        machine.stop(90)
        self.assertEqual([], self.world.freezers)                # refused before any handle was taken
        self.world.refuse_qemu = False
        self.world = World(self.root / "second")
        self.metadata["execution_paths"]["vm_home"] = str(self.world.home)
        machine = self.machine()
        self.start(machine)
        machine.stop(90)
        self.assertEqual(1, self.world.events.count("freezer-closed"))


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-binding-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "repository"
        self.home = self.root / "machine"
        (self.repo / "tools/windows-gate").mkdir(parents=True)
        (self.home / "gate").mkdir(parents=True)
        (self.home / "gate/base.json").write_bytes(b'{"authored":true}')
        self.files = {}
        for path in owned.SOURCE_PATHS:
            data = b"# authored backend presence fixture\n"
            (self.repo / path).write_bytes(data)
            self.files[path] = owned.sha(data)
        self.spec = {"repository": str(self.repo), "commit": owned.REVIEWED_COMMIT,
                     "files": self.files, "base_declaration_sha256": owned.sha(b'{"authored":true}'),
                     "run_id": "wg-20261102T1600Z-authored-01"}

    def git(self, command, **kwargs):
        if "rev-parse" in command:
            return NS(returncode=0, stdout=owned.REVIEWED_COMMIT + "\n")
        relative = command[-1].split(":", 1)[1]
        return NS(returncode=0, stdout=(self.repo / relative).read_bytes())

    def test_complete_closure_and_declaration_are_bound(self):
        with patch.object(owned.subprocess, "run", side_effect=self.git):
            result = owned.binding(self.spec, self.home)
        self.assertEqual(set(owned.SOURCE_PATHS), set(result["files"]))
        self.assertEqual(self.spec["repository"], result["repository"])
        self.assertEqual(self.spec["run_id"], result["run_id"])

    def test_native_id_cannot_select_machine_identity(self):
        self.spec["run_id"] = "ice-authored-native-run"
        with self.assertRaisesRegex(owned.OwnedError, "run-id"):
            owned.binding(self.spec, self.home)

    def test_missing_changed_or_extra_member_refuses(self):
        with patch.object(owned.subprocess, "run", side_effect=self.git):
            changed = self.repo / owned.SOURCE_PATHS[0]
            changed.write_bytes(b"changed authored source")
            with self.assertRaisesRegex(owned.OwnedError, "drift"):
                owned.binding(self.spec, self.home)
            changed.write_bytes(b"# authored backend presence fixture\n")
            missing = dict(self.spec, files=dict(self.files))
            missing["files"].pop(owned.SOURCE_PATHS[0])
            with self.assertRaisesRegex(owned.OwnedError, "closure"):
                owned.binding(missing, self.home)
            self.spec["files"]["tools/windows-gate/unlisted.py"] = "0" * 64
            with self.assertRaisesRegex(owned.OwnedError, "closure"):
                owned.binding(self.spec, self.home)

    def test_new_local_import_is_refused_even_with_updated_own_pin(self):
        relative = owned.SOURCE_PATHS[0]
        data = b"import unlisted\n"
        (self.repo / relative).write_bytes(data)
        self.files[relative] = owned.sha(data)
        (self.repo / "tools/windows-gate/unlisted.py").write_bytes(b"# authored\n")
        with patch.object(owned.subprocess, "run", side_effect=self.git):
            with self.assertRaisesRegex(owned.OwnedError, "unsealed-import"):
                owned.binding(self.spec, self.home)


@unittest.skipUnless(REVIEWED_SOURCE, "explicit reviewed backend checkout required")
class ReviewedApiTests(unittest.TestCase):
    """Use exact pinned signatures and filesystem owners with authored inputs."""
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ice-owned-api-")
        cls.root = Path(cls.tmp.name)
        (cls.root / "gate").mkdir()
        declaration = b'{"authored":true}'
        (cls.root / "gate/base.json").write_bytes(declaration)
        cls.spec = {"repository": str(REVIEWED_SOURCE), "commit": owned.REVIEWED_COMMIT,
                    "files": {name: owned.sha((REVIEWED_SOURCE / name).read_bytes())
                              for name in owned.SOURCE_PATHS},
                    "base_declaration_sha256": owned.sha(declaration),
                    "run_id": "wg-20261102T1600Z-authored-01"}
        cls.real = owned.load(cls.spec, cls.root)

    @classmethod
    def tearDownClass(cls):
        for name in cls.real:
            owned.sys.modules.pop(name, None)
        cls.tmp.cleanup()

    def test_real_layout_rejects_native_id(self):
        machine = self.real["wg_machine"]
        with self.assertRaises(Exception):
            machine.RunLayout(self.root, "ice-authored-native-run")
        layout = machine.RunLayout(self.root, self.spec["run_id"])
        self.assertEqual(self.root / "gate-runs" / self.spec["run_id"], layout.run_dir)
        self.assertNotEqual(self.real["wg_common"].BASE_DOMAIN, layout.domain_name)

    def test_host_probe_matches_frozen_reference_for_equal_and_unequal_qemu_versions(self):
        result = subprocess.run(["git", "-C", str(REVIEWED_SOURCE), "show",
                    owned.REVIEWED_COMMIT + ":tools/windows-gate/wg_session.py"],
                    capture_output=True, timeout=15)
        self.assertEqual(0, result.returncode)
        tree = ast.parse(result.stdout)
        selected = [n for n in tree.body if isinstance(n, ast.Assign) and
                    any(isinstance(t, ast.Name) and t.id in {"QEMU_VERSION", "SWTPM_VERSION"}
                        for t in n.targets)]
        selected += [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "probe_host"]
        self.assertEqual(3, len(selected))
        future = ast.ImportFrom(module="__future__", names=[ast.alias(name="annotations")], level=0)
        reference_tree = ast.fix_missing_locations(ast.Module(body=[future, *selected], type_ignores=[]))
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp));modules = world.modules();provider = world.provider(None, None)
            hasher = lambda path: owned.sha(path.read_bytes())
            scope = {"re": re, "Path": Path, "ET": owned.ET, "sha256_bytes": owned.sha,
                     "sha256_file": hasher, "wg_machine": modules["wg_machine"]}
            exec(compile(reference_tree, "reviewed-host-probe-contract", "exec"), scope)
            class Executor:
                def run(self, command):
                    return NS(returncode=0, text=("QEMU emulator version 11.1.2\n" if
                        command[0] == "qemu-system-x86_64" else "TPM emulator version 0.10.2\n"))
            for api_version in ["11.1.2", "11.1.3"]:
                with self.subTest(api_version=api_version):
                    provider.versions = lambda: {"qemu": api_version, "libvirt": "12.8.0"}
                    expected = scope["probe_host"](provider, Executor(), world.host, world.base, hasher)
                    with patch.object(owned, "version_probe", side_effect=["11.1.2", "0.10.2"]):
                        observed = owned.probe_host(modules, provider, world.host, hasher)
                    self.assertEqual(expected, observed)
                    self.assertEqual(12, len(observed))
                    self.assertEqual([], self.real["wg_base"].pin_problems(expected, observed))
                    wanted = "11.1.2" if api_version == "11.1.2" else "11.1.2 (libvirt reports 11.1.3)"
                    self.assertEqual(wanted, observed["qemu_version"])

    def test_real_admission_uses_callable_probe_and_rejects_running_base(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp))
            provider = world.provider(None, None)
            check = self.real["wg_machine"].check_admission
            self.assertEqual([], check(provider, [], lambda port: False, 200 * 1024**3,
                                       0, world.base_name))
            world.base_state = "running"
            self.assertTrue(check(provider, [], lambda port: False, 200 * 1024**3,
                                  0, world.base_name))

    def test_real_admission_refusal_preserves_exact_inputs_without_reprobing(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp));modules = world.modules()
            machine, common = self.real["wg_machine"], self.real["wg_common"]
            modules["wg_machine"].check_admission = machine.check_admission
            modules["wg_machine"].room_required = machine.room_required
            modules["wg_common"].DOMAIN_PREFIX = common.DOMAIN_PREFIX
            calls = {"state": 0, "names": 0, "port": 0}
            def state(name):
                calls["state"] += 1
                self.assertEqual(world.base_name, name)
                return "running"
            def names():
                calls["names"] += 1
                return [world.base_name, common.DOMAIN_PREFIX + "authored"]
            def busy(port):
                calls["port"] += 1
                self.assertEqual(common.SSH_PORT, port)
                return True
            modules["wg_machine"].port_in_use = busy
            kept = 4096;report = {}
            growth = owned.run_growth(common, DECLARED, STAGED)
            frozen = machine.room_required(kept, growth["disk_bytes"])
            required = owned.run_room_required(common, growth, kept)
            problems = owned.observe_admission(modules,
                NS(domain_state=state, domain_names=names),
                [Path("/private/PRIVATE-AUTHORED-CANARY/BASE-SUSPECT")],
                frozen - 1, kept, report, growth)
            self.assertEqual(6, len(problems))      # five frozen reasons, then this run's room
            self.assertEqual(["base-suspect-marker", "base-not-stopped", "run-domain-exists",
                              "ssh-port-in-use", "insufficient-disk-space", "insufficient-disk-space"],
                             [r["code"] for r in report["reasons"]])
            self.assertEqual({"state": 1, "names": 1, "port": 1}, calls)
            self.assertEqual("running", report["base_state"])
            self.assertEqual(1, report["run_domain_count"])
            self.assertTrue(report["ssh_port_busy"])
            self.assertTrue(report["canonical_marker_present"])
            self.assertEqual(frozen - 1, report["free_bytes"])
            self.assertEqual(required, report["required_free_bytes"])
            self.assertEqual(frozen, report["frozen_required_free_bytes"])
            self.assertEqual(kept, report["retained_exclusive_bytes"])
            self.assertFalse(report["accepted"])
            self.assertNotIn("CANARY", json.dumps(report))
            self.assertNotIn("/private", json.dumps(report))

    def test_real_admission_exact_disk_boundary_passes_and_errors_propagate(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp));modules = world.modules();machine = self.real["wg_machine"]
            modules["wg_machine"].check_admission = machine.check_admission
            modules["wg_machine"].room_required = machine.room_required
            growth = owned.run_growth(self.real["wg_common"], DECLARED, STAGED)
            provider = world.provider(None, None);report = {}
            need = owned.run_room_required(self.real["wg_common"], growth, 0)
            self.assertEqual([], owned.observe_admission(modules, provider, [], need, 0, report, growth))
            self.assertTrue(report["accepted"])
            self.assertEqual("shut off", report["base_state"])
            # One byte under this run's line the frozen predicate alone, with the same disk term, admits;
            # one byte under its own line it refuses; this run's rule refuses both.
            frozen_line = machine.room_required(0, growth["disk_bytes"])
            self.assertEqual(frozen_line, report["frozen_required_free_bytes"])
            self.assertEqual([], machine.check_admission(provider, [], lambda port: False, need - 1, 0,
                                                         world.base_name, disk_bytes=growth["disk_bytes"]))
            self.assertEqual(1, len(machine.check_admission(provider, [], lambda port: False, frozen_line - 1, 0,
                                                            world.base_name, disk_bytes=growth["disk_bytes"])))
            report = {}
            problems = owned.observe_admission(modules, provider, [], need - 1, 0, report, growth)
            self.assertEqual(1, len(problems))
            self.assertEqual(["insufficient-disk-space"], [r["code"] for r in report["reasons"]])
            self.assertFalse(report["accepted"])
            error = RuntimeError("PRIVATE-AUTHORED-CANARY")
            def failed_state(name): raise error
            provider.domain_state = failed_state;report = {}
            with self.assertRaises(RuntimeError) as caught:
                owned.observe_admission(modules, provider, [], need, 0, report, growth)
            self.assertIs(error, caught.exception)
            self.assertEqual("RuntimeError", report["error_class"])
            self.assertNotIn("CANARY", json.dumps(report))

    def absent_provider(self, world, *, state_race=False, error_code=42):
        """Real pinned Virt, with only an authored libvirt connection underneath."""
        class LibvirtError(Exception):
            def get_error_code(self): return error_code

        class Domain:
            def __init__(self, vanish=False): self.vanish = vanish
            def state(self, flags=0):
                if self.vanish: raise LibvirtError("authored domain vanished")
                return (5, 0)

        class Connection:
            closed = False
            def lookupByName(self, name):
                if name == world.base_name: return Domain()
                if state_race: return Domain(vanish=True)
                raise LibvirtError("authored no-domain result")
            def close(self): self.closed = True

        connection = Connection()
        libvirt = NS(libvirtError=LibvirtError, VIR_ERR_NO_DOMAIN=42,
                     open=lambda uri: connection)

        def factory(layout, recorded):
            provider = self.real["wg_virt"].Virt(layout, recorded,
                        libvirt_module=libvirt, qemu_module=NS())
            authored = world.provider(layout, recorded)
            for name in ["persistent_xml", "versions", "machine_canonical", "to_native"]:
                setattr(provider, name, getattr(authored, name))
            return provider
        return factory, connection

    def test_real_virt_no_domain_contract_admits_until_authored_clone_refusal(self):
        for state_race in [False, True]:
            with self.subTest(state_race=state_race), tempfile.TemporaryDirectory(dir=self.root) as temp:
                world = World(Path(temp));world.refuse_clone = True
                factory, connection = self.absent_provider(world, state_race=state_race)
                metadata = {"run_id": "ice-authored-native-run", "owned_backend": self.spec,
                            "execution_paths": {"vm_home": temp}, "files": STAGED["files"]}
                machine = owned.OwnedTransient(metadata, modules=world.modules(),
                            **dict(world.kwargs(), provider_factory=factory))
                with patch.object(owned, "version_probe", return_value="authored-version"):
                    with self.assertRaisesRegex(owned.OwnedError, "clone-refused"):
                        machine.start(630)
                self.assertIn("clone-owned", world.events)
                self.assertNotIn("create-owned", world.events)
                self.assertTrue(machine.stop(90))
                self.assertTrue(connection.closed)

    def test_real_virt_other_lookup_error_cannot_grant_admission(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp));factory, connection = self.absent_provider(world, error_code=99)
            metadata = {"run_id": "ice-authored-native-run", "owned_backend": self.spec,
                        "execution_paths": {"vm_home": temp}, "files": STAGED["files"]}
            machine = owned.OwnedTransient(metadata, modules=world.modules(),
                        **dict(world.kwargs(), provider_factory=factory))
            with self.assertRaises(self.real["wg_virt"].VirtError):
                machine.start(630)
            self.assertTrue(machine.stop(90))
            self.assertTrue(connection.closed)
            self.assertNotIn("clone-owned", world.events)
            self.assertFalse(machine.layout.run_dir.exists())

    def test_real_signatures_and_owners_cover_full_lifecycle(self):
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            root = Path(temp)
            world = World(root, self.real["wg_machine"].BasePaths(root))
            modules = world.modules()
            for module, names in {
                    "wg_machine": ("check_admission", "build_domain_xml", "precreate_check",
                        "check_qemu_cmdline", "check_swtpm_cmdline", "check_open_files",
                        "lower_priority", "SpaceWatchdog", "room_required"),
                    "wg_runner": ("guard_base", "prepare_copies"),
                    "wg_base": ("load_declaration", "pin_problems")}.items():
                for name in names:
                    setattr(modules[module], name, create_autospec(
                        getattr(self.real[module], name), side_effect=getattr(modules[module], name)))
            for name in ("RunLayout", "BasePaths", "Pins"):
                setattr(modules["wg_machine"], name, getattr(self.real["wg_machine"], name))
            modules["wg_runner"].Context = self.real["wg_runner"].Context
            modules["wg_files"].Recorder = self.real["wg_files"].Recorder
            metadata = {"run_id": "ice-authored-native-run", "owned_backend": self.spec,
                        "execution_paths": {"vm_home": str(root)}, "files": STAGED["files"]}
            machine = owned.OwnedTransient(metadata, modules=modules, **world.kwargs())
            with patch.object(owned, "version_probe", return_value="authored-version"), \
                    patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + world.uuid.encode() + b"\0"):
                machine.start(630)
            world.events.append("collection")
            self.assertTrue(machine.stop(90))
            self.assertEqual(world.before, world.snapshot())
            self.assertLess(world.events.index("collection"), world.events.index("close-connection"))
            self.assertTrue(machine.rec.is_recorded(machine.layout.xml))
            self.assertEqual("disk.qcow2", machine.layout.disk.name)
            self.assertTrue(machine.report["disk_bound"]["within_admitted"])
            self.assertEqual(machine.layout.disk.stat().st_size, machine.report["disk_bound"]["file_bytes"])

    def test_real_room_rule_binds_above_frozen_rule_and_clears_its_watchdog(self):
        common, machine = self.real["wg_common"], self.real["wg_machine"]
        self.assertGreaterEqual(common.FLOOR_BYTES, owned.MINIMUM_FLOOR_BYTES)
        self.assertGreaterEqual(common.MARGIN_BYTES, owned.MINIMUM_MARGIN_BYTES)
        for measured in (None, ()):
            growth = owned.run_growth(common, DECLARED, STAGED, measured)
            for kept in (0, 4096, KEPT):
                with self.subTest(measured=measured, kept=kept):
                    need = owned.run_room_required(common, growth, kept)
                    frozen = machine.room_required(kept, growth["disk_bytes"])
                    self.assertEqual(sum(growth.values()) - growth["disk_bytes"], need - frozen)
                    self.assertGreater(need, frozen)
                    # What this run's admitted growth leaves is never under the line the watchdog stops at.
                    self.assertGreaterEqual(need - sum(growth.values()), common.FLOOR_BYTES + common.MARGIN_BYTES)
            if measured == ():          # no measured runs: the frozen default line, as before
                self.assertEqual(machine.room_required(0), machine.room_required(0, common.DISK_VIRTUAL_BYTES))
                self.assertGreater(growth["disk_bytes"], common.DISK_VIRTUAL_BYTES)
        line = common.FLOOR_BYTES + common.MARGIN_BYTES
        for free, room in [(line - 1, False), (line, True)]:
            with self.subTest(free=free):
                tripped = []
                dog = machine.SpaceWatchdog(self.root, tripped.append,
                                            statvfs=lambda path, free=free: NS(f_bavail=free, f_frsize=1))
                self.assertEqual(room, dog.check_once())
                self.assertEqual(not room, bool(tripped))

    def test_real_frozen_room_predicate_takes_the_measured_term(self):
        common, machine = self.real["wg_common"], self.real["wg_machine"]
        self.assertEqual(common.FLOOR_BYTES + 16 * GIB + common.MARGIN_BYTES + 7, machine.room_required(7, 16 * GIB))
        self.assertEqual(machine.room_required(7, common.DISK_VIRTUAL_BYTES), machine.room_required(7))
        for bad in (0, -1, True, 1.5, None, "1"):
            with self.subTest(bad=bad), self.assertRaises(machine.MachineError):
                machine.room_required(0, bad)
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp));provider = world.provider(None, None)
            line = machine.room_required(KEPT, 16 * GIB)
            check = machine.check_admission
            self.assertEqual([], check(provider, [], lambda port: False, line, KEPT, world.base_name,
                                       disk_bytes=16 * GIB))
            problems = check(provider, [], lambda port: False, line - 1, KEPT, world.base_name, disk_bytes=16 * GIB)
            self.assertEqual(["insufficient-disk-space"], [owned.admission_reason(p)["code"] for p in problems])
            self.assertTrue(check(provider, [], lambda port: False, line, KEPT, world.base_name))  # the default

    def test_real_watchdog_is_armed_only_at_floor_plus_margin(self):
        common, machine = self.real["wg_common"], self.real["wg_machine"]
        trips = []
        dog = owned.arm_watchdog(machine, common, self.root, trips.append,
                                 statvfs=lambda path: NS(f_bavail=1 << 50, f_frsize=1))
        self.assertIsInstance(dog, machine.SpaceWatchdog)
        self.assertEqual((1 << 50, []), (dog.minimum_free, trips))
        for other in (NS(FLOOR_BYTES=common.FLOOR_BYTES + 1, MARGIN_BYTES=common.MARGIN_BYTES),
                      NS(FLOOR_BYTES=common.FLOOR_BYTES, MARGIN_BYTES=common.MARGIN_BYTES - 1)):
            with self.subTest(other=other), self.assertRaisesRegex(owned.OwnedError, "watchdog-unarmed"):
                owned.arm_watchdog(machine, other, self.root, trips.append,
                                   statvfs=lambda path: NS(f_bavail=1 << 50, f_frsize=1))
        line = common.FLOOR_BYTES + common.MARGIN_BYTES
        with self.assertRaisesRegex(owned.OwnedError, "watchdog-unarmed"):
            owned.arm_watchdog(machine, common, self.root, trips.append,
                               statvfs=lambda path: NS(f_bavail=line - 1, f_frsize=1))
        self.assertEqual(1, len(trips))

    def test_real_watchdog_driven_by_the_sensor_trips_once_and_the_loop_destroys_until_gone(self):
        """The frozen class itself behind the sensor and our loop: it reads the counters through the sensor, calls on_trip
        once, and the loop (not the frozen class) keeps destroying until the domain is gone."""
        common, frozen = self.real["wg_common"], self.real["wg_machine"]
        line = common.FLOOR_BYTES + common.MARGIN_BYTES
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            world = World(Path(temp))
            world.destroy_failures = 2
            modules = world.modules()
            modules["wg_machine"].SpaceWatchdog = frozen.SpaceWatchdog
            for name in ("wg_machine", "wg_common"):
                modules[name].FLOOR_BYTES, modules[name].MARGIN_BYTES = common.FLOOR_BYTES, common.MARGIN_BYTES
            metadata = {"run_id": "ice-authored-native-run", "owned_backend": self.spec,
                        "execution_paths": {"vm_home": temp}, "files": STAGED["files"]}
            machine = owned.OwnedTransient(metadata, modules=modules, **world.kwargs(poll_seconds=0.02))
            self.addCleanup(machine.watch_stop.set)
            with patch.object(owned, "version_probe", return_value="authored-version"), \
                    patch.object(owned, "cmdline", side_effect=lambda pid: b"owned\0" + world.uuid.encode() + b"\0"):
                machine.start(630)
            self.assertIsInstance(machine.watchdog, frozen.SpaceWatchdog)
            world.free[0], world.hidden[0] = line + GIB, 2 * GIB        # statvfs shows room that btrfs has taken
            self.assertTrue(wait_for(lambda: machine.space_stopped))
            self.assertTrue(machine.watchdog.tripped)
            self.assertEqual(line - GIB, machine.watchdog.minimum_free)
            self.assertEqual(3, machine.stop_attempts)                  # failed, failed, gone
            world.free[0] = line - 10 * GIB                              # falling on: the frozen class is not asked again
            time.sleep(0.1)
            self.assertEqual(line - GIB, machine.watchdog.minimum_free)
            self.assertFalse(machine.stop(90))
            self.assertEqual("free-space-below-stop-line", machine.report["space_watchdog"]["trip"])

    def test_reviewed_closure_changes_only_the_room_predicate(self):
        """Every sealed module but wg_machine is byte-identical to the closure reviewed before; in wg_machine
        only room_required and check_admission's signature and room call differ."""
        def show(commit, relative):
            result = subprocess.run(["git", "-C", str(REVIEWED_SOURCE), "show", commit + ":" + relative],
                                    capture_output=True, timeout=15)
            self.assertEqual(0, result.returncode)
            return result.stdout
        parent = subprocess.run(["git", "-C", str(REVIEWED_SOURCE), "rev-parse", owned.REVIEWED_COMMIT + "^"],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(PREVIOUS_REVIEWED_COMMIT, parent.stdout.strip())
        for relative in owned.SOURCE_PATHS:
            if not relative.endswith("/wg_machine.py"):
                self.assertEqual(show(PREVIOUS_REVIEWED_COMMIT, relative), show(owned.REVIEWED_COMMIT, relative))
        before, after = (ast.parse(show(c, "tools/windows-gate/wg_machine.py"))
                         for c in (PREVIOUS_REVIEWED_COMMIT, owned.REVIEWED_COMMIT))
        names = lambda tree: [getattr(n, "name", None) for n in tree.body]
        self.assertEqual(names(before), names(after))
        changed = {"room_required", "check_admission"}
        for old, new in zip(before.body, after.body):
            if getattr(old, "name", None) not in changed:
                self.assertEqual(ast.dump(old), ast.dump(new))
        old, new = (next(n for n in t.body if getattr(n, "name", None) == "check_admission") for t in (before, after))
        self.assertEqual(ast.dump(old.args.args[0]), ast.dump(new.args.args[0]))
        self.assertEqual([ast.dump(a) for a in old.args.args], [ast.dump(a) for a in new.args.args])
        self.assertEqual([ast.dump(d) for d in old.args.defaults], [ast.dump(d) for d in new.args.defaults])
        self.assertEqual(([], ["disk_bytes"]), ([a.arg for a in old.args.kwonlyargs],
                                                [a.arg for a in new.args.kwonlyargs]))
        self.assertEqual(len(old.body), len(new.body))
        room = [i for i, s in enumerate(old.body) if isinstance(s, ast.Assign) and
                [t.id for t in s.targets if isinstance(t, ast.Name)] == ["need"]]
        self.assertEqual(1, len(room))
        for index, (o, n) in enumerate(zip(old.body, new.body)):
            if index != room[0]:
                self.assertEqual(ast.dump(o), ast.dump(n))
        self.assertEqual("room_required(kept_bytes, disk_bytes)", ast.unparse(new.body[room[0]].value))


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--reviewed-backend-source", type=Path)
    options, remaining = parser.parse_known_args()
    REVIEWED_SOURCE = options.reviewed_backend_source
    ReviewedApiTests.__unittest_skip__ = REVIEWED_SOURCE is None
    unittest.main(argv=[__file__, *remaining])
