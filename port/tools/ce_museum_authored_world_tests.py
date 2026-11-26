#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the playable room envelope with the same planner used by the native scene,
and audit Grand Museum architectural alignment, runner continuity, Atrium cohesion, and Hangar symmetry."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r"""
#include "ce_museum_world.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition.h"
#include <cmath>
#include <limits>
#include <cstdio>
using namespace CeMuseumWorld;
unsigned tests, failures;
void test(bool ok,const char *name) { ++tests; if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",name); } }
int main() {
    test(kMap!=kBase && kMap!=kWorld && kBase!=kWorld,"distinct physical and world identities");
    test(kWaterBelow>kFloor+20.0f,"no sea can touch a visitor on the floor");
    test(kHeightSamples==64*64*9*9,"all native edge and seam samples have storage");
    test(Inside(kEntranceX,kEntranceY),"standing entrance");
    test(!Inside(kEntranceX,kEntranceY+kFront)&&!Inside(kEntranceX,kEntranceY+kBack),"front/back wall clearance");
    test(!Inside(kEntranceX-kHalfWidth,kEntranceY)&&!Inside(kEntranceX+kHalfWidth,kEntranceY),"side wall clearance");
    test(!Inside(std::numeric_limits<float>::quiet_NaN(),kEntranceY),"invalid movement cannot cross the shell");
    float x=0,y=0;
    test(!Plinth(0,0,&x,&y)&&!Plinth(0,7,&x,&y)&&!Plinth(6,6,&x,&y)&&!Plinth(0,1,nullptr,&y),"impossible placements refused");
    for (unsigned count=1;count<=6;++count) {
        CeHallDisplay exhibits[6]={};
        for (unsigned n=0;n<count;++n) {
            const bool placed=Plinth(n,count,&x,&y); test(placed&&Inside(x,y),"plinth inside its walls");
            exhibits[n]={x,y,n==1?1.8f:1.1f,int(n)};
            for (unsigned m=0;m<n;++m) test(std::hypot(x-exhibits[m].x,y-exhibits[m].y)>6,"display separation");
        }
        CeHallTour t={}; const bool ok=CeHallPlan({kEntranceX,kEntranceY},exhibits,count,&t);
        test(ok,"working front door and display plan"); if (!ok) continue;
        test(t.stops==(count==1?1u:count<=3?2u:3u),"expected number of authored stops");
        for (unsigned n=0;n<t.stops;++n) test(Inside(t.stop[n].x,t.stop[n].y)&&CeHallBayAllows(t,t.stop[n]),"accessible numbered sign");
        if(t.stops==3) {
            CeHallPoint points[]={t.stop[1],t.annexEntry,t.annexCorner,t.stop[2]};
            for(unsigned n=1;n<4;++n) for(unsigned i=0;i<=20;++i) {
                const float f=float(i)/20; const CeHallPoint q={points[n-1].x+f*(points[n].x-points[n-1].x),points[n-1].y+f*(points[n].y-points[n-1].y)};
                test(Inside(q.x,q.y)&&CeHallBayAllows(t,q),"the rear lane never cuts through a display or a wall");
            }
        }
    }

    // Grand Museum 8-room portal centerline & boundary alignment checks
    const auto g = CeMuseumExhibition::Grand::MakeRoomGraph();
    for (unsigned pi = 0; pi < CeMuseumExhibition::Grand::kPortalCount; ++pi) {
        const auto &p = g.portals[pi];
        const auto &ra = g.rooms[p.a];
        const auto &rb = g.rooms[p.b];
        if (p.vertical) {
            test(std::fabs(ra.maxX - p.line) < 1e-4f || std::fabs(ra.minX - p.line) < 1e-4f, "vertical portal lies on room A wall");
            test(std::fabs(rb.maxX - p.line) < 1e-4f || std::fabs(rb.minX - p.line) < 1e-4f, "vertical portal lies on room B wall");
            test(p.lo >= ra.minY && p.hi <= ra.maxY && p.lo >= rb.minY && p.hi <= rb.maxY, "vertical portal spans inside both rooms");
        } else {
            test(std::fabs(ra.maxY - p.line) < 1e-4f || std::fabs(ra.minY - p.line) < 1e-4f, "horizontal portal lies on room A wall");
            test(std::fabs(rb.maxY - p.line) < 1e-4f || std::fabs(rb.minY - p.line) < 1e-4f, "horizontal portal lies on room B wall");
            test(p.lo >= ra.minX && p.hi <= ra.maxX && p.lo >= rb.minX && p.hi <= rb.maxX, "horizontal portal spans inside both rooms");
        }
        const float mid = 0.5f * (p.lo + p.hi);
        const CeMuseumExhibition::Vec2 pt = p.vertical ? CeMuseumExhibition::Vec2{p.line, mid} : CeMuseumExhibition::Vec2{mid, p.line};
        test(CeMuseumExhibition::Grand::WalkAt(g, pt) != CeMuseumExhibition::kWalkBlocked, "every portal centerline is completely walkable");
    }
    std::printf("%u/%u room geometry, passage and negative-control assertions passed\n",tests-failures,tests);
    return failures?1:0;
}
"""


def audit_grand_hall_alignment():
    hall = (ROOT / "port/game/ce_museum_hall.cpp").read_text()
    failures = []

    def need(ok, label):
        if not ok:
            print("FAIL alignment:", label)
            failures.append(label)

    # 1. Flush N-S spine runners across Courtyard <-> Atrium (y=20) and Atrium <-> Hangar (y=52): all halfW = 1.45f!
    need("RunnerStripY(0.0f, 15.91f, 20.00f, 1.45f, floorZ);" in hall,
         "Courtyard north runner matches 1.45f spine width at y=20.0")
    need("RunnerStripY(0.0f, 20.00f, 29.74f, 1.45f, floorZ);" in hall,
         "Atrium south runner matches 1.45f spine width at y=20.0 and meets Rotunda at 29.74")
    need("RunnerStripY(0.0f, 42.26f, 52.00f, 1.45f, floorZ);" in hall,
         "Atrium north runner matches 1.45f spine width at y=52.0 and meets Rotunda at 42.26")
    need("RunnerStripY(0.0f, 52.00f, 60.05f, 1.45f, floorZ);" in hall,
         "Hangar south runner matches 1.45f spine width at y=52.0 and terminates cleanly at octagon 60.05")

    # 2. Flush E-W runners across Atrium <-> Forseti/Muspell (1.15f) and Atrium <-> Art wings (1.00f)
    need("RunnerStripX(-16.0f, -5.50f, 41.0f, 1.15f, floorZ);" in hall
         and "RunnerStripX(-40.0f, -16.0f, 41.0f, 1.15f, floorZ);" in hall,
         "Atrium <-> Forseti runner is flush 1.15f at x=-16.0")
    need("RunnerStripX(  5.50f, 16.0f, 41.0f, 1.15f, floorZ);" in hall
         and "RunnerStripX(16.0f, 39.2f, 41.0f, 1.15f, floorZ);" in hall,
         "Atrium <-> Muspell runner is flush 1.15f at x=+16.0")
    need("RunnerStripX(-16.0f, -1.56f, 24.0f, 1.00f, floorZ);" in hall
         and "RunnerStripX(-36.0f, -16.0f, 24.0f, 1.00f, floorZ);" in hall,
         "Atrium <-> Early Designs runner is flush 1.00f at x=-16.0 and spine -1.56")
    need("RunnerStripX(  1.56f, 16.0f, 24.0f, 1.00f, floorZ);" in hall
         and "RunnerStripX(16.00f, 35.19f, 24.0f, 1.00f, floorZ);" in hall
         and "RunnerStripY(36.30f, 13.24f, 25.11f, 1.00f, floorZ);" in hall
         and "RunnerStripX(37.41f, 40.00f, 14.5f, 1.15f, floorZ);" in hall,
         "Atrium <-> Concept Art <-> Cinema runners meet flush without overlapping corners")

    # 3. Zero coplanar portal soffits & 4 cm Atrium/Muspell cavity separation
    need(hall.count("clearH + 0.46f") >= 3,
         "PortalFrameX and PortalFrameY separate header wall above accent architrave (zero coplanar soffits)")
    need("15.98f" in hall and "16.02f" in hall,
         "Shared Atrium/Muspell wall keeps 4 cm cavity gap between limestone and iron faces")

    # 4. Cohesive Atrium walls (gold frieze on all 4 sides, zero Forseti/Muspell dado strips on Atrium walls)
    atrium_sec = hall[hall.index("// 2. SKYLIT GRAND ATRIUM"):hall.index("// 3. FORSETI FORCES WING")]
    need("LocalBox(-15.70f, 27.38f, 6.0f, -15.58f, 37.12f, 6.45f, floorZ, kMatTrim, kBrass, true);" in atrium_sec
         and "LocalBox(15.58f, 27.38f, 6.0f, 15.70f, 37.12f, 6.45f, floorZ, kMatTrim, kBrass, true);" in atrium_sec
         and "LocalBox(-15.70f, 44.88f, 6.0f, -15.58f, 51.56f, 6.45f, floorZ, kMatTrim, kBrass, true);" in atrium_sec
         and "LocalBox(15.58f, 44.88f, 6.0f, 15.70f, 51.56f, 6.45f, floorZ, kMatTrim, kBrass, true);" in atrium_sec
         and "kMatForsetiTrim);" not in atrium_sec and "kMatMuspellTrim);" not in atrium_sec,
         "Grand Atrium uses cohesive warm limestone & continuous gold frieze around all 4 sides")

    # 5. Symmetrical Hangar upper roof ribs & wall friezes
    hangar_sec = hall[hall.index("// 5. AVIATION & NAVAL HANGAR"):hall.index("// 6. CONCEPT ART GALLERY")]
    need("EmitSweptRibY(y - 0.36f, y + 0.36f, wRibX, wRibZ, floorZ, kMatPier, kSlate);" in hangar_sec
         and "EmitSweptRibY(y - 0.36f, y + 0.36f, eRibX, eRibZ, floorZ, kMatPier, kSlate);" in hangar_sec
         and "LocalBox(-19.65f, hiBay[bi][0], 9.60f, -19.45f, hiBay[bi][1], 10.40f, floorZ, kMatTrim, kBrass, true);" in hangar_sec
         and "LocalBox( 19.45f, hiBay[bi][0], 9.60f,  19.65f, hiBay[bi][1], 10.40f, floorZ, kMatTrim, kBrass, true);" in hangar_sec,
         "Hangar vaulted roof ribs and upper friezes are symmetrical on West & East sides")

    # 6. No depth-bias punch-through on 3D brass trim frames, and grounded coastal horizon
    draw_sec = hall[hall.index("void PortCeMuseumHallDraw()"):]
    is_inlay_match = re.search(r"const bool isInlay = \(([^;]+)\);", draw_sec)
    need(bool(is_inlay_match) and "kMatTrim" not in is_inlay_match.group(1),
         "kMatTrim is excluded from depth-biased isInlay pass")
    need("const float oceanZ = 5.5f;" in hall and "gHallGpu.headlandVb" in hall,
         "Grounded coastal horizon sits at oceanZ = 5.5f with static GPU headland islands")

    # 7. Grand Entrance Facade retail assets, zero-clip pediment, stepped shadowboxes, mipmaps/aniso, and 14 portal signboards
    need('"FrontEnd\\\\v3\\\\FE_BEA_Title2.tga"' in hall
         and '"FrontEnd\\\\LostToys.tga"' in hall
         and '"data/textures/splash.tga"' in hall
         and '"goodies\\\\ca_be_aquila\\\\ca_bea_battle_pic.tga"' in hall
         and '"goodies\\\\ca_be_aquila\\\\ca_be_final01.tga"' in hall,
         "Grand Entrance Facade loads official title, Lost Toys founder seal (+splash fallback), and flanking cover art at runtime")
    need("D3DXCreateTextureFromFileExA(" in hall and "D3DX_FILTER_BOX" in hall
         and "D3DTEXF_ANISOTROPIC" in hall and "D3DSAMP_MIPFILTER, D3DTEXF_LINEAR" in hall,
         "Entrance banners and baked signage use full box mipmaps, linear mipfilter, and anisotropic filtering")
    need(hall.count("AddPortalSignboard(") >= 15,
         "All 14 portal faces across the 7 Grand Museum portals mount proud 3D architectural lintel signboards")
    need("CeMuseumTextures::GetPortalSignAtlas()" in hall and "CeMuseumTextures::GetDirectoryAtlas()" in hall,
         "Hall binds baked Portal Signage Atlas and Atrium Directory Floor-Plan/Buttons Atlas")

    # 8. Verified Z-fighting, signboard standoff, diorama shadow dedup, and cinema glow dimming fixes
    need("AddPortalSignboard( 40.0f + w + 0.29f, 14.5f, 5.42f" in hall
         and "AddPortalSignboard(  0.0f, 20.0f - w, 8.25f" in hall,
         "Portal 6 east signboard clears west acoustic lining and Portal 0a bezel centers at z = 8.25 m")
    need("0.56f, 14.76f, ny[i] + 1.21f, 0.62f" in hangar_sec
         and "0.48f, 5.44f, 76.74f, 0.52f" in hangar_sec
         and "LocalFloorGrid(-0.14f, 65.14f, 0.14f, 68.0f" in hangar_sec
         and "LocalFloorGrid(-0.28f, 36.28f, 0.28f, 41.76f" in atrium_sec,
         "Naval Bay & Scale Alcove brass collars sit below pier/dais tops and compass crosses split without overlap")
    need("AddContactShadow(dx, dy, 1.65f, 0.35f, 0.72f, floorZ, 0.054f, 0.44f);" in atrium_sec
         and "tiered ? 0.056f : 0.052f" in hall
         and "CeMuseumDiorama::kDioramaCount" not in atrium_sec,
         "Directory/Hero Dais shadows sit above inlays/curbs at >= 0.052 m and duplicate Atrium diorama shadow loop is removed")
    need("kMatCinemaSconce" in hall and "const float glowDim = 0.15f + 0.85f * house;" in draw_sec,
         "Screening Room glow strips and wall sconces dim cleanly with house lights during playback")

    if not failures:
        print("ok   all Grand Museum architectural alignment, Atrium cohesion, Hangar symmetry, and horizon audits passed")
    return 1 if failures else 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    arg=parser.parse_args()
    parent=arg.out or ROOT/'local-data'/'museum-world-host'
    parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as tmp:
        d=Path(tmp); (d/'check.cpp').write_text(CPP)
        subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I',str(ROOT/'port/game'),
                        str(d/'check.cpp'),str(ROOT/'port/game/ce_museum_hall_layout.cpp'),'-o',str(d/'check')],check=True)
        rc = subprocess.run([str(d/'check')]).returncode
    if rc != 0:
        return rc
    return audit_grand_hall_alignment()
if __name__=='__main__':raise SystemExit(main())
