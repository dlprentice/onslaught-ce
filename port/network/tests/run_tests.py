#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and exercise the transport without game files, SDL, a GPU or privileged network configuration.

    python3 port/network/tests/run_tests.py [--enet-source PATH] [--windows-link]

Uses the project's pinned compilers/sysroot and already-available pinned Monocypher source. No downloads or installs.
The ENet source is the reviewed upstream commit below (post-1.3.18 fragment/accounting/UB fixes), pinned in deps.lock
and read from the dependency cache unless --enet-source names another copy.
Two actual fixture processes communicate only over 127.0.0.1. A user-space UDP proxy adds seeded impairments and
replays/corrupts application frames. Fixed fixture keys are authored test values; no real invite is read or printed.
"""
import argparse
import heapq
import json
import os
from pathlib import Path
import random
import re
import resource
import select
import socket
import struct
import subprocess
import sys
import threading
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "port/tools"))
import deps  # noqa: E402

if __package__:
    from .test_conformance_output_guard import ConformanceOutputGuardTests
else:
    from test_conformance_output_guard import ConformanceOutputGuardTests

ENET_COMMIT = "5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"
ENET_URL = f"https://codeload.github.com/lsalzman/enet/tar.gz/{ENET_COMMIT}"
ENET_SHA256 = "537d42f31b0c0d27f5a9635f6c224f2451c1c1484bfde50e94c884e96181c290"
ENET_LICENSE = "MIT"
NETWORK = ROOT / "port/network"
WORK = ROOT / "local-data/network/tests"


def run(args):
    subprocess.run([str(x) for x in args], check=True)


def build(enet, windows=False):
    target = "windows" if windows else "linux"
    out = WORK / target
    out.mkdir(parents=True, exist_ok=True)
    mc = deps.SRC / "monocypher-4.0.2/src"
    if not (mc / "monocypher.c").is_file() or not (enet / "include/enet/enet.h").is_file():
        raise SystemExit("network tests: supply the pinned ENet and Monocypher sources; nothing is downloaded")
    if windows:
        cc = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang"
        cxx = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
        flags, link = [], ["-lwinmm", "-lws2_32", "-lbcrypt", "-static-libstdc++", "-static-libgcc"]
    else:
        cc = deps.DATA / "llvm/bin/clang"
        cxx = deps.DATA / "llvm/bin/clang++"
        flags = ["-m32", *deps.sysroot_flags()]
        link = ["-m32", *deps.sysroot_link_flags(), "-lpthread"]
    includes = [f"-I{enet / 'include'}", f"-I{mc}", f"-I{NETWORK}"]
    config = [] if windows else ["-DHAS_FCNTL=1", "-DHAS_POLL=1", "-DHAS_GETADDRINFO=1", "-DHAS_GETNAMEINFO=1",
                                  "-DHAS_INET_PTON=1", "-DHAS_INET_NTOP=1", "-DHAS_MSGHDR_FLAGS=1", "-DHAS_SOCKLEN_T=1"]
    objects = []
    for source in [*(enet / f"{x}.c" for x in ("callbacks", "compress", "host", "list", "packet", "peer", "protocol",
                                                         "win32" if windows else "unix")), mc / "monocypher.c"]:
        obj = out / (source.stem + ".o")
        run([cc, *flags, "-std=gnu99", "-O2", *includes, *config, "-c", source, "-o", obj])
        objects.append(obj)
    for source in [NETWORK / "codec.cpp", NETWORK / "transport.cpp"]:
        obj = out / (source.stem + ".o")
        run([cxx, *flags, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", *includes, "-c", source, "-o", obj])
        objects.append(obj)
    for fixture in ("codec_tests", "protocol_tests", "peer", "state_peer", "silent_host_driver"):
        exe = out / (fixture + (".exe" if windows else ""))
        linked = objects if fixture not in ("protocol_tests", "state_peer") else [x for x in objects if x.stem != "transport"]
        run([cxx, *flags, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", *includes,
             NETWORK / f"tests/{fixture}.cpp", *linked, *link, "-o", exe])
    return out / "peer"


class Proxy:
    """One loopback socket forwards ENet datagrams; no packet contents or secrets are logged."""
    def __init__(self, host_port, impairment=False, tamper=False, stale=False, oversize=False,
                 latency_ms=0, drop_state_part=False, late_state_part=False, drop_marker=False):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.host = ("127.0.0.1", host_port)
        self.guest = None
        self.impairment, self.tamper, self.stale, self.oversize = impairment, tamper, stale, oversize
        self.latency_ms, self.drop_state_part, self.late_state_part = latency_ms, drop_state_part, late_state_part
        self.drop_marker = drop_marker
        self.rng = random.Random(20261025)
        self.queue, self.serial = [], 0
        self.stop = threading.Event()
        self.stats = dict(received=0, dropped=0, duplicated=0, reordered=0, tampered=0, replayed=0, oversize=0,
                          max_datagram=0, plaintext_pattern=0, nonce_collisions=0, encrypted_frames=0)
        self.stats.update(state_reliable=0, state_unsequenced=0, targeted_fragments=0, targeted_dropped=0, targeted_delayed=0,
                          marker_dropped=0)
        self.delivered = {}
        self.nonces = {}
        self.saved = None
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def enqueue(self, data, address, delay=0):
        self.serial += 1
        heapq.heappush(self.queue, (time.monotonic() + delay, self.serial, data, address))

    def account_delivery(self, serial, address):
        # Fixed latency preserves order. Count an actual emission inversion within each direction instead.
        previous=self.delivered.get(address,0)
        if serial<previous:self.stats["reordered"]+=1
        self.delivered[address]=max(previous,serial)

    def loop(self):
        try:
            while not self.stop.is_set():
                while self.queue and self.queue[0][0] <= time.monotonic():
                    _, serial, data, address = heapq.heappop(self.queue)
                    self.sock.sendto(data, address)
                    self.account_delivery(serial, address)
                if not select.select([self.sock], [], [], 0.002)[0]:
                    continue
                data, address = self.sock.recvfrom(65536)
                self.stats["received"] += 1
                self.stats["max_datagram"] = max(self.stats["max_datagram"], len(data))
                if address == self.host:
                    if self.guest is None:
                        continue
                    dest = self.guest
                else:
                    self.guest = address
                    dest = self.host
                # The fixture baseline starts with these authored bytes; ciphertext must conceal them.
                pattern = bytes((i * 37 + 99) & 255 for i in range(32))
                if pattern in data:
                    self.stats["plaintext_pattern"] += 1
                at = data.find(b"CEN1")
                kind = data[at + 6] if at >= 0 and at + 48 <= len(data) else 0
                epoch = struct.unpack_from(">I", data, at + 8)[0] if kind else 0
                # Retransmission may repeat identical ciphertext; reuse for different ciphertext must never occur,
                # including across the real epoch barrier. The nonce's role/channel counters are not reset there.
                cursor = 0
                while True:
                    found = data.find(b"CEN1", cursor)
                    if found < 0 or found + 64 > len(data):
                        break
                    cursor = found + 4
                    version, frame_kind, channel = struct.unpack_from(">HBB", data, found + 4)
                    total = struct.unpack_from(">I", data, found + 24)[0]
                    part, parts = struct.unpack_from(">HH", data, found + 28)
                    if version != 1 or not parts or part >= parts or total > 65536:
                        continue
                    n = min(1000, total - part * 1000)
                    end = found + 48 + n + 16
                    if n < 0 or end > len(data) or frame_kind == 1:
                        continue
                    nonce = (data[found + 32:found + 48], address == self.host, channel,
                             data[found + 12:found + 16])
                    frame = data[found:end]
                    if nonce in self.nonces and self.nonces[nonce] != frame:
                        self.stats["nonce_collisions"] += 1
                    self.nonces[nonce] = frame
                    self.stats["encrypted_frames"] += 1
                state_frames=[]
                cursor=0
                while True:
                    found=data.find(b"CEN1",cursor)
                    if found<0 or found+64>len(data):break
                    cursor=found+4
                    version,frame_kind,frame_channel=struct.unpack_from(">HBB",data,found+4)
                    total=struct.unpack_from(">I",data,found+24)[0];part,parts=struct.unpack_from(">HH",data,found+28)
                    if version!=1 or frame_kind!=18 or frame_channel!=2 or not parts or part>=parts or total>65536:continue
                    n=min(1000,total-part*1000);length=48+n+16
                    if n<0 or found+length>len(data):continue
                    if found>=8 and data[found-8]&15==9 and data[found-7]==2 and struct.unpack_from(">H",data,found-2)[0]==length:
                        self.stats["state_unsequenced"]+=1
                    elif found>=6 and data[found-6]&15==6 and data[found-5]==2 and struct.unpack_from(">H",data,found-2)[0]==length:
                        self.stats["state_reliable"]+=1
                    else:continue
                    state_frames.append((struct.unpack_from(">I",data,found+20)[0],part))
                if address==self.host and (3,1) in state_frames and (self.drop_state_part or self.late_state_part):
                    self.stats["targeted_fragments"]+=1
                    if self.drop_state_part:self.stats["targeted_dropped"]+=1;continue
                    self.stats["targeted_delayed"]+=1;self.enqueue(data,dest,1+self.latency_ms/2000);continue
                # The clear frame header carries kind at +6 and tick at +16: drop the first guest marker datagram.
                if self.drop_marker and address != self.host and not self.stats["marker_dropped"]:
                    marks = [f for f in range(len(data) - 20) if data[f:f + 4] == b"CEN1" and data[f + 6] == 16 and
                             struct.unpack_from(">I", data, f + 16)[0] == 4242]
                    if marks:
                        self.stats["marker_dropped"] += 1
                        continue
                if self.tamper and kind == 18 and not self.stats["tampered"]:
                    damaged = bytearray(data)
                    damaged[at + 48] ^= 0x40
                    self.enqueue(bytes(damaged), dest)
                    self.stats["tampered"] += 1
                    continue
                if self.oversize and kind == 18 and not self.stats["oversize"]:
                    # Preserve ENet framing but make an authenticated application's total length impossible.
                    damaged = bytearray(data)
                    struct.pack_into(">I", damaged, at + 24, 0xffffffff)
                    self.enqueue(bytes(damaged), dest)
                    self.stats["oversize"] += 1
                    continue
                if self.stale:
                    if kind == 18 and epoch == 1 and self.saved is None:
                        self.saved = (data, dest)
                    if kind == 16 and epoch == 2 and self.saved:
                        self.enqueue(*self.saved)
                        self.saved = None
                        self.stats["replayed"] += 1
                if self.impairment and self.rng.random() < 0.12:
                    self.stats["dropped"] += 1
                    continue
                delay = self.latency_ms / 2000 + (self.rng.uniform(0, 0.028) if self.impairment else 0)
                self.enqueue(data, dest, delay)
                if self.impairment and self.rng.random() < 0.08:
                    self.enqueue(data, dest, delay + 0.011)
                    self.stats["duplicated"] += 1
        except OSError:
            if not self.stop.is_set():
                raise

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.sock.close()


def start_peer(role, port, scenario, executable=None, extra=()):
    proc = subprocess.Popen([str(executable or PEER), role, str(port), scenario, *map(str, extra)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, bufsize=1)
    first = proc.stdout.readline()
    if not first.startswith("BOUND "):
        out, err = proc.communicate(timeout=2)
        raise AssertionError(f"{role} did not bind: {first}{out}{err}")
    return proc, int(first.split()[1])


def pair(scenario="normal", executable=None, extra=(), **proxy_args):
    host, host_port = start_peer("host", 0, scenario, executable, extra)
    proxy = Proxy(host_port, **proxy_args)
    guest = None
    try:
        guest, _ = start_peer("guest", proxy.port, scenario, executable, extra)
        hout, herr = host.communicate(timeout=22)
        gout, gerr = guest.communicate(timeout=22)
        result = dict(host=dict(rc=host.returncode, out=hout, err=herr),
                      guest=dict(rc=guest.returncode, out=gout, err=gerr), proxy=proxy.stats)
        name = scenario + "".join(f"-{k}-{v}" for k, v in proxy_args.items() if v) + ("-"+"-".join(map(str,extra)) if extra else "")
        (WORK / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n")
        return result
    finally:
        for proc in (host, guest):
            if proc and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)
        proxy.close()


class TransportTests(unittest.TestCase):
    def check_pair(self, **kwargs):
        result = pair(**kwargs)
        for role in ("host", "guest"):
            self.assertEqual(0, result[role]["rc"], result)
            self.assertEqual("", result[role]["err"], result)
        self.assertLessEqual(result["proxy"]["max_datagram"], 1200, result)
        self.assertEqual(0, result["proxy"]["plaintext_pattern"], result)
        self.assertEqual(0, result["proxy"]["nonce_collisions"], result)
        self.assertGreater(result["proxy"]["encrypted_frames"], 0, result)
        return result

    def test_proxy_reorder_counter_is_delivery_inversion(self):
        proxy=object.__new__(Proxy);proxy.stats={"reordered":0};proxy.delivered={}
        proxy.account_delivery(1,"guest");proxy.account_delivery(2,"host");proxy.account_delivery(4,"guest")
        self.assertEqual(0,proxy.stats["reordered"])
        proxy.account_delivery(3,"guest");self.assertEqual(1,proxy.stats["reordered"])
        proxy.account_delivery(5,"host");self.assertEqual(1,proxy.stats["reordered"])

    def test_codec(self):
        run([PEER.parent / "codec_tests"])

    def test_authored_protocol_failure_controls(self):
        run([PEER.parent / "protocol_tests"])

    def test_two_process_auth_baseline_inputs_state_order_epoch(self):
        result = self.check_pair()
        self.assertIn("epoch=2", result["host"]["out"])
        self.assertIn("control=12", result["guest"]["out"])

    def test_seeded_loss_reordering_and_duplication(self):
        result = self.check_pair(impairment=True)
        for name in ("dropped", "duplicated", "reordered"):
            self.assertGreater(result["proxy"][name], 0)

    def test_ordered_input_batches_survive_loss_and_caller_backpressure(self):
        result = self.check_pair(scenario="ordered-input", impairment=True)
        self.assertIn("input=300", result["host"]["out"])
        self.assertIn("backpressure=1", result["guest"]["out"])

    def check_state(self, scenario, size=2501, poll_budget=32, **kwargs):
        result=pair(scenario=scenario,executable=PEER.parent/"state_peer",extra=(size,poll_budget),**kwargs)
        for role in ("host","guest"):
            self.assertEqual(0,result[role]["rc"],result)
            self.assertEqual("",result[role]["err"],result)
        self.assertEqual(0,result["proxy"]["state_reliable"],result)
        self.assertGreater(result["proxy"]["state_unsequenced"],0,result)
        self.assertLessEqual(result["proxy"]["max_datagram"],1200,result)
        self.assertEqual(0,result["proxy"]["plaintext_pattern"],result)
        self.assertEqual(0,result["proxy"]["nonce_collisions"],result)
        return result

    def test_missing_old_STATE_fragment_does_not_block_newer(self):
        result=self.check_state("gap",drop_state_part=True,latency_ms=100)
        self.assertEqual(1,result["proxy"]["targeted_fragments"],result)
        self.assertEqual(1,result["proxy"]["targeted_dropped"],result)

    def test_late_retired_STATE_fragment_cannot_resurrect(self):
        result=self.check_state("late",late_state_part=True,latency_ms=300)
        self.assertEqual(1,result["proxy"]["targeted_delayed"],result)

    def test_delta_sized_STATE_20Hz_delay_loss_reorder(self):
        for size,latency in ((2501,100),(5239,300),(10393,300)):
            with self.subTest(size=size,latency=latency):
                result=self.check_state("stream",size=size,latency_ms=latency,impairment=True)
                self.assertGreater(result["proxy"]["dropped"],0,result)
                self.assertGreater(result["proxy"]["reordered"],0,result)

    def test_raw_STATE_overload_then_lower_rate_recovery(self):
        self.check_state("raw",size=18001,latency_ms=300)

    def test_wrong_invite_never_admits(self):
        result = self.check_pair(scenario="bad-key")
        for role in ("host", "guest"):
            self.assertNotIn("ACTIVE", result[role]["out"])

    def test_incompatible_never_admits(self):
        result = self.check_pair(scenario="incompatible")
        for role in ("host", "guest"):
            self.assertNotIn("ACTIVE", result[role]["out"])
        # Each side can name the other's version: the host from the HELLO, the guest from the authenticated refusal.
        self.assertIn("PEER_VERSION 9.8.6-guest", result["host"]["out"])
        self.assertIn("PEER_VERSION 9.8.7-host", result["guest"]["out"])

    def test_silent_peer_expires(self):
        self.check_pair(scenario="timeout")

    def test_reliable_resend_after_peer_stall_is_prompt(self):
        # A blocked peer acknowledges late and ENet takes that for RTT; one lost reliable control then waited seconds.
        result = self.check_pair(scenario="stall-resend", drop_marker=True)
        self.assertEqual(1, result["proxy"]["marker_dropped"], result)
        sent = int(re.search(r"MARKER_SENT (\d+)", result["guest"]["out"]).group(1))
        received = int(re.search(r"MARKER_RECEIVED (\d+)", result["host"]["out"]).group(1))
        self.assertLess(received - sent, 1200, result)

    def test_outgoing_queue_is_bounded(self):
        result = self.check_pair(scenario="backpressure")
        self.assertIn("BOUNDED", result["host"]["out"])

    def test_altered_ciphertext_is_not_delivered(self):
        result = self.check_pair(tamper=True)
        self.assertEqual(1, result["proxy"]["tampered"])
        self.assertRegex(result["guest"]["out"], r"rejected=[1-9]")

    def test_oversize_header_is_not_delivered(self):
        result = self.check_pair(oversize=True)
        self.assertEqual(1, result["proxy"]["oversize"])
        self.assertRegex(result["guest"]["out"], r"rejected=[1-9]")

    def test_old_epoch_packet_replay(self):
        result = self.check_pair(stale=True)
        self.assertEqual(1, result["proxy"]["replayed"])

    def test_pre_connection_silent_host(self):
        from silent_host_test import verify_silent_host
        verify_silent_host(PEER.with_name("silent_host_driver"))


if __name__ == "__main__":
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--enet-source", type=Path, default=deps.SRC / f"enet-{ENET_COMMIT}")
    parser.add_argument("--windows-link", action="store_true", help="also cross-compile/link fixtures; does not run Windows")
    parser.add_argument("--output",type=Path,help="fresh private output directory; default is a new repo-local run")
    args, rest = parser.parse_known_args()
    if args.output:
        WORK=args.output.resolve();WORK.mkdir(parents=True,exist_ok=False)
    else:
        WORK.mkdir(parents=True,exist_ok=True);WORK=Path(tempfile.mkdtemp(prefix="run-",dir=WORK))
    print("network controls output:",WORK,flush=True)
    PEER = build(args.enet_source.resolve())
    if args.windows_link:
        build(args.enet_source.resolve(), windows=True)
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)
