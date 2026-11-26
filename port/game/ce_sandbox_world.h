// SPDX-License-Identifier: GPL-3.0-only
// Clean-room procedural 512 x 512 m combined-arms multi-biome heightfield,
// elevated Sanctuary Citadel plateau with perimeter parapet and access ramp,
// authored version-50 world body serializer, and retail 700_res_PC.aya
// preflight contract for Living Sandbox.
// Pure C++14 header: no engine headers and no host STL allocations so both the
// game build and standalone ASan/UBSan host tests share identical logic.
#ifndef CE_SANDBOX_WORLD_H
#define CE_SANDBOX_WORLD_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace CeSandboxWorld {

// Archive level 700 natively ships combined-arms Forseti and Muspell ground,
// air, naval, mech, infantry, turret, and factory meshes. Clean-room ID 978
// never collides with retail campaign or multiplayer worlds.
enum {
	kArchiveLevel = 700,
	kBaseWorldId = 978,
	kMapId = 978,
	kWorldVersion = 50,
	kGridShift = 9,
	kCells = 1 << kGridShift,           // 512 x 512 m combined-arms theater
	kPatchCells = 8,
	kPatchSamples = kPatchCells + 1,    // 9 x 9 seam-duplicated patch samples
	kPatches = kCells / kPatchCells,    // 64 x 64 patches
	kHeightSamples = kPatches * kPatches * kPatchSamples * kPatchSamples,
	kMaxActors = 48,
	kMaxWorldBodyBytes = 65536,
	kNameLimit = 127,
	kOidUnit = 8,                       // matches OID_CUnit in src/oids.h
	kOidStart = 15,                     // matches OID_CStart in src/oids.h
	kOidSquad = 28,                     // matches OID_CSquad in src/oids.h
	kAllegianceForseti = 0,             // matches kForsetiAllegiance
	kAllegianceMuspell = 1,             // matches kMuspellAllegiance
	kSkyCubeIndex = 16,                 // starry moonlit coastal sky cube packed in 700_res_PC.aya
	kWaterSetIndex = 0,                 // coastal ocean water set
	kCaptureNodeCount = 5,
};

// Z points DOWNWARD in engine world space (Z < kWaterLevel is above water).
constexpr float kHeightScale = 1.0f / 512.0f;  // int16 [-28160, +28160] -> [-55 m, +55 m]
constexpr int16_t kMaxRawSample = 28160;       // 55.0 m * 512
constexpr float kMinZBound = -55.0f;
constexpr float kMaxZBound = 55.0f;
constexpr float kWaterLevel = -8.0f;           // coastal sea plane
constexpr float kValleyFloorZ = -18.0f;        // 10 m above sea level
constexpr float kCoastalSeabedZ = 7.0f;        // 15 m deep SE/E coastal naval strait

// Faction main base plateaus (SW Forseti vs NE Muspell).
constexpr float kForsetiHqX = 112.0f;
constexpr float kForsetiHqY = 112.0f;
constexpr float kMuspellHqX = 400.0f;
constexpr float kMuspellHqY = 400.0f;
constexpr float kBasePlateauZ = -18.0f;

// Elevated Sanctuary Museum Citadel flank plateau (NW flank — shared geometry).
constexpr float kCitadelCenterX = 112.0f;
constexpr float kCitadelCenterY = 392.0f;
constexpr float kCitadelFloorZ = -34.0f;       // 26 m above sea level, 16 m above valley
constexpr float kCitadelParapetZ = -38.0f;     // 4 m raised North/West/East perimeter parapet
constexpr float kCitadelMinX = 72.0f;
constexpr float kCitadelMaxX = 152.0f;
constexpr float kCitadelMinY = 352.0f;
constexpr float kCitadelMaxY = 432.0f;

// Grand 16 m wide south-east access ramp from Citadel terrace down to valley.
constexpr float kRampTopX = 128.0f;
constexpr float kRampTopY = 352.0f;
constexpr float kRampBottomX = 144.0f;
constexpr float kRampBottomY = 316.0f;
constexpr float kRampHalfWidth = 8.0f;

// P0 player spawn and emergency Citadel recall pad facing SE toward (256, 256).
constexpr float kPlayerSpawnX = 112.0f;
constexpr float kPlayerSpawnY = 366.0f;
constexpr float kPlayerSpawnZ = -34.0f;
constexpr float kPlayerSpawnYaw = -2.2232f;    // (-sin(yaw), cos(yaw)) -> (+0.795, -0.607)

// Sanctuary shield dome & director keep-out radii around (112, 392).
constexpr float kCitadelDomeRadius = 56.0f;
constexpr float kDirectorKeepOutRadius = 64.0f;
constexpr float kDirectorSteerClearRadius = 68.0f;

// Deep SE/E coastal naval strait bounds.
constexpr float kStraitMinX = 310.0f;
constexpr float kStraitMaxX = 485.0f;
constexpr float kStraitMinY = 40.0f;
constexpr float kStraitMaxY = 220.0f;

struct CaptureNodeSpec {
	const char *name;
	const char *shortTag;
	float x, y;
	float targetZ;
	float initialControl; // [-100 Muspell .. +100 Forseti]
};

inline const CaptureNodeSpec &CaptureNode(unsigned idx) {
	static const CaptureNodeSpec kNodes[kCaptureNodeCount] = {
		{"FORSETI FORWARD",    "FWD-F", 176.0f, 184.0f, -18.0f,  65.0f},
		{"WEST RIDGE",         "RIDGE", 208.0f, 272.0f, -24.0f,   0.0f},
		{"CENTRAL CROSSROADS", "CROSS", 256.0f, 256.0f, -18.0f,   0.0f},
		{"COASTAL HARBOR",     "HARBR", 320.0f, 200.0f, -13.5f,   0.0f},
		{"MUSPELL FORWARD",    "FWD-M", 336.0f, 328.0f, -18.0f, -65.0f},
	};
	return kNodes[idx < kCaptureNodeCount ? idx : 0u];
}

// Verified retail PC unit definitions backed by 700_res_PC.aya and standalone preloads.
constexpr const char *kPlayerConfig = "Standard";
// Forseti units
constexpr const char *kUnitForsetiHq = "Forseti Pulse Tank Factory";
constexpr const char *kUnitBroadsword = "M-1 Broadsword Main Battle Tank";
constexpr const char *kUnitSabre = "AV-14A Sabre Pulse Tank";
constexpr const char *kUnitBeamTank = "Beam Tank";
constexpr const char *kUnitPlasmaArtillery = "Plasma Artillery";
constexpr const char *kUnitForsetiTrooper = "Forseti Trooper";
constexpr const char *kUnitVenom = "XF-28 Venom";
constexpr const char *kUnitWildcatJason = "Jason Fighter";
constexpr const char *kUnitWildcatBilly = "Billy Fighter";
constexpr const char *kUnitWildcatTara = "Tara Fighter";
constexpr const char *kUnitMonitorBomber = "B-7 Monitor Bomber";
constexpr const char *kUnitMarshall = "FSV Marshall";
constexpr const char *kUnitVenturer = "Aircarrier"; // optional standalone preload, fallback FSV Marshall
// Muspell units
constexpr const char *kUnitMuspellHq = "Muspell Mech Factory";
constexpr const char *kUnitHeavyGunTank = "Heavy Gun Tank";
constexpr const char *kUnitGunTank = "Gun Tank";
constexpr const char *kUnitMortarTank = "Mortar Tank";
constexpr const char *kUnitRocketTank = "Rocket Tank";
constexpr const char *kUnitHeavyMech = "Heavy Mech";
constexpr const char *kUnitHeavyMech2 = "Heavy Mech 2";
constexpr const char *kUnitCrabMech = "Crab Mech";
constexpr const char *kUnitArachnadrone = "Arachnadrone";
constexpr const char *kUnitMuspellGrunt = "Muspell Grunt";
constexpr const char *kUnitMuspellWarrior = "Muspell Warrior";
constexpr const char *kUnitMuspellFighter = "Muspell Fighter";
constexpr const char *kUnitMuspellAdvFighter = "Muspell Advanced Fighter";
constexpr const char *kUnitEnforcer = "A-19 Enforcer";
constexpr const char *kUnitSkyTyrant = "B-4 Sky Tyrant Bomber";
constexpr const char *kUnitManticore = "Manticore BB";
// Turrets
constexpr const char *kUnitFlakTurret = "Flak Turret";
constexpr const char *kUnitCannonTurret = "Cannon Turret";
constexpr const char *kUnitSamSite = "SAM Site";

constexpr unsigned kRequiredUnitCount = 16u;
inline const char *RequiredUnitName(unsigned index) {
	static const char *const kNames[kRequiredUnitCount] = {
		kUnitForsetiHq,
		kUnitBroadsword,
		kUnitSabre,
		kUnitBeamTank,
		kUnitPlasmaArtillery,
		kUnitForsetiTrooper,
		kUnitVenom,
		kUnitMarshall,
		kUnitHeavyGunTank,
		kUnitGunTank,
		kUnitHeavyMech,
		kUnitCrabMech,
		kUnitMuspellGrunt,
		kUnitMuspellFighter,
		kUnitMuspellHq,
		kUnitManticore,
	};
	return index < kRequiredUnitCount ? kNames[index] : nullptr;
}

// Standalone retail mesh/textures for Forseti Pulse Tank Factory HQ
// (not packed inside 700_res_PC.aya; Chrome3.tga is already in 700_res_PC.aya).
constexpr unsigned kForsetiHqMeshCount = 1u;
inline const char *ForsetiHqMeshName(unsigned index) {
	return index == 0u ? "fb_tank_factory.msh" : nullptr;
}

constexpr unsigned kForsetiHqTextureCount = 4u;
inline const char *ForsetiHqTextureName(unsigned index) {
	static const char *const kTextures[kForsetiHqTextureCount] = {
		"meshtex\\A8_FB_hangermorebits_lit.tga",
		"meshtex\\FB_hangerbits.tga",
		"meshtex\\FB_hangertop01.tga",
		"meshtex\\FB_hangertop02.tga",
	};
	return index < kForsetiHqTextureCount ? kTextures[index] : nullptr;
}

// Optional standalone meshes/textures for F.S. Venturer escalation flagship.
// Gracefully falls back to FSV Marshall if standalone files are absent.
constexpr unsigned kOptionalMeshCount = 2u;
inline const char *OptionalMeshName(unsigned index) {
	static const char *const kMeshes[kOptionalMeshCount] = {
		"f_ventura.msh",
		"f_ventura_gun.msh",
	};
	return index < kOptionalMeshCount ? kMeshes[index] : nullptr;
}

constexpr unsigned kOptionalTextureCount = 4u;
inline const char *OptionalTextureName(unsigned index) {
	static const char *const kTextures[kOptionalTextureCount] = {
		"meshtex\\f_ventura01.tga",
		"meshtex\\f_ventura02.tga",
		"meshtex\\f_ventura03.tga",
		"meshtex\\F_Ventura_Gun_Texture_512.tga",
	};
	return index < kOptionalTextureCount ? kTextures[index] : nullptr;
}

// Standalone retail meshes this theater's units can put on screen but that
// 700_res_PC.aya does not pack. A mesh the engine cannot find is drawn as its
// missing-mesh marker (default.msh, a green "?" cube), so each of these is
// loaded from its own data/resources/meshes/m_<name>.aya before the level
// starts. Every one is optional: a file an install lacks is skipped and logged,
// never a refusal. `texture` names the one texture the level archive lacks too
// (nullptr when the archive already holds all the mesh needs).
//   m_rocket.msh                 every rocket round and every escape pod (the
//                                engine names this mesh itself, in Rocket.cpp
//                                and EscapePod.cpp): the floating "?" cube
//   fa_wildcard_t_destroyed.msh  a downed Tara Wildcat
//   m_innerwreckage.msh          inner hull wreckage
//   fa_f24.msh                   the base air fighter the Wildcats derive from
struct SupportMeshSpec {
	const char *mesh;
	const char *texture;
};

constexpr unsigned kSupportMeshCount = 7u;
inline const SupportMeshSpec &SupportMesh(unsigned index) {
	static const SupportMeshSpec kMeshes[kSupportMeshCount] = {
		{"m_rocket.msh",                "meshtex\\m_rocket.TGA"},
		{"fa_wildcard_t_destroyed.msh", nullptr},
		{"m_innerwreckage.msh",         "meshtex\\BOSSfenrir-FRONT.tga"},
		{"fa_f24.msh",                  nullptr},
		{"Fa_wildcard_j.msh",           nullptr},
		{"Fa_wildcard_t.msh",           nullptr},
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
	unsigned n = 0u;
	while (n <= kNameLimit && text[n]) {
		const unsigned char c = static_cast<unsigned char>(text[n]);
		if (c < 32u || c >= 127u) return false;
		++n;
	}
	if (n == 0u || n > kNameLimit) return false;
	if (outLen) *outLen = n;
	return true;
}

inline bool IsForbiddenUnit(const char *unitName) {
	if (!ValidPrintableName(unitName)) return true;
	if (StrEqualsIgnoreCase(unitName, "Fenrir Multiplayer")) return true;
	return false;
}

inline float Clamp01(float t) {
	return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
}

inline float SmoothStep01(float t) {
	const float u = Clamp01(t);
	return u * u * (3.0f - 2.0f * u);
}

inline float SmoothStepRange(float edge0, float edge1, float x) {
	if (edge1 == edge0) return x >= edge1 ? 1.0f : 0.0f;
	return SmoothStep01((x - edge0) / (edge1 - edge0));
}

inline float Lerp(float a, float b, float t) {
	return a + (b - a) * t;
}

// Distance from (x, y) to line segment (ax, ay)-(bx, by), plus parameter t in [0..1].
inline float DistToSegment(float x, float y, float ax, float ay, float bx, float by, float *outT = nullptr) {
	const float vx = bx - ax;
	const float vy = by - ay;
	const float len2 = vx * vx + vy * vy;
	float t = 0.0f;
	if (len2 > 1e-6f) {
		t = Clamp01(((x - ax) * vx + (y - ay) * vy) / len2);
	}
	if (outT) *outT = t;
	const float px = ax + vx * t;
	const float py = ay + vy * t;
	return sqrtf((x - px) * (x - px) + (y - py) * (y - py));
}

// Returns true when (x, y) lies inside the Sanctuary Dome keep-out circle.
inline bool IsInsideCitadelKeepOut(float x, float y, float radius = kDirectorKeepOutRadius) {
	const float dx = x - kCitadelCenterX;
	const float dy = y - kCitadelCenterY;
	return (dx * dx + dy * dy) < (radius * radius);
}

// Projects any point inside the Sanctuary keep-out radius cleanly outside onto
// the valley-facing perimeter so AI units and spawns never enter the dome.
inline void ClampOutsideCitadelKeepOut(float &x, float &y, float minRadius = kDirectorSteerClearRadius) {
	const float dx = x - kCitadelCenterX;
	const float dy = y - kCitadelCenterY;
	const float d2 = dx * dx + dy * dy;
	if (d2 >= minRadius * minRadius) return;
	float nx = dx, ny = dy;
	const float d = sqrtf(d2);
	if (d > 1e-3f) {
		nx /= d;
		ny /= d;
	} else {
		// Default push direction: south-east toward central crossroads.
		nx = 0.780869f;
		ny = -0.624695f;
	}
	x = kCitadelCenterX + nx * minRadius;
	y = kCitadelCenterY + ny * minRadius;
	if (x < 32.0f) x = 32.0f;
	if (x > 480.0f) x = 480.0f;
	if (y < 32.0f) y = 32.0f;
	if (y > 480.0f) y = 480.0f;
}

// Returns true inside the exact flat Sanctuary Citadel terrace footprint.
inline bool IsOnCitadelTerrace(float x, float y) {
	return x >= kCitadelMinX && x <= kCitadelMaxX &&
	       y >= kCitadelMinY && y <= kCitadelMaxY;
}

// Returns [0..1] blend factor indicating proximity to the grand road network
// (used by BuildProceduralMixerMap for road/concrete surface texturing).
inline float RoadBlendAt(float x, float y) {
	static const float kSegs[6][4] = {
		{kForsetiHqX, kForsetiHqY, 176.0f, 184.0f},
		{176.0f, 184.0f, 256.0f, 256.0f},
		{256.0f, 256.0f, 336.0f, 328.0f},
		{336.0f, 328.0f, kMuspellHqX, kMuspellHqY},
		{176.0f, 184.0f, 208.0f, 272.0f},
		{kRampTopX, kRampTopY, kRampBottomX, kRampBottomY},
	};
	float best = 0.0f;
	for (unsigned i = 0; i < 6u; ++i) {
		const float d = DistToSegment(x, y, kSegs[i][0], kSegs[i][1], kSegs[i][2], kSegs[i][3]);
		const float halfW = (i == 5u) ? 7.5f : 5.5f;
		if (d < halfW + 4.0f) {
			const float w = 1.0f - SmoothStepRange(halfW, halfW + 4.0f, d);
			if (w > best) best = w;
		}
	}
	return best;
}

// Returns [0..1] blend factor for fortified concrete base / node / citadel pads.
inline float ConcretePadBlendAt(float x, float y) {
	if (x >= kCitadelMinX - 1.0f && x <= kCitadelMaxX + 1.0f &&
	    y >= kCitadelMinY - 1.0f && y <= kCitadelMaxY + 1.0f) {
		return 1.0f;
	}
	float best = 0.0f;
	auto circlePad = [&](float cx, float cy, float rInner, float rOuter) {
		const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
		if (d < rOuter) {
			const float w = 1.0f - SmoothStepRange(rInner, rOuter, d);
			if (w > best) best = w;
		}
	};
	circlePad(kForsetiHqX, kForsetiHqY, 22.0f, 30.0f);
	circlePad(kMuspellHqX, kMuspellHqY, 22.0f, 30.0f);
	for (unsigned i = 0; i < kCaptureNodeCount; ++i) {
		const CaptureNodeSpec &n = CaptureNode(i);
		circlePad(n.x, n.y, 10.0f, 16.0f);
	}
	return best;
}

// ---------------------------------------------------------------------------
// Clean-room procedural 512 x 512 m multi-biome combined-arms heightfield.
//   - Rolling central valley around Z = -18.0 m (10 m above sea level)
//   - West Ridge elevated tactical flank around (208, 272) at Z = -24.0 m
//   - Forseti HQ SW plateau (112, 112) and Muspell HQ NE plateau (400, 400)
//   - Deep SE/E coastal naval strait X in [310..485], Y in [40..220], Z = +7.0 m
//   - Elevated Sanctuary Citadel NW plateau X in [72..152], Y in [352..432],
//     exact flat terrace Z = -34.0 m, 4 m raised North/West/East parapet
//     berm Z = -38.0 m outside the flat terrace, and smooth 16 m wide
//     south-east grand access ramp from (128, 352, -34) to (144, 316, -18).
// ---------------------------------------------------------------------------
inline float HeightAt(float x, float y) {
	if (!(x >= 0.0f && x <= static_cast<float>(kCells)) ||
	    !(y >= 0.0f && y <= static_cast<float>(kCells))) {
		return kValleyFloorZ;
	}

	// 1. Gently undulating central valley biome around Z = -18.0 m.
	const float undulation =
		1.85f * sinf(x * 0.023f + y * 0.017f) +
		1.15f * cosf(x * 0.041f - y * 0.031f) +
		0.55f * sinf((x - y) * 0.067f);
	float z = kValleyFloorZ + undulation;

	// 2. Tactical West Ridge high ground around (208, 272) rising to ~ -24.0 m.
	{
		const float dx = (x - 208.0f) / 48.0f;
		const float dy = (y - 272.0f) / 38.0f;
		const float r2 = dx * dx + dy * dy;
		if (r2 < 4.5f) {
			const float lift = 6.4f * expf(-r2 * 1.15f);
			z -= lift;
		}
	}

	// 3. Framing outer alpine/basalt border mountains (keeping SE naval channel open).
	{
		float edgeLift = 0.0f;
		// West border mountains
		if (x < 46.0f) {
			const float t = 1.0f - SmoothStepRange(10.0f, 46.0f, x);
			edgeLift = (t * 22.0f > edgeLift) ? (t * 22.0f) : edgeLift;
		}
		// North border mountains
		if (y > 466.0f) {
			const float t = SmoothStepRange(466.0f, 502.0f, y);
			edgeLift = (t * 22.0f > edgeLift) ? (t * 22.0f) : edgeLift;
		}
		// South-West border ridge (stops before SE naval strait x > 280)
		if (y < 46.0f && x < 290.0f) {
			const float ty = 1.0f - SmoothStepRange(10.0f, 46.0f, y);
			const float tx = 1.0f - SmoothStepRange(240.0f, 290.0f, x);
			const float l = ty * tx * 18.0f;
			if (l > edgeLift) edgeLift = l;
		}
		// North-East border ridge (above coastal strait y > 245)
		if (x > 466.0f && y > 235.0f) {
			const float tx = SmoothStepRange(466.0f, 502.0f, x);
			const float ty = SmoothStepRange(235.0f, 285.0f, y);
			const float l = tx * ty * 20.0f;
			if (l > edgeLift) edgeLift = l;
		}
		if (edgeLift > 0.0f) {
			const float crag = 2.8f * sinf(x * 0.11f + y * 0.09f) + 1.6f * cosf(x * 0.17f - y * 0.15f);
			z -= edgeLift + crag * SmoothStepRange(0.0f, 12.0f, edgeLift);
		}
	}

	// 4. Deep SE/E coastal naval strait: X in [310..485], Y in [40..220], seabed Z = +7.0 m.
	// Also keep the open SE ocean corner (x >= 320, y <= 210) cleanly submerged so naval
	// task forces have sweeping maneuvering room right off Coastal Harbor (320, 200).
	{
		// Inner deep basin mask (smooth shoreline transition over 26 m).
		const float mxIn = SmoothStepRange(296.0f, 332.0f, x);
		const float mxOut = 1.0f - SmoothStepRange(478.0f, 506.0f, x);
		const float myIn = SmoothStepRange(16.0f, 46.0f, y);
		const float myOut = 1.0f - SmoothStepRange(186.0f, 224.0f, y);
		float straitMask = mxIn * mxOut * myIn * myOut;

		// Preserve a gentle coastal harbor bluff around (320, 200) so ground troops capture Node 3 dry.
		const float hdx = x - 320.0f;
		const float hdy = y - 200.0f;
		const float hDist = sqrtf(hdx * hdx + hdy * hdy);
		const float harborCut = SmoothStepRange(18.0f, 34.0f, hDist);
		straitMask *= harborCut;

		if (straitMask > 0.001f) {
			const float seabedRipple = 0.6f * sinf(x * 0.045f + y * 0.035f);
			z = Lerp(z, kCoastalSeabedZ + seabedRipple, straitMask);
		}
	}

	// 5. Flatten Forseti HQ (112, 112), Muspell HQ (400, 400), and the 5 capture nodes.
	auto flattenCircle = [&](float cx, float cy, float flatR, float blendR, float targetZ) {
		const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
		if (d < blendR) {
			const float w = 1.0f - SmoothStepRange(flatR, blendR, d);
			z = Lerp(z, targetZ, w);
		}
	};
	flattenCircle(kForsetiHqX, kForsetiHqY, 26.0f, 42.0f, kBasePlateauZ);
	flattenCircle(kMuspellHqX, kMuspellHqY, 26.0f, 42.0f, kBasePlateauZ);
	for (unsigned i = 0; i < kCaptureNodeCount; ++i) {
		const CaptureNodeSpec &n = CaptureNode(i);
		flattenCircle(n.x, n.y, 12.0f, 24.0f, n.targetZ);
	}

	// 6. Elevated Sanctuary Museum Citadel Flank Plateau (NW flank).
	// Exact flat terrace: X in [72..152], Y in [352..432], Z = -34.0 m.
	// Outside the terrace, cliff drops smoothly over 22 m to the valley, while the
	// North, West, and East perimeter edges carry a 4 m raised parapet berm (Z = -38.0 m),
	// leaving the South-East overlook wide open to the valley below.
	{
		const float dxOut = (x < kCitadelMinX) ? (kCitadelMinX - x)
		                  : (x > kCitadelMaxX) ? (x - kCitadelMaxX) : 0.0f;
		const float dyOut = (y < kCitadelMinY) ? (kCitadelMinY - y)
		                  : (y > kCitadelMaxY) ? (y - kCitadelMaxY) : 0.0f;
		const float boxDist = sqrtf(dxOut * dxOut + dyOut * dyOut);

		if (boxDist < 24.0f) {
			const float plateauW = 1.0f - SmoothStepRange(0.0f, 22.0f, boxDist);
			z = Lerp(z, kCitadelFloorZ, plateauW);
		}

		// 4 m raised parapet rampart berm (Z = -38.0 m) strictly outside the flat
		// terrace footprint along North (Y in [432..444]), West (X in [60..72]),
		// and East (X in [152..164], Y >= 364 so South-East overlook stays wide open!).
		float bermW = 0.0f;
		if (y > kCitadelMaxY && y < kCitadelMaxY + 13.0f &&
		    x >= kCitadelMinX - 10.0f && x <= kCitadelMaxX + 10.0f) {
			const float rise = SmoothStepRange(kCitadelMaxY, kCitadelMaxY + 4.0f, y);
			const float fall = 1.0f - SmoothStepRange(kCitadelMaxY + 7.5f, kCitadelMaxY + 13.0f, y);
			const float side = SmoothStepRange(kCitadelMinX - 10.0f, kCitadelMinX - 2.0f, x) *
			                   (1.0f - SmoothStepRange(kCitadelMaxX + 2.0f, kCitadelMaxX + 10.0f, x));
			const float w = rise * fall * side;
			if (w > bermW) bermW = w;
		}
		if (x < kCitadelMinX && x > kCitadelMinX - 13.0f &&
		    y >= kCitadelMinY + 6.0f && y <= kCitadelMaxY + 10.0f) {
			const float rise = SmoothStepRange(kCitadelMinX, kCitadelMinX - 4.0f, x);
			const float fall = 1.0f - SmoothStepRange(kCitadelMinX - 7.5f, kCitadelMinX - 13.0f, x);
			const float side = SmoothStepRange(kCitadelMinY + 6.0f, kCitadelMinY + 16.0f, y) *
			                   (1.0f - SmoothStepRange(kCitadelMaxY + 2.0f, kCitadelMaxY + 10.0f, y));
			const float w = rise * fall * side;
			if (w > bermW) bermW = w;
		}
		if (x > kCitadelMaxX && x < kCitadelMaxX + 13.0f &&
		    y >= kCitadelMinY + 16.0f && y <= kCitadelMaxY + 10.0f) {
			const float rise = SmoothStepRange(kCitadelMaxX, kCitadelMaxX + 4.0f, x);
			const float fall = 1.0f - SmoothStepRange(kCitadelMaxX + 7.5f, kCitadelMaxX + 13.0f, x);
			const float side = SmoothStepRange(kCitadelMinY + 16.0f, kCitadelMinY + 26.0f, y) *
			                   (1.0f - SmoothStepRange(kCitadelMaxY + 2.0f, kCitadelMaxY + 10.0f, y));
			const float w = rise * fall * side;
			if (w > bermW) bermW = w;
		}
		if (bermW > 0.001f) {
			const float bermZ = Lerp(kCitadelFloorZ, kCitadelParapetZ, bermW);
			if (bermZ < z) z = bermZ;
		}
	}

	// 7. Smooth 16 m wide South-East Grand Access Ramp descending from
	//    (128, 352, Z=-34.0) to (144, 316, Z=-18.0) without disturbing the flat terrace.
	if (!IsOnCitadelTerrace(x, y)) {
		float rampT = 0.0f;
		const float rampDist = DistToSegment(x, y, kRampTopX, kRampTopY, kRampBottomX, kRampBottomY, &rampT);
		if (rampDist < kRampHalfWidth + 7.0f) {
			const float lateralW = 1.0f - SmoothStepRange(kRampHalfWidth, kRampHalfWidth + 7.0f, rampDist);
			const float rampZ = Lerp(kCitadelFloorZ, kValleyFloorZ, SmoothStep01(rampT));
			z = Lerp(z, rampZ, lateralW);
		}
	}

	// 8. Re-assert exact flat terrace inside [72..152] x [352..432] so Citadel
	//    architecture, Archive Pedestal, and Cinema Room always sit dead-level.
	if (IsOnCitadelTerrace(x, y)) {
		z = kCitadelFloorZ;
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

inline uint8_t ComputeCellLight(int x, int y, float sunX, float sunY, float sunZ) {
	const float x0 = static_cast<float>(x > 0 ? x - 1 : 0);
	const float x1 = static_cast<float>(x < kCells - 1 ? x + 1 : kCells - 1);
	const float y0 = static_cast<float>(y > 0 ? y - 1 : 0);
	const float y1 = static_cast<float>(y < kCells - 1 ? y + 1 : kCells - 1);
	const float dzdx = (HeightAt(x1, static_cast<float>(y)) - HeightAt(x0, static_cast<float>(y))) /
	                   ((x1 - x0) > 0.5f ? (x1 - x0) : 1.0f);
	const float dzdy = (HeightAt(static_cast<float>(x), y1) - HeightAt(static_cast<float>(x), y0)) /
	                   ((y1 - y0) > 0.5f ? (y1 - y0) : 1.0f);
	float nx = dzdx, ny = dzdy, nz = -1.0f;
	const float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
	if (nlen > 1e-4f) { nx /= nlen; ny /= nlen; nz /= nlen; }
	float slen = sqrtf(sunX * sunX + sunY * sunY + sunZ * sunZ);
	float lx = sunX, ly = sunY, lz = sunZ;
	if (slen > 1e-4f) { lx /= slen; ly /= slen; lz /= slen; }
	float ndotl = nx * lx + ny * ly + nz * lz;
	if (ndotl < 0.0f) ndotl = 0.0f;
	if (ndotl > 1.0f) ndotl = 1.0f;
	// Grade terrain lightmap to cool moonlit night [14..30] so the battlefield
	// harmonizes with the starry moon skybox (kSkyCubeIndex 16) and glowing Citadel.
	const int shade = static_cast<int>(14.0f + ndotl * 16.0f + 0.5f);
	if (shade < 14) return 14u;
	if (shade > 32) return 32u;
	return static_cast<uint8_t>(shade);
}

// ---------------------------------------------------------------------------
// Clean-room authored version-50 WORLD body serializer (zero heap allocation,
// zero retail scripts, zero copied campaign actors).
// ---------------------------------------------------------------------------
struct UnitSpawnSpec {
	const char *actorName;
	const char *unitDef;
	int allegiance;
	float position[3];
	float orientation[3];
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

// Initial combined-arms deployment so frame 1 showcases the Citadel overlook
// looking straight down across an active Forseti vs Muspell battle across the
// valley, West Ridge, Central Crossroads, and SE coastal strait.
inline unsigned BuildDefaultOrderOfBattle(UnitSpawnSpec *outUnits, unsigned capacity) {
	if (!outUnits || capacity < 28u) return 0u;
	unsigned n = 0u;
	auto addGround = [&](const char *actor, const char *def, int alleg, float x, float y, float yaw) {
		const float z = HeightAt(x, y);
		outUnits[n++] = {actor, def, alleg, {x, y, z}, {yaw, 0.0f, 0.0f}};
	};
	auto addAir = [&](const char *actor, const char *def, int alleg, float x, float y, float z, float yaw) {
		outUnits[n++] = {actor, def, alleg, {x, y, z}, {yaw, 0.0f, 0.0f}};
	};

	static constexpr float kYawNE = -0.7854f; // (+0.707, +0.707)
	static constexpr float kYawSW =  2.3562f; // (-0.707, -0.707)
	static constexpr float kYawE  = -1.5708f; // (+1, 0)
	static constexpr float kYawW  =  1.5708f; // (-1, 0)

	// Forseti HQ core & base perimeter defenses (SW plateau)
	addGround("ce.sb.hq.forseti",       kUnitForsetiHq,       kAllegianceForseti, kForsetiHqX, kForsetiHqY, kYawNE);
	addGround("ce.sb.turret.f1",        kUnitFlakTurret,      kAllegianceForseti, 130.0f, 122.0f, kYawNE);
	addGround("ce.sb.turret.f2",        kUnitCannonTurret,    kAllegianceForseti, 122.0f, 132.0f, kYawNE);

	// Forseti initial combined-arms vanguard advancing from Forseti Forward toward Crossroads
	addGround("ce.sb.f.broadsword.1",   kUnitBroadsword,      kAllegianceForseti, 184.0f, 196.0f, kYawNE);
	addGround("ce.sb.f.broadsword.2",   kUnitBroadsword,      kAllegianceForseti, 198.0f, 184.0f, kYawNE);
	addGround("ce.sb.f.sabre.1",        kUnitSabre,           kAllegianceForseti, 212.0f, 224.0f, kYawNE);
	addGround("ce.sb.f.sabre.2",        kUnitSabre,           kAllegianceForseti, 224.0f, 214.0f, kYawNE);
	addGround("ce.sb.f.beam.1",         kUnitBeamTank,        kAllegianceForseti, 206.0f, 256.0f, kYawNE);
	addGround("ce.sb.f.arty.1",         kUnitPlasmaArtillery, kAllegianceForseti, 168.0f, 176.0f, kYawNE);
	addGround("ce.sb.f.troop.1",        kUnitForsetiTrooper,  kAllegianceForseti, 218.0f, 232.0f, kYawNE);
	addGround("ce.sb.f.troop.2",        kUnitForsetiTrooper,  kAllegianceForseti, 228.0f, 222.0f, kYawNE);
	addAir   ("ce.sb.f.venom.1",        kUnitVenom,           kAllegianceForseti, 190.0f, 210.0f, -46.0f, kYawNE);
	addAir   ("ce.sb.f.wildcat.1",      kUnitWildcatJason,    kAllegianceForseti, 214.0f, 200.0f, -50.0f, kYawNE);

	// Muspell HQ core & base perimeter defenses (NE plateau)
	addGround("ce.sb.hq.muspell",       kUnitMuspellHq,       kAllegianceMuspell, kMuspellHqX, kMuspellHqY, kYawSW);
	addGround("ce.sb.turret.m1",        kUnitFlakTurret,      kAllegianceMuspell, 382.0f, 388.0f, kYawSW);
	addGround("ce.sb.turret.m2",        kUnitSamSite,         kAllegianceMuspell, 388.0f, 378.0f, kYawSW);

	// Muspell initial combined-arms vanguard advancing from Muspell Forward toward Crossroads
	addGround("ce.sb.m.htank.1",        kUnitHeavyGunTank,    kAllegianceMuspell, 318.0f, 314.0f, kYawSW);
	addGround("ce.sb.m.htank.2",        kUnitHeavyGunTank,    kAllegianceMuspell, 304.0f, 326.0f, kYawSW);
	addGround("ce.sb.m.gtank.1",        kUnitGunTank,         kAllegianceMuspell, 286.0f, 282.0f, kYawSW);
	addGround("ce.sb.m.gtank.2",        kUnitGunTank,         kAllegianceMuspell, 296.0f, 272.0f, kYawSW);
	addGround("ce.sb.m.mech.1",         kUnitHeavyMech,       kAllegianceMuspell, 276.0f, 288.0f, kYawSW);
	addGround("ce.sb.m.crab.1",         kUnitCrabMech,        kAllegianceMuspell, 284.0f, 264.0f, kYawSW);
	addGround("ce.sb.m.mortar.1",       kUnitMortarTank,      kAllegianceMuspell, 340.0f, 332.0f, kYawSW);
	addGround("ce.sb.m.grunt.1",        kUnitMuspellGrunt,    kAllegianceMuspell, 278.0f, 272.0f, kYawSW);
	addGround("ce.sb.m.grunt.2",        kUnitMuspellGrunt,    kAllegianceMuspell, 290.0f, 260.0f, kYawSW);
	addAir   ("ce.sb.m.fighter.1",      kUnitMuspellFighter,  kAllegianceMuspell, 312.0f, 304.0f, -48.0f, kYawSW);
	addAir   ("ce.sb.m.advfite.1",      kUnitMuspellAdvFighter, kAllegianceMuspell, 326.0f, 292.0f, -52.0f, kYawSW);

	// Opening coastal strait naval duel off Coastal Harbor (X in [345..450], Y in [90..175])
	outUnits[n++] = {"ce.sb.naval.marshall", kUnitMarshall, kAllegianceForseti,
	                 {358.0f, 126.0f, kWaterLevel}, {kYawE, 0.0f, 0.0f}};
	outUnits[n++] = {"ce.sb.naval.manticore", kUnitManticore, kAllegianceMuspell,
	                 {436.0f, 146.0f, kWaterLevel}, {kYawW, 0.0f, 0.0f}};
	return n;
}

inline WorldPlan BuildDefaultWorldPlan(const UnitSpawnSpec *units, unsigned unitCount) {
	WorldPlan plan = {};
	plan.playerConfig = kPlayerConfig;
	plan.baseLevel = kBaseWorldId;
	plan.mapNumber = kMapId;
	plan.startPosition[0] = kPlayerSpawnX;
	plan.startPosition[1] = kPlayerSpawnY;
	plan.startPosition[2] = kPlayerSpawnZ;
	plan.startOrientation[0] = kPlayerSpawnYaw;
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
		u32(0u);
		u32(static_cast<uint32_t>(allegiance));
		u32(0xffffffffu);
		zstr("");
		zstr(name);
		zstr("");
		u32(1u);
		u32(0u);
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
		if (IsForbiddenUnit(u.unitDef)) return false;
		if (u.allegiance != kAllegianceForseti && u.allegiance != kAllegianceMuspell)
			return false;
		if (!VecValid(u.position, -512.0f, 1024.0f) ||
		    !VecValid(u.orientation, -100.0f, 100.0f))
			return false;
		// Enforce Sanctuary Dome keep-out on every initial AI unit spawn.
		if (IsInsideCitadelKeepOut(u.position[0], u.position[1], kDirectorKeepOutRadius))
			return false;
	}
	return true;
}

inline bool WriteWorldBody(const WorldPlan &plan, bool real,
                           uint8_t *outBuf, unsigned outCap, unsigned *outSize) {
	if (!outBuf || !outSize || outCap < 256u) return false;
	*outSize = 0u;
	if (!ValidateWorldPlan(plan)) return false;

	FlatWriter w = {outBuf, outCap, 0u, false};
	w.u16(kWorldVersion);
	w.u32(3u); w.u32(0u); w.u32(0u);
	w.u32(1u); w.shortName(plan.playerConfig);
	w.u32(0u); w.u32(0u); w.u32(0u); w.u32(0u);
	w.u32(real ? static_cast<uint32_t>(plan.baseLevel) : 0xffffffffu);
	w.u32(0u);                                       // zero mission scripts
	w.u32(static_cast<uint32_t>(plan.mapNumber));
	w.u32(0u);                                       // zero named meshes

	const uint16_t thingCount = real ? static_cast<uint16_t>(1u + plan.unitCount) : 0u;
	w.u16(thingCount);
	if (real) {
		w.u32(kOidStart);
		w.initThing("ce.sb.player1", kAllegianceForseti, plan.startPosition, plan.startOrientation);
		w.u32(plan.startInJetMode ? 1u : 0u);
		w.u32(1u);

		for (unsigned i = 0; i < plan.unitCount; ++i) {
			const UnitSpawnSpec &u = plan.units[i];
			w.u32(kOidUnit);
			w.initThing(u.actorName, u.allegiance, u.position, u.orientation);
			w.shortName(u.unitDef);
			w.u32(0u);
		}
	}

	w.u16(0u); w.u32(0u);                            // zero tree groups & tree types
	w.u16(1u); w.u32(0u); w.u32(0u);                 // empty influence map
	w.u16(0u);                                       // zero waypoint paths
	for (unsigned i = 0; i < 5u; ++i) w.f32(0.0f);
	w.u32(0u); w.u32(0u);
	for (unsigned i = 0; i < 4u; ++i) w.f32(0.0f);
	for (unsigned i = 0; i < 3u; ++i) w.u32(0u);
	w.f32(0.0f);

	if (w.overflow || w.pos == 0u || w.pos > kMaxWorldBodyBytes) return false;
	*outSize = w.pos;
	return true;
}

struct WorldSummary {
	uint16_t version;
	int32_t baseLevel;
	uint32_t scriptCount;
	uint32_t mapNumber;
	uint16_t thingCount;
	uint16_t startCount;
	uint16_t unitCount;
	uint16_t forsetiCount;
	uint16_t muspellCount;
	bool sawForbiddenUnit;
	bool sawUnitInsideKeepOut;
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
	float f32() {
		const uint32_t bits = u32();
		float v = 0.0f;
		memcpy(&v, &bits, sizeof(v));
		return v;
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
	if (r.u32() != 3u) return false;
	r.skip(8u);
	if (r.u32() != 1u) return false;
	char cfg[132] = {};
	if (!r.readShortName(cfg, sizeof(cfg))) return false;
	r.skip(16u);
	out->baseLevel = static_cast<int32_t>(r.u32());
	out->scriptCount = r.u32();
	out->mapNumber = r.u32();
	const uint32_t namedMeshes = r.u32();
	if (out->scriptCount != 0u || namedMeshes != 0u) return false;
	out->thingCount = r.u16();
	for (uint16_t i = 0; i < out->thingCount; ++i) {
		const uint32_t oid = r.u32();
		const float px = r.f32();
		const float py = r.f32();
		(void)r.f32();
		r.skip(16u); // ori[3] + meshNo
		const uint32_t alleg = r.u32();
		r.skip(4u);  // target
		if (!r.skipZStr() || !r.skipZStr() || !r.skipZStr()) return false;
		r.skip(8u);  // active, attachScripts
		if (oid == kOidStart) {
			r.skip(8u);
			++out->startCount;
		} else if (oid == kOidUnit) {
			char def[132] = {};
			if (!r.readShortName(def, sizeof(def))) return false;
			r.skip(4u);
			++out->unitCount;
			if (alleg == kAllegianceForseti) ++out->forsetiCount;
			else if (alleg == kAllegianceMuspell) ++out->muspellCount;
			if (IsForbiddenUnit(def)) out->sawForbiddenUnit = true;
			if (IsInsideCitadelKeepOut(px, py, kDirectorKeepOutRadius))
				out->sawUnitInsideKeepOut = true;
		} else {
			return false;
		}
	}
	return !r.err && !out->sawForbiddenUnit && !out->sawUnitInsideKeepOut;
}

// Synthesizes the 325-byte PMSH/PMS2 chunk header that CMesh::Deserialize reads
// before loading data\resources\meshes\m_<meshName>.aya (optional Venturer preload).
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

enum PreflightFailure {
	kPreflightOk = 0,
	kPreflightMissingArchive,
	kPreflightCorruptArchive,
	kPreflightMissingDefinition,
	kPreflightWorldBuildFailed,
};

struct PreflightInput {
	bool archiveExists;
	bool archiveValid;
	bool unitsPresent[kRequiredUnitCount];
};

struct PreflightResult {
	PreflightFailure code;
	const char *message;
};

inline PreflightResult EvaluatePreflight(const PreflightInput &in) {
	if (!in.archiveExists) {
		return {kPreflightMissingArchive,
		        "Living Sandbox requires level 700 resources from your retail install. Your career is unchanged."};
	}
	if (!in.archiveValid) {
		return {kPreflightCorruptArchive,
		        "Living Sandbox could not read level 700 resources. Check your game install and try again."};
	}
	for (unsigned i = 0; i < kRequiredUnitCount; ++i) {
		if (!in.unitsPresent[i]) {
			return {kPreflightMissingDefinition,
			        "Living Sandbox could not find required unit definitions in your game data. Your career is unchanged."};
		}
	}
	return {kPreflightOk, ""};
}

} // namespace CeSandboxWorld

#endif
