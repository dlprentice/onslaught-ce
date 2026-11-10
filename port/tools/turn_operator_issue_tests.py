#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Invented local inputs and injected transport only; no real key or network."""
from concurrent.futures import ThreadPoolExecutor
import contextlib
import io
import json
import os
from pathlib import Path
import socket
import ssl
import stat
import tempfile
import threading
import unittest
from unittest import mock

import turn_operator_issue as issue


def fake_token():
    return "authored-only-" + "k" * 128


def body(user=120, password=128):
    return json.dumps({"iceServers": [
        {"urls": sorted(issue.STUN_URLS)},
        {"urls": sorted(issue.UDP_URLS | issue.OTHER_TURN_URLS), "username": "u" * user, "credential": "p" * password},
    ]}).encode("ascii")


class FakeTransport:
    def __init__(self, response=None, fail=False, before=None):
        self.response = response if response is not None else body()
        self.fail = fail
        self.before = before
        self.calls = 0
        self.ttls = []
        self.lock = threading.Lock()

    def generate(self, key, ttl):
        with self.lock:
            self.calls += 1
            self.ttls.append(ttl)
        if not isinstance(key, issue.ProviderKey) or key.api_token != fake_token():
            raise issue.Rejected("key file rejected")
        if self.before:
            self.before()
        if self.fail:
            raise RuntimeError("authored transport failure")
        return self.response


class FakeResponse:
    def __init__(self, data=None, status=200, url=None, headers=None):
        self.data = body() if data is None else data
        self.status = status
        self.url = url or issue.ENDPOINT_PREFIX + "authored_key" + issue.ENDPOINT_SUFFIX
        self.headers = {"Content-Type": "application/json", "Content-Length": str(len(self.data))}
        if headers:
            self.headers.update(headers)
        self.read_sizes = []

    def __enter__(self): return self
    def __exit__(self, *args): return None
    def geturl(self): return self.url
    def read(self, size): self.read_sizes.append(size); return self.data[:size]


class OfflineTests(unittest.TestCase):
    def setUp(self):
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        base = Path(__file__).resolve().parents[2] / "local-data/turn-operator-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=base)).resolve()
        self.root.chmod(0o700)
        for name in ("socket", "create_connection", "getaddrinfo"):
            self.stack.enter_context(mock.patch.object(socket, name, side_effect=AssertionError("offline test attempted network")))


class ProviderTests(OfflineTests):
    def test_exact_pilot_lifetime_request_bounds(self):
        self.assertEqual((issue.DEFAULT_TTL, issue.MAX_TTL), (7200, 14400))
        for ttl in (1, 7200, 14400):
            opener = mock.Mock(); opener.open.return_value = FakeResponse(status=201)
            with mock.patch.object(issue.urllib.request, "build_opener", return_value=opener):
                issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), ttl)
            self.assertEqual(json.loads(opener.open.call_args.args[0].data), {"ttl": ttl})
        with mock.patch.object(issue.urllib.request, "build_opener") as opener:
            for ttl in (0, 14401, True, -1):
                with self.assertRaises(issue.Rejected):
                    issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), ttl)
        opener.assert_not_called()

    def test_two_stun_urls_and_eight_total_urls_are_ignored_without_new_destinations(self):
        response = json.loads(body(64, 64))
        response["iceServers"][0]["urls"] = ["stun:stun.cloudflare.com:3478", "stun:stun.cloudflare.com:443"]
        self.assertEqual(sum(len(entry["urls"]) for entry in response["iceServers"]), 8)
        lease = issue.parse_provider_response(json.dumps(response).encode("ascii"))
        self.assertTrue(set(lease.relays[0].urls) == issue.UDP_URLS)
        self.assertTrue(len(lease.relays[0].username) == 64 and len(lease.relays[0].credential) == 64)
        response["iceServers"][0]["urls"].append("stun:stun.cloudflare.com:53")
        with self.assertRaises(issue.Rejected):
            issue.parse_provider_response(json.dumps(response).encode("ascii"))

    def test_created_status_accepted_with_honest_user_agent(self):
        opener = mock.Mock(); opener.open.return_value = FakeResponse(status=201)
        with mock.patch.object(issue.urllib.request, "build_opener", return_value=opener):
            result = issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), 120)
        self.assertTrue(result == body())
        self.assertEqual(opener.open.call_args.args[0].get_header("User-agent"), issue.USER_AGENT)

    def test_documented_shape_and_long_values(self):
        lease = issue.parse_provider_response(body())
        self.assertTrue(len(lease.relays) == 1 and set(lease.relays[0].urls) == issue.UDP_URLS)
        self.assertTrue(len(lease.relays[0].username) == 120 and len(lease.relays[0].credential) == 128)
        maximum = issue.parse_provider_response(body(513, 513))
        self.assertTrue(len(maximum.relays[0].username) == 513 and len(maximum.relays[0].credential) == 513)
        self.assertTrue("u" * 120 not in repr(lease) and "p" * 128 not in repr(lease))
        self.assertTrue(fake_token() not in repr(issue.ProviderKey("authored_key", fake_token())))

    def test_rejects_empty_overlong_or_control_credentials(self):
        for user, password in ((0, 128), (120, 0), (514, 128), (120, 514)):
            with self.subTest(lengths=(user, password)), self.assertRaises(issue.Rejected):
                issue.parse_provider_response(body(user, password))
        for control in ("\x00", "\n", " ", "\u2603"):
            response = json.loads(body())
            response["iceServers"][1]["credential"] = control
            with self.assertRaises(issue.Rejected):
                issue.parse_provider_response(json.dumps(response).encode("ascii"))

    def test_rejects_unknown_destinations_fields_duplicates_and_shapes(self):
        mutations = []
        for url in ("turn:other.invalid:3478?transport=udp", "turn:turn.cloudflare.com:9999?transport=udp", "turn:127.0.0.1:3478?transport=udp"):
            response = json.loads(body()); response["iceServers"][1]["urls"] = [url]; mutations.append(response)
        response = json.loads(body()); response["unexpected"] = 1; mutations.append(response)
        response = json.loads(body()); response["iceServers"][1]["apiToken"] = "invented"; mutations.append(response)
        response = json.loads(body()); response["iceServers"][1]["urls"] *= 2; mutations.append(response)
        response = json.loads(body()); response["iceServers"][1]["urls"] = sorted(issue.OTHER_TURN_URLS); mutations.append(response)
        mutations += [{"iceServers": []}, {"iceServers": {}}, {"iceServers": [{"urls": issue.STUN_URL}]}, {"iceServers": [True]}]
        for response in mutations:
            with self.assertRaises(issue.Rejected):
                issue.parse_provider_response(json.dumps(response).encode("ascii"))
        for data in (b"", b" " * (issue.MAX_BODY + 1), b'{"iceServers":[],"iceServers":[]}', b'{"iceServers":NaN}', body() + b"x"):
            with self.assertRaises(issue.Rejected): issue.parse_provider_response(data)

    def test_fixed_transport_request_and_no_environment_routing_or_tls_keylog(self):
        response = FakeResponse()
        opener = mock.Mock()
        opener.open.return_value = response
        keylog = self.root / "tls-keylog-must-not-exist"
        with mock.patch.dict(os.environ, {"SSLKEYLOGFILE": str(keylog), "HTTPS_PROXY": "http://other.invalid:9999"}, clear=True), mock.patch.object(issue.urllib.request, "build_opener", return_value=opener) as build:
            data = issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), 120)
        self.assertTrue(data == body())
        args, kwargs = opener.open.call_args
        request = args[0]
        self.assertTrue(request.full_url == response.url and request.get_method() == "POST")
        self.assertTrue(request.get_header("Authorization") == "Bearer " + fake_token())
        self.assertEqual(request.get_header("User-agent"), "OnslaughtCE-Relay-Operator/0.1")
        self.assertEqual(json.loads(request.data), {"ttl": 120})
        self.assertEqual(kwargs, {"timeout": issue.REQUEST_TIMEOUT})
        handlers = build.call_args.args
        proxy = next(h for h in handlers if isinstance(h, issue.urllib.request.ProxyHandler))
        https = next(h for h in handlers if isinstance(h, issue.urllib.request.HTTPSHandler))
        self.assertEqual(proxy.proxies, {})
        self.assertTrue(https._context.check_hostname and https._context.verify_mode == ssl.CERT_REQUIRED)
        self.assertTrue(https._context.keylog_filename is None)
        self.assertFalse(keylog.exists())
        self.assertTrue(any(isinstance(h, issue._NoRedirect) for h in handlers))
        self.assertEqual(response.read_sizes, [issue.MAX_BODY + 1])

    def test_transport_refuses_redirect_status_size_encoding_and_content_type(self):
        cases = [FakeResponse(status=302), FakeResponse(url="https://other.invalid/"),
                 FakeResponse(headers={"Content-Length": str(issue.MAX_BODY + 1)}),
                 FakeResponse(headers={"Content-Encoding": "gzip"}),
                 FakeResponse(headers={"Content-Type": "text/html"}), FakeResponse(data=b"x" * (issue.MAX_BODY + 1))]
        for response in cases:
            opener = mock.Mock(); opener.open.return_value = response
            with mock.patch.object(issue.urllib.request, "build_opener", return_value=opener), self.assertRaises(issue.Rejected):
                issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), 120)
            self.assertEqual(opener.open.call_count, 1)
        with self.assertRaises(issue.Rejected):
            issue._NoRedirect().redirect_request(None, None, 302, "", {}, "https://other.invalid/")

    def test_transport_rejects_key_path_injection_and_suppresses_exception_payloads(self):
        with mock.patch.object(issue.urllib.request, "build_opener") as build:
            for key in (issue.ProviderKey("../other", fake_token()), issue.ProviderKey("authored_key", "bad\nvalue")):
                with self.assertRaises(issue.Rejected): issue.FixedCloudflareTransport().generate(key, 120)
            build.assert_not_called()
        opener = mock.Mock(); opener.open.side_effect = RuntimeError("authored exception payload")
        with mock.patch.object(issue.urllib.request, "build_opener", return_value=opener):
            with self.assertRaises(issue.Rejected) as caught:
                issue.FixedCloudflareTransport().generate(issue.ProviderKey("authored_key", fake_token()), 120)
        self.assertEqual(str(caught.exception), "provider request failed")
        self.assertTrue(fake_token() not in str(caught.exception))


class FileTests(OfflineTests):
    def setUp(self):
        super().setUp()
        self.keys = self.root / "keys"; self.keys.mkdir(mode=0o700)
        self.output = self.root / "grants"; self.output.mkdir(mode=0o700)
        self.key = self.keys / "provider-key.json"
        self.key.write_text(json.dumps({"keyId": "authored_key", "apiToken": fake_token()}))
        self.key.chmod(0o600)
        self.context = "a" * 64

    def separate_keys(self, stem="separate", ending=b""):
        id_directory = self.root / (stem + "-id"); id_directory.mkdir(mode=0o700, exist_ok=True)
        api_directory = self.root / (stem + "-api"); api_directory.mkdir(mode=0o700, exist_ok=True)
        token_id = id_directory / "token-id.txt"
        api_token = api_directory / "api-token.txt"
        token_id.write_bytes(b"authored_key" + ending); token_id.chmod(0o600)
        api_token.write_bytes(fake_token().encode("ascii") + ending); api_token.chmod(0o600)
        return issue.SeparateKeyFiles(token_id, api_token)

    def test_separate_key_files_allow_exact_value_and_one_line_ending(self):
        for ending in (b"", b"\n", b"\r\n"):
            source = self.separate_keys(ending=ending)
            loaded = issue._load_key(source)
            self.assertTrue(loaded.key_id == "authored_key" and loaded.api_token == fake_token())
            self.assertTrue(fake_token() not in repr(loaded) + repr(source))

    def test_separate_key_files_use_same_issuance_limits_and_private_outputs(self):
        source = self.separate_keys(ending=b"\n")
        self.write_request(); transport = FakeTransport()
        issue.issue_from_request(source, self.output, "host", 600, transport, lambda: 100)
        self.assertEqual(transport.ttls, [600]); self.assertEqual(transport.calls, 1)
        self.assertTrue(fake_token().encode() not in (self.output / "relay-host.json").read_bytes())
        again = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_from_request(source, self.output, "host", 600, again, lambda: 100)
        self.assertEqual(again.calls, 0)

    def test_separate_key_file_and_parent_permissions_are_required(self):
        source = self.separate_keys()
        for path in (source.token_id_file, source.api_token_file, source.token_id_file.parent, source.api_token_file.parent):
            original = stat.S_IMODE(path.stat().st_mode)
            try:
                path.chmod(0o644 if path.is_file() else 0o755)
                transport = FakeTransport()
                with self.assertRaises(issue.Rejected):
                    issue.issue_local(source, self.output, "host", self.context, 600, transport, lambda: 100)
                self.assertEqual(transport.calls, 0)
            finally:
                path.chmod(original)

    def test_separate_key_symlink_files_and_parents_are_rejected(self):
        for field in ("token_id_file", "api_token_file"):
            source = self.separate_keys(stem="symlink-" + field)
            target = getattr(source, field)
            link = target.parent / "file-link.txt"; link.symlink_to(target)
            directory = self.root / (field + "-directory-link"); directory.symlink_to(target.parent, target_is_directory=True)
            for path in (link, directory / target.name):
                candidate = issue.SeparateKeyFiles(path if field == "token_id_file" else source.token_id_file,
                                                   path if field == "api_token_file" else source.api_token_file)
                transport = FakeTransport()
                with self.assertRaises(issue.Rejected):
                    issue.issue_local(candidate, self.output, "host", self.context, 600, transport, lambda: 100)
                self.assertEqual(transport.calls, 0)

    def test_separate_key_hardlinks_and_owner_mismatch_are_rejected(self):
        for field in ("token_id_file", "api_token_file"):
            source = self.separate_keys(stem="hardlink-" + field)
            target = getattr(source, field); link = target.parent / "hard-link.txt"
            os.link(target, link)
            transport = FakeTransport()
            with self.assertRaises(issue.Rejected):
                issue.issue_local(source, self.output, "host", self.context, 600, transport, lambda: 100)
            self.assertEqual(transport.calls, 0)
        source = self.separate_keys(stem="owner")
        actual_uid = os.geteuid()
        with mock.patch.object(issue.os, "geteuid", return_value=actual_uid + 1), self.assertRaises(issue.Rejected):
            issue._load_key(source)

    def test_separate_key_content_is_exact_bounded_ascii_without_extra_lines(self):
        cases = [("token_id_file", value) for value in (b"", b"bad/id", b" authored_key", b"authored_key ", b"authored_key\n\n", b"i" * 129)]
        cases += [("api_token_file", value) for value in (b"", b" bad", b"bad ", b"bad\r", b"bad\nextra", b"bad\n\n", b"t" * 2049, "not-ascii-\u2603".encode("utf-8"))]
        for index, (field, value) in enumerate(cases):
            source = self.separate_keys(stem="invalid-" + str(index))
            getattr(source, field).write_bytes(value)
            with self.assertRaises(issue.Rejected): issue._load_key(source)
        source = self.separate_keys(stem="maximum")
        source.token_id_file.write_bytes(b"i" * 128 + b"\r\n")
        source.api_token_file.write_bytes(b"t" * 2048 + b"\r\n")
        loaded = issue._load_key(source)
        self.assertTrue(len(loaded.key_id) == 128 and len(loaded.api_token) == 2048)

    def test_separate_key_inputs_remain_outside_client_grants_and_distinct(self):
        source = self.separate_keys()
        with self.assertRaises(issue.Rejected):
            issue._load_key(issue.SeparateKeyFiles(source.token_id_file, source.token_id_file))
        for field in ("token_id_file", "api_token_file"):
            target = self.output / (field + ".txt")
            target.write_bytes(getattr(source, field).read_bytes()); target.chmod(0o600)
            candidate = issue.SeparateKeyFiles(target if field == "token_id_file" else source.token_id_file,
                                               target if field == "api_token_file" else source.api_token_file)
            transport = FakeTransport()
            with self.assertRaises(issue.Rejected):
                issue.issue_local(candidate, self.output, "host", self.context, 600, transport, lambda: 100)
            self.assertEqual(transport.calls, 0)

    def invoke(self, transport=None, role="host", now=100, ttl=600, context=None):
        transport = transport or FakeTransport()
        issue.issue_local(self.key, self.output, role, context or self.context, ttl, transport, lambda: now)
        return transport

    def write_request(self, role="host", directory=None, changes=None):
        directory = directory or self.output
        value = {"schema": 1, "role": role, "context": self.context,
                 "requestedAt": 100, "requestExpiresAt": 280, "grantExpiresAt": 700}
        value.update(changes or {})
        path = directory / ("relay-" + role + ".request.json")
        path.write_text(json.dumps(value)); path.chmod(0o600)
        return path

    def watch(self, transport=None, seconds=1, after_pause=None):
        elapsed = [0.0]
        self.elapsed_watch = elapsed
        def pause(delta):
            elapsed[0] += delta
            if after_pause: after_pause()
        return issue.watch_local(self.key, self.output, 600, seconds, transport or FakeTransport(),
                                 lambda: 100, lambda: elapsed[0], pause)

    def test_watcher_services_exactly_two_roles_once(self):
        self.write_request("host"); self.write_request("guest")
        transport = FakeTransport()
        self.assertEqual(self.watch(transport), 2)
        self.assertEqual(transport.calls, 2)
        self.assertTrue((self.output / "relay-host.json").is_file() and (self.output / "relay-guest.json").is_file())
        again = FakeTransport()
        with self.assertRaises(issue.Rejected): self.watch(again)
        self.assertEqual(again.calls, 0)

    def test_watcher_waits_for_guest_without_reissuing_host(self):
        self.write_request("host")
        transport = FakeTransport()
        self.assertEqual(self.watch(transport, after_pause=lambda: self.write_request("guest")), 2)
        self.assertEqual(transport.calls, 2)

    def test_watcher_missing_requests_expires_without_key_reads_or_spending(self):
        transport = FakeTransport()
        with mock.patch.object(issue, "_load_key") as key, self.assertRaises(issue.Rejected):
            self.watch(transport)
        key.assert_not_called()
        self.assertEqual(transport.calls, 0)
        self.assertLessEqual(self.elapsed_watch[0], 1.1)

    def test_watcher_pins_first_context_before_guest_spending(self):
        self.write_request("host"); self.write_request("guest", changes={"context": "b" * 64})
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected): self.watch(transport)
        self.assertEqual(transport.calls, 1)
        self.assertFalse((self.output / "relay-guest.json").exists())

    def test_watcher_failure_and_invalid_lifetime_never_retry(self):
        self.write_request("host"); self.write_request("guest")
        transport = FakeTransport(fail=True)
        with self.assertRaises(issue.Rejected): self.watch(transport)
        self.assertEqual(transport.calls, 1)
        again = FakeTransport()
        with self.assertRaises(issue.Rejected): self.watch(again)
        self.assertEqual(again.calls, 0)
        for seconds in (0, 181, True):
            with self.assertRaises(issue.Rejected): self.watch(FakeTransport(), seconds=seconds)

    def test_pinned_directory_identity_is_checked_before_spending(self):
        self.write_request()
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_from_request(self.key, self.output, "host", 600, transport, lambda: 100,
                                     expected_directory=(-1, -1))
        with self.assertRaises(issue.Rejected):
            issue.issue_local(self.key, self.output, "host", self.context, 600, transport, lambda: 100,
                             expected_directory=(-1, -1))
        self.assertEqual(transport.calls, 0)

    def test_reads_actual_preparation_context_and_clamps_grant(self):
        self.write_request(changes={"grantExpiresAt": 500})
        transport = FakeTransport()
        issue.issue_from_request(self.key, self.output, "host", 600, transport, lambda: 100)
        self.assertEqual(transport.ttls, [400])
        grant = json.loads((self.output / "relay-host.json").read_bytes())
        self.assertTrue(grant["context"] == self.context and grant["role"] == "host")
        self.assertEqual(grant["expiresAt"], 500)

    def test_default_pilot_ttl_is_explicitly_clamped_after_setup(self):
        self.write_request(changes={"grantExpiresAt": 7300})
        clock = iter((150, 150, 151)); transport = FakeTransport()
        issue.issue_from_request(self.key, self.output, "host", issue.DEFAULT_TTL, transport, lambda: next(clock))
        self.assertEqual(transport.ttls, [7150])
        grant = json.loads((self.output / "relay-host.json").read_bytes())
        self.assertEqual((grant["issuedAt"], grant["expiresAt"]), (150, 7300))

    def test_four_hour_grant_is_bounded_and_does_not_enable_renewal(self):
        self.write_request(changes={"grantExpiresAt": 14500})
        transport = FakeTransport()
        issue.issue_from_request(self.key, self.output, "host", 14400, transport, lambda: 100)
        self.assertEqual(transport.ttls, [14400])
        self.assertEqual(json.loads((self.output / "relay-host.json").read_bytes())["expiresAt"], 14500)
        again = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_from_request(self.key, self.output, "host", 14400, again, lambda: 100)
        self.assertEqual(again.calls, 0)

    def test_long_grant_request_does_not_extend_negotiation_or_watch(self):
        self.write_request(changes={"grantExpiresAt": 14500})
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_from_request(self.key, self.output, "host", 7200, transport, lambda: 280)
        with self.assertRaises(issue.Rejected):
            issue.watch_local(self.key, self.output, 7200, 181, transport, lambda: 100)
        self.assertEqual(transport.calls, 0)

    def test_provider_cannot_echo_master_token_into_client_credentials(self):
        for field in ("username", "credential"):
            directory = self.output / field; directory.mkdir(mode=0o700)
            response = json.loads(body())
            response["iceServers"][1][field] = "prefix-" + fake_token() + "-suffix"
            transport = FakeTransport(response=json.dumps(response).encode("ascii"))
            with self.assertRaises(issue.Rejected):
                issue.issue_local(self.key, directory, "host", self.context, 600, transport, lambda: 100)
            self.assertEqual(transport.calls, 1)
            self.assertFalse((directory / "relay-host.json").exists())
            self.assertTrue(all(fake_token().encode() not in p.read_bytes() for p in directory.iterdir()))

    def test_one_shot_binds_request_directory_across_key_read(self):
        self.write_request()
        original_load = issue._load_key
        retired = self.root / "retired-request-directory"
        def replace_directory(path):
            result = original_load(path)
            self.output.rename(retired)
            self.output.mkdir(mode=0o700)
            self.write_request(changes={"context": "b" * 64})
            return result
        transport = FakeTransport()
        with mock.patch.object(issue, "_load_key", side_effect=replace_directory), self.assertRaises(issue.Rejected):
            issue.issue_from_request(self.key, self.output, "host", 600, transport, lambda: 100)
        self.assertEqual(transport.calls, 0)
        self.assertTrue((retired / "relay-host.request.json").is_file())
        self.assertFalse((self.output / "relay-host.json").exists())

    def test_rejects_stale_mismatched_and_permissive_preparation_requests(self):
        changes_list = ({"role": "guest"}, {"requestExpiresAt": 100}, {"requestedAt": 101},
                        {"requestExpiresAt": 281}, {"grantExpiresAt": 100 + issue.MAX_TTL + 1}, {"context": "invalid"})
        for index, changes in enumerate(changes_list):
            directory = self.output / str(index); directory.mkdir(mode=0o700)
            self.write_request(directory=directory, changes=changes)
            transport = FakeTransport()
            with self.assertRaises(issue.Rejected):
                issue.issue_from_request(self.key, directory, "host", 600, transport, lambda: 100)
            self.assertEqual(transport.calls, 0)
        request = self.write_request(); request.chmod(0o644)
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_from_request(self.key, self.output, "host", 600, transport, lambda: 100)
        self.assertEqual(transport.calls, 0)

    def test_request_expiry_and_clock_rollback_are_rechecked_before_spending(self):
        self.write_request()
        for times in ((100, 99), (100, 280)):
            clock = iter(times); transport = FakeTransport()
            with self.assertRaises(issue.Rejected):
                issue.issue_from_request(self.key, self.output, "host", 600, transport, lambda: next(clock))
            self.assertEqual(transport.calls, 0)

    def test_writes_private_bound_grant_without_key_and_reserves_before_request(self):
        transport = FakeTransport(before=lambda: self.assertTrue((self.output / "relay-host.attempt.json").is_file()))
        self.invoke(transport)
        grant = self.output / "relay-host.json"
        data = grant.read_bytes(); parsed = json.loads(data)
        self.assertTrue(set(parsed) == {"schema", "role", "context", "issuedAt", "expiresAt", "iceServers"})
        self.assertTrue(parsed["role"] == "host" and parsed["context"] == self.context)
        self.assertEqual((parsed["issuedAt"], parsed["expiresAt"]), (100, 700))
        self.assertTrue(fake_token().encode() not in data and b"apiToken" not in data and b"keyId" not in data)
        self.assertTrue(len(parsed["iceServers"][0]["username"]) == 120)
        for file in self.output.iterdir():
            self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
        self.assertEqual(transport.calls, 1)

    def test_two_roles_share_context_and_session_deadline_without_renewal(self):
        first = self.invoke(ttl=issue.MAX_TTL)
        second = self.invoke(role="guest", now=110, ttl=issue.MAX_TTL)
        self.assertEqual((first.calls, second.calls), (1, 1))
        self.assertEqual(second.ttls, [issue.MAX_TTL - 10])
        data = json.loads((self.output / "relay-guest.json").read_bytes())
        self.assertEqual((data["issuedAt"], data["expiresAt"]), (110, 100 + issue.MAX_TTL))
        repeated = FakeTransport()
        with self.assertRaises(issue.Rejected): self.invoke(repeated)
        self.assertEqual(repeated.calls, 0)

    def test_failure_keeps_attempt_receipt_and_new_process_cannot_retry(self):
        first = FakeTransport(fail=True)
        with self.assertRaises(issue.Rejected): self.invoke(first)
        self.assertTrue((self.output / "relay-host.attempt.json").is_file())
        self.assertFalse((self.output / "relay-host.json").exists())
        second = FakeTransport()
        with self.assertRaises(issue.Rejected): self.invoke(second)
        self.assertEqual((first.calls, second.calls), (1, 0))

    def test_response_rejection_and_expiry_keep_attempt_receipts(self):
        with self.assertRaises(issue.Rejected): self.invoke(FakeTransport(response=b"{}"))
        self.assertFalse((self.output / "relay-host.json").exists())
        clock = iter((100, 700))
        with self.assertRaises(issue.Rejected):
            issue.issue_local(self.key, self.output, "guest", self.context, 600, FakeTransport(), lambda: next(clock))
        self.assertFalse((self.output / "relay-guest.json").exists())
        self.assertTrue((self.output / "relay-guest.attempt.json").is_file())

    def test_wrong_or_expired_session_context_never_requests(self):
        self.invoke()
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected): self.invoke(transport, role="guest", context="b" * 64)
        with self.assertRaises(issue.Rejected): self.invoke(transport, role="guest", now=100 + issue.MAX_TTL)
        self.assertEqual(transport.calls, 0)

    def test_existing_session_record_cannot_loosen_lifetime_or_types(self):
        records = [
            {"schema": True, "context": self.context, "issuedAt": 100, "expiresAt": 700},
            {"schema": 1, "context": self.context, "issuedAt": True, "expiresAt": 700},
            {"schema": 1, "context": self.context, "issuedAt": 100, "expiresAt": 100 + issue.MAX_TTL + 1},
            {"schema": 1, "context": self.context, "issuedAt": 100, "expiresAt": 700, "unknown": 1},
        ]
        for index, record in enumerate(records):
            directory = self.output / str(index); directory.mkdir(mode=0o700)
            metadata = directory / "relay-session.json"
            metadata.write_text(json.dumps(record)); metadata.chmod(0o600)
            transport = FakeTransport()
            with self.assertRaises(issue.Rejected):
                issue.issue_local(self.key, directory, "host", self.context, 120, transport, lambda: 100)
            self.assertEqual(transport.calls, 0)

    def test_invalid_limits_and_context_never_request(self):
        for ttl in (0, 14401, True, -1):
            transport = FakeTransport()
            with self.assertRaises(issue.Rejected): self.invoke(transport, ttl=ttl)
            self.assertEqual(transport.calls, 0)
        for context in ("short", "A" * 64, "g" * 64):
            with self.assertRaises(issue.Rejected): self.invoke(context=context)
        self.assertFalse(list(self.output.iterdir()))

    def test_existing_unique_grant_is_not_overwritten_and_does_not_spend(self):
        target = self.output / "relay-host.json"
        target.write_bytes(b"authored retained output"); target.chmod(0o600)
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected): self.invoke(transport)
        self.assertEqual(target.read_bytes(), b"authored retained output")
        self.assertEqual(transport.calls, 0)

    def test_permission_checked_key_and_output(self):
        transport = FakeTransport()
        for path in (self.key, self.keys, self.output):
            original = stat.S_IMODE(path.stat().st_mode)
            path.chmod(0o644 if path.is_file() else 0o755)
            with self.assertRaises(issue.Rejected): self.invoke(transport)
            path.chmod(original)
        self.assertEqual(transport.calls, 0)

    def test_key_must_stay_outside_client_grant_directory(self):
        inside = self.output / "provider-key.json"
        inside.write_bytes(self.key.read_bytes()); inside.chmod(0o600)
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_local(inside, self.output, "host", self.context, 120, transport, lambda: 100)
        self.assertEqual(transport.calls, 0)

    def test_symlink_ancestors_files_and_hardlinks_are_rejected(self):
        alias = self.root / "key-link.json"; alias.symlink_to(self.key)
        directory_alias = self.root / "keys-alias"; directory_alias.symlink_to(self.keys, target_is_directory=True)
        transport = FakeTransport()
        for key in (alias, directory_alias / self.key.name):
            with self.assertRaises(issue.Rejected):
                issue.issue_local(key, self.output, "host", self.context, 120, transport, lambda: 100)
        hardlink = self.keys / "hardlink.json"; os.link(self.key, hardlink)
        with self.assertRaises(issue.Rejected): self.invoke(transport)
        self.assertEqual(transport.calls, 0)

    def test_output_symlink_and_changed_privacy_never_write_grant(self):
        alias = self.root / "grants-alias"; alias.symlink_to(self.output, target_is_directory=True)
        transport = FakeTransport()
        with self.assertRaises(issue.Rejected):
            issue.issue_local(self.key, alias, "host", self.context, 120, transport, lambda: 100)
        self.assertEqual(transport.calls, 0)
        transport = FakeTransport(before=lambda: self.output.chmod(0o755))
        with self.assertRaises(issue.Rejected): self.invoke(transport)
        self.assertFalse((self.output / "relay-host.json").exists())
        self.assertTrue((self.output / "relay-host.attempt.json").is_file())

    def test_concurrent_operator_attempts_spend_once(self):
        transport = FakeTransport()
        barrier = threading.Barrier(2)
        def invoke_once():
            barrier.wait()
            try:
                self.invoke(transport)
                return True
            except issue.Rejected:
                return False
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: invoke_once(), range(2)))
        self.assertEqual(sum(results), 1)
        self.assertEqual(transport.calls, 1)

    def test_final_files_appear_only_after_complete_private_publication(self):
        real_publish = issue._publish_no_replace
        observed = []
        def publish(directory, temporary, final):
            with self.assertRaises(FileNotFoundError):
                os.stat(final, dir_fd=directory, follow_symlinks=False)
            fd = os.open(temporary, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
            try:
                info = os.fstat(fd)
                payload = os.read(fd, issue.MAX_BODY + 1)
                self.assertTrue(bool(json.loads(payload)))
                self.assertEqual((stat.S_IMODE(info.st_mode), info.st_nlink), (0o600, 1))
            finally:
                os.close(fd)
            real_publish(directory, temporary, final)
            self.assertEqual(os.stat(final, dir_fd=directory, follow_symlinks=False).st_nlink, 1)
            observed.append(final)
        with mock.patch.object(issue, "_publish_no_replace", side_effect=publish): self.invoke()
        self.assertEqual(set(observed), {"relay-session.json", "relay-host.attempt.json", "relay-host.json"})

    def test_publication_failure_keeps_receipt_without_visible_partial_grant(self):
        real_publish = issue._publish_no_replace
        def publish(directory, temporary, final):
            if final == "relay-host.json": raise issue.Rejected("grant write failed")
            real_publish(directory, temporary, final)
        transport = FakeTransport()
        with mock.patch.object(issue, "_publish_no_replace", side_effect=publish), self.assertRaises(issue.Rejected):
            self.invoke(transport)
        self.assertEqual(transport.calls, 1)
        self.assertFalse((self.output / "relay-host.json").exists())
        self.assertTrue((self.output / "relay-host.attempt.json").is_file())
        next_transport = FakeTransport()
        with self.assertRaises(issue.Rejected): self.invoke(next_transport)
        self.assertEqual(next_transport.calls, 0)

    def test_cli_requires_explicit_issue_and_never_echoes_input(self):
        self.write_request()
        arguments = ["--key-file", str(self.key), "--directory", str(self.output), "--role", "host"]
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.FixedCloudflareTransport, "generate") as provider:
            self.assertEqual(issue.main(arguments), 1)
            self.assertEqual(issue.main(["--ttl", fake_token()]), 1)
        provider.assert_not_called()
        self.assertTrue(fake_token() not in stdout.getvalue() + stderr.getvalue())

    def test_operator_cli_uses_private_preparation_request_without_context_argument(self):
        self.write_request()
        arguments = ["--issue", "--key-file", str(self.key), "--directory", str(self.output), "--role", "host"]
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.time, "time", return_value=100), mock.patch.object(issue.FixedCloudflareTransport, "generate", return_value=body()) as provider:
            self.assertEqual(issue.main(arguments), 0)
        self.assertEqual(provider.call_count, 1)
        self.assertTrue(fake_token() not in stdout.getvalue() + stderr.getvalue())
        self.assertTrue((self.output / "relay-host.json").is_file())

    def test_cli_default_pilot_ttl_and_over_maximum_are_exact(self):
        self.write_request(changes={"grantExpiresAt": 7300})
        arguments = ["--issue", "--key-file", str(self.key), "--directory", str(self.output), "--role", "host"]
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.time, "time", return_value=100), mock.patch.object(issue.FixedCloudflareTransport, "generate", return_value=body()) as provider:
            self.assertEqual(issue.main(arguments), 0)
        self.assertEqual(provider.call_args.args[-1], 7200)
        directory = self.output / "over-maximum"; directory.mkdir(mode=0o700)
        self.write_request(directory=directory, changes={"grantExpiresAt": 14500})
        arguments = ["--issue", "--key-file", str(self.key), "--directory", str(directory), "--role", "host", "--ttl", "14401"]
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.time, "time", return_value=100), mock.patch.object(issue.FixedCloudflareTransport, "generate") as provider:
            self.assertEqual(issue.main(arguments), 1)
        provider.assert_not_called()

    def test_operator_cli_watcher_is_explicit_and_bounded(self):
        self.write_request("host"); self.write_request("guest")
        arguments = ["--issue", "--key-file", str(self.key), "--directory", str(self.output),
                     "--role", "both", "--watch-seconds", "120"]
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.time, "time", return_value=100), mock.patch.object(issue.FixedCloudflareTransport, "generate", return_value=body()) as provider:
            self.assertEqual(issue.main(arguments), 0)
        self.assertEqual(provider.call_count, 2)
        self.assertTrue(fake_token() not in stdout.getvalue() + stderr.getvalue())

    def test_cli_rejects_partial_mixed_duplicate_or_same_file_key_options(self):
        source = self.separate_keys(); self.write_request()
        json_option = ["--key-file", str(self.key)]
        id_option = ["--token-id-file", str(source.token_id_file)]
        api_option = ["--api-token-file", str(source.api_token_file)]
        cases = [[], id_option, api_option, json_option + id_option, json_option + api_option,
                 json_option + id_option + api_option, json_option * 2, id_option * 2 + api_option,
                 id_option + api_option * 2,
                 id_option + ["--api-token-file", str(source.token_id_file)]]
        base = ["--issue", "--directory", str(self.output), "--role", "host"]
        for options in cases:
            stdout, stderr = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue, "issue_from_request") as one, mock.patch.object(issue, "watch_local") as both, mock.patch.object(issue, "_load_key") as load:
                self.assertEqual(issue.main(base + options), 1)
            one.assert_not_called(); both.assert_not_called(); load.assert_not_called()
            self.assertTrue(fake_token() not in stdout.getvalue() + stderr.getvalue())

    def test_cli_separate_key_files_support_one_shot_and_bounded_watcher(self):
        for role in ("host", "both"):
            directory = self.output / role; directory.mkdir(mode=0o700)
            self.write_request("host", directory=directory)
            if role == "both": self.write_request("guest", directory=directory)
            source = self.separate_keys(stem="cli-" + role, ending=b"\r\n")
            arguments = ["--issue", "--token-id-file", str(source.token_id_file), "--api-token-file", str(source.api_token_file),
                         "--directory", str(directory), "--role", role]
            if role == "both": arguments += ["--watch-seconds", "120"]
            stdout, stderr = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), mock.patch.object(issue.time, "time", return_value=100), mock.patch.object(issue.FixedCloudflareTransport, "generate", return_value=body()) as provider:
                self.assertEqual(issue.main(arguments), 0)
            self.assertEqual(provider.call_count, 2 if role == "both" else 1)
            self.assertTrue(fake_token() not in stdout.getvalue() + stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
