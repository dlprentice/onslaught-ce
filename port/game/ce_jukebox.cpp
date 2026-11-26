// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_jukebox.cpp: the Music Player (Onslaught CE > Extras > Music Player): the game's ten soundtrack pieces and the player's
// own tracks (Your Music) in a list; Select plays one, Select on the playing one stops it, and the page follows the track's end by
// its Order row (In Order, Shuffle, One Track). Nothing is written: not ce.ini, not the options, not the music volume, which stays
// the Sound Options page's.
//
// The game does the playing. A soundtrack piece is played with the game's own playlist call (CMusic::PlayFromList, with its fade: the
// menu theme fades out and the chosen piece fades in, as the music menu of the console does it) and one of the player's tracks with
// CMusic::Play, which streams the file as it is. While a piece plays the play type is "single", so the game stops at the track's end
// and this page, which sees that, chooses what follows. Leaving the page by any way (Back, another CE page, a level starting, the
// front end going away) puts the menu theme back exactly as the Credits page does: the selection play type and the front end's
// piece with the game's fade. A page that never started a piece leaves the music alone.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "music.h"
#include "ce_extras.h"
#include "ce_music.h"
#include "ce_jukebox_state.h"
#include "ce_name_text.h"
#include "ce_ogg_length.h"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <string.h>

extern "C" unsigned	CE_MusicSeed(void);			// ce_music_scan.cpp

namespace
{
enum { kName = 80, kMaxCachedRows = 280 };
const char *const kOrders[CE_ORDER_COUNT] = {"In Order", "Shuffle", "One Track"};

SCeJukebox	sBox;				// which row plays and what follows
float		sStarted;			// when the piece began, on the front end's clock
int			sTotal;				// its length in whole seconds, 0 when it could not be read
int			sRowSeconds[kMaxCachedRows];	// cached track length in seconds per row (-1: unread)
int			sCachedCount = -1;

void	ResetLengths()
{
	for (int i = 0; i < kMaxCachedRows; i++)
		sRowSeconds[i] = -1;
	sCachedCount = -1;
}

int		RetailCount()
{
	int n = 0;
	for (CSong *song = MUSIC.mFirstSong; song; song = song->mNext)
		n++;
	return n;
}

int		CustomCount()
{
	return CeMusic::Allowed() ? CeMusic::Tracks() : 0;
}

// Row 0 is the Order row, then the soundtrack, then the player's own tracks.
int		Count()
{
	return 1 + RetailCount() + CustomCount();
}

void	Widen(WCHAR *out, int n, const char *in)
{
	PortCeExtrasWiden(out, n, in);
}

// A track's name as the list shows it: the file name without its folder or its ending, in the characters the font has.
void	CustomName(int custom, WCHAR *out, int n)
{
	const char *path = CeMusic::Track(custom);
	if (!path)
	{
		out[0] = 0;
		return;
	}
	const char *name = path;
	for (const char *p = path; *p; p++)
		if ((*p == '/') || (*p == '\\'))
			name = p + 1;
	char stem[256];
	snprintf(stem, sizeof(stem), "%s", name);
	const int length = int(strlen(stem));
	if ((length > 4) && (stem[length - 4] == '.') &&
		((stem[length - 3] | 0x20) == 'o') &&
		((stem[length - 2] | 0x20) == 'g') &&
		((stem[length - 1] | 0x20) == 'g'))
		stem[length - 4] = 0;
	// the path is bytes of UTF-8 on Linux and of the ANSI page on Windows
	PortCeExtrasWidenName(out, n, stem);
	PortCeExtrasInFont(out);
}

// The seconds a file plays, read from its two ends (ce_ogg_length.h); 0 when it cannot be read.
int		SecondsOf(FILE *file)
{
	if (!file)
		return 0;
	static unsigned char head[CE_OGG_HEAD], tail[CE_OGG_TAIL];
	int seconds = 0;
	const size_t got = fread(head, 1, sizeof(head), file);
	if ((got > 0) && !fseek(file, 0, SEEK_END))
	{
		const long size = ftell(file);
		const long from = (size > long(sizeof(tail))) ? size - long(sizeof(tail)) : 0;
		if ((size > 0) && !fseek(file, from, SEEK_SET))
		{
			const size_t end = fread(tail, 1, sizeof(tail), file);
			seconds = CeOggSeconds(head, int(got), tail, int(end));
		}
	}
	fclose(file);
	return seconds;
}

// The soundtrack is the game's own files, opened as the game opens them (the path layer).
int		SoundtrackSeconds(const char *name)
{
	return SecondsOf(fopen(name, "rb"));
}

// Whether a retail soundtrack file can be opened through the path layer and begins with valid Vorbis headers.
bool	SoundtrackStarts(const char *name)
{
	if (!name || !*name)
		return false;
	FILE *file = fopen(name, "rb");
	if (!file)
		return false;
	static unsigned char buf[CE_OGG_START];
	const size_t n = fread(buf, 1, sizeof(buf), file);
	fclose(file);
	return n > 0 && CeOggStarts(buf, int(n), n < sizeof(buf));
}

// The player's own tracks are anywhere on the disk: the C library's fopen, not the game's (port.h sends game files' fopen through the path layer, which
// refuses absolute paths). Opened for reading only.
#undef fopen
int		TrackSeconds(const char *path)
{
	return SecondsOf(fopen(path, "rb"));
}

// Whether the music worker can start the file: the same first bytes it needs, read here for reading only (ce_ogg_length.h). The worker
// gives no sign when it cannot, and the page would show a track that never plays.
bool	TrackStarts(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return false;
	static unsigned char head[CE_OGG_START];
	const size_t got = fread(head, 1, sizeof(head), file);
	fclose(file);
	return (got > 0) && CeOggStarts(head, int(got), got < sizeof(head));
}

int		LengthOf(int row)
{
	const int retail = RetailCount();
	if (row < 1)
		return 0;
	if (row <= retail)
	{
		CSong *song = MUSIC.GetSong(row - 1);
		return song ? SoundtrackSeconds(song->mFilename) : 0;
	}
	const char *path = CeMusic::Track(row - 1 - retail);
	return path ? TrackSeconds(path) : 0;
}

int		CachedLength(int row)
{
	const int count = Count();
	if (count != sCachedCount)
	{
		ResetLengths();
		sCachedCount = count;
	}
	if ((row < 1) || (row >= kMaxCachedRows))
		return 0;
	if (sRowSeconds[row] < 0)
		sRowSeconds[row] = LengthOf(row);
	return sRowSeconds[row];
}

// The device the state machine plays through (ce_jukebox_state.h): the game's own music calls.
bool	DevicePlay(int row)
{
	const int retail = RetailCount();
	if (row <= retail)
	{
		CSong *song = MUSIC.GetSong(row - 1);
		if (!song || !SoundtrackStarts(song->mFilename))
		{
			fprintf(stderr, PORT_LOG_PREFIX "music player: retail row %d cannot be played\n", row);
			return false;
		}
	}
	else
	{
		const char *name = CeMusic::Track(row - 1 - retail);
		if (!name || !TrackStarts(name))
		{
			fprintf(stderr, PORT_LOG_PREFIX "music player: row %d cannot be played\n", row);
			return false;
		}
	}
	MUSIC.mPlayType = MPT_SINGLE;		// the game stops at a track's end and this page chooses what follows
	if (row <= retail)
	{
		// the game's playlist call, with its fade. It leaves alone a piece the stream is on already, so what the game last started is
		// forgotten first: it may have been one of the player's tracks, which the list does not know.
		MUSIC.mCurrentSong = NULL;
		MUSIC.PlayFromList(MUSIC.GetSong(row - 1), TRUE);
	}
	else
	{
		static char path[4096];
		const char *name = CeMusic::Track(row - 1 - retail);
		const int wrote = name ? snprintf(path, sizeof(path), "%s", name) : -1;
		if (wrote < 0 || size_t(wrote) >= sizeof(path))
		{
			fprintf(stderr, PORT_LOG_PREFIX "music player: row %d path exceeds buffer (%zu bytes)\n", row, sizeof(path));
			return false;
		}
		MUSIC.mCurrentSong = NULL;
		// CMusic::Play clears protected mQueuedSong (patch 0383).
		MUSIC.Play(path);
	}
	sStarted = PLATFORM.GetSysTimeFloat();
	sTotal = CachedLength(row);
	fprintf(stderr, PORT_LOG_PREFIX "music player: playing row %d (%d s)\n", row, sTotal);
	return true;
}

void	DeviceStop()
{
	// CMusic::Stop clears protected mQueuedSong (patch 0383).
	MUSIC.Stop();
	sTotal = 0;
	fprintf(stderr, PORT_LOG_PREFIX "music player: stopped\n");
}

// The menu theme back, as the Credits page gives it back: the selection play type and the front end's piece, with the game's fade.
void	DeviceRestore()
{
	MUSIC.mPlayType = MPT_SELECTION;
	MUSIC.mCurrentSong = NULL;
	MUSIC.PlaySelection(MUS_FRONTEND, TRUE);
	sTotal = 0;
	fprintf(stderr, PORT_LOG_PREFIX "music player: the menu theme is back\n");
}

bool	DevicePlaying()
{
	return MUSIC.mPlaying;
}

// Shuffle draws from the page's own numbers (a small generator seeded from the clock, as the playlist's is), never from the playlist's
// stream, so that a visit to this page does not change the order a level's music plays in later.
unsigned	DeviceRandom()
{
	static unsigned state;
	if (!state)
		state = CE_MusicSeed() | 1u;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

bool	DeviceAllowed()
{
	return CeMusic::Allowed();
}

const SCeJukeboxDevice	kDevice = {Count, DevicePlay, DeviceStop, DeviceRestore, DevicePlaying, DeviceRandom, DeviceAllowed};

//*********************************************************************************
int		PageCount()
{
	return Count();
}

void	PageRow(int index, SCeListRow *out)
{
	const int n = int(sizeof(out->mLeft) / sizeof(WCHAR));
	const int retail = RetailCount();
	if (index == 0)
	{
		Widen(out->mLeft, n, "Play Order");
		Widen(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), kOrders[sBox.mOrder]);
		return;
	}
	if (index <= retail)
	{
		char text[32];
		snprintf(text, sizeof(text), "Soundtrack %d", index);
		Widen(out->mLeft, n, text);
	}
	else
		CustomName(index - 1 - retail, out->mLeft, n);
	if (index == sBox.mNow)
	{
		Widen(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), "Playing");
		out->mTone = CE_TONE_GOOD;
	}
	else if (index == sBox.mFailed)
	{
		// the track last asked for that could not be played stays marked until another is started, whatever row the cursor is on
		Widen(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), "Cannot Play");
		out->mTone = CE_TONE_BAD;
	}
	else
	{
		const int seconds = CachedLength(index);
		if (seconds > 0)
		{
			char text[16];
			snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
			Widen(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), text);
		}
	}
}

void	PageHint(int index, WCHAR *help, WCHAR *extra, int n)
{
	if (index == 0)
	{
		Widen(help, n, "What plays when a track ends. Select to change.");
		Widen(extra, n, "For this visit only.");
		return;
	}
	if (index == sBox.mFailed)
		Widen(help, n, "This track cannot be played.");
	else
		Widen(help, n, (index == sBox.mNow) ? "Playing. Select to stop." : "Select to play.");
	if ((sBox.mNow > 0) && (index != sBox.mNow))
	{
		// the playing row is out of sight: say which one it is, kept to one line so the progress bar stays above the help line
		static SCeListRow playing;
		memset(&playing, 0, sizeof(playing));
		PageRow(sBox.mNow, &playing);
		Widen(extra, n, "Playing ");
		int at = 0;
		while (extra[at])
			at++;
		for (int i = 0; playing.mLeft[i] && (at < n - 2); i++)
			extra[at++] = playing.mLeft[i];
		extra[at++] = '.';
		extra[at] = 0;
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		if (font)
		{
			SIZE sz;
			font->GetTextExtent(extra, &sz);
			while ((float(sz.cx) > 416.f) && (at > 12))
			{
				at--;
				while ((at > 12) && ((extra[at - 1] == WCHAR(' ')) || (extra[at - 1] == WCHAR('.'))))
					at--;
				extra[at - 3] = WCHAR('.');
				extra[at - 2] = WCHAR('.');
				extra[at - 1] = WCHAR('.');
				extra[at] = 0;
				font->GetTextExtent(extra, &sz);
			}
		}
	}
	else
		Widen(extra, n, "Music volume is in Sound Options.");
}

bool	PageSelect(int index)
{
	return CeJukeboxSelect(&sBox, &kDevice, index);
}

const WCHAR	*PageEmpty()
{
	static WCHAR none[] = L"No music is available right now.";
	return none;
}

void	PageOpen()
{
	CeJukeboxOpen(&sBox);
	sTotal = 0;
	ResetLengths();
	CeMusic::Prepare();
}

void	PageClose()
{
	CeJukeboxClose(&sBox, &kDevice);
	ResetLengths();
}

void	PageProcess()
{
	CeJukeboxProcess(&sBox, &kDevice);
}

// The elapsed time and, when the file's length is known, the Sound Options slider's ten segments (7 wide at a pitch of 8, in its
// colours) between it and the length, all on one line. A bar that is not the player's to move has no arrows.
void	PageDraw(float y)
{
	if (sBox.mNow <= 0)
		return;
	const int elapsed = int(PLATFORM.GetSysTimeFloat() - sStarted);
	char text[48];
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	const float barH = float(font->GetHeight()) > 14.f ? float(font->GetHeight()) : 14.f;
	if (y + barH > 452.f)
		y = 452.f - barH;
	SIZE size;
	WCHAR line[48];
	if (sTotal <= 0)
	{
		snprintf(text, sizeof(text), "%d:%02d", elapsed / 60, elapsed % 60);
		Widen(line, 48, text);
		font->GetTextExtent(line, &size);
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y, 0xffffffff, line, 0, 0, 1.f, 1.f);
		return;
	}
	snprintf(text, sizeof(text), "%d:%02d", elapsed / 60, elapsed % 60);
	Widen(line, 48, text);
	font->GetTextExtent(line, &size);
	font->DrawTextWithShadow(272.f - float(size.cx), y, 0xffffffff, line, 0, 0, 1.f, 1.f);
	snprintf(text, sizeof(text), "%d:%02d", sTotal / 60, sTotal % 60);
	Widen(line, 48, text);
	font->DrawTextWithShadow(368.f, y, 0xffffffff, line, 0, 0, 1.f, 1.f);
	int lit = 1 + (elapsed * 10) / sTotal;			// a piece that has begun shows its first segment
	if (lit > 10)
		lit = 10;
	for (int s = 0; s < 10; s++)
	{
		const float x = 280.f + float(s) * 8.f;
		FRONTEND.DrawPanel(x, y + 3.f, x + 7.f, y + 14.f, 0.5f, (s < lit) ? 0xff00a000 : 0xff505050);
	}
}
}

const SCeListPage	*PortCeJukeboxPage()
{
	static const SCeListPage page = {L"Music Player", PageCount, PageRow, PageHint, PageSelect, PageEmpty, PageOpen, PageClose,
									 PageProcess, PageDraw, 1.f, 1, true};
	return &page;
}

#endif
