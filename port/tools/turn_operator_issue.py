#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Operator-only fixed-provider issuance; import does no I/O.

A live request needs --issue and a separate protected local key file. Tests
inject an offline transport. There is no public listener or authentication flow.
"""
from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass, field
import json
import errno
import math
import os
from pathlib import Path
import re
import secrets
import ssl
import stat
import sys
import time
from typing import Callable, Protocol
import urllib.request

MAX_BODY = 32768
MAX_SECRET = 513
DEFAULT_TTL = 7200
MAX_TTL = 14400
REQUEST_TIMEOUT = 10
USER_AGENT = "OnslaughtCE-Relay-Operator/0.1"
ENDPOINT_PREFIX = "https://rtc.live.cloudflare.com/v1/turn/keys/"
ENDPOINT_SUFFIX = "/credentials/generate-ice-servers"
UDP_URLS = frozenset((
    "turn:turn.cloudflare.com:3478?transport=udp",
    "turn:turn.cloudflare.com:443?transport=udp",
))
OTHER_TURN_URLS = frozenset((
    "turn:turn.cloudflare.com:3478?transport=tcp",
    "turn:turn.cloudflare.com:80?transport=tcp",
    "turns:turn.cloudflare.com:5349?transport=tcp",
    "turns:turn.cloudflare.com:443?transport=tcp",
))
STUN_URL = "stun:stun.cloudflare.com:3478"
STUN_URLS = frozenset((STUN_URL, "stun:stun.cloudflare.com:443"))


class Rejected(Exception):
    """Only fixed labels may appear in operator-facing failures."""

    def __init__(self, code: str = "issuance rejected") -> None:
        allowed = {
            "issuance rejected", "invalid operator input", "private file checks unavailable",
            "private directory rejected", "private file rejected", "key file rejected",
            "issuance already attempted", "session context mismatch", "provider request failed",
            "provider response rejected", "grant expired before writing", "grant write failed",
            "local watch expired",
        }
        super().__init__(code if code in allowed else "issuance rejected")


@dataclass(frozen=True)
class ProviderKey:
    key_id: str = field(repr=False)
    api_token: str = field(repr=False)


@dataclass(frozen=True)
class SeparateKeyFiles:
    token_id_file: Path = field(repr=False)
    api_token_file: Path = field(repr=False)


KeyFiles = Path | SeparateKeyFiles


def _key_paths(source: KeyFiles) -> tuple[Path, ...]:
    if isinstance(source, Path):
        return (source,)
    if isinstance(source, SeparateKeyFiles) and isinstance(source.token_id_file, Path) and isinstance(source.api_token_file, Path) and source.token_id_file != source.api_token_file:
        return (source.token_id_file, source.api_token_file)
    raise Rejected("key file rejected")


def _checked_key(key_id: object, api_token: object) -> ProviderKey:
    if not isinstance(key_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", key_id) or not _secret(api_token, 2048):
        raise Rejected("key file rejected")
    return ProviderKey(key_id, api_token)


@dataclass(frozen=True)
class Relay:
    urls: tuple[str, ...]
    username: str = field(repr=False)
    credential: str = field(repr=False)


@dataclass(frozen=True)
class ProviderLease:
    relays: tuple[Relay, ...] = field(repr=False)


class Transport(Protocol):
    def generate(self, key: ProviderKey, ttl: int) -> bytes: ...


def _pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise Rejected("provider response rejected")
        result[key] = value
    return result


def _decode(data: bytes, limit: int) -> object:
    if type(data) is not bytes or not 0 < len(data) <= limit:
        raise Rejected("provider response rejected")
    try:
        return json.loads(data.decode("ascii"), object_pairs_hook=_pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(Rejected("provider response rejected")))
    except (UnicodeError, ValueError, RecursionError):
        raise Rejected("provider response rejected") from None


def _secret(value: object, maximum: int = MAX_SECRET) -> bool:
    return isinstance(value, str) and 0 < len(value) <= maximum and all(33 <= ord(c) <= 126 for c in value)


def parse_provider_response(data: bytes) -> ProviderLease:
    """Validate the documented shape; retain only supported fixed UDP URLs."""
    response = _decode(data, MAX_BODY)
    if not isinstance(response, dict) or set(response) != {"iceServers"}:
        raise Rejected("provider response rejected")
    entries = response["iceServers"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= 8:
        raise Rejected("provider response rejected")
    relays: list[Relay] = []
    selected: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) not in ({"urls"}, {"urls", "username", "credential"}):
            raise Rejected("provider response rejected")
        urls = entry["urls"]
        if not isinstance(urls, list) or not 1 <= len(urls) <= 8 or any(not isinstance(url, str) for url in urls):
            raise Rejected("provider response rejected")
        if len(set(urls)) != len(urls):
            raise Rejected("provider response rejected")
        if all(url in STUN_URLS for url in urls) and set(entry) == {"urls"}:
            continue
        if set(entry) != {"urls", "username", "credential"} or any(url not in UDP_URLS | OTHER_TURN_URLS for url in urls):
            raise Rejected("provider response rejected")
        if not _secret(entry["username"]) or not _secret(entry["credential"]):
            raise Rejected("provider response rejected")
        udp = tuple(url for url in urls if url in UDP_URLS)
        if selected.intersection(udp):
            raise Rejected("provider response rejected")
        selected.update(udp)
        if udp:
            relays.append(Relay(udp, entry["username"], entry["credential"]))
    if not selected or len(selected) > 2:
        raise Rejected("provider response rejected")
    return ProviderLease(tuple(relays))


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        raise Rejected("provider request failed")


class FixedCloudflareTransport:
    """Explicit operator path only; fixed endpoint, no proxies or retries."""

    def generate(self, key: ProviderKey, ttl: int) -> bytes:
        if not isinstance(key, ProviderKey) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", key.key_id) or not _secret(key.api_token, 2048):
            raise Rejected("key file rejected")
        if type(ttl) is not int or not 1 <= ttl <= MAX_TTL:
            raise Rejected("invalid operator input")
        endpoint = ENDPOINT_PREFIX + key.key_id + ENDPOINT_SUFFIX
        try:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = True
            context.verify_mode = ssl.CERT_REQUIRED
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_default_certs(ssl.Purpose.SERVER_AUTH)
            opener = urllib.request.build_opener(
                urllib.request.ProxyHandler({}), _NoRedirect(), urllib.request.HTTPSHandler(context=context),
            )
            request = urllib.request.Request(
                endpoint, data=json.dumps({"ttl": ttl}, separators=(",", ":")).encode("ascii"),
                headers={"Authorization": "Bearer " + key.api_token, "Content-Type": "application/json", "Accept": "application/json", "User-Agent": USER_AGENT},
                method="POST",
            )
            with opener.open(request, timeout=REQUEST_TIMEOUT) as response:
                if response.status not in (200, 201) or response.geturl() != endpoint:
                    raise Rejected("provider request failed")
                if response.headers.get("Content-Encoding", "identity").lower() != "identity" or response.headers.get("Content-Type", "").partition(";")[0].strip().lower() != "application/json":
                    raise Rejected("provider response rejected")
                length = response.headers.get("Content-Length")
                if length is not None and (not length.isdecimal() or int(length) > MAX_BODY):
                    raise Rejected("provider response rejected")
                body = response.read(MAX_BODY + 1)
                if len(body) > MAX_BODY:
                    raise Rejected("provider response rejected")
                return body
        except Rejected:
            raise
        except Exception:
            raise Rejected("provider request failed") from None


def _open_directory(path: Path) -> int:
    if os.name != "posix" or not hasattr(os, "O_NOFOLLOW"):
        raise Rejected("private file checks unavailable")
    text = os.fspath(path)
    if not text.startswith("/") or len(text) > 4096 or any(p in ("", ".", "..") for p in text[1:].split("/")):
        raise Rejected("private directory rejected")
    current = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in text[1:].split("/"):
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=current)
            os.close(current)
            current = next_fd
        info = os.fstat(current)
        if info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) & 0o077:
            raise Rejected("private directory rejected")
        return current
    except Exception:
        os.close(current)
        raise Rejected("private directory rejected") from None


def _read_private(directory: int, name: str, limit: int) -> bytes:
    fd = -1
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=directory)
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_uid != os.geteuid() or stat.S_IMODE(before.st_mode) & 0o077 or before.st_nlink != 1 or not 0 < before.st_size <= limit:
            raise Rejected("private file rejected")
        chunks: list[bytes] = []
        remaining = limit + 1
        while remaining:
            chunk = os.read(fd, remaining)
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        after = os.fstat(fd)
        data = b"".join(chunks)
        if len(data) != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise Rejected("private file rejected")
        return data
    except Exception:
        raise Rejected("private file rejected") from None
    finally:
        if fd >= 0:
            os.close(fd)


def _publish_no_replace(directory: int, temporary: str, final: str) -> None:
    try:
        rename = ctypes.CDLL(None, use_errno=True).renameat2
        rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        if rename(directory, temporary.encode("ascii"), directory, final.encode("ascii"), 1) != 0:
            if ctypes.get_errno() == errno.EEXIST:
                raise Rejected("issuance already attempted")
            raise Rejected("grant write failed")
    except Rejected:
        raise
    except Exception:
        raise Rejected("grant write failed") from None


def _write_new(directory: int, name: str, value: object) -> None:
    info = os.fstat(directory)
    if info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) & 0o077:
        raise Rejected("private directory rejected")
    data = json.dumps(value, separators=(",", ":"), ensure_ascii=True).encode("ascii") + b"\n"
    if len(data) > MAX_BODY:
        raise Rejected("grant write failed")
    fd = -1
    try:
        temporary = ".relay-publish-" + secrets.token_hex(12)
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=directory)
        offset = 0
        while offset < len(data):
            written = os.write(fd, data[offset:])
            if written <= 0:
                raise Rejected("grant write failed")
            offset += written
        os.fsync(fd)
        _publish_no_replace(directory, temporary, name)
        os.fsync(directory)
    except FileExistsError:
        raise Rejected("issuance already attempted") from None
    except Rejected:
        raise
    except Exception:
        raise Rejected("grant write failed") from None
    finally:
        if fd >= 0:
            os.close(fd)


def _plain_key_value(path: Path, limit: int) -> str:
    if not path.is_absolute() or path.name in ("", ".", ".."):
        raise Rejected("key file rejected")
    directory = _open_directory(path.parent)
    try:
        value = _read_private(directory, path.name, limit)
    finally:
        os.close(directory)
    if value.endswith(b"\r\n"):
        value = value[:-2]
    elif value.endswith(b"\n"):
        value = value[:-1]
    try:
        return value.decode("ascii")
    except UnicodeError:
        raise Rejected("key file rejected") from None


def _load_key(source: KeyFiles) -> ProviderKey:
    paths = _key_paths(source)
    if len(paths) == 2:
        return _checked_key(_plain_key_value(paths[0], 130), _plain_key_value(paths[1], 2050))
    path = paths[0]
    if not path.is_absolute() or path.name in ("", ".", ".."):
        raise Rejected("key file rejected")
    directory = _open_directory(path.parent)
    try:
        value = _decode(_read_private(directory, path.name, 4096), 4096)
    finally:
        os.close(directory)
    if not isinstance(value, dict) or set(value) != {"keyId", "apiToken"}:
        raise Rejected("key file rejected")
    return _checked_key(value["keyId"], value["apiToken"])


def _session_deadline(directory: int, context: str, now: int) -> int:
    metadata = {"schema": 1, "context": context, "issuedAt": now, "expiresAt": now + MAX_TTL}
    try:
        _write_new(directory, "relay-session.json", metadata)
        return now + MAX_TTL
    except Rejected as error:
        if str(error) != "issuance already attempted":
            raise
    existing = _decode(_read_private(directory, "relay-session.json", 1024), 1024)
    if not isinstance(existing, dict) or set(existing) != set(metadata) or type(existing.get("schema")) is not int or existing.get("schema") != 1 or existing.get("context") != context:
        raise Rejected("session context mismatch")
    issued, expires = existing["issuedAt"], existing["expiresAt"]
    if type(issued) is not int or type(expires) is not int or not issued <= now < expires or not 0 < expires - issued <= MAX_TTL:
        raise Rejected("session context mismatch")
    return expires


def issue_local(key_file: KeyFiles, directory: Path, role: str, context: str, ttl: int,
                transport: Transport, clock: Callable[[], int] = lambda: int(time.time()),
                request_deadline: int | None = None, grant_deadline: int | None = None,
                expected_directory: tuple[int, int] | None = None) -> None:
    """One local attempt per role/session; never retry an ambiguous issuance."""
    if role not in ("host", "guest") or not isinstance(context, str) or not re.fullmatch(r"[0-9a-f]{64}", context) or type(ttl) is not int or not 1 <= ttl <= MAX_TTL:
        raise Rejected("invalid operator input")
    if not directory.is_absolute() or any(not path.is_absolute() or path.is_relative_to(directory) for path in _key_paths(key_file)):
        raise Rejected("key file rejected")
    key = _load_key(key_file)
    out = _open_directory(directory)
    try:
        info = os.fstat(out)
        if expected_directory is not None and (info.st_dev, info.st_ino) != expected_directory:
            raise Rejected("private directory rejected")
        try:
            os.stat("relay-" + role + ".json", dir_fd=out, follow_symlinks=False)
        except FileNotFoundError:
            pass
        else:
            raise Rejected("issuance already attempted")
        now = clock()
        if type(now) is not int or not 0 <= now <= 2**63 - MAX_TTL:
            raise Rejected("invalid operator input")
        if request_deadline is not None and (type(request_deadline) is not int or now >= request_deadline):
            raise Rejected("invalid operator input")
        deadline = _session_deadline(out, context, now)
        if grant_deadline is not None:
            if type(grant_deadline) is not int or grant_deadline <= now:
                raise Rejected("invalid operator input")
            deadline = min(deadline, grant_deadline)
        ttl = min(ttl, deadline - now)
        # Reserve and fsync before spending. Failure/crash never clears this receipt.
        _write_new(out, "relay-" + role + ".attempt.json", {"schema": 1, "role": role, "context": context, "issuedAt": now, "expiresAt": now + ttl})
        try:
            lease = parse_provider_response(transport.generate(key, ttl))
        except Exception:
            raise Rejected("provider request failed") from None
        if any(key.api_token in relay.username or key.api_token in relay.credential for relay in lease.relays):
            raise Rejected("provider response rejected")
        after = clock()
        if type(after) is not int or not now <= after < now + ttl:
            raise Rejected("grant expired before writing")
        grant = {
            "schema": 1, "role": role, "context": context, "issuedAt": now, "expiresAt": now + ttl,
            "iceServers": [{"urls": list(relay.urls), "username": relay.username, "credential": relay.credential} for relay in lease.relays],
        }
        _write_new(out, "relay-" + role + ".json", grant)
    finally:
        os.close(out)


def issue_from_request(key_file: KeyFiles, directory: Path, role: str, ttl: int,
                       transport: Transport, clock: Callable[[], int] = lambda: int(time.time()),
                       expected_context: str | None = None,
                       expected_directory: tuple[int, int] | None = None) -> str:
    """Read the actual pending CE context without publishing its reusable secret."""
    if role not in ("host", "guest"):
        raise Rejected("invalid operator input")
    source = _open_directory(directory)
    try:
        info = os.fstat(source)
        request_directory_identity = (info.st_dev, info.st_ino)
        if expected_directory is not None and (info.st_dev, info.st_ino) != expected_directory:
            raise Rejected("private directory rejected")
        request = _decode(_read_private(source, "relay-" + role + ".request.json", 4096), 4096)
    finally:
        os.close(source)
    fields = {"schema", "role", "context", "requestedAt", "requestExpiresAt", "grantExpiresAt"}
    if not isinstance(request, dict) or set(request) != fields or type(request.get("schema")) is not int or request["schema"] != 1 or request["role"] != role or not isinstance(request["context"], str) or not re.fullmatch(r"[0-9a-f]{64}", request["context"]):
        raise Rejected("invalid operator input")
    if expected_context is not None and request["context"] != expected_context:
        raise Rejected("session context mismatch")
    start, expires, grant_expires = (request[k] for k in ("requestedAt", "requestExpiresAt", "grantExpiresAt"))
    now = clock()
    if any(type(n) is not int for n in (start, expires, grant_expires, now)) or not 0 <= start <= now < expires <= grant_expires or expires - start > 180 or grant_expires - start > MAX_TTL:
        raise Rejected("invalid operator input")
    def current_request_time() -> int:
        current = clock()
        if type(current) is not int or not start <= current < expires:
            raise Rejected("invalid operator input")
        return current
    issue_local(key_file, directory, role, request["context"], ttl, transport, current_request_time,
                expires, grant_expires, request_directory_identity)
    return request["context"]


def watch_local(key_file: KeyFiles, directory: Path, ttl: int, seconds: int, transport: Transport,
                clock: Callable[[], int] = lambda: int(time.time()),
                monotonic: Callable[[], float] = time.monotonic,
                pause: Callable[[float], None] = time.sleep) -> int:
    """One directory/context, two fixed roles, no retries or public listener."""
    if type(seconds) is not int or not 1 <= seconds <= 180 or type(ttl) is not int or not 1 <= ttl <= MAX_TTL:
        raise Rejected("invalid operator input")
    source = _open_directory(directory)
    try:
        info = os.fstat(source)
        identity = (info.st_dev, info.st_ino)
        started = monotonic()
        if type(started) not in (int, float) or not math.isfinite(started) or started < 0:
            raise Rejected("invalid operator input")
        last = started
        deadline = started + seconds
        completed: set[str] = set()
        context = None
        def current_time() -> float:
            nonlocal last
            current = monotonic()
            if type(current) not in (int, float) or not math.isfinite(current) or current < last:
                raise Rejected("invalid operator input")
            last = current
            if current >= deadline:
                raise Rejected("local watch expired")
            return current
        while True:
            for role in ("host", "guest"):
                current_time()
                if role in completed:
                    continue
                try:
                    os.stat("relay-" + role + ".request.json", dir_fd=source, follow_symlinks=False)
                except FileNotFoundError:
                    continue
                context = issue_from_request(key_file, directory, role, ttl, transport, clock,
                                             expected_context=context, expected_directory=identity)
                completed.add(role)
            if len(completed) == 2:
                return 2
            pause(min(0.1, deadline - current_time()))
    finally:
        os.close(source)


class _Arguments(argparse.ArgumentParser):
    def error(self, message):
        raise Rejected("invalid operator input")


class _KeyPathOnce(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        if getattr(namespace, self.dest, None) is not None:
            raise Rejected("invalid operator input")
        setattr(namespace, self.dest, values)


def _selected_key_files(args: argparse.Namespace) -> KeyFiles:
    if args.key_file is not None:
        if args.token_id_file is not None or args.api_token_file is not None:
            raise Rejected("invalid operator input")
        return args.key_file
    if args.token_id_file is None or args.api_token_file is None:
        raise Rejected("invalid operator input")
    source = SeparateKeyFiles(args.token_id_file, args.api_token_file)
    _key_paths(source)
    return source


def main(argv: list[str] | None = None) -> int:
    parser = _Arguments(description=__doc__)
    parser.add_argument("--issue", action="store_true", help="explicitly authorize one operator request")
    parser.add_argument("--key-file", type=Path, action=_KeyPathOnce, help="private key JSON outside the grant directory")
    parser.add_argument("--token-id-file", type=Path, action=_KeyPathOnce, help="alternative: private plain-text TURN token ID file")
    parser.add_argument("--api-token-file", type=Path, action=_KeyPathOnce, help="alternative: private plain-text API token file")
    parser.add_argument("--directory", type=Path, required=True, help="existing private session grant directory")
    parser.add_argument("--role", choices=("host", "guest", "both"), required=True)
    parser.add_argument("--watch-seconds", type=int, default=0, help="both roles only: bounded local watch, at most 180 seconds")
    parser.add_argument("--ttl", type=int, default=DEFAULT_TTL,
                        help=f"requested seconds, default {DEFAULT_TTL}, at most {MAX_TTL}")
    try:
        args = parser.parse_args(argv)
        if not args.issue:
            raise Rejected("invalid operator input")
        key_files = _selected_key_files(args)
        if args.role == "both":
            watch_local(key_files, args.directory, args.ttl, args.watch_seconds, FixedCloudflareTransport())
        else:
            if args.watch_seconds:
                raise Rejected("invalid operator input")
            issue_from_request(key_files, args.directory, args.role, args.ttl, FixedCloudflareTransport())
    except Rejected as error:
        print(str(error), file=sys.stderr)
        return 1
    except Exception:
        print("issuance rejected", file=sys.stderr)
        return 1
    print("short-lived local relay grant written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
