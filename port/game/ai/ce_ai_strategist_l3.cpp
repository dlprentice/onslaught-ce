// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_strategist_l3.cpp: Optional host-only asynchronous Layer-3 Strategist.
// Compiled in HOST_BUFFER_GAME_MODULES (pcxx): strictly zero common.h, zero MemoryManager.h,
// zero bea_game_new, zero CRT rand(). Never ships model weights in the base package — loads
// optional user-installed `<user_dir>/ai-models/strategist.bin` if present, otherwise stays
// completely dormant (0 threads spawned, 0 main-thread stalls, Poll() returns NULL).
#include "ai/ce_ai_strategist_l3.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

const char *PortUserDir();

namespace
{

static constexpr size_t kMinModelBytes = 96u;
static constexpr size_t kMaxModelBytes = 64u * 1024u * 1024u;
static constexpr char   kModelMagic[8] = { 'C', 'E', 'S', 'T', 'R', 'A', 'T', '1' };

struct CeStrategistModelHeader
{
	char     magic[8];
	uint32_t version;
	uint32_t flags;
	int16_t  postureBias[5];
	uint8_t  baseAggressionQ8;
	uint8_t  baseAirBiasQ8;
	uint16_t reserved;
	uint16_t reserved2;
	char     label[64];
};
static_assert(sizeof(CeStrategistModelHeader) == 96u, "strategist model header size");

// Single-writer (SIM thread) -> single-reader (worker thread) wait-free, race-free handoff.
// Main thread skips publishing if worker has not yet copied the previous snapshot (seq != consumed).
struct SlotInbox
{
	CePlayerObs            buf[2];
	std::atomic<uint8_t>   active{0u};
	std::atomic<uint32_t>  seq{0u};
	std::atomic<uint32_t>  consumed{0u};
};

// Single-writer (worker thread) -> single-reader (SIM thread) non-blocking try_lock output.
struct SlotOutbox
{
	std::mutex             mu;
	std::atomic<bool>      valid{false};
	CeStrategicPrior       shared{};
	CeStrategicPrior       mainCopy{};
};

struct StrategistRuntime
{
	bool                    probed = false;
	bool                    modelLoaded = false;
	CeStrategistModelHeader model{};
	char                    modelLabelClean[48]{};

	SlotInbox               inbox[CE_PLAYER_MAX_SLOTS];
	SlotOutbox              outbox[CE_PLAYER_MAX_SLOTS];

	std::atomic<bool>       stopFlag{false};
	std::atomic<uint32_t>   notifySeq{0u};
	std::thread             worker;

	~StrategistRuntime()
	{
		StopWorker();
	}

	bool LoadExternalModel()
	{
		probed = true;
		modelLoaded = false;
		const char *userDir = PortUserDir();
		if (!userDir || !userDir[0])
			return false;

		std::string path(userDir);
		if (!path.empty() && path.back() != '/' && path.back() != '\\')
			path.push_back('/');
		path += "ai-models/strategist.bin";

		std::FILE *fp = std::fopen(path.c_str(), "rb");
		if (!fp)
			return false;

		if (std::fseek(fp, 0, SEEK_END) != 0)
		{
			std::fclose(fp);
			return false;
		}
		const long sz = std::ftell(fp);
		if (sz < static_cast<long>(kMinModelBytes) || static_cast<size_t>(sz) > kMaxModelBytes)
		{
			std::fclose(fp);
			return false;
		}
		if (std::fseek(fp, 0, SEEK_SET) != 0)
		{
			std::fclose(fp);
			return false;
		}

		CeStrategistModelHeader hdr{};
		const size_t nread = std::fread(&hdr, 1u, sizeof(hdr), fp);
		std::fclose(fp);
		if (nread != sizeof(hdr))
			return false;
		if (std::memcmp(hdr.magic, kModelMagic, sizeof(kModelMagic)) != 0 || hdr.version != 1u)
			return false;

		model = hdr;
		// Sanitize model label into printable ASCII.
		size_t outLen = 0u;
		for (size_t i = 0u; i < sizeof(hdr.label) && hdr.label[i] != '\0' && outLen + 1u < sizeof(modelLabelClean); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(hdr.label[i]);
			if (c >= 0x20u && c <= 0x7Eu)
				modelLabelClean[outLen++] = static_cast<char>(c);
		}
		if (outLen == 0u)
			std::memcpy(modelLabelClean, "Strategist", 11u);
		else
			modelLabelClean[outLen] = '\0';

		modelLoaded = true;
		return true;
	}

	void StartWorkerIfReady()
	{
		if (!probed)
			LoadExternalModel();
		if (!modelLoaded || worker.joinable())
			return;
		stopFlag.store(false, std::memory_order_release);
		worker = std::thread([this]() { WorkerLoop(); });
	}

	void StopWorker()
	{
		stopFlag.store(true, std::memory_order_release);
		if (worker.joinable())
			worker.join();
		for (int i = 0; i < CE_PLAYER_MAX_SLOTS; ++i)
		{
			outbox[i].valid.store(false, std::memory_order_release);
			const uint32_t s = inbox[i].seq.load(std::memory_order_acquire);
			inbox[i].consumed.store(s, std::memory_order_release);
		}
	}

	void EvaluateSnapshot(uint8_t slot, const CePlayerObs &obs, uint32_t gen)
	{
		CeStrategicPrior p{};
		p.generation = gen;
		p.sourceTick = obs.simTick;

		int friendlyCount = 0;
		int hostileCount = 0;
		int airborneHostiles = 0;
		bool baseThreat = false;
		bool hasHostileCentroid = false;
		float sumHx = 0.0f;
		float sumHy = 0.0f;
		float repairX = 256.0f;
		float repairY = 256.0f;
		bool hasRepair = false;

		const uint32_t nEnt = (obs.entityCount <= static_cast<uint32_t>(CE_PLAYER_OBS_MAX_ENTITIES))
			? obs.entityCount : static_cast<uint32_t>(CE_PLAYER_OBS_MAX_ENTITIES);
		for (uint32_t i = 0u; i < nEnt; ++i)
		{
			const CePlayerEntityObs &e = obs.entities[i];
			if (e.isFriendly)
			{
				++friendlyCount;
				if (e.isRepairPad && !hasRepair)
				{
					hasRepair = true;
					repairX = e.pos[0];
					repairY = e.pos[1];
				}
			}
			else
			{
				++hostileCount;
				if (e.airborne)
					++airborneHostiles;
				sumHx += e.pos[0];
				sumHy += e.pos[1];
				hasHostileCentroid = true;
				if (e.distSqQuantized < 75u * 75u * 16u && e.isBattleEngine)
					baseThreat = true;
			}
		}

		// Score the 5 strategic postures using external model biases + tactical summary.
		int32_t score[5] = {
			static_cast<int32_t>(model.postureBias[0]),
			static_cast<int32_t>(model.postureBias[1]),
			static_cast<int32_t>(model.postureBias[2]),
			static_cast<int32_t>(model.postureBias[3]),
			static_cast<int32_t>(model.postureBias[4])
		};
		if (obs.lifeRatio < 0.40f && hasRepair)
			score[CE_STRAT_REPAIR] += 180;
		if (baseThreat || hostileCount > friendlyCount + 2)
			score[CE_STRAT_DEFEND] += 110;
		if (friendlyCount >= hostileCount && obs.lifeRatio > 0.65f && hostileCount > 0)
			score[CE_STRAT_PUSH] += 105;
		if (hostileCount >= 2 && obs.energyRatio > 0.55f)
			score[CE_STRAT_FLANK] += 75;

		int bestPosture = 0;
		for (int k = 1; k < 5; ++k)
		{
			if (score[k] > score[bestPosture])
				bestPosture = k;
		}
		p.posture = static_cast<CeStrategicPosture>(bestPosture);

		int aggr = static_cast<int>(model.baseAggressionQ8);
		if (p.posture == CE_STRAT_PUSH)   aggr += 36;
		if (p.posture == CE_STRAT_DEFEND) aggr -= 24;
		if (p.posture == CE_STRAT_REPAIR) aggr -= 64;
		if (aggr < 0) aggr = 0;
		if (aggr > 255) aggr = 255;
		p.aggressionQ8 = static_cast<uint8_t>(aggr);

		int air = static_cast<int>(model.baseAirBiasQ8);
		if (airborneHostiles > 0) air += 40;
		if (obs.energyRatio < 0.35f) air -= 48;
		if (air < 0) air = 0;
		if (air > 255) air = 255;
		p.airBiasQ8 = static_cast<uint8_t>(air);

		if (p.posture == CE_STRAT_REPAIR && hasRepair)
		{
			p.hasFocusPoint = 1u;
			p.focusX = repairX;
			p.focusY = repairY;
		}
		else if (hasHostileCentroid && hostileCount > 0)
		{
			p.hasFocusPoint = 1u;
			p.focusX = sumHx / static_cast<float>(hostileCount);
			p.focusY = sumHy / static_cast<float>(hostileCount);
		}

		static const char *kPostureNames[5] = { "BALANCED", "PUSH", "DEFEND", "FLANK", "REPAIR" };
		char rawHint[200];
		std::snprintf(rawHint, sizeof(rawHint), "[%s] Posture %s (hostiles=%d, air=%d, hp=%d%%)",
		              modelLabelClean, kPostureNames[bestPosture], hostileCount, airborneHostiles,
		              static_cast<int>(obs.lifeRatio * 100.0f));
		CeAiStrategistSanitizeHint(rawHint, p.hint);

		// Publish cleanly under SlotOutbox mutex (main thread uses try_to_lock -> 0 stalls, 0 data races).
		SlotOutbox &ob = outbox[slot];
		{
			std::lock_guard<std::mutex> lk(ob.mu);
			ob.shared = p;
		}
		ob.valid.store(true, std::memory_order_release);
	}

	void WorkerLoop()
	{
		CePlayerObs localCopy{};
		uint32_t lastNotify = 0u;
		while (!stopFlag.load(std::memory_order_acquire))
		{
			const uint32_t curNotify = notifySeq.load(std::memory_order_acquire);
			bool didWork = false;
			if (curNotify != lastNotify)
			{
				lastNotify = curNotify;
				for (uint8_t s = 0u; s < static_cast<uint8_t>(CE_PLAYER_MAX_SLOTS); ++s)
				{
					if (stopFlag.load(std::memory_order_acquire))
						break;
					SlotInbox &in = inbox[s];
					const uint32_t seq = in.seq.load(std::memory_order_acquire);
					if (seq == in.consumed.load(std::memory_order_relaxed))
						continue;
					const uint8_t idx = in.active.load(std::memory_order_acquire) & 1u;
					localCopy = in.buf[idx];
					// Release buffer back to SIM thread immediately after copy completes.
					in.consumed.store(seq, std::memory_order_release);
					EvaluateSnapshot(s, localCopy, seq);
					didWork = true;
				}
			}
			if (!didWork)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
};

StrategistRuntime gRuntime;

} // namespace

extern "C" void CeAiStrategistSanitizeHint(const char *src, char out[164])
{
	if (!out)
		return;
	if (!src)
	{
		out[0] = '\0';
		return;
	}
	size_t n = 0u;
	for (const char *p = src; *p != '\0' && n < 160u; ++p)
	{
		const unsigned char c = static_cast<unsigned char>(*p);
		if (c >= 0x20u && c <= 0x7Eu)
			out[n++] = static_cast<char>(c);
		else if (c == '\t' || c == '\n' || c == '\r')
			out[n++] = ' ';
	}
	out[n] = '\0';
}

extern "C" void PortCeAiStrategistPublishObs(uint8_t slot, const CePlayerObs *obs)
{
	if (!obs || slot >= static_cast<uint8_t>(CE_PLAYER_MAX_SLOTS))
		return;
	if (!gRuntime.probed)
		gRuntime.StartWorkerIfReady();
	if (!gRuntime.modelLoaded)
		return;
	if (!gRuntime.worker.joinable())
		gRuntime.StartWorkerIfReady();

	// Wait-free, race-free handoff: drop snapshot if worker has not finished copying previous buffer.
	SlotInbox &in = gRuntime.inbox[slot];
	const uint32_t curSeq = in.seq.load(std::memory_order_relaxed);
	if (curSeq != in.consumed.load(std::memory_order_acquire))
		return;
	const uint8_t cur = in.active.load(std::memory_order_relaxed) & 1u;
	const uint8_t next = static_cast<uint8_t>(cur ^ 1u);
	in.buf[next] = *obs;
	in.active.store(next, std::memory_order_release);
	in.seq.store(curSeq + 1u, std::memory_order_release);
	gRuntime.notifySeq.fetch_add(1u, std::memory_order_release);
}

extern "C" const CeStrategicPrior *PortCeAiStrategistPoll(uint8_t slot)
{
	if (slot >= static_cast<uint8_t>(CE_PLAYER_MAX_SLOTS) || !gRuntime.modelLoaded)
		return nullptr;
	SlotOutbox &ob = gRuntime.outbox[slot];
	if (!ob.valid.load(std::memory_order_acquire))
		return nullptr;

	// Non-blocking try_lock snapshot into main-thread-owned copy (0 stalls, 0 data races).
	std::unique_lock<std::mutex> lk(ob.mu, std::try_to_lock);
	if (lk.owns_lock())
		ob.mainCopy = ob.shared;
	return ob.mainCopy.generation ? &ob.mainCopy : nullptr;
}

extern "C" void PortCeAiStrategistLevelReset(void)
{
	gRuntime.StopWorker();
	gRuntime.probed = false;
	gRuntime.modelLoaded = false;
	for (int i = 0; i < CE_PLAYER_MAX_SLOTS; ++i)
		std::memset(&gRuntime.outbox[i].mainCopy, 0, sizeof(gRuntime.outbox[i].mainCopy));
}

extern "C" void PortCeAiStrategistShutdown(void)
{
	gRuntime.StopWorker();
	gRuntime.probed = false;
	gRuntime.modelLoaded = false;
	for (int i = 0; i < CE_PLAYER_MAX_SLOTS; ++i)
		std::memset(&gRuntime.outbox[i].mainCopy, 0, sizeof(gRuntime.outbox[i].mainCopy));
}
