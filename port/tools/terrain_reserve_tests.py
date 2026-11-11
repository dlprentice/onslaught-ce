#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real owned terrain reserve with authored resource failures.

The native buffer class is represented by a small device-lifetime double. The
reserve and frame policy themselves are compiled unchanged. No game data, GPU,
profile or window is used; native rendering remains a separate gate.

The reserve is prepared at a frame boundary, once per initialisation, and only
while Extended is requested: these controls prove a session on Normal never
creates a reserved buffer and that a refusal is not retried frame after frame.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import quiet_crash

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "port/platform/common"
GAME = ROOT / "port/game"
SOURCE = GAME / "ce_terrain_reserve.cpp"
POLICY = COMMON / "terrain_quality.cpp"
DRIVER = ROOT / "port/tools/tests/terrain_reserve_driver.cpp"

TYPES = r"""
#ifndef AUTHORED_TERRAIN_RESERVE_TYPES
#define AUTHORED_TERRAIN_RESERVE_TYPES
#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>
using UBYTE=unsigned char;
using UWORD=unsigned short;
using SINT=int;
class CLandscapeVB;
extern std::vector<CLandscapeVB*> registry;
extern int constructions, destructions, creates, liveDevices, failCreate;
class CLandscapeVB {
public:
    UWORD mSlot=std::numeric_limits<UWORD>::max();
    int mCells=0, mSize=0;
    void *mVB=nullptr;
    CLandscapeVB(){++constructions;registry.push_back(this);}
    ~CLandscapeVB(){
        ++destructions;
        if(mVB)--liveDevices;
        registry.erase(std::find(registry.begin(),registry.end(),this));
    }
    void Create(int cells){
        mCells=cells;mSize=(cells+1)*(cells+1)*int(5*sizeof(float));
        ++creates;
        if(creates!=failCreate){mVB=this;++liveDevices;}
    }
};
struct CDXPatchList { int mCount=0; UBYTE *mPatches=nullptr; };
class CDXPatchManager { public: CDXPatchList *mLists=nullptr; };
#endif
"""


class Reserve(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/terrain-reserve-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="reserve-", dir=parent))
        (cls.out / "reserve_types.h").write_text(TYPES)
        for name in ("common.h", "DX.h", "landscape.h", "LandscapeVB.h"):
            (cls.out / name).write_text('#include "reserve_types.h"\n')
        cls.binaries = {public: cls.build("ce" if public else "reference", public=public)
                        for public in (False, True)}

    @classmethod
    def build(cls, name, *, public=True, source=SOURCE):
        binary = cls.out / name
        cmd = ["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(cls.out), "-I", str(COMMON), "-I", str(GAME),
               str(source), str(POLICY), str(DRIVER), "-Wl,--wrap=malloc", "-o", str(binary)]
        if public:
            cmd.insert(1, "-DCE_VERSION=1")
        p = subprocess.run(cmd, capture_output=True, timeout=60)
        (cls.out / (name + ".compile.log")).write_bytes(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError("owned terrain reserve fixture did not compile; diagnostic retained")
        return binary

    def run_case(self, mode, *, env=None, public=True, binary=None):
        return quiet_crash.run([str(binary or self.binaries[public]), mode],
                               env={"PATH": os.environ.get("PATH", "/usr/bin"),
                                    "HOME": "/nonexistent", **(env or {})},
                               capture_output=True, text=True, timeout=10)

    def passed(self, mode, **kwargs):
        p = self.run_case(mode, **kwargs)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(p.stdout, "PASS\n")

    def test_normal_creates_no_reserved_buffer_until_extended_is_requested(self):
        self.passed("normal")

    def test_reinitialisation_starts_from_nothing_again(self):
        self.passed("reinit")

    def test_bounded_unique_allocation_and_refusal(self):
        self.passed("allocate")

    def test_release_and_reset_preserve_other_owners(self):
        self.passed("release-reset")

    def test_two_views_with_same_tile_id_keep_distinct_owners(self):
        self.passed("two-views")

    def test_runtime_off_and_on_reuse_resources(self):
        self.passed("toggle")

    def test_partial_device_creation_is_rolled_back_and_not_retried(self):
        self.passed("failed-create")

    def test_diagnostic_fault_rolls_back_every_stop_on_the_real_path(self):
        for spec in ("0:0", "0:4", "0:11", "1:0", "1:4", "2:0", "2:3", "2:5"):
            with self.subTest(spec=spec):
                self.passed("fault", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "1",
                                          "BEA_PORT_TERRAIN_RESERVE_FAULT": spec})

    def test_a_new_extended_request_earns_one_renewed_attempt_after_a_fault(self):
        env = {"BEA_PORT_TERRAIN_QUALITY_LOG": "1"}
        for spec in ("0:0:1", "0:4:1", "2:3:1", "1:0:1", "2:0:1"):
            with self.subTest(spec=spec):
                self.passed("fault-retry", env=dict(env, BEA_PORT_TERRAIN_RESERVE_FAULT=spec))
        for spec in ("0:0", "0:4", "2:3", "1:4", "2:0"):
            with self.subTest(spec=spec):
                self.passed("fault-retry-every", env=dict(env, BEA_PORT_TERRAIN_RESERVE_FAULT=spec))

    def test_diagnostic_fault_is_inert_without_the_diagnostic_or_with_a_bad_spec(self):
        self.passed("fault-inert", env={"BEA_PORT_TERRAIN_RESERVE_FAULT": "1:0"})
        self.passed("fault-inert", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "0", "BEA_PORT_TERRAIN_RESERVE_FAULT": "2:0"})
        for spec in ("", "3:0", "-1:0", "1", "1:", "1:x", "2:-1", "1:0x", "11:0", " 1:0", "1:0 ", "1:100001",
                     "1:0:", "1:0:0", "1:0:x", "1:0:1001", "1:0:-1", "1:0:1:1", "1:0:1 "):
            with self.subTest(spec=spec):
                self.passed("fault-inert", env={"BEA_PORT_TERRAIN_QUALITY_LOG": "1",
                                                "BEA_PORT_TERRAIN_RESERVE_FAULT": spec})

    def test_failed_owned_allocation_is_rolled_back(self):
        self.passed("failed-malloc")

    def test_resource_budget_and_invalid_shape_refuse(self):
        for mode in ("budget", "invalid-size", "invalid-cells", "nearest-budget", "nearest-invalid-size", "nearest-invalid-cells"):
            with self.subTest(mode=mode):
                self.passed(mode)

    def test_device_loss_preserves_cpu_owners_then_recovers(self):
        self.passed("device-cycle")

    def test_faithful_paths_never_allocate_a_reserve(self):
        self.passed("fenced", public=False)
        for key in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            with self.subTest(key=key):
                self.passed("fenced", env={key: "1", "BEA_PORT_CE_LAYER": "1"})

    def test_resource_and_ownership_mutants_are_causal(self):
        original = SOURCE.read_text()
        cases = [
            ("omit-nearest", "!prepared || level < 0 || level >= 3", "!prepared || level < 1 || level >= 3",
             "allocate", {}),
            ("offer-normal", "!CeTerrainQualityExtended() || ", "", "toggle", {}),
            ("ignore-device", "if (!pool.buffers[i].mVB) return false;", "if (false) return false;", "device-cycle", {}),
            ("leak-partial", "if (!valid) DestroyPools();", "if (!valid) prepared = false;", "failed-create", {}),
            ("skip-release", "++pool->free;", "pool->free += 0;", "release-reset", {}),
            ("eager-allocation", "shaped = CaptureShape(manager);", "shaped = CaptureShape(manager); if (shaped) Prepare();",
             "normal", {}),
            ("retry-every-frame", "if (!attempted) Prepare();", "if (!attempted || !prepared) Prepare();",
             "failed-malloc", {}),
            ("fault-without-diagnostic", "if (!CeTerrainQualityDiagnostics()) return false;", "",
             "fault-inert", {"BEA_PORT_TERRAIN_RESERVE_FAULT": "1:0"}),
            ("fault-ignores-index", "faultLevel == level && faultIndex == i", "faultLevel == level && i == 0",
             "fault", {"BEA_PORT_TERRAIN_QUALITY_LOG": "1", "BEA_PORT_TERRAIN_RESERVE_FAULT": "2:3"}),
            ("no-renewed-attempt", "if (attempted && !prepared && CeTerrainQualityFreshRequest()) attempted = false;", "",
             "failed-malloc", {}),
            ("renew-on-every-frame", "if (attempted && !prepared && CeTerrainQualityFreshRequest()) attempted = false;",
             "if (attempted && !prepared) attempted = false;", "failed-malloc", {}),
            ("fault-attempt-limit-ignored", "if (faultAttempts && prepareAttempts > faultAttempts) faultLevel = -1;", "",
             "fault-retry", {"BEA_PORT_TERRAIN_QUALITY_LOG": "1", "BEA_PORT_TERRAIN_RESERVE_FAULT": "2:3:1"}),
        ]
        for name, old, new, mode, env in cases:
            with self.subTest(name=name):
                self.assertEqual(original.count(old), 1)
                source = self.out / (name + ".cpp")
                source.write_text(original.replace(old, new))
                p = self.run_case(mode, env=env, binary=self.build(name, source=source))
                self.assertEqual(p.returncode, 1)
                self.assertIn("Assertion failed in authored reserve driver", p.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
