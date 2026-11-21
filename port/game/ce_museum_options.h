// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_options.h: the options a museum visit hands back to the player, as plain arithmetic over a career-shaped object so a host test runs
// the very code the visit runs (ce_museum_options_tests.py).
//
// The pause menu's pages write these eight fields into the career record (sound and music volume, the two inverted axes, vibration, the controller preset and
// the screen position). A visit gives the whole record back as it found it, but an option the visitor changed is theirs to keep: only these fields are carried
// over the restore, so the progress half of the record (levels, grades, goodies, kills) is exactly as it was. The template is instantiated with the game's
// own CCareer in the engine adapter and with an authored stand-in in the host test.
#ifndef CE_MUSEUM_OPTIONS_H
#define CE_MUSEUM_OPTIONS_H

struct CeMuseumKeptOptions
{
	float sound, music;
	int invert[2], invertJet[2], vibration[2];
	int controller[2], screenX, screenY;
};

template <class Career>
inline CeMuseumKeptOptions CeMuseumKeepOptions(Career &career)
{
	CeMuseumKeptOptions kept = CeMuseumKeptOptions();
	kept.sound = career.GetSoundVolume();
	kept.music = career.GetMusicVolume();
	for (int player = 0; player != 2; ++player)
	{
		kept.invert[player] = career.GetInvertYAxis(player);
		kept.invertJet[player] = career.GetInvertJetYAxis(player);
		kept.vibration[player] = career.GetVibration(player);
		kept.controller[player] = career.GetControllerConfigurationNum(player);
	}
	kept.screenX = career.GetScreenPosX();
	kept.screenY = career.GetScreenPosY();
	return kept;
}

template <class Career>
inline void CeMuseumRestoreOptions(Career &career, const CeMuseumKeptOptions &kept)
{
	career.SetSoundVolume(kept.sound);
	career.SetMusicVolume(kept.music);
	for (int player = 0; player != 2; ++player)
	{
		career.SetInvertYAxis(player, kept.invert[player]);
		career.SetInvertJetYAxis(player, kept.invertJet[player]);
		career.SetVibration(player, kept.vibration[player]);
		career.SetControllerConfigurationNum(player, kept.controller[player]);
	}
	career.SetScreenPosX(kept.screenX);
	career.SetScreenPosY(kept.screenY);
}

// What any game save of the career writes while a visit holds its snapshot (patch 0279: CCareer::Save, so the pause menu's Continue, the Video page's display
// reset and every other options or career save alike): the record as the visit found it, with the options the visitor has now (the eight fields the leave
// gives back) and the god flags this save is writing (patch 0052 has already set them as the file must hold them, as the bonus and editor filters read them).
// A guard: a level's save writes the live career, and this keeps whatever a visit changes in the live career out of every file (no route seen changes it).
template <class Career>
inline void CeMuseumCareerToSave(Career &saved, Career &live)
{
	CeMuseumRestoreOptions(saved, CeMuseumKeepOptions(live));
	for (int player = 0; player != 2; ++player)
		saved.SetIsGod(player, live.GetIsGod(player));
}

#endif
