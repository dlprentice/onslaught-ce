#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare or execute a separately reviewed, offline synthetic Windows fixture gate.

Preparation never starts a VM. Execution accepts a sealed candidate and an explicit
independent-review receipt. No inherited runner, restore, game or provider is used.
"""
from __future__ import annotations

import argparse
import contextlib
import csv
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import selectors
import shutil
import stat
import struct
import subprocess
import tarfile
import time
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import ice_windows_owned_backend as owned_backend

DOMAIN = "onslaught-ce-win11"
ROLES = ("closed", "grant", "directory", "protocol")
SHA = re.compile(r"[0-9a-f]{64}\Z")
COMMIT = re.compile(r"[0-9a-f]{40}\Z")
NAME = re.compile(r"[a-z0-9][a-z0-9_.-]{0,95}\Z")
MAX_FILE = 32 * 1024 * 1024
FAILED_OUTPUT_BYTES = 8192
GUEST_FAILURE_RECORDS = 16      # guest command failures kept one by one in the result and the receipts folder; a last row counts the rest
GUEST_FAILURE_BYTES = 8192      # of each output stream, as the acknowledgement steps keep
STEP_LABEL = re.compile(r"[a-z][a-z0-9-]{0,47}\Z")
PREPARED_LINKS = 6
SYSTEM_DLLS = frozenset(("advapi32.dll", "bcrypt.dll", "crypt32.dll", "gdi32.dll", "imm32.dll", "kernel32.dll",
    "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "secur32.dll", "setupapi.dll", "shell32.dll",
    "shlwapi.dll", "user32.dll", "ucrtbase.dll", "version.dll", "winmm.dll", "winspool.drv", "ws2_32.dll"))


class GateError(RuntimeError):
    """The public message is always a fixed code, never tool output or a path."""


class BoundedCommandFailure(GateError):
    def __init__(self, code, stdout, stderr, reason):
        super().__init__("bounded-command-failed")
        self.code, self.stdout, self.stderr, self.reason = code, stdout, stderr, reason


def require(condition: bool, code: str) -> None:
    if not condition:
        raise GateError(code)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def plain_file(path: Path, maximum: int = MAX_FILE) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        fd = os.open(path, flags)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and 0 <= info.st_size <= maximum, "unsafe-input-file")
            data = stream.read(maximum + 1)
            require(len(data) == info.st_size and len(data) <= maximum, "input-size-changed")
            return data
    except OSError:
        raise GateError("input-file-unavailable") from None


def write_new(path: Path, data: bytes) -> None:
    with path.open("xb") as stream:
        os.chmod(path, 0o600)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def read_json(data: bytes) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate-json-field")
            result[key] = value
        return result
    value = json.loads(data, object_pairs_hook=unique)
    require(isinstance(value, dict), "json-object-required")
    return value


def relative(value: str) -> bool:
    parts = PurePosixPath(value).parts
    return bool(parts) and not value.startswith("/") and "\\" not in value and len(value) <= 240 and \
        all(NAME.fullmatch(part) and part not in (".", "..") and not part.endswith(".") for part in parts) and \
        "/".join(parts) == value


def execution_paths(value: dict) -> dict:
    require(set(value) == {"heavy_lock", "gpu_lock", "vm_home", "coordination_log"}, "execution-path-roster")
    require(all(isinstance(p, str) and Path(p).is_absolute() and str(Path(p)) == p and ".." not in Path(p).parts
        for p in value.values()), "execution-path-format")
    require(Path(value["heavy_lock"]).name == "onslaught-heavy.lock" and Path(value["gpu_lock"]).name == "godot-gpu.lock" and
        Path(value["vm_home"]).name == "windows-vm" and Path(value["coordination_log"]).name == "COORDINATION.md", "execution-path-purpose")
    require(len(set(value.values())) == 4, "execution-path-alias")
    return dict(value)


def _reap_owned_command(process):
    """Make both bounded stop attempts on this exact Popen child, even after errors."""
    try:
        process.terminate()
    except BaseException:
        pass
    try:
        process.wait(timeout=2)
    except BaseException:
        try:
            process.kill()
        except BaseException:
            pass
        try:
            process.wait(timeout=2)
        except BaseException:
            raise GateError("command cleanup unverified") from None
    require(process.returncode is not None, "command cleanup unverified")


def bounded_command(argv: list[str], *, timeout: float = 15, data: bytes | None = None,
                    limit: int = 131072, cancel=None) -> tuple[int, bytes, bytes]:
    """Bound transport output and kill only the exact child this call created. `cancel`, when given, is asked every
    tenth of a second and ends the call (command-cancelled) as soon as it answers true."""
    require(all(isinstance(x, str) and "\0" not in x for x in argv), "invalid-command")
    environment = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "SSH_ASKPASS", "SSH_AUTH_SOCK"):
        environment.pop(key, None)
    environment.update(LC_ALL="C", BROWSER="/bin/true", BEA_REPORT_NO_OPEN="1")
    output = [bytearray(), bytearray()]
    pending = memoryview(data or b"")
    deadline = time.monotonic() + timeout
    selector = None
    failure = None
    process = subprocess.Popen(argv, stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
    try:
        selector = selectors.DefaultSelector()
        assert process.stdout is not None and process.stderr is not None
        selector.register(process.stdout, selectors.EVENT_READ, 0)
        selector.register(process.stderr, selectors.EVENT_READ, 1)
        if process.stdin is not None:
            os.set_blocking(process.stdin.fileno(), False)
            selector.register(process.stdin, selectors.EVENT_WRITE, 2)
        while selector.get_map():
            require(time.monotonic() < deadline, "command-deadline")
            require(cancel is None or not cancel(), "command-cancelled")
            for key, _ in selector.select(min(0.1, max(0, deadline - time.monotonic()))):
                if key.data == 2:
                    if pending:
                        try:
                            count = os.write(key.fd, pending[:65536])
                        except BrokenPipeError:
                            count = len(pending)
                        pending = pending[count:]
                    if not pending:
                        selector.unregister(key.fileobj)
                        key.fileobj.close()
                else:
                    chunk = os.read(key.fd, 8192)
                    if chunk:
                        require(sum(map(len, output)) + len(chunk) <= limit, "command-output-bound")
                        output[key.data].extend(chunk)
                    else:
                        selector.unregister(key.fileobj)
            if process.poll() is not None and not selector.get_map():
                break
        return process.wait(timeout=max(0.01, deadline - time.monotonic())), bytes(output[0]), bytes(output[1])
    except BaseException as exc:
        failure = exc
        try:
            _reap_owned_command(process)
        except GateError:
            failure = BoundedCommandFailure(process.returncode, bytes(output[0]),
                                            bytes(output[1]), "command cleanup unverified")
            raise failure from exc
        if isinstance(exc, (GateError, subprocess.TimeoutExpired)):
            reason = str(exc) if isinstance(exc, GateError) else 'process-timeout'
            failure = BoundedCommandFailure(process.returncode, bytes(output[0]),
                                            bytes(output[1]), reason)
            raise failure from None
        raise
    finally:
        close_failed = False
        for resource in (selector, process.stdin, process.stdout, process.stderr):
            if resource is not None:
                try:
                    resource.close()
                except BaseException:
                    close_failed = True
        if close_failed:
            if failure is None:
                raise GateError("command pipe cleanup failed") from None
            failure.pipe_cleanup_failed = True


def pe_imports(data: bytes) -> tuple[int, set[str]]:
    """Small bounded PE32 import inventory; no executable is run during preparation."""
    def word(offset: int, form: str) -> int:
        require(0 <= offset <= len(data) - struct.calcsize(form), "invalid-pe-bounds")
        return struct.unpack_from(form, data, offset)[0]
    require(data[:2] == b"MZ" and len(data) >= 64, "not-a-pe-file")
    pe = word(60, "<I")
    require(data[pe:pe + 4] == b"PE\0\0" and word(pe + 4, "<H") == 0x14C, "not-selected-x86-abi")
    sections, optional_size = word(pe + 6, "<H"), word(pe + 20, "<H")
    optional = pe + 24
    require(word(optional, "<H") == 0x10B and 1 <= sections <= 96 and optional_size >= 224 and
        word(optional + 92, "<I") >= 14 and word(optional + 200, "<I") == 0, "invalid-or-delay-import-pe32")
    subsystem = word(optional + 68, "<H")
    table = optional + optional_size
    def offset(rva: int) -> int:
        for index in range(sections):
            section = table + index * 40
            virtual_size, address, size, raw = (word(section + n, "<I") for n in (8, 12, 16, 20))
            if address <= rva < address + max(virtual_size, size):
                require(rva - address < size, "pe-import-unbacked")
                return raw + rva - address
        raise GateError("pe-import-rva")
    imports: set[str] = set()
    rva = word(optional + 104, "<I")
    if rva:
        entry = offset(rva)
        for index in range(256):
            row = entry + index * 20
            fields = [word(row + n * 4, "<I") for n in range(5)]
            if not any(fields):
                break
            begin = offset(fields[3]); end = data.find(b"\0", begin, begin + 128)
            require(end >= begin, "pe-import-name")
            try:
                name = data[begin:end].decode("ascii").lower()
            except UnicodeError:
                raise GateError("pe-import-name") from None
            require(NAME.fullmatch(name) is not None, "pe-import-name")
            imports.add(name)
        else:
            raise GateError("pe-import-count")
    return subsystem, imports


def pinned(spec: dict) -> bytes:
    require(set(spec) == {"path", "sha256"} and SHA.fullmatch(spec["sha256"]) is not None, "invalid-pin")
    value = plain_file(Path(spec["path"]))
    require(digest(value) == spec["sha256"], "pin-mismatch")
    return value


def source_binding(spec: dict) -> dict:
    require(set(spec) == {"repository", "commit", "files", "build_receipt"} and COMMIT.fullmatch(spec["commit"]) is not None,
        "invalid-source-binding")
    require(isinstance(spec["files"], dict) and 1 <= len(spec["files"]) <= 64, "invalid-source-roster")
    for path, expected in spec["files"].items():
        require(relative(path) and path.startswith("port/") and SHA.fullmatch(expected) is not None, "invalid-source-pin")
        code, content, _ = bounded_command(["git", "-C", spec["repository"], "show", spec["commit"] + ":" + path], limit=1024 * 1024)
        require(code == 0 and digest(content) == expected, "frozen-source-mismatch")
    pinned(spec["build_receipt"])
    return {"commit": spec["commit"], "files": spec["files"], "build_receipt_sha256": spec["build_receipt"]["sha256"]}



def toolchain_binding(spec: dict) -> dict:
    value = pinned(spec)
    receipt = read_json(value)
    require(receipt.get("schema") == 1 and receipt.get("kind") == "archive-installation-identity" and
            receipt.get("verified") is True and receipt.get("mismatch_count") == 0 and receipt.get("extra_count") == 0,
            "toolchain-not-verified")
    manifest = read_json(pinned(receipt["manifest"]))
    require(manifest["archive_sha256"] == receipt["archive"]["sha256"] and
            not manifest["mismatches"] and not manifest["extras"] and
            len(manifest["entries"]) == receipt["entry_count"] > 0 and
            len(manifest["roots"]) == 2 and manifest["roots"] == receipt["roots"] and
            all(len(row["installations"]) == 2 and all(item["matches_archive"] is True for item in row["installations"])
                for row in manifest["entries"]), "toolchain-manifest-mismatch")
    require(SHA.fullmatch(receipt["archive"]["sha256"]) is not None and
            isinstance(receipt["version"], str) and NAME.fullmatch(receipt["version"]) is not None,
            "toolchain-identity")
    pinned(receipt["verifier"])
    return {"receipt_sha256": digest(value), "manifest_sha256": receipt["manifest"]["sha256"],
            "archive_sha256": receipt["archive"]["sha256"], "version": receipt["version"]}


def grant_progress(value: list) -> list:
    require(isinstance(value, list) and len(value) == 4 and len(set(value)) == 4 and
            all(isinstance(line, str) and 1 <= len(line) <= 160 and
                all(32 <= ord(char) <= 126 for char in line) and
                not any(word in line for word in ("PASS", "FAIL", "UNVERIFIED", "UNEXERCISED")) for line in value),
            "grant-progress-lines")
    return list(value)


def task_xml(root: str, run_id: str, manifest_hash: str, sid: str = "SID-TO-BIND") -> bytes:
    require(re.fullmatch(r"S-1-5-(?:[0-9]+-)*[0-9]+", sid) is not None or sid == "SID-TO-BIND", "invalid-guest-sid")
    return ("<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        '<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">'
        '<RegistrationInfo><Description>Owned synthetic private-file fixtures</Description></RegistrationInfo>'
        '<Principals><Principal id="owner"><UserId>' + escape(sid) + '</UserId><LogonType>InteractiveToken</LogonType>'
        '<RunLevel>LeastPrivilege</RunLevel></Principal></Principals><Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>'
        '<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>'
        '<AllowHardTerminate>true</AllowHardTerminate><Hidden>true</Hidden><ExecutionTimeLimit>PT7M</ExecutionTimeLimit></Settings>'
        '<Actions Context="owner"><Exec><Command>' + escape(root + "\\ice-native-supervisor.exe") + '</Command><Arguments>' +
        escape('--run "' + root + '" ' + manifest_hash) + '</Arguments><WorkingDirectory>' + escape(root) +
        '</WorkingDirectory></Exec></Actions></Task>\n').encode("utf-16")



def backend_binding(spec, vm_home):
    try:
        return owned_backend.binding(spec, vm_home)
    except Exception:
        raise GateError("owned-backend-binding-refused") from None


def ack_command(root, run_id):
    return ('cmd.exe /d /c "if exist ' + root + '\\ack.txt (exit /b 75) else (if exist ' + root +
        '\\ack.tmp (exit /b 75) else ((echo ' + run_id + ')>' + root + '\\ack.tmp && ren ' + root + '\\ack.tmp ack.txt))"')


def owned_ack_command(root, run_id, sid):
    require(isinstance(sid, str) and re.fullmatch(r"S-1-5-(?:[0-9]+-)*[0-9]+", sid) is not None, "ack-owner-identity")
    command = ack_command(root, run_id)
    rename = " && ren " + root + "\\ack.tmp ack.txt"
    require(command.count(rename) == 1, "ack-publisher-contract")
    owner = ' && icacls "' + root + '\\ack.tmp" /setowner "*' + sid + '" /q'
    return command.replace(rename, owner + rename, 1)


def ack_classification(code, stdout, stderr):
    value = (stdout + stderr).lower()
    if code == 0: return "success"
    if code == 75: return "collision"
    if code == 76: return "absent"
    if code == 255: return "ssh-transport-failed"
    if b"used by another process" in value: return "sharing-violation"
    if b"access is denied" in value or b"permission denied" in value: return "access-denied"
    if b"syntax" in value or b"was unexpected" in value: return "command-syntax"
    if b"cannot find" in value or b"not found" in value: return "path-not-found"
    return "guest-command-failed"

def prepare(selection: dict, out: Path) -> dict:
    required = {'schema', 'run_id', 'candidate', 'runner', 'toolchain_identity', 'supervisor', 'cases', 'grant_progress_lines', 'forbidden_output_tokens', 'inactive_domain_sha256', 'guest_user', 'execution_paths', 'owned_backend'}
    require(set(selection) == required and selection["schema"] == 1, "selection-schema")
    run_id = selection["run_id"]
    require(NAME.fullmatch(run_id) is not None and run_id.startswith("ice-" + selection["candidate"]["commit"][:12] + "-"), "run-id")
    require(SHA.fullmatch(selection["inactive_domain_sha256"]) is not None, "domain-pin")
    require(re.fullmatch(r"[A-Za-z0-9_.-]{1,32}", selection["guest_user"]) is not None, "guest-user")
    paths = execution_paths(selection["execution_paths"])
    require(not out.exists() and not out.is_symlink(), "prepared-root-exists")
    candidate = source_binding(selection["candidate"]); runner = source_binding(selection["runner"])
    toolchain = toolchain_binding(selection["toolchain_identity"])
    progress = grant_progress(selection["grant_progress_lines"])
    files: dict[str, bytes] = {"ice-native-supervisor.exe": pinned(selection["supervisor"])}
    require(pe_imports(files["ice-native-supervisor.exe"])[0] == 2, "supervisor-must-be-hidden-gui-subsystem")
    require([case.get("name") for case in selection["cases"]] == list(ROLES), "case-roster")
    cases = []
    for case in selection["cases"]:
        require(set(case) == {"name", "exe", "dlls", "argument_kind"} and case["argument_kind"] == "root", "case-schema")
        prefix = "package/" + case["name"] + "/"
        for pin in [case["exe"], *case["dlls"]]:
            name = Path(pin["path"]).name.lower()
            require(NAME.fullmatch(name) is not None and name.endswith((".exe", ".dll")), "artifact-name")
            path = prefix + name; require(path not in files, "duplicate-artifact")
            files[path] = pinned(pin)
        cases.append({"name": case["name"], "path": prefix + Path(case["exe"]["path"]).name.lower(), "kind": case["argument_kind"]})
    for name, content in files.items():
        _, imports = pe_imports(content)
        directory = str(PurePosixPath(name).parent)
        for imported in imports:
            system = imported in SYSTEM_DLLS or imported.startswith(("api-ms-win-", "ext-ms-win-"))
            local = imported if directory == "." else directory + "/" + imported
            require(system or local in files, "runtime-dll-closure-missing")
    require(sum(map(len, files.values())) <= 128 * 1024 * 1024, "payload-bound")
    tokens = selection["forbidden_output_tokens"]
    require(isinstance(tokens, list) and 1 <= len(tokens) <= 32 and len(set(tokens)) == len(tokens) and
        all(isinstance(x, str) and 8 <= len(x) <= 128 and all(32 <= ord(c) <= 126 for c in x) for x in tokens), "fabricated-log-sentinels")
    lines = ["CE-ICE-NATIVE-GATE\t1", "run\t" + run_id, "source\t" + candidate["commit"]]
    lines += ["file\t%s\t%s\t%d" % (name, digest(content), len(content)) for name, content in sorted(files.items())]
    lines += ["case\t{name}\t{path}\t{kind}".format(**case) for case in cases]
    lines += ["forbid\t" + token for token in tokens]
    lines += ["progress\tgrant\t" + line for line in progress]
    manifest = ("\n".join(lines) + "\n").encode("ascii")
    native_hash = digest(manifest); guest_root = "C:\\ce-native-" + run_id
    files["gate-plan.txt"] = manifest
    template = task_xml(guest_root, run_id, native_hash)
    metadata = {"schema": 1, "run_id": run_id, "candidate": candidate, "runner": runner,
        "toolchain": toolchain, "grant_progress_lines": progress, "guest_user": selection["guest_user"],
        "execution_paths": paths,
        "inactive_domain_sha256": selection["inactive_domain_sha256"], "native_manifest_sha256": native_hash,
        "task_template_sha256": digest(template), "files": {name: {"sha256": digest(value), "size": len(value)} for name, value in files.items()}}
    metadata["owned_backend"] = backend_binding(selection.get("owned_backend"), Path(selection["execution_paths"]["vm_home"]))
    out.mkdir(mode=0o700, parents=False)
    package = out / "payload"; package.mkdir(mode=0o700)
    for name, value in files.items():
        path = package / name; path.parent.mkdir(parents=True, exist_ok=True, mode=0o700); write_new(path, value)
    write_new(out / "task-template.xml", template)
    write_new(out / "selection.private.json", json_bytes(selection))
    write_new(out / "sealed.json", json_bytes(metadata))
    return metadata


def verify_prepared(root: Path) -> dict:
    metadata = read_json(plain_file(root / "sealed.json", 1024 * 1024))
    require(metadata["schema"] == 1 and NAME.fullmatch(metadata["run_id"]) is not None, "sealed-schema")
    files = metadata["files"]
    found = {p.relative_to(root / "payload").as_posix() for p in (root / "payload").rglob("*") if p.is_file() or p.is_symlink()}
    require(found == set(files), "sealed-payload-roster")
    for name, pin in files.items():
        require(relative(name), "sealed-path")
        value = plain_file(root / "payload" / name)
        require(len(value) == pin["size"] and digest(value) == pin["sha256"], "sealed-payload-changed")
    require(digest(plain_file(root / "task-template.xml")) == metadata["task_template_sha256"], "task-template-changed")
    selection = read_json(plain_file(root / "selection.private.json", 1024 * 1024))
    require(execution_paths(selection["execution_paths"]) == metadata["execution_paths"], "execution-path-binding-changed")
    require(source_binding(selection["candidate"]) == metadata["candidate"] and source_binding(selection["runner"]) == metadata["runner"], "source-binding-changed")
    require(toolchain_binding(selection["toolchain_identity"]) == metadata["toolchain"], "toolchain-binding-changed")
    require(grant_progress(selection["grant_progress_lines"]) == metadata["grant_progress_lines"], "grant-progress-binding-changed")
    require(metadata.get("owned_backend") == backend_binding(selection.get("owned_backend"), Path(metadata["execution_paths"]["vm_home"])), "owned-backend-seal-drift")
    return metadata


def require_sealed_runner_modules(metadata: dict) -> None:
    """This process runs the gate and the owned adapter from this directory, so both must be the sealed runner's
    bytes. verify_prepared checks the runner pins against the runner commit, not the files imported here; without
    this a packet sealed against an older adapter would run a newer one."""
    pins = metadata["runner"]["files"]
    require(isinstance(pins, dict), "runner-module-drift")
    for module in (Path(__file__), Path(owned_backend.__file__)):
        require(pins.get("port/tools/" + module.name) == digest(plain_file(module)), "runner-module-drift")


def validate_ready(ready: dict, metadata: dict) -> None:
    require(set(ready) == {"schema", "run", "manifest_sha256", "standard_user", "windows_build"} and
            type(ready["schema"]) is int and ready["schema"] == 1 and ready["run"] == metadata["run_id"] and
            ready["manifest_sha256"] == metadata["native_manifest_sha256"] and ready["standard_user"] is True and
            type(ready["windows_build"]) is int and ready["windows_build"] >= 22000, "ready-mismatch")


def validate_native_receipt(done: dict, metadata: dict) -> None:
    require(set(done) == {"schema", "run", "source_commit", "manifest_sha256", "standard_user", "failure", "inputs_unchanged", "ack_control", "cases"}, "done-schema")
    require(type(done["schema"]) is int and done["schema"] == 1 and done["run"] == metadata["run_id"] and
            done["source_commit"] == metadata["candidate"]["commit"] and
            done["manifest_sha256"] == metadata["native_manifest_sha256"], "done-identity")
    require(type(done["standard_user"]) is bool and type(done["inputs_unchanged"]) is bool and
            type(done["failure"]) is int and 0 <= done["failure"] <= 65535, "done-status")
    require(isinstance(done["ack_control"], list) and len(done["ack_control"]) <= 2, "ack-control-bound")
    for index, control in enumerate(done["ack_control"]):
        require(isinstance(control, dict) and set(control) == {"variant", "stage", "wait", "exit", "accounting"} and
                control["variant"] == ("old", "new")[index] and
                control["stage"] in ("launch", "wait", "exit-query", "accounting-query", "active-processes", "complete"),
                "ack-control-schema")
        wait = control["wait"]
        require(wait is None or isinstance(wait, dict) and set(wait) == {"result", "error", "elapsed_ms"} and
                all(type(wait[key]) is int and 0 <= wait[key] <= 0xffffffff for key in ("result", "error")) and
                type(wait["elapsed_ms"]) is int and 0 <= wait["elapsed_ms"] <= 0xffffffffffffffff, "ack-control-wait")
        for key, field in (("exit", "code"), ("accounting", "active_processes")):
            value = control[key]
            extra = {"first_active_processes", "queries", "elapsed_ms"} if key == "accounting" else set()
            require(value is None or isinstance(value, dict) and set(value) == {"ok", field, "error"} | extra and
                    type(value["ok"]) is bool and type(value["error"]) is int and 0 <= value["error"] <= 0xffffffff and
                    ((value["ok"] and value["error"] == 0 and type(value[field]) is int and 0 <= value[field] <= 0xffffffff) or
                     (not value["ok"] and value[field] is None)), "ack-control-" + key)
            if value is not None and key == "accounting":
                require((value["first_active_processes"] is None or type(value["first_active_processes"]) is int and
                         0 <= value["first_active_processes"] <= 0xffffffff) and
                        type(value["queries"]) is int and 1 <= value["queries"] <= 0xffffffff and
                        type(value["elapsed_ms"]) is int and 0 <= value["elapsed_ms"] <= 0xffffffffffffffff,
                        "ack-control-accounting-bound")
    require(isinstance(done["cases"], list) and len(done["cases"]) <= len(ROLES) and
            all(isinstance(case, dict) for case in done["cases"]) and
            [case.get("name") for case in done["cases"]] == list(ROLES)[:len(done["cases"])], "native-case-roster")
    identities = set()
    for case in done["cases"]:
        require(set(case) == {"name", "pid", "creation_time", "identity_confirmed", "exit", "forced", "cleanup_confirmed",
                "infrastructure_failure", "output_complete", "no_forbidden_output", "summary_valid", "checks",
                "stdout_bytes", "stderr_bytes", "stdout_sha256", "stderr_sha256", "job_completion"}, "native-case-schema")
        job = case["job_completion"]
        require(job is None or isinstance(job, dict) and set(job) == {"query_ok", "query_error", "first_active_processes",
                "active_processes", "queries", "elapsed_ms", "deadline_ms"} and type(job["query_ok"]) is bool and
                type(job["query_error"]) is int and 0 <= job["query_error"] <= 0xffffffff and
                all(job[key] is None or type(job[key]) is int and 0 <= job[key] <= 0xffffffff
                    for key in ("first_active_processes", "active_processes")) and
                type(job["queries"]) is int and 1 <= job["queries"] <= 0xffffffff and
                type(job["elapsed_ms"]) is int and 0 <= job["elapsed_ms"] <= 0xffffffffffffffff and
                type(job["deadline_ms"]) is int and job["deadline_ms"] == 90000, "native-case-completion-schema")
        require(type(case["pid"]) is int and 0 <= case["pid"] <= 0xffffffff and
                type(case["creation_time"]) is int and 0 <= case["creation_time"] <= 0xffffffffffffffff and
                type(case["infrastructure_failure"]) is int and 0 <= case["infrastructure_failure"] <= 65535,
                "native-case-identity-type")
        require(all(type(case[key]) is bool for key in ("identity_confirmed", "cleanup_confirmed", "output_complete",
                "no_forbidden_output", "summary_valid")), "native-case-status-type")
        if case["identity_confirmed"]:
            identity = (case["pid"], case["creation_time"])
            require(case["pid"] > 0 and case["creation_time"] > 0 and identity not in identities, "native-child-identity")
            identities.add(identity)
        require(case["checks"] is None or type(case["checks"]) is int and 0 <= case["checks"] <= 10000000,
                "native-check-count")
        if case["output_complete"]:
            require(type(case["exit"]) is int and 0 <= case["exit"] <= 0xffffffff and type(case["forced"]) is bool,
                    "native-exit-status")
            for stream in ("stdout", "stderr"):
                require(type(case[stream + "_bytes"]) is int and 0 <= case[stream + "_bytes"] <= 1024 * 1024 and
                        isinstance(case[stream + "_sha256"], str) and SHA.fullmatch(case[stream + "_sha256"]) is not None,
                        "native-output-bound")
        else:
            require(case["infrastructure_failure"] > 0 and case["exit"] is None and case["forced"] is None and
                    case["checks"] is None and not case["summary_valid"] and not case["no_forbidden_output"] and
                    not case["cleanup_confirmed"] and all(case[stream + suffix] is None for stream in ("stdout", "stderr")
                    for suffix in ("_bytes", "_sha256")), "native-incomplete-case-status")


def validate_done(done: dict, metadata: dict) -> None:
    validate_native_receipt(done, metadata)
    require(done["standard_user"] is True and done["failure"] == 0 and done["inputs_unchanged"] is True,
            "native-gate-failed")
    require(len(done["cases"]) == len(ROLES), "native-case-roster")
    for case in done["cases"]:
        require(case["identity_confirmed"] and case["cleanup_confirmed"] and case["output_complete"] and
                case["infrastructure_failure"] == 0 and case["exit"] == 0 and case["forced"] is False and
                case["no_forbidden_output"] and case["summary_valid"], "native-case-failed")
        require((case["name"] == "closed" and case["checks"] is None) or
                (case["name"] != "closed" and type(case["checks"]) is int and case["checks"] > 0), "native-check-count")
        job = case["job_completion"]
        require(job is not None and job["query_ok"] and job["query_error"] == 0 and type(job["first_active_processes"]) is int and job["active_processes"] == 0 and
                job["elapsed_ms"] <= job["deadline_ms"], "native-case-completion-failed")


def failed_stream(stream: dict, size: int, sha256: str) -> tuple[bytes, dict]:
    """Decode one retained stream; size and hash must equal the sealed case receipt's."""
    require(isinstance(stream, dict) and set(stream) == {"bytes", "sha256", "retained_bytes", "hex"}, "failed-stream-schema")
    require(type(stream["bytes"]) is int and stream["bytes"] == size and stream["sha256"] == sha256, "failed-stream-identity")
    retained = min(size, FAILED_OUTPUT_BYTES)
    require(type(stream["retained_bytes"]) is int and stream["retained_bytes"] == retained, "failed-stream-bound")
    require(isinstance(stream["hex"], str) and len(stream["hex"]) == 2 * retained and
            re.fullmatch(r"[0-9a-f]*", stream["hex"]) is not None, "failed-stream-encoding")
    value = bytes.fromhex(stream["hex"])
    complete = retained == size
    require(not complete or digest(value) == sha256, "failed-stream-digest")
    return value, {"bytes": size, "sha256": sha256, "retained_bytes": retained, "retained_sha256": digest(value),
                   "complete": complete}


def validate_failed_case(value: dict, case: dict, metadata: dict) -> dict:
    require(set(value) == {"schema", "run", "manifest_sha256", "name", "stdout", "stderr"} and
            type(value["schema"]) is int and value["schema"] == 1 and value["run"] == metadata["run_id"] and
            value["manifest_sha256"] == metadata["native_manifest_sha256"] and value["name"] == case["name"], "failed-case-identity")
    return {stream: failed_stream(value[stream], case[stream + "_bytes"], case[stream + "_sha256"])
            for stream in ("stdout", "stderr")}


FIXED_REQUEST_ERRORS = {"none": "none", "relay request unavailable": "request-unavailable",
    "relay request directory is not private": "request-directory-not-private",
    "relay request security unavailable": "request-security-unavailable",
    "relay request temporary unavailable": "request-temporary-unavailable",
    "relay request temporary is not private": "request-temporary-not-private",
    "relay request write failed": "request-write-failed", "relay request verification failed": "request-verification-failed",
    "relay request already exists": "request-already-exists", "relay request publication failed": "request-publication-failed"}
REQUEST_OUTCOME = re.compile(rb"Request outcome call=([0-9]{1,6}) positive=([0-9]{1,6}) expected=([01]) actual=([01]) "
    rb"fixed-error=([a-z ]{1,48}) step=([a-z-]{1,24}) native=([01]) ntstatus=0x([0-9A-F]{8}) iostatus=0x([0-9A-F]{8}) "
    rb"lasterror=([0-9]{1,10})\r?\n")


def request_outcome(stderr: bytes) -> dict | None:
    """Typed numbers from the fixture's fixed failure line; never its raw text."""
    found = REQUEST_OUTCOME.search(stderr)
    if found is None:
        return None
    call, positive, expected, actual, error, step, native, status, io_status, last_error = found.groups()
    return {"call": int(call), "positive_call": int(positive), "expected_success": expected == b"1", "actual_success": actual == b"1",
            "fixed_error_class": FIXED_REQUEST_ERRORS.get(error.decode("ascii"), "unclassified"), "step": step.decode("ascii"),
            "native_status_returned": native == b"1", "ntstatus": int(status, 16), "io_status": int(io_status, 16),
            "last_error": int(last_error)}


def collect_failed_case(prepared: Path, metadata: dict, backend, done: dict | None) -> dict:
    """Keep one failed case's bounded output privately; result.json gets only sizes, hashes and typed numbers."""
    case = done["cases"][-1] if done and done["cases"] else None
    if case is None or not case["output_complete"] or (case["exit"] == 0 and case["forced"] is False and case["summary_valid"]):
        return {"status": "not-applicable"}
    if not case["no_forbidden_output"]:
        return {"status": "withheld-forbidden-output", "name": case["name"]}
    try:
        value = backend.read_json("failed-case", 2)
        streams = validate_failed_case(value, case, metadata)
        report = {"status": "preserved", "name": case["name"]}
        for stream, (data, identity) in streams.items():
            filename = "failed-%s.%s.private.bin" % (case["name"], stream)
            write_new(prepared / "receipts" / filename, data)
            report[stream] = {**identity, "file": filename}
        report["request_outcome"] = request_outcome(streams["stderr"][0])
        return report
    except (GateError, OSError, ValueError, KeyError, TypeError) as error:
        return {"status": "unavailable-or-rejected", "name": case["name"],
                "reason": str(error) if isinstance(error, GateError) else type(error).__name__}


def collect_failure(prepared: Path, metadata: dict, backend) -> dict:
    report = {"attempted": False, "receipts": {}, "failed_case": None, "readback": None}
    if not getattr(backend, "root_owned", False):
        return report
    report["attempted"] = True
    saved_deadline = backend.deadline
    backend.deadline = time.monotonic() + 30
    try:
        done = None
        for name, validate in (("ready", validate_ready), ("done", validate_native_receipt)):
            try:
                value = backend.read_json(name, 2)
                validate(value, metadata)
                write_new(prepared / "receipts" / ("recovered-" + name + ".json"), json_bytes(value))
                report["receipts"][name] = "preserved"
                if name == "done":
                    done = value
            except (GateError, OSError, ValueError, KeyError, TypeError):
                report["receipts"][name] = "unavailable-or-rejected"
        report["failed_case"] = collect_failed_case(prepared, metadata, backend, done)
        try:
            report["readback"] = backend.readback(metadata, best_effort=True)
            write_new(prepared / "receipts" / "failure-readback.json", json_bytes(report["readback"]))
        except (GateError, OSError, ValueError, KeyError, TypeError):
            report["readback"] = {"complete": False, "failure": "collection-unavailable"}
    finally:
        backend.deadline = saved_deadline
    return report


SPACE_TRIPPED = "owned-space-watchdog-tripped"


def space_tripped(backend) -> bool:
    """Has the owned machine's space watchdog tripped (stopped the guest, or lost sight of root's free space)?"""
    event = getattr(getattr(backend, "owned", None), "space_tripped", None)
    return event is not None and event.is_set() is True


def run_gate(prepared: Path, metadata: dict, backend) -> dict:
    """Dependency-injected only for host/guest unit tests; CLI always supplies RealBackend."""
    receipts = prepared / "receipts"
    receipts.mkdir(mode=0o700)
    attach = getattr(backend, "attach_receipts", None)
    if attach is not None:      # so that a failure before staging can keep its private output too
        attach(receipts)
    result = {"schema": 1, "run": metadata["run_id"], "pass": False, "failure": None,
              "vm_start_attempted": False, "vm_started": False, "task_created": False,
              "native": None, "failure_collection": None, "cleanup_failures": []}
    handled = (GateError, OSError, ValueError, KeyError, TypeError)
    try:
        with backend.locks():
            try:
                backend.preflight(metadata)
                result["vm_start_attempted"] = True
                backend.start_vm()
                result["vm_started"] = True
                backend.wait_guest(180)
                backend.stage(prepared, metadata, receipts)
                backend.ack_control()
                backend.create_task()
                result["task_created"] = True
                backend.start_task()
                ready = backend.read_json("ready", 30)
                validate_ready(ready, metadata)
                write_new(receipts / "ready.json", json_bytes(ready))
                backend.acknowledge()
                done = backend.read_json("done", 390)
                validate_native_receipt(done, metadata)
                result["native"] = done
                write_new(receipts / "done.json", json_bytes(done))
                validate_done(done, metadata)
                backend.wait_task_exit(10)
                readback = backend.readback(metadata)
                write_new(receipts / "readback.json", json_bytes(readback))
                result["pass"] = True
            except handled as exc:
                result["failure"] = str(exc) if isinstance(exc, GateError) else type(exc).__name__
                result["failure_collection"] = collect_failure(prepared, metadata, backend)
            finally:
                if result["task_created"]:
                    try:
                        backend.finish_task(force=not result["pass"])
                    except handled as exc:
                        if str(exc) != SPACE_TRIPPED:       # a guest stopped for space has no task left to end
                            result["cleanup_failures"].append("owned-task-cleanup")
                if result["vm_start_attempted"]:
                    try:
                        backend.stop_vm(90)
                    except handled as exc:
                        if str(exc) == SPACE_TRIPPED:       # cleanup itself was verified; the run fails on the trip
                            result["pass"] = False
                        else:
                            result["cleanup_failures"].append("owned-vm-shutdown")
    except handled as exc:
        if result["failure"] is None:
            result["failure"] = str(exc) if isinstance(exc, GateError) else type(exc).__name__
        result["pass"] = False
    finally:
        if result["cleanup_failures"]:
            result["pass"] = False
        if space_tripped(backend):
            # The cause is the trip, not the guest step that failed first when the guest was stopped.
            if result["failure"] not in (None, SPACE_TRIPPED):
                result["first_failure_seen"] = result["failure"]
            result["failure"], result["pass"] = SPACE_TRIPPED, False
        result["host_diagnostics"] = list(getattr(backend, "diagnostics", []))
        machine = getattr(backend, "owned", None)
        result["owned_machine"] = machine.report if machine is not None else None
        result["acknowledgement"] = getattr(backend, "ack_report", None)
        write_new(receipts / "result.json", json_bytes(result))
    return result


class RealBackend:
    def __init__(self, metadata: dict):
        paths = execution_paths(metadata["execution_paths"])
        self.vm_home = Path(paths["vm_home"]); self.metadata = metadata; self.coordination = Path(paths["coordination_log"])
        self.heavy_lock = Path(paths["heavy_lock"]); self.gpu_lock = Path(paths["gpu_lock"])
        self.run_id = metadata["run_id"]; self.root = "C:\\ce-native-" + self.run_id
        self.task = "CE-ICE-" + self.run_id; self.uuid = None; self.sid = None; self.staged = {}; self.task_hash = None
        self.deadline = None
        self.root_owned = False
        self.ssh = ["ssh", "-F", "/dev/null", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
            "-o", "UpdateHostKeys=no", "-o", "IdentitiesOnly=yes", "-o", "ConnectTimeout=3", "-o", "UserKnownHostsFile=" + str(self.vm_home / "ssh/known_hosts"),
            "-i", str(self.vm_home / "ssh/id_ed25519"), "-p", "2522", "-l", metadata["guest_user"], "127.0.0.1"]
        self.diagnostics = []
        self.owned = None
        self.receipts = None
        self.ack_report = {"control": None, "primary": None, "operations": []}

    def virsh(self, *args, timeout=15):
        code, out, error = bounded_command(['virsh', '-c', 'qemu:///session', *args], timeout=timeout)
        if code:
            operation = args[0] if args and args[0] in {"domstate", "dominfo", "domuuid", "dumpxml", "managedsave-dumpxml"} else "other"
            self.diagnostics.append(owned_backend.diagnostic("preflight", operation, code, error))
            self.diagnostics[:] = self.diagnostics[-16:]
        require(code == 0, "virsh-operation-failed")
        return out

    def space(self) -> None:
        """Fail the run now, with its cause, once the guest has been stopped for space or root's free space is no
        longer watched; taken before every guest step and in every wait."""
        owned = getattr(self, "owned", None)
        if owned is not None:
            try:
                owned.space_guard()
            except owned_backend.OwnedError:
                raise GateError(SPACE_TRIPPED) from None

    def guest_result(self, command: str, *, data: bytes | None = None, timeout: float = 15, limit=131072):
        self.space()
        if self.deadline is not None:
            remaining = self.deadline - time.monotonic()
            require(remaining > 0, "whole-gate-deadline")
            timeout = min(timeout, remaining)
        if getattr(self, "owned", None) is None:
            return bounded_command([*self.ssh, command], data=data, timeout=timeout, limit=limit)
        # A guest that is frozen but not yet destroyed keeps its ssh connection open: end the call at the trip.
        return bounded_command([*self.ssh, command], data=data, timeout=timeout, limit=limit,
                               cancel=self.space_cancelled)

    def space_cancelled(self) -> bool:
        """Check a hung reader too, rather than waiting for its trip event to be set."""
        try:
            self.space()
        except GateError as error:
            if str(error) == SPACE_TRIPPED:
                return True
            raise
        return space_tripped(self)

    def attach_receipts(self, receipts: Path) -> None:
        self.receipts = receipts

    def record_guest_failure(self, label: str, command: str, code, out: bytes, error: bytes, reason: str) -> None:
        """Keep why a guest command failed, in the run's records: the step's label, the exit code, the reason, the
        command's hash and bounded copies of both output streams as private files (mode 0600, beside the other
        private receipts). result.json gets each stream's size and hash and the file's name, never its text. The
        first GUEST_FAILURE_RECORDS failures are kept one by one; one last row counts the rest."""
        diagnostics = self.__dict__.setdefault("diagnostics", [])
        count = self.__dict__.get("guest_failure_count", 0) + 1
        self.guest_failure_count = count
        if count > GUEST_FAILURE_RECORDS:
            more = self.__dict__.get("guest_failure_more")
            if more is None:
                more = self.guest_failure_more = {"stage": "guest", "operation": "more-failures", "count": 0}
                diagnostics.append(more)
            more["count"] = count - GUEST_FAILURE_RECORDS
            return
        receipts = getattr(self, "receipts", None)
        row = {"stage": "guest", "operation": label, "index": count, "returncode": code, "reason": reason,
               "command_sha256": digest(command.encode("utf-8", "replace"))}
        for name, value in (("stdout", out), ("stderr", error)):
            value = bytes(value or b"")
            entry = {"bytes": len(value), "sha256": digest(value), "retained_bytes": min(len(value), GUEST_FAILURE_BYTES),
                     "truncated": len(value) > GUEST_FAILURE_BYTES, "file": None}
            if receipts is not None and value:
                filename = "guest-%02d-%s.%s.private.bin" % (count, label, name)
                try:
                    write_new(Path(receipts) / filename, value[:GUEST_FAILURE_BYTES])
                    entry["file"] = filename
                except OSError as failure:
                    entry["error"] = type(failure).__name__
            row[name] = entry
        diagnostics.append(row)

    def guest(self, command: str, *, data: bytes | None = None, timeout: float = 15, allow_failure: bool = False,
              label: str | None = None, explain: bool = False) -> tuple[int, bytes]:
        """Run a guest command. A failure that raises (a transport failure, or an exit code the caller did not allow)
        is recorded first under `label`, the fixed name of the step (record_guest_failure), then raised. A caller
        that allows a failed exit code only to judge it itself passes `explain` to have that exit recorded too; a
        poll that expects to fail does not."""
        if not isinstance(label, str) or STEP_LABEL.fullmatch(label) is None:
            label = "unlabelled"
        try:
            code, out, error = self.guest_result(command, data=data, timeout=timeout)
        except BoundedCommandFailure as failure:
            self.record_guest_failure(label, command, failure.code, failure.stdout, failure.stderr, failure.reason)
            raise
        if code != 0 and (explain or not allow_failure):
            self.record_guest_failure(label, command, code, out, error, "nonzero-exit")
        if code != 0 and not allow_failure:
            raise GateError("guest-operation-failed")
        return code, out

    @contextlib.contextmanager
    def locks(self):
        handles = []; claimed = False
        try:
            for path in (self.heavy_lock, self.gpu_lock, self.vm_home / "vm.lock"):
                fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
                handle = os.fdopen(fd, "a+"); handles.append(handle)
                require(stat.S_ISREG(os.fstat(fd).st_mode) and os.fstat(fd).st_uid == os.getuid(), "unsafe-lock")
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise GateError("native-gate-slot-busy") from None
            with self.coordination.open("a") as stream:
                stream.write(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()) + " CLAIM " + self.run_id + " offline synthetic Windows fixtures; no game; bounded 15 minutes\n")
            claimed = True
            yield
        finally:
            for handle in reversed(handles):
                handle.close()
            if claimed:
                with self.coordination.open("a") as stream:
                    stream.write(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()) + " DONE " + self.run_id + " fixture lock ownership released\n")

    def preflight(self, metadata: dict) -> None:
        require(plain_file(self.vm_home / "net-mode", 64).strip() == b"offline", "vm-not-offline")
        require(self.virsh("domstate", DOMAIN).strip() == b"shut off", "vm-not-stopped")
        info = self.virsh("dominfo", DOMAIN).decode("ascii", "strict")
        require(re.search(r"^Managed save:\s+no\s*$", info, re.MULTILINE) is not None, "managed-save-not-absent")
        xml = self.virsh("dumpxml", "--inactive", DOMAIN)
        require(digest(xml) == metadata["inactive_domain_sha256"], "domain-identity-changed")
        tree = ET.fromstring(xml); self.uuid = tree.findtext("uuid")
        require(self.uuid and tree.findtext("name") == DOMAIN and not tree.findall("./devices/interface") and not tree.findall("./devices/hostdev"), "unexpected-domain-device")
        args = [x.get("value", "") for x in tree.findall("./{http://libvirt.org/schemas/domain/qemu/1.0}commandline/{http://libvirt.org/schemas/domain/qemu/1.0}arg")]
        networks = [x for x in args if x.startswith("user,")]
        require(len(networks) == 1 and "restrict=on" in networks[0].split(",") and
            [x for x in networks[0].split(",") if x.startswith("hostfwd=")] == ["hostfwd=tcp:127.0.0.1:2522-:22"], "unexpected-network-boundary")
        require(all(x.get("type") == "vnc" and x.get("listen") in ("127.0.0.1", "::1") for x in tree.findall("./devices/graphics")), "unexpected-display-boundary")
        memory = {x.split(":", 1)[0]: int(x.split()[1]) * 1024 for x in Path("/proc/meminfo").read_text().splitlines()}
        # Coarse early exits only: the room rule (free space against the floor, margin, growth and kept copies, read
        # through the sensor that sees what btrfs has not accounted yet) is OwnedTransient's admission. No CPU pressure
        # limit: the machine rule of 2026-11-03 set none, and a run's evidence here is functional, not timed.
        require(memory["MemAvailable"] >= 12 * 1024**3 and shutil.disk_usage(self.vm_home).free > 50 * 1024**3, "machine-capacity")
        for name in ("id_ed25519", "known_hosts"):
            path = self.vm_home / "ssh" / name; entry = path.lstat()
            require(stat.S_ISREG(entry.st_mode) and entry.st_uid == os.getuid(), "ssh-file-identity")
            if name == "id_ed25519": require(entry.st_mode & 0o077 == 0, "ssh-key-permissions")
        os.nice(max(0, 10 - os.getpriority(os.PRIO_PROCESS, 0)))
        try:
            self.owned = owned_backend.OwnedTransient(metadata)
        except Exception:
            raise GateError("owned-backend-admission-refused") from None

    def start_vm(self):
        self.deadline = time.monotonic() + 630
        require(self.owned is not None, "owned-backend-not-admitted")
        try:
            self.owned.start(self.deadline)
            self.uuid = self.owned.uuid
        except Exception as error:
            receipt = owned_backend.diagnostic("vm-start", "owned-transient-create", None, str(error))
            receipt["failure_code"] = str(error) if isinstance(error, owned_backend.OwnedError) else "backend-operation-failed"
            receipt["exception_class"] = type(error).__name__
            self.diagnostics.append(receipt)
            raise GateError("owned-transient-start-failed") from None

    def wait_guest(self, seconds: int) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.space()
            try:
                code, output = self.guest("cmd.exe /d /c echo CE-ICE-READY", timeout=5, allow_failure=True, label="wait-guest")
            except GateError:
                code, output = 1, b""
            if code == 0 and output.strip() == b"CE-ICE-READY": return
            time.sleep(1)
        raise GateError("guest-boot-deadline")

    def stage(self, prepared: Path, metadata: dict, receipts: Path) -> None:
        self.receipts = receipts
        _, output = self.guest("whoami /user /fo csv /nh", label="stage-whoami")
        rows = list(csv.reader(io.StringIO(output.decode("utf-8", "strict"))))
        require(len(rows) == 1 and len(rows[0]) == 2 and re.fullmatch(r"S-1-5-(?:[0-9]+-)*[0-9]+", rows[0][1]) is not None, "guest-user-identity")
        self.sid = rows[0][1]
        self.guest('cmd.exe /d /c "if exist ' + self.root + ' (exit /b 73) else (mkdir ' + self.root + ')"', label="stage-create-root")
        self.root_owned = True
        self.guest('icacls "' + self.root + '" /inheritance:r /grant:r "*' + self.sid +
            ':(OI)(CI)F" "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"', label="stage-grant-root")
        self.guest('icacls "' + self.root + '" /setowner "*' + self.sid + '"', label="stage-owner-root")
        rendered = task_xml(self.root, self.run_id, metadata["native_manifest_sha256"], self.sid)
        self.task_hash = digest(rendered); write_new(receipts / "task.xml", rendered)
        archive = io.BytesIO()
        with tarfile.open(fileobj=archive, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            for name in sorted(metadata["files"]):
                value = plain_file(prepared / "payload" / name)
                entry = tarfile.TarInfo(name); entry.size = len(value); entry.mode = 0o600; entry.mtime = 0
                tar.addfile(entry, io.BytesIO(value))
            entry = tarfile.TarInfo("task.xml"); entry.size = len(rendered); entry.mode = 0o600; entry.mtime = 0
            tar.addfile(entry, io.BytesIO(rendered))
        require(len(archive.getbuffer()) <= 129 * 1024 * 1024, "archive-bound")
        self.guest('tar -xf - -C "' + self.root.replace("\\", "/") + '"', data=archive.getvalue(), timeout=90, label="stage-extract")
        self.guest('icacls "' + self.root + '" /setowner "*' + self.sid + '" /T', label="stage-owner-tree")
        self.readback(metadata)
        for name in ("output", "fixtures", "work", "ack.txt"):
            self.guest('cmd.exe /d /c "if exist ' + self.root + '\\' + name + ' (exit /b 74) else (exit /b 0)"', label="stage-absent-check")
        self.prepare_reparse(metadata)

    def prepare_reparse(self, metadata):
        require(self.root_owned, "reparse-root-not-owned")
        executables = [name for name in metadata["files"] if name.startswith("package/grant/") and name.endswith(".exe")]
        require(len(executables) == 1, "reparse-preparation-executable")
        executable = self.root + "\\" + executables[0].replace("/", "\\")
        end = min(time.monotonic() + 30, self.deadline or float("inf"))
        code, output, operation = self._ack_step("fixture-reparse-prepare",
            'cmd.exe /d /c "' + executable + ' --prepare-native-reparse ' + self.root + '"', end)
        matches = output.rstrip(b"\r\n") == b"PASS prepared native reparse fixtures=%d" % PREPARED_LINKS
        report = {"exit_code": code, "success_marker_matches": matches, "declared_prepared_links": PREPARED_LINKS, "operation": operation}
        write_new(self.receipts / "reparse-preparation.json", json_bytes(report))
        require(code == 0 and matches and not operation.get("artifact_error"), "native-reparse-preparation-failed")

    def readback(self, metadata: dict, *, best_effort: bool = False) -> dict:
        expected = {name: pin["sha256"] for name, pin in metadata["files"].items()}
        if self.task_hash:
            expected["task.xml"] = self.task_hash
        rows = []
        for name, wanted in expected.items():
            require(relative(name), "readback-path")
            row = {"path": name, "status": "unavailable", "sha256": None}
            try:
                if self.deadline is not None and time.monotonic() >= self.deadline:
                    row["status"] = "deadline"
                else:
                    _, output = self.guest('certutil -hashfile "' + self.root + '\\' + name.replace('/', '\\') + '" SHA256',
                                           timeout=2 if best_effort else 15, label="readback-hash")
                    hashes = re.findall(rb"(?im)^\s*([0-9a-f]{64})\s*$", output)
                    if len(hashes) == 1:
                        row["sha256"] = hashes[0].lower().decode("ascii")
                        row["status"] = "match" if row["sha256"] == wanted else "mismatch"
            except (GateError, OSError, ValueError, KeyError, TypeError):
                pass
            rows.append(row)
        report = {"complete": all(row["status"] == "match" for row in rows), "files": rows}
        if not best_effort:
            require(report["complete"], "guest-file-readback")
        return report

    def create_task(self) -> None:
        self.guest('schtasks /Create /TN "' + self.task + '" /XML "' + self.root + '\\task.xml"', label="create-task")

    def start_task(self) -> None:
        started = time.monotonic()
        timing = {"started_time_ns": time.time_ns()}; self.ack_report["task_start"] = timing
        try:
            self.guest('schtasks /Run /TN "' + self.task + '"', label="start-task")
        finally:
            timing["elapsed_ms"] = round((time.monotonic() - started) * 1000, 3)

    def read_json(self, name: str, seconds: int) -> dict:
        require(name in ("ready", "done", "failed-case"), "receipt-name")
        started = time.monotonic(); deadline = started + seconds
        timing = {"started_time_ns": time.time_ns(), "poll_attempts": 0, "seconds_limit": seconds}
        if name == "ready": self.ack_report.setdefault("ready_polls", []).append(timing)
        try:
            while time.monotonic() < deadline:
                timing["poll_attempts"] += 1
                code, output = self.guest('cmd.exe /d /c type "' + self.root + '\\output\\' + name + '.json"', timeout=min(2.0, max(0.01, deadline - time.monotonic())), allow_failure=True,
                                          label="read-" + name)
                if code == 0:
                    require(len(output) <= 65536, "receipt-bound")
                    try: return read_json(output)
                    except (ValueError, UnicodeError): raise GateError("malformed-native-receipt") from None
                time.sleep(0.25)
            raise GateError("native-receipt-deadline")
        finally:
            timing["elapsed_ms"] = round((time.monotonic() - started) * 1000, 3)

    def _ack_step(self, label, command, end):
        require(self.receipts is not None and len(self.ack_report["operations"]) < 32, "ack-receipt-bound")
        started = time.monotonic()
        code, out, error = None, b"", b""
        row = {"operation": label, "command_sha256": digest(command.encode()), "started_time_ns": time.time_ns()}
        try:
            remaining = end - started
            require(remaining > 0, "ack-operation-deadline")
            code, out, error = self.guest_result(command, timeout=min(5, remaining), limit=16384)
            row["classification"] = ack_classification(code, out, error)
            row["transport_complete"] = True
        except (GateError, OSError) as failure:
            if isinstance(failure, BoundedCommandFailure):
                code, out, error = failure.code, failure.stdout, failure.stderr
                row["transport_reason"] = failure.reason
            row["classification"] = "transport-or-deadline-failure"
            row["transport_complete"] = False
            row["exception_class"] = type(failure).__name__
        row["transport_elapsed_ms"] = round((time.monotonic() - started) * 1000, 3)
        row["exit_code"] = code
        index = len(self.ack_report["operations"])
        for name, value in [("stdout", out), ("stderr", error)]:
            filename = "ack-%02d-%s.%s.private.bin" % (index, label, name)
            try:
                write_new(self.receipts / filename, value[:8192])
                row[name] = {"file": filename, "bytes": len(value), "retained_bytes": min(8192, len(value)),
                             "sha256": digest(value), "truncated": len(value) > 8192}
            except OSError as failure:
                row["artifact_error"] = type(failure).__name__
        self.ack_report["operations"].append(row)
        return code, out, row

    def _ack_file(self, root, name, label, end):
        path = root + "\\" + name
        code, value, operation = self._ack_step(label,
            'cmd.exe /d /c "if exist ' + path + ' (type ' + path + ') else (exit /b 76)"', end)
        return {"status": "present" if code == 0 else "absent" if code == 76 else "unavailable",
                "exit_code": code, "bytes": len(value), "sha256": digest(value) if code == 0 else None,
                "matches_run": code == 0 and len(value) <= 128 and value.rstrip(b"\r\n") == self.run_id.encode(),
                "artifact_error": operation.get("artifact_error")}

    def _ack_round(self, root, label, end):
        code, _, operation = self._ack_step(label + "-publish", owned_ack_command(root, self.run_id, self.sid), end)
        return {"exit_code": code, "publication": operation,
                "temporary": self._ack_file(root, "ack.tmp", label + "-temporary", end),
                "final": self._ack_file(root, "ack.txt", label + "-final", end)}

    def ack_control(self):
        require(self.root_owned, "ack-root-not-owned")
        end = min(time.monotonic() + 30, self.deadline or float("inf"))
        root = self.root + "\\ack-control"
        report = {"pass": False}; self.ack_report["control"] = report
        code, _, setup = self._ack_step("control-create", 'cmd.exe /d /c "if exist ' + root +
            ' (exit /b 73) else (mkdir ' + root + ')"', end)
        report["setup"] = setup
        require(code == 0 and not setup.get("artifact_error"), "ack-control-setup-failed")
        first = report["first"] = self._ack_round(root, "control-first", end)
        require(first["exit_code"] == 0, "ack-control-publication-failed")
        require(first["temporary"]["status"] == "absent" and first["final"]["matches_run"], "ack-control-readback-failed")
        collision = report["collision"] = self._ack_round(root, "control-collision", end)
        require(collision["exit_code"] == 75 and collision["temporary"]["status"] == "absent" and
            collision["final"]["matches_run"] and first["final"]["sha256"] == collision["final"]["sha256"], "ack-control-collision-failed")
        require(not any(r.get("artifact_error") for r in self.ack_report["operations"]), "ack-control-artifact-failed")
        report["pass"] = True

    def acknowledge(self) -> None:
        require(self.root_owned, "ack-root-not-owned")
        report = self._ack_round(self.root, "primary", min(time.monotonic() + 15, self.deadline or float("inf")))
        self.ack_report["primary"] = report
        require(report["exit_code"] == 0, "ack-publication-failed")
        require(report["temporary"]["status"] == "absent" and report["final"]["matches_run"], "ack-readback-failed")
        require(not any(r.get("artifact_error") for r in self.ack_report["operations"]), "ack-artifact-failed")

    def wait_task_exit(self, seconds: int) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            _, output = self.guest('schtasks /Query /TN "' + self.task + '" /V /FO CSV', timeout=5, label="task-status")
            rows = list(csv.DictReader(io.StringIO(output.decode("utf-8", "strict"))))
            require(len(rows) == 1 and {"TaskName", "Status", "Last Result"}.issubset(rows[0]), "task-status-schema")
            row = rows[0]
            require(row["TaskName"].lstrip("\\") == self.task, "task-status-identity")
            if row["Status"] == "Ready":
                require(row["Last Result"] == "0", "supervisor-exit-failed")
                return
            require(row["Status"] in ("Running", "Queued"), "supervisor-state-failed")
            time.sleep(0.25)
        raise GateError("supervisor-exit-deadline")

    def finish_task(self, *, force: bool) -> None:
        if force:
            self.deadline = None
            code, _ = self.guest('schtasks /End /TN "' + self.task + '"', allow_failure=True, explain=True, label="end-task")
            require(code == 0, "owned-task-end-unconfirmed")
        # Preserve the unique registered task and guest root as evidence; never delete a task or tree.

    def stop_vm(self, seconds):
        self.deadline = 0
        require(self.owned is not None, "owned-backend-not-admitted")
        if not self.owned.stop(seconds):
            # A tripped run cannot pass, but its cleanup may still be verified; say which of the two failed.
            tripped = space_tripped(self)
            verified = self.owned.report.get("cleanup_verified") is True
            raise GateError(SPACE_TRIPPED if tripped and verified else "owned-transient-cleanup-unverified")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare"); prep.add_argument("--selection", type=Path, required=True); prep.add_argument("--out", type=Path, required=True)
    check = sub.add_parser("verify"); check.add_argument("prepared", type=Path)
    execute = sub.add_parser("execute"); execute.add_argument("prepared", type=Path); execute.add_argument("--independent-review", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "prepare":
            result = prepare(read_json(plain_file(args.selection, 1024 * 1024)), args.out)
            print(json.dumps({"prepared": True, "run_id": result["run_id"], "native_manifest_sha256": result["native_manifest_sha256"]}))
            return 0
        metadata = verify_prepared(args.prepared)
        require_sealed_runner_modules(metadata)
        if args.action == "verify":
            print(json.dumps({"verified": True, "run_id": metadata["run_id"]})); return 0
        review = read_json(plain_file(args.independent_review, 65536))
        require(set(review) == {"schema", "scope", "sealed_sha256", "runner_sha256", "approved", "reviewer"} and review["schema"] == 1 and
            review["scope"] == "offline-synthetic-windows-fixtures" and review["approved"] is True and
            isinstance(review["reviewer"], str) and 1 <= len(review["reviewer"]) <= 96 and
            review["sealed_sha256"] == digest(plain_file(args.prepared / "sealed.json")) and
            review["runner_sha256"] == digest(plain_file(Path(__file__))), "independent-review-required")
        result = run_gate(args.prepared, metadata, RealBackend(metadata))
        print(json.dumps({"pass": result["pass"], "run_id": metadata["run_id"], "failure": result["failure"], "cleanup_failures": result["cleanup_failures"]}))
        return 0 if result["pass"] else 1
    except (GateError, OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"pass": False, "failure": str(error) if isinstance(error, GateError) else "invalid-input-or-io"}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
