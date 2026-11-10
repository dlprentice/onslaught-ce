#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check optional terrain geometry distance against authored views and resources.

The policy fixtures run the actual module. Terrain fixtures compile the complete
existing CalcLODs and helpers, using the terrain lifetime harness's authored world
and checked resources. No installed game, profile, GPU, window or network is used.
Original-source compiler output and generated fixtures stay in ignored local-data.
These controls establish selection and allocation contracts, not native pixels.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "port/tools"
INCLUDE = ROOT / "port/platform/common"
SOURCE = INCLUDE / "terrain_quality.cpp"
DRIVER = TOOLS / "tests/terrain_quality_driver.cpp"
sys.path.insert(0, str(TOOLS))
import render_contract_tests as common
import terrain_buffer_lifetime_tests as terrain


def environment(**values):
    return {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent", **values}


class Policy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/terrain-quality-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="policy-", dir=parent))
        cls.binaries = {public: cls.build("ce" if public else "reference", SOURCE, public)
                        for public in (False, True)}

    @classmethod
    def build(cls, name, source, public=True):
        binary = cls.out / name
        cmd = ["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(INCLUDE),
               str(source), str(DRIVER), "-o", str(binary)]
        if public:
            cmd.insert(1, "-DCE_VERSION=1")
        result = subprocess.run(cmd, capture_output=True, timeout=60)
        (cls.out / (name + ".compile.log")).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("authored terrain policy did not compile; private diagnostic retained")
        return binary

    def run_case(self, mode, *, public=True, env=None, binary=None):
        return subprocess.run([str(binary or self.binaries[public]), mode], env=environment(**(env or {})),
                              capture_output=True, text=True, timeout=10)

    def passed(self, mode, **kwargs):
        done = self.run_case(mode, **kwargs)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout, "PASS\n")

    def test_normal_is_default(self):
        self.passed("default")

    def test_stored_choice_waits_for_frame(self):
        self.passed("stored")

    def test_frame_immutable_on_off(self):
        self.passed("switch")

    def test_queued_first_and_last_request(self):
        self.passed("queued-first")

    def test_resource_loss_is_frame_bounded_and_recovers(self):
        self.passed("resources")

    def test_faithful_fences_precede_settings(self):
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            for value in ("1", "0", "reference"):
                with self.subTest(name=name, value=value):
                    self.passed("fenced", env={name: value, "BEA_PORT_CE_LAYER": "1",
                                               "BEA_PORT_TERRAIN_QUALITY_LOG": "1"})
        self.passed("fenced", public=False)
        self.passed("fenced", env={"DRIVER_LAYER_OFF": "1"})

    def test_empty_fences_allow_ordinary_choice(self):
        self.passed("stored", env={name: "" for name in (
            "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")})

    def test_bounded_distance_domain_and_normal_bits(self):
        self.passed("domain")

    def test_diagnostics_do_not_enable_extended(self):
        self.passed("diagnostics", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "1"})

    def test_only_a_changed_request_is_fresh_when_the_check_is_sampled(self):
        self.passed("fresh-request")

    def test_diagnostic_window_reports_reserve_traffic_and_frame_intervals(self):
        self.passed("diagnostic-fields", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "1"})

    def test_nothing_is_counted_timed_or_logged_without_the_diagnostic(self):
        self.passed("diagnostic-off")
        self.passed("diagnostic-off", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "0"})

    def test_policy_mutants_are_causal(self):
        original = SOURCE.read_text()
        cases = [
            ("mid-frame", "requested = value;", "requested = value; extended = value == 1;", "switch", {}),
            ("ignore-faithful", "if (value && *value) return false;", "if (false && value && *value) return false;",
             "fenced", {"BEA_PORT_CLOCK": "1"}),
            ("default-extended", 'CE_SettingGetBool("terrain_detail_distance", 0)',
             'CE_SettingGetBool("terrain_detail_distance", 1)', "default", {}),
            ("wrong-range", "distanceSquared * 0.25f", "distanceSquared * 0.5f", "domain", {}),
            ("ignore-resources", "wantsExtension && resourcesReady", "wantsExtension", "resources", {}),
            ("hide-resource-refusal", "resourcesUsable ? CE_TERRAIN_DETAIL_RESOURCE_FALLBACK",
             "resourcesUsable ? CE_TERRAIN_DETAIL_ACTIVE", "resources", {}),
            ("fresh-every-frame", "freshRequest = requested != frameRequest;", "freshRequest = true;", "fresh-request", {}),
            ("never-fresh", "freshRequest = requested != frameRequest;", "freshRequest = false;", "fresh-request", {}),
            ("slow-threshold", "if (frameMicros[i] > 16667) ++out.over16;", "if (frameMicros[i] > 16667000) ++out.over16;",
             "diagnostic-fields", {"BEA_PORT_TERRAIN_QUALITY_LOG": "1"}),
            ("grants-as-refusals", "Increment((granted ? reserveGrants : reserveRefusals)[",
             "Increment((granted ? reserveRefusals : reserveGrants)[", "diagnostic-fields",
             {"BEA_PORT_TERRAIN_QUALITY_LOG": "1"}),
            ("stale-window", "    frameIntervals = 0;\n    farCells = allocationFailures = 0;",
             "    farCells = allocationFailures = 0;", "diagnostic-fields", {"BEA_PORT_TERRAIN_QUALITY_LOG": "1"}),
            ("counts-without-diagnostic", "bool CeTerrainQualityDiagnostics()\n{\n    return Allowed() && diagnostics;",
             "bool CeTerrainQualityDiagnostics()\n{\n    return Allowed();", "diagnostic-off", {}),
        ]
        for name, old, new, mode, env in cases:
            with self.subTest(name=name):
                self.assertEqual(original.count(old), 1)
                source = self.out / (name + ".cpp")
                source.write_text(original.replace(old, new))
                result = self.run_case(mode, env=env, binary=self.build(name, source))
                self.assertEqual(result.returncode, 1)
                self.assertIn("Assertion failed in authored terrain driver", result.stderr)


TERRAIN_MAIN = r"""
int testPatchLimit=4096;
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    CeTerrainQualityResourceCheck([](){return true;});
    const int choice=std::atoi(argv[1]);
    const float height=std::strtof(argv[2],nullptr);
    testPatchLimit=std::atoi(argv[3]);
    const int movement=std::atoi(argv[4]);
    if(choice>=0 && !CE_TerrainDetailRequest(choice))return 3;
    CeTerrainQualityBeginFrame();
    CDXLandscape *land=new(MEMTYPE_LANDSCAPE) CDXLandscape;
    CIBuffer index;land->mIB=&index;land->mNumViews=2;land->mViews=new CLandscapeView[2];
    Camera cameras[2];
    for(int v=0;v<2;++v) {
        auto &view=land->mViews[v];view.CreateMipLevels(5);InitializeTiles(view);
        cameras[v].pos={192,192,height};view.mCamera=&cameras[v];
        for(int i=0;i<4096;++i) {
            // Authored height/complexity bands, not decoded game content.
            view.mTiles[i].mComplexity=float((i/64)%5);
            view.mTiles[i].mMidHeight=float((i%64)/8);
        }
    }
    index.RestoreDeviceObjects();
    for(auto &entry:PATCHMANAGER.mIBs) {
        entry.mIB=new IDirect3DIndexBuffer9(8);entry.mIB->words={0,1,2,3};entry.mIB->valid=true;
    }
    C3DGamut gamut;std::fill(std::begin(gamut.mSpans),std::end(gamut.mSpans),0);
    gamut.mMaxX=gamut.mMaxY=63;
    for(int v=0;v<2;++v)for(int l=0;l<5;++l)land->mViews[v].mLODs[l].mUnknown08=0;
    trace.clear();const char *unsafe="";
    for(int tick=0;tick<(movement==1?6:1);++tick) {
        if(movement==1) {
            cameras[0].pos.X+=11; cameras[0].pos.Y+=7;
            cameras[0].pos.Z-=9;
        }
        land->CalcLODs(&gamut,0);
        device.indices=index.mIB;
        try {land->DrawCounts(0);}catch(const Unsafe &failure){unsafe=failure.reason;break;}
    }
    if(movement==2) {
        SLandscapeCell *selected=nullptr;
        for(int i=0;i<4096;++i)if(land->mViews[0].mTiles[i].mPatch){
            selected=&land->mViews[0].mTiles[i];break;
        }
        if(!selected)return 7;
        reserveWitness=*selected->mPatch;
        selected->mPatch->mSlot=0xffff;
        selected->mPatch=&reserveWitness;
        reserveWitnessActive=true;
        // Model resource loss with a still-live owned CPU object: the actual
        // terrain consumer must retire it before trying to draw Normal terrain.
        CeTerrainQualityResourceCheck([](){return false;});
        if(!CE_TerrainDetailRequest(1))return 3;
        CeTerrainQualityBeginFrame();
        land->CalcLODs(&gamut,0);
        device.indices=index.mIB;
        try {land->DrawCounts(0);}catch(const Unsafe &failure){unsafe=failure.reason;}
        if(reserveReleaseCalls!=1 || selected->mPatch==&reserveWitness ||
           reserveWitness.mSlot!=0xffff || CeTerrainQualityExtended()) {
            std::fprintf(stderr,"Authored reserve retirement control failed: releases=%d replaced=%d free=%d extended=%d owner_checks=%d owner_hits=%d\n",
                         reserveReleaseCalls,selected->mPatch!=&reserveWitness,
                         reserveWitness.mSlot==0xffff,CeTerrainQualityExtended(),reserveOwnerChecks,reserveOwnerHits);return 8;
        }
    }
    Dump(*land,0,unsafe);return 0;
}
"""


class ActualTerrain(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/terrain-quality-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="terrain-", dir=parent))
        cls.receipt = {"patches": [], "compiles": [], "cases": []}
        common.FIX = "0210-ce-terrain-detail-distance.patch"
        common.SOURCES = terrain.FILES
        cls.sources = common.variants(ROOT, cls.out, cls.receipt)
        cls.binaries = {}
        for name, sources in cls.sources.items():
            cls.binaries[name] = cls.build(name, sources)
        changed = dict(cls.sources["patched"])
        text = changed["DXLandscape.cpp"]
        pattern = r"(SINT\s+tex\s*=\s*ChooseLandscapeTexture\s*\([^;]*?)\bdist2\b"
        text, count = re.subn(pattern, lambda m: m.group(1) + "geometryDist2", text)
        if count != 1:
            raise RuntimeError("texture-input mutant could not be formed")
        changed["DXLandscape.cpp"] = text
        cls.binaries["texture-mutant"] = cls.build("texture-mutant", changed)
        changed = dict(cls.sources["patched"])
        text, count = re.subn(r"(?m)^(\s*)(AppendLandscapeFarCell\s*\()",
                              lambda m: m.group(1) + "if (false) " + m.group(2), changed["DXLandscape.cpp"])
        if count != 1:
            raise RuntimeError("coarse-fallback mutant could not be formed")
        changed["DXLandscape.cpp"] = text
        cls.binaries["fallback-mutant"] = cls.build("fallback-mutant", changed)
        changed = dict(cls.sources["patched"])
        old = "!CeTerrainQualityExtended() && CeTerrainReserveOwns(tile->mPatch)"
        if changed["DXLandscape.cpp"].count(old) != 1:
            raise RuntimeError("owned-retirement mutant could not be formed")
        changed["DXLandscape.cpp"] = changed["DXLandscape.cpp"].replace(old, "false")
        cls.binaries["retirement-mutant"] = cls.build("retirement-mutant", changed)

    @classmethod
    def tearDownClass(cls):
        (cls.out / "receipt.private.json").write_text(json.dumps(cls.receipt, indent=2) + "\n")

    @classmethod
    def build(cls, name, sources):
        text = terrain.terrain_driver(sources, quality_policy=True)
        start = text.rindex("int main(int argc,char **argv)")
        text = text[:start] + TERRAIN_MAIN
        replacement = r"""
extern int testPatchLimit;
struct PatchList {
    CLandscapeVB patches[4096];
    CLandscapeVB *Allocate(int slot) {
        for(int i=0;i<testPatchLimit;++i)
            if(patches[i].mSlot==0xffff) {
                patches[i].mSlot=(UWORD)slot;Record("allocate",slot);return &patches[i];
            }
        Record("exhausted",slot);return nullptr;
    }
};
"""
        text, count = re.subn(r"struct PatchList \{.*?\n\};", lambda _: replacement, text, flags=re.S)
        if count != 1:
            raise RuntimeError("authored patch-pool fixture changed")
        # These geometry/pool-exhaustion controls deliberately offer no optional
        # reserve. The separate reserve suite exercises its actual owned backend.
        marker = "void ToWCHAR(char *)"
        if text.count(marker) != 1:
            raise RuntimeError("authored terrain declarations changed")
        text = text.replace(marker,
                            "CLandscapeVB reserveWitness;\n"
                            "bool reserveWitnessActive=false;int reserveReleaseCalls=0,reserveOwnerChecks=0,reserveOwnerHits=0;\n"
                            "CLandscapeVB *CeTerrainReserveAllocate(int,unsigned short){return nullptr;}\n"
                            "bool CeTerrainReserveOwns(const void*p){++reserveOwnerChecks;bool owned=reserveWitnessActive && p==&reserveWitness;if(owned)++reserveOwnerHits;return owned;}\n"
                            "void CeTerrainReserveReleased(const void*p){if(CeTerrainReserveOwns(p))++reserveReleaseCalls;}\n" + marker, 1)
        # Observe the actual texture selector's arguments and result at its call
        # boundary. A geometry extension must not scale these cache coordinates.
        selector = terrain.definition(sources["DXLandscape.cpp"], "static inline SINT ChooseLandscapeTexture(")
        header = selector[:selector.index("{")]
        args = header[header.index("(") + 1:header.rindex(")")]
        names = [re.findall(r"[A-Za-z_]\w*", argument)[-1] for argument in args.split(",")]
        wrapper = (selector.replace("ChooseLandscapeTexture", "OriginalChooseLandscapeTexture", 1) + "\n" + header +
                   '{\n    SINT result=OriginalChooseLandscapeTexture(' + ",".join(names) + ');\n' +
                   '    Record("texture-choice",cam.X,cam.Y,cam.Z,dist2,dz2,result);\n    return result;\n}')
        if text.count(selector) != 1:
            raise RuntimeError("actual texture selector extraction changed")
        text = text.replace(selector, wrapper)
        prefix = ('#include "terrain_quality.h"\n#include "portlog.h"\n#include <stdlib.h>\n'
                  'bool PortCeLayer(){return true;}\n'
                  'extern "C" int CE_SettingGetBool(const char*,int fallback){'
                  'const char *value=getenv("DRIVER_TERRAIN_STORED");return value?atoi(value):fallback;}\n'
                  'void PortSessionLog(PortSessionCategory,bool,const char*,...){}\n')
        # Include the actual authored module; all extracted terrain stays private.
        text = prefix + text + '\n#include "' + str(SOURCE) + '"\n'
        cxx = 'g++ -DCE_VERSION=1 -I' + str(INCLUDE)
        try:
            return common.compile_driver(cxx, cls.out, name + "-fixture", text, cls.receipt)
        finally:
            (cls.out / "receipt.private.json").write_text(json.dumps(cls.receipt, indent=2) + "\n")

    def run_case(self, variant, choice, height=-20, capacity=4096, movement=0, env=None):
        args = [str(choice), str(height), str(capacity), str(movement)]
        proc = subprocess.run([str(self.binaries[variant]), *args], env=environment(**(env or {})),
                              capture_output=True, text=True, timeout=15)
        self.assertEqual(proc.returncode, 0, "actual terrain fixture failed; source/diagnostics are private")
        text = re.sub(r'(?<![\w"])(-?0x[0-9a-f.]+p[+-]?[0-9]+)', r'"\1"', proc.stdout)
        value = json.loads(text)
        self.assertEqual(value["unsafe"], "")
        self.receipt["cases"].append({"variant": variant, "arguments": args,
                                     "hash": hashlib.sha256(proc.stdout.encode()).hexdigest(), "unsafe": value["unsafe"]})
        return value

    def test_normal_actual_geometry_and_trace_are_unchanged(self):
        for height in (-20, -90, -220):
            for movement in (0, 1):
                with self.subTest(height=height, movement=movement):
                    self.assertEqual(self.run_case("baseline", 0, height, movement=movement),
                                     self.run_case("patched", 0, height, movement=movement))

    def test_actual_normal_draw_retires_reserve_owners_after_resource_loss(self):
        self.run_case("patched", 0, movement=2)
        p = subprocess.run([str(self.binaries["retirement-mutant"]), "0", "-20", "4096", "2"],
                           env=environment(), capture_output=True, text=True, timeout=15)
        self.assertEqual(p.returncode, 8)
        self.assertIn("Authored reserve retirement control failed", p.stderr)

    def test_forced_faithful_ignores_stored_extension(self):
        # No explicit request: faithful paths refuse requests and return Normal.
        baseline = self.run_case("baseline", -1)
        self.assertEqual(baseline, self.run_case("patched", -1, env={"BEA_PORT_CLOCK": "1", "BEA_PORT_CE_LAYER": "1",
                                                                  "DRIVER_TERRAIN_STORED": "1"}))

    def test_extended_changes_geometry_and_survives_complete_pool_exhaustion(self):
        changed = 0
        for height in (-20, -90, -220):
            before = self.run_case("patched", 0, height)
            after = self.run_case("patched", 1, height)
            changed += before["tiles"] != after["tiles"]
            self.assertEqual(before["view"], after["view"])
            self.assertGreaterEqual(sum(tile[0] >= 0 for tile in after["tiles"]),
                                    sum(tile[0] >= 0 for tile in before["tiles"]))
            for capacity in (0, 1, 4):
                result = self.run_case("patched", 1, height, capacity, movement=1)
                attached = [tile[2] for tile in result["tiles"] if tile[2] != 65535]
                self.assertEqual(len(attached), len(set(attached)), "a patch belongs to more than one cell")
                for index, tile in enumerate(result["tiles"]):
                    if tile[2] != 65535:
                        self.assertEqual(tile[2], index)
                if capacity == 0:
                    self.assertFalse(attached)
                    self.assertGreater(result["far_count"], 2, "exhaustion discarded coarse terrain")
        self.assertGreater(changed, 0, "the fixture never exercised extended geometry")

    def test_exhaustion_control_catches_discarded_coarse_terrain(self):
        candidate = self.run_case("patched", 1, capacity=0)
        mutant = self.run_case("fallback-mutant", 1, capacity=0)
        self.assertGreater(candidate["far_count"], 2)
        self.assertLessEqual(mutant["far_count"], 2)

    def test_texture_coordinates_stay_original_and_mutant_is_caught(self):
        def selections(result):
            rows = [line.split(":")[1:] for line in result["trace"] if line.startswith("texture-choice:")]
            return {tuple(row[:3]): row[3:] for row in rows}
        baseline = selections(self.run_case("baseline", 0))
        candidate = selections(self.run_case("patched", 1))
        overlap = baseline.keys() & candidate.keys()
        self.assertTrue(overlap, "texture control had no shared terrain cells")
        self.assertTrue(all(baseline[key] == candidate[key] for key in overlap))
        mutant = selections(self.run_case("texture-mutant", 1))
        common_keys = baseline.keys() & mutant.keys()
        self.assertTrue(any(baseline[key] != mutant[key] for key in common_keys),
                        "texture-distance mutant did not exercise the actual selector")


if __name__ == "__main__":
    unittest.main(verbosity=2)
