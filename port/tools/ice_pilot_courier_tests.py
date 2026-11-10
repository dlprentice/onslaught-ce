#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored offline courier checks; optional pinned real exchange consumer."""
from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack, redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import select
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import ice_pilot_courier as courier

ROOT = Path(__file__).resolve().parents[2]
OUTPUT: Path
EXCHANGE_BINARY: Path | None = None
NOW = 110
CONTEXT = hashlib.sha256(b"authored courier fixture context").hexdigest()
MONOCYPHER_URL = "https://monocypher.org/download/monocypher-4.0.2.tar.gz"
MONOCYPHER_SHA256 = "38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c"
MONOCYPHER_LICENSE = "BSD-2-Clause OR CC0-1.0"
EXCHANGE_PINS = {
    "ice_exchange.cpp": "b33b5f779741d75f1c0279f6c1e89401438ea352e07c60382566444d48055cf9",
    "ice_exchange.h": "822373dbd060e019242dc708fa754442fca27d1b42d3bc402e51b438ebd4a9b3",
    "network.h": "3a02a7717e42e85a9f57a2f4b068b929f7cc78789ef1fd60f3ba00b1b49d27ec",
}


def request(role: str, context: str = CONTEXT, start: int = 100) -> dict:
    return {"schema": 1, "role": role, "context": context, "requestedAt": start,
            "requestExpiresAt": start + 120, "grantExpiresAt": start + courier.private.MAX_TTL}


def grant(role: str, context: str = CONTEXT) -> dict:
    return {"schema": 1, "role": role, "context": context, "issuedAt": 105, "expiresAt": 100 + courier.private.MAX_TTL,
            "iceServers": [{"urls": ["stun:stun.cloudflare.com:3478", "stun:stun.cloudflare.com:443"]},
                           {"urls": sorted(courier.private.UDP_URLS | courier.private.OTHER_TURN_URLS),
                            "username": "invented-courier-user", "credential": "invented-courier-password"}]}


def record(role: str, attempt: bytes = bytes(range(32)), end: int = 400) -> bytes:
    # Authored envelope only; the optional C++ check supplies real AEAD records.
    return b"CEICE002" + bytes([1 if role == "host" else 2]) + struct.pack("<QQ", 100, end) + attempt + bytes(24) + struct.pack("<H", 3) + b"xyz" + bytes(16)


def write(path: Path, value: bytes | dict) -> None:
    data = courier._json(value) if isinstance(value, dict) else value
    with path.open("wb") as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(data)


class CourierTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="fixture-", dir=OUTPUT)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.host = self.newdir("host")
        self.guest = self.newdir("guest")
        self.context = CONTEXT
        write(self.host / "relay-host.request.json", request("host"))
        write(self.guest / "relay-guest.request.json", request("guest"))
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for owner, name in ((socket, "socket"), (socket, "getaddrinfo"), (courier.private, "_load_key"),
                            (courier.private, "issue_local"), (courier.private, "issue_from_request"),
                            (courier.private.FixedCloudflareTransport, "generate")):
            self.stack.enter_context(patch.object(owner, name, side_effect=AssertionError("unexpected external operation")))

    def newdir(self, name: str) -> Path:
        path = self.root / name
        path.mkdir(mode=0o700)
        return path

    def run_transfer(self, action: str, kind: str, session: Path, parcel: Path, now: int = NOW):
        courier.transfer(action, kind, session, parcel, lambda: now)

    def refused(self, action: str, kind: str, session: Path, parcel: Path, now: int = NOW):
        with self.assertRaisesRegex(courier.Refused, "^pilot transfer refused$"):
            self.run_transfer(action, kind, session, parcel, now)

    def route(self, kind: str, source: Path, target: Path) -> Path:
        outgoing, incoming = self.newdir(kind + "-out"), self.newdir(kind + "-in")
        self.run_transfer("export", kind, source, outgoing)
        # This authored copy stands in for a separately chosen private channel.
        write(incoming / courier.PARCEL, (outgoing / courier.PARCEL).read_bytes())
        self.run_transfer("import", kind, target, incoming)
        return incoming

    def offer_ready(self):
        self.route("request", self.guest, self.host)
        for role in ("host", "guest"):
            write(self.host / ("relay-" + role + ".json"), grant(role, self.context))
        write(self.host / courier._record_name(self.context, "offer"), record("host"))

    def test_complete_three_transfers_and_private_bytes(self):
        self.offer_ready()
        self.route("offer", self.host, self.guest)
        write(self.guest / courier._record_name(self.context, "answer"), record("guest"))
        self.route("answer", self.guest, self.host)
        for name in (courier._record_name(self.context, "offer"), courier._record_name(self.context, "answer"), "relay-guest.json", "relay-guest.request.json"):
            self.assertEqual((self.host / name).read_bytes(), (self.guest / name).read_bytes())
            self.assertEqual(stat.S_IMODE((self.host / name).stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE((self.guest / name).stat().st_mode), 0o600)

    def test_existing_byte_identical_import_is_idempotent(self):
        incoming = self.route("request", self.guest, self.host)
        target = self.host / "relay-guest.request.json"
        before = target.stat()
        self.run_transfer("import", "request", self.host, incoming, 150)
        after = target.stat()
        self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))
        self.refused("import", "request", self.host, incoming, 220)

    def test_replacement_never_overwrites(self):
        incoming = self.route("request", self.guest, self.host)
        target = self.host / "relay-guest.request.json"
        changed = request("guest", start=101)
        write(target, changed)
        self.refused("import", "request", self.host, incoming)
        self.assertEqual(json.loads(target.read_bytes()), changed)

    def test_expired_setup_refuses_still_live_grant_and_exchange(self):
        self.offer_ready()
        outgoing = self.newdir("offer-out")
        self.run_transfer("export", "offer", self.host, outgoing)
        self.refused("import", "offer", self.guest, outgoing, 220)
        self.assertFalse((self.guest / "relay-guest.json").exists())

    def test_receiver_earlier_deadline_is_not_extended(self):
        write(self.host / "relay-host.request.json", request("host", start=90))
        outgoing = self.newdir("request-out")
        self.run_transfer("export", "request", self.guest, outgoing)
        self.run_transfer("import", "request", self.host, outgoing, 209)
        self.refused("import", "request", self.host, outgoing, 210)

    def test_grant_cap_comes_from_issuer_and_original_request(self):
        with patch.object(courier.private, "MAX_TTL", 14400):
            value = request("guest")
            self.assertEqual(courier._request(courier._json(value), "guest", NOW)["grantExpiresAt"], 14500)
            self.assertEqual(courier._grant(courier._json(grant("guest")), value, NOW)["expiresAt"], 14500)
            value["grantExpiresAt"] += 1
            with self.assertRaises(courier.Refused):
                courier._request(courier._json(value), "guest", NOW)
            value = request("guest")
            with self.assertRaises(courier.Refused):
                courier._request(courier._json(value), "guest", 220)

    def test_request_validation_boundaries(self):
        changes = ({"schema": True}, {"role": "host"}, {"context": "other"}, {"requestedAt": True},
                   {"requestedAt": 111}, {"requestExpiresAt": 110}, {"requestExpiresAt": 221},
                   {"grantExpiresAt": 99}, {"grantExpiresAt": 101 + courier.private.MAX_TTL}, {"extra": 1})
        outgoing = self.newdir("out")
        for change in changes:
            with self.subTest(change=tuple(change)):
                write(self.guest / "relay-guest.request.json", request("guest") | change)
                self.refused("export", "request", self.guest, outgoing)
                self.assertFalse((outgoing / courier.PARCEL).exists())

    def test_context_role_and_grant_policy(self):
        value = request("guest")
        changes = ({"schema": True}, {"role": "host"}, {"context": "0" * 64}, {"issuedAt": 99},
                   {"issuedAt": 111}, {"expiresAt": 110}, {"expiresAt": 101 + courier.private.MAX_TTL},
                   {"iceServers": [{"urls": ["turn:unapproved.invalid:3478?transport=udp"], "username": "invented", "credential": "invented"}]},
                   {"iceServers": [{"urls": ["turns:turn.cloudflare.com:443?transport=tcp"], "username": "invented", "credential": "invented"}]})
        for change in changes:
            with self.subTest(change=tuple(change)), self.assertRaises((courier.Refused, courier.private.Rejected)):
                courier._grant(courier._json(grant("guest") | change), value, NOW)

    def test_record_envelope_roles_attempt_and_lifetime(self):
        offer = record("host")
        courier._exchange(record("guest"), "guest", NOW, offer)
        samples = [b"short", record("host"), record("guest", bytes([1]) * 32), record("guest", end=401), record("guest", end=110)]
        for sample in samples:
            with self.subTest(size=len(sample)), self.assertRaises(courier.Refused):
                courier._exchange(sample, "guest", NOW, offer)
        damaged = bytearray(record("guest")); damaged[81] = 4
        with self.assertRaises(courier.Refused):
            courier._exchange(bytes(damaged), "guest", NOW, offer)

    def test_bad_parcel_fields_and_duplicate_keys(self):
        outgoing = self.newdir("out")
        self.run_transfer("export", "request", self.guest, outgoing)
        original = json.loads((outgoing / courier.PARCEL).read_bytes())
        changes = ({"schema": True}, {"kind": "offer"}, {"context": "0" * 64}, {"createdAt": 111},
                   {"expiresAt": 110}, {"expiresAt": 231}, {"records": {"../key.json": "eA=="}},
                   {"records": {"relay-guest.request.json": "!"}}, {"records": {"relay-guest.request.json": "eA==", "master-token.json": "eA=="}})
        for change in changes:
            with self.subTest(change=tuple(change)):
                write(outgoing / courier.PARCEL, original | change)
                self.refused("import", "request", self.host, outgoing)
        write(outgoing / courier.PARCEL, b'{"schema":1,"schema":1}')
        self.refused("import", "request", self.host, outgoing)
        write(outgoing / courier.PARCEL, b"x" * (courier.MAX_PARCEL + 1))
        self.refused("import", "request", self.host, outgoing)
        self.assertFalse((self.host / "relay-guest.request.json").exists())

    def test_mismatched_local_context(self):
        outgoing = self.newdir("out")
        self.run_transfer("export", "request", self.guest, outgoing)
        write(self.host / "relay-host.request.json", request("host", "0" * 64))
        self.refused("import", "request", self.host, outgoing)

    def test_no_unlisted_files_exported(self):
        secret = b"invented-unrelated-master-canary"
        write(self.guest / "master-token.json", secret)
        outgoing = self.newdir("out")
        self.run_transfer("export", "request", self.guest, outgoing)
        parcel = (outgoing / courier.PARCEL).read_bytes()
        self.assertNotIn(secret, parcel)
        self.assertNotIn(base64.b64encode(secret), parcel)
        self.assertEqual(set(json.loads(parcel)["records"]), {"relay-guest.request.json"})

    def test_private_permissions_links_alias_and_lock(self):
        outgoing = self.newdir("out")
        request_path = self.guest / "relay-guest.request.json"
        os.chmod(request_path, 0o644)
        self.refused("export", "request", self.guest, outgoing)
        os.chmod(request_path, 0o600)
        os.chmod(outgoing, 0o755)
        self.refused("export", "request", self.guest, outgoing)
        os.chmod(outgoing, 0o700)
        self.refused("export", "request", self.guest, self.guest)
        alias = self.root / "alias"; alias.symlink_to(self.guest, target_is_directory=True)
        self.refused("export", "request", alias, outgoing)
        nested = self.guest / "nested"; nested.mkdir(mode=0o700)
        self.refused("export", "request", self.guest, alias / "nested")
        hard = self.guest / "hard"; os.link(request_path, hard)
        self.refused("export", "request", self.guest, outgoing)
        hard.unlink()
        request_path.rename(self.guest / "original")
        request_path.symlink_to("original")
        self.refused("export", "request", self.guest, outgoing)
        request_path.unlink(); (self.guest / "original").rename(request_path)
        fd = os.open(self.guest, os.O_RDONLY | os.O_DIRECTORY)
        try:
            courier.fcntl.flock(fd, courier.fcntl.LOCK_EX)
            self.refused("export", "request", self.guest, outgoing)
        finally:
            os.close(fd)

    def test_offer_publishes_before_grant_and_all_destinations_preflight(self):
        self.offer_ready()
        outgoing = self.newdir("out")
        self.run_transfer("export", "offer", self.host, outgoing)
        write(self.guest / "relay-guest.json", b"conflicting-existing-file")
        self.refused("import", "offer", self.guest, outgoing)
        offer_name = courier._record_name(self.context, "offer")
        self.assertFalse((self.guest / offer_name).exists())
        (self.guest / "relay-guest.json").unlink()
        published = []
        original = courier._publish
        def observed(fd, name, data):
            published.append(name)
            if name == "relay-guest.json":
                self.assertTrue((self.guest / offer_name).exists())
            original(fd, name, data)
        with patch.object(courier, "_publish", observed):
            self.run_transfer("import", "offer", self.guest, outgoing)
        self.assertEqual(published, [offer_name, "relay-guest.json"])

    def test_partial_offer_import_can_retry_unchanged(self):
        self.offer_ready()
        outgoing = self.newdir("out")
        self.run_transfer("export", "offer", self.host, outgoing)
        original = courier._publish
        def interrupted(fd, name, data):
            if name == "relay-guest.json":
                raise OSError("invented-private-canary")
            original(fd, name, data)
        with patch.object(courier, "_publish", interrupted):
            self.refused("import", "offer", self.guest, outgoing)
        self.assertFalse((self.guest / "relay-guest.json").exists())
        self.run_transfer("import", "offer", self.guest, outgoing, 150)
        self.assertEqual((self.guest / "relay-guest.json").read_bytes(), (self.host / "relay-guest.json").read_bytes())

    def test_deadline_and_directory_swap_before_publication(self):
        outgoing = self.newdir("out")
        times = iter((110, 220))
        with self.assertRaises(courier.Refused):
            courier.transfer("export", "request", self.guest, outgoing, lambda: next(times))
        self.assertFalse((outgoing / courier.PARCEL).exists())
        calls = 0
        def swapped():
            nonlocal calls
            calls += 1
            if calls == 2:
                outgoing.rename(self.root / "old-out")
                outgoing.mkdir(mode=0o700)
            return 110
        with self.assertRaises(courier.Refused):
            courier.transfer("export", "request", self.guest, outgoing, swapped)
        self.assertFalse((outgoing / courier.PARCEL).exists())
        self.assertFalse((self.root / "old-out" / courier.PARCEL).exists())

    def test_cli_only_fixed_output_and_relative_paths(self):
        outgoing = self.newdir("out")
        args = ["export", "request", "--session-directory", str(self.guest), "--transfer-directory", str(outgoing)]
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr), patch.object(courier.time, "time", return_value=NOW):
            self.assertEqual(courier.main(args), 0)
        self.assertEqual(stdout.getvalue(), "pilot transfer complete\n")
        self.assertEqual(stderr.getvalue(), "")
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            self.assertEqual(courier.main(args), 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "pilot transfer refused\n")
        for invalid in (("invented-secret-value",), ("export", "request", "--master-token", "invented-secret-value")):
            stderr = io.StringIO()
            with redirect_stderr(stderr), self.assertRaises(SystemExit):
                courier.main(list(invalid))
            self.assertEqual(stderr.getvalue(), "pilot transfer refused\n")
        prior = Path.cwd()
        try:
            os.chdir(self.root)
            courier.transfer("export", "request", Path("guest"), Path("out"), lambda: NOW)
        finally:
            os.chdir(prior)

    def test_atomic_publication_failure_removes_only_own_temporary(self):
        outgoing = self.newdir("out")
        write(outgoing / "retained", b"invented retained data")
        with patch.object(courier.private, "_publish_no_replace", side_effect=OSError("invented-private-canary")):
            self.refused("export", "request", self.guest, outgoing)
        self.assertEqual(sorted(p.name for p in outgoing.iterdir()), ["retained"])


DRIVER = r'''
#include "ice_exchange.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
extern "C" int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES]) {
    static unsigned counter=0;
    for (unsigned i=0;i<CE_NET_SECRET_BYTES;++i) out[i]=uint8_t(i+ ++counter);
    return 1; // Deterministic authored fixture only, never production entropy.
}
int main(int argc,char** argv) {
    if(argc!=3) return 1;
    CE_NetConfig config{}; config.role=std::strcmp(argv[1],"host")?CE_NET_GUEST:CE_NET_HOST;
    for(unsigned i=0;i<CE_NET_SECRET_BYTES;++i)config.secret[i]=uint8_t(i+7);
    for(unsigned i=0;i<CE_NET_DIGEST_BYTES;++i)config.compatibility[i]=uint8_t(i+33);
    auto exchange=CeIceExchange::Open(config,argv[2],100);
    if(!exchange)return 2;
    char context[65]; for(unsigned i=0;i<32;++i)std::sprintf(context+i*2,"%02x",exchange->Context()[i]);
    std::cout<<context<<std::endl;
    const char* local=config.role==CE_NET_HOST?"v=0\r\na=authored-host\r\n":"v=0\r\na=authored-guest\r\n";
    const char* remote=config.role==CE_NET_HOST?"v=0\r\na=authored-guest\r\n":"v=0\r\na=authored-host\r\n";
    for(std::string command;std::getline(std::cin,command);) {
        if(command=="P")std::cout<<int(exchange->PublishLocal(local,std::strlen(local),110))<<std::endl;
        else if(command=="R") {
            char output[4096]{};size_t size=0;auto result=exchange->PollRemote(output,sizeof(output),&size,110);
            if(result==CeIceExchangeResult::Ready && (size!=std::strlen(remote)||std::memcmp(output,remote,size)))return 3;
            std::cout<<int(result)<<std::endl;
        } else return 4;
    }
}
'''


class RealExchangeTests(CourierTests):
    # Avoid inheriting the boundary suite a second time.
    def run(self, result=None):
        if self._testMethodName != "test_real_exchange_accepts_courier_and_refuses_modified_ciphertext":
            return result
        return super().run(result)

    def child(self, role: str, directory: Path):
        child = subprocess.Popen([str(EXCHANGE_BINARY), role, str(directory)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        def close():
            if child.poll() is None:
                child.stdin.close()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(timeout=3)
            child.stdout.close(); child.stderr.close()
        self.addCleanup(close)
        return child

    def line(self, child, command=None):
        if command is not None:
            child.stdin.write(command + "\n"); child.stdin.flush()
        self.assertTrue(select.select([child.stdout], [], [], 3)[0], "authored exchange response timed out")
        result = child.stdout.readline().strip()
        self.assertNotEqual(result, "")
        return result

    def test_real_exchange_accepts_courier_and_refuses_modified_ciphertext(self):
        if EXCHANGE_BINARY is None:
            self.skipTest("optional pinned exchange fixture was not requested")
        host, guest = self.child("host", self.host), self.child("guest", self.guest)
        self.context = self.line(host)
        self.assertEqual(self.context, self.line(guest))
        write(self.host / "relay-host.request.json", request("host", self.context))
        write(self.guest / "relay-guest.request.json", request("guest", self.context))
        self.route("request", self.guest, self.host)
        for role in ("host", "guest"):
            write(self.host / ("relay-" + role + ".json"), grant(role, self.context))
        self.assertEqual(self.line(guest, "P"), "0")
        self.assertEqual(self.line(host, "P"), "1")
        self.assertEqual(self.line(guest, "R"), "0")
        self.route("offer", self.host, self.guest)
        self.assertEqual(self.line(guest, "R"), "1")
        self.assertEqual(self.line(guest, "P"), "1")
        self.assertEqual(self.line(host, "R"), "0")
        self.route("answer", self.guest, self.host)
        self.assertEqual(self.line(host, "R"), "1")
        self.assertEqual(self.line(host, "R"), "0")
        answer = self.host / courier._record_name(self.context, "answer")
        changed = bytearray(answer.read_bytes()); changed[83] ^= 1
        write(answer, bytes(changed))
        self.assertEqual(self.line(host, "R"), "2")
        # A fresh receiver proves ciphertext authentication, not just retention.
        other = self.newdir("fresh-guest")
        fresh = self.child("guest", other)
        self.assertEqual(self.context, self.line(fresh))
        write(other / "relay-guest.request.json", request("guest", self.context))
        parcel = self.newdir("tampered-offer")
        courier.transfer("export", "offer", self.host, parcel, lambda: NOW)
        value = json.loads((parcel / courier.PARCEL).read_bytes())
        name = courier._record_name(self.context, "offer")
        changed = bytearray(base64.b64decode(value["records"][name])); changed[83] ^= 1
        value["records"][name] = base64.b64encode(changed).decode("ascii")
        write(parcel / courier.PARCEL, value)
        courier.transfer("import", "offer", other, parcel, lambda: NOW)
        self.assertEqual(self.line(fresh, "R"), "2")


def build_exchange(source: Path, archive: Path, output: Path) -> Path:
    for name, expected in EXCHANGE_PINS.items():
        if hashlib.sha256((source / "port/network" / name).read_bytes()).hexdigest() != expected:
            raise SystemExit("exchange source identity refused")
    if hashlib.sha256(archive.read_bytes()).hexdigest() != MONOCYPHER_SHA256:
        raise SystemExit("cryptography archive identity refused")
    build = output / "exchange-build"; build.mkdir(mode=0o700)
    with tarfile.open(archive, "r:gz") as stream:
        stream.extractall(build, filter="data")
    mono = build / "monocypher-4.0.2/src"
    driver = build / "courier_exchange_driver.cpp"; driver.write_text(DRIVER)
    binary = build / "courier_exchange_driver"
    commands = [["cc", "-std=c11", "-O2", "-c", str(mono / "monocypher.c"), "-o", str(build / "monocypher.o")],
                ["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-DCE_NETWORK_ICE=1", "-I" + str(mono), "-I" + str(source / "port/network"),
                 str(source / "port/network/ice_exchange.cpp"), str(driver), str(build / "monocypher.o"), "-o", str(binary)]]
    with (build / "build.log").open("w") as log:
        for command in commands:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    return binary


def main():
    global OUTPUT, EXCHANGE_BINARY
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exchange-source", type=Path)
    parser.add_argument("--monocypher-archive", type=Path)
    args = parser.parse_args()
    OUTPUT = args.output.resolve()
    if OUTPUT.exists() or not OUTPUT.is_relative_to((ROOT / "local-data").resolve()):
        parser.error("new repo-local output required")
    if bool(args.exchange_source) != bool(args.monocypher_archive):
        parser.error("both pinned exchange inputs are required")
    OUTPUT.mkdir(mode=0o700, parents=True)
    if args.exchange_source:
        EXCHANGE_BINARY = build_exchange(args.exchange_source, args.monocypher_archive, OUTPUT)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CourierTests)
    if EXCHANGE_BINARY:
        suite.addTest(RealExchangeTests("test_real_exchange_accepts_courier_and_refuses_modified_ciphertext"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
