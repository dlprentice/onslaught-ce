// SPDX-License-Identifier: GPL-3.0-only
// Runs the actual selected player-gate digest over authored stubs; not a game or network test.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

template<class T> struct SPtrSet {
    std::vector<T *> values; int reported=-1;
    int Size() { return reported<0?(int)values.size():reported; }
};
template<class T> class ListIterator {
    SPtrSet<T> *list; size_t i=0;
public:
    explicit ListIterator(SPtrSet<T> *items):list(items) {}
    T *First() { i=0; return i<list->values.size()?list->values[i]:nullptr; }
    T *Next() { ++i; return i<list->values.size()?list->values[i]:nullptr; }
};
struct CWeaponData { const char *mName; };
struct CWeapon { CWeaponData *mData=nullptr; int mActive=1; };
struct Walker { CWeapon *mPrimaryWeapon=nullptr,*mAugWeapon=nullptr; SPtrSet<CWeapon> mWeapons; int mCurrentWeapon=0; };
struct Jet { SPtrSet<CWeapon> mWeapons; int mCurrentWeapon=0; };
struct CPlayer;
struct CBattleEngine { int mFlightModeActive=0,mPoweredUp=1,mActive=1,mState=2,mAugActive=0; Walker *mWalkerPart=nullptr; Jet *mJetPart=nullptr; SPtrSet<CWeapon> mWeapons; CPlayer *owner=nullptr; CPlayer *GetPlayer(){return owner;} };
struct CPlayer { int number=0; CBattleEngine *engine=nullptr; int GetNumber(){return number;} CBattleEngine *GetBattleEngine(){return engine;} };
struct Game { int count=2; CPlayer *p[2]={}; int GetNPlayers(){return count;} CPlayer *GetPlayer(int i){return p[i];} } GAME;
struct CENetworkWorldAdapter {
#include ACTUAL_GATES
};
struct Rig {
    CWeaponData data[6]={{"pulse"},{"augmentation"},{"walker"},{"jet"},{"unit"},{"jet-alt"}};
    CWeapon weapons[6]; Walker walker; Jet jet; CBattleEngine engine; CPlayer player;
    explicit Rig(int number) {
        for(int i=0;i<6;++i) weapons[i].mData=&data[i];
        walker.mPrimaryWeapon=&weapons[0];walker.mAugWeapon=&weapons[1];walker.mWeapons.values.push_back(&weapons[2]);
        jet.mWeapons.values.push_back(&weapons[3]);jet.mWeapons.values.push_back(&weapons[5]);engine.mWeapons.values.push_back(&weapons[4]);
        engine.mWalkerPart=&walker;engine.mJetPart=&jet;player.number=number;player.engine=&engine;engine.owner=&player;
    }
};
static int failures=0;
static void check(bool ok,const char *label){std::printf("%s %s\n",ok?"PASS":"FAIL",label);failures+=!ok;}
static bool digest(uint64_t &out){return CENetworkWorldAdapter::LockstepPlayerGates(out);}
int main(){
    Rig host(1),guest(2),clone(2); GAME.p[0]=&host.player;GAME.p[1]=&guest.player;
    uint64_t baseline=0,value=0;check(digest(baseline),"complete inventories accepted");
    guest.engine.mFlightModeActive=1;check(digest(value)&&value!=baseline,"old 0/1 flight asymmetry detected");guest.engine.mFlightModeActive=0;
    guest.engine.mPoweredUp=0;check(digest(value)&&value!=baseline,"player power gate detected");guest.engine.mPoweredUp=1;
    guest.engine.mActive=0;check(digest(value)&&value!=baseline,"player active gate detected");guest.engine.mActive=1;
    guest.engine.mState=3;check(digest(value)&&value!=baseline,"battle engine mode detected");guest.engine.mState=2;
    guest.engine.mAugActive=1;check(digest(value)&&value!=baseline,"augmentation selection detected");guest.engine.mAugActive=0;
    guest.walker.mCurrentWeapon=1;check(digest(value)&&value!=baseline,"walker selection detected");guest.walker.mCurrentWeapon=0;
    guest.jet.mCurrentWeapon=1;check(digest(value)&&value!=baseline,"jet selection detected");guest.jet.mCurrentWeapon=0;
    host.walker.mCurrentWeapon=1;check(digest(value)&&value!=baseline,"host walker selection detected");host.walker.mCurrentWeapon=0;
    host.jet.mCurrentWeapon=1;check(digest(value)&&value!=baseline,"host jet selection detected");host.jet.mCurrentWeapon=0;
    const char *labels[]={"primary weapon active detected","augmentation active detected","walker weapon active detected","jet weapon active detected","unit weapon active detected"};
    for(int i=0;i<5;++i){guest.weapons[i].mActive=0;check(digest(value)&&value!=baseline,labels[i]);guest.weapons[i].mActive=1;}
    guest.data[0].mName="laser";check(digest(value)&&value!=baseline,"weapon names bind gate state");guest.data[0].mName="pulse";
    GAME.p[1]=&clone.player;check(digest(value)&&value==baseline,"equal state ignores process object addresses");GAME.p[1]=&guest.player;
    guest.walker.mPrimaryWeapon=nullptr;check(digest(value)&&value!=baseline,"empty primary slot is distinguished");guest.walker.mPrimaryWeapon=&guest.weapons[0];
    guest.engine.mWalkerPart=nullptr;check(digest(value)&&value!=baseline,"absent walker part is distinguished");guest.engine.mWalkerPart=&guest.walker;
    guest.player.engine=nullptr;check(digest(value)&&value!=baseline,"absent engine is represented");guest.player.engine=&guest.engine;
    guest.engine.owner=nullptr;check(!digest(value),"unowned engine refused");guest.engine.owner=&guest.player;
    guest.engine.owner=&host.player;check(!digest(value),"wrong reverse owner refused");guest.engine.owner=&guest.player;
    guest.player.engine=&host.engine;check(!digest(value),"aliased player engines refused");guest.player.engine=&guest.engine;
    host.player.engine=nullptr;guest.player.engine=nullptr;check(digest(value),"two absent engines represented");host.player.engine=&host.engine;guest.player.engine=&guest.engine;
    guest.weapons[0].mData=nullptr;check(!digest(value),"missing weapon data refused");guest.weapons[0].mData=&guest.data[0];
    guest.data[0].mName=nullptr;check(!digest(value),"missing weapon name refused");guest.data[0].mName="";check(!digest(value),"empty weapon name refused");
    std::string long_name(256,'a');guest.data[0].mName=long_name.c_str();check(!digest(value),"overlong weapon name refused");guest.data[0].mName="pulse";
    auto &items=guest.jet.mWeapons;items.values.assign(64,&guest.weapons[3]);check(digest(value),"64 weapon entries accepted");
    items.values.push_back(&guest.weapons[3]);check(!digest(value),"oversize inventory refused");items.values={&guest.weapons[3],&guest.weapons[5]};
    items.reported=3;check(!digest(value),"incomplete inventory traversal refused");items.reported=-1;
    items.values.insert(items.values.begin(),nullptr);check(!digest(value),"null list entry refused");items.values.erase(items.values.begin());
    guest.player.number=1;check(!digest(value),"incorrect player ownership refused");guest.player.number=2;
    GAME.count=1;check(!digest(value),"one player refused");GAME.count=2;
    GAME.p[1]=nullptr;check(!digest(value),"missing player refused");GAME.p[1]=&guest.player;
    check(digest(value)&&value==baseline,"restored state returns same digest");
    return failures?1:0;
}
