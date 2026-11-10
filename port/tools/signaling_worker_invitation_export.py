#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export a trusted Worker-bound pair for the native invitation importer.

Reads only explicit private inputs. The only service operation is Current for
each role. Both native invitations become visible in one no-replacement rename.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import fcntl
import json
import os
from pathlib import Path
import re
import secrets
import select
import stat
import sys
import time

import ice_pilot_courier as wire
import signaling_api_contract as api
import signaling_pair_bindings as bindings


PAIR_FIELDS = frozenset(("schema", "approval", "host", "guest", "issuedAt", "expiresAt"))
MAX_TIME = (1 << 63) - 14400
MAX_CONFIG = 2048
TOKEN_FILE = bindings.TOKEN_FILE
ADMISSION_FILE = bindings.ADMISSION_FILE


class Refused(Exception):
    pass


class PublicationUnverified(Refused):
    """The complete directory may already be visible and must be preserved."""


def require(value: bool) -> None:
    if not value:
        raise Refused()


def absolute(path: Path) -> Path:
    path = Path(path)
    require(".." not in path.parts)
    return path.absolute()


def private_file(path: Path, limit: int) -> bytes:
    path = absolute(path)
    directory = wire.private._open_directory(path.parent)
    try:
        return wire.private._read_private(directory, path.name, limit)
    finally:
        os.close(directory)


def key_from_fd(descriptor: int) -> bytes:
    require(type(descriptor) is int and descriptor >= 0)
    fd = os.dup(descriptor)
    try:
        before = os.fstat(fd)
        require(before.st_uid == os.geteuid() and stat.S_IMODE(before.st_mode) & 0o077 == 0)
        regular = stat.S_ISREG(before.st_mode)
        require(regular or stat.S_ISFIFO(before.st_mode))
        if regular:
            require(before.st_size == 32 and os.lseek(fd, 0, os.SEEK_CUR) == 0)
        deadline = time.monotonic() + 5
        data = bytearray()
        while True:
            if not regular:
                remaining = deadline - time.monotonic()
                require(remaining > 0 and bool(select.select([fd], [], [], remaining)[0]))
            chunk = os.read(fd, 33 - len(data))
            if not chunk:
                break
            data.extend(chunk)
            require(len(data) <= 32)
        if regular:
            after = os.fstat(fd)
            require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
                    == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns))
        require(len(data) == 32)
        return bytes(data)
    finally:
        os.close(fd)


def unique_object(items):
    result = {}
    for name, value in items:
        require(name not in result)
        result[name] = value
    return result


def read_pair(path: Path) -> dict:
    value = json.loads(private_file(path, MAX_CONFIG).decode("utf-8"), object_pairs_hook=unique_object)
    require(type(value) is dict and set(value) == PAIR_FIELDS)
    require(type(value["schema"]) is int and value["schema"] == 1)
    require(type(value["approval"]) is str and bool(re.fullmatch(r"[0-9a-f]{32}", value["approval"])))
    for role in ("host", "guest"):
        require(type(value[role]) is str and bool(re.fullmatch(r"p_[0-9a-f]{32}_" + role, value[role])))
    require(value["host"] != value["guest"])
    require(all(type(value[name]) is int and 0 <= value[name] <= MAX_TIME for name in ("issuedAt", "expiresAt")))
    return value


class Clock:
    def __init__(self, clock):
        self.clock, self.last = clock, -1

    def __call__(self) -> int:
        now = self.clock()
        require(type(now) is int and 0 <= now <= MAX_TIME and now >= self.last)
        self.last = now
        return now


def current_time() -> int:
    return int(time.time())


def validate_times(pair: dict, now: int, maximum_seconds: int) -> None:
    require(type(maximum_seconds) is int and 1 <= maximum_seconds <= 14400)
    require(pair["issuedAt"] <= now < pair["expiresAt"] <= pair["issuedAt"] + maximum_seconds)
    require(pair["expiresAt"] <= now + 14400)


def native_metadata(role: bindings.RoleAdmission) -> dict:
    # The native parser accepts exactly four fields. issuedAt remains in CEPAIR1.
    return dict(schema=1, approval=role.approval, role=role.role, expiresAt=role.expires_at)


def absent(directory: int, name: str) -> bool:
    try:
        os.stat(name, dir_fd=directory, follow_symlinks=False)
    except FileNotFoundError:
        return True
    return False


def write_private(directory: int, name: str, data: bytes) -> None:
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                 0o600, dir_fd=directory)
    try:
        pending = memoryview(data)
        while pending:
            written = os.write(fd, pending)
            require(written > 0)
            pending = pending[written:]
        os.fsync(fd)
    finally:
        os.close(fd)
    require(wire.private._read_private(directory, name, len(data)) == data)


def directory_identity(value) -> tuple[int, int] | None:
    return (value.st_dev, value.st_ino) if stat.S_ISDIR(value.st_mode) else None


def same_directory_entry(directory: int, name: str, identity: tuple[int, int]) -> bool:
    try:
        return directory_identity(os.stat(name, dir_fd=directory, follow_symlinks=False)) == identity
    except FileNotFoundError:
        return False


def publish_pair(base: int, final: str, names: dict[str, str], admission: bindings.PairAdmission,
                 check_time) -> None:
    temporary = ".worker-invitation-" + secrets.token_hex(12)
    handles = ExitStack()
    staged = identity = None
    directories = []
    committed = False
    try:
        os.mkdir(temporary, mode=0o700, dir_fd=base)
        identity = directory_identity(os.stat(temporary, dir_fd=base, follow_symlinks=False))
        require(identity is not None)
        staged = os.open(temporary, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=base)
        handles.callback(os.close, staged)
        require(directory_identity(os.fstat(staged)) == identity)
        for role in ("host", "guest"):
            name = names[role]
            os.mkdir(name, mode=0o700, dir_fd=staged)
            entry = dict(name=name, identity=None, fd=None, files=[])
            directories.append(entry)
            entry["identity"] = directory_identity(os.stat(name, dir_fd=staged, follow_symlinks=False))
            require(entry["identity"] is not None)
            fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=staged)
            handles.callback(os.close, fd)
            entry["fd"] = fd
            require(directory_identity(os.fstat(fd)) == entry["identity"])
            value = getattr(admission, role)
            for filename, data in ((TOKEN_FILE, value.token), (ADMISSION_FILE, wire._json(native_metadata(value)))):
                entry["files"].append(filename)
                write_private(fd, filename, data)
            os.fsync(fd)
        os.fsync(staged)
        check_time()
        require(same_directory_entry(base, temporary, identity))
        wire.private._publish_no_replace(base, temporary, final)
        require(same_directory_entry(base, final, identity))
        os.fsync(base)
        committed = True
    finally:
        try:
            if not committed and identity is not None:
                try:
                    published = same_directory_entry(base, final, identity)
                    private_staging = same_directory_entry(base, temporary, identity)
                except OSError:
                    raise PublicationUnverified() from None
                # A rename can take effect before its caller receives a result or signal.
                # Never remove capabilities through a moved or already published directory FD.
                if published or not private_staging:
                    raise PublicationUnverified() from None
                for entry in directories:
                    if (entry["identity"] is None or not same_directory_entry(staged, entry["name"], entry["identity"])
                            or (entry["fd"] is not None and directory_identity(os.fstat(entry["fd"])) != entry["identity"])):
                        raise PublicationUnverified() from None
                for entry in directories:
                    for filename in entry["files"]:
                        try:
                            os.unlink(filename, dir_fd=entry["fd"])
                        except FileNotFoundError:
                            pass
                    os.rmdir(entry["name"], dir_fd=staged)
                os.rmdir(temporary, dir_fd=base)
        finally:
            handles.close()


def export_worker(pair_config: Path, origin: str, host_output: Path, guest_output: Path, *,
                  admission_key: Path | None = None, admission_key_fd: int | None = None,
                  maximum_seconds: int = 7200, clock=current_time) -> None:
    require(sys.platform.startswith("linux"))
    require((admission_key is None) != (admission_key_fd is None))
    transport = api.HttpsTransport(origin)  # Fixed, verified HTTPS; no proxy, account or ambient key lookup.
    pair = read_pair(pair_config)
    checked_clock = Clock(clock)
    validate_times(pair, checked_clock(), maximum_seconds)
    outputs = {"host": absolute(host_output), "guest": absolute(guest_output)}
    parent = outputs["host"].parent
    require(parent == outputs["guest"].parent and outputs["host"].name != outputs["guest"].name)
    require(all(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", path.name) for path in outputs.values()))
    require(bool(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", parent.name)))
    base = wire.private._open_directory(parent.parent)
    try:
        fcntl.flock(base, fcntl.LOCK_EX | fcntl.LOCK_NB)
        require(absent(base, parent.name))
        key = private_file(admission_key, 32) if admission_key is not None else key_from_fd(admission_key_fd)
        require(len(key) == 32)
        verifier = bindings.PairCapabilityVerifier(key, maximum_seconds=maximum_seconds)
        members = {}
        for role in ("host", "guest"):
            token = verifier.mint(pair[role], role, pair["issuedAt"], pair["expiresAt"])
            members[role] = bindings.RoleAdmission(pair["approval"], pair[role], role,
                                                  pair["issuedAt"], pair["expiresAt"], token)
        admission = bindings.PairAdmission(members["host"], members["guest"])
        for role in ("host", "guest"):
            client = api.Client(transport, members[role].token, checked_clock)
            view = client.current()
            # Current alone establishes the existing client's immutable private pair binding.
            require(view.status in ("awaiting_pair_consent", "admitted") and view.role == role)
            require(client._approval == pair["approval"] and client._pair_expires == pair["expiresAt"])
            require(view.pair_expires_at == pair["expiresAt"])
        def still_current():
            validate_times(pair, checked_clock(), maximum_seconds)
        still_current()
        publish_pair(base, parent.name, {role: path.name for role, path in outputs.items()}, admission, still_current)
    finally:
        os.close(base)


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refused()


def main(argv=None) -> int:
    try:
        parser = Parser(description=__doc__)
        parser.add_argument("--pair-config", type=Path, required=True)
        parser.add_argument("--service-origin", required=True)
        parser.add_argument("--host-output", type=Path, required=True)
        parser.add_argument("--guest-output", type=Path, required=True)
        parser.add_argument("--maximum-seconds", type=int, default=7200)
        key = parser.add_mutually_exclusive_group(required=True)
        key.add_argument("--admission-key", type=Path)
        key.add_argument("--admission-key-fd", type=int)
        options = parser.parse_args(argv)
        export_worker(options.pair_config, options.service_origin, options.host_output, options.guest_output,
                      admission_key=options.admission_key, admission_key_fd=options.admission_key_fd,
                      maximum_seconds=options.maximum_seconds)
    except PublicationUnverified:
        print("Export could not be verified; preserve the private pair directory.", file=sys.stderr)
        return 1
    except Exception:
        print("Invitation export refused.", file=sys.stderr)
        return 1
    print("Exported both private role invitations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
