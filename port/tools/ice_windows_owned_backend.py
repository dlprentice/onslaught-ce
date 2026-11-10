#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned adapter to the reviewed Windows gate's owned no-restore foundation.

The dependency is selected and sealed explicitly. This module never starts,
changes permissions on, redefines, shuts down or destroys the persistent base.
The libvirt connection owning AUTODESTROY stays open until collection and owned
shutdown have finished. Machine copies and failed evidence are retained. The only
process this module ever signals is the run's own QEMU, held by a pidfd that was
checked against the run's UUID (Freezer): SIGSTOP, at a space-watchdog trip.
"""
import ast
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import stat
import struct
import subprocess
import sys
import threading
import time
import types
import uuid
import xml.etree.ElementTree as ET


REVIEWED_COMMIT = "0152961bdb8651feb442149ec8a543b856152007"
MODULES = ("wg_common", "net_relay", "wg_plan", "wg_machine", "wg_files",
           "wg_base", "wg_runner", "wg_virt")
SOURCE_PATHS = tuple("tools/windows-gate/" + name + ".py" for name in MODULES)
RUN_ID = re.compile(r"wg-[0-9]{8}T[0-9]{4}Z-[a-z0-9]{4,12}-[0-9]{2}\Z")


class OwnedError(RuntimeError):
    pass


def require(condition, code):
    if not condition:
        raise OwnedError(code)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cmdline(pid):
    require(isinstance(pid, int) and pid > 1, "owned-process-identity")
    with (Path("/proc") / str(pid) / "cmdline").open("rb") as stream:
        data = stream.read(1024 * 1024 + 1)
    require(len(data) <= 1024 * 1024, "owned-process-command-limit")
    return data


def plain_bytes(path, limit=2 * 1024 * 1024):
    path = Path(path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_size <= limit,
                "owned-backend-input-type")
        data = os.read(fd, limit + 1)
        require(len(data) == info.st_size, "owned-backend-input-size")
        return data
    finally:
        os.close(fd)


def binding(spec, vm_home):
    """Bind every local Python import and the existing immutable declaration."""
    require(isinstance(spec, dict) and set(spec) == {
                "repository", "commit", "files", "base_declaration_sha256", "run_id"} and
            spec.get("commit") == REVIEWED_COMMIT,
            "owned-backend-source-commit")
    require(isinstance(spec["run_id"], str) and RUN_ID.fullmatch(spec["run_id"]) is not None,
            "owned-backend-run-id")
    require(set(spec.get("files", {})) == set(SOURCE_PATHS), "owned-backend-closure")
    repository = Path(spec["repository"])
    require(repository.is_absolute() and repository.resolve() == repository,
            "owned-backend-repository")
    result = subprocess.run(["git", "-C", str(repository), "rev-parse", "HEAD"],
                            capture_output=True, text=True, timeout=15)
    require(result.returncode == 0 and result.stdout.strip() == REVIEWED_COMMIT,
            "owned-backend-checkout")
    captured = {}
    for relative in SOURCE_PATHS:
        data = plain_bytes(repository / relative)
        expected = spec["files"][relative]
        require(isinstance(expected, str) and sha(data) == expected,
                "owned-backend-source-drift")
        committed = subprocess.run(["git", "-C", str(repository), "show",
                                    REVIEWED_COMMIT + ":" + relative],
                                   capture_output=True, timeout=15)
        require(committed.returncode == 0 and sha(committed.stdout) == expected,
                "owned-backend-source-pin")
        captured[relative] = data
    # A newly added local import must become a reviewed, sealed closure member.
    directory = repository / "tools/windows-gate"
    for data in captured.values():
        for node in ast.walk(ast.parse(data)):
            names = ([a.name for a in node.names] if isinstance(node, ast.Import)
                     else [node.module] if isinstance(node, ast.ImportFrom) and node.module else [])
            for name in names:
                if (directory / (name + ".py")).exists():
                    require(name in MODULES, "owned-backend-unsealed-import")
    declaration = plain_bytes(Path(vm_home) / "gate/base.json")
    require(sha(declaration) == spec.get("base_declaration_sha256"),
            "owned-backend-base-declaration-drift")
    return {"repository": str(repository), "commit": REVIEWED_COMMIT,
            "files": dict(spec["files"]), "base_declaration_sha256": sha(declaration),
            "run_id": spec["run_id"]}


def load(spec, vm_home):
    """Execute the just-verified bytes, rather than reopening source on import."""
    binding(spec, vm_home)
    root = Path(spec["repository"])
    captured = {name: plain_bytes(root / ("tools/windows-gate/" + name + ".py"))
                for name in MODULES}
    for name, data in captured.items():
        require(sha(data) == spec["files"]["tools/windows-gate/" + name + ".py"],
                "owned-backend-source-drift")
    loaded = {}
    for name in MODULES:
        require(name not in sys.modules, "owned-backend-module-already-loaded")
    try:
        for name in MODULES:
            module = types.ModuleType(name)
            module.__file__ = str(root / ("tools/windows-gate/" + name + ".py"))
            sys.modules[name] = module
            loaded[name] = module
            exec(compile(captured[name], module.__file__, "exec"), module.__dict__)
    except BaseException:
        for name in loaded:
            sys.modules.pop(name, None)
        raise
    return loaded


def diagnostic(stage, operation, returncode, stderr):
    """Only fixed diagnostic categories cross the receipt/output boundary."""
    raw = stderr if isinstance(stderr, bytes) else str(stderr).encode("utf-8", "replace")
    bounded = raw[:4096]
    lowered = bounded.lower()
    code = ("permission-denied" if b"permission denied" in lowered else
            "not-found" if b"no such file" in lowered else
            "connection-failed" if b"failed to connect" in lowered else
            "resource-busy" if b"busy" in lowered else "unclassified")
    return {"stage": stage, "operation": operation, "returncode": returncode,
            "reason": code, "stderr_bytes": len(raw), "stderr_truncated": len(raw) > 4096,
            "stderr_redacted": bool(raw), "stderr_content_retained": False}


def version_probe(command, pattern):
    completed = subprocess.run(command, capture_output=True, timeout=10)
    require(completed.returncode == 0 and len(completed.stdout) <= 65536,
            "owned-host-version-probe")
    found = re.search(pattern, completed.stdout)
    require(found is not None, "owned-host-version-unreadable")
    return found.group(1).decode("ascii")


def probe_host(modules, provider, host, hasher):
    """Match the reviewed foundation's host-pin comparison, without a guest."""
    xml = provider.persistent_xml()
    versions = provider.versions()
    qemu = version_probe([host["emulator_path"], "--version"],
                         rb"QEMU emulator version ([0-9]+\.[0-9]+\.[0-9]+)")
    swtpm = version_probe(["swtpm", "--version"],
                          rb"TPM emulator version ([0-9]+\.[0-9]+\.[0-9]+)")
    if versions["qemu"] != qemu:
        qemu += " (libvirt reports " + versions["qemu"] + ")"
    return {"firmware_path": host["firmware_path"],
            "firmware_sha256": hasher(Path(host["firmware_path"])),
            "emulator_path": host["emulator_path"],
            "qemu_version": qemu,
            "libvirt_version": versions["libvirt"], "swtpm_version": swtpm,
            "machine_alias": host["machine_alias"],
            "machine_type": provider.machine_canonical(host["machine_alias"]),
            "domain_uuid": ET.fromstring(xml).findtext("uuid"),
            "persistent_xml_sha256": sha(xml.encode()),
            "pci_reference": modules["wg_machine"].pci_devices(provider.to_native(xml)),
            "ssh_host_key_sha256": host.get("ssh_host_key_sha256", "")}


def pin_identity(field, value):
    """Retain exact identity without serializing private paths or structures."""
    try:
        raw = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    except (TypeError, ValueError):
        return {"type": type(value).__name__, "json_serializable": False}
    result = {"type": type(value).__name__, "json_sha256": sha(raw), "json_bytes": len(raw)}
    if isinstance(value, str):
        version = (field in {"qemu_version", "libvirt_version", "swtpm_version"} and
                   re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}(?: \(libvirt reports [0-9]+(?:\.[0-9]+){1,3}\))?", value))
        if version or field.endswith("_sha256") and re.fullmatch(r"[0-9a-f]{64}", value):
            result["value"] = value
    return result


def host_pin_report(expected, observed, keys, problems):
    comparisons = [{"field": key, "match": expected.get(key) == observed.get(key),
                    "declared": pin_identity(key, expected.get(key)),
                    "observed": pin_identity(key, observed.get(key))} for key in keys]
    return {"accepted": not bool(problems), "problem_count": len(problems),
            "mismatched_fields": [row["field"] for row in comparisons if not row["match"]],
            "comparisons": comparisons,
            "problem_identities": [{"sha256": sha(str(p).encode()),
                                     "bytes": len(str(p).encode())} for p in problems]}


def admission_reason(message):
    """Classify the reviewed messages without retaining private paths/text."""
    raw = str(message).encode("utf-8", "replace")
    prefixes = (("a base-suspect marker exists (", "base-suspect-marker"),
                ("the persistent domain ", "base-not-stopped"),
                ("a run domain already exists: ", "run-domain-exists"),
                ("port ", "ssh-port-in-use"),
                ("UDP port ", "udp-port-in-use"),
                ("free space ", "insufficient-disk-space"))
    code = next((code for prefix, code in prefixes if str(message).startswith(prefix)),
                "unclassified-admission-refusal")
    return {"code": code, "message_sha256": sha(raw), "message_bytes": len(raw)}


# Room. Admission reserves the copy's full modelled qcow2 growth, plus the unchanged
# 50 GiB floor, 5 GiB margin, non-disk terms and retained exclusive bytes. The frozen
# admission predicate receives that same worst-case disk term; this adapter adds the
# non-disk terms. The measured growth table below is retained for observations only.
#
# The previous measured-growth rule relied on stopping a writer within two seconds.
# Host writer experiments do not establish a bound on libvirt destroy, and sending
# SIGSTOP is not proof that every QEMU thread has stopped. Therefore neither observed
# write rates nor a successful freeze request reduce admission's disk term.
#
# The sensor subtracts Btrfs pending data bytes from statvfs: read bytes_may_use first,
# then bytes_reserved, then statvfs. A reservation moving along that sequence between
# reads is counted at least once. New allocations and other writers remain concurrent;
# this is not a filesystem reservation or a guarantee against unrelated disk growth.
# All unreadable sensor inputs refuse admission or trip an already armed watchdog.
#
# The watchdog still requests a stop at floor + margin and fails the run on a trip,
# dead/stalled reader or unverified cleanup. Stop attempts and elapsed times are
# recorded truthfully. Libvirt operations can exceed two seconds; the watchdog is an
# additional failure detector, not a proven finite-time substitute for reserved room.
# Raw earlier measurement receipts remain historical evidence in the lane records.
MINIMUM_FLOOR_BYTES = 50 * 1024 ** 3             # 50 GB in either unit: 50 GiB is above 50 * 1000 ** 3
MINIMUM_MARGIN_BYTES = 5 * 1024 ** 3             # the frozen margin; never lowered
DISK_GROWTH_FLOOR_BYTES = 16 * 1024 ** 3
DISK_GROWTH_FACTOR = 2
# The disk growth of every real owned run whose disk copy is kept on this machine: the bytes of the copy's
# extents that FIEMAP does not flag shared, or that no other kept copy holds, whichever is larger (logical
# extent bytes; root compresses, so physical bytes are at most these). Measured 2026-11-03T15:37Z, read
# only, by local-data/windows-room-20261103/measure_run_growth.py; btrfs filesystem du's Exclusive equals
# the unshared bytes for all six. Copies removed by retention or cleanup could not be measured.
MEASURED_DISK_GROWTH = (
    ("wg-20261030T2012Z-reh6s4-01", 354578432),
    ("wg-20261030T2012Z-reh6s6-01", 365408256),
    ("wg-20261030T2012Z-reh6s7-01", 1268715520),
    ("wg-20261030T2330Z-rel0113p5-01", 369389568),
    ("wg-20261101T1702Z-fwpub0113-03", 362119168),
    ("wg-20261103T0024Z-nativegate-13", 339263488),
)
# The watchdog's sensor and loop (see the room note). The ioctl number is _IOR(0x94, 31, a 1,024-byte reply).
BTRFS_IOC_FS_INFO = 0x8400941F
BTRFS_SYSFS = Path("/sys/fs/btrfs")
POLL_SECONDS = 0.1          # between two readings
STALE_SECONDS = 2.0         # the gate's own check: no completed reading for this long trips the watchdog
STOP_ATTEMPTS_WATCH = 100   # attempt-count cap; provider latency is not bounded by the poll interval
STOP_ATTEMPTS_FINAL = 5     # more attempts by whoever ends the run (start, the guard, stop), 0.2 s apart
STOP_RETRY_SECONDS = 0.2
STOP_LOG_ENTRIES = 16       # attempts kept in the receipt one by one; the count covers all
# Historical host measurements only; none bounds a real libvirt stop or reduces admission.
MEASURED_STOP_SECONDS = 2.0
MEASURED_WRITE_BURST_BYTES = 4_400_000_000
MEASURED_SENSOR_MISS_BYTES = 64 << 20
QCOW2_HEADER_BYTES = 112
# The declared base's qcow2 layout, read from a copy's header on 2026-11-03 (version 3, one internal
# snapshot). The run's own copy is checked against it after the clone, before any guest exists.
QCOW2_ADMITTED = {"cluster_bytes": 1 << 16, "l2_entry_bytes": 8, "refcount_bits": 16}
TPM_STATE_BYTES = 1 << 20          # the swtpm state directory; the declared state file is 11,059 B
STAGING_EXTRA_BYTES = 1 << 20      # task.xml and the tar headers staged beside the sealed payload
CAPTURE_SESSION_BYTES = 64 << 20   # the rest of net.pcap: one ssh connection per guest command or poll,
                                   # receipts read back, the guest's own traffic (813,641 B measured beside
                                   # a 6,517,777 B payload)
RECORD_BYTES = 16 << 20            # domain XML, created.json, the gate's receipts (27,482 B measured), the
                                   # QEMU log (7,544 B measured; virtlogd keeps at most four 2 MiB files)
                                   # and the swtpm log (0 B measured)


def ceil_div(value, unit):
    return -(-value // unit)


def qcow2_geometry(header):
    """The layout facts of a qcow2 header that bound its growth; any layout the bound does not model refuses."""
    require(isinstance(header, bytes) and len(header) >= 72 and header[:4] == b"QFI\xfb",
            "owned-disk-format")
    (version, backing_offset, backing_size, cluster_bits, virtual, crypt, _, _, _, _, snapshots,
     _) = struct.unpack_from(">IQIIQIIQQIIQ", header, 4)
    incompatible, refcount_order = 0, 4
    if version == 3:
        require(len(header) >= 104, "owned-disk-format")
        incompatible, _, _, refcount_order, length = struct.unpack_from(">QQQII", header, 72)
        require(length >= 104, "owned-disk-format")
    # Modelled incompatible bits: dirty (lazy refcounts), compression type, extended L2 entries. The
    # corrupt and external-data bits, unknown bits, a backing file and encryption all refuse.
    require(version in (2, 3) and backing_offset == 0 and backing_size == 0 and crypt == 0 and
            9 <= cluster_bits <= 21 and 0 < virtual <= 1 << 50 and incompatible & ~0x19 == 0 and
            0 <= refcount_order <= 6, "owned-disk-format")
    return {"virtual_bytes": virtual, "cluster_bytes": 1 << cluster_bits,
            "l2_entry_bytes": 16 if incompatible & 0x10 else 8,
            "refcount_bits": 1 << refcount_order, "snapshots": snapshots}


def qcow2_growth(geometry, file_bytes):
    """The most a reflink copy of this qcow2 can come to hold alone: every guest cluster written once,
    every L2 table, the L1 table and the header rewritten, and refcount blocks and table for the longest
    the file can become. Discard punches holes, which hold nothing."""
    require(type(file_bytes) is int and file_bytes > 0, "owned-room-inputs")
    cluster = geometry["cluster_bytes"]
    data = ceil_div(geometry["virtual_bytes"], cluster)
    tables = ceil_div(data, cluster // geometry["l2_entry_bytes"])
    l1 = ceil_div(tables * 8, cluster)
    fixed = ceil_div(file_bytes, cluster) + data + tables + l1 + 1
    per_block = cluster * 8 // geometry["refcount_bits"]
    blocks = table = 0
    for _ in range(64):
        total = fixed + blocks + table
        wanted = (ceil_div(total, per_block), ceil_div(ceil_div(total, per_block) * 8, cluster))
        if wanted == (blocks, table):
            return {"disk_data_bytes": data * cluster,
                    "disk_metadata_bytes": (tables + l1 + 1 + blocks + table) * cluster}
        blocks, table = wanted
    raise OwnedError("owned-room-inputs")


def declared_size(declaration, key):
    try:
        value = declaration["files"][key]["stat"]["size"]
    except (KeyError, TypeError):
        value = None
    require(type(value) is int and value > 0, "owned-room-inputs")
    return value


def disk_terms(common, declaration, measured=None):
    """Reserve full modelled disk growth; retain measured estimates for observation only.

    No measured run proves a bounded stop of another guest. A measurement that names
    no owned run, names one twice, or exceeds the modelled worst case still refuses.
    """
    measured = MEASURED_DISK_GROWTH if measured is None else measured
    worst = qcow2_growth(dict(QCOW2_ADMITTED, virtual_bytes=common.DISK_VIRTUAL_BYTES),
                         declared_size(declaration, "disk"))
    worst_case = worst["disk_data_bytes"] + worst["disk_metadata_bytes"]
    require(isinstance(measured, tuple) and
            all(isinstance(row, tuple) and len(row) == 2 for row in measured), "owned-room-measurements")
    names = [name for name, _ in measured]
    require(len(set(names)) == len(names) and
            all(isinstance(name, str) and RUN_ID.fullmatch(name) is not None for name in names) and
            all(type(value) is int and 0 < value <= worst_case for _, value in measured),
            "owned-room-measurements")
    largest = max((value for _, value in measured), default=None)
    estimate = (None if largest is None else
                min(worst_case, max(DISK_GROWTH_FACTOR * largest, DISK_GROWTH_FLOOR_BYTES)))
    return {"basis": "worst-case", "disk_bytes": worst_case,
            "measured_estimate_bytes": estimate, "stop_latency_bound_proven": False,
            "worst_case_bytes": worst_case, "measured_runs": len(measured),
            "largest_measured_bytes": largest, "factor": DISK_GROWTH_FACTOR,
            "floor_bytes": DISK_GROWTH_FLOOR_BYTES}


def run_growth(common, declaration, metadata, measured=None):
    """This run's growth on root, by what it writes there (see the room note above)."""
    staged = metadata.get("files") if isinstance(metadata, dict) else None
    require(isinstance(staged, dict) and bool(staged) and
            all(isinstance(pin, dict) and type(pin.get("size")) is int and pin["size"] >= 0
                for pin in staged.values()), "owned-room-inputs")
    return {"disk_bytes": disk_terms(common, declaration, measured)["disk_bytes"],
            "firmware_vars_bytes": declared_size(declaration, "vars"),
            "tpm_state_bytes": TPM_STATE_BYTES,
            "network_capture_bytes": 2 * (sum(pin["size"] for pin in staged.values()) +
                                          STAGING_EXTRA_BYTES) + CAPTURE_SESSION_BYTES,
            "record_bytes": RECORD_BYTES}


def run_room_required(common, growth, kept_bytes):
    """The free space this run needs: the floor and margin still there after its growth and the kept
    bytes."""
    require(type(common.FLOOR_BYTES) is int and common.FLOOR_BYTES >= MINIMUM_FLOOR_BYTES and
            type(common.MARGIN_BYTES) is int and common.MARGIN_BYTES >= MINIMUM_MARGIN_BYTES,
            "owned-room-floor")
    require(type(kept_bytes) is int and kept_bytes >= 0 and isinstance(growth, dict) and
            type(growth.get("disk_bytes")) is int and growth["disk_bytes"] > 0 and
            all(type(value) is int and value >= 0 for value in growth.values()), "owned-room-inputs")
    return common.FLOOR_BYTES + common.MARGIN_BYTES + sum(growth.values()) + kept_bytes


def copy_disk_bound(path, disk):
    """Read the run's own copy's header: its worst case must not exceed the declared disk's, the disk every
    measured run wrote to, or the measured term does not describe this copy."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "owned-disk-format")
        header = os.pread(fd, QCOW2_HEADER_BYTES, 0)
    finally:
        os.close(fd)
    geometry = qcow2_geometry(header)
    bound = qcow2_growth(geometry, info.st_size)
    copy = bound["disk_data_bytes"] + bound["disk_metadata_bytes"]
    return {**geometry, "file_bytes": info.st_size, "worst_case_bytes": copy,
            "admitted_worst_case_bytes": disk["worst_case_bytes"], "admitted_disk_bytes": disk["disk_bytes"],
            "within_admitted": copy <= disk["worst_case_bytes"]}


def arm_watchdog(machine, common, path, on_trip, statvfs):
    """The frozen SpaceWatchdog, armed: it reads the floor and margin admission used, a probe with authored
    free-space readings shows it stops exactly at floor + margin, and its first real reading is above that
    line. Anything else refuses; no copy or guest exists yet."""
    line = common.FLOOR_BYTES + common.MARGIN_BYTES
    require(getattr(machine, "FLOOR_BYTES", None) == common.FLOOR_BYTES and
            getattr(machine, "MARGIN_BYTES", None) == common.MARGIN_BYTES, "owned-space-watchdog-unarmed")
    for free, room in ((line, True), (line - 1, False)):
        trips = []
        probe = machine.SpaceWatchdog(path, trips.append,
                                      statvfs=lambda _path, free=free: types.SimpleNamespace(f_bavail=free,
                                                                                              f_frsize=1))
        require(probe.check_once() is room and len(trips) == (0 if room else 1),
                "owned-space-watchdog-unarmed")
    watchdog = machine.SpaceWatchdog(path, on_trip, statvfs=statvfs)
    require(watchdog.check_once() is True, "owned-space-watchdog-unarmed")
    return watchdog


def space_trip_code(reason):
    return (reason if reason in ("space-watch-failed", "space-watch-stalled") else
            "free-space-below-stop-line" if str(reason).startswith("free space ") else "unclassified-trip")


def btrfs_data_folder(path):
    """The sysfs folder with the data space counters of the btrfs file system under `path`. The file system is
    named by its own id (BTRFS_IOC_FS_INFO, an ioctl any user may call on any file or folder of it), never by a mount
    table or a device name. A path on anything but btrfs, or a file system whose counters are not there, refuses: the
    sensor below is only known to be exact on btrfs."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    except OSError:
        raise OwnedError("owned-space-sensor-path") from None
    try:
        reply = bytearray(1024)
        fcntl.ioctl(fd, BTRFS_IOC_FS_INFO, reply)
    except OSError:
        raise OwnedError("owned-space-sensor-filesystem") from None
    finally:
        os.close(fd)
    folder = BTRFS_SYSFS / str(uuid.UUID(bytes=bytes(reply[16:32]))) / "allocation" / "data"
    try:
        require(folder.is_dir(), "owned-space-sensor-filesystem")
    except OSError:
        raise OwnedError("owned-space-sensor-filesystem") from None
    return folder


def space_counter(folder, name):
    """One of the data space counters, in bytes: ASCII decimal digits and nothing else."""
    try:
        with open(Path(folder) / name, "rb", buffering=0) as stream:
            raw = stream.read(64)
    except OSError:
        raise OwnedError("owned-space-sensor-reading") from None
    require(re.fullmatch(rb"[0-9]{1,18}\n?", raw) is not None, "owned-space-sensor-reading")
    return int(raw)


class FreeSpace:
    """The free bytes under a path as the watchdog and admission read them: statvfs's, less the data bytes the file
    system has taken but statvfs does not show yet.

    btrfs counts a buffered write as used only when the delayed reference of its extent runs, at the latest at the
    next transaction commit (30 s by default), so statvfs (f_bavail * f_frsize, the figure df shows) can sit
    several GB above the truth for as long as a guest keeps writing. The same bytes are in the file system's data
    space counters the moment write(2) takes them: bytes_may_use while dirty in the page cache, bytes_reserved once
    writeback has chosen their extent; both are readable without root. Reading may-use, then reserved, then
    statvfs avoids losing an already counted reservation as it transfers along those stages. The readings are
    not atomic with new allocations or unrelated writers. Anything unreadable raises OwnedError: the caller
    refuses admission or trips the watchdog; there is no fallback to statvfs alone.

    `statvfs` and `unaccounted` are replaceable for authored tests; `unaccounted()` returns (reserved, may_use)."""

    def __init__(self, path, statvfs=os.statvfs, unaccounted=None):
        self.path = Path(path)
        self._statvfs = statvfs
        self._unaccounted = unaccounted
        self._lock = threading.Lock()
        self.polls = 0
        self.last = None
        self.minimum_free = None
        self.maximum_unaccounted = 0

    def _read_statvfs(self):
        try:
            return self._statvfs(self.path)
        except OSError:
            raise OwnedError("owned-space-sensor-reading") from None

    def _bind(self):
        folder = btrfs_data_folder(self.path)

        def unaccounted():
            may_use = space_counter(folder, "bytes_may_use")
            reserved = space_counter(folder, "bytes_reserved")
            return reserved, may_use
        reserved, may_use = unaccounted()
        # The folder belongs to this file system and counts bytes: the data it holds as used cannot exceed what
        # statvfs says is used on the whole file system, nor can that and what is reserved exceed its size.
        st = self._read_statvfs()
        require(all(type(v) is int and v >= 0 for v in (st.f_blocks, st.f_bfree, st.f_frsize)),
                "owned-space-sensor-units")
        total, used = st.f_blocks * st.f_frsize, (st.f_blocks - st.f_bfree) * st.f_frsize
        data = space_counter(folder, "bytes_used")
        require(data <= used and data + reserved + may_use <= total, "owned-space-sensor-units")
        self._unaccounted = unaccounted

    def read(self):
        """One reading: {free, statvfs_free, unaccounted} in bytes, `free` never negative."""
        if self._unaccounted is None:
            self._bind()
        reserved, may_use = self._unaccounted()
        require(all(type(v) is int and 0 <= v < 1 << 60 for v in (reserved, may_use)), "owned-space-sensor-reading")
        st = self._read_statvfs()
        require(type(st.f_bavail) is int and type(st.f_frsize) is int, "owned-space-sensor-reading")
        statvfs_free = st.f_bavail * st.f_frsize
        require(0 <= statvfs_free < 1 << 60, "owned-space-sensor-reading")
        hidden = reserved + may_use
        reading = {"free": max(0, statvfs_free - hidden), "statvfs_free": statvfs_free, "unaccounted": hidden}
        with self._lock:
            self.polls += 1
            self.last = reading
            self.minimum_free = (reading["free"] if self.minimum_free is None
                                 else min(self.minimum_free, reading["free"]))
            self.maximum_unaccounted = max(self.maximum_unaccounted, hidden)
        return reading

    def statvfs(self, path=None):
        """The reading in the shape the frozen SpaceWatchdog's statvfs parameter has: blocks of one byte."""
        return types.SimpleNamespace(f_bavail=self.read()["free"], f_frsize=1)


def observe_admission(modules, provider, markers, free_bytes, kept_bytes, report, growth, disk=None):
    """Call the admission predicates with the same observed inputs and this run's disk term, then this run's
    room rule."""
    machine, common, files = (modules[n] for n in ("wg_machine", "wg_common", "wg_files"))
    need = run_room_required(common, growth, kept_bytes)
    report.update(marker_count=len(markers),
                  canonical_marker_present=any(Path(p).name == files.MARKER_NAME for p in markers),
                  free_bytes=free_bytes, retained_exclusive_bytes=kept_bytes,
                  required_free_bytes=need,
                  frozen_required_free_bytes=machine.room_required(kept_bytes, growth["disk_bytes"]),
                  room={"floor_bytes": common.FLOOR_BYTES, "margin_bytes": common.MARGIN_BYTES,
                        "disk_bytes": growth["disk_bytes"], "disk": dict(disk) if disk else None,
                        "growth_bytes": sum(growth.values()), "growth": dict(growth)},
                  base_state=None, base_state_type=None, run_domain_count=None,
                  ssh_port=common.SSH_PORT, ssh_port_busy=None, accepted=None,
                  reason_count=None, reasons=[], reasons_truncated=False)

    def domain_state(name):
        value = provider.domain_state(name)
        if name == common.BASE_DOMAIN:
            report["base_state"] = (value if isinstance(value, str) and
                                     re.fullmatch(r"[a-z ]{1,32}", value) else None)
            report["base_state_type"] = type(value).__name__
        return value

    def domain_names():
        names = provider.domain_names()
        if isinstance(names, (list, tuple)):
            report["run_domain_count"] = sum(isinstance(name, str) and
                    name.startswith(common.DOMAIN_PREFIX) for name in names)
        return names

    def port_busy(port):
        value = machine.port_in_use(port)
        if port == common.SSH_PORT:
            report["ssh_port_busy"] = bool(value)
        return value

    view = types.SimpleNamespace(domain_state=domain_state, domain_names=domain_names)
    try:
        problems = machine.check_admission(view, markers, port_busy, free_bytes,
                                           kept_bytes, common.BASE_DOMAIN, disk_bytes=growth["disk_bytes"])
    except Exception as error:
        report["error_class"] = type(error).__name__
        raise
    if free_bytes < need:
        problems.append(f"free space {free_bytes} bytes is below the {need} bytes this run needs "
                        f"(floor {common.FLOOR_BYTES}, margin {common.MARGIN_BYTES}, growth "
                        f"{sum(growth.values())}, kept {kept_bytes})")
    report["accepted"] = not bool(problems)
    report["reason_count"] = len(problems)
    report["reasons"] = [admission_reason(p) for p in problems[:16]]
    report["reasons_truncated"] = len(problems) > 16
    return problems


class Freezer:
    """A handle on the run's QEMU process that cannot name another. The pidfd is opened first and the process's
    command line is read after it: only the process this pidfd holds can still show the run's own UUID, so a pid
    that was reused in between is refused. stop() sends SIGSTOP through the pidfd.
    Its return records a sent signal, not an observation that every QEMU thread has stopped.
    Admission does not rely on a bound for this request or libvirt's response.
    """

    def __init__(self, pid, run_uuid):
        require(isinstance(pid, int) and pid > 1, "owned-freeze-identity")
        self.fd = os.pidfd_open(pid)
        try:
            with open("/proc/" + str(pid) + "/cmdline", "rb") as stream:
                data = stream.read(1024 * 1024 + 1)
            require(len(data) <= 1024 * 1024 and run_uuid.encode() in data.split(b"\0"), "owned-freeze-identity")
        except BaseException:
            os.close(self.fd)
            raise

    def stop(self):
        signal.pidfd_send_signal(self.fd, signal.SIGSTOP)

    def close(self):
        fd, self.fd = self.fd, -1
        if fd >= 0:
            os.close(fd)


class OwnedTransient:
    def __init__(self, metadata, *, modules=None, provider_factory=None,
                 clock=time.monotonic, sleep=time.sleep, statvfs=os.statvfs, unaccounted=None,
                 poll_seconds=POLL_SECONDS, freezer_factory=Freezer):
        self.metadata = metadata
        self.vm_home = Path(metadata["execution_paths"]["vm_home"])
        self.modules = modules if modules is not None else load(metadata["owned_backend"], self.vm_home)
        self.clock, self.sleep, self.statvfs = clock, sleep, statvfs
        self.sensor = FreeSpace(self.vm_home, statvfs, unaccounted)
        self.poll_seconds = poll_seconds
        self.freezer_factory = freezer_factory
        self.freezer = None
        self.freeze_requested = False
        machine, files, base = (self.modules[n] for n in ("wg_machine", "wg_files", "wg_base"))
        machine_run_id = metadata["owned_backend"]["run_id"]
        require(RUN_ID.fullmatch(machine_run_id) is not None, "owned-backend-run-id")
        self.layout = machine.RunLayout(self.vm_home, machine_run_id)
        self.base = machine.BasePaths(self.vm_home)
        require(self.layout.run_dir == self.vm_home / "gate-runs" / machine_run_id,
                "owned-run-location")
        self.rec = files.Recorder(self.layout.run_dir, machine_run_id)
        self.decl = base.load_declaration(plain_bytes(self.vm_home / "gate/base.json"))
        host = self.decl["host"]
        self.pins = machine.Pins(host["machine_alias"], host["machine_type"], host["domain_uuid"],
                                 host["firmware_path"], host["emulator_path"])
        self.context = self.modules["wg_runner"].Context(
            self.layout, self.base, self.decl, self.pins, self.rec, self.vm_home / "gate")
        self.uuid = str(uuid.uuid4())
        require(self.uuid != host["domain_uuid"], "owned-domain-uuid-collision")
        self.provider = (provider_factory(self.layout, self.rec.is_recorded) if provider_factory else
                         self.modules["wg_virt"].Virt(self.layout, self.rec.is_recorded))
        self.connect_attempted = False
        self.connected = self.created = self.closed = False
        self.run_recorded = False
        self.create_attempted = False
        self.force_used = False
        self.growth = None
        self.space_tripped = threading.Event()
        self.space_lock = threading.Lock()      # the trip's record
        self.stop_lock = threading.Lock()       # one destroyer at a time
        self.space_stopped = False
        self.tripped_at = None
        self.stop_attempts = 0
        self.watch_stop = threading.Event()
        self.watch_thread = None
        self.trip_stop_thread = None
        self.watchdog = None
        self.last_poll = None                   # when the watch thread last began a reading
        self.polls = 0
        self.max_gap = 0.0                      # the longest time between two readings
        self.report = {"domain_name": self.layout.domain_name, "domain_uuid": self.uuid,
                       "run_id": machine_run_id,
                       "base_started": False, "created": False, "connection_closed": False,
                       "forced": False, "copies_retained": True,
                       "base_guard_after_stop": "not-run-no-owned-files",
                       "space_watchdog": {"armed": False, "stop_line_bytes": None, "first_free_bytes": None,
                                          "tripped": False, "trip": None, "stopped_guest": False,
                                          "minimum_free_bytes": None, "sensor": "btrfs-statvfs-less-unaccounted",
                                          "poll_seconds": poll_seconds, "polls": 0, "max_poll_gap_seconds": None,
                                          "max_unaccounted_bytes": None, "trip_reading": None,
                                          "stop_attempts": 0, "stop_log": [], "stop_seconds": None,
                                          "stop_gave_up": False, "freeze_ready": False, "freeze_status": "not-requested",
                                          "freeze_seconds": None},
                       "disk_growth": {"measured": False, "reason": "not-reached"}}

    def _states(self):
        return {self.modules["wg_common"].BASE_DOMAIN:
                self.provider.domain_state(self.modules["wg_common"].BASE_DOMAIN)}

    def _guard(self, phase):
        try:
            problems = self.modules["wg_runner"].guard_base(self.context, phase, self._states())
        except Exception as error:
            self.report["base_guard_failure"] = {"phase": phase,
                                                  "exception_class": type(error).__name__}
            raise OwnedError("owned-base-guard-error") from None
        if problems:
            self.report["base_guard_failure"] = {"phase": phase, "problem_count": len(problems)}
        require(not problems, "immutable-base-identity-changed")

    def _kept_bytes(self):
        total = 0
        root = self.vm_home / "gate-runs"
        if not root.exists():
            return total
        count = 0
        for parent, folders, names in os.walk(root, followlinks=False):
            require(not any((Path(parent) / name).is_symlink() for name in folders),
                    "owned-runs-linked-directory")
            for name in names:
                path = Path(parent) / name
                info = path.lstat()
                require(stat.S_ISREG(info.st_mode), "owned-runs-nonregular-file")
                exclusive = self.modules["wg_files"].exclusive_bytes(path)
                # An unreadable extent map is unknown, never a zero-byte file.
                require(type(exclusive) is int and exclusive >= 0, "owned-kept-inventory-unreadable")
                total += exclusive
                count += 1
                require(count <= 100000, "owned-runs-inventory-limit")
        return total

    def prepare(self):
        machine, files = self.modules["wg_machine"], self.modules["wg_files"]
        self.connect_attempted = True
        self.provider.connect()
        self.connected = True
        require(self.layout.domain_name != self.modules["wg_common"].BASE_DOMAIN,
                "owned-domain-name-collision")
        # The pinned Virt contract uses "undefined" for both no-domain paths.
        # Unknown states and lookup/provider errors do not grant admission.
        require(self.provider.domain_state(self.layout.domain_name) == "undefined",
                "owned-domain-already-exists")
        require(not self.layout.run_dir.exists(), "owned-run-already-exists")
        self.report["admission"] = {}
        self.disk = disk_terms(self.modules["wg_common"], self.decl)
        self.growth = run_growth(self.modules["wg_common"], self.decl, self.metadata)
        try:
            kept = self._kept_bytes()
        except (OwnedError, OSError):
            self.report["admission"].update(accepted=False, kept_inventory_read=False, reason_count=1,
                                            reasons=[{"code": "kept-inventory-unreadable"}])
            raise OwnedError("owned-kept-inventory-unreadable") from None
        try:
            reading = self.sensor.read()
        except OwnedError:
            self.report["admission"].update(accepted=False, space_sensor_read=False, reason_count=1,
                                            reasons=[{"code": "space-sensor-unreadable"}])
            raise
        self.report["admission"].update(space_sensor_read=True, free_statvfs_bytes=reading["statvfs_free"],
                                        unaccounted_bytes=reading["unaccounted"])
        problems = observe_admission(self.modules, self.provider,
            files.marker_paths(self.vm_home / "gate"), reading["free"],
            kept, self.report["admission"], self.growth, self.disk)
        require(not problems, "owned-machine-admission-refused")
        self._arm_watchdog()
        probe = probe_host(self.modules, self.provider, self.decl["host"], self.context.hasher)
        base = self.modules["wg_base"]
        problems = base.pin_problems(self.decl["host"], probe)
        self.report["host_pins"] = host_pin_report(self.decl["host"], probe, base.HOST_KEYS, problems)
        require(not problems, "owned-machine-host-pin-drift")
        self.rec.create_run_dir()
        self.run_recorded = True
        self.report["recorder_created"] = True
        self._guard("fixture-before-clone")
        problems = self.modules["wg_runner"].prepare_copies(self.context, self._states())
        require(not problems, "owned-machine-clone-refused")
        self.report["disk_bound"] = copy_disk_bound(self.layout.disk, self.disk)
        require(self.report["disk_bound"]["within_admitted"], "owned-disk-growth-bound")
        xml = machine.build_domain_xml(self.base.domain_xml.read_text(), self.layout, True,
                                        self.pins, self.uuid, forwards=(), tunnel_socket=None,
                                        relay_forwards=())
        problems = machine.precreate_check(xml, self.layout, self.base, self.pins, True,
                                           (), None, relay_forwards=())
        require(not problems, "owned-machine-xml-refused")
        native = self.provider.to_native(xml)
        require(not machine.pci_problems(self.decl["host"]["pci_reference"],
                                         machine.pci_devices(native)), "owned-machine-pci-drift")
        require(not machine.memory_problems(machine.meminfo_available_bytes(
                    Path("/proc/meminfo").read_text()), machine.guest_memory_bytes(xml)),
                "owned-machine-memory-headroom")
        self.rec.write_new(self.layout.xml, xml.encode(), keep=True)
        self.xml = xml
        self.report["clone_verified"] = True

    def _arm_watchdog(self):
        """Admission is complete only once the frozen watchdog watches root at floor + margin, before any
        copy or guest exists; it then runs until stop(). Its reading is the sensor's (statvfs less what btrfs
        has taken and not yet shown), its loop is ours (see _watch), and a trip is retried until the guest is gone."""
        common, admission = self.modules["wg_common"], self.report["admission"]
        self.report["space_watchdog"]["stop_line_bytes"] = common.FLOOR_BYTES + common.MARGIN_BYTES
        try:
            self.watchdog = arm_watchdog(self.modules["wg_machine"], common, self.vm_home,
                                         self._space_trip, self.sensor.statvfs)
            self.last_poll = self.clock()
            thread = threading.Thread(target=self._watch, daemon=True)
            thread.start()
            self.watch_thread = thread
            require(thread.is_alive() and not self.space_tripped.is_set(), "owned-space-watchdog-unarmed")
        except Exception:
            admission.update(accepted=False, space_watchdog_armed=False,
                             reason_count=(admission.get("reason_count") or 0) + 1,
                             reasons=list(admission.get("reasons") or []) + [{"code": "space-watchdog-unarmed"}])
            raise OwnedError("owned-space-watchdog-unarmed") from None
        admission["space_watchdog_armed"] = True
        self.report["space_watchdog"].update(armed=True, first_free_bytes=self.watchdog.minimum_free)

    def _polled(self):
        """A reading is being taken: count it and keep the longest time between two."""
        now = self.clock()
        if self.last_poll is not None:
            self.max_gap = max(self.max_gap, now - self.last_poll)
        self.last_poll = now
        self.polls += 1

    def _watch(self):
        """One reading every poll_seconds until the run ends. After a trip the same loop keeps destroying the
        guest, one attempt per poll, until it is gone or STOP_ATTEMPTS_WATCH attempts have failed."""
        while not self.watch_stop.wait(self.poll_seconds):
            if self.space_tripped.is_set():
                if self.space_stopped:
                    return
                if self.stop_attempts >= STOP_ATTEMPTS_WATCH:
                    self.report["space_watchdog"]["stop_gave_up"] = True
                    return
                self._stop_for_space(1, STOP_ATTEMPTS_WATCH)
                continue
            self._polled()          # stamped as the reading starts: the gap that matters is reading to reading
            try:
                self.watchdog.check_once()      # a trip calls _space_trip, whose first attempt is this poll's
            except BaseException as error:
                self._space_trip("space-watch-failed", error)

    def _space_trip(self, reason, error=None, *, background=False):
        """Record the first trip; an in-flight command's guard never waits on libvirt."""
        with self.space_lock:
            first = not self.space_tripped.is_set()
            if first:
                watchdog = self.report["space_watchdog"]
                watchdog["trip"] = space_trip_code(reason)
                watchdog["trip_reading"] = dict(self.sensor.last) if self.sensor.last else None
                if error is not None:
                    watchdog["watch_error"] = (str(error) if isinstance(error, OwnedError) else type(error).__name__)
                self.tripped_at = self.clock()
            self.space_tripped.set()
        if background:
            if first:
                thread = threading.Thread(target=self._stop_for_space,
                                          args=(1, STOP_ATTEMPTS_WATCH), daemon=True)
                self.trip_stop_thread = thread
                try:
                    thread.start()
                except Exception as error:
                    self.report["space_watchdog"]["stop_dispatch_error_class"] = type(error).__name__
            return
        self._stop_for_space(1, STOP_ATTEMPTS_WATCH)

    def _freeze_guest(self):
        """Request SIGSTOP on the verified owned process before trying libvirt destruction.

        A successful send is not an observed stop of every QEMU thread. Admission
        reserves full disk growth and does not rely on this optional request.
        Called only while stop_lock is held, including against final cleanup.
        """
        freezer = self.freezer
        if freezer is None or self.freeze_requested:
            return
        record = self.report["space_watchdog"]
        try:
            freezer.stop()
            record["freeze_status"] = "sent"
        except ProcessLookupError:
            record["freeze_status"] = "already-exited"
        except BaseException as error:
            record["freeze_status"] = "failed"
            record["freeze_error_class"] = type(error).__name__
            return
        self.freeze_requested = True
        record["freeze_seconds"] = None if self.tripped_at is None else round(self.clock() - self.tripped_at, 3)

    def _stop_for_space(self, limit, cap):
        """One coordinated destroyer; never wait for another caller's provider operation."""
        if not self.stop_lock.acquire(blocking=False):
            return self.space_stopped
        try:
            return self._stop_for_space_locked(limit, cap)
        finally:
            self.stop_lock.release()

    def _stop_for_space_locked(self, limit, cap):
        """Attempt-count bound, not a libvirt latency guarantee; caller owns stop_lock."""
        for turn in range(limit):
            if self.space_stopped:
                return True
            if not self.created or self.closed or self.stop_attempts >= cap:
                return False
            self._freeze_guest()
            if turn:
                self.sleep(STOP_RETRY_SECONDS)
            self.stop_attempts += 1
            self.force_used = True
            self.report["space_watchdog"]["stop_attempts"] = self.stop_attempts
            began = self.clock()
            row = {"attempt": self.stop_attempts,
                   "started_seconds_after_trip": (None if self.tripped_at is None else
                                                  round(began - self.tripped_at, 3))}
            try:
                self.provider.destroy(self.layout.domain_name)
                gone = self.provider.domain_state(self.layout.domain_name) == "undefined"
                row["outcome"] = "gone" if gone else "still-present"
            except BaseException as error:
                gone = False
                row.update(outcome="failed", error_class=type(error).__name__)
            finished = self.clock()
            row["seconds"] = round(finished - began, 3)
            log = self.report["space_watchdog"]
            log["stop_attempts"] = self.stop_attempts
            if len(log["stop_log"]) < STOP_LOG_ENTRIES:
                log["stop_log"].append(row)
            if gone:
                self.force_used = True
                self.space_stopped = True
                log["stop_seconds"] = None if self.tripped_at is None else round(finished - self.tripped_at, 3)
                return True
        return False

    def space_guard(self):
        """Check reader health between steps and during an in-flight guest command.

        A dead or stale reader records the trip and dispatches an independent stop
        attempt. The caller raises promptly even if the sensor or libvirt is hung.
        """
        if not self.space_tripped.is_set():
            thread = self.watch_thread
            if thread is None or self.watch_stop.is_set() or self.last_poll is None:
                return
            alive = thread.is_alive()
            if alive and self.clock() - self.last_poll <= STALE_SECONDS:
                return
            self._space_trip("space-watch-stalled" if alive else "space-watch-failed", background=True)
        raise OwnedError("owned-space-watchdog-tripped")

    def _unshared(self, path):
        value = self.modules["wg_files"].exclusive_bytes(path)
        return value if type(value) is int and value >= 0 else None

    def _copy_growth(self):
        """The run's own disk copy once the guest has stopped: its unshared bytes (the frozen exclusive_bytes,
        the table's unshared column), beside the disk term admission counted."""
        result = {"measured": False, "reason": None, "admitted_disk_bytes": (self.growth or {}).get("disk_bytes"),
                  "apparent_bytes": None, "allocated_bytes": None, "unshared_bytes": None,
                  "run_folder_unshared_bytes": None, "within_admitted_disk_term": None}
        try:
            info = self.layout.disk.lstat()
        except FileNotFoundError:
            return dict(result, reason="no-copy")
        except OSError:
            return dict(result, reason="copy-unreadable")
        if not stat.S_ISREG(info.st_mode):
            return dict(result, reason="copy-not-regular")
        result.update(apparent_bytes=info.st_size, allocated_bytes=info.st_blocks * 512)
        unshared = self._unshared(self.layout.disk)
        if unshared is None:
            return dict(result, reason="extent-map-unavailable")
        folder = 0
        for parent, _, names in os.walk(self.layout.run_dir, followlinks=False):
            for name in names:
                path = Path(parent) / name
                value = self._unshared(path) if stat.S_ISREG(path.lstat().st_mode) else 0
                folder = None if folder is None or value is None else folder + value
        admitted = result["admitted_disk_bytes"]
        result.update(measured=True, unshared_bytes=unshared, run_folder_unshared_bytes=folder,
                      within_admitted_disk_term=type(admitted) is int and unshared <= admitted)
        return result

    def start(self, deadline):
        self.prepare()
        # No guest starts unless the armed watchdog is still watching and has not tripped.
        require(not self.space_tripped.is_set(), "owned-space-watchdog-tripped")
        require(self.watch_thread is not None and self.watch_thread.is_alive(), "owned-space-watchdog-unarmed")
        self.create_attempted = True
        self.report["create_attempted"] = True
        self.provider.create(self.xml)  # reviewed createXML + AUTODESTROY, never persistent start
        self.created = True
        self.report["created"] = True
        if self.space_tripped.is_set():     # a trip while create ran saw no domain yet: stop it now
            self._stop_for_space(STOP_ATTEMPTS_FINAL, STOP_ATTEMPTS_WATCH + STOP_ATTEMPTS_FINAL)
            raise OwnedError("owned-space-watchdog-stopped-guest" if self.space_stopped
                             else "owned-space-watchdog-tripped")
        machine = self.modules["wg_machine"]
        pid = machine.find_qemu_pid(self.layout.domain_name)
        arguments = cmdline(pid)
        require(self.uuid.encode() in arguments.split(b"\0"), "owned-qemu-uuid")
        require(not machine.check_qemu_cmdline(
                    arguments,
                    self.layout, self.base, self.pins, True, (), None, relay_forwards=()),
                "owned-qemu-identity")
        require(not machine.check_open_files(machine.open_targets(pid), self.base),
                "owned-qemu-opened-base")
        tpm = machine.find_swtpm_pid(self.layout)
        require(not machine.check_swtpm_cmdline(cmdline(tpm),
                                               self.layout, self.base), "owned-tpm-identity")
        require(not machine.check_open_files(machine.open_targets(tpm), self.base),
                "owned-tpm-opened-base")
        require(cmdline(pid) == arguments, "owned-qemu-changed-before-priority")
        try:
            self.freezer = self.freezer_factory(pid, self.uuid)
            self.report["space_watchdog"]["freeze_ready"] = True
        except Exception:
            self.freezer = None     # a trip then destroys through libvirt alone; the receipt says so
        machine.lower_priority(pid, 10)
        require(self.clock() < deadline, "owned-machine-start-deadline")

    def stop(self, seconds):
        """Coordinate final cleanup with the watchdog; an in-flight stop is not verified cleanup.

        seconds bounds polite-shutdown polling, not synchronous provider latency.
        Do not issue another destroy, state query or AUTODESTROY close while a
        watchdog call still owns the provider stop path.
        """
        self.watch_stop.set()
        if not self.stop_lock.acquire(blocking=False):
            self.report.update(cleanup_verified=False, cleanup_error="owned-cleanup-unverified",
                               cleanup_failure_code="owned-stop-in-flight", connection_close_deferred=True,
                               base_guard_after_stop="not-run-stop-in-flight", forced=self.force_used)
            self._record_space_state()
            return False
        try:
            return self._stop_locked(seconds)
        finally:
            self.stop_lock.release()

    def _record_space_state(self):
        self.report["space_watchdog"].update(
            tripped=self.space_tripped.is_set(), stopped_guest=self.space_stopped,
            minimum_free_bytes=getattr(self.watchdog, "minimum_free", None), polls=self.polls,
            max_poll_gap_seconds=round(self.max_gap, 3), max_unaccounted_bytes=self.sensor.maximum_unaccounted)
        if self.space_tripped.is_set():
            self.report["failure_cause"] = ("owned-space-watchdog-stopped-guest" if self.space_stopped
                                            else "owned-space-watchdog-tripped")

    def _stop_locked(self, seconds):
        good = True
        end = self.clock() + seconds
        try:
            if self.created:
                tripped = self.space_tripped.is_set()
                if tripped:     # a guest that filled root gets no polite shutdown: destroy it until it is gone
                    self._stop_for_space_locked(STOP_ATTEMPTS_FINAL, STOP_ATTEMPTS_WATCH + STOP_ATTEMPTS_FINAL)
                if self.provider.domain_state(self.layout.domain_name) != "undefined" and not tripped:
                    self.provider.shutdown(self.layout.domain_name)
                while (self.provider.domain_state(self.layout.domain_name) != "undefined" and not tripped and
                       self.clock() < end):
                    self.sleep(min(1, max(0, end - self.clock())))
                if self.provider.domain_state(self.layout.domain_name) != "undefined":
                    self.force_used = True
                    self.provider.destroy(self.layout.domain_name)
                    good = False
                require(self.provider.domain_state(self.layout.domain_name) == "undefined",
                        "owned-domain-stop-unverified")
            if self.run_recorded and not (self.create_attempted and not self.created):
                try:    # the record never decides cleanup
                    self.report["disk_growth"] = self._copy_growth()
                except Exception:
                    self.report["disk_growth"] = {"measured": False, "reason": "measurement-failed"}
            if self.create_attempted and not self.created:
                # An ambiguous create never grants shutdown/destroy ownership.
                # Closing this connection is its owned AUTODESTROY fallback, with no latency bound claimed.
                good = False
            if self.connected and self.run_recorded:
                self.report["base_guard_after_stop"] = "failed"
                self._guard("fixture-after-owned-stop")
                self.report["base_guard_after_stop"] = "verified"
        except BaseException as error:
            good = False
            self.report["cleanup_error"] = "owned-cleanup-unverified"
            self.report["cleanup_failure_code"] = (str(error) if isinstance(error, OwnedError)
                                                    else "owned-cleanup-operation-failed")
            self.report["cleanup_exception_class"] = type(error).__name__
        finally:
            self.watch_stop.set()
            if self.freezer is not None:
                try:
                    self.freezer.close()
                except BaseException:
                    pass
            if self.watch_thread is not None:
                if self.watch_thread.ident is not None:     # a thread that never started cannot be joined
                    self.watch_thread.join(timeout=3)
                good = good and not self.watch_thread.is_alive()
            self._record_space_state()
            if self.connect_attempted:
                # Last-resort AUTODESTROY on this connection can affect only its
                # transient child. It is never a successful cleanup substitute.
                try:
                    self.provider.close()
                    self.closed = True
                    self.report["connection_closed"] = True
                    self.report["connection_close_deferred"] = False
                except BaseException:
                    good = False
                    self.report["connection_close_error"] = "owned-connection-close-unverified"
        self.report["forced"] = self.force_used
        self.report["cleanup_verified"] = good      # the guest gone, the base unchanged, the connection closed
        return good and not self.force_used and not self.space_tripped.is_set()
