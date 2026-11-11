#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""quiet_crash.py: a child that aborts on purpose through the helper leaves no core dump and no coredumpctl record.

The machine check needs systemd-coredump (a core_pattern that pipes to it) and coredumpctl, and skips without them."""
import os
import resource
import shutil
import signal
import subprocess
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import quiet_crash  # noqa: E402

ABORT = [sys.executable, "-c", "import os; os.abort()"]
SHOW_LIMIT = [sys.executable, "-c",
              "import resource,os; print(*resource.getrlimit(resource.RLIMIT_CORE)); print(os.environ.get('ASAN_OPTIONS'))"]


def records_for(pid):
    listed = subprocess.run(["coredumpctl", "list", "--no-pager", "--no-legend", str(pid)], capture_output=True, text=True, timeout=30)
    return [line for line in listed.stdout.splitlines() if line.strip()]


def systemd_coredump_handles_crashes():
    try:
        return shutil.which("coredumpctl") is not None and "systemd-coredump" in Path("/proc/sys/kernel/core_pattern").read_text()
    except OSError:
        return False


class QuietCrashTests(unittest.TestCase):
    def test_the_child_runs_with_the_kernels_no_dump_limit_that_exec_keeps(self):
        r = quiet_crash.run(SHOW_LIMIT, capture_output=True, text=True, timeout=30)
        self.assertEqual(0, r.returncode, r.stderr)
        self.assertEqual(f"{quiet_crash.NO_CORE_LIMIT} {quiet_crash.NO_CORE_LIMIT}", r.stdout.splitlines()[0])      # not 0: 0 still starts the handler
        self.assertEqual(1, quiet_crash.NO_CORE_LIMIT)

    def test_a_child_cannot_raise_the_limit_again(self):
        code = "import resource,sys\ntry:\n    resource.setrlimit(resource.RLIMIT_CORE,(resource.RLIM_INFINITY,resource.RLIM_INFINITY)); print('raised')\nexcept (ValueError, OSError): print('refused')"
        r = quiet_crash.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=30)
        self.assertEqual("refused", r.stdout.strip(), r.stderr)

    def test_the_sanitizer_runtimes_are_told_to_leave_the_limit_alone(self):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:disable_coredump=1:abort_on_error=1")
        r = quiet_crash.run(SHOW_LIMIT, capture_output=True, text=True, timeout=30, env=env)
        self.assertEqual("detect_leaks=1:abort_on_error=1:disable_coredump=0", r.stdout.splitlines()[1])
        self.assertEqual("disable_coredump=0", quiet_crash.environment({})["UBSAN_OPTIONS"])
        self.assertEqual({"A": "1"}.items() & quiet_crash.environment({"A": "1"}).items(), {("A", "1")})       # the rest is kept

    def test_a_preexec_function_of_the_callers_is_refused(self):
        with self.assertRaises(TypeError):
            quiet_crash.run(ABORT, preexec_fn=lambda: None)

    def test_the_parent_keeps_its_own_limit(self):
        before = resource.getrlimit(resource.RLIMIT_CORE)
        quiet_crash.run([sys.executable, "-c", "pass"], timeout=30)
        self.assertEqual(before, resource.getrlimit(resource.RLIMIT_CORE))

    def test_a_deliberate_abort_leaves_no_new_coredump_record(self):
        if not systemd_coredump_handles_crashes():
            self.skipTest("no coredumpctl, or core_pattern does not pipe to systemd-coredump")
        child = subprocess.Popen(ABORT, preexec_fn=quiet_crash.preexec, env=quiet_crash.environment(), stderr=subprocess.DEVNULL)
        self.assertEqual(-signal.SIGABRT, child.wait(timeout=30))        # it did abort
        time.sleep(2.0)                                                  # systemd-coredump records a moment after the crash
        self.assertEqual([], records_for(child.pid), "a deliberate abort was recorded")
        again = quiet_crash.run(ABORT, stderr=subprocess.DEVNULL, timeout=30)
        self.assertEqual(-signal.SIGABRT, again.returncode)


if __name__ == "__main__":
    unittest.main(verbosity=2)
