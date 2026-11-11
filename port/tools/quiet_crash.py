#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run a test child that is meant to crash (a known-bad control that aborts or faults on purpose) without leaving a core dump.

Some suites prove a failure mode by making a small test program abort or fault. On a machine whose core_pattern pipes to
systemd-coredump, each such crash stores a core file and a journal record, and a desktop may show "Process crashed". What
stops it, measured on Linux 7.2 with systemd-coredump:

  * RLIMIT_CORE 0 stops the stored core file but not the record: the kernel still starts the handler, which lists the crash
    with no core ("none" in `coredumpctl list`).
  * prctl(PR_SET_DUMPABLE, 0) set between fork and exec does nothing for an exec'd program: exec makes the process
    dumpable again. It holds only for a child that is never exec'd, or one that sets it itself.
  * RLIMIT_CORE 1 is the kernel's own "no core dump" value for a piped core_pattern: it logs one kernel line ("RLIMIT_CORE
    is set to 1, aborting core"), starts no handler, and the limit survives exec. That is what preexec() sets, with the
    hard limit as well, so that the child cannot raise it.
  * A sanitizer runtime resets the soft limit to 0 at start unless told not to (disable_coredump=1 is its default), which
    brings the "none" record back, so environment() turns that off.

    import quiet_crash
    quiet_crash.run([str(binary), case], capture_output=True, text=True, timeout=10)      # subprocess.run, quiet
    subprocess.Popen(command, preexec_fn=quiet_crash.preexec, env=quiet_crash.environment(env))

This file is for test runners; the game is not changed by it.
"""
import ctypes
import os
import resource
import subprocess
import sys

NO_CORE_LIMIT = 1                      # the kernel's "abort the dump" value for a piped core_pattern; 0 does not mean that
PR_SET_DUMPABLE = 4
SANITIZER_OPTIONS = ("ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS", "MSAN_OPTIONS", "LSAN_OPTIONS")


def preexec():
    """preexec_fn for a child: no core dump, and no coredump record, from the child or anything it execs."""
    try:
        resource.setrlimit(resource.RLIMIT_CORE, (NO_CORE_LIMIT, NO_CORE_LIMIT))
    except (ValueError, OSError):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))       # a parent that set the hard limit to 0 cannot be undone: no core file, but a record
    if sys.platform.startswith("linux"):
        try:
            ctypes.CDLL(None, use_errno=True).prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)     # lost at exec; kept by a forked-only child
        except OSError:
            pass


def environment(env=None):
    """A copy of env (default: this process's) in which the sanitizer runtimes leave RLIMIT_CORE as preexec() set it."""
    out = dict(os.environ if env is None else env)
    for name in SANITIZER_OPTIONS:
        options = [o for o in out.get(name, "").split(":") if o and not o.startswith("disable_coredump=")]
        out[name] = ":".join(options + ["disable_coredump=0"])
    return out


def run(command, **kwargs):
    """subprocess.run with preexec() and environment(); a caller's own preexec_fn is refused, not silently replaced."""
    if "preexec_fn" in kwargs:
        raise TypeError("quiet_crash.run sets preexec_fn itself")
    kwargs["env"] = environment(kwargs.get("env"))
    return subprocess.run(command, preexec_fn=preexec, **kwargs)
