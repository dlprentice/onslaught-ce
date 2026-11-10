// SPDX-License-Identifier: GPL-3.0-only
// The checks of ce_playunit_tests.py that run the play-as-a-unit rules (port/game/ce_playunit_rules.h) as compiled code. It includes the header as it is (or as a
// mutant of it) and prints one line per check: "ok NAME" or "FAIL NAME: what", and "checks N failed M" at the end. No game object is used.
#if defined(CE_PLAYUNIT_CAMERA_CONTROL)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// The module's camera adapter run against stand-ins for the game's classes. FMatrix is faithful to the game's maths header in the one way the adapter depends on: its constructors take
// rows (so GetColumnX() is the first number of each row), checked once by compiling a probe against that header.
struct FVector {
    float X, Y, Z;
    FVector(float x=0, float y=0, float z=0): X(x), Y(y), Z(z) {}
    FVector operator+(const FVector& v) const { return FVector(X+v.X,Y+v.Y,Z+v.Z); }
    FVector operator-(const FVector& v) const { return FVector(X-v.X,Y-v.Y,Z-v.Z); }
    FVector operator*(float s) const { return FVector(X*s,Y*s,Z*s); }
};
static float Dot(const FVector& a, const FVector& b) { return a.X*b.X+a.Y*b.Y+a.Z*b.Z; }
static FVector Cross(const FVector& a, const FVector& b) {
    return FVector(a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X);
}
static FVector Unit(const FVector& p) { return p*(1.f/std::sqrt(Dot(p,p))); }
struct FMatrix;
static FMatrix Columns(const FVector& x, const FVector& y, const FVector& z);
struct FMatrix {
    FVector Row[3];
    FMatrix(): Row{FVector(1,0,0),FVector(0,1,0),FVector(0,0,1)} {}
    FMatrix(const FVector& a, const FVector& b, const FVector& c): Row{a,b,c} {}
    FVector GetColumnX() const { return FVector(Row[0].X,Row[1].X,Row[2].X); }
    FVector GetColumnY() const { return FVector(Row[0].Y,Row[1].Y,Row[2].Y); }
    FVector GetColumnZ() const { return FVector(Row[0].Z,Row[1].Z,Row[2].Z); }
    FVector operator*(const FVector& p) const { return FVector(Dot(Row[0],p),Dot(Row[1],p),Dot(Row[2],p)); }
    FMatrix operator*(const FMatrix& m) const { return Columns(*this*m.GetColumnX(),*this*m.GetColumnY(),*this*m.GetColumnZ()); }
    FMatrix Transpose() const { return FMatrix(GetColumnX(),GetColumnY(),GetColumnZ()); }
};
static FMatrix Columns(const FVector& x, const FVector& y, const FVector& z) {
    return FMatrix(FVector(x.X,y.X,z.X),FVector(x.Y,y.Y,z.Y),FVector(x.Z,y.Z,z.Z));
}
// The game's yaw turns the forward axis (0,1,0) to (-sin, cos, 0) and its pitch to (0, cos, sin): the world's z axis points down, so a positive pitch looks down.
static FMatrix Yaw(float r) {
    return Columns(FVector(std::cos(r),std::sin(r),0),FVector(-std::sin(r),std::cos(r),0),FVector(0,0,1));
}
static FMatrix Pitch(float r) {
    return Columns(FVector(1,0,0),FVector(0,std::cos(r),std::sin(r)),FVector(0,-std::sin(r),std::cos(r)));
}
static FMatrix Roll(float r) {
    return Columns(FVector(std::cos(r),0,std::sin(r)),FVector(0,1,0),FVector(-std::sin(r),0,std::cos(r)));
}
class CThing {
public:
    FVector rawPos, oldPos, shownPos;
    FMatrix rawOri, oldOri, shownOri;
    FVector GetPos() const { return rawPos; }
    FVector GetOldPos() const { return oldPos; }
    FVector GetRenderPos() const { return shownPos; }
    FMatrix GetOrientation() const { return rawOri; }
    FMatrix GetOldOrientation() const { return oldOri; }
    FMatrix GetRenderOrientation() const { return shownOri; }
};
class CUnit: public CThing {
public:
    bool dying=false;
    bool IsDying() const { return dying; }
};
class CCamera {
public:
    virtual ~CCamera() {}
    virtual FVector GetPos()=0;
    virtual FMatrix GetOrientation()=0;
    virtual FVector GetOldPos() { return GetPos(); }
    virtual FMatrix GetOldOrientation() { return GetOrientation(); }
    virtual float GetZoom() { return .8f; }
};
class CGenericCamera: public CCamera {
public:
    FVector mPos; FMatrix mOrientation; float mZoom=1.f;
    FVector GetPos() override { return mPos; }
    FMatrix GetOrientation() override { return mOrientation; }
    float GetZoom() override { return mZoom; }
};
// A camera above and behind the unit that looks at it. The game's own camera also does things the unit's pose does not decide (on a slope it tilted by up to 7 degrees more than
// the unit did, in steps of an update); extraPitch, extraRoll and extraBack stand for that: set between frames they are a step of the game's camera alone.
static const FVector kFixtureOffset(0,-6,-3);
static FMatrix NativeView(const FVector& delta, const FMatrix& unit) {
    const FVector forward=Unit(delta);
    const FVector right=Unit(Cross(forward,unit.GetColumnZ()));
    return Columns(right,forward,Cross(right,forward));
}
class CThing3rdPersonCamera: public CCamera {
    CUnit *target;
public:
    float extraPitch=0, extraRoll=0, extraBack=0;
    explicit CThing3rdPersonCamera(CUnit *u): target(u) {}
    CThing *GetThing() { return target; }
    FVector GetPos() override { return target->rawPos+target->rawOri*(kFixtureOffset+FVector(0,-extraBack,0)); }
    FVector GetOldPos() override { return target->oldPos+target->oldOri*kFixtureOffset; }
    FMatrix GetOrientation() override { return NativeView(target->rawPos-GetPos(),target->rawOri)*Pitch(extraPitch)*Roll(extraRoll); }
    FMatrix GetOldOrientation() override { return NativeView(target->oldPos-(target->oldPos+target->oldOri*kFixtureOffset),target->oldOri); }
};
class CPlayer {};
class CeTankCamera;
static CeTankCamera *gCamera=nullptr;
static CUnit *gUnit=nullptr, *gHeld=nullptr;
static bool gDriving=false, gAllowed=true;
static int gLevel=27;
struct Game {
    CCamera *camera=nullptr; CPlayer *primary=nullptr; int level=27;
    CCamera *GetCurrentCamera(int) { return camera; }
    CPlayer *GetPlayer(int n) { return n==0 ? primary : nullptr; }
    int GetCurrentLevel() { return level; }
} GAME;
static bool Allowed(bool enabled) { return enabled && gAllowed; }
static CUnit *Held() { return gHeld; }
// The frame counter and the clock the adapter reads: a test moves them by hand, one frame at a time.
static unsigned gPresentNow=0;
static unsigned long long gClockNs=1000000000ull;
extern "C" unsigned PortPresentCount(void) { return gPresentNow; }
bool PortClockVirtual() { return false; }
unsigned long long PortClockRawNs() { return gClockNs; }
static void NextFrame(double ms) { ++gPresentNow; gClockNs+=(unsigned long long)(ms*1e6); }
#include "ce_playunit_view.h"
#include "ce_playunit_camera_class.inc"
#include "ce_playunit_view_state.inc"
#include "ce_playunit_view_adapter.inc"
#include "ce_playunit_render_adapter.inc"
static unsigned failures=0, checks=0;
static void CheckCamera(bool good,const char *label) {
    ++checks; if(!good) {++failures;std::printf("FAIL %s\n",label);}
}
static bool NearCamera(FVector a,FVector b,float eps=.00002f) {
    return std::fabs(a.X-b.X)<eps && std::fabs(a.Y-b.Y)<eps && std::fabs(a.Z-b.Z)<eps;
}
static bool NearCamera(FMatrix a,FMatrix b,float eps=.00002f) {
    return NearCamera(a.GetColumnX(),b.GetColumnX(),eps) && NearCamera(a.GetColumnY(),b.GetColumnY(),eps) && NearCamera(a.GetColumnZ(),b.GetColumnZ(),eps);
}
// The six numbers of a camera as the player's frame shows it, against the unit's displayed pose.
static CeUnitView Shown(CCamera *camera,const CUnit& unit) {
    const FVector right=camera->GetOrientation().GetColumnX(), forward=camera->GetOrientation().GetColumnY(), offset=camera->GetPos()-unit.GetRenderPos();
    const FVector heading=unit.GetRenderOrientation().GetColumnY();
    const float r[3]={right.X,right.Y,right.Z}, f[3]={forward.X,forward.Y,forward.Z}, o[3]={offset.X,offset.Y,offset.Z};
    CeUnitView v; std::memset(&v,0,sizeof v);
    if(!CeUnitViewDescribe(r,f,o,std::atan2(heading.Y,heading.X),&v)) std::printf("FAIL the shown camera has no heading\n");
    return v;
}
// The angle between two directions, from the sine and the cosine: the arccosine alone cannot see an angle under a milliradian in single precision.
static float Angle(const FVector& a,const FVector& b) {
    const FVector c=Cross(a,b);
    return std::atan2(std::sqrt(Dot(c,c)),Dot(a,b));
}
// A unit that turns on level ground at `rate` radians a second, moving ahead at 2 units a second, displayed between its last two updates as the game does (an update is 1/20 s).
struct Course {
    CUnit *unit; float yaw=0, rate=0; unsigned phase=0; FVector pos=FVector(0,0,0);
    void Start(CUnit *u,float y,float r) { unit=u; yaw=y; rate=r; phase=0; pos=FVector(3,5,-2); u->oldOri=u->rawOri=u->shownOri=Yaw(y); u->oldPos=u->rawPos=u->shownPos=pos; }
    // one frame of `ms` milliseconds: the unit's displayed pose moves by that much, and an update happens every 50 ms of them
    void Advance(double ms) {
        const float dt=float(ms)*.001f;
        yaw+=rate*dt; pos=pos+FVector(-std::sin(yaw),std::cos(yaw),0)*(2.f*dt);
        unit->shownOri=Yaw(yaw); unit->shownPos=pos;
        if(++phase*ms>=50.0) { phase=0; unit->oldOri=unit->rawOri; unit->oldPos=unit->rawPos; unit->rawOri=unit->shownOri; unit->rawPos=unit->shownPos; }
    }
};
// The camera as the player's frame shows it, once for this frame.
static CCamera *Frame(CCamera *logical,CPlayer *player,double ms) { NextFrame(ms); return PortCePlayUnitRenderCamera(logical,player); }
// What the rigid mode shows for the same state of the game (it keeps nothing between frames, so asking changes nothing).
static void RigidOf(CCamera *logical,CPlayer *player,FVector *pos,FMatrix *ori) {
    const int mode=gViewMode; gViewMode=kViewRigid;
    CCamera *c=PortCePlayUnitRenderCamera(logical,player); *pos=c->GetPos(); *ori=c->GetOrientation();
    gViewMode=mode;
}
static void Smoothing(CUnit& unit, CPlayer& player) {
    CeTankCamera logical(&unit); gCamera=&logical;
    GAME.camera=&logical; GAME.primary=&player; gDriving=true; gUnit=gHeld=&unit; gAllowed=true;
    Course course;
    // level ground, turning: the displayed camera is the rigid one on every frame, and the turn is not smoothed
    ViewReset(); gViewMode=kViewSmooth;
    course.Start(&unit,.4f,1.5f);
    float worst=0, worstYaw=0;
    for(unsigned i=0;i<240;++i) {
        course.Advance(1000.0/60);
        FVector rigidPos; FMatrix rigidOri;
        RigidOf(&logical,&player,&rigidPos,&rigidOri);
        CCamera *shown=Frame(&logical,&player,1000.0/60);
        const FVector d=shown->GetPos()-rigidPos;
        worst=std::fmax(worst,std::sqrt(Dot(d,d)));
        worst=std::fmax(worst,Angle(shown->GetOrientation().GetColumnY(),rigidOri.GetColumnY()));
        worst=std::fmax(worst,Angle(shown->GetOrientation().GetColumnX(),rigidOri.GetColumnX()));
        worstYaw=std::fmax(worstYaw,std::fabs(Shown(shown,unit).yaw));
    }
    CheckCamera(worst<.0002f,"on level ground the smoothed camera is the rigid one, turning included");
    CheckCamera(worstYaw<.0002f,"the camera's heading is the unit's displayed heading plus the game's own, nothing more");
    // a step of the game's own camera in roll (9 degrees at once, as the frame-feel log showed on a slope) is spread over the frames that follow
    for(double ms: {1000.0/30,1000.0/60,1000.0/144,1000.0/360}) {
        ViewReset(); course.Start(&unit,.9f,0.f); logical.extraRoll=0; logical.extraPitch=0;
        for(unsigned i=0;i<20;++i) { course.Advance(ms); Frame(&logical,&player,ms); }
        CeUnitView before=Shown(PortCePlayUnitRenderCamera(&logical,&player),unit);
        logical.extraRoll=.157f;
        double t=0; float biggest=0, last=Shown(PortCePlayUnitRenderCamera(&logical,&player),unit).roll, at150=0, at600=0, over=0;
        FVector previousRight=PortCePlayUnitRenderCamera(&logical,&player)->GetOrientation().GetColumnX();
        while(t<1500) {
            course.Advance(ms); CCamera *c=Frame(&logical,&player,ms); t+=ms;
            const CeUnitView v=Shown(c,unit);
            biggest=std::fmax(biggest,Angle(previousRight,c->GetOrientation().GetColumnX()));
            previousRight=c->GetOrientation().GetColumnX();
            if(t<=150) at150=v.roll-before.roll;
            if(t<=600) at600=v.roll-before.roll;
            over=std::fmax(over,v.roll-before.roll-.157f);
            last=v.roll;
        }
        // the step is 9 degrees; the most a frame may turn the camera is what a critically damped spring at 10 a second allows (0.58 rad/s) at this frame time, with a little for rounding
        CheckCamera(biggest<float(.0105*ms/ (1000.0/60.0))+.0002f && biggest>0.f,"a 9 degree step of the game's camera moves the shown one by about half a degree a frame at most");
        CheckCamera(at150>.157f*.3f && at150<.157f*.75f,"a step is part-way after 150 ms");
        CheckCamera(at600>.157f*.9f,"a step is nearly done after 600 ms");
        CheckCamera(over<.0005f,"a step is not overshot");
        CheckCamera(std::fabs(last-before.roll-.157f)<.0003f,"a step is finished in the end");
    }
    // the same step at 30, 60, 144 and 360 frames a second ends the same: the settling is exact for any frame time
    float ends[4]; int k=0;
    for(double ms: {1000.0/30,1000.0/60,1000.0/144,1000.0/360}) {
        ViewReset(); course.Start(&unit,-.3f,0.f); logical.extraRoll=0; logical.extraPitch=0;
        for(unsigned i=0;i<10;++i) { course.Advance(ms); Frame(&logical,&player,ms); }
        logical.extraPitch=-.12f;
        double t=0; CCamera *c=nullptr;
        while(t<400-1e-6) { course.Advance(ms); c=Frame(&logical,&player,ms); t+=ms; }
        ends[k++]=Shown(c,unit).pitch;
    }
    CheckCamera(std::fabs(ends[0]-ends[3])<.003f && std::fabs(ends[1]-ends[3])<.003f && std::fabs(ends[2]-ends[3])<.003f,"the same step at 30, 60, 144 and 360 frames a second ends in the same place");
    // a frame's other views take the camera the first one used, and do not move it on
    ViewReset(); course.Start(&unit,.2f,0.f); logical.extraRoll=0; logical.extraPitch=0;
    for(unsigned i=0;i<10;++i) { course.Advance(1000.0/60); Frame(&logical,&player,1000.0/60); }
    logical.extraRoll=.1f; course.Advance(1000.0/60);
    CCamera *first=Frame(&logical,&player,1000.0/60);
    const FVector firstPos=first->GetPos(); const FMatrix firstOri=first->GetOrientation();
    gClockNs+=2000000ull;
    CCamera *second=PortCePlayUnitRenderCamera(&logical,&player);
    CheckCamera(NearCamera(second->GetPos(),firstPos,1e-6f) && NearCamera(second->GetOrientation(),firstOri,1e-6f),"a frame's second view is shown the camera the first one used");
    // the settling starts afresh at the game's camera on a new possession, after a gap, and when the game's camera is somewhere the ground could not have put it
    ViewReset(); course.Start(&unit,.2f,0.f); logical.extraRoll=.1f; logical.extraPitch=0;
    course.Advance(1000.0/60); CCamera *fresh=Frame(&logical,&player,1000.0/60);
    // (the displayed camera is one object: what a test wants of it is copied before anything asks for another)
    const FVector freshPos=fresh->GetPos(); const FMatrix freshOri=fresh->GetOrientation();
    FVector rigidPos; FMatrix rigidOri;
    RigidOf(&logical,&player,&rigidPos,&rigidOri);
    CheckCamera(NearCamera(freshOri,rigidOri,1e-5f) && NearCamera(freshPos,rigidPos,1e-4f),"a new possession shows the game's camera at once");
    logical.extraRoll=0; ViewReset(); course.Start(&unit,.2f,0.f);
    for(unsigned i=0;i<10;++i) { course.Advance(1000.0/60); Frame(&logical,&player,1000.0/60); }
    logical.extraRoll=.2f; course.Advance(500.0); CCamera *gap=Frame(&logical,&player,500.0);
    CheckCamera(std::fabs(Shown(gap,unit).roll-.2f)<.0003f,"a gap of half a second between two frames shows the game's camera at once");
    logical.extraRoll=0; course.Advance(1000.0/60); Frame(&logical,&player,1000.0/60);
    logical.extraPitch=-1.f; course.Advance(1000.0/60); CCamera *jump=Frame(&logical,&player,1000.0/60);
    const FMatrix jumpOri=jump->GetOrientation();
    RigidOf(&logical,&player,&rigidPos,&rigidOri);
    CheckCamera(NearCamera(jumpOri,rigidOri,1e-5f),"a tilt the ground could not have made is shown as it is");
    logical.extraPitch=0;
    // the other two modes: the game's own camera, and the camera fixed to the displayed pose
    ViewReset(); course.Start(&unit,.5f,0.f); course.Advance(1000.0/60);
    gViewMode=kViewStock; CheckCamera(Frame(&logical,&player,1000.0/60)==&logical,"the stock mode shows the game's own camera");
    gViewMode=kViewRigid; logical.extraRoll=.1f; CCamera *r1=Frame(&logical,&player,1000.0/60);
    const FMatrix before=r1->GetOrientation(); logical.extraRoll=.25f; CCamera *r2=Frame(&logical,&player,1000.0/60);
    CheckCamera(!NearCamera(before,r2->GetOrientation(),1e-3f),"the rigid mode shows each step of the game's camera at once");
    gViewMode=kViewSmooth; logical.extraRoll=0;
    gDriving=false; gCamera=nullptr; GAME.camera=nullptr;
}
// The six numbers and the camera they make are each other's inverse, for any heading, a tilt of a quarter turn either way and offsets of any size.
static void RoundTrip() {
    unsigned bad=0, tried=0;
    for(float unitYaw: {-3.f,-1.2f,0.f,.7f,2.9f}) for(float yaw: {-.8f,0.f,.5f}) for(float pitch: {-1.2f,-.4f,0.f,.45f,1.1f}) for(float roll: {-.9f,-.1f,0.f,.2f,.8f}) {
        const CeUnitView in={yaw,pitch,roll,-5.f+yaw,1.5f*roll,3.f-pitch};
        float r[3],f[3],o[3]; CeUnitViewBuild(in,unitYaw,r,f,o);
        CeUnitView out; ++tried;
        if(!CeUnitViewDescribe(r,f,o,unitYaw,&out)) { ++bad; continue; }
        if(std::fabs(CeUnitViewWrap(out.yaw-in.yaw))>2e-5f || std::fabs(out.pitch-in.pitch)>2e-5f || std::fabs(out.roll-in.roll)>2e-5f ||
           std::fabs(out.along-in.along)>2e-5f || std::fabs(out.side-in.side)>2e-5f || std::fabs(out.down-in.down)>2e-5f) ++bad;
        // the axes are unit length and at right angles
        if(std::fabs(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]-1.f)>2e-5f || std::fabs(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]-1.f)>2e-5f || std::fabs(r[0]*f[0]+r[1]*f[1]+r[2]*f[2])>2e-5f) ++bad;
    }
    CheckCamera(tried==375 && bad==0,"the six numbers of a camera and the camera they make are each other's inverse");
    // a camera looking straight down, or with a number that is not a number, has no six numbers
    const float up[3]={0,0,1}, right[3]={1,0,0}, zero[3]={0,0,0}; CeUnitView v;
    CheckCamera(!CeUnitViewDescribe(right,up,zero,0.f,&v),"a camera looking straight down has no heading");
    const float nan[3]={NAN,0,0}, flat[3]={0,1,0};
    CheckCamera(!CeUnitViewDescribe(right,nan,zero,0.f,&v) && !CeUnitViewDescribe(right,flat,nan,0.f,&v),"a number that is not a number gives no camera");
    CheckCamera(CeUnitViewWrap(INFINITY)==0.f && CeUnitViewWrap(-INFINITY)==0.f && CeUnitViewWrap(NAN)==0.f && std::fabs(CeUnitViewWrap(7.f)-(7.f-6.2831853f))<1e-5f,"an angle that is not finite wraps to zero, and others to within half a turn");
}
int main() {
    CUnit unit, otherUnit;
    CPlayer player, otherPlayer;
    CGenericCamera otherView;
    GAME.primary=&player;
    RoundTrip();
    unit.rawPos=FVector(4,-8,3); unit.oldPos=FVector(-2,3,-4);
    unit.oldOri=Yaw(-1.1f)*Pitch(.4f);
    for(int mode: {kViewSmooth,kViewRigid})
    for(unsigned scenario=0;scenario<4;++scenario) {
        gViewMode=mode; ViewReset();
        unit.rawOri=Yaw(.6f*float(scenario))*Pitch(.1f);
        unit.shownOri=Yaw(.3f*float(scenario))*Pitch(-.2f);
        unit.shownPos=FVector(-1+float(scenario),7,-2);
        if(scenario==0) {unit.shownPos=unit.rawPos;unit.shownOri=unit.rawOri;}
        const CUnit before=unit;
        {
            CeTankCamera logical(&unit); gCamera=&logical;
            GAME.camera=&logical; gDriving=true; gUnit=gHeld=&unit;
            const FVector logicalPos=logical.GetPos();
            const FMatrix logicalView=logical.GetOrientation();
            const FVector expectedPos=unit.shownPos+unit.shownOri*kFixtureOffset;
            const FMatrix expectedView=NativeView(unit.shownPos-expectedPos,unit.shownOri);
            NextFrame(16.7);
            CCamera *shown=PortCePlayUnitRenderCamera(&logical,&player);
            CheckCamera(shown!=&logical,"separate presentation camera");
            CheckCamera(NearCamera(shown->GetPos(),expectedPos),"position follows displayed pose");
            CheckCamera(NearCamera(shown->GetOrientation(),expectedView),"view preserves framing in displayed pose");
            CheckCamera(NearCamera(shown->GetOldPos(),expectedPos),"old pose does not repeat interpolation");
            CheckCamera(NearCamera(shown->GetOldOrientation(),expectedView),"old orientation does not repeat interpolation");
            CheckCamera(shown->GetZoom()==logical.GetZoom(),"native zoom preserved");
            for(float fraction: {0.f,.17f,.5f,.99f,1.f}) {
                const FVector displayed=shown->GetOldPos()*(1-fraction)+shown->GetPos()*fraction;
                CheckCamera(NearCamera(displayed,expectedPos),"render fraction cannot move an already displayed pose backward");
            }
            CheckCamera(GAME.camera==&logical && NearCamera(logical.GetPos(),logicalPos) && NearCamera(logical.GetOrientation(),logicalView),"logical camera is unchanged");
            CheckCamera(NearCamera(unit.rawPos,before.rawPos) && NearCamera(unit.rawOri,before.rawOri) && NearCamera(unit.shownPos,before.shownPos) && NearCamera(unit.shownOri,before.shownOri) && unit.dying==before.dying,"unit is unchanged");
            CheckCamera(PortCePlayUnitRenderCamera(&otherView,&player)==&otherView,"existing Commander or foreign view preserved");
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&otherPlayer)==&logical,"other player refused");
            GAME.camera=&otherView;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"borrowed logical camera refused");
            GAME.camera=&logical; gDriving=false;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"released possession returns original camera");
            gDriving=true; gAllowed=false;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"faithful or disabled mode refused");
            gAllowed=true; GAME.level=28;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"another level refused");
            GAME.level=27; gHeld=nullptr;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"removed unit refused");
            gHeld=&otherUnit;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"replacement unit refused");
            gHeld=&unit; unit.dying=true;
            CheckCamera(PortCePlayUnitRenderCamera(&logical,&player)==&logical,"dying unit refused");
            unit.dying=false;
        }
        CheckCamera(gCamera==nullptr,"tracked camera destructor clears notification");
    }
    gViewMode=kViewSmooth;
    Smoothing(unit,player);
    CeTankCamera retained(&unit);gCamera=&retained;
    {CeTankCamera unrelated(&unit);}
    CheckCamera(gCamera==&retained,"unrelated destruction preserves current camera");
    std::printf("camera checks=%u failures=%u\n",checks,failures);
    return failures ? 2 : 0;
}
#elif !defined(CE_PLAYUNIT_GUIDE_CONTROL)
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ce_playunit_rules.h"

static int gChecks, gFailed;

static void Check(bool good, const char *name, const char *what = "")
{
	++gChecks;
	if (good)
	{
		printf("ok %s\n", name);
		return;
	}
	++gFailed;
	printf("FAIL %s: %s\n", name, what);
}

static bool Near(float a, float b, float eps = 1e-4f) { return fabsf(a - b) <= eps; }

static CePlayUnitGate Baseline()
{
	CePlayUnitGate g;
	memset(&g, 0, sizeof(g));
	g.optionOn = true;
	g.ceLayer = true;
	g.players = 1;
	return g;
}

static CePlayUnitFacts Tank()
{
	CePlayUnitFacts f;
	memset(&f, 0, sizeof(f));
	f.friendly = f.alive = f.vehicle = f.groundUnit = f.normalSquad = f.armed = true;
	return f;
}

static void Gate()
{
	Check(CePlayUnitAllowed(Baseline()), "gate: the plain game with the option on is allowed");
	{ CePlayUnitGate g = Baseline(); g.optionOn = false; Check(!CePlayUnitAllowed(g), "gate: the option off is refused"); }
	{ CePlayUnitGate g = Baseline(); g.ceLayer = false; Check(!CePlayUnitAllowed(g), "gate: no CE layer is refused"); }
	{ CePlayUnitGate g = Baseline(); g.recording = true; Check(!CePlayUnitAllowed(g), "gate: a recording is refused"); }
	{ CePlayUnitGate g = Baseline(); g.fencedEnv = true; Check(!CePlayUnitAllowed(g), "gate: a fenced variable is refused"); }
	{ CePlayUnitGate g = Baseline(); g.networkBoot = true; Check(!CePlayUnitAllowed(g), "gate: a network session is refused"); }
	{ CePlayUnitGate g = Baseline(); g.demo = true; Check(!CePlayUnitAllowed(g), "gate: a demo or test mode is refused"); }
	{ CePlayUnitGate g = Baseline(); g.multiplayer = true; Check(!CePlayUnitAllowed(g), "gate: a multiplayer level is refused"); }
	{ CePlayUnitGate g = Baseline(); g.players = 2; Check(!CePlayUnitAllowed(g), "gate: two players are refused"); }
	{ CePlayUnitGate g = Baseline(); g.players = 0; Check(!CePlayUnitAllowed(g), "gate: no player is refused"); }
	// The proof switch lifts the three fences of replay and recording, and no other.
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.ceLayer = false; g.recording = true; g.fencedEnv = true; Check(CePlayUnitAllowed(g), "gate: the proof switch lifts the replay fences"); }
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.optionOn = false; Check(!CePlayUnitAllowed(g), "gate: the proof switch does not turn the option on"); }
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.networkBoot = true; Check(!CePlayUnitAllowed(g), "gate: the proof switch does not lift the network fence"); }
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.demo = true; Check(!CePlayUnitAllowed(g), "gate: the proof switch does not lift the demo fence"); }
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.multiplayer = true; Check(!CePlayUnitAllowed(g), "gate: the proof switch does not lift the multiplayer fence"); }
	{ CePlayUnitGate g = Baseline(); g.proof = true; g.players = 2; Check(!CePlayUnitAllowed(g), "gate: the proof switch does not lift the single-player fence"); }
}

static void Eligibility()
{
	Check(CePlayUnitTankEligible(Tank()), "eligible: a friendly armed tank in a normal squad");
	{ CePlayUnitFacts f = Tank(); f.friendly = false; Check(!CePlayUnitTankEligible(f), "eligible: an enemy is refused"); }
	{ CePlayUnitFacts f = Tank(); f.alive = false; Check(!CePlayUnitTankEligible(f), "eligible: a dying unit is refused"); }
	{ CePlayUnitFacts f = Tank(); f.vehicle = false; Check(!CePlayUnitTankEligible(f), "eligible: a non-vehicle is refused"); }
	{ CePlayUnitFacts f = Tank(); f.groundUnit = false; Check(!CePlayUnitTankEligible(f), "eligible: a jeep, mech or aircraft is refused"); }
	{ CePlayUnitFacts f = Tank(); f.scripted = true; Check(!CePlayUnitTankEligible(f), "eligible: a unit a mission script runs is refused"); }
	{ CePlayUnitFacts f = Tank(); f.objective = true; Check(!CePlayUnitTankEligible(f), "eligible: an objective is refused"); }
	{ CePlayUnitFacts f = Tank(); f.deploys = true; Check(!CePlayUnitTankEligible(f), "eligible: artillery is refused"); }
	{ CePlayUnitFacts f = Tank(); f.normalSquad = false; Check(!CePlayUnitTankEligible(f), "eligible: a unit outside a normal squad is refused"); }
	{ CePlayUnitFacts f = Tank(); f.spawned = true; Check(!CePlayUnitTankEligible(f), "eligible: a squad that follows its lead unit is refused"); }
	{ CePlayUnitFacts f = Tank(); f.armed = false; Check(!CePlayUnitTankEligible(f), "eligible: an unarmed unit is refused"); }
}

static void Angles()
{
	Check(Near(CePlayUnitClamp(2.f, -1.f, 1.f), 1.f) && Near(CePlayUnitClamp(-2.f, -1.f, 1.f), -1.f) && Near(CePlayUnitClamp(0.25f, -1.f, 1.f), 0.25f), "clamp: limits");
	Check(Near(CePlayUnitClamp(NAN, -1.f, 1.f), 0.f), "clamp: a number that is not one is no input");
	Check(Near(CePlayUnitWrap(7.f), 7.f - 2.f * 3.14159265f) && Near(CePlayUnitWrap(-7.f), -7.f + 2.f * 3.14159265f), "wrap: into one turn");
	Check(Near(CePlayUnitWrap(3.f), 3.f) && Near(CePlayUnitWrap(-3.f), -3.f), "wrap: leaves a small angle alone");
	Check(Near(CePlayUnitWrap(INFINITY), 0.f), "wrap: an infinite angle is none");

	// Right is clockwise from above, which lowers the angle; left raises it.
	Check(Near(CePlayUnitSteer(0.f, 0.f, 1.f, 0.1f, 1.f, 1.f), -0.1f), "steer: right lowers the heading");
	Check(Near(CePlayUnitSteer(0.f, 0.f, -1.f, 0.1f, 1.f, 1.f), 0.1f), "steer: left raises the heading");
	Check(Near(CePlayUnitSteer(0.f, 0.f, 0.5f, 0.1f, 2.f, 1.f), -0.1f), "steer: half deflection at twice the rate");
	Check(Near(CePlayUnitSteer(0.3f, 0.3f, 0.f, 0.1f, 1.f, 1.f), 0.3f), "steer: no input keeps the heading");
	Check(Near(CePlayUnitSteer(0.f, 0.f, 3.f, 0.1f, 1.f, 1.f), -0.1f), "steer: deflection is limited to full");
	// The request never leads the unit's heading by more than maxLead.
	Check(Near(CePlayUnitSteer(2.f, 0.f, 0.f, 0.f, 1.f, 1.2f), 1.2f) && Near(CePlayUnitSteer(-2.f, 0.f, 0.f, 0.f, 1.f, 1.2f), -1.2f), "steer: the request leads by at most maxLead");
	Check(Near(CePlayUnitSteer(1.1f, 0.f, 1.f, 0.1f, 1.f, 1.2f), 1.0f), "steer: within maxLead the request moves freely");
	// Across the half turn the answer stays in range.
	Check(Near(CePlayUnitSteer(3.1f, 3.1f, -1.f, 0.1f, 1.f, 1.2f), 3.2f - 2.f * 3.14159265f), "steer: wraps past a half turn");
	Check(Near(CePlayUnitSteer(0.f, 0.f, NAN, 0.1f, 1.f, 1.f), 0.f), "steer: a deflection that is not a number is none");
	Check(Near(CePlayUnitSteer(0.f, 0.f, 1.f, -5.f, 1.f, 1.f), 0.f) && Near(CePlayUnitSteer(0.f, 0.f, 1.f, NAN, 1.f, 1.f), 0.f), "steer: a negative or unknown time step turns nothing");
}

static void Points()
{
	float tx, ty;
	Check(CePlayUnitPoint(10.f, 20.f, 0.f, 40.f, &tx, &ty) && Near(tx, 50.f) && Near(ty, 20.f), "point: along the x axis");
	Check(CePlayUnitPoint(10.f, 20.f, 3.14159265f / 2.f, 40.f, &tx, &ty) && Near(tx, 10.f, 1e-3f) && Near(ty, 60.f), "point: along the y axis");
	Check(CePlayUnitPoint(0.f, 0.f, -3.14159265f / 4.f, 10.f, &tx, &ty) && tx > 7.f && ty < -7.f, "point: clockwise of the x axis is south-east");
	Check(!CePlayUnitPoint(10.f, 20.f, 0.f, 0.f, &tx, &ty) && Near(tx, 10.f) && Near(ty, 20.f), "point: no reach, no point (the unit's own place)");
	Check(!CePlayUnitPoint(NAN, 20.f, 0.f, 40.f, &tx, &ty) && !CePlayUnitPoint(10.f, 20.f, NAN, 40.f, &tx, &ty), "point: numbers that are not finite give none");
}

static void Reissue()
{
	Check(CePlayUnitReissue(true, 10, 10, 6, 100.f, 8.f), "reissue: the wishes changed");
	Check(CePlayUnitReissue(false, 16, 10, 6, 100.f, 8.f), "reissue: the interval is over");
	Check(!CePlayUnitReissue(false, 15, 10, 6, 100.f, 8.f), "reissue: not before the interval is over");
	Check(CePlayUnitReissue(false, 11, 10, 6, 3.f, 8.f), "reissue: the unit is near the last point");
	Check(!CePlayUnitReissue(false, 11, 10, 6, 30.f, 8.f), "reissue: nothing asks for one");
}

static void Handback()
{
	const CePlayUnitReturnFacts normal = {true, true, true, false, true, true, true};
	Check(CePlayUnitChooseReturn(normal) == CE_PLAYUNIT_RETURN_SQUAD, "return: live original squad resumes ownership");
	{ auto f = normal; f.sameLevel = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_GONE, "return: another level cannot inherit a stale lease"); }
	{ auto f = normal; f.unitLive = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_GONE, "return: dead or removed unit is not written"); }
	{ auto f = normal; f.sameTeam = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_EXTERNAL, "return: a changed side is preserved"); }
	{ auto f = normal; f.currentSquad = true; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_EXTERNAL, "return: a newer squad assignment wins"); }
	{ auto f = normal; f.aiStillOwned = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_EXTERNAL, "return: resumed AI orders win"); }
	{ auto f = normal; f.originalSquadLive = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_INDEPENDENT, "return: missing original squad uses independent fallback"); }
	{ auto f = normal; f.originalSquadCompatible = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_INDEPENDENT, "return: original squad on another side is not rejoined"); }
	{ auto f = normal; f.unitLive = false; f.currentSquad = true; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_GONE, "return: disappearance precedes ownership checks"); }
	{ auto f = normal; f.currentSquad = true; f.originalSquadLive = false; Check(CePlayUnitChooseReturn(f) == CE_PLAYUNIT_RETURN_EXTERNAL, "return: a newer owner precedes the orphan fallback"); }
}

int main()
{
	Handback();
	Gate();
	Eligibility();
	Angles();
	Points();
	Reissue();
	printf("checks %d failed %d\n", gChecks, gFailed);
	return gFailed ? 1 : 0;
}

#else

// Exercise the guide-command region extracted from the production module.
// The fixture freezes physical position so it measures command bookkeeping,
// not native movement, collision, camera behavior or how control feels.

#include "ce_playunit_rules.h"
#include <cmath>
#include <cstdio>
#include <vector>
#define FALSE 0
struct FVector { float X,Y,Z; FVector(float x=0,float y=0,float z=0):X(x),Y(y),Z(z){} };
struct Orientation { FVector GetColumnY() const { return FVector(1,0,0); } };
struct Clock { float time; float GetTime() const { return time; } } EVENT_MANAGER;
unsigned gTick,gLastIssue; float gDesired,gLastAngle,gLastTime; bool gMoving,gHaveTime; FVector gLastPoint;
struct Unit {
    unsigned calls=0; float issued=0; std::vector<unsigned> ticks;
    FVector GetPos() const { return FVector(); }
    Orientation GetOrientation() const { return Orientation(); }
    void GoToPoint(FVector p,int) { ++calls; issued=atan2f(p.Y,p.X); ticks.push_back(gTick); }
};
FVector Ground(float x,float y) { return FVector(x,y,0); }
void ResetFixture() {gTick=gLastIssue=0;gDesired=gLastAngle=gLastTime=0;gMoving=gHaveTime=false;gLastPoint=FVector();EVENT_MANAGER.time=0;}

#include "ce_playunit_guide_constants.inc"
void Step(Unit *u, CePlayUnitDrive in) {
#include "ce_playunit_drive_region.inc"
}

int main() {
 unsigned total=0;
 for(int hz: {30,60,120}) {
  ResetFixture();Unit unit;unsigned stale=0;float largest=0;
  for(unsigned frame=1;frame<=unsigned(hz/2);++frame) {
   ++gTick;EVENT_MANAGER.time=float(frame)/hz;
   CePlayUnitDrive input={1,0,1,false,false}; Step(&unit,input);
   float gap=fabsf(CePlayUnitWrap(gDesired-unit.issued));
   if(gap>0.05001f)++stale;
   if(gap>largest)largest=gap;
  }
  printf("{\"fps\":%d,\"stale_frames\":%u,\"commands\":%u,\"largest_heading_gap\":%.9f}\n",hz,stale,unit.calls,largest);
  total+=stale;
 }
 ResetFixture();Unit straight;
 for(unsigned frame=1;frame<=25;++frame) {++gTick;EVENT_MANAGER.time=float(frame)/60;CePlayUnitDrive input={1,0,0,false,false};Step(&straight,input);}
 if(straight.ticks!=std::vector<unsigned>({1,7,13,19,25}))return 3;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;CePlayUnitDrive stop={0,0,0,false,false};Step(&straight,stop);
 unsigned stopped=straight.calls;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;Step(&straight,stop);if(straight.calls!=stopped)return 4;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;CePlayUnitDrive restart={1,0,0,false,false};Step(&straight,restart);if(straight.calls!=stopped+1)return 5;
 printf("{\"straight_schedule\":true,\"single_stop\":true,\"restart\":true}\n");
 return total?2:0;
}

#endif
