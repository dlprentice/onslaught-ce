#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Closed-pair HTTP contract, explicit HTTPS client and Linux private delivery.

No listener, authentication provider or live issuer is supplied. The injected
store owns authentication and durable admission. Client requests use one explicit
HTTPS origin; the fixture transport exercises the same contract without sockets.
"""
from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass, field
import fcntl
import hashlib
import http.client
import os
from pathlib import Path
import re
import select
import ssl
import stat
import sys
import time
from typing import Callable, Protocol
from urllib.parse import urlsplit

import signaling_session_store as service

wire = service.wire
PREFIX = "/ce-signaling/v1/"
MAX_BODY = 65536
HEADERS = (("Content-Type", "application/json"), ("Cache-Control", "no-store"),
           ("Pragma", "no-cache"), ("X-Content-Type-Options", "nosniff"))
STATUSES = frozenset(("authentication_required", "not_admitted", "invalid_request",
                     "rate_limited", "awaiting_pair_consent", "admitted",
                     "awaiting_peer_request", "exchanging", "service_delivery_complete",
                     "issuing", "issuance_unknown", "setup_expired", "grant_expired",
                     "closed", "expired", "unavailable"))
HTTP_CODES = {"authentication_required": 401, "not_admitted": 403,
              "invalid_request": 400, "rate_limited": 429, "unavailable": 503}
FIELDS = {"schema", "status", "role", "setupExpiresAt", "grantExpiresAt", "grant",
          "record", "recordDigest", "approval", "pairExpiresAt"}


class Refused(Exception):
    def __init__(self):
        super().__init__("local signaling operation refused")


def require(value: bool) -> None:
    if not value:
        raise Refused()


def _token(value: bytes) -> str:
    require(type(value) is bytes and 0 < len(value) <= 4096)
    text = value.decode("ascii")
    require(re.fullmatch(r"[A-Za-z0-9._~+/-]+={0,2}", text) is not None)
    return text


@dataclass(frozen=True)
class Request:
    method: str
    path: str
    headers: tuple[tuple[str, str], ...] = field(repr=False)
    body: bytes = field(repr=False)


@dataclass(frozen=True)
class Response:
    code: int
    headers: tuple[tuple[str, str], ...]
    body: bytes = field(repr=False)


class Backend(Protocol):
    def dispatch(self, route: str, opaque_token: bytes, body: bytes) -> service.Reply: ...


class Transport(Protocol):
    """Trusted transport to one separately selected service origin."""
    def send(self, request: Request) -> Response: ...


class HttpsTransport:
    """Direct, verified TLS to one explicit origin; no proxies, cookies or netrc.

    Construction does not make a request. Only send() uses the network. The
    service origin and bearer token are explicit inputs; provider master keys
    have no place in this client. HTTP redirects are never followed.
    """
    def __init__(self, endpoint: str, *, timeout: int = 10):
        try:
            require(type(endpoint) is str and len(endpoint) <= 2048 and endpoint.isascii())
            require(all(33 <= ord(c) <= 126 for c in endpoint))
            require(type(timeout) is int and 1 <= timeout <= 30)
            value = urlsplit(endpoint)
            require(value.scheme == "https" and value.username is None and value.password is None)
            require(value.path in ("", "/") and not value.query and not value.fragment)
            host = value.hostname
            require(host is not None and re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?", host) is not None)
            require(all(part and len(part) <= 63 and not part.startswith("-") and not part.endswith("-") for part in host.split(".")))
            self._host, self._port, self._timeout = host, value.port if value.port is not None else 443, timeout
            require(1 <= self._port <= 65535)
        except Exception:
            raise Refused() from None

    def send(self, request: Request) -> Response:
        connection = None
        try:
            require(type(request) is Request and request.method == "POST")
            require(type(request.path) is str and request.path.startswith(PREFIX) and request.path[len(PREFIX):] in service.ROUTES)
            require(type(request.body) is bytes and len(request.body) <= MAX_BODY)
            require(type(request.headers) is tuple and len(request.headers) == 2)
            headers = dict(request.headers)
            require(set(headers) == {"Authorization", "Content-Type"} and headers["Content-Type"] == "application/json")
            authorization = headers["Authorization"]
            require(type(authorization) is str and authorization.startswith("Bearer "))
            _token(authorization[7:].encode("ascii"))
            context = ssl.create_default_context()
            require(context.check_hostname and context.verify_mode == ssl.CERT_REQUIRED)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            # http.client has no environment-proxy, cookie-jar or netrc lookup.
            connection = http.client.HTTPSConnection(self._host, self._port, timeout=self._timeout, context=context)
            connection.request("POST", request.path, body=request.body, headers=headers)
            response = connection.getresponse()
            require(response.status in {200, 400, 401, 403, 429, 503})
            received = response.getheaders()
            require(type(received) is list and len(received) <= 32)
            normalized = {}
            for name, value in received:
                require(type(name) is str and type(value) is str and len(name) <= 64 and len(value) <= 1024)
                require(name.isascii() and value.isascii() and "\r" not in value and "\n" not in value)
                require(name.lower() not in normalized)
                normalized[name.lower()] = value
            require(sum(len(n) + len(v) for n, v in normalized.items()) <= 8192)
            require(all(normalized.get(name.lower()) == value for name, value in HEADERS))
            require("location" not in normalized)
            length = normalized.get("content-length")
            if length is not None:
                require(length.isdecimal() and len(length) <= 6 and 0 <= int(length) <= MAX_BODY)
                require("transfer-encoding" not in normalized)
            if "transfer-encoding" in normalized:
                require(normalized["transfer-encoding"].lower() == "chunked")
            payload = response.read(MAX_BODY + 1)
            require(type(payload) is bytes and len(payload) <= MAX_BODY)
            require(length is None or len(payload) == int(length))
            return Response(response.status, HEADERS, payload)
        except Exception:
            raise Refused() from None
        finally:
            if connection is not None:
                try:
                    connection.close()
                except Exception:
                    pass


def _response(reply: service.Reply) -> Response:
    require(type(reply) is service.Reply and reply.status in STATUSES)
    data = {"schema": 1, "status": reply.status}
    for public, member in (("role", "role"), ("setupExpiresAt", "setup_expires_at"),
                           ("grantExpiresAt", "grant_expires_at"), ("grant", "grant"),
                           ("recordDigest", "record_digest"), ("approval", "approval"),
                           ("pairExpiresAt", "pair_expires_at")):
        value = getattr(reply, member)
        if value is not None:
            data[public] = value
    if reply.record is not None:
        require(type(reply.record) is bytes and len(reply.record) <= wire.MAX_RECORD)
        data["record"] = base64.b64encode(reply.record).decode("ascii")
    body = wire._json(data)
    require(len(body) <= MAX_BODY)
    return Response(HTTP_CODES.get(reply.status, 200), HEADERS, body)


class Api:
    """Fixed POST entry seam; only the store's verifier can create a principal."""
    def __init__(self, backend: Backend):
        self._backend = backend

    def handle(self, request: Request) -> Response:
        try:
            require(type(request) is Request and request.method == "POST")
            require(type(request.path) is str and request.path.startswith(PREFIX))
            route = request.path[len(PREFIX):]
            require(route in service.ROUTES)
            require(type(request.body) is bytes and len(request.body) <= MAX_BODY)
            require(type(request.headers) is tuple and len(request.headers) == 2)
            headers = {}
            for item in request.headers:
                require(type(item) is tuple and len(item) == 2)
                name, value = item
                require(type(name) is str and type(value) is str and len(name) <= 32 and len(value) <= 4103)
                require(name.isascii() and value.isascii() and "\r" not in value and "\n" not in value)
                require(name.lower() not in headers)
                headers[name.lower()] = value
            require(set(headers) == {"authorization", "content-type"})
            require(headers["content-type"] == "application/json")
            authorization = headers["authorization"]
            require(authorization.startswith("Bearer "))
            token = authorization[7:].encode("ascii")
            _token(token)
        except Exception:
            return _response(service.Reply("invalid_request"))
        try:
            # Authentication, membership and consent precede JSON parsing and
            # permit allocation in the frozen store; no body identity is trusted.
            return _response(self._backend.dispatch(route, token, request.body))
        except Exception:
            return _response(service.Reply("unavailable"))


class WsgiApplication:
    """Callable server entry seam; it never starts a server or configures TLS.

    The host must supply a trustworthy HTTPS WSGI environment and request/time
    limits. Forwarded identity headers and forwarded scheme claims are ignored.
    Host and Content-Length belong to the server; only the two contract headers
    are forwarded to Api. Bearer verification remains in its configured store.
    """
    def __init__(self, endpoint: Api):
        self._endpoint = endpoint

    def __call__(self, environ, start_response):
        try:
            require(environ.get("wsgi.url_scheme") == "https" and not environ.get("QUERY_STRING"))
            length = environ.get("CONTENT_LENGTH", "")
            require(type(length) is str and length.isdecimal() and len(length) <= 6 and int(length) <= MAX_BODY)
            body = environ["wsgi.input"].read(int(length))
            require(type(body) is bytes and len(body) == int(length))
            request = Request(environ.get("REQUEST_METHOD"), environ.get("PATH_INFO"),
                              (("Authorization", environ.get("HTTP_AUTHORIZATION", "")),
                               ("Content-Type", environ.get("CONTENT_TYPE", ""))), body)
            response = self._endpoint.handle(request)
        except Exception:
            response = _response(service.Reply("invalid_request"))
        labels = {200: "OK", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden",
                  429: "Too Many Requests", 503: "Service Unavailable"}
        start_response(str(response.code) + " " + labels[response.code],
                       list(response.headers) + [("Content-Length", str(len(response.body)))])
        return [response.body]


@dataclass(frozen=True)
class View:
    """Only fixed status, assigned role and absolute deadlines reach the caller."""
    status: str
    role: str | None = None
    setup_expires_at: int | None = None
    grant_expires_at: int | None = None
    pair_expires_at: int | None = None


def _view(reply: service.Reply, status: str | None = None) -> View:
    return View(status or reply.status, reply.role, reply.setup_expires_at,
                reply.grant_expires_at, reply.pair_expires_at)


def _decode(response: Response) -> service.Reply:
    require(type(response) is Response and type(response.code) is int)
    require(response.headers == HEADERS and type(response.body) is bytes)
    data = wire.private._decode(response.body, MAX_BODY)
    require(type(data) is dict and {"schema", "status"} <= set(data) <= FIELDS)
    require(type(data["schema"]) is int and data["schema"] == 1 and data["status"] in STATUSES)
    require(response.code == HTTP_CODES.get(data["status"], 200))
    role = data.get("role")
    require(role is None or role in service.ROLES)
    for name in ("setupExpiresAt", "grantExpiresAt", "pairExpiresAt"):
        require(name not in data or wire._integer(data[name]))
    approval, digest = data.get("approval"), data.get("recordDigest")
    require(approval is None or type(approval) is str and re.fullmatch(r"[0-9a-f]{32}", approval) is not None)
    require(digest is None or type(digest) is str and re.fullmatch(r"[0-9a-f]{64}", digest) is not None)
    record = None
    if "record" in data:
        encoded = data["record"]
        require(type(encoded) is str and len(encoded) <= 5600)
        record = base64.b64decode(encoded, validate=True)
        require(base64.b64encode(record).decode("ascii") == encoded and len(record) <= wire.MAX_RECORD)
        require(hashlib.sha256(record).hexdigest() == digest)
    grant = data.get("grant")
    require(grant is None or type(grant) is dict)
    if grant is not None or record is not None:
        require(data["status"] in ("exchanging", "service_delivery_complete"))
        require(role is not None and "setupExpiresAt" in data and "grantExpiresAt" in data)
    return service.Reply(data["status"], role, data.get("setupExpiresAt"), data.get("grantExpiresAt"),
                         grant, record, digest, approval, data.get("pairExpiresAt"))


class Client:
    """One explicit consent to one immutable operator-approved pair; no retry loop."""
    def __init__(self, transport: Transport, token: bytes, clock: Callable[[], int]):
        try:
            _token(token)
        except Exception:
            raise Refused() from None
        self._transport, self._token, self._clock = transport, token, clock
        self._approval = self._role = self._pair_expires = None
        self._last_clock = None

    def _now(self) -> int:
        now = wire._clock(self._clock, self._last_clock)
        self._last_clock = now
        return now

    def _call(self, route: str, body: dict) -> service.Reply:
        try:
            require(route in service.ROUTES)
            self._now()
            if route != "pair/current":
                require(self._approval is not None)
                body = {"approval": self._approval} | body
            response = self._transport.send(Request("POST", PREFIX + route,
                (("Authorization", "Bearer " + _token(self._token)), ("Content-Type", "application/json")), wire._json(body)))
            result = _decode(response)
            now = self._now()
            require(result.role is None or self._role is None or result.role == self._role)
            require(result.approval is None or self._approval is None or result.approval == self._approval)
            require(result.pair_expires_at is None or self._pair_expires is None or result.pair_expires_at == self._pair_expires)
            if result.approval is not None:
                require(result.role is not None and result.pair_expires_at is not None and now < result.pair_expires_at)
            return result
        except Exception:
            raise Refused() from None

    def current(self) -> View:
        reply = self._call("pair/current", {})
        require(reply.grant is None and reply.record is None)
        if reply.approval is not None:
            self._approval, self._role, self._pair_expires = reply.approval, reply.role, reply.pair_expires_at
        return _view(reply)

    def accept(self) -> View:
        return _view(self._call("pair/accept", {}))

    def close_pair(self) -> View:
        return _view(self._call("pair/close", {}))

    def stage(self, stage: str, directory: Path, *, after_authenticated_start: bool = False) -> StageClient:
        return StageClient(self, stage, directory, after_authenticated_start)


class StageClient:
    """One already-selected Linux directory, request and absolute setup window.

    Publication is atomic per file. For a guest, the peer offer is published
    before its grant, so CE cannot start on a grant without the offered attempt.
    CE remains responsible for authenticating the encrypted record itself.
    """
    def __init__(self, client: Client, stage: str, directory: Path, after_start: bool):
        self._directory = -1
        try:
            require(sys.platform.startswith("linux"))
            require(stage in service.STAGES and type(after_start) is bool and after_start == (stage == "game"))
            require(client._approval is not None and client._role in service.ROLES)
            self._client, self._stage, self._after_start = client, stage, after_start
            self._role = client._role
            self._path = Path(directory).absolute()
            self._directory = wire.private._open_directory(self._path)
            self._name = "relay-" + self._role + ".request.json"
            self._frozen = wire._read(self._directory, self._name)
            self._request = wire._request(self._frozen, self._role, client._now())
            self._deadline = min(self._request["requestExpiresAt"], client._pair_expires)
        except Exception:
            self.close()
            raise Refused() from None

    def close(self) -> None:
        if self._directory >= 0:
            os.close(self._directory)
            self._directory = -1

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _check(self) -> int:
        require(self._directory >= 0)
        now = self._client._now()
        require(now < self._deadline)
        wire._same_directory(self._path, self._directory)
        require(wire._read(self._directory, self._name) == self._frozen)
        wire._request(self._frozen, self._role, now)
        return now

    def _deliver(self, reply: service.Reply) -> View:
        now = self._check()
        require(reply.role is None or reply.role == self._role)
        if reply.setup_expires_at is not None:
            require(reply.setup_expires_at <= self._request["requestExpiresAt"])
            self._deadline = min(self._deadline, reply.setup_expires_at)
            require(now < self._deadline)
        grant, record = reply.grant, reply.record
        if grant is None and record is None:
            return _view(reply)
        require(reply.role == self._role and reply.setup_expires_at is not None)
        grant_name = "relay-" + self._role + ".json"
        if grant is not None:
            grant = wire._grant(wire._json(grant), self._request, now)
            require(reply.grant_expires_at == grant["expiresAt"])
        else:
            grant = wire._grant(wire._read(self._directory, grant_name), self._request, now)
        self._deadline = min(self._deadline, grant["expiresAt"])
        peer_kind = "offer" if self._role == "guest" else "answer"
        record_name = wire._record_name(self._request["context"], peer_kind)
        outputs = []
        if record is not None:
            offer = wire._read(self._directory, wire._record_name(self._request["context"], "offer")) if self._role == "host" else None
            self._deadline = min(self._deadline, wire._exchange(record, "host" if self._role == "guest" else "guest", now, offer))
            outputs.append((record_name, record))
        elif self._role == "guest":
            try:
                os.stat(record_name, dir_fd=self._directory, follow_symlinks=False)
            except FileNotFoundError:
                return _view(reply, "awaiting_peer_record")
            offer = wire._read(self._directory, record_name)
            self._deadline = min(self._deadline, wire._exchange(offer, "host", now))
        outputs.append((grant_name, wire._json(grant)))
        # Refuse all conflicting targets before publishing even the first file.
        for name, payload in outputs:
            wire._existing(self._directory, name, payload)
        for name, payload in outputs:
            self._check()
            wire._publish(self._directory, name, payload)
        self._check()
        if record is not None:
            acknowledged = self._client._call("stage/ack", {"stage": self._stage, "digest": reply.record_digest})
            require(acknowledged.status in ("exchanging", "service_delivery_complete"))
            return _view(acknowledged)
        return _view(reply)

    def _run(self, operation: str) -> View:
        try:
            fcntl.flock(self._directory, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self._check()
            if operation == "request":
                body = {"stage": self._stage, "request": self._request, "afterAuthenticatedStart": self._after_start}
            elif operation == "poll":
                body = {"stage": self._stage}
            else:
                kind = "offer" if self._role == "host" else "answer"
                payload = wire._read(self._directory, wire._record_name(self._request["context"], kind))
                offer = wire._read(self._directory, wire._record_name(self._request["context"], "offer")) if self._role == "guest" else None
                wire._exchange(payload, self._role, self._check(), offer)
                body = {"stage": self._stage, "record": base64.b64encode(payload).decode("ascii")}
            return self._deliver(self._client._call("stage/" + operation, body))
        except Exception:
            raise Refused() from None
        finally:
            if self._directory >= 0:
                fcntl.flock(self._directory, fcntl.LOCK_UN)

    def request(self) -> View:
        return self._run("request")

    def poll(self) -> View:
        return self._run("poll")

    def publish(self) -> View:
        return self._run("record")


def _identity_token(fd: int) -> bytes:
    """Consume only an explicitly supplied private file/pipe descriptor."""
    require(type(fd) is int and fd >= 0)
    info = os.fstat(fd)
    require(info.st_uid == os.geteuid() and stat.S_IMODE(info.st_mode) & 0o077 == 0)
    require((stat.S_ISREG(info.st_mode) and info.st_nlink == 1) or stat.S_ISFIFO(info.st_mode))
    deadline = time.monotonic() + 5
    result = b""
    while len(result) <= 4096:
        remaining = deadline - time.monotonic()
        require(remaining > 0 and bool(select.select([fd], [], [], remaining)[0]))
        chunk = os.read(fd, 4097 - len(result))
        if not chunk:
            break
        result += chunk
    _token(result)
    return result


def _restore_pair_binding(directory: Path, client: Client) -> None:
    selected = -1
    try:
        path = Path(directory).absolute()
        selected = wire.private._open_directory(path)
        try:
            os.stat("signaling-pair.json", dir_fd=selected, follow_symlinks=False)
        except FileNotFoundError:
            return
        value = wire.private._decode(wire.private._read_private(selected, "signaling-pair.json", 1024), 1024)
        require(type(value) is dict and set(value) == {"schema", "approval", "role", "expiresAt"})
        require(type(value["schema"]) is int and value["schema"] == 1)
        require(type(value["approval"]) is str and re.fullmatch(r"[0-9a-f]{32}", value["approval"]) is not None)
        require(value["role"] in service.ROLES and wire._integer(value["expiresAt"]))
        require(client._now() < value["expiresAt"])
        wire._same_directory(path, selected)
        client._approval, client._role, client._pair_expires = value["approval"], value["role"], value["expiresAt"]
    except Exception:
        raise Refused() from None
    finally:
        if selected >= 0:
            os.close(selected)


def _pair_binding(directory: Path, client: Client, stage_directory: Path | None) -> None:
    """Persist no token: only the immutable pair approval selected by this user.

    An explicit separate directory prevents a restarted CLI invocation from
    silently adopting a replacement pair. A replacement uses a fresh directory.
    The CE setup directory retains its existing fixed record names.
    """
    selected = other = -1
    try:
        path = Path(directory).absolute()
        selected = wire.private._open_directory(path)
        if stage_directory is not None:
            other = wire.private._open_directory(Path(stage_directory).absolute())
            a, b = os.fstat(selected), os.fstat(other)
            require((a.st_dev, a.st_ino) != (b.st_dev, b.st_ino))
        fcntl.flock(selected, fcntl.LOCK_EX | fcntl.LOCK_NB)
        require(client._approval is not None and client._role in service.ROLES)
        value = {"schema": 1, "approval": client._approval, "role": client._role,
                 "expiresAt": client._pair_expires}
        payload = wire._json(value)
        wire._same_directory(path, selected)
        wire._publish(selected, "signaling-pair.json", payload)
        wire._same_directory(path, selected)
    except Exception:
        raise Refused() from None
    finally:
        if other >= 0:
            os.close(other)
        if selected >= 0:
            os.close(selected)


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refused()


def main(argv: list[str] | None = None) -> int:
    """One finite explicit action. No ambient credential or endpoint discovery."""
    try:
        parser = _Parser(description=__doc__)
        parser.add_argument("operation", choices=("current", "accept", "close", "request", "poll", "publish"))
        parser.add_argument("--endpoint", required=True, help="One approved HTTPS origin, with no path or query.")
        parser.add_argument("--identity-token-fd", type=int, required=True, help="Explicit private file/pipe fd containing an unadorned bearer token.")
        parser.add_argument("--client-directory", type=Path, required=True, help="Existing private pair-binding directory, separate from the CE setup directory.")
        parser.add_argument("--stage", choices=service.STAGES)
        parser.add_argument("--directory", type=Path)
        parser.add_argument("--after-authenticated-start", action="store_true")
        args = parser.parse_args(argv)
        stage_operation = args.operation in ("request", "poll", "publish")
        require((args.stage is not None and args.directory is not None) if stage_operation else
                (args.stage is None and args.directory is None and not args.after_authenticated_start))
        transport = HttpsTransport(args.endpoint)
        token = _identity_token(args.identity_token_fd)
        client = Client(transport, token, lambda: int(time.time()))
        _restore_pair_binding(args.client_directory, client)
        view = client.current()
        if client._approval is not None:
            _pair_binding(args.client_directory, client, args.directory)
        if args.operation == "accept":
            view = client.accept()
        elif args.operation == "close" and view.status not in ("closed", "expired"):
            view = client.close_pair()
        elif stage_operation:
            with client.stage(args.stage, args.directory, after_authenticated_start=args.after_authenticated_start) as stage:
                view = getattr(stage, args.operation)()
        # Omit approval handles, tokens, provider data, record digests and paths.
        print(wire._json({"status": view.status, "role": view.role,
                          "setupExpiresAt": view.setup_expires_at,
                          "grantExpiresAt": view.grant_expires_at,
                          "pairExpiresAt": view.pair_expires_at}).decode("ascii"), end="")
        return 0 if view.status in {"awaiting_pair_consent", "admitted", "awaiting_peer_request",
                                    "awaiting_peer_record", "exchanging", "issuing",
                                    "service_delivery_complete", "closed"} else 1
    except Exception:
        print("local signaling operation refused", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
