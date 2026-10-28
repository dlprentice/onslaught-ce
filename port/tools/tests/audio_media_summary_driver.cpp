// SPDX-License-Identifier: GPL-3.0-only
// The real session logger, replacement mixer, music worker and Bink buffer lifecycle together. Decoder and SDL
// boundaries generate authored PCM only; this exercises no game, physical device or retail material.
#include <unistd.h>
#define AUDIO_MEDIA_MAIN ExistingMediaMain
#include "audio_media_driver.cpp"

int main(int argc, char **argv)
{
 if (argc != 2) return 1;
 const std::string mode = argv[1];
 diagnosticsEnabled = PortSessionEnabled();
 mediaQueueModel = true;
 if (mode == "queue-fail") mediaQueueError = true;
 device = Make<Sound>();
 gSound = device;
 Video *film = nullptr;
 bool music = false;
 if (mode == "disabled") Check(!diagnosticsEnabled, "faithful/recording fence disables diagnostics");
 else Check(diagnosticsEnabled, "ordinary CE diagnostics enabled");
 if (mode != "empty")
 {
  StartMusic(); music = true;
  film = Film();
  if (mode == "muted") film->sound->SetVolume(DSBVOLUME_MIN);
  FeedFrames(64);
  Expect(0, 64, mode == "muted" ? 1000 : 3000);
 }
 else FeedFrames(64);
 if (mode == "flood")
 {
  for (int i = 0; i < 2100; ++i) PortSessionLog(PORT_SESSION_WINDOW, false, "ordinary fill=%d", i);
  for (int i = 0; i < 600; ++i) PortSessionLog(PORT_SESSION_AUDIO, true, "error fill=%d", i);
 }
 if (mode == "saturate")
 {
  SDL_LockMutex(gMixer.lock);
  gMixer.musicFrames = gMixer.filmFrames = gMixer.bothRoleFrames = ~Uint64(0);
  gMixer.callbackFrames = gMixer.queueSamples = gMixer.queueFailures = ~Uint64(0);
  gMixer.queueAtMixed = gMixer.retiredQueueObservations = ~Uint64(0);
  gMixer.callbackMaxFrames = ~0u;
  gMixer.queueLastBytes = gMixer.queueMaxBytes = INT_MAX;
  SDL_UnlockMutex(gMixer.lock);
  for (unsigned &counter : gCounts) __atomic_store_n(&counter, ~0u, __ATOMIC_RELAXED);
 }
 if (film) Free(film);
 if (music) ShutdownMusicStream();
 Check(mediaCallbackLogCalls == 0, "audio callback formats and writes no session messages");
 device->Release(); // actual Mixer::Stop -> MediaSnapshot -> PortSessionSummary
 PortSessionSummary(); // no repeated final snapshot on a later summary
 std::printf("PASS %s: %u checks\n", mode.c_str(), checks);
 return 0;
}
