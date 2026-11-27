#!/usr/bin/env python3
"""Manual, selected-career backup: check actual files in entirely synthetic directories."""
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "local-data/port/ce-career-backup-tests"
SIZE = 10004


class StoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(dir=SCRATCH)
        cls.runner = Path(cls.temp.name) / "driver"
        subprocess.run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "port/platform/linux/ce_backup_store.cpp"),
                        str(ROOT / "port/tools/tests/ce_backup_store_driver.cpp"), "-o", str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.case = tempfile.TemporaryDirectory(dir=self.temp.name)
        self.addCleanup(self.case.cleanup)
        self.parent = Path(self.case.name)
        self.u, self.g, self.p, self.dest = (self.parent / n for n in ("user","game","package","copies"))
        for d in (self.u,self.g,self.p,self.dest): d.mkdir()
        self.saves = self.u / "savegames"
        self.saves.mkdir()
        self.name = "Pilot.bes"
        self.original = self.saves / self.name
        self.data = bytes((q * 17 + 9) % 256 for q in range(SIZE))
        self.original.write_bytes(self.data)
        self.capture = self.parent / "snapshot"
        self.capture.write_bytes(self.data)
        self.other = self.saves / "Other.bes"
        self.other.write_bytes(bytes(reversed(self.data)))

    def args(self, dest=None, name=None):
        return list(map(str, [self.runner, self.u, self.g, self.p, dest or self.dest, name or self.name, self.capture]))

    def run_case(self, dest=None, name=None, ok=False, limit=None):
        before = {x.name: (x.lstat().st_ino, x.read_bytes()) for x in self.saves.iterdir() if x.is_file()}
        def cap():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))
        p = subprocess.run(self.args(dest,name), capture_output=True, text=True, timeout=6,
                           preexec_fn=cap if limit else None)
        self.assertEqual(p.returncode, 0 if ok else 1, p.stderr)
        self.assertEqual(before, {x.name: (x.lstat().st_ino, x.read_bytes()) for x in self.saves.iterdir() if x.is_file()})
        if ok:
            self.assertTrue(p.stdout.strip())
            return Path(p.stdout.strip())
        self.assertFalse(p.stdout.strip())

    def verify(self, folder):
        manifest = json.loads((folder / "onslaught-backup.json").read_bytes())
        self.assertEqual(set(x.name for x in folder.iterdir()), {self.name, "onslaught-backup.json"})
        self.assertEqual(folder.parent, self.dest)
        self.assertRegex(folder.name, r"^onslaught-backup-[0-9]{8}-[0-9]{6}(-[0-9]+)?$")
        self.assertEqual(Path(manifest["Folder"]), folder)
        self.assertEqual(Path(manifest["Game"]), self.u)
        self.assertEqual(manifest["Target"], 1)
        self.assertEqual(manifest["Files"], [{"Name": self.name,"Original": str(self.original),"Size":SIZE,
                                             "Sha256":hashlib.sha256(self.data).hexdigest()}])
        self.assertEqual((folder/self.name).read_bytes(),self.data)

    def test_picker_cancel_and_background_job_keep_their_original_page(self):
        runner = Path(self.temp.name) / "worker-driver"
        subprocess.run(["g++", "-std=c++17", "-pthread", "-O1", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "port/platform/common/ce_career_backup.cpp"),
                        str(ROOT / "port/tools/tests/ce_backup_worker_driver.cpp"), "-o", str(runner)], check=True)
        subprocess.run([str(runner)], timeout=8, check=True)

    def test_repeat_and_simultaneous_sets_never_replace(self):
        held = self.run_case(ok=True); self.verify(held)
        first = {f.name:(f.stat().st_ino,f.read_bytes()) for f in held.iterdir()}
        ps = [subprocess.Popen(self.args(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(4)]
        results = [p.communicate(timeout=8) for p in ps]
        self.assertTrue(all(p.returncode == 0 for p in ps))
        paths = [Path(out.strip()) for out,_ in results]
        self.assertEqual(len(set([held, *paths])),5)
        for f in paths: self.verify(f)
        self.assertEqual(first,{f.name:(f.stat().st_ino,f.read_bytes()) for f in held.iterdir()})
        self.assertEqual(self.original.read_bytes(),self.data)

    def test_external_existing_folder_only(self):
        for root in (self.u,self.saves,self.p,self.g):
            d=root/"copies";d.mkdir()
            self.run_case(root);self.run_case(d)
            self.assertEqual(list(d.iterdir()),[])
        self.run_case(self.parent/"absent");self.run_case("relative")
        self.run_case(str(self.dest)+"/../copies")
        self.assertEqual(list(self.dest.iterdir()),[])

    def test_all_link_components_refused(self):
        link=self.parent/"link";link.symlink_to(self.dest,target_is_directory=True)
        deep=self.dest/"nested";deep.mkdir()
        self.run_case(link);self.run_case(link/"nested")
        self.assertFalse(list(deep.iterdir()))
        self.original.unlink();self.original.symlink_to(self.capture)
        self.run_case();self.assertFalse(any(self.dest.glob("*/onslaught-backup.json")))
        self.original.unlink();os.link(self.capture,self.original)
        self.run_case();self.assertFalse(any(self.dest.glob("*/onslaught-backup.json")))

    def test_missing_or_stale_selection_refused(self):
        self.original.write_bytes(self.data[:-2]+b"xx");self.run_case()
        self.original.write_bytes(self.data[:-1]);self.run_case()
        self.original.unlink(); self.run_case()
        (self.g/"savegames").mkdir();(self.g/"savegames"/self.name).write_bytes(self.data)
        self.run_case()
        self.assertFalse(list(self.dest.iterdir()))

    def test_fault_does_not_publish_or_damage_saved_set(self):
        held=self.run_case(ok=True);self.verify(held)
        self.run_case(limit=256)
        self.assertEqual(list(self.dest.glob("*/onslaught-backup.json")),[held/"onslaught-backup.json"])
        self.verify(held)

    def test_unsafe_names_and_wrong_length_never_publish(self):
        for name in ("../Pilot.bes", "A/B.bes", "NUL.bes", "x.txt", "x*.bes"):
            self.run_case(name=name)
        self.capture.write_bytes(self.data[:-1]);self.run_case()
        self.assertFalse(list(self.dest.iterdir()))

    def test_selected_linux_card_names_recover_their_utf8_leaf_and_display(self):
        code = r"""
#include "ce_backup_name.h"
#include <cstdint>
#include <cstring>
extern "C" unsigned PortSaveNameFromFileCp1252(const char *,void *,unsigned);
extern "C" unsigned PortSaveNameFromLegacyCp1252(const char *,void *,unsigned);
int main() {
    struct Row {const char *s; bool hidden;};
    const Row cases[]={{"Pilot",false},{u8"Pil\u00f6t",false},{u8"Mi-\u05d8\u05d9\u05d9\u05e1",false},
                       {u8"Bir\u202ech",true},{u8"Pilot\u2066One",true}};
    for(auto r:cases) {
        uint16_t chosen[256]={},display[256]={};char file[256],withExtension[256];
        if(!PortSaveNameFromFileCp1252(r.s,chosen,256) &&
           !PortSaveNameFromLegacyCp1252(r.s,chosen,256))return 1;
        if(!CeBackupSelectedName(chosen,file,sizeof(file),display))return 2;
        if(CeBackupNameHasDirectionMark(display)!=r.hidden)return 3;
        std::strcpy(withExtension,r.s);std::strcat(withExtension,".bes");
        if(std::strcmp(file,withExtension))return 4;
    }
}
"""
        src = self.parent / "list_name_check.cpp"
        src.write_text(code)
        binary = self.parent / "list-name-check"
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                        "-I"+str(ROOT/"port/platform/common"),str(src),
                        str(ROOT/"port/platform/linux/ce_backup_name.cpp"),
                        str(ROOT/"port/platform/linux/save_cp1252.cpp"),
                        str(ROOT/"port/platform/common/save_names.cpp"),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

    def test_direction_formatting_refused_before_any_set_or_name_is_printed(self):
        for mark in (0x061c, 0x200e, 0x200f, *range(0x202a,0x202f), *range(0x2066,0x206a)):
            name = "Pilot" + chr(mark) + "One.bes"
            path = self.saves / name
            path.write_bytes(self.data)
            before = path.stat().st_ino
            result = subprocess.run(self.args(name=name), capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"hidden text direction marks", result.stderr)
            self.assertFalse(result.stdout)
            self.assertNotIn(chr(mark).encode(), result.stderr)
            self.assertEqual(list(self.dest.iterdir()), [])
            self.assertEqual((path.stat().st_ino,path.read_bytes()),(before,self.data))

    def test_natural_right_to_left_and_mixed_names_are_unchanged(self):
        for name in ("Misión.bes", "טייס.bes", "طيار.bes", "Pilot-טייס.bes"):
            self.name = name
            target = self.saves / name
            self.original.rename(target); self.original = target
            self.verify(self.run_case(ok=True))

    def test_quoted_folder_and_unicode_name_roundtrip(self):
        new=self.parent/'Tést "Archive"';self.dest.rename(new);self.dest=new
        self.name="Pilöt.bes";newname=self.saves/self.name;self.original.rename(newname);self.original=newname
        self.verify(self.run_case(ok=True))

    def test_trailing_slash_trimmed_and_posix_backslash_preserved_through_backup_controller(self):
        slash_dest = self.parent / "CopiesSlash"
        slash_dest.mkdir()
        bslash_neighbor = self.parent / "CopiesBackslash"
        bslash_neighbor.mkdir()
        bslash_dest = self.parent / "CopiesBackslash\\"
        bslash_dest.mkdir()
        code = r"""
#include "ce_career_backup.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
extern "C" SDL_Window *SDL_GetKeyboardFocus(){return nullptr;}
extern "C" void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback,void *,SDL_Window *,const char *,bool){}
int main(int argc, char **argv) {
    if (argc != 8) return 2;
    FILE *f = fopen(argv[6], "rb");
    if (!f) return 3;
    unsigned char bytes[10004] = {};
    if (fread(bytes, 1, sizeof(bytes), f) != sizeof(bytes)) { fclose(f); return 4; }
    fclose(f);
    CeBackupEnter(argv[3]);
    CeBackupStatus st{};
    CeBackupPoll(&st);
    if (std::strcmp(st.folder, argv[4]) != 0) return 5;
    if (!CeBackupStart(argv[1], argv[2], argv[5], bytes, sizeof(bytes))) return 6;
    for (int i = 0; i < 2000; ++i) {
        CeBackupPoll(&st);
        if (st.state != CE_BACKUP_WORKING) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CeBackupLeave();
    const CeBackupState want = !std::strcmp(argv[7], "ok") ? CE_BACKUP_OK : CE_BACKUP_ERROR;
    return st.state == want ? 0 : 7;
}
"""
        src = self.parent / "controller_trim_check.cpp"
        src.write_text(code)
        binary = self.p / "controller-trim-check"
        subprocess.run(["g++", "-std=c++17", "-pthread", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "port/platform/common"),
                        str(src),
                        str(ROOT / "port/platform/common/ce_career_backup.cpp"),
                        str(ROOT / "port/platform/linux/ce_backup_store.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(self.u), str(self.g), str(slash_dest) + "///",
                        str(slash_dest), self.name, str(self.capture), "ok"], check=True, timeout=10)
        sets = list(slash_dest.glob("onslaught-backup-*"))
        self.assertEqual(len(sets), 1)
        # POSIX backslash is preserved (never silently redirected into sibling CopiesBackslash).
        subprocess.run([str(binary), str(self.u), str(self.g), str(bslash_dest) + "//",
                        str(bslash_dest), self.name, str(self.capture), "error"], check=True, timeout=10)
        self.assertEqual(list(bslash_neighbor.iterdir()), [])
        self.assertEqual(list(bslash_dest.iterdir()), [])

    def test_chosen_backup_folder_persisted_on_poll(self):
        code = r"""
#include "ce_career_backup.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <string>
namespace {
SDL_DialogFileCallback gCb = nullptr;
void *gData = nullptr;
int gWrites = 0;
std::string gKey, gVal;
}
extern "C" SDL_Window *SDL_GetKeyboardFocus() { return nullptr; }
extern "C" void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback cb, void *data, SDL_Window *, const char *, bool) {
    gCb = cb; gData = data;
}
extern "C" int CE_SettingSetText(const char *key, const char *value) {
    ++gWrites; gKey = key ? key : ""; gVal = value ? value : ""; return 1;
}
bool CeBackupStore(const CeBackupInput &, char *, size_t, const char *&) { return false; }
int main() {
    CeBackupEnter("/initial");
    CeBackupStatus st{};
    CeBackupPoll(&st);
    if (gWrites != 0) return 1;
    CeBackupChoose();
    const char *bad[] = {"relative", nullptr};
    gCb(gData, bad, 0);
    CeBackupPoll(&st);
    if (gWrites != 0) return 2;
    CeBackupChoose();
    const char *good[] = {"/chosen/backups///", nullptr};
    gCb(gData, good, 0);
    if (gWrites != 0) return 3; // must persist on game thread in CeBackupPoll, not inside dialog callback
    CeBackupPoll(&st);
    if (gWrites != 1 || gKey != "career_backup_folder" || gVal != "/chosen/backups") return 4;
    CeBackupPoll(&st);
    if (gWrites != 1) return 5; // persist once per pick
    CeBackupLeave();
    return 0;
}
"""
        src = self.parent / "persist_check.cpp"
        src.write_text(code)
        binary = self.p / "persist-check"
        subprocess.run(["g++", "-std=c++17", "-pthread", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "port/platform/common"),
                        str(src), str(ROOT / "port/platform/common/ce_career_backup.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
