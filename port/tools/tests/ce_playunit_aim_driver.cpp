// Authored geometry for the native unit aim calculations extracted by the runner.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "ce_playunit_rules.h"

typedef std::intptr_t SINT;

static const float PI = 3.14159265358979323846f;
static const float PI_M2 = PI * 2.0f;
struct FVector {
    float x, y, z;
    FVector(float a=0, float b=0, float c=0) : x(a), y(b), z(c) {}
    FVector operator+(const FVector& v) const { return FVector(x+v.x,y+v.y,z+v.z); }
    FVector operator-(const FVector& v) const { return FVector(x-v.x,y-v.y,z-v.z); }
    float Azimuth() const { return std::atan2(x,y); }
};
struct FMatrix { FVector operator*(const FVector& v) const { return v; } };
static float AngleDifference(float a, float b) {
    float d=a-b;
    while(d > PI) d-=PI_M2;
    while(d < -PI) d+=PI_M2;
    return d;
}
struct FakeGame { float GetTime() const { return 10.0f; } };
static FakeGame game;
#define GAME (&game)
struct FakeEventManager {
    FakeEventManager* operator->() { return this; }
    float GetTime() const { return 10.0f; }
    template<class... T> void AddEvent(T...) {}
};
static FakeEventManager eventManager;
#define EVENT_MANAGER eventManager
static const unsigned TF_DYING=1;
struct CUnit;
struct Reader { CUnit* value=0; CUnit* ToRead() const { return value; } };
struct CUnitAI { Reader mTarget; };
struct UnitData { float mUnknownBC=2.0f, mUnknownDC=PI; };
struct RoundData { unsigned mFlags=0; };
struct WeaponMode { RoundData* mRound=0; };
struct CWeapon {
    WeaponMode* mMode=0;
    FVector lastPitchPoint;
    unsigned pitchCalls=0;
    FVector lastShotPoint;
    unsigned aimCalls=0;
    void WeaponUnknown509140(SINT, FVector* point) {
        lastShotPoint=*point;
        ++aimCalls;
    }
    float CalculateDesiredPitch(const FVector& point) {
        lastPitchPoint=point;
        ++pitchCalls;
        return std::atan2(point.z,std::sqrt(point.x*point.x+point.y*point.y));
    }
};
struct CUnit {
    CUnitAI* mAI=0;
    CWeapon* mCurrentWeapon=0;
    UnitData data;
    FVector mPos, mUnknown1F8, targetPoint;
    FMatrix mOrientation;
    float mUnknownE0=0, mUnknownE8=0, mUnknownEC=0, mUnknownF4=0;
    float mUnknown20C=0;
    int mUnknown224=1;
    unsigned mFlags=0;
    bool dying=false, inactive=false;
    UnitData* GetUnitData() { return &data; }
    bool IsDying() const { return dying; }
    bool Unknown109() const { return inactive; }
    bool Unknown84() const { return false; }
    void Unknown74() {}
    void Unknown86() {}
    void WeaponUnknown509140(SINT target,SINT point) {
        mCurrentWeapon->WeaponUnknown509140(target,reinterpret_cast<FVector*>(point));
    }
    float Unknown92() const { return 0; }
    void GetTargetablePos(FVector* out) const { *out=targetPoint; }
    void YawStep();
    void PitchStep();
};
static CUnit* controlled=0;
static FVector gAimPoint;
static bool gDriving=false, gOnFoot=false, gAimValid=false, allowed=true;
static CUnit* Held() { return controlled; }
static bool Allowed(bool option) { return option && allowed; }
static CWeapon* FireWeapon(CUnit* unit,bool) { return unit->mCurrentWeapon; }

@OWNER_API@
@DRIVE_FIRE@

// The runner inserts the actual generated Unit.cpp yaw and pitch blocks here.
@NATIVE_AIM@

static bool same(const FVector& a,const FVector& b) {
    return a.x==b.x && a.y==b.y && a.z==b.z;
}
static int check(bool ok,const char* label) {
    std::printf("%s %s\n",ok?"PASS":"FAIL",label);
    return ok?0:1;
}
int main(int argc,char** argv) {
    bool expectHuman=argc>1 && !std::strcmp(argv[1],"human");
    CUnit enemy, unit, ordinary;
    CUnitAI ai;
    RoundData round;
    WeaponMode mode;
    CWeapon gun;
    round.mFlags=~0u;
    mode.mRound=&round; gun.mMode=&mode;
    enemy.targetPoint=FVector(-40,100,-25);
    ai.mTarget.value=&enemy;
    unit.mAI=&ai; unit.mCurrentWeapon=&gun;
    ordinary.mAI=&ai; ordinary.mCurrentWeapon=&gun;
    controlled=&unit; gDriving=true;
    const FVector humanPoint(40,100,25);
    DriveFire(&unit,humanPoint,true,CE_PLAYUNIT_FIRE_READY,false);
    int bad=0;
    bad+=check(gAimValid && same(gAimPoint,humanPoint) && gun.aimCalls==0,
               "idle look publishes aim without arming or firing");
    unit.YawStep(); unit.PitchStep();
    bad+=check(expectHuman ? unit.mUnknownE0>0 : unit.mUnknownE0<0,
               expectHuman?"human yaw owns possessed turret":"baseline yaw still follows AI target");
    bad+=check(same(gun.lastPitchPoint,expectHuman?humanPoint:enemy.targetPoint),
               expectHuman?"human pitch uses native weapon solver":"baseline pitch still follows AI target");
    ordinary.YawStep(); ordinary.PitchStep();
    bad+=check(ordinary.mUnknownE0<0 && same(gun.lastPitchPoint,enemy.targetPoint),
               "ordinary unit keeps AI aim");
    DriveFire(&unit,humanPoint,true,CE_PLAYUNIT_FIRE_READY,true);
    bad+=check(gun.aimCalls==1 && same(gun.lastShotPoint,gAimPoint),
               "native shot and visual aim share one published point");
    const FVector sentinel(13,17,19);
    FVector answer=sentinel;
    gDriving=false;
    bad+=check(!PortCePlayUnitAimPoint(&unit,answer) && same(answer,sentinel),
               "inactive ownership leaves output untouched");
    gDriving=true; gOnFoot=true;
    bad+=check(!PortCePlayUnitAimPoint(&unit,answer),"infantry uses its own aiming path");
    gOnFoot=false; allowed=false;
    bad+=check(!PortCePlayUnitAimPoint(&unit,answer),"play-mode fences refuse aim ownership");
    allowed=true; gAimValid=false;
    bad+=check(!PortCePlayUnitAimPoint(&unit,answer),"unavailable aim is not reused");
    gAimValid=true;
    bad+=check(!PortCePlayUnitAimPoint(&ordinary,answer) && !PortCePlayUnitAimPoint(0,answer),
               "other and missing units are refused");
    gDriving=false; unit.mUnknownE0=0;
    unit.YawStep(); unit.PitchStep();
    bad+=check(unit.mUnknownE0<0 && same(gun.lastPitchPoint,enemy.targetPoint),
               "handback restores AI aim");
    return bad?1:0;
}
