#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Content compatibility fixtures using the real Linux overlay and synthetic sentinel data only.

No game/VM or download. Uses already installed pinned compilers and DXVK headers, as files_tests.py does.
Windows coverage here is compile-only, never presented as Windows runtime or cross-platform multiplayer acceptance.
"""
import hashlib
import os
from pathlib import Path
import resource
import shutil
import struct
import subprocess
import tempfile
import unittest

import deps

FILES = {
    "data/resources/base_res_pc.aya": b"base archive sentinel",
    "data/resources/852_res_pc.aya": b"level archive sentinel",
    "data/default physics.dat": b"physics sentinel",
    "data/battle engine configurations.dat": b"configuration sentinel",
    "data/worldheaders.dat": b"header sentinel",
}


def transcript(files, players=((0, "Standard"), (0, "Standard")), directories=()):
    """Independent wire-format oracle: file digest framing, no host filenames or metadata."""
    blob = bytearray(b"Onslaught CE BLAKE2b-256 effective content preflight v1\0")
    word = lambda n: struct.pack(">I", n)
    text = lambda s: word(len(s.encode("ascii"))) + s.encode("ascii")
    blob += word(1) + text("fixture-simulation-v1") + word(852)
    blob += b"".join(word(n) for n in (1, 9, 5, 4))
    for index, name in players:
        blob += word(index) + text(name)
    entries = dict(files)
    for name in files:
        parent = Path(name).parent
        while str(parent) not in (".", "data"):
            entries[str(parent)] = None
            parent = parent.parent
    entries.update((name, None) for name in directories)
    for name, payload in sorted(entries.items()):
        blob += word(0 if payload is None else 1) + text(name)
        if payload is not None:
            blob += struct.pack(">Q", len(payload)) + hashlib.blake2b(payload, digest_size=32).digest()
    blob += word(len(files)) + struct.pack(">Q", sum(map(len, files.values())))
    return hashlib.blake2b(blob, digest_size=32).hexdigest()


class Compatibility(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        scratch = deps.WORK / "tests"
        scratch.mkdir(parents=True, exist_ok=True)
        cls.tmp_build = tempfile.TemporaryDirectory(prefix="compatibility-build-", dir=scratch)
        cls.addClassCleanup(cls.tmp_build.cleanup)
        build = Path(cls.tmp_build.name)
        compiler = deps.DATA / "llvm/bin/clang++"
        platform = deps.PORT / "platform/linux"
        cls.crypto = build / "monocypher.o"
        cls.mc = deps.SRC / "monocypher-4.0.2/src"
        subprocess.run([str(deps.DATA / "llvm/bin/clang"), "-m32", "-O0", "-c",
                        str(cls.mc / "monocypher.c"), "-o", str(cls.crypto)], check=True, capture_output=True, text=True)
        common = [str(compiler), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-O0",
                  "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
                  "-fno-autolink", "-Wno-ignored-attributes", "-Wno-invalid-noreturn", "-Wno-microsoft-cast",
                  f"-I{platform}", f"-I{deps.PORT / 'include/linux'}", f"-I{deps.PORT / 'network'}",
                  f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", f"-I{cls.mc}"]
        sources = [str(deps.PORT / "network/tests/compatibility_driver.cpp"), str(deps.PORT / "network/compatibility.cpp"), str(deps.PORT / "network/native_posix_stat.cpp"),
                   str(platform / "files.cpp"), str(platform / "paths.cpp"), str(deps.PORT / "tools/tests/mods_none.cpp"), str(cls.crypto), "-Wl,--wrap=fread,--wrap=port_fopen"]
        cls.driver = build / "compatibility"
        subprocess.run([*common, '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"', *sources, "-o", str(cls.driver)],
                       check=True, capture_output=True, text=True)
        cls.unconfigured = build / "unconfigured"
        subprocess.run([*common, *sources, "-o", str(cls.unconfigured)], check=True, capture_output=True, text=True)
        cls.build = build

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="compatibility-case-", dir=deps.WORK / "tests")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game, self.user = self.root / "game", self.root / "user"
        self.user.mkdir()
        for name, data in FILES.items():
            self.write(self.game / name, data)

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def run_driver(self, *, expected=0, binary=None, args=None, env=None):
        e = dict(os.environ, HOME=str(self.root), BEA_GAME_DIR=str(self.game), BEA_USER_DIR=str(self.user))
        for key in ("COMPAT_TEST_SHORT_READ", "COMPAT_TEST_MUTATE", "COMPAT_TEST_ALTERNATIVE_GAME",
                    "COMPAT_TEST_ALTERNATIVE_USER", "COMPAT_TEST_UNINITIALIZED", "COMPAT_TEST_CACHE_QUERY"):
            e.pop(key, None)
        if env:
            for key, value in env.items():
                if value is None:
                    e.pop(key, None)
                else:
                    e[key] = str(value)
        r = subprocess.run([str(binary or self.driver), *(args or ["1", "852", "0", "Standard", "0", "Standard"])],
                           env=e, capture_output=True, text=True, timeout=15)
        self.assertEqual(r.returncode, expected, r.stdout + r.stderr)
        if expected == 1:
            self.assertTrue(r.stdout.startswith("REFUSED "), r.stdout)
            self.assertNotIn(str(self.root), r.stdout)
        return r.stdout.strip()

    def test_complete_digest_has_independent_known_transcript(self):
        self.assertEqual(transcript(FILES), "aa87eefef6ef88c8753d8b4a4b112f33dad1ca5579e74f3f1282a262c9f30168")
        self.assertEqual(self.run_driver(), f"{transcript(FILES)} {len(FILES)} {sum(map(len, FILES.values()))}")

    def test_prospective_data_read_fence(self):
        r = subprocess.run([str(self.driver), "--path-selftests"], capture_output=True, text=True, timeout=15)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(r.stdout.strip(), "PASS path admission cases")

    def test_host_paths_case_and_creation_order_do_not_change_digest(self):
        first = self.run_driver()
        other = self.root / "elsewhere" / "game"
        for name, data in reversed(list(FILES.items())):
            self.write(other / name.upper(), data)
        self.game = other
        self.assertEqual(self.run_driver(), first)

    def test_partial_user_directory_is_merged_and_shadowed_game_bytes_do_not_matter(self):
        name = "data/resources/852_res_pc.aya"
        self.write(self.user / name.upper(), b"user replacement")
        effective = dict(FILES, **{name: b"user replacement"})
        first = self.run_driver()
        self.assertEqual(first.split()[0], transcript(effective))
        self.write(self.game / name, b"irrelevant shadowed original")
        self.assertEqual(self.run_driver(), first)

    def test_unknown_extension_and_detached_mesh_are_real_inputs(self):
        first = self.run_driver()
        extra = {"data/resources/meshes/m_custom.aya": b"mesh and animation", "data/odd/custom.unrecognised": b"unknown"}
        for name, data in extra.items():
            self.write(self.game / name, data)
        result = self.run_driver()
        self.assertNotEqual(first, result)
        self.assertEqual(result.split()[0], transcript(dict(FILES, **extra)))

    def test_ordered_presets_change_digest(self):
        first = self.run_driver(args=["1", "852", "0", "Standard", "1", "Other"])
        second = self.run_driver(args=["1", "852", "1", "Other", "0", "Standard"])
        self.assertNotEqual(first, second)
        self.assertEqual(first.split()[0], transcript(FILES, ((0, "Standard"), (1, "Other"))))

    def test_root_saves_options_logs_and_platform_binaries_are_not_content(self):
        first = self.run_driver()
        for base in (self.game, self.user):
            for name in ("savegames/Private.bes", "defaultoptions.bea", "onslaught-ce.log", "ce.ini", "bea", "bea.exe"):
                self.write(base / name, b"private sentinel")
        self.assertEqual(first, self.run_driver())
        for base in (self.game, self.user):
            self.assertEqual((base / "savegames/Private.bes").read_bytes(), b"private sentinel")

    def test_private_content_inside_data_refuses_instead_of_being_omitted(self):
        for name in ("data/savegames/Private.bes", "data/private.log", "data/ce.ini", "data/.env"):
            with self.subTest(name=name):
                path = self.game / name
                self.write(path, b"private")
                self.run_driver(expected=1)
                path.unlink()
                if path.parent.name == "savegames":
                    path.parent.rmdir()

    def test_empty_and_absent_autoexec_are_equivalent_but_nonempty_refuses(self):
        first = self.run_driver()
        self.write(self.game / "AUTOEXEC.CON", b"")
        self.assertEqual(first, self.run_driver())
        self.write(self.game / "AUTOEXEC.CON", b"exec other.con\n")
        self.run_driver(expected=1)
        self.write(self.user / "autoexec.con", b"")
        self.assertEqual(first, self.run_driver())

    def test_case_aliases_refuse(self):
        self.write(self.game / "data/WorldHeaders.dat", b"conflicting alias")
        self.run_driver(expected=1)

    def test_data_root_aliases_refuse(self):
        (self.game / "DATA").mkdir()
        self.run_driver(expected=1)

    def test_empty_authored_directory_is_bound(self):
        first = self.run_driver()
        (self.game / "data/empty").mkdir()
        second = self.run_driver()
        self.assertNotEqual(first, second)
        self.assertEqual(second.split()[0], transcript(FILES, directories=("data/empty",)))

    def test_nonportable_names_refuse(self):
        for name in ("CON", "lpt1.aya", "tail.", "tail ", "name:stream", "nonascii-\u00e9.aya"):
            with self.subTest(name=name):
                path = self.game / "data" / name
                self.write(path, b"not portable")
                self.run_driver(expected=1)
                path.unlink()

    def test_content_links_refuse(self):
        for directory in (False, True):
            with self.subTest(directory=directory):
                target = self.root / ("external-dir" if directory else "external-file")
                if directory:
                    target.mkdir()
                else:
                    target.write_bytes(b"external")
                path = self.game / "data/linked"
                path.symlink_to(target, target_is_directory=directory)
                self.run_driver(expected=1)
                path.unlink()

    def test_unreadable_directory_refuses_instead_of_silently_disappearing(self):
        path = self.game / "data/unreadable"
        self.write(path / "child", b"must not disappear")
        path.chmod(0)
        try:
            self.run_driver(expected=1)
        finally:
            path.chmod(0o700)

    def test_special_files_refuse_without_opening(self):
        os.mkfifo(self.game / "data/pipe")
        self.run_driver(expected=1)

    def test_user_file_directory_conflicts_refuse(self):
        self.write(self.user / "data/resources", b"not a directory")
        self.run_driver(expected=1)

    def test_missing_required_file_refuses(self):
        (self.game / "data/default physics.dat").unlink()
        self.run_driver(expected=1)

    def test_wrong_schema_level_or_preset_refuses(self):
        for args in (["2", "852", "0", "Standard", "0", "Standard"],
                     ["1", "100", "0", "Standard", "0", "Standard"],
                     ["1", "852", "20", "Standard", "0", "Standard"],
                     ["1", "852", "0", "", "0", "Standard"],
                     ["1", "852", "0", "a" * 96, "0", "Standard"]):
            with self.subTest(args=args):
                self.run_driver(expected=1, args=args)

    def test_unconfigured_simulation_refuses(self):
        self.run_driver(expected=1, binary=self.unconfigured)

    def test_missing_or_relative_launcher_root_refuses(self):
        for env in ({"BEA_GAME_DIR": None}, {"BEA_USER_DIR": None}, {"BEA_GAME_DIR": "."}, {"BEA_USER_DIR": self.game}):
            with self.subTest(env=env):
                self.run_driver(expected=1, env=env)

    def test_deep_and_long_content_paths_refuse(self):
        path = self.game / "data"
        for _ in range(34):
            path /= "d"
        path.mkdir(parents=True)
        self.run_driver(expected=1)
        shutil.rmtree(self.game / "data/d")
        self.write(self.game / "data" / ("a" * 238), b"over logical path bound")
        self.run_driver(expected=1)

    def test_actual_game_reader_must_open_the_inventoried_identity(self):
        alternative = self.root / "alternative"
        shutil.copytree(self.game, alternative)
        self.run_driver(expected=1, env={"COMPAT_TEST_ALTERNATIVE_GAME": alternative})

    def test_uninitialized_cached_roots_refuse_without_initializing_or_opening(self):
        result = self.run_driver(expected=1, env={"COMPAT_TEST_UNINITIALIZED": "1"})
        self.assertIn("reader opens=0", result)
        self.assertIn("uninitialized or changed", result)

    def test_cached_user_change_cannot_hide_an_old_only_script_or_data(self):
        old = self.root / "old-user"
        self.write(old / "autoexec.con", b"exec hidden-script.con\n")
        self.write(old / "data/cache-only.aya", b"old-only authored input")
        result = self.run_driver(expected=1, env={"BEA_USER_DIR": old, "COMPAT_TEST_ALTERNATIVE_USER": self.user})
        self.assertIn("reader opens=0", result)
        self.assertIn("uninitialized or changed", result)
        self.assertEqual((old / "autoexec.con").read_bytes(), b"exec hidden-script.con\n")
        # The actual old root, without an environment/cache mismatch, independently refuses its nonempty script.
        self.run_driver(expected=1, env={"BEA_USER_DIR": old})

    def test_cached_root_query_returns_exact_pair_and_refuses_small_buffers(self):
        self.assertEqual(self.run_driver(env={"COMPAT_TEST_CACHE_QUERY": "1"}), self.run_driver())

    def test_short_read_without_eof_refuses(self):
        self.run_driver(expected=1, env={"COMPAT_TEST_SHORT_READ": "1"})

    def test_in_place_content_change_during_read_refuses(self):
        self.run_driver(expected=1, env={"COMPAT_TEST_MUTATE": self.game / "data/battle engine configurations.dat"})

    def test_windows_translation_unit_compiles(self):
        compiler = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
        if not compiler.is_file():
            self.skipTest("pinned Windows compiler unavailable; no Windows compile acceptance")
        subprocess.run([str(compiler), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"', f"-I{self.mc}", "-c",
                        str(deps.PORT / "network/compatibility.cpp"), "-o", str(self.build / "windows-compatibility.o")],
                       check=True, capture_output=True, text=True)

    def test_windows_cached_root_backend_compiles(self):
        compiler = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"
        if not compiler.is_file():
            self.skipTest("pinned Windows compiler unavailable; no Windows compile acceptance")
        subprocess.run([str(compiler), "-std=c++17", "-fsyntax-only", str(deps.PORT / "platform/windows/files.cpp")],
                       check=True, capture_output=True, text=True)

    def test_linux_translation_unit_compiles_strictly(self):
        compiler = deps.DATA / "llvm/bin/clang++"
        subprocess.run([str(compiler), "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"', f"-I{self.mc}", "-c",
                        str(deps.PORT / "network/compatibility.cpp"), "-o", str(self.build / "linux-compatibility.o")],
                       check=True, capture_output=True, text=True)

    def test_windows_full_identity_at_the_actual_read_consumer(self):
        source = (deps.PORT / "network/compatibility.cpp").read_text()

        def block(signature, structure=False):
            start = source.index(signature)
            end = source.index("{", start) + 1
            depth = 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end] + (";" if structure else "") + "\n"

        actual = "".join(block(s, True) for s in ("struct Stamp {", "struct Entry {"))
        actual += "thread_local unsigned internal_reads;\n" + block("struct InternalRead {", True)
        actual += "using HASH_CTX = crypto_blake2b_ctx;\n"
        for signature in ("void HashInit(HASH_CTX *hash)", "void HashUpdate(HASH_CTX *hash, const void *data, size_t size)",
                          "void HashFinal(HASH_CTX *hash, uint8_t out[32])", "bool Refuse(char *error, size_t size, const char *message)",
                          "bool HandleStamp(HANDLE h, Stamp &out)", "bool PathStamp(const fs::path &path, Stamp &out)",
                          "bool FileStamp(FILE *file, Stamp &out)",
                          "bool Read(const std::string &logical, const Entry &entry, uint8_t digest[32], char *error, size_t size)"):
            actual += block(signature)
        template = (deps.PORT / "network/tests/compatibility_identity_driver.cpp.in").read_text()
        generated = self.build / "identity-driver.cpp"
        generated.write_text(template.replace("@ACTUAL@", actual))
        binary = self.build / "identity-driver"
        subprocess.run([str(deps.DATA / "llvm/bin/clang++"), "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        f"-I{self.mc}", f"-I{deps.PORT / 'network/tests'}", str(generated), str(self.crypto), "-o", str(binary)],
                       check=True, capture_output=True, text=True)
        a, b = self.root / "selected.aya", self.root / "other.aya"
        a.write_bytes(b"AAAA")
        b.write_bytes(b"BBBB")
        cases = {"same-file": True, "same-full-legacy-different": True, "id128-collision": False,
                 "volume64-collision": False, "unknown-legacy": False, "unknown-full-zero": False,
                 "unknown-full-ones": False, "unknown-volume-zero": False, "unknown-volume-ones": False,
                 "full-query-failure": False, "legacy-metadata-failure": False, "close-failure": False, "wrong-type": False}
        for case, expected in cases.items():
            with self.subTest(case=case):
                r = subprocess.run([str(binary), case, str(a), str(b)], capture_output=True, text=True, timeout=5)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                name, admitted, full_queries, selected = r.stdout.split()
                self.assertEqual(name, case)
                self.assertEqual(int(admitted), expected, r.stdout)
                if expected:
                    self.assertGreater(int(full_queries), 0)
                    self.assertEqual(int(selected), 1)

    def test_windows_roots_refuse_before_legacy_init_can_truncate_or_create(self):
        source = (deps.PORT / "network/compatibility.cpp").read_text()
        backend = (deps.PORT / "platform/windows/files.cpp").read_text()

        def block(text, signature):
            start = text.index(signature)
            end = text.index("{", start) + 1
            depth = 1
            while depth:
                depth += (text[end] == "{") - (text[end] == "}")
                end += 1
            return text[start:end] + "\n"

        # Couple the real root predicate to the real platform initialization and check its consumer order too.
        compute = source[source.index('extern "C" int CE_CompatibilityCompute'):]
        self.assertLess(compute.index("WindowsLauncherRoot(game)"), compute.index("Inventory(game_root"))
        self.assertLess(compute.index("Inventory(game_root"), compute.index('port_fopen("autoexec.con"'))
        self.assertIn("const fs::path game_root = fs::u8path(game), user_root = fs::u8path(user)", compute)
        template = (deps.PORT / "network/tests/compatibility_roots_driver.cpp.in").read_text()
        code = template.replace("@GUARD@", block(source, "bool WindowsLauncherRoot(const char *root)"))
        code = code.replace("@LEGACY@", block(backend, "void MakeDirs(const char *path)") + block(backend, "void Names()") + block(backend, "void Init()"))
        code = code.replace("@QUERY@", block(backend, "int PortCachedContentRoots(char *game, size_t game_size, char *user, size_t user_size)"))
        generated, binary = self.build / "roots-driver.cpp", self.build / "roots-driver"
        generated.write_text(code)
        subprocess.run([str(deps.DATA / "llvm/bin/clang++"), "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        f"-I{deps.PORT / 'network/tests'}", str(generated), "-o", str(binary)],
                       check=True, capture_output=True, text=True)
        cases = {"ascii-short": True, "utf8-short": True, "ascii-259": True, "ascii-260": False,
                 "utf8-wide": False, "game-wide": False, "invalid-utf8": False, "wrong-code-page": False,
                 "dot-parent": False, "dot-component": False, "repeat-separator": False, "unc": False, "relative": False}
        for case, expected in cases.items():
            with self.subTest(case=case):
                r = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=5)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                name, admitted, initialized, created, size, exact, cached, small = r.stdout.split()
                self.assertEqual(name, case)
                self.assertEqual(int(admitted), expected, r.stdout)
                self.assertEqual(int(initialized), expected, r.stdout)
                self.assertEqual(int(created), 0, r.stdout)
                self.assertEqual(int(exact), expected, r.stdout)
                self.assertEqual(int(cached), expected, r.stdout)
                self.assertEqual(int(small), 1, r.stdout)
                if case == "ascii-259": self.assertEqual(int(size), 259)
                if case == "ascii-260": self.assertEqual(int(size), 260)
                if case == "utf8-wide": self.assertEqual(int(size), 273)


if __name__ == "__main__":
    unittest.main()
