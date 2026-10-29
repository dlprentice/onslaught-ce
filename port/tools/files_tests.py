#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux file-overlay regressions using the real platform sources, without the game or retail data.

    python3 port/tools/files_tests.py [-v]

Needs the pinned LLVM compiler and dxvk-native headers from deps.py, plus host 32-bit C/C++ development libraries.
The driver stubs only handle allocation/checking and last-error storage. Fixtures are short sentinel files, not
careers; all reads, writes, enumeration and path resolution use files.cpp and paths.cpp. Nothing launches the game.
"""
import hashlib
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest

import deps


DRIVER = r'''#include "handles.h"
#include <io.h>
#include <stdlib.h>
#include <string.h>

extern "C" FILE *port_fopen(const char *, const char *);
extern "C" int port_access(const char *, int);
extern "C" LONG WINAPI PortRegCreateKeyExA(HKEY,LPCSTR,DWORD,LPSTR,DWORD,REGSAM,LPSECURITY_ATTRIBUTES,PHKEY,LPDWORD);
extern "C" LONG WINAPI PortRegSetValueExA(HKEY,LPCSTR,DWORD,DWORD,const BYTE *,DWORD);
extern "C" LONG WINAPI PortRegCloseKey(HKEY);
PortHandle *PortHandleNew(PortHandleKind kind, size_t size)
{
    PortHandle *h = (PortHandle *)calloc(1, size);
    h->kind = kind;
    return h;
}
void PortHandleFree(PortHandle *h) { free(h); }
PortHandle *PortCheck(HANDLE h, int kind)
{
    PortHandle *p = (PortHandle *)h;
    return p && p->kind == kind ? p : NULL;
}
void PortSetLastError(DWORD) {}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    PortPathsInit();
    if (!strcmp(argv[1], "registry"))
    {
        HKEY key;
        PortRegCreateKeyExA(HKEY_CURRENT_USER, "fixture", 0, NULL, 0, 0, NULL, &key, NULL);
        BYTE value = 42;
        LONG result = PortRegSetValueExA(key, "value", 0, 3, &value, 1);
        PortRegCloseKey(key);
        return result == ERROR_SUCCESS ? 0 : 1;
    }
    if (!strcmp(argv[1], "crt"))
    {
        _finddata_t d;
        long h = _findfirst(argv[2], &d);
        if (h == -1) return 0;
        do { printf("%s\t%u\t%lu\n", d.name, d.attrib, d.size); } while (_findnext(h, &d) == 0);
        return _findclose(h);
    }
    if (!strcmp(argv[1], "win32"))
    {
        WIN32_FIND_DATAA d;
        HANDLE h = FindFirstFileA(argv[2], &d);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do { printf("%s\t%lu\t%lu\n", d.cFileName, d.dwFileAttributes, d.nFileSizeLow); }
        while (FindNextFileA(h, &d));
        return FindClose(h) ? 0 : 1;
    }
    if (!strcmp(argv[1], "delete")) return DeleteFileA(argv[2]) ? 0 : 1;
    if (!strcmp(argv[1], "access")) return port_access(argv[2], 0) == 0 ? 0 : 1;
    if (!strcmp(argv[1], "attributes")) return GetFileAttributesA(argv[2]) == INVALID_FILE_ATTRIBUTES ? 1 : 0;
    if (!strcmp(argv[1], "mkdir")) return CreateDirectoryA(argv[2], NULL) ? 0 : 1;
    if (!strcmp(argv[1], "win-read"))
    {
        HANDLE h = CreateFileA(argv[2], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return 1;
        char b[4096];
        DWORD n;
        while (ReadFile(h, b, sizeof b, &n, NULL) && n) fwrite(b, 1, n, stdout);
        PortFileClose((PortHandle *)h);
        PortHandleFree((PortHandle *)h);
        return 0;
    }
    if (!strcmp(argv[1], "win-open"))
    {
        if (argc != 5) return 2;
        HANDLE h = CreateFileA(argv[2], strtoul(argv[4], NULL, 0), 0, NULL, atoi(argv[3]), 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return 1;
        PortFileClose((PortHandle *)h);
        PortHandleFree((PortHandle *)h);
        return 0;
    }
    bool append = !strcmp(argv[1], "append"), update = !strcmp(argv[1], "update");
    bool write = !strcmp(argv[1], "write") || append || update;
    if (write && argc != 4) return 2;
    FILE *f = port_fopen(argv[2], append ? "ab" : update ? "r+b" : write ? "wb" :
                        !strcmp(argv[1], "read-ra") ? "ra" : "rb");
    if (!f) return 1;
    if (write)
    {
        size_t n = strlen(argv[3]);
        bool ok = fwrite(argv[3], 1, n, f) == n;
        return fclose(f) == 0 && ok ? 0 : 1;
    }
    char b[256];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) != 0) fwrite(b, 1, n, stdout);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok ? 0 : 1;
}
'''

# Linker wrappers exist only in this test executable. No fault-control variables enter production code.
FAULTS = r'''#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_openat(int,const char*,int,...);
extern "C" int __real_linkat(int,const char*,int,const char*,int);
extern "C" int __real_unlinkat(int,const char*,int);
static bool Fault(const char *name) { const char *f=getenv("FILES_TEST_FAULT"); return f && !strcmp(f,name); }
extern "C" ssize_t __wrap_read(int fd,void *p,size_t n)
{
    static int calls;
    ++calls;
    if (Fault("read-failure") && calls>1) { errno=EIO; return -1; }
    if (Fault("read-interrupt") && calls==1) { errno=EINTR; return -1; }
    return __real_read(fd,p,Fault("read-failure") ? 8 : n);
}
extern "C" ssize_t __wrap_write(int fd,const void *p,size_t n)
{
    static int calls;
    ++calls;
    if (Fault("write-failure") && calls>1) { errno=ENOSPC; return -1; }
    if (Fault("write-interrupt") && calls==1) { errno=EINTR; return -1; }
    if ((Fault("write-failure") || Fault("short-write")) && n>1) n/=2;
    return __real_write(fd,p,n);
}
extern "C" int __wrap_fsync(int fd)
{
    if (Fault("sync-failure")) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
extern "C" int __wrap_close(int fd)
{
    char name[64],path[4096]={0};
    snprintf(name,sizeof(name),"/proc/self/fd/%d",fd);
    readlink(name,path,sizeof(path)-1);
    int result=__real_close(fd);
    if (Fault("close-failure") && strstr(path,"/game/") && strstr(path,".bes")) { errno=EIO; return -1; }
    return result;
}
extern "C" int __wrap_openat(int parent,const char *name,int flags,...)
{
    mode_t mode=0;
    if (flags&O_CREAT) { va_list args; va_start(args,flags); mode=va_arg(args,int); va_end(args); }
    int fd=__real_openat(parent,name,flags,mode);
    const char *redirect=getenv("FILES_TEST_REDIRECT");
    if (fd>=0 && redirect && !strcmp(name,"SAVEGAMES") && (flags&O_DIRECTORY))
    {
        if (renameat(parent,name,parent,"held-savegames")==0) symlinkat(redirect,parent,name);
    }
    return fd;
}
extern "C" int __wrap_linkat(int from,const char *source,int parent,const char *name,int flags)
{
    if (Fault("publish-race"))
    {
        int fd=__real_openat(parent,name,O_WRONLY|O_CREAT|O_EXCL,0600);
        if (fd>=0) { const char text[]="concurrent user bytes"; __real_write(fd,text,sizeof(text)-1); __real_close(fd); }
    }
    return __real_linkat(from,source,parent,name,flags);
}
extern "C" int __wrap_unlinkat(int parent,const char *name,int flags)
{
    if (Fault("cleanup-failure") && !strncmp(name,".ce-copy-",9)) { errno=EACCES; return -1; }
    return __real_unlinkat(parent,name,flags);
}
'''


class FileOverlay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        scratch = deps.WORK / "tests"
        scratch.mkdir(parents=True, exist_ok=True)
        cls.build = tempfile.TemporaryDirectory(prefix="files-build-", dir=scratch)
        cls.addClassCleanup(cls.build.cleanup)
        build = Path(cls.build.name)
        source = build / "driver.cpp"
        source.write_text(DRIVER)
        faults = build / "faults.cpp"
        faults.write_text(FAULTS)
        cls.driver = build / "files-driver"
        sha256 = build / "sha256.o"
        subprocess.run([str(deps.DATA / "llvm/bin/clang"), "--target=i686-pc-linux-gnu", "-m32", "-O1", "-c",
                        str(deps.PORT / "updater/sha256.c"), "-o", str(sha256)], check=True, capture_output=True)
        platform = deps.PORT / "platform/linux"
        cmd = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
               "-fno-autolink", "-w", "-O0", f"-I{platform}", f"-I{deps.PORT / 'include/linux'}",
               f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", str(source), str(platform / "files.cpp"),
               str(platform / "paths.cpp"), str(deps.PORT / "platform/common/registry.cpp"), str(faults),
               # The mod layer the read paths consult (platform/common/mods.h), with its ce.ini reader and SHA-256.
               str(deps.PORT / "platform/common/mods.cpp"), str(deps.PORT / "platform/common/ce.cpp"),
               str(sha256), "-lz",
               "-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=openat,--wrap=linkat,--wrap=unlinkat",
               "-o", str(cls.driver)]
        subprocess.run(cmd, check=True, capture_output=True, text=True)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="files-case-", dir=deps.WORK / "tests")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / "game"
        self.user = self.root / "user"
        self.saves = self.game / "SaVeGaMeS"
        self.saves.mkdir(parents=True)
        # Deliberately unlike name order. Upper-case suffixes and directory spelling exercise Windows path matching.
        for name in ("zeta.bes", "Alpha.bes", "_tail.bes", "beta.BES"):
            (self.saves / name).write_bytes(f"original {name}".encode())
        self.original = self.snapshot()
        self.addCleanup(lambda: self.assertEqual(self.original, self.snapshot(), "original files changed"))

    def snapshot(self):
        return self.tree_snapshot(self.game)

    @staticmethod
    def tree_snapshot(root):
        return {str(p.relative_to(root)): "link:" + os.readlink(p) if p.is_symlink() else
                hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else "directory"
                for p in root.rglob("*")}

    def run_driver(self, action, path, *args, expected=0, fault=None, redirect=None, cwd=None, extra_env=None):
        env = dict(os.environ, BEA_GAME_DIR=str(self.game), BEA_USER_DIR=str(self.user), HOME=str(self.root))
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT",
                     "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"):
            env.pop(name, None)
        env.update(extra_env or {})
        env.pop("FILES_TEST_FAULT", None)
        env.pop("FILES_TEST_REDIRECT", None)
        if fault:
            env["FILES_TEST_FAULT"] = fault
        if redirect:
            env["FILES_TEST_REDIRECT"] = str(redirect)
        result = subprocess.run([str(self.driver), action, path, *args], env=env, capture_output=True, cwd=cwd)
        self.assertEqual(result.returncode, expected, result.stderr.decode(errors="replace"))
        return result.stdout

    def entries(self, api="crt", spec=r"savegames\*.bes"):
        return [(name, int(attr), int(size)) for name, attr, size in
                (line.split("\t") for line in self.run_driver(api, spec).decode().splitlines())]

    def assert_names(self, names, spec=r"savegames\*.bes"):
        for api in ("crt", "win32"):
            with self.subTest(api=api, spec=spec):
                self.assertEqual(names, [name for name, _, _ in self.entries(api, spec)])

    def user_file(self, name, data=b"user sentinel"):
        folder = self.user / "SAVEGAMES"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(data)

    def test_mod_copy_between_user_copy_and_game_file(self):
        (self.game / "data").mkdir()
        (self.game / "data/Thing.bin").write_bytes(b"original thing")
        self.original = self.snapshot()
        mod = self.user / "mods/m"
        (mod / "data").mkdir(parents=True)
        (mod / "data/Thing.bin").write_bytes(b"modded thing")
        (mod / "mod.json").write_text('{"schema": 1, "id": "m", "name": "M", "version": "1", "files": [{"path": '
                                      '"data/Thing.bin", "size": 12, "sha256": "%s"}]}'
                                      % hashlib.sha256(b"modded thing").hexdigest())
        (self.user / "ce.ini").write_text("mods=m\n")
        mod_before = self.tree_snapshot(mod)
        for action in ("read", "win-read"):
            with self.subTest(action=action):
                self.assertEqual(b"modded thing", self.run_driver(action, r"DATA\thing.BIN"))
                self.assertEqual(b"original thing", self.run_driver(action, r"data\Thing.bin",
                                                                    extra_env={"BEA_PORT_TAPE": "tape"}))
        # An update copies the game's file into the user folder, never the mod's copy; the user copy then wins.
        self.run_driver("update", r"data\Thing.bin", "O")
        self.assertEqual(b"Original thing", (self.user / "data/Thing.bin").read_bytes())
        self.assertEqual(b"Original thing", self.run_driver("read", r"data\Thing.bin"))
        self.assertEqual(b"Original thing", self.run_driver("win-read", r"data\Thing.bin"))
        self.assertEqual(mod_before, self.tree_snapshot(mod), "a mod folder changed")

    def test_original_list_without_user_save_folder(self):
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual([], list(self.user.iterdir()))  # searching creates no shadow save folder

    def test_original_list_with_empty_user_save_folder(self):
        (self.user / "savegames").mkdir(parents=True)
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual([], list((self.user / "savegames").iterdir()))

    def test_non_directory_user_save_folder_does_not_hide_original_entries(self):
        self.user.mkdir()
        (self.user / "savegames").write_bytes(b"not a directory")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual(b"original Alpha.bes", self.run_driver("read", r"savegames\Alpha.bes"))

    def test_disjoint_roots_merge_in_name_order(self):
        self.user_file("delta.bes")
        self.user_file("Aardvark.BES")
        expected = ["Aardvark.BES", "Alpha.bes", "beta.BES", "delta.bes", "zeta.bes", "_tail.bes"]
        self.assert_names(expected)
        self.assert_names(expected, "SAVEGAMES/*.BES")

    def test_bare_patterns_merge_roots_and_preserve_star_dot_star(self):
        (self.game / "AlphaRoot.bes").write_bytes(b"root sentinel")
        self.original = self.snapshot()
        self.user.mkdir()
        (self.user / "betaRoot.bes").write_bytes(b"user root sentinel")
        self.assert_names(["AlphaRoot.bes", "betaRoot.bes"], "*.bes")
        self.assert_names([".", "..", "AlphaRoot.bes", "betaRoot.bes", "SaVeGaMeS"], "*.*")

    def test_user_only_directory(self):
        (self.user / "other").mkdir(parents=True)
        (self.user / "other/solo.bes").write_bytes(b"user-only sentinel")
        self.assert_names(["solo.bes"], r"other\*.bes")

    def test_user_case_collision_shadows_original_even_with_bad_contents(self):
        self.user_file("aLPHa.BES", b"bad")
        self.assert_names(["aLPHa.BES", "beta.BES", "zeta.bes", "_tail.bes"])
        for api in ("crt", "win32"):
            self.assertEqual(3, self.entries(api)[0][2])
        self.assertEqual(b"bad", self.run_driver("read", r"savegames\Alpha.bes"))

    def test_same_spelling_is_listed_once(self):
        self.user_file("Alpha.bes")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual(b"user sentinel", self.run_driver("read", r"savegames\Alpha.bes"))

    def test_directory_shadow_keeps_its_type(self):
        (self.user / "savegames/ALPHA.bes").mkdir(parents=True)
        for api in ("crt", "win32"):
            entries = self.entries(api)
            self.assertEqual(["ALPHA.bes", "beta.BES", "zeta.bes", "_tail.bes"], [e[0] for e in entries])
            self.assertTrue(entries[0][1] & 0x10)  # retail skips directories; do not substitute the original file

    def test_absolute_search_reads_only_the_named_directory(self):
        self.user_file("delta.bes")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"], str(self.saves / "*.bes"))

    def test_save_writes_and_deletes_stay_in_user_folder(self):
        self.run_driver("delete", r"savegames\Alpha.bes", expected=1)
        self.assertEqual(b"original Alpha.bes", self.run_driver("read", r"savegames\Alpha.bes"))
        self.run_driver("write", r"savegames\Alpha.bes", "changed user sentinel")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual(b"changed user sentinel", self.run_driver("read", r"savegames\Alpha.bes"))
        self.run_driver("delete", r"savegames\Alpha.bes")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assertEqual(b"original Alpha.bes", self.run_driver("read", r"savegames\Alpha.bes"))

    def assert_refused(self, path):
        before = self.tree_snapshot(self.user)
        attempts = [("read",), ("write", "refused write"), ("append", "refused append"),
                    ("update", "refused update"), ("delete",), ("access",), ("attributes",),
                    ("win-open", "3", "0x80000000"),   # OPEN_EXISTING, GENERIC_READ
                    ("win-open", "2", "0x40000000"),   # CREATE_ALWAYS, GENERIC_WRITE
                    ("win-open", "3", "0xc0000000"),   # OPEN_EXISTING, read/write (copy on write)
                    ("win-open", "4", "0x40000000")]   # OPEN_ALWAYS, GENERIC_WRITE (copy on write)
        for action, *args in attempts:
            with self.subTest(path=path, action=action, args=args):
                self.run_driver(action, path, *args, expected=1)
                self.assertEqual(before, self.tree_snapshot(self.user), "refused access changed writable files")

    def check_user_case_collision(self, names):
        (self.saves / "Pair.bes").write_bytes(b"must not fall back to original")
        self.original = self.snapshot()
        for name in names:
            self.user_file(name, name.encode())
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        for spelling in ("Pair.BES", "Pair.bes", "PAIR.Bes"):
            self.assert_refused("savegames/" + spelling)
        self.assert_refused(r".\SAVEGAMES\..\savegames\Pair.bes")

    def test_user_case_collision_refuses_every_spelling_and_operation(self):
        self.check_user_case_collision(("Pair.BES", "Pair.bes"))

    def test_user_case_collision_is_independent_of_creation_order(self):
        self.check_user_case_collision(("Pair.bes", "Pair.BES"))

    def test_original_case_collision_refuses_new_shadow_and_copy_on_write(self):
        for name in ("Pair.BES", "Pair.bes"):
            (self.saves / name).write_bytes(name.encode())
        self.original = self.snapshot()
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assert_refused(r"savegames\Pair.BES")
        self.assert_refused(r"savegames\Pair.bes")
        self.assertEqual([], list(self.user.iterdir()))

    def test_unique_user_can_shadow_ambiguous_originals(self):
        for name in ("Pair.BES", "Pair.bes"):
            (self.saves / name).write_bytes(name.encode())
        self.original = self.snapshot()
        self.user_file("PAIR.Bes", b"unique user")
        self.assert_names(["Alpha.bes", "beta.BES", "PAIR.Bes", "zeta.bes", "_tail.bes"])
        self.assertEqual(b"unique user", self.run_driver("read", r"savegames\Pair.bes"))
        self.run_driver("update", r"savegames\Pair.bes", "updated")
        self.run_driver("delete", r"savegames\Pair.bes")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assert_refused(r"savegames\Pair.bes")

    def test_ambiguous_user_save_directory_blocks_original_fallback(self):
        self.user_file("Pair.bes")
        (self.user / "savegames").mkdir()
        (self.user / "savegames/Other.bes").write_bytes(b"other sentinel")
        self.assert_names([])
        self.assert_refused(r"savegames\Pair.bes")
        self.assert_refused(r"SAVEGAMES\Alpha.bes")
        self.run_driver("mkdir", "savegames", expected=1)

    def test_unique_user_directory_survives_ambiguous_original_directory(self):
        (self.game / "savegames").mkdir()
        (self.game / "savegames/Other.bes").write_bytes(b"other original")
        self.original = self.snapshot()
        self.assert_names([])
        self.assert_refused(r"savegames\Alpha.bes")
        self.user_file("Solo.bes", b"unique user")
        self.assert_names(["Solo.bes"])
        self.assertEqual(b"unique user", self.run_driver("read", r"savegames\Solo.bes"))
        self.assert_refused(r"savegames\New.bes")

    def test_directory_entry_counts_as_a_case_collision(self):
        self.user_file("Pair.bes")
        (self.user / "SAVEGAMES/Pair.BES").mkdir()
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assert_refused(r"savegames\Pair.bes")

    def test_dangling_symlink_counts_as_a_case_collision(self):
        self.user_file("Pair.bes")
        (self.user / "SAVEGAMES/Pair.BES").symlink_to("missing-target")
        self.assert_names(["Alpha.bes", "beta.BES", "zeta.bes", "_tail.bes"])
        self.assert_refused(r"savegames\Pair.bes")

    def test_unique_original_copy_on_write_still_works(self):
        self.run_driver("append", r"savegames\Alpha.BES", " added")
        self.assertEqual(b"original Alpha.bes added", self.run_driver("read", r"savegames\Alpha.bes"))
        self.run_driver("win-open", r"savegames\beta.bes", "3", "0xc0000000")
        self.assertEqual(b"original beta.BES", self.run_driver("read", r"savegames\beta.bes"))

    def test_unique_uppercase_suffix_keeps_one_identity(self):
        self.user_file("Pair.BES", b"original user")
        self.assertEqual(b"original user", self.run_driver("read", r"savegames\Pair.bes"))
        self.run_driver("write", r"savegames\Pair.bes", "new user")
        self.assertEqual(["Pair.BES"], [p.name for p in (self.user / "SAVEGAMES").iterdir()])
        self.assertEqual(b"new user", (self.user / "SAVEGAMES/Pair.BES").read_bytes())
        self.run_driver("delete", r"savegames\Pair.bes")
        self.assertEqual([], list((self.user / "SAVEGAMES").iterdir()))

    def test_collision_added_after_listing_is_refused_on_access(self):
        self.user_file("Pair.BES")
        self.assert_names(["Alpha.bes", "beta.BES", "Pair.BES", "zeta.bes", "_tail.bes"])
        self.user_file("Pair.bes", b"newly conflicting user")
        self.assert_refused(r"savegames\Pair.bes")

    def test_unreadable_user_directory_cannot_uncover_originals(self):
        self.user_file("Pair.bes")
        folder = self.user / "SAVEGAMES"
        before = self.tree_snapshot(self.user)
        folder.chmod(0)
        try:
            self.assert_names([])
            for action, args in (("read", []), ("write", ["refused"]), ("append", ["refused"]),
                                 ("update", ["refused"]), ("delete", [])):
                self.run_driver(action, r"savegames\Alpha.bes", *args, expected=1)
        finally:
            folder.chmod(0o755)
        self.assertEqual(before, self.tree_snapshot(self.user))

    def test_absolute_reads_retain_exact_host_path_semantics(self):
        self.user_file("Pair.BES", b"upper")
        self.user_file("Pair.bes", b"lower")
        for name, data in (("Pair.BES", b"upper"), ("Pair.bes", b"lower")):
            path = str(self.user / "SAVEGAMES" / name)
            self.assertEqual(data, self.run_driver("read", path))
            self.run_driver("write", path, "refused", expected=1)

    def assert_writes_refused(self, path):
        for action, args in (("write", ["refused"]), ("append", ["refused"]), ("update", ["refused"]),
                             ("delete", []), ("win-open", ["2", "0x40000000"]),
                             ("win-open", ["3", "0xc0000000"])):
            with self.subTest(action=action):
                self.run_driver(action, path, *args, expected=1)

    def test_save_leaf_symlink_cannot_write_original(self):
        folder = self.user / "savegames"
        folder.mkdir(parents=True)
        (folder / "Alpha.bes").symlink_to(self.saves / "Alpha.bes")
        self.assert_writes_refused(r"savegames\Alpha.bes")
        self.assertTrue((folder / "Alpha.bes").is_symlink())

    def test_save_parent_symlink_cannot_write_original(self):
        self.user.mkdir()
        (self.user / "savegames").symlink_to(self.saves, target_is_directory=True)
        self.assert_writes_refused(r"savegames\Alpha.bes")
        self.run_driver("mkdir", r"savegames\new-folder", expected=1)

    def test_user_root_symlink_cannot_write_original(self):
        self.user.symlink_to(self.game, target_is_directory=True)
        self.assert_writes_refused(r"savegames\Alpha.bes")
        self.run_driver("mkdir", "new-folder", expected=1)

    def test_symlink_above_missing_user_root_does_not_create_original_directories(self):
        alias = self.root / "alias"
        alias.symlink_to(self.game, target_is_directory=True)
        self.user = alias / "new-user"
        self.run_driver("write", "defaultoptions.bea", "refused", expected=1)
        self.assertFalse((self.game / "new-user").exists())

    def test_user_root_equal_to_or_inside_original_is_refused(self):
        for folder in (self.game, self.game / "new-user"):
            self.user = folder
            self.run_driver("write", "defaultoptions.bea", "refused", expected=1)
        self.assertFalse((self.game / "new-user").exists())

    def test_relative_user_root_beneath_original_is_refused(self):
        self.user = Path(".")
        self.run_driver("write", "defaultoptions.bea", "refused", expected=1, cwd=self.saves)

    def test_execute_only_ancestor_still_allows_user_writes(self):
        parent = self.root / "search-only"
        self.user = parent / "user"
        self.user.mkdir(parents=True)
        parent.chmod(0o111)
        try:
            self.run_driver("write", "defaultoptions.bea", "user settings")
            self.assertEqual(b"user settings", (self.user / "defaultoptions.bea").read_bytes())
        finally:
            parent.chmod(0o755)

    def test_hardlinked_user_save_is_refused_before_truncation(self):
        folder = self.user / "savegames"
        folder.mkdir(parents=True)
        os.link(self.saves / "Alpha.bes", folder / "Alpha.bes")
        self.assert_writes_refused(r"savegames\Alpha.bes")

    def test_settings_and_registry_links_cannot_write_original(self):
        self.user.mkdir()
        for name in ("defaultoptions.bea", "registry.ini"):
            (self.user / name).symlink_to(self.saves / "Alpha.bes")
        self.run_driver("write", "defaultoptions.bea", "refused", expected=1)
        self.run_driver("registry", "unused", expected=1)

    def test_registry_write_still_uses_only_the_user_folder(self):
        (self.game / "registry.ini").write_bytes(b"original registry")
        self.original = self.snapshot()
        self.run_driver("registry", "unused")
        self.assertEqual(b"fixture\\value=3:2a\n", (self.user / "registry.ini").read_bytes())

    def test_registry_case_variant_retains_other_keys(self):
        self.user.mkdir()
        registry = self.user / "Registry.INI"
        registry.write_bytes(b"other\\keep=3:aa\n")
        self.run_driver("registry", "unused")
        self.assertEqual(b"other\\keep=3:aa\nfixture\\value=3:2a\n", registry.read_bytes())
        self.assertFalse((self.user / "registry.ini").exists())

    def test_unreadable_registry_is_not_replaced(self):
        self.user.mkdir()
        registry = self.user / "registry.ini"
        before = b"other\\keep=3:aa\n"
        registry.write_bytes(before)
        registry.chmod(0o200)
        try:
            self.run_driver("registry", "unused", expected=1)
        finally:
            registry.chmod(0o600)
        self.assertEqual(before, registry.read_bytes())

    def test_read_only_creating_dispositions_never_target_original(self):
        for disposition in ("1", "2", "4", "5"):
            self.run_driver("win-open", r"savegames\Alpha.bes", disposition, "0x80000000", expected=1)

    def test_copy_failure_never_publishes_partial_user_file(self):
        for fault in ("read-failure", "write-failure", "sync-failure", "close-failure"):
            with self.subTest(fault=fault):
                self.run_driver("win-open", r"savegames\Alpha.bes", "3", "0xc0000000", expected=1, fault=fault)
                self.assertEqual([], [p for p in self.user.rglob("*") if p.is_file()])

    def test_copy_retries_short_and_interrupted_io(self):
        for name, fault in (("Alpha.bes", "short-write"), ("beta.BES", "read-interrupt"),
                            ("zeta.bes", "write-interrupt")):
            self.run_driver("win-open", "savegames/" + name, "3", "0xc0000000", fault=fault)
            self.assertEqual((self.saves / name).read_bytes(), self.run_driver("read", "savegames/" + name))

    def test_copy_does_not_replace_a_new_user_file(self):
        self.run_driver("win-open", r"savegames\Alpha.bes", "3", "0xc0000000", expected=1, fault="publish-race")
        self.assertEqual(b"concurrent user bytes", self.run_driver("read", r"savegames\Alpha.bes"))
        self.assertFalse(list(self.user.rglob(".ce-copy-*")))

    def test_copy_cleanup_failure_reports_failure_and_retains_complete_bytes(self):
        self.run_driver("win-open", r"savegames\Alpha.bes", "3", "0xc0000000", expected=1, fault="cleanup-failure")
        data = (self.saves / "Alpha.bes").read_bytes()
        self.assertEqual(data, self.run_driver("read", r"savegames\Alpha.bes"))
        retained = list(self.user.rglob(".ce-copy-*"))
        self.assertEqual(1, len(retained))
        self.assertEqual(data, retained[0].read_bytes())

    def test_existing_user_file_is_not_recopied(self):
        self.user_file("Alpha.bes", b"retained user bytes")
        self.run_driver("win-open", r"savegames\Alpha.bes", "3", "0xc0000000", fault="write-failure")
        self.assertEqual(b"retained user bytes", self.run_driver("read", r"savegames\Alpha.bes"))

    def test_game_read_mode_ra_is_still_readable(self):
        data = b"read-only cardid sentinel"
        (self.game / "cardid.txt").write_bytes(data)
        self.original = self.snapshot()
        self.assertEqual(data, self.run_driver("read-ra", "cardid.txt"))

    def test_parent_replacement_after_open_does_not_redirect_write(self):
        self.user_file("Alpha.bes", b"old user")
        self.run_driver("write", r"savegames\Alpha.bes", "new user", redirect=self.saves)
        self.assertEqual(b"new user", (self.user / "held-savegames/Alpha.bes").read_bytes())
        self.assertTrue((self.user / "SAVEGAMES").is_symlink())


if __name__ == "__main__":
    unittest.main()
