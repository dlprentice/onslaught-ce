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
 using CeCinemaRoom::InputDevice;
 check(!CeCinemaRoom::UpdatePadActive(true,false,true,false),"disconnected pad clears active pad mode");
 check(CeCinemaRoom::UpdatePadActive(false,true,true,false),"connected pad input activates pad mode");
 check(!CeCinemaRoom::UpdatePadActive(true,true,false,true),"kbm input switches back to kbm mode");
 check(CeCinemaRoom::UpdatePadActive(true,true,false,false)&&!CeCinemaRoom::UpdatePadActive(false,true,false,false),"idle preserves active device");
 const InputDevice padFirst[2]={InputDevice::Pad,InputDevice::Kbm};
 const InputDevice padOnly[2]={InputDevice::Pad,InputDevice::None};
 const InputDevice kbmFirst[2]={InputDevice::Kbm,InputDevice::Pad};
 check(CeCinemaRoom::PickFireSlot(padFirst,false)==1,"kbm skips pad in slot 0 and picks kbm slot 1");
 check(CeCinemaRoom::PickFireSlot(padOnly,false)==-1,"kbm never displays pad button when only pad is bound");
 check(CeCinemaRoom::PickFireSlot(kbmFirst,true)==1,"active pad picks pad slot 1 over kbm slot 0");
 check(CeCinemaRoom::PickFireSlot(padFirst,true)==0,"active pad picks pad slot 0");
 check(CeCinemaRoom::StepHouseLight(0.5f,0.0f,30,true)==0.5f,"paused game holds house lights constant");
 check(CeCinemaRoom::StepHouseLight(1.0f,0.0f,45,false)==0.0f,"playing dims house lights to 0 over 45 frames");
 check(CeCinemaRoom::StepHouseLight(0.0f,1.0f,45,false)==1.0f,"stopping brightens house lights back to 1 over 45 frames");
 std::printf("%u/%u authored circulation, seating and action checks passed\n",total-failed,total);
 return failed?1:0;
}
"""
if __name__=='__main__':
 cinema=(ROOT/'port/game/ce_museum_cinema.inc').read_text()
 if 'Through this doorway: use the projector to watch game films.' not in cinema:
  raise SystemExit('FAIL: expected doorway copy missing in ce_museum_cinema.inc')
 if 'lastAim' in cinema or 'PortMouseActivitySeq()' not in cinema:
  raise SystemExit('FAIL: CinemaTrackDevice must use PortMouseActivitySeq without aim-dot heuristic')
 hall=(ROOT/'port/game/ce_museum_hall.cpp').read_text()
 if 'HouseLightReset();' not in hall or 'StepHouseLight(gHouseLight, target, steps, GAME.IsPaused()' not in hall:
  raise SystemExit('FAIL: CeMuseumHallClear must reset house lights and HouseLight must hold while paused')
 if 'jambLo' not in cinema or 'headerLo = 5.06f' not in cinema or '(kBack - kFront) * 0.5f - 0.32f' not in cinema:
  raise SystemExit('FAIL: Screening Room acoustic linings must trim cleanly outside Portal 6 jambs and proscenium wall')
 if 'g.current = kMatCinemaSconce;' not in cinema or 'g.current = kMatMuspellGlow;' in cinema:
  raise SystemExit('FAIL: Screening Room wall sconces must use kMatCinemaSconce so they dim during film playback')
 if 'Point(a,bottom,kCurtainValleyLow),Point(a,top,kCurtainValleyHigh)' not in cinema:
  raise SystemExit('FAIL: Screening Room curtain fold quads must face (0, -1, 0) into the room')
 if 'kConsoleX + 0.13f' not in cinema or '1.11f,.18f' not in cinema:
  raise SystemExit('FAIL: Cinema console pedestal must center under casing (kConsoleX+0.13) with cap flush at z=1.29')
 if 'const float z = ground + kScreenZ;' not in cinema:
  raise SystemExit('FAIL: CinemaScreen must offset screen Z by ground floor elevation')
 panel=(ROOT/'port/game/ce_museum_cinema_panel.inc').read_text()
 if 'LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);' not in panel or 'LT.SetModulate(0);' not in panel:
  raise SystemExit('FAIL: CinemaPanelDraw must restore D3D9 depth/cull/alpha/modulate render states on exit')
 out=ROOT/'local-data'/'museum-cinema-tests';out.mkdir(exist_ok=True,parents=True)
 with tempfile.TemporaryDirectory(dir=out) as root:
  d=Path(root);(d/'test.cpp').write_text(TEST)
  subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I',str(ROOT/'port/game'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
  raise SystemExit(subprocess.run([str(d/'test')]).returncode)
