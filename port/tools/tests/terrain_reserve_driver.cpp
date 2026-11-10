// SPDX-License-Identifier: GPL-3.0-only
#include "reserve_types.h"
#include "ce_terrain_reserve.h"
#include "terrain_quality.h"
#include "portlog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

std::vector<CLandscapeVB*> registry;
int constructions, destructions, creates, liveDevices, failCreate;
static bool failMalloc;
static int failedMallocs, settingsReads;

extern "C" void *__real_malloc(std::size_t);
extern "C" void *__wrap_malloc(std::size_t bytes)
{
    if (failMalloc) { failMalloc=false; ++failedMallocs; return nullptr; }
    return __real_malloc(bytes);
}

static void Check(bool value, int line)
{
    if (value) return;
    std::fprintf(stderr,"Assertion failed in authored reserve driver, line %d\n",line);
    std::exit(1);
}
#define CHECK(v) Check((v),__LINE__)
bool PortCeLayer(){return true;}
extern "C" int CE_SettingGetBool(const char*,int fallback){++settingsReads;return fallback;}
void PortSessionLog(PortSessionCategory,bool,const char*,...){}

struct Fixture {
    CDXPatchList lists[3];
    CLandscapeVB *original[3];
    CDXPatchManager manager;
    int originalObjects, originalCreates, originalDevices;
    Fixture(){
        registry.reserve(4096);
        const int counts[]={4,3,2};
        const int cells[]={2,4,8};
        for(int level=0;level<3;++level){
            lists[level].mCount=counts[level];
            original[level]=new CLandscapeVB[counts[level]];
            lists[level].mPatches=reinterpret_cast<UBYTE*>(original[level]);
            for(int i=0;i<counts[level];++i)original[level][i].Create(cells[level]);
        }
        manager.mLists=lists;originalObjects=int(registry.size());
        originalCreates=creates;originalDevices=liveDevices;
    }
    void CheckOriginal(){
        const int counts[]={4,3,2};
        for(int level=0;level<3;++level){
            CHECK(lists[level].mCount==counts[level]);
            CHECK(lists[level].mPatches==reinterpret_cast<UBYTE*>(original[level]));
            for(int i=0;i<counts[level];++i){
                CHECK(original[level][i].mSlot==std::numeric_limits<UWORD>::max());
                CHECK(original[level][i].mVB!=nullptr);
            }
        }
    }
    ~Fixture(){
        CeTerrainReserveShutdown();
        CHECK(int(registry.size())==originalObjects);
        CHECK(liveDevices==originalDevices);
        for(auto *array:original)delete[]array;
        CHECK(registry.empty() && liveDevices==0);
        CHECK(constructions==destructions);
    }
};

static void Release(CLandscapeVB *p)
{
    CeTerrainReserveReleased(p);
    p->mSlot=std::numeric_limits<UWORD>::max();
}

static std::vector<CLandscapeVB*> Exhaust(int level,int expected)
{
    std::vector<CLandscapeVB*> result;
    for(int i=0;i<expected;++i){
        auto *p=CeTerrainReserveAllocate(level,static_cast<UWORD>(i+10));
        CHECK(p && p->mSlot==i+10 && p->mVB);
        CHECK(CeTerrainReserveOwns(p));
        CHECK(!CeTerrainReserveOwns(reinterpret_cast<char*>(p)+1));
        CHECK(std::find(result.begin(),result.end(),p)==result.end());
        result.push_back(p);
    }
    CHECK(!CeTerrainReserveAllocate(level,500));
    return result;
}

// The reserve is prepared at a frame boundary, and only while Extended is requested.
static void Activate()
{
    CHECK(CE_TerrainDetailRequest(1));
    CeTerrainQualityBeginFrame();
}

static std::size_t ParseFault(int &level)
{
    const char *spec=std::getenv("BEA_PORT_TERRAIN_RESERVE_FAULT");
    CHECK(spec && (spec[0]=='0'||spec[0]=='1'||spec[0]=='2') && spec[1]==':');
    level=spec[0]-'0';
    return static_cast<std::size_t>(std::strtoul(spec+2,nullptr,10));
}

// The buffers created before a fault stops a preparation, which are then rolled back.
static int MadeBeforeFault(int level,std::size_t index){return (level==2?21:level==1?12:0)+int(index);}

int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];
    {
        Fixture f;
        if(!std::strcmp(mode,"failed-create"))failCreate=creates+4;
        if(!std::strcmp(mode,"failed-malloc"))failMalloc=true;
        const bool nearest=!std::strncmp(mode,"nearest-",8);
        const char *shapeMode=nearest?mode+8:mode;
        const int shapeTier=nearest?0:1;
        const int oldCount=f.lists[shapeTier].mCount;
        if(!std::strcmp(shapeMode,"budget"))f.lists[shapeTier].mCount=1000000000;
        if(!std::strcmp(shapeMode,"invalid-size"))f.original[shapeTier][0].mSize=0;
        if(!std::strcmp(shapeMode,"invalid-cells"))f.original[shapeTier][0].mCells=0;
        CeTerrainReserveInit(&f.manager);
        f.lists[shapeTier].mCount=oldCount;
        const bool untouched=int(registry.size())==f.originalObjects && creates==f.originalCreates && liveDevices==f.originalDevices;
        if(!std::strcmp(mode,"fenced")){
            CHECK(untouched);
            CHECK(!CE_TerrainDetailRequest(1));CeTerrainQualityBeginFrame();
            CHECK(!CeTerrainQualityExtended() && settingsReads==0);
            CHECK(untouched);
        }else if(!std::strcmp(mode,"failed-create") || !std::strcmp(mode,"failed-malloc") ||
                 !std::strcmp(shapeMode,"budget") || !std::strcmp(shapeMode,"invalid-size") || !std::strcmp(shapeMode,"invalid-cells")){
            CHECK(untouched);                       // initialisation creates nothing
            CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_PENDING);
            Activate();
            CHECK(int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
            CHECK(!CeTerrainQualityExtended());
            CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
            CHECK(!CeTerrainReserveAllocate(1,1));
            if(!std::strcmp(mode,"failed-malloc"))CHECK(failedMallocs==1);
            // A refusal is not retried frame after frame, nor by repeating the same request.
            const int attempts=creates;
            for(int frame=0;frame<5;++frame)CeTerrainQualityBeginFrame();
            CHECK(CE_TerrainDetailRequest(1));CeTerrainQualityBeginFrame();
            CHECK(creates==attempts && int(registry.size())==f.originalObjects);
            CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
            // Choosing Extended again, after Normal, earns exactly one new attempt.
            auto again=[&](){
                CHECK(CE_TerrainDetailRequest(0));CeTerrainQualityBeginFrame();
                CHECK(!CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                CHECK(CE_TerrainDetailRequest(1));CeTerrainQualityBeginFrame();
            };
            if(!std::strcmp(mode,"failed-malloc")){
                again();                              // the armed refusal was spent: the new request succeeds
                CHECK(CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                CHECK(int(registry.size())==f.originalObjects+27);
                auto *p=CeTerrainReserveAllocate(1,100);CHECK(p);Release(p);
            }else if(!std::strcmp(mode,"failed-create")){
                failCreate=creates+2;                 // the new attempt fails again, at its second buffer
                again();
                CHECK(!CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                CHECK(creates==attempts+2 && int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
                for(int frame=0;frame<5;++frame)CeTerrainQualityBeginFrame();
                CHECK(creates==attempts+2);
                failCreate=0;CeTerrainReserveInit(&f.manager);   // a re-initialisation also starts afresh
                CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_PENDING);
                CeTerrainQualityBeginFrame();
                CHECK(CeTerrainQualityExtended());
                CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                auto *p=CeTerrainReserveAllocate(1,100);CHECK(p);Release(p);
            }else{
                const int objects=constructions;      // a refused shape is refused again, and creates nothing
                again();
                CHECK(!CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                CHECK(creates==attempts && constructions==objects && int(registry.size())==f.originalObjects);
            }
        }else if(!std::strcmp(mode,"fault") || !std::strcmp(mode,"fault-inert") ||
                 !std::strcmp(mode,"fault-retry") || !std::strcmp(mode,"fault-retry-every")){
            int level=0;std::size_t index=0;
            if(std::strcmp(mode,"fault-inert"))index=ParseFault(level);
            CHECK(untouched);
            Activate();
            if(!std::strcmp(mode,"fault-retry") || !std::strcmp(mode,"fault-retry-every")){
                const int made=MadeBeforeFault(level,index);
                CHECK(creates==f.originalCreates+made);
                CHECK(!CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                CHECK(int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
                CHECK(CE_TerrainDetailRequest(0));CeTerrainQualityBeginFrame();
                CHECK(CE_TerrainDetailRequest(1));CeTerrainQualityBeginFrame();      // the player chooses Extended again
                if(!std::strcmp(mode,"fault-retry")){
                    // The fault covered the first attempt only: the new request prepares the real reserve.
                    CHECK(CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                    CHECK(int(registry.size())==f.originalObjects+27);
                    CHECK(creates==f.originalCreates+made+27);
                }else{
                    // A fault on every attempt: refused again, rolled back again, nothing kept.
                    CHECK(!CeTerrainQualityExtended() && CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                    CHECK(creates==f.originalCreates+2*made);
                    CHECK(int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
                }
            }else if(!std::strcmp(mode,"fault")){
                // The device buffers made before the stop were created and then rolled back.
                const int made=MadeBeforeFault(level,index);
                CHECK(creates==f.originalCreates+made);
                CHECK(int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
                CHECK(!CeTerrainQualityExtended());
                CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                CHECK(!CeTerrainReserveAllocate(1,1) && !CeTerrainReserveAllocate(2,1));
            }else{
                CHECK(int(registry.size())==f.originalObjects+27);
                CHECK(CeTerrainQualityExtended());
                CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
            }
        }else{
            CHECK(untouched);
            CeTerrainQualityBeginFrame();
            if(!std::strcmp(mode,"normal")){
                // Normal never creates a reserved buffer, however many frames pass.
                for(int frame=0;frame<20;++frame)CeTerrainQualityBeginFrame();
                CHECK(!CeTerrainQualityExtended() && untouched);
                CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                for(int level=0;level<3;++level)CHECK(!CeTerrainReserveAllocate(level,1));
                Activate();                         // the first request is what prepares it
                CHECK(int(registry.size())==f.originalObjects+27);
            }else{
                Activate();
                CHECK(int(registry.size())==f.originalObjects+27);
                CHECK(CeTerrainQualityExtended());
                CHECK(!CeTerrainReserveAllocate(-1,1) && !CeTerrainReserveAllocate(3,1));
                CHECK(!CeTerrainReserveOwns(f.original[1]));
                if(!std::strcmp(mode,"allocate")){
                    auto nearest=Exhaust(0,12),mid=Exhaust(1,9),near=Exhaust(2,6);
                    for(auto *p:nearest){
                        CHECK(std::find(mid.begin(),mid.end(),p)==mid.end());
                        CHECK(std::find(near.begin(),near.end(),p)==near.end());
                        Release(p);
                    }
                    for(auto *p:mid)Release(p);
                    for(auto *p:near)Release(p);
                }else if(!std::strcmp(mode,"release-reset")){
                    auto mid=Exhaust(1,9);Release(mid[3]);Release(mid[3]);
                    auto *p=CeTerrainReserveAllocate(1,100);CHECK(p==mid[3]);
                    CHECK(!CeTerrainReserveAllocate(1,101));
                    CeTerrainReserveReleased(f.original[0]);
                    CDXPatchList foreign;CeTerrainReserveReset(&foreign);
                    CHECK(!CeTerrainReserveAllocate(1,102));
                    CeTerrainReserveReset(&f.lists[1]);
                    for(auto *q:mid)CHECK(q->mSlot==std::numeric_limits<UWORD>::max());
                    mid=Exhaust(1,9);for(auto *q:mid)Release(q);
                }else if(!std::strcmp(mode,"toggle")){
                    auto *p=CeTerrainReserveAllocate(1,100);CHECK(p);const int before=creates;
                    CHECK(CE_TerrainDetailRequest(0));CHECK(CeTerrainQualityExtended());
                    CeTerrainQualityBeginFrame();CHECK(!CeTerrainQualityExtended());
                    CHECK(!CeTerrainReserveAllocate(1,101));Release(p);
                    CHECK(CE_TerrainDetailRequest(1));CeTerrainQualityBeginFrame();
                    auto mid=Exhaust(1,9);for(auto *q:mid)Release(q);
                    CHECK(creates==before);
                }else if(!std::strcmp(mode,"two-views")){
                    const int capacities[]={12,9,6};
                    for(int level=0;level<3;++level){
                        auto *a=CeTerrainReserveAllocate(level,100);
                        auto *b=CeTerrainReserveAllocate(level,100);
                        CHECK(a && b && a!=b);
                        Release(a);CHECK(b->mSlot==100 && b->mVB);
                        auto rest=Exhaust(level,capacities[level]-1);
                        CHECK(std::find(rest.begin(),rest.end(),b)==rest.end());
                        for(auto *q:rest)Release(q);
                        Release(b);
                    }
                }else if(!std::strcmp(mode,"device-cycle")){
                    auto *p=CeTerrainReserveAllocate(1,100);CHECK(p);
                    for(auto *q:registry)if(CeTerrainReserveOwns(q)){CHECK(q->mVB);q->mVB=nullptr;--liveDevices;}
                    CHECK(CeTerrainQualityExtended());CeTerrainQualityBeginFrame();
                    CHECK(!CeTerrainQualityExtended() && CeTerrainReserveOwns(p));
                    CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_RESOURCE_FALLBACK);
                    CHECK(!CeTerrainReserveAllocate(1,101));Release(p);
                    const int objects=constructions;
                    for(auto *q:registry)if(CeTerrainReserveOwns(q)){q->mVB=q;++liveDevices;}
                    CeTerrainQualityBeginFrame();CHECK(CeTerrainQualityExtended());
                    CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_ACTIVE);
                    auto mid=Exhaust(1,9);for(auto *q:mid)Release(q);
                    CHECK(constructions==objects);
                }else if(!std::strcmp(mode,"reinit")){
                    // A new level (or a presentation reset that rebuilds the pools) starts from nothing again.
                    auto *p=CeTerrainReserveAllocate(1,100);CHECK(p);Release(p);
                    CeTerrainReserveShutdown();
                    CHECK(int(registry.size())==f.originalObjects && liveDevices==f.originalDevices);
                    CHECK(CE_TerrainDetailStatus()==CE_TERRAIN_DETAIL_PENDING);
                    CeTerrainQualityBeginFrame();CHECK(!CeTerrainQualityExtended());
                    CeTerrainReserveInit(&f.manager);
                    CHECK(int(registry.size())==f.originalObjects);
                    CeTerrainQualityBeginFrame();CHECK(CeTerrainQualityExtended());
                    CHECK(int(registry.size())==f.originalObjects+27);
                }else CHECK(false);
            }
            f.CheckOriginal();
        }
    }
    std::puts("PASS");
}
