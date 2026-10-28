// SPDX-License-Identifier: GPL-3.0-only
// Execute the production CE console (port/game/ce_palette.cpp) with the real patched message handler against authored
// fake game boundaries: its fences, key ownership, line editor, parser, command gates, level-end undo and panel layout.
#include "ce_palette_fake.h"
#include "ce_console.h"
#include "ce_music.h"
#include "ce_photo.h"
#include <cstdio>
#include <map>

PCLTShell LT; WORD gCharScanCode = 0; bool gKeyPressed = false; Cli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
FakeGame GAME; FakeFrontend FRONTEND; FakeInterface GAMEINTERFACE; FakeHud HUD; FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO; FakeConsole CONSOLE; std::vector<FakeRect> sprites; std::vector<FakeDraw> draws;
bool fakeRecording = false;
#ifdef FAKE_NETWORK
extern "C" void SetBootRequested(bool value);
#endif
#include "ce_layer.inc"
#include "msgproc.inc"

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}

#ifdef CE_VERSION
// ---- the console's game actions (ce_console.h), recorded --------------------------------------------------------------
static std::vector<std::string> lines(60);		// the retail scrollback, newest first; 0 is the line being written
static char rawRow[0x80];						// an injected row with no terminator, as CConsole::Print can leave one
static bool useRawRow = false;
static bool cheats[6];
static std::string theme;
static int actions = 0;							// every call that changes the game
static std::vector<std::string> log;
static bool navMap = false, landscape = true, freeCamera = false, paused = false, remote = false, crosshair = true;
static float gamma = 0.f;
static int level = 200, jumped = -1, won = 0, lost = 0, players = 1;
static std::string film; static int films = 0; static bool filmLocalise = false;
static std::string played;
static bool snow = true, split = true; static int alpha = 200; static float lod = 1.f, rain = 0.f; static int hitR = 50;
static bool splitRegistered = true;
static void act(const std::string &what) { ++actions; log.push_back(what); }

extern "C" int CE_SettingIs(const char *key, const char *value) { return !std::strcmp(key, "console_theme") && theme == value; }
static int devStatsIni = 0, devStatsReads = 0;
extern "C" int CE_SettingGetBool(const char *key, int def) { ++devStatsReads; return !std::strcmp(key, "dev_stats") ? devStatsIni : def; }
void PortCeDevStatsOut(const char *, ...) {}
void PortCeDevStatsDraw() {}
// Photo mode (ce_photo.h), recorded; port/tools/ce_photo_tests.py runs the real one.
static bool photoOn = false, photoAvailable = true; static int photoKeys = 0;
bool PortCePhotoOn() { return photoOn; }
bool PortCePhotoSet(bool on) { if (!photoAvailable) { photoOn = false; return false; } act(on ? "photo on" : "photo off"); photoOn = on; return true; }
bool PortCePhotoKey(unsigned, bool) { ++photoKeys; return false; }

namespace CeConsoleGame {
bool ConsoleLine(int i, char *out, int n) {
    if (i < 0 || i >= 60 || n <= 0) return false;
    if (useRawRow && i == 1) {
        int k = 0;
        for (; k < n - 1 && k < 0x80 && rawRow[k]; ++k) out[k] = rawRow[k];
        out[k] = 0;
        return true;
    }
    std::snprintf(out, n, "%s", lines[i].c_str());
    return true;
}
int ConsoleLines() { return 60; }
void Print(const char *text) {
    check(std::strlen(text) < 100, "console lines are bounded before printing");
    lines.insert(lines.begin() + 1, text);
    lines.resize(60);
}
VarType Var(const char *name, void **data) {
    struct { const char *n; VarType t; void *d; } vars[] = {
        {"cg_snow", VAR_BOOL, &snow}, {"cg_consolealpha", VAR_INT, &alpha}, {"cg_meshlodbias", VAR_FLOAT, &lod},
        {"atm_raindensity", VAR_FLOAT, &rain}, {"cg_hiteffectfactorr", VAR_INT, &hitR},
        {"g_framelength", VAR_FLOAT, &lod}, {"cg_battleenginevisible", VAR_BOOL, &snow},
        {"cg_horizontalsplitscreen", VAR_BOOL, &split}};
    for (auto &v : vars)
        if (!std::strcmp(v.n, name) && (std::strcmp(name, "cg_horizontalsplitscreen") || splitRegistered)) { *data = v.d; return v.t; }
    return VAR_NONE;
}
bool CheatOn(int cheat) { return cheat >= 0 && cheat < 6 && cheats[cheat]; }
const char *Version() { return "Onslaught CE Vtest"; }
void WhatAmI() { Print("------------------"); Print("Graphics card info"); Print("Description    : Fake GPU"); }
void ListAtmospherics() { Print("Atmospheric effects : "); Print("Rain"); }
void NavMap(bool on) { act(on ? "navmap on" : "navmap off"); navMap = on; }
bool NavMapOn() { return navMap; }
bool LandscapePresent() { return landscape; }
bool FreeCameraOn() { return freeCamera; }
bool Paused() { return paused; }
bool ToggleFreeCamera() { act("camera"); freeCamera = !freeCamera; return true; }
int RemoteCameraOn() { if (remote || freeCamera) return -1; if (!crosshair) return 0; act("remote on"); remote = true; return 1; }
int RemoteCameraOff() { if (!remote) return 0; if (freeCamera) return -1; act("remote off"); remote = false; return 1; }
bool RemoteCameraActive() { return remote; }
void SetGammaBias(float v) { act("gamma"); gamma = v; }
int CurrentLevel() { return level; }
bool CampaignLevel(int l) { return l == 100 || l == 200 || l == 311; }
void JumpToLevel(int l) { act("jump"); jumped = l; }
void Win() { act("win"); ++won; }
void Lose() { act("lose"); ++lost; }
bool Film(const char *name, char *file, int n, bool *localise) {
    if (!std::strcmp(name, "05")) { std::snprintf(file, n, "cutscenes\\05"); *localise = true; return true; }
    if (!std::strcmp(name, "UsTheMovie")) { std::snprintf(file, n, "UsTheMovie"); *localise = false; return true; }
    return false;
}
void PlayFilm(const char *file, bool localise) { act("film"); film = file; filmLocalise = localise; ++films; }
bool Sound(const char *name, char *exact, int n) {
    if (!std::strcmp(name, "boom") || !std::strcmp(name, "BOOM")) { std::snprintf(exact, n, "Boom"); return true; }
    return false;
}
int Sounds() { return 3; }
void PlaySample(const char *exact) { act("sound"); played = exact; }
int Players() { return players; }
void SetHorizontalSplit(bool horizontal) { act("split"); void *d; if (Var("cg_horizontalsplitscreen", &d) == VAR_BOOL) *(bool *)d = horizontal; }
}

// ---- the music playlist's console side (ce_music.h), recorded ----------------------------------------------------------
static std::string musicDir, musicNow; static int musicTracks = 0, musicType = CE_MUSIC_LINEAR, musicNexts = 0, musicClears = 0;
static bool musicPlaying = false;
namespace CeMusic {
bool Allowed() { return true; }
int AddFolder(const char *dir, int *skipped, int *truncated) {
    act("music adddir"); musicDir = dir; *skipped = 1; *truncated = 0;
    if (std::strcmp(dir, "/music/My Tunes") && std::strcmp(dir, "/ok")) return -1;
    musicTracks += 3; return 3;
}
int Tracks() { return musicTracks; }
int Type() { return musicType; }
void SetType(int t) { act("music type"); musicType = t; }
bool Next() { ++musicNexts; if (!musicPlaying || !musicTracks) return false; musicNow = "/music/My Tunes/b.ogg"; return true; }
void Clear() { act("music clear"); ++musicClears; musicTracks = 0; musicNow.clear(); }
const char *Now() { return musicNow.empty() ? nullptr : musicNow.c_str(); }
const char *Folder() { return musicDir.c_str(); }
}
#endif

// ---- input --------------------------------------------------------------------------------------------------------------
static LPARAM lp(unsigned scan, bool repeat = false) { return (LPARAM(scan & 0x7f) << 16) | ((scan & 0x80) ? 0x1000000 : 0) | (repeat ? 0x40000000 : 0); }
static void down(unsigned scan, unsigned vk = 'A', bool repeat = false) { LT.MsgProc(nullptr, WM_KEYDOWN, vk, lp(scan, repeat)); }
static void up(unsigned scan, unsigned vk = 'A') { LT.MsgProc(nullptr, WM_KEYUP, vk, lp(scan)); }
static void character(unsigned scan, unsigned ch) { LT.MsgProc(nullptr, WM_CHAR, ch, lp(scan)); }
#ifdef CE_VERSION
static int otherDown = 0, otherUp = 0, otherChar = 0;
static void OtherTrap(WORD, KeyEventType event) { if (event == KEY_EVENT_DOWN) ++otherDown; if (event == KEY_EVENT_UP) ++otherUp; if (event == KEY_EVENT_CHAR) ++otherChar; }
static void press(unsigned scan, unsigned vk = 0) { down(scan, vk); up(scan, vk); }
static void enter() { down(DIK_RETURN, 13); character(DIK_RETURN, 13); up(DIK_RETURN, 13); }
static void type(const char *s) { for (; *s; ++s) { down(0x20, 'A'); character(0x20, (unsigned char)*s); up(0x20, 'A'); } }
static void command(const char *text) { type(text); enter(); }
static bool printed(const char *text) {		// a line in the scrollback since the last clear()
    for (int i = 1; i < 60; ++i) if (lines[i].find(text) != std::string::npos) return true;
    return false;
}
static void clear() { lines.assign(60, std::string()); }
static std::string newest() { return lines[1]; }
static void render() { draws.clear(); sprites.clear(); PortCePaletteRender(); }
static std::wstring inputLine() {		// the prompt is drawn just before the key hint, the panel's last line
    return draws.size() >= 2 ? draws[draws.size() - 2].text : L"";
}
static void openPalette() {
    down(DIK_GRAVE, 0xc0);
    character(DIK_GRAVE, '`');
    check(LT.GetKeytrap() != nullptr && LT.GetKeytrap() != OtherTrap, "grave opens the console");
    check(!LT.KeyDown[DIK_GRAVE], "opening grave is not a game keydown");
    up(DIK_GRAVE, 0xc0);
    check(!LT.KeyDown[DIK_GRAVE] && LT.KeyWasUp[DIK_GRAVE], "opening grave release clears state");
    render();
    check(inputLine() == L">  ", "opening grave character does not enter the line; the cursor bar follows the prompt");
}
static void closePalette() {
    down(DIK_ESCAPE, 27); character(DIK_ESCAPE, 27); up(DIK_ESCAPE, 27);
    check(!LT.GetKeytrap(), "Escape closes the console");
}
static void refused() {
    check(!PortCePaletteOpen(DIK_GRAVE, false), "admission refused");
    check(!LT.GetKeytrap(), "refusal leaves trap unowned");
}
static void fits() {
    for (const auto &s : sprites)
        check(s.x >= 0 && s.y >= 0 && s.x + s.w <= PLATFORM.w + 0.01f && s.y + s.h <= PLATFORM.h + 0.01f, "panel fits the window");
    for (const auto &d : draws)
        check(d.x >= 0 && d.y >= 0 && d.x + float(d.text.size() * 8) * d.sx <= PLATFORM.w + 0.01f &&
              d.y + PLATFORM.font.height * d.sy <= PLATFORM.h + 0.01f, "text fits the window");
}

static void gates() {
    // Every cheat command is refused, with no game call, until its own cheat is on; then it runs.
    struct { const char *line; int cheat; const char *action; } gated[] = {
        {"camera", 4, "camera"}, {"remotecameraon", 4, "remote on"}, {"map 100", 1, "jump"}, {"win", 1, "win"}, {"lose", 1, "lose"}};
    for (auto &g : gated) {
        openPalette();
        clear();
        const int before = actions;
        command(g.line);
        check(actions == before, "a cheat command does nothing without its cheat");
        check(printed("needs the"), "the refusal names the cheat");
        for (int other = 0; other < 6; ++other) if (other != g.cheat) cheats[other] = true;
        command(g.line);
        check(actions == before, "another cheat does not open a command");
        for (int other = 0; other < 6; ++other) cheats[other] = false;
        cheats[g.cheat] = true;
        command(g.line);
        check(actions == before + 1 && log.back() == g.action, "the command runs with its cheat on");
        cheats[g.cheat] = false;
        if (LT.GetKeytrap()) closePalette();
        if (remote) { cheats[4] = true; openPalette(); command("remotecameraoff"); closePalette(); cheats[4] = false; }
        if (freeCamera) { cheats[4] = true; openPalette(); command("camera"); closePalette(); cheats[4] = false; }
    }
    check(jumped == 100 && won == 1 && lost == 1, "level commands reach the game once each");
    // map takes only campaign levels and whole numbers.
    cheats[1] = true; openPalette();
    const int before = actions;
    for (const char *bad : {"map 530", "map 888", "map 000", "map -1", "map 1e2", "map 100x", "map", "map 100 200", "map 0x64"}) {
        command(bad);
        check(actions == before, "map refuses what is not a campaign level");
    }
    closePalette(); cheats[1] = false;
}

static void retailRefusals() {
    openPalette();
    const int before = actions;
    for (const char *line : {"exec autoexec.con", "bind f win", "Bind", "memstats x", "DumpMem x", "dumptextures", "listbinds",
                             "echo hi", "menu", "useconfiguration 1", "buildstaticshadows", "quit", "Exit"}) {
        command(line);
        check(actions == before, "retail commands outside the table do nothing");
    }
    command("EXEC autoexec.con");
    check(newest() == "exec is not available in Onslaught CE.", "a refused retail command says so");
    command("quit");
    check(newest() == "Use the pause menu (Esc) to quit.", "quitting points at the pause menu");
    command("set g_framelength 1");
    check(actions == before && lod == 1.f && printed("No variable g_framelength"), "the tick length is never settable");
    command("set cg_battleenginevisible 0");
    check(snow && printed("No variable cg_battleenginevisible"), "instance variables are never settable");
    closePalette();
}

static void inputBounds() {
    openPalette();
    const int before = actions;
    // 63 characters fit and run (an unknown command); the 64th is refused, never cut.
    std::string line(63, 'x');
    command(line.c_str());
    check(newest() == "Unknown command " + line + ". Type help.", "a full-length line is kept whole");
    type((line + "y").c_str());
    render();
    check(inputLine().find(L"refused") != std::wstring::npos, "overlength input shows it is refused");
    enter();
    check(actions == before && printed("Input refused"), "overlength input never runs");
    played.clear();
    // Each check types "playsound boom" (it does not close the console) with one character in the middle.
    auto plays = [] { int n = 0; for (const auto &l : log) n += l == "sound"; return n; };
    type("playsound bo"); down(0x20, 'A'); character(0x20, 0x00e9); up(0x20, 'A'); type("om"); enter();
    check(plays() == 0 && printed("Input refused"), "an unsupported character cannot be dropped to make a command");
    type("playsound bo"); character(0x0f, 9); type("om"); enter();
    check(plays() == 1, "Tab's character never enters the line");
    for (unsigned c : {127u, 0x100u, 0xffffu}) {
        type("playsound bo"); character(0x20, c); type("om"); enter();
        check(plays() == 1, "delete and wide characters are refused");
    }
    for (unsigned c : {1u, 8u, 13u, 27u}) { type("playsound bo"); character(0x20, c); type("om"); enter(); }
    check(plays() == 5, "control characters (Enter's, Backspace's and Escape's own) never enter the line");
    type("playsound boomx"); press(DIK_BACK, 8); enter();
    check(plays() == 6, "Backspace edits once");
    type((line + "zz").c_str()); press(DIK_BACK, 8); type("playsound boom"); enter();
    check(plays() == 7 && LT.GetKeytrap(), "Backspace clears a refused line");
    // Words: at most four; each as long as the line allows.
    command("show a b c d");
    check(printed("Too many words"), "more than four words are refused");
    command("   help    win   ");
    check(printed("Wins this level"), "spaces separate words");
    closePalette();
}

static void history() {
    openPalette();
    command("version");
    command("status");
    press(DIK_UP); render();
    check(inputLine() == L"> status ", "Up recalls the last command");
    press(DIK_UP); render();
    check(inputLine() == L"> version ", "Up again recalls the one before");
    press(DIK_DOWN); press(DIK_DOWN); render();
    check(inputLine() == L">  ", "Down returns to an empty line");
    for (int i = 0; i < 20; ++i) { char c[16]; std::snprintf(c, sizeof c, "version %d", i); command(c); }
    for (int i = 0; i < 30; ++i) press(DIK_UP);
    render();
    check(inputLine() == L"> version 12 ", "history keeps eight lines");
    closePalette(); openPalette();
    type("LISTat"); press(DIK_TAB, 9); render();
    check(inputLine() == L"> listatmospherics  ", "Tab completes a unique prefix");
    closePalette(); openPalette();
    type("navmapo"); press(DIK_TAB, 9); render();
    check(inputLine() == L"> navmapo ", "Tab leaves an ambiguous prefix");
    closePalette();
}

static void commands() {
    openPalette();
    clear();
    command("help");
    check(printed("Commands") && printed("navmapon") && printed("camera*") && printed("win*"), "help lists commands and marks cheat ones");
    command("help map");
    check(printed("Unlock all levels"), "help names a command's cheat");
    command("version");
    check(newest() == "Onslaught CE Vtest", "version");
    command("status");
    check(newest() == "Level 200. Normal camera. Battle running.", "status");
    command("cg_whatami");
    check(newest() == "Description    : Fake GPU", "cg_whatami prints the game's own lines");
    command("ListAtmospherics");
    check(newest() == "Rain", "listatmospherics, any case");
    clear();
    command("showvars");
    check(printed("cg_snow = 1") && printed("(not here)") && !printed("g_framelength"), "showvars lists values and missing variables");
    command("show cg_meshlodbias");
    check(newest() == "cg_meshlodbias = 1.000", "show");
    // set: type and range checks, then the change is put back when the level ends.
    int before = actions;
    for (const char *bad : {"set cg_snow maybe", "set cg_meshlodbias 9", "set cg_meshlodbias 0.1", "set atm_raindensity 1.5",
                            "set cg_hiteffectfactorr 401", "set cg_hiteffectfactorr -1", "set cg_meshlodbias 1..2",
                            "set cg_consolealpha 10", "set cg_snow", "set cg_meshlodbias nan"})
        command(bad);
    check(snow && lod == 1.f && rain == 0.f && hitR == 50 && alpha == 200 && actions == before, "set refuses bad values");
    command("set cg_snow off"); command("set cg_meshlodbias 0.5"); command("set atm_raindensity 1"); command("set cg_hiteffectfactorr 400");
    command("set cg_snow 1"); command("set cg_snow 0");
    check(!snow && lod == 0.5f && rain == 1.f && hitR == 400, "set changes whitelisted variables");
    command("navmapon");
    check(navMap, "navmapon");
    command("setgammabias 1");
    check(gamma == 0.005f, "the gamma bias is clamped");
    command("setgammabias -1");
    check(gamma == 0.005f && printed("Usage: setgammabias"), "a negative gamma bias is refused");
    played.clear();
    command("playsound ../x");
    check(played.empty() && printed("No loaded sound"), "playsound never takes an unloaded name");
    command("playsound BOOM");
    check(played == "Boom", "playsound plays a loaded sound by its own name");
    command("fmv_play ..\\..\\x");
    check(films == 0 && printed("Films: a cutscene number"), "fmv_play only lists the game's films");
    command("fmv_play 05");
    check(films == 0 && printed("Films are not played during a battle"), "fmv_play plays no film in a battle (review 0.1.9)");
    PortCeConsoleFrame();
    PortCeConsoleFrame();
    check(films == 0, "nor on a later frame");	// and the console stays open
    command("splitscreen diagonal");
    check(printed("Usage: splitscreen"), "splitscreen takes two words");
    command("splitscreen vertical");
    PortCeConsoleLevelStart();
    check(split, "a solo level keeps the game's split");
    players = 2; splitRegistered = true; PortCeConsoleLevelStart();
    check(!split, "the next two-player level splits side by side");
    split = true; PortCeConsoleLevelStart();
    check(split, "only once");
    players = 1;
    // A film asked for, then the level ends first: it never plays.
    command("fmv_play UsTheMovie");
    PortCeConsoleLevelEnd();
    PortCeConsoleFrame();
    check(films == 0, "a pending film is dropped at level end");
    check(snow && lod == 1.f && rain == 0.f && hitR == 50, "level end restores every variable");
    openPalette(); command("set atm_raindensity 0.5"); rain = 0.25f;	// the level's own script changes it afterwards
    PortCeConsoleLevelEnd();
    check(rain == 0.25f, "a value the game changed since is left alone");
    rain = 0.f;
    check(!navMap, "level end restores the navigation map");
    check(gamma == 0.f, "level end resets the gamma bias");
    before = actions;
    PortCeConsoleLevelEnd();
    check(actions == before, "a second level end changes nothing");
    // The free camera and remote camera.
    cheats[4] = true;
    openPalette();
    command("remotecameraoff");
    check(printed("not on"), "remote camera off when none");
    crosshair = false; command("remotecameraon"); crosshair = true;
    check(!remote && printed("nothing under the crosshair"), "remote camera needs a target");
    command("remotecameraon");
    check(remote, "remote camera on");
    command("camera");
    check(!freeCamera && printed("unavailable"), "the free camera waits for the remote camera");
    PortCeConsoleLevelEnd();
    check(!remote && !LT.GetKeytrap(), "level end returns from the remote camera and closes the console");
    landscape = false; openPalette(); command("camera"); command("navmapon");
    check(!freeCamera && !navMap, "no landscape: no free camera or navigation map");
    landscape = true; closePalette(); cheats[4] = false;
}

static void panel() {
    openPalette();
    for (int i = 0; i < 40; ++i) { char t[32]; std::snprintf(t, sizeof t, "line %d", i); CeConsoleGame::Print(t); }
    render();
    check(sprites.size() >= 2 && sprites[0].colour == 0xc8000000, "the original's dark panel, cg_consolealpha 200");
    bool newestShown = false;
    for (const auto &d : draws) newestShown = newestShown || d.text == L"line 39";
    check(newestShown, "the newest line is shown");
    for (int i = 0; i < 3; ++i) press(DIK_PRIOR);
    render();
    bool scrolled = true;
    for (const auto &d : draws) scrolled = scrolled && d.text != L"line 39";
    check(scrolled, "PgUp scrolls back");
    for (int i = 0; i < 40; ++i) press(DIK_PRIOR);
    render();
    for (int i = 0; i < 40; ++i) press(DIK_NEXT);
    render();
    newestShown = false;
    for (const auto &d : draws) newestShown = newestShown || d.text == L"line 39";
    check(newestShown, "PgDn returns");
    // The font lacks "_" and the brackets: an underscore is a bar, brackets are parentheses.
    CeConsoleGame::Print("cg_whatami [OK]");
    render();
    bool mapped = false;
    for (const auto &d : draws) mapped = mapped || d.text == L"cg whatami (OK)";
    check(mapped, "underscore and brackets are drawable");
    size_t bars = 0;
    for (const auto &s : sprites) bars += s.h <= 1.01f;
    check(bars >= 3, "underscores (and the prompt's cursor) are drawn as bars");
    // The load log pads status lines to 80 columns: the padding is collapsed.
    CeConsoleGame::Print("Loading Textures                                                                 (OK)");
    render();
    mapped = false;
    for (const auto &d : draws) mapped = mapped || d.text == L"Loading Textures  (OK)";
    check(mapped, "padding collapsed");
    // A row the game left without a terminator, 0x80 bytes of text.
    std::memset(rawRow, 'w', sizeof rawRow);
    useRawRow = true;
    render();
    useRawRow = false;
    for (auto size : {std::pair<int,int>(640,480), {1920,1080}, {3840,2160}, {480,640}, {320,200}}) {
        PLATFORM.w = size.first; PLATFORM.h = size.second;
        render();
        check(draws.size() >= 3 && !inputLine().empty(), "scrollback, prompt and hint visible");
        fits();
    }
    PLATFORM.w = 640; PLATFORM.h = 480;
    for (int height : {80, 200, 1000}) {
        PLATFORM.font.height = height;
        render();
        check(!inputLine().empty(), "a tall font still shows the prompt");
        fits();
    }
    PLATFORM.font.height = 16;
    render();
    check(draws.back().colour == 0xffa8a8a8 && inputLine() == L">  ", "default text colours");
    command("set cg_consolealpha 100");
    render();
    check(sprites[0].colour == 0x64000000, "cg_consolealpha sets the background");
    PortCeConsoleLevelEnd();
    check(alpha == 200, "and is put back when the level ends");
    theme = "green";
    openPalette();
    render();
    bool green = false;
    for (const auto &d : draws) green = green || d.colour == 0xff40ff40;
    check(green && sprites[0].colour == 0xc8000000, "console_theme=green colours the text, not the panel");
    theme = "";
    closePalette();
}

static void lifetimes() {
    // A key held before opening remains a game key until its ordinary release.
    down(0x11, 'W');
    check(LT.KeyDown[0x11], "preexisting held key reaches game");
    down(DIK_GRAVE, 0xc0); character(DIK_GRAVE, '`');
    auto paletteTrap = LT.GetKeytrap();
    check(paletteTrap != nullptr, "grave opens");
    down(DIK_GRAVE, 0xc0, true); character(DIK_GRAVE, '`');
    check(LT.GetKeytrap() == paletteTrap, "opening key repeat does not close");
    up(DIK_GRAVE, 0xc0); up(0x11, 'W');
    check(!LT.KeyDown[0x11] && LT.KeyWasUp[0x11], "release of preexisting held key reaches game state");
    // A character key still held across close has no binding-up or char delivered to another owner.
    down(0x20, 'A'); character(0x20, 'x');
    const auto before = CONSOLE.binds.size();
    closePalette();
    down(0x20, 'A', true); character(0x20, 'x'); up(0x20, 'A');
    check(CONSOLE.binds.size() == before && !LT.KeyDown[0x20] && LT.KeyWasUp[0x20], "console-owned key release clears state without a bind");
    check(!PortCePaletteSuppressChar(0x20), "release clears pending suppression");
    down(0x30, 'B'); up(0x30, 'B');
    check(CONSOLE.binds.size() == before, "the CE layer runs no key binds");
    // Competing input owners always win, and held console events never leak to that owner.
    openPalette(); down(0x20, 'A'); character(0x20, 'x');
    LT.SetKeytrap(OtherTrap);
    down(0x20, 'A', true); character(0x20, 'x'); up(0x20, 'A');
    render();
    check(LT.GetKeytrap() == OtherTrap && draws.empty() && otherDown == 0 && otherChar == 0 && otherUp == 0, "replacement trap retained; stale character and up suppressed");
    down(0x21, 'B'); character(0x21, 'b'); up(0x21, 'B');
    check(otherDown == 1 && otherChar == 1 && otherUp == 1, "new owner's own events are delivered");
    LT.MsgProc(nullptr, WM_ACTIVATEAPP, 0, 0); PortCePaletteClose(); PortCeConsoleLevelEnd();
    check(LT.GetKeytrap() == OtherTrap, "focus loss and teardown never clear a foreign trap");
    LT.SetKeytrap(nullptr);
    openPalette(); down(0x20, 'A'); character(0x20, 'x');
    LT.MsgProc(nullptr, WM_ACTIVATEAPP, 0, 0);
    check(!LT.GetKeytrap(), "focus loss closes the console");
    up(0x20, 'A'); check(!LT.KeyDown[0x20] && LT.KeyWasUp[0x20], "focus-loss keyup clears state");
    // Focus loss is not a level end: nothing is put back.
    openPalette(); command("navmapon"); LT.MsgProc(nullptr, WM_ACTIVATEAPP, 0, 0);
    check(navMap, "focus loss keeps the level's changes");
    PortCeConsoleLevelEnd(); check(!navMap, "until the level ends");
    openPalette(); GAME.state = 4; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "leaving play closes the console"); GAME.state = GAME_STATE_PLAYING;
    openPalette(); GAME.pause.mActive = TRUE; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "pause menu taking focus closes the console"); GAME.pause.mActive = FALSE;
    // A command typed while play ends is not run.
    openPalette(); type("navmapon"); GAME.state = 4; enter(); GAME.state = GAME_STATE_PLAYING;
    check(!navMap && !LT.GetKeytrap(), "no command runs once play has ended");
}

static void devStats() {
    // ce.ini's dev_stats=1 turns it on (read once); devstats overrides it for the level; level end returns to ce.ini.
    devStatsIni = 1;
    check(PortCeDevStatsOn(), "dev_stats=1 shows the overlay");
    devStatsIni = 0;
    check(PortCeDevStatsOn() && devStatsReads == 1, "ce.ini is read once");
    openPalette();
    command("devstats off");
    check(!PortCeDevStatsOn(), "devstats off");
    command("devstats");
    check(PortCeDevStatsOn(), "devstats toggles");
    command("devstats maybe");
    check(printed("Usage: devstats"), "devstats takes on or off");
    command("devstats 0");
    check(!PortCeDevStatsOn(), "devstats 0");
    command("devstats off");
    PortCeConsoleLevelEnd();
    check(PortCeDevStatsOn(), "level end returns to ce.ini (on)");
    openPalette(); command("devstats off"); closePalette();
    // Fences: every replay, record, probe, clock and network switch hides it; multiplayer and the front end too.
    openPalette(); command("devstats on"); closePalette();
    for (const char *flag : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"}) {
        setenv(flag, "1", 1); check(!PortCeDevStatsOn(), "a fence hides the overlay"); unsetenv(flag);
    }
    fakeRecording = true; check(!PortCeDevStatsOn(), "recording hides it"); fakeRecording = false;
    GAME.multiplayer = true; check(!PortCeDevStatsOn(), "multiplayer hides it"); GAME.multiplayer = false;
    FRONTEND.running = true; check(!PortCeDevStatsOn(), "the front end hides it"); FRONTEND.running = false;
    PLAYABLE_DEMO = true; check(!PortCeDevStatsOn(), "the demo hides it"); PLAYABLE_DEMO = false;
    // The proof switch draws it under a tape only with the CE layer forced on, never for a network game.
    setenv("BEA_PORT_TAPE", "1", 1); setenv("BEA_PORT_DEVSTATS_PROOF", "1", 1);
    check(!PortCeDevStatsOn(), "the proof switch alone does not open the fence");
    setenv("BEA_PORT_CE_LAYER", "1", 1);
    check(PortCeDevStatsOn(), "the proof switch with the CE layer forced draws under a tape");
#ifdef FAKE_NETWORK
    SetBootRequested(true); check(!PortCeDevStatsOn(), "never in a network game"); SetBootRequested(false);
#endif
    unsetenv("BEA_PORT_TAPE"); unsetenv("BEA_PORT_DEVSTATS_PROOF"); unsetenv("BEA_PORT_CE_LAYER");
    PortCeConsoleLevelEnd();
    devStatsIni = 0;
}

static void photo() {
    // photo switches photo mode (the panel closes for a clear view); off leaves it open; photo mode hides devstats.
    openPalette(); command("devstats on");
    check(PortCeDevStatsOn(), "devstats on before photo mode");
    clear(); command("photo");
    check(photoOn && log.back() == "photo on" && !LT.GetKeytrap() && printed("Photo mode on"), "photo turns it on and closes the panel");
    check(!PortCeDevStatsOn(), "photo mode hides the developer information");
    openPalette(); clear(); command("photo off");
    check(!photoOn && LT.GetKeytrap() && printed("Photo mode off."), "photo off, the panel stays");
    check(PortCeDevStatsOn(), "the developer information returns after photo mode");
    clear(); command("photo on");
    check(photoOn, "photo on");
    openPalette(); clear(); command("photo 1");
    check(photoOn && !LT.GetKeytrap(), "photo 1 keeps it on");
    openPalette(); clear(); command("photo 0");
    check(!photoOn, "photo 0");
    clear(); command("photo maybe");
    check(!photoOn && printed("Usage: photo"), "photo takes on or off");
    clear(); command("photo on now");
    check(!photoOn && printed("Usage: photo"), "photo takes one word at most");
    photoAvailable = false; clear(); command("photo on");
    check(!photoOn && printed("Photo mode is temporarily unavailable. F12 saves a screenshot."), "photo mode refusal is said so");
    photoAvailable = true;
    clear(); command("help photo");
    check(printed("photo [on|off]") && printed("F12 saves a screenshot"), "help photo names the keys");
    command("devstats off"); closePalette();
    check(photoKeys > 0, "the message handler offers keys to photo mode first");
    PortCeConsoleLevelEnd();
}

static void music() {
    // The retail playlist commands as music ...: status, a folder with spaces, play types, next and clear.
    openPalette();
    clear(); command("music");
    check(printed("Your music: 0 tracks, play type linear.") && printed("The game's music plays."), "music alone reports the playlist");
    clear(); command("music adddir /music/My Tunes");
    check(musicDir == "/music/My Tunes" && printed("Added 3 tracks; 1 .ogg file left out.") && printed("stereo, 44100 Hz"),
          "music adddir takes the rest of the line as the folder, spaces included");
    clear(); command("music adddir /music/My Tunes Are Many");
    check(musicDir == "/music/My Tunes Are Many" && printed("Cannot read the folder /music/My Tunes Are Many."),
          "a folder of more words than the console's word limit still reaches adddir");
    clear(); command("music adddir");
    check(printed("Usage: music adddir <folder>"), "adddir needs a folder");
    clear(); command("music playtype random");
    check(musicType == CE_MUSIC_RANDOM && printed("Play type random"), "playtype random");
    clear(); command("music playtype SINGLE");
    check(musicType == CE_MUSIC_SINGLE && printed("Play type single"), "playtype ignores case");
    clear(); command("music playtype shuffle");
    check(musicType == CE_MUSIC_SINGLE && printed("Usage: music playtype single|linear|random."), "an unknown play type changes nothing");
    clear(); command("music playtype random please");
    check(musicType == CE_MUSIC_SINGLE && printed("Usage: music playtype"), "a play type with extra words is refused");
    clear(); command("music next");
    check(printed("Your music plays with a level's music."), "next refused when the player's music is not playing");
    musicPlaying = true; clear(); command("music next");
    check(printed("Playing b.ogg"), "next names the track it plays");
    clear(); command("music clear");
    check(musicClears == 1 && printed("Playlist cleared"), "clear");
    clear(); command("music loud");
    check(printed("Usage: music, music adddir"), "an unknown music word prints the usage");
    clear(); command("music clear now please");
    check(musicClears == 1 && printed("Usage: music"), "extra words after clear are refused");
    closePalette();
    musicPlaying = false; musicType = CE_MUSIC_LINEAR;
}

static void fences() {
    // CE layer remains cached on, so these exercise separate live fences even under CE forced on for a replay.
    for (const char *flag : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"}) {
        setenv(flag, "1", 1); refused(); unsetenv(flag);
    }
    fakeRecording = true; refused(); fakeRecording = false;
#ifdef FAKE_NETWORK
    SetBootRequested(true); refused(); SetBootRequested(false);
#endif
    GAME.multiplayer = true; refused(); GAME.multiplayer = false;
    GAME.players = 2; refused(); GAME.players = 1;
    GAME.state = 2; refused(); GAME.state = 3;
    GAME.pause.mActive = TRUE; refused(); GAME.pause.mActive = FALSE;
    GAMEINTERFACE.menu = true; refused(); GAMEINTERFACE.menu = false;
    GAMEINTERFACE.mShowJoypad = true; refused(); GAMEINTERFACE.mShowJoypad = false;
    GAME.playerPresent = false; refused(); GAME.playerPresent = true;
    GAME.controllerPresent = false; refused(); GAME.controllerPresent = true;
    GAME.cameraPresent = false; refused(); GAME.cameraPresent = true;
    GAME.pausePresent = false; refused(); GAME.pausePresent = true;
    FRONTEND.running = true; refused(); FRONTEND.running = false;
    PLATFORM.present = false; refused(); PLATFORM.present = true;
    HUD.present = false; refused(); HUD.present = true;
    HUD.white.width = 0; refused(); HUD.white.width = 64;
    PLATFORM.font.height = 0; refused(); PLATFORM.font.height = 16;
    PLATFORM.w = 0; refused(); PLATFORM.w = 640;
    openPalette(); HUD.present = false; PortCePaletteCheck();
    check(!LT.GetKeytrap(), "loss of render resources releases trap"); HUD.present = true;
    CLIPARAMS.mRecordDemo = true; refused(); CLIPARAMS.mRecordDemo = false;
    CLIPARAMS.mPlaybackDemo = true; refused(); CLIPARAMS.mPlaybackDemo = false;
    CLIPARAMS.mAutoConfigTest = true; refused(); CLIPARAMS.mAutoConfigTest = false;
    CLIPARAMS.mAttractMode = true; refused(); CLIPARAMS.mAttractMode = false;
    CLIPARAMS.mStressTest = 1; refused(); CLIPARAMS.mStressTest = 0;
    PLAYABLE_DEMO = true; refused(); PLAYABLE_DEMO = false;
    // A film asked for, then a fence closes before the next frame: it never plays.
    openPalette(); command("fmv_play 05"); setenv("BEA_PORT_TAPE", "1", 1); PortCeConsoleFrame(); unsetenv("BEA_PORT_TAPE");
    check(films == 0, "a fenced frame plays no film");
    PortCeConsoleLevelEnd();
    LT.SetKeytrap(OtherTrap); down(DIK_GRAVE, 0xc0); up(DIK_GRAVE, 0xc0);
    check(LT.GetKeytrap() == OtherTrap && otherDown == 2 && otherUp == 2, "existing owner retains its grave events");
    LT.SetKeytrap(nullptr);
}
#endif

int main(int argc, char **argv) {
#ifdef CE_VERSION
    if (argc > 1 && !std::strcmp(argv[1], "denied")) {
        refused();
        devStatsIni = 1;
        check(!PortCeDevStatsOn(), "a disabled layer never draws the overlay");
        PortCeConsoleFrame(); PortCeConsoleLevelStart(); PortCeConsoleLevelEnd();
        check(actions == 0, "a disabled console makes no game call");
        std::puts("disabled mode passed");
        return 0;
    }
    gates();
    retailRefusals();
    inputBounds();
    history();
    commands();
    panel();
    lifetimes();
    devStats();
    photo();
    music();
    fences();
    std::puts("console admission, gates, bounds, undo, panel and key lifetimes passed");
#else
    (void)argc; (void)argv;
    down(DIK_GRAVE, 0xc0); character(DIK_GRAVE, '`'); up(DIK_GRAVE, 0xc0);
    check(!LT.GetKeytrap() && CONSOLE.binds.size() == 2 && !LT.KeyDown[DIK_GRAVE] && LT.KeyWasUp[DIK_GRAVE], "faithful message path unchanged");
    std::puts("faithful message path passed");
#endif
}
