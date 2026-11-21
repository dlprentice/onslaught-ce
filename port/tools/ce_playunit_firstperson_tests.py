#!/usr/bin/env python3
"""Exercise the owning camera filter in normal, first-person and external views."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
MODULE = PORT / "game/ce_playunit.cpp"
SCENES = PORT / "patches/0241-ce-infantry-first-person.patch"


def body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for at in range(brace, len(source)):
        depth += (source[at] == "{") - (source[at] == "}")
        if not depth:
            return source[start:at + 1]
    raise ValueError("incomplete definition")


def source():
    s = MODULE.read_text()
    return body(s, "bool DrawnFromFootEye()") + "\n" + body(s, "bool PortCePlayUnitHidesOwnMesh(")


class FirstPerson(unittest.TestCase):
    def test_the_possessed_body_is_hidden_in_only_its_current_render_camera(self):
        fixture = r'''
#include "ce_playunit_look.h"
struct FVector { float X = 0, Y = 0, Z = 0; };
struct CThing { int placeholder; };
struct CUnit : CThing {};
// the engine draws through a copy of the camera (CInterpolatedCamera): a different object at the same place
struct CCamera { FVector pos; FVector GetPos() { return pos; } };
static bool gDriving, gOnFoot, gFootFirstPerson;
static bool gMirrorPass;
static int gLevel;
static CUnit *gUnit;
static CCamera *gCamera;
static const CCamera *gFootRenderedView;
struct Game {
    CCamera *camera = nullptr;
    int level = 0; bool free = false;
    CCamera *GetCamera(int) {return camera;}
    int GetCurrentLevel() {return level;}
    bool IsFreeCameraOn(int) {return free;}
} GAME;
struct Engine { CCamera *camera = nullptr; CCamera *GetCamera(){return camera;} } ENGINE;
bool gUnitViewMade=false, gUnitViewHideBody=false;
struct { bool valid; } gUnitViewRig = {};
@OWNER@
int main() {
    CUnit soldier, observer;
    CCamera simulation, eye, alternate, drawing;
    eye.pos = {1.f, 2.f, -3.f}; drawing.pos = eye.pos; alternate.pos = {1.f, 2.f, -4.f}; simulation.pos = {0.f, 0.f, 0.f};
    gUnit=&soldier; gCamera=&simulation; gFootRenderedView=&eye;
    GAME.camera=&simulation; ENGINE.camera=&drawing; gLevel=GAME.level=4;
    gOnFoot = gDriving = gFootFirstPerson = true;
    if (!PortCePlayUnitHidesOwnMesh(&soldier) || PortCePlayUnitHidesOwnMesh(&observer)) return 1;
    gMirrorPass=true; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 11;
    gMirrorPass=false;
    ENGINE.camera=&alternate; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 2;
    ENGINE.camera=&drawing; GAME.camera=&alternate; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 3;
    GAME.camera=&simulation; GAME.free=true; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 4;
    GAME.free=false; gDriving=false; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 5;
    gDriving=true; gFootFirstPerson=false; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 6;
    gFootFirstPerson=true; gOnFoot=false; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 7;
    gOnFoot=true; GAME.level++; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 8;
    GAME.level=gLevel; gFootRenderedView=nullptr; if (PortCePlayUnitHidesOwnMesh(&soldier)) return 9;
    const float at[3]={3.f,-3.f,8.f}; float e[3]; CePlayUnitFootFirstEye(at,e);
    if(e[0]!=at[0]||e[1]!=at[1]||!(e[2]<at[2])||fabsf(e[2]-at[2])>2.1f) return 10;
    gUnitViewMade=true; gUnitViewRig.valid=true; gFootFirstPerson=true;
 gUnitViewHideBody=false; if (UnitViewHidesBody()) return 12;
 gUnitViewHideBody=true; if (!UnitViewHidesBody()) return 13;
 gFootFirstPerson=false; if (!UnitViewHidesBody()) return 14;
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            cpp, binary = td / "fixture.cpp", td / "fixture"
            cpp.write_text(fixture.replace("@OWNER@", body(MODULE.read_text(), "bool UnitViewHidesBody()") + "\n" + source()))
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                            "-I", str(PORT / "game"), str(cpp), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    def test_bound_zoom_is_owned_by_the_active_modern_unit_only(self):
        module = MODULE.read_text()
        receive = body(module, "virtual void ReceiveButtonAction(")
        native_actions = body(module, "static bool IsEngineButton(")
        needed = sorted(set(re.findall(r"\bBUTTON_[A-Z_0-9]+", receive + native_actions)))
        fixture = r'''
struct CController {};
struct Player {
    int deliveries=0, action=0;
    void ReceiveButtonAction(CController*, int b, float) {++deliveries;action=b;}
} player;
struct Game {
    Player *GetPlayer(int){return &player;}
} GAME;
using CPlayer=Player;
struct Career {bool GetInvertYAxis(int){return false;}} CAREER;
enum { @ACTIONS@, UNRELATED };
struct {float forward=0,back=0,turn=0,look=0,pitch=0; bool fire=false,leave=false;} gIn;
bool gOnFoot=false,gFootZoomIn=false,gFootZoomOut=false;
int gViewMode=1; const int kViewLook=1;
class Pilot { public: bool mDriving=false;
@RECEIVE@
@NATIVE@
};
int main(){
    Pilot controller;CController pad;
    // The home pilot receives its ordinary native Zoom events.
    controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_IN,1);
    if(player.deliveries!=1 || player.action!=BUTTON_MECH_CHANGE_ZOOM_IN) return 1;
    controller.mDriving=true;
    // A vehicle does not send engine actions to its parked pilot.
    controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_IN,1);
    controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_OUT,1);
    if (player.deliveries!=1 || !gFootZoomIn || !gFootZoomOut) return 2;
 gFootZoomIn=gFootZoomOut=false;
    // A different active camera is not a soldier-look view.
    gOnFoot=true;gViewMode=0;
    controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_IN,1);
    if(player.deliveries!=1 || gFootZoomIn) return 3;
    // In either bound direction only an active input requests a switch.
    gViewMode=kViewLook;controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_IN,0);
    if(gFootZoomIn) return 4;
    controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_IN,1);
    if(!gFootZoomIn || gFootZoomOut || player.deliveries!=1) return 5;
    gFootZoomIn=false;controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_OUT,1);
    if(gFootZoomIn || !gFootZoomOut || player.deliveries!=1) return 6;
    controller.ReceiveButtonAction(&pad,UNRELATED,1);
    if(player.deliveries!=2 || player.action!=UNRELATED) return 7;
    controller.mDriving=false;controller.ReceiveButtonAction(&pad,BUTTON_MECH_CHANGE_ZOOM_OUT,1);
    if(player.deliveries!=3 || player.action!=BUTTON_MECH_CHANGE_ZOOM_OUT) return 8;
    return 0;
}
'''
        fixture = fixture.replace("@ACTIONS@", ",".join(needed)).replace("@RECEIVE@", receive).replace("@NATIVE@", native_actions)
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            cpp, binary = folder / "controls.cpp", folder / "controls"
            cpp.write_text(fixture)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", str(cpp), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    def test_drawing_never_changes_visibility_or_suppresses_shadow_submission(self):
        adds = [l[1:].strip() for l in SCENES.read_text().splitlines() if l.startswith("+") and not l.startswith("+++")]
        self.assertEqual(adds.count("if (PortCePlayUnitHidesOwnMesh(this)) return;"), 2)
        self.assertFalse(any(re.search(r"Flag|Shadow|Delete|Shutdown", l) for l in adds))

    def test_modern_views_use_the_presented_camera_origin_for_the_fire_ray(self):
        src = MODULE.read_text()
        aimed = body(src, "void FireTick(")
        self.assertIn('(gOnFoot && gViewMode != kViewLook) ? FootAimEye(u->GetPos()) : gCamera->GetPos()', aimed)
        self.assertIn("FootAimEye(place)", body(src, "FVector LookEye("))
        self.assertNotIn("gFootRenderedView", body(src, "FVector FootAimEye("))


if __name__ == "__main__":
    unittest.main()
