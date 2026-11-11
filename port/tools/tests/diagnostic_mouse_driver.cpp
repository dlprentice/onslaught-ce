// SPDX-License-Identifier: GPL-3.0-only
// The support includes actual production shims and complete source callers. Only SDL window/physical-state boundaries are faked.
#include <string>
#include "TEST_SUPPORT_INC"

static void DiagnosticEvent(Uint32 type, int x = 0, int y = 0, unsigned button = SDL_BUTTON_LEFT, SDL_Window *source = testWindow, bool queued = true)
{
    SDL_Event e{}; e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.xrel = (float)x; e.motion.yrel = (float)y; }
    else { e.button.button = (Uint8)button; e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN; }
    if (queued && !SDL_PushEvent(&e)) exit(7);
    if (!queued) e.common.timestamp = SDL_GetTicksNS();
#if DIAGNOSTIC_API
    PortDinputDiagnosticMouseEvent(source, &e, queued);
#endif
}
static void Focus(bool value) {
    focused = value;
    SDL_Event e{}; e.type = value ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST; e.window.windowID = 17;
    if (!SDL_PushEvent(&e)) exit(7);
}
static bool SDLCALL RejectMouseUp(void *, SDL_Event *e) { return e->type != SDL_EVENT_MOUSE_BUTTON_UP; }
static bool PairState(int x, int y, bool held) {
    return sMouseState.lX == x && sMouseState.lY == y && bool(MOUSE_HELD[0]) == held;
}
int main(int argc, char **argv)
{
    if (argc != 3 || !SDL_Init(SDL_INIT_EVENTS)) return 2;
    faithfulProfile = !strcmp(argv[2], "faithful");
    const char *test = argv[1];
    gNumWindows = 1; gWindows[0].sdl = testWindow; gWindows[0].handle = reinterpret_cast<HWND>(1); gWindows[0].proc = Proc;
    CHECK(SDL_AddEventWatch(WheelWatch, nullptr));
    Device *mouse = Make<Device>(MOUSE, 0); sMouse = mouse;
    mouse->SetCooperativeLevel(MainWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    CHECK(SUCCEEDED(mouse->Acquire()));
    if (!strcmp(test,"queue-causal")) {
        realRelativeCache = true;
        setenv("BEA_PORT_FAKE_KEYS", "m1:7:-3,b2:1:d,b4:1:u", 1);
        testPresent = 1; Drain(); CHECK(SUCCEEDED(PollMouseState()));
        int x=sMouseState.lX,y=sMouseState.lY;
        testPresent = 2; Drain(); CHECK(SUCCEEDED(PollMouseState())); bool held=MOUSE_HELD[0];
        CHECK(SUCCEEDED(PollMouseState())); CHECK(sMouseState.lX==0 && sMouseState.lY==0);
        testPresent = 4; Drain(); CHECK(SUCCEEDED(PollMouseState())); bool released=MOUSE_BUTTONS[0].mReleased;
        printf("{\"relative_x\":%d,\"relative_y\":%d,\"held\":%s,\"release\":%s,\"SDL_cache_is_real\":true}\n",x,y,held?"true":"false",released?"true":"false");
        CHECK(x==7 && y==-3 && held && released && !MOUSE_HELD[0]);
    } else if (!strcmp(test,"raw-queue-only")) {
        realRelativeCache=true; setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);
        SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION;e.motion.xrel=9;e.motion.yrel=-8;CHECK(SDL_PushEvent(&e));
        e=SDL_Event{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.down=true;CHECK(SDL_PushEvent(&e));
        // An ordinary pushed event has no authored sampler call, even with the fixture environment set.
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    }
#if DIAGNOSTIC_API
    else if (!strcmp(test,"final-motion")) {
        setenv("BEA_PORT_FAKE_KEYS","b1:1:d,b2:1:u,m3:7:-3",1);
        testPresent=1;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(MOUSE_HELD[0]);
        testPresent=2;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(!MOUSE_HELD[0]);
        testPresent=3;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(7,-3,false));
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"additive-once")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);relativeX=2;relativeY=3;relativeButtons=SDL_BUTTON_RMASK;
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(9,0,true));CHECK(sMouseState.rgbButtons[1]==0x80);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,true));CHECK(sMouseState.rgbButtons[1]==0x80);
        DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_UP);CHECK(SUCCEEDED(PollMouseState()));CHECK(!MOUSE_HELD[0]);CHECK(sMouseState.rgbButtons[1]==0x80);
    } else if (!strcmp(test,"physical-hold-survives-release")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);relativeButtons=SDL_BUTTON_LMASK;
        DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_UP);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(MOUSE_HELD[0]);PortDinputDiagnosticMouseEnd(testWindow);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(MOUSE_HELD[0]);
    } else if (!strcmp(test,"unset") || !strcmp(test,"unset-after-feed")) {
        if (!strcmp(test,"unset-after-feed")) {setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);}
        unsetenv("BEA_PORT_FAKE_KEYS");relativeX=2;relativeY=3;relativeButtons=SDL_BUTTON_RMASK;
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,8,-4);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(2,3,false));CHECK(sMouseState.rgbButtons[1]==0x80);
    } else if (!strcmp(test,"focus") || !strcmp(test,"focus-race")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);
        if (!strcmp(test,"focus-race")) {mouse->Unacquire();focused=true;injectFocusLoss=true;CHECK(SUCCEEDED(mouse->Acquire()));}
        else {DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);Focus(false);}
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,2,3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        Focus(true);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,4,-2);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(4,-2,false));
    } else if (!strcmp(test,"focus-queued-epoch")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);SDL_Event old{};old.type=SDL_EVENT_MOUSE_MOTION;old.motion.xrel=9;old.motion.yrel=8;CHECK(SDL_PushEvent(&old));
        SDL_Delay(1);Focus(false);Focus(true);CHECK(old.common.timestamp<gDiagnosticSince);
        PortDinputDiagnosticMouseEvent(testWindow,&old,true);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,2,3);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(2,3,false));
    } else if (!strcmp(test,"destroy-epoch")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);SDL_Event gone{};gone.type=SDL_EVENT_WINDOW_DESTROYED;
        PortDinputDiagnosticMouseEvent(testWindow,&gone,true);Focus(true);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,9,8);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        mouse->Unacquire();mouse->Acquire();DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,2,3);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(2,3,false));
    } else if (!strcmp(test,"wrong-event-window")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,3,4);
        SDL_Event other{};other.type=SDL_EVENT_MOUSE_MOTION;other.motion.windowID=18;other.motion.xrel=99;CHECK(SDL_PushEvent(&other));PortDinputDiagnosticMouseEvent(testWindow,&other,true);
        DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_UP,0,0,SDL_BUTTON_LEFT,reinterpret_cast<SDL_Window *>(2),false);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(3,4,false));
    } else if (!strcmp(test,"queue-filter-failure")) {
        setenv("BEA_PORT_FAKE_KEYS","b1:1:d,b2:1:u,m3:7:-3",1);testPresent=1;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(MOUSE_HELD[0]);
        SDL_SetEventFilter(RejectMouseUp,nullptr);testPresent=2;Drain();SDL_SetEventFilter(nullptr,nullptr);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        testPresent=3;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"pair-bounds")) {
        std::string limit="b1:1:d";for(int i=0;i<256;i++)limit+=",m2:0:0";limit+=",b3:1:u";CHECK(!DiagnosticMousePair(limit.c_str(),1,1));
        std::string bytes="b1:1:d";for(int i=0;i<200;i++)bytes+=",a2:0.00000000000000000000000000000000000000000000000000000000000000000000000000000000001:1";bytes+=",b3:1:u";CHECK(!DiagnosticMousePair(bytes.c_str(),1,1));
        CHECK(DiagnosticMousePair("b1:1:d,c2:11,3:26:11:d,b4:1:u",1,1));
    } else if (!strcmp(test,"reacquire")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(SUCCEEDED(mouse->Unacquire()));CHECK(SUCCEEDED(mouse->Acquire()));
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"before-acquire")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);mouse->Unacquire();DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);mouse->Acquire();
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"wrong-window")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,3,4);
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,99,99,SDL_BUTTON_LEFT,reinterpret_cast<SDL_Window *>(2));
        DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN,0,0,SDL_BUTTON_LEFT,reinterpret_cast<SDL_Window *>(2));
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(3,4,false));
    } else if (!strcmp(test,"destroy")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        CHECK(PortDestroyWindow(MainWindow()));CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"replay") || !strcmp(test,"record")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,7,-3);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (!strcmp(test,"replay")) replay=true;else recording=true;
        CHECK(SUCCEEDED(PollMouseState()));
        if (replay) CHECK(PairState(3,-4,true));else CHECK(PairState(0,0,false));
        replay=recording=false;CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"record-queue") || !strcmp(test,"replay-queue")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:7:-3,b2:1:d,b4:1:u",1);
        if (!strcmp(test,"record-queue")) {recording=true;setenv("BEA_PORT_RECORD","fixture",1);}else {replay=true;setenv("BEA_PORT_TAPE","fixture",1);}
        testPresent=1;Drain();testPresent=2;Drain();testPresent=4;Drain();
        if(recording) CHECK(recordedMotion==1 && recordedButtons==2 && recordedWindowMouse==3);
        replay=recording=false;unsetenv("BEA_PORT_RECORD");unsetenv("BEA_PORT_TAPE");CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"queue-failure") || !strcmp(test,"overflow") || !strcmp(test,"sample-overflow")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (!strcmp(test,"overflow")) for(int i=0;i<33;i++)DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,32767,0);
        else if (!strcmp(test,"sample-overflow")) {DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,32767,0);relativeX=2147483520.f;}
        else DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_UP,0,0,SDL_BUTTON_LEFT,testWindow,false);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(!MOUSE_HELD[0]);
        CHECK(sMouseState.lX==(!strcmp(test,"sample-overflow")?2147483520:0));
        DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,2,3);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        PortDinputDiagnosticMouseEnd(testWindow);DiagnosticEvent(SDL_EVENT_MOUSE_MOTION,2,3);CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(2,3,false));
    } else if (!strcmp(test,"invalid-pairs")) {
        CHECK(DiagnosticMousePair("b1:1:d,m2:1:1,b4:1:u",1,1));
        CHECK(!DiagnosticMousePair("b1:1:d",1,1));CHECK(!DiagnosticMousePair("b1:1:d,garbage,b4:1:u",1,1));
        CHECK(!DiagnosticMousePair("b1:1:d,m99:1:1,b4:1:u",1,1));CHECK(!DiagnosticMousePair("b1:1:d,b4000:1:u",1,1));
        setenv("BEA_PORT_FAKE_KEYS","b1:1:d",1);testPresent=1;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"invalid-motion")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);
        const float invalid[]={32768.f,0.5f,std::nanf(""),INFINITY};
        for(float value:invalid) {
            DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
            SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.xrel=value;CHECK(SDL_PushEvent(&e));
            PortDinputDiagnosticMouseEvent(testWindow,&e,true);
            CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
            PortDinputDiagnosticMouseEnd(testWindow);
        }
    } else if (!strcmp(test,"button-map")) {
        setenv("BEA_PORT_FAKE_KEYS","m1:1:1",1);
        const unsigned native[]={SDL_BUTTON_LEFT,SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE,SDL_BUTTON_X1,SDL_BUTTON_X2};
        for(unsigned index=0;index<5;++index) {
            DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_DOWN,0,0,native[index]);
            CHECK(SUCCEEDED(PollMouseState()));
            for(unsigned b=0;b<5;++b)CHECK(sMouseState.rgbButtons[b]==(b==index?0x80:0));
            DiagnosticEvent(SDL_EVENT_MOUSE_BUTTON_UP,0,0,native[index]);
            CHECK(SUCCEEDED(PollMouseState()));
            for(unsigned b=0;b<5;++b)CHECK(sMouseState.rgbButtons[b]==0);
        }
    } else if (!strcmp(test,"wrapped-button") || !strcmp(test,"malformed-head") || !strcmp(test,"malformed-motion")) {
        const char *fixture=!strcmp(test,"wrapped-button")?"b1:257:d,b2:257:u":
            !strcmp(test,"malformed-head")?"b1:1:dGARBAGE,b2:1:u":"m1:7:-3GARBAGE";
        realRelativeCache=true;setenv("BEA_PORT_FAKE_KEYS",fixture,1);
        testPresent=1;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        printf("{\"rejected_current_token\":\"%s\",\"held\":false,\"dx\":0,\"dy\":0}\n",test);
        testPresent=2;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    } else if (!strcmp(test,"complete-token-bounds")) {
        DiagnosticMouseToken token{};
        CHECK(ParseDiagnosticMouseToken("m1:+7:-3",&token) && token.x==7 && token.y==-3);
        CHECK(ParseDiagnosticMouseToken("m4294967295:-32767:32767",&token) && token.present==UINT_MAX);
        CHECK(!ParseDiagnosticMouseToken("m4294967296:7:-3",&token));
        CHECK(!ParseDiagnosticMouseToken("m1:4294967303:-3",&token));
        CHECK(!ParseDiagnosticMouseToken("m1:7:-3GARBAGE",&token));
        CHECK(!ParseDiagnosticMouseToken("m1:7:-3:4",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:257:d",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:4294967297:d",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:-255:d",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:0:d",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:6:d",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:1:dGARBAGE",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:1:d:2:3GARBAGE",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:1:d:nan:3",&token));
        CHECK(!ParseDiagnosticMouseToken("b1:1:d:2:inf",&token));
        CHECK(!DiagnosticMousePair("b1:1:d,m2:1:1GARBAGE,b4:1:u",1,1));
        CHECK(!DiagnosticMousePair("b1:1:d,b4:1:uGARBAGE",1,1));
    } else if (!strcmp(test,"coordinate-pair")) {
        setenv("BEA_PORT_FAKE_KEYS","b1:1:d:4.5:6,b2:1:u:4.5:6,m3:+7:-3",1);
        testPresent=1;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,true));
        testPresent=2;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
        testPresent=3;Drain();CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(7,-3,false));
    } else if (!strcmp(test,"legacy-menu-malformed")) {
        mouse->Unacquire();
        setenv("BEA_PORT_FAKE_KEYS","b1:257:dGARBAGE,b2:257:u",1);
        testPresent=1;Drain();CHECK(MOUSE_HELD[0]);
        testPresent=2;Drain();CHECK(!MOUSE_HELD[0]);CHECK(windowMouse==2);
    } else if (!strcmp(test,"legacy-record-malformed")) {
        recording=true;setenv("BEA_PORT_RECORD","fixture",1);
        setenv("BEA_PORT_FAKE_KEYS","b1:257:dGARBAGE,b2:257:u,m3:7:-3GARBAGE",1);
        testPresent=1;Drain();testPresent=2;Drain();testPresent=3;Drain();
        CHECK(recordedMotion==1 && recordedButtons==2 && recordedWindowMouse==3);
        CHECK(SUCCEEDED(PollMouseState()));CHECK(PairState(0,0,false));
    }
#endif
    else return 2;
    if (sMouse) {mouse->Release();sMouse=nullptr;}
    SDL_RemoveEventWatch(WheelWatch,nullptr);SDL_Quit();
    return 0;
}
