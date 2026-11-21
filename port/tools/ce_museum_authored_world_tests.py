#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the playable room envelope with the same planner used by the native scene."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r"""
#include "ce_museum_world.h"
#include "ce_museum_hall.h"
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
    std::printf("%u/%u room geometry, passage and negative-control assertions passed\n",tests-failures,tests);
    return failures?1:0;
}
"""

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
        return subprocess.run([str(d/'check')]).returncode
if __name__=='__main__':raise SystemExit(main())
