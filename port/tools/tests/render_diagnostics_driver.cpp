// SPDX-License-Identifier: GPL-3.0-only
// Production diagnostic helpers/logger and actual render-method predicates; authored renderer/SDL boundaries only.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <string>
#include <vector>
#include <limits>
#include <cerrno>
#include <SDL3/SDL.h>
static unsigned clockReads, fieldReads;
Uint64 SDL_GetTicks() { ++clockReads; return 1234; }
#ifdef CE_VERSION
bool PortCeLayer() { return getenv("TEST_CE_OFF") == nullptr; }
#endif
#include SESSION_LOGGER
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stdout,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)
struct ObservedInt {
 int value = 0;
 operator int() const { ++fieldReads; return value; }
 ObservedInt &operator=(int n) { value=n; return *this; }
};
struct ObservedFloat { float value=0; operator float() const { ++fieldReads; return value; } };
struct FVector { ObservedFloat X,Y,Z; };
struct { ObservedInt mGridWidth,mGridHeight,mMapNumber; } MAP;
static bool gMirrorPass;
struct History { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
#define FALSE 0
#define TRUE 1
struct CRenderMethod {
 const char *mName="water";
 int mMethod=2;
 ObservedInt mAccepted;
 bool Try(int);
 void Accept();
 void BeginVBufCheck();
 bool CheckVBufValid();
} WATER_METHOD;
static bool VBUFTEXTURE_VALIDATE, VBUFTEXTURE_VALIDATE_FAILED;
#include RENDER_METHOD_SOURCE
static ObservedInt WATER_USE_SHADER;
#define KC_S 7544.89258f
#define KC_TOP 3772.44629f
#include RENDER_DIAGNOSTICS_SOURCE
static void Water(int method) {
#ifdef CE_VERSION
 PortWaterRenderPath(method);
#endif
}
static void Context(const FVector &pos,float water,int view=0) {
#ifdef CE_VERSION
 PortRenderContext(pos,water,view);
#endif
}
static void Sky(unsigned number) {
#ifdef CE_VERSION
 PortSkyCubeLoad(number);
#endif
}
struct Game { bool multi=false; bool IsMultiplayer() { return multi; } } GAME;
static void *sPixelShader=(void*)1,*sShader=(void*)1;
struct Landscape { void *mMirrorTexture=(void*)1; } landscape;
struct Engine { Landscape *GetLandscape() { return &landscape; } } ENGINE;
#define DIK_F1 59
struct Platform { bool held=false; bool KeyOn(int key) { CHECK(key==DIK_F1); return held; } } PLATFORM;
static std::vector<int> entered;
#include WATER_SELECTION_SOURCE
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 std::string test=argv[1];
 MAP.mGridWidth.value=512;MAP.mGridHeight.value=512;MAP.mMapNumber.value=110;
 FVector p; p.X.value=256;p.Y.value=256;p.Z.value=-50;
 if(test=="disabled") {
  CHECK(!PortSessionEnabled());unsigned before=fieldReads;errno=EDOM;
  for(int n=0;n<500;n++) { Water(n%3);Context(p,0);Sky(n%256); }
  CHECK(fieldReads==before&&clockReads==0&&errno==EDOM&&gNormal==0);
 } else {
  CHECK(PortSessionEnabled());
  if(test=="water-shader") { RunWater(2);CHECK(entered==std::vector<int>{2});CHECK(WATER_METHOD.mMethod==2&&WATER_METHOD.mAccepted.value==1); }
  else if(test=="water-fixed") { sPixelShader=nullptr;RunWater(2);CHECK(entered==std::vector<int>{2}); }
  else if(test=="water-multi") { GAME.multi=true;RunWater(2); }
  else if(test=="water-f1") { PLATFORM.held=true;RunWater(2); }
  else if(test=="water-method1") { WATER_METHOD.mMethod=1;RunWater(1);CHECK(entered==std::vector<int>{1}); }
  else if(test=="water-method0") { WATER_METHOD.mMethod=0;RunWater(0);CHECK(entered==std::vector<int>{0}); }
  else if(test=="water-fallback") { RunWater(0);CHECK(entered==std::vector<int>({2,1,0}));CHECK(WATER_METHOD.mMethod==0&&WATER_METHOD.mAccepted.value==1); }
  else if(test=="water-accepted") { RunWater(2);for(int n=0;n<100;n++)RunWater(2);CHECK(gNormal==2); }
  else if(test=="water-limit") { for(int n=0;n<500;n++){WATER_USE_SHADER.value=n%2;WATER_METHOD.mAccepted.value=n%3?1:0;Water(2);}CHECK(gNormal==32); }
  else if(test=="context-bands") { for(float height:{-1.f,0.f,1.f,32.f,33.f,128.f,129.f,512.f,513.f}) {p.Z.value=-height;Context(p,0);}CHECK(gNormal==5); }
  else if(test=="context-grid") {Context(p,0);p.X.value=-1;Context(p,0);p.X.value=0;Context(p,0);p.X.value=512;Context(p,0);p.X.value=511;p.Y.value=512;Context(p,0);MAP.mGridWidth.value=0;Context(p,0);CHECK(gNormal==5); }
  else if(test=="context-mirror-view") {gMirrorPass=true;Context(p,0);CHECK(fieldReads==0);gMirrorPass=false;Context(p,0,1);CHECK(fieldReads==0);Context(p,0);CHECK(gNormal==1); }
  else if(test=="context-nonfinite") {p.Z.value=std::numeric_limits<float>::infinity();Context(p,0);p.X.value=std::numeric_limits<float>::quiet_NaN();Context(p,0);CHECK(gNormal==2); }
  else if(test=="context-change") {Context(p,0);for(int n=0;n<300;n++)Context(p,0);CHECK(gNormal==1);MAP.mMapNumber.value=120;Context(p,0);MAP.mGridWidth.value=1024;Context(p,0);CHECK(gNormal==3); }
  else if(test=="context-limit") {for(int n=0;n<500;n++){p.Z.value=n%2?-1.f:-513.f;Context(p,0);}CHECK(gNormal==32); }
  else if(test=="sky") {Sky(7);for(int n=0;n<100;n++)Sky(7);Sky(8);CHECK(gNormal==2); }
  else if(test=="sky-limit") {for(int n=0;n<500;n++)Sky(n%256);CHECK(gNormal==32); }
  else if(test=="shared-limit") {for(int n=0;n<2048;n++)PortSessionLog(PORT_SESSION_GAME,false,"prefill");Sky(7);Context(p,0);Water(2);CHECK(gNormal==2049&&clockReads==2049);}
  else return 2;
 }
 printf("case=%s failures=%d reads=%u lines=%u clock=%u\n",argv[1],failures,fieldReads,gNormal,clockReads);
 return failures?1:0;
}
