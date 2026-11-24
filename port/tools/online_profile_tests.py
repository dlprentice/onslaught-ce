#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for port/network/online_profile.{h,cpp}: Ed25519 keygen, owner-only ACLs,
RFC 8032 SHA-512 cross-verification against Node.js WebCrypto, Crockford base32 player_id,
#tag derivation, callsign sanitization, stats persistence, and Linux32/Windows32 cross-builds.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
DATA = Path(os.environ.get("BEA_PORT_DATA", ROOT / "local-data/port"))
MONO_SRC = DATA / "src/monocypher-4.0.2/src"
LLVM_BIN = DATA / "llvm/bin"
SYSROOT = DATA / "sysroot-sniper"
MINGW_BIN = Path(os.environ.get("BEA_LLVM_MINGW", DATA / "llvm-mingw/bin"))

CROCKFORD_LOWER = "0123456789abcdefghjkmnpqrstvwxyz"

DRIVER_CPP = r"""
// SPDX-License-Identifier: GPL-3.0-only
#include "online_profile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include "windows_private_acl.h"
#include <aclapi.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
int gChecks = 0;
int gFails = 0;

void Check(bool cond, const char* msg) {
    ++gChecks;
    if (!cond) {
        ++gFails;
        std::fprintf(stderr, "FAIL: %s\n", msg);
    }
}

#ifdef _WIN32
bool GrantWorldRead(const std::wstring& path) {
    std::array<BYTE, SECURITY_MAX_SID_SIZE> world{};
    DWORD worldSize = static_cast<DWORD>(world.size());
    if (!CreateWellKnownSid(WinWorldSid, nullptr, world.data(), &worldSize)) return false;
    std::vector<BYTE> userSid;
    if (!CeWinAcl::CurrentUserSid(userSid)) return false;
    EXPLICIT_ACCESSW ea[2]{};
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(userSid.data());
    ea[1].grfAccessPermissions = GENERIC_READ;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(world.data());
    PACL newDacl = nullptr;
    if (SetEntriesInAclW(2, ea, nullptr, &newDacl) != ERROR_SUCCESS || !newDacl) return false;
    DWORD r = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    nullptr, nullptr, newDacl, nullptr);
    LocalFree(newDacl);
    return r == ERROR_SUCCESS;
}
#endif
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: driver <workdir>\n");
        return 2;
    }
    const std::string base = argv[1];

    // 1. RFC 8032 Test Vector 1 (seed = 9d61b19d... -> known Ed25519 public key d75a9801...).
    {
        const uint8_t rfc_seed[32] = {
            0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60,
            0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
            0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
            0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60
        };
        CeOnlineProfile rfc;
        Check(rfc.InitFromSeed(rfc_seed, "AquilaPilot"), "RFC 8032 seed init");
        Check(rfc.PubkeyHex() == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
              "RFC 8032 SHA-512 Ed25519 public key matches exact standard");
        Check(rfc.PlayerId().size() == 26, "player_id is 26 Crockford base32 chars");
        Check(rfc.Tag().size() == 4, "tag is 4 uppercase hex chars");

        const std::string nonce = "0123456789abcdef0123456789abcdef";
        const uint64_t issued = 1763900000ULL;
        std::string sig;
        Check(rfc.SignAuthChallenge(nonce, issued, rfc.Callsign(), sig), "RFC auth sign");
        Check(sig.size() == 128, "signature is 128 hex chars");
        Check(CeOnlineProfile::VerifyAuthChallenge(rfc.PubkeyHex(), nonce, issued, rfc.Callsign(), sig),
              "RFC auth verify succeeds");

        // Tampered checks.
        std::string bad_sig = sig;
        bad_sig[0] = (bad_sig[0] == '0') ? '1' : '0';
        Check(!CeOnlineProfile::VerifyAuthChallenge(rfc.PubkeyHex(), nonce, issued, rfc.Callsign(), bad_sig),
              "tampered signature rejected");
        Check(!CeOnlineProfile::VerifyAuthChallenge(rfc.PubkeyHex(), nonce, issued + 1, rfc.Callsign(), sig),
              "tampered timestamp rejected");
        Check(!CeOnlineProfile::VerifyAuthChallenge(rfc.PubkeyHex(), nonce, issued, "OtherPilot", sig),
              "tampered callsign rejected");
        Check(!CeOnlineProfile::VerifyAuthChallenge(rfc.PubkeyHex(), "ffffffffffffffffffffffffffffffff", issued, rfc.Callsign(), sig),
              "tampered nonce rejected");

        // Emit JSON vector for Node.js WebCrypto cross-verification.
        std::printf("VECTOR_JSON={\"pubkey\":\"%s\",\"player_id\":\"%s\",\"tag\":\"%s\","
                    "\"nonce\":\"%s\",\"issued_at\":%llu,\"callsign\":\"%s\",\"signature\":\"%s\"}\n",
                    rfc.PubkeyHex().c_str(), rfc.PlayerId().c_str(), rfc.Tag().c_str(),
                    nonce.c_str(), static_cast<unsigned long long>(issued),
                    rfc.Callsign().c_str(), sig.c_str());
    }

    // 2. Callsign sanitization & validation.
    {
        Check(CeOnlineProfile::SanitizeCallsign("  Ace   Pilot_1-2  ") == "Ace Pilot_1-2",
              "callsign trims and collapses spaces");
        Check(CeOnlineProfile::SanitizeCallsign("Bad\nName\t!@#$%^&*()") == "BadName",
              "callsign strips unsafe/control chars");
        Check(CeOnlineProfile::SanitizeCallsign("ab", "Pilot1234") == "Pilot1234",
              "too-short callsign falls back");
        Check(CeOnlineProfile::SanitizeCallsign("1234567890123456789012345").size() == 20,
              "overlong callsign truncated to 20");
        Check(!CeOnlineProfile::IsValidCallsign("ab"), "2-char callsign invalid");
        Check(!CeOnlineProfile::IsValidCallsign(" LeadSpace"), "leading space invalid");
        Check(!CeOnlineProfile::IsValidCallsign("Two  Spaces"), "double space invalid");
        Check(!CeOnlineProfile::IsValidCallsign("Bad\nLine"), "newline invalid");
        Check(CeOnlineProfile::IsValidCallsign("Forseti_Ace-01"), "valid callsign accepted");
    }

    // 3. Disk keygen, permissions, reload, stats & settings persistence.
    {
#ifdef _WIN32
        const std::string dir1 = base + "\\prof1";
#else
        const std::string dir1 = base + "/prof1";
#endif
        CeOnlineProfile p1;
        std::string err;
        Check(p1.LoadOrCreate(dir1.c_str(), &err), "LoadOrCreate creates new profile");
        Check(p1.Loaded(), "profile loaded");
        Check(p1.Callsign() == "Pilot" + p1.Tag(), "default callsign is Pilot+TAG");
        Check(p1.DisplayName() == p1.Callsign() + "#" + p1.Tag(), "display name Callsign#TAG");
        Check(p1.SetCallsign("  Valkyrie  One  ", true, &err), "SetCallsign persists");
        Check(p1.Callsign() == "Valkyrie One", "SetCallsign sanitized");
        Check(!p1.SetCallsign("!!", true, &err), "invalid callsign rejected");
        Check(p1.RecordMatchResult(true, true, &err), "RecordMatchResult win");
        Check(p1.RecordMatchResult(false, true, &err), "RecordMatchResult loss");
        Check(p1.SetPreferredSide("muspell", true, &err), "SetPreferredSide muspell");
        Check(!p1.SetPreferredSide("invalid", true, &err), "invalid side rejected");
        Check(p1.SetLobbyUrl("https://lobby.example.invalid", true, &err), "SetLobbyUrl valid");
        Check(!p1.SetLobbyUrl("https://bad url\n", true, &err), "invalid lobby url rejected");

        // Verify strict file permissions on key file.
#ifndef _WIN32
        struct stat st{};
        const std::string key_path = dir1 + "/online-profile.key";
        Check(lstat(key_path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
              (st.st_mode & 0777) == 0600 && st.st_size == 32,
              "Linux online-profile.key has strict 0600 permissions and 32 bytes");
#endif

        // Reload and verify identity + settings match exactly.
        CeOnlineProfile p2;
        Check(p2.LoadOrCreate(dir1.c_str(), &err), "reload existing profile succeeds");
        Check(p2.PubkeyHex() == p1.PubkeyHex(), "reloaded pubkey identical");
        Check(p2.PlayerId() == p1.PlayerId(), "reloaded player_id identical");
        Check(p2.Tag() == p1.Tag(), "reloaded tag identical");
        Check(p2.Callsign() == "Valkyrie One", "reloaded callsign persisted");
        Check(p2.MatchesPlayed() == 2 && p2.MatchesWon() == 1, "reloaded match stats persisted");
        Check(p2.PreferredSide() == "muspell", "reloaded preferred_side persisted");
        Check(p2.LobbyUrl() == "https://lobby.example.invalid", "reloaded lobby_url persisted");

        // Emit runtime-generated key signature for WebCrypto check too.
        const std::string nonce2 = "fedcba9876543210fedcba9876543210";
        const uint64_t issued2 = 1763900123ULL;
        std::string sig2;
        Check(p2.SignAuthChallenge(nonce2, issued2, p2.Callsign(), sig2), "runtime key auth sign");
        std::printf("VECTOR2_JSON={\"pubkey\":\"%s\",\"player_id\":\"%s\",\"tag\":\"%s\","
                    "\"nonce\":\"%s\",\"issued_at\":%llu,\"callsign\":\"%s\",\"signature\":\"%s\"}\n",
                    p2.PubkeyHex().c_str(), p2.PlayerId().c_str(), p2.Tag().c_str(),
                    nonce2.c_str(), static_cast<unsigned long long>(issued2),
                    p2.Callsign().c_str(), sig2.c_str());

        // 4. Insecure key file permissions MUST fail closed!
#ifdef _WIN32
        std::wstring wide_key;
        Check(CeWinAcl::NormalizeAbsolutePath((dir1 + "\\online-profile.key").c_str(), wide_key),
              "normalize key path");
        Check(GrantWorldRead(wide_key), "grant world read on Windows key");
        CeOnlineProfile p_bad;
        Check(!p_bad.LoadOrCreate(dir1.c_str(), &err),
              "Windows refuses world-readable online-profile.key");
#else
        Check(chmod(key_path.c_str(), 0644) == 0, "chmod 0644 key");
        CeOnlineProfile p_bad;
        Check(!p_bad.LoadOrCreate(dir1.c_str(), &err),
              "Linux refuses 0644 online-profile.key");
        Check(chmod(key_path.c_str(), 0600) == 0, "restore 0600 key");

        // Symlink key MUST fail closed!
        const std::string dir_sym = base + "/prof_sym";
        Check(mkdir(dir_sym.c_str(), 0700) == 0, "mkdir prof_sym");
        Check(symlink(key_path.c_str(), (dir_sym + "/online-profile.key").c_str()) == 0, "symlink key");
        CeOnlineProfile p_sym;
        Check(!p_sym.LoadOrCreate(dir_sym.c_str(), &err), "Linux refuses symlinked online-profile.key");

        // Truncated key MUST fail closed!
        const std::string dir_trunc = base + "/prof_trunc";
        Check(mkdir(dir_trunc.c_str(), 0700) == 0, "mkdir prof_trunc");
        int fd = open((dir_trunc + "/online-profile.key").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        Check(fd >= 0 && write(fd, "short", 5) == 5 && close(fd) == 0, "write short key");
        CeOnlineProfile p_trunc;
        Check(!p_trunc.LoadOrCreate(dir_trunc.c_str(), &err), "Linux refuses short online-profile.key");

        // Nested recursive directory creation + symlinked ancestor (e.g. /home -> /var/home) MUST succeed (#27)
        const std::string nested_dir = base + "/nested_a/nested_b/prof_nested";
        CeOnlineProfile p_nested;
        Check(p_nested.LoadOrCreate(nested_dir.c_str(), &err), "Linux recursively creates nested user_dir");
        const std::string real_anc = base + "/real_home";
        const std::string link_anc = base + "/link_home";
        Check(mkdir(real_anc.c_str(), 0700) == 0 && symlink(real_anc.c_str(), link_anc.c_str()) == 0,
              "create symlinked ancestor");
        CeOnlineProfile p_anc;
        Check(p_anc.LoadOrCreate((link_anc + "/onslaught-ce").c_str(), &err),
              "Linux permits symlinked ancestor dir (/home -> /var/home)");
        // Symlinked final profile directory itself MUST still be refused!
        const std::string link_leaf = base + "/link_leaf_dir";
        Check(symlink(dir1.c_str(), link_leaf.c_str()) == 0, "symlink final profile dir");
        CeOnlineProfile p_leaf;
        Check(!p_leaf.LoadOrCreate(link_leaf.c_str(), &err),
              "Linux refuses symlinked final profile directory");
#endif
    }

    std::printf("ONLINE_PROFILE_TESTS checks=%d failures=%d\n", gChecks, gFails);
    return gFails == 0 ? 0 : 1;
}
"""

NODE_VERIFY_JS = r"""
const vecs = JSON.parse(process.argv[1]);
const pkcs8Prefix = Buffer.from('302a300506032b6570032100', 'hex');

(async () => {
  for (const v of vecs) {
    const pubRaw = Buffer.from(v.pubkey, 'hex');
    const spki = Buffer.concat([pkcs8Prefix, pubRaw]);
    const key = await crypto.subtle.importKey('spki', spki, { name: 'Ed25519' }, false, ['verify']);
    const msg = Buffer.from(
      `onslaught-ce-lobby-auth-v1\n${v.nonce}\n${v.issued_at}\n${v.pubkey}\n${v.callsign}`,
      'utf8'
    );
    const sig = Buffer.from(v.signature, 'hex');
    const ok = await crypto.subtle.verify('Ed25519', key, sig, msg);
    if (!ok) {
      console.error('WebCrypto Ed25519 verify FAILED for', v.callsign);
      process.exit(1);
    }
    const tamperedSig = Buffer.from(sig);
    tamperedSig[0] ^= 0x01;
    const badOk = await crypto.subtle.verify('Ed25519', key, tamperedSig, msg);
    if (badOk) {
      console.error('WebCrypto Ed25519 accepted tampered signature');
      process.exit(1);
    }
    const tamperedMsg = Buffer.from(msg.toString('utf8') + 'x', 'utf8');
    const badMsgOk = await crypto.subtle.verify('Ed25519', key, sig, tamperedMsg);
    if (badMsgOk) {
      console.error('WebCrypto Ed25519 accepted tampered message');
      process.exit(1);
    }
  }
  console.log('WEBCRYPTO_ED25519_PASS', vecs.length);
})().catch((e) => { console.error(e); process.exit(1); });
"""


def crockford_base32_lower(data: bytes) -> str:
    out = []
    acc = 0
    bits = 0
    for b in data:
        acc = (acc << 8) | b
        bits += 8
        while bits >= 5:
            bits -= 5
            out.append(CROCKFORD_LOWER[(acc >> bits) & 31])
    if bits > 0:
        out.append(CROCKFORD_LOWER[(acc << (5 - bits)) & 31])
    return "".join(out)


def parse_vectors(stdout: str) -> list[dict]:
    vecs = []
    for line in stdout.splitlines():
        for prefix in ("VECTOR_JSON=", "VECTOR2_JSON="):
            if line.startswith(prefix):
                vecs.append(json.loads(line[len(prefix):]))
    return vecs


class OnlineProfileTests(unittest.TestCase):
    def _compile_native(self, tmp: Path, profile_cpp: Path, asan: bool = True) -> Path:
        cc = shutil.which("clang") or shutil.which("cc")
        cxx = shutil.which("clang++") or shutil.which("c++")
        self.assertTrue(cc and cxx, "native C/C++ compiler required")
        san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if asan else []
        objs = []
        for src, name in [
            (MONO_SRC / "monocypher.c", "mono.o"),
            (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
            (PORT / "updater/sha256.c", "sha256.o"),
        ]:
            obj = tmp / name
            subprocess.run(
                [cc, "-std=gnu11", "-O2", *san, "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                check=True, capture_output=True, text=True, timeout=60,
            )
            objs.append(obj)
        driver = tmp / "driver.cpp"
        driver.write_text(DRIVER_CPP)
        exe = tmp / "profile_tests"
        subprocess.run(
            [
                cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", *san,
                "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                str(profile_cpp), str(driver), *[str(o) for o in objs], "-o", str(exe),
            ],
            check=True, capture_output=True, text=True, timeout=90,
        )
        return exe

    def test_native_profile_webcrypto_and_identity(self):
        self.assertTrue((MONO_SRC / "optional/monocypher-ed25519.c").is_file(),
                        f"monocypher-ed25519.c missing under {MONO_SRC}")
        with tempfile.TemporaryDirectory(prefix="ce-online-profile-") as td:
            tmp = Path(td)
            exe = self._compile_native(tmp, PORT / "network/online_profile.cpp", asan=True)
            work = tmp / "work"
            work.mkdir(mode=0o700)
            res = subprocess.run([str(exe), str(work)], capture_output=True, text=True, timeout=30)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"ONLINE_PROFILE_TESTS checks=[1-9][0-9]* failures=0")
            vecs = parse_vectors(res.stdout)
            self.assertEqual(len(vecs), 2, res.stdout)

            # Verify Crockford base32 player_id and #tag independently in Python.
            for v in vecs:
                digest = hashlib.sha256(bytes.fromhex(v["pubkey"])).digest()
                self.assertEqual(v["player_id"], crockford_base32_lower(digest[:16]))
                self.assertEqual(len(v["player_id"]), 26)
                for forbidden in "ilou":
                    self.assertNotIn(forbidden, v["player_id"])
                self.assertEqual(v["tag"], digest[:2].hex().upper())

            # CRITICAL: Cross-verify C++ Ed25519 signatures against Node.js WebCrypto!
            node = shutil.which("node")
            self.assertIsNotNone(node, "node required for WebCrypto Ed25519 cross-verification")
            nv = subprocess.run(
                [node, "-e", NODE_VERIFY_JS, json.dumps(vecs)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(nv.returncode, 0, nv.stdout + nv.stderr)
            self.assertIn("WEBCRYPTO_ED25519_PASS 2", nv.stdout)

    def test_causal_mutant_permission_check_bypass_caught(self):
        src = (PORT / "network/online_profile.cpp").read_text()
        target = "(before.st_mode & 0077) != 0 ||"
        self.assertEqual(src.count(target), 1)
        mutated = src.replace(target, "false ||", 1)
        with tempfile.TemporaryDirectory(prefix="ce-online-profile-mut-") as td:
            tmp = Path(td)
            mut_cpp = tmp / "online_profile_mut.cpp"
            mut_cpp.write_text(mutated)
            exe = self._compile_native(tmp, mut_cpp, asan=False)
            work = tmp / "work"
            work.mkdir(mode=0o700)
            res = subprocess.run([str(exe), str(work)], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(res.returncode, 0, "0644 permission bypass mutant survived")
            self.assertIn("refuses 0644", res.stderr)

    def test_linux32_sniper_cross_compile_and_run(self):
        cc = LLVM_BIN / "clang"
        cxx = LLVM_BIN / "clang++"
        if not (cc.is_file() and cxx.is_file() and SYSROOT.is_dir()):
            self.skipTest("pinned Linux32 sniper toolchain unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-online-profile-l32-") as td:
            tmp = Path(td)
            flags = ["-m32", f"--sysroot={SYSROOT}"]
            objs = []
            for src, name in [
                (MONO_SRC / "monocypher.c", "mono.o"),
                (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
                (PORT / "updater/sha256.c", "sha256.o"),
            ]:
                obj = tmp / name
                subprocess.run(
                    [str(cc), *flags, "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)
            driver = tmp / "driver.cpp"
            driver.write_text(DRIVER_CPP)
            exe = tmp / "profile_l32"
            # Match production pcxx -mms-bitfields layout on i686 Linux!
            subprocess.run(
                [
                    str(cxx), *flags, "-std=c++17", "-O2", "-mms-bitfields",
                    "-Wall", "-Wextra", "-Werror",
                    "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                    str(PORT / "network/online_profile.cpp"), str(driver),
                    *[str(o) for o in objs],
                    f"--ld-path={MINGW_BIN / 'ld.lld'}", "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )
            work = tmp / "work"
            work.mkdir(mode=0o700)
            loader = SYSROOT / "lib/ld-linux.so.2"
            libs = f"{SYSROOT / 'lib/i386-linux-gnu'}:{SYSROOT / 'usr/lib/i386-linux-gnu'}"
            res = subprocess.run(
                [str(loader), "--library-path", libs, str(exe), str(work)],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"ONLINE_PROFILE_TESTS checks=[1-9][0-9]* failures=0")

    def test_windows32_cross_compile_and_wine_acl(self):
        cc = MINGW_BIN / "i686-w64-mingw32-clang"
        cxx = MINGW_BIN / "i686-w64-mingw32-clang++"
        if not (cc.is_file() and cxx.is_file()):
            self.skipTest("pinned llvm-mingw toolchain unavailable")
        with tempfile.TemporaryDirectory(prefix="ce-online-profile-w32-") as td:
            tmp = Path(td)
            objs = []
            for src, name in [
                (MONO_SRC / "monocypher.c", "mono.o"),
                (MONO_SRC / "optional/monocypher-ed25519.c", "ed25519.o"),
                (PORT / "updater/sha256.c", "sha256.o"),
            ]:
                obj = tmp / name
                subprocess.run(
                    [str(cc), "-std=gnu11", "-O2", "-I", str(MONO_SRC), "-c", str(src), "-o", str(obj)],
                    check=True, capture_output=True, text=True, timeout=60,
                )
                objs.append(obj)
            driver = tmp / "driver.cpp"
            driver.write_text(DRIVER_CPP)
            exe = tmp / "profile_w32.exe"
            subprocess.run(
                [
                    str(cxx), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(PORT / "network"), "-I", str(MONO_SRC),
                    str(PORT / "network/online_profile.cpp"), str(driver),
                    *[str(o) for o in objs],
                    "-lbcrypt", "-ladvapi32", "-static-libstdc++", "-static-libgcc",
                    "-o", str(exe),
                ],
                check=True, capture_output=True, text=True, timeout=90,
            )
            wine = shutil.which("wine")
            if not wine:
                return
            prefix = tmp / "wineprefix"
            drive_c = prefix / "drive_c/ce_test"
            drive_c.mkdir(parents=True, mode=0o700)
            env = {k: v for k, v in os.environ.items()
                   if k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
            env.update(
                WINEPREFIX=str(prefix),
                WINEDEBUG="-all",
                WINEDLLOVERRIDES="mscoree,mshtml,winemenubuilder.exe,winepulse.drv,winealsa.drv,wineoss.drv=",
            )
            res = subprocess.run(
                [wine, str(exe), r"C:\ce_test"],
                capture_output=True, text=True, env=env, timeout=120,
            )
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertRegex(res.stdout, r"ONLINE_PROFILE_TESTS checks=[1-9][0-9]* failures=0")
            vecs = parse_vectors(res.stdout)
            self.assertEqual(len(vecs), 2, res.stdout)
            node = shutil.which("node")
            if node:
                nv = subprocess.run(
                    [node, "-e", NODE_VERIFY_JS, json.dumps(vecs)],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(nv.returncode, 0, nv.stdout + nv.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
