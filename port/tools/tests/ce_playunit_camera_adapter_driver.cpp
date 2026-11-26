// SPDX-License-Identifier: GPL-3.0-only
// Authored host world: production adapter text is substituted by the runner.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "ce_playunit_camera.h"
#include "ce_playunit_look.h"

struct FVector {
    float X, Y, Z;
    FVector(float x=0, float y=0, float z=0):X(x),Y(y),Z(z) {}
};
struct CBoundingBox { FVector mOrigin, mAxes; bool mValid; };
struct CUnit {
    CBoundingBox box; float scale; int reads; bool missing; FVector pos;
    CUnit():scale(1),reads(0),missing(false) {
        box.mOrigin=FVector(0,0,-1); box.mAxes=FVector(.25f,.35f,1); box.mValid=true;
    }
    CBoundingBox *GetBoundingBox() { ++reads; return missing ? NULL : &box; }
    float GetRenderScale() const { return scale; }
    FVector GetPos() const { return pos; }
};
const int kViewLook=1;
bool gDriving=false, gOnFoot=true, gFootFirstPerson=false;
struct { bool enabled = false; } gSpotter;
static void CePlayUnitSpotterReset(void *) {}
int gViewMode=kViewLook;
float gLookYaw=0, gLookPitch=0;
void *gFootRenderedView=NULL;
CUnit *held=NULL;
CUnit *Held() { return held; }
struct Game { bool paused; bool IsPaused() const { return paused; } } GAME={false};
unsigned long long frame=1, ns=1000000;
unsigned long long PortPresentCount() { return frame; }
unsigned long long ViewNs() { return ns; }
FVector ClampUnitViewGround(FVector eye);
int clampCalls=0, notices=0;
char notice[64]={}, tipText[128]={}, morph[32]="X";
void UnitViewHelp(char *out,int n) { std::snprintf(out,n,"%s: Return",morph); }
void CePlayUnitNotice(const char *name,const char *tip,bool) {
    ++notices; std::snprintf(notice,sizeof(notice),"%s",name);
    std::snprintf(tipText,sizeof(tipText),"%s",tip);
}

@ADAPTER@

FVector ClampUnitViewGround(FVector eye) { ++clampCalls; return eye; }
void Zoom(CUnit *u,bool zoomIn,bool zoomOut) {
@ZOOM@
}
FVector FootAimEye(const FVector &place) { return FVector(place.X,place.Y,place.Z-9); }
struct Camera {
    FVector GetPos() const {
        CePlayUnitCameraPose p;
        if (!held || !UnitViewPose(held->GetPos(),p)) return FVector();
        return FVector(p.eye[0],p.eye[1],p.eye[2]);
    }
} camera;
Camera *gCamera=&camera;
FVector FireEye(CUnit *u) { return @FIRE_EYE@; }

static int checks=0, failures=0;
static void check(bool ok,const char *name) {
    ++checks; if(!ok) { ++failures; std::printf("FAIL %s\n",name); }
}
static bool near(float a,float b,float eps=1e-5f) { return std::fabs(a-b)<eps; }
static bool same(const FVector &a,const FVector &b) {
    return near(a.X,b.X)&&near(a.Y,b.Y)&&near(a.Z,b.Z);
}
static CePlayUnitCameraPose tick(int n=1) {
    CePlayUnitCameraPose p={};
    for(int i=0;i<n;++i) { ++frame; ns+=16666667; check(UnitViewPose(held->pos,p),"valid pose"); }
    return p;
}
static void acquire(CUnit &unit,bool foot) {
    ResetUnitView(); held=&unit; gDriving=true; gOnFoot=foot;
    gFootFirstPerson=false; GAME.paused=false; gViewMode=kViewLook;
    gLookYaw=gLookPitch=0;
}
int main() {
    CUnit foot,other,tank;
    held=&foot;
    check(!MakeUnitView(held)&&foot.reads==0,"ordinary owner does no bounds work");
    gDriving=true; gViewMode=0;
    check(!MakeUnitView(held)&&foot.reads==0,"legacy view does no bounds work");
    gViewMode=kViewLook;
    check(!MakeUnitView(NULL),"missing unit refused");
    foot.missing=true; check(!MakeUnitView(held),"missing box retains fallback");
    foot.missing=false; foot.box.mValid=false;
    check(!MakeUnitView(held),"invalid box retains fallback");
    foot.box.mValid=true; foot.scale=0;
    check(!MakeUnitView(held),"zero scale retains fallback");
    foot.scale=1; check(MakeUnitView(held),"native bounds accepted");
    check(gUnitViewState.view==kCePlayUnitCameraWide,"foot starts wide, whole in the picture");
    check(gUnitViewRig.firstPerson,"foot gets an eye capability");
    check(near(gUnitViewRig.eye[2],-1.86f),"eye derives from full height");
    int reads=foot.reads; check(MakeUnitView(held)&&foot.reads==reads,"same owner caches bounds");
    const float span=gUnitViewRig.span;
    Zoom(held,true,false); tick(75);
    check(gUnitViewState.view==kCePlayUnitCameraClose&&!gFootFirstPerson,"one zoom in reaches close");
    CePlayUnitCameraPose p=tick();
    check(!p.hideBody&&!UnitViewHidesBody(),"close body remains visible");
    check(same(FireEye(held),gCamera->GetPos()),"close fire uses camera origin");
    Zoom(held,true,false);
    check(gUnitViewState.view==kCePlayUnitCameraFirst&&gFootFirstPerson,"bound zoom selects first");
    check(!UnitViewHidesBody(),"requested first does not hide at old shoulder");
    check(std::strcmp(notice,"First person")==0&&std::strstr(tipText,"X: Return"),"view notice preserves bound return");
    p=tick(75);
    check(p.hideBody&&UnitViewHidesBody(),"body hides at actual eye clearance");
    check(near(p.eye[2],-1.86f,.001f),"first view reaches model eye");
    check(same(FireEye(held),gCamera->GetPos()),"first fire uses camera origin");
    Zoom(held,false,true);
    check(!gFootFirstPerson&&UnitViewHidesBody(),"body stays hidden while leaving eye");
    p=tick(75); check(!p.hideBody&&!UnitViewHidesBody(),"body returns at shoulder clearance");
    Zoom(held,false,true); check(gUnitViewState.view==kCePlayUnitCameraWide,"second zoom out selects wide");
    p=tick(); const float rail=gUnitViewState.rail; ns+=9000000;
    CePlayUnitCameraPose q={}; check(UnitViewPose(held->pos,q)&&near(gUnitViewState.rail,rail),"repeated camera query does not advance rail");
    GAME.paused=true; p=tick(5); check(near(gUnitViewState.rail,rail),"pause freezes positional transition"); GAME.paused=false;
    p=tick(75); check(p.arm>gUnitViewRig.closeDistance,"wide is farther than close");
    const float beforeYaw=gUnitViewState.rail; gLookYaw=1.1f; gLookPitch=.8f;
    check(UnitViewPose(held->pos,q)&&near(gUnitViewState.rail,beforeYaw),"look does not drive view distance");
    check(std::fabs(q.forward[0]-p.forward[0])>.1f,"look changes immediately within same frame");
    const float oldEye=q.eye[0]; held->pos.X+=5;
    check(UnitViewPose(held->pos,q)&&near(q.eye[0]-oldEye,5),"view follows owner with no world lag");
    held=&other; check(MakeUnitView(held)&&gUnitViewState.identity==reinterpret_cast<size_t>(&other),"owner change resets identity");
    check(gUnitViewState.view==kCePlayUnitCameraWide,"owner change resets selected view");
    ResetUnitView(); check(!gUnitViewMade&&!gUnitViewRig.valid&&!gUnitViewHideBody,"return clears rig and body ownership");
    other.scale=2; check(MakeUnitView(held)&&near(gUnitViewRig.span,2*span),"render scale contributes to extents");
    CePlayUnitLookLaw law=UnitLookLaw(); check(law.pitchUp<-1.2f&&law.pitchDown>1.2f,"infantry may look up and down naturally");
    acquire(tank,false); tank.box.mOrigin=FVector(0,0,-.5f); tank.box.mAxes=FVector(1,1.5f,.5f);
    check(MakeUnitView(held)&&gUnitViewRig.firstPerson,"vehicle with valid bounds gets turret first person");
    check(near(gUnitViewRig.eye[2],-0.90f),"vehicle eye sits near top of turret bounds");
    check(gUnitViewState.view==kCePlayUnitCameraWide,"vehicle starts wide");
    Zoom(held,true,false); check(gUnitViewState.view==kCePlayUnitCameraClose&&!gFootFirstPerson,"vehicle zoom reaches close");
    p=tick(75); check(!p.hideBody&&!UnitViewHidesBody(),"vehicle close view keeps hull visible");
    Zoom(held,true,false); check(gUnitViewState.view==kCePlayUnitCameraFirst&&gFootFirstPerson,"vehicle zoom reaches first person");
    check(!UnitViewHidesBody(),"vehicle first request does not hide before entering hull");
    p=tick(75);
    check(p.hideBody&&UnitViewHidesBody(),"vehicle hides own hull at turret eye");
    check(near(p.eye[2],-0.90f,.001f),"vehicle first view reaches turret eye");
    check(same(FireEye(held),gCamera->GetPos()),"vehicle first fire uses camera origin");
    int count=notices; Zoom(held,true,false);
    check(gUnitViewState.view==kCePlayUnitCameraFirst&&notices==count,"vehicle first person saturates quietly");
    Zoom(held,false,true); check(gUnitViewState.view==kCePlayUnitCameraClose&&!gFootFirstPerson,"vehicle zoom out leaves first person");
    p=tick(75); check(!p.hideBody&&!UnitViewHidesBody(),"vehicle hull returns at close clearance");
    Zoom(held,false,true); check(gUnitViewState.view==kCePlayUnitCameraWide,"vehicle zoom returns wide");
    // Direct toggle remembers third-person view (Wide <-> First and Close <-> First)
    check(CePlayUnitCameraToggleFirst(&gUnitViewState,gUnitViewRig,&gUnitViewThird)==kCePlayUnitCameraFirst,"toggle from wide enters first");
    check(CePlayUnitCameraToggleFirst(&gUnitViewState,gUnitViewRig,&gUnitViewThird)==kCePlayUnitCameraWide,"toggle from first restores wide");
    Zoom(held,true,false);
    check(CePlayUnitCameraToggleFirst(&gUnitViewState,gUnitViewRig,&gUnitViewThird)==kCePlayUnitCameraFirst,"toggle from close enters first");
    check(CePlayUnitCameraToggleFirst(&gUnitViewState,gUnitViewRig,&gUnitViewThird)==kCePlayUnitCameraClose,"toggle from first restores close");
    // A degenerate flat vehicle box with zero height cannot claim first person and saturates at close
    CUnit flat; flat.box.mOrigin=FVector(0,0,0); flat.box.mAxes=FVector(1,1.5f,0);
    acquire(flat,false);
    check(MakeUnitView(held)&&!gUnitViewRig.firstPerson,"flat vehicle without height cannot claim first person");
    Zoom(held,true,false); check(gUnitViewState.view==kCePlayUnitCameraClose,"flat vehicle reaches close");
    count=notices; Zoom(held,true,false);
    check(gUnitViewState.view==kCePlayUnitCameraClose&&notices==count,"flat vehicle saturates quietly at close");
    law=UnitLookLaw(); const CePlayUnitLookLaw original=CePlayUnitTankLook();
    check(near(law.pitchUp,original.pitchUp)&&near(law.pitchDown,original.pitchDown),"tank pitch remains native possession law");
    acquire(foot,true); gViewMode=0;
    check(same(FireEye(held),FootAimEye(held->pos)),"legacy infantry keeps existing fire origin");
    check(clampCalls>0,"native ground seam invoked");
    std::printf("checks=%d failed=%d\n",checks,failures);
    return failures?1:0;
}
