// SPDX-License-Identifier: GPL-3.0-only
// The CE music playlist (port/game/ce_music.cpp): the player's own folder of .ogg tracks played in place of the level
// music, with the retail playlist's play types. Patch 0103 calls the first two from CPCMusic; the console's music
// command (ce_palette.cpp) calls the rest.
#ifndef PORT_CE_MUSIC_H
#define PORT_CE_MUSIC_H

// CPCMusic::DevicePlay: the file to stream for a cue of this selection (EMusicSelection): one of the player's tracks for
// a level's cue when the playlist has any and the CE layer may run, else the game's own file, unchanged.
char	*PortCeMusicPlay(char *gameFile, int selection);
// CPCMusic::DeviceGetTrackFinished, when the stream reports its track finished: true when that was one of the player's
// tracks and the playlist has dealt with it (the next track started, or silence in single mode), so the game's own
// music logic does not see the end.
bool	PortCeMusicTrackEnded();

enum CeMusicType { CE_MUSIC_SINGLE, CE_MUSIC_LINEAR, CE_MUSIC_RANDOM };

namespace CeMusic
{
bool		Allowed();								// the fences: the CE layer on, no replay, recording, probe or network
int			AddFolder(const char *dir, int *skipped, int *truncated);	// tracks added, -1 when the folder cannot be read
int			Tracks();
int			Type();
void		SetType(int type);
bool		Next();									// the next track now, when the player's music is playing
void		Clear();								// back to the game's music
const char	*Now();									// the player's track playing now, or NULL
const char	*Folder();								// the last folder added, or ""
}

#endif
