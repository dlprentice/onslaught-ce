#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline, fixed-record courier for an explicitly coordinated Linux ICE pilot.

This program does not contact a provider or peer, load a provider key, mint a
grant, authenticate an account, or choose a file-transfer channel. Imports use
the caller's existing request; they never create a new session identity.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import fcntl
import json
import os
from pathlib import Path
import re
import secrets
import stat
import sys
import time
from typing import Callable

import turn_operator_issue as private

PARCEL = "ice-pilot-transfer.json"
MAX_PARCEL = 65536
MAX_RECORD = 83 + 4095 + 16
REQUEST_SECONDS = 120
EXCHANGE_SECONDS = 300
REQUEST_FIELDS = {"schema", "role", "context", "requestedAt", "requestExpiresAt", "grantExpiresAt"}
GRANT_FIELDS = {"schema", "role", "context", "issuedAt", "expiresAt", "iceServers"}
PARCEL_FIELDS = {"schema", "kind", "context", "createdAt", "expiresAt", "records"}


class Refused(Exception):
    """Failures intentionally contain no input values or upstream exceptions."""

    def __init__(self) -> None:
        super().__init__("pilot transfer refused")


def _require(condition: bool) -> None:
    if not condition:
        raise Refused()


def _json(value: object) -> bytes:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True).encode("ascii") + b"\n"


def _integer(value: object) -> bool:
    return type(value) is int and 0 <= value <= 2**63 - private.MAX_TTL


def _context(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _request(data: bytes, role: str, now: int, context: str | None = None) -> dict:
    value = private._decode(data, 4096)
    _require(isinstance(value, dict) and set(value) == REQUEST_FIELDS)
    _require(type(value["schema"]) is int and value["schema"] == 1 and value["role"] == role)
    _require(_context(value["context"]) and (context is None or value["context"] == context))
    start, end, grant = (value[k] for k in ("requestedAt", "requestExpiresAt", "grantExpiresAt"))
    _require(all(_integer(n) for n in (start, end, grant, now)))
    _require(start <= now < end <= grant and end - start <= REQUEST_SECONDS and grant - start <= private.MAX_TTL)
    return value


def _grant(data: bytes, request: dict, now: int) -> dict:
    value = private._decode(data, private.MAX_BODY)
    _require(isinstance(value, dict) and set(value) == GRANT_FIELDS)
    _require(type(value["schema"]) is int and value["schema"] == 1)
    _require(value["role"] == request["role"] and value["context"] == request["context"])
    start, end = value["issuedAt"], value["expiresAt"]
    _require(_integer(start) and _integer(end))
    _require(request["requestedAt"] <= start <= now < end <= request["grantExpiresAt"] and end - start <= private.MAX_TTL)
    # Pure validation only: this does not instantiate a transport or read a key.
    private.parse_provider_response(_json({"iceServers": value["iceServers"]}))
    return value


def _exchange(data: bytes, role: str, now: int, offer: bytes | None = None) -> int:
    _require(99 < len(data) <= MAX_RECORD and data[:8] == b"CEICE002")
    _require(data[8] == (1 if role == "host" else 2))
    start, end = int.from_bytes(data[9:17], "little"), int.from_bytes(data[17:25], "little")
    size = int.from_bytes(data[81:83], "little")
    _require(0 < size <= 4095 and len(data) == 99 + size)
    _require(_integer(start) and _integer(end) and start < end and end - start <= EXCHANGE_SECONDS)
    _require(now < end and start <= now + 30)
    if offer is not None:
        offer_end = _exchange(offer, "host", now)
        _require(data[25:57] == offer[25:57] and end <= offer_end)
    # The receiving CeIceExchange, not the courier, authenticates ciphertext.
    return end


def _record_name(context: str, kind: str) -> str:
    return "ce-ice-" + context + "." + kind


def _names(kind: str, context: str) -> tuple[str, ...]:
    if kind == "request":
        return ("relay-guest.request.json",)
    if kind == "offer":
        # Publication order matters: the offer exists before ICE can start.
        return (_record_name(context, "offer"), "relay-guest.json")
    return (_record_name(context, "answer"),)


def _limit(name: str) -> int:
    if name.endswith(".request.json"):
        return 4096
    if name.endswith(".offer") or name.endswith(".answer"):
        return MAX_RECORD
    return private.MAX_BODY


def _read(directory: int, name: str) -> bytes:
    return private._read_private(directory, name, _limit(name))


def _existing(directory: int, name: str, expected: bytes) -> bool:
    try:
        os.stat(name, dir_fd=directory, follow_symlinks=False)
    except FileNotFoundError:
        return False
    _require(private._read_private(directory, name, MAX_PARCEL) == expected)
    return True


def _publish(directory: int, name: str, data: bytes) -> None:
    if _existing(directory, name, data):
        return  # Identical retry is idempotent; timestamps are never renewed.
    temporary = ".ice-pilot-publish-" + secrets.token_hex(12)
    fd = -1
    owned = False
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=directory)
        owned = True
        offset = 0
        while offset < len(data):
            count = os.write(fd, data[offset:])
            _require(count > 0)
            offset += count
        os.fsync(fd)
        private._publish_no_replace(directory, temporary, name)
        owned = False
        os.fsync(directory)
    finally:
        if fd >= 0:
            os.close(fd)
        if owned:
            os.unlink(temporary, dir_fd=directory)


def _same_directory(path: Path, expected: int) -> None:
    current = private._open_directory(path)
    try:
        a, b = os.fstat(expected), os.fstat(current)
        _require((a.st_dev, a.st_ino) == (b.st_dev, b.st_ino))
        _require(a.st_uid == os.geteuid() and stat.S_IMODE(a.st_mode) & 0o077 == 0)
    finally:
        os.close(current)


def _clock(clock: Callable[[], int], first: int | None = None, deadline: int | None = None) -> int:
    now = clock()
    _require(_integer(now) and (first is None or now >= first) and (deadline is None or now < deadline))
    return now


def _export_records(directory: int, kind: str, request: dict, now: int) -> tuple[dict[str, bytes], int]:
    context = request["context"]
    deadline = request["requestExpiresAt"]
    if kind == "request":
        records = {"relay-guest.request.json": _read(directory, "relay-guest.request.json")}
        _request(records["relay-guest.request.json"], "guest", now, context)
    elif kind == "offer":
        guest = _request(_read(directory, "relay-guest.request.json"), "guest", now, context)
        grant_data = _read(directory, "relay-guest.json")
        grant = _grant(grant_data, guest, now)
        offer_name = _record_name(context, "offer")
        offer = _read(directory, offer_name)
        deadline = min(deadline, guest["requestExpiresAt"], grant["expiresAt"], _exchange(offer, "host", now))
        records = {offer_name: offer, "relay-guest.json": grant_data}
    else:
        grant = _grant(_read(directory, "relay-guest.json"), request, now)
        offer = _read(directory, _record_name(context, "offer"))
        answer_name = _record_name(context, "answer")
        answer = _read(directory, answer_name)
        deadline = min(deadline, grant["expiresAt"], _exchange(answer, "guest", now, offer))
        records = {answer_name: answer}
    return records, deadline


def _parcel(data: bytes, kind: str, request: dict, now: int) -> tuple[dict[str, bytes], int]:
    value = private._decode(data, MAX_PARCEL)
    _require(isinstance(value, dict) and set(value) == PARCEL_FIELDS)
    _require(type(value["schema"]) is int and value["schema"] == 1 and value["kind"] == kind and value["context"] == request["context"])
    start, end = value["createdAt"], value["expiresAt"]
    _require(_integer(start) and _integer(end) and start <= now < end and end - start <= REQUEST_SECONDS)
    records = value["records"]
    expected = _names(kind, request["context"])
    _require(isinstance(records, dict) and set(records) == set(expected))
    decoded = {}
    for name in expected:
        encoded = records[name]
        _require(isinstance(encoded, str) and len(encoded) <= MAX_PARCEL)
        raw = base64.b64decode(encoded, validate=True)
        _require(0 < len(raw) <= _limit(name) and base64.b64encode(raw).decode("ascii") == encoded)
        decoded[name] = raw
    return decoded, min(end, request["requestExpiresAt"])


def _import_records(directory: int, kind: str, request: dict, records: dict[str, bytes], now: int) -> int:
    context = request["context"]
    if kind == "request":
        incoming = _request(records["relay-guest.request.json"], "guest", now, context)
        return incoming["requestExpiresAt"]
    if kind == "offer":
        grant = _grant(records["relay-guest.json"], request, now)
        return min(grant["expiresAt"], _exchange(records[_record_name(context, "offer")], "host", now))
    grant = _grant(_read(directory, "relay-host.json"), request, now)
    offer = _read(directory, _record_name(context, "offer"))
    return min(grant["expiresAt"], _exchange(records[_record_name(context, "answer")], "guest", now, offer))


def transfer(action: str, kind: str, session_directory: Path, transfer_directory: Path,
             clock: Callable[[], int] = lambda: int(time.time())) -> None:
    """One finite action. Both directories must already exist and be private.

    Clock injection exists only for authored fixtures; the CLI always uses UTC.
    A parcel is not a signature. Delivery requires an operator-chosen private,
    authenticated channel; actual exchange authentication remains in CE.
    """
    session = parcel_dir = -1
    try:
        _require(sys.platform.startswith("linux") and action in ("export", "import") and kind in ("request", "offer", "answer"))
        session_path, parcel_path = Path(session_directory).absolute(), Path(transfer_directory).absolute()
        session = private._open_directory(session_path)
        parcel_dir = private._open_directory(parcel_path)
        a, b = os.fstat(session), os.fstat(parcel_dir)
        _require((a.st_dev, a.st_ino) != (b.st_dev, b.st_ino))
        # Nonblocking locks only coordinate this courier; no lock file is added
        # to the application's private record directory.
        for fd in sorted((session, parcel_dir), key=lambda n: (os.fstat(n).st_dev, os.fstat(n).st_ino)):
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        now = _clock(clock)
        role = "guest" if (action, kind) in (("export", "request"), ("import", "offer"), ("export", "answer")) else "host"
        request_name = "relay-" + role + ".request.json"
        request_data = _read(session, request_name)
        request = _request(request_data, role, now)
        if action == "export":
            records, deadline = _export_records(session, kind, request, now)
            value = {"schema": 1, "kind": kind, "context": request["context"], "createdAt": now, "expiresAt": deadline,
                     "records": {name: base64.b64encode(raw).decode("ascii") for name, raw in records.items()}}
            data = _json(value)
            _require(len(data) <= MAX_PARCEL)
            outputs, destination = {PARCEL: data}, parcel_dir
        else:
            records, deadline = _parcel(private._read_private(parcel_dir, PARCEL, MAX_PARCEL), kind, request, now)
            deadline = min(deadline, _import_records(session, kind, request, records, now))
            outputs, destination = records, session
        # Validate every destination before publishing any record. Individual
        # files are atomic, but an offer/grant pair is not one transaction.
        for name, data in outputs.items():
            _existing(destination, name, data)
        _require(_read(session, request_name) == request_data)
        for name, data in outputs.items():
            _clock(clock, now, deadline)
            _same_directory(session_path, session)
            _same_directory(parcel_path, parcel_dir)
            _publish(destination, name, data)
        _clock(clock, now, deadline)
        _same_directory(session_path, session)
        _same_directory(parcel_path, parcel_dir)
    except (Refused, private.Rejected, OSError, ValueError, TypeError, KeyError, binascii.Error, RecursionError):
        raise Refused() from None
    finally:
        if parcel_dir >= 0:
            os.close(parcel_dir)
        if session >= 0:
            os.close(session)


class _Parser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        self.exit(2, "pilot transfer refused\n")


def main(argv: list[str] | None = None) -> int:
    parser = _Parser(description=__doc__)
    parser.add_argument("action", choices=("export", "import"))
    parser.add_argument("kind", choices=("request", "offer", "answer"))
    parser.add_argument("--session-directory", type=Path, required=True)
    parser.add_argument("--transfer-directory", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        transfer(args.action, args.kind, args.session_directory, args.transfer_directory)
    except Refused:
        print("pilot transfer refused", file=sys.stderr)
        return 1
    print("pilot transfer complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
