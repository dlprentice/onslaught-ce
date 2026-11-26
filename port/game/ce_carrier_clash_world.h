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
// Flagship opening stations: West lane vs East lane, 160 m apart, 20 m altitude stagger.
constexpr float kVenturerStartX = 180.0f;
constexpr float kVenturerStartY = 262.0f;
constexpr float kVenturerStartZ = -30.0f;
constexpr float kFenrirStartX = 340.0f;
constexpr float kFenrirStartY = 266.0f;
constexpr float kFenrirStartZ = -50.0f;

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

// Standalone retail meshes this battle can put on screen but that 742_res_PC.aya
// does not pack. A mesh the engine cannot find is drawn as its missing-mesh
// marker (default.msh, a green "?" cube): here that was every rocket in flight
// and every escape pod leaving a downed fighter (the engine names m_rocket.msh
// itself, in Rocket.cpp and EscapePod.cpp), and a destroyed Tara Wildcat. Each is
// optional: a file an install lacks is skipped and logged, never a refusal, and
// these stay out of the strict preflight above. `texture` names the one texture
// the level archive lacks too (nullptr when it already holds all the mesh needs).
struct SupportMeshSpec {
	const char *mesh;
	const char *texture;
};

constexpr unsigned kSupportMeshCount = 3u;
inline const SupportMeshSpec &SupportMesh(unsigned index) {
	static const SupportMeshSpec kMeshes[kSupportMeshCount] = {
		{"m_rocket.msh",                "meshtex\\m_rocket.TGA"},
		{"fa_wildcard_t_destroyed.msh", nullptr},
		{"cockpit2b.msh",               nullptr},
	};
	return kMeshes[index < kSupportMeshCount ? index : 0u];
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
// Sunset key light. One to-sun vector (Z counts downward, so -Z is up) shared by
// the engine sun (flare, unit light, water glint), the terrain lightmap and the
// host tests: low over the north-north-west horizon, behind F.S. Venturer as
// seen from the launch headland. kSunAz* is its unit horizontal bearing.
// ---------------------------------------------------------------------------
constexpr float kSunDirX = -0.524f;
constexpr float kSunDirY = 0.838f;
constexpr float kSunDirZ = -0.150f;            // ~8.6 degrees above the horizon, in the sunset glow band
constexpr float kSunAzX = -0.530f;
constexpr float kSunAzY = 0.848f;

// ---------------------------------------------------------------------------
// Clean-room procedural 512 x 512 m volcanic basalt archipelago heightfield.
// Elevations below are metres above sea level (positive up); the engine height
// is Z = kWaterLevel - elevation. Each landform has its own character, a steep
// face toward the sun and a long tail in its lee:
//   - a tall jagged sea-stack landmark dead ahead of the launch headland (~38 m)
//   - a crescent island around a sheltered shallow cove, east of the strait (~19 m)
//   - a long low stepped reef bar north-west, under F.S. Venturer (~6 m)
//   - tilted ridge islands, a needle and skerry shoals around the rim
//   - the player's terraced launch headland with a flat landing pad
// The strait between them is open deep water by construction (StraitGate).
// Everything is deterministic arithmetic on an integer lattice hash.
// ---------------------------------------------------------------------------
constexpr float kSeabedElev = kWaterLevel - kDeepSeaFloorZ;   // -16.5 m
constexpr float kShelfElev = -1.5f;            // wave-cut shelf fringing every shore
constexpr float kBastionElev = kWaterLevel - kBastionTopZ;    // +7.0 m
constexpr float kBastionPadRadius = 10.0f;     // exactly flat landing pad
constexpr float kMaxLandElev = 43.0f;          // Z >= -53.0 m
constexpr float kReefMaxElev = 6.5f;           // below the pad: never breaks its horizon
constexpr float kStackX = 280.0f;              // sea-stack landmark
constexpr float kStackY = 440.0f;
constexpr float kStackPeakElev = 30.5f;
constexpr float kCrescentX = 428.0f;           // crescent island and its cove
constexpr float kCrescentY = 292.0f;
constexpr float kCrescentRingRadius = 42.0f;
constexpr float kCoveFloorElev = -3.2f;
constexpr float kReefStartX = 58.0f;           // reef bar spine, south-west end
constexpr float kReefStartY = 356.0f;
constexpr float kReefDirX = 0.807f;            // unit, toward its north-east end
constexpr float kReefDirY = 0.591f;
constexpr float kReefLength = 138.8f;
// Deep-water keep-out around the flagship lanes: no land inside it.
constexpr float kStraitMinX = 152.0f;
constexpr float kStraitMaxX = 363.0f;
constexpr float kStraitMinY = 176.0f;
constexpr float kStraitMaxY = 356.0f;
constexpr float kStraitShoulder = 20.0f;       // land fades in over this distance outside it
// Player eye at the opening station, for the Venturer sightline fan.
constexpr float kLaunchEyeX = kBastionX;
constexpr float kLaunchEyeY = kBastionY + 5.5f;

inline float Clamp01(float t) {
	return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

inline float SmoothRange(float lo, float hi, float v) {
	const float t = Clamp01((v - lo) / (hi - lo));
	return t * t * (3.0f - 2.0f * t);
}

inline float Mix(float a, float b, float t) {
	return a + (b - a) * t;
}

inline uint32_t TerrainHash(int32_t ix, int32_t iy, uint32_t seed) {
	uint32_t h = static_cast<uint32_t>(ix) * 0x9E3779B1u;
	h ^= static_cast<uint32_t>(iy) * 0x85EBCA77u;
	h ^= seed * 0xC2B2AE3Du;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

// Smooth lattice value noise in [-1, +1].
inline float ValueNoise(float x, float y, uint32_t seed) {
	const float fx = floorf(x);
	const float fy = floorf(y);
	const int32_t ix = static_cast<int32_t>(fx);
	const int32_t iy = static_cast<int32_t>(fy);
	float tx = x - fx;
	float ty = y - fy;
	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	const float kScale = 2.0f / 65535.0f;
	const float a = static_cast<float>(TerrainHash(ix, iy, seed) & 0xffffu) * kScale - 1.0f;
	const float b = static_cast<float>(TerrainHash(ix + 1, iy, seed) & 0xffffu) * kScale - 1.0f;
	const float c = static_cast<float>(TerrainHash(ix, iy + 1, seed) & 0xffffu) * kScale - 1.0f;
	const float d = static_cast<float>(TerrainHash(ix + 1, iy + 1, seed) & 0xffffu) * kScale - 1.0f;
	const float ab = a + (b - a) * tx;
	const float cd = c + (d - c) * tx;
	return ab + (cd - ab) * ty;
}

// Three-octave ridge noise in [-1, +1]: sharp crests, rounded gullies.
inline float CragNoise(float x, float y, uint32_t seed) {
	float sum = 0.0f;
	float amp = 1.0f;
	float fx = x, fy = y;
	for (uint32_t o = 0u; o < 3u; ++o) {
		const float n = 1.0f - fabsf(ValueNoise(fx, fy, seed + o));
		sum += amp * n * n;
		amp *= 0.5f;
		fx = fx * 2.07f + 11.3f;
		fy = fy * 2.07f - 7.1f;
	}
	return sum * (2.0f / 1.75f) - 1.0f;
}

// A flat tread then a riser every `step` metres, with a tread at `base`.
inline float Terrace(float elev, float step, float base, float tread) {
	const float q = (elev - base) / step;
	const float n = floorf(q);
	return base + step * (n + SmoothRange(tread, 1.0f, q - n));
}

// Shore cross-section at normalised distance q from a crest (0) to open water
// (1): a steep submarine pedestal up to the wave-cut shelf, then the dry
// profile from the shoreline to the crest, flat inside `flat`.
inline float ShoreProfile(float q, float flat, float crestElev) {
	return kSeabedElev +
	       (kShelfElev - kSeabedElev) * (1.0f - SmoothRange(0.72f, 1.0f, q)) +
	       (crestElev - kShelfElev) * (1.0f - SmoothRange(flat, 0.86f, q));
}

// 0 inside the deep-water strait, rising to 1 one shoulder outside it.
inline float StraitGate(float x, float y) {
	const float dx = x < kStraitMinX ? kStraitMinX - x : (x > kStraitMaxX ? x - kStraitMaxX : 0.0f);
	const float dy = y < kStraitMinY ? kStraitMinY - y : (y > kStraitMaxY ? y - kStraitMaxY : 0.0f);
	return SmoothRange(0.0f, kStraitShoulder, sqrtf(dx * dx + dy * dy));
}

struct LandPoint {
	float elev;   // metres above sea level
	float bare;   // 0 = soil that carries grass on its flats, 1 = wave-washed or sheer basalt
};

inline void LandRaise(LandPoint &acc, float elev, float bare, float gate) {
	const float e = kSeabedElev + (elev - kSeabedElev) * gate;
	if (e > acc.elev) {
		acc.elev = e;
		acc.bare = bare;
	}
}

// Sea-stack landmark: a broad terraced basalt stack rising out of deep water
// with a steep sunward scarp, all-round wave-cut ledges, and a long buttressed lee.
inline void StackLand(float u, float v, float gate, LandPoint &acc) {
	const float px = u - kStackX;
	const float py = v - kStackY;
	if (px < -72.0f || px > 72.0f || py < -72.0f || py > 72.0f) return;
	const float lee = -(px * kSunAzX + py * kSunAzY);
	const float side = px * kSunAzY - py * kSunAzX;
	const float wobble = 1.0f + 0.16f * ValueNoise(u * 0.11f, v * 0.11f, 71u);
	const float ra = (lee < 0.0f ? lee / 16.0f : lee / 48.0f) * wobble;
	const float rb = (side < 0.0f ? side / 18.0f : side / 24.0f) * wobble;
	const float r = sqrtf(ra * ra + rb * rb);
	if (r >= 1.0f) return;
	const float wall = 1.0f - SmoothRange(0.52f, 1.0f, r);
	const float crown = 1.0f - SmoothRange(0.06f, 0.5f, r);
	float elev = kSeabedElev + (kStackPeakElev - kSeabedElev) * (0.68f * wall + 0.32f * crown * crown);
	const float terraceMix = Mix(0.52f, 0.85f, SmoothRange(-6.0f, 14.0f, lee)) *
	                         SmoothRange(1.5f, 5.5f, elev);
	elev = Mix(elev, Terrace(elev, 6.0f, 1.2f, 0.48f), terraceMix);
	elev += 2.8f * SmoothRange(0.0f, 6.0f, elev) * CragNoise(u * 0.09f, v * 0.09f, 73u);
	LandRaise(acc, elev, 1.0f, gate);
}

// A lone basalt needle off the stack's shoulder.
inline void NeedleLand(float u, float v, float gate, LandPoint &acc) {
	const float px = u - 306.0f;
	const float py = v - 456.0f;
	if (px < -15.0f || px > 15.0f || py < -15.0f || py > 15.0f) return;
	const float r = sqrtf(px * px + py * py) / 12.0f;
	if (r >= 1.0f) return;
	float elev = kSeabedElev + (11.0f - kSeabedElev) * (1.0f - SmoothRange(0.28f, 1.0f, r));
	elev += 1.4f * SmoothRange(0.0f, 4.0f, elev) * CragNoise(u * 0.16f, v * 0.16f, 79u);
	LandRaise(acc, elev, 1.0f, gate);
}

// Long low reef bar: three asymmetric volcanic ribs parted by a deep surge
// channel at t ~ 0.29 and a shallow wash-over saddle at t ~ 0.74, with craggy
// wave-cut ledges instead of flat uniform slabs.
inline void ReefLand(float u, float v, float gate, LandPoint &acc) {
	if (u < 6.0f || u > 222.0f || v < 304.0f || v > 490.0f) return;
	const float px = u - kReefStartX;
	const float py = v - kReefStartY;
	const float t = Clamp01((px * kReefDirX + py * kReefDirY) / kReefLength);
	const float cx = px - kReefDirX * kReefLength * t;
	const float cy = py - kReefDirY * kReefLength * t;
	const float d = sqrtf(cx * cx + cy * cy);
	const float lean = d > 0.01f ? (cx * kSunAzX + cy * kSunAzY) / d : 0.0f;
	const float width = Mix(34.0f, 13.0f, SmoothRange(-0.5f, 0.5f, lean)) *
	                    (1.0f + 0.22f * ValueNoise(t * 6.0f, 3.3f, 97u));
	const float q = d / width;
	if (q >= 1.0f) return;
	float crest = 6.0f - 1.5f * SmoothRange(0.24f, 0.33f, t) - 2.1f * SmoothRange(0.69f, 0.79f, t);
	const float gapA = 1.0f - SmoothRange(0.0f, 0.052f, fabsf(t - 0.29f));
	const float gapB = 1.0f - SmoothRange(0.0f, 0.042f, fabsf(t - 0.74f));
	crest = Mix(crest, -2.6f, gapA);
	crest = Mix(crest, -0.4f, gapB);
	float elev = ShoreProfile(q, 0.08f, crest);
	elev = Mix(elev, Terrace(elev, 1.8f, 0.6f, 0.50f), 0.5f * SmoothRange(0.6f, 1.6f, elev));
	elev += 1.2f * SmoothRange(0.3f, 2.0f, elev) * CragNoise(u * 0.13f, v * 0.13f, 101u);
	if (elev > kReefMaxElev) elev = kReefMaxElev;
	LandRaise(acc, elev, 1.0f, gate);
}

// Crescent island: a ring ridge open to the south-west around a shallow cove.
// The rim is highest at the back and toward the sun, where its outer face is a
// cliff; inside it shelves gently to a beach, and the horns sink into a sill.
inline void CrescentLand(float u, float v, float gate, LandPoint &acc) {
	const float px = u - kCrescentX;
	const float py = v - kCrescentY;
	if (px < -82.0f || px > 82.0f || py < -82.0f || py > 82.0f) return;
	const float r = sqrtf(px * px + py * py);
	const float inv = r > 0.5f ? 1.0f / r : 0.0f;
	const float back = 0.5f * (1.0f + (px * 0.780f + py * 0.626f) * inv);   // 0 at the mouth, 1 behind the cove
	const float sunLean = (px * kSunAzX + py * kSunAzY) * inv;
	const float rim = SmoothRange(0.16f, 0.62f, back);
	const float crest = -4.0f + rim * (15.0f + 7.0f * back + 2.5f * sunLean) *
	                    (1.0f + 0.12f * ValueNoise(u * 0.045f, v * 0.045f, 113u));
	const float rho = r - kCrescentRingRadius;
	float elev;
	if (rho < 0.0f) {
		// Cove side: the rim's inner wall, then a wide gentle beach down to the
		// lagoon floor. The beach thins out toward the horns, so the mouth
		// stays an open channel.
		const float q = -rho / 34.0f;
		const float beachTop = Mix(kCoveFloorElev, 1.5f, rim);
		elev = kCoveFloorElev +
		       (beachTop - kCoveFloorElev) * (1.0f - SmoothRange(0.35f, 1.0f, q)) +
		       (crest - beachTop) * (1.0f - SmoothRange(0.08f, 0.5f, q));
	} else {
		elev = ShoreProfile(rho / Mix(26.0f, 10.0f, SmoothRange(-0.3f, 0.5f, sunLean)), 0.08f, crest);
	}
	const float lagoon = kSeabedElev + (kCoveFloorElev - kSeabedElev) *
	                     (1.0f - SmoothRange(kCrescentRingRadius - 2.0f, kCrescentRingRadius + 15.0f, r));
	if (lagoon > elev) elev = lagoon;
	elev = Mix(elev, Terrace(elev, 5.5f, 1.2f, 0.5f), 0.7f * SmoothRange(4.0f, 8.0f, elev));
	elev += 2.0f * SmoothRange(1.0f, 7.0f, elev) * CragNoise(u * 0.075f, v * 0.075f, 117u);
	LandRaise(acc, elev, 0.0f, gate);
}

// Tilted ridge island: its crest runs across the sun's bearing, with a short
// scarp facing the sun and a long terraced dip slope behind it.
inline void RidgeLand(float u, float v, float gate, LandPoint &acc,
                      float cx, float cy, float halfLen, float scarp, float dip,
                      float crestElev, uint32_t seed) {
	const float px = u - cx;
	const float py = v - cy;
	const float reach = 1.2f * (halfLen > dip ? halfLen : dip);
	if (px < -reach || px > reach || py < -reach || py > reach) return;
	const float along = (px * kSunAzY - py * kSunAzX) / halfLen;
	const float across = px * kSunAzX + py * kSunAzY;
	const float rb = across > 0.0f ? across / scarp : across / dip;
	const float r = sqrtf(along * along + rb * rb) *
	                (1.0f + 0.14f * ValueNoise(u * 0.07f, v * 0.07f, seed));
	if (r >= 1.0f) return;
	float elev = ShoreProfile(r, 0.10f, crestElev);
	const float step = (crestElev - 1.0f) * (1.0f / 3.0f);
	elev = Mix(elev, Terrace(elev, step, 1.0f, 0.5f),
	           0.75f * SmoothRange(0.0f, 8.0f, -across) * SmoothRange(1.0f, 3.0f, elev));
	elev += 0.13f * crestElev * SmoothRange(0.5f, 5.0f, elev) * CragNoise(u * 0.08f, v * 0.08f, seed + 3u);
	LandRaise(acc, elev, 0.0f, gate);
}

// Skerry shoal: a shallow bank with scattered rocks breaking the surface.
inline void SkerryLand(float u, float v, float gate, LandPoint &acc,
                       float cx, float cy, float radius, float topElev, uint32_t seed) {
	const float px = u - cx;
	const float py = v - cy;
	if (px < -radius || px > radius || py < -radius || py > radius) return;
	const float r = sqrtf(px * px + py * py) / radius;
	if (r >= 1.0f) return;
	const float bank = 1.0f - SmoothRange(0.25f, 1.0f, r);
	const float rocks = CragNoise(u * 0.055f, v * 0.055f, seed);
	const float top = -4.5f + (topElev + 4.5f) * SmoothRange(-0.25f, 0.75f, rocks);
	LandRaise(acc, kSeabedElev + (top - kSeabedElev) * bank, 1.0f, gate);
}

// The player's launch headland: an exactly flat landing pad at (kBastionX,
// kBastionY) on a rocky overlook whose broken lip drops sheer into the bay to
// the north, a high back terrace behind it, a sunward cliff and benches
// stepping down to a beach in its lee.
inline void HeadlandLand(float x, float y, LandPoint &acc) {
	if (x < 174.0f || x > 326.0f || y < 18.0f || y > 182.0f) return;
	const float dxp = x - kBastionX;
	const float dyp = y - kBastionY;
	const float rPad = sqrtf(dxp * dxp + dyp * dyp);
	if (rPad <= kBastionPadRadius) {
		LandRaise(acc, kBastionElev, 1.0f, 1.0f);
		return;
	}
	// The outline wanders only away from the pad, so the pad's rim is exact.
	const float wander = 8.0f * SmoothRange(kBastionPadRadius, kBastionPadRadius + 16.0f, rPad);
	const float u = x + wander * ValueNoise(x * 0.027f, y * 0.027f, 131u);
	const float v = y + wander * ValueNoise(x * 0.027f, y * 0.027f, 137u);
	// Spine from the overlook tip (260, 152) south-south-west to (238, 78).
	const float sx = -22.0f, sy = -74.0f;
	const float px = u - 260.0f;
	const float py = v - 152.0f;
	const float t = Clamp01((px * sx + py * sy) / (sx * sx + sy * sy));
	const float cx = px - sx * t;
	const float cy = py - sy * t;
	const float d = sqrtf(cx * cx + cy * cy);
	const float lean = d > 0.01f ? (cx * kSunAzX + cy * kSunAzY) / d : 0.0f;
	const float width = Mix(46.0f, 21.0f, SmoothRange(-0.35f, 0.55f, lean));
	if (d >= width) return;
	const float crest = kBastionElev + 6.0f * SmoothRange(0.40f, 0.62f, t) - 4.0f * SmoothRange(0.86f, 1.0f, t);
	float elev = ShoreProfile(d / width, 12.5f / width, crest);
	elev = Mix(elev, Terrace(elev, 3.0f, 1.0f, 0.55f), 0.85f * SmoothRange(1.0f, 2.5f, elev));
	elev += 1.1f * SmoothRange(1.0f, 5.0f, elev) * CragNoise(u * 0.10f, v * 0.10f, 139u);
	// Broken rocky lip around the pad, notched below it; nothing ahead of the
	// pad stands above it.
	const float lip = SmoothRange(kBastionPadRadius + 0.3f, kBastionPadRadius + 2.0f, rPad) *
	                  (1.0f - SmoothRange(kBastionPadRadius + 3.5f, kBastionPadRadius + 6.5f, rPad));
	const float notch = CragNoise(x * 0.21f, y * 0.21f, 149u);
	elev -= 1.5f * lip * (notch > 0.0f ? notch : 0.0f);
	const float cap = kBastionElev + 8.0f * SmoothRange(0.36f, 0.50f, t);
	if (elev > cap) elev = cap;
	elev = Mix(kBastionElev, elev, SmoothRange(kBastionPadRadius, kBastionPadRadius + 6.0f, rPad));
	// The launch headland is bare volcanic basalt out into the strait's deep bay.
	LandRaise(acc, elev, 1.0f, 1.0f - SmoothRange(166.0f, 181.0f, y));
}

inline LandPoint LandAt(float x, float y) {
	LandPoint acc = {kSeabedElev, 0.0f};
	HeadlandLand(x, y, acc);
	const float gate = StraitGate(x, y);
	if (gate <= 0.0f) return acc;
	// Two-octave domain warp: island outlines wander by up to 12.5 m. Its
	// gradient stays below 1, so the warp never folds the map.
	const float u = x + 10.0f * ValueNoise(x * 0.0205f, y * 0.0205f, 11u) +
	                2.5f * ValueNoise(x * 0.05f, y * 0.05f, 13u);
	const float v = y + 10.0f * ValueNoise(x * 0.0205f, y * 0.0205f, 23u) +
	                2.5f * ValueNoise(x * 0.05f, y * 0.05f, 29u);
	StackLand(u, v, gate, acc);
	NeedleLand(u, v, gate, acc);
	SkerryLand(u, v, gate, acc, 252.0f, 416.0f, 26.0f, 3.5f, 83u);
	ReefLand(u, v, gate, acc);
	CrescentLand(u, v, gate, acc);
	RidgeLand(u, v, gate, acc, 70.0f, 178.0f, 44.0f, 16.0f, 46.0f, 14.0f, 151u);   // west ridge
	RidgeLand(u, v, gate, acc, 404.0f, 438.0f, 30.0f, 11.0f, 31.0f, 11.0f, 157u);  // north-east islet
	RidgeLand(u, v, gate, acc, 134.0f, 88.0f, 28.0f, 11.0f, 29.0f, 9.0f, 163u);    // south-west islet
	SkerryLand(u, v, gate, acc, 392.0f, 98.0f, 42.0f, 4.5f, 167u);                 // south-east shoal
	if (acc.elev > kMaxLandElev) acc.elev = kMaxLandElev;
	return acc;
}

struct TerrainSample {
	float z;      // engine height (Z counts downward)
	float bare;   // LandPoint::bare
};

inline TerrainSample SampleTerrain(float x, float y) {
	if (!(x >= 0.0f && x <= static_cast<float>(kCells)) ||
	    !(y >= 0.0f && y <= static_cast<float>(kCells))) {
		return {kDeepSeaFloorZ, 0.0f};
	}
	const LandPoint land = LandAt(x, y);
	// Gently undulating seabed beneath open water; it fades out under land.
	const float swell =
		1.1f * sinf(x * 0.031f + y * 0.019f) +
		0.7f * cosf(x * 0.053f - y * 0.041f);
	float z = kWaterLevel - land.elev +
	          swell * (1.0f - SmoothRange(0.0f, 5.0f, land.elev - kSeabedElev));

	// Strict +/- 55 m heightfield clamp (retail CDXLandscape clamps tiles at +/- 60 m).
	if (z < kMinZBound + 1.0f) z = kMinZBound + 1.0f;
	if (z > kMaxZBound - 1.0f) z = kMaxZBound - 1.0f;
	return {z, land.bare};
}

inline float HeightAt(float x, float y) {
	return SampleTerrain(x, y).z;
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

// Cosine of the angle between the terrain's upward normal at a cell and the
// to-sun vector, clamped to [0, 1].
inline float CellSunDot(int x, int y, float sunX, float sunY, float sunZ) {
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
	return ndotl;
}

// Lightmap byte for a lit fraction in [0, 1]: 16 in full shadow, 46 square to the sunset key
// so CHeightField's 2x colour-table scaling warms sunward scarps without clipping to noon white.
inline uint8_t LightFromSunDot(float lit) {
	const int shade = static_cast<int>(16.0f + lit * 30.0f + 0.5f);
	if (shade < 14) return 14u;
	if (shade > 46) return 46u;
	return static_cast<uint8_t>(shade);
}

// Computes unit-normal lighting value in [14..46] for MIXERMAP.mMap[y * 512 + x].
inline uint8_t ComputeCellLight(int x, int y, float sunX, float sunY, float sunZ) {
	return LightFromSunDot(CellSunDot(x, y, sunX, sunY, sunZ));
}

// How much of the sun a cell sees past the terrain between it and the sun:
// 0 in full cast shadow, 1 in the open, with a penumbra that widens with
// distance. Cells under the sea are hidden by it and are left open.
inline float SunOpenAt(int x, int y, float sunX, float sunY, float sunZ) {
	const float horiz = sqrtf(sunX * sunX + sunY * sunY);
	if (!(horiz > 1e-4f) || !(sunZ < 0.0f)) return 1.0f;
	const float elev = kWaterLevel - HeightAt(static_cast<float>(x), static_cast<float>(y));
	if (elev < -1.0f) return 1.0f;
	const float ux = sunX / horiz;
	const float uy = sunY / horiz;
	const float rise = -sunZ / horiz;
	float open = 1.0f;
	for (float t = 2.5f; t < 160.0f; t += 2.5f + t * 0.06f) {
		const float px = static_cast<float>(x) + ux * t;
		const float py = static_cast<float>(y) + uy * t;
		if (px < 0.0f || px > static_cast<float>(kCells) ||
		    py < 0.0f || py > static_cast<float>(kCells)) break;
		const float ray = elev + 0.5f + rise * t;
		if (ray > kMaxLandElev + 1.0f) break;
		const float clear = (ray - (kWaterLevel - HeightAt(px, py))) / (0.6f + 0.12f * t);
		if (clear < open) {
			open = clear;
			if (open <= 0.0f) return 0.0f;
		}
	}
	return open;
}

// ComputeCellLight with cast shadows: the lee of every cliff and stack falls dark.
inline uint8_t ComputeCellLightShadowed(int x, int y, float sunX, float sunY, float sunZ) {
	const float lit = CellSunDot(x, y, sunX, sunY, sunZ);
	if (lit <= 0.0f) return LightFromSunDot(0.0f);
	return LightFromSunDot(lit * SunOpenAt(x, y, sunX, sunY, sunZ));
}

// ---------------------------------------------------------------------------
// MIXERMAP layer alphas over the base (grass) texture: layer 0 sand, layer 1
// basalt rock. CHeightField's mixer adds a texel's signed top byte [-32, +31]
// to a layer's alpha and compares signed 32-bit differences against what is
// already there, so both alphas stay inside [-62, +62] with
// rock >= max(0, sand) - 62. That set is convex, so the mixer's bilinear spans
// between samples stay inside it and no difference can wrap. Each ramp is
// narrow and saturates: a sand skirt along the waterline, bare rock on steep
// faces, on wave-washed reefs and on the stack, and clean grass on the flats
// above, so no half-blended patches smear down the cliffs.
// ---------------------------------------------------------------------------
constexpr int kMixAlphaMin = -62;
constexpr int kMixAlphaMax = 62;
constexpr float kMixSeabedElev = -7.0f;        // at and below this the base texture is left alone

struct MixAlphas {
	int sand;
	int rock;
};

// Terrain steepness (rise over run) across a 3 m stencil: wider than the
// finest crag octave, narrow enough to tell a terrace riser from its tread.
inline float SlopeAt(float x, float y) {
	const float kMax = static_cast<float>(kCells);
	const float x0 = x >= 1.5f ? x - 1.5f : 0.0f;
	const float x1 = x <= kMax - 1.5f ? x + 1.5f : kMax;
	const float y0 = y >= 1.5f ? y - 1.5f : 0.0f;
	const float y1 = y <= kMax - 1.5f ? y + 1.5f : kMax;
	const float gx = (HeightAt(x1, y) - HeightAt(x0, y)) / ((x1 - x0) > 0.5f ? (x1 - x0) : 1.0f);
	const float gy = (HeightAt(x, y1) - HeightAt(x, y0)) / ((y1 - y0) > 0.5f ? (y1 - y0) : 1.0f);
	return sqrtf(gx * gx + gy * gy);
}

inline int MixAlphaFromWeight(float w) {
	const float a = static_cast<float>(kMixAlphaMin) +
	                static_cast<float>(kMixAlphaMax - kMixAlphaMin) * Clamp01(w);
	int v = static_cast<int>(a >= 0.0f ? a + 0.5f : a - 0.5f);
	if (v < kMixAlphaMin) v = kMixAlphaMin;
	if (v > kMixAlphaMax) v = kMixAlphaMax;
	return v;
}

inline MixAlphas ComputeMixAlphas(float x, float y) {
	MixAlphas out = {kMixAlphaMin, kMixAlphaMin};
	const TerrainSample s = SampleTerrain(x, y);
	const float elev = kWaterLevel - s.z;
	if (elev <= kMixSeabedElev) return out;
	const float steep = SmoothRange(0.62f, 0.95f, SlopeAt(x, y));
	// Rock: steep faces, bare summits, and bare volcanic landforms (the launch
	// headland, sea-stack, needle, reefs and skerries); fades out under the sea.
	float rock = steep;
	const float summit = SmoothRange(27.0f, 30.0f, elev);
	if (summit > rock) rock = summit;
	const float washed = s.bare * SmoothRange(-0.6f, 0.6f, elev);
	if (washed > rock) rock = washed;
	rock *= SmoothRange(-5.0f, -2.0f, elev);
	// Sand: shoreline skirt on sheltered soil-bearing islands and coves only,
	// never a pale ring around bare basalt stacks, reefs or the launch headland.
	const float sand = (1.0f - s.bare) *
	                   SmoothRange(kMixSeabedElev, -3.5f, elev) *
	                   (1.0f - SmoothRange(1.3f, 2.7f, elev)) * (1.0f - steep);
	out.sand = MixAlphaFromWeight(sand);
	out.rock = MixAlphaFromWeight(rock);
	const int rockFloor = (out.sand > 0 ? out.sand : 0) + kMixAlphaMin;
	if (out.rock < rockFloor) out.rock = rockFloor;
	return out;
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
	// Capital flagships open 160 m apart, each on its own side of the strait and
	// its own altitude band (Fenrir 20 m above the Venturer): two ~100 m hulls
	// trade broadsides across open water and never share airspace. These match
	// the first patrol waypoints of CeCarrierClashDirector.
	outUnits[n++] = {"ce.cc.venturer", kVenturerUnit, kAllegianceForseti,
	                 {kVenturerStartX, kVenturerStartY, kVenturerStartZ}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.fenrir", fenrirDef, kAllegianceMuspell,
	                 {kFenrirStartX, kFenrirStartY, kFenrirStartZ}, {kYawWNW, 0.0f, 0.0f}};
	// Forseti naval cruisers (FSV Marshall) flanking Venturer in the deep channel
	outUnits[n++] = {"ce.cc.marshall.1", kEscortCruiserUnit, kAllegianceForseti,
	                 {176.0f, 214.0f, kWaterLevel}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.marshall.2", kEscortCruiserUnit, kAllegianceForseti,
	                 {214.0f, 290.0f, kWaterLevel}, {-1.45f, 0.0f, 0.0f}};
	// Forseti Wildcat air patrol orbiting above Venturer
	outUnits[n++] = {"ce.cc.wildcat.1", kWildcatJasonUnit, kAllegianceForseti,
	                 {176.0f, 248.0f, -48.0f}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.wildcat.2", kWildcatBillyUnit, kAllegianceForseti,
	                 {202.0f, 242.0f, -50.0f}, {kYawENE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.wildcat.3", kWildcatTaraUnit, kAllegianceForseti,
	                 {192.0f, 276.0f, -54.0f}, {kYawENE, 0.0f, 0.0f}};
	// Muspell Advanced Fighter screen ahead of Fenrir
	outUnits[n++] = {"ce.cc.atf.1", kMuspellFighterUnit, kAllegianceMuspell,
	                 {320.0f, 250.0f, -62.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.2", kMuspellFighterUnit, kAllegianceMuspell,
	                 {330.0f, 280.0f, -66.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.3", kMuspellFighterUnit, kAllegianceMuspell,
	                 {350.0f, 256.0f, -64.0f}, {kYawWNW, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.cc.atf.4", kMuspellFighterUnit, kAllegianceMuspell,
	                 {316.0f, 272.0f, -68.0f}, {kYawWNW, 0.0f, 0.0f}};
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
	// Version-50 trailing block (2+4 + 2+4+4 + 2 + 20 + 8 + 16 + 12 + 4 = 78 bytes).
	r.skip(78u);
	return !r.err && r.pos == r.len && !out->sawForbiddenUnit;
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
