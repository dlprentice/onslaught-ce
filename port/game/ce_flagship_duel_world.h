// SPDX-License-Identifier: GPL-3.0-only
// Clean-Room Level 888 ("Flagship Duel"): pure version-50 WORLD body serializer,
// PMSH/PMS2 mesh-stub synthesizer, strict retail PC preflight matrix, and asset
// closure table for both 1P Solo (level 788) and 2P Split-Screen (level 888).
// Pure C++14 header: zero engine headers and zero heap/STL container allocations
// so both game builds (/QIfist) and standalone ASan/UBSan host tests share exact logic.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum class CeFlagshipDuelMode : uint8_t {
	kSolo1P = 0,
	kSplitScreen2P = 1,
};

namespace CeFlagshipDuelWorld {

static constexpr int kSoloLevel = 788;
static constexpr int kSplitLevel = 888;
static constexpr int kDonorArchiveLevel = 857;
static constexpr int kDonorBaseWorldId = 10;
static constexpr int kDonorMapId = 6;
static constexpr const char *kDonorArchivePath = "data\\resources\\857_res_PC.aya";

static constexpr uint16_t kWorldVersion = 50u;
static constexpr uint32_t kWorldHeaderVersion = 3u;
static constexpr unsigned kMaxWorldBodyBytes = 8192u;
static constexpr size_t kMaxBeConfigs = 8u;
static constexpr size_t kMaxActors = 24u;
static constexpr size_t kMaxShortNameBuf = 64u;

static constexpr int32_t kWorldTypeSinglePlayer = 0;
static constexpr int32_t kWorldTypeVersusMultiplayer = 2;

static constexpr uint32_t kOidUnit = 8u;        // matches OID_CUnit in src/oids.h
static constexpr uint32_t kOidStart = 15u;      // matches OID_CStart in src/oids.h
static constexpr uint32_t kOidSpawnPoint = 41u; // matches OID_CSpawnPoint in src/oids.h

static constexpr float kPi = 3.14159265f;

// Authentic Level 888 flagship stations: 320.0 m apart North-vs-South over the
// flat-sea 857 archipelago arena, nose-to-nose across X = 256, Z = -60.
static constexpr float kVenturerStartX = 256.0f;
static constexpr float kVenturerStartY = 420.0f;
static constexpr float kVenturerStartZ = -60.0f;
static constexpr float kVenturerStartYaw = kPi; // Facing South (-Y) toward Fenrir

static constexpr float kFenrirStartX = 256.0f;
static constexpr float kFenrirStartY = 100.0f;
static constexpr float kFenrirStartZ = -60.0f;
static constexpr float kFenrirStartYaw = 0.0f; // Facing North (+Y) toward Venturer

static constexpr float kMinOpeningSeparation2D = 300.0f;
static constexpr float kMinAirborneClearanceAboveWater = 18.0f;

// Clear maritime atmosphere with perpendicular pure-West sun (32 deg elevation,
// SunPosY = 0) so both North-South opponents have equal cross-axis lighting,
// neither player spawns staring into sun glare, and both flagships silhouette
// sharply across the 320 m strait instead of fading into donor level 857's fog.
static constexpr float kDuelFogDensity = 0.0010f;
static constexpr float kDuelSunDirX = -0.848f;
static constexpr float kDuelSunDirY = 0.0f;
static constexpr float kDuelSunDirZ = -0.530f;

// Retail PC battle engine configurations for Level 888 in worldheaders.dat:
// order MUST match {"Standard", "Laser", "Blaster"} so CFEPMultiplayerStart
// indices [0..2] resolve identically in WORLD.GetBattleEngineConfiguration().
static constexpr const char *kSplit2PBeConfigs[] = {
	"Standard",
	"Laser",
	"Blaster",
};
static constexpr size_t kSplit2PBeConfigCount =
	sizeof(kSplit2PBeConfigs) / sizeof(kSplit2PBeConfigs[0]);

inline int LevelForMode(CeFlagshipDuelMode mode) {
	return (mode == CeFlagshipDuelMode::kSplitScreen2P) ? kSplitLevel : kSoloLevel;
}

inline unsigned char AsciiLower(unsigned char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

inline bool StrEqualsIgnoreCase(const char *a, const char *b) {
	if (!a || !b) return false;
	while (*a && *b) {
		if (AsciiLower(static_cast<unsigned char>(*a++)) !=
		    AsciiLower(static_cast<unsigned char>(*b++)))
			return false;
	}
	return *a == '\0' && *b == '\0';
}

inline bool ValidPrintableName(const char *s) {
	if (!s || !s[0]) return false;
	const size_t n = strlen(s);
	if (n > 120u) return false;
	for (size_t i = 0; i < n; ++i) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c < 0x20u || c > 0x7Eu) return false;
	}
	return true;
}

inline bool IsForbiddenUnit(const char *unitDef) {
	return StrEqualsIgnoreCase(unitDef, "Fenrir Multiplayer");
}

enum CeFdActorRole : uint8_t {
	kRolePlayerStart = 0,
	kRolePlayerSpawnPoint = 1,
	kRoleVenturer = 2,
	kRoleFenrir = 3,
	kRoleForsetiEscort = 4,
	kRoleMuspellEscort = 5,
};

struct CeFdActorSpec {
	uint32_t oid;
	CeFdActorRole role;
	float x;
	float y;
	float z;
	float yaw;
	float pitch;
	float roll;
	int32_t allegiance;
	const char *definition;
	const char *name;
	int32_t playerNumber;
	int32_t planeMode;
};

struct CeFdWorldPlan {
	CeFlagshipDuelMode mode;
	int32_t levelNumber;
	int32_t mapNumber;
	int32_t baseLevel;
	int32_t worldType;
	const char *beConfigs[kMaxBeConfigs];
	size_t beConfigCount;
	CeFdActorSpec actors[kMaxActors];
	size_t actorCount;

	void PushConfig(const char *cfg) {
		if (beConfigCount < kMaxBeConfigs)
			beConfigs[beConfigCount++] = cfg;
	}
	void PushActor(const CeFdActorSpec &a) {
		if (actorCount < kMaxActors)
			actors[actorCount++] = a;
	}
};

inline CeFdWorldPlan CeFlagshipDuelBuildWorldPlan(CeFlagshipDuelMode mode) {
	CeFdWorldPlan plan = {};
	plan.mode = mode;
	plan.levelNumber = LevelForMode(mode);
	plan.mapNumber = kDonorMapId;
	plan.baseLevel = kDonorBaseWorldId;
	if (mode == CeFlagshipDuelMode::kSplitScreen2P) {
		plan.worldType = kWorldTypeVersusMultiplayer;
		for (size_t i = 0; i < kSplit2PBeConfigCount; ++i)
			plan.PushConfig(kSplit2PBeConfigs[i]);
	} else {
		plan.worldType = kWorldTypeSinglePlayer;
		plan.PushConfig("Standard");
	}

	// Player 1 Start: Forseti Battle Engine, airborne Jet mode off F.S. Venturer's
	// aft-starboard quarter, framing Venturer in the foreground and Fenrir downrange.
	plan.PushActor({
		kOidStart, kRolePlayerStart,
		228.0f, 478.0f, -52.0f,
		kPi, 0.0f, 0.0f,
		0, "", "ce.fd.player1",
		1, 1
	});

	if (mode == CeFlagshipDuelMode::kSplitScreen2P) {
		// Player 2 Start: Muspell Battle Engine, mirrored off Fenrir's aft-starboard
		// quarter, framing Fenrir in the foreground and F.S. Venturer downrange.
		plan.PushActor({
			kOidStart, kRolePlayerStart,
			284.0f, 42.0f, -52.0f,
			0.0f, 0.0f, 0.0f,
			1, "", "ce.fd.player2",
			2, 1
		});

		// 4 Respawn anchors (2 North flanking Venturer for P1, 2 South flanking Fenrir for P2).
		plan.PushActor({
			kOidSpawnPoint, kRolePlayerSpawnPoint,
			168.0f, 436.0f, -42.0f,
			-2.6179939f, 0.0f, 0.0f,
			0, "", "ce.fd.spawn.p1.w",
			1, 1
		});
		plan.PushActor({
			kOidSpawnPoint, kRolePlayerSpawnPoint,
			344.0f, 436.0f, -42.0f,
			2.6179939f, 0.0f, 0.0f,
			0, "", "ce.fd.spawn.p1.e",
			1, 1
		});
		plan.PushActor({
			kOidSpawnPoint, kRolePlayerSpawnPoint,
			168.0f, 84.0f, -42.0f,
			-0.5235988f, 0.0f, 0.0f,
			1, "", "ce.fd.spawn.p2.w",
			2, 1
		});
		plan.PushActor({
			kOidSpawnPoint, kRolePlayerSpawnPoint,
			344.0f, 84.0f, -42.0f,
			0.5235988f, 0.0f, 0.0f,
			1, "", "ce.fd.spawn.p2.e",
			2, 1
		});
	}

	// Forseti Flagship: F.S. Venturer ("Aircarrier") at (256, 420, -60), facing South (yaw = pi).
	plan.PushActor({
		kOidUnit, kRoleVenturer,
		kVenturerStartX, kVenturerStartY, kVenturerStartZ,
		kVenturerStartYaw, 0.0f, 0.0f,
		0, "Aircarrier", "ce.fd.venturer",
		0, 0
	});

	// Muspell Flagship: Fenrir ("Fenrir") at (256, 100, -60), facing North (yaw = 0).
	// Explicitly uses campaign "Fenrir" with all 7 destructible sub-components and fighter bays.
	plan.PushActor({
		kOidUnit, kRoleFenrir,
		kFenrirStartX, kFenrirStartY, kFenrirStartZ,
		kFenrirStartYaw, 0.0f, 0.0f,
		1, "Fenrir", "ce.fd.fenrir",
		0, 0
	});

	// Opening escort screens: 3 per side in 1P, 2 per side in 2P split-screen.
	plan.PushActor({
		kOidUnit, kRoleForsetiEscort,
		216.0f, 382.0f, -76.0f,
		kPi, 0.0f, 0.0f,
		0, "Jason Fighter", "ce.fd.wildcat.1",
		0, 0
	});
	plan.PushActor({
		kOidUnit, kRoleForsetiEscort,
		296.0f, 382.0f, -76.0f,
		kPi, 0.0f, 0.0f,
		0, "Billy Fighter", "ce.fd.wildcat.2",
		0, 0
	});
	if (mode == CeFlagshipDuelMode::kSolo1P) {
		plan.PushActor({
			kOidUnit, kRoleForsetiEscort,
			256.0f, 368.0f, -82.0f,
			kPi, 0.0f, 0.0f,
			0, "Tara Fighter", "ce.fd.wildcat.3",
			0, 0
		});
	}

	plan.PushActor({
		kOidUnit, kRoleMuspellEscort,
		216.0f, 138.0f, -76.0f,
		0.0f, 0.0f, 0.0f,
		1, "Muspell Advanced Fighter", "ce.fd.atf.1",
		0, 0
	});
	plan.PushActor({
		kOidUnit, kRoleMuspellEscort,
		296.0f, 138.0f, -76.0f,
		0.0f, 0.0f, 0.0f,
		1, "Muspell Advanced Fighter", "ce.fd.atf.2",
		0, 0
	});
	if (mode == CeFlagshipDuelMode::kSolo1P) {
		plan.PushActor({
			kOidUnit, kRoleMuspellEscort,
			256.0f, 152.0f, -82.0f,
			0.0f, 0.0f, 0.0f,
			1, "Muspell Advanced Fighter", "ce.fd.atf.3",
			0, 0
		});
	}

	return plan;
}

// Binary writer matching CWorld::Read (src/world.cpp) version 50 format:
//   - u16 version (50)
//   - SWorldHeader (u32 hdr=3, u32 unused0, u32 levelNo, u32 cfgCount, u8-prefixed cfg names,
//     u32 coop, u32 versus, u32 split, u32 worldType)
//   - u32 baseLevel (0xffffffff for base world, plan.baseLevel for real world)
//   - u32 scriptCount (0), u32 mapNumber (6), u32 namedMeshes (0)
//   - u16 thingCount + serialized CStart / CSpawnPoint / CUnit entries
//   - version-50 world tail (trees, influence map v1, paths, grading, occupancy, startTime)
struct CeFdWorldWriter {
	uint8_t *buf;
	unsigned cap;
	unsigned pos;
	bool overflow;

	void u8(uint8_t v) {
		if (pos + 1u > cap) { overflow = true; return; }
		buf[pos++] = v;
	}
	void u16(uint16_t v) {
		u8(uint8_t(v & 0xFFu));
		u8(uint8_t((v >> 8) & 0xFFu));
	}
	void u32(uint32_t v) {
		u8(uint8_t(v & 0xFFu));
		u8(uint8_t((v >> 8) & 0xFFu));
		u8(uint8_t((v >> 16) & 0xFFu));
		u8(uint8_t((v >> 24) & 0xFFu));
	}
	void f32(float v) {
		uint32_t bits = 0u;
		memcpy(&bits, &v, sizeof(bits));
		u32(bits);
	}
	void shortName(const char *s) {
		const size_t n = s ? strlen(s) : 0u;
		if (n > 255u) { overflow = true; return; }
		u8(static_cast<uint8_t>(n));
		for (size_t i = 0; i < n; ++i)
			u8(uint8_t(s[i]));
	}
	void zstr(const char *s) {
		const size_t n = s ? strlen(s) : 0u;
		for (size_t i = 0; i < n; ++i)
			u8(uint8_t(s[i]));
		u8(0u);
	}
	void initThing(const CeFdActorSpec &a) {
		f32(a.x); f32(a.y); f32(a.z);
		f32(a.yaw); f32(a.pitch); f32(a.roll);
		u32(0u);                       // mMeshNo
		u32(uint32_t(a.allegiance));   // mAllegiance
		u32(0xFFFFFFFFu);              // mTarget (-1)
		zstr("");                      // mScript (none)
		zstr(a.name ? a.name : "");    // mName
		zstr("");                      // mSpawnScript (none)
		u32(1u);                       // mActive = TRUE
		u32(0u);                       // mAttachScriptsToUnits = FALSE
	}
};

inline bool CeFlagshipDuelSerializeWorld(const CeFdWorldPlan &plan, bool real,
                                         uint8_t *outBuf, unsigned outCap, unsigned *outLen) {
	if (outLen) *outLen = 0u;
	if (!outBuf || !outLen || outCap < 128u) return false;
	if (plan.beConfigCount == 0u || plan.beConfigCount > kMaxBeConfigs) return false;
	if (real && plan.actorCount > kMaxActors) return false;

	CeFdWorldWriter w = {outBuf, outCap, 0u, false};

	w.u16(kWorldVersion);
	w.u32(kWorldHeaderVersion);
	w.u32(0u);
	w.u32(uint32_t(plan.levelNumber));
	w.u32(uint32_t(plan.beConfigCount));
	for (size_t i = 0; i < plan.beConfigCount; ++i) {
		if (!ValidPrintableName(plan.beConfigs[i])) return false;
		w.shortName(plan.beConfigs[i]);
	}

	if (plan.worldType == kWorldTypeVersusMultiplayer) {
		w.u32(0u); // cooperative = 0
		w.u32(1u); // versus = 1
		w.u32(1u); // split = 1
		w.u32(uint32_t(kWorldTypeVersusMultiplayer));
	} else {
		w.u32(0u);
		w.u32(0u);
		w.u32(0u);
		w.u32(uint32_t(kWorldTypeSinglePlayer));
	}

	w.u32(real ? uint32_t(plan.baseLevel) : 0xFFFFFFFFu);
	w.u32(0u); // zero mission scripts
	w.u32(uint32_t(plan.mapNumber));
	w.u32(0u); // zero named meshes

	const uint16_t thingCount = real ? static_cast<uint16_t>(plan.actorCount) : 0u;
	w.u16(thingCount);
	if (real) {
		for (size_t i = 0; i < plan.actorCount; ++i) {
			const CeFdActorSpec &a = plan.actors[i];
			w.u32(a.oid);
			w.initThing(a);
			if (a.oid == kOidStart || a.oid == kOidSpawnPoint) {
				w.u32(uint32_t(a.planeMode));
				w.u32(uint32_t(a.playerNumber));
			} else if (a.oid == kOidUnit) {
				if (!ValidPrintableName(a.definition)) return false;
				w.shortName(a.definition);
				w.u32(0u); // dies_on = 0
			} else {
				return false;
			}
		}
	}

	w.u16(0u); w.u32(0u);                            // zero tree groups & tree types
	w.u16(1u); w.u32(0u); w.u32(0u);                 // empty influence map v1
	w.u16(0u);                                       // zero waypoint paths
	for (unsigned i = 0; i < 5u; ++i) w.f32(0.0f);   // grading times
	w.u32(0u); w.u32(0u);                            // grading counts
	for (unsigned i = 0; i < 4u; ++i) w.f32(0.0f);   // trailing floats
	for (unsigned i = 0; i < 3u; ++i) w.u32(0u);     // zero occupancy grids
	w.f32(0.0f);                                     // start time = 0

	if (w.overflow) return false;
	*outLen = w.pos;
	return true;
}

struct CeFdWorldSummary {
	bool valid;
	uint16_t worldVersion;
	uint32_t headerVersion;
	uint32_t levelNumber;
	int32_t worldType;
	int32_t mapNumber;
	int32_t baseLevel;
	uint16_t thingCount;
	size_t startCount;
	size_t spawnPointCount;
	size_t unitCount;
	bool sawPlayer1Start;
	bool sawPlayer2Start;
	bool hasVenturer;
	bool hasFenrir;
	bool hasForbiddenFenrirMp;
	float venturerX, venturerY, venturerZ, venturerYaw;
	float fenrirX, fenrirY, fenrirZ, fenrirYaw;
	char beConfigs[kMaxBeConfigs][kMaxShortNameBuf];
	size_t beConfigCount;
	char unitDefs[kMaxActors][kMaxShortNameBuf];
	size_t unitDefCount;
};

class CeFdByteReader {
public:
	CeFdByteReader(const uint8_t *data, size_t len)
		: mData(data), mLen(len), mPos(0), mOk(true) {}

	bool Ok() const { return mOk; }
	size_t Remaining() const { return (mPos <= mLen) ? (mLen - mPos) : 0u; }

	uint8_t ReadU8() {
		if (mPos + 1u > mLen) { mOk = false; return 0u; }
		return mData[mPos++];
	}
	uint16_t ReadU16() {
		const uint16_t lo = ReadU8();
		const uint16_t hi = ReadU8();
		return static_cast<uint16_t>(lo | (hi << 8));
	}
	uint32_t ReadU32() {
		const uint32_t b0 = ReadU8(), b1 = ReadU8(), b2 = ReadU8(), b3 = ReadU8();
		return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
	}
	int32_t ReadS32() { return int32_t(ReadU32()); }
	float ReadF32() {
		const uint32_t bits = ReadU32();
		float v = 0.0f;
		memcpy(&v, &bits, sizeof(v));
		return v;
	}
	bool ReadShortName(char *outBuf, size_t outCap) {
		if (!outBuf || outCap == 0u) { mOk = false; return false; }
		outBuf[0] = '\0';
		const uint8_t n = ReadU8();
		if (!mOk || mPos + n > mLen || size_t(n) + 1u > outCap) {
			mOk = false;
			return false;
		}
		if (n > 0u) memcpy(outBuf, mData + mPos, n);
		outBuf[n] = '\0';
		mPos += n;
		return true;
	}
	bool SkipZStr() {
		while (mPos < mLen) {
			if (mData[mPos++] == 0u) return true;
		}
		mOk = false;
		return false;
	}
	void Skip(size_t n) {
		if (mPos + n > mLen) { mOk = false; return; }
		mPos += n;
	}

private:
	const uint8_t *mData;
	size_t mLen;
	size_t mPos;
	bool mOk;
};

inline CeFdWorldSummary CeFlagshipDuelInspectWorld(const uint8_t *bytes, size_t len, bool expectReal = true) {
	CeFdWorldSummary s = {};
	s.valid = false;
	if (!bytes || len < 64u || len > kMaxWorldBodyBytes) return s;

	CeFdByteReader r(bytes, len);
	s.worldVersion = r.ReadU16();
	if (s.worldVersion != kWorldVersion) return s;

	s.headerVersion = r.ReadU32();
	if (s.headerVersion != kWorldHeaderVersion) return s;
	(void)r.ReadU32();
	s.levelNumber = r.ReadU32();
	const uint32_t numConfigs = r.ReadU32();
	if (numConfigs == 0u || numConfigs > kMaxBeConfigs) return s;
	for (uint32_t i = 0; i < numConfigs; ++i) {
		if (!r.ReadShortName(s.beConfigs[s.beConfigCount], kMaxShortNameBuf))
			return s;
		++s.beConfigCount;
	}
	r.Skip(12u); // coop, versus, split
	s.worldType = r.ReadS32();

	s.baseLevel = r.ReadS32();
	const uint32_t scriptCount = r.ReadU32();
	s.mapNumber = r.ReadS32();
	const uint32_t namedMeshes = r.ReadU32();
	if (scriptCount != 0u || namedMeshes != 0u) return s;

	s.thingCount = r.ReadU16();
	if (s.thingCount > kMaxActors) return s;
	for (uint16_t i = 0; i < s.thingCount; ++i) {
		const uint32_t oid = r.ReadU32();
		const float posX = r.ReadF32();
		const float posY = r.ReadF32();
		const float posZ = r.ReadF32();
		const float yaw = r.ReadF32();
		(void)r.ReadF32(); // pitch
		(void)r.ReadF32(); // roll
		(void)r.ReadU32(); // meshNo
		(void)r.ReadU32(); // allegiance
		(void)r.ReadU32(); // target
		if (!r.SkipZStr() || !r.SkipZStr() || !r.SkipZStr()) return s;
		r.Skip(8u); // active, attachScripts

		if (oid == kOidStart) {
			const int32_t planeMode = r.ReadS32();
			const int32_t playerNo = r.ReadS32();
			if (planeMode != 1 || posZ > -kMinAirborneClearanceAboveWater) return s;
			if (playerNo == 1) s.sawPlayer1Start = true;
			if (playerNo == 2) s.sawPlayer2Start = true;
			++s.startCount;
		} else if (oid == kOidSpawnPoint) {
			const int32_t planeMode = r.ReadS32();
			const int32_t playerNo = r.ReadS32();
			if (planeMode != 1 || (playerNo != 1 && playerNo != 2)) return s;
			if (posZ > -kMinAirborneClearanceAboveWater) return s;
			++s.spawnPointCount;
		} else if (oid == kOidUnit) {
			char def[kMaxShortNameBuf] = {};
			if (!r.ReadShortName(def, sizeof(def))) return s;
			(void)r.ReadU32(); // dies_on
			++s.unitCount;
			if (s.unitDefCount < kMaxActors) {
				memcpy(s.unitDefs[s.unitDefCount], def, sizeof(def));
				++s.unitDefCount;
			}
			if (StrEqualsIgnoreCase(def, "Aircarrier")) {
				s.hasVenturer = true;
				s.venturerX = posX;
				s.venturerY = posY;
				s.venturerZ = posZ;
				s.venturerYaw = yaw;
			}
			if (StrEqualsIgnoreCase(def, "Fenrir")) {
				s.hasFenrir = true;
				s.fenrirX = posX;
				s.fenrirY = posY;
				s.fenrirZ = posZ;
				s.fenrirYaw = yaw;
			}
			if (IsForbiddenUnit(def))
				s.hasForbiddenFenrirMp = true;
		} else {
			return s;
		}
	}

	// Version-50 trailing block (2+4 + 2+4+4 + 2 + 20 + 8 + 16 + 12 + 4 = 78 bytes).
	r.Skip(78u);
	if (!r.Ok() || r.Remaining() != 0u) return s;

	if (!expectReal) {
		s.valid = (s.baseLevel == -1) && (s.thingCount == 0u);
		return s;
	}

	const float dx = s.venturerX - s.fenrirX;
	const float dy = s.venturerY - s.fenrirY;
	const float sep2D = sqrtf(dx * dx + dy * dy);
	const bool startsValid = (s.worldType == kWorldTypeVersusMultiplayer)
		? (s.startCount == 2u && s.sawPlayer1Start && s.sawPlayer2Start && s.spawnPointCount >= 4u)
		: (s.startCount == 1u && s.sawPlayer1Start && !s.sawPlayer2Start);

	s.valid = startsValid && s.hasVenturer && s.hasFenrir && !s.hasForbiddenFenrirMp &&
	          (sep2D >= kMinOpeningSeparation2D) &&
	          (s.venturerZ <= -kMinAirborneClearanceAboveWater) &&
	          (s.fenrirZ <= -kMinAirborneClearanceAboveWater);
	return s;
}

// Synthesizes the 325-byte PMSH/PMS2 chunk header that CMesh::Deserialize reads
// before loading `data\resources\meshes\m_<meshName>.aya`.
inline bool BuildMeshStubChunk(const char *meshName, int32_t runtimeId, bool keepBuckets,
                               uint8_t *outBuf, unsigned outCap, unsigned *outSize) {
	if (!outBuf || !outSize || outCap < 325u || !ValidPrintableName(meshName)) return false;
	const unsigned nameLen = static_cast<unsigned>(strlen(meshName));
	if (nameLen >= 300u) return false;
	memset(outBuf, 0, 325u);
	outBuf[0] = 'P'; outBuf[1] = 'M'; outBuf[2] = 'S'; outBuf[3] = 'H';
	const uint32_t outerSize = 317u;
	memcpy(outBuf + 4, &outerSize, 4);
	outBuf[8] = 'P'; outBuf[9] = 'M'; outBuf[10] = 'S'; outBuf[11] = '2';
	const uint32_t innerSize = 309u;
	memcpy(outBuf + 12, &innerSize, 4);
	memcpy(outBuf + 16, meshName, nameLen);
	memcpy(outBuf + 316, &runtimeId, 4);
	const int32_t optionalFlag = 0;
	memcpy(outBuf + 320, &optionalFlag, 4);
	outBuf[324] = keepBuckets ? 1u : 0u;
	*outSize = 325u;
	return true;
}

inline bool BuildTextureDiskPath(const char *texName, char *outPath, size_t outSize) {
	if (!texName || !texName[0] || !outPath || outSize < 64u) return false;
	char mangled[160] = {};
	size_t i = 0u;
	for (; texName[i] && i + 1u < sizeof(mangled); ++i)
		mangled[i] = (texName[i] == '\\') ? '%' : texName[i];
	mangled[i] = '\0';
	snprintf(outPath, outSize, "data\\resources\\dxtntextures\\%s(0)A1R5G5B5.aya", mangled);
	return true;
}

// ============================================================================
// Retail PC Asset Closure & Strict Preflight Matrix (Donor: 857_res_PC.aya)
// ============================================================================

// Textures absent from 857_res_PC.aya (plus Venturer safety checks) preloaded
// from `data\resources\dxtntextures\meshtex%<name>(0)A1R5G5B5.aya`.
static constexpr const char *kCeFdPreloadTextures[] = {
	"meshtex\\BOSSfenrir-4PAGE.tga",
	"meshtex\\BOSSfenrir-3PAGE.tga",
	"meshtex\\BOSSfenrir-FRONT.tga",
	"meshtex\\BOSSfenrir-int.tga",
	"meshtex\\M_atf.tga",
	"meshtex\\f_ventura01.tga",
	"meshtex\\f_ventura02.tga",
	"meshtex\\f_ventura03.tga",
	"meshtex\\F_Ventura_Gun_Texture_512.tga",
	"meshtex\\FA_XF24.tga",
};
static constexpr size_t kCeFdRequiredTextureCount =
	sizeof(kCeFdPreloadTextures) / sizeof(kCeFdPreloadTextures[0]);

// Standalone retail meshes preloaded via BuildMeshStubChunk + CMesh::Deserialize
// from `data\resources\meshes\m_<meshName>.aya` (skipped when already loaded by 857).
static constexpr const char *kCeFdPreloadMeshes[] = {
	// Forseti Aircarrier + deck pad + AA guns + carrier F24 fighter
	"f_ventura.msh",
	"f_ventura_hpad.msh",
	"f_ventura_gun.msh",
	"fa_f24.msh",
	// Muspell Fenrir + all 7 sub-components + interior wreckage
	"boss-fenrir.msh",
	"m_fbossgt.msh",
	"m_fbossmg.msh",
	"m_fbosspe.msh",
	"m_fbossbe.msh",
	"m_fbossme.msh",
	"m_fbossmea.msh",
	"m_fbossbd.msh",
	"m_innerwreckage.msh",
	// Forseti Wildcat escort variants
	"fa_wildcard_j.msh",
	"fa_wildcard_b.msh",
	"fa_wildcard_t.msh",
	// Muspell Advanced Fighter escort
	"m_atf.msh",
};
static constexpr size_t kCeFdRequiredMeshCount =
	sizeof(kCeFdPreloadMeshes) / sizeof(kCeFdPreloadMeshes[0]);

static constexpr const char *kCeFdExpectedDonorBeMeshes[] = {
	"f_be1.msh",
	"f_be2.msh",
	"m_be1.msh",
	"m_be2.msh",
	"cockpit2.msh",
	"cockpit2b.msh",
};
static constexpr size_t kCeFdExpectedDonorBeMeshCount =
	sizeof(kCeFdExpectedDonorBeMeshes) / sizeof(kCeFdExpectedDonorBeMeshes[0]);

struct CeFdSupportMeshSpec {
	const char *mesh;
	const char *texture;
};

static constexpr CeFdSupportMeshSpec kCeFdSupportMeshes[] = {
	{"m_rocket.msh", "meshtex\\m_rocket.TGA"},
	{"fa_wildcard_t_destroyed.msh", NULL},
};
static constexpr size_t kCeFdSupportMeshCount =
	sizeof(kCeFdSupportMeshes) / sizeof(kCeFdSupportMeshes[0]);

static constexpr const char *kCeFdRequiredUnitDefs[] = {
	"Aircarrier",
	"Fenrir",
	"Jason Fighter",
	"Billy Fighter",
	"Tara Fighter",
	"Muspell Advanced Fighter",
};
static constexpr size_t kCeFdRequiredUnitDefCount =
	sizeof(kCeFdRequiredUnitDefs) / sizeof(kCeFdRequiredUnitDefs[0]);

enum class CeFdPreflightStatus : uint8_t {
	kOk = 0,
	kMenusOff = 1,
	kAnotherSessionOpen = 2,
	kMissingDonorArchive = 3,
	kMissingCarrierMesh = 4,
	kMissingCarrierTexture = 5,
	kMissingUnitDefinition = 6,
	kForbiddenFenrirMultiplayer = 7,
};

struct CeFdPreflightInput {
	bool ceMenusEnabled;
	bool anotherSessionOpen;
	bool hasDonorArchive857;
	bool hasAllRequiredMeshes;
	bool hasAllRequiredTextures;
	bool hasAllRequiredUnitDefs;
	bool usesFenrirMultiplayer;
};

struct CeFdPreflightVerdict {
	CeFdPreflightStatus status;
	bool allowed;
	const char *message;
};

inline CeFdPreflightVerdict CeFlagshipDuelEvaluatePreflight(const CeFdPreflightInput &in) {
	if (!in.ceMenusEnabled) {
		return {CeFdPreflightStatus::kMenusOff, false, ""};
	}
	if (in.anotherSessionOpen) {
		return {
			CeFdPreflightStatus::kAnotherSessionOpen,
			false,
			"Close any active mod, online game, museum visit or editor session before starting Flagship Duel."
		};
	}
	if (in.usesFenrirMultiplayer) {
		return {
			CeFdPreflightStatus::kForbiddenFenrirMultiplayer,
			false,
			"Flagship Duel refused broken Fenrir Multiplayer unit data. Your career is unchanged."
		};
	}
	if (!in.hasDonorArchive857) {
		return {
			CeFdPreflightStatus::kMissingDonorArchive,
			false,
			"Flagship Duel requires retail level 857 resources (857-res-PC.aya) in your game folder."
		};
	}
	if (!in.hasAllRequiredMeshes) {
		return {
			CeFdPreflightStatus::kMissingCarrierMesh,
			false,
			"Flagship Duel could not find required retail Venturer or Fenrir model files in your game folder."
		};
	}
	if (!in.hasAllRequiredTextures) {
		return {
			CeFdPreflightStatus::kMissingCarrierTexture,
			false,
			"Flagship Duel could not find required retail Venturer or Fenrir texture files in your game folder."
		};
	}
	if (!in.hasAllRequiredUnitDefs) {
		return {
			CeFdPreflightStatus::kMissingUnitDefinition,
			false,
			"Flagship Duel could not find required flagship or fighter definitions in physics data."
		};
	}
	return {CeFdPreflightStatus::kOk, true, ""};
}

} // namespace CeFlagshipDuelWorld
