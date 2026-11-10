#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Invented identities and payloads only; no network, credentials or game data."""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
import unittest

from relay_access_policy import (
    AUDIENCE, AccessPolicy, Code, CredentialGrant, IssuedCredential, Limits, VerifiedPrincipal,
)


class FakeIssuer:
    def __init__(self, mode="ok"):
        self.mode = mode
        self.calls = []

    def issue(self, grant):
        self.calls.append(grant)
        if self.mode == "raise":
            raise RuntimeError("invented issuer failure")
        if self.mode == "wrong_grant":
            return IssuedCredential(replace(grant, expires_at=grant.expires_at + 1), "invented-payload")
        if self.mode == "wrong_type":
            return "invented-payload"
        return IssuedCredential(grant, "invented-payload")


class FakeAuthenticator:
    """A test fixture, not an authentication implementation."""

    @staticmethod
    def verify(proof, now=100):
        identities = {
            "invented-owner-proof": "owner",
            "invented-guest-proof": "guest",
            "invented-other-proof": "other",
        }
        subject = identities.get(proof)
        return VerifiedPrincipal(subject, now, now + 120) if subject else None


class AccessPolicyTests(unittest.TestCase):
    def setUp(self):
        self.issuer = FakeIssuer()
        self.policy = AccessPolicy(self.issuer)
        self.owner = FakeAuthenticator.verify("invented-owner-proof")
        self.guest = FakeAuthenticator.verify("invented-guest-proof")
        self.other = FakeAuthenticator.verify("invented-other-proof")

    def prepare(self, policy=None, now=100, subject="guest", session="session", invitation="invitation"):
        policy = policy or self.policy
        owner = FakeAuthenticator.verify("invented-owner-proof", now)
        self.assertTrue(policy.create_session(owner, session, now).allowed)
        self.assertTrue(policy.invite(owner, session, invitation, subject, now).allowed)

    def test_authenticated_intended_recipient_gets_one_session_grant(self):
        self.prepare()
        result = self.policy.issue_credentials(self.guest, "session", "invitation", 101)
        self.assertTrue(result.allowed)
        self.assertEqual(result.credential.grant, CredentialGrant("session", "guest", "invitation", 101, 700))
        self.assertEqual(result.credential.payload, "invented-payload")
        self.assertEqual(len(self.issuer.calls), 1)

    def test_authentication_fixture_rejects_unknown_proof(self):
        self.assertIsNone(FakeAuthenticator.verify("invented-unknown-proof"))

    def test_every_operation_requires_trusted_authentication(self):
        self.prepare()
        calls = [
            lambda: self.policy.create_session(None, "other-session", 100),
            lambda: self.policy.invite(None, "session", "other-invite", "guest", 100),
            lambda: self.policy.issue_credentials(None, "session", "invitation", 100),
            lambda: self.policy.close_session(None, "session", 100),
        ]
        for call in calls:
            with self.subTest(call=call):
                self.assertEqual(call().code, Code.UNAUTHENTICATED)
        self.assertEqual(self.issuer.calls, [])

    def test_expired_future_wrong_audience_and_unbounded_authentication_are_denied(self):
        contexts = [
            replace(self.owner, expires_at=100),
            replace(self.owner, issued_at=101),
            replace(self.owner, audience="invented-other-audience"),
            replace(self.owner, expires_at=221),
            replace(self.owner, subject=""),
            replace(self.owner, issued_at=True),
            replace(self.owner, expires_at=float("inf")),
            {"subject": "owner", "audience": AUDIENCE},
        ]
        for context in contexts:
            with self.subTest(context=context):
                self.assertEqual(self.policy.create_session(context, "session", 100).code, Code.UNAUTHENTICATED)
        self.assertEqual(self.issuer.calls, [])

    def test_session_and_invitation_handles_are_bounded(self):
        for value in ("", "x" * 65, "bad id", "nonascii_\u2603", None, 4):
            with self.subTest(value=value):
                policy = AccessPolicy(FakeIssuer())
                self.assertEqual(policy.create_session(self.owner, value, 100).code, Code.INVALID)
                policy.create_session(self.owner, "session", 100)
                self.assertEqual(policy.invite(self.owner, "session", value, "guest", 100).code, Code.INVALID)

    def test_session_owner_controls_invitation_and_close(self):
        self.prepare()
        self.assertEqual(self.policy.invite(self.other, "session", "other-invite", "other", 100).code, Code.DENIED)
        self.assertEqual(self.policy.close_session(self.guest, "session", 100).code, Code.DENIED)
        self.assertEqual(self.issuer.calls, [])

    def test_owner_has_a_reserved_participant_slot(self):
        self.prepare()
        self.assertTrue(self.policy.invite(self.owner, "session", "owner-invite", "owner", 100).allowed)
        self.assertEqual(self.policy.invite(self.owner, "session", "third-invite", "other", 100).code, Code.DENIED)
        self.assertTrue(self.policy.issue_credentials(self.owner, "session", "owner-invite", 100).allowed)
        self.assertTrue(self.policy.issue_credentials(self.guest, "session", "invitation", 100).allowed)
        self.assertEqual(len(self.issuer.calls), 2)

    def test_unknown_wrong_recipient_and_wrong_session_never_reach_issuer(self):
        self.prepare()
        attempts = [
            (self.other, "session", "invitation"),
            (self.guest, "session", "unknown"),
            (self.guest, "other-session", "invitation"),
        ]
        for principal, session, invitation in attempts:
            with self.subTest(principal=principal, session=session, invitation=invitation):
                self.assertEqual(self.policy.issue_credentials(principal, session, invitation, 100).code, Code.DENIED)
        self.assertEqual(self.issuer.calls, [])

    def test_invitation_cannot_be_retargeted_or_replaced(self):
        self.prepare()
        self.assertTrue(self.policy.create_session(self.owner, "other-session", 100).allowed)
        self.assertEqual(self.policy.invite(self.owner, "other-session", "invitation", "guest", 100).code, Code.DENIED)
        self.assertEqual(self.policy.invite(self.owner, "session", "new-invitation", "guest", 100).code, Code.DENIED)
        self.assertTrue(self.policy.issue_credentials(self.guest, "session", "invitation", 100).allowed)

    def test_invitation_is_consumed_once_and_has_no_renewal(self):
        self.prepare()
        self.assertTrue(self.policy.issue_credentials(self.guest, "session", "invitation", 100).allowed)
        self.assertEqual(self.policy.issue_credentials(self.guest, "session", "invitation", 100).code, Code.DENIED)
        self.assertEqual(self.policy.invite(self.owner, "session", "renewal", "guest", 100).code, Code.DENIED)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_invitation_expiry_is_exclusive(self):
        self.prepare()
        before_expiry = FakeAuthenticator.verify("invented-guest-proof", 219)
        self.assertTrue(self.policy.issue_credentials(before_expiry, "session", "invitation", 219).allowed)
        policy = AccessPolicy(self.issuer)
        self.prepare(policy)
        at_expiry = FakeAuthenticator.verify("invented-guest-proof", 220)
        self.assertEqual(policy.issue_credentials(at_expiry, "session", "invitation", 220).code, Code.DENIED)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_expired_invitation_keeps_session_tombstone(self):
        self.prepare()
        fresh_owner = FakeAuthenticator.verify("invented-owner-proof", 220)
        self.assertEqual(self.policy.invite(fresh_owner, "session", "new-invitation", "guest", 220).code, Code.DENIED)
        self.assertEqual(self.policy.usage().invitations, 1)

    def test_grant_lifetime_is_clamped_to_session_expiry(self):
        self.assertTrue(self.policy.create_session(self.owner, "session", 100).allowed)
        fresh_owner = FakeAuthenticator.verify("invented-owner-proof", 690)
        fresh_guest = FakeAuthenticator.verify("invented-guest-proof", 699)
        self.assertEqual(self.policy.invite(fresh_owner, "session", "invitation", "guest", 690).expires_at, 700)
        result = self.policy.issue_credentials(fresh_guest, "session", "invitation", 699)
        self.assertEqual(result.expires_at, 700)
        self.assertEqual(result.credential.grant.expires_at, 700)

    def test_expired_session_cannot_issue(self):
        self.prepare()
        fresh_guest = FakeAuthenticator.verify("invented-guest-proof", 700)
        self.assertEqual(self.policy.issue_credentials(fresh_guest, "session", "invitation", 700).code, Code.DENIED)
        self.assertEqual(self.policy.usage().sessions, 0)
        self.assertEqual(self.policy.usage().invitations, 0)
        self.assertEqual(self.issuer.calls, [])

    def test_close_prevents_mint_but_retains_session_until_expiry(self):
        self.prepare()
        self.assertTrue(self.policy.close_session(self.owner, "session", 100).allowed)
        self.assertEqual(self.policy.issue_credentials(self.guest, "session", "invitation", 100).code, Code.DENIED)
        self.assertEqual(self.policy.create_session(self.owner, "session", 100).code, Code.DENIED)
        self.assertEqual(self.policy.invite(self.owner, "session", "owner-invite", "owner", 100).code, Code.DENIED)
        self.assertEqual(self.policy.usage().sessions, 1)
        fresh_owner = FakeAuthenticator.verify("invented-owner-proof", 700)
        self.assertTrue(self.policy.create_session(fresh_owner, "session", 700).allowed)

    def test_issuer_failure_or_invalid_response_burns_invitation(self):
        for mode in ("raise", "wrong_type", "wrong_grant"):
            with self.subTest(mode=mode):
                issuer = FakeIssuer(mode)
                policy = AccessPolicy(issuer)
                self.prepare(policy)
                self.assertEqual(policy.issue_credentials(self.guest, "session", "invitation", 100).code, Code.ISSUER_FAILED)
                self.assertEqual(policy.issue_credentials(self.guest, "session", "invitation", 100).code, Code.DENIED)
                self.assertEqual(len(issuer.calls), 1)

    def test_denied_attempts_spend_principal_budget_and_retry_boundary_is_exact(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), principal_attempts=2))
        for _ in range(2):
            self.assertEqual(policy.issue_credentials(self.guest, "missing", "missing", 100).code, Code.DENIED)
        limited = policy.create_session(self.guest, "session", 101)
        self.assertEqual((limited.code, limited.retry_at), (Code.RATE_LIMITED, 160))
        self.assertTrue(policy.create_session(self.guest, "session", 160).allowed)
        self.assertEqual(self.issuer.calls, [])

    def test_authentication_failures_spend_global_budget(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), global_attempts=2))
        self.assertEqual(policy.create_session(None, "session", 100).code, Code.UNAUTHENTICATED)
        self.assertEqual(policy.create_session(None, "session", 100).code, Code.UNAUTHENTICATED)
        limited = policy.create_session(self.owner, "session", 101)
        self.assertEqual((limited.code, limited.retry_at), (Code.RATE_LIMITED, 160))
        self.assertTrue(policy.create_session(self.owner, "session", 160).allowed)
        self.assertEqual(policy.usage().global_attempts, 1)

    def test_principal_tracking_capacity_is_bounded_and_expires(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), principals=1))
        self.assertTrue(policy.create_session(self.owner, "session", 100).allowed)
        limited = policy.create_session(self.guest, "guest-session", 100)
        self.assertEqual((limited.code, limited.retry_at), (Code.CAPACITY, 160))
        self.assertEqual(policy.usage().principals, 1)
        self.assertTrue(policy.create_session(self.guest, "guest-session", 160).allowed)
        self.assertEqual(policy.usage().principals, 1)

    def test_principal_capacity_retry_waits_for_a_whole_bucket_to_expire(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), principals=1))
        self.assertTrue(policy.create_session(self.owner, "session", 100).allowed)
        self.assertTrue(policy.invite(self.owner, "session", "invitation", "guest", 110).allowed)
        limited = policy.create_session(self.guest, "guest-session", 110)
        self.assertEqual((limited.code, limited.retry_at), (Code.CAPACITY, 170))
        self.assertEqual(policy.create_session(self.guest, "guest-session", 160).code, Code.CAPACITY)
        self.assertTrue(policy.create_session(self.guest, "guest-session", 170).allowed)

    def test_session_and_invitation_capacity_never_call_issuer(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), sessions=1, invitations=1))
        self.prepare(policy)
        self.assertEqual(policy.create_session(self.owner, "other-session", 100).code, Code.CAPACITY)
        self.assertEqual(policy.invite(self.owner, "session", "owner-invite", "owner", 100).code, Code.CAPACITY)
        usage = policy.usage()
        self.assertEqual((usage.sessions, usage.invitations), (1, 1))
        self.assertEqual(self.issuer.calls, [])

    def test_nonmonotonic_or_invalid_clock_cannot_mint_or_reopen_budgets(self):
        self.prepare()
        for now in (99, -1, True, "100", float("inf")):
            with self.subTest(now=now):
                self.assertEqual(self.policy.issue_credentials(self.guest, "session", "invitation", now).code, Code.CLOCK)
        self.assertEqual(self.issuer.calls, [])
        self.assertTrue(self.policy.issue_credentials(self.guest, "session", "invitation", 100).allowed)

    def test_concurrent_redemptions_call_issuer_exactly_once(self):
        policy = AccessPolicy(self.issuer, replace(Limits(), principal_attempts=32))
        self.prepare(policy)
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: policy.issue_credentials(self.guest, "session", "invitation", 100), range(16)))
        self.assertEqual(sum(result.allowed for result in results), 1)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_payload_and_invitation_handle_are_omitted_from_repr(self):
        self.prepare(invitation="invented-sensitive-handle")
        result = self.policy.issue_credentials(self.guest, "session", "invented-sensitive-handle", 100)
        for value in (result, result.credential, result.credential.grant):
            self.assertNotIn("invented-payload", repr(value))
            self.assertNotIn("invented-sensitive-handle", repr(value))

    def test_configuration_has_hard_upper_bounds_and_positive_integer_limits(self):
        cases = [
            {"session_seconds": 3601}, {"credential_seconds": 601}, {"invitation_seconds": 301},
            {"authentication_seconds": 301}, {"window_seconds": 301}, {"sessions": 129},
            {"invitations": 257}, {"principals": 257}, {"principal_attempts": 1001},
            {"global_attempts": 1001}, {"session_seconds": 599}, {"sessions": 0},
            {"global_attempts": -1}, {"principal_attempts": True},
        ]
        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                replace(Limits(), **changes)


if __name__ == "__main__":
    unittest.main()
