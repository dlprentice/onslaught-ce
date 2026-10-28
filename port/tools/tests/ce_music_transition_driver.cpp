// SPDX-License-Identifier: GPL-3.0-only
// The actual CE playlist and patched music stream, coupled to the actual replacement mixer. Ogg/SDL/events are
// controlled boundaries producing authored, distinguishable PCM. This is not a game or device-output test.
#define main ExistingRefillMain
#include "audio_refill_driver.cpp"
#undef main
#include "ce_music.h"

enum { MUS_TUTORIAL = 1, MUS_STEALTH = 2, MUS_INGAME = 3 };
struct SongBoundary { char mFilename[256]; };
struct MusicBoundary { bool mPlaying = true; SongBoundary *mCurrentSong = nullptr; } MUSIC;
struct CliBoundary { bool mAutoConfigTest = false, mRecordDemo = false, mPlaybackDemo = false;
 bool mAttractMode = false; int mStressTest = 0; } CLIPARAMS;
static bool PLAYABLE_DEMO = false;
bool PortRecording() { return false; }
extern "C" int CE_SettingGetText(const char *, char *, int) { return 0; }
extern "C" int CE_SettingIs(const char *, const char *) { return 0; }
extern "C" unsigned CE_MusicSeed() { return 1; }
extern "C" int CE_MusicScan(const char *dir, int (*add)(const char *, void *), void *ctx, int *skipped, int *truncated)
{
 if (std::strcmp(dir, "/authored")) return -1;
 *skipped = *truncated = 0;
 add("/authored/a.ogg", ctx); add("/authored/b.ogg", ctx);
 return 0;
}
#include "playlist.inc"

static void Track(short marker, const char *message)
{
 auto pcm = MixFrames(16);
 short expected = (short)lrintf((float)marker * (32767.f / 32768.f));
 for (short sample : pcm) Check(sample == expected, message);
 Check(gMixer.numVoices == 1, "track change keeps one actual mixer voice");
}

int main(int argc, char **argv)
{
 if (argc != 2) return 1;
 namedMusicReads = true;
 diagnosticsEnabled = PortSessionEnabled();
 Check(diagnosticsEnabled, "ordinary CE path enabled");
 device = Make<Sound>();
 InitMusicStream();
 SongBoundary original{};
 std::strcpy(original.mFilename, "game.ogg");
 MUSIC.mCurrentSong = &original;
 int skipped = -1, truncated = -1;
 const bool addAfterStart = !std::strcmp(argv[1], "add");
 if (!addAfterStart) Check(CeMusic::AddFolder("/authored", &skipped, &truncated) == 2, "preload authored playlist");
 char *first = PortCeMusicPlay(original.mFilename, MUS_INGAME);
 PlayMusicStream(first);
 Check(Dispatch() == 1, "initial actual worker event");
 Track(addAfterStart ? 1000 : 2000, "initial actual PCM comes from requested track");
 if (addAfterStart)
 {
  Check(CeMusic::AddFolder("/authored", &skipped, &truncated) == 2, "add tracks while game music is active");
  Check(Dispatch() == 1, "added playlist actual worker event");
  Track(2000, "adddir immediately starts newly selected PCM");
 }
 else if (!std::strcmp(argv[1], "next"))
 {
  Check(CeMusic::Next(), "next is admitted");
  Check(Dispatch() == 1, "next actual worker event");
  Track(3000, "next immediately starts newly selected PCM");
 }
 else if (!std::strcmp(argv[1], "clear"))
 {
  CeMusic::Clear();
  Check(Dispatch() == 1, "clear actual worker event");
  Track(1000, "clear immediately restores game PCM");
 }
 else if (!std::strcmp(argv[1], "end"))
 {
  // The real stream stops before publishing its finished flag; exercise playlist auto-advance after that stop.
  StopMusicStream();
  MUSIC_STREAM_FINISHED = true;
  Check(PortCeMusicTrackEnded(), "playlist handles a naturally finished track");
  Check(Dispatch() == 1, "finished track actual worker event");
  Track(3000, "finished track advances to the next authored PCM");
 }
 else if (!std::strcmp(argv[1], "clear-idle"))
 {
  StopMusicStream();
  MUSIC.mPlaying = false;
  CeMusic::Clear();
  Check(Dispatch() == 0 && gMixer.numVoices == 0, "clearing an idle playlist does not start music");
 }
 else if (!std::strcmp(argv[1], "burst"))
 {
  Check(CeMusic::Next() && CeMusic::Next(), "two pending skips are admitted");
  Check(Dispatch() == 1, "successive skips leave only the latest request");
  Track(2000, "successive skips start the latest requested PCM");
 }
 else return 1;
 ShutdownMusicStream();
 device->Release();
 std::printf("PASS %s: %u checks, controlled mixer output\n", argv[1], checks);
 return 0;
}
