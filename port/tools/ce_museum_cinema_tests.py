#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the cinema doorway, seated rows, usable viewing zone and clear routes on authored coordinates."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
TEST=r"""
#include "ce_museum_world.h"
#include <cstdio>
using namespace CeCinemaRoom;
bool walk(float side,float depth){return CeMuseumWorld::Inside(CeMuseumWorld::kEntranceX+side,CeMuseumWorld::kEntranceY+depth);}
unsigned total,failed;
void check(bool ok,const char *why){++total;if(!ok){++failed;std::fprintf(stderr,"FAIL: %s\n",why);}}
int main(){
 check(!walk(kDoorX,kDoorY-kDoorHalf)&&!walk(kDoorX,kDoorY+kDoorHalf),"solid jambs");
 check(!walk(kDoorX,12)&&!walk(kDoorX,16),"no passage through uncut wall");
 check(!walk(kRight,kDoorY)&&!walk(kScreenX,kBack)&&!walk(kScreenX,kFront),"cannot leave through outer room");
 for(int y=-2;y<=2;++y)for(int i=0;i<=140;++i)
   check(walk(kDoorX-4.0f+i*.1f,kDoorY+y*.5f),"usable door passage both directions");
 for(int i=0;i<=90;++i)check(walk(kScreenX,kFront+.60f+i*.12f),"unobstructed central aisle from back row to console");
 check(Console(kScreenX,kDoorY)&&!Console(kDoorX-9.0f,kDoorY)&&!Console(kRight-1,17),"spatially scoped play control");
 for(unsigned row=0;row<3;++row)for(unsigned n=0;n<6;++n){
   float x=SeatX(n),y=1.0f-row*1.60f;
   check(Seat(x,y)&&!walk(x,y),"each physical seat has its own obstruction");
 }
 check(walk(0,0)&&walk(CeMuseumWorld::kHalfWidth-1.0f,8.5f)&&walk(0,CeMuseumWorld::kBack-1.0f)&&!walk(-CeMuseumWorld::kHalfWidth,0),"entrance, hall beside the portal, back of the nave and opposite wall");
 bool held=false,captured=false;using CeCinemaRoom::FireIntent;
 auto edge=[&](bool up,bool in){return CeCinemaRoom::FireEdge(up,in,held,captured);};
 check(edge(false,false)==FireIntent::Ordinary,"ordinary press");
 check(edge(false,true)==FireIntent::Ordinary,"held across the doorway");
 check(edge(true,true)==FireIntent::Ordinary&&!held&&!captured,"held release never plays");
 check(edge(true,true)==FireIntent::Ordinary,"unpaired release cannot start");
 check(edge(false,true)==FireIntent::Consume&&held&&captured,"fresh console press");
 check(edge(false,true)==FireIntent::Consume,"repeated charge does not start");
 check(edge(true,true)==FireIntent::Operate&&!held&&!captured,"one release operates");
 check(edge(true,true)==FireIntent::Ordinary,"second release cannot operate");
 check(edge(false,true)==FireIntent::Consume&&edge(true,false)==FireIntent::Consume,"leave or pause after press swallows only its own release");
 check(edge(false,false)==FireIntent::Ordinary&&edge(true,false)==FireIntent::Ordinary,"ordinary controls restored");
 std::printf("%u/%u authored circulation, seating and action checks passed\n",total-failed,total);
 return failed?1:0;
}
"""
if __name__=='__main__':
 out=ROOT/'local-data'/'museum-cinema-tests';out.mkdir(exist_ok=True,parents=True)
 with tempfile.TemporaryDirectory(dir=out) as root:
  d=Path(root);(d/'test.cpp').write_text(TEST)
  subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I',str(ROOT/'port/game'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
  raise SystemExit(subprocess.run([str(d/'test')]).returncode)
