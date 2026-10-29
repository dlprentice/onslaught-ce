// SPDX-License-Identifier: GPL-3.0-only
// The retail music state machine (src/Music.cpp) and music device (src/PCMusic.cpp) after the port patches, the actual
// CE playlist (port/game/ce_music.cpp), its folder listing (port/platform/common/ce_music_scan.cpp) and ce.ini reader
// (port/platform/common/ce.cpp), linked against a recording music stream. One case per process: ce_music_tests.py.
#include "common.h"
#include "music.h"
#include "ce_music.h"
#include "../platform/common/portlog.h"
#include <map>

FakeConsole CONSOLE; FakeCareer CAREER; FakeLog LOG; FakeCli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
char gAllCheatsEnabled = 0;
static int draws = 0;
#undef rand
int CountedRand() { ++draws; return std::rand(); }
#define rand CountedRand
static bool recording = false;
bool PortRecording() { return recording; }
extern "C" bool SDL_OpenURL(const char *) { return false; }	// ce.cpp's report page, never called here
extern "C" unsigned long long SDL_GetTicks(void) { return 0; }	// ce.cpp's settings retry clock, never read here
bool PortSessionEnabled() { return false; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}

// ---- the music stream, recorded --------------------------------------------------------------------------------------
static std::vector<std::string> streamed;
static bool finished = false;
static int stops = 0;
void InitMusicStream() {}
void ShutdownMusicStream() {}
void PlayMusicStream(char *filename) { streamed.push_back(filename); finished = false; }
void StopMusicStream() { ++stops; }
void SetMusicStreamVolume(float) {}
void PumpMusicStream() {}
bool GetMusicStreamFinished() { return finished; }

// ---- the game's data\music listing ------------------------------------------------------------------------------------
static int findAt = 0;
static const char *const kGameTracks[] = {"BEA_00(Master).ogg", "BEA_01(Master).ogg", "BEA_02(Master).ogg", "BEA_03(Master).ogg",
										  "BEA_04(Master).ogg", "BEA_05(Master).ogg", "BEA_06(Master).ogg", "BEA_07(Master).ogg",
										  "BEA_08(Master).ogg", "BEA_09(Master).ogg"};
HANDLE FindFirstFile(const char *spec, WIN32_FIND_DATA *data) {
	if (std::strcmp(spec, "data\\music\\*.ogg")) return INVALID_HANDLE_VALUE;
	findAt = 0;
	std::snprintf(data->cFileName, MAX_PATH, "%s", kGameTracks[findAt++]);
	return (HANDLE)1;
}
BOOL FindNextFile(HANDLE, WIN32_FIND_DATA *data) {
	if (findAt >= 10) return FALSE;
	std::snprintf(data->cFileName, MAX_PATH, "%s", kGameTracks[findAt++]);
	return TRUE;
}
BOOL FindClose(HANDLE) { return TRUE; }
void con_m_playtype(char *cmd);		// Music.cpp: compiled in retail, its registration is #if 0
#ifdef FAKE_NETWORK
extern "C" int CE_BootRequested(void) { return std::getenv("TEST_NET_BOOT") != nullptr; }
#endif

static void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s (streamed %zu, last %s, draws %d, play type %d)\n", what, streamed.size(),
					 streamed.empty() ? "-" : streamed.back().c_str(), draws, (int)MUSIC.mPlayType);
		std::exit(2);
	}
}
static bool Game(const std::string &s) { return s.rfind("data\\music\\BEA_", 0) == 0; }
static std::string Leaf(const std::string &s) { size_t k = s.find_last_of("/\\"); return k == std::string::npos ? s : s.substr(k + 1); }
static void End() { finished = true; MUSIC.UpdateStatus(); }

int main(int argc, char **argv) {
	if (argc != 2) return 3;
	std::string mode = argv[1];
	const char *dir = std::getenv("TEST_MUSIC_DIR");
	(void)dir;
	recording = std::getenv("TEST_RECORDING") != nullptr;
	CLIPARAMS.mAttractMode = std::getenv("TEST_ATTRACT") != nullptr;
	PLAYABLE_DEMO = std::getenv("TEST_DEMO") != nullptr;
	MUSIC.Initialise();
	check(MUSIC.mPlayType == MPT_LINEAR && MUSIC.mFirstSong, "retail Initialise: linear, the game's tracks listed");

	if (mode == "measure") {
		// The report's open question: with the retail m_playtype command set to random, does a level's cue keep it?
		char cmd[] = "m_playtype random";
		con_m_playtype(cmd);
		check(MUSIC.mPlayType == MPT_RANDOM, "the retail command sets random");
		MUSIC.PlaySelection(MUS_INGAME);
		std::printf("MEASURE non-fade level cue: play type %d after (random is %d, selection %d), streamed %s, %d rand draw\n",
					(int)MUSIC.mPlayType, (int)MPT_RANDOM, (int)MPT_SELECTION, Leaf(streamed.back()).c_str(), draws);
		check(MUSIC.mPlayType == MPT_SELECTION && Game(streamed.back()) && draws == 1, "the cue replaces random with selection");
		// The fading cue (fade TRUE while playing) keeps the play type but still queues one of the game's tracks.
		con_m_playtype(cmd);
		int before = (int)streamed.size();
		MUSIC.PlaySelection(MUS_INGAME, TRUE);
		for (int i = 0; i < 100 && (int)streamed.size() == before; ++i) MUSIC.UpdateStatus();
		std::printf("MEASURE fading level cue: play type %d after, streamed %s\n", (int)MUSIC.mPlayType, Leaf(streamed.back()).c_str());
		check(MUSIC.mPlayType == MPT_RANDOM && Game(streamed.back()), "the fading cue plays a game track from the shared list");
		End();
		std::printf("MEASURE track end under random: streamed %s (the playlist is the game's own tracks)\n", Leaf(streamed.back()).c_str());
		check(Game(streamed.back()), "random picks from the game's own tracks");
		std::puts("measured");
		return 0;
	}

	MUSIC.PlaySelection(MUS_FRONTEND);
	check(streamed.size() == 1 && streamed.back() == (PLAYABLE_DEMO ? "data\\music\\BEA_01(Master).ogg" : "data\\music\\BEA_08(Master).ogg"),
		  "the front end plays the game's track (8, or the demo's 1)");
	const int frontDraws = draws;
	MUSIC.PlaySelection(MUS_INGAME);
	const std::string cue = streamed.back();
	const int perCue = PLAYABLE_DEMO ? 0 : 1;		// the demo's level cue is track 0, with no draw
	check(draws == frontDraws + perCue, "a level cue makes the game's one rand() draw, as in the faithful game");
	check(MUSIC.mPlayType == MPT_SELECTION, "the game's music state is the faithful one");

	if (mode == "faithful" || mode == "fenced" || mode == "empty" || mode == "relative") {
		check(Game(cue), "the level plays the game's track");
		End();
		check(Game(streamed.back()) && draws == frontDraws + 2 * perCue, "the game's track end is the game's own (another draw)");
#ifdef CE_VERSION
		check(!CeMusic::Allowed() || mode != "fenced", "a fenced run is not allowed");
#endif
		std::puts(mode == "faithful" ? "faithful music unchanged" : "game music kept");
		return 0;
	}

#ifndef CE_VERSION
	return 3;
}
#else
	check(dir && cue.rfind(std::string(dir) + "/", 0) == 0 && (mode == "long" || mode == "random" || Leaf(cue) == "a.ogg"),
		  "a level cue streams the player's first track");
	const std::string game = MUSIC.mCurrentSong ? MUSIC.mCurrentSong->mFilename : "";
	check(Game(game), "the game's own current song is untouched");
	if (mode == "linear") {
		check(CeMusic::Tracks() == 3, "three playable tracks of the folder's ten names");
		End(); check(Leaf(streamed.back()) == "B.ogg", "linear: name order ignoring case");
		End(); check(Leaf(streamed.back()) == "c.OGG", "linear: the third");
		End(); check(Leaf(streamed.back()) == "a.ogg", "linear: round again");
		check(draws == frontDraws + 1, "the player's track ends never draw the game's rand()");
		check(MUSIC.mCurrentSong && game == MUSIC.mCurrentSong->mFilename && MUSIC.mPlayType == MPT_SELECTION, "the game's state untouched");
		MUSIC.PlaySelection(MUS_INGAME);
		check(Leaf(streamed.back()) == "B.ogg" && draws == frontDraws + 2, "the next level cue steps on, with the game's own draw");
		MUSIC.PlaySelection(MUS_FRONTEND);
		check(streamed.back() == "data\\music\\BEA_08(Master).ogg", "the front end's music is the game's");
		End(); check(Game(streamed.back()), "a game track's end is the game's");
		MUSIC.PlaySelection(MUS_TUTORIAL);
		check(Leaf(streamed.back()) == "c.OGG", "the tutorial is a level");
		// The console's side.
		check(CeMusic::Next() && Leaf(streamed.back()) == "a.ogg", "music next");
		CeMusic::Clear();
		check(Game(streamed.back()) && streamed.back() == MUSIC.mCurrentSong->mFilename, "clear: the game's current track again");
		const size_t now = streamed.size();
		End();
		check(streamed.size() == now + 1 && Game(streamed.back()), "after clear the game's music logic runs again");
		int skipped = 0, truncated = 0;
		check(CeMusic::AddFolder(dir, &skipped, &truncated) == 3 && skipped == 5 && !truncated, "adddir: three added, five left out");
		check(Leaf(streamed.back()) == "a.ogg", "adddir while a level's music plays switches now");
		check(CeMusic::AddFolder(dir, &skipped, &truncated) == 0 && CeMusic::Tracks() == 3, "the same folder twice adds nothing");
		check(CeMusic::AddFolder("relative/folder", &skipped, &truncated) < 0, "a relative folder is refused");
		std::puts("linear playlist passed");
		return 0;
	}
	if (mode == "random") {
		std::map<std::string, int> seen;
		std::string last = Leaf(cue);
		for (int i = 0; i < 60; ++i) {
			End();
			const std::string next = Leaf(streamed.back());
			check(next != last, "random never repeats the track that just ended");
			++seen[next]; last = next;
		}
		check(seen.size() == 3, "random reaches every track");
		check(draws == frontDraws + 1, "the shuffle is the playlist's own, never the game's rand()");
		std::puts("random playlist passed");
		return 0;
	}
	if (mode == "single") {
		const size_t now = streamed.size();
		const int stopped = stops;
		for (int i = 0; i < 5; ++i) End();
		check(streamed.size() == now && stops == stopped && draws == frontDraws + 1, "single: one track, then silence, no game cue");
		MUSIC.PlaySelection(MUS_INGAME);
		check(Leaf(streamed.back()) == "a.ogg", "single: the same track at the next cue");
		check(CeMusic::Next() && Leaf(streamed.back()) == "B.ogg" && CeMusic::Type() == CE_MUSIC_SINGLE, "next steps on in single mode");
		std::puts("single playlist passed");
		return 0;
	}
	if (mode == "bounds") {
		check(CeMusic::Tracks() == 256, "a folder of 300 playable tracks gives the first 256");
		check(Leaf(cue) == "a.ogg", "in name order");
		int skipped = 0, truncated = 0;
		check(CeMusic::AddFolder(std::getenv("TEST_MUSIC_DIR2"), &skipped, &truncated) == 0 && CeMusic::Tracks() == 256, "a full list takes no more");
		std::puts("bounds passed");
		return 0;
	}
	if (mode == "long") {
		int skipped = 0, truncated = 0;
		CeMusic::AddFolder(dir, &skipped, &truncated);
		check(CeMusic::Tracks() == 1 && cue.size() == 255 && skipped == 1, "a 255-byte path plays; a 256-byte one is left out");
		std::puts("path bound passed");
		return 0;
	}
	return 3;
}
#endif
