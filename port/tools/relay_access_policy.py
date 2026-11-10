#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline access-policy experiment, with no authentication or provider adapter.

VerifiedPrincipal must come from a trusted server-side verifier, never from a
client's identity claims. IDs are server-owned handles, not generated tokens.
The injected issuer used by the accompanying tests returns invented data only.
See port/network/RELAY-ACCESS.md for the integration boundary and limits.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from threading import RLock
from typing import Protocol


AUDIENCE = "onslaught-ce-relay"


def _integer(value: object) -> bool:
    return type(value) is int and value >= 0


def _identifier(value: object) -> bool:
    return (
        isinstance(value, str)
        and 1 <= len(value) <= 64
        and all(c in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in value)
    )


@dataclass(frozen=True)
class Limits:
    """Authored experiment limits; these are not a hosted-service capacity plan."""

    authentication_seconds: int = 120
    invitation_seconds: int = 120
    credential_seconds: int = 600
    session_seconds: int = 600
    window_seconds: int = 60
    principal_attempts: int = 12
    global_attempts: int = 120
    sessions: int = 64
    invitations: int = 128
    principals: int = 256

    def __post_init__(self) -> None:
        ceilings = {
            "authentication_seconds": 300,
            "invitation_seconds": 300,
            "credential_seconds": 600,
            "session_seconds": 3600,
            "window_seconds": 300,
            "principal_attempts": 1000,
            "global_attempts": 1000,
            "sessions": 128,
            "invitations": 256,
            "principals": 256,
        }
        for name, ceiling in ceilings.items():
            value = getattr(self, name)
            if not _integer(value) or not 1 <= value <= ceiling:
                raise ValueError(f"{name} is outside the experiment bounds")
        if max(self.invitation_seconds, self.credential_seconds) > self.session_seconds:
            raise ValueError("invitations and credentials must fit inside a session")


@dataclass(frozen=True)
class VerifiedPrincipal:
    """Trusted verifier output. Constructing this object does not authenticate."""

    subject: str
    issued_at: int
    expires_at: int
    audience: str = AUDIENCE


@dataclass(frozen=True)
class CredentialGrant:
    session: str
    subject: str
    invitation: str = field(repr=False)
    issued_at: int
    expires_at: int


@dataclass(frozen=True)
class IssuedCredential:
    grant: CredentialGrant
    payload: object = field(repr=False)


class Issuer(Protocol):
    def issue(self, grant: CredentialGrant) -> IssuedCredential: ...


class Code(str, Enum):
    ALLOWED = "allowed"
    DENIED = "denied"
    UNAUTHENTICATED = "unauthenticated"
    INVALID = "invalid_request"
    RATE_LIMITED = "rate_limited"
    CAPACITY = "capacity"
    CLOCK = "clock_error"
    ISSUER_FAILED = "issuer_failed"


@dataclass(frozen=True)
class Decision:
    code: Code
    expires_at: int | None = None
    retry_at: int | None = None
    credential: IssuedCredential | None = field(default=None, repr=False)

    @property
    def allowed(self) -> bool:
        return self.code is Code.ALLOWED


@dataclass(frozen=True)
class Usage:
    sessions: int
    invitations: int
    principals: int
    global_attempts: int
    principal_attempts: int


@dataclass
class _Session:
    owner: str
    expires_at: int
    members: set[str]
    invited: set[str] = field(default_factory=set)
    closed: bool = False


@dataclass
class _Invitation:
    session: str
    subject: str
    expires_at: int
    used: bool = False


class AccessPolicy:
    """One in-memory authority, serialized through a fake issuer invocation.

    Every attempt, including authentication failures, spends the global budget.
    Valid identities also spend a per-principal budget before admission checks.
    A failed issuer attempt burns the invitation: there is no automatic retry.
    """

    def __init__(self, issuer: Issuer, limits: Limits = Limits()) -> None:
        self.limits = limits
        self._issuer = issuer
        self._lock = RLock()
        self._last_now = 0
        self._sessions: dict[str, _Session] = {}
        self._invitations: dict[str, _Invitation] = {}
        self._global: deque[int] = deque()
        self._principals: dict[str, deque[int]] = {}

    def _enter(self, principal: VerifiedPrincipal | None, now: int) -> Decision | None:
        if not _integer(now) or now < self._last_now:
            return Decision(Code.CLOCK)
        self._last_now = now
        self._sessions = {key: value for key, value in self._sessions.items() if value.expires_at > now}
        self._invitations = {
            key: value for key, value in self._invitations.items() if value.session in self._sessions
        }
        cutoff = now - self.limits.window_seconds
        while self._global and self._global[0] <= cutoff:
            self._global.popleft()
        for subject, attempts in list(self._principals.items()):
            while attempts and attempts[0] <= cutoff:
                attempts.popleft()
            if not attempts:
                del self._principals[subject]
        if len(self._global) >= self.limits.global_attempts:
            return Decision(Code.RATE_LIMITED, retry_at=self._global[0] + self.limits.window_seconds)
        self._global.append(now)
        if (
            not isinstance(principal, VerifiedPrincipal)
            or not _identifier(principal.subject)
            or principal.audience != AUDIENCE
            or not _integer(principal.issued_at)
            or not _integer(principal.expires_at)
            or not principal.issued_at <= now < principal.expires_at
            or principal.expires_at - principal.issued_at > self.limits.authentication_seconds
        ):
            return Decision(Code.UNAUTHENTICATED)
        attempts = self._principals.get(principal.subject)
        if attempts is None:
            if len(self._principals) >= self.limits.principals:
                retry_at = min(times[-1] for times in self._principals.values()) + self.limits.window_seconds
                return Decision(Code.CAPACITY, retry_at=retry_at)
            attempts = self._principals[principal.subject] = deque()
        if len(attempts) >= self.limits.principal_attempts:
            return Decision(Code.RATE_LIMITED, retry_at=attempts[0] + self.limits.window_seconds)
        attempts.append(now)
        return None

    def create_session(self, principal: VerifiedPrincipal | None, session: str, now: int) -> Decision:
        with self._lock:
            denied = self._enter(principal, now)
            if denied is not None:
                return denied
            if not _identifier(session):
                return Decision(Code.INVALID)
            if session in self._sessions:
                return Decision(Code.DENIED)
            if len(self._sessions) >= self.limits.sessions:
                return Decision(Code.CAPACITY)
            expires_at = now + self.limits.session_seconds
            self._sessions[session] = _Session(principal.subject, expires_at, {principal.subject})
            return Decision(Code.ALLOWED, expires_at=expires_at)

    def invite(
        self, principal: VerifiedPrincipal | None, session: str, invitation: str, subject: str, now: int
    ) -> Decision:
        with self._lock:
            denied = self._enter(principal, now)
            if denied is not None:
                return denied
            if not all(_identifier(value) for value in (session, invitation, subject)):
                return Decision(Code.INVALID)
            current = self._sessions.get(session)
            if (
                current is None
                or current.closed
                or current.owner != principal.subject
                or invitation in self._invitations
                or subject in current.invited
                or (subject not in current.members and len(current.members) >= 2)
            ):
                return Decision(Code.DENIED)
            if len(self._invitations) >= self.limits.invitations:
                return Decision(Code.CAPACITY)
            expires_at = min(now + self.limits.invitation_seconds, current.expires_at)
            self._invitations[invitation] = _Invitation(session, subject, expires_at)
            current.members.add(subject)
            current.invited.add(subject)
            return Decision(Code.ALLOWED, expires_at=expires_at)

    def issue_credentials(
        self, principal: VerifiedPrincipal | None, session: str, invitation: str, now: int
    ) -> Decision:
        with self._lock:
            denied = self._enter(principal, now)
            if denied is not None:
                return denied
            if not all(_identifier(value) for value in (session, invitation)):
                return Decision(Code.INVALID)
            current = self._sessions.get(session)
            invite = self._invitations.get(invitation)
            if (
                current is None
                or current.closed
                or invite is None
                or invite.session != session
                or invite.subject != principal.subject
                or invite.expires_at <= now
                or invite.used
            ):
                return Decision(Code.DENIED)
            invite.used = True
            grant = CredentialGrant(
                session, principal.subject, invitation, now,
                min(now + self.limits.credential_seconds, current.expires_at),
            )
            try:
                credential = self._issuer.issue(grant)
                if not isinstance(credential, IssuedCredential) or credential.grant != grant:
                    return Decision(Code.ISSUER_FAILED)
            except Exception:
                return Decision(Code.ISSUER_FAILED)
            return Decision(Code.ALLOWED, expires_at=grant.expires_at, credential=credential)

    def close_session(self, principal: VerifiedPrincipal | None, session: str, now: int) -> Decision:
        with self._lock:
            denied = self._enter(principal, now)
            if denied is not None:
                return denied
            if not _identifier(session):
                return Decision(Code.INVALID)
            current = self._sessions.get(session)
            if current is None or current.closed or current.owner != principal.subject:
                return Decision(Code.DENIED)
            current.closed = True
            return Decision(Code.ALLOWED)

    def usage(self) -> Usage:
        """Bounded state counts only; expiry is collected on the next attempt."""
        with self._lock:
            return Usage(
                len(self._sessions), len(self._invitations), len(self._principals),
                len(self._global), sum(map(len, self._principals.values())),
            )
