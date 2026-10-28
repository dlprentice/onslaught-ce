// SPDX-License-Identifier: GPL-3.0-only
// The CE music playlist: the retail playlist commands (m_adddir, m_playtype single|linear|random; registered only in
// Stuart's build, src/Music.cpp:120) brought back for the player's own folder of .ogg tracks, which plays in place of
// the level music. ce.ini's music_dir and music_playtype set it at start; the console's music command changes it for
// the session.
//
// Why beside, not inside, the retail list: retail's one playlist holds the game's own tracks (CPCMusic::DeviceInitialise
// adds data\music), and a level's cue picks from it by number with the game's rand() (CMusic::PlaySelection) and sets
// the play type back to MPT_SELECTION whatever it was (measured: mPlayType 1 before the level-200 cue, 3 after). So the
// retail cue still runs exactly as it does (its rand() draw included, so the battle's random numbers are the faithful
// game's), and patch 0103 changes only the file the music device streams for a level's cue. The player's track ends
// are handled here with the playlist's own shuffle, never the game's rand().
//
// Fenced like the console: nothing here runs in a replay, a recording, under probes or the virtual clocks, for a test
// pilot or a network game, or where the CE layer is off; it never writes a file.
#ifdef CE_VERSION

#include "common.h"
#include "music.h"
#include "CLIParams.h"
#include "console.h"
#include "ce_music.h"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The CE music playlist requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_MusicScan(const char *dir, int (*add)(const char *path, void *ctx), void *ctx, int *skipped,
							int *truncated);
extern "C" unsigned CE_MusicSeed(void);
bool PortCeLayer();
bool PortRecording();
void PlayMusicStream(char *filename);		// PCMusicStream.cpp

namespace {
const int kTracks = 256;					// the playlist's limit; a bigger folder adds its first 256 by name
const int kPath = 256;						// a track's path: the music stream's sTrackName (255 bytes and a NUL)
char tracks[kTracks][kPath];
int count;
int type = CE_MUSIC_LINEAR;					// retail's default (CMusic::Initialise: mPlayType = MPT_LINEAR)
int at = -1;								// the track last chosen
bool loaded;								// ce.ini read (once, at the first level cue or console command)
bool levelCue;								// the game's last cue was a level's (MUS_TUTORIAL, MUS_STEALTH, MUS_INGAME)
bool custom;								// the stream is playing one of the player's tracks
unsigned shuffle;							// the playlist's own random state; 0 until seeded
char folder[kPath];

// Replays, recordings, probes, the virtual clocks, test pilots and network games, as the console (ce_palette.cpp).
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

int Fold(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : (unsigned char)c; }

int Compare(const char *a, const char *b)	// retail's stricmp order (CMusic::AddToPlayList)
{
	for (;; ++a, ++b)
	{
		int x = Fold(*a), y = Fold(*b);
		if (x != y || !x) return x - y;
	}
}

// Adds path in name order, ignoring a name already listed, as CMusic::AddToPlayList does; 0 when the list is full.
int Add(const char *path, void *)
{
	if (count >= kTracks) return 0;
	if (strlen(path) >= size_t(kPath)) return 1;
	int i = 0;
	while (i < count && Compare(tracks[i], path) < 0) ++i;
	if (i < count && !Compare(tracks[i], path)) return 1;
	memmove(tracks[i + 1], tracks[i], size_t(count - i) * kPath);
	snprintf(tracks[i], kPath, "%s", path);
	if (at >= i) ++at;
	++count;
	return 1;
}

unsigned Draw()
{
	if (!shuffle) shuffle = CE_MusicSeed();
	shuffle ^= shuffle << 13;
	shuffle ^= shuffle >> 17;
	shuffle ^= shuffle << 5;
	return shuffle;
}

// The track for a level's cue or a track's end: random picks another track when there is one; single and linear step on.
void Step(bool cue)
{
	if (count <= 0) { at = -1; return; }
	if (type == CE_MUSIC_RANDOM)
	{
		int pick = int(Draw() % unsigned(count));
		if (count > 1 && pick == at) pick = (pick + 1 + int(Draw() % unsigned(count - 1))) % count;
		at = pick;
	}
	else if (type == CE_MUSIC_SINGLE && cue && at >= 0 && at < count) {}	// single: the same track at each cue
	else at = (at + 1) % count;
}

void Play()
{
	custom = true;
	PlayMusicStream(tracks[at]);
}

// Adds dir's tracks (a leading ~/ is the home folder on Linux); the tracks added, or -1 when dir cannot be listed.
int Scan(const char *dir, int *skipped, int *truncated)
{
	char path[kPath];
#ifndef _WIN32
	const char *home = getenv("HOME");
	if (dir[0] == '~' && dir[1] == '/' && home && *home)
	{
		if (snprintf(path, sizeof(path), "%s%s", home, dir + 1) >= int(sizeof(path))) return -1;
		dir = path;
	}
#endif
	const int before = count;
	if (CE_MusicScan(dir, Add, NULL, skipped, truncated) < 0) return -1;
	snprintf(folder, sizeof(folder), "%s", dir);
	return count - before;
}

void Load()
{
	if (loaded) return;
	loaded = true;
	if (CE_SettingIs("music_playtype", "single")) type = CE_MUSIC_SINGLE;
	else if (CE_SettingIs("music_playtype", "random")) type = CE_MUSIC_RANDOM;
	char dir[kPath];
	if (!CE_SettingGetText("music_dir", dir, sizeof(dir))) return;
	int skipped = 0, truncated = 0;
	int added = Scan(dir, &skipped, &truncated);
	if (added < 0)
		fprintf(stderr, PORT_LOG_PREFIX "music: ce.ini's music_dir is not a folder that can be read; the game's music plays\n");
	else
		fprintf(stderr, PORT_LOG_PREFIX "music: %d track%s from ce.ini's music_dir (%d left out)%s\n", added,
				added == 1 ? "" : "s", skipped, truncated ? "; only the first 4096 names were read" : "");
}
}

bool CeMusic::Allowed()
{
	if (!PortCeLayer() || PortRecording()) return false;
	for (const char *name : kFenced) if (Present(name)) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	return !(PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
			 CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode);
}

int CeMusic::AddFolder(const char *dir, int *skipped, int *truncated)
{
	if (!Allowed()) return -1;
	Load();
	const int before = count;
	if (Scan(dir, skipped, truncated) < 0) return -1;
	// A level's music playing from the game's list switches to the player's now.
	if (!custom && before == 0 && count > 0 && levelCue && MUSIC.mPlaying)
	{
		Step(true);
		Play();
	}
	return count - before;
}

int CeMusic::Tracks() { return count; }
int CeMusic::Type() { return type; }
void CeMusic::SetType(int t)
{
	if (t >= CE_MUSIC_SINGLE && t <= CE_MUSIC_RANDOM) type = t;
}

bool CeMusic::Next()
{
	if (!Allowed() || !count || !levelCue || !MUSIC.mPlaying) return false;
	const int was = type;
	if (type == CE_MUSIC_SINGLE) type = CE_MUSIC_LINEAR;	// single steps to the next track too, and stays single
	Step(false);
	type = was;
	Play();
	return true;
}

void CeMusic::Clear()
{
	loaded = true;
	count = 0;
	at = -1;
	folder[0] = 0;
	if (custom && MUSIC.mPlaying && MUSIC.mCurrentSong) PlayMusicStream(MUSIC.mCurrentSong->mFilename);
	custom = false;
}

const char *CeMusic::Now() { return custom && at >= 0 && at < count ? tracks[at] : NULL; }
const char *CeMusic::Folder() { return folder; }

char *PortCeMusicPlay(char *gameFile, int selection)
{
	levelCue = selection == MUS_TUTORIAL || selection == MUS_STEALTH || selection == MUS_INGAME;
	if (!levelCue || !CeMusic::Allowed())
	{
		custom = false;
		return gameFile;
	}
	Load();
	if (!count)
	{
		custom = false;
		return gameFile;
	}
	Step(true);
	custom = true;
	return tracks[at];
}

bool PortCeMusicTrackEnded()
{
	if (!custom) return false;
	if (!CeMusic::Allowed() || !count)
	{
		custom = false;
		return false;
	}
	if (type == CE_MUSIC_SINGLE) return true;	// retail's single: the track plays once, then silence
	Step(false);
	Play();
	return true;
}

#endif
