#ifndef CE_CMDUNIT_ORDERS_FIXTURE_H
#define CE_CMDUNIT_ORDERS_FIXTURE_H
#include <stdint.h>
#include <stddef.h>
#include "ce_cmdorders.h"
#define TRUE 1
#define FALSE 0
struct FVector {
    float X, Y, Z;
    FVector(float x=0, float y=0, float z=0) : X(x),Y(y),Z(z) {}
};
struct CSquad { int identity; bool shutting=false; bool IsShuttingDown(){return shutting;} ~CSquad() { PortCeCmdThingGone(this); } };
struct IScript { int identity; };
extern bool fixtureReadsForbidden;
extern bool fixtureRuntime;
extern bool fixtureLand;
extern bool fixtureSkipPoint;
extern void (*fixtureAfterPoint)();
extern void (*fixtureAfterStop)();
void FixtureRead();
void FixtureRegister(CUnit*);
void FixtureUnregister(CUnit*);
class CGuide {
public:
    CUnit* unit = NULL;
    FVector destination;
    int mode = 0;
    int writes = 0;
    int attempts = 0;
    CUnit* GetUnit() { FixtureRead(); return unit; }
    void GoToPoint(FVector point, bool overridePoint) {
        ++attempts;
        if (!PortCeCmdGuidePoint(this, overridePoint)) return;
        destination=point;mode=1;++writes;
    }
};
class CUnit {
public:
    CGuide* mGuide = NULL;
    CSquad* squad = NULL;
    IScript* script = NULL;
    int number = 1;
    unsigned kind = 3;
    int allegiance = 1;
    int state = 2;
    float life = 100;
    bool dying = false;
    bool shutting = false;
    bool commandable = true;
    bool ground = true;
    int stops = 0;
    FVector position;
    CUnit() { FixtureRegister(this); }
    ~CUnit() { FixtureUnregister(this);PortCeCmdThingGone(this); }
    bool IsShuttingDown() { FixtureRead();return shutting; }
    bool IsDying() { FixtureRead();return dying; }
    float GetLife() { FixtureRead();return life; }
    int GetThingNumber() { FixtureRead();return number; }
    unsigned GetThingType() { FixtureRead();return kind; }
    CSquad* GetSquad() { FixtureRead();return squad; }
    IScript* GetMissionScript() { FixtureRead();return script; }
    int GetAllegiance() { FixtureRead();return allegiance; }
    int GetAIState() { FixtureRead();return state; }
    FVector GetPos() { FixtureRead();return position; }
    void GoToPoint(FVector point, bool nativeOverride) {
        if(mGuide && !fixtureSkipPoint)mGuide->GoToPoint(point,nativeOverride);
        if(fixtureAfterPoint)fixtureAfterPoint();
    }
    void Stop() {
        ++stops;if(mGuide)mGuide->mode=0;
        if(fixtureAfterStop)fixtureAfterStop();
    }
};
#endif
