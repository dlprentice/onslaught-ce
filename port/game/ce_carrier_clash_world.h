// SPDX-License-Identifier: GPL-3.0-only
// Clean-room procedural ocean arena heightfield, authored version-50 world body
// serializer, and strict retail asset preflight rules for Carrier Clash.
// Pure C++14 header: no engine headers and no host STL allocations so both the
// game build and standalone ASan/UBSan host tests share identical logic.
#ifndef CE_CARRIER_CLASH_WORLD_H
#define CE_CARRIER_CLASH_WORLD_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace CeCarrierClashWorld {

// Archive level 742 ships Fenrir, its 7 sub-components, FSV Marshall, Wildcat
// wings, Muspell Advanced Fighters, Aquila Battle Engine, and ocean textures.
// Clean-room world/map IDs 977 never collide with retail campaign worlds.
enum {
	kArchiveLevel = 742,
	kBaseWorldId = 977,
	kMapId = 977,
	kWorldVersion = 50,
	kGridShift = 9,
	kCells = 1 << kGridShift,           // 512 x 512 m arena
	kPatchCells = 8,
	kPatchSamples = kPatchCells + 1,    // 9 x 9 seam-duplicated patch samples
	kPatches = kCells / kPatchCells,    // 64 x 64 patches
	kHeightSamples = kPatches * kPatches * kPatchSamples * kPatchSamples,
	kMaxActors = 32,
	kMaxWorldBodyBytes = 65536,
	kNameLimit = 127,
	kOidUnit = 8,                       // matches OID_CUnit in src/oids.h
	kOidStart = 15,                     // matches OID_CStart in src/oids.h
	kOidSquad = 28,                     // matches OID_CSquad in src/oids.h
	kAllegianceForseti = 0,             // matches kForsetiAllegiance
	kAllegianceMuspell = 1,             // matches kMuspellAllegiance
	kSkyCubeIndex = 4,                  // maritime horizon sky cube packed in 742_res_PC.aya
	kWaterSetIndex = 0,                 // retail ocean water caustics/reflections packed in 742_res_PC.aya
};

// Z counts downward in engine world space (Z < kWaterLevel is above sea level).
constexpr float kHeightScale = 1.0f / 512.0f;  // int16 [-28160, +28160] -> [-55 m, +55 m]
constexpr int16_t kMaxRawSample = 28160;       // 55.0 m * 512
constexpr float kMinZBound = -55.0f;
constexpr float kMaxZBound = 55.0f;
constexpr float kWaterLevel = -10.0f;          // ocean plane
constexpr float kDeepSeaFloorZ = 6.5f;         // 16.5 m deep naval corridor seabed
constexpr float kBastionX = 258.0f;            // player coastal launch headland centre
constexpr float kBastionY = 144.0f;
constexpr float kBastionTopZ = -17.0f;         // 7.0 m above waterline

// Canonical retail PC unit and mesh identifiers (never Fenrir Multiplayer).
constexpr const char *kPlayerConfig = "Standard";
constexpr const char *kVenturerUnit = "Aircarrier";
constexpr const char *kFenrirUnit = "Fenrir";
constexpr const char *kFenrirAltUnit = "Fenrir 2";
constexpr const char *kEscortCruiserUnit = "FSV Marshall";
constexpr const char *kWildcatJasonUnit = "Jason Fighter";
constexpr const char *kWildcatBillyUnit = "Billy Fighter";
constexpr const char *kWildcatTaraUnit = "Tara Fighter";
constexpr const char *kMuspellFighterUnit = "Muspell Advanced Fighter";
constexpr const char *kForbiddenFenrirUnit = "Fenrir Multiplayer";

constexpr unsigned kRequiredUnitCount = 7u;
inline const char *RequiredUnitName(unsigned index) {
	static const char *const kNames[kRequiredUnitCount] = {
		kVenturerUnit,
		kFenrirUnit,
		kEscortCruiserUnit,
		kWildcatJasonUnit,
		kWildcatBillyUnit,
		kWildcatTaraUnit,
		kMuspellFighterUnit,
	};
	return index < kRequiredUnitCount ? kNames[index] : nullptr;
}

constexpr unsigned kPreloadMeshCount = 4u;
inline const char *PreloadMeshName(unsigned index) {
	static const char *const kMeshes[kPreloadMeshCount] = {
		"f_ventura.msh",
		"f_ventura_gun.msh",
		"fa_f24.msh",
		"m_fbossmg.msh",
	};
	return index < kPreloadMeshCount ? kMeshes[index] : nullptr;
}

constexpr unsigned kPreloadTextureCount = 5u;
inline const char *PreloadTextureName(unsigned index) {
	static const char *const kTextures[kPreloadTextureCount] = {
		"meshtex\\f_ventura01.tga",
		"meshtex\\f_ventura02.tga",
		"meshtex\\f_ventura03.tga",
		"meshtex\\F_Ventura_Gun_Texture_512.tga",
		"meshtex\\FA_XF24.tga",
	};
	return index < kPreloadTextureCount ? kTextures[index] : nullptr;
}

inline int AsciiLower(int c) {
	return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline bool StrEqualsIgnoreCase(const char *a, const char *b) {
	if (!a || !b) return false;
	while (*a && *b) {
		if (AsciiLower(static_cast<unsigned char>(*a)) !=
		    AsciiLower(static_cast<unsigned char>(*b)))
			return false;
		++a;
		++b;
	}
	return *a == '\0' && *b == '\0';
}

inline bool ValidPrintableName(const char *text, unsigned *outLen = nullptr) {
	if (!text) return false;
	unsigned n = 0;
	while (n <= kNameLimit && text[n]) {
		const unsigned char c = static_cast<unsigned char>(text[n]);
		if (c < 32u || c >= 127u) return false;
		++n;
	}
	if (n == 0u || n > kNameLimit) return false;
	if (outLen) *outLen = n;
	return true;
}

// Explicitly refuses the nonexistent retail PC unit "Fenrir Multiplayer" as well
// as malformed or empty names.
inline bool IsForbiddenUnit(const char *unitName) {
	if (!ValidPrintableName(unitName)) return true;
	if (StrEqualsIgnoreCase(unitName, kForbiddenFenrirUnit)) return true;
	return false;
}

inline bool IsAllowedUnitName(const char *unitName) {
	return !IsForbiddenUnit(unitName);
}

// ---------------------------------------------------------------------------
// Clean-room procedural 512 x 512 m maritime battleground heightfield.
// Wide deep-water naval corridor through the centre framed by craggy rocky
// sea-stacks/islets around the perimeter plus a flat coastal launch bastion
// for the player Battle Engine on the South-West flank.
// ---------------------------------------------------------------------------
struct SeaStack {
	float cx, cy;
	float radiusX, radiusY;
	float heightAboveSea;  // metres above kWaterLevel (positive lifts toward -Z)
	float flatTop;         // [0..1] plateau flattening factor
	float cragAmp;         // harmonic ridge relief in metres
};

inline float HeightAt(float x, float y) {
	if (!(x >= 0.0f && x <= static_cast<float>(kCells)) ||
	    !(y >= 0.0f && y <= static_cast<float>(kCells))) {
		return kDeepSeaFloorZ;
	}

	// Gently undulating seabed beneath the deep naval corridor.
	const float swell =
		1.1f * sinf(x * 0.031f + y * 0.019f) +
		0.7f * cosf(x * 0.053f - y * 0.041f);
	float z = kDeepSeaFloorZ + swell;

	// Framing rocky sea-stacks and asymmetric basalt headlands surrounding the
	// ~280 m central ocean strait. Every peak keeps well inside [-54 m, +14 m]
	// and leaves the central corridor (x in [180..340], y in [185..345]) wide open
	// while framing an open sunlit sea horizon between distant headland clusters.
	static const SeaStack kStacks[] = {
		// West basalt cliff cluster (layered headland + outer sea-stack)
		{ 76.0f, 224.0f, 52.0f, 36.0f, 38.0f, 0.22f, 5.2f},
		{108.0f, 256.0f, 28.0f, 24.0f, 23.0f, 0.14f, 3.8f},
		{ 88.0f, 376.0f, 46.0f, 32.0f, 33.0f, 0.18f, 4.4f},
		// Distant North-West & North-East archipelago framing open center horizon
		{146.0f, 444.0f, 54.0f, 30.0f, 31.0f, 0.20f, 4.6f},
		{384.0f, 448.0f, 58.0f, 34.0f, 36.0f, 0.22f, 5.0f},
		{422.0f, 412.0f, 32.0f, 26.0f, 21.0f, 0.16f, 3.4f},
		// East craggy archipelago
		{448.0f, 298.0f, 46.0f, 44.0f, 41.0f, 0.16f, 5.0f},
		{416.0f, 216.0f, 34.0f, 30.0f, 27.0f, 0.18f, 4.0f},
		// South-West & South-East outer shoal crags behind the launch headland
		{132.0f,  96.0f, 42.0f, 30.0f, 28.0f, 0.18f, 3.6f},
		{382.0f,  94.0f, 44.0f, 32.0f, 26.0f, 0.20f, 3.5f},
	};

	const unsigned stackCount = static_cast<unsigned>(sizeof(kStacks) / sizeof(kStacks[0]));
	for (unsigned i = 0; i < stackCount; ++i) {
		const SeaStack &s = kStacks[i];
		const float dx = (x - s.cx) / s.radiusX;
		const float dy = (y - s.cy) / s.radiusY;
		const float r2 = dx * dx + dy * dy;
		if (r2 >= 4.5f) continue;
		const float env = expf(-r2 * 1.15f);
		float lift = env * (s.heightAboveSea + (kDeepSeaFloorZ - kWaterLevel));
		if (s.flatTop > 0.0f && env > (1.0f - s.flatTop)) {
			const float cap = (1.0f - s.flatTop) * (s.heightAboveSea + (kDeepSeaFloorZ - kWaterLevel));
			if (lift > cap) lift = cap + (lift - cap) * 0.25f;
		}
		const float crag =
			s.cragAmp * env *
			(0.6f * sinf(x * 0.19f + y * 0.13f + float(i) * 1.7f) +
			 0.4f * cosf(x * 0.27f - y * 0.23f - float(i) * 0.9f));
		const float stackZ = kDeepSeaFloorZ - lift + crag;
		if (stackZ < z) z = stackZ;
	}

	// Player coastal launch headland: compact rocky overlook at (258, 144), Z = -17.0 m
	// (7.0 m above the -10.0 m waterline), dropping cleanly into the open bay ahead.
	{
		const float dx = x - kBastionX;
		const float dy = y - kBastionY;
		const float dist = sqrtf(dx * dx + dy * dy);
		const float flatR = 9.5f;
		const float slopeR = 23.0f;
		if (dist < slopeR) {
			float t = 0.0f;
			if (dist <= flatR) {
				t = 1.0f;
			} else {
				const float u = (dist - flatR) / (slopeR - flatR);
				// Smooth cubic Hermite falloff from plateau edge down into ocean floor
				t = 1.0f - u * u * (3.0f - 2.0f * u);
			}
			const float rimNoise = (dist > flatR)
				? (1.2f * t * (1.0f - t) * sinf(x * 0.29f + y * 0.23f))
				: 0.0f;
			const float bastionZ = kDeepSeaFloorZ + (kBastionTopZ - kDeepSeaFloorZ) * t + rimNoise;
			if (bastionZ < z) z = bastionZ;
		}
	}

	// Strict +/- 55 m heightfield clamp (retail CDXLandscape clamps tiles at +/- 60 m).
	if (z < kMinZBound + 1.0f) z = kMinZBound + 1.0f;
	if (z > kMaxZBound - 1.0f) z = kMaxZBound - 1.0f;
	return z;
}

inline int16_t QuantizeHeight(float z) {
	if (!(z >= kMinZBound && z <= kMaxZBound)) return 0;
	const float scaled = z / kHeightScale;
	int32_t raw = static_cast<int32_t>(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
	if (raw < -kMaxRawSample) raw = -kMaxRawSample;
	if (raw > kMaxRawSample) raw = kMaxRawSample;
	return static_cast<int16_t>(raw);
}

// Fills all 64 * 64 * 81 patch samples in retail CHeightField::Load order:
//   index = ((tx * 64 + ty) * 9 + sy) * 9 + sx
// Exact seam continuity is guaranteed because adjacent tiles evaluate HeightAt
// at identical integer world coordinates (tx*8 + 8 == (tx+1)*8 + 0).
inline void FillHeightSamples(int16_t *outSamples) {
	if (!outSamples) return;
	for (int tx = 0; tx < kPatches; ++tx) {
		for (int ty = 0; ty < kPatches; ++ty) {
			int16_t *patch = outSamples + ((tx * kPatches + ty) * kPatchSamples * kPatchSamples);
			for (int sy = 0; sy < kPatchSamples; ++sy) {
				const float wy = static_cast<float>(ty * kPatchCells + sy);
				for (int sx = 0; sx < kPatchSamples; ++sx) {
					const float wx = static_cast<float>(tx * kPatchCells + sx);
					patch[sy * kPatchSamples + sx] = QuantizeHeight(HeightAt(wx, wy));
				}
			}
		}
	}
}

// Computes unit-normal lighting value in [14..62] for MIXERMAP.mMap[y * 512 + x].
inline uint8_t ComputeCellLight(int x, int y, float sunX, float sunY, float sunZ) {
	const float x0 = static_cast<float>(x > 0 ? x - 1 : 0);
	const float x1 = static_cast<float>(x < kCells - 1 ? x + 1 : kCells - 1);
	const float y0 = static_cast<float>(y > 0 ? y - 1 : 0);
	const float y1 = static_cast<float>(y < kCells - 1 ? y + 1 : kCells - 1);
	const float dzdx = (HeightAt(x1, static_cast<float>(y)) - HeightAt(x0, static_cast<float>(y))) /
	                   ((x1 - x0) > 0.5f ? (x1 - x0) : 1.0f);
	const float dzdy = (HeightAt(static_cast<float>(x), y1) - HeightAt(static_cast<float>(x), y0)) /
	                   ((y1 - y0) > 0.5f ? (y1 - y0) : 1.0f);
	// Upward surface normal in Z-down space is (+dzdx, +dzdy, -1).
	float nx = dzdx, ny = dzdy, nz = -1.0f;
	const float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
	if (nlen > 1e-4f) { nx /= nlen; ny /= nlen; nz /= nlen; }
	float slen = sqrtf(sunX * sunX + sunY * sunY + sunZ * sunZ);
	float lx = sunX, ly = sunY, lz = sunZ;
	if (slen > 1e-4f) { lx /= slen; ly /= slen; lz /= slen; }
	float ndotl = nx * lx + ny * ly + nz * lz;
	if (ndotl < 0.0f) ndotl = 0.0f;
	if (ndotl > 1.0f) ndotl = 1.0f;
	const int shade = static_cast<int>(18.0f + ndotl * 44.0f + 0.5f);
	if (shade < 14) return 14u;
	if (shade > 62) return 62u;
	return static_cast<uint8_t>(shade);
}

// ---------------------------------------------------------------------------
// Clean-room authored version-50 WORLD body serializer (zero heap allocation,
// zero retail scripts, zero copied campaign actors, zero Fenrir Multiplayer).
// ---------------------------------------------------------------------------
struct UnitSpawnSpec {
	const char *actorName;   // unique clean-room label (e.g. "ce.cc.venturer")
	const char *unitDef;     // retail physics unit definition name
	int allegiance;          // kAllegianceForseti or kAllegianceMuspell
	float position[3];       // X, Y, Z (Z counts downward)
	float orientation[3];    // yaw, pitch, roll (radians)
};

struct WorldPlan {
	const char *playerConfig;
	int baseLevel;
	int mapNumber;
	float startPosition[3];
	float startOrientation[3];
	bool startInJetMode;
	const UnitSpawnSpec *units;
	unsigned unitCount;
};

// Default clean-room Carrier Clash order of battle:
//   - Player Aquila Battle Engine on South coastal overlook facing North
//   - F.S. Venturer (Aircarrier) on West flank + 2 FSV Marshall cruisers + 3 Wildcats
//   - Fenrir (Fenrir) on East flank facing Venturer + 4 Muspell Advanced Fighters
inline unsigned BuildDefaultOrderOfBattle(UnitSpawnSpec *outUnits, unsigned capacity,
                                          bool useFenrirAlt = false) {
	if (!outUnits || capacity < 11u) return 0u;
	const char *fenrirDef = useFenrirAlt ? kFenrirAltUnit : kFenrirUnit;
	// Yaw convention: forward is +Y, yaw turns +Y toward (-sin(yaw), cos(yaw)).
	// East-North-East heading => yaw ~ -1.25 rad; West-North-West => yaw ~ +1.35 rad.
	static constexpr float kYawENE = -1.25f;
	static constexpr float kYawWNW =  1.35f;
	unsigned n = 0u;
	// Capital flagships framed cleanly left-vs-right across the open strait
	outUnits[n++] = {"ce.cc.venturer", kVenturerUnit, kAllegianceForseti,
	                 {216.0f, 236.0f, -32.0f}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.fenrir", fenrirDef, kAllegianceMuspell,
	                 {304.0f, 240.0f, -36.0f}, {kYawWNW, 0.0f, 0.0f}};
	// Forseti naval cruisers (FSV Marshall) flanking Venturer in the deep channel
	outUnits[n++] = {"ce.cc.marshall.1", kEscortCruiserUnit, kAllegianceForseti,
	                 {208.0f, 198.0f, kWaterLevel}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.marshall.2", kEscortCruiserUnit, kAllegianceForseti,
	                 {242.0f, 268.0f, kWaterLevel}, {-1.45f, 0.0f, 0.0f}};
	// Forseti Wildcat air patrol orbiting above Venturer
	outUnits[n++] = {"ce.cc.wildcat.1", kWildcatJasonUnit, kAllegianceForseti,
	                 {212.0f, 224.0f, -48.0f}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.wildcat.2", kWildcatBillyUnit, kAllegianceForseti,
	                 {236.0f, 218.0f, -50.0f}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.wildcat.3", kWildcatTaraUnit, kAllegianceForseti,
	                 {226.0f, 250.0f, -54.0f}, {kYawENE, 0.0f, 0.0f}};
	// Muspell Advanced Fighter screen ahead of Fenrir
	outUnits[n++] = {"ce.cc.atf.1", kMuspellFighterUnit, kAllegianceMuspell,
	                 {286.0f, 226.0f, -48.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.2", kMuspellFighterUnit, kAllegianceMuspell,
	                 {296.0f, 252.0f, -52.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.3", kMuspellFighterUnit, kAllegianceMuspell,
	                 {314.0f, 232.0f, -50.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.4", kMuspellFighterUnit, kAllegianceMuspell,
	                 {282.0f, 246.0f, -56.0f}, {kYawWNW, 0.0f, 0.0f}};
	return n;
}

inline WorldPlan BuildDefaultWorldPlan(const UnitSpawnSpec *units, unsigned unitCount) {
	WorldPlan plan = {};
	plan.playerConfig = kPlayerConfig;
	plan.baseLevel = kBaseWorldId;
	plan.mapNumber = kMapId;
	// Position player near the northern overlook edge of the rocky headland,
	// looking straight North (+Y) across the shimmering ocean strait at both flagships.
	plan.startPosition[0] = kBastionX;
	plan.startPosition[1] = kBastionY + 5.5f;
	plan.startPosition[2] = kBastionTopZ;
	plan.startOrientation[0] = 0.0f;
	plan.startOrientation[1] = 0.0f;
	plan.startOrientation[2] = 0.0f;
	plan.startInJetMode = false;
	plan.units = units;
	plan.unitCount = unitCount;
	return plan;
}

struct FlatWriter {
	uint8_t *buf;
	unsigned cap;
	unsigned pos;
	bool overflow;

	void u8(uint8_t v) {
		if (pos + 1u > cap) { overflow = true; return; }
		buf[pos++] = v;
	}
	void u16(uint16_t v) {
		u8(static_cast<uint8_t>(v & 0xffu));
		u8(static_cast<uint8_t>((v >> 8) & 0xffu));
	}
	void u32(uint32_t v) {
		u8(static_cast<uint8_t>(v & 0xffu));
		u8(static_cast<uint8_t>((v >> 8) & 0xffu));
		u8(static_cast<uint8_t>((v >> 16) & 0xffu));
		u8(static_cast<uint8_t>((v >> 24) & 0xffu));
	}
	void f32(float v) {
		uint32_t bits = 0u;
		memcpy(&bits, &v, sizeof(bits));
		u32(bits);
	}
	void vec3(const float *v) {
		f32(v[0]); f32(v[1]); f32(v[2]);
	}
	void shortName(const char *s) {
		const unsigned n = static_cast<unsigned>(strlen(s));
		if (n > 255u || pos + 1u + n > cap) { overflow = true; return; }
		u8(static_cast<uint8_t>(n));
		memcpy(buf + pos, s, n);
		pos += n;
	}
	void zstr(const char *s) {
		const unsigned n = static_cast<unsigned>(strlen(s)) + 1u;
		if (pos + n > cap) { overflow = true; return; }
		memcpy(buf + pos, s, n);
		pos += n;
	}
	void initThing(const char *name, int allegiance, const float *position, const float *orientation) {
		vec3(position);
		vec3(orientation);
		u32(0u);                                    // mMeshNo
		u32(static_cast<uint32_t>(allegiance));     // mAllegiance
		u32(0xffffffffu);                           // mTarget (no target index)
		zstr("");                                   // mScript (none)
		zstr(name);                                 // mName
		zstr("");                                   // mSpawnScript (none)
		u32(1u);                                    // mActive = TRUE
		u32(0u);                                    // mAttachScriptsToUnits = FALSE
	}
};

inline bool VecValid(const float *v, float lo, float hi) {
	if (!v) return false;
	for (unsigned i = 0; i < 3u; ++i)
		if (!(v[i] >= lo && v[i] <= hi)) return false;
	return true;
}

inline bool ValidateWorldPlan(const WorldPlan &plan) {
	if (!ValidPrintableName(plan.playerConfig)) return false;
	if (plan.baseLevel < 0 || plan.baseLevel > 999) return false;
	if (plan.mapNumber < 0 || plan.mapNumber > 999) return false;
	if (!VecValid(plan.startPosition, -512.0f, 1024.0f) ||
	    !VecValid(plan.startOrientation, -100.0f, 100.0f))
		return false;
	if (plan.unitCount == 0u || plan.unitCount > kMaxActors || !plan.units)
		return false;
	for (unsigned i = 0; i < plan.unitCount; ++i) {
		const UnitSpawnSpec &u = plan.units[i];
		if (!ValidPrintableName(u.actorName)) return false;
		if (!IsAllowedUnitName(u.unitDef)) return false;
		if (u.allegiance != kAllegianceForseti && u.allegiance != kAllegianceMuspell)
			return false;
		if (!VecValid(u.position, -512.0f, 1024.0f) ||
		    !VecValid(u.orientation, -100.0f, 100.0f))
			return false;
	}
	return true;
}

// Writes one complete clean-room version-50 WORLD body into `outBuf`.
// `real == false` produces the empty base world; `real == true` produces the
// playable Carrier Clash world with CStart + all validated UnitSpawnSpecs.
inline bool WriteWorldBody(const WorldPlan &plan, bool real,
                           uint8_t *outBuf, unsigned outCap, unsigned *outSize) {
	if (!outBuf || !outSize || outCap < 256u) return false;
	*outSize = 0u;
	if (!ValidateWorldPlan(plan)) return false;

	FlatWriter w = {outBuf, outCap, 0u, false};
	w.u16(kWorldVersion);
	w.u32(3u); w.u32(0u); w.u32(0u);                 // header + unused fields
	w.u32(1u); w.shortName(plan.playerConfig);       // 1 player BE configuration
	w.u32(0u); w.u32(0u); w.u32(0u); w.u32(0u);      // ordinary single-player world
	w.u32(real ? static_cast<uint32_t>(plan.baseLevel) : 0xffffffffu);
	w.u32(0u);                                       // zero mission scripts
	w.u32(static_cast<uint32_t>(plan.mapNumber));
	w.u32(0u);                                       // zero named meshes

	const uint16_t thingCount = real ? static_cast<uint16_t>(1u + plan.unitCount) : 0u;
	w.u16(thingCount);
	if (real) {
		// Player 1 Battle Engine start
		w.u32(kOidStart);
		w.initThing("ce.cc.player1", kAllegianceForseti, plan.startPosition, plan.startOrientation);
		w.u32(plan.startInJetMode ? 1u : 0u);        // mPlaneMode
		w.u32(1u);                                   // mPlayerNumber (1-based)

		// Capital ships & escort units
		for (unsigned i = 0; i < plan.unitCount; ++i) {
			const UnitSpawnSpec &u = plan.units[i];
			w.u32(kOidUnit);
			w.initThing(u.actorName, u.allegiance, u.position, u.orientation);
			w.shortName(u.unitDef);
			w.u32(0u);                               // dies_on = 0 (never pre-killed)
		}
	}

	w.u16(0u); w.u32(0u);                            // zero tree groups & tree types
	w.u16(1u); w.u32(0u); w.u32(0u);                 // empty influence map
	w.u16(0u);                                       // zero waypoint paths
	for (unsigned i = 0; i < 5u; ++i) w.f32(0.0f);   // grading times
	w.u32(0u); w.u32(0u);                            // grading counts
	for (unsigned i = 0; i < 4u; ++i) w.f32(0.0f);   // trailing floats
	for (unsigned i = 0; i < 3u; ++i) w.u32(0u);     // zero occupancy grids
	w.f32(0.0f);                                     // start time = 0

	if (w.overflow || w.pos == 0u || w.pos > kMaxWorldBodyBytes) return false;
	*outSize = w.pos;
	return true;
}

// Parses and validates a generated version-50 world body (used by both preflight
// self-check and host unit tests).
struct WorldSummary {
	uint16_t version;
	int32_t baseLevel;
	uint32_t scriptCount;
	uint32_t mapNumber;
	uint16_t thingCount;
	uint16_t startCount;
	uint16_t unitCount;
	bool sawVenturer;
	bool sawFenrir;
	bool sawForbiddenUnit;
};

struct FlatReader {
	const uint8_t *buf;
	unsigned len;
	unsigned pos;
	bool err;

	uint8_t u8() {
		if (pos + 1u > len) { err = true; return 0u; }
		return buf[pos++];
	}
	uint16_t u16() {
		const uint16_t lo = u8();
		const uint16_t hi = u8();
		return static_cast<uint16_t>(lo | (hi << 8));
	}
	uint32_t u32() {
		const uint32_t b0 = u8(), b1 = u8(), b2 = u8(), b3 = u8();
		return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
	}
	void skip(unsigned n) {
		if (n > len - pos) { err = true; pos = len; return; }
		pos += n;
	}
	bool readShortName(char *out, unsigned outCap) {
		const unsigned n = u8();
		if (err || n >= outCap || n > len - pos) { err = true; return false; }
		memcpy(out, buf + pos, n);
		out[n] = '\0';
		pos += n;
		return true;
	}
	bool skipZStr() {
		while (pos < len) {
			if (buf[pos++] == 0u) return true;
		}
		err = true;
		return false;
	}
};

inline bool VerifyWorldBody(const uint8_t *buf, unsigned len, WorldSummary *out) {
	if (!buf || len < 64u || !out) return false;
	*out = WorldSummary{};
	FlatReader r = {buf, len, 0u, false};
	out->version = r.u16();
	if (out->version != kWorldVersion) return false;
	const uint32_t hdr = r.u32();
	if (hdr != 3u) return false;
	r.skip(8u); // unused words
	const uint32_t cfgCount = r.u32();
	if (cfgCount != 1u) return false;
	char cfg[132] = {};
	if (!r.readShortName(cfg, sizeof(cfg))) return false;
	r.skip(16u); // ordinary world header words
	out->baseLevel = static_cast<int32_t>(r.u32());
	out->scriptCount = r.u32();
	out->mapNumber = r.u32();
	const uint32_t namedMeshes = r.u32();
	if (out->scriptCount != 0u || namedMeshes != 0u) return false;
	out->thingCount = r.u16();
	for (uint16_t i = 0; i < out->thingCount; ++i) {
		const uint32_t oid = r.u32();
		r.skip(36u); // pos[3], ori[3], meshNo, allegiance, target
		if (!r.skipZStr() || !r.skipZStr() || !r.skipZStr()) return false;
		r.skip(8u); // active, attachScripts
		if (oid == kOidStart) {
			r.skip(8u); // planeMode, playerNumber
			++out->startCount;
		} else if (oid == kOidUnit) {
			char def[132] = {};
			if (!r.readShortName(def, sizeof(def))) return false;
			r.skip(4u); // dies_on
			++out->unitCount;
			if (IsForbiddenUnit(def)) out->sawForbiddenUnit = true;
			if (StrEqualsIgnoreCase(def, kVenturerUnit)) out->sawVenturer = true;
			if (StrEqualsIgnoreCase(def, kFenrirUnit) ||
			    StrEqualsIgnoreCase(def, kFenrirAltUnit)) out->sawFenrir = true;
		} else {
			return false;
		}
	}
	return !r.err && !out->sawForbiddenUnit;
}

// Synthesizes the 325-byte PMSH/PMS2 chunk header that CMesh::Deserialize reads
// before loading data\resources\meshes\m_<meshName>.aya.
inline bool BuildMeshStubChunk(const char *meshName, int32_t runtimeId, bool keepBuckets,
                               uint8_t *outBuf, unsigned outCap, unsigned *outSize) {
	if (!outBuf || !outSize || outCap < 325u || !ValidPrintableName(meshName)) return false;
	const unsigned nameLen = static_cast<unsigned>(strlen(meshName));
	if (nameLen >= 300u) return false;
	memset(outBuf, 0, 325u);
	// Outer chunk: "PMSH", size = 317 (8 + 309)
	outBuf[0] = 'P'; outBuf[1] = 'M'; outBuf[2] = 'S'; outBuf[3] = 'H';
	const uint32_t outerSize = 317u;
	memcpy(outBuf + 4, &outerSize, 4);
	// Inner chunk: "PMS2", size = 309 (300 + 4 + 4 + 1)
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

// Preflight asset descriptor table & pure verdict helper (for host testing and
// runtime preflight diagnostics).
enum PreflightFailure {
	kPreflightOk = 0,
	kPreflightForbiddenUnit,
	kPreflightMissingArchive,
	kPreflightCorruptArchive,
	kPreflightMissingMesh,
	kPreflightMissingDefinition,
	kPreflightWorldBuildFailed,
};

struct PreflightInput {
	bool archiveExists;
	bool archiveValid;
	bool meshesValid[kPreloadMeshCount];
	bool texturesValid[kPreloadTextureCount];
	bool unitsPresent[kRequiredUnitCount];
	const char *requestedBossUnit;
};

struct PreflightResult {
	PreflightFailure code;
	const char *message;
};

inline PreflightResult EvaluatePreflight(const PreflightInput &in) {
	if (!in.requestedBossUnit || IsForbiddenUnit(in.requestedBossUnit)) {
		return {kPreflightForbiddenUnit,
		        "Carrier Clash refused an unsupported unit definition. Your career is unchanged."};
	}
	if (!in.archiveExists) {
		return {kPreflightMissingArchive,
		        "Carrier Clash requires level 742 resources from your retail install. Your career is unchanged."};
	}
	if (!in.archiveValid) {
		return {kPreflightCorruptArchive,
		        "Carrier Clash could not read level 742 resources. Check your game install and try again."};
	}
	for (unsigned i = 0; i < kPreloadMeshCount; ++i) {
		if (!in.meshesValid[i]) {
			return {kPreflightMissingMesh,
			        "Carrier Clash is missing required retail carrier model files. Your career is unchanged."};
		}
	}
	for (unsigned i = 0; i < kPreloadTextureCount; ++i) {
		if (!in.texturesValid[i]) {
			return {kPreflightMissingMesh,
			        "Carrier Clash is missing required retail carrier textures. Your career is unchanged."};
		}
	}
	for (unsigned i = 0; i < kRequiredUnitCount; ++i) {
		if (!in.unitsPresent[i]) {
			return {kPreflightMissingDefinition,
			        "Carrier Clash could not find required unit definitions in your game data. Your career is unchanged."};
		}
	}
	return {kPreflightOk, ""};
}

} // namespace CeCarrierClashWorld

#endif
