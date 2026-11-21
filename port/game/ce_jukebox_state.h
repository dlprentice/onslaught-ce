// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_jukebox_state.h: what the Music Player does, apart from the game: which row plays, what follows a track that ended,
// and when the menu theme is given back. The page (ce_jukebox.cpp) supplies the device (the game's music calls); a host test
// (port/tools/ce_jukebox_tests.py) supplies a fake one and checks every call. Row 0 is the Order row; rows 1 up are the tracks.
#ifndef CE_JUKEBOX_STATE_H
#define CE_JUKEBOX_STATE_H

enum ECeJukeboxOrder
{
	CE_ORDER_IN_ORDER = 0,
	CE_ORDER_SHUFFLE,
	CE_ORDER_ONE_TRACK,
	CE_ORDER_COUNT
};

struct SCeJukeboxDevice
{
	int			(*mCount)();			// rows, the Order row included
	bool		(*mPlay)(int row);		// start a track row (the game fades the menu theme out); false when the track cannot be played, nothing then changed
	void		(*mStop)();				// stop what plays
	void		(*mRestore)();			// give the menu theme back
	bool		(*mPlaying)();			// the game's music is playing
	unsigned	(*mRandom)();			// the player's own random numbers, never the game's
	bool		(*mAllowed)();			// the music may be played here at all
};

struct SCeJukebox
{
	int		mNow;					// the row playing, -1 for none
	int		mOrder;					// ECeJukeboxOrder, for this visit only
	bool	mTouched;				// a track was started in this visit: the menu theme is ours to give back
	int		mFailed;				// the row last asked for that could not be played, -1 for none (its hint says so)
};

inline void	CeJukeboxOpen(SCeJukebox *box)
{
	box->mNow = -1;
	box->mOrder = CE_ORDER_IN_ORDER;
	box->mTouched = false;
	box->mFailed = -1;
}

// Starts a track row; false, with the state as it was but for mFailed, when the device could not play it.
inline bool	CeJukeboxStart(SCeJukebox *box, const SCeJukeboxDevice *device, int row)
{
	if (!device->mPlay(row))
	{
		box->mFailed = row;
		return false;
	}
	box->mFailed = -1;
	box->mNow = row;
	box->mTouched = true;
	return true;
}

// Select on a row; true when it did something (the select sound).
inline bool	CeJukeboxSelect(SCeJukebox *box, const SCeJukeboxDevice *device, int row)
{
	if (!device->mAllowed() || (row < 0) || (row >= device->mCount()))
		return false;
	if (row == 0)
	{
		box->mOrder = (box->mOrder + 1) % CE_ORDER_COUNT;
		return true;
	}
	if (row == box->mNow)
	{
		device->mStop();
		box->mNow = -1;
		box->mFailed = -1;
	}
	else
		CeJukeboxStart(box, device, row);
	return true;
}

// The row after a track that ended, by the Order row: the next, wrapping; any other for Shuffle; none for One Track.
inline int	CeJukeboxFollowing(const SCeJukebox *box, const SCeJukeboxDevice *device)
{
	const int count = device->mCount();
	const int tracks = count - 1;
	if ((box->mOrder == CE_ORDER_ONE_TRACK) || (tracks < 2))
		return -1;
	if (box->mOrder == CE_ORDER_SHUFFLE)
	{
		int pick = 1 + int(device->mRandom() % unsigned(tracks - 1));		// one of the others, never the same twice running
		if (pick >= box->mNow)
			pick++;
		return pick;
	}
	return (box->mNow >= count - 1) ? 1 : box->mNow + 1;
}

// Once an update: the game stopped at the end of a track this page started; choose what follows. A track that cannot be played is
// passed over for the one after it, as far as one round of the list; when none can be played the music stays stopped.
inline void	CeJukeboxProcess(SCeJukebox *box, const SCeJukeboxDevice *device)
{
	if ((box->mNow <= 0) || device->mPlaying())
		return;
	SCeJukebox ended = *box;
	for (int tries = device->mCount(); tries > 0; tries--)
	{
		const int next = CeJukeboxFollowing(&ended, device);
		if (next < 0)
			break;
		if (CeJukeboxStart(box, device, next))
			return;
		ended.mNow = next;
	}
	box->mNow = -1;
}

// Leaving the page by any way; calling it again, or without a track ever started, does nothing.
inline void	CeJukeboxClose(SCeJukebox *box, const SCeJukeboxDevice *device)
{
	box->mNow = -1;
	if (!box->mTouched)
		return;
	box->mTouched = false;
	device->mRestore();
}

#endif
