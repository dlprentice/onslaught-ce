// SPDX-License-Identifier: GPL-3.0-only
// Actual caller/shell/skip/NextFrame definitions are inserted by close_caller_tests.py. Fake device/OS boundaries only.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
using BOOL = int; using UINT = unsigned; using SINT = int; using FLOAT = float;
using HRESULT = int32_t; using HWND = void *;
const int TRUE = 1, FALSE = 0, S_OK = 0;
#define PC 1
#define PS2 3
#define TARGET PC
const unsigned WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_USER = 0x400, RESTORE = 0x500, SKIP = 0x501;
const unsigned PM_REMOVE = 1, TIMER_GETAPPTIME = 1, TIMER_GETELAPSEDTIME = 2, MAX_JOYPADS = 4;
const HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868u);
const HRESULT D3DERR_DEVICENOTRESET = static_cast<HRESULT>(0x88760869u);
#define FAILED(value) ((value) < 0)
enum EQuitType { QT_NONE, QT_QUIT_TO_FRONTEND, QT_QUIT_TO_SYSTEM };
struct MSG { unsigned message = 0; };
struct Limit {}; struct WouldBlock {}; struct Unexpected {};
static unsigned checks, polls, scenes, render_calls, opens, closes, video_shutdowns;
static unsigned controller_news, controller_deletes, handler_shutdowns, music_plays, music_stops, dispatches;
static bool lost_device, skip_pending, close_delivered;
static std::deque<unsigned> queue;
static void Check(bool value) { ++checks; if (!value) throw Unexpected{}; }
static void Budget(unsigned count) { if (count > 12) throw Limit{}; }
static unsigned sKeysRead, spKeysRead;
struct CGame { int quit = 0; void SetQuit(int value) { quit = value; } void RollCredits(); } GAME;
struct FrontEnd { int quit = 0; void SetQuit(int value) { quit = value; } } FRONTEND;
BOOL PeekMessage(MSG *, HWND, unsigned, unsigned, unsigned);
BOOL GetMessage(MSG *, HWND, unsigned, unsigned);
int TranslateAccelerator(HWND, void *, MSG *) { return 0; }
void TranslateMessage(MSG *) {}
void DispatchMessage(MSG *);
void SendMessage(HWND, unsigned, unsigned, unsigned) { throw Unexpected{}; }
void Sleep(unsigned) { throw Limit{}; }
FLOAT DXUtil_Timer(unsigned request) { return request == TIMER_GETAPPTIME ? 7.0f : 0.25f; }
@CE_LAYER@
struct Device { HRESULT TestCooperativeLevel() { return lost_device ? D3DERR_DEVICELOST : S_OK; } };
struct DisplayMode { int Format = 0; };
struct D3DAdapterInfo { DisplayMode d3ddmDesktop; };
struct D3D { void GetAdapterDisplayMode(unsigned, DisplayMode *) { throw Unexpected{}; } };
struct Parameters { int BackBufferFormat = 0; };
struct PCLTShell {
    BOOL m_bActive = TRUE, m_bReady = TRUE, m_bWindowed = TRUE, m_bFrameMoving = TRUE, m_bSingleStep = TRUE;
    FLOAT m_fTime = -1, m_fElapsedTime = -2, mScreenShapeScale = 0.75f;
    MSG mMsg; HWND m_hWnd = reinterpret_cast<HWND>(1); void *mhAccel = nullptr;
    Device *m_pd3dDevice = nullptr; D3D *m_pD3D = nullptr; D3DAdapterInfo m_Adapters[1];
    unsigned m_dwAdapter = 0; Parameters m_d3dpp;
    BOOL HandlingSystemStuff(bool first); void TriggerQuit(); @WANTS_TO_QUIT@
    HWND GetHWnd() { return m_hWnd; }
    HRESULT Reset3DEnvironment(BOOL, BOOL) { throw Unexpected{}; }
    void UpdateJoystick(unsigned, bool) {}
    void SRS(unsigned, BOOL) {} void SetModulate(unsigned) {}
    unsigned GetWindowWidth() { return 640; } unsigned GetWindowHeight() { return 480; }
} LT;
struct CPCPlatform {
    EQuitType Process();
    BOOL BeginScene() { Budget(++scenes); return !lost_device; }
    void EndScene() {} void ClearScreen(unsigned) {} void Flip(BOOL = TRUE) {}
    unsigned GetWindowWidth() { return 640; } unsigned GetWindowHeight() { return 480; }
    float GetSysTimeFloat() { return 1.0f; }
} PLATFORM;
@SHELL_STATICS@
#ifdef CE_VERSION
@QUIT_QUERY@
#endif
@TRIGGER_QUIT@
@HANDLING@
@PROCESS@
BOOL PeekMessage(MSG *msg, HWND, unsigned, unsigned, unsigned) {
    Budget(++polls); if (queue.empty()) return FALSE;
    msg->message = queue.front(); queue.pop_front(); return TRUE;
}
BOOL GetMessage(MSG *msg, HWND h, unsigned low, unsigned high) {
    if (queue.empty()) throw WouldBlock{};
    return PeekMessage(msg, h, low, high, PM_REMOVE);
}
void DispatchMessage(MSG *msg) {
    ++dispatches;
    if (msg->message == WM_CLOSE) { LT.TriggerQuit(); close_delivered = true; }
    else if (msg->message == RESTORE) { LT.m_bActive = TRUE; lost_device = false; }
    else if (msg->message == SKIP) skip_pending = true;
    else throw Unexpected{};
}
const int BUTTON_SKIP_CUTSCENE = 7, CONTROL_FRONTEND = 0, MEMTYPE_CONTROLLER = 1;
const unsigned D3DRS_ZENABLE = 1, D3DRS_ZWRITEENABLE = 2, MUS_CREDITS = 3, SA_TOP_LEFT = 0, SPT_NONE = 0;
using EControlType = int;
void *operator new(std::size_t n, int) { return ::operator new(n); }
class CController;
struct IController {
    virtual ~IController() {}
    virtual void ReceiveButtonAction(CController *, int, float) = 0;
    void Shutdown() { ++handler_shutdowns; }
};
class CController {
    IController *owner;
public:
    CController(IController *value, int, int = 0) : owner(value) { ++controller_news; }
    virtual ~CController() { ++controller_deletes; }
    void Flush() { if (skip_pending) { skip_pending = false; owner->ReceiveButtonAction(this, BUTTON_SKIP_CUTSCENE, 1); } }
};
using CPCController = CController;
using CCONTROLLER = CController;
struct CFMV : IController {
    BOOL mQuit = FALSE, mSkippable = TRUE;
    void ReceiveButtonAction(CController *, int, float);
};
struct Options { BOOL mAttractMode = FALSE, mSkipFMV = FALSE, mAutoConfigTest = FALSE, mMusic = TRUE; } CLIPARAMS;
@FILM_SKIP@
struct Bink { unsigned FrameNum = 0, Frames = 3; };
struct BinkThread { void Lock() {} void Unlock() {} } sBinkOpenThread;
bool BinkWait(Bink *) { return false; }
bool PortClockFilmFrameWaits() { return false; }
void BinkDoFrame(Bink *) {}
void BinkNextFrame(Bink *b) { ++b->FrameNum; }
struct CDXFrontEndVideo {
    Bink backing; Bink *mBink = nullptr;
    BOOL mFrameCopied = FALSE, mFrameDecoded = TRUE;
    void Init() {}
    void Open(char *, int, int, BOOL, BOOL, int) { ++opens; backing.FrameNum = 0; mBink = &backing; }
    int NextFrame(bool);
    int GetWidth() { return 640; } int GetHeight() { return 480; }
    // Fake render boundary: like production Render, only a successful render advances/copies the decoded frame.
    bool Render(float, float, float, float, float, unsigned, BOOL) {
        ++render_calls; BinkNextFrame(mBink); mFrameCopied = TRUE; return true;
    }
    void CloseVideo() { ++closes; mBink = nullptr; }
    void Shutdown() { ++video_shutdowns; }
};
@NEXT_FRAME@
struct CDXFMV : CFMV { CDXFrontEndVideo mVideo; BOOL Play(char *, int, int, int, int, int); };
struct State { void UseDefault() {} } STATE;
struct RenderInfo { void Reset() {} void Apply() {} void SetFogEnabled(bool) {} } RENDERINFO;
struct MouseButton { bool mDown = false; } MOUSE_BUTTONS[3];
void ResetMouseTransientState() { for (auto &button : MOUSE_BUTTONS) button.mDown = false; }
struct CDXTexture { int mHeight = 480, mWidth = 640; } *gFMVSplash;
int FMV_SKIP_FRAMES = 0x7fffffff;
struct CDXSpriteRenderer { template<class... Args> static void DrawColouredSprite(Args...) {} };
bool PortWideActive() { return false; } void PortWideViewport(int) {}
void PortCeGamepadBeginCycle() {}
float PortUiLeft() { return 0; } float PortUiTop() { return 0; } float PortUiScale() { return 1; }
struct CMonitor { static void Shutdown() {} };
// A bounded synthetic successful file-open boundary, never a game path or retail input.
FILE *test_fopen(const char *, const char *) { return reinterpret_cast<FILE *>(1); }
int test_fclose(FILE *) { return 0; }
#define fopen test_fopen
#define fclose test_fclose
@FILM@
#undef fopen
#undef fclose
struct Music { void PlaySelection(unsigned) { ++music_plays; } void UpdateStatus() {} void Stop() { ++music_stops; } } MUSIC;
struct Credits { bool RenderCredits(float, unsigned) { return ++render_calls < 3; } } CREDITS;
@CREDITS_HANDLER@
@CREDITS@
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    setenv("BEA_PORT_CE_LAYER", argv[3], 1);
    bool film = !strcmp(argv[1], "film");
    const char *scenario = argv[2];
    bool closed = strstr(scenario, "close") != nullptr;
    bool entry = !strcmp(scenario, "already-close");
    bool following = !strcmp(scenario, "close-next-call");
    if (closed && !entry) queue = {WM_CLOSE, SKIP};
    if (!strcmp(scenario, "skip")) queue = {SKIP};
    if (!strcmp(scenario, "minimized-close") || !strcmp(scenario, "lost-close")) {
        lost_device = true; LT.m_bActive = strcmp(scenario, "minimized-close") != 0;
        queue = {WM_CLOSE, RESTORE, SKIP};
    }
    if (entry) { LT.TriggerQuit(); close_delivered = true; }
    Device device; D3D d3d; LT.m_pd3dDevice = &device; LT.m_pD3D = &d3d;
    CDXFMV player; char name[] = "synthetic";
    bool returned = false, film_result = false; const char *stop = "returned";
    try {
        if (film) film_result = player.Play(name, 0, 0, 0, 0, 0); else GAME.RollCredits();
        returned = true;
        if (following) {
            unsigned prior = controller_news;
            if (film) player.Play(name, 0, 0, 0, 0, 0); else GAME.RollCredits();
#ifdef CE_VERSION
            if (PortCeLayer() && !strcmp("@CORRECTED@", "yes")) Check(controller_news == prior);
#endif
        }
    } catch (const Limit &) { stop = "budget"; }
    catch (const WouldBlock &) { stop = "would-block"; }
    catch (const Unexpected &) { stop = "unexpected"; }
    bool cleanup = controller_news == controller_deletes && (film ? opens == closes && closes == video_shutdowns :
        music_plays == music_stops && handler_shutdowns == controller_news / 2);
    bool entry_skipped = entry && !controller_news && (film ? !opens : !music_plays);
    bool prompt = closed && returned && (entry_skipped || (!render_calls && scenes == 0));
    // NextFrame executes as production; scene/decoder/device counters independently distinguish prompt close from end-of-film.
    if (returned) {
        Check(cleanup);
        if (!closed) Check(render_calls > 0 && (film ? film_result == !strcmp(scenario, "skip") : true));
    }
    else Check(closed && close_delivered && !cleanup && controller_news > 0);
    printf("{\"returned\":%s,\"stop\":\"%s\",\"prompt_close\":%s,\"closed\":%s,\"cleanup\":%s,\"entry_skipped\":%s,"
           "\"checks\":%u,\"renders\":%u,\"scenes\":%u,\"opens\":%u,\"closes\":%u,\"controller_news\":%u,"
           "\"controller_deletes\":%u,\"handler_shutdowns\":%u,\"dispatches\":%u,\"queued\":%zu,\"film_result\":%s}\n",
           returned ? "true" : "false", stop, prompt ? "true" : "false", close_delivered ? "true" : "false",
           cleanup ? "true" : "false", entry_skipped ? "true" : "false", checks, render_calls, scenes, opens, closes,
           controller_news, controller_deletes, handler_shutdowns, dispatches, queue.size(), film_result ? "true" : "false");
    return !strcmp(stop, "unexpected") ? 1 : 0;
}
