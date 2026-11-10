// SPDX-License-Identifier: GPL-3.0-only
#include "ce_project.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
std::string gameDirectory, userDirectory;
int checks = 0, failures = 0, careerMutations = 0, careerReads = 0, careerWrites = 0;
void Check(bool value, const char* message) { ++checks; if (!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",message); } }
struct FVector { float X, Y, Z; };
struct CInitThing { char mName[128]; FVector mPos; };
struct CThing { FVector loaded; void Init(CInitThing* init) { loaded = init->mPos; } };
typedef int SINT; typedef int BOOL;
const BOOL FALSE = 0, TRUE = 1;
const int MCE_FAILURE = -1;
#include "ce_project_hooks.private.h"
}
const char* PortGameDir() { return gameDirectory.c_str(); }
const char* PortUserDir() { return userDirectory.c_str(); }
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    gameDirectory = argv[2]; userDirectory = argv[3]; std::string scenario = argv[4];
    if (scenario != "off") { setenv("BEA_CE_PROJECT",argv[1],1); setenv("BEA_USER_DIR",argv[3],1); }
    else { unsetenv("BEA_CE_PROJECT"); unsetenv("BEA_USER_DIR"); }
    if (scenario == "network") setenv("BEA_NET_HOST","authored-network-intent",1);
    const bool active = scenario != "off";
    Check(PortCeProjectRequested() == active,"project selection is explicit");
    Check(PortCeProjectCareerAllowed() == !active,"selected project fences career even before validation");
    CareerMutators(); CareerStorage();
    Check(careerMutations == (active ? 0 : 7),"all source-bound career mutators are fenced before side effects");
    Check(careerReads == (active ? 0 : 1) && careerWrites == (active ? 0 : 3),"all source-bound career read/write boundaries are fenced");
    int level = PortCeProjectLaunchLevel();
    if (scenario == "invalid" || scenario == "network") {
        Check(level < 0,"malformed or unsafe launch refuses before world loading");
        Check(PortCeProjectExitStatus() == 1,"a refused launch reports failure as its exit status");
    }
    else if (scenario == "off") {
        Check(level < 0 && PortCeProjectBeginWorld(777,false),"ordinary launches retain their own level selection");
        float x=1,y=2,z=3;
        Check(PortCeProjectApplyPlacement("AuthoredAnchor",15,false,&x,&y,&z) && x==1 && y==2 && z==3 && PortCeProjectEndWorld(false),"ordinary world initialization is unchanged");
    } else {
        Check(level == 777,"authored project selects its linked installed map");
        if (scenario == "wrong-map") {
            Check(!PortCeProjectBeginWorld(778,false),"a different map is refused");
            Check(PortCeProjectExitStatus() == 1,"a refused map reports failure as its exit status");
        } else if (scenario == "no-map") {
            Check(PortCeProjectExitStatus() == 1,"a launch whose map never loaded reports failure");
            Check(std::strstr(PortCeProjectMessage(),"did not load") != nullptr,"a map that never loaded has its own diagnostic");
        } else {
            CInitThing anchor = {}, other = {}; std::strcpy(anchor.mName,"AuthoredAnchor"); std::strcpy(other.mName,"UnrelatedObject");
            anchor.mPos = {1,2,3}; other.mPos = {4,5,6}; CThing a = {}, b = {};
            CInitThing* inits[] = {&anchor,&other}; CThing* things[] = {&a,&b};
            if (scenario == "missing") std::strcpy(anchor.mName,"MissingObject");
            if (scenario == "duplicate") std::strcpy(other.mName,"AuthoredAnchor");
            if (scenario == "near-miss") { std::strcpy(anchor.mName,"AuthoredAnchorX"); std::strcpy(other.mName,"AuthoredAncho"); }
            if (scenario == "base-only" || scenario == "base-and-level") {
                // The base world loads before the level, and CE binds only the level's own objects: a base object of the
                // bound name keeps its position and is not a second match.
                CInitThing baseAnchor = {}; std::strcpy(baseAnchor.mName,"AuthoredAnchor"); baseAnchor.mPos = {7,8,9}; CThing c = {};
                CInitThing* baseInits[] = {&baseAnchor}; CThing* baseThings[] = {&c};
                Check(LoadAuthoredWorld(level,baseInits,baseThings,1,TRUE),"the base world loads in project mode");
                Check(c.loaded.X==7 && c.loaded.Y==8 && c.loaded.Z==9,"a base-world object of the bound name keeps its position");
                if (scenario == "base-only") std::strcpy(anchor.mName,"LevelObject");
            }
            bool expected = scenario == "load" || scenario == "base-and-level";
            Check(LoadAuthoredWorld(level,inits,things,2,FALSE) == expected,"actual patch hooks accept only one complete unambiguous binding");
            Check(PortCeProjectLoaded() == expected,"a failed or partial world load cannot report project success");
            Check(PortCeProjectExitStatus() == (expected ? 0 : 1),"the exit status is the project's outcome");
            if (scenario == "near-miss") Check(a.loaded.X==1 && b.loaded.X==4,"names that only begin alike are not the bound name");
            if (expected) {
                Check(a.loaded.X==12.5f && a.loaded.Y==-7.25f && a.loaded.Z==-3.0f,"the authored transform reaches object Init in Aquila XYZ");
                Check(b.loaded.X==4 && b.loaded.Y==5 && b.loaded.Z==6,"an unrelated installed object is unchanged");
                Check(std::strstr(PortCeProjectMessage(),"placements loaded") != nullptr,"the successful project load has an honest visible status");
                Check(!PortCeProjectBeginWorld(level,false),"the prototype cannot chain into another world load");
                Check(PortCeProjectExitStatus() == 1,"a refused second load reports failure");
            }
        }
    }
    std::printf("CE_PROJECT_RUNTIME: %d checks, %d failures.\n",checks,failures);
    return failures ? 1 : 0;
}
