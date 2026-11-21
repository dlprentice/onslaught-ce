// SPDX-License-Identifier: GPL-3.0-only
// Commander Mode local retail voice line cues: deterministic non-RNG variant selection,
// rate-limited cooldown gate, safe language-folder path builder, and silent-failure playback
// of existing retail voice files from the player's own game install.
#ifndef PORT_CE_CMD_VOICE_H
#define PORT_CE_CMD_VOICE_H

#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr std::uint64_t kCeCmdVoiceCooldownNs = 1800000000ull; // >= 1.8 seconds between spoken lines

enum CeCmdVoiceCue
{
	CE_VOICE_NONE = 0,
	CE_VOICE_SELECT_UNIT,
	CE_VOICE_SELECT_STRUCTURE,
	CE_VOICE_MOVE,
	CE_VOICE_ATTACK,
	CE_VOICE_HOLD,
	CE_VOICE_FOLLOW,
	CE_VOICE_FALLBACK,
	CE_VOICE_BUILD_QUEUE,
	CE_VOICE_BUILD_READY,
	CE_VOICE_CANCEL,
	CE_VOICE_COUNT
};

// Deterministic, RNG-free voice cue gate and retail speech path builder.
struct CeCmdVoiceGate
{
	std::uint64_t lastPlayedNs;
	bool hasPlayed;
	unsigned cursors[CE_VOICE_COUNT];

	void Reset()
	{
		lastPlayedNs = 0;
		hasPlayed = false;
		for (int i = 0; i < CE_VOICE_COUNT; ++i) cursors[i] = 0;
	}

	// Returns true when cue is valid and at least cooldownNs have elapsed since the last spoken cue.
	// Immune to backwards clocks; never calls RNG.
	bool ShouldPlay(CeCmdVoiceCue cue, std::uint64_t nowNs, std::uint64_t cooldownNs = kCeCmdVoiceCooldownNs) const
	{
		if (cue <= CE_VOICE_NONE || cue >= CE_VOICE_COUNT) return false;
		if (!hasPlayed) return true;
		if (nowNs < lastPlayedNs) return false;
		return (nowNs - lastPlayedNs) >= cooldownNs;
	}

	// Selects the next retail MessageBox voice stem for this cue category (deterministic round-robin)
	// and updates the cooldown timestamp. Returns NULL if suppressed by cooldown or invalid cue.
	const char *TryNextStem(CeCmdVoiceCue cue, std::uint64_t nowNs, std::uint64_t cooldownNs = kCeCmdVoiceCooldownNs)
	{
		if (!ShouldPlay(cue, nowNs, cooldownNs)) return NULL;
		const char *const *stems = NULL;
		unsigned count = 0;
		StemsFor(cue, &stems, &count);
		if (!stems || count == 0) return NULL;
		const unsigned idx = cursors[cue] % count;
		++cursors[cue];
		lastPlayedNs = nowNs;
		hasPlayed = true;
		return stems[idx];
	}

	// Sanitize a runtime language folder name to one of the game's known language folders.
	static const char *SafeLanguage(const char *lang)
	{
		static const char *const kKnown[5] = {"english", "french", "german", "spanish", "italian"};
		if (!lang || !lang[0]) return "english";
		for (int i = 0; i < 5; ++i)
		{
			const char *a = lang;
			const char *b = kKnown[i];
			bool eq = true;
			while (*a || *b)
			{
				unsigned char ca = static_cast<unsigned char>(*a);
				unsigned char cb = static_cast<unsigned char>(*b);
				if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
				if (ca != cb) { eq = false; break; }
				++a; ++b;
			}
			if (eq) return kKnown[i];
		}
		return "english";
	}

	// Formats a bounded relative game path for the player's local retail speech file:
	// data\sounds\<language>\MessageBox\<stem>.ogg
	static bool FormatSpeechPath(const char *language, const char *stem, char *out, unsigned room)
	{
		if (!out || room < 40 || !stem || !stem[0]) return false;
		for (const char *p = stem; *p; ++p)
		{
			const char c = *p;
			const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			                (c >= '0' && c <= '9') || c == '_';
			if (!ok) return false;
		}
		const char *lang = SafeLanguage(language);
		const int written = std::snprintf(out, room, "data\\sounds\\%s\\MessageBox\\%s.ogg", lang, stem);
		return written > 0 && static_cast<unsigned>(written) < room;
	}

	static void StemsFor(CeCmdVoiceCue cue, const char *const **outStems, unsigned *outCount)
	{
		static const char *const kSelectUnit[] = {"tutorial_technician_01", "racing_start_2"};
		static const char *const kSelectStruct[] = {"tutorial_technician_01", "612_airfield_one_online"};
		static const char *const kMove[] = {"racing_start_3", "300_outpost_9k", "tutorial_movement"};
		static const char *const kAttack[] = {"322_go", "322_throat", "tutorial_weapon"};
		static const char *const kHold[] = {"732_hold_on", "400_alpha_holding"};
		static const char *const kFollow[] = {"231_hold_out", "reinforcements_enroute_2"};
		static const char *const kFallback[] = {"700_fall_back", "wingman_leaving_jason_2"};
		static const char *const kBuildQueue[] = {"reinforcements_enroute_1", "reinforcements_enroute_2"};
		static const char *const kBuildReady[] = {"312_reinforcements", "411_reinforcements_landed"};
		static const char *const kCancel[] = {"tutorial_technician_01"};

		switch (cue)
		{
		case CE_VOICE_SELECT_UNIT:      *outStems = kSelectUnit;   *outCount = 2; return;
		case CE_VOICE_SELECT_STRUCTURE: *outStems = kSelectStruct; *outCount = 2; return;
		case CE_VOICE_MOVE:             *outStems = kMove;         *outCount = 3; return;
		case CE_VOICE_ATTACK:           *outStems = kAttack;       *outCount = 3; return;
		case CE_VOICE_HOLD:             *outStems = kHold;         *outCount = 2; return;
		case CE_VOICE_FOLLOW:           *outStems = kFollow;       *outCount = 2; return;
		case CE_VOICE_FALLBACK:         *outStems = kFallback;     *outCount = 2; return;
		case CE_VOICE_BUILD_QUEUE:      *outStems = kBuildQueue;   *outCount = 2; return;
		case CE_VOICE_BUILD_READY:      *outStems = kBuildReady;   *outCount = 2; return;
		case CE_VOICE_CANCEL:           *outStems = kCancel;       *outCount = 1; return;
		default:                        *outStems = NULL;          *outCount = 0; return;
		}
	}
};

void CeCmdVoicePlay(CeCmdVoiceCue cue, std::uint64_t nowNs);
void CeCmdVoiceStop();

#endif
