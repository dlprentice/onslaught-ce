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
static bool gOnFoot=false, gFootFirstPerson=false;
static const CCamera *gFootRenderedView=nullptr;
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
// The look camera (ce_playunit_look.h) is the module's own and is checked by the rules section: here it only marks where the frame would be the look's.
static unsigned gLookShown=0;
static void LookShown(CUnit *, CGenericCamera *) { ++gLookShown; }
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
#include "ce_playunit_look.h"

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

// ---- the class table ----
static bool AnyShape(const CePlayUnitFacts &) { return true; }
static bool NoShape(const CePlayUnitFacts &) { return false; }

static void Table()
{
	const CePlayUnitClassRow *rows = CePlayUnitClasses();
	Check(rows[CE_PLAYUNIT_CLASS_TANK].enabled, "table: the tank row is on");
	Check(!rows[CE_PLAYUNIT_CLASS_JEEP].enabled, "table: the jeep row is off");
	int on = 0;
	for (int i = 0; i < CE_PLAYUNIT_CLASS_COUNT; ++i) on += rows[i].enabled ? 1 : 0;
	Check(on == 2, "only the tank and ordinary infantry rows are enabled");
	CePlayUnitFacts foot = Tank(); foot.vehicle = false; foot.groundUnit = false; foot.normalSquad = false;
	foot.ordinaryFoot = true; foot.family = CE_PLAYUNIT_FAMILY_INFANTRY;
	Check(CePlayUnitClassFor(foot) == CE_PLAYUNIT_CLASS_INFANTRY, "ordinary independent soldier is eligible");
	foot.scripted = true; Check(CePlayUnitClassFor(foot) < 0, "script-owned soldier is retained");
	foot.scripted = false; foot.ordinaryFoot = false;
	Check(CePlayUnitClassFor(foot) < 0, "another foot unit or another assignment is retained");
	Check(CePlayUnitClassFor(Tank()) == CE_PLAYUNIT_CLASS_TANK, "table: a tank is the tank class");
	{ CePlayUnitFacts f = Tank(); f.groundUnit = false; Check(CePlayUnitClassFor(f) == -1 && !CePlayUnitTankEligible(f), "table: a jeep is refused while its row is off"); }
	{ CePlayUnitFacts f = Tank(); f.vehicle = false; Check(CePlayUnitClassFor(f) == -1, "table: a non-vehicle fits no row"); }
	const CePlayUnitLaw &t = rows[CE_PLAYUNIT_CLASS_TANK].law;
	Check(Near(t.reach, 40.f) && Near(t.pivotReach, 6.f) && Near(t.turnRate, 1.4f) && Near(t.maxLead, 1.2f) && Near(t.nearRadius, 8.f) && Near(t.pivotNear, 2.f) && t.reissue == 6u && t.pivotReissue == 2u,
		  "table: the tank's control law is the one measured");
	Check(t.freeFire, "table: the tank has free fire");
	Check(!rows[CE_PLAYUNIT_CLASS_JEEP].law.freeFire, "table: the jeep has no free fire");
	// A row of a test's own, that fits any shape, must not loosen what every class refuses.
	const CePlayUnitClassRow any = {"any", true, AnyShape, rows[CE_PLAYUNIT_CLASS_TANK].law};
	const CePlayUnitClassRow extra[3] = {rows[CE_PLAYUNIT_CLASS_TANK], any, any};		// the third is beyond the two rows the table has
	{ CePlayUnitFacts f = Tank(); f.vehicle = false; Check(CePlayUnitClassIn(extra, 2, f) == 1, "table: a row with a wider shape takes what its shape takes"); }
	for (int row = 0; row < 2; ++row)
	{
		CePlayUnitFacts base = Tank();
		Check(CePlayUnitEligibleIn(extra, 2, row, base), "table: the plain tank fits each row");
		{ CePlayUnitFacts f = base; f.friendly = false; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: an enemy is refused by every row"); }
		{ CePlayUnitFacts f = base; f.alive = false; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: a dying unit is refused by every row"); }
		{ CePlayUnitFacts f = base; f.scripted = true; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: a scripted unit is refused by every row"); }
		{ CePlayUnitFacts f = base; f.objective = true; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: an objective is refused by every row"); }
		{ CePlayUnitFacts f = base; f.deploys = true; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: a deploying unit is refused by every row"); }
		{ CePlayUnitFacts f = base; f.normalSquad = false; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: a unit outside a normal squad is refused by every row"); }
		{ CePlayUnitFacts f = base; f.spawned = true; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: a spawned unit is refused by every row"); }
		{ CePlayUnitFacts f = base; f.armed = false; Check(!CePlayUnitEligibleIn(extra, 2, row, f), "table: an unarmed unit is refused by every row"); }
	}
	CePlayUnitClassRow off[1] = {rows[CE_PLAYUNIT_CLASS_TANK]};
	off[0].enabled = false;
	Check(!CePlayUnitEligibleIn(off, 1, 0, Tank()), "table: a row that is off takes nothing");
	off[0].enabled = true;
	off[0].shape = NoShape;
	Check(!CePlayUnitEligibleIn(off, 1, 0, Tank()), "table: a shape that fits nothing takes nothing");
	off[0].shape = NULL;
	Check(!CePlayUnitEligibleIn(off, 1, 0, Tank()), "table: a row with no shape takes nothing");
	Check(!CePlayUnitEligibleIn(extra, 2, 2, Tank()) && !CePlayUnitEligibleIn(extra, 2, -1, Tank()) && !CePlayUnitEligibleIn(NULL, 0, 0, Tank()), "table: a row that is not there takes nothing");
}

// ---- free fire ----
static float Flat(void *, float, float) { return 0.f; }
static float Slope(void *, float x, float) { return -0.1f * x; }		// the ground rises (z falls) as x grows
static float Ridge(void *, float x, float) { return (x > 4.f && x < 6.f) ? -20.f : 0.f; }
static float Bump(void *, float x, float) { return (x > 4.f && x < 6.f) ? -0.8f : 0.f; }

static void FreeFire()
{
	const CePlayUnitGate base = Baseline();
	Check(CePlayUnitFreeFireAllowed(base, true, false), "free fire: the plain game with the option on is allowed");
	Check(!CePlayUnitFreeFireAllowed(base, false, false), "free fire: a class without it is refused");
	Check(!CePlayUnitFreeFireAllowed(base, true, true), "free fire: open Commander view is refused");
	{ CePlayUnitGate g = base; g.proof = true; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: the proof switch does not lift the replay fences for it"); }
	{ CePlayUnitGate g = base; g.optionOn = false; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: the option off is refused"); }
	{ CePlayUnitGate g = base; g.ceLayer = false; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: no CE layer (a tape, probes or a virtual clock) is refused"); }
	{ CePlayUnitGate g = base; g.recording = true; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: a recording is refused"); }
	{ CePlayUnitGate g = base; g.fencedEnv = true; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: a fenced variable is refused"); }
	{ CePlayUnitGate g = base; g.networkBoot = true; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: a network session is refused"); }
	{ CePlayUnitGate g = base; g.players = 2; Check(!CePlayUnitFreeFireAllowed(g, true, false), "free fire: a second player is refused"); }

	const float down[3] = {0.70710678f, 0.f, 0.70710678f}, eye[3] = {0.f, 0.f, -10.f};
	float hit[3] = {0.f, 0.f, 0.f};
	Check(CePlayUnitViewGround(eye, down, 400.f, 1.5f, Flat, NULL, hit) && Near(hit[0], 10.f, 0.01f) && Near(hit[1], 0.f, 0.01f) && Near(hit[2], 0.f, 0.01f), "view: the ray reaches flat ground where the geometry says");
	Check(CePlayUnitViewGround(eye, down, 400.f, 1.5f, Slope, NULL, hit) && Near(hit[0], 10.f / 1.1f, 0.01f) && Near(hit[2], -0.1f * hit[0], 0.01f), "view: the ray reaches rising ground where the geometry says");
	{ const float up[3] = {0.70710678f, 0.f, -0.70710678f}; Check(!CePlayUnitViewGround(eye, up, 400.f, 1.5f, Flat, NULL, hit), "view: looking at the sky gives no point"); }
	{ const float level[3] = {1.f, 0.f, 0.f}; Check(!CePlayUnitViewGround(eye, level, 400.f, 1.5f, Flat, NULL, hit), "view: a level look gives no point"); }
	Check(!CePlayUnitViewGround(eye, down, 5.f, 1.5f, Flat, NULL, hit), "view: ground beyond the reach gives no point");
	{ const float under[3] = {0.f, 0.f, 2.f}; Check(!CePlayUnitViewGround(under, down, 400.f, 1.5f, Flat, NULL, hit), "view: an eye under the ground gives no point"); }
	{ const float bad[3] = {NAN, 0.f, 1.f}; Check(!CePlayUnitViewGround(eye, bad, 400.f, 1.5f, Flat, NULL, hit), "view: a direction that is not a number gives no point"); }
	Check(!CePlayUnitViewGround(eye, down, 400.f, 0.f, Flat, NULL, hit) && !CePlayUnitViewGround(eye, down, 0.f, 1.5f, Flat, NULL, hit) && !CePlayUnitViewGround(eye, down, 400.f, 1.5f, NULL, NULL, hit), "view: no step, no reach or no map gives no point");
	Check(CePlayUnitViewGround(eye, down, 400.f, 40.f, Flat, NULL, hit) && Near(hit[0], 10.f, 0.01f), "view: a coarse search is narrowed to the same point");

	const float from[3] = {0.f, 0.f, 0.f}, to[3] = {30.f, 40.f, 0.f};
	float c[3];
	Check(CePlayUnitRange(from, to, 5.f, 100.f, c) == CE_PLAYUNIT_RANGE_OK && Near(c[0], 30.f) && Near(c[1], 40.f), "range: a point within reach is kept");
	Check(CePlayUnitRange(from, to, 5.f, 25.f, c) == CE_PLAYUNIT_RANGE_TOO_FAR && Near(c[0], 15.f) && Near(c[1], 20.f) && Near(c[2], 0.f), "range: a far point is clamped to the weapon's range on the same line");
	Check(CePlayUnitRange(from, to, 60.f, 100.f, c) == CE_PLAYUNIT_RANGE_TOO_NEAR && Near(c[0], 36.f) && Near(c[1], 48.f), "range: a near point is clamped to the weapon's minimum on the same line");
	Check(CePlayUnitRange(from, to, 50.f, 50.f, c) == CE_PLAYUNIT_RANGE_OK, "range: the limits are in reach");
	Check(CePlayUnitRange(from, to, 5.f, 0.f, c) == CE_PLAYUNIT_RANGE_NONE && CePlayUnitRange(from, to, 0.f, 0.f, c) == CE_PLAYUNIT_RANGE_NONE && CePlayUnitRange(from, to, 5.f, NAN, c) == CE_PLAYUNIT_RANGE_NONE, "range: no positive range is none");
	Check(CePlayUnitRange(from, to, 90.f, 80.f, c) == CE_PLAYUNIT_RANGE_NONE, "range: a minimum above the maximum is none");
	Check(CePlayUnitRange(from, from, 0.f, 80.f, c) == CE_PLAYUNIT_RANGE_NONE, "range: the unit's own place has no direction");

	const float muzzle[3] = {0.f, 0.f, -1.f}, mark[3] = {10.f, 0.f, 0.f};
	Check(CePlayUnitLineClear(muzzle, mark, 24, 0.6f, Flat, NULL), "line: flat ground is open");
	Check(!CePlayUnitLineClear(muzzle, mark, 24, 0.6f, Ridge, NULL), "line: a ridge across the line blocks it");
	Check(CePlayUnitLineClear(muzzle, mark, 24, 0.6f, Bump, NULL), "line: a rise within the tolerance does not");
	Check(!CePlayUnitLineClear(muzzle, mark, 24, 0.1f, Bump, NULL), "line: a rise past a smaller tolerance does");
	Check(!CePlayUnitLineClear(muzzle, mark, 24, 0.6f, NULL, NULL), "line: no map is not open");

	// the reticle
	const int R = CE_PLAYUNIT_RANGE_OK;
	Check(CePlayUnitFireStateOf(false, true, R, true, true, false, false) == CE_PLAYUNIT_FIRE_NONE, "state: no weapon says nothing");
	Check(CePlayUnitFireStateOf(true, false, R, true, true, false, false) == CE_PLAYUNIT_FIRE_OUT_OF_RANGE, "state: no ground under the view is out of range");
	Check(CePlayUnitFireStateOf(true, true, CE_PLAYUNIT_RANGE_TOO_FAR, true, true, false, false) == CE_PLAYUNIT_FIRE_READY, "state: a far point is fired at the weapon's reach");
	Check(CePlayUnitFireStateOf(true, true, CE_PLAYUNIT_RANGE_TOO_NEAR, true, true, false, false) == CE_PLAYUNIT_FIRE_OUT_OF_RANGE, "state: a near point is out of range");
	Check(CePlayUnitFireStateOf(true, true, CE_PLAYUNIT_RANGE_NONE, true, true, false, false) == CE_PLAYUNIT_FIRE_OUT_OF_RANGE, "state: no range is out of range");
	Check(CePlayUnitFireStateOf(true, true, R, false, true, false, false) == CE_PLAYUNIT_FIRE_BLOCKED, "state: a blocked line says so");
	Check(CePlayUnitFireStateOf(true, true, CE_PLAYUNIT_RANGE_TOO_NEAR, false, true, false, false) == CE_PLAYUNIT_FIRE_OUT_OF_RANGE, "state: range comes before the line");
	Check(CePlayUnitFireStateOf(true, true, R, true, true, false, true) == CE_PLAYUNIT_FIRE_ARMING, "state: arming");
	Check(CePlayUnitFireStateOf(true, true, R, true, true, true, true) == CE_PLAYUNIT_FIRE_READY, "state: armed and ready is ready");
	Check(CePlayUnitFireStateOf(true, true, R, true, false, false, false) == CE_PLAYUNIT_FIRE_RECOVERING, "state: between shots is recovering");
	Check(CePlayUnitFireStateOf(true, true, R, true, false, false, true) == CE_PLAYUNIT_FIRE_ARMING, "state: arming comes before recovering");
	Check(CePlayUnitFireStateOf(true, true, R, true, true, false, false) == CE_PLAYUNIT_FIRE_READY, "state: nothing in the way is ready");

	for (int state = CE_PLAYUNIT_FIRE_NONE; state <= CE_PLAYUNIT_FIRE_BLOCKED; ++state)
	{
		const bool native = state == CE_PLAYUNIT_FIRE_READY || state == CE_PLAYUNIT_FIRE_ARMING || state == CE_PLAYUNIT_FIRE_RECOVERING;
		Check(CePlayUnitFireActs(true, state) == native, "fire: held fire gives the native calls only where there is something to ask");
		Check(!CePlayUnitFireActs(false, state), "fire: no held input, no native calls");
		Check(CePlayUnitFireAims(true, state) == (state != CE_PLAYUNIT_FIRE_NONE), "fire: held fire turns the gun wherever there is a weapon");
		Check(!CePlayUnitFireAims(false, state), "fire: no held input, the gun is not turned");
	}
}

// Taking a unit by key: why a unit is refused, where the view looks, which unit a press means, and the key's edges.
static bool NoRowShape(const CePlayUnitFacts &) { return false; }

static void Taking()
{
	// Every combination of the facts: a unit is refused exactly when no class takes it, and the first question that applies is the one asked.
	int combos = 0, agree = 0, order = 0;
	for (unsigned bits = 0; bits < (1u << 10); ++bits)
	{
		CePlayUnitFacts f;
		memset(&f, 0, sizeof(f));
		f.friendly = bits & 1; f.alive = bits & 2; f.vehicle = bits & 4; f.groundUnit = bits & 8; f.scripted = bits & 16;
		f.objective = bits & 32; f.deploys = bits & 64; f.normalSquad = bits & 128; f.spawned = bits & 256; f.armed = bits & 512;
		++combos;
		const int r = CePlayUnitRefusalFor(f);
		if ((r == CE_PLAYUNIT_TAKE_OK) == (CePlayUnitClassFor(f) >= 0)) ++agree;
		int expect = CE_PLAYUNIT_TAKE_OK;
		if (CePlayUnitClassFor(f) < 0)
		{
			if (!f.friendly) expect = CE_PLAYUNIT_REFUSE_ENEMY;
			else if (!f.alive) expect = CE_PLAYUNIT_REFUSE_DYING;
			else if (f.scripted || f.objective) expect = CE_PLAYUNIT_REFUSE_MISSION;
			else if (f.deploys) expect = CE_PLAYUNIT_REFUSE_DEPLOYS;
			else if (!(f.vehicle && f.groundUnit)) expect = CE_PLAYUNIT_REFUSE_KIND;
			else if (!f.normalSquad || f.spawned) expect = CE_PLAYUNIT_REFUSE_SQUAD;
			else expect = CE_PLAYUNIT_REFUSE_UNARMED;
		}
		if (r == expect) ++order;
	}
	Check(combos == 1024 && agree == combos, "refusal: refused exactly when no class takes the unit");
	Check(order == combos, "refusal: the first question that applies is the one asked");
	{ CePlayUnitFacts f = Tank(); f.vehicle = false; f.normalSquad = false; f.family = CE_PLAYUNIT_FAMILY_INFANTRY;
	  Check(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_KIND, "refusal: a soldier is refused for what it is, not for its squad"); }
	{ CePlayUnitFacts f = Tank(); f.friendly = false; f.deploys = true; Check(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_ENEMY, "refusal: an enemy is an enemy before anything else"); }
	{ CePlayUnitFacts f = Tank(); f.objective = true; Check(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_MISSION, "refusal: an objective belongs to the mission"); }
	{ CePlayUnitFacts f = Tank(); f.spawned = true; Check(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_SQUAD, "refusal: a squad that follows its lead keeps its unit"); }
	// A table with no row on refuses every unit for its kind once the common questions pass.
	{
		const CePlayUnitClassRow none[1] = {{"none", true, NoRowShape, CePlayUnitClasses()[0].law}};
		Check(CePlayUnitRefusalIn(none, 1, Tank()) == CE_PLAYUNIT_REFUSE_KIND, "refusal: a shape no row has is refused for its kind");
	}
	// Every refusal has a sentence that ends with a full stop and uses only what the game's small face draws; every family's kind sentence differs from the plain one.
	bool sentences = true, kinds = true;
	for (int r = CE_PLAYUNIT_REFUSE_ENEMY; r < CE_PLAYUNIT_REFUSE_COUNT; ++r)
		for (int fam = 0; fam < CE_PLAYUNIT_FAMILY_COUNT; ++fam)
		{
			const char *t = CePlayUnitRefusalSentence(r, fam);
			const size_t n = t ? strlen(t) : 0;
			if (!n || t[n - 1] != '.' || n > 60 || strpbrk(t, "$@[\\]^_{|}~`")) sentences = false;
		}
	for (int fam = CE_PLAYUNIT_FAMILY_GROUND_VEHICLE; fam < CE_PLAYUNIT_FAMILY_COUNT; ++fam)
		if (!strcmp(CePlayUnitRefusalSentence(CE_PLAYUNIT_REFUSE_KIND, fam), CePlayUnitRefusalSentence(CE_PLAYUNIT_REFUSE_KIND, CE_PLAYUNIT_FAMILY_OTHER))) kinds = false;
	Check(sentences, "refusal: each sentence ends with a full stop, fits and uses only what the small face draws");
	Check(kinds, "refusal: each kind of unit is named in its sentence");
	Check(!strcmp(CePlayUnitRefusalSentence(CE_PLAYUNIT_REFUSE_KIND, CE_PLAYUNIT_FAMILY_INFANTRY), "This soldier cannot be taken."), "refusal: only the unsupported soldier is refused");
	Check(!strcmp(CePlayUnitRefusalName(CE_PLAYUNIT_TAKE_OK), "ok") && !strcmp(CePlayUnitRefusalName(CE_PLAYUNIT_REFUSE_UNARMED), "unarmed") && !strcmp(CePlayUnitRefusalName(99), "?"), "refusal: the census names");

	// The picture: a 640x480 frame, places in its pixels.
	float score = -2.f;
	const float reach = 320.f, W = 640.f, H = 480.f;
	const CePlayUnitScreenPlace middle = {320, 240, 0, 50, 50}, aside = {352, 240, 0, 50, 50}, big = {352, 240, 40, 50, 50}, sized = {352, 240, 16, 50, 50};
	const CePlayUnitScreenPlace left = {-1, 240, 0, 50, 50}, right = {641, 240, 0, 50, 50}, top = {320, -1, 0, 50, 50}, bottom = {320, 481, 0, 50, 50};
	const CePlayUnitScreenPlace corner = {640, 480, 0, 50, 50}, behind = {320, 240, 0, -5, 50}, nearPlane = {320, 240, 0, 0.5f, 0.5f}, far = {320, 240, 0, 400, 400};
	const CePlayUnitScreenPlace review = {391, 243, 9, 15, 15.6f};		// the unit a review found taken: in the picture, 71 pixels right of the reticle
	Check(CePlayUnitAtScreen(middle, W, H, reach, &score) && Near(score, 0.f), "sight: a unit at the middle of the picture scores nothing");
	Check(CePlayUnitAtScreen(aside, W, H, reach, &score) && Near(score, 0.1f), "sight: the distance from the middle in half-widths of the 4:3 frame");
	Check(CePlayUnitAtScreen(sized, W, H, reach, &score) && Near(score, 0.05f), "sight: the unit's size brings its edge nearer the middle");
	Check(CePlayUnitAtScreen(big, W, H, reach, &score) && Near(score, 0.f), "sight: a unit that covers the middle scores nothing");
	Check(!CePlayUnitAtScreen(left, W, H, reach, &score) && !CePlayUnitAtScreen(right, W, H, reach, &score) && CePlayUnitAtScreen(corner, W, H, reach, &score), "sight: the frame's side edges");
	Check(!CePlayUnitAtScreen(top, W, H, reach, &score) && !CePlayUnitAtScreen(bottom, W, H, reach, &score), "sight: the frame's top and bottom edges");
	Check(!CePlayUnitAtScreen(behind, W, H, reach, &score), "sight: behind the eye is out of sight");
	Check(!CePlayUnitAtScreen(far, W, H, reach, &score), "sight: beyond reach is out of sight");
	Check(!CePlayUnitAtScreen(nearPlane, W, H, reach, &score), "sight: nearer than the near plane is out of sight");
	Check(CePlayUnitAtScreen(review, W, H, reach, &score) && score > kCePlayUnitSightReticle, "sight: a near unit beside the line is not at the reticle");
	{
		// a wide frame keeps the 4:3 frame's scale: 32 pixels at 480 high is a tenth of its half-width whatever the width
		const CePlayUnitScreenPlace wide = {544, 240, 0, 50, 50};
		Check(CePlayUnitAtScreen(wide, 1024.f, 480.f, reach, &score) && Near(score, 0.1f), "sight: a wide screen is measured in the 4:3 frame's half-widths");
		const CePlayUnitScreenPlace tall = {240, 432, 0, 50, 50};
		Check(CePlayUnitAtScreen(tall, 480.f, 800.f, reach, &score) && Near(score, 32.f / 240.f), "sight: a tall screen is measured across its width");
		const CePlayUnitScreenPlace nanv = {NAN, 240, 0, 50, 50};
		Check(!CePlayUnitAtScreen(middle, 0.f, H, reach, &score) && !CePlayUnitAtScreen(nanv, W, H, reach, &score), "sight: no frame or a number that is not one sees nothing");
	}

	// A switch's wait: a unit found and ready is taken; one that is gone or no longer takeable gives up at once; one not ready waits, and the wait ends.
	{
		unsigned wait = 0;
		Check(CePlayUnitSwitchStep(&wait, true, true, true) == CE_PLAYUNIT_SWITCH_IDLE, "switch: no wait, no switch");
		wait = 3;
		Check(CePlayUnitSwitchStep(&wait, true, true, true) == CE_PLAYUNIT_SWITCH_TAKE && wait == 0, "switch: found and ready is taken");
		wait = 3;
		Check(CePlayUnitSwitchStep(&wait, false, false, false) == CE_PLAYUNIT_SWITCH_GIVE_UP && wait == 0, "switch: a unit that is gone gives up at once");
		wait = 3;
		Check(CePlayUnitSwitchStep(&wait, true, true, false) == CE_PLAYUNIT_SWITCH_GIVE_UP && wait == 0, "switch: a unit no longer takeable gives up at once");
		wait = kCePlayUnitSwitchWait;
		unsigned steps = 0;
		int last = CE_PLAYUNIT_SWITCH_WAIT;
		while (last == CE_PLAYUNIT_SWITCH_WAIT && steps < 1000) { last = CePlayUnitSwitchStep(&wait, true, false, false); ++steps; }
		Check(last == CE_PLAYUNIT_SWITCH_GIVE_UP && steps == kCePlayUnitSwitchWait && wait == 0, "switch: a unit never ready gives up when the wait is over");
		wait = 2;
		Check(CePlayUnitSwitchStep(&wait, true, false, true) == CE_PLAYUNIT_SWITCH_WAIT && CePlayUnitSwitchStep(&wait, true, true, true) == CE_PLAYUNIT_SWITCH_TAKE, "switch: a wait that becomes ready takes");
	}

	// Which unit a press means.
	{
		const CePlayUnitSighted s[4] = {{0.05f, CE_PLAYUNIT_REFUSE_KIND}, {0.10f, CE_PLAYUNIT_TAKE_OK}, {0.08f, CE_PLAYUNIT_TAKE_OK}, {-1.f, CE_PLAYUNIT_TAKE_OK}};
		int why = -5;
		Check(CePlayUnitChoose(s, 4, &why) == 2 && why == CE_PLAYUNIT_TAKE_OK, "choose: the takeable unit nearest the middle, over a nearer one that is refused");
	}
	{
		const CePlayUnitSighted s[3] = {{0.09f, CE_PLAYUNIT_REFUSE_UNARMED}, {0.02f, CE_PLAYUNIT_REFUSE_KIND}, {-1.f, CE_PLAYUNIT_TAKE_OK}};
		int why = -5;
		Check(CePlayUnitChoose(s, 3, &why) == -1 && why == CE_PLAYUNIT_REFUSE_KIND, "choose: with none takeable, the reason of the unit nearest the middle");
	}
	{
		const CePlayUnitSighted s[2] = {{-1.f, CE_PLAYUNIT_TAKE_OK}, {-1.f, CE_PLAYUNIT_REFUSE_KIND}};
		int why = -5;
		Check(CePlayUnitChoose(s, 2, &why) == -1 && why == CE_PLAYUNIT_REFUSE_COUNT && CePlayUnitChoose(NULL, 0, &why) == -1 && why == CE_PLAYUNIT_REFUSE_COUNT, "choose: nothing in sight says so");
	}

	// The key's edges: one take per press, none for a held key, none while a press may not count, and none when that ends with the key still held.
	{
		CePlayUnitKey k = {false};
		const bool a = CePlayUnitPressed(&k, true, true), b = CePlayUnitPressed(&k, true, true), c = CePlayUnitPressed(&k, false, true), d = CePlayUnitPressed(&k, true, true);
		Check(a && !b && !c && d, "key: one take for each press, none while it is held");
		CePlayUnitKey m = {false};
		const bool e = CePlayUnitPressed(&m, true, false), f = CePlayUnitPressed(&m, true, true), g = CePlayUnitPressed(&m, false, true), h = CePlayUnitPressed(&m, true, true);
		Check(!e && !f && !g && h, "key: a press made while it may not count is not taken later");
	}
	Check(kCePlayUnitSightReach > 0.f && kCePlayUnitSightReticle > 0.f && kCePlayUnitSightReticle < 0.3f && kCePlayUnitSwitchWait > 0u, "sight: the reach, the reticle and the switch's wait are sane");
}

// The look camera (ce_playunit_look.h): its axes, where it hangs, and how the mouse turns it.
static void Look()
{
	const CePlayUnitLookLaw &law = CePlayUnitTankLook();
	float r[3], f[3], d[3];
	CePlayUnitLookAxes(0.f, 0.f, r, f, d);
	Check(Near(r[0], 1.f) && Near(f[1], 1.f) && Near(d[2], 1.f), "look: at rest the axes are the world's (right x, forward y, down z)");
	CePlayUnitLookAxes(1.5707963f, 0.f, r, f, d);
	Check(Near(f[0], -1.f) && Near(f[1], 0.f) && Near(r[1], 1.f), "look: a quarter turn of yaw faces -x, as the game's yaw turns its forward axis");
	CePlayUnitLookAxes(0.f, 0.3f, r, f, d);
	Check(f[2] > 0.25f && Near(f[1], cosf(0.3f)), "look: a positive pitch looks down (the world's z is down)");
	bool unit = true;
	for (float y = -3.f; y <= 3.f; y += 0.7f)
		for (float pt = -0.5f; pt <= 0.6f; pt += 0.25f)
		{
			CePlayUnitLookAxes(y, pt, r, f, d);
			const float rr = r[0] * r[0] + r[1] * r[1] + r[2] * r[2], ff = f[0] * f[0] + f[1] * f[1] + f[2] * f[2], dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
			const float rf = r[0] * f[0] + r[1] * f[1] + r[2] * f[2], rd = r[0] * d[0] + r[1] * d[1] + r[2] * d[2], fd = f[0] * d[0] + f[1] * d[1] + f[2] * d[2];
			if (!Near(rr, 1.f) || !Near(ff, 1.f) || !Near(dd, 1.f) || !Near(rf, 0.f) || !Near(rd, 0.f) || !Near(fd, 0.f) || r[2] != 0.f) unit = false;
		}
	Check(unit, "look: the axes are of unit length, square to each other, and the right axis stays level");
	const float place[3] = {10.f, 20.f, -5.f};
	float eye[3];
	CePlayUnitLookEye(place, 0.f, 0.f, law, eye);
	Check(Near(eye[0], 10.f) && Near(eye[1], 20.f - law.distance) && Near(eye[2], -5.f - law.lift), "look: the eye is behind the pivot by the distance, the pivot above the place by the lift");
	CePlayUnitLookEye(place, 0.f, 0.4f, law, eye);
	Check(eye[2] < -5.f - law.lift, "look: looking down raises the eye behind the unit");
	// the pivot is on the look's line: the eye plus the forward axis times the distance is the pivot
	CePlayUnitLookAxes(0.8f, 0.2f, r, f, d);
	CePlayUnitLookEye(place, 0.8f, 0.2f, law, eye);
	Check(Near(eye[0] + f[0] * law.distance, 10.f) && Near(eye[1] + f[1] * law.distance, 20.f) && Near(eye[2] + f[2] * law.distance, -5.f - law.lift), "look: the reticle's line passes through the pivot");
	float yaw = 0.f, pitch = 0.f;
	CePlayUnitLookTurn(&yaw, &pitch, 100.f, 0.f, law);
	CePlayUnitLookAxes(yaw, pitch, r, f, d);
	Check(f[0] > 0.2f && Near(yaw, -100.f * law.perCount), "look: the mouse to the right turns the look right (toward +x from +y)");
	yaw = 0.f; pitch = 0.f;
	CePlayUnitLookTurn(&yaw, &pitch, 0.f, 40.f, law);
	Check(pitch > 0.f && Near(pitch, 40.f * law.perCount), "look: the mouse moved down tilts the look down");
	CePlayUnitLookTurn(&yaw, &pitch, 0.f, 100000.f, law);
	Check(Near(pitch, law.pitchDown), "look: the tilt down stops at its limit");
	CePlayUnitLookTurn(&yaw, &pitch, 0.f, -100000.f, law);
	Check(Near(pitch, law.pitchUp), "look: the tilt up stops at its limit");
	yaw = 3.1f; pitch = 0.f;
	CePlayUnitLookTurn(&yaw, &pitch, -100.f, 0.f, law);
	Check(yaw <= 3.1415927f && yaw >= -3.1415927f && Near(CePlayUnitLookWrap(3.1f + 100.f * law.perCount), yaw), "look: the yaw stays within a turn");
	yaw = 0.5f; pitch = 0.1f;
	CePlayUnitLookTurn(&yaw, &pitch, NAN, 3.f, law);
	Check(Near(yaw, 0.5f) && Near(pitch, 0.1f), "look: a movement that is not a number turns nothing");
	Check(Near(CePlayUnitLookYawOf(0.f, 1.f), 0.f) && Near(CePlayUnitLookYawOf(-1.f, 0.f), 1.5707963f) && Near(CePlayUnitLookYawOf(1.f, 0.f), -1.5707963f), "look: the yaw of a heading");
	{
		float ry[3], ff[3], dd[3];
		const float hx = 0.6f, hy = -0.8f;
		CePlayUnitLookAxes(CePlayUnitLookYawOf(hx, hy), 0.f, ry, ff, dd);
		Check(Near(ff[0], hx) && Near(ff[1], hy), "look: a take looks along the unit's heading");
	}
	Check(law.distance > 4.f && law.lift > 0.f && law.pitchUp < 0.f && law.pitchDown > 0.f && law.perCount > 0.f, "look: the tank's law is sane");
	// at the lowest look the point under the reticle on flat ground is beyond the hull and the gun's least range (the round-2 review: the reticle sat on the driven tank)
	Check(law.lift / tanf(law.pitchDown) >= kCePlayUnitLookAimFloor, "look: at the lowest look the reticle is beyond the driven unit");
	{
		// the same, measured: the ray from the eye along the look at the lowest tilt meets flat ground (z = 0, the unit's place) this far ahead of the place
		const float at[3] = {0.f, 0.f, 0.f};
		float e[3], rr[3], fw[3], dw[3];
		CePlayUnitLookEye(at, 0.f, law.pitchDown, law, e);
		CePlayUnitLookAxes(0.f, law.pitchDown, rr, fw, dw);
		const float k = -e[2] / fw[2];
		Check(fw[2] > 0.f && e[1] + fw[1] * k >= kCePlayUnitLookAimFloor - 0.01f, "look: the lowest look's ground point, traced, is beyond the floor");
	}
	yaw = 0.f; pitch = 0.f;
	CePlayUnitLookStick(&yaw, &pitch, 1.f, 0.f, 1.f, law);
	Check(Near(yaw, -law.stickRate, 1e-3f) && Near(pitch, 0.f), "look: a stick held right for a second turns the look right at the stick's rate");
	yaw = 0.f; pitch = 0.f;
	CePlayUnitLookStick(&yaw, &pitch, 0.f, 0.5f, 0.1f, law);
	Check(Near(pitch, 0.5f * law.stickRate * 0.1f, 1e-4f) && Near(yaw, 0.f), "look: a stick pushed down tilts the look down");
	CePlayUnitLookStick(&yaw, &pitch, 0.f, 9.f, 100.f, law);
	Check(Near(pitch, law.pitchDown), "look: a stick tilts no further than the mouse may");
	yaw = 0.2f; pitch = 0.1f;
	CePlayUnitLookStick(&yaw, &pitch, NAN, 1.f, 0.1f, law);
	CePlayUnitLookStick(&yaw, &pitch, 1.f, 1.f, -0.1f, law);
	Check(Near(yaw, 0.2f) && Near(pitch, 0.1f), "look: a stick that is not a number, or no time, turns nothing");
}

// The direct law (CePlayUnitTankMotionOf, CePlayUnitTankGuard) and the notice that is replaced while shown.
static void Motion()
{
	const CePlayUnitLaw &law = CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law;
	const float y0 = 0.4f;
	CePlayUnitTankMotion m = CePlayUnitTankMotionOf(y0, 1.f, 0.f, 0.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 1.f) && Near(m.yaw, y0), "drive: W drives forward at the top speed, straight on");
	m = CePlayUnitTankMotionOf(y0, 0.5f, 0.f, 0.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 0.5f), "drive: half a stick is half the speed");
	m = CePlayUnitTankMotionOf(y0, 0.f, 1.f, 0.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, -law.reverseShare) && law.reverseShare > 0.f && law.reverseShare <= 1.f && Near(m.yaw, y0), "drive: S backs up, slower than forward");
	m = CePlayUnitTankMotionOf(y0, 1.f, 1.f, 0.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 0.f), "drive: W and S together hold still");
	m = CePlayUnitTankMotionOf(y0, 0.f, 0.f, 0.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 0.f) && Near(m.yaw, y0), "drive: nothing held, nothing moves");
	m = CePlayUnitTankMotionOf(y0, 1.f, 0.f, 1.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(m.share > 0.5f && Near(m.share, 1.f - law.turnSlow) && Near(m.yaw, y0 - law.steerLead), "drive: W and D steer right while moving, a little slower");
	m = CePlayUnitTankMotionOf(y0, 1.f, 0.f, -1.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(m.share > 0.5f && Near(m.yaw, y0 + law.steerLead), "drive: W and A steer left while moving");
	m = CePlayUnitTankMotionOf(y0, 0.f, 0.f, 1.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 0.f) && Near(m.yaw, y0 - law.steerLead), "drive: D alone turns on the spot");
	m = CePlayUnitTankMotionOf(y0, 0.f, 1.f, 1.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(m.share < 0.f && Near(m.yaw, y0 - law.steerLead), "drive: S and D back up turning the hull right");
	m = CePlayUnitTankMotionOf(3.1f, 1.f, 0.f, -1.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(m.yaw > -3.1415927f && m.yaw <= 3.1415927f && Near(m.yaw, CePlayUnitWrap(3.1f + law.steerLead)), "drive: the heading wraps past a half turn");
	m = CePlayUnitTankMotionOf(y0, NAN, NAN, NAN, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 0.f) && Near(m.yaw, y0), "drive: input that is not a number is none");
	m = CePlayUnitTankMotionOf(y0, 5.f, 0.f, 7.f, law.steerLead, law.reverseShare, law.turnSlow);
	Check(Near(m.share, 1.f - law.turnSlow) && Near(m.yaw, y0 - law.steerLead), "drive: input past full is full");
	Check(law.steerLead > 0.1f && law.steerLead < 1.f && law.turnSlow >= 0.f && law.turnSlow < 0.5f, "drive: the tank's direct law is sane");
	Check(Near(CePlayUnitTankGuard(1.f, true, false, true), 0.f), "guard: forward into a closed cell stops");
	Check(Near(CePlayUnitTankGuard(-0.6f, true, true, false), 0.f), "guard: reverse into a closed cell stops");
	Check(Near(CePlayUnitTankGuard(-0.6f, true, false, true), -0.6f), "guard: reverse away from a closed cell ahead goes");
	Check(Near(CePlayUnitTankGuard(1.f, true, true, false), 1.f), "guard: forward away from a closed cell behind goes");
	Check(Near(CePlayUnitTankGuard(1.f, false, false, false), 1.f), "guard: a unit already off open ground is not held there");
	Check(Near(CePlayUnitTankGuard(0.f, true, false, false), 0.f), "guard: standing still is standing still");
	// the notice: 350 ms of opening (caps and body), a notice of 4000 ms
	Check(CePlayUnitNoticeAgeOnReplace(0, 0, 350) == 0 && CePlayUnitNoticeAgeOnReplace(4000, 4000, 350) == 0, "notice: with none shown a new one opens from the start");
	Check(CePlayUnitNoticeAgeOnReplace(120, 4000, 350) == 120, "notice: one opening goes on opening");
	Check(CePlayUnitNoticeAgeOnReplace(2000, 4000, 350) == 350, "notice: one open stays open");
	Check(CePlayUnitNoticeAgeOnReplace(3900, 4000, 350) == 100, "notice: one closing opens again from the width it had");
	{
		// a press every 250 ms for two seconds: once open, the capsule never shows its opening again
		unsigned long long age = 0, shown = 0;
		bool reopened = false, opened = false;
		for (int ms = 0; ms <= 2000; ms += 50)
		{
			if (ms % 250 == 0) { age = CePlayUnitNoticeAgeOnReplace(age, shown, 350); shown = 4000; }
			if (age >= 350) opened = true;
			else if (opened) reopened = true;
			age += 50;
		}
		Check(opened && !reopened, "notice: a key pressed again and again keeps the capsule open");
	}
}

int main()
{
	Look();
	Motion();
	Handback();
	Gate();
	Eligibility();
	Angles();
	Points();
	Reissue();
	Table();
	FreeFire();
	Taking();
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
struct Unit;
struct FakeDriveGuide { Unit *GetUnit() { return nullptr; } };
FakeDriveGuide *gDriveGuide = nullptr;
struct Unit {
    unsigned calls=0; float issued=0; std::vector<unsigned> ticks; FakeDriveGuide *mGuide=nullptr;
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
   CePlayUnitDrive input={1,0,1,false,false,0,0}; Step(&unit,input);
   float gap=fabsf(CePlayUnitWrap(gDesired-unit.issued));
   if(gap>0.05001f)++stale;
   if(gap>largest)largest=gap;
  }
  printf("{\"fps\":%d,\"stale_frames\":%u,\"commands\":%u,\"largest_heading_gap\":%.9f}\n",hz,stale,unit.calls,largest);
  total+=stale;
 }
 ResetFixture();Unit straight;
 for(unsigned frame=1;frame<=25;++frame) {++gTick;EVENT_MANAGER.time=float(frame)/60;CePlayUnitDrive input={1,0,0,false,false,0,0};Step(&straight,input);}
 if(straight.ticks!=std::vector<unsigned>({1,7,13,19,25}))return 3;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;CePlayUnitDrive stop={0,0,0,false,false,0,0};Step(&straight,stop);
 unsigned stopped=straight.calls;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;Step(&straight,stop);if(straight.calls!=stopped)return 4;
 ++gTick;EVENT_MANAGER.time=float(gTick)/60;CePlayUnitDrive restart={1,0,0,false,false,0,0};Step(&straight,restart);if(straight.calls!=stopped+1)return 5;
 printf("{\"straight_schedule\":true,\"single_stop\":true,\"restart\":true}\n");
 return total?2:0;
}

#endif
