#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The public build has no network: build.py --public defines CE_NETWORK_OFF, and with it the actual statements at the three
entry points (the CE page's menu entry in ce_netpages.cpp, the role reads in CE_BootStart and CE_SessionConfigureLevel) offer
nothing and read no BEA_NET_HOST / BEA_NET_JOIN. Each statement is taken from its source and compiled with and without the
define; taking the guard out of any one of them must be caught. build.py's define is checked in its source, since a compiled
check would need a whole build. This does not run the game or establish that no other code path starts a session.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

from network_world_tests import actual_body

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
GUARD = "CE_NETWORK_ENABLED"
ROLES = re.compile(r"^[ \t]*bool host = [^;\n]*guest = [^;\n]*;", re.M)

ROLE_DRIVER = """
#include "%(header)s"
static bool Present(const char *) { return true; }
static bool EnvPresent(const char *) { return true; }
int main()
{
	%(statement)s
	return (host ? 1 : 0) | (guest ? 2 : 0);
}
"""

OFFER_DRIVER = """
#include "%(header)s"
#include <stdlib.h>
static bool PortCeMenus() { return true; }
static struct { bool mRecordDemo = false, mPlaybackDemo = false, mAutoConfigTest = false; } CLIPARAMS;
%(body)s
int main() { return PortCeNetOffered() ? 1 : 0; }
"""


def role_statement(path, reader):
    found = [m.group(0).strip() for m in ROLES.finditer(path.read_text()) if reader in m.group(0)]
    if len(found) != 1:
        raise SystemExit("%s: expected one role-read statement naming %s, found %d" % (path.name, reader, len(found)))
    return found[0]


def build_py_defines_network_off(text):
    """--public's block defines CE_NETWORK_OFF; --test-automation's does not."""
    public = text[text.index("    if a.public:\n"):text.index("    sources = build_mirror()")]
    private = text[text.index("    if a.test_automation:\n"):text.index("    if a.system_clang:")]
    return "-DCE_NETWORK_OFF=1" in public and "CE_NETWORK_OFF" not in private


def run(compiler, tmp, name, source, define):
    src = Path(tmp) / (name + ".cpp")
    src.write_text(source)
    exe = Path(tmp) / name
    cmd = [compiler, "-std=c++17", "-O0", "-I", str(GAME), *(["-DCE_NETWORK_OFF=1"] if define else []), str(src), "-o", str(exe)]
    subprocess.run(cmd, check=True, capture_output=True)
    return subprocess.run([str(exe)], capture_output=True, timeout=30, env={"PATH": "/usr/bin"}).returncode


def main():
    ok = True

    def check(name, good):
        nonlocal ok
        print(("PASS " if good else "FAIL ") + name)
        ok &= bool(good)

    build_py = (ROOT / "port/build.py").read_text()
    check("build.py --public defines CE_NETWORK_OFF and --test-automation does not", build_py_defines_network_off(build_py))
    check("mutant: build.py without the define is caught",
          not build_py_defines_network_off(build_py.replace('CE_DEFINES.append("-DCE_NETWORK_OFF=1")', "pass")))

    # A public build compiles none of the network modules; every other port file stays in.
    sys.path.insert(0, str(ROOT / "port"))
    import build
    modules = [build.PORT / "network" / "codec.cpp", build.PORT / "network" / "compatibility.cpp", build.PORT / "game" / "ce_network_session.cpp",
               build.PORT / "game" / "ce_network_lockstep.cpp", build.PORT / "game" / "ce_netpages.cpp", build.PORT / "platform/common/ce_netlobby.cpp",
               build.PORT / "platform/common/ce_netcode.cpp", Path("/x/monocypher-4.0.2/src/monocypher.c")]
    others = [build.PORT / "game" / "ce_frontend.cpp", build.PORT / "platform/common/window.cpp", build.PORT / "platform/common/ce.cpp",
              build.PORT / "platform/linux/files.cpp", build.PORT / "game" / "ce_debrief.cpp",
              build.PORT / "updater/sha256.c"]      # the launcher's SHA-256: the mod checks use it in every build, the network game's join code in a private one
    check("a public build leaves out every network module", all(build.network_module(m) for m in modules))
    check("a public build keeps every other port file", not any(build.network_module(o) for o in others))
    saved_public, saved_network = build.PUBLIC, build.WITH_NETWORK
    try:
        build.PUBLIC, build.WITH_NETWORK = True, False
        # Source selection does not need to fetch missing dependencies for this authored control.
        with patch.object(build.deps, "unpack"):
            selected = build.port_sources("linux")
        check("the actual public source list includes no network modules or legacy network stub",
              not any(build.network_module(src) or src.name in {m.name for m in modules} | {"network_off.cpp"} for src, _, _ in selected))
    finally:
        build.PUBLIC, build.WITH_NETWORK = saved_public, saved_network
    # The default frontend resolves to the hub's own text, with no reference that needs a network stand-in.
    frontend = (ROOT / "port/game/ce_frontend.cpp").read_text()
    check("the network-disabled frontend contains no network-page references", "PortCeNet" not in build.network_view(frontend, off=True))
    check("the legacy network stand-in file is gone and build.py does not add it to the public build",
          not (ROOT / "port/platform/public/network_off.cpp").exists() and "network_off.cpp" not in build_py)
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("a C++ compiler is required")
    header = "ce_network_boot.h"
    boot = role_statement(GAME / "ce_network_boot.cpp", 'Present("BEA_NET_HOST")')
    session = role_statement(GAME / "ce_network_session.cpp", 'EnvPresent("BEA_NET_HOST")')
    offer = actual_body((GAME / "ce_netpages.cpp").read_text(), "bool\tPortCeNetOffered()")
    with tempfile.TemporaryDirectory(prefix="ce-network-off-") as tmp:
        for label, statement in (("CE_BootStart roles", boot), ("CE_SessionConfigureLevel roles", session)):
            for define, want in ((False, 3), (True, 0)):
                got = run(compiler, tmp, "roles", ROLE_DRIVER % {"header": header, "statement": statement}, define)
                check("%s in a %s build: exit %d, want %d" % (label, "public" if define else "private", got, want), got == want)
            if GUARD + " && " not in statement:
                check("%s carry the guard" % label, False)
                continue
            mutant = statement.replace(GUARD + " && ", "")
            got = run(compiler, tmp, "roles", ROLE_DRIVER % {"header": header, "statement": mutant}, True)
            check("mutant: %s without the guard are caught in a public build (exit %d)" % (label, got), got != 0)
        for define, want in ((False, 1), (True, 0)):
            got = run(compiler, tmp, "offer", OFFER_DRIVER % {"header": header, "body": offer}, define)
            check("PortCeNetOffered in a %s build: exit %d, want %d" % ("public" if define else "private", got, want), got == want)
        if "!" + GUARD + " || " not in offer:
            check("PortCeNetOffered carries the guard", False)
        else:
            mutant = offer.replace("!" + GUARD + " || ", "")
            got = run(compiler, tmp, "offer", OFFER_DRIVER % {"header": header, "body": mutant}, True)
            check("mutant: PortCeNetOffered without the guard is caught in a public build (exit %d)" % got, got != 0)
    print("network public-off controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
