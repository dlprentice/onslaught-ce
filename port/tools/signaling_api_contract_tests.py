#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored local API/store/client integration; no game, network or provider."""
from __future__ import annotations

import argparse
import base64
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

OUTPUT: Path
CRYPTO: api.service.parcels.Monocypher
HOST, GUEST, OTHER = b"invented-host-token", b"invented-guest-token", b"invented-other-token"
IDENTITY_KEY = hashlib.sha256(b"authored API identity binding").digest()
PARCEL_KEY = hashlib.sha256(b"authored API separate parcel key").digest()
CONTEXT = hashlib.sha256(b"authored API lobby context").hexdigest()
GAME_CONTEXT = hashlib.sha256(b"authored API game context").hexdigest()
CANARY = "invented-api-password-canary"


class Clock:
    now = 100

    def __call__(self):
        return self.now


class Verifier:
    def verify(self, token, now):
        subject = {HOST: "authored_host", GUEST: "authored_guest", OTHER: "authored_other"}.get(token)
        return api.service.Principal(subject, 0, 20000) if subject else None


class Issuer:
    def __init__(self):
        self.calls = []

    def issue(self, request):
        self.calls.append(request)
        return api.wire._json({"iceServers": [{"urls": sorted(api.wire.private.UDP_URLS),
            "username": "invented-api-user-" + str(len(self.calls)), "credential": CANARY}]})


class LocalTransport:
    def __init__(self, endpoint):
        self.endpoint, self.calls = endpoint, []
        self.lose = {}
        self.modify = None

    def send(self, request):
        self.calls.append(request)
        response = self.endpoint.handle(request)
        route = request.path.removeprefix(api.PREFIX)
        if self.lose.get(route, 0):
            self.lose[route] -= 1
            raise OSError(CANARY)
        return self.modify(response) if self.modify else response


class HttpResponse:
    def __init__(self, code, headers, body):
        self.status, self.headers, self.body = code, headers, body
        self.read_limits = []

    def getheaders(self):
        return self.headers

    def read(self, limit):
        self.read_limits.append(limit)
        return self.body[:limit]


class ConnectionFactory:
    """Replace only the socket/TLS boundary, retaining the real WSGI entry."""
    def __init__(self, endpoint):
        self.application = api.WsgiApplication(endpoint)
        self.connections = []
        self.modify = None

    def __call__(self, host, port, **kwargs):
        connection = HttpConnection(self, host, port, kwargs)
        self.connections.append(connection)
        return connection


class HttpConnection:
    def __init__(self, factory, host, port, options):
        self.factory, self.host, self.port, self.options = factory, host, port, options
        self.closed = False

    def request(self, method, path, *, body, headers):
        self.method, self.path, self.payload, self.headers = method, path, body, headers

    def getresponse(self):
        result = []
        environ = {"wsgi.url_scheme": "https", "REQUEST_METHOD": self.method, "PATH_INFO": self.path,
                   "CONTENT_LENGTH": str(len(self.payload)), "CONTENT_TYPE": self.headers["Content-Type"],
                   "HTTP_AUTHORIZATION": self.headers["Authorization"], "wsgi.input": io.BytesIO(self.payload)}
        body = b"".join(self.factory.application(environ, lambda status, headers: result.append((int(status[:3]), headers))))
        self.response = HttpResponse(result[0][0], result[0][1], body)
        return self.factory.modify(self.response) if self.factory.modify else self.response

    def close(self):
        self.closed = True


class ApiContractTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="authored-", dir=OUTPUT)
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.clock, self.issuer = Clock(), Issuer()
        stack = ExitStack()
        self.addCleanup(stack.close)
        for owner, name in ((socket, "socket"), (socket, "getaddrinfo"),
                            (api.wire.private, "_load_key"),
                            (api.wire.private, "issue_local"),
                            (api.wire.private, "issue_from_request"),
                            (api.wire.private.FixedCloudflareTransport, "generate")):
            stack.enter_context(patch.object(owner, name, side_effect=AssertionError("unexpected external operation")))
        self.store_path = self.newdir("store")
        self.store = self.open_store()
        self.approval = self.store.approve_pair("authored_host", "authored_guest", 100 + api.wire.private.MAX_TTL)
        self.endpoint = api.Api(self.store)
        self.transport = LocalTransport(self.endpoint)
        self.host = api.Client(self.transport, HOST, self.clock)
        self.guest = api.Client(self.transport, GUEST, self.clock)
        self.host.current()
        self.guest.current()
        self.cli_directories = {HOST: self.newdir("host-cli-binding"), GUEST: self.newdir("guest-cli-binding")}

    def open_store(self):
        cipher = api.service.parcels.ParcelCipher(CRYPTO, PARCEL_KEY)
        store = api.service.SessionStore(self.store_path, Verifier(), self.issuer, self.clock, IDENTITY_KEY, cipher)
        self.addCleanup(store.close)
        return store

    def newdir(self, name):
        path = self.root / name
        path.mkdir(mode=0o700)
        return path

    def consent(self):
        self.assertEqual(self.host.accept().status, "awaiting_pair_consent")
        self.assertEqual(self.guest.accept().status, "admitted")

    def stage(self, client, role, name="lobby", context=CONTEXT, suffix=""):
        directory = self.newdir(name + "-" + role + suffix)
        request = fixtures.request(role, context, self.clock.now)
        fixtures.write(directory / ("relay-" + role + ".request.json"), request)
        stage = client.stage(name, directory, after_authenticated_start=name == "game")
        self.addCleanup(stage.close)
        return stage, directory

    def prepare(self, name="lobby", context=CONTEXT):
        if name == "lobby":
            self.consent()
        host, host_dir = self.stage(self.host, "host", name, context)
        guest, guest_dir = self.stage(self.guest, "guest", name, context)
        self.assertEqual(host.request().status, "awaiting_peer_request")
        self.assertEqual(guest.request().status, "awaiting_peer_request")
        self.clock.now += 1
        return host, guest, host_dir, guest_dir

    def publish_record(self, stage, directory, role, context=CONTEXT, attempt=bytes(range(32))):
        name = api.wire._record_name(context, "offer" if role == "host" else "answer")
        fixtures.write(directory / name, fixtures.record(role, attempt=attempt))
        return stage.publish()

    def complete(self, name="lobby", context=CONTEXT):
        host, guest, host_dir, guest_dir = self.prepare(name, context)
        self.assertEqual(host.poll().status, "exchanging")
        self.assertEqual(guest.poll().status, "awaiting_peer_record")
        self.assertFalse((guest_dir / "relay-guest.json").exists())
        self.publish_record(host, host_dir, "host", context)
        self.clock.now += 1
        self.assertEqual(guest.poll().status, "exchanging")
        self.publish_record(guest, guest_dir, "guest", context)
        self.clock.now += 1
        self.assertEqual(host.poll().status, "service_delivery_complete")
        return host, guest, host_dir, guest_dir

    def refused(self, function, *args, **kwargs):
        with self.assertRaisesRegex(api.Refused, "^local signaling operation refused$"):
            function(*args, **kwargs)

    def raw(self, route, body=b"{}", token=HOST, **changes):
        request = api.Request("POST", api.PREFIX + route,
            (("Authorization", "Bearer " + token.decode("ascii")), ("Content-Type", "application/json")), body)
        return self.endpoint.handle(replace(request, **changes))

    def test_two_stage_delivery_uses_only_fixed_records_and_four_issuances(self):
        host, guest, host_dir, guest_dir = self.complete()
        host_grant = json.loads((host_dir / "relay-host.json").read_bytes())
        guest_grant = json.loads((guest_dir / "relay-guest.json").read_bytes())
        self.assertEqual((host_grant["role"], guest_grant["role"]), ("host", "guest"))
        self.assertNotEqual(host_grant["iceServers"][0]["username"], guest_grant["iceServers"][0]["username"])
        for directory, role in ((host_dir, "host"), (guest_dir, "guest")):
            self.assertEqual(set(p.name for p in directory.iterdir()), {
                "relay-" + role + ".request.json", "relay-" + role + ".json",
                api.wire._record_name(CONTEXT, "offer"), api.wire._record_name(CONTEXT, "answer")})
            self.assertTrue(all(p.stat().st_mode & 0o077 == 0 for p in directory.iterdir()))
        self.complete("game", GAME_CONTEXT)
        self.assertEqual(len(self.issuer.calls), 4)
        self.assertEqual([(r.stage, r.role) for r in self.issuer.calls],
                         [("lobby", "host"), ("lobby", "guest"), ("game", "host"), ("game", "guest")])
        self.assertTrue(all(call.path in {api.PREFIX + r for r in api.service.ROUTES} for call in self.transport.calls))
        visible = repr(host.poll()) + repr(self.transport.calls) + repr(self.raw("pair/current"))
        self.assertNotIn(CANARY, visible)
        self.assertNotIn(HOST.decode(), visible)
        self.assertNotIn(self.approval, visible)

    def test_unauthenticated_body_is_not_decoded_and_never_reaches_issuer(self):
        with patch.object(api.wire.private, "_decode", side_effect=AssertionError("body decoded")) as decode:
            reply = self.raw("stage/poll", b"not JSON", b"invented-forged-token")
        self.assertEqual(reply.code, 401)
        decode.assert_not_called()
        self.assertEqual(api._decode(reply).status, "authentication_required")
        self.assertEqual(self.issuer.calls, [])

    def test_no_body_identity_role_or_service_handle_can_admit_a_third_user(self):
        self.consent()
        cases = [{"subject": "authored_host"}, {"principal": {"subject": "authored_host"}},
                 {"approval": self.approval, "role": "host"}, {"approval": self.approval, "stage": "lobby"}]
        for value in cases:
            self.assertEqual(self.raw("stage/poll", api.wire._json(value), OTHER).code, 403)
        self.assertEqual(self.issuer.calls, [])

    def test_missing_or_duplicate_headers_and_nonfixed_routes_are_refused(self):
        for changes in ({"method": "GET"}, {"path": api.PREFIX + "stage/poll?token=" + CANARY},
                        {"path": "https://example.invalid/ce-signaling/v1/stage/poll"},
                        {"headers": ()}, {"headers": (("Authorization", "Bearer token"), ("authorization", "Bearer other"))},
                        {"headers": (("Authorization", "Bearer token\r\nX-Identity: forged"), ("Content-Type", "application/json"))},
                        {"body": b"x" * (api.MAX_BODY + 1)}):
            response = self.raw("pair/current", **changes)
            self.assertEqual(response.code, 400)
            self.assertNotIn(CANARY.encode(), response.body)
        self.assertEqual(self.issuer.calls, [])

    def test_pair_consent_is_explicit_and_replacement_is_not_silently_accepted(self):
        host, directory = self.stage(self.host, "host")
        self.assertEqual(host.request().status, "awaiting_pair_consent")
        self.assertEqual(self.issuer.calls, [])
        self.host.close_pair()
        self.store.approve_pair("authored_host", "authored_guest", self.clock.now + 1000)
        self.refused(self.host.current)
        self.assertEqual(self.host.accept().status, "invalid_request")
        self.assertFalse((directory / "relay-host.json").exists())

    def test_response_loss_after_request_and_issuance_reuses_saved_grant(self):
        self.consent()
        host, host_dir = self.stage(self.host, "host")
        guest, guest_dir = self.stage(self.guest, "guest")
        self.transport.lose["stage/request"] = 1
        self.refused(host.request)
        self.assertEqual(host.request().status, "awaiting_peer_request")
        guest.request()
        self.clock.now += 1
        self.transport.lose["stage/poll"] = 1
        self.refused(host.poll)
        self.assertEqual(len(self.issuer.calls), 1)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.clock.now += 1
        host.poll()
        saved = (host_dir / "relay-host.json").read_bytes()
        self.clock.now += 1
        host.poll()
        self.assertEqual((host_dir / "relay-host.json").read_bytes(), saved)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_record_and_ack_response_loss_is_immutable_and_does_not_reissue(self):
        host, guest, host_dir, guest_dir = self.prepare()
        host.poll()
        guest.poll()
        self.transport.lose["stage/record"] = 1
        self.refused(self.publish_record, host, host_dir, "host")
        self.assertEqual(host.publish().status, "exchanging")
        self.clock.now += 1
        self.transport.lose["stage/ack"] = 1
        self.refused(guest.poll)
        original = (guest_dir / api.wire._record_name(CONTEXT, "offer")).read_bytes()
        self.assertTrue((guest_dir / "relay-guest.json").exists())
        self.clock.now += 1
        guest.poll()
        self.assertEqual((guest_dir / api.wire._record_name(CONTEXT, "offer")).read_bytes(), original)
        self.publish_record(guest, guest_dir, "guest")
        self.clock.now += 1
        self.assertEqual(host.poll().status, "service_delivery_complete")
        self.assertEqual(len(self.issuer.calls), 2)

    def test_restart_redelivers_without_minting_again(self):
        host, guest, host_dir, guest_dir = self.prepare()
        host.poll()
        original = (host_dir / "relay-host.json").read_bytes()
        self.store.close()
        self.store = self.open_store()
        self.endpoint._backend = self.store
        replacement = api.Client(self.transport, HOST, self.clock)
        replacement.current()
        stage = replacement.stage("lobby", host_dir)
        self.addCleanup(stage.close)
        self.clock.now += 1
        stage.request()
        stage.poll()
        self.assertEqual((host_dir / "relay-host.json").read_bytes(), original)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_guest_offer_precedes_grant_and_all_destinations_preflight(self):
        host, guest, host_dir, guest_dir = self.prepare()
        host.poll()
        self.assertEqual(guest.poll().status, "awaiting_peer_record")
        self.publish_record(host, host_dir, "host")
        self.clock.now += 1
        conflict = guest_dir / "relay-guest.json"
        fixtures.write(conflict, b"authored unique pre-existing file")
        self.refused(guest.poll)
        offer = guest_dir / api.wire._record_name(CONTEXT, "offer")
        self.assertFalse(offer.exists())
        self.assertEqual(conflict.read_bytes(), b"authored unique pre-existing file")
        # A new fixture directory holds the same already-selected request;
        # removing the owned conflict is fixture cleanup, never product behavior.
        conflict.unlink()
        self.clock.now += 1
        order = []
        original = api.wire._publish
        def observe(directory, name, payload):
            order.append(name)
            original(directory, name, payload)
        with patch.object(api.wire, "_publish", side_effect=observe):
            guest.poll()
        self.assertEqual(order[:2], [offer.name, "relay-guest.json"])

    def test_changed_request_directory_and_symlink_cannot_receive_a_grant(self):
        host, guest, host_dir, guest_dir = self.prepare()
        request_path = host_dir / "relay-host.request.json"
        original = request_path.read_bytes()
        fixtures.write(request_path, fixtures.request("host", GAME_CONTEXT))
        self.refused(host.poll)
        fixtures.write(request_path, original)
        displaced = self.root / "displaced-host"
        host_dir.rename(displaced)
        host_dir.mkdir(mode=0o700)
        fixtures.write(host_dir / request_path.name, original)
        self.refused(host.poll)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.assertFalse((displaced / "relay-host.json").exists())
        symlink = self.root / "symlink"
        symlink.symlink_to(guest_dir, target_is_directory=True)
        self.refused(self.guest.stage, "lobby", symlink)
        self.assertEqual(self.issuer.calls, [])

    def test_wrong_role_or_context_grant_and_changed_record_digest_are_refused(self):
        host, guest, host_dir, guest_dir = self.prepare()
        def wrong_grant(response):
            data = json.loads(response.body)
            if "grant" in data:
                data["grant"]["context"] = GAME_CONTEXT
            return replace(response, body=api.wire._json(data))
        self.transport.modify = wrong_grant
        self.refused(host.poll)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.transport.modify = None
        self.clock.now += 1
        host.poll()
        guest.poll()
        self.publish_record(host, host_dir, "host")
        self.clock.now += 1
        def wrong_digest(response):
            data = json.loads(response.body)
            if "record" in data:
                data["recordDigest"] = "0" * 64
            return replace(response, body=api.wire._json(data))
        self.transport.modify = wrong_digest
        self.refused(guest.poll)
        self.assertFalse((guest_dir / "relay-guest.json").exists())
        self.assertFalse((guest_dir / api.wire._record_name(CONTEXT, "offer")).exists())

    def test_answer_attempt_must_match_own_offer_before_upload(self):
        host, guest, host_dir, guest_dir = self.prepare()
        host.poll()
        guest.poll()
        self.publish_record(host, host_dir, "host")
        self.clock.now += 1
        guest.poll()
        before = len(self.transport.calls)
        self.refused(self.publish_record, guest, guest_dir, "guest", attempt=b"z" * 32)
        self.assertEqual(len(self.transport.calls), before)
        self.assertEqual(len(self.issuer.calls), 2)

    def test_swapped_assigned_role_in_reply_cannot_publish_a_grant(self):
        host, guest, host_dir, guest_dir = self.prepare()
        def swapped(response):
            data = json.loads(response.body)
            data["role"] = "guest"
            return replace(response, body=api.wire._json(data))
        self.transport.modify = swapped
        self.refused(host.poll)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.assertFalse((host_dir / "relay-guest.json").exists())

    def test_delayed_response_and_backwards_clock_publish_nothing(self):
        host, guest, host_dir, guest_dir = self.prepare()
        def delay(response):
            self.clock.now = 220
            return response
        self.transport.modify = delay
        self.refused(host.poll)
        self.assertFalse((host_dir / "relay-host.json").exists())
        self.clock.now = 200
        self.refused(host.poll)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_server_setup_deadline_cannot_extend_original_request(self):
        host, guest, host_dir, guest_dir = self.prepare()
        def extend(response):
            data = json.loads(response.body)
            data["setupExpiresAt"] = 221
            return replace(response, body=api.wire._json(data))
        self.transport.modify = extend
        self.refused(host.poll)
        self.assertFalse((host_dir / "relay-host.json").exists())

    def test_game_cannot_start_without_prior_lobby_delivery_or_attestation(self):
        self.consent()
        stage, directory = self.stage(self.host, "host", "game", GAME_CONTEXT)
        self.assertEqual(stage.request().status, "invalid_request")
        self.refused(self.host.stage, "game", directory)
        self.assertEqual(self.issuer.calls, [])

    def test_client_rejects_redirect_cache_unknown_duplicate_or_oversized_reply(self):
        for change in (lambda response: replace(response, code=302),
                       lambda response: replace(response, headers=(("Content-Type", "application/json"),)),
                       lambda response: replace(response, body=b'{"schema":1,"status":"admitted","subject":"forged"}'),
                       lambda response: replace(response, body=b'{"schema":1,"status":"admitted","status":"closed"}'),
                       lambda response: replace(response, body=b"x" * (api.MAX_BODY + 1))):
            self.transport.modify = change
            self.refused(self.host.current)
        self.assertEqual(self.issuer.calls, [])

    def test_exception_messages_and_opaque_fields_never_reach_fixed_error_reply(self):
        with patch.object(self.store, "dispatch", side_effect=RuntimeError(CANARY)):
            response = self.raw("pair/current")
        self.assertEqual(response.code, 503)
        self.assertEqual(response.body, b'{"schema":1,"status":"unavailable"}\n')
        self.assertNotIn(CANARY, repr(response))
        self.refused(api.Client, self.transport, b"token\n" + CANARY.encode(), self.clock)

    def test_same_second_poll_is_bounded_and_no_retry_loop_exists(self):
        host, guest, host_dir, guest_dir = self.prepare()
        host.poll()
        before = len(self.transport.calls)
        self.assertEqual(host.poll().status, "rate_limited")
        self.assertEqual(len(self.transport.calls), before + 1)
        self.assertEqual(len(self.issuer.calls), 1)

    def test_https_fixed_origin_tls_and_wsgi_entry_use_no_ambient_credentials(self):
        factory = ConnectionFactory(self.endpoint)
        with patch.object(api.http.client, "HTTPSConnection", side_effect=factory), \
             patch.dict(os.environ, {"HTTPS_PROXY": "https://" + CANARY + ".invalid", "NETRC": CANARY}):
            client = api.Client(api.HttpsTransport("https://signaling.example.invalid:8443"), HOST, self.clock)
            self.assertEqual(client.current().status, "awaiting_pair_consent")
        connection = factory.connections[0]
        self.assertEqual((connection.host, connection.port, connection.path),
                         ("signaling.example.invalid", 8443, api.PREFIX + "pair/current"))
        context = connection.options["context"]
        self.assertTrue(context.check_hostname)
        self.assertEqual(context.verify_mode, api.ssl.CERT_REQUIRED)
        self.assertGreaterEqual(context.minimum_version, api.ssl.TLSVersion.TLSv1_2)
        self.assertEqual(connection.options["timeout"], 10)
        self.assertEqual(set(connection.headers), {"Authorization", "Content-Type"})
        self.assertEqual(connection.response.read_limits, [api.MAX_BODY + 1])
        self.assertTrue(connection.closed)
        self.assertEqual(self.issuer.calls, [])

    def test_invalid_https_origin_refuses_before_any_connection(self):
        with patch.object(api.http.client, "HTTPSConnection", side_effect=AssertionError("connection attempted")) as connect:
            for endpoint in ("http://signaling.example.invalid", "https://user:" + CANARY + "@example.invalid",
                             "https://example.invalid/path", "https://example.invalid?token=" + CANARY,
                             "https://example.invalid#fragment", "https://example.invalid:0", " https://example.invalid",
                             "https://example.invalid\n", "https://example..invalid"):
                self.refused(api.HttpsTransport, endpoint)
        connect.assert_not_called()

    def test_https_redirect_header_ambiguity_and_body_limits_are_refused(self):
        request = api.Request("POST", api.PREFIX + "pair/current",
            (("Authorization", "Bearer " + HOST.decode()), ("Content-Type", "application/json")), b"{}")
        for modify in (
            lambda r: HttpResponse(302, r.headers + [("Location", "https://other.invalid")], r.body),
            lambda r: HttpResponse(r.status, [("Content-Type", "application/json")], r.body),
            lambda r: HttpResponse(r.status, r.headers + [("Content-Length", str(len(r.body)))], r.body),
            lambda r: HttpResponse(r.status, list(api.HEADERS) + [("Content-Length", "65537")], b"x"),
            lambda r: HttpResponse(r.status, list(api.HEADERS) + [("Content-Length", "1")], b"{}"),
            lambda r: HttpResponse(r.status, list(api.HEADERS), b"x" * (api.MAX_BODY + 1)),
            lambda r: HttpResponse(r.status, list(api.HEADERS) + [("X-Extra", "x" * 1025)], r.body),
            lambda r: HttpResponse(r.status, list(api.HEADERS) + [("Content-Length", "2"), ("Transfer-Encoding", "chunked")], b"{}")):
            factory = ConnectionFactory(self.endpoint)
            factory.modify = modify
            with patch.object(api.http.client, "HTTPSConnection", side_effect=factory):
                self.refused(api.HttpsTransport("https://signaling.example.invalid").send, request)
            self.assertEqual(len(factory.connections), 1)
            self.assertTrue(factory.connections[0].closed)
        self.assertEqual(self.issuer.calls, [])

    def test_wsgi_rejects_http_query_and_bad_lengths_without_trusting_forwarded_identity(self):
        application = api.WsgiApplication(self.endpoint)
        environ = {"wsgi.url_scheme": "https", "REQUEST_METHOD": "POST", "PATH_INFO": api.PREFIX + "pair/current",
                   "CONTENT_LENGTH": "2", "CONTENT_TYPE": "application/json", "HTTP_AUTHORIZATION": "Bearer " + OTHER.decode(),
                   "HTTP_X_FORWARDED_USER": "authored_host", "HTTP_X_FORWARDED_PROTO": "https", "wsgi.input": io.BytesIO(b"{}")}
        result = []
        body = b"".join(application(environ, lambda status, headers: result.append((status, headers))))
        self.assertTrue(result[0][0].startswith("403"))
        self.assertEqual(json.loads(body)["status"], "not_admitted")
        for change in ({"wsgi.url_scheme": "http"}, {"QUERY_STRING": "token=" + CANARY},
                       {"CONTENT_LENGTH": "65537"}, {"CONTENT_LENGTH": "invalid"}, {"CONTENT_LENGTH": "3"}):
            result = []
            values = environ | {"wsgi.input": io.BytesIO(b"{}")} | change
            body = b"".join(application(values, lambda status, headers: result.append((status, headers))))
            self.assertTrue(result[0][0].startswith("400"))
            self.assertNotIn(CANARY.encode(), body)
        self.assertEqual(self.issuer.calls, [])

    def cli(self, operation, token, factory, stage=None, directory=None):
        path = self.root / ("authored-cli-token-" + str(len(factory.connections)))
        fixtures.write(path, token)
        fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
        self.addCleanup(os.close, fd)
        arguments = [operation, "--endpoint", "https://signaling.example.invalid", "--identity-token-fd", str(fd),
                     "--client-directory", str(self.cli_directories[token])]
        if stage:
            arguments += ["--stage", stage, "--directory", str(directory)]
            if stage == "game":
                arguments.append("--after-authenticated-start")
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(api.http.client, "HTTPSConnection", side_effect=factory), \
             patch.object(api.time, "time", return_value=self.clock.now), redirect_stdout(output), redirect_stderr(errors):
            code = api.main(arguments)
        self.assertEqual(errors.getvalue(), "")
        self.assertNotIn(token.decode(), output.getvalue())
        self.assertNotIn(CANARY, output.getvalue())
        self.assertNotIn(self.approval, output.getvalue())
        return code, json.loads(output.getvalue())

    def test_finite_cli_drives_real_entry_store_and_private_publication_without_sockets(self):
        factory = ConnectionFactory(self.endpoint)
        self.assertEqual(self.cli("current", HOST, factory)[1]["status"], "awaiting_pair_consent")
        self.assertEqual(self.cli("accept", HOST, factory)[1]["status"], "awaiting_pair_consent")
        self.assertEqual(self.cli("accept", GUEST, factory)[1]["status"], "admitted")
        host_dir, guest_dir = self.newdir("cli-host"), self.newdir("cli-guest")
        fixtures.write(host_dir / "relay-host.request.json", fixtures.request("host", CONTEXT))
        fixtures.write(guest_dir / "relay-guest.request.json", fixtures.request("guest", CONTEXT))
        self.cli("request", HOST, factory, "lobby", host_dir)
        self.cli("request", GUEST, factory, "lobby", guest_dir)
        self.clock.now += 1
        self.cli("poll", HOST, factory, "lobby", host_dir)
        self.assertEqual(self.cli("poll", GUEST, factory, "lobby", guest_dir)[1]["status"], "awaiting_peer_record")
        fixtures.write(host_dir / api.wire._record_name(CONTEXT, "offer"), fixtures.record("host"))
        self.cli("publish", HOST, factory, "lobby", host_dir)
        self.clock.now += 1
        self.cli("poll", GUEST, factory, "lobby", guest_dir)
        fixtures.write(guest_dir / api.wire._record_name(CONTEXT, "answer"), fixtures.record("guest"))
        self.cli("publish", GUEST, factory, "lobby", guest_dir)
        self.clock.now += 1
        self.assertEqual(self.cli("poll", HOST, factory, "lobby", host_dir)[1]["status"], "service_delivery_complete")
        self.assertEqual(len(self.issuer.calls), 2)
        self.assertTrue((host_dir / "relay-host.json").exists())
        self.assertTrue((guest_dir / "relay-guest.json").exists())
        self.assertTrue(all(c.closed for c in factory.connections))
        binding = self.cli_directories[HOST] / "signaling-pair.json"
        self.assertEqual(json.loads(binding.read_bytes())["approval"], self.approval)
        self.assertNotIn(HOST, binding.read_bytes())
        self.assertNotIn(CANARY.encode(), binding.read_bytes())

    def test_cli_pair_binding_refuses_replacement_across_invocations(self):
        api._pair_binding(self.cli_directories[HOST], self.host, None)
        original = (self.cli_directories[HOST] / "signaling-pair.json").read_bytes()
        self.host.close_pair()
        self.store.approve_pair("authored_host", "authored_guest", self.clock.now + 1000)
        replacement = api.Client(self.transport, HOST, self.clock)
        replacement.current()
        self.refused(api._pair_binding, self.cli_directories[HOST], replacement, None)
        self.assertEqual((self.cli_directories[HOST] / "signaling-pair.json").read_bytes(), original)
        self.assertEqual(self.issuer.calls, [])

    def test_pair_binding_cannot_add_metadata_to_the_ce_setup_directory(self):
        stage, directory = self.stage(self.host, "host")
        self.refused(api._pair_binding, directory, self.host, directory)
        self.assertFalse((directory / "signaling-pair.json").exists())

    def test_cli_can_close_from_saved_binding_after_call_budget_exhaustion(self):
        factory = ConnectionFactory(self.endpoint)
        self.cli("current", HOST, factory)
        with self.store._tx() as db:
            db.execute("UPDATE pairs SET calls=1024 WHERE id=?", (self.approval,))
        code, view = self.cli("close", HOST, factory)
        self.assertEqual((code, view["status"]), (0, "closed"))
        self.assertEqual(self.issuer.calls, [])

    def test_cli_argument_refusal_neither_reads_ambient_identity_nor_reflects_input(self):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(api, "_identity_token", side_effect=AssertionError("identity read")) as identity, \
             redirect_stdout(output), redirect_stderr(errors):
            code = api.main(["request", "--endpoint", "https://example.invalid", "--identity-token-fd", "0", "--unknown", CANARY])
        self.assertEqual(code, 1)
        self.assertEqual(output.getvalue(), "")
        self.assertEqual(errors.getvalue(), "local signaling operation refused\n")
        identity.assert_not_called()


def causal_controls():
    """Require selected integration cases to detect independently removed gates."""
    global api
    original = api
    source_path = Path(original.__file__)
    source = source_path.read_text()
    controls = (
        ("identity_token_forwarding", "self._backend.dispatch(route, token, request.body)",
         "self._backend.dispatch(route, b'invented-host-token', request.body)",
         "test_no_body_identity_role_or_service_handle_can_admit_a_third_user"),
        ("guest_offer_before_grant", 'elif self._role == "guest":', "elif False:",
         "test_guest_offer_precedes_grant_and_all_destinations_preflight"),
        ("all_targets_preflight", "        for name, payload in outputs:\n            wire._existing(self._directory, name, payload)",
         "        pass  # Authored in-memory removal of preflight.",
         "test_guest_offer_precedes_grant_and_all_destinations_preflight"),
        ("immutable_request", "require(wire._read(self._directory, self._name) == self._frozen)", "require(True)",
         "test_changed_request_directory_and_symlink_cannot_receive_a_grant"),
        ("delivery_digest", "require(hashlib.sha256(record).hexdigest() == digest)", "require(True)",
         "test_wrong_role_or_context_grant_and_changed_record_digest_are_refused"),
        ("setup_deadline", 'require(reply.setup_expires_at <= self._request["requestExpiresAt"])', "require(True)",
         "test_server_setup_deadline_cannot_extend_original_request"),
        ("tls_verification", "require(context.check_hostname and context.verify_mode == ssl.CERT_REQUIRED)",
         "context.check_hostname = False; context.verify_mode = ssl.CERT_NONE",
         "test_https_fixed_origin_tls_and_wsgi_entry_use_no_ambient_credentials"),
    )
    records = []
    try:
        for index, (name, before, after, case) in enumerate(controls):
            api = original
            positive = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(unittest.TestSuite([ApiContractTests(case)]))
            if not positive.wasSuccessful() or source.count(before) != 1:
                raise RuntimeError("causal positive or source selector refused")
            module_name = "authored_signaling_api_control_" + str(index)
            mutant = types.ModuleType(module_name)
            mutant.__file__ = str(source_path)
            sys.modules[module_name] = mutant
            try:
                exec(compile(source.replace(before, after, 1), str(source_path), "exec"), mutant.__dict__)
                api = mutant
                result = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(unittest.TestSuite([ApiContractTests(case)]))
                record = {"control": name, "case": case, "positive_passed": True,
                          "mutant_detected": not result.wasSuccessful(),
                          "failures": len(result.failures), "errors": len(result.errors)}
                records.append(record)
                if result.wasSuccessful() or result.testsRun != 1:
                    raise RuntimeError("causal mutant was not detected")
            finally:
                api = original
                del sys.modules[module_name]
        (OUTPUT / "causal-controls.private.json").write_text(json.dumps(records, indent=2) + "\n")
        print("LOCAL_SIGNALING_API_CAUSAL_PASS", len(records))
        return 0
    finally:
        api = original


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
    CRYPTO = api.service.parcels.Monocypher(args.crypto_library, args.crypto_sha256)
    if args.causal_controls_only:
        return causal_controls()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ApiContractTests))
    print("LOCAL_SIGNALING_API_CONTRACT_PASS" if result.wasSuccessful() else "LOCAL_SIGNALING_API_CONTRACT_FAIL")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
