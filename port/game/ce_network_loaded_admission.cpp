// SPDX-License-Identifier: GPL-3.0-only
// Read actual loaded native ownership. No caller preset/cheat claims and no gameplay repair.
#include "ce_network_loaded_admission.h"
#include "common.h"
#include "game.h"
#include "world.h"
#include "player.h"
#include "PCController.h"
#include "Start.h"
#include "BattleEngine.h"
#include "BattleEngineConfigurations.h"
#include "BattleEngineDataManager.h"
#include "Career.h"
#include "frontend.h"
#include "CLIParams.h"
#include "../platform/common/typeinfo_bridge.h"
#include <string.h>

extern SINT PLAYABLE_DEMO;
extern SINT performing_stress_test;
extern char gAllCheatsEnabled;
#ifdef CE_VERSION
extern bool gPortCheatNameOnly;
#endif
extern "C" int CE_CheatCachedStatus(unsigned *mask);
extern "C" int CE_NetAutomationIdle(void);
extern "C" int CE_NetWindowAutomationIdle(void);
extern "C" int CE_NetUpdateAutomationIdle(void);
extern "C" int CE_NetClockOrdinary(void);

namespace {
char loaded_error[192];
bool Refuse(const char *reason) {
    // Every reason is a fixed local string; never put a content path or career name in this API.
    const size_t size = strlen(reason);
    if (size >= sizeof loaded_error) return false;
    memcpy(loaded_error, reason, size + 1);
    return false;
}
bool Name(const char *name) {
    if (!name || !name[0]) return false;
    // CBattleEngineData::Load uses a native 256-byte name buffer. This is a conservative
    // engineering admission bound, not an assertion that the original loader bounds its reads.
    for (unsigned i = 0; i < 256; ++i) if (!name[i]) return true;
    return false;
}
}

class CENetworkLoadedAdmission {
    static bool Flags() {
#if defined(EDITORBUILD) || defined(EDITORBUILD2) || defined(RESBUILDER) || defined(E3BUILD)
        return Refuse("this compiled game mode is not admitted for networking");
#endif
        if (CLIPARAMS.mDeveloperMode || CLIPARAMS.mDevKit || CLIPARAMS.mArtistTest ||
            CLIPARAMS.mBuildResources || CLIPARAMS.mBuildPCResources || CLIPARAMS.mBuildPS2Resources ||
            CLIPARAMS.mBuildXBOXResources || CLIPARAMS.mNoBaseResources || CLIPARAMS.mBuildModelInfo ||
            CLIPARAMS.mBuildGoodies || CLIPARAMS.mResBuilderMode || CLIPARAMS.mAutoConfigTest ||
            CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo || CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode ||
            CLIPARAMS.mDecimateMeshes || CLIPARAMS.mNoMeshPartReduction || CLIPARAMS.mFindGoodWater || CLIPARAMS.mFindBadWater ||
            PLAYABLE_DEMO || performing_stress_test || E3_BUILD || gAllCheatsEnabled)
            return Refuse("developer, build, demo, stress or cheat mode is active");
        if (GAME.mFrameLength != 0.05f || GAME.mAdvanceFrame || GAME.mFixedFrameRate ||
            GAME.mAllowedAutoAim != TRUE || GAME.mFullscreenMultiplayer)
            return Refuse("native frame, stepping or gameplay settings are unsupported");
        const int clock = CE_NetClockOrdinary();
        if (clock < 0) return Refuse("native clock configuration is not initialized");
        if (clock != 1) return Refuse("networking requires the ordinary native clock");
        const int automation = CE_NetAutomationIdle();
        const int window = CE_NetWindowAutomationIdle();
        const int update = CE_NetUpdateAutomationIdle();
        if (automation < 0 || window < 0 || update < 0)
            return Refuse("native automation configuration is not initialized");
        if (automation != 1 || window != 1 || update != 1)
            return Refuse("native input, pacing or data automation is configured or was used");
        unsigned cheats = 0;
        if (CE_CheatCachedStatus(&cheats) != 1)
            return Refuse("native cheat configuration is not initialized");
        if (cheats) return Refuse("a CE cheat switch is active");
#ifdef CE_VERSION
        if (gPortCheatNameOnly) return Refuse("native cheat query is in a temporary save operation");
#endif
        bool terminated = false;
        for (unsigned i = 0; i < sizeof FRONTEND.mFEPSaveGame.mSaveGameName / sizeof FRONTEND.mFEPSaveGame.mSaveGameName[0]; ++i)
            if (!FRONTEND.mFEPSaveGame.mSaveGameName[i]) { terminated = true; break; }
        if (!terminated) return Refuse("native career name is not terminated");
        for (int cheat = 0; cheat < 6; ++cheat)
            if (FRONTEND.mFEPSaveGame.IsCheatActive(cheat)) return Refuse("a native career cheat is active");
        return true;
    }
    static bool Registry(CBattleEngineData *&selected) {
        const int count = UBattleEngineConfigurations::CountConfigurations();
        if (count < 1 || count > kMaxConfigurations)
            return Refuse("loaded level configuration count is unsupported");
        const char *raw = UBattleEngineConfigurations::NetworkConfigurationName(0);
        if (!Name(raw)) return Refuse("loaded raw level configuration zero has no valid name");
        // The native manager has no retail-defined maximum. Bound this engineering scan explicitly.
        const unsigned maximum = 65536;
        const int expected = UBattleEngineDataManager::sData.Size();
        if (expected < 1 || (unsigned)expected > maximum)
            return Refuse("loaded configuration registry exceeds the engineering bound");
        unsigned seen = 0, matches = 0;
        selected = NULL;
        ListIterator<CBattleEngineData> entries(&UBattleEngineDataManager::sData);
        for (CBattleEngineData *data = entries.First(); data; data = entries.Next()) {
            if (++seen > (unsigned)expected || !Name(data->mConfigurationName))
                return Refuse("loaded configuration registry is malformed");
            if (!strcmp(raw, data->mConfigurationName)) { selected = data; ++matches; }
        }
        if (seen != (unsigned)expected || matches != 1)
            return Refuse("raw level configuration name does not resolve uniquely");
        return true;
    }
public:
    static bool Validate() {
        loaded_error[0] = 0;
        if (GAME.GetCurrentLevel() != 852 || !GAME.IsMultiplayer() || !WORLD.IsMultiplayer() || GAME.GetNPlayers() != 2 ||
            GAME.GetGameState() < GAME_STATE_PRE_RUNNING || GAME.GetGameState() > GAME_STATE_PLAYING || GAME.mQuit != QT_NONE)
            return Refuse("loaded admission requires native multiplayer level 852 and two players before START");
        const SFrontEndSettings *settings = GAME.GetFrontEndSettings();
        if (settings->mInvertSides || settings->mPlayer1Configuration != 0 || settings->mPlayer2Configuration != 0)
            return Refuse("engineering networking requires actual configuration zero and uninverted sides");
        if (!Flags()) return false;
        CBattleEngineData *data = NULL;
        if (!Registry(data)) return false;
        CBattleEngine *engines[2] = {NULL, NULL};
        for (unsigned i = 0; i < 2; ++i) {
            CPlayer *player = GAME.GetPlayer(i);
            CController *controller = GAME.mController[i];
            CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
            if (!player || !engine || player->GetNumber() != (int)i + 1 || engine->GetPlayer() != player ||
                !PortTypeInfoEqual(typeid(*engine), typeid(CBattleEngine)) || !controller ||
                !PortTypeInfoEqual(typeid(*controller), typeid(CPCController)) || controller->GetPadNumber() != (int)i ||
                controller->GetToControl() != player || controller->mPlaying || controller->mRecording || GAME.IsFreeCameraOn(i))
                return Refuse("loaded player, controller and engine ownership is unsupported");
            if (CAREER.GetIsGod(i) || player->IsGod() || engine->GetVulnerable() != TRUE || engine->mInfinateEnergy)
                return Refuse("loaded player god or invulnerability state is unsupported");
            if (engine->mConfigurationId != 0 || engine->mConfiguration != data ||
                !Name(engine->mConfiguration->mConfigurationName) || strcmp(engine->mConfiguration->mConfigurationName,
                UBattleEngineConfigurations::NetworkConfigurationName(0)))
                return Refuse("loaded engine configuration does not match the raw authored binding");
            engines[i] = engine;
        }
        if (engines[0] == engines[1]) return Refuse("two distinct native engines are required");
        unsigned starts[2] = {0, 0};
        unsigned start_count = 0;
        ListIterator<CStart> start_iterator(&WORLD.GetStartNB());
        for (CStart *start = start_iterator.First(); start; start = start_iterator.Next()) {
            if (++start_count > 2 || !PortTypeInfoEqual(typeid(*start), typeid(CStart)) ||
                start->GetPlayerNumber() < 1 || start->GetPlayerNumber() > 2)
                return Refuse("loaded native start ownership is unsupported");
            const unsigned player = start->GetPlayerNumber() - 1;
            if (++starts[player] != 1 || start->mInit.mConfigurationId != 0 || start->GetPlayerObject() != engines[player])
                return Refuse("loaded starts do not match canonical configuration zero engines");
        }
        if (start_count != 2 || starts[0] != 1 || starts[1] != 1)
            return Refuse("two canonical loaded native starts are required");
        unsigned membership[2] = {0, 0}, things = 0;
        ListIterator<CThing> iterator(&WORLD.GetThingNB());
        for (CThing *thing = iterator.First(); thing; thing = iterator.Next()) {
            if (++things > 128) return Refuse("loaded world exceeds the existing adapter entity bound");
            for (unsigned i = 0; i < 2; ++i) if (thing == engines[i]) ++membership[i];
        }
        if (membership[0] != 1 || membership[1] != 1)
            return Refuse("loaded engines are not unique members of the native world");
        return true;
    }
};

extern "C" int CE_NetLoadedAdmissionValidate(void) { return CENetworkLoadedAdmission::Validate() ? 1 : 0; }
extern "C" const char *CE_NetLoadedAdmissionError(void) { return loaded_error; }
