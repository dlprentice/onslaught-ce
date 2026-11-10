#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored accountless admission and fake-provider composition checks."""
from __future__ import annotations

import argparse
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from dataclasses import replace
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import ice_pilot_courier_tests as fixtures
import signaling_api_contract as api
import signaling_api_contract_tests as api_checks
import signaling_pair_bindings as bindings

OUTPUT: Path
CRYPTO: bindings.service.parcels.Monocypher
ADMISSION_KEY = hashlib.sha256(b"authored distinct accountless admission key").digest()
IDENTITY_KEY = hashlib.sha256(b"authored distinct store identity key").digest()
PARCEL_KEY = hashlib.sha256(b"authored distinct stored parcel key").digest()
MASTER = "authored-provider-master-canary"
CONTEXT = hashlib.sha256(b"authored accountless lobby context").hexdigest()
GAME_CONTEXT = hashlib.sha256(b"authored accountless game context").hexdigest()


class Provider:
    def __init__(self):
        self.ttls = []
        self.response = None
        self.failure = False

    def generate(self, key, ttl):
        self.ttls.append(ttl)
        if self.failure:
            raise RuntimeError(MASTER)
        if self.response is not None:
            return self.response
        return bindings.wire._json({"iceServers": [
            {"urls": ["stun:stun.cloudflare.com:3478", "stun:stun.cloudflare.com:443"]},
            {"urls": sorted(bindings.private.UDP_URLS), "username": "authored-user-" + str(len(self.ttls)),
             "credential": "authored-provider-temporary-password"}]})


class BindingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="authored-", dir=OUTPUT)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        stack = ExitStack()
        self.addCleanup(stack.close)
        for owner, name in ((socket, "socket"), (socket, "getaddrinfo"),
                            (bindings.private, "_load_key"), (bindings.private, "issue_local"),
                            (bindings.private, "issue_from_request"),
                            (bindings.private.FixedCloudflareTransport, "generate")):
            stack.enter_context(patch.object(owner, name, side_effect=AssertionError("unexpected external operation")))
        self.clock = api_checks.Clock()
        self.provider = Provider()
        self.verifier = bindings.PairCapabilityVerifier(ADMISSION_KEY)
        self.issuer = bindings.FixedProviderIssuer(bindings.private.ProviderKey("authored-key-id", MASTER), self.provider)
        self.store_path = self.newdir("store")
        self.store = self.open_store(self.verifier)
        self.bundle = bindings.prepare_pair(self.store, self.verifier, self.clock)
        self.endpoint = api.Api(self.store)
        self.transport = api_checks.LocalTransport(self.endpoint)
        self.host = api.Client(self.transport, self.bundle.host.token, self.clock)
        self.guest = api.Client(self.transport, self.bundle.guest.token, self.clock)
        self.host.current()
        self.guest.current()

    def newdir(self, name):
        path = self.root / name
        path.mkdir(mode=0o700)
        return path

    def open_store(self, verifier):
        cipher = bindings.service.parcels.ParcelCipher(CRYPTO, PARCEL_KEY)
        store = bindings.service.SessionStore(self.store_path, verifier, self.issuer, self.clock, IDENTITY_KEY, cipher)
        self.addCleanup(store.close)
        return store

    def refused(self, function, *args, **kwargs):
        with self.assertRaisesRegex(bindings.Refused, "^pair binding refused$"):
            function(*args, **kwargs)

    def raw(self, token, route="pair/current", value=None):
        request = api.Request("POST", api.PREFIX + route,
            (("Authorization", "Bearer " + token.decode("ascii")), ("Content-Type", "application/json")),
            bindings.wire._json(value if value is not None else {}))
        return self.endpoint.handle(request)

    def signed(self, value=None, raw=None):
        payload = bindings._payload(value) if raw is None else raw
        signature = bindings.hmac.digest(ADMISSION_KEY, bindings.DOMAIN + payload, "sha256")
        return b".".join((bindings.PREFIX, bindings._encode(payload), bindings._encode(signature)))

    def claims(self, token=None):
        token = token or self.bundle.host.token
        return json.loads(bindings._decode(token.split(b".")[1], 512))

    def consent(self):
        self.assertEqual(self.host.accept().status, "awaiting_pair_consent")
        self.assertEqual(self.guest.accept().status, "admitted")

    def stages(self, name="lobby", context=CONTEXT):
        if name == "lobby":
            self.consent()
        values = []
        for role, client in (("host", self.host), ("guest", self.guest)):
            directory = self.newdir(name + "-" + role)
            fixtures.write(directory / ("relay-" + role + ".request.json"), fixtures.request(role, context, self.clock.now))
            stage = client.stage(name, directory, after_authenticated_start=name == "game")
            self.addCleanup(stage.close)
            self.assertEqual(stage.request().status, "awaiting_peer_request")
            values.append((stage, directory))
        self.clock.now += 1
        return values

    def complete(self, name="lobby", context=CONTEXT):
        (host, host_dir), (guest, guest_dir) = self.stages(name, context)
        self.assertEqual(host.poll().status, "exchanging")
        self.assertEqual(guest.poll().status, "awaiting_peer_record")
        fixtures.write(host_dir / api.wire._record_name(context, "offer"), fixtures.record("host"))
        host.publish()
        self.clock.now += 1
        guest.poll()
        fixtures.write(guest_dir / api.wire._record_name(context, "answer"), fixtures.record("guest"))
        guest.publish()
        self.clock.now += 1
        self.assertEqual(host.poll().status, "service_delivery_complete")
        return host_dir, guest_dir

    def test_accountless_subjects_roles_audience_and_pair_expiry_are_distinct(self):
        self.assertNotEqual(self.bundle.host.subject, self.bundle.guest.subject)
        self.assertNotEqual(self.bundle.host.token, self.bundle.guest.token)
        for role, member in (("host", self.bundle.host), ("guest", self.bundle.guest)):
            principal = self.verifier.verify(member.token, self.clock.now)
            self.assertIsInstance(principal, bindings.service.Principal)
            self.assertEqual((principal.subject, principal.audience, principal.kind), (member.subject, bindings.service.AUDIENCE, "user"))
            self.assertEqual(principal.expires_at, member.expires_at)
            self.assertTrue(bindings._subject(principal.subject, role))
            self.assertEqual(set(self.claims(member.token)), bindings.FIELDS)
            self.assertEqual(member.expires_at, 100 + bindings.private.DEFAULT_TTL)
        self.assertEqual(self.provider.ttls, [])

    def test_signature_tamper_wrong_key_and_ce_join_or_record_never_authenticate(self):
        token = self.bundle.host.token
        changed = self.claims() | {"role": "guest"}
        signature = token.split(b".")[2]
        forged = b".".join((bindings.PREFIX, bindings._encode(bindings._payload(changed)), signature))
        for value in (forged, b"authored-private-CE-join-secret", fixtures.record("host"), b"x" * (bindings.MAX_TOKEN + 1)):
            self.assertIsNone(self.verifier.verify(value, self.clock.now))
        wrong = bindings.PairCapabilityVerifier(hashlib.sha256(b"authored wrong admission key").digest())
        self.assertIsNone(wrong.verify(token, self.clock.now))
        self.assertEqual(self.raw(forged).code, 401)
        self.assertEqual(self.provider.ttls, [])

    def test_bad_mac_refuses_before_payload_json_parsing(self):
        pieces = self.bundle.host.token.split(b".")
        forged = b".".join((pieces[0], pieces[1], bindings._encode(b"z" * 32)))
        with patch.object(bindings.private, "_decode", side_effect=AssertionError("payload parsed")) as decode:
            self.assertIsNone(self.verifier.verify(forged, self.clock.now))
        decode.assert_not_called()

    def test_authenticated_but_invalid_claim_shapes_expiry_role_context_are_refused(self):
        value = self.claims()
        invalid = [value | {"audience": "other-service"}, value | {"role": "guest"},
                   value | {"subject": "claimed-human-name"}, value | {"issuedAt": True},
                   value | {"issuedAt": self.clock.now + 1}, value | {"expiresAt": self.clock.now},
                   value | {"expiresAt": value["issuedAt"] + bindings.private.MAX_TTL + 1},
                   value | {"context": CONTEXT}, value | {"joinSecret": "authored-CE-secret"},
                   value | {"provider": "other.invalid"}, value | {"schema": 2}]
        for claims in invalid:
            self.assertIsNone(self.verifier.verify(self.signed(claims), self.clock.now))
        duplicate = bindings._payload(value)[:-1] + b',"role":"host"}'
        self.assertIsNone(self.verifier.verify(self.signed(raw=duplicate), self.clock.now))
        padded = self.bundle.host.token.split(b".")
        padded[1] += b"="
        self.assertIsNone(self.verifier.verify(b".".join(padded), self.clock.now))
        self.assertEqual(self.provider.ttls, [])

    def test_expired_capability_or_body_identity_cannot_issue(self):
        value = {"approval": self.bundle.host.approval, "stage": "lobby", "subject": self.bundle.guest.subject}
        self.assertEqual(self.raw(self.bundle.host.token, "stage/poll", value).code, 200)
        self.consent()
        self.assertEqual(self.raw(self.bundle.host.token, "stage/poll", value).code, 400)
        self.clock.now = self.bundle.host.expires_at
        self.assertEqual(self.raw(self.bundle.host.token, "stage/poll", value).code, 401)
        self.assertEqual(self.provider.ttls, [])

    def test_two_stages_compose_real_verifier_issuer_store_and_private_client(self):
        host_dir, guest_dir = self.complete()
        self.complete("game", GAME_CONTEXT)
        self.assertEqual(len(self.provider.ttls), 4)
        self.assertTrue(all(0 < ttl <= bindings.private.DEFAULT_TTL for ttl in self.provider.ttls))
        for role, directory in (("host", host_dir), ("guest", guest_dir)):
            grant = json.loads((directory / ("relay-" + role + ".json")).read_bytes())
            self.assertEqual((grant["role"], grant["context"]), (role, CONTEXT))
            self.assertLessEqual(grant["expiresAt"], self.bundle.host.expires_at)
            self.assertNotIn(MASTER, json.dumps(grant))
        self.assertNotIn(self.bundle.host.token.decode(), repr(self.bundle))
        self.assertNotIn(self.bundle.guest.token.decode(), repr(self.bundle))

    def test_new_capability_still_requires_actual_approved_membership(self):
        token = self.verifier.mint("p_" + "a" * 32 + "_host", "host", 100, 1000)
        self.assertIsNotNone(self.verifier.verify(token, 100))
        self.assertEqual(self.raw(token).code, 403)
        self.assertEqual(self.raw(token, "pair/mint").code, 400)
        self.assertEqual(self.provider.ttls, [])

    def test_bootstrap_failure_keeps_durable_four_permits_spent_and_does_not_retry(self):
        self.host.close_pair()
        with patch.object(self.verifier, "mint", side_effect=RuntimeError(MASTER)):
            self.refused(bindings.prepare_pair, self.store, self.verifier, self.clock)
        with self.store._tx() as db:
            self.assertEqual(tuple(db.execute("SELECT pairs,permits FROM budgets WHERE day=0").fetchone()), (2, 8))
            self.assertEqual(db.execute("SELECT count(*) FROM permits").fetchone()[0], 8)
        self.refused(bindings.prepare_pair, self.store, self.verifier, self.clock)
        self.assertEqual(self.provider.ttls, [])

    def test_wrong_configured_verifier_burns_approval_before_returning_tokens(self):
        self.host.close_pair()
        other = bindings.PairCapabilityVerifier(hashlib.sha256(b"authored other verifier").digest())
        self.refused(bindings.prepare_pair, self.store, other, self.clock)
        with self.store._tx() as db:
            self.assertEqual(db.execute("SELECT permits FROM budgets WHERE day=0").fetchone()[0], 8)
        self.assertEqual(self.provider.ttls, [])

    def test_role_exports_are_private_separate_and_idempotent(self):
        directories = {role: self.newdir(role + "-admission") for role in bindings.service.ROLES}
        for role, member, other in (("host", self.bundle.host, self.bundle.guest), ("guest", self.bundle.guest, self.bundle.host)):
            directory = directories[role]
            bindings.export_role(self.bundle, role, directory, self.clock)
            before = {p.name: p.read_bytes() for p in directory.iterdir()}
            self.clock.now += 1
            bindings.export_role(self.bundle, role, directory, self.clock)
            self.assertEqual({p.name: p.read_bytes() for p in directory.iterdir()}, before)
            self.assertEqual(before[bindings.TOKEN_FILE], member.token)
            self.assertEqual(json.loads(before[bindings.ADMISSION_FILE])["role"], role)
            self.assertEqual(set(before), {bindings.ADMISSION_FILE, bindings.TOKEN_FILE})
            for path in directory.iterdir():
                self.assertEqual(path.stat().st_mode & 0o077, 0)
                self.assertNotIn(other.token, path.read_bytes())
                self.assertNotIn(MASTER.encode(), path.read_bytes())
                self.assertNotIn(ADMISSION_KEY, path.read_bytes())
        self.refused(bindings.export_role, self.bundle, "guest", directories["host"], self.clock)

    def test_export_conflict_preflights_both_files_before_any_publication(self):
        directory = self.newdir("conflict")
        fixtures.write(directory / bindings.TOKEN_FILE, b"authored unique pre-existing data")
        self.refused(bindings.export_role, self.bundle, "host", directory, self.clock)
        self.assertFalse((directory / bindings.ADMISSION_FILE).exists())
        self.assertEqual((directory / bindings.TOKEN_FILE).read_bytes(), b"authored unique pre-existing data")

    def test_partial_export_retry_uses_original_token_and_expiry(self):
        directory = self.newdir("partial")
        original = bindings.wire._publish
        def fail_token(fd, name, payload):
            if name == bindings.TOKEN_FILE:
                raise OSError(MASTER)
            original(fd, name, payload)
        with patch.object(bindings.wire, "_publish", side_effect=fail_token):
            self.refused(bindings.export_role, self.bundle, "host", directory, self.clock)
        metadata = (directory / bindings.ADMISSION_FILE).read_bytes()
        self.clock.now += 1
        bindings.export_role(self.bundle, "host", directory, self.clock)
        self.assertEqual((directory / bindings.ADMISSION_FILE).read_bytes(), metadata)
        self.assertEqual((directory / bindings.TOKEN_FILE).read_bytes(), self.bundle.host.token)

    def test_export_directory_swap_and_expiry_cannot_publish_token(self):
        directory = self.newdir("swap")
        calls = 0
        def swapped():
            nonlocal calls
            calls += 1
            if calls == 3:
                directory.rename(self.root / "displaced")
                directory.mkdir(mode=0o700)
            return self.clock.now
        self.refused(bindings.export_role, self.bundle, "host", directory, swapped)
        self.assertFalse((directory / bindings.TOKEN_FILE).exists())
        self.assertFalse((self.root / "displaced" / bindings.TOKEN_FILE).exists())
        self.clock.now = self.bundle.host.expires_at
        self.refused(bindings.export_role, self.bundle, "guest", self.newdir("expired"), self.clock)

    def test_operator_export_drives_existing_finite_https_cli_without_account_or_socket(self):
        directory = self.newdir("operator-host")
        bindings.export_role(self.bundle, "host", directory, self.clock)
        fd = os.open(directory / bindings.TOKEN_FILE, os.O_RDONLY | os.O_CLOEXEC)
        self.addCleanup(os.close, fd)
        factory = api_checks.ConnectionFactory(self.endpoint)
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(api.http.client, "HTTPSConnection", side_effect=factory), \
             patch.object(api.time, "time", return_value=self.clock.now), redirect_stdout(output), redirect_stderr(errors):
            code = api.main(["accept", "--endpoint", "https://signaling.example.invalid", "--identity-token-fd", str(fd),
                             "--client-directory", str(self.newdir("host-client-binding"))])
        self.assertEqual((code, errors.getvalue()), (0, ""))
        self.assertEqual(json.loads(output.getvalue())["status"], "awaiting_pair_consent")
        self.assertNotIn(self.bundle.host.token.decode(), output.getvalue())
        self.assertNotIn(MASTER, output.getvalue())
        self.assertEqual(self.provider.ttls, [])
        self.assertTrue(all(connection.closed for connection in factory.connections))

    def test_issuer_fixed_policy_rejects_bad_requests_without_transport_call(self):
        valid = bindings.service.Issuance("a" * 32, "host", "lobby", 120)
        for request in (replace(valid, attempt="bad"), replace(valid, role="other"), replace(valid, stage="other"),
                        replace(valid, ttl=True), replace(valid, ttl=0), replace(valid, ttl=bindings.private.DEFAULT_TTL + 1)):
            self.refused(self.issuer.issue, request)
        self.assertEqual(self.provider.ttls, [])
        response = json.loads(self.issuer.issue(valid))
        self.assertEqual(self.provider.ttls, [120])
        self.assertEqual(set(response), {"iceServers"})
        self.assertEqual(set(response["iceServers"][0]["urls"]), bindings.private.UDP_URLS)

    def test_master_echo_stays_secret_and_never_remints(self):
        (host, host_dir), _ = self.stages()
        self.provider.response = bindings.wire._json({"iceServers": [{"urls": sorted(bindings.private.UDP_URLS),
            "username": "authored-user", "credential": MASTER}]})
        self.assertEqual(host.poll().status, "issuance_unknown")
        self.clock.now += 1
        self.assertEqual(host.poll().status, "issuance_unknown")
        self.assertEqual(len(self.provider.ttls), 1)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.assertNotIn(MASTER, repr(host.poll()))

    def test_provider_exception_is_fixed_and_keeps_the_permit_burned(self):
        (host, host_dir), _ = self.stages()
        self.provider.failure = True
        self.assertEqual(host.poll().status, "issuance_unknown")
        self.clock.now += 1
        self.assertEqual(host.poll().status, "issuance_unknown")
        self.assertEqual(len(self.provider.ttls), 1)
        self.assertFalse((host_dir / "relay-host.json").exists())
        with self.assertRaisesRegex(bindings.Refused, "^pair binding refused$") as error:
            self.issuer.issue(bindings.service.Issuance("a" * 32, "host", "lobby", 120))
        self.assertNotIn(MASTER, str(error.exception))

    def test_lost_provider_result_response_and_restart_reuse_same_durable_grant(self):
        (host, host_dir), _ = self.stages()
        self.transport.lose["stage/poll"] = 1
        with self.assertRaises(api.Refused):
            host.poll()
        self.assertEqual(len(self.provider.ttls), 1)
        self.store.close()
        self.verifier = bindings.PairCapabilityVerifier(ADMISSION_KEY)
        self.store = self.open_store(self.verifier)
        self.endpoint._backend = self.store
        self.clock.now += 1
        client = api.Client(self.transport, self.bundle.host.token, self.clock)
        client.current()
        stage = client.stage("lobby", host_dir)
        self.addCleanup(stage.close)
        self.assertEqual(stage.poll().status, "exchanging")
        self.assertTrue((host_dir / "relay-host.json").exists())
        self.assertEqual(len(self.provider.ttls), 1)

    def test_close_stops_issuance_without_treating_capability_as_an_account(self):
        self.host.close_pair()
        self.assertIsNotNone(self.verifier.verify(self.bundle.host.token, self.clock.now))
        self.assertEqual(self.raw(self.bundle.host.token, "stage/poll", {"approval": self.bundle.host.approval, "stage": "lobby"}).code, 200)
        self.assertEqual(json.loads(self.raw(self.bundle.host.token).body)["status"], "closed")
        self.assertEqual(self.provider.ttls, [])


def causal_controls():
    global bindings
    original = bindings
    source_path = Path(original.__file__)
    source = source_path.read_text()
    controls = (
        ("signature", 'require(hmac.compare_digest(signature, hmac.digest(self._key, DOMAIN + payload, "sha256")))',
         "require(True)", "test_signature_tamper_wrong_key_and_ce_join_or_record_never_authenticate"),
        ("audience", 'value["audience"] == service.AUDIENCE and ', "",
         "test_authenticated_but_invalid_claim_shapes_expiry_role_context_are_refused"),
        ("role_subject", '_subject(value["subject"], value["role"])', "True",
         "test_authenticated_but_invalid_claim_shapes_expiry_role_context_are_refused"),
        ("time_window", 'value["issuedAt"] <= now < value["expiresAt"]', "True",
         "test_authenticated_but_invalid_claim_shapes_expiry_role_context_are_refused"),
        ("configured_membership", 'require(reply.status == "awaiting_pair_consent" and reply.role == role and\n                    reply.approval == approval and reply.pair_expires_at == expires)',
         "require(True)", "test_wrong_configured_verifier_burns_approval_before_returning_tokens"),
        ("issuer_ttl", "require(type(request.ttl) is int and 1 <= request.ttl <= self.maximum_seconds)", "require(True)",
         "test_issuer_fixed_policy_rejects_bad_requests_without_transport_call"),
        ("master_echo", "require(not any(self._key.api_token in relay.username or self._key.api_token in relay.credential for relay in lease.relays))",
         "require(True)", "test_master_echo_stays_secret_and_never_remints"),
        ("export_preflight", "        for name, payload in outputs:\n            wire._existing(selected, name, payload)",
         "        pass  # Authored in-memory removal of preflight.",
         "test_export_conflict_preflights_both_files_before_any_publication"),
    )
    records = []
    try:
        for index, (name, before, after, case) in enumerate(controls):
            bindings = original
            positive = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(unittest.TestSuite([BindingTests(case)]))
            if not positive.wasSuccessful() or source.count(before) != 1:
                raise RuntimeError("pair binding causal positive or selector refused")
            module_name = "authored_pair_binding_control_" + str(index)
            mutant = types.ModuleType(module_name)
            mutant.__file__ = str(source_path)
            sys.modules[module_name] = mutant
            try:
                exec(compile(source.replace(before, after, 1), str(source_path), "exec"), mutant.__dict__)
                bindings = mutant
                result = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(unittest.TestSuite([BindingTests(case)]))
                records.append({"control": name, "case": case, "positive_passed": True,
                                "mutant_detected": not result.wasSuccessful(), "failures": len(result.failures), "errors": len(result.errors)})
                if result.wasSuccessful() or result.testsRun != 1:
                    raise RuntimeError("pair binding causal mutant undetected")
            finally:
                bindings = original
                del sys.modules[module_name]
        (OUTPUT / "causal-controls.private.json").write_text(json.dumps(records, indent=2) + "\n")
        print("SIGNALING_PAIR_BINDINGS_CAUSAL_PASS", len(records))
        return 0
    finally:
        bindings = original


def main():
    global OUTPUT, CRYPTO
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crypto-library", type=Path, required=True)
    parser.add_argument("--crypto-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--causal-controls-only", action="store_true")
    args = parser.parse_args()
    OUTPUT = args.output.absolute()
    OUTPUT.mkdir(mode=0o700, parents=True, exist_ok=False)
    CRYPTO = bindings.service.parcels.Monocypher(args.crypto_library, args.crypto_sha256)
    if args.causal_controls_only:
        return causal_controls()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(BindingTests))
    print("SIGNALING_PAIR_BINDINGS_PASS" if result.wasSuccessful() else "SIGNALING_PAIR_BINDINGS_FAIL")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
