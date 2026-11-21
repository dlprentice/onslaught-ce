#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored controls for the bounded lock waits: temporary lock files, real flock and real queue entries.

Every lock here is a file in a temporary folder, held by this test process through its own open file descriptions (or by a
child process), the way another job on the machine would hold it. Nothing touches the machine's own lock files.
"""
from __future__ import annotations

from contextlib import contextmanager
import fcntl
import hashlib
import importlib.util
import io
import json
import os
import pathlib
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest import mock

import pair_lock_wait as waits

ROOT = Path(__file__).resolve().parents[2]
LIMIT = 30                                        # seconds any wait that must succeed may take
GOOD = (0.5, 20 * 1024**3, 200 * 1024**3)         # pressure, available bytes, free bytes
# The pinned support runner that these waits stand in for (its object in this repository's history).
RUNNER_COMMIT = "728c1dfaddb6c18d4e90b9f667685d59582c7ea3"
RUNNER_SHA256 = "c410f1a0383baa049d4a90191d1223989626af403c0042b9ff757d06e1b8b979"
# A job that holds one lock file, as any other job on the machine would, and says so.
HOLD = ("import fcntl, sys, time\n"
        "stream = open(sys.argv[1], 'a')\n"
        "fcntl.flock(stream, fcntl.LOCK_EX)\n"
        "print('held', flush=True)\n"
        "time.sleep(float(sys.argv[2]))\n")


class Refusal(ValueError):
    pass


class Stop(BaseException):
    """Not an Exception, like a keyboard interrupt: cleanup must not depend on catching Exception."""


def lock_table(path):
    """[(blocked, pid)] for the flock requests on a file, from the kernel's lock table; None when it cannot be read."""
    try:
        status = os.stat(path)
        lines = Path("/proc/locks").read_text().splitlines()
    except OSError:
        return None
    rows = []
    for line in lines:
        fields = line.split()
        blocked = len(fields) > 1 and fields[1] == "->"
        rest = fields[2:] if blocked else fields[1:]
        if len(rest) < 6 or rest[0] != "FLOCK":
            continue
        major, minor, inode = rest[4].split(":")
        if int(inode) == status.st_ino and int(major, 16) == os.major(status.st_dev) and int(minor, 16) == os.minor(status.st_dev):
            rows.append((blocked, int(rest[3])))
    return rows


def blocked_pids(path):
    return sorted(pid for blocked, pid in lock_table(path) or [] if blocked)


def free_now(path):
    """Whether a new open file description can take the lock at once, which is whether nothing holds it."""
    with open(path, "a") as probe:
        try:
            fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        return True


def gone(pid):
    return not Path(f"/proc/{pid}").exists()


class Holder:
    """Another job holding a lock through its own open file description."""

    def __init__(self, path):
        self.stream = open(path, "a")
        fcntl.flock(self.stream, fcntl.LOCK_EX)

    def release(self):
        self.stream.close()


class Run:
    """waiting_resource_locks in a thread, so that a test can act on the locks while it waits."""

    def __init__(self, support, body=None, readings=None, **options):
        self.support, self.body, self.options = support, body, options
        self.readings = readings or (lambda root: GOOD)
        self.record, self.events, self.pids, self.error = {}, [], {}, None
        self.seen = {}
        self.done, self.leave = threading.Event(), threading.Event()
        self.started = time.monotonic()
        self.elapsed = None
        self.thread = threading.Thread(target=self.go, daemon=True)
        self.thread.start()

    def mark(self, key):
        self.seen.setdefault(key, threading.Event()).set()

    def reached(self, key):
        return key in self.seen and self.seen[key].is_set()

    def note(self, event, **info):
        key = f"{event}:{info.get('kind') or info.get('stage') or ''}"
        self.events.append(key)
        if "pids" in info:
            self.pids[info["kind"]] = list(info["pids"])
        self.mark(key)

    def go(self):
        try:
            with waits.waiting_resource_locks(self.support, self.record, readings=self.readings, progress=self.note,
                                              **self.options):
                if self.body:
                    self.body()
                self.mark("entered")
                self.leave.wait(LIMIT)
        except BaseException as error:
            self.error = error
        finally:
            self.elapsed = time.monotonic() - self.started
            self.done.set()

    def wait(self, key):
        deadline = time.monotonic() + LIMIT
        while time.monotonic() < deadline:
            if self.seen.setdefault(key, threading.Event()).wait(0.02):
                return
            if self.done.is_set():
                break
        raise AssertionError(f"{key} not reached; error={self.error!r}; events={self.events}")

    def finish(self):
        self.leave.set()
        self.thread.join(LIMIT)
        assert not self.thread.is_alive(), "the wait did not end"
        return self


class Logged(io.TextIOWrapper):
    """A text file that tells which lock file it closes, and when."""
    closes = []

    def close(self):
        if not self.closed:
            Logged.closes.append(Path(self.name).name)
        super().close()


def logged_open(path, mode="r"):
    return Logged(io.BufferedWriter(io.FileIO(path, "a")), encoding="utf-8")


class LockWaitTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="pair-lock-wait-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.slots = [self.root / f"slot{n}.lock" for n in range(3)]
        self.gpu = self.root / "gpu.lock"
        self.support = types.SimpleNamespace(HEAVY_LOCKS=tuple(self.slots), GPU_LOCK=self.gpu, ROOT=self.root,
                                             PairRefusal=Refusal)
        self.holders, self.runs = [], []
        self.addCleanup(self.release_everything)

    def release_everything(self):
        for run in self.runs:
            run.leave.set()
            run.thread.join(5)
        for holder in self.holders:
            holder.release()

    def hold(self, *paths):
        holders = [Holder(path) for path in paths]
        self.holders.extend(holders)
        return holders

    def start(self, **options):
        run = Run(self.support, **options)
        self.runs.append(run)
        return run

    def assert_nothing_queued_or_held(self, run, *paths):
        for kind, pids in run.pids.items():
            for pid in pids:
                self.assertTrue(gone(pid), f"queue entry {pid} of the {kind} wait is still there")
        for path in paths:
            if path.exists():
                self.assertEqual(blocked_pids(path), [], path.name)
                self.assertTrue(free_now(path), f"{path.name} is still held")

    def lock_table_works(self, path):
        rows = lock_table(path)
        if rows is None or (False, os.getpid()) not in rows:
            self.skipTest("the kernel lock table does not list this file's locks")

    # the numbers and labels these waits stand on

    def test_the_bounds_labels_and_limits_are_the_reviewed_ones(self):
        self.assertEqual(waits.SLOT_WAIT_SECONDS, 2700.0)
        self.assertEqual(waits.GPU_WAIT_SECONDS, 1200.0)
        self.assertEqual(waits.SLOT_REFUSAL, "ordinary heavy slots occupied")
        self.assertEqual(waits.RESOURCE_REFUSAL, "run resources unavailable")
        self.assertEqual(waits.GPU_REFUSAL, "gpu lock occupied")
        self.assertEqual(waits.QUEUE_REFUSAL, "lock queue entry failed")
        self.assertEqual((waits.PRESSURE_LIMIT, waits.AVAILABLE_MINIMUM, waits.FREE_MINIMUM),
                         (20, 12 * 1024**3, 50 * 1024**3))

    def test_the_system_readings_are_three_plausible_numbers(self):
        pressure, available, free = waits.system_readings(self.root)
        self.assertTrue(0 <= pressure <= 100 and available > 0 and free > 0)

    # nothing is busy

    def test_both_free_takes_the_first_slot_and_the_gpu_lock_without_waiting_or_queueing(self):
        run = self.start()
        run.wait("entered")
        self.assertFalse(self.slots[1].exists() or self.slots[2].exists(), "later slots are opened only when needed")
        self.assertFalse(free_now(self.slots[0]))
        self.assertFalse(free_now(self.gpu))
        self.assertIsNone(run.finish().error)
        self.assertEqual(run.events, ["admission:start", "granted:slot", "granted:gpu", "admission:granted", "acquired:"])
        self.assertEqual(run.pids, {})
        slot, gpu = run.record["slot"], run.record["gpu"]
        self.assertEqual((slot["index"], slot["immediate"], slot["path"]), (0, True, str(self.slots[0])))
        self.assertEqual((gpu["immediate"], gpu["path"]), (True, str(self.gpu)))
        self.assertLess(slot["waited_seconds"] + gpu["waited_seconds"], 1.0)
        self.assertEqual((run.record["outcome"], slot["bound_seconds"], gpu["bound_seconds"]), ("acquired", 2700.0, 1200.0))
        self.assertTrue(all(run.record["admission"][stage]["passed"] for stage in ("start", "granted")))
        self.assertGreaterEqual(run.record["held_seconds"], 0.0)
        for path in (*self.slots, self.gpu):
            self.assertTrue(free_now(path))
        json.dumps(run.record)

    def test_a_free_later_slot_is_taken_at_once_when_the_first_is_busy(self):
        self.hold(self.slots[0])
        run = self.start()
        run.wait("entered")
        self.assertEqual(run.record["slot"]["index"], 1)
        self.assertTrue(run.record["slot"]["immediate"])
        self.assertNotIn("queued:slot", run.events)
        self.assertFalse(free_now(self.slots[1]))
        self.assertIsNone(run.finish().error)
        self.assertTrue(free_now(self.slots[1]))

    # a slot is busy and then frees: any slot will do

    def slot_wait(self, freed):
        holders = self.hold(*self.slots)
        run = self.start()
        run.wait("queued:slot")
        self.lock_table_works(self.slots[0])
        self.assertEqual(len(run.pids["slot"]), 3)
        deadline = time.monotonic() + LIMIT
        while time.monotonic() < deadline and any(not blocked_pids(path) for path in self.slots):
            time.sleep(0.02)
        for path in self.slots:     # the caller is queued, blocking, on every slot at the same time
            self.assertEqual(len(blocked_pids(path)), 1, path.name)
        self.assertEqual(sorted(sum((blocked_pids(path) for path in self.slots), [])), sorted(run.pids["slot"]))
        self.assertTrue(free_now(self.gpu), "the gpu lock must not be held while waiting for a slot")
        self.assertEqual(blocked_pids(self.gpu), [], "nor queued on")
        self.assertNotIn("granted:slot", run.events)
        holders[freed].release()
        run.wait("entered")
        slot = run.record["slot"]
        self.assertEqual((slot["index"], slot["immediate"], slot["path"]), (freed, False, str(self.slots[freed])))
        self.assertGreater(slot["waited_seconds"], 0.0)
        self.assertEqual(run.events[:4], ["admission:start", "queued:slot", "granted:slot", "granted:gpu"])
        self.assertFalse(free_now(self.slots[freed]))
        for pid in run.pids["slot"]:
            self.assertTrue(gone(pid), "the cancelled queue entries are collected")
        for number, holder in enumerate(holders):
            if number != freed:
                self.assertEqual(blocked_pids(self.slots[number]), [])
                holder.release()
                self.assertTrue(free_now(self.slots[number]), "a slot that was not granted must not stay locked")
        self.assertIsNone(run.finish().error)
        for path in (*self.slots, self.gpu):
            self.assertTrue(free_now(path))

    def test_waits_for_the_first_slot_to_free(self):
        self.slot_wait(0)

    def test_waits_for_the_second_slot_to_free(self):
        self.slot_wait(1)

    def test_waits_for_the_last_slot_to_free(self):
        self.slot_wait(2)

    def test_slots_held_by_the_standard_flock_command_are_waited_for(self):
        if not shutil.which("flock"):
            self.skipTest("no flock command")
        for path in self.slots:
            path.touch()
        jobs = [subprocess.Popen(["flock", "-x", str(path), "sleep", seconds], start_new_session=True)
                for path, seconds in zip(self.slots, ("1.5", "30", "30"))]
        try:
            deadline = time.monotonic() + LIMIT
            while time.monotonic() < deadline and any(free_now(path) for path in self.slots):
                time.sleep(0.02)
            run = self.start()
            run.wait("entered")
            self.assertEqual(run.record["slot"]["index"], 0)
            self.assertFalse(run.record["slot"]["immediate"])
            self.assertIsNone(run.finish().error)
        finally:
            for job in jobs:
                try:
                    os.killpg(job.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                job.wait()

    # the gpu lock is busy and then frees

    def test_the_gpu_wait_holds_the_slot_and_ends_when_the_gpu_lock_frees(self):
        gpu_holder, = self.hold(self.gpu)
        run = self.start()
        run.wait("queued:gpu")
        self.lock_table_works(self.gpu)
        self.assertEqual(run.events, ["admission:start", "granted:slot", "queued:gpu"])
        self.assertFalse(free_now(self.slots[0]), "the slot stays held while the gpu lock is waited for")
        self.assertEqual(len(run.pids["gpu"]), 1)
        deadline = time.monotonic() + LIMIT
        while time.monotonic() < deadline and not blocked_pids(self.gpu):
            time.sleep(0.02)
        self.assertEqual(blocked_pids(self.gpu), run.pids["gpu"])
        self.assertFalse(run.reached("entered"))
        gpu_holder.release()
        run.wait("entered")
        self.assertEqual(run.events, ["admission:start", "granted:slot", "queued:gpu", "granted:gpu", "admission:granted",
                                      "acquired:"])
        gpu = run.record["gpu"]
        self.assertEqual((gpu["immediate"], gpu["path"]), (False, str(self.gpu)))
        self.assertGreater(gpu["waited_seconds"], 0.0)
        self.assertTrue(gone(run.pids["gpu"][0]))
        self.assertFalse(free_now(self.gpu))
        self.assertIsNone(run.finish().error)
        self.assertTrue(free_now(self.gpu) and free_now(self.slots[0]))

    def test_the_gpu_lock_is_not_asked_for_until_the_slot_is_held(self):
        holders = self.hold(*self.slots)
        gpu_holder, = self.hold(self.gpu)
        run = self.start()
        run.wait("queued:slot")
        time.sleep(0.2)
        self.assertEqual(run.events, ["admission:start", "queued:slot"])
        self.assertEqual(blocked_pids(self.gpu), [])
        holders[1].release()
        run.wait("queued:gpu")
        self.assertEqual(run.events, ["admission:start", "queued:slot", "granted:slot", "queued:gpu"])
        gpu_holder.release()
        run.wait("entered")
        self.assertIsNone(run.finish().error)

    # the bounds

    def test_a_slot_timeout_refuses_releases_everything_and_creates_no_output(self):
        self.hold(*self.slots)
        output = self.root / "output"
        run = self.start(slot_bound=0.6, body=output.mkdir)
        run.finish()
        self.assertIsInstance(run.error, Refusal)
        self.assertEqual(str(run.error), waits.SLOT_REFUSAL)
        self.assertFalse(output.exists(), "the block must not start")
        self.assertGreaterEqual(run.elapsed, 0.55)
        self.assertLess(run.elapsed, 15)
        self.assertEqual(run.events, ["admission:start", "queued:slot", "timeout:slot"])
        self.assertEqual(len(run.pids["slot"]), 3)
        self.assertFalse(self.gpu.exists(), "the gpu lock is never opened, let alone held, when no slot is granted")
        for holder in self.holders:
            holder.release()
        self.assert_nothing_queued_or_held(run, *self.slots)
        record = run.record
        self.assertEqual((record["outcome"], record["reason"]), ("refused", waits.SLOT_REFUSAL))
        self.assertTrue(record["slot"]["timed_out"])
        self.assertGreaterEqual(record["slot"]["waited_seconds"], 0.55)
        self.assertNotIn("gpu", record)

    def test_a_gpu_timeout_refuses_releases_the_slot_and_creates_no_output(self):
        self.hold(self.gpu)
        output = self.root / "output"
        run = self.start(gpu_bound=0.6, body=output.mkdir)
        run.finish()
        self.assertIsInstance(run.error, Refusal)
        self.assertEqual(str(run.error), waits.GPU_REFUSAL)
        self.assertFalse(output.exists())
        self.assertGreaterEqual(run.elapsed, 0.55)
        self.assertEqual(run.events, ["admission:start", "granted:slot", "queued:gpu", "timeout:gpu"])
        for holder in self.holders:
            holder.release()
        self.assert_nothing_queued_or_held(run, *self.slots, self.gpu)
        self.assertEqual((run.record["outcome"], run.record["reason"]), ("refused", waits.GPU_REFUSAL))
        self.assertTrue(run.record["gpu"]["timed_out"])
        self.assertEqual(run.record["slot"]["index"], 0)

    def test_each_wait_lasts_as_long_as_its_own_bound(self):
        holders = self.hold(*self.slots)
        started = time.monotonic()
        with self.assertRaises(Refusal):
            with waits.waiting_resource_locks(self.support, slot_bound=1.5, readings=lambda root: GOOD):
                self.fail("the block must not start")
        self.assertTrue(1.45 <= time.monotonic() - started < 2.9, time.monotonic() - started)
        for holder in holders:
            holder.release()
        self.hold(self.gpu)
        started = time.monotonic()
        with self.assertRaises(Refusal):
            with waits.waiting_resource_locks(self.support, gpu_bound=1.5, readings=lambda root: GOOD):
                self.fail("the block must not start")
        self.assertTrue(1.45 <= time.monotonic() - started < 2.9, time.monotonic() - started)

    def test_a_zero_bound_still_takes_a_free_slot_and_refuses_a_busy_one_at_once(self):
        with waits.waiting_resource_locks(self.support, slot_bound=0, gpu_bound=0, readings=lambda root: GOOD) as record:
            self.assertTrue(record["slot"]["immediate"] and record["gpu"]["immediate"])
        self.hold(*self.slots)
        queued = {}
        started = time.monotonic()
        with self.assertRaises(Refusal) as caught:
            with waits.waiting_resource_locks(self.support, slot_bound=0, readings=lambda root: GOOD,
                                              progress=lambda event, **info: queued.update(info) if event == "queued" else None):
                pass
        self.assertEqual(str(caught.exception), waits.SLOT_REFUSAL)
        self.assertEqual(queued["pids"], [], "no queue entry is started once the bound has passed")
        self.assertLess(time.monotonic() - started, 5)

    # the admission check

    def test_the_admission_check_refuses_before_any_lock_file_is_opened(self):
        for readings in ((20.01, 20 * 1024**3, 200 * 1024**3), (0.0, 12 * 1024**3 - 1, 200 * 1024**3),
                         (0.0, 20 * 1024**3, 50 * 1024**3 - 1), (99.0, 0, 0)):
            with self.subTest(readings=readings):
                record = {}
                with self.assertRaises(Refusal) as caught:
                    with waits.waiting_resource_locks(self.support, record, readings=lambda root: readings):
                        self.fail("the block must not start")
                self.assertEqual(str(caught.exception), waits.RESOURCE_REFUSAL)
                self.assertFalse(any(path.exists() for path in (*self.slots, self.gpu)))
                self.assertFalse(record["admission"]["start"]["passed"])
                self.assertEqual((record["outcome"], record["reason"]), ("refused", waits.RESOURCE_REFUSAL))
        for readings in ((20, 12 * 1024**3, 50 * 1024**3), (0.0, 12 * 1024**3, 50 * 1024**3 + 1)):
            with self.subTest(at_the_limit=readings):
                with waits.waiting_resource_locks(self.support, readings=lambda root: readings) as record:
                    self.assertTrue(record["admission"]["start"]["passed"])

    def test_the_admission_check_is_made_again_once_both_locks_are_held(self):
        calls = []

        def readings(root):
            calls.append(root)
            return GOOD if len(calls) == 1 else (50.0, GOOD[1], GOOD[2])

        output = self.root / "output"
        run = self.start(readings=readings, body=output.mkdir)
        run.finish()
        self.assertEqual(str(run.error), waits.RESOURCE_REFUSAL)
        self.assertEqual(calls, [self.root, self.root])
        self.assertEqual(run.events, ["admission:start", "granted:slot", "granted:gpu", "admission:granted"])
        self.assertFalse(output.exists())
        self.assertTrue(run.record["admission"]["start"]["passed"])
        self.assertFalse(run.record["admission"]["granted"]["passed"])
        self.assert_nothing_queued_or_held(run, *self.slots, self.gpu)

    # nothing is left behind

    def test_an_exception_while_queued_on_the_slots_cancels_every_entry(self):
        holders = self.hold(*self.slots)
        record, queued = {}, {}

        def progress(event, **info):
            if event == "queued":
                queued.update(info)
                raise Stop("stop")

        with self.assertRaises(Stop):
            with waits.waiting_resource_locks(self.support, record, readings=lambda root: GOOD, progress=progress):
                self.fail("the block must not start")
        self.assertEqual(len(queued["pids"]), 3)
        self.assertTrue(all(gone(pid) for pid in queued["pids"]))
        for holder in holders:
            holder.release()
        for path in self.slots:
            self.assertEqual(blocked_pids(path), [])
            self.assertTrue(free_now(path))
        self.assertEqual((record["outcome"], record["reason"]), ("stopped", "Stop"))

    def test_an_exception_while_queued_on_the_gpu_lock_releases_the_slot_too(self):
        self.hold(self.gpu)
        queued = {}

        def progress(event, **info):
            if event == "queued":
                queued.update(info)
                raise Stop("stop")

        with self.assertRaises(Stop):
            with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=progress):
                self.fail("the block must not start")
        self.assertEqual(queued["kind"], "gpu")
        self.assertTrue(all(gone(pid) for pid in queued["pids"]))
        self.assertTrue(free_now(self.slots[0]))
        self.assertEqual(blocked_pids(self.gpu), [])

    def test_an_interrupt_while_waiting_cancels_every_entry_and_releases_the_slot(self):
        def interrupted(number, frame):
            raise InterruptedError("interrupted")

        for held, expected_kind in ((self.slots, "slot"), ((self.gpu,), "gpu")):
            with self.subTest(waiting_for=expected_kind):
                holders = self.hold(*held)
                queued = {}

                def progress(event, **info):
                    if event == "queued":
                        queued[info["kind"]] = info["pids"]

                timer = threading.Timer(0.6, signal.pthread_kill, (threading.main_thread().ident, signal.SIGUSR1))
                previous = signal.signal(signal.SIGUSR1, interrupted)
                try:
                    timer.start()
                    with self.assertRaises(InterruptedError):
                        with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=progress):
                            self.fail("the block must not start")
                finally:
                    timer.cancel()
                    timer.join()
                    signal.signal(signal.SIGUSR1, previous)
                self.assertEqual(list(queued), [expected_kind])
                self.assertTrue(all(gone(pid) for pid in queued[expected_kind]))
                for holder in holders:
                    holder.release()
                for path in (*self.slots, self.gpu):
                    self.assertTrue(free_now(path), path.name)

    def test_an_unexpected_queue_entry_status_is_refused_and_collected(self):
        self.hold(*self.slots)
        queued = {}
        with mock.patch.object(waits, "_QUEUE_ENTRY", "import sys\nsys.exit(3)\n"):
            with self.assertRaises(Refusal) as caught:
                with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD,
                                                  progress=lambda event, **info: queued.update(info) if event == "queued" else None):
                    self.fail("the block must not start")
        self.assertEqual(str(caught.exception), waits.QUEUE_REFUSAL)
        self.assertTrue(all(gone(pid) for pid in queued["pids"]))

    def test_a_queue_entry_that_claims_a_lock_it_did_not_take_is_not_believed(self):
        holders = self.hold(*self.slots)
        queued = {}
        with mock.patch.object(waits, "_QUEUE_ENTRY", "import os\nos._exit(0)\n"):
            with self.assertRaises(Refusal) as caught:
                with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD,
                                                  progress=lambda event, **info: queued.update(info) if event == "queued" else None):
                    self.fail("the block must not start")
        self.assertEqual(str(caught.exception), waits.QUEUE_REFUSAL)
        self.assertTrue(all(gone(pid) for pid in queued["pids"]))
        self.assertFalse(self.gpu.exists())
        for holder in holders:
            holder.release()
        self.assertTrue(all(free_now(path) for path in self.slots))

    def test_the_bound_holds_even_if_a_queue_entry_never_ends(self):
        self.hold(*self.slots)
        queued = {}
        started = time.monotonic()
        with mock.patch.object(waits, "_QUEUE_ENTRY", "import time\ntime.sleep(600)\n"):
            with self.assertRaises(Refusal) as caught:
                with waits.waiting_resource_locks(self.support, slot_bound=1.5, readings=lambda root: GOOD,
                                                  progress=lambda event, **info: queued.update(info) if event == "queued" else None):
                    self.fail("the block must not start")
        self.assertEqual(str(caught.exception), waits.SLOT_REFUSAL)
        self.assertTrue(1.45 <= time.monotonic() - started < 2.9, time.monotonic() - started)
        self.assertEqual(len(queued["pids"]), 3)
        self.assertTrue(all(gone(pid) for pid in queued["pids"]), "an entry that does not end is killed at the bound")

    def test_entries_that_run_out_their_own_timers_end_the_wait_as_a_timeout(self):
        self.hold(*self.slots)
        queued = {}
        started = time.monotonic()
        with mock.patch.object(waits, "_QUEUE_ENTRY", "import signal, time\nsignal.setitimer(signal.ITIMER_REAL, 0.2)\ntime.sleep(600)\n"):
            with self.assertRaises(Refusal) as caught:
                with waits.waiting_resource_locks(self.support, slot_bound=20, readings=lambda root: GOOD,
                                                  progress=lambda event, **info: queued.update(info) if event == "queued" else None):
                    self.fail("the block must not start")
        self.assertEqual(str(caught.exception), waits.SLOT_REFUSAL)
        self.assertLess(time.monotonic() - started, 10, "every entry has ended, so there is nothing left to wait for")
        self.assertTrue(all(gone(pid) for pid in queued["pids"]))

    def test_a_queue_entry_that_cannot_be_watched_is_stopped_before_the_error_leaves(self):
        self.hold(*self.slots)
        started = []
        real = subprocess.Popen

        def popen(*args, **kwargs):
            process = real(*args, **kwargs)
            started.append(process.pid)
            return process

        def refuse(pid):
            raise OSError(24, "no descriptors")

        with mock.patch.object(waits.subprocess, "Popen", popen), mock.patch.object(waits.os, "pidfd_open", refuse):
            with self.assertRaises(OSError):
                with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD):
                    self.fail("the block must not start")
        self.assertEqual(len(started), 1)
        self.assertTrue(gone(started[0]))

    def hold_in_child(self, path, seconds):
        """Another process holding the lock for `seconds`; returned once it holds it."""
        job = subprocess.Popen([sys.executable, "-I", "-c", HOLD, str(path), str(seconds)], stdout=subprocess.PIPE, env={})
        self.addCleanup(lambda: (job.kill(), job.wait(), job.stdout.close()))
        self.assertEqual(job.stdout.readline(), b"held\n")
        return job

    def test_no_descriptor_is_left_open_however_a_wait_ends(self):
        def count():
            return len(os.listdir("/proc/self/fd"))

        def stop_when_queued(event, **info):
            if event == "queued":
                raise Stop("stop")

        jobs = [self.hold_in_child(self.slots[0], 1.5), self.hold_in_child(self.slots[1], 30), self.hold_in_child(self.slots[2], 30)]
        before = count()
        with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD) as record:      # granted by a queue entry
            self.assertFalse(record["slot"]["immediate"])
        self.assertEqual(count(), before, "a wait that was queued and granted")
        jobs[0].wait()
        self.hold_in_child(self.slots[0], 30)
        before = count()
        with self.assertRaises(Refusal):                                                              # a slot timeout
            with waits.waiting_resource_locks(self.support, slot_bound=0.3, readings=lambda root: GOOD):
                pass
        self.assertEqual(count(), before, "a slot timeout")
        with self.assertRaises(Stop):                                                                 # stopped while queued
            with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=stop_when_queued):
                pass
        self.assertEqual(count(), before, "a stop while queued")
        try:                                                  # the same, counted while the exception and its frames are alive
            with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=stop_when_queued):
                pass
        except Stop:
            in_flight = count()
        self.assertEqual(in_flight, before, "a stop while queued, its traceback still alive")
        jobs[1].kill()
        jobs[1].wait()                                                                                # slot 1 is free now
        self.hold_in_child(self.gpu, 1.5)
        before = count()
        calls = []

        def readings(root):
            calls.append(root)
            return GOOD if len(calls) == 1 else (99.0, 0, 0)

        with self.assertRaises(Refusal):                  # the gpu lock granted to a queue entry, then refused by the second check
            with waits.waiting_resource_locks(self.support, slot_bound=0.3, gpu_bound=5, readings=readings):
                pass
        self.assertEqual(len(calls), 2)
        self.assertEqual(count(), before, "a refusal after the gpu lock was granted")
        self.hold_in_child(self.gpu, 30)
        before = count()
        try:                                                  # stopped while queued for the gpu lock with a slot held
            with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=stop_when_queued):
                pass
        except Stop:
            in_flight = count()
        self.assertEqual(in_flight, before, "a stop while queued for the gpu lock, its traceback still alive")

    def test_files_that_were_not_granted_are_closed_before_the_grant_or_the_timeout_is_told(self):
        def count():
            return len(os.listdir("/proc/self/fd"))

        seen = {}

        def progress(event, **info):
            if event in ("granted", "timeout"):
                seen[(event, info["kind"])] = count()

        jobs = [self.hold_in_child(self.slots[0], 1.5), self.hold_in_child(self.slots[1], 30), self.hold_in_child(self.slots[2], 30)]
        before = count()
        with waits.waiting_resource_locks(self.support, readings=lambda root: GOOD, progress=progress):
            pass
        self.assertEqual(seen[("granted", "slot")], before + 1, "only the granted slot file is left open")
        self.assertEqual(seen[("granted", "gpu")], before + 2, "the granted slot file and the gpu lock file")
        jobs[0].wait()
        self.hold_in_child(self.slots[0], 30)
        before = count()
        with self.assertRaises(Refusal):
            with waits.waiting_resource_locks(self.support, slot_bound=0.3, readings=lambda root: GOOD, progress=progress):
                pass
        self.assertEqual(seen[("timeout", "slot")], before, "every file is closed when the timeout is told")

    def test_the_lowest_slot_wins_when_several_are_granted_together(self):
        class Entry:
            def __init__(self, index):
                self.index = index
                self.process = subprocess.Popen([sys.executable, "-I", "-S", "-c", "import time\ntime.sleep(0.2)\n"], env={})
                self.pidfd = os.pidfd_open(self.process.pid)

        entries = [Entry(number) for number in (2, 0, 1)]
        try:
            for entry in entries:       # all three have exited, so all three are ready at the first look
                self.assertEqual(select.select([entry.pidfd], [], [], LIMIT)[0], [entry.pidfd])
            self.assertEqual(waits._first_granted(self.support, entries, time.monotonic() + LIMIT), 0)
        finally:
            for entry in entries:
                entry.process.kill()
                entry.process.wait()
                os.close(entry.pidfd)

    def test_the_queue_entry_ends_by_its_own_timer_and_exits_zero_when_granted(self):
        held, = self.hold(self.slots[0])
        stream = open(self.slots[0], "a")
        self.addCleanup(stream.close)
        started = time.monotonic()
        entry = subprocess.Popen([sys.executable, "-I", "-S", "-c", waits._QUEUE_ENTRY, str(stream.fileno()), "0.4",
                                  str(os.getpid())], pass_fds=(stream.fileno(),), env={})
        self.assertEqual(entry.wait(LIMIT), -signal.SIGALRM)
        self.assertGreaterEqual(time.monotonic() - started, 0.35)
        held.release()
        entry = subprocess.Popen([sys.executable, "-I", "-S", "-c", waits._QUEUE_ENTRY, str(stream.fileno()), "30",
                                  str(os.getpid())], pass_fds=(stream.fileno(),), env={})
        self.assertEqual(entry.wait(LIMIT), 0)
        self.assertFalse(free_now(self.slots[0]), "the lock is held through the descriptor this process still has")
        # told that its parent is another process, the entry leaves at once without asking for the lock
        entry = subprocess.Popen([sys.executable, "-I", "-S", "-c", waits._QUEUE_ENTRY, str(stream.fileno()), "30",
                                  str(os.getpid() + 1)], pass_fds=(stream.fileno(),), env={})
        self.assertEqual(entry.wait(LIMIT), 1)

    def test_a_queue_entry_dies_with_its_parent(self):
        held, = self.hold(self.slots[0])
        self.lock_table_works(self.slots[0])
        script = ("import sys\n"
                  f"sys.path.insert(0, {str(Path(__file__).resolve().parent)!r})\n"
                  "import pair_lock_wait as waits\n"
                  "stream = open(sys.argv[1], 'a')\n"
                  "entry = waits._QueueEntry(0, stream.fileno(), 600.0)\n"
                  "print(entry.process.pid, flush=True)\n"
                  "sys.stdin.read()\n")
        parent = subprocess.Popen([sys.executable, "-I", "-c", script, str(self.slots[0])], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, env={})
        try:
            pid = int(parent.stdout.readline())
            deadline = time.monotonic() + LIMIT
            while time.monotonic() < deadline and blocked_pids(self.slots[0]) != [pid]:
                time.sleep(0.02)
            self.assertFalse(gone(pid))
            parent.kill()
            parent.wait()
            deadline = time.monotonic() + LIMIT
            while time.monotonic() < deadline and not gone(pid):
                time.sleep(0.02)
            self.assertTrue(gone(pid), "an orphaned queue entry must not stay queued")
        finally:
            parent.kill()
            parent.wait()
            parent.stdout.close()
            parent.stdin.close()

    def test_a_block_that_raises_releases_the_gpu_lock_first_and_then_the_slot(self):
        self.hold(self.slots[0])
        Logged.closes = []
        record = {}
        with mock.patch.object(waits, "open", logged_open, create=True):
            with self.assertRaises(KeyError):
                with waits.waiting_resource_locks(self.support, record, readings=lambda root: GOOD):
                    self.assertFalse(free_now(self.slots[1]))
                    self.assertFalse(free_now(self.gpu))
                    raise KeyError("body")
        # slot0 was busy and is let go as soon as slot1 is granted; then the gpu lock is released, then the slot.
        self.assertEqual(Logged.closes, ["slot0.lock", "gpu.lock", "slot1.lock"])
        self.assertEqual(record["outcome"], "acquired")
        self.assertGreaterEqual(record["held_seconds"], 0.0)
        self.assertTrue(free_now(self.slots[1]) and free_now(self.gpu))

    # the pinned support runner, where it does not wait

    def runner(self):
        try:
            data = subprocess.run(["git", "-C", str(ROOT), "show", RUNNER_COMMIT + ":port/tools/ice_game_pair.py"],
                                  capture_output=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("the pinned support runner is not in this checkout")
        self.assertEqual(hashlib.sha256(data).hexdigest(), RUNNER_SHA256)
        folder = self.root / "runner" / "a" / "b"
        folder.mkdir(parents=True)
        (folder / "ice_game_pair.py").write_bytes(data)
        spec = importlib.util.spec_from_file_location("pinned_runner", folder / "ice_game_pair.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.HEAVY_LOCKS, module.GPU_LOCK, module.ROOT = tuple(self.slots), self.gpu, self.root
        return module

    @contextmanager
    def system(self, pressure, available, free):
        real = pathlib.Path.read_text

        def read_text(path, *args, **kwargs):
            if str(path) == "/proc/pressure/cpu":
                return f"some avg10=0.00 avg60={pressure} avg300=0.00 total=0\nfull avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
            if str(path) == "/proc/meminfo":
                return f"MemTotal:       65000000 kB\nMemAvailable:   {available // 1024} kB\n"
            return real(path, *args, **kwargs)

        with mock.patch.object(pathlib.Path, "read_text", read_text), \
                mock.patch.object(shutil, "disk_usage", lambda root: types.SimpleNamespace(free=free)):
            yield

    def test_it_admits_and_refuses_exactly_where_the_pinned_runner_does_when_nothing_is_held(self):
        runner = self.runner()
        limits = (20, 12 * 1024**3, 50 * 1024**3)
        cases = [limits, (20.5, limits[1], limits[2]), (0.0, limits[1] - 1024, limits[2]), (0.0, limits[1], limits[2] - 1),
                 (19.99, limits[1] + 1024, limits[2] + 1), (0.0, 0, 0)]

        def outcome(make):
            try:
                with make():
                    held = (free_now(self.slots[0]), free_now(self.gpu))
                return ("entered", held)
            except Exception as error:
                return (type(error).__name__, str(error))

        for case in cases:
            with self.subTest(readings=case), self.system(*case):
                expected = outcome(runner.resource_locks)
                actual = outcome(lambda: waits.waiting_resource_locks(runner))
                self.assertEqual(actual, expected)
                self.assertEqual(expected[0] == "entered", case[0] <= 20 and case[1] >= limits[1] and case[2] >= limits[2])

    def test_it_takes_and_refuses_what_the_pinned_runner_does_and_waits_where_that_runner_does_not(self):
        runner = self.runner()

        def taken(make):
            with make():
                return [free_now(path) for path in (*self.slots, self.gpu)]

        with self.system(0.0, GOOD[1], GOOD[2]):
            # the runner takes the first slot that is free now, and so does the wait
            self.hold(self.slots[0])
            self.assertEqual(taken(runner.resource_locks), [False, False, True, False])
            self.assertEqual(taken(lambda: waits.waiting_resource_locks(runner)), [False, False, True, False])
            # every slot held: the runner refuses at once with its label; the wait refuses with the same one after its bound
            self.hold(self.slots[1], self.slots[2])
            with self.assertRaises(runner.PairRefusal) as expected:
                with runner.resource_locks():
                    pass
            with self.assertRaises(runner.PairRefusal) as actual:
                with waits.waiting_resource_locks(runner, slot_bound=0.3):
                    pass
            self.assertEqual((type(actual.exception), str(actual.exception)), (type(expected.exception), str(expected.exception)))
            for holder in self.holders:
                holder.release()
            self.holders.clear()
            # the gpu lock held: the runner lets an OSError escape; the wait refuses with a label after its bound
            self.hold(self.gpu)
            with self.assertRaises(BlockingIOError):
                with runner.resource_locks():
                    pass
            with self.assertRaises(runner.PairRefusal) as actual:
                with waits.waiting_resource_locks(runner, gpu_bound=0.3):
                    pass
            self.assertEqual(str(actual.exception), waits.GPU_REFUSAL)
            self.assertTrue(free_now(self.slots[0]))


if __name__ == "__main__":
    unittest.main()
