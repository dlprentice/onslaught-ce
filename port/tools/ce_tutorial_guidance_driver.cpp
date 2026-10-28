// SPDX-License-Identifier: GPL-3.0-only
// Runs the exact patched bodies of IScript::AddHelpMessage/PlayCharMessageWait, the radio queue (CMessageBox::AddMessage,
// TryAdvance, StartMessage, SwitchMessagesOn), the help renderer, the binding table and lookup, the dash code, the native
// word wrapper, the port's key mapping and the CE guidance, against synthetic game, font, clock and SDL layout boundaries.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <new>
#include <algorithm>
#include <limits>
#include <strings.h>
#include <SDL3/SDL.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
using WCHAR = wchar_t;
using SINT = int;
using BOOL = int;
using LONG = long;
using LPSTR = char *;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define FONT_NORMAL 0
#define D3DSAMP_MINFILTER 1
#define D3DSAMP_MAGFILTER 2
#define D3DTEXF_LINEAR 3
#define D3DTSS_COLOROP 4
#define D3DTOP_MODULATE 5
#define THING_TYPE_UNIT 1
#define MT_SCRIPT 3
#define SCRIPT_MESSAGE_RESUME 7
#define GAME_STATE_PLAYING 3
#define MESSAGE_BOX_START 3004
#define MESSAGE_BOX_FINISH 3002
#define TRACE(...) ((void)0)

static int checks;
static void Check(bool ok, const char *why) { ++checks; if (!ok) { fprintf(stderr, "FAIL: %s\n", why); exit(2); } }

static const int kDodge = 667656903, kZoomIn = 8268984, kZoomOut = 17186000, kRetro = 2302408, kOtherRadio = 999;

#include "control.inc"
#include "bindings.inc"
#include "lookup.inc"
#include "key_name.inc"

// SDL's layout translation is the boundary: 0 US, 1 AZERTY letters (A and Q swapped, digits are symbols),
// 2 a non-Latin layout, 3 no translation.
static int fakeLayout;
static int layoutQueries;
extern "C" SDL_Keycode SDLCALL SDL_GetKeyFromScancode(SDL_Scancode scan, SDL_Keymod mod, bool keyEvent)
{
	Check(mod == SDL_KMOD_NONE && !keyEvent, "keyboard layout queried without modifiers");
	++layoutQueries;
	if (fakeLayout == 3)
		return SDLK_UNKNOWN;
	if (fakeLayout == 2 && scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z)
		return 0x3b1 + (scan - SDL_SCANCODE_A);	// Greek letters
	if (fakeLayout == 1 && scan == SDL_SCANCODE_A) return 'q';
	if (fakeLayout == 1 && scan == SDL_SCANCODE_Q) return 'a';
	if (fakeLayout == 1 && scan == SDL_SCANCODE_W) return 'z';
	if (fakeLayout == 1 && scan == SDL_SCANCODE_Z) return 'w';
	if (fakeLayout == 1 && scan == SDL_SCANCODE_1) return '&';
	if (scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z) return 'a' + (scan - SDL_SCANCODE_A);
	if (scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9) return '1' + (scan - SDL_SCANCODE_1);
	if (scan == SDL_SCANCODE_0) return '0';
	if (scan == SDL_SCANCODE_SEMICOLON) return ';';
	return SDLK_UNKNOWN;
}
#include "dik.inc"
#include "key_layout.inc"
#include "ce_layer.inc"
#include "pad_header.inc"

// The actual SDL-facing pad-info routine and actual CE default-binding provider run below against a synthetic device.
// The fake supplies connection state and physical capabilities; it does not manufacture any CE binding.
using DWORD = unsigned long;
using HRESULT = int;
#define SUCCEEDED(x) ((x) >= 0)
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD 0x15
#define MAX_JOYPADS 4
struct DIDEVCAPS { DWORD dwSize, dwFlags, dwDevType; };
struct DIJOYSTATE2 { unsigned char rgbButtons[32] = {}; };
struct FakePadState { bool connected = false; unsigned axes = 0, buttons = 0; int numAxes = 0, numButtons = 0, numHats = 0; } fakePad;
struct IDirectInputDevice8A {};
enum Kind { KEYBOARD, MOUSE, JOYSTICK };
struct Device : IDirectInputDevice8A {
	Kind kind = JOYSTICK; SDL_Gamepad *pad = nullptr; SDL_Joystick *joy = nullptr;
	HRESULT GetCapabilities(DIDEVCAPS *c) { c->dwDevType = joy ? DI8DEVTYPE_JOYSTICK : DI8DEVTYPE_GAMEPAD; return 0; }
} fakeDevice;
extern "C" bool SDLCALL SDL_GamepadConnected(SDL_Gamepad *) { return fakePad.connected; }
extern "C" bool SDLCALL SDL_JoystickConnected(SDL_Joystick *) { return fakePad.connected; }
extern "C" bool SDLCALL SDL_GamepadHasAxis(SDL_Gamepad *, SDL_GamepadAxis a) { return (fakePad.axes & (1u << (int)a)) != 0; }
extern "C" bool SDLCALL SDL_GamepadHasButton(SDL_Gamepad *, SDL_GamepadButton b) { return (fakePad.buttons & (1u << (int)b)) != 0; }
extern "C" int SDLCALL SDL_GetNumJoystickAxes(SDL_Joystick *) { return fakePad.numAxes; }
extern "C" int SDLCALL SDL_GetNumJoystickButtons(SDL_Joystick *) { return fakePad.numButtons; }
extern "C" int SDLCALL SDL_GetNumJoystickHats(SDL_Joystick *) { return fakePad.numHats; }

#ifdef CE_VERSION
#include "pad_info.inc"
#endif

struct SIZE { int cx, cy; };
struct SWordWrapFill { WCHAR c[40][100]; };
WCHAR *ToWCHAR(const char *text) { static WCHAR result[128]; int i = 0; for (; text[i] && i < 127; ++i) result[i] = (unsigned char)text[i]; result[i] = 0; return result; }
static std::vector<std::wstring> drawn;
static int wrapCalls;
struct CDXBitmapFont {
	bool valid = true; int mUnknown16c = 0; int advance = 8; wchar_t reject = 0;
	bool IsValid() { return valid; }
	bool CanDisplay(WCHAR c) { return c >= 32 && c <= 126 && c != L'~' && c != L'[' && c != L']' && c != reject; }
	int WordWrap(SWordWrapFill *fill, WCHAR *text, float width);
	void GetTextExtent(WCHAR *s, SIZE *size) { size->cx = (int)std::wcslen(s) * advance; size->cy = 16; }
	void DrawTextDynamic(float, float, float, float, float, unsigned, WCHAR *s, float, int, int) { drawn.emplace_back(s); }
};
#include "font.inc"
struct Platform {
	CDXBitmapFont font; bool absent = false; CDXBitmapFont *Font(int) { return absent ? nullptr : &font; }
	float GetSysTimeFloat() { return 1.f; } bool KeyOn(int) { return false; }
	int GetWindowWidth() { return 640; } int GetWindowHeight() { return 480; }
} PLATFORM;
struct { int mAutoConfigTest = 0; bool mRecordDemo = false, mPlaybackDemo = false; } CLIPARAMS;
int PLAYABLE_DEMO = 0;
struct CThing { bool unit = false, dying = false; bool IsA(int) { return unit; } bool IsDying() { return dying; } };
using CUnit = CThing;
struct Text {
	int language = 0; WCHAR speaker[8] = L"Speaker", message[15] = L"Authored radio";
	int GetLanguage() { return language; } WCHAR *GetString(int id) { return id == 42 ? speaker : message; }
	char *GetSoundName(int) { return nullptr; } const char *GetLanguageName() { return "english"; }
} TEXT;
struct Event { int id = 0; float delay = 0; void *owner = nullptr, *vm = nullptr; void Set(int i, float d, void *o, void *v) { id = i; delay = d; owner = o; vm = v; } };
using CScheduledEvent = Event;
struct EventManager {
	float time = 1; Event events[8]; int used = 0; bool available = true; int lastId = 0; float lastDelay = 0;
	float GetTime() { return time; }
	Event *GetNextFreeEvent() { return available && used < 8 ? &events[used++] : nullptr; }
	void AddEvent(float delay, int id, void *) { lastId = id; lastDelay = delay; }
} EVENT_MANAGER;
struct { void AddMessage(const char *, ...) {} } LOG;
struct {
	int mJoypads = 1; bool mUnknown33474[MAX_JOYPADS] = {true, true, true, true};
	bool mJoypadIsNewtype[MAX_JOYPADS] = {true, true, true, true};
	Device *pJoystick[MAX_JOYPADS] = {}; DIJOYSTATE2 state[MAX_JOYPADS];
	DIJOYSTATE2 *JoyState(int pad) { return &state[pad]; }
	void SSS_NoCache(int, int, int) {} void STS(int, int, int) {}
} LT;
struct { void Apply() {} } RENDERINFO;
struct { int GetControllerConfigurationNum(int) { return 0; } } CAREER;
struct { float mViewportWidth = 640, mViewportX = 0; template <class T> void SetMessageSpeaker(T &) {} } HUD;
#define MAX_HELP_TEXT_MESSAGES 2
struct SHelpTextMessage { WCHAR *mText = nullptr; float mTime = 0; int mActive = FALSE; };
struct CHelpTextDisplay { SHelpTextMessage mMessages[2]; void AddMessage(WCHAR *); void Render(); };
template <class T> struct ActiveReader { T *ptr = nullptr; T *ToRead() { return ptr; } T *operator->() { return ptr; } void SetReader(T *v) { ptr = v; } };
struct CMessage {
	WCHAR *mName, *mText; int mStringId; float mDelay, mEndTime = 0; Event *mEvent; int mPriority;
	bool mHasSpeaker = false, mFinishedPlaying = false; ActiveReader<CThing> mSpeaker;
	static int alive;
	CMessage(WCHAR *s, WCHAR *t, int i, float v, CUnit *u, Event *e, int p)
		: mName(s), mText(t), mStringId(i), mDelay(v), mEvent(e), mPriority(p), mHasSpeaker(u != nullptr) { mSpeaker.SetReader(u); ++alive; }
	~CMessage() { --alive; }
};
int CMessage::alive;
template <class T> struct SPtrSet {
	std::vector<T *> items; int Size() const { return (int)items.size(); }
	T *First() { return items.front(); } void Append(T *p) { items.push_back(p); }
	void Remove(T *p) { items.erase(std::remove(items.begin(), items.end(), p), items.end()); }
};
#define FOR_ALL_ITEMS_IN(set, item) for (size_t idx = 0; idx < (set).items.size() && (((item) = (set).items[idx]), true); ++idx)
struct CMessageBox {
	CMessage *mCurrent = nullptr; SPtrSet<CMessage> mQueue; float mStartTime = 0, mUnknown020 = 0, mUnknown1b4 = 0;
	int mAllowedToPlay = TRUE, mCanAdvance = TRUE, mUnknown024 = 0, mPortrait = 0, mUnknown1ac = 0;
	void *mPortraits[1][1] = {{nullptr}}, *mPortraitTexture = nullptr;
	char mVoiceName[256] = {}; int reveals = 0;
	void AddMessage(CMessage *); void TryAdvance(); void StartMessage(); void SwitchMessagesOn();
	int GetPortrait(WCHAR *) { return 0; } void AdvanceReveal() { reveals++; }
	// The finish event's own work (HandleEvent MESSAGE_BOX_FINISH then MESSAGE_BOX_NEXT), for the queue tests.
	void Finish() { mQueue.Remove(mCurrent); delete mCurrent; mCurrent = nullptr; mAllowedToPlay = TRUE; TryAdvance(); }
	~CMessageBox() { for (auto *p : mQueue.items) delete p; }
};
struct CPCController {
	int mPadNumber = 0;
	#include "player_pad.inc"
} fakePlayerController;
struct CGame {
	CHelpTextDisplay *help = nullptr; CMessageBox *box = nullptr; int level = 100, state = GAME_STATE_PLAYING; bool multi = false;
	CPCController *mController[2] = {&fakePlayerController, nullptr};
	CHelpTextDisplay *GetHelpTextDisplay() { return help; } CMessageBox *GetMessageBox() { return box; }
	CPCController *GetController(int index);
	int GetCurrentlyRunningLevelNum() { return level; } bool IsMultiplayer() { return multi; } bool IsPaused() { return false; } int GetGameState() { return state; }
	float GetLevelStartTime() { return 0; }
} GAME;
#define CCONTROLLER CPCController
#include "game_controller.inc"
#undef CCONTROLLER
bool PortCeBonusCoopRunning() { return false; }
static char gVoiceName[260]; static ActiveReader<CMessage> gVoiceMessage;
struct { char mFileName[260]; int mSize; void Acquire() {} void Unlock() {} void StartAsync() {} } gVoiceLoader;
static bool PortClockVirtual() { return false; }
static char *FromWCHAR(WCHAR *) { static char s[] = "fixture"; return s; }
struct CVM { bool mStop = false; CVM() {} CVM(const CVM &v) : mStop(v.mStop) {} } MISSION_SCRIPT_VM;
void *operator new(size_t size, int) { return ::operator new(size); }
void operator delete(void *p, int) noexcept { ::operator delete(p); }
struct Saved { std::vector<CVM *> v; void Append(CVM *p) { v.push_back(p); } ~Saved() { for (auto *p : v) delete p; } };
struct CDataType { int value; float f; int GetInt() { return value; } float GetFloat() { return f; } };
struct IScript { CThing *mThing; Saved mSavedVMs; void AddHelpMessage(CDataType **, int, CDataType **); void PlayCharMessageWait(CDataType **, int, CDataType **); };
WCHAR retail_help[] = L"Original help";
WCHAR *Localization_GetStringById(int id) { Check(id == 0xdd, "original localization ID"); return retail_help; }

// The dash code runs the source's actual StrafeLeft/StrafeRight.
#define DASH_BOOST_ROLL 0.08f
#define ENGINE_VOLUME 1.f
#define ST_FOLLOWANDDIE 0
#define BATTLE_ENGINE_SLOW_MOVEMENT_FACTOR 4.f
struct FVector { float x, y, z; FVector(float a, float b, float c) : x(a), y(b), z(c) {} void operator/=(float f) { x /= f; y /= f; z /= f; } };
struct FMatrix { FMatrix(float, float, float) {} FVector operator*(const FVector &v) { return v; } };
struct WalkerMain {
	bool ground = true, mSlowMovement = false; int mStrafeSound = 0; float mZoomOutTime = 1, mRollvel = 0; struct { float mYaw = 0; } mCurrentOrientation;
	bool IsOnGround() { return ground; } void AddVelocity(FVector) {}
};
struct Sound { void PlayEffect(int, WalkerMain *, float, int) {} bool IsInitialised() { return false; } } SOUND;
struct CBattleEngineWalkerPart {
	WalkerMain *mMainPart; float mLastStartHardRightTime = -10, mLastStartHardLeftTime = -10, mLastMoveXVal = 0; int mDoingDashCount = 0;
	static float mDashTime, mDashStart, mDashEnd, mDashVelocity; static int mDashLength;
	explicit CBattleEngineWalkerPart(WalkerMain *p) : mMainPart(p) {} float GetCurrentAccleration() { return 1; } void LoseWeaponCharge() {}
	void StrafeLeft(float); void StrafeRight(float);
};
#include "walker.inc"

// The actual console variable layout and lookup locate the actual walker's registered timing value.
#include "console_types.inc"
#define stricmp strcasecmp
struct CConsole {
	CConsoleVar *mVariables = nullptr;
	#include "console_find.inc"
} CONSOLE;
#undef stricmp
static CConsoleVar dashVariable = {};

// Actual game dead-zone processing: the fake supplies an unscaled physical-axis value.
#include "analog_dead_zone.inc"
struct CController {
	float raw = 0.f;
	float GetJoyAnalogueLeftX(int) { return raw; } float GetJoyAnalogueLeftY(int) { return raw; }
	float GetJoyAnalogueRightX(int) { return raw; } float GetJoyAnalogueRightY(int) { return raw; }
	float GetJoyPovX(int) { return raw; } float GetJoyPovY(int) { return raw; }
	BOOL GetJoyButtonOn(int, int) { return FALSE; }
	float GetMappedInputValue(int, int);
};
#include "mapped_input.inc"

// This fixture checks cue selection and mapping precedence with a neutral pad state. Ordered SDL button delivery
// is exercised separately by input_lifetime_tests.py and ce_gamepad_edge_tests.py.
int PortCePadNext(void *, bool, unsigned *out) { *out = 0; return 0; }
void PortCePadForget(void *) {}
#include "gamepad.inc"
#include "guidance.inc"

// The exact port-patched mapping loop, with synthetic input reads and an observer on sent actions. This tests whether
// a named saved input actually gets reached after a centered mouse axis, and the CE default's mouse-rest exception.
static bool gMouseAxisLocked[2] = {}, sSendingMouseRest = false, sMouseAxisRead = false;
static int sCeMouseRestId = -1, MOUSE_X = 320, MOUSE_Y = 240, MOUSE_WHEEL = 0;
static float gMouseSensitivity = 1.f, probeAxis = -1.f;
static bool probeKey = false;
static void *sMouse = nullptr;
struct { bool mReleased; } MOUSE_BUTTONS[3] = {};
static bool MOUSE_HELD[3] = {};
struct MappingProbe {
	int mPadNumber = 0, mNumMappings = 4; bool mPlaying = false, mRecording = false, mPausedBeforeMap = false;
	float mLastRepeatTime = 0, mNextDelayTime = 0;
	bool set[128] = {}; float sent[128] = {}; int count[128] = {};
	void CalcNumMappings() {} void ReadControllerState() {} void RecordControllerState() {}
	bool IsButtonSet(int id) { return set[id]; } void UnsetButton(int id) { set[id] = false; }
	void SendButtonAction(int id, float value) { set[id] = true; sent[id] = value; ++count[id]; }
	bool GetJoyButtonOnce(int, int) { return false; } bool GetJoyButtonOn(int, int) { return false; }
	bool GetJoyButtonRelease(int, int) { return false; } bool GetKeyOnce(int) { return false; }
	bool GetKeyOn(int key) { return probeKey && key == 0x10; } bool GetKeyRelease(int) { return false; }
	bool RepeatOccured(bool, SControlBinding *) { return false; }
	float GetMappedInputValue(int, int) { return probeAxis; }
	void DoMappings();
};
#include "mapping_probe.inc"

#include "help.inc"
#include "script.inc"
#include "message.inc"

static void CheckDash(float dt, bool leftFirst, bool shouldDash, bool ground = true, bool busy = false,
	float first = 1.f, float second = 1.f, const char *why = nullptr)
{
	WalkerMain engine; engine.ground = ground; CBattleEngineWalkerPart walker(&engine); if (busy) walker.mDoingDashCount = 1;
	EVENT_MANAGER.time = 1.f; if (leftFirst) walker.StrafeLeft(-first); else walker.StrafeRight(first);
	EVENT_MANAGER.time = 1.f + dt; if (leftFirst) walker.StrafeRight(second); else walker.StrafeLeft(-second);
	Check((walker.mDoingDashCount > 1) == shouldDash, why ? why : leftFirst ? "actual left to right dash timing" : "actual right to left dash timing");
}
static int Active(CHelpTextDisplay &h) { int n = 0; for (auto &m : h.mMessages) n += !!m.mActive; return n; }
static std::wstring TextAt(CHelpTextDisplay &h, int i) { return h.mMessages[i].mActive ? std::wstring(h.mMessages[i].mText) : L""; }
static int Find(CHelpTextDisplay &h, const wchar_t *part) { for (int i = 0; i < 2; ++i) if (TextAt(h, i).find(part) != std::wstring::npos) return i; return -1; }
static void CallHelp(IScript &s, int id) { CDataType d{id, 0}; CDataType *a[] = {&d}; s.AddHelpMessage(a, 1, nullptr); }
static void CallRadio(IScript &s, int id) { CDataType d0{42, 0}, d1{id, 0}, d2{0, 2.5f}; CDataType *a[] = {&d0, &d1, &d2}; s.PlayCharMessageWait(a, 3, nullptr); }
static void SetInput(int id, int slot, int pad, int type, int key) { SControlInput &i = FindControlBinding(id)->mInput[slot]; i.mPad = pad; i.mType = type; i.mKey = key; }
static void Env(const char *key, const char *value) { setenv(key, value, 1); }
static bool Is(const char *mode, const char *name) { return !strcmp(mode, name); }
// The MESSAGE_BOX_START event the queue scheduled, 0.2 game seconds later.
static void StartScheduled(CMessageBox &box) { Check(EVENT_MANAGER.lastId == MESSAGE_BOX_START && EVENT_MANAGER.lastDelay == .2f, "actual queue schedules radio start"); EVENT_MANAGER.time += .2f; box.StartMessage(); }
static std::wstring Dodge(CHelpTextDisplay &h) { int i = Find(h, L"odge"); return i < 0 ? L"" : TextAt(h, i); }

// The native wrapper over every showing cue, at the font's real 550-pixel width and unbroken.
static void WrapChecks(CHelpTextDisplay &h)
{
	for (int i = 0; i < 2; ++i)
	{
		if (!h.mMessages[i].mActive)
			continue;
		SWordWrapFill fill; ++wrapCalls;
		int lines = PLATFORM.font.WordWrap(&fill, h.mMessages[i].mText, 550.f);
		Check(lines >= 1 && lines < 40, "native wrapper line count bounded");
		for (int l = 0; l < lines; ++l) Check(std::wcslen(fill.c[l]) < 100, "native wrapper line within its buffer");
		int unbroken = PLATFORM.font.WordWrap(&fill, h.mMessages[i].mText, 1e9f);
		Check(unbroken == 1 && std::wcslen(fill.c[0]) == std::wcslen(h.mMessages[i].mText), "unwrapped cue fits one native line");
	}
}

static const char *const kFenceModes[] = {"layer-off", "demo", "autoconfig", "record-demo", "play-demo", "multiplayer", "other-level",
	"language", "font-missing", "font-invalid", "forced-BEA_PORT_CLOCK", "forced-BEA_PORT_TAPE", "forced-BEA_PORT_PROBES",
	"forced-BEA_PORT_RECORD", "forced-BEA_NET_HOST", "forced-BEA_NET_JOIN", "pad-fence"};

int main(int argc, char **argv)
{
#ifdef __linux__
	prctl(PR_SET_DUMPABLE, 0);
#endif
	if (argc != 2)
		return 1;
	const char *mode = argv[1];
	const char *envs[] = {"BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_NET_HOST", "BEA_NET_JOIN"};
	for (const char *e : envs)
		unsetenv(e);
	bool fence = false;
	for (const char *f : kFenceModes)
		fence = fence || Is(mode, f);
	if (Is(mode, "layer-off")) Env("BEA_PORT_CE_LAYER", "0");
	if (Is(mode, "layer-force")) Env("BEA_PORT_CE_LAYER", "1");
	if (Is(mode, "empty-tape")) Env("BEA_PORT_TAPE", "");
	if (Is(mode, "pad-fence")) { Env("BEA_PORT_CE_LAYER", "1"); Env("BEA_PORT_TAPE", "1"); }
	if (!strncmp(mode, "forced-", 7)) { Env("BEA_PORT_CE_LAYER", "1"); Env(mode + 7, "1"); }
	if (Is(mode, "demo")) PLAYABLE_DEMO = 1;
	if (Is(mode, "autoconfig")) CLIPARAMS.mAutoConfigTest = 1;
	if (Is(mode, "record-demo")) CLIPARAMS.mRecordDemo = true;
	if (Is(mode, "play-demo")) CLIPARAMS.mPlaybackDemo = true;
	if (Is(mode, "multiplayer")) GAME.multi = true;
	if (Is(mode, "other-level")) GAME.level = 101;
	if (Is(mode, "language")) TEXT.language = 1;
	if (Is(mode, "font-missing")) PLATFORM.absent = true;
	if (Is(mode, "font-invalid")) PLATFORM.font.valid = false;
	bool padCase = !strncmp(mode, "pad-", 4) || !strncmp(mode, "stick-", 6);
	if (padCase)
	{
		LT.pJoystick[0] = &fakeDevice;
		fakePad.connected = !Is(mode, "pad-disconnected") && !Is(mode, "pad-hotplug");
		fakePad.axes = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
		fakePad.buttons = (1u << SDL_GAMEPAD_BUTTON_MISC1) - 1;
		fakePad.numAxes = Is(mode, "stick-no-twist") ? 3 : 4; fakePad.numButtons = 8; fakePad.numHats = 1;
		if (!strncmp(mode, "stick-", 6)) fakeDevice.joy = reinterpret_cast<SDL_Joystick *>(&fakePad);
		else fakeDevice.pad = reinterpret_cast<SDL_Gamepad *>(&fakePad);
		if (Is(mode, "pad-missing-axis")) fakePad.axes &= ~(1u << SDL_GAMEPAD_AXIS_LEFTX);
		if (Is(mode, "pad-missing-button")) fakePad.buttons &= ~(1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		if (Is(mode, "pad-disabled")) LT.mUnknown33474[0] = false;
		if (Is(mode, "pad-second") || Is(mode, "pad-player-two") || Is(mode, "pad-mouse-player-two"))
			{ LT.pJoystick[1] = LT.pJoystick[0]; LT.pJoystick[0] = nullptr; LT.mJoypads = 2; }
	}
	if (Is(mode, "pad-player-two") || Is(mode, "keyboard-player-two") || Is(mode, "pad-mouse-player-two")) fakePlayerController.mPadNumber = 1;
	if (Is(mode, "no-controller")) GAME.mController[0] = nullptr;
	bool ce = true;
#ifndef CE_VERSION
	ce = false;
#endif

	CHelpTextDisplay h; CMessageBox box; CThing thing; IScript script{&thing, {}}; GAME.help = &h; GAME.box = &box;
	std::strcpy(dashVariable.mName, "g_dash_time"); dashVariable.mType = CVar_float;
	dashVariable.mData = &CBattleEngineWalkerPart::mDashTime; CONSOLE.mVariables = &dashVariable;

	// --- the dash facts the cue states (always: they are the game's code, not the cue's) ---
	if (Is(mode, "normal"))
	{
		CheckDash(.15f, true, true); CheckDash(.15f, false, true);
		CheckDash(.11f, true, true); CheckDash(.19f, false, true);
		CheckDash(.05f, true, false); CheckDash(.09f, false, false); CheckDash(.25f, true, false); CheckDash(.21f, false, false);
		CheckDash(.15f, true, false, false); CheckDash(.15f, true, false, true, true);
		CheckDash(.15f, true, false, true, false, .89f, 1.f, "partial first strafe cannot start a dash");
		CheckDash(.15f, false, false, true, false, .89f, 1.f, "partial first strafe cannot start a dash");
		CheckDash(.15f, true, false, true, false, 1.f, .79f, "partial opposite strafe cannot finish a dash");
		CheckDash(.15f, false, false, true, false, 1.f, .79f, "partial opposite strafe cannot finish a dash");
		CheckDash(.15f, true, false, true, false, .9f, 1.f, "threshold equality is not a hard start");
		CheckDash(.15f, true, false, true, false, 1.f, .8f, "threshold equality is not a hard finish");
		CheckDash(.15f, true, true, true, false, .91f, .81f); CheckDash(.15f, false, true, true, false, .91f, .81f);
		CController input;
		input.raw = .91f; float belowStart = input.GetMappedInputValue(0, ANALOGUE_X1);
		input.raw = .82f; float belowEnd = input.GetMappedInputValue(0, ANALOGUE_X1);
		input.raw = .92f; float aboveStart = input.GetMappedInputValue(0, ANALOGUE_X1);
		input.raw = .84f; float aboveEnd = input.GetMappedInputValue(0, ANALOGUE_X1);
		CheckDash(.15f, true, false, true, false, belowStart, 1.f, "partial first deflection fails after deadzone");
		CheckDash(.15f, true, false, true, false, 1.f, belowEnd, "partial second deflection fails after deadzone");
		CheckDash(.15f, true, true, true, false, aboveStart, aboveEnd, "near-full deflection succeeds after deadzone");
		CheckDash(.101f, true, true); CheckDash(.199f, false, true);
		WalkerMain engine; CBattleEngineWalkerPart walker(&engine);
		EVENT_MANAGER.time = 1.f; walker.StrafeLeft(-1.f); EVENT_MANAGER.time = 1.15f; walker.StrafeLeft(-1.f);
		Check(walker.mDoingDashCount == 0, "same direction twice is not a dash");
		CBattleEngineWalkerPart::mDashTime = .4f; CheckDash(.3f, true, true); CheckDash(.15f, true, false);
		CBattleEngineWalkerPart::mDashTime = .2f; EVENT_MANAGER.time = 1.f;
	}
	if (Is(mode, "timing-change") || Is(mode, "pad-timing-change")) CBattleEngineWalkerPart::mDashTime = .4f;
	if (Is(mode, "timing-missing") || Is(mode, "timing-missing-generic")) CONSOLE.mVariables = nullptr;
	if (Is(mode, "timing-type")) dashVariable.mType = CVar_int;
	if (Is(mode, "timing-null")) dashVariable.mData = nullptr;
	if (Is(mode, "timing-nan")) CBattleEngineWalkerPart::mDashTime = std::numeric_limits<float>::quiet_NaN();
	if (Is(mode, "timing-small")) CBattleEngineWalkerPart::mDashTime = .001f;
	if (Is(mode, "timing-large")) CBattleEngineWalkerPart::mDashTime = 11.f;

	// --- the key-name boundary: every physical letter and digit the input mapping knows round-trips ---
#ifdef CE_VERSION
	for (int i = 0; i < 26; ++i)
	{
		auto sc = (SDL_Scancode)(SDL_SCANCODE_A + i); char name[2] = {};
		Check(PortScancodeForDik(DikFor(sc)) == sc, "letter scancode inverts DikFor");
		Check(PortCeTutorialPrintableKey(DikFor(sc), name, sizeof name) && name[0] == 'A' + i && !name[1], "letter named in US layout");
	}
	for (int i = 0; i < 10; ++i)
	{
		auto sc = i == 9 ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + i); char name[2] = {};
		Check(PortCeTutorialPrintableKey(DikFor(sc), name, sizeof name) && name[0] == (i == 9 ? '0' : '1' + i), "digit named in US layout");
	}
	{
		char name[8] = "x";
		Check(!PortCeTutorialPrintableKey(0, name, sizeof name) && !PortCeTutorialPrintableKey(0x100, name, sizeof name) &&
			!PortCeTutorialPrintableKey(-1, name, sizeof name) && !PortCeTutorialPrintableKey(0x1e, name, 1) &&
			!PortCeTutorialPrintableKey(0x1e, nullptr, 8), "out of range keys and small buffers are refused");
		Check(!PortCeTutorialPrintableKey(0x27, name, sizeof name), "symbol keys use the generic text");
		Check(!PortCeTutorialPrintableKey(0x4f, name, sizeof name), "keypad keys use the generic text");
	}
#endif

	if (Is(mode, "event-exhausted")) EVENT_MANAGER.available = false;
	if (Is(mode, "box-missing")) GAME.box = nullptr;
	if (Is(mode, "help-missing")) GAME.help = nullptr;

	// --- bindings and layouts the player may have ---
	if (Is(mode, "layout-azerty")) fakeLayout = 1;
	if (Is(mode, "layout-nonlatin")) fakeLayout = 2;
	if (Is(mode, "pad-nonlatin")) fakeLayout = 2;
	if (Is(mode, "layout-unknown")) fakeLayout = 3;
	if (Is(mode, "remap-keys")) { SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0x10); SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0x12); }
	if (Is(mode, "keyboard-player-two")) { SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, KEY_ON, 0x10); SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, KEY_ON, 0x12); }
	if (Is(mode, "remap-arrows")) { SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0xcb); SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0xcd); }
	if (Is(mode, "remap-layout"))
	{
		// Bound in the menu to the key at US Q's position; the player's layout types A there.
		fakeLayout = 1; SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0x10);
		FindControlBinding(BUTTON_MECH_STRAFE_LEFT)->mInput[0].mVKey = 'Z';
		char legacy[32] = {};
		Check(GetKeyNameTextA(0x10L << 16, legacy, sizeof legacy) > 0 && !strcmp(legacy, "Q"), "the Linux legacy key-name table stays US");
	}
	if (Is(mode, "secondary")) { SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, BUTTON_ON, 0); SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, KEY_ON, 0x10); }
	if (Is(mode, "joystick")) SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, BUTTON_ON, 0);
	if (Is(mode, "unbound")) { SetInput(BUTTON_MECH_STRAFE_LEFT, 0, -1, KEY_ON, 0); SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 0, -1, KEY_ON, 0); }
	if (Is(mode, "timing-missing-generic")) SetInput(BUTTON_MECH_STRAFE_LEFT, 0, -1, KEY_ON, 0);
	if (Is(mode, "pad-custom"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, ANALOGUE_MINUS, ANALOGUE_X2);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, ANALOGUE_PLUS, ANALOGUE_X2);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 1, 0, BUTTON_ONCE, 8);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 1, 0, BUTTON_ONCE, 7);
	}
	if (Is(mode, "pad-strafe-buttons"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, BUTTON_ON, 0); // rotated physical right face button
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, BUTTON_ON, 1); // rotated physical left face button
	}
	if (Is(mode, "pad-mouse-blocked") || Is(mode, "pad-mouse-normal") || Is(mode, "pad-mouse-player-two") ||
		Is(mode, "pad-mouse-default") || Is(mode, "mouse-blocked-keys") || Is(mode, "mouse-unbound-keys"))
	{
		bool locked = Is(mode, "pad-mouse-normal"), unbound = Is(mode, "mouse-unbound-keys");
		SetInput(BUTTON_MECH_STRAFE_LEFT, 0, unbound ? -1 : 0, locked ? MOUSE_AXIS_MINUS : MOUSE_AXIS_MINUS_ANY, 0);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, unbound ? -1 : 0, locked ? MOUSE_AXIS_PLUS : MOUSE_AXIS_PLUS_ANY, 0);
		if (Is(mode, "mouse-blocked-keys") || unbound)
		{
			SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, KEY_ON, 0x10);
			SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, KEY_ON, 0x12);
		}
		else if (!Is(mode, "pad-mouse-default"))
		{
			int pad = Is(mode, "pad-mouse-player-two") ? 1 : 0;
			SetInput(BUTTON_MECH_STRAFE_LEFT, 1, pad, ANALOGUE_MINUS, ANALOGUE_X2);
			SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, pad, ANALOGUE_PLUS, ANALOGUE_X2);
		}
	}
	if (Is(mode, "pad-mouse-zoom"))
	{
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 0, 0, MOUSE_AXIS_PLUS_ANY, 0);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 0, 0, MOUSE_AXIS_MINUS_ANY, 0);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 1, 0, BUTTON_ONCE, 10);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 1, 0, BUTTON_ONCE, 9);
	}
	if (Is(mode, "pad-second"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 1, ANALOGUE_MINUS, ANALOGUE_X1);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 1, ANALOGUE_PLUS, ANALOGUE_X1);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 1, 1, BUTTON_ONCE, 10);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 1, 1, BUTTON_ONCE, 9);
	}
	if (Is(mode, "pad-incomplete")) SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 1, 0, BUTTON_ONCE, 8);
	if (Is(mode, "pad-disabled"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1);
	}
	if (Is(mode, "pad-face"))
	{
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 1, 0, BUTTON_ONCE, 3); // rotated raw SDL south button
		SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 1, 0, BUTTON_ONCE, 0); // rotated raw SDL east button
	}
	if (Is(mode, "pad-wrong-sign"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 1, 0, ANALOGUE_PLUS, ANALOGUE_X1);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 1, 0, ANALOGUE_MINUS, ANALOGUE_X1);
	}
	if (Is(mode, "pad-long-keys"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0xcb);
		SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0xcd);
	}
	if (Is(mode, "duplicate")) { SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0x1e); SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 0, 0, MOUSE_BUTTON_ONCE, 4); }
	if (Is(mode, "strafe-once")) SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ONCE, 0x1e);
	if (Is(mode, "wheel-held")) SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 0, 0, MOUSE_BUTTON_ON, 4);
	if (Is(mode, "unprintable")) { PLATFORM.font.reject = L'W'; SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0x11); }
	if (Is(mode, "font-no-dot")) PLATFORM.font.reject = L'.';
	if (Is(mode, "long-labels") || Is(mode, "long-names"))
	{
		SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0x36); SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0xcd);
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 0, 0, MOUSE_BUTTON_ONCE, 2); SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 0, 0, MOUSE_BUTTON_ONCE, 1);
		PLATFORM.font.advance = 1;	// so narrow the native wrapper never breaks the line
	}

	// --- queue states before the dodge radio arrives ---
	CMessage *prior = nullptr;
	if (Is(mode, "queue-busy") || Is(mode, "dead-queued") || Is(mode, "deleted-queued"))
	{
		prior = new CMessage(TEXT.speaker, TEXT.message, kOtherRadio, 1.f, nullptr, nullptr, 10); box.AddMessage(prior);
		Check(box.mCurrent == prior, "actual queue started the earlier radio");
		StartScheduled(box);
		Check(Active(h) == 0, "an unrelated radio message adds no cue");
	}
	if (Is(mode, "queue-blocked")) box.mCanAdvance = FALSE;
	if (Is(mode, "dead-queued") || Is(mode, "deleted-queued") || Is(mode, "dead-before-start")) thing.unit = true;

	float enqueued = EVENT_MANAGER.time;
	CallRadio(script, kDodge);
	if (Is(mode, "event-exhausted"))
	{
		Check(box.mQueue.Size() == 0 && Active(h) == 0, "event exhaustion leaves no radio and no cue");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}
	Check(MISSION_SCRIPT_VM.mStop && script.mSavedVMs.v.size() == 1, "original mission VM stops and saves once");
	Check(EVENT_MANAGER.events[0].id == SCRIPT_MESSAGE_RESUME && EVENT_MANAGER.events[0].delay == 0.05f && EVENT_MANAGER.events[0].owner == &script,
		"original resume event intact");
	if (Is(mode, "box-missing"))
	{
		Check(Active(h) == 0 && CMessage::alive == 0, "missing radio box leaves no cue and no message");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}
	Check(Active(h) == 0, "no cue when the script only queues the radio");

	if (prior)
	{
		// The earlier message plays on: long enough that a cue started at enqueue would already have expired.
		EVENT_MANAGER.time = enqueued + 26.f; h.Render();
		Check(Active(h) == 0, "no cue while the dodge radio waits in the queue");
		if (Is(mode, "dead-queued")) thing.dying = true;
		if (Is(mode, "deleted-queued")) box.mQueue.items.back()->mSpeaker.SetReader(nullptr);
		box.Finish();
		if (Is(mode, "dead-queued") || Is(mode, "deleted-queued"))
		{
			Check(box.mQueue.Size() == 0 && !box.mCurrent && CMessage::alive == 0, "the queue discards a message whose speaker is gone");
			EVENT_MANAGER.time += 1.f; h.Render();
			Check(Active(h) == 0, "a discarded radio message never shows its cue");
			printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
			return 0;
		}
	}
	if (Is(mode, "queue-blocked"))
	{
		EVENT_MANAGER.time = enqueued + 26.f; h.Render();
		Check(!box.mCurrent && Active(h) == 0, "messages switched off: the radio and its cue wait");
		box.SwitchMessagesOn();
	}
	Check(box.mCurrent && box.mCurrent->mName == TEXT.speaker && box.mCurrent->mText == TEXT.message && box.mCurrent->mStringId == kDodge &&
		box.mCurrent->mDelay == 2.5f && box.mCurrent->mPriority == 10, "radio message and schedule unchanged");
	Check(Active(h) == 0, "no cue before the radio's start event");
	if (Is(mode, "dead-before-start"))
	{
		thing.dying = true; StartScheduled(box);
		Check(Active(h) == 0 && EVENT_MANAGER.lastId == MESSAGE_BOX_FINISH && box.reveals == 0, "a speaker dead at the start shows no cue");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}
	int revealsBefore = box.reveals;
	StartScheduled(box);
	float started = EVENT_MANAGER.time;
	Check(box.reveals == revealsBefore + 1, "original radio reveal begins once");
	bool expected = ce && !fence && !Is(mode, "help-missing") && !Is(mode, "font-no-dot");
	Check(Active(h) == (expected ? 1 : 0), "dodge cue admission at the radio's actual start");

	if (Is(mode, "help-missing"))
	{
		CallHelp(script, kZoomIn); CallHelp(script, kRetro);
		Check(Active(h) == 0, "missing help display is harmless");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}
	if (!expected)
	{
		CallHelp(script, kZoomIn); CallHelp(script, kZoomOut);
		Check(Active(h) == 0, "zoom cues suppressed");
		CallHelp(script, kRetro);
		Check(Active(h) == 1 && h.mMessages[0].mText == retail_help, "original help unchanged");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}

	// --- the cue shown, timed from the radio's start ---
	Check(h.mMessages[0].mTime == started, "cue timed from the radio's start, not its enqueue");
	std::wstring dodge = Dodge(h);
	Check(dodge.size() > 0 && dodge.size() <= 99, "cue fits the native 100-character line");
	bool generic = Is(mode, "joystick") || Is(mode, "unbound") || Is(mode, "duplicate") || Is(mode, "layout-nonlatin") || Is(mode, "no-controller") || Is(mode, "timing-missing-generic") ||
		Is(mode, "pad-mouse-blocked") || Is(mode, "pad-mouse-normal") || Is(mode, "mouse-blocked-keys") ||
		Is(mode, "layout-unknown") || Is(mode, "strafe-once") || Is(mode, "long-names") || Is(mode, "unprintable");
	bool timingUnknown = Is(mode, "timing-missing") || Is(mode, "timing-type") || Is(mode, "timing-null") ||
		Is(mode, "timing-nan") || Is(mode, "timing-small") || Is(mode, "timing-large") || Is(mode, "timing-missing-generic");
	std::wstring timing = (Is(mode, "timing-change") || Is(mode, "pad-timing-change")) ? L"201-399ms" : L"101-199ms";
	if (generic)
		Check(dodge == (timingUnknown ? L"Ground dodge: fully strafe one way, then the other. Timing varies." :
			L"Ground dodge: fully strafe one way, then the other 101-199ms apart."), "unsupported movement binding uses the generic text");
	else if (Is(mode, "pad-timing-change"))
		Check(dodge == L"Ground dodge: A then D or fully push left stick left then right 201-399ms apart. Reverse for left.",
			"registered dash timing changes the controller cue");
	else if (Is(mode, "pad-default") || Is(mode, "pad-missing-button") || Is(mode, "pad-second") || Is(mode, "pad-swap"))
		Check(dodge == L"Ground dodge: A then D or fully push left stick left then right 101-199ms apart. Reverse for left.", "default gamepad and keyboard strafe named");
	else if (Is(mode, "pad-nonlatin") || Is(mode, "pad-player-two") || Is(mode, "pad-mouse-default"))
		Check(dodge == L"Ground dodge right: fully push left stick left then right 101-199ms apart. Reverse for left.", "a connected pad remains usable when the keyboard layout cannot be named");
	else if (Is(mode, "stick-default"))
		Check(dodge == L"Ground dodge: A then D or fully push twist left then right 101-199ms apart. Reverse for left.", "flight stick and keyboard strafe named");
	else if (Is(mode, "pad-custom"))
		Check(dodge == L"Ground dodge: A then D or fully push right stick left then right 101-199ms apart. Reverse for left.", "custom controller strafe named, not disabled default");
	else if (Is(mode, "pad-mouse-player-two"))
		Check(dodge == L"Ground dodge right: fully push right stick left then right 101-199ms apart. Reverse for left.", "player two reads its own slot despite player one's mouse axis");
	else if (Is(mode, "pad-strafe-buttons"))
		Check(dodge.find(L"fully push right button then left button") != std::wstring::npos,
			"different face buttons retain their full physical names");
	else if (Is(mode, "pad-long-keys"))
		Check(dodge == L"Ground dodge right: fully push left stick left then right 101-199ms apart. Reverse for left.", "long combined line uses the actual controller binding");
	else
	{
		const wchar_t *keys = Is(mode, "layout-azerty") ? L"Q, then D " : (Is(mode, "remap-keys") || Is(mode, "keyboard-player-two") || Is(mode, "mouse-unbound-keys")) ? L"Q, then E " :
			Is(mode, "remap-arrows") ? L"Left arrow, then Right arrow " : Is(mode, "remap-layout") ? L"A, then D " :
			Is(mode, "secondary") ? L"Q, then D " : Is(mode, "long-labels") ? L"Right Shift, then Right arrow " : L"A, then D ";
		std::wstring expectedText = std::wstring(L"Ground dodge right: ") + keys;
		if (timingUnknown) { expectedText.pop_back(); expectedText += L". Timing varies."; }
		else expectedText += timing + L" apart.";
		expectedText += L" Reverse for left.";
		Check(dodge == expectedText, Is(mode, "timing-change") ? "registered dash timing changes the cue" : "dodge names the current keys and the actual timing");
	}
	if (Is(mode, "pad-mouse-blocked") || Is(mode, "pad-mouse-normal") || Is(mode, "mouse-blocked-keys"))
	{
		// Keep the fixture's actual bindings fixed and toggle only the mouse boundary.
		sMouse = reinterpret_cast<void *>(1); probeKey = Is(mode, "mouse-blocked-keys"); probeAxis = probeKey ? 0.f : -1.f;
		MappingProbe blocked; blocked.DoMappings();
		Check(blocked.count[BUTTON_MECH_STRAFE_LEFT] == 1 && blocked.sent[BUTTON_MECH_STRAFE_LEFT] == 0.f,
			"slot-0 mouse rest suppresses slot-1 joystick or key");
		sMouse = nullptr; MappingProbe withoutMouse; withoutMouse.DoMappings();
		Check(withoutMouse.count[BUTTON_MECH_STRAFE_LEFT] == 1 && withoutMouse.sent[BUTTON_MECH_STRAFE_LEFT] == -1.f,
			"the same slot-1 input reaches the action with mouse absent");
		probeKey = false;
	}
	if (Is(mode, "pad-mouse-default"))
	{
		sMouse = reinterpret_cast<void *>(1); probeAxis = -1.f; MappingProbe controller; controller.DoMappings();
		Check(controller.count[BUTTON_MECH_STRAFE_LEFT] == 2 && controller.sent[BUTTON_MECH_STRAFE_LEFT] == -1.f,
			"centered mouse does not suppress the CE default");
		sMouse = nullptr;
	}
	if (Is(mode, "pad-mouse-player-two"))
	{
		sMouse = reinterpret_cast<void *>(1); probeAxis = -1.f; MappingProbe controller; controller.mPadNumber = 1; controller.DoMappings();
		Check(controller.count[BUTTON_MECH_STRAFE_LEFT] == 1 && controller.sent[BUTTON_MECH_STRAFE_LEFT] == -1.f,
			"player-two controller reads its own slot without evaluating slot zero");
		sMouse = nullptr;
	}
	EVENT_MANAGER.time = started + 19.5f; drawn.clear(); h.Render();
	std::wstring shown;
	for (auto &line : drawn) shown += (shown.empty() ? L"" : L" ") + line;
	Check(Active(h) == 1 && shown == dodge, "the existing renderer draws the whole cue through the radio");
	WrapChecks(h);
	if (Is(mode, "timing-change") || Is(mode, "pad-timing-change"))
	{
		CBattleEngineWalkerPart::mDashTime = .2f; drawn.clear(); h.Render();
		Check(Dodge(h).find(L"101-199ms") != std::wstring::npos && Dodge(h).find(L"201-399ms") == std::wstring::npos,
			"a changed registered dash time is refreshed before drawing");
		CBattleEngineWalkerPart::mDashTime = .4f;
	}
	if (Is(mode, "pad-hotplug"))
	{
		fakePad.connected = true; drawn.clear(); h.Render();
		Check(Dodge(h).find(L"left stick left") != std::wstring::npos, "connecting a pad refreshes its current guidance");
		fakePad.connected = false; drawn.clear(); h.Render();
		Check(Dodge(h) == dodge, "unplugging a pad removes its guidance");
	}
	if (Is(mode, "pad-swap"))
	{
		fakeDevice.pad = nullptr; fakeDevice.joy = reinterpret_cast<SDL_Joystick *>(&fakePad); drawn.clear(); h.Render();
		Check(Dodge(h).find(L"twist left") != std::wstring::npos && Dodge(h).find(L"left stick left") == std::wstring::npos,
			"a replacement stick refreshes both the actual preset and device names");
	}
	if (Is(mode, "expire"))
	{
		EVENT_MANAGER.time = started + 20.5f; h.Render();
		Check(Find(h, L"odge") < 0, "dodge cue expires 20 seconds after the radio started");
		printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
		return 0;
	}
	EVENT_MANAGER.time = started + .5f;

	// --- the same message starting again neither stacks a second cue nor restarts the shown one ---
	box.StartMessage();
	Check(Active(h) == 1 && Find(h, L"odge") >= 0 && h.mMessages[Find(h, L"odge")].mTime == started,
		"a repeated start does not duplicate the cue");

	// --- zoom help: retail ignores both IDs; the first adds a cue now ---
	CallHelp(script, kZoomIn);
	Check(Find(h, L"oom") >= 0, "zoom cue added at the first zoom help");
	Check(Active(h) == 1 && Find(h, L"odge") < 0, "a new CE cue retires the other: one CE cue at a time");
	std::wstring zoom = TextAt(h, Find(h, L"oom"));
	Check(zoom.size() <= 99, "zoom cue fits the native line");
	if (Is(mode, "pad-default") || Is(mode, "pad-missing-axis") || Is(mode, "pad-long-keys") || Is(mode, "pad-second") || Is(mode, "pad-mouse-default") || Is(mode, "pad-timing-change"))
		Check(zoom == L"Zoom: Mouse wheel down/Mouse wheel up or right bumper/left bumper (in/out). Some weapons only.", "available default controller zoom and mouse named");
	else if (Is(mode, "stick-default") || Is(mode, "stick-no-twist") || Is(mode, "pad-swap"))
		Check(zoom.find(L"stick button 6") != std::wstring::npos && zoom.find(L"stick button 8") != std::wstring::npos,
			"available flight stick zoom buttons named");
	else if (Is(mode, "pad-custom"))
		Check(zoom.find(L"right stick button") != std::wstring::npos && zoom.find(L"left stick button") != std::wstring::npos &&
			zoom.find(L"bumper") == std::wstring::npos, "custom controller zoom named, not disabled default");
	else if (Is(mode, "pad-face"))
		Check(zoom.find(L"lower button") != std::wstring::npos && zoom.find(L"right button") != std::wstring::npos &&
			zoom.find(L"upper button") == std::wstring::npos, "rotated physical face buttons named");
	else if (Is(mode, "pad-nonlatin") || Is(mode, "pad-player-two"))
		Check(zoom.find(L"right bumper") != std::wstring::npos && zoom.find(L"left bumper") != std::wstring::npos,
			"available controller zoom remains named when keys cannot be named");
	else if (Is(mode, "wheel-held") || Is(mode, "unbound") || Is(mode, "duplicate") || Is(mode, "keyboard-player-two") || Is(mode, "no-controller") ||
		Is(mode, "pad-mouse-zoom") || Is(mode, "pad-mouse-player-two"))
		Check(zoom == L"Zoom: see Zoom In and Zoom Out in Controller Configuration. Not every weapon zooms.", "unsupported zoom binding uses the generic text");
	else if (Is(mode, "long-labels") || Is(mode, "long-names"))
		Check(zoom == L"Zoom in: Middle mouse button. Zoom out: Right mouse button. Not every weapon zooms.", "mouse-button zoom named");
	else
		Check(zoom == L"Zoom in: Mouse wheel down. Zoom out: Mouse wheel up. Not every weapon zooms.", "actual default wheel directions");
	CallHelp(script, kZoomOut);
	Check(Active(h) == 1, "second zoom help adds nothing");
	std::wstring before0 = TextAt(h, 0), before1 = TextAt(h, 1);
	CallHelp(script, kZoomIn); CallHelp(script, 123456);
	Check(TextAt(h, 0) == before0 && TextAt(h, 1) == before1, "full slots and unrelated IDs keep the existing help");

	WrapChecks(h);

	// --- the game's own help keeps its priority ---
	if (Is(mode, "yield"))
	{
		CallHelp(script, kRetro);
		Check(Active(h) == 2 && Find(h, L"oom") >= 0, "the original help takes the free slot beside the CE cue");
		CallHelp(script, kRetro);
		Check(Active(h) == 2 && Find(h, L"oom") < 0 && h.mMessages[0].mText == retail_help && h.mMessages[1].mText == retail_help,
			"a CE cue yields to the original help");
		box.StartMessage(); CallHelp(script, kZoomIn);
		Check(Active(h) == 2 && h.mMessages[0].mText == retail_help && h.mMessages[1].mText == retail_help,
			"a CE cue never displaces the original help it yielded to");
	}
	if (Is(mode, "slots"))
	{
		WCHAR original[] = L"Other help"; h.mMessages[0].mText = original; h.mMessages[1].mActive = FALSE;
		h.mMessages[1].mText = retail_help; h.mMessages[1].mActive = TRUE;
		box.StartMessage(); CallHelp(script, kZoomIn);
		Check(TextAt(h, 0) == L"Other help" && TextAt(h, 1) == L"Original help", "cues never overwrite the game's own help");
	}

	// --- live changes are reflected before each draw ---
	if (Is(mode, "refresh"))
	{
		SetInput(BUTTON_MECH_CHANGE_ZOOM_IN, 0, 0, KEY_ON, 0x11); SetInput(BUTTON_MECH_CHANGE_ZOOM_OUT, 0, 0, KEY_ON, 0x1f);
		drawn.clear(); h.Render();
		Check(TextAt(h, Find(h, L"oom")) == L"Zoom in: W. Zoom out: S. Not every weapon zooms.", "remapped zoom refreshed before the draw");
		std::wstring all;
		for (auto &line : drawn) all += line + L" ";
		Check(all.find(L"Zoom in: W.") != std::wstring::npos, "existing renderer drew the refreshed zoom cue");
		fakeLayout = 1; h.Render();
		Check(TextAt(h, Find(h, L"oom")) == L"Zoom in: Z. Zoom out: S. Not every weapon zooms.", "keyboard layout change refreshed the zoom cue");
		fakeLayout = 0;
		// The dodge radio starting again shows the dodge cue in place of the zoom cue; remap it while it shows.
		box.StartMessage();
		Check(Active(h) == 1 && Find(h, L"oom") < 0 && Dodge(h).find(L": A, then D ") != std::wstring::npos, "the dodge cue replaces the zoom cue");
		SetInput(BUTTON_MECH_STRAFE_LEFT, 0, 0, KEY_ON, 0x10); SetInput(BUTTON_MECH_STRAFE_RIGHT, 0, 0, KEY_ON, 0x12);
		drawn.clear(); h.Render();
		Check(Dodge(h).find(L": Q, then E ") != std::wstring::npos, "remapped dodge refreshed before the draw");
		all.clear();
		for (auto &line : drawn) all += line + L" ";
		Check(all.find(L"Q, then E") != std::wstring::npos, "existing renderer drew the refreshed dodge cue");
		fakeLayout = 1; h.Render();
		Check(Dodge(h).find(L": A, then E ") != std::wstring::npos, "keyboard layout change refreshed");
		fakeLayout = 2; h.Render();
		Check(Dodge(h).find(L"strafe one way") != std::wstring::npos, "non-Latin layout falls back at the next draw");
		fakeLayout = 0; h.Render();
		Check(Dodge(h).find(L": Q, then E ") != std::wstring::npos, "restored layout refreshed");
		SetInput(BUTTON_MECH_STRAFE_LEFT, 0, -1, KEY_ON, 0); h.Render();
		Check(Dodge(h).find(L"strafe one way") != std::wstring::npos, "unbinding falls back at the next draw");
		Env("BEA_NET_HOST", "1"); h.Render();
		Check(Active(h) == 0, "cues disappear if a network session starts");
		unsetenv("BEA_NET_HOST");
	}
	printf("PASS %s: %d assertions; synthetic boundaries, no game or device\n", mode, checks);
	return 0;
}
