#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Windows NTFS private ACL, ICE credential, and sealed kernel-section handoff checks under Wine.

    python3 port/tools/ice_windows_acl_wine_tests.py [-v]

Cross-compiles port/tools/tests/ice_windows_acl_wine_driver.cpp with the pinned llvm-mingw
toolchain against the real port/network/windows_private_acl.h, port/network/ice_credentials.h,
and port/game/ce_network_boot_authority.cpp, then executes it under headless Wine (never touching
the desktop, host home, or external network), including a real CreateProcessA(..., bInheritHandles=TRUE)
sealed read-only kernel section handoff and causal mutants for both ACL and read-only section enforcement.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DATA = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port"))
MINGW = Path(os.environ.get("BEA_LLVM_MINGW", DATA / "llvm-mingw/bin"))
CXX = MINGW / "i686-w64-mingw32-clang++"
DRIVER = PORT / "tools/tests/ice_windows_acl_wine_driver.cpp"
ACL_H = PORT / "network/windows_private_acl.h"
AUTH_CPP = PORT / "game/ce_network_boot_authority.cpp"
PREFIX = Path(os.environ["BEA_WINE_PREFIX"]) if os.environ.get("BEA_WINE_PREFIX") else None


def find_juice_include() -> Path | None:
    ice_root = DATA / "ice-relay"
    if not ice_root.is_dir():
        return None
    for candidate in sorted(ice_root.rglob("juice/juice.h")):
        return candidate.parents[1]
    return None


def missing() -> str:
    if PREFIX is None:
        return "BEA_WINE_PREFIX"
    need = []
    if not shutil.which("wine"):
        need.append("wine")
    if not CXX.is_file():
        need.append(f"llvm-mingw ({CXX})")
    if find_juice_include() is None:
        need.append(f"libjuice headers under {DATA / 'ice-relay'}")
    if not (PREFIX / "drive_c/ncuser").is_dir():
        need.append(f"{PREFIX / 'drive_c/ncuser'}")
    return ", ".join(need)


def windows_path(path: Path) -> str:
    rel = path.resolve().relative_to((PREFIX / "drive_c").resolve())
    return "C:\\" + str(rel).replace("/", "\\")


def run_driver(label: str, acl_text: str | None = None, auth_text: str | None = None) -> subprocess.CompletedProcess[str]:
    base = PREFIX / "drive_c/ncuser/ice-win-acl-tests" / label
    if base.exists():
        shutil.rmtree(base)
    src_dir = base / "src"
    (src_dir / "network").mkdir(parents=True)
    (src_dir / "game").mkdir(parents=True)
    (src_dir / "platform/common").mkdir(parents=True)
    (src_dir / "network/windows_private_acl.h").write_text(acl_text if acl_text is not None else ACL_H.read_text())
    auth_path = src_dir / "game/ce_network_boot_authority.cpp"
    auth_path.write_text(auth_text if auth_text is not None else AUTH_CPP.read_text())
    for rel in ("network/network.h", "network/ice_credentials.h", "network/ice_exchange.h", "network/signaling_provider.h",
                "network/ice_signaling.h", "game/ce_network_boot_authority.h", "game/ce_network_stl.h",
                "game/ce_network_campaign.h", "platform/common/ce_netcode.h"):
        dst = src_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PORT / rel, dst)

    juice_inc = find_juice_include()
    exe = base / "driver.exe"
    strict = acl_text is None and auth_text is None
    cmd = [
        str(CXX), "-std=c++17", "-O2",
        *(["-Wall", "-Wextra", "-Werror"] if strict else ["-w"]),
        "-DCE_NETWORK_ICE=1", "-DJUICE_STATIC=1",
        "-I", str(src_dir),
        "-I", str(PORT),
        "-I", str(PORT / "network"),
        "-I", str(PORT / "game"),
        "-I", str(juice_inc),
        str(DRIVER),
        str(auth_path),
        "-ladvapi32", "-lws2_32", "-static-libstdc++", "-static-libgcc",
        "-o", str(exe),
    ]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if built.returncode != 0:
        raise AssertionError(f"{label} failed to compile:\n{built.stdout}\n{built.stderr}")

    work = base / "work"
    work.mkdir(mode=0o700)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    env.update(
        WINEPREFIX=str(PREFIX),
        WINEDEBUG="-all",
        WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
    )
    return subprocess.run(["wine", str(exe), windows_path(work)], capture_output=True, text=True, env=env, timeout=120)


@unittest.skipIf(bool(missing()), f"needs {missing()}")
class WindowsIceAclWineTests(unittest.TestCase):
    def test_real_windows_acl_credentials_and_sealed_handoff_pass(self):
        result = run_driver("actual")
        combined = result.stdout + "\n" + result.stderr
        self.assertEqual(result.returncode, 0, combined)
        self.assertRegex(combined, r"CHILD_CONSUME checks=[1-9][0-9]* failures=0")
        self.assertRegex(combined, r"WINDOWS_ICE_ACL_TESTS checks=[1-9][0-9]* failures=0")

    def test_causal_mutant_bypassed_acl_check_is_caught(self):
        acl = ACL_H.read_text()
        target = "if (!EqualSid(sid, user) && !EqualSid(sid, system.data()) &&\n            !EqualSid(sid, admins.data()) && !EqualSid(sid, creator.data()))\n            return false;"
        self.assertEqual(acl.count(target), 1)
        mutated = acl.replace(target, "(void)sid;", 1)
        result = run_driver("mutant-acl", acl_text=mutated)
        combined = result.stdout + "\n" + result.stderr
        self.assertNotEqual(result.returncode, 0, f"ACL bypass mutant survived:\n{combined}")
        self.assertIn("FAIL:", combined)

    def test_causal_mutant_writable_section_accepted_is_caught(self):
        auth = AUTH_CPP.read_text()
        target = "if (void* writable = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, 0)) {\n        UnmapViewOfFile(writable);\n        return false;\n    }"
        self.assertEqual(auth.count(target), 1)
        mutated = auth.replace(target, "", 1)
        result = run_driver("mutant-writable-section", auth_text=mutated)
        combined = result.stdout + "\n" + result.stderr
        self.assertNotEqual(result.returncode, 0, f"writable-section mutant survived:\n{combined}")
        self.assertIn("refuses forged writable kernel section", combined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
