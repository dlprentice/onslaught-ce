// SPDX-License-Identifier: GPL-3.0-only
// Compiles the actual tutorial-partner selector and patched script methods against small authored stubs.
// It does not run a mission, load retail content, or test career ownership.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

enum { THING_TYPE_UNIT=1, THING_TYPE_BATTLE_ENGINE=2 };
enum Operation { WEAPON_ENABLE, WEAPON_DISABLE, FLIGHT_ENABLE, FLIGHT_DISABLE, ACTIVATE, DEACTIVATE };
static unsigned call_order=0;
class CDataType {
public:
    explicit CDataType(const char *name="") : text(name) {}
    char *GetString() { return const_cast<char *>(text); }
private:
    const char *text;
};
class CThing {
public:
    bool active=true; unsigned calls[6]={}, order[6]={};
    void Seen(Operation op) { ++calls[op]; order[op]=++call_order; }
    virtual ~CThing() {}
    virtual bool IsA(int) { return false; }
    virtual void Activate() { Seen(ACTIVATE); active=true; }
    virtual void Deactivate() { Seen(DEACTIVATE); active=false; }
};
class CUnit : public CThing {
public:
    bool cannon=true, vulcan=true;
    virtual bool IsA(int type) { return type==THING_TYPE_UNIT; }
    virtual void EnableWeapon(char *name) { Seen(WEAPON_ENABLE); SetWeapon(name,true); }
    virtual void DisableWeapon(char *name) { Seen(WEAPON_DISABLE); SetWeapon(name,false); }
    void SetWeapon(const char *name,bool value) {
        if (!std::strcmp(name,"Pulse Cannon Pod")) cannon=value;
        if (!std::strcmp(name,"Mech Twin Vulcan Cannon")) vulcan=value;
    }
};
class CBattleEngine : public CUnit {
public:
    bool flight=true;
    bool IsA(int type) override { return type==THING_TYPE_UNIT || type==THING_TYPE_BATTLE_ENGINE; }
    void EnableFlightMode() { Seen(FLIGHT_ENABLE); flight=true; }
    void DisableFlightMode() { Seen(FLIGHT_DISABLE); flight=false; }
};
class CPlayer {
public:
    CBattleEngine *engine=nullptr;
    CBattleEngine *GetBattleEngine() { return engine; }
};
struct GameStub {
    int count=2; CPlayer *players[2]={};
    int GetNPlayers() { return count; }
    CPlayer *GetPlayer(int i) { return players[i]; }
} GAME;
enum Stage { OFF, CONNECTING, LOADED, STARTING, PRERUN, RUNNING, STOPPING, FINISHED, FAILED, ENDED };
struct LockstepStub { Stage stage=OFF; uint32_t level=0; } L;
extern "C" int CE_LockstepSharing(void);
extern "C" int CE_LockstepCoopPlayers(void);
extern "C" void *CE_LockstepTutorialPartner(void *thing);
class IScript {
public:
    CThing *mThing;
    explicit IScript(CThing *thing) : mThing(thing) {}
    void EnableWeapon(CDataType **,int,CDataType **);
    void DisableWeapon(CDataType **,int,CDataType **);
    void EnableFlightMode(CDataType **,int,CDataType **);
    void DisableFlightMode(CDataType **,int,CDataType **);
    void Activate(CDataType **,int,CDataType **);
    void Deactivate(CDataType **,int,CDataType **);
};
#include ACTUAL_LOCKSTEP
#include ACTUAL_PARTNER
#include ACTUAL_SCRIPT

static int failures=0;
static void check(bool condition,const char *label) {
    std::printf("%s %s\n",condition?"PASS":"FAIL",label);
    if (!condition) ++failures;
}
int main() {
    CBattleEngine host,guest; CPlayer p1,p2; p1.engine=&host; p2.engine=&guest;
    GAME.players[0]=&p1; GAME.players[1]=&p2;
    CDataType pulse("Pulse Cannon Pod"), vulcan("Mech Twin Vulcan Cannon");
    CDataType *pulse_arg[]={&pulse}, *vulcan_arg[]={&vulcan};
    IScript host_script(&host); L.stage=LOADED; L.level=100;
    host_script.DisableFlightMode(nullptr,0,nullptr);
    check(!host.flight&&!guest.flight,"tutorial disables flight on both");
    check(host.calls[FLIGHT_DISABLE]==1&&guest.calls[FLIGHT_DISABLE]==1&&host.order[FLIGHT_DISABLE]<guest.order[FLIGHT_DISABLE],"flight disable once and in order");
    host_script.EnableFlightMode(nullptr,0,nullptr);
    check(host.flight&&guest.flight,"tutorial enables flight on both");
    check(host.calls[FLIGHT_ENABLE]==1&&guest.calls[FLIGHT_ENABLE]==1&&host.order[FLIGHT_ENABLE]<guest.order[FLIGHT_ENABLE],"flight enable once and in order");
    host_script.DisableWeapon(pulse_arg,1,nullptr);
    check(host.calls[WEAPON_DISABLE]==1&&guest.calls[WEAPON_DISABLE]==1&&host.order[WEAPON_DISABLE]<guest.order[WEAPON_DISABLE],"weapon disable once and in order");
    host_script.DisableWeapon(vulcan_arg,1,nullptr);
    check(host.calls[WEAPON_DISABLE]==2&&guest.calls[WEAPON_DISABLE]==2&&host.order[WEAPON_DISABLE]<guest.order[WEAPON_DISABLE],"second weapon disable once and in order");
    check(!host.cannon&&!guest.cannon&&!host.vulcan&&!guest.vulcan,"tutorial disables named weapons on both");
    host_script.EnableWeapon(pulse_arg,1,nullptr);
    check(host.calls[WEAPON_ENABLE]==1&&guest.calls[WEAPON_ENABLE]==1&&host.order[WEAPON_ENABLE]<guest.order[WEAPON_ENABLE],"weapon enable once and in order");
    host_script.EnableWeapon(vulcan_arg,1,nullptr);
    check(host.calls[WEAPON_ENABLE]==2&&guest.calls[WEAPON_ENABLE]==2&&host.order[WEAPON_ENABLE]<guest.order[WEAPON_ENABLE],"second weapon enable once and in order");
    check(host.cannon&&guest.cannon&&host.vulcan&&guest.vulcan,"tutorial enables named weapons on both");
    host_script.Deactivate(nullptr,0,nullptr);
    check(!host.active&&!guest.active,"tutorial deactivates both");
    check(host.calls[DEACTIVATE]==1&&guest.calls[DEACTIVATE]==1&&host.order[DEACTIVATE]<guest.order[DEACTIVATE],"deactivate once and in order");
    host_script.Activate(nullptr,0,nullptr);
    check(host.active&&guest.active,"tutorial activates both");
    check(host.calls[ACTIVATE]==1&&guest.calls[ACTIVATE]==1&&host.order[ACTIVATE]<guest.order[ACTIVATE],"activate once and in order");
    for (Stage stage : {CONNECTING,LOADED,STARTING,PRERUN,RUNNING,STOPPING}) {
        L.stage=stage; L.level=100;
        check(CE_LockstepTutorialPartner(&host)==static_cast<CThing *>(&guest),"partner available during shared tutorial stages");
    }
    for (Stage stage : {OFF,FINISHED,FAILED,ENDED}) {
        L.stage=stage; L.level=100; host.flight=guest.flight=true;
        host_script.DisableFlightMode(nullptr,0,nullptr);
        check(!host.flight&&guest.flight,"offline or finished keeps partner unchanged");
    }
    for (unsigned level : {110u,856u,858u}) {
        L.stage=RUNNING; L.level=level; host.cannon=guest.cannon=true;
        host_script.DisableWeapon(pulse_arg,1,nullptr);
        check(!host.cannon&&guest.cannon,"other level keeps partner unchanged");
    }
    L.stage=RUNNING; L.level=100;
    IScript guest_script(&guest); host.flight=guest.flight=true;
    guest_script.DisableFlightMode(nullptr,0,nullptr);
    check(host.flight&&!guest.flight,"guest-targeted operation does not affect host");
    CThing other; IScript other_script(&other); host.active=guest.active=true;
    other_script.Deactivate(nullptr,0,nullptr);
    check(!other.active&&host.active&&guest.active,"unrelated thing does not affect players");
    check(!CE_LockstepTutorialPartner(nullptr),"null target refused");
    GAME.count=1; check(!CE_LockstepTutorialPartner(&host),"single player refused"); GAME.count=2;
    GAME.players[1]=nullptr; check(!CE_LockstepTutorialPartner(&host),"missing guest refused"); GAME.players[1]=&p2;
    p2.engine=nullptr; check(!CE_LockstepTutorialPartner(&host),"missing guest engine refused"); p2.engine=&guest;
    GAME.players[0]=nullptr; check(!CE_LockstepTutorialPartner(&host),"missing host refused"); GAME.players[0]=&p1;
    p2.engine=&host; check(!CE_LockstepTutorialPartner(&host),"aliased engines refused"); p2.engine=&guest;
    return failures?1:0;
}
