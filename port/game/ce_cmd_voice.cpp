// SPDX-License-Identifier: GPL-3.0-only
// Commander Mode local retail voice playback: plays short recorded Forseti radio lines
// directly from the player's own install while Commander is open in real-clock play.
// Never touches RNG, mission simulation, radio subtitles/portraits, or replays.
#ifdef CE_VERSION

#include "common.h"
#include "SoundManager.h"
#include "AudibleThing.h"
#include "OggFileRead.h"
#include "text.h"
#include "ce_commander.h"
#include "ce_cmd_voice.h"

#include <stdlib.h>
#include <string.h>

bool PortClockVirtual();

namespace
{
const unsigned kVoiceMaxBytes = 0x100000;
const char kVoiceSampleName[] = "ce commander voice";

class CeCmdVoiceOwner : public IAudibleThing
{
public:
	FVector GetSoundPos() { return FVector(0.f, 0.f, 0.f); }
	FMatrix GetSoundOrientation()
	{
		return FMatrix(FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
	}
	FVector GetSoundVelocity() { return FVector(0.f, 0.f, 0.f); }
	void SampleFinishedPlaying(CSoundEvent *) {}
};

CeCmdVoiceOwner gVoiceOwner;
CeCmdVoiceGate gVoiceGate;
CSample *gVoiceSample = NULL;

bool TryFallbackUiClick(CeCmdVoiceCue cue)
{
	const char *target = (cue == CE_VOICE_CANCEL) ? "Front End Back" : "Front End Select";
	unsigned guard = 65536;
	for (CSample *s = SOUND.GetFirstSample(); s && guard; s = s->mNextSample, --guard)
	{
		if (!static_cast<const CPCSample *>(s)->mDSBuffer || !s->mName[0]) continue;
		if (memchr(s->mName, 0, sizeof(s->mName)) == NULL) continue;
		if (stricmp(s->mName, target) != 0) continue;
		SOUND.PlaySample(s, &gVoiceOwner, SOUND.GetRadioMessageVolume() * 0.7f, ST_NOTRACKING, true,
		                 0.f, 0.f, -1.f, false, 1.f, false, true, ST_GAME_SOUND);
		return true;
	}
	return false;
}

bool TryPlayRetailOgg(const char *path)
{
	if (!path || !*path) return false;
	COggFileRead reader;
	char mutablePath[260];
	if (strlen(path) >= sizeof(mutablePath)) return false;
	strcpy(mutablePath, path);
	if (FAILED(reader.Open(mutablePath)) || !reader.HasFormat()) return false;
	if (reader.GetChannels() != 1 || reader.GetSampleRate() != 44100)
	{
		reader.Close();
		return false;
	}
	BYTE *pcm = static_cast<BYTE *>(malloc(kVoiceMaxBytes));
	if (!pcm)
	{
		reader.Close();
		return false;
	}
	UINT size = 0;
	const HRESULT hr = reader.Read(kVoiceMaxBytes, pcm, &size);
	reader.Close();
	if (FAILED(hr) || size < 2)
	{
		free(pcm);
		return false;
	}
	SOUND.KillSamplesForThing(&gVoiceOwner);
	if (gVoiceSample)
	{
		CSample *old = gVoiceSample;
		gVoiceSample = NULL;
		delete old;
	}
	CSample *sample = SOUND.CreateSampleFromData(pcm, int(size & ~1u), FALSE, NULL);
	free(pcm);
	if (!sample || !static_cast<CPCSample *>(sample)->mDSBuffer)
	{
		delete sample;
		return false;
	}
	gVoiceSample = sample;
	strncpy(gVoiceSample->mName, kVoiceSampleName, sizeof(gVoiceSample->mName) - 1);
	gVoiceSample->mName[sizeof(gVoiceSample->mName) - 1] = 0;
	SOUND.PlaySample(gVoiceSample, &gVoiceOwner, SOUND.GetRadioMessageVolume(), ST_NOTRACKING, false,
	                 0.f, 0.f, -1.f, false, 1.f, false, true, ST_GAME_SOUND);
	return true;
}
}

void CeCmdVoicePlay(CeCmdVoiceCue cue, std::uint64_t nowNs)
{
	if (!CeCmdMapOn() || !PortCeCmdMapOpen() || PortClockVirtual()) return;
	if (!SOUND.IsInitialised() || SOUND.GetRadioMessageVolume() <= 0.001f) return;
	const char *stem = gVoiceGate.TryNextStem(cue, nowNs);
	if (!stem) return;
	const char *lang = TEXT.mLoaded ? TEXT.GetLanguageName() : "english";
	char path[260];
	if (CeCmdVoiceGate::FormatSpeechPath(lang, stem, path, sizeof(path)) && TryPlayRetailOgg(path))
		return;
	if (strcmp(CeCmdVoiceGate::SafeLanguage(lang), "english") != 0 &&
	    CeCmdVoiceGate::FormatSpeechPath("english", stem, path, sizeof(path)) && TryPlayRetailOgg(path))
		return;
	TryFallbackUiClick(cue);
}

void CeCmdVoiceStop()
{
	gVoiceGate.Reset();
	if (SOUND.IsInitialised())
		SOUND.KillSamplesForThing(&gVoiceOwner);
	if (gVoiceSample)
	{
		CSample *old = gVoiceSample;
		gVoiceSample = NULL;
		delete old;
	}
}

#else

#include "ce_cmd_voice.h"

void CeCmdVoicePlay(CeCmdVoiceCue, std::uint64_t) {}
void CeCmdVoiceStop() {}

#endif
