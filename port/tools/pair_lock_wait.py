#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded blocking waits for one of several heavy-work slots and then the GPU lock.

A pair support runner that takes its two machine locks with a single non-blocking attempt each refuses at once whenever
another job holds either. On a machine where other jobs queue for the same locks with blocking waits, that attempt
never wins. waiting_resource_locks keeps the runner's admission check, lock files, lock order and refusal labels and
changes only how the locks are taken (the runner has no label for a busy GPU lock: it lets the failed attempt escape as a
bare OSError, and here the bound of that wait is refused with GPU_REFUSAL):

1. The admission check (CPU pressure, available memory, free disk) runs first, as in the runner.
2. Any one heavy slot. A slot that is free now is taken at once. Otherwise the caller queues on every slot at the same
   time, so it keeps its place in the order the kernel serves blocking requests: a queue entry is a small child
   process that blocks in flock on the slot file's open description, which it shares with this process, and the first
   slot granted is kept while the other entries are cancelled. The wait is bounded by SLOT_WAIT_SECONDS.
3. While holding that slot, the GPU lock the same way, bounded by GPU_WAIT_SECONDS. The GPU lock is neither held nor
   queued on while a slot is being waited for.
4. The admission check again, because the waits took time.
5. The caller's block runs holding both locks. Leaving it releases the GPU lock first and then the slot.

At a bound, the runner's own refusal is raised before the caller's block starts, so nothing that block would create
exists. A refusal, an exception or an interrupt at any point releases everything held and cancels every queue entry.
The record the caller passes in is filled as the waits proceed: which lock file was granted, how long each wait took
and what the admission readings were. This module opens only the lock files it is given and starts only its queue
entries.

A lock granted to a queue entry is held through the descriptor this process shares with it and is released only when this
process closes that descriptor. The kernel's lock table names the process that asked for a lock, so for a lock taken
after a wait it lists a queue entry that has already exited, not this process.
"""
from __future__ import annotations

from contextlib import ExitStack, contextmanager
import datetime
import fcntl
import math
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import time

SLOT_WAIT_SECONDS = 2700.0   # the longest wait for any heavy slot
GPU_WAIT_SECONDS = 1200.0    # the longest wait for the GPU lock, made while a slot is held
# The runner's own labels: a slot it cannot take, and a failed admission check. The GPU label is new, because the
# runner lets the failed non-blocking attempt on the GPU lock escape as a bare OSError.
SLOT_REFUSAL = "ordinary heavy slots occupied"
GPU_REFUSAL = "gpu lock occupied"
RESOURCE_REFUSAL = "run resources unavailable"
QUEUE_REFUSAL = "lock queue entry failed"
# The runner's admission limits.
PRESSURE_LIMIT = 20
AVAILABLE_MINIMUM = 12 * 1024**3
FREE_MINIMUM = 50 * 1024**3

# One queue entry, run by a child process on a descriptor it inherits: block in flock, exit 0 once the lock is granted.
# It dies with its parent (PR_SET_PDEATHSIG, with a check that the parent was still there), and if the lock is not
# granted when its timer fires the default action of SIGALRM ends it, so an entry cannot outlive its bound either.
_QUEUE_ENTRY = (
    "import ctypes, fcntl, os, signal, sys\n"
    "ctypes.CDLL(None).prctl(1, signal.SIGKILL)\n"
    "if os.getppid() != int(sys.argv[3]):\n"
    "    os._exit(1)\n"
    "signal.setitimer(signal.ITIMER_REAL, float(sys.argv[2]))\n"
    "fcntl.flock(int(sys.argv[1]), fcntl.LOCK_EX)\n"
    "os._exit(0)\n")


def system_readings(root):
    """CPU pressure (60 s average), available memory in bytes and free disk bytes under `root`, read as the runner reads them."""
    pressure = float(Path("/proc/pressure/cpu").read_text().split("avg60=")[1].split()[0])
    available = int(next(line.split()[1] for line in Path("/proc/meminfo").read_text().splitlines()
                         if line.startswith("MemAvailable:"))) * 1024
    return pressure, available, shutil.disk_usage(root).free


def _admission(support, readings):
    pressure, available, free = readings(support.ROOT)
    return dict(pressure=pressure, available_bytes=available, free_bytes=free,
                passed=bool(pressure <= PRESSURE_LIMIT and available >= AVAILABLE_MINIMUM and free >= FREE_MINIMUM))


def _utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


class _QueueEntry:
    """One blocking flock request, made by a child process on a descriptor it shares with this process."""

    def __init__(self, index, fd, seconds):
        self.index = index
        self.pidfd = None
        command = [sys.executable, "-I", "-S", "-c", _QUEUE_ENTRY, str(fd), repr(seconds), str(os.getpid())]
        self.process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL, env={}, close_fds=True, pass_fds=(fd,))
        try:
            self.pidfd = os.pidfd_open(self.process.pid)
        except BaseException:
            self.cancel()
            raise

    def cancel(self):
        """End the child and collect it, so that the shared descriptor is referenced by this process alone."""
        try:
            if self.process.poll() is None:
                if self.pidfd is None:
                    self.process.kill()
                else:
                    signal.pidfd_send_signal(self.pidfd, signal.SIGKILL)
            self.process.wait()
        finally:
            if self.pidfd is not None:
                os.close(self.pidfd)
                self.pidfd = None


def _first_granted(support, entries, deadline):
    """The lowest index among the queue entries granted their lock, waiting until one is or `deadline` passes (then None)."""
    poller = select.poll()
    waiting = {}
    for entry in entries:
        poller.register(entry.pidfd, select.POLLIN)
        waiting[entry.pidfd] = entry
    granted = []
    while waiting and not granted:
        left = deadline - time.monotonic()
        if left <= 0:
            break
        for pidfd, _ in poller.poll(math.ceil(left * 1000)):
            entry = waiting.pop(pidfd)
            poller.unregister(pidfd)
            status = entry.process.wait()
            if status == 0:
                granted.append(entry.index)
            elif status != -signal.SIGALRM:
                raise support.PairRefusal(QUEUE_REFUSAL)
    return min(granted) if granted else None


def _queue(support, streams, deadline, note, kind):
    """Queue on every stream at once; the lowest index granted before `deadline`, or None. No entry outlives the call."""
    entries = []
    try:
        for index, stream in enumerate(streams):
            left = deadline - time.monotonic()
            if left <= 0:
                break
            entries.append(_QueueEntry(index, stream.fileno(), left))
        note("queued", kind=kind, pids=[entry.process.pid for entry in entries])
        return _first_granted(support, entries, deadline)
    finally:
        for entry in entries:
            entry.cancel()


def _granted(support, paths, streams, index, immediate, began, note, kind):
    waited = time.monotonic() - began
    kept = streams[index]
    if not immediate:
        try:
            # A lock a queue entry took on the shared descriptor is held through this very descriptor: asking for it again
            # here succeeds without change. Refused, the entry did not really hold it.
            fcntl.flock(kept, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise support.PairRefusal(QUEUE_REFUSAL) from None
    for number, stream in enumerate(streams):
        if number != index:
            stream.close()
    note("granted", kind=kind, index=index, immediate=immediate, waited=waited)
    return kept, dict(index=index, path=str(paths[index]), immediate=immediate, waited_seconds=round(waited, 3))


def _hold_first(support, paths, bound, note, kind):
    """Take the first lock file that is free now, else queue on all of them until one is granted.

    Returns (stream, info). The stream holds the granted lock and is the only file left open. It is None, with every
    file closed, when nothing was granted within `bound` seconds."""
    began = time.monotonic()
    streams = []
    try:
        for index, path in enumerate(paths):
            streams.append(open(path, "a"))
            try:
                fcntl.flock(streams[index], fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                continue
            return _granted(support, paths, streams, index, True, began, note, kind)
        index = _queue(support, streams, began + bound, note, kind)
        if index is not None:
            return _granted(support, paths, streams, index, False, began, note, kind)
        waited = time.monotonic() - began
        for stream in streams:
            stream.close()
        note("timeout", kind=kind, waited=waited)
        return None, dict(timed_out=True, waited_seconds=round(waited, 3))
    except BaseException:
        for stream in streams:
            stream.close()
        raise


@contextmanager
def waiting_resource_locks(support, record=None, *, slot_bound=SLOT_WAIT_SECONDS, gpu_bound=GPU_WAIT_SECONDS,
                           readings=system_readings, progress=None):
    """Hold one heavy slot and the GPU lock of `support` (HEAVY_LOCKS, GPU_LOCK, ROOT and PairRefusal) for the block.

    `record` is filled as the waits proceed and, when the block ends, is the complete account. `readings(root)` returns
    (pressure, available bytes, free bytes); `progress(event, **info)` is told each step, so a caller or a test can
    follow the waits. Raises support.PairRefusal at a bound or when the admission check fails."""
    record = {} if record is None else record
    note = progress if progress is not None else (lambda event, **info: None)
    record.update(schema=1, outcome="waiting")
    try:
        with ExitStack() as held:
            note("admission", stage="start")
            record["admission"] = dict(start=_admission(support, readings))
            if not record["admission"]["start"]["passed"]:
                raise support.PairRefusal(RESOURCE_REFUSAL)
            for kind, paths, bound, label in (("slot", tuple(support.HEAVY_LOCKS), slot_bound, SLOT_REFUSAL),
                                              ("gpu", (support.GPU_LOCK,), gpu_bound, GPU_REFUSAL)):
                record[kind] = dict(bound_seconds=bound, began_utc=_utc())
                stream, info = _hold_first(support, paths, bound, note, kind)
                record[kind].update(info, ended_utc=_utc())
                if stream is None:
                    raise support.PairRefusal(label)
                held.enter_context(stream)
            note("admission", stage="granted")
            record["admission"]["granted"] = _admission(support, readings)
            if not record["admission"]["granted"]["passed"]:
                raise support.PairRefusal(RESOURCE_REFUSAL)
            record["outcome"] = "acquired"
            note("acquired")
            entered = time.monotonic()
            try:
                yield record
            finally:
                record["held_seconds"] = round(time.monotonic() - entered, 3)
    except BaseException as error:
        if record["outcome"] != "acquired":
            refused = isinstance(error, support.PairRefusal)
            record.update(outcome="refused" if refused else "stopped", reason=str(error) if refused else type(error).__name__)
        raise
