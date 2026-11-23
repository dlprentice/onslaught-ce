// SPDX-License-Identifier: GPL-3.0-only
// port/network/npeer_ring.h: self-contained C++17 N-peer deterministic lockstep input & hash ring foundation.
//
// Supports 2..kMaxPeers (default 4) peers in a host-relayed star topology:
//   - Slot 0 is the Host; slots 1..(playerCount - 1) are Guests.
//   - Each tick T advances only when every peer active at T has submitted input (`(readyMask & reqMask) == reqMask`).
//   - Host serializes a deterministic big-endian star-relay bundle (`BuildHostBundle`) containing all active peers'
//     inputs and the authoritative per-tick `activeMask`; Guests atomically validate & store `activeMask` inside
//     `InputFrame` (`ApplyHostBundle`) so mid-stream peer disconnects never cause host/guest desync or `MALFORMED` stall.
//   - Periodic per-peer state hashes enforce `mHashEvery` alignment and bounded tick-window validation so stale,
//     unaligned, or out-of-window packets can never clobber in-progress hash slots; verified ticks are counted once,
//     desync attribution uses majority/plurality voting reporting ALL mismatching slots (`divergingMask`), and
//     `CheckHashOverdue` detects missing/timed-out peer hashes.
//   - Disconnect & rebind operations are atomic, honor `mDisconnectTick` & `mResumeTick` in `ActiveMaskAt(tick)`,
//     scrub ring entries `>= resumeTick`, and prevent UB.
//
// Pure header with zero game-header or platform dependencies so it compiles cleanly under C++17 + ASan/UBSan,
// including when `src/game.h` (`#define MAX_PLAYERS 4`) is included beforehand.
#ifndef PORT_NETWORK_NPEER_RING_H
#define PORT_NETWORK_NPEER_RING_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum CeNPeerStatus
{
	CE_NPEER_OK = 0,
	CE_NPEER_ERR_INVALID_ARG = -1,
	CE_NPEER_ERR_NOT_INIT = -2,
	CE_NPEER_ERR_INACTIVE_SLOT = -3,
	CE_NPEER_ERR_STALE_TICK = -4,
	CE_NPEER_ERR_FUTURE_TICK = -5,
	CE_NPEER_ERR_CONFLICT = -6,
	CE_NPEER_ERR_NOT_READY = -7,
	CE_NPEER_ERR_BUFFER_TOO_SMALL = -8,
	CE_NPEER_ERR_MALFORMED_FRAME = -9,
	CE_NPEER_ERR_HOST_LOST = -10,
	CE_NPEER_ERR_TOO_FEW_PEERS = -11
};

enum CeNPeerHashResult
{
	CE_NPEER_HASH_PENDING = 0,
	CE_NPEER_HASH_MATCH = 1,
	CE_NPEER_HASH_DESYNC = 2,
	CE_NPEER_HASH_INVALID = 3
};

struct CeNPeerDesyncReport
{
	uint32_t	tick;
	uint8_t		referenceSlot;
	uint8_t		divergingSlot;	// lowest active slot disagreeing with consensus
	uint8_t		divergingMask;	// bitmask of ALL active slots disagreeing with consensus
	uint64_t	expectedHash;
	uint64_t	actualHash;
};

struct CeNPeerHashOverdueReport
{
	uint32_t	tick;
	uint8_t		expectedMask;
	uint8_t		presentMask;
	uint8_t		missingMask;
};

// Default compact input payload (fixed-width, endian-neutral serialization via explicit codec).
struct CeNPeerInputPayload
{
	uint32_t	buttons0;
	uint32_t	buttons1;
	int16_t		axisYaw;
	int16_t		axisPitch;
	int16_t		axisMove;
	int16_t		axisStrafe;

	static constexpr size_t kWireBytes = 16u;

	bool operator==(const CeNPeerInputPayload &o) const
	{
		return buttons0 == o.buttons0 && buttons1 == o.buttons1 &&
		       axisYaw == o.axisYaw && axisPitch == o.axisPitch &&
		       axisMove == o.axisMove && axisStrafe == o.axisStrafe;
	}
	bool operator!=(const CeNPeerInputPayload &o) const { return !(*this == o); }

	void EncodeBE(uint8_t out[kWireBytes]) const
	{
		out[0]  = (uint8_t)((buttons0 >> 24) & 0xffu);
		out[1]  = (uint8_t)((buttons0 >> 16) & 0xffu);
		out[2]  = (uint8_t)((buttons0 >>  8) & 0xffu);
		out[3]  = (uint8_t)( buttons0        & 0xffu);
		out[4]  = (uint8_t)((buttons1 >> 24) & 0xffu);
		out[5]  = (uint8_t)((buttons1 >> 16) & 0xffu);
		out[6]  = (uint8_t)((buttons1 >>  8) & 0xffu);
		out[7]  = (uint8_t)( buttons1        & 0xffu);
		const uint16_t y = (uint16_t)axisYaw;
		const uint16_t p = (uint16_t)axisPitch;
		const uint16_t m = (uint16_t)axisMove;
		const uint16_t s = (uint16_t)axisStrafe;
		out[8]  = (uint8_t)((y >> 8) & 0xffu);
		out[9]  = (uint8_t)( y       & 0xffu);
		out[10] = (uint8_t)((p >> 8) & 0xffu);
		out[11] = (uint8_t)( p       & 0xffu);
		out[12] = (uint8_t)((m >> 8) & 0xffu);
		out[13] = (uint8_t)( m       & 0xffu);
		out[14] = (uint8_t)((s >> 8) & 0xffu);
		out[15] = (uint8_t)( s       & 0xffu);
	}

	void DecodeBE(const uint8_t in[kWireBytes])
	{
		buttons0 = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
		buttons1 = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 8) | (uint32_t)in[7];
		axisYaw    = (int16_t)(((uint16_t)in[8]  << 8) | (uint16_t)in[9]);
		axisPitch  = (int16_t)(((uint16_t)in[10] << 8) | (uint16_t)in[11]);
		axisMove   = (int16_t)(((uint16_t)in[12] << 8) | (uint16_t)in[13]);
		axisStrafe = (int16_t)(((uint16_t)in[14] << 8) | (uint16_t)in[15]);
	}
};

template <size_t kMaxPeers = 4u, size_t RING_SIZE = 64u, typename InputT = CeNPeerInputPayload>
class CeNPeerLockstepRing
{
	static_assert(kMaxPeers >= 2u && kMaxPeers <= 8u, "kMaxPeers must be in 2..8");
	static_assert(RING_SIZE >= 8u && ((RING_SIZE & (RING_SIZE - 1u)) == 0u), "RING_SIZE must be a power of 2 >= 8");

public:
	static constexpr uint32_t kBundleMagic = 0x43454E34u;	// "CEN4"
	static constexpr uint8_t  kBundleVersion = 1u;
	static constexpr size_t   kBundleHeaderBytes = 12u;		// magic(4) + ver(1) + mask(1) + count(1) + rsv(1) + tick(4)
	static constexpr size_t   kBundleEntryBytes = 1u + InputT::kWireBytes;

	CeNPeerLockstepRing() { Reset(); }

	void Reset()
	{
		mInitialised = false;
		mPlayerCount = 0;
		mLocalSlot = 0;
		mActiveMask = 0;
		mDisconnectedMask = 0;
		mPendingRebindMask = 0;
		mInputDelay = 0;
		mHashEvery = 20u;
		mCurrentTick = 0;
		mHashesVerified = 0;
		mHostLost = false;
		for (size_t i = 0; i < kMaxPeers; i++)
		{
			mDisconnectTick[i] = 0;
			mResumeTick[i] = 0;
		}
		for (size_t r = 0; r < RING_SIZE; r++)
		{
			mInputSlots[r].valid = false;
			mInputSlots[r].hasHostMask = false;
			mInputSlots[r].tick = 0;
			mInputSlots[r].readyMask = 0;
			mInputSlots[r].activeMask = 0;
			for (size_t p = 0; p < kMaxPeers; p++)
				mInputSlots[r].inputs[p] = InputT{};

			mHashSlots[r].valid = false;
			mHashSlots[r].verified = false;
			mHashSlots[r].hasExpectedMask = false;
			mHashSlots[r].tick = 0;
			mHashSlots[r].presentMask = 0;
			mHashSlots[r].expectedMask = 0;
			for (size_t p = 0; p < kMaxPeers; p++)
				mHashSlots[r].hashes[p] = 0;
		}
	}

	CeNPeerStatus Init(uint8_t playerCount, uint8_t localSlot, uint32_t inputDelay = 3u, uint32_t hashEvery = 20u)
	{
		Reset();
		if (playerCount < 2u || playerCount > kMaxPeers)
			return CE_NPEER_ERR_INVALID_ARG;
		if (localSlot >= playerCount)
			return CE_NPEER_ERR_INVALID_ARG;
		if (inputDelay >= RING_SIZE / 2u || hashEvery == 0u)
			return CE_NPEER_ERR_INVALID_ARG;

		mPlayerCount = playerCount;
		mLocalSlot = localSlot;
		mActiveMask = (uint8_t)((1u << playerCount) - 1u);
		mInputDelay = inputDelay;
		mHashEvery = hashEvery;
		mInitialised = true;
		return CE_NPEER_OK;
	}

	// Seeds ticks 0 .. (inputDelay - 1) with neutral zero inputs for all active slots (standard lockstep lead-in).
	CeNPeerStatus SeedInitialDelay(const InputT &neutralInput = InputT{})
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		for (uint32_t t = 0; t < mInputDelay; t++)
		{
			const uint8_t reqMask = ActiveMaskAt(t);
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				if ((reqMask & BitOf(s)) == 0u)
					continue;
				const CeNPeerStatus st = SubmitInput(s, t, neutralInput);
				if (st != CE_NPEER_OK)
					return st;
			}
		}
		return CE_NPEER_OK;
	}

	bool		IsInitialised() const { return mInitialised; }
	bool		IsHost() const { return mInitialised && mLocalSlot == 0u; }
	bool		HostLost() const { return mHostLost; }
	uint8_t		PlayerCount() const { return mPlayerCount; }
	uint8_t		LocalSlot() const { return mLocalSlot; }
	uint8_t		ActiveMask() const { return mActiveMask; }
	uint8_t		DisconnectedMask() const { return mDisconnectedMask; }
	uint8_t		PendingRebindMask() const { return mPendingRebindMask; }
	uint32_t	CurrentTick() const { return mCurrentTick; }
	uint32_t	InputDelay() const { return mInputDelay; }
	uint32_t	HashEvery() const { return mHashEvery; }
	uint32_t	HashesVerified() const { return mHashesVerified; }

	// Returns the bitmask of slots active and required at `tick`:
	// - A peer disconnected at `mDisconnectTick[s]` stays included for `tick < mDisconnectTick[s]` and excluded for `tick >= mDisconnectTick[s]`.
	// - A peer rebound at `mResumeTick[s] > mCurrentTick` is excluded for `tick < mResumeTick[s]` (preventing deadlock) and included for `tick >= mResumeTick[s]`.
	uint8_t ActiveMaskAt(uint32_t tick) const
	{
		uint8_t mask = mActiveMask;
		if (mDisconnectedMask != 0u)
		{
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				const uint8_t bit = BitOf(s);
				if ((mDisconnectedMask & bit) != 0u && (uint32_t)(tick - mDisconnectTick[s]) >= 0x80000000u)
					mask |= bit;
			}
		}
		if (mPendingRebindMask != 0u)
		{
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				const uint8_t bit = BitOf(s);
				if ((mPendingRebindMask & bit) != 0u && (uint32_t)(tick - mResumeTick[s]) >= 0x80000000u)
					mask &= (uint8_t)~bit;
			}
		}
		return mask;
	}

	static uint8_t ActivePeerCount(uint8_t mask)
	{
		uint8_t count = 0;
		for (uint8_t s = 0; s < kMaxPeers; s++)
			if ((mask & BitOf(s)) != 0u)
				count++;
		return count;
	}

	// Submits one peer's input for `tick`. Rejects inactive/disconnected/pre-resume slots, stale ticks,
	// out-of-window future ticks, and conflicting duplicate submissions for the same (slot, tick).
	CeNPeerStatus SubmitInput(uint8_t slot, uint32_t tick, const InputT &input)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (slot >= mPlayerCount)
			return CE_NPEER_ERR_INVALID_ARG;

		// Unsigned modular tick distance check (zero signed overflow, rejects stale ticks and far future ticks).
		const uint32_t ahead = tick - mCurrentTick;
		if (ahead >= 0x80000000u)
			return CE_NPEER_ERR_STALE_TICK;
		if (ahead >= (uint32_t)RING_SIZE)
			return CE_NPEER_ERR_FUTURE_TICK;

		const uint8_t bit = BitOf(slot);
		InputFrame &frame = mInputSlots[tick & kRingMask];
		const bool hasExisting = frame.valid && frame.tick == tick;
		const uint8_t reqMask = (hasExisting && frame.hasHostMask) ? frame.activeMask : ActiveMaskAt(tick);
		if ((reqMask & bit) == 0u)
			return CE_NPEER_ERR_INACTIVE_SLOT;

		if (!hasExisting)
		{
			frame.valid = true;
			frame.hasHostMask = false;
			frame.tick = tick;
			frame.readyMask = 0u;
			frame.activeMask = reqMask;
		}
		if ((frame.readyMask & bit) != 0u)
		{
			return (frame.inputs[slot] == input) ? CE_NPEER_OK : CE_NPEER_ERR_CONFLICT;
		}
		frame.inputs[slot] = input;
		frame.readyMask |= bit;
		return CE_NPEER_OK;
	}

	uint8_t ReadyMaskForTick(uint32_t tick) const
	{
		if (!mInitialised)
			return 0u;
		const InputFrame &frame = mInputSlots[tick & kRingMask];
		if (!frame.valid || frame.tick != tick)
			return 0u;
		const uint8_t reqMask = frame.hasHostMask ? frame.activeMask : ActiveMaskAt(tick);
		return (uint8_t)(frame.readyMask & reqMask);
	}

	bool IsTickReady(uint32_t tick) const
	{
		if (!mInitialised || mHostLost)
			return false;
		const InputFrame &frame = mInputSlots[tick & kRingMask];
		if (!frame.valid || frame.tick != tick)
			return false;
		const uint8_t reqMask = frame.hasHostMask ? frame.activeMask : ActiveMaskAt(tick);
		if (reqMask == 0u)
			return false;
		return (frame.readyMask & reqMask) == reqMask;
	}

	bool CanAdvance() const
	{
		return IsTickReady(mCurrentTick);
	}

	// Host star-relay serialization: packs all peers active at `tick` into a deterministic big-endian frame.
	CeNPeerStatus BuildHostBundle(uint32_t tick, uint8_t *outBuf, size_t capacity, size_t *outSize) const
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (!outBuf || !outSize)
			return CE_NPEER_ERR_INVALID_ARG;
		if (!IsTickReady(tick))
			return CE_NPEER_ERR_NOT_READY;

		const InputFrame &frame = mInputSlots[tick & kRingMask];
		const uint8_t reqMask = frame.hasHostMask ? frame.activeMask : ActiveMaskAt(tick);
		const uint8_t peerCount = ActivePeerCount(reqMask);
		const size_t needed = kBundleHeaderBytes + (size_t)peerCount * kBundleEntryBytes;
		if (capacity < needed)
			return CE_NPEER_ERR_BUFFER_TOO_SMALL;

		outBuf[0]  = (uint8_t)((kBundleMagic >> 24) & 0xffu);
		outBuf[1]  = (uint8_t)((kBundleMagic >> 16) & 0xffu);
		outBuf[2]  = (uint8_t)((kBundleMagic >>  8) & 0xffu);
		outBuf[3]  = (uint8_t)( kBundleMagic        & 0xffu);
		outBuf[4]  = kBundleVersion;
		outBuf[5]  = reqMask;
		outBuf[6]  = peerCount;
		outBuf[7]  = 0u;
		outBuf[8]  = (uint8_t)((tick >> 24) & 0xffu);
		outBuf[9]  = (uint8_t)((tick >> 16) & 0xffu);
		outBuf[10] = (uint8_t)((tick >>  8) & 0xffu);
		outBuf[11] = (uint8_t)( tick        & 0xffu);

		size_t pos = kBundleHeaderBytes;
		for (uint8_t s = 0; s < mPlayerCount; s++)
		{
			if ((reqMask & BitOf(s)) == 0u)
				continue;
			outBuf[pos++] = s;
			frame.inputs[s].EncodeBE(outBuf + pos);
			pos += InputT::kWireBytes;
		}
		*outSize = pos;
		return CE_NPEER_OK;
	}

	// Guest (or loopback) atomic bundle decode: validates header, exact length, slot ordering/uniqueness,
	// authoritative per-tick activeMask validity, and tick window BEFORE mutating any ring state.
	// Stores the host bundle's `activeMask` on the tick's InputFrame so mid-stream peer disconnects
	// consume identically on Host and Guest without false MALFORMED_FRAME stalls.
	CeNPeerStatus ApplyHostBundle(const uint8_t *buf, size_t len, uint32_t *outTick = nullptr)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (!buf || len < kBundleHeaderBytes)
			return CE_NPEER_ERR_MALFORMED_FRAME;

		const uint32_t magic = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
		                       ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
		if (magic != kBundleMagic || buf[4] != kBundleVersion || buf[7] != 0u)
			return CE_NPEER_ERR_MALFORMED_FRAME;

		const uint32_t tick = ((uint32_t)buf[8]  << 24) | ((uint32_t)buf[9]  << 16) |
		                      ((uint32_t)buf[10] <<  8) |  (uint32_t)buf[11];
		const uint32_t ahead = tick - mCurrentTick;
		if (ahead >= 0x80000000u)
			return CE_NPEER_ERR_STALE_TICK;
		if (ahead >= (uint32_t)RING_SIZE)
			return CE_NPEER_ERR_FUTURE_TICK;

		const uint8_t mask = buf[5];
		const uint8_t count = buf[6];
		const uint8_t allSlotsMask = (uint8_t)((1u << mPlayerCount) - 1u);
		if (mask == 0u || (mask & ~allSlotsMask) != 0u || (mask & 1u) == 0u ||
		    count < 2u || count != ActivePeerCount(mask))
			return CE_NPEER_ERR_MALFORMED_FRAME;

		const size_t expectedLen = kBundleHeaderBytes + (size_t)count * kBundleEntryBytes;
		if (len != expectedLen)
			return CE_NPEER_ERR_MALFORMED_FRAME;

		// Pass 1: validate every slot entry and check for duplicates or conflicts atomically.
		InputT decoded[kMaxPeers];
		uint8_t seenMask = 0u;
		int prevSlot = -1;
		size_t pos = kBundleHeaderBytes;
		for (uint8_t i = 0; i < count; i++)
		{
			const uint8_t slot = buf[pos++];
			if (slot >= mPlayerCount || (int)slot <= prevSlot)
				return CE_NPEER_ERR_MALFORMED_FRAME;
			const uint8_t bit = BitOf(slot);
			if ((mask & bit) == 0u || (seenMask & bit) != 0u)
				return CE_NPEER_ERR_MALFORMED_FRAME;
			decoded[slot].DecodeBE(buf + pos);
			pos += InputT::kWireBytes;
			seenMask |= bit;
			prevSlot = (int)slot;
		}
		if (seenMask != mask)
			return CE_NPEER_ERR_MALFORMED_FRAME;

		InputFrame &existing = mInputSlots[tick & kRingMask];
		if (existing.valid && existing.tick == tick)
		{
			if (existing.hasHostMask && existing.activeMask != mask)
				return CE_NPEER_ERR_CONFLICT;
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				const uint8_t bit = BitOf(s);
				if ((seenMask & bit) != 0u && (existing.readyMask & bit) != 0u && existing.inputs[s] != decoded[s])
					return CE_NPEER_ERR_CONFLICT;
			}
		}

		// Pass 2: commit atomically with authoritative host activeMask.
		existing.valid = true;
		existing.hasHostMask = true;
		existing.tick = tick;
		existing.activeMask = mask;
		existing.readyMask = mask;
		for (size_t s = 0; s < kMaxPeers; s++)
		{
			if (s < mPlayerCount && (mask & BitOf((uint8_t)s)) != 0u)
				existing.inputs[s] = decoded[s];
			else
				existing.inputs[s] = InputT{};
		}
		if (outTick)
			*outTick = tick;
		return CE_NPEER_OK;
	}

	// Consumes `mCurrentTick`, copies all peer inputs active at `mCurrentTick` (honoring any authoritative
	// host bundle mask stored on the frame) into `outInputs`, snapshots the consumed mask onto any aligned
	// hash checkpoint for both Host and Guest, clears the ring entry, advances tick, and retires any pending
	// rebind flag whose resumeTick has been reached.
	CeNPeerStatus ConsumeTick(InputT outInputs[kMaxPeers], uint8_t *outMask = nullptr)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (!outInputs)
			return CE_NPEER_ERR_INVALID_ARG;
		if (!CanAdvance())
			return CE_NPEER_ERR_NOT_READY;

		InputFrame &frame = mInputSlots[mCurrentTick & kRingMask];
		const uint8_t reqMask = frame.hasHostMask ? frame.activeMask : ActiveMaskAt(mCurrentTick);
		for (size_t s = 0; s < kMaxPeers; s++)
		{
			if (s < mPlayerCount && (reqMask & BitOf((uint8_t)s)) != 0u)
				outInputs[s] = frame.inputs[s];
			else
				outInputs[s] = InputT{};
		}
		if (outMask)
			*outMask = reqMask;
		if ((mCurrentTick % mHashEvery) == 0u)
		{
			HashFrame &hf = mHashSlots[(mCurrentTick / mHashEvery) & kRingMask];
			if (!hf.valid || hf.tick != mCurrentTick)
			{
				if (!hf.valid || (uint32_t)(mCurrentTick - hf.tick) < 0x80000000u)
				{
					hf.valid = true;
					hf.verified = false;
					hf.tick = mCurrentTick;
					hf.presentMask = 0u;
					hf.expectedMask = reqMask;
					hf.hasExpectedMask = true;
				}
			}
			else
			{
				hf.expectedMask = reqMask;
				hf.hasExpectedMask = true;
			}
		}
		frame.valid = false;
		frame.hasHostMask = false;
		frame.readyMask = 0u;
		frame.activeMask = 0u;
		mCurrentTick++;

		if (mPendingRebindMask != 0u)
		{
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				const uint8_t bit = BitOf(s);
				if ((mPendingRebindMask & bit) != 0u && mResumeTick[s] == mCurrentTick)
					mPendingRebindMask &= (uint8_t)~bit;
			}
		}
		return CE_NPEER_OK;
	}

	// Submits a peer's periodic state hash for `tick`.
	// Enforces strict `mHashEvery` alignment and a bounded non-aliasing tick window around `mCurrentTick`
	// so unaligned, stale, or far-future packets can never overwrite an in-progress hash slot.
	CeNPeerStatus SubmitHash(uint8_t slot, uint32_t tick, uint64_t stateHash)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (slot >= mPlayerCount)
			return CE_NPEER_ERR_INVALID_ARG;
		if ((tick % mHashEvery) != 0u)
			return CE_NPEER_ERR_INVALID_ARG;

		// Total window spans at most (RING_SIZE - 1) hash intervals, so no two in-window ticks can alias.
		const uint32_t ahead = tick - mCurrentTick;
		const uint64_t maxPast64 = (uint64_t)(RING_SIZE / 2u) * (uint64_t)mHashEvery;
		const uint64_t maxFuture64 = (uint64_t)(RING_SIZE / 2u - 1u) * (uint64_t)mHashEvery;
		if (ahead >= 0x80000000u)
		{
			const uint32_t behind = mCurrentTick - tick;
			if ((uint64_t)behind > maxPast64)
				return CE_NPEER_ERR_STALE_TICK;
		}
		else if ((uint64_t)ahead > maxFuture64)
		{
			return CE_NPEER_ERR_FUTURE_TICK;
		}

		const size_t idx = (tick / mHashEvery) & kRingMask;
		HashFrame &hf = mHashSlots[idx];
		const bool sameTick = hf.valid && hf.tick == tick;
		const uint8_t bit = BitOf(slot);
		const uint8_t reqMask = (sameTick && hf.hasExpectedMask) ? hf.expectedMask : ActiveMaskAt(tick);
		if ((reqMask & bit) == 0u)
			return CE_NPEER_ERR_INACTIVE_SLOT;

		if (hf.valid && hf.tick != tick)
		{
			// Never allow an older reordered tick to clobber a newer tick occupying the same ring slot.
			if ((uint32_t)(tick - hf.tick) >= 0x80000000u)
				return CE_NPEER_ERR_STALE_TICK;
		}
		if (!sameTick)
		{
			hf.valid = true;
			hf.verified = false;
			hf.hasExpectedMask = false;
			hf.expectedMask = 0u;
			hf.tick = tick;
			hf.presentMask = 0u;
		}
		if ((hf.presentMask & bit) != 0u)
			return (hf.hashes[slot] == stateHash) ? CE_NPEER_OK : CE_NPEER_ERR_CONFLICT;
		hf.hashes[slot] = stateHash;
		hf.presentMask |= bit;
		return CE_NPEER_OK;
	}

	// Checks whether all peers active at `tick` submitted matching hashes.
	// Uses majority/plurality voting (tie-breaking by lowest active slot) so a diverging host in a 3..4 peer
	// match is attributed accurately, and populates `outReport->divergingMask` with ALL mismatching slots.
	// Repeated calls on an already-verified tick do not double-count `mHashesVerified`.
	CeNPeerHashResult CheckHashes(uint32_t tick, CeNPeerDesyncReport *outReport = nullptr)
	{
		if (!mInitialised || (tick % mHashEvery) != 0u)
			return CE_NPEER_HASH_INVALID;
		const size_t idx = (tick / mHashEvery) & kRingMask;
		HashFrame &hf = mHashSlots[idx];
		const uint8_t reqMask = (hf.valid && hf.tick == tick && hf.hasExpectedMask) ? hf.expectedMask : ActiveMaskAt(tick);
		if (reqMask == 0u)
			return CE_NPEER_HASH_INVALID;
		if (!hf.valid || hf.tick != tick)
			return CE_NPEER_HASH_PENDING;
		if ((hf.presentMask & reqMask) != reqMask)
			return CE_NPEER_HASH_PENDING;

		// Majority / plurality voting across all active slots (ties break to the lowest active slot).
		uint8_t refSlot = 0u;
		uint8_t bestVotes = 0u;
		for (uint8_t s = 0; s < mPlayerCount; s++)
		{
			if ((reqMask & BitOf(s)) == 0u)
				continue;
			uint8_t votes = 0u;
			for (uint8_t k = 0; k < mPlayerCount; k++)
			{
				if ((reqMask & BitOf(k)) != 0u && hf.hashes[k] == hf.hashes[s])
					votes++;
			}
			if (votes > bestVotes)
			{
				bestVotes = votes;
				refSlot = s;
			}
		}
		if (bestVotes == 0u)
			return CE_NPEER_HASH_INVALID;

		const uint64_t expected = hf.hashes[refSlot];
		uint8_t divergingMask = 0u;
		uint8_t firstDiverging = 0u;
		bool anyDiverged = false;
		for (uint8_t s = 0; s < mPlayerCount; s++)
		{
			if ((reqMask & BitOf(s)) == 0u)
				continue;
			if (hf.hashes[s] != expected)
			{
				if (!anyDiverged)
				{
					firstDiverging = s;
					anyDiverged = true;
				}
				divergingMask |= BitOf(s);
			}
		}
		if (anyDiverged)
		{
			if (outReport)
			{
				outReport->tick = tick;
				outReport->referenceSlot = refSlot;
				outReport->divergingSlot = firstDiverging;
				outReport->divergingMask = divergingMask;
				outReport->expectedHash = expected;
				outReport->actualHash = hf.hashes[firstDiverging];
			}
			return CE_NPEER_HASH_DESYNC;
		}
		if (!hf.verified)
		{
			hf.verified = true;
			mHashesVerified++;
		}
		return CE_NPEER_HASH_MATCH;
	}

	// Detects whether any unverified hash checkpoint at least `maxLagTicks` behind `mCurrentTick`
	// is missing hashes from one or more active peers. Reports the earliest overdue checkpoint.
	bool CheckHashOverdue(uint32_t maxLagTicks, CeNPeerHashOverdueReport *outReport = nullptr) const
	{
		if (!mInitialised || mHostLost || mHashEvery == 0u)
			return false;
		if (mCurrentTick < maxLagTicks)
			return false;
		const uint32_t cutoffTick = mCurrentTick - maxLagTicks;
		const uint32_t maxIntervals = (uint32_t)(RING_SIZE / 2u);
		const uint32_t cutoffInterval = cutoffTick / mHashEvery;
		const uint32_t curInterval = mCurrentTick / mHashEvery;
		const uint32_t minInterval = (curInterval >= maxIntervals) ? (curInterval - maxIntervals + 1u) : 0u;
		if (cutoffInterval < minInterval)
			return false;

		for (uint32_t iv = minInterval; iv <= cutoffInterval; iv++)
		{
			const uint32_t tick = iv * mHashEvery;
			const size_t idx = iv & kRingMask;
			const HashFrame &hf = mHashSlots[idx];
			const bool sameTick = hf.valid && hf.tick == tick;
			if (sameTick && hf.verified)
				continue;
			const uint8_t reqMask = (sameTick && hf.hasExpectedMask) ? hf.expectedMask : ActiveMaskAt(tick);
			if (reqMask == 0u)
				continue;
			const uint8_t present = sameTick ? (uint8_t)(hf.presentMask & reqMask) : 0u;
			const uint8_t missing = (uint8_t)(reqMask & ~present);
			if (missing != 0u)
			{
				if (outReport)
				{
					outReport->tick = tick;
					outReport->expectedMask = reqMask;
					outReport->presentMask = present;
					outReport->missingMask = missing;
				}
				return true;
			}
		}
		return false;
	}

	// Quarantines a disconnecting peer atomically.
	// - If slot 0 (Host) disconnects, marks mHostLost = true and returns CE_NPEER_ERR_HOST_LOST.
	// - If disconnecting `slot` would leave fewer than 2 active peers, returns CE_NPEER_ERR_TOO_FEW_PEERS
	//   WITHOUT mutating masks or scrubbing ring entries.
	// - Otherwise removes `slot` from `mActiveMask` & `mPendingRebindMask`, marks it in `mDisconnectedMask`,
	//   records `mDisconnectTick[slot] = atTick` (honored by ActiveMaskAt), and scrubs input/hash bits >= atTick.
	CeNPeerStatus DisconnectPeer(uint8_t slot, uint32_t atTick)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (slot >= mPlayerCount)
			return CE_NPEER_ERR_INVALID_ARG;
		const uint8_t bit = BitOf(slot);
		if (slot == 0u)
		{
			mHostLost = true;
			mActiveMask &= (uint8_t)~bit;
			mPendingRebindMask &= (uint8_t)~bit;
			mDisconnectedMask |= bit;
			mDisconnectTick[slot] = atTick;
			return CE_NPEER_ERR_HOST_LOST;
		}
		if ((mActiveMask & bit) == 0u)
			return CE_NPEER_ERR_INACTIVE_SLOT;

		const uint8_t nextActiveMask = (uint8_t)(mActiveMask & ~bit);
		if (ActivePeerCount(nextActiveMask) < 2u)
			return CE_NPEER_ERR_TOO_FEW_PEERS;

		mActiveMask = nextActiveMask;
		mPendingRebindMask &= (uint8_t)~bit;
		mDisconnectedMask |= bit;
		mDisconnectTick[slot] = atTick;

		// Scrub stale ready bits from disconnected slot for ticks >= atTick so rebind starts spotless.
		for (size_t r = 0; r < RING_SIZE; r++)
		{
			if (mInputSlots[r].valid && !mInputSlots[r].hasHostMask &&
			    (uint32_t)(mInputSlots[r].tick - atTick) < 0x80000000u)
				mInputSlots[r].readyMask &= (uint8_t)~bit;
			if (mHashSlots[r].valid && (uint32_t)(mHashSlots[r].tick - atTick) < 0x80000000u)
				mHashSlots[r].presentMask &= (uint8_t)~bit;
		}
		return CE_NPEER_OK;
	}

	// Rebinds a previously disconnected guest slot cleanly starting at `resumeTick` (`mCurrentTick <= resumeTick < mCurrentTick + RING_SIZE`).
	// Ticks in `mCurrentTick .. resumeTick - 1` advance without requiring `slot` (preventing deadlock); starting at
	// `resumeTick`, `slot` is required again and all stale ring entries for `slot` at ticks `>= resumeTick` are scrubbed.
	CeNPeerStatus RebindPeer(uint8_t slot, uint32_t resumeTick)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (slot == 0u || slot >= mPlayerCount)
			return CE_NPEER_ERR_INVALID_ARG;
		const uint8_t bit = BitOf(slot);
		if ((mDisconnectedMask & bit) == 0u)
			return CE_NPEER_ERR_INVALID_ARG;
		const uint32_t ahead = resumeTick - mCurrentTick;
		if (ahead >= 0x80000000u)
			return CE_NPEER_ERR_STALE_TICK;
		if (ahead >= (uint32_t)RING_SIZE)
			return CE_NPEER_ERR_FUTURE_TICK;

		for (size_t r = 0; r < RING_SIZE; r++)
		{
			if (mInputSlots[r].valid && (uint32_t)(mInputSlots[r].tick - resumeTick) < 0x80000000u)
			{
				mInputSlots[r].readyMask &= (uint8_t)~bit;
				mInputSlots[r].inputs[slot] = InputT{};
			}
			if (mHashSlots[r].valid && (uint32_t)(mHashSlots[r].tick - resumeTick) < 0x80000000u)
			{
				mHashSlots[r].presentMask &= (uint8_t)~bit;
				mHashSlots[r].hashes[slot] = 0u;
			}
		}
		mDisconnectedMask &= (uint8_t)~bit;
		mActiveMask |= bit;
		mResumeTick[slot] = resumeTick;
		if (resumeTick != mCurrentTick)
			mPendingRebindMask |= bit;
		else
			mPendingRebindMask &= (uint8_t)~bit;
		return CE_NPEER_OK;
	}

private:
	static constexpr size_t kRingMask = RING_SIZE - 1u;
	static uint8_t BitOf(uint8_t slot) { return (uint8_t)(1u << slot); }

	struct InputFrame
	{
		uint32_t	tick;
		uint8_t		readyMask;
		uint8_t		activeMask;
		bool		hasHostMask;
		bool		valid;
		InputT		inputs[kMaxPeers];
	};

	struct HashFrame
	{
		uint32_t	tick;
		uint8_t		presentMask;
		uint8_t		expectedMask;
		bool		hasExpectedMask;
		bool		valid;
		bool		verified;
		uint64_t	hashes[kMaxPeers];
	};

	bool		mInitialised;
	bool		mHostLost;
	uint8_t		mPlayerCount;
	uint8_t		mLocalSlot;
	uint8_t		mActiveMask;
	uint8_t		mDisconnectedMask;
	uint8_t		mPendingRebindMask;
	uint32_t	mInputDelay;
	uint32_t	mHashEvery;
	uint32_t	mCurrentTick;
	uint32_t	mHashesVerified;
	uint32_t	mDisconnectTick[kMaxPeers];
	uint32_t	mResumeTick[kMaxPeers];
	InputFrame	mInputSlots[RING_SIZE];
	HashFrame	mHashSlots[RING_SIZE];
};

#endif
