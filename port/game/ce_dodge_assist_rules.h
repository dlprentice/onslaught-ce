// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_dodge_assist_rules.h: the rules of the Dodge Button, with no game types, so that a test runs exactly the code the
// game runs (ce_dodge_assist.cpp reads the game and calls these; ce_dodge_assist_tests.py runs them on their own and beside the
// game's own strafe code).
//
// What the game's dodge is. CBattleEngineWalkerPart::StrafeLeft and StrafeRight keep the last strafe value they were given
// (mLastMoveXVal), when each side's full push began (mLastStartHard{Left,Right}Time) and whether a dash is under way. A push to a
// side "begins" when the last value had not reached g_dash_start (0.9) toward that side and this one is beyond it. A push beyond
// g_dash_end (0.8) toward a side, from a last value short of that, dashes that way when the OTHER side's full push began more than
// half and less than the whole of g_dash_time (0.2 s) ago, by the game's own clock (EVENT_MANAGER.GetTime()). They do nothing in
// the air or during a dash. The player's strafe action reaches them only when an input sends it, so the last value stays what it
// was while nothing is pressed.
//
// The Dodge Button does not dash, move or time a dash itself. When the player presses it together with a left or right push of
// their own, it sends the two strafe actions of a double tap through the game's own controller path, as keys would send them: a
// full push to the other side, then, between 0.55 and 0.95 of g_dash_time later, a full push to the chosen side. The game's
// StrafeLeft and StrafeRight decide, with their own code, and the dash is the original one. While the double tap runs, the
// player's own strafe actions are not passed on, so that they cannot join or break it.
//
// Two facts of that code shape the rules. The first push is itself a push beyond g_dash_end, so it dashes the other way at once
// when the chosen side's push began between half and the whole of g_dash_time earlier (a player who presses D and, 0.15 s later,
// the Dodge Button): the first push waits until that has passed. And it counts as the beginning of a push only when the last
// value was not already full that way (a player who strafed left, let go, and now dodges right would have the first push ignored
// by the game, as the same double tap typed by hand is): a neutral strafe action, which the game handles as no input, comes first
// when the last value was.
//
// Both facts need the walker's memory, which the assist cannot read (it is private to the walker), so it keeps a copy: the last
// value and when each side's full push began. The copy is right only if it sees exactly the actions the walker takes. So it takes
// every action of the player's own that the walker accepts (Real, and Follow while the assist is idle: after a pause, say), none that the
// walker refuses (in the air or during a dash: the assist's own second push included, whose update says whether the walker takes it,
// Update::mAccepts), and starts again when the game's clock runs backwards (a new level, a new walker) or when the glue says the walker
// it followed is gone (a Battle Engine that is dying or another one: the game's clock runs on when a lost player is respawned). A copy
// that missed a begin would let the first push go out too early and the walker's own code dash toward the other side.
#ifndef PORT_CE_DODGE_ASSIST_RULES_H
#define PORT_CE_DODGE_ASSIST_RULES_H

namespace CeDodge
{
enum { kMaxTaps = 3 };

// What the assist sends: the strafe action of a side (-1 BUTTON_MECH_STRAFE_LEFT, +1 BUTTON_MECH_STRAFE_RIGHT), a full push as the
// game's own key reading sends it (mHard) or a neutral 0 (not mHard).
struct Tap
{
	int		mSide;
	bool	mHard;
};

// The walker's own numbers: g_dash_time, g_dash_start, g_dash_end, as the game registers them (retail 0.2, 0.9, 0.8).
struct Walker
{
	float	mTime, mStart, mEnd;
};

// What the game says at the end of one controller update.
struct Update
{
	bool	mEligible;			// the assist may act: ordinary play, the setting on, the player drives a Battle Engine, the game running
	bool	mCanDodge;			// that engine is a living, powered walker on the ground and not dashing
	bool	mAccepts;			// that walker takes a strafe action sent now (on the ground, not dashing): what the second push would reach
	bool	mDodgeDown;			// the Dodge Button (key or pad button) is held
	float	mNow;				// EVENT_MANAGER.GetTime()
	Walker	mWalker;
};

class Assist
{
public:
	// How far the player's own push must go to count as a direction, the margin kept from both ends of the window (of
	// g_dash_time), how long a press waits for the walker to be ready (seconds), and how many times the double tap may be started
	// (again once if a slow frame carried the second push past the window).
	static float Direction() { return 0.5f; }
	static float Margin() { return 0.05f; }
	static float Patience() { return 0.35f; }
	static int Attempts() { return 2; }

	Assist() { Reset(); }

	// Everything, the walker's remembered pushes included (a new level, or the clock running backwards).
	void Reset()
	{
		Cancel();
		mLastX = 0.f;
		mStart[0] = mStart[1] = -1e9f;
		mLastNow = 0.f;
		mHaveNow = false;
	}

	// The double tap and any press waiting to start one; what the walker remembers (a pause leaves it as it was) is kept.
	void Cancel()
	{
		mWaiting = false;
		mOwning = false;
		mWant = 0;
		mWantAt = 0.f;
		mSide = 0;
		mFirstAt = 0.f;
		mAttempt = 0;
		mPushing = 0;
		mAdopt = true;						// a press made while the assist was off is not one: what is held when it is back counts as held
		mSeen[0] = mSeen[1] = 0.f;
	}

	// The player's own strafe actions are not passed on while this is true.
	bool Owning() const { return mOwning; }
	bool Waiting() const { return mWaiting; }
	int Side() const { return mSide; }

	// One strafe action the player's own inputs sent (side as above, value as the game sends it: negative to the left); accepted:
	// the walker takes it (it is on the ground and not dashing). Returns true when it is not to be passed on. Called for every
	// one, dropped or not, so that the assist sees the player's press.
	bool Real(int side, float value, float now, const Walker &w, bool accepted)
	{
		if (mHaveNow && now < mLastNow)
			Reset();						// the clock ran backwards (see Step): this first update's push belongs to the new walker
		const float toward = value * (float)side;
		float &seen = mSeen[side < 0 ? 0 : 1];
		if (toward > seen)
			seen = toward;
		if (mOwning)
			return true;
		if (accepted)
			Deliver(side, value, now, w);
		return false;
	}

	// A strafe action of the player's own while the assist is idle (a pause has just ended, a level has just begun): the walker takes it all the
	// same, so the copy of its memory follows it; nothing else of the assist is touched, and it is not a press.
	void Follow(int side, float value, float now, const Walker &w, bool accepted)
	{
		if (mHaveNow && now < mLastNow)
			Reset();
		if (accepted)
			Deliver(side, value, now, w);
	}

	// Once at the end of each controller update, after the player's own inputs were handled. Writes the taps to send now (they
	// reach the walker after the player's own actions of the update) and returns how many.
	int Step(const Update &u, Tap *out)
	{
		const Walker &w = u.mWalker;
		if (!u.mEligible || !(w.mTime > 0.f))
		{
			Cancel();
			return 0;
		}
		if (mHaveNow && u.mNow < mLastNow)
			Reset();						// the clock ran backwards: a new level, a new walker
		mHaveNow = true;
		mLastNow = u.mNow;
		const float now = u.mNow;

		// A press is the button and one direction coming together: from neither (a tap, or a hold begun later) or from the other
		// direction (D let go and A pressed in the same update, which a roll of the fingers does about half the time at twenty updates a second).
		const int pushing = u.mDodgeDown ? Seen() : 0;
		const bool pressed = pushing != 0 && pushing != mPushing && !mAdopt;
		mPushing = pushing;
		mAdopt = false;
		mSeen[0] = mSeen[1] = 0.f;

		int count = 0;
		if (!mWaiting)
		{
			mOwning = false;
			if (pressed)
			{
				mWant = pushing;
				mWantAt = now;
			}
			if (mWant != 0)
			{
				// A press is kept for Patience() seconds, let go or not: a tap of the button is a press, and the walker may be mid-dash
				// or still too near the player's own push to start (Completes).
				if (now - mWantAt > Patience())
					mWant = 0;												// the walker was never ready
				else if (u.mCanDodge && !Completes(mWant, now, w))
				{
					count = First(mWant, now, w, out);
					mSide = mWant;
					mWant = 0;
					mWaiting = true;
					mOwning = true;
					mAttempt = 1;
				}
			}
			return count;
		}
		const float elapsed = now - mFirstAt;
		const float lo = w.mTime * (0.5f + Margin()), hi = w.mTime * (1.f - Margin());
		if (elapsed < lo)
			return 0;														// too soon for the walker: nothing is sent, the player's inputs stay held back
		if (elapsed <= hi)
		{
			out[count++] = Tap{mSide, true};
			if (u.mAccepts)
				Deliver(mSide, (float)mSide, now, w);					// the walker does not remember a push it refused (it left the ground)
			mWaiting = false;
			mOwning = false;
		}
		else if (mAttempt < Attempts() && u.mAccepts && !Completes(mSide, now, w))
		{
			count = First(mSide, now, w, out);								// a slow frame carried the push past the window: once more
			++mAttempt;
		}
		else
		{
			mWaiting = false;
			mOwning = false;
		}
		return count;
	}

private:
	// The walker's own bookkeeping of one strafe value, as StrafeLeft and StrafeRight keep it.
	void Deliver(int side, float value, float now, const Walker &w)
	{
		const float toward = value * (float)side, last = mLastX * (float)side;
		if (last < w.mStart && toward > w.mStart)
			mStart[side < 0 ? 0 : 1] = now;
		mLastX = value;
	}

	// Would a full push to the other side, now, dash that way, the wrong way? It would when the push toward `side` began between
	// half and the whole of g_dash_time ago (the margin kept to either side of that).
	bool Completes(int side, float now, const Walker &w) const
	{
		const float age = now - mStart[side < 0 ? 0 : 1];
		return age > w.mTime * (0.5f - Margin()) && age < w.mTime * (1.f + Margin());
	}

	// The first push of a double tap toward `side`: a full push to the other side, after a neutral action when the walker would not
	// count it as the beginning of a push.
	int First(int side, float now, const Walker &w, Tap *out)
	{
		int count = 0;
		const int other = -side;
		if (mLastX * (float)other >= w.mStart)
		{
			out[count++] = Tap{side, false};
			Deliver(side, 0.f, now, w);
		}
		out[count++] = Tap{other, true};
		Deliver(other, (float)other, now, w);
		mFirstAt = now;
		return count;
	}

	// The side the player is pushing toward this update: 0 when neither, or both.
	int Seen() const
	{
		const bool left = mSeen[0] >= Direction(), right = mSeen[1] >= Direction();
		return left == right ? 0 : (left ? -1 : 1);
	}

	bool	mWaiting, mOwning, mAdopt, mHaveNow;
	int		mWant, mSide, mAttempt, mPushing;	// mPushing: the side the player asked for in the last update (0: none)
	float	mWantAt, mFirstAt, mLastX, mLastNow;
	float	mStart[2];			// when each side's full push began (0 left, 1 right), by the game's clock
	float	mSeen[2];			// how far the player pushed toward each side during this update
};
}

#endif
