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
static void frame() { CeModelQualityBeginFrame(); CeTerrainQualityBeginFrame(); }
static void live(bool models, bool terrain)
{
    check(CeModelQualityEnabled()==models,"full-model active snapshot");
    check(CeTerrainGeometryDistanceSquared(16.f)==(terrain?4.f:16.f),"terrain active snapshot");
}
static bool unsaved(CMenuMenuItem *p) { return wcsstr(p->GetChoiceText(p->GetValue()),L"not saved")!=nullptr; }
// The real policy decides readiness from a check the terrain reserve installs when a level initialises.
static bool terrainReady = true;
static bool TerrainReady() { return terrainReady; }
static void apply(List &list)
{
    CApplyMenuItem action{&list}; action.ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
}
static void choose(List &list,int full,int terrain) { item(list,0)->mSelection=full; item(list,1)->mSelection=terrain; }

static bool same(const wchar_t *a, const wchar_t *b) { return !wcscmp(a, b); }

// The Terrain row names what was asked and says so when the frame draws something else.
static void wording(List &list, const std::string &scenario)
{
    CMenuMenuItem *terrain = item(list, 1);
    const bool failing = scenario == "wording-unsaved";
    if (scenario == "wording-frontend") CeTerrainQualityResourceCheck(nullptr);   // no level, nothing initialised
    check(same(terrain->GetChoiceText(0), L"Normal") && same(terrain->GetChoiceText(1), L"Extended"), "plain choices");
    choose(list, 0, 1);
    if (failing) failRenames = 100;
    apply(list);
    failRenames = 0;
    check(CE_TerrainDetailChoice() == 1, "request queued");
    // A request no frame has sampled yet is simply the choice: pending must never read as a failure.
    check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_PENDING, "pending until a frame samples the request");
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (not saved)" : L"Extended"), "pending reads as the plain choice");
    if (scenario == "wording-frontend") {
        frame();   // a frame with no initialised level has nothing to refuse
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
    check(same(terrain->GetChoiceText(0), failing ? L"Normal (not saved)" : L"Normal"), "the other choice is not annotated");
    check(same(item(list, 0)->GetChoiceText(1), L"On"), "the models row is untouched: not requested, not saved, not annotated");
    terrainReady = true;
    frame();
    check(CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE && CeTerrainGeometryDistanceSquared(16.f) == 4.f, "recovered");
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (not saved)" : L"Extended"), "recovery clears the wording");
    // Choosing Normal after a refusal leaves nothing to explain.
    terrainReady = false;
    frame();
    check(same(terrain->GetChoiceText(1), failing ? L"Extended (using Normal, not saved)" : L"Extended (using Normal)"), "refused again");
    choose(list, 1, 0);
    apply(list);
    frame();
    check(CE_TerrainDetailChoice() == 0 && CE_TerrainDetailStatus() == CE_TERRAIN_DETAIL_ACTIVE, "Normal needs no reserve");
    // The later Apply wrote the preference, so the unsaved notice is gone too.
    check(same(terrain->GetChoiceText(1), L"Extended"), "Extended is plain once Normal is chosen");
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
        auto p=static_cast<CMenuMenuItem *>(PortCeFullModelsItem());
        p->SetValue(1);delete p;
        check(ini()==before && notices==0,"fenced request does not persist or show a save notice");
        const bool retained = scenario=="menus-off";
        check(CE_FullModelsChoice()==int(retained)&&CE_TerrainDetailChoice()==int(retained),"menu availability does not override faithful or stored rendering policy");
        frame();live(retained,retained);
    } else {
        check(list.mItems.size()==2,"exactly two native quality rows");
        check(!wcscmp(item(list,0)->GetText(),L"Full models: "),"full models row name");
        check(!wcscmp(item(list,1)->GetText(),L"Terrain detail distance: "),"terrain row name");
        check(!wcscmp(item(list,0)->GetChoiceText(0),L"Off")&&!wcscmp(item(list,0)->GetChoiceText(1),L"On"),"model choices");
        check(!wcscmp(item(list,1)->GetChoiceText(0),L"Normal")&&!wcscmp(item(list,1)->GetChoiceText(1),L"Extended"),"terrain choices");
        if(scenario=="stored") {
            check(item(list,0)->GetValue()==1&&item(list,1)->GetValue()==1,"existing preferences load");
            struct stat first{},last{};check(!stat(iniPath().c_str(),&first),"existing settings inode");
            apply(list);check(!stat(iniPath().c_str(),&last),"unchanged settings inode");
            check(ini()==before&&first.st_ino==last.st_ino,"unchanged Apply preserves exact file and preferences");
            frame();live(true,true);
        } else {
            check(item(list,0)->GetValue()==0&&item(list,1)->GetValue()==0,"normal defaults");
            frame();live(false,false);
            if(scenario=="defaults" || scenario=="fresh") {
                apply(list);check(ini()==before,"unchanged rows do not manufacture preferences");
                if(scenario=="fresh") { struct stat state{}; check(stat(iniPath().c_str(),&state)!=0&&errno==ENOENT,"fresh default Apply creates no settings file"); }
            } else if(scenario=="wording" || scenario=="wording-unsaved" || scenario=="wording-frontend") {
                wording(list, scenario);
            } else if(scenario=="invalid") {
                item(list,0)->mSelection=2;item(list,0)->Commit();
                item(list,1)->mSelection=-1;item(list,1)->Commit();
                check(item(list,0)->GetValue()==0&&item(list,1)->GetValue()==0,"invalid values refused");
                check(item(list,0)->mSelection==0&&item(list,1)->mSelection==0,"native commit remains coherent after refusal");
                check(ini()==before&&notices==0,"invalid value does not reach settings writer");
                check(wcsstr(item(list,0)->GetText(),L"not applied"),"refusal is visible");
            } else {
                if(scenario=="keyboard") {
                    for(unsigned i=0;i<2;++i) {
                        auto p=item(list,i);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_RIGHT,1.f);
                        p->ReceiveButtonAction(nullptr,BUTTON_FRONTEND_MENU_SELECT,1.f);
                        check(p->mSelection==1,"actual keyboard selection");
                    }
                } else if(scenario=="mouse") {
                    for(unsigned i=0;i<2;++i) {
                        item(list,i)->mExpanded=true;item(list,i)->FixtureMouseSelect(1);
                        check(item(list,i)->mSelection==1,"actual mouse choice selection");
                    }
                } else choose(list,1,scenario=="one-row"?0:1);
                for(unsigned i=0;i<2;++i) item(list,i)->FixtureRenderSync();
                check(item(list,0)->mSelection==1,"pending choice survives native render synchronization");
                check(CE_FullModelsChoice()==0&&CE_TerrainDetailChoice()==0,"selection waits for Apply");
                check(ini()==before,"selection alone writes no preferences");
                CApplyMenuItem action{&list};action.ReceiveButtonAction(nullptr,0,1.f);
                check(gMenuApplyCount==0,"unrelated button is not Apply");
                const bool allFail=scenario=="save-fails"||scenario=="retry-later";
                const bool partial=scenario=="partial-save";
                if(allFail) failRenames=100;else if(partial) failRenames=1;
                apply(list);failRenames=0;
                check(gMenuApplyCount==1,"native Apply traverses both rows once");
                check(CE_FullModelsChoice()==1&&CE_TerrainDetailChoice()==(scenario=="one-row"?0:1),"valid choices stay queued even if persistence fails");
                live(false,false);frame();live(true,scenario!="one-row");
                check(!item(list,0)->HasPendingChange()&&!item(list,1)->HasPendingChange(),"native commit records requested values");
                if(allFail) {
                    check(ini()==before&&notices==2,"failed writes preserve old data and issue visible feedback");
                    check(unsaved(item(list,0))&&unsaved(item(list,1)),"both failed saves labelled");
                    List reopened;NativeRows(&reopened);
                    check(item(reopened,0)->GetValue()==1&&unsaved(item(reopened,0)),"session choice and notice survive reopening");
                    if(scenario=="retry-later") {
                        item(list,1)->mSelection=0;apply(list);frame();live(true,false);
                        check(!unsaved(item(list,0))&&!unsaved(item(list,1)),"later Apply saves session preference and clears both notices");
                        check(ini().find("full_models=1\n")!=std::string::npos&&ini().find("terrain_detail_distance=0\n")!=std::string::npos,"later saved values");
                    }
                } else if(partial) {
                    check(unsaved(item(list,0))&&!unsaved(item(list,1))&&notices==1,"save outcomes stay independent");
                    check(ini().find("full_models=0\n")!=std::string::npos&&ini().find("terrain_detail_distance=1\n")!=std::string::npos,"second write preserves first refused preference");
                } else {
                    check(!unsaved(item(list,0))&&!unsaved(item(list,1))&&notices==0,"successful save has ordinary labels");
                    check(ini().find("unknown=keep\n")!=std::string::npos,"unrelated preference preserved");
                    check(ini().find("full_models=1\n")!=std::string::npos,"models preference persisted");
                    if(scenario=="one-row") check(ini().find("terrain_detail_distance=0\n")!=std::string::npos,"untouched terrain preference preserved");
                    if(scenario=="round-trip") {
                        choose(list,0,0);apply(list);live(true,true);frame();live(false,false);
                        check(ini().find("full_models=0\n")!=std::string::npos&&ini().find("terrain_detail_distance=0\n")!=std::string::npos,"both options switch back live");
                    }
                }
            }
        }
    }
    std::puts("PASS");
}
