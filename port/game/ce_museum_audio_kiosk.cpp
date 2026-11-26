// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_audio_kiosk.cpp: Runtime OGG speech playback for Grand Museum Audio
// Kiosks using COggFileRead + CSoundManager::CreateSampleFromData + CeMuseumMusicDuck.
// Safe under dummy audio, pause-safe, mutually exclusive with Screening Room cinema, and
// releases all decoded buffers immediately on wing exit or visit cancellation.
#ifdef CE_VERSION
#include "common.h"
#include "SoundManager.h"
#include "PCSoundManager.h"
#include "AudibleThing.h"
#include "OggFileRead.h"
#include "text.h"
#include "ce_museum_audio_kiosk.h"
#include "ce_museum_cinema.h"
#include "ce_cmd_voice.h"
#include "ce_catalogue_sounds.h"
#include "../platform/common/portlog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace CeMuseumAudioKiosk {
namespace {

const char kSampleName[] = "ce museum audio kiosk";

class KioskAudioOwner : public IAudibleThing {
public:
    FVector GetSoundPos() { return FVector(0.f, 0.f, 0.f); }
    FMatrix GetSoundOrientation() {
        return FMatrix(FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
    }
    FVector GetSoundVelocity() { return FVector(0.f, 0.f, 0.f); }
    void SampleFinishedPlaying(CSoundEvent *) {}
};

KioskAudioOwner gOwner;
CSample *gSample = NULL;
State gState;
bool gInitialized = false;

double KioskClock() {
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    if (rate.QuadPart <= 0) return 0.0;
    static LONGLONG firstQpc = 0;
    static bool haveFirst = false;
    if (!haveFirst) { firstQpc = now.QuadPart; haveFirst = true; }
    return double(now.QuadPart - firstQpc) / double(rate.QuadPart);
}

void ReleaseSample() {
    if (gSample) {
        SOUND.KillSamplesForThing(&gOwner);
        CSample *sample = gSample;
        gSample = NULL;
        delete sample;
    }
}

bool TryOpenOgg(COggFileRead *reader, const char *lang, const char *stem) {
    if (!reader || !lang || !*lang || !SafeStem(stem)) return false;
    char path[260];
    const int n = std::snprintf(path, sizeof path, "data\\sounds\\%s\\MessageBox\\%s.ogg", lang, stem);
    if (n <= 0 || static_cast<unsigned>(n) >= sizeof path) return false;
    return SUCCEEDED(reader->Open(path)) && reader->HasFormat();
}

bool BackendStart(void *, const char *stem) {
    ReleaseSample();
    if (!SOUND.IsInitialised() || !SafeStem(stem)) return false;
    COggFileRead reader;
    const char *lang = TEXT.mLoaded ? TEXT.GetLanguageName() : "english";
    bool opened = TryOpenOgg(&reader, lang, stem);
    if (!opened && stricmp(lang, "english") != 0)
        opened = TryOpenOgg(&reader, "english", stem);
    if (!opened) {
        PortSessionLog(PORT_SESSION_GAME, false, "museum kiosk: speech clip %s unavailable", stem);
        return false;
    }
    if (reader.GetChannels() != 1 || reader.GetSampleRate() != 44100) {
        reader.Close();
        return false;
    }
    BYTE *pcm = static_cast<BYTE *>(std::malloc(kMaxDecodeBytes));
    if (!pcm) {
        reader.Close();
        return false;
    }
    UINT size = 0;
    const HRESULT hr = reader.Read(kMaxDecodeBytes, pcm, &size);
    reader.Close();
    if (FAILED(hr) || size < 2u) {
        std::free(pcm);
        return false;
    }
    CSample *sample = SOUND.CreateSampleFromData(pcm, static_cast<int>(size & ~1u), FALSE, NULL);
    std::free(pcm);
    if (!sample || !static_cast<CPCSample *>(sample)->mDSBuffer) {
        delete sample;
        return false;
    }
    std::strncpy(sample->mName, kSampleName, sizeof(sample->mName) - 1);
    sample->mName[sizeof(sample->mName) - 1] = 0;
    gSample = sample;
    SOUND.PlaySample(sample, &gOwner, SOUND.GetRadioMessageVolume(), ST_NOTRACKING,
                     false, 0.f, 0.f, -1.0f, false, 1.f, false, true, ST_GAME_SOUND);
    if (!SOUND.IsSamplePlaying(const_cast<char *>(kSampleName), &gOwner)) {
        ReleaseSample();
        return false;
    }
    PortSessionLog(PORT_SESSION_GAME, false, "museum kiosk: playing %s (%u bytes)", stem, static_cast<unsigned>(size));
    return true;
}

void BackendStop(void *) {
    ReleaseSample();
}

bool BackendPlaying(void *) {
    return gSample && SOUND.IsSamplePlaying(const_cast<char *>(kSampleName), &gOwner);
}

Backend MakeBackend() {
    Backend be = {NULL, BackendStart, BackendStop, BackendPlaying};
    return be;
}

} // namespace

void RuntimeInit() {
    ReleaseSample();
    Reset(&gState);
    gInitialized = true;
}

void RuntimeShutdown() {
    ReleaseSample();
    gOwner.Shutdown();
    Reset(&gState);
    gInitialized = false;
}

void RuntimeBeforeSoundReinit() {
    if (gInitialized)
        Stop(&gState, MakeBackend(), KioskClock());
    ReleaseSample();
}

void RuntimeUpdate(CeMuseumTour::Wing wing, bool paused, bool cinemaPlaying,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    if (!gInitialized) RuntimeInit();
    gState.hoveredKiosk = PickKiosk(eyeX, eyeY, eyeZ, aimX, aimY, aimZ, wing);
    Step(&gState, wing, paused, cinemaPlaying, MakeBackend(), KioskClock());
}

bool RuntimeHandleFire(bool release, float value, bool cinemaPlaying) {
    if (!gInitialized) return false;
    const bool usable = gState.hoveredKiosk >= 0 && !cinemaPlaying;
    const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
        release || value <= 0.0f, usable, gState.fireHeld, gState.fireCaptured);
    if (intent == CeCinemaRoom::FireIntent::Operate && usable) {
        Operate(&gState, gState.hoveredKiosk, MakeBackend(), KioskClock(), cinemaPlaying);
        return true;
    }
    return intent != CeCinemaRoom::FireIntent::Ordinary;
}

bool RuntimeIsAimingControl() {
    return gInitialized && gState.hoveredKiosk >= 0;
}

long RuntimeMusicDuckHundredths() {
    if (!gInitialized) return 0;
    const double now = KioskClock();
    CeMuseumMusicDuck::Settle(&gState.duck, now);
    return DuckHundredths(gState, now);
}

bool RuntimeCue(char *title, unsigned titleCap, char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!gInitialized || gState.hoveredKiosk < 0 || gState.hoveredKiosk >= kKioskCount) return false;
    const int k = gState.hoveredKiosk;
    const int t = gState.trackIndex[k] % kTracksPerKiosk;
    const KioskSpec &ks = kKiosks[k];
    if (title && titleCap) std::snprintf(title, titleCap, "Audio Kiosk - %s", ks.wingLabel);
    if (line && lineCap) {
        if (gState.phase == kPhasePlaying && gState.activeKiosk == k)
            std::snprintf(line, lineCap, "Playing %d/%d: %s.", t + 1, int(kTracksPerKiosk), ks.tracks[t].title);
        else if (gState.phase == kPhaseUnavailable && gState.activeKiosk == k)
            std::snprintf(line, lineCap, "Audio clip unavailable in this session.");
        else
            std::snprintf(line, lineCap, "Dispatch %d/%d: %s.", t + 1, int(kTracksPerKiosk), ks.tracks[t].title);
    }
    if (note && noteCap) {
        std::snprintf(note, noteCap, "%s",
                      (gState.phase == kPhasePlaying && gState.activeKiosk == k)
                          ? "Press Fire to stop and advance to the next recording."
                          : "Press Fire to listen to this field recording.");
    }
    return true;
}

} // namespace CeMuseumAudioKiosk

extern "C" void PortCeMuseumCinemaBeforeSoundReinit(void);   // ce_museum_hall.cpp: closes the projector's film

extern "C" void PortCeSoundBeforeReinit(void) {
    CeMuseumAudioKiosk::RuntimeBeforeSoundReinit();
    // The projector's film plays its track through the device being rebuilt:
    // close it before the device goes, never after.
    PortCeMuseumCinemaBeforeSoundReinit();
    CeCmdVoiceStop();
    PortCeCatalogueSoundStop();
}
#else
#include "ce_museum_audio_kiosk.h"
namespace CeMuseumAudioKiosk {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing, bool, bool, float, float, float, float, float, float) {}
bool RuntimeHandleFire(bool, float, bool) { return false; }
bool RuntimeIsAimingControl() { return false; }
long RuntimeMusicDuckHundredths() { return 0; }
bool RuntimeCue(char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
} // namespace CeMuseumAudioKiosk

extern "C" void PortCeSoundBeforeReinit(void) {}
#endif
