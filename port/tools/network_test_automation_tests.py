#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CE_NET_TEST_AUTOMATION controls: the private network test switch cannot reach a public build, and only it lets
authored BEA_PORT_FAKE_KEYS/FAKE_CLICK input count as idle at network admission.

The actual CE_NetWindowAutomationIdle body from window.cpp is compiled into a small driver with and without the define.
This does not run the game or establish gameplay; tapes, probes and dumps are owned by other predicates.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from network_world_tests import actual_body

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r"""
#include <cstdio>
static bool gWindowAutomationReady, gWindowAutomationConfigured;
static int gWindowFakeState;
static const char *gWindowFakeKeys;
%s
int main()
{
	int fails = 0;
	auto check = [&](const char *name, int got, int want) {
		std::printf("%%s %%s got=%%d want=%%d\n", got == want ? "PASS" : "FAIL", name, got, want);
		fails += got != want;
	};
	check("unprepared", CE_NetWindowAutomationIdle(), -1);
	gWindowAutomationReady = true; gWindowFakeState = 2; gWindowFakeKeys = nullptr; gWindowAutomationConfigured = false;
	check("no-automation", CE_NetWindowAutomationIdle(), 1);
	gWindowFakeKeys = "400:82:148:d,580:82:148:u"; gWindowAutomationConfigured = true;
	check("fake-keys", CE_NetWindowAutomationIdle(), TEST_BUILD ? 1 : 0);
	gWindowFakeKeys = nullptr; gWindowFakeState = 1; gWindowAutomationConfigured = true;
	check("fake-click", CE_NetWindowAutomationIdle(), TEST_BUILD ? 1 : 0);
	return fails != 0;
}
"""


def main():
    ok = True
    # 1. The build refuses the switch together with --public before doing any work.
    p = subprocess.run([sys.executable, str(ROOT / "port/build.py"), "--public", "--test-automation", "--syntax", "x.cpp"],
                       capture_output=True, text=True, timeout=60)
    refused = p.returncode != 0 and "cannot be combined with --public" in p.stdout + p.stderr
    print(("PASS" if refused else "FAIL") + " build.py refuses --test-automation with --public (exit %d)" % p.returncode)
    ok &= refused
    # 2. A test-automation build carries its own network identity, never the public one of the same source.
    sys.path.insert(0, str(ROOT / "port"))
    import build
    if build.deps.SYSROOT_NAME == "sniper":
        build.PUBLIC, build.TEST_AUTOMATION = True, False
        build.configure_network_identity()
        public = build.NETWORK_SOURCE_ID
        build.PUBLIC, build.TEST_AUTOMATION = False, True
        build.configure_network_identity()
        test = build.NETWORK_SOURCE_ID
        distinct = bool(public and test and public != test)
        print(("PASS" if distinct else "FAIL") + " test-automation network identity differs from the public one (%s vs %s)" % (
            (test or "none")[:12], (public or "none")[:12]))
        ok &= distinct
    else:
        print("SKIP network identity check: sysroot %s is not the public one" % build.deps.SYSROOT_NAME)
    # 3. The actual predicate: a normal build refuses authored input; only the test build counts it as idle.
    text = (ROOT / "port/platform/common/window.cpp").read_text()
    body = actual_body(text, 'extern "C" int CE_NetWindowAutomationIdle(void)')
    compiler = shutil.which("clang++")
    if not compiler:
        raise SystemExit("clang++ is required")
    with tempfile.TemporaryDirectory(prefix="ce-test-automation-") as tmp:
        src = Path(tmp) / "driver.cpp"
        src.write_text(DRIVER % body)
        for name, defines in (("normal", ["-DTEST_BUILD=0"]), ("test-automation", ["-DTEST_BUILD=1", "-DCE_NET_TEST_AUTOMATION=1"])):
            exe = Path(tmp) / name
            subprocess.run([compiler, "-std=c++17", "-O1", *defines, str(src), "-o", str(exe)], check=True)
            r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            sys.stdout.write("".join("  %s: %s\n" % (name, l) for l in r.stdout.splitlines()))
            print(("PASS" if r.returncode == 0 else "FAIL") + " %s build predicate (exit %d)" % (name, r.returncode))
            ok &= r.returncode == 0
    print("network test automation controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
