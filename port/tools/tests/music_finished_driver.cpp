// SPDX-License-Identifier: GPL-3.0-only
// Reuse the existing production-worker boundaries; this entry point runs the actual EOF/start handler and PC poll.
#define main ExistingMusicStopMain
#include "music_stream_stop_driver.cpp"
#undef main
#include "../../platform/common/portlog.h"
bool PortDevLog() { return false; }
bool PortSessionEnabled() { return false; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
void PortSessionAsset(PortSessionCategory, const char *, const char *, long) {}
void PortSessionMediaActive(PortSessionCategory, const void *, bool) {}
bool PortAudioMediaRole(const void *, PortSessionCategory) { return false; }
void PortSessionCount(PortSessionCounter) {}
void PortSessionAudioError(const char *) {}
void PortSessionSummary() {}
void PortSessionPulse(unsigned) {}
// This fixture tests the worker/poll, with a controlled buffer rather than the production mixer.
void PortAudioPublishMusicHalf(void *destination, const void *source, DWORD bytes) { memcpy(destination, source, bytes); }
bool PortCeMusicTrackEnded() { return false; }	// patch 0103's CE playlist: no player's track plays in this harness
class CPCMusic { public: BOOL DeviceGetTrackFinished(); };
#include POLL_SOURCE

int main(int argc, char **argv)
{
	if (argc == 3) return ExistingMusicStopMain(argc, argv);
	if (argc != 2) return 1;
	ce_layer = !strcmp(argv[1], "ce"); virtual_clock = false;
	if (!ce_layer) setenv("BEA_PORT_TAPE", "faithful-boundary", 1);
	DSBUFFERDESC desc{}; DirectSound sound;
	sound.CreateSoundBuffer(&desc, &sBuffer, nullptr);
	sPlayEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	sFirstHalfEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	sSecondHalfEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	sQuitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	sReader = new COggFileRead;
#ifdef CE_VERSION
	sCeStreamAdmission = PortCeLayer();
	if (sCeStreamAdmission) { InitializeCriticalSection(&sCeStreamLock); sCeStreamLockReady = true; }
#endif
	char track[] = "authored.ogg";
	PlayMusicStream(track); MusicStreamHandle(0);
	CPCMusic poll;
	Check(!poll.DeviceGetTrackFinished(), "production poll sees initial active track");
	std::atomic<bool> start{false};
	std::thread worker([&] {
		while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
		for (unsigned i = 0; i < 128; ++i)
		{
			sSilentHalves = 3; MusicStreamHandle(1);
			PlayMusicStream(track); MusicStreamHandle(0);
		}
		sSilentHalves = 3; MusicStreamHandle(1);
	});
	start.store(true, std::memory_order_release);
	unsigned finished = 0;
	for (unsigned i = 0; i < 200000; ++i) finished += poll.DeviceGetTrackFinished() != 0;
	worker.join();
	Check(poll.DeviceGetTrackFinished(), "production poll sees final EOF");
	// The worker was joined before private-state cleanup; no real thread or audio device was created.
	delete sReader; sReader = nullptr;
#ifdef CE_VERSION
	if (sCeStreamLockReady) { sCeStreamLockReady = false; DeleteCriticalSection(&sCeStreamLock); }
#endif
	std::printf("{\"polls\":200000,\"finished_observations\":%u,\"checks\":%u}\n", finished, checks);
	return 0;
}
