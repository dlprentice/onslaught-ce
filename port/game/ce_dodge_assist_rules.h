// A single axis follows the walker's ordinary opposite-push gesture. Keep
// its input history through pauses, refuse a second dash while one is active,
// and send both pushes only through the existing walker input actions.
// PlanarAssist arbitrates between axes so a held diagonal remains one request.
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
	int InputDirection() const { return Seen(); }
	bool HasInput() const { return mSeen[0] > Direction() || mSeen[1] > Direction(); }
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
// Each axis has its own real input history. Only one may synthesize a gesture
// at a time; a diagonal held across frames does not turn into a second dodge.
class PlanarAssist : public Assist
{
public:
	void Reset()
	{
		Assist::Reset();
		mFore.Reset();
		mAxis = mLastAxis = 0;
		mChord = false;
	}
	bool Waiting() const { return Assist::Waiting() || mFore.Waiting(); }
	bool Owning() const { return Assist::Owning() || mFore.Owning(); }
	int Side() const { return mLastAxis == 2 ? 2 * mFore.Side() : Assist::Side(); }
	bool Real(int direction, float value, float now, const Walker &walker, bool accepted)
	{
		return direction == -2 || direction == 2
			? mFore.Real(direction / 2, value, now, walker, accepted)
			: Assist::Real(direction, value, now, walker, accepted);
	}
	void Follow(int direction, float value, float now, const Walker &walker, bool accepted)
	{
		if (direction == -2 || direction == 2)
			mFore.Follow(direction / 2, value, now, walker, accepted);
		else
			Assist::Follow(direction, value, now, walker, accepted);
	}
	int Step(const Update &u, Tap *out)
	{
		const int lateral = InputDirection(), depthSide = mFore.InputDirection();
		const bool hasX = HasInput(), hasY = mFore.HasInput();
		const bool ambiguous = (hasX && hasY) || (hasX && !lateral) || (hasY && !depthSide);
		if (!u.mEligible || !u.mDodgeDown || (!hasX && !hasY)) {
			mAxis = 0; mChord = false;
		} else if (ambiguous) {
			// Finish an already chosen direction. With no first edge there is no choice.
			mChord = true;
		} else if (!mChord) {
			mAxis = hasX ? 1 : 2;
		}
		// Finish the pending pair even if the player releases the modifier.
		const int axis = Assist::Waiting() ? 1 : mFore.Waiting() ? 2 : mAxis;
		Update x = u, y = u;
		x.mDodgeDown = u.mDodgeDown && axis == 1;
		y.mDodgeDown = u.mDodgeDown && axis == 2;
		Tap depth[kMaxTaps];
		const int lateralCount = Assist::Step(x, out);
		const int depthCount = mFore.Step(y, depth);
		if (depthCount) {
			for (int i = 0; i < depthCount; ++i)
				out[i] = Tap{2 * depth[i].mSide, depth[i].mHard};
			mLastAxis = 2;
			return depthCount;
		}
		if (lateralCount) mLastAxis = 1;
		return lateralCount;
	}
private:
	Assist mFore;
	int mAxis = 0, mLastAxis = 0;
	bool mChord = false;
};
}

#endif
