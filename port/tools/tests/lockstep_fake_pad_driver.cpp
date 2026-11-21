// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cstdint>

#define PORT_LOG_PREFIX "test: "
using Sint16 = int16_t;
using Uint8 = uint8_t;
using SDL_JoystickID = unsigned;
struct SDL_Joystick { int axes[4]; bool buttons[8]; unsigned hats[1]; };
static unsigned present;
[[maybe_unused]] static bool running;
static int queries, updates, failures;
static SDL_Joystick devices[2];
extern "C" unsigned PortPresentCount() { return present; }
extern "C" int CE_LockstepTicking(void) __attribute__((weak));
#ifndef NO_LOCKSTEP_PROVIDER
extern "C" int CE_LockstepTicking() { ++queries; return running; }
#endif
static void Check(bool yes, const char *label) { if (!yes) { ++failures; std::fprintf(stderr,"FAIL %s\n",label); } }
static void SDL_SetJoystickVirtualAxis(SDL_Joystick *p,int n,Sint16 value) { Check(n>=0 && n<4,"axis bound"); if(n>=0&&n<4)p->axes[n]=value; }
static void SDL_SetJoystickVirtualButton(SDL_Joystick *p,int n,bool value) { Check(n>=0 && n<8,"button bound"); if(n>=0&&n<8)p->buttons[n]=value; }
static void SDL_SetJoystickVirtualHat(SDL_Joystick *p,int n,Uint8 value) { Check(n==0,"hat bound"); if(n==0)p->hats[n]=value; }
static void SDL_UpdateJoysticks() { ++updates; }
static void SDL_CloseJoystick(SDL_Joystick *) {}
static void SDL_DetachVirtualJoystick(SDL_JoystickID) {}
@struct@
static void FakePadPlug(int n) { gFakePads[n].pad=&devices[n]; gFakePads[n].id=n+1; gFakePads[n].axes=4; gFakePads[n].buttons=8; gFakePads[n].hats=1; }
@attach@
@clock@
@step@

static bool Neutral(const SDL_Joystick &d) {
    for(int v:d.axes) if(v) return false;
    for(bool v:d.buttons) if(v) return false;
    return d.hats[0]==0;
}
static void Step(unsigned p) { present=p; FakePadStep(); }
static void Reset() {
    for(auto &f:gFakePads) f={}; for(auto &d:devices) d={};
    running=false; queries=updates=0; present=0;
    unsetenv("BEA_PORT_FAKE_PAD"); unsetenv("BEA_PORT_FAKE_PAD2");
    unsetenv("BEA_PORT_FAKE_PAD_KIND"); unsetenv("BEA_PORT_FAKE_PAD_GAME"); unsetenv("BEA_PORT_FAKE_PAD_LATE"); unsetenv("BEA_PORT_FAKE_PAD_END");
}
int main() {
    Reset(); FakePadPlug(0);
    gFakePads[0].script="120:b0:d,123:b0:u,130:h0:1,160:h0:0";
    running=true; Step(119); Check(Neutral(devices[0]),"ordinary not early");
    Step(120); Check(devices[0].buttons[0],"ordinary press at absolute Present");
    Step(123); Check(!devices[0].buttons[0],"ordinary release");
    Step(130); Check(devices[0].hats[0]==1,"ordinary hat");
    Step(160); Check(Neutral(devices[0]) && queries==0,"ordinary no session query");

    Reset(); setenv("BEA_PORT_FAKE_PAD","1",1); setenv("BEA_PORT_FAKE_PAD_GAME","60:h0:1,180:h0:0,360:b0:d,390:b0:u",1);
    FakePadAttach(); Check(!gFakePads[0].game_script,"gamepad does not select flight-stick exercise");
    Reset(); setenv("BEA_PORT_FAKE_PAD","1",1);setenv("BEA_PORT_FAKE_PAD2","1",1);
    setenv("BEA_PORT_FAKE_PAD_KIND","stick",1); setenv("BEA_PORT_FAKE_PAD_GAME","60:h0:1,180:h0:0,360:b0:d,390:b0:u",1);
    FakePadAttach(); Check(gFakePads[0].game_script && !gFakePads[1].game_script,"only first flight stick selects exercise");
    gFakePads[0].script="8:h0:4,12:h0:0";
    Step(8); Check(devices[0].hats[0]==4,"frontend script works before connection");
    Step(9000); Check(!gFakePads[0].game_started,"connection wait cannot start or consume game script");
    devices[0].axes[3]=100; devices[0].buttons[7]=true; devices[0].hats[0]=4;
    running=true; Step(10000);
#if defined(CE_VERSION) && !defined(NO_LOCKSTEP_PROVIDER)
    Check(gFakePads[0].game_started && gFakePads[0].game_present==10000,"starts at actual running transition");
    Check(Neutral(devices[0]),"entry releases every control");
    Step(10059); Check(Neutral(devices[0]),"relative script not early");
    Step(10060); Check(devices[0].hats[0]==1,"relative movement starts");
    Step(10180); Check(Neutral(devices[0]),"movement releases");
    Step(10360); Check(devices[0].buttons[0],"relative trigger starts");
    Step(10390); Check(Neutral(devices[0]),"trigger releases");
    devices[0].axes[1]=-600; devices[0].buttons[0]=true; devices[0].hats[0]=1;
    running=false;Step(10400);Check(Neutral(devices[0]) && gFakePads[0].game_stopped && !*gFakePads[0].script,"departure neutralizes and retires exercise");
    const int done=updates;running=true;Step(20000);
    Check(Neutral(devices[0]) && updates==done && gFakePads[0].game_present==10000,"later session cannot restart retired exercise");

    // the end screen: an end script runs from the session's end, on its own clock, and only after the exercise
    Reset(); setenv("BEA_PORT_FAKE_PAD","1",1); setenv("BEA_PORT_FAKE_PAD_KIND","stick",1);
    setenv("BEA_PORT_FAKE_PAD_GAME","60:h0:1,90:h0:0",1); setenv("BEA_PORT_FAKE_PAD_END","30:h0:4,33:h0:0,60:b0:d,63:b0:u",1);
    FakePadAttach(); Check(gFakePads[0].end_script && !gFakePads[1].end_script,"only the exercising stick takes an end script");
    Step(5000); Check(Neutral(devices[0]),"end script not before the session");
    running=true; Step(6000); Step(6060); Check(devices[0].hats[0]==1,"exercise runs before the end script");
    running=false; Step(7000); Check(Neutral(devices[0]) && gFakePads[0].game_stopped && gFakePads[0].end_present==7000,"end releases and arms the end script");
    Step(7029); Check(Neutral(devices[0]),"end script not early");
    Step(7030); Check(devices[0].hats[0]==4,"end script moves on its own clock");
    Step(7033); Check(Neutral(devices[0]),"end script releases");
    Step(7060); Check(devices[0].buttons[0],"end script presses");
    Step(7063); Check(Neutral(devices[0]),"end script releases its press");

    Reset();FakePadPlug(0);gFakePads[0].script="";gFakePads[0].game_script="30:h0:1,40:h0:0";
    running=true;Step(std::numeric_limits<unsigned>::max()-20);
    Step(8);Check(Neutral(devices[0]),"wrap not early");
    Step(9);Check(devices[0].hats[0]==1,"relative input survives Present wrap");
    Step(19);Check(Neutral(devices[0]),"wrap release");

    Reset();setenv("BEA_PORT_FAKE_PAD","0:attach,10:b0:d",1);
    setenv("BEA_PORT_FAKE_PAD_KIND","stick",1);setenv("BEA_PORT_FAKE_PAD_LATE","1",1);
    setenv("BEA_PORT_FAKE_PAD_GAME","60:h0:1,90:h0:0",1);FakePadAttach();
    running=true;Step(10);
    Check(gFakePads[0].pad && gFakePads[0].game_started && Neutral(devices[0]),"late attach cannot leak a menu press");
    Step(69);Check(Neutral(devices[0]),"late attach relative script not early");
    Step(70);Check(devices[0].hats[0]==1 && !devices[0].buttons[0],"late attach movement uses new clock");
    Step(100);Check(Neutral(devices[0]),"late attach movement releases");
#else
    Check(!gFakePads[0].game_started && queries==0,"missing provider or faithful build cannot arm exercise");
#endif
    std::printf("gameplay fake pad checks: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
