#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh-process admission controls using actual Linux readers and tiny authored data, never retail/game inputs.

Opened hooks are an explicit fixture boundary; actual production entry/reader/console wiring and loaded-object
admission remain separate source-coupled gates. Windows coverage here is target syntax, not Windows execution.
"""
import hashlib
import json
import os
from pathlib import Path
import resource
import struct
import subprocess
import tempfile
import unittest

import deps
from network_compatibility_tests import FILES


def golden(files, directories=(), world_schema=11):
    word = lambda n: struct.pack(">I", n)
    text = lambda value: word(len(value.encode("ascii"))) + value.encode("ascii")
    blob = bytearray(b"Onslaught CE BLAKE2b-256 fresh process admission v2\0")
    blob += word(2) + text("fixture-simulation-v1") + word(852)
    blob += b"".join(word(n) for n in (1, world_schema, 5, 5, 1, 0, 0, 0))
    entries = dict(files)
    for name in files:
        parent = Path(name).parent
        while str(parent) not in (".", "data"):
            entries[str(parent)] = None
            parent = parent.parent
    entries.update((name, None) for name in directories)
    for name, data in sorted(entries.items()):
        blob += word(0 if data is None else 1) + text(name)
        if data is not None:
            blob += struct.pack(">Q", len(data)) + hashlib.blake2b(data, digest_size=32).digest()
    blob += word(len(files)) + struct.pack(">Q", sum(map(len, files.values())))
    return hashlib.blake2b(blob, digest_size=32).hexdigest()


def golden_lockstep(files, level):
    """The lockstep co-op contract's admission digest, from the wire format alone (its own domain; the level and protocol version in the context)."""
    word = lambda n: struct.pack(">I", n)
    text = lambda value: word(len(value.encode("ascii"))) + value.encode("ascii")
    blob = bytearray(b"Onslaught CE BLAKE2b-256 lockstep admission v1\0")
    blob += word(1) + text("fixture-simulation-v1") + word(level) + word(1)
    entries = dict(files)
    for name in files:
        parent = Path(name).parent
        while str(parent) not in (".", "data"):
            entries[str(parent)] = None
            parent = parent.parent
    for name, data in sorted(entries.items()):
        blob += word(0 if data is None else 1) + text(name)
        if data is not None:
            blob += struct.pack(">Q", len(data)) + hashlib.blake2b(data, digest_size=32).digest()
    blob += word(len(files)) + struct.pack(">Q", sum(map(len, files.values())))
    return hashlib.blake2b(blob, digest_size=32).hexdigest()


class Admission(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        owner = deps.WORK / "network-compatibility-admission-tests"
        owner.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=owner))
        cls.receipt = {"limits": __doc__, "compile": [], "cases": []}
        cls.addClassCleanup(lambda: (cls.out / "result.json").write_text(json.dumps(cls.receipt, indent=2) + "\n"))
        data, port = deps.DATA, deps.PORT
        mc = deps.SRC / "monocypher-4.0.2/src"
        crypto = cls.out / "monocypher.o"
        cls.crypto, cls.mc = crypto, mc
        commands = [[str(data / "llvm/bin/clang"), "-m32", "-O0", "-c", str(mc / "monocypher.c"), "-o", str(crypto)]]
        common = [str(data / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-O0",
                  "-pthread", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
                  "-fno-autolink", "-Wno-ignored-attributes", "-Wno-invalid-noreturn", "-Wno-microsoft-cast",
                  f"-I{port / 'platform/linux'}", f"-I{port / 'include/linux'}", f"-I{port / 'network'}",
                  f"-I{data / 'dxvk-native/usr/include/dxvk'}", f"-I{mc}"]
        sources = [str(port / "network/tests/compatibility_admission_driver.cpp"), str(port / "network/compatibility.cpp"), str(port / "network/native_posix_stat.cpp"),
                   str(port / "platform/linux/files.cpp"), str(port / "platform/linux/paths.cpp"), str(port / "platform/common/crtfile.cpp"), str(port / "tools/tests/mods_none.cpp"), str(crypto),
                   "-Wl,--wrap=port_fopen,--wrap=pread64,--wrap=fopen,--wrap=open,--wrap=CE_CompatibilityAuthoredOpenFailed"]
        cls.driver, cls.undefined = cls.out / "admission", cls.out / "undefined"
        commands += [[*common, '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"', *sources, "-o", str(cls.driver)],
                     [*common, *sources, "-o", str(cls.undefined)],
                     [str(data / "llvm-mingw/bin/i686-w64-mingw32-clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                      "-fsyntax-only", f"-I{mc}", str(port / "network/compatibility.cpp")]]
        for n, command in enumerate(commands):
            proc = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (cls.out / f"compile-{n}.log").write_text(proc.stdout + proc.stderr)
            cls.receipt["compile"].append({"command": command, "exit": proc.returncode})
            if proc.returncode:
                raise RuntimeError(proc.stdout + proc.stderr)
        cls.receipt["source_sha256"] = hashlib.sha256((port / "network/compatibility.cpp").read_bytes()).hexdigest()
        cls.receipt["header_sha256"] = hashlib.sha256((port / "network/compatibility.h").read_bytes()).hexdigest()
        cls.receipt["driver_sha256"] = hashlib.sha256((port / "network/tests/compatibility_admission_driver.cpp").read_bytes()).hexdigest()
        print(cls.out / "result.json", flush=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="authored-", dir=self.out)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game, self.user = self.root / "game", self.root / "user"
        self.user.mkdir()
        for name, data in FILES.items():
            target = self.game / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)

    def case(self, name, binary=None, extra_env=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "ADMISSION_", "COMPAT_TEST_"))}
        env.update(HOME=str(self.root), BEA_GAME_DIR=str(self.game), BEA_USER_DIR=str(self.user))
        env.update(extra_env or {})
        proc = subprocess.run([str(binary or self.driver), name], env=env, capture_output=True, text=True, timeout=15)
        self.receipt["cases"].append({"case": name, "exit": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr})
        (self.out / (name + ".log")).write_text(proc.stdout + proc.stderr)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertNotIn(str(self.root), proc.stdout + proc.stderr)
        fields = proc.stdout.split()
        self.assertEqual(fields[:2], ["PASS", name])
        self.assertGreater(int(fields[2]), 0)
        return fields

    def test_golden_context_is_independent_and_immutable(self):
        self.assertEqual(self.case("golden")[3:], [golden(FILES), str(len(FILES)), str(sum(map(len, FILES.values())))])
        self.assertNotEqual(golden(FILES), golden(FILES, world_schema=10))

    def lockstep_files(self, level, archive=True):
        """The fixture content plus the level's own mission archive (the level-852 one stays: the content is one data folder)."""
        files = dict(FILES)
        if archive:
            files["data/resources/%d_res_pc.aya" % level] = b"lockstep level %d archive sentinel" % level
            (self.game / ("data/resources/%d_res_pc.aya" % level)).write_bytes(files["data/resources/%d_res_pc.aya" % level])
        return files

    def test_lockstep_context_is_independent_immutable_and_per_level(self):
        digests = set()
        for level in (100, 856, 858):
            files = self.lockstep_files(level)
            env = {"COMPAT_TEST_LOCKSTEP": "1", "COMPAT_TEST_LEVEL": str(level)}
            fields = self.case("golden", extra_env=env)
            self.assertEqual(fields[3:], [golden_lockstep(files, level), str(len(files)), str(sum(map(len, files.values())))])
            digests.add(fields[3])
            (self.game / ("data/resources/%d_res_pc.aya" % level)).unlink()   # one archive at a time: the next level's content is this folder plus its own
            # the same content under the level-852 contract is another digest: the two admissions can never be mistaken for each other
            self.assertNotEqual(golden_lockstep(files, level), golden(files))
        self.assertEqual(3, len(digests))

    def test_lockstep_refuses_other_levels_and_a_missing_mission_archive(self):
        env = {"COMPAT_TEST_LOCKSTEP": "1"}
        for level in (852, 855, 857, 859, 0, 101):
            with self.subTest(level=level):
                self.lockstep_files(856)
                self.case("prepare-refuses", extra_env={**env, "COMPAT_TEST_LEVEL": str(level)})
        # an admitted level whose own archive is not in the content
        self.case("prepare-refuses", extra_env={**env, "COMPAT_TEST_LEVEL": "858"})

    def test_lockstep_boundary_holds_and_a_changed_input_refuses(self):
        self.lockstep_files(856)
        env = {"COMPAT_TEST_LOCKSTEP": "1", "COMPAT_TEST_LEVEL": "856"}
        self.case("golden", extra_env=env)
        self.case("boundary-change", extra_env=env)
        self.case("late-file", extra_env=env)

    def test_empty_autoexec_internal_hook_route(self):
        (self.user / "autoexec.con").write_bytes(b"")
        self.assertEqual(self.case("empty-script")[3], golden(FILES))

    def test_cross_root_case_order_and_caller_digest_do_not_set_context(self):
        for name in FILES:
            source = self.game / name
            # Existing fixture covers insensitive resolution independently of host enumeration order.
            source.rename(source.with_name(source.name.upper()))
        self.assertEqual(self.case("golden", extra_env={"BEA_NET_COMPAT": "0" * 64})[3], golden(FILES))
        self.assertEqual(self.case("golden", extra_env={"BEA_NET_COMPAT": "f" * 64})[3], golden(FILES))

    def test_user_shadow_and_unknown_extension_are_real_context_inputs(self):
        (self.user / "data").mkdir()
        (self.user / "data/default physics.dat").write_bytes(b"shadow physics")
        (self.game / "data/unknown.custom").write_bytes(b"authored dependency")
        effective = dict(FILES, **{"data/default physics.dat": b"shadow physics", "data/unknown.custom": b"authored dependency"})
        self.assertEqual(self.case("golden")[3], golden(effective))

    def test_missing_simulation_identity_refuses(self):
        self.case("undefined-id", self.undefined)

    def test_consequential_mutants_are_rejected_by_actual_consumers(self):
        text = (deps.PORT / "network/compatibility.cpp").read_text()
        base = self.receipt["compile"][1]["command"]
        mutants = (
            ("prior-history-ignored", "state.phase != FRESH || state.prior_history", "state.phase != FRESH",
             "prior-read", "unproven history or roots cannot prepare"),
            ("opened-digest-ignored", "actual != digest->second", "false",
             "native-bad-digest", "native complete digest required"),
            ("retirement-resurrected", "state.phase = RETIRED; state.prior_history = true;", "state.phase = FRESH; state.prior_history = false;",
             "retire", "retired context cannot be resurrected"),
            ("failed-open-ignored", "if (state.snapshot.tree.find(name) != state.snapshot.tree.end())", "if (false)",
             "disappeared-file", "real failed open immediately revokes context before any boundary rescan"),
        )
        self.receipt["mutations"] = []
        for label, old, new, case, assertion in mutants:
            with self.subTest(mutation=label):
                self.assertEqual(text.count(old), 1)
                source, binary = self.out / (label + ".cpp"), self.out / label
                source.write_text(text.replace(old, new))
                command = [str(source) if item == str(deps.PORT / "network/compatibility.cpp") else str(binary) if item == str(self.driver) else item for item in base]
                command.insert(1, "-I" + str(deps.PORT / "network"))
                compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "ADMISSION_", "COMPAT_TEST_"))}
                env.update(HOME=str(self.root), BEA_GAME_DIR=str(self.game), BEA_USER_DIR=str(self.user))
                run = subprocess.run([str(binary), case], env=env, capture_output=True, text=True, timeout=10)
                (self.out / (label + ".log")).write_text(run.stdout + run.stderr)
                row = {"label": label, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                       "compile_exit": compiled.returncode, "exit": run.returncode, "assertion": assertion,
                       "caught": run.returncode != 0 and assertion in run.stderr}
                self.receipt["mutations"].append(row)
                self.assertTrue(row["caught"], run.stdout + run.stderr)

    def test_actual_windows_native_object_reopen_preserves_caller(self):
        source = (deps.PORT / "network/compatibility.cpp").read_text()

        def block(signature, structure=False):
            start = source.index(signature)
            end = source.index("{", start) + 1
            depth = 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end] + (";" if structure else "") + "\n"

        actual = block("struct Stamp {", True)
        for signature in ("void HashInit(HASH_CTX *hash)", "void HashUpdate(HASH_CTX *hash, const void *data, size_t size)",
                          "void HashFinal(HASH_CTX *hash, uint8_t out[32])", "bool HandleStamp(HANDLE h, Stamp &out)",
                          "bool NativeStamp(intptr_t native, Stamp &stamp)",
                          "bool NativeDigest(intptr_t native, const Stamp &expected, Digest &digest)"):
            actual += block(signature)
        template = deps.PORT / "network/tests/compatibility_native_driver.cpp.in"
        generated, binary = self.out / "windows-native.cpp", self.out / "windows-native"
        generated.write_text(template.read_text().replace("@ACTUAL@", actual))
        command = [str(deps.DATA / "llvm/bin/clang++"), "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                   f"-I{self.mc}", f"-I{deps.PORT / 'network/tests'}", str(generated), str(self.crypto), "-o", str(binary)]
        proc = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (self.out / "windows-native-compile.log").write_text(proc.stdout + proc.stderr)
        self.receipt["compile"].append({"command": command, "exit": proc.returncode})
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        data = self.root / "native-authored.aya"
        data.write_bytes(b"AAAA")
        for case in ("same", "no-buffering", "reopen-fail", "reopen-wrong-id", "reopen-query-fail", "seek-fail",
                     "read-fail", "short-eof", "close-fail", "original-drift", "wrong-original", "wrong-type", "query-fail"):
            with self.subTest(case=case):
                proc = subprocess.run([str(binary), case, str(data)], capture_output=True, text=True, timeout=5)
                self.receipt["cases"].append({"case": "windows-native-" + case, "exit": proc.returncode,
                                              "stdout": proc.stdout, "stderr": proc.stderr})
                self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
                fields = proc.stdout.split()
                expected = case in ("same", "no-buffering")
                self.assertEqual(fields[:4], [case, str(int(expected)), str(int(expected)), "1"])
                self.assertEqual(fields[5], "0" if case in ("reopen-fail", "wrong-original", "wrong-type", "query-fail") else "1")


# One fresh process/namespace per stateful scenario; no reset API can erase the history under test.
for _case in (
    "rootguard", "rootguard-absent", "rootguard-relative", "rootguard-same",
    "prior-read", "prior-console", "prior-variable", "prior-script", "prior-runtime", "prior-unsupported", "uninitialized",
    "file-offset", "native-offset", "native-short", "native-bad-digest", "wrong-handle",
    "write", "traversal", "absolute", "unknown-reader", "repeated-dot", "embedded-dot", "wrong-contract", "old-world-contract", "wrong-level", "missing-probe", "late-file", "changed-file",
    "boundary-change", "boundary-shadow", "boundary-script", "console", "variable", "script", "runtime", "unsupported",
    "empty-command", "changed-roots", "retire", "retire-offline", "concurrent",
    "absent-file", "absent-native", "disappeared-file", "disappeared-native", "unreadable-file", "unreadable-native",
    "raw-overlay-miss",
):
    def _method(self, scenario=_case):
        self.case(scenario)
    setattr(Admission, "test_" + _case.replace("-", "_"), _method)

if __name__ == "__main__":
    unittest.main()
