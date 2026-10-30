// SPDX-License-Identifier: GPL-3.0-only
// Runs the real patched CCareer::Save and CPauseMenu::Resume, the CE bonus module, and the CE menu/god gates with
// authored in-memory objects. It opens no game, player files or saves. Included fragments are cut from the sources.
#include "ce_bonus_fake.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

typedef std::int16_t SWORD;
typedef std::uint32_t DWORD;
enum { TK_AIRCRAFT = 0, TK_VEHICLES = 1 };
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define PORT_CE_GOD_CHEAT 3
#define PORT_CE_GOD_KEY "god_mode"
#define PORT_LOG_PREFIX "fixture: "

CCareer CAREER;
FakeFrontend FRONTEND;
FakeGame GAME;
struct { int mAutoConfigTest, mLevelNo; } CLIPARAMS;
typedef int SINT;
enum { QT_QUIT_TO_FRONTEND = 1, QT_QUIT_TO_SYSTEM = 2 };
int PLAYABLE_DEMO;
bool gPortCheatNameOnly;
static bool cheatSwitch, readFailed, settingWriteFailed;
static int settingReads, settingWrites, savedMask;
int CE_CheatOn(int) { return cheatSwitch; }
int CE_TrySettingGet(const char *, int, int *out) { ++settingReads; *out = savedMask; return !readFailed; }
int CE_SettingSet(const char *, int value)
{ ++settingWrites; if (settingWriteFailed) return 0; savedMask = value; return 1; }

#ifdef CE_VERSION
struct { int mNextLevel; } SYSTEM;
#include "native_policy.inc"
bool PortCeBonusStart(int);
bool PortCeBonusLevelEnd();
bool PortCeBonusCoopRunning();
bool PortCeBonusReturning();
bool PortCeBonusRaceReturning();
bool PortCeBonusCoopReturning();
int PortCeBonusTakeReturn();
void PortCeBonusBeforeConsoleLevel();
void PortCeBonusFilterSave(CCareer *, void *);

// The actual console jump action and the actual System.cpp loop from the patched source. The target RunLevel is an
// authored boundary which records the career it receives; it changes no progress on its own.
namespace { int sPendingLevel = -1; }
#include "native_jump_action.inc"
struct TargetGame
{
 int calls, level;
 CCareer onEntry;
 int RunLevel(int destination) { ++calls; level = GAME.level = destination; onEntry = CAREER; return QT_QUIT_TO_FRONTEND; }
} TARGET_GAME;
static void runActualConsoleLoop(int quit)
{
 SINT result = -1;
#define GAME TARGET_GAME
#include "native_jump_loop.inc"
#undef GAME
 (void)result;
}
#endif

struct SControlBinding { int mId, mConfigurable, value; };
SControlBinding CONTROL_BINDINGS[] = {{10, 1, 111}, {20, 0, 222}, {30, 1, 333}, {-1, 0, 0}};
static SWORD current_version_stamp() { return 0x1234; }
static const unsigned char options[] = {0x91, 0x23, 0x45, 0x67};
char *CCareer::WriteOptions(char *dest) { std::memcpy(dest, options, sizeof(options)); return dest + sizeof(options); }
int CCareer::SizeOfSaveGame() { return sizeof(SWORD) + sizeof(CCareer) + 2 * sizeof(SControlBinding) + sizeof(options); }
#include "native_screen.inc"
#include "native_save.inc"

struct CController {};
enum { MEMTYPE_SCRATCHPAD = 0, MCE_SUCCESS = 0 };
void *operator new[](std::size_t n, int) { return ::operator new[](n); }
void operator delete[](void *p, int) noexcept { ::operator delete[](p); }
static int creates, writes, notices, defaultWrites, uncontrols, mouseInitialisations, deactivations;
static bool createFailure, writeFailure, optionsFailure, pendingOptionsFailure;
static std::vector<char> careerBytes, defaultBytes;
struct FakeCard
{
 int CreateSave(int, char *, int *save, int) { ++creates; *save = 4; return createFailure ? 1 : MCE_SUCCESS; }
 int WriteSave(int, int, char *, const char *buf, int size)
 { ++writes; if (writeFailure) return 1; careerBytes.assign(buf, buf + size); return MCE_SUCCESS; }
} MEMORYCARD;
struct CFEPOptions
{
 static void WriteDefaultOptionsFile(const char *buf, int size)
 { ++defaultWrites; if (optionsFailure) pendingOptionsFailure = true; else defaultBytes.assign(buf, buf + size); }
};
bool PortCeTakeOptionsFailure() { bool result = pendingOptionsFailure; pendingOptionsFailure = false; return result; }
void PortCeSaveFailedNotice() { ++notices; }
struct CPauseMenu
{
 bool mOptionsPage;
 void RelinquishControl() { ++uncontrols; }
 void InitMouse() { ++mouseInitialisations; }
 void DeActivate(int) { ++deactivations; }
 void Resume(CController *);
};
#include "native_resume.inc"

static void check(bool condition, const char *message)
{ if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); } }

static void initial()
{
 std::memset(&CAREER, 0x65, sizeof(CAREER));
 std::memset(&FRONTEND, 0, sizeof(FRONTEND));
 GAME.level = -1;
 CLIPARAMS.mLevelNo = -1;
 CAREER.progress = 101;
 CAREER.sound = .25f; CAREER.music = .5f;
 CAREER.killedAircraft = 0x7a123456u; CAREER.killedVehicles = 0x85234567u;
 for (int p = 0; p < 2; ++p)
 {
  CAREER.god[p] = p; CAREER.invertY[p] = p; CAREER.invertJetY[p] = !p;
  CAREER.vibration[p] = p; CAREER.controllerConfig[p] = p + 1;
 }
 std::strcpy(FRONTEND.mFEPSaveGame.mSaveGameName, "authored");
}

static void changeLive()
{
 CAREER.progress = 909;
 CAREER.unknownBefore[10] ^= 0x31; CAREER.unknownAfter[20] ^= 0x64;
 CAREER.sound = .75f; CAREER.music = .125f;
 CAREER.killedAircraft = 0xa5111111u; CAREER.killedVehicles = 0x4a222222u;
 for (int p = 0; p < 2; ++p)
 {
  CAREER.god[p] = 1; CAREER.invertY[p] = !p; CAREER.invertJetY[p] = p;
  CAREER.vibration[p] = !p; CAREER.controllerConfig[p] = p + 5;
 }
}

// The expected object is constructed independently: progress and unknown bytes come from entry, all settings from
// live, with only the high bytes of the kill counters used for screen position. The existing god policy is applied.
static CCareer expectedFiltered(CCareer entry, const CCareer &live, bool suppressGod)
{
 entry.sound = live.sound; entry.music = live.music;
 entry.killedAircraft = (entry.killedAircraft & 0x00ffffffu) | (live.killedAircraft & 0xff000000u);
 entry.killedVehicles = (entry.killedVehicles & 0x00ffffffu) | (live.killedVehicles & 0xff000000u);
 for (int p = 0; p < 2; ++p)
 {
  entry.god[p] = suppressGod ? 0 : live.god[p];
  entry.invertY[p] = live.invertY[p]; entry.invertJetY[p] = live.invertJetY[p];
  entry.vibration[p] = live.vibration[p]; entry.controllerConfig[p] = live.controllerConfig[p];
 }
 return entry;
}

static void verifyBytes(const std::vector<char> &bytes, const CCareer &expected)
{
 check(bytes.size() == unsigned(CCareer::SizeOfSaveGame()), "saved length");
 SWORD version; std::memcpy(&version, bytes.data(), sizeof version);
 check(version == 0x1234, "version preserved");
 check(std::memcmp(bytes.data() + sizeof version, &expected, sizeof expected) == 0, "career bytes differ");
 const char *tail = bytes.data() + sizeof version + sizeof expected;
 check(std::memcmp(tail, &CONTROL_BINDINGS[0], sizeof(SControlBinding)) == 0, "first binding");
 tail += sizeof(SControlBinding);
 check(std::memcmp(tail, &CONTROL_BINDINGS[2], sizeof(SControlBinding)) == 0, "second binding");
 tail += sizeof(SControlBinding);
 check(std::memcmp(tail, options, sizeof options) == 0, "PC options trailer");
}

static std::vector<char> directSave(CCareer &career)
{
 std::vector<char> bytes(CCareer::SizeOfSaveGame() + 8, char(0x4d));
 career.Save(bytes.data());
 for (unsigned i = CCareer::SizeOfSaveGame(); i < bytes.size(); ++i) check(bytes[i] == char(0x4d), "write past save");
 bytes.resize(CCareer::SizeOfSaveGame());
 return bytes;
}

int main(int argc, char **argv)
{
 check(argc == 3, "mode and level required");
 const char *mode = argv[1]; int level = std::atoi(argv[2]);
 initial();
 if (std::strcmp(mode, "autoconfig") == 0) CLIPARAMS.mAutoConfigTest = 1;
 if (std::strcmp(mode, "demo") == 0) PLAYABLE_DEMO = 1;
 const bool gates = std::strcmp(mode, "gates") == 0 || std::strcmp(mode, "autoconfig") == 0 || std::strcmp(mode, "demo") == 0;
#ifdef CE_VERSION
 if (gates)
 {
  check(!PortCeMenus(), "replay/record/probe gate must disable CE menus");
  check(!PortCeBonusStart(level), "disabled menu must refuse bonus");
  changeLive(); CCareer live = CAREER;
  verifyBytes(directSave(CAREER), live);
  check(std::memcmp(&CAREER, &live, sizeof live) == 0, "disabled gate changes no live bytes");
  return 0;
 }
 if (std::strcmp(mode, "allocation-failed") == 0)
 {
  CCareer original = CAREER;
  check(!PortCeBonusStart(level), "snapshot allocation failure refuses entry");
  check(std::memcmp(&CAREER, &original, sizeof original) == 0, "failed start leaves career unchanged");
  changeLive(); CCareer live = CAREER;
  verifyBytes(directSave(CAREER), live);
  return 0;
 }
 if (std::strncmp(mode, "jump-", 5) == 0)
 {
  const bool ordinary = std::strcmp(mode, "jump-ordinary") == 0;
  const bool wrongGame = std::strcmp(mode, "jump-wrong-game") == 0;
  const bool wrongSelection = std::strcmp(mode, "jump-wrong-selection") == 0;
  const bool notDeparted = std::strcmp(mode, "jump-not-departed") == 0;
  CCareer entry = CAREER;
  if (!ordinary) { check(PortCeBonusStart(level), "bonus entry before map"); GAME.level = level; }
  else { GAME.level = 100; FRONTEND.mSelectedLevel = 100; }
  changeLive();
  CCareer live = CAREER;
  if (wrongGame) GAME.level = 100; // e.g. backed out of the co-op start, then another game ran
  if (wrongSelection) FRONTEND.mSelectedLevel = 100;
  const CCareer expected = (!ordinary && !wrongGame && !wrongSelection && !notDeparted)
                           ? expectedFiltered(entry, live, false) : live;
  CeConsoleGame::JumpToLevel(110);
  check(GAME.quit == QT_QUIT_TO_FRONTEND, "map asks the old level to quit");
  runActualConsoleLoop(notDeparted ? QT_QUIT_TO_SYSTEM : GAME.quit);
  if (notDeparted)
  {
   check(TARGET_GAME.calls == 0, "a non-frontend exit does not enter the destination");
   check(std::memcmp(&CAREER, &live, sizeof live) == 0, "a non-departure does not restore early");
   return 0;
  }
  check(TARGET_GAME.calls == 1 && TARGET_GAME.level == 110 && FRONTEND.mSelectedLevel == 110,
        "the real loop enters the selected destination once");
  check(std::memcmp(&TARGET_GAME.onEntry, &expected, sizeof expected) == 0,
        "map destination must see entry progress and current settings");
  check(std::memcmp(&CAREER, &expected, sizeof expected) == 0, "map restores before entering the destination");
  check(!PortCeBonusReturning() && !PortCeBonusRaceReturning() && !PortCeBonusCoopReturning() &&
        PortCeBonusTakeReturn() == 0, "map leaves no bonus return for the destination");
  // Progress earned in the destination belongs to the ordinary career, not the discarded bonus snapshot.
  CAREER.progress = 404;
  CCareer destinationCareer = CAREER;
  verifyBytes(directSave(CAREER), destinationCareer);
  check(std::memcmp(&CAREER, &destinationCareer, sizeof destinationCareer) == 0, "destination save remains live");
  return 0;
 }
 const bool inactive = std::strcmp(mode, "regular") == 0 || std::strcmp(mode, "no-snapshot") == 0;
 CCareer entry = CAREER;
 if (!inactive) { check(PortCeBonusStart(level), "bonus start"); GAME.level = level; }
#else
 (void)gates; (void)level;
 const bool inactive = true;
 CCareer entry = CAREER;
#endif
 changeLive();
#ifdef CE_VERSION
 if (std::strcmp(mode, "different-game") == 0) GAME.level = level + 1;
 if (std::strcmp(mode, "different-selected") == 0) FRONTEND.mSelectedLevel = level + 1;
 bool different = std::strcmp(mode, "different-game") == 0 || std::strcmp(mode, "different-selected") == 0;
 bool nameCheat = std::strcmp(mode, "name-cheat") == 0;
 bool suppressGod = !inactive && !different && !nameCheat && std::strcmp(mode, "other-object") != 0;
 cheatSwitch = suppressGod || nameCheat;
 FRONTEND.mFEPSaveGame.cheat = nameCheat;
 readFailed = std::strcmp(mode, "god-read-fail") == 0;
 settingWriteFailed = std::strcmp(mode, "god-write-fail") == 0;
#else
 bool different = false, suppressGod = false;
#endif
 CCareer live = CAREER;
 CCareer expected = (!inactive && !different) ? expectedFiltered(entry, live, suppressGod) : live;
 if (std::strcmp(mode, "other-object") == 0)
 {
  CCareer other = live; other.progress = 123456;
  verifyBytes(directSave(other), other);
  check(std::memcmp(&CAREER, &live, sizeof live) == 0, "other object's save changed live career");
  return 0;
 }
 if (std::strcmp(mode, "create-fail") == 0) createFailure = true;
 if (std::strcmp(mode, "write-fail") == 0) writeFailure = true;
 if (std::strcmp(mode, "options-fail") == 0) optionsFailure = true;
 if (std::strcmp(mode, "no-name") == 0) FRONTEND.mFEPSaveGame.mSaveGameName[0] = 0;
 CPauseMenu pause{};
 pause.mOptionsPage = std::strcmp(mode, "options-open") == 0;
 pause.Resume(nullptr);
 check(std::memcmp(&CAREER, &live, sizeof live) == 0, "save changed live career or failed to restore god flags");
 check(deactivations == 1, "pause deactivated");
 if (pause.mOptionsPage)
 {
  check(creates == 0 && writes == 0 && defaultWrites == 0 && GAME.unpauses == 0, "options page must not save");
  return 0;
 }
 check(GAME.unpauses == 1 && uncontrols == 1 && mouseInitialisations == 1, "normal resume calls");
 check(defaultWrites == 1, "default options written once");
 if (!optionsFailure) verifyBytes(defaultBytes, expected);
 if (!FRONTEND.mFEPSaveGame.mSaveGameName[0]) check(creates == 0 && writes == 0, "no career name");
 else if (createFailure) check(creates == 1 && writes == 0, "create failure stops write");
 else if (writeFailure) check(creates == 1 && writes == 1 && careerBytes.empty(), "write failure reported");
 else { check(creates == 1 && writes == 1, "career written once"); verifyBytes(careerBytes, expected); }
#ifdef CE_VERSION
 check(notices == ((createFailure || writeFailure || optionsFailure) ? 1 : 0), "save failure notice");
 if (suppressGod)
 {
  check(settingReads == 1, "god policy called once");
  check(settingWrites == (readFailed ? 0 : 1), "god setting write follows read result");
  if (!readFailed && !settingWriteFailed) check(savedMask == 3, "god setting keeps current live flags");
  if (settingWriteFailed) check(savedMask == 0, "failed god setting write did not change its value");
 }
 if (nameCheat) check(settingReads == 0 && settingWrites == 0, "name cheat does not change CE setting");
 if (std::strcmp(mode, "repeat") == 0)
 {
  check(PortCeBonusLevelEnd(), "first bonus ends");
  CAREER.progress = 202; CAREER.unknownBefore[10] = 0x44;
  CCareer secondEntry = CAREER;
  check(PortCeBonusStart(903), "second bonus starts"); GAME.level = 903;
  changeLive(); CCareer secondLive = CAREER;
  verifyBytes(directSave(CAREER), expectedFiltered(secondEntry, secondLive, true));
  check(std::memcmp(&CAREER, &secondLive, sizeof secondLive) == 0, "second save changes no live bytes");
 }
#else
 check(notices == 0, "private build has no CE notice");
#endif
 return 0;
}
