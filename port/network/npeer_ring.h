// SPDX-License-Identifier: GPL-3.0-only
// port/network/npeer_ring.h: self-contained C++17 N-peer deterministic lockstep input & hash ring foundation.
//
// Supports 2..MAX_PLAYERS (default 4) peers in a host-relayed star topology:
//   - Slot 0 is the Host; slots 1..(playerCount - 1) are Guests.
//   - Each tick T advances only when every peer active at T has submitted input (`(readyMask & ActiveMaskAt(T)) == ActiveMaskAt(T)`).
//   - Host serializes a deterministic big-endian star-relay bundle (`BuildHostBundle`) containing all active peers'
//     inputs for tick T; Guests atomically validate & apply it (`ApplyHostBundle`).
//   - Periodic per-peer state hashes enforce `mHashEvery` alignment and bounded tick-window validation so stale,
//     unaligned, or out-of-window packets can never clobber in-progress hash slots; verified ticks are counted once
//     and any desync reports the exact diverging slot and tick (`CeNPeerDesyncReport`).
//   - Disconnect & rebind operations safely quarantine dropped slots, honor `resumeTick >= mCurrentTick` without
//     deadlocking ticks `mCurrentTick .. resumeTick - 1`, scrub ring entries `>= resumeTick`, and prevent UB.
//
// Pure header with zero game-header or platform dependencies so it compiles cleanly under C++17 + ASan/UBSan.
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
	uint8_t		divergingSlot;
	uint64_t	expectedHash;
	uint64_t	actualHash;
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

template <size_t MAX_PLAYERS = 4u, size_t RING_SIZE = 64u, typename InputT = CeNPeerInputPayload>
class CeNPeerLockstepRing
{
	static_assert(MAX_PLAYERS >= 2u && MAX_PLAYERS <= 8u, "MAX_PLAYERS must be in 2..8");
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
		for (size_t i = 0; i < MAX_PLAYERS; i++)
		{
			mDisconnectTick[i] = 0;
			mResumeTick[i] = 0;
		}
		for (size_t r = 0; r < RING_SIZE; r++)
		{
			mInputSlots[r].valid = false;
			mInputSlots[r].tick = 0;
			mInputSlots[r].readyMask = 0;
			for (size_t p = 0; p < MAX_PLAYERS; p++)
				mInputSlots[r].inputs[p] = InputT{};

			mHashSlots[r].valid = false;
			mHashSlots[r].verified = false;
			mHashSlots[r].tick = 0;
			mHashSlots[r].presentMask = 0;
			for (size_t p = 0; p < MAX_PLAYERS; p++)
				mHashSlots[r].hashes[p] = 0;
		}
	}

	CeNPeerStatus Init(uint8_t playerCount, uint8_t localSlot, uint32_t inputDelay = 3u, uint32_t hashEvery = 20u)
	{
		Reset();
		if (playerCount < 2u || playerCount > MAX_PLAYERS)
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

	// Returns the bitmask of slots that are active and required at `tick`.
	// A peer rebound at `resumeTick > mCurrentTick` is excluded for `tick < resumeTick` (preventing deadlock)
	// and included starting at `tick >= resumeTick`.
	uint8_t ActiveMaskAt(uint32_t tick) const
	{
		uint8_t mask = mActiveMask;
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
		for (uint8_t s = 0; s < MAX_PLAYERS; s++)
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
		if ((ActiveMaskAt(tick) & bit) == 0u || (mDisconnectedMask & bit) != 0u)
			return CE_NPEER_ERR_INACTIVE_SLOT;

		InputFrame &frame = mInputSlots[tick & kRingMask];
		if (!frame.valid || frame.tick != tick)
		{
			frame.valid = true;
			frame.tick = tick;
			frame.readyMask = 0u;
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
		return (uint8_t)(frame.readyMask & ActiveMaskAt(tick));
	}

	bool IsTickReady(uint32_t tick) const
	{
		const uint8_t reqMask = ActiveMaskAt(tick);
		if (!mInitialised || mHostLost || reqMask == 0u)
			return false;
		const InputFrame &frame = mInputSlots[tick & kRingMask];
		if (!frame.valid || frame.tick != tick)
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

		const uint8_t reqMask = ActiveMaskAt(tick);
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

		const InputFrame &frame = mInputSlots[tick & kRingMask];
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

	// Guest (or loopback) atomic bundle decode: validates header, exact length, slot ordering/uniqueness, active mask,
	// and tick window BEFORE mutating any ring state.
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
		const uint8_t reqMask = ActiveMaskAt(tick);
		if (mask == 0u || mask != reqMask || count != ActivePeerCount(mask))
			return CE_NPEER_ERR_MALFORMED_FRAME;

		const size_t expectedLen = kBundleHeaderBytes + (size_t)count * kBundleEntryBytes;
		if (len != expectedLen)
			return CE_NPEER_ERR_MALFORMED_FRAME;

		// Pass 1: validate every slot entry and check for duplicates or conflicts atomically.
		InputT decoded[MAX_PLAYERS];
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

		const InputFrame &existing = mInputSlots[tick & kRingMask];
		if (existing.valid && existing.tick == tick)
		{
			for (uint8_t s = 0; s < mPlayerCount; s++)
			{
				const uint8_t bit = BitOf(s);
				if ((seenMask & bit) != 0u && (existing.readyMask & bit) != 0u && existing.inputs[s] != decoded[s])
					return CE_NPEER_ERR_CONFLICT;
			}
		}

		// Pass 2: commit atomically.
		for (uint8_t s = 0; s < mPlayerCount; s++)
		{
			if ((seenMask & BitOf(s)) != 0u)
			{
				const CeNPeerStatus st = SubmitInput(s, tick, decoded[s]);
				if (st != CE_NPEER_OK)
					return st;
			}
		}
		if (outTick)
			*outTick = tick;
		return CE_NPEER_OK;
	}

	// Consumes `mCurrentTick`, copies all peer inputs active at `mCurrentTick` into `outInputs`, clears the
	// ring entry, advances tick, and retires any pending rebind flag whose resumeTick has been reached.
	CeNPeerStatus ConsumeTick(InputT outInputs[MAX_PLAYERS], uint8_t *outMask = nullptr)
	{
		if (!mInitialised)
			return CE_NPEER_ERR_NOT_INIT;
		if (mHostLost)
			return CE_NPEER_ERR_HOST_LOST;
		if (!outInputs)
			return CE_NPEER_ERR_INVALID_ARG;
		if (!CanAdvance())
			return CE_NPEER_ERR_NOT_READY;

		const uint8_t reqMask = ActiveMaskAt(mCurrentTick);
		InputFrame &frame = mInputSlots[mCurrentTick & kRingMask];
		for (size_t s = 0; s < MAX_PLAYERS; s++)
		{
			if (s < mPlayerCount && (reqMask & BitOf((uint8_t)s)) != 0u)
				outInputs[s] = frame.inputs[s];
			else
				outInputs[s] = InputT{};
		}
		if (outMask)
			*outMask = reqMask;
		frame.valid = false;
		frame.readyMask = 0u;
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

		const uint8_t bit = BitOf(slot);
		if ((ActiveMaskAt(tick) & bit) == 0u || (mDisconnectedMask & bit) != 0u)
			return CE_NPEER_ERR_INACTIVE_SLOT;

		const size_t idx = (tick / mHashEvery) & kRingMask;
		HashFrame &hf = mHashSlots[idx];
		if (hf.valid && hf.tick != tick)
		{
			// Never allow an older reordered tick to clobber a newer tick occupying the same ring slot.
			if ((uint32_t)(tick - hf.tick) >= 0x80000000u)
				return CE_NPEER_ERR_STALE_TICK;
		}
		if (!hf.valid || hf.tick != tick)
		{
			hf.valid = true;
			hf.verified = false;
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
	// On desync, populates `outReport` with the exact reference slot, diverging slot, tick, and both hashes.
	// Repeated calls on an already-verified tick do not double-count `mHashesVerified`.
	CeNPeerHashResult CheckHashes(uint32_t tick, CeNPeerDesyncReport *outReport = nullptr)
	{
		if (!mInitialised || (tick % mHashEvery) != 0u)
			return CE_NPEER_HASH_INVALID;
		const uint8_t reqMask = ActiveMaskAt(tick);
		if (reqMask == 0u)
			return CE_NPEER_HASH_INVALID;
		const size_t idx = (tick / mHashEvery) & kRingMask;
		HashFrame &hf = mHashSlots[idx];
		if (!hf.valid || hf.tick != tick)
			return CE_NPEER_HASH_PENDING;
		if ((hf.presentMask & reqMask) != reqMask)
			return CE_NPEER_HASH_PENDING;

		// Pick the lowest active slot (host slot 0 under normal star topology) as reference.
		uint8_t refSlot = 0u;
		bool foundRef = false;
		for (uint8_t s = 0; s < mPlayerCount; s++)
		{
			if ((reqMask & BitOf(s)) != 0u)
			{
				refSlot = s;
				foundRef = true;
				break;
			}
		}
		if (!foundRef)
			return CE_NPEER_HASH_INVALID;

		const uint64_t expected = hf.hashes[refSlot];
		for (uint8_t s = (uint8_t)(refSlot + 1u); s < mPlayerCount; s++)
		{
			if ((reqMask & BitOf(s)) == 0u)
				continue;
			if (hf.hashes[s] != expected)
			{
				if (outReport)
				{
					outReport->tick = tick;
					outReport->referenceSlot = refSlot;
					outReport->divergingSlot = s;
					outReport->expectedHash = expected;
					outReport->actualHash = hf.hashes[s];
				}
				return CE_NPEER_HASH_DESYNC;
			}
		}
		if (!hf.verified)
		{
			hf.verified = true;
			mHashesVerified++;
		}
		return CE_NPEER_HASH_MATCH;
	}

	// Quarantines a disconnecting peer cleanly.
	// - If slot 0 (Host) disconnects, marks mHostLost = true and returns CE_NPEER_ERR_HOST_LOST.
	// - If fewer than 2 active peers remain, returns CE_NPEER_ERR_TOO_FEW_PEERS.
	// - Otherwise removes `slot` from `mActiveMask` & `mPendingRebindMask`, marks it in `mDisconnectedMask`,
	//   and clears future input/hash bits >= atTick.
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

		mActiveMask &= (uint8_t)~bit;
		mPendingRebindMask &= (uint8_t)~bit;
		mDisconnectedMask |= bit;
		mDisconnectTick[slot] = atTick;

		// Scrub stale ready bits from disconnected slot for ticks >= atTick so rebind starts spotless.
		for (size_t r = 0; r < RING_SIZE; r++)
		{
			if (mInputSlots[r].valid && (uint32_t)(mInputSlots[r].tick - atTick) < 0x80000000u)
				mInputSlots[r].readyMask &= (uint8_t)~bit;
			if (mHashSlots[r].valid && (uint32_t)(mHashSlots[r].tick - atTick) < 0x80000000u)
				mHashSlots[r].presentMask &= (uint8_t)~bit;
		}
		if (ActivePeerCount(mActiveMask) < 2u)
			return CE_NPEER_ERR_TOO_FEW_PEERS;
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
		bool		valid;
		InputT		inputs[MAX_PLAYERS];
	};

	struct HashFrame
	{
		uint32_t	tick;
		uint8_t		presentMask;
		bool		valid;
		bool		verified;
		uint64_t	hashes[MAX_PLAYERS];
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
	uint32_t	mDisconnectTick[MAX_PLAYERS];
	uint32_t	mResumeTick[MAX_PLAYERS];
	InputFrame	mInputSlots[RING_SIZE];
	HashFrame	mHashSlots[RING_SIZE];
};

#endif
