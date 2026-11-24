// SPDX-License-Identifier: GPL-3.0-only
// Native menu consumers, real quality policies and real settings I/O; only platform presentation is authored here.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "model_quality.h"
#include "terrain_quality.h"
#include "aniso_quality.h"
#include "reflection_quality.h"
#include "portlog.h"

static void check(bool okay, const char *reason)
{
    if (!okay) { std::fprintf(stderr, "quality UI assertion: %s\n", reason); std::exit(1); }
}
static bool menus = true;
static int notices, failRenames;
bool PortCeMenus() { return menus; }
void PortCeSaveFailedNotice() { ++notices; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" uint64_t SDL_GetTicksNS() { return 1000000000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }
extern "C" int __real_renameat(int, const char *, int, const char *);
extern "C" int __wrap_renameat(int a, const char *b, int c, const char *d)
{
    if (failRenames > 0) { --failRenames; errno = EACCES; return -1; }
    return __real_renameat(a,b,c,d);
}

using WCHAR = wchar_t;
using EPCText = int;
struct CController {};
enum { BUTTON_FRONTEND_MENU_UP=1, BUTTON_FRONTEND_MENU_DOWN=2, BUTTON_FRONTEND_MENU_LEFT=3,
       BUTTON_FRONTEND_MENU_RIGHT=4, BUTTON_FRONTEND_MENU_SELECT=5, BUTTON_FRONTEND_MENU_BACK=6,
       FES_MOVE=7, FES_SELECT=8, FES_BACK=9 };
struct CFrontEnd { static void PlaySound(int) {} static bool MouseClickedInRect(float,float,float,float) { return true; } };
enum EMemoryType { MenuMemory=0x80 };
void *operator new(size_t n, EMemoryType) { return ::operator new(n); }
struct CMenuItem { CMenuItem(EPCText,int) {} virtual ~CMenuItem() {} virtual void Commit() {} };
struct CMenuMenuItem : CMenuItem {
    int mSelection=-1, mValue=-1;
    bool mDeferred=false, mExpanded=false;
    CMenuMenuItem(EPCText,int,bool);
    virtual WCHAR *GetText()=0;
    virtual int GetNumChoices()=0;
    virtual WCHAR *GetChoiceText(int)=0;
    virtual int GetValue()=0;
    virtual void SetValue(int)=0;
    virtual void ReceiveButtonAction(CController *,int,float);
    void Refresh();
    void Commit() override;
    void SetSelection(int n) { mSelection = n; }
    bool HasPendingChange();
    void FixtureRenderSync();
    void FixtureMouseSelect(int);
};
#include "native_quality_menu.inc"
#include "native_quality_action.inc"
void CMenuMenuItem::FixtureRenderSync()
{
#include "native_quality_sync.inc"
}
void CMenuMenuItem::FixtureMouseSelect(int i)
{
    float lx=0,ly=0,cx2=1,cy2=1;
#include "native_quality_mouse.inc"
}
struct CeDetailPreset;
void PortCeGraphicsRegisterDetailPresetStager(void (*)(const CeDetailPreset &)) {}
#include QUALITY_SOURCE
static_assert(TerrainPending == CE_TERRAIN_DETAIL_PENDING && TerrainActive == CE_TERRAIN_DETAIL_ACTIVE &&
              TerrainResourceFallback == CE_TERRAIN_DETAIL_RESOURCE_FALLBACK, "menu and policy agree on terrain status");
struct List {
    std::vector<CMenuItem *> mItems;
    void AddItem(CMenuItem *item) { mItems.push_back(item); }
    ~List() { for (auto item : mItems) delete item; }
};
using CMenuOptionsList=List;
int gMenuApplyCount;
#define FOR_ALL_ITEMS_IN(container,item) for(auto it=(container).begin();it!=(container).end()&&((item)=*it,true);++it)
struct CApplyMenuItem { CMenuOptionsList *mList; void ReceiveButtonAction(CController *,int,float); };
#include "native_quality_apply.inc"
static void NativeRows(List *list)
{
#include "native_quality_rows.inc"
}
static CMenuMenuItem *item(List &list, unsigned index) { return static_cast<CMenuMenuItem *>(list.mItems.at(index)); }
static std::string iniPath() { return std::string(getenv("BEA_USER_DIR"))+"/ce.ini"; }
static std::string ini()
{
    std::ifstream stream(iniPath());
    return std::string(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
}
static void frame() {
    CeModelQualityBeginFrame();
    CeTerrainQualityBeginFrame();
    CeAnisoQualityBeginFrame();
    CeWaterReflectionBeginFrame();
    CeDynamicShadowBeginFrame();
}
static void live(bool units, bool trees, bool terrain, unsigned aniso = 4, bool anisoActive = true,
                 unsigned waterTarget = 256, unsigned shadowTarget = 64)
{
    check(CeUnitModelsQualityEnabled()==units,"unit-model active snapshot");
    check(CeTreeModelsQualityEnabled()==trees,"tree-model active snapshot");
    check(CeTerrainGeometryDistanceSquared(16.f)==(terrain?4.f:16.f),"terrain active snapshot");
    if (anisoActive) {
        check(CeAnisoEffectiveLevel(4, 16, true) == (aniso == 0 ? 1u : aniso), "aniso active snapshot");
        check(CeWaterReflectionTargetRes() == waterTarget, "water reflection target snapshot");
        check(CeDynamicShadowTargetRes() == shadowTarget, "dynamic shadow target snapshot");
    } else {
        check(!CeAnisoQualityActive() && CeAnisoEffectiveLevel(4, 16, true) == 4u, "fenced aniso stays retail");
        check(!CeWaterReflectionQualityActive() && CeWaterReflectionTargetRes() == 256u, "fenced water stays 256");
        check(!CeDynamicShadowQualityActive() && CeDynamicShadowTargetRes() == 64u, "fenced shadow stays 64");
    }
}
static bool unsaved(CMenuMenuItem *p) { return wcsstr(p->GetChoiceText(p->GetValue()),L"not saved")!=nullptr; }
static bool terrainReady = true;
static bool TerrainReady() { return terrainReady; }
static void apply(List &list)
{
    CApplyMenuItem action{&list}; action.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
}
static void choose(List &list, int units, int trees, int terrain)
{
    item(list,0)->mSelection = units;
    item(list,1)->mSelection = trees;
    item(list,2)->mSelection = terrain;
}

static bool same(const wchar_t *a, const wchar_t *b) { return !wcscmp(a, b); }

static void wording(List &list, const std::string &scenario)
{
    CMenuMenuItem *terrain = item(list, 2);
    const bool failing = scenario == "wording-unsaved";
    if (scenario == "wording-frontend") CeTerrainQualityResourceCheck(nullptr);
    check(same(terrain->GetChoiceText(0), L"Normal") && same(terrain->GetChoiceText(1), L"Extended"), "plain choices");
    choose(list, 0, 0, 1);
    if (failing) failRenames = 100;
    apply(list);
    failRenames = 0;
    check(CE_TerrainDetailChoice() == 1, "request queued");
    check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING, "pending until a frame samples the request");
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (not saved)" : L"Extended"), "pending reads as the plain choice");
    if (scenario == "wording-frontend") {
        frame();
        check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING, "no level stays pending");
        check(same(terrain->GetChoiceText(1), L"Extended") && !wcsstr(terrain->GetChoiceText(1), L"Normal ("), "frontend is not a failure");
        check(CeTerrainGeometryDistanceSquared(16.f) == 16.f, "no reserve, no extension");
        return;
    }
    terrainReady = false;
    frame();
    check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_RESOURCE_FALLBACK, "refusal is observed at the frame boundary");
    check(CeTerrainGeometryDistanceSquared(16.f) == 16.f, "the refused frame draws Normal");
    check(CE_TerrainDetailChoice() == 1, "the request is preserved through the refusal");
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (using Normal, not saved)" : L"Extended (using Normal)"),
          "requested Extended is not shown as active after a resource fallback");
    check(same(terrain->GetChoiceText(0), L"Normal"), "the other choice is not annotated");
    check(same(item(list, 0)->GetChoiceText(1), L"Yes"), "the unit-models row is untouched");
    terrainReady = true;
    frame();
    check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE && CeTerrainGeometryDistanceSquared(16.f) == 4.f, "recovered");
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (not saved)" : L"Extended"), "recovery clears the wording");
    terrainReady = false;
    frame();
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (using Normal, not saved)" : L"Extended (using Normal)"), "refused again");
    choose(list, 1, 0, 0);
    apply(list);
    frame();
    check(CE_TerrainDetailChoice() == 0 && CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE, "Normal needs no reserve");
    check(same(terrain->GetChoiceText(1), L"Extended"), "Extended is plain once Normal is chosen");

    // Water reflection and dynamic shadow allocation fallback wording
    item(list, 4)->mSelection = 2; item(list, 5)->mSelection = 2;
    apply(list); frame();
    CeWaterReflectionSetAllocatedRes(512, 1024);
    CeDynamicShadowSetAllocatedRes(64, 256);
    check(same(item(list, 4)->GetChoiceText(2), L"Ultra (using High)"), "water reflection fallback to High shown");
    check(same(item(list, 4)->GetChoiceText(0), L"Normal"), "non-current water choice plain");
    check(same(item(list, 5)->GetChoiceText(2), L"Ultra (using Normal)"), "dynamic shadow fallback to Normal shown");
    CeWaterReflectionSetAllocatedRes(1024, 1024);
    CeDynamicShadowSetAllocatedRes(256, 256);
    check(same(item(list, 4)->GetChoiceText(2), L"Ultra"), "water reflection recovery clears wording");
    check(same(item(list, 5)->GetChoiceText(2), L"Ultra"), "dynamic shadow recovery clears wording");
}

int main(int argc,char **argv)
{
    CeTerrainQualityResourceCheck(TerrainReady);
    check(argc==2,"one scenario required");
    const std::string scenario=argv[1];
    if(scenario=="menus-off") menus=false;
    const std::string before=ini();
    List list; NativeRows(&list);
    if(scenario=="fenced" || scenario=="menus-off") {
        check(!PortCeQualityOptionsAvailable()&&list.mItems.empty(),"faithful/private menu has no quality rows");
        auto p=static_cast<CMenuMenuItem *>(PortCeUnitModelsItem());
        p->SetValue(1);delete p;
        check(ini()==before && notices==0,"fenced request does not persist or show a save notice");
        const bool retained = scenario=="menus-off";
        check(CE_FullUnitModelsChoice()==int(retained)&&CE_FullTreeModelsChoice()==int(retained)&&CE_TerrainDetailChoice()==int(retained),
              "menu availability does not override faithful or stored rendering policy");
        check(CE_AnisoLevelChoice()==(retained ? 8 : 4),"aniso menu availability matches rendering policy");
        check(CE_WaterReflectionResChoice()==(retained ? 512 : 256),"water menu availability matches rendering policy");
        check(CE_DynamicShadowResChoice()==(retained ? 128 : 64),"shadow menu availability matches rendering policy");
        frame();live(retained,retained,retained,retained ? 8 : 4,retained,retained ? 512 : 256,retained ? 128 : 64);
    } else {
        check(list.mItems.size()==6,"exactly six native quality rows");
        check(!wcscmp(item(list,0)->GetText(),L"High-detail units:"),"unit models row name");
        check(!wcscmp(item(list,1)->GetText(),L"3D distant trees:"),"tree models row name");
        check(!wcscmp(item(list,2)->GetText(),L"Terrain detail distance:"),"terrain row name");
        check(!wcscmp(item(list,3)->GetText(),L"Anisotropic filtering:"),"aniso row name");
        check(!wcscmp(item(list,4)->GetText(),L"Water reflections:"),"water row name");
        check(!wcscmp(item(list,5)->GetText(),L"Dynamic shadows:"),"shadow row name");
        check(!wcscmp(item(list,0)->GetChoiceText(0),L"No")&&!wcscmp(item(list,0)->GetChoiceText(1),L"Yes"),"unit model choices");
        check(!wcscmp(item(list,1)->GetChoiceText(0),L"No")&&!wcscmp(item(list,1)->GetChoiceText(1),L"Yes"),"tree model choices");
        check(!wcscmp(item(list,2)->GetChoiceText(0),L"Normal")&&!wcscmp(item(list,2)->GetChoiceText(1),L"Extended"),"terrain choices");
        check(item(list,3)->GetNumChoices()==4 && !wcscmp(item(list,3)->GetChoiceText(0),L"Off") &&
              !wcscmp(item(list,3)->GetChoiceText(1),L"4x") && !wcscmp(item(list,3)->GetChoiceText(2),L"8x") &&
              !wcscmp(item(list,3)->GetChoiceText(3),L"16x"),"aniso choices");
        check(item(list,4)->GetNumChoices()==3 && !wcscmp(item(list,4)->GetChoiceText(0),L"Normal") &&
              !wcscmp(item(list,4)->GetChoiceText(1),L"High") && !wcscmp(item(list,4)->GetChoiceText(2),L"Ultra"),"water choices");
        check(item(list,5)->GetNumChoices()==3 && !wcscmp(item(list,5)->GetChoiceText(0),L"Normal") &&
              !wcscmp(item(list,5)->GetChoiceText(1),L"High") && !wcscmp(item(list,5)->GetChoiceText(2),L"Ultra"),"shadow choices");
        for (unsigned idx = 0; idx < 6; ++idx) check(PortCeQualityRow(item(list,idx)), "live row is known");
        check(!PortCeQualityRow(nullptr)&&!PortCeQualityRow(&list),"non-rows unknown");
        {
            List other;NativeRows(&other);const void *gone=other.mItems.at(0);
            check(PortCeQualityRow(gone),"a second set is known too");
            for(auto p:other.mItems) delete p;
            other.mItems.clear();
            check(!PortCeQualityRow(gone)&&PortCeQualityRow(item(list,0)),"a deleted row is forgotten; the others stay known");
        }
        if(scenario=="stored") {
            check(item(list,0)->GetValue()==1&&item(list,1)->GetValue()==1&&item(list,2)->GetValue()==1&&
                  item(list,3)->GetValue()==2&&item(list,4)->GetValue()==1&&item(list,5)->GetValue()==1,"existing preferences load");
            struct stat first{},last{};check(!stat(iniPath().c_str(),&first),"existing settings inode");
            apply(list);check(!stat(iniPath().c_str(),&last),"unchanged settings inode");
            check(ini()==before&&first.st_ino==last.st_ino,"unchanged Apply preserves exact file and preferences");
            frame();live(true,true,true,8,true,512,128);
        } else {
            const bool isFresh = scenario=="fresh";
            const int expectUnit = isFresh ? 1 : 0;
            const int expectAnisoChoice = isFresh ? 3 : 1;
            const unsigned expectAnisoLevel = isFresh ? 16u : 4u;
            const int expectWaterChoice = isFresh ? 1 : 0;
            const unsigned expectWaterRes = isFresh ? 512u : 256u;
            const int expectShadowChoice = isFresh ? 1 : 0;
            const unsigned expectShadowRes = isFresh ? 128u : 64u;
            check(item(list,0)->GetValue()==expectUnit&&item(list,1)->GetValue()==0&&item(list,2)->GetValue()==0&&
                  item(list,3)->GetValue()==expectAnisoChoice&&item(list,4)->GetValue()==expectWaterChoice&&
                  item(list,5)->GetValue()==expectShadowChoice,"defaults match fresh vs saved-off profile");
            frame();live(expectUnit!=0,false,false,expectAnisoLevel,true,expectWaterRes,expectShadowRes);
            if(scenario=="defaults" || scenario=="fresh") {
                apply(list);check(ini()==before,"unchanged rows do not manufacture preferences");
                if(scenario=="fresh") { struct stat state{}; check(stat(iniPath().c_str(),&state)!=0&&errno==ENOENT,"fresh default Apply creates no settings file"); }
            } else if(scenario=="wording" || scenario=="wording-unsaved" || scenario=="wording-frontend") {
                wording(list, scenario);
            } else if(scenario=="hw-cap-2x") {
                CeAnisoSetHardwareCaps(2, true);
                CeRenderTargetQualitySetHardwareCaps(512, 512);
                CMenuMenuItem *aniso = item(list, 3);
                check(aniso->GetNumChoices()==2,"2x GPU exposes Off and 2x choices");
                check(same(aniso->GetChoiceText(0), L"Off") && same(aniso->GetChoiceText(1), L"2x"), "2x GPU labels choice 1 honestly as 2x");
                check(aniso->GetValue()==1,"stored 4x maps to active 2x choice on 2x GPU");
                check(item(list,4)->GetNumChoices()==2,"512px GPU caps water reflections at High");
                check(item(list,5)->GetNumChoices()==3,"512px GPU still offers Ultra 256px dynamic shadows");
                aniso->mSelection=0; apply(list); frame();
                check(CE_AnisoLevelChoice()==0 && CeAnisoEffectiveLevel(4, 2, true)==1u && !CeAnisoUseAnisotropicFilter(4, 2, true), "2x GPU can switch to Off");
                aniso->mSelection=1; apply(list); frame();
                check(CE_AnisoLevelChoice()==4 && CeAnisoEffectiveLevel(4, 2, true)==2u && CeAnisoUseAnisotropicFilter(4, 2, true), "2x GPU restores 2x AF cleanly");
            } else if(scenario=="invalid") {
                item(list,0)->mSelection=2;item(list,0)->Commit();
                item(list,1)->mSelection=2;item(list,1)->Commit();
                item(list,2)->mSelection=-1;item(list,2)->Commit();
                item(list,3)->mSelection=4;item(list,3)->Commit();
                item(list,4)->mSelection=3;item(list,4)->Commit();
                item(list,5)->mSelection=3;item(list,5)->Commit();
                check(item(list,0)->GetValue()==0&&item(list,1)->GetValue()==0&&item(list,2)->GetValue()==0&&
                      item(list,3)->GetValue()==1&&item(list,4)->GetValue()==0&&item(list,5)->GetValue()==0,"invalid values refused");
                check(item(list,0)->mSelection==0&&item(list,1)->mSelection==0&&item(list,2)->mSelection==0&&
                      item(list,3)->mSelection==1&&item(list,4)->mSelection==0&&item(list,5)->mSelection==0,"native commit remains coherent after refusal");
                check(ini()==before&&notices==0,"invalid value does not reach settings writer");
                check(!wcscmp(item(list,0)->GetText(),L"High-detail units:"),"the label does not change on a refusal");
                check(same(item(list,0)->GetChoiceText(item(list,0)->GetValue()),L"No (not changed)"),"refusal is visible after the value in effect");
                check(same(item(list,0)->GetChoiceText(1),L"Yes"),"the choice not in effect carries no state");
                check(same(item(list,2)->GetChoiceText(0),L"Normal (not changed)"),"the terrain refusal reads the same way");
                check(same(item(list,3)->GetChoiceText(1),L"4x (not changed)"),"the aniso refusal reads the same way");
                check(same(item(list,4)->GetChoiceText(0),L"Normal (not changed)"),"the water refusal reads the same way");
                check(same(item(list,5)->GetChoiceText(0),L"Normal (not changed)"),"the shadow refusal reads the same way");
            } else {
                if(scenario=="keyboard") {
                    for(unsigned i=0;i<3;++i) {
                        auto p=item(list,i);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_RIGHT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                        check(p->mSelection==1,"actual keyboard selection");
                    }
                    for(unsigned i=3;i<6;++i) {
                        auto p=item(list,i);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_RIGHT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                    }
                    check(item(list,3)->mSelection==2&&item(list,4)->mSelection==1&&item(list,5)->mSelection==1,"keyboard selection on multi-tier rows");
                } else if(scenario=="mouse") {
                    for(unsigned i=0;i<3;++i) {
                        item(list,i)->mExpanded=true;item(list,i)->FixtureMouseSelect(1);
                        check(item(list,i)->mSelection==1,"actual mouse choice selection");
                    }
                    item(list,3)->mExpanded=true;item(list,3)->FixtureMouseSelect(3);
                    item(list,4)->mExpanded=true;item(list,4)->FixtureMouseSelect(2);
                    item(list,5)->mExpanded=true;item(list,5)->FixtureMouseSelect(2);
                    check(item(list,3)->mSelection==3&&item(list,4)->mSelection==2&&item(list,5)->mSelection==2,"actual mouse choice selection on tiers");
                } else {
                    const bool oneRow = scenario=="one-row";
                    choose(list,1,oneRow?0:1,oneRow?0:1);
                    item(list,3)->mSelection=(oneRow?1:3);
                    item(list,4)->mSelection=(oneRow?0:2);
                    item(list,5)->mSelection=(oneRow?0:2);
                }
                for(unsigned i=0;i<6;++i) item(list,i)->FixtureRenderSync();
                check(item(list,0)->mSelection==1,"pending choice survives native render synchronization");
                check(CE_FullUnitModelsChoice()==0&&CE_FullTreeModelsChoice()==0&&CE_TerrainDetailChoice()==0&&
                      CE_AnisoLevelChoice()==4&&CE_WaterReflectionResChoice()==256&&CE_DynamicShadowResChoice()==64,"selection waits for Apply");
                check(ini()==before,"selection alone writes no preferences");
                CApplyMenuItem action{&list};action.ReceiveButtonAction(nullptr,0,1.f);
                check(gMenuApplyCount==0,"unrelated button is not Apply");
                const bool allFail=scenario=="save-fails"||scenario=="retry-later";
                const bool partial=scenario=="partial-save";
                if(allFail) failRenames=100;else if(partial) failRenames=1;
                apply(list);failRenames=0;
                check(gMenuApplyCount==1,"native Apply traverses all rows once");
                const bool oneRow = scenario=="one-row";
                const int wantAniso = oneRow ? 4 : (scenario=="keyboard" ? 8 : 16);
                const unsigned wantWater = oneRow ? 256u : (scenario=="keyboard" ? 512u : 1024u);
                const unsigned wantShadow = oneRow ? 64u : (scenario=="keyboard" ? 128u : 256u);
                check(CE_FullUnitModelsChoice()==1&&CE_FullTreeModelsChoice()==(oneRow?0:1)&&CE_TerrainDetailChoice()==(oneRow?0:1)&&
                      CE_AnisoLevelChoice()==wantAniso&&CE_WaterReflectionResChoice()==int(wantWater)&&CE_DynamicShadowResChoice()==int(wantShadow),
                      "valid choices stay queued even if persistence fails");
                live(false,false,false,4,true,256,64);
                frame();
                live(true,!oneRow,!oneRow,static_cast<unsigned>(wantAniso),true,wantWater,wantShadow);
                for (unsigned idx=0; idx<6; ++idx) check(!item(list,idx)->HasPendingChange(),"native commit records requested values");
                if(allFail) {
                    check(ini()==before&&notices==6,"failed writes preserve old data and issue visible feedback");
                    for (unsigned idx=0; idx<6; ++idx) check(unsaved(item(list,idx)),"all failed saves labelled");
                    List reopened;NativeRows(&reopened);
                    check(item(reopened,0)->GetValue()==1&&unsaved(item(reopened,0)),"unit session choice and notice survive reopening");
                    check(item(reopened,4)->GetValue()==2&&unsaved(item(reopened,4)),"water session choice and notice survive reopening");
                    check(item(reopened,5)->GetValue()==2&&unsaved(item(reopened,5)),"shadow session choice and notice survive reopening");
                    if(scenario=="retry-later") {
                        item(list,2)->mSelection=0;apply(list);frame();live(true,true,false,16,true,1024,256);
                        for (unsigned idx=0; idx<6; ++idx) check(!unsaved(item(list,idx)),"later Apply saves session preference and clears all notices");
                        check(ini().find("full_unit_models=1\n")!=std::string::npos&&ini().find("full_tree_models=1\n")!=std::string::npos&&
                              ini().find("terrain_detail_distance=0\n")!=std::string::npos&&ini().find("anisotropic_filtering=16\n")!=std::string::npos&&
                              ini().find("water_reflection_res=1024\n")!=std::string::npos&&ini().find("dynamic_shadow_res=256\n")!=std::string::npos,
                              "later saved values");
                    }
                } else if(partial) {
                    check(unsaved(item(list,0))&&!unsaved(item(list,1))&&!unsaved(item(list,2))&&!unsaved(item(list,3))&&
                          !unsaved(item(list,4))&&!unsaved(item(list,5))&&notices==1,"save outcomes stay independent");
                    check(ini().find("full_unit_models=0\n")!=std::string::npos&&ini().find("full_tree_models=1\n")!=std::string::npos&&
                          ini().find("water_reflection_res=1024\n")!=std::string::npos&&ini().find("dynamic_shadow_res=256\n")!=std::string::npos,
                          "later writes preserve first refused preference");
                } else {
                    for (unsigned idx=0; idx<6; ++idx) check(!unsaved(item(list,idx)),"successful save has ordinary labels");
                    check(notices==0,"no save-failed notice");
                    check(ini().find("unknown=keep\n")!=std::string::npos,"unrelated preference preserved");
                    check(ini().find("full_unit_models=1\n")!=std::string::npos,"unit models preference persisted");
                    if(oneRow) check(ini().find("full_tree_models=0\n")!=std::string::npos&&ini().find("terrain_detail_distance=0\n")!=std::string::npos&&
                                     ini().find("water_reflection_res=256\n")!=std::string::npos&&ini().find("dynamic_shadow_res=64\n")!=std::string::npos,
                                     "untouched preferences preserved");
                    if(scenario=="round-trip") {
                        choose(list,0,0,0);item(list,3)->mSelection=0;item(list,4)->mSelection=0;item(list,5)->mSelection=0;
                        apply(list);live(true,true,true,16,true,1024,256);frame();live(false,false,false,0,true,256,64);
                        check(ini().find("full_unit_models=0\n")!=std::string::npos&&ini().find("full_tree_models=0\n")!=std::string::npos&&
                              ini().find("terrain_detail_distance=0\n")!=std::string::npos&&ini().find("anisotropic_filtering=0\n")!=std::string::npos&&
                              ini().find("water_reflection_res=256\n")!=std::string::npos&&ini().find("dynamic_shadow_res=64\n")!=std::string::npos,
                              "all options switch back live");
                    }
                }
            }
        }
    }
    std::puts("PASS");
}
