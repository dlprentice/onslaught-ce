#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline SQLite contract prototype. No HTTP listener or real auth/provider adapter.

Use invented identities and grants only. The verifier and issuer are injected
fixtures. Authentication, Cloudflare transactions/bindings, deployed key
management and native delivery remain unimplemented deployment requirements.
"""
from __future__ import annotations

import base64
from contextlib import contextmanager
from dataclasses import dataclass, field
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import stat
from threading import RLock
from typing import Callable, Protocol

import ice_pilot_courier as wire
import signaling_parcel_crypto as parcels

AUDIENCE = "onslaught-ce-closed-pilot"
ROUTES = frozenset(("pair/current", "pair/accept", "pair/close", "stage/request", "stage/poll", "stage/record", "stage/ack"))
STAGES = ("lobby", "game")
ROLES = ("host", "guest")


class Refused(Exception):
    def __init__(self):
        super().__init__("offline signaling operation refused")


def require(condition: bool) -> None:
    if not condition:
        raise Refused()


@dataclass(frozen=True)
class Principal:
    """Trusted verifier result, not a client request or authentication mechanism."""
    subject: str = field(repr=False)
    issued_at: int
    expires_at: int
    audience: str = AUDIENCE
    kind: str = "user"


class Verifier(Protocol):
    def verify(self, opaque_token: bytes, now: int) -> Principal | None: ...


@dataclass(frozen=True)
class Issuance:
    attempt: str = field(repr=False)
    role: str
    stage: str
    ttl: int


class FakeIssuer(Protocol):
    """Tests return invented provider-shaped bytes; no implementation is supplied."""
    def issue(self, request: Issuance) -> bytes: ...


@dataclass(frozen=True)
class Reply:
    status: str
    role: str | None = None
    setup_expires_at: int | None = None
    grant_expires_at: int | None = None
    grant: dict | None = field(default=None, repr=False)
    record: bytes | None = field(default=None, repr=False)
    record_digest: str | None = field(default=None, repr=False)
    approval: str | None = field(default=None, repr=False)
    pair_expires_at: int | None = None


@dataclass(frozen=True)
class _Pending:
    pair: str = field(repr=False)
    issuance: Issuance
    request: bytes = field(repr=False)
    issued_at: int
    deadline: int
    lease: int


SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (name TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS budgets (day INTEGER PRIMARY KEY, pairs INTEGER NOT NULL, permits INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS pairs (
 id TEXT PRIMARY KEY, host BLOB NOT NULL, guest BLOB NOT NULL, expires INTEGER NOT NULL,
 closed TEXT NOT NULL DEFAULT '', host_consent INTEGER NOT NULL DEFAULT 0,
 guest_consent INTEGER NOT NULL DEFAULT 0, calls INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS stages (
 pair TEXT NOT NULL REFERENCES pairs(id) ON DELETE CASCADE, stage TEXT NOT NULL,
 context TEXT NOT NULL, deadline INTEGER NOT NULL, PRIMARY KEY(pair,stage));
CREATE TABLE IF NOT EXISTS requests (
 pair TEXT NOT NULL, stage TEXT NOT NULL, role TEXT NOT NULL, payload BLOB NOT NULL,
 last_poll INTEGER NOT NULL DEFAULT -1, PRIMARY KEY(pair,stage,role),
 FOREIGN KEY(pair,stage) REFERENCES stages(pair,stage) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS permits (
 pair TEXT NOT NULL REFERENCES pairs(id) ON DELETE CASCADE, stage TEXT NOT NULL, role TEXT NOT NULL,
 state TEXT NOT NULL DEFAULT 'available', attempt TEXT, lease INTEGER, expires INTEGER,
 grant_payload BLOB, credential_names BLOB, PRIMARY KEY(pair,stage,role));
CREATE TABLE IF NOT EXISTS records (
 pair TEXT NOT NULL, stage TEXT NOT NULL, role TEXT NOT NULL, payload BLOB, digest TEXT NOT NULL,
 expires INTEGER NOT NULL, acked INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(pair,stage,role),
 FOREIGN KEY(pair,stage) REFERENCES stages(pair,stage) ON DELETE CASCADE);
"""


class SessionStore:
    """Fixed offline routes, one active approved pair, two approvals/eight permits/day.

    A single SQLite transaction coordinates this prototype. This is not evidence
    of distributed Durable Object atomicity. Provider reservations are durable
    before the injected call and are never refunded or automatically retried.
    """

    def __init__(self, directory: Path, verifier: Verifier, issuer: FakeIssuer,
                 clock: Callable[[], int], binding_key: bytes, parcel_cipher: parcels.ParcelCipher,
                 grant_seconds: int = wire.private.DEFAULT_TTL):
        self._lock = RLock()
        self._directory = -1
        self._db = None
        self._verifier, self._issuer, self._clock = verifier, issuer, clock
        self._key = binding_key
        self._parcels = parcel_cipher
        self._grant_seconds = grant_seconds
        try:
            require(type(binding_key) is bytes and len(binding_key) == 32)
            require(isinstance(parcel_cipher, parcels.ParcelCipher))
            require(not parcel_cipher.matches_key(binding_key))
            require(type(grant_seconds) is int and 1 <= grant_seconds <= wire.private.MAX_TTL)
            self._path = Path(directory).absolute()
            self._directory = wire.private._open_directory(self._path)
            created = False
            try:
                fd = os.open("signaling.sqlite", os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=self._directory)
                os.close(fd)
                created = True
            except FileExistsError:
                pass
            info = os.stat("signaling.sqlite", dir_fd=self._directory, follow_symlinks=False)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and info.st_nlink == 1 and stat.S_IMODE(info.st_mode) & 0o077 == 0)
            # Anchor SQLite's fixed basename to the already-checked directory.
            filename = "/proc/self/fd/" + str(self._directory) + "/signaling.sqlite"
            target = filename if created else "file:" + filename + "?mode=ro"
            self._db = sqlite3.connect(target, uri=not created, isolation_level=None, timeout=3, check_same_thread=False)
            self._db.row_factory = sqlite3.Row
            self._db.execute("PRAGMA foreign_keys=ON")
            policy = json.dumps({"schema": 2, "key": hashlib.sha256(binding_key).hexdigest(),
                                 "parcelKey": parcel_cipher.identity, "parcelFormat": parcels.MAGIC.decode("ascii"),
                                 "grantSeconds": grant_seconds, "grantCap": wire.private.MAX_TTL}, sort_keys=True)
            if not created:
                # Never convert a plaintext database in place: old journals and
                # free pages could retain its plaintext even after conversion.
                require(self._db.execute("PRAGMA user_version").fetchone()[0] == 2)
                old = self._db.execute("SELECT value FROM meta WHERE name='policy'").fetchone()
                require(old is not None and old[0] == policy)
                self._db.close()
                self._db = sqlite3.connect(filename, isolation_level=None, timeout=3, check_same_thread=False)
                self._db.row_factory = sqlite3.Row
                self._db.execute("PRAGMA foreign_keys=ON")
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.execute("PRAGMA synchronous=FULL")
            if created:
                self._db.executescript(SCHEMA)
                with self._tx() as db:
                    db.execute("INSERT INTO meta VALUES('policy',?)", (policy,))
                    db.execute("INSERT INTO meta VALUES('last_clock','0')")
                    db.execute("INSERT INTO meta VALUES('parcel_store',?)", (secrets.token_hex(16),))
                    db.execute("PRAGMA user_version=2")
            row = self._db.execute("SELECT value FROM meta WHERE name='parcel_store'").fetchone()
            require(row is not None and re.fullmatch(r"[0-9a-f]{32}", row[0]) is not None)
            self._parcel_store = row[0]
        except Exception:
            self.close()
            raise Refused() from None

    def close(self) -> None:
        with self._lock:
            if self._db is not None:
                self._db.close()
                self._db = None
            if self._directory >= 0:
                os.close(self._directory)
                self._directory = -1
            if isinstance(self._parcels, parcels.ParcelCipher):
                self._parcels.close()

    def _associated(self, kind: str, pair: str, stage: str, role: str, context: str,
                    attempt: str = "", expires: int = 0, digest: str = "") -> bytes:
        return wire._json({"schema": 2, "store": self._parcel_store, "purpose": kind,
                           "pair": pair, "stage": stage, "role": role, "context": context,
                           "attempt": attempt, "expires": expires, "digest": digest})

    def _seal(self, payload: bytes, *identity, **metadata) -> bytes:
        return self._parcels.seal(payload, self._associated(*identity, **metadata))

    def _open(self, payload: bytes, *identity, **metadata) -> bytes:
        return self._parcels.open(payload, self._associated(*identity, **metadata))

    @contextmanager
    def _tx(self):
        with self._lock:
            require(self._db is not None)
            wire._same_directory(self._path, self._directory)
            self._db.execute("BEGIN IMMEDIATE")
            try:
                yield self._db
                self._db.execute("COMMIT")
            except BaseException:
                self._db.execute("ROLLBACK")
                raise

    def _subject(self, subject: str) -> bytes:
        require(isinstance(subject, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", subject) is not None)
        return hmac.digest(self._key, (AUDIENCE + "\0" + subject).encode("ascii"), "sha256")

    def _enter(self, db, now: int) -> None:
        require(wire._integer(now))
        previous = int(db.execute("SELECT value FROM meta WHERE name='last_clock'").fetchone()[0])
        require(now >= previous)
        db.execute("UPDATE meta SET value=? WHERE name='last_clock'", (str(now),))
        db.execute("UPDATE pairs SET closed='expired' WHERE expires<=? AND closed=''", (now,))
        db.execute("UPDATE permits SET state='unknown' WHERE state='reserved' AND lease<=?", (now,))
        db.execute("UPDATE permits SET grant_payload=NULL WHERE expires<=? OR pair IN (SELECT id FROM pairs WHERE closed!='') OR EXISTS (SELECT 1 FROM stages WHERE stages.pair=permits.pair AND stages.stage=permits.stage AND deadline<=?)", (now, now))
        db.execute("UPDATE records SET payload=NULL WHERE expires<=? OR pair IN (SELECT id FROM pairs WHERE closed!='') OR EXISTS (SELECT 1 FROM stages WHERE stages.pair=records.pair AND stages.stage=records.stage AND deadline<=?)", (now, now))
        db.execute("DELETE FROM pairs WHERE expires+30<=?", (now,))
        db.execute("DELETE FROM budgets WHERE day<?", (now // 86400 - 1,))

    def approve_pair(self, host: str, guest: str, expires_at: int) -> str:
        """Trusted offline operator seam. There is deliberately no public route."""
        try:
            now = self._clock()
            host_key, guest_key = self._subject(host), self._subject(guest)
            require(host_key != guest_key and wire._integer(expires_at))
            require(now < expires_at <= now + wire.private.MAX_TTL)
            with self._tx() as db:
                self._enter(db, now)
                require(db.execute("SELECT count(*) FROM pairs WHERE closed='' AND expires>?", (now,)).fetchone()[0] == 0)
                day = now // 86400
                db.execute("INSERT OR IGNORE INTO budgets VALUES(?,0,0)", (day,))
                budget = db.execute("SELECT pairs,permits FROM budgets WHERE day=?", (day,)).fetchone()
                require(budget[0] < 2 and budget[1] + 4 <= 8)
                db.execute("UPDATE budgets SET pairs=pairs+1,permits=permits+4 WHERE day=?", (day,))
                pair = secrets.token_hex(16)
                db.execute("INSERT INTO pairs(id,host,guest,expires) VALUES(?,?,?,?)", (pair, host_key, guest_key, expires_at))
                db.executemany("INSERT INTO permits(pair,stage,role) VALUES(?,?,?)", [(pair, stage, role) for stage in STAGES for role in ROLES])
            return pair
        except (Refused, wire.Refused, wire.private.Rejected, sqlite3.Error, OSError, ValueError, TypeError):
            raise Refused() from None

    @staticmethod
    def _complete(db, pair: str, stage: str) -> bool:
        rows = db.execute("SELECT acked FROM records WHERE pair=? AND stage=?", (pair, stage)).fetchall()
        return len(rows) == 2 and all(row[0] for row in rows)

    @staticmethod
    def _fields(body: dict, expected: set[str]) -> None:
        require(set(body) == expected)

    def _stage(self, db, pair, name: str, now: int):
        require(name in STAGES)
        current = db.execute("SELECT * FROM stages WHERE pair=? AND stage=?", (pair["id"], name)).fetchone()
        if current is not None and now >= current["deadline"] and not self._complete(db, pair["id"], name):
            return Reply("setup_expired")
        return current

    def _view(self, db, pair, stage, role: str, now: int) -> Reply:
        own = db.execute("SELECT * FROM permits WHERE pair=? AND stage=? AND role=?", (pair["id"], stage["stage"], role)).fetchone()
        if own["expires"] is not None and now >= own["expires"]:
            return Reply("grant_expired", role, stage["deadline"], own["expires"])
        if own["state"] == "unknown":
            return Reply("issuance_unknown", role, stage["deadline"])
        if own["state"] == "reserved":
            return Reply("issuing", role, stage["deadline"])
        peer = "guest" if role == "host" else "host"
        record = db.execute("SELECT payload,digest,expires FROM records WHERE pair=? AND stage=? AND role=?", (pair["id"], stage["stage"], peer)).fetchone()
        complete = self._complete(db, pair["id"], stage["stage"])
        status = "service_delivery_complete" if complete else "exchanging" if own["state"] == "issued" else "awaiting_peer_request"
        grant = json.loads(self._open(own["grant_payload"], "grant", pair["id"], stage["stage"], role,
                                      stage["context"], attempt=own["attempt"], expires=own["expires"])) if own["grant_payload"] is not None else None
        received = self._open(record[0], "record", pair["id"], stage["stage"], peer, stage["context"],
                              expires=record[2], digest=record[1]) if record and record[0] is not None else None
        return Reply(status, role, stage["deadline"], own["expires"], grant,
                     received, record[1] if record else None,
                     approval=pair["id"], pair_expires_at=pair["expires"])

    def _request(self, db, pair, role: str, body: dict, now: int) -> Reply:
        self._fields(body, {"stage", "request", "afterAuthenticatedStart"})
        name = body["stage"]
        require(name in STAGES and type(body["afterAuthenticatedStart"]) is bool)
        require(body["afterAuthenticatedStart"] == (name == "game"))
        if name == "game":
            require(self._complete(db, pair["id"], "lobby"))
        payload = wire._json(body["request"])
        request = wire._request(payload, role, now)
        current = self._stage(db, pair, name, now)
        if isinstance(current, Reply):
            return current
        if current is None:
            if name == "game":
                prior = db.execute("SELECT context FROM stages WHERE pair=? AND stage='lobby'", (pair["id"],)).fetchone()
                require(prior is not None and prior[0] != request["context"])
            deadline = min(request["requestExpiresAt"], now + 120, pair["expires"])
            db.execute("INSERT INTO stages VALUES(?,?,?,?)", (pair["id"], name, request["context"], deadline))
        else:
            require(current["context"] == request["context"])
        old = db.execute("SELECT payload FROM requests WHERE pair=? AND stage=? AND role=?", (pair["id"], name, role)).fetchone()
        require(old is None or self._open(old[0], "request", pair["id"], name, role, request["context"]) == payload)
        if old is None:
            sealed = self._seal(payload, "request", pair["id"], name, role, request["context"])
            db.execute("INSERT INTO requests(pair,stage,role,payload) VALUES(?,?,?,?)", (pair["id"], name, role, sealed))
        db.execute("UPDATE stages SET deadline=min(deadline,?) WHERE pair=? AND stage=?", (request["requestExpiresAt"], pair["id"], name))
        current = db.execute("SELECT * FROM stages WHERE pair=? AND stage=?", (pair["id"], name)).fetchone()
        return self._view(db, pair, current, role, now)

    def _poll(self, db, pair, role: str, body: dict, now: int):
        self._fields(body, {"stage"})
        stage = self._stage(db, pair, body["stage"], now)
        if isinstance(stage, Reply):
            return stage
        if stage is None:
            return Reply("awaiting_peer_request", role)
        own = db.execute("SELECT * FROM requests WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], role)).fetchone()
        if own is None:
            return Reply("awaiting_peer_request", role, stage["deadline"])
        if own["last_poll"] >= now:
            return Reply("rate_limited", role, stage["deadline"])
        db.execute("UPDATE requests SET last_poll=? WHERE pair=? AND stage=? AND role=?", (now, pair["id"], body["stage"], role))
        count = db.execute("SELECT count(*) FROM requests WHERE pair=? AND stage=?", (pair["id"], body["stage"])).fetchone()[0]
        permit = db.execute("SELECT * FROM permits WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], role)).fetchone()
        if count != 2 or permit["state"] != "available":
            return self._view(db, pair, stage, role, now)
        plaintext = self._open(own["payload"], "request", pair["id"], body["stage"], role, stage["context"])
        request = wire._request(plaintext, role, now, stage["context"])
        require(now < stage["deadline"])
        ttl = min(self._grant_seconds, request["grantExpiresAt"] - now, pair["expires"] - now)
        require(ttl > 0)
        token = secrets.token_hex(16)
        lease = min(now + 10, stage["deadline"])
        changed = db.execute("UPDATE permits SET state='reserved',attempt=?,lease=? WHERE pair=? AND stage=? AND role=? AND state='available'", (token, lease, pair["id"], body["stage"], role)).rowcount
        require(changed == 1)
        return _Pending(pair["id"], Issuance(token, role, body["stage"], ttl), plaintext, now, stage["deadline"], lease)

    def _record(self, db, pair, role: str, body: dict, now: int) -> Reply:
        self._fields(body, {"stage", "record"})
        stage = self._stage(db, pair, body["stage"], now)
        if isinstance(stage, Reply):
            return stage
        require(stage is not None and now < stage["deadline"])
        own = db.execute("SELECT state,expires FROM permits WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], role)).fetchone()
        require(own[0] == "issued" and now < own[1])
        encoded = body["record"]
        require(isinstance(encoded, str) and len(encoded) <= 5600)
        payload = base64.b64decode(encoded, validate=True)
        require(base64.b64encode(payload).decode("ascii") == encoded)
        expires = wire._exchange(payload, role, now)
        digest = hashlib.sha256(payload).hexdigest()
        old = db.execute("SELECT digest FROM records WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], role)).fetchone()
        if old is not None:
            require(old[0] == digest)
            return self._view(db, pair, stage, role, now)
        offer = None
        if role == "guest":
            row = db.execute("SELECT payload,acked,expires,digest FROM records WHERE pair=? AND stage=? AND role='host'", (pair["id"], body["stage"])).fetchone()
            require(row is not None and row[0] is not None and row[1] == 1)
            offer = self._open(row[0], "record", pair["id"], body["stage"], "host", stage["context"], expires=row[2], digest=row[3])
        if offer is not None:
            wire._exchange(payload, role, now, offer)
        sealed = self._seal(payload, "record", pair["id"], body["stage"], role, stage["context"], expires=expires, digest=digest)
        db.execute("INSERT OR IGNORE INTO records(pair,stage,role,payload,digest,expires) VALUES(?,?,?,?,?,?)", (pair["id"], body["stage"], role, sealed, digest, expires))
        return self._view(db, pair, stage, role, now)

    def _ack(self, db, pair, role: str, body: dict, now: int) -> Reply:
        self._fields(body, {"stage", "digest"})
        stage = self._stage(db, pair, body["stage"], now)
        if isinstance(stage, Reply):
            return stage
        require(stage is not None and now < stage["deadline"])
        peer = "guest" if role == "host" else "host"
        row = db.execute("SELECT digest FROM records WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], peer)).fetchone()
        require(row is not None and body["digest"] == row[0])
        db.execute("UPDATE records SET acked=1 WHERE pair=? AND stage=? AND role=?", (pair["id"], body["stage"], peer))
        if self._complete(db, pair["id"], body["stage"]):
            db.execute("UPDATE records SET payload=NULL WHERE pair=? AND stage=?", (pair["id"], body["stage"]))
        return self._view(db, pair, stage, role, now)

    def _issue(self, pending: _Pending) -> Reply:
        grant = None
        try:
            lease = wire.private.parse_provider_response(self._issuer.issue(pending.issuance))
            request = json.loads(pending.request)
            grant = {"schema": 1, "role": pending.issuance.role, "context": request["context"],
                     "issuedAt": pending.issued_at, "expiresAt": pending.issued_at + pending.issuance.ttl,
                     "iceServers": [{"urls": list(item.urls), "username": item.username, "credential": item.credential} for item in lease.relays]}
            wire._grant(wire._json(grant), request, self._clock())
        except Exception:
            grant = None
        with self._tx() as db:
            now = self._clock()
            self._enter(db, now)
            pair = db.execute("SELECT * FROM pairs WHERE id=?", (pending.pair,)).fetchone()
            current = db.execute("SELECT state,attempt FROM permits WHERE pair=? AND stage=? AND role=?", (pending.pair, pending.issuance.stage, pending.issuance.role)).fetchone()
            # A known result that arrives after close/timeout is never delivered,
            # but retain its identifier for a future bounded revocation adapter.
            if grant is not None and current is not None and current[1] == pending.issuance.attempt:
                names = self._seal(wire._json(sorted({entry["username"] for entry in grant["iceServers"]})),
                                   "revocation", pending.pair, pending.issuance.stage, pending.issuance.role,
                                   grant["context"], attempt=pending.issuance.attempt, expires=grant["expiresAt"])
                db.execute("UPDATE permits SET expires=?,credential_names=? WHERE pair=? AND stage=? AND role=?", (grant["expiresAt"], names, pending.pair, pending.issuance.stage, pending.issuance.role))
            usable = grant is not None and pair is not None and pair["closed"] == "" and current is not None and current[0] == "reserved" and current[1] == pending.issuance.attempt and now < min(pending.deadline, pending.lease)
            if not usable:
                db.execute("UPDATE permits SET state='unknown' WHERE pair=? AND stage=? AND role=? AND state='reserved'", (pending.pair, pending.issuance.stage, pending.issuance.role))
                return Reply("issuance_unknown", pending.issuance.role, pending.deadline)
            sealed = self._seal(wire._json(grant), "grant", pending.pair, pending.issuance.stage, pending.issuance.role,
                                grant["context"], attempt=pending.issuance.attempt, expires=grant["expiresAt"])
            db.execute("UPDATE permits SET state='issued',grant_payload=?,expires=?,credential_names=? WHERE pair=? AND stage=? AND role=?", (sealed, grant["expiresAt"], names, pending.pair, pending.issuance.stage, pending.issuance.role))
            stage = db.execute("SELECT * FROM stages WHERE pair=? AND stage=?", (pending.pair, pending.issuance.stage)).fetchone()
            return self._view(db, pair, stage, pending.issuance.role, now)

    def dispatch(self, route: str, opaque_token: bytes, body: bytes) -> Reply:
        """In-process fixed route. Never accepts an identity object from a body."""
        if type(route) is not str or route not in ROUTES:
            return Reply("invalid_request")
        try:
            now = self._clock()
            require(wire._integer(now) and type(opaque_token) is bytes and 0 < len(opaque_token) <= 4096)
            principal = self._verifier.verify(opaque_token, now)
            require(isinstance(principal, Principal) and principal.kind == "user" and principal.audience == AUDIENCE)
            require(wire._integer(principal.issued_at) and wire._integer(principal.expires_at) and principal.issued_at <= now < principal.expires_at)
            subject = self._subject(principal.subject)
        except Exception:
            return Reply("authentication_required")
        try:
            with self._tx() as db:
                self._enter(db, now)
                pair = db.execute("SELECT * FROM pairs WHERE host=? OR guest=? ORDER BY rowid DESC LIMIT 1", (subject, subject)).fetchone()
                if pair is None:
                    return Reply("not_admitted")
                role = "host" if pair["host"] == subject else "guest"
                if pair["closed"]:
                    return Reply(pair["closed"], role)
                # Closing remains possible after the call budget is exhausted.
                if pair["calls"] >= 1024 and route != "pair/close":
                    return Reply("rate_limited", role)
                db.execute("UPDATE pairs SET calls=calls+1 WHERE id=?", (pair["id"],))
                # Invalid authorized requests still spend their call allowance,
                # but cannot leave a partially changed stage or permit behind.
                db.execute("SAVEPOINT operation")
                try:
                    value = wire.private._decode(body, 65536)
                    require(isinstance(value, dict))
                    result = self._operation(db, pair, role, route, value, now)
                except (Refused, parcels.Refused, wire.Refused, wire.private.Rejected, ValueError, TypeError, KeyError, RecursionError):
                    db.execute("ROLLBACK TO operation")
                    result = Reply("invalid_request")
                db.execute("RELEASE operation")
            # No SQLite lock/transaction is held during the injected fake call.
            return self._issue(result) if isinstance(result, _Pending) else result
        except (Refused, parcels.Refused, wire.Refused, wire.private.Rejected, sqlite3.Error, OSError, ValueError, TypeError, KeyError, RecursionError):
            return Reply("invalid_request")

    def _operation(self, db, pair, role: str, route: str, value: dict, now: int):
        # Resolve membership first, then compare this immutable approval. An
        # old in-flight acceptance must never consent to a replacement pair.
        if route != "pair/current":
            require(value.pop("approval", None) == pair["id"])
        if route.startswith("pair/"):
            self._fields(value, set())
            if route == "pair/close":
                db.execute("UPDATE pairs SET closed='closed' WHERE id=?", (pair["id"],))
                db.execute("UPDATE permits SET grant_payload=NULL WHERE pair=?", (pair["id"],))
                db.execute("UPDATE records SET payload=NULL WHERE pair=?", (pair["id"],))
                return Reply("closed", role)
            if route == "pair/accept":
                column = "host_consent" if role == "host" else "guest_consent"
                db.execute("UPDATE pairs SET " + column + "=1 WHERE id=?", (pair["id"],))
                pair = db.execute("SELECT * FROM pairs WHERE id=?", (pair["id"],)).fetchone()
            return Reply("admitted" if pair["host_consent"] and pair["guest_consent"] else "awaiting_pair_consent",
                         role, approval=pair["id"], pair_expires_at=pair["expires"])
        if not (pair["host_consent"] and pair["guest_consent"]):
            return Reply("awaiting_pair_consent", role)
        operation = {"stage/request": self._request, "stage/poll": self._poll,
                     "stage/record": self._record, "stage/ack": self._ack}[route]
        return operation(db, pair, role, value, now)

    def revocation_candidates(self) -> tuple[tuple[str, int], ...]:
        """Trusted offline inspection only; no provider operation is performed."""
        with self._tx() as db:
            now = self._clock()
            self._enter(db, now)
            rows = db.execute("SELECT permits.*,stages.context FROM permits JOIN pairs ON pairs.id=permits.pair JOIN stages ON stages.pair=permits.pair AND stages.stage=permits.stage WHERE (pairs.closed!='' OR permits.state='unknown') AND credential_names IS NOT NULL AND permits.expires>?", (now,)).fetchall()
            return tuple((name, row["expires"]) for row in rows for name in json.loads(self._open(
                row["credential_names"], "revocation", row["pair"], row["stage"], row["role"], row["context"],
                attempt=row["attempt"], expires=row["expires"])))
