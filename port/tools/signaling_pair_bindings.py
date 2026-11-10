#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Trusted accountless pair bootstrap and private fixed-provider issuer seams.

No listener, public mint, key loader or deployment is supplied. Callers supply
server-only keys explicitly; fixtures use authored values and a fake provider.
The existing SessionStore remains the durable membership and permit authority.
"""
from __future__ import annotations

import base64
from dataclasses import dataclass, field
import fcntl
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
from typing import Callable

import signaling_session_store as service

wire = service.wire
private = wire.private
PREFIX = b"CEPAIR1"
DOMAIN = b"Onslaught CE pair capability v1\0"
MAX_TOKEN = 1024
FIELDS = {"schema", "audience", "subject", "role", "issuedAt", "expiresAt"}
TOKEN_FILE = "identity-token"
ADMISSION_FILE = "pair-admission.json"


class Refused(Exception):
    def __init__(self):
        super().__init__("pair binding refused")


def require(value: bool) -> None:
    if not value:
        raise Refused()


def _encode(value: bytes) -> bytes:
    return base64.urlsafe_b64encode(value).rstrip(b"=")


def _decode(value: bytes, maximum: int) -> bytes:
    require(type(value) is bytes and 0 < len(value) <= maximum * 2)
    result = base64.b64decode(value + b"=" * (-len(value) % 4), altchars=b"-_", validate=True)
    require(len(result) <= maximum and _encode(result) == value)
    return result


def _subject(value: object, role: object) -> bool:
    return (role in service.ROLES and type(value) is str and
            re.fullmatch(r"p_[0-9a-f]{32}_" + str(role), value) is not None)


def _payload(value: dict) -> bytes:
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("ascii")


class PairCapabilityVerifier:
    """A bearer proves possession of a role-scoped capability, not an account.

    The admission signing key must be distinct from provider, CE join, identity
    binding and parcel keys. It is a server-only explicit configuration input.
    Fresh subjects are generated for every approved pair. Actual membership and
    assigned role are resolved by SessionStore, never by a request-body claim.
    """
    def __init__(self, signing_key: bytes, *, maximum_seconds: int = private.DEFAULT_TTL):
        try:
            require(type(signing_key) is bytes and len(signing_key) == 32)
            require(type(maximum_seconds) is int and 1 <= maximum_seconds <= private.MAX_TTL)
            self._key, self.maximum_seconds = signing_key, maximum_seconds
        except Exception:
            raise Refused() from None

    def _fields(self, value: dict) -> None:
        require(type(value) is dict and set(value) == FIELDS)
        require(type(value["schema"]) is int and value["schema"] == 1)
        require(value["audience"] == service.AUDIENCE and _subject(value["subject"], value["role"]))
        start, end = value["issuedAt"], value["expiresAt"]
        require(wire._integer(start) and wire._integer(end) and start < end <= start + self.maximum_seconds)

    def mint(self, subject: str, role: str, issued_at: int, expires_at: int) -> bytes:
        """Trusted operator call only; there is no corresponding public route."""
        try:
            value = {"schema": 1, "audience": service.AUDIENCE, "subject": subject,
                     "role": role, "issuedAt": issued_at, "expiresAt": expires_at}
            self._fields(value)
            payload = _payload(value)
            signature = hmac.digest(self._key, DOMAIN + payload, "sha256")
            token = b".".join((PREFIX, _encode(payload), _encode(signature)))
            require(len(token) <= MAX_TOKEN)
            return token
        except Exception:
            raise Refused() from None

    def verify(self, opaque_token: bytes, now: int) -> service.Principal | None:
        try:
            require(wire._integer(now) and type(opaque_token) is bytes and 0 < len(opaque_token) <= MAX_TOKEN)
            parts = opaque_token.split(b".")
            require(len(parts) == 3 and parts[0] == PREFIX)
            payload, signature = _decode(parts[1], 512), _decode(parts[2], 32)
            require(len(signature) == 32)
            require(hmac.compare_digest(signature, hmac.digest(self._key, DOMAIN + payload, "sha256")))
            # Invalid MACs never reach JSON parsing or principal creation.
            value = private._decode(payload, 512)
            self._fields(value)
            require(_payload(value) == payload and value["issuedAt"] <= now < value["expiresAt"])
            return service.Principal(value["subject"], value["issuedAt"], value["expiresAt"])
        except Exception:
            return None


@dataclass(frozen=True)
class RoleAdmission:
    approval: str = field(repr=False)
    subject: str = field(repr=False)
    role: str
    issued_at: int
    expires_at: int
    token: bytes = field(repr=False)


@dataclass(frozen=True)
class PairAdmission:
    host: RoleAdmission = field(repr=False)
    guest: RoleAdmission = field(repr=False)


def prepare_pair(store: service.SessionStore, verifier: PairCapabilityVerifier,
                 clock: Callable[[], int], *, seconds: int = private.DEFAULT_TTL) -> PairAdmission:
    """One trusted approval, followed by two role capabilities; never retries.

    approve_pair commits the existing four-permit/global-budget reservation
    first. A later failure leaves that reservation spent. Keep the returned
    private bundle for identical export retries; never mint a replacement as a
    response-loss recovery action. No provider request occurs in this function.
    """
    try:
        require(type(verifier) is PairCapabilityVerifier)
        require(type(seconds) is int and 1 <= seconds <= verifier.maximum_seconds)
        now = wire._clock(clock)
        expires = now + seconds
        require(wire._integer(expires))
        nonce = secrets.token_hex(16)
        require(re.fullmatch(r"[0-9a-f]{32}", nonce) is not None)
        subjects = {role: "p_" + nonce + "_" + role for role in service.ROLES}
        approval = store.approve_pair(subjects["host"], subjects["guest"], expires)
        require(type(approval) is str and re.fullmatch(r"[0-9a-f]{32}", approval) is not None)
        admissions = {}
        for role in service.ROLES:
            token = verifier.mint(subjects[role], role, now, expires)
            # Round-trip the configured verifier and actual membership owner.
            # A wrong verifier/role/expiry configuration burns, never refunds.
            reply = store.dispatch("pair/current", token, b"{}")
            require(reply.status == "awaiting_pair_consent" and reply.role == role and
                    reply.approval == approval and reply.pair_expires_at == expires)
            admissions[role] = RoleAdmission(approval, subjects[role], role, now, expires, token)
        return PairAdmission(admissions["host"], admissions["guest"])
    except Exception:
        raise Refused() from None


def export_role(admission: PairAdmission, role: str, directory: Path,
                clock: Callable[[], int]) -> None:
    """Export only one role to its explicitly selected existing private folder.

    No token is printed, placed in a URL or written to an ambient location.
    Metadata and token publication are individually atomic and never replace a
    file. Identical retries reuse the original bytes and original expiry.
    """
    selected = -1
    try:
        require(type(admission) is PairAdmission and role in service.ROLES)
        member = admission.host if role == "host" else admission.guest
        require(type(member) is RoleAdmission and member.role == role and _subject(member.subject, role))
        require(type(member.token) is bytes and 0 < len(member.token) <= MAX_TOKEN)
        require(type(member.approval) is str and re.fullmatch(r"[0-9a-f]{32}", member.approval) is not None)
        now = wire._clock(clock)
        require(member.issued_at <= now < member.expires_at)
        path = Path(directory).absolute()
        selected = private._open_directory(path)
        fcntl.flock(selected, fcntl.LOCK_EX | fcntl.LOCK_NB)
        metadata = wire._json({"schema": 1, "approval": member.approval, "role": role,
                               "issuedAt": member.issued_at, "expiresAt": member.expires_at})
        outputs = ((ADMISSION_FILE, metadata), (TOKEN_FILE, member.token))
        for name, payload in outputs:
            wire._existing(selected, name, payload)
        for name, payload in outputs:
            wire._clock(clock, now, member.expires_at)
            wire._same_directory(path, selected)
            wire._publish(selected, name, payload)
        wire._clock(clock, now, member.expires_at)
        wire._same_directory(path, selected)
    except Exception:
        raise Refused() from None
    finally:
        if selected >= 0:
            os.close(selected)


class FixedProviderIssuer:
    """Private in-process binding for SessionStore's already reserved permit.

    The default transport has the existing fixed Cloudflare endpoint, certificate
    checks, bounds and no retry/proxy policy. A fake transport may be injected
    for authored checks. No caller-selected provider/key/URL or key loading is
    exposed by the player API. This is not a deployed private Worker binding.
    """
    def __init__(self, key: private.ProviderKey, transport: private.Transport | None = None,
                 *, maximum_seconds: int = private.DEFAULT_TTL):
        try:
            require(type(key) is private.ProviderKey)
            require(type(maximum_seconds) is int and 1 <= maximum_seconds <= private.MAX_TTL)
            self._key = private._checked_key(key.key_id, key.api_token)
            self._transport = transport if transport is not None else private.FixedCloudflareTransport()
            self.maximum_seconds = maximum_seconds
        except Exception:
            raise Refused() from None

    def issue(self, request: service.Issuance) -> bytes:
        try:
            require(type(request) is service.Issuance and request.role in service.ROLES and request.stage in service.STAGES)
            require(type(request.attempt) is str and re.fullmatch(r"[0-9a-f]{32}", request.attempt) is not None)
            require(type(request.ttl) is int and 1 <= request.ttl <= self.maximum_seconds)
            lease = private.parse_provider_response(self._transport.generate(self._key, request.ttl))
            require(not any(self._key.api_token in relay.username or self._key.api_token in relay.credential for relay in lease.relays))
            return wire._json({"iceServers": [{"urls": list(relay.urls), "username": relay.username,
                                               "credential": relay.credential} for relay in lease.relays]})
        except Exception:
            raise Refused() from None
