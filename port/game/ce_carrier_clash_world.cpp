// SPDX-License-Identifier: GPL-3.0-only
// Engine adapter for Carrier Clash's clean-room procedural ocean arena,
// strict retail asset preflight, mesh preloading, and version-50 world bodies.
#ifdef CE_VERSION

#include "common.h"
#include "membuffer.h"
#include "chunker.h"
#include "world.h"
#include "map.h"
#include "mixermap.h"
#include "Texture.h"
#include "mesh.h"
#include "ltshell.h"
#include "engine.h"
#include "WorldPhysicsManager.h"
#include "oids.h"
#include "game.h"
#include "ce_carrier_clash.h"
#include "../platform/common/portlog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
int uncompress(unsigned char *dest, unsigned long *destLen,
               const unsigned char *source, unsigned long sourceLen);
}

static_assert(CeCarrierClashWorld::kOidUnit == OID_CUnit, "OID_CUnit mismatch");
static_assert(CeCarrierClashWorld::kOidStart == OID_CStart, "OID_CStart mismatch");
static_assert(CeCarrierClashWorld::kOidSquad == OID_CSquad, "OID_CSquad mismatch");
static_assert(CeCarrierClashWorld::kAllegianceForseti == kForsetiAllegiance, "Forseti allegiance mismatch");
static_assert(CeCarrierClashWorld::kAllegianceMuspell == kMuspellAllegiance, "Muspell allegiance mismatch");

namespace {

const unsigned long kAyaPackBuffer = 0x102927ul;
const unsigned long kAyaUnpackBlock = 0x100000ul;

struct WorldBodyStorage {
	uint8_t baseBytes[CeCarrierClashWorld::kMaxWorldBodyBytes];
	unsigned baseLen;
	uint8_t realBytes[CeCarrierClashWorld::kMaxWorldBodyBytes];
	unsigned realLen;
	unsigned readMask;
	bool ready;
	bool failed;
	bool arenaInstalled;
};

WorldBodyStorage gWorld = {};

struct EngineMediaAccess : public CEngine {
	static CDXKempyCube *KempyCube() {
		return static_cast<EngineMediaAccess *>(static_cast<CEngine *>(&ENGINE))->mKempyCube;
	}
	static CDXWater *Water() {
		return static_cast<EngineMediaAccess *>(static_cast<CEngine *>(&ENGINE))->mWater;
	}
};

bool ContainsIgnoreCase(const char *haystack, const char *needle) {
	if (!haystack || !needle || !needle[0]) return false;
	const size_t nlen = std::strlen(needle);
	for (const char *p = haystack; *p; ++p) {
		size_t i = 0u;
		while (i < nlen && p[i] &&
		       CeCarrierClashWorld::AsciiLower(static_cast<unsigned char>(p[i])) ==
		       CeCarrierClashWorld::AsciiLower(static_cast<unsigned char>(needle[i]))) {
			++i;
		}
		if (i == nlen) return true;
	}
	return false;
}

void RefreshProceduralMapExtents() {
	SINT minX = CeCarrierClashWorld::kCells;
	SINT minY = CeCarrierClashWorld::kCells;
	SINT maxX = -1;
	SINT maxY = -1;
	for (SINT y = 0; y < CeCarrierClashWorld::kCells; ++y) {
		for (SINT x = 0; x < CeCarrierClashWorld::kCells; ++x) {
			if (CeCarrierClashWorld::HeightAt(static_cast<float>(x), static_cast<float>(y)) < MAP.mWaterLevel) {
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
		}
	}
	if (minX > maxX || minY > maxY) {
		minX = 0;
		minY = 0;
		maxX = CeCarrierClashWorld::kCells - 1;
		maxY = CeCarrierClashWorld::kCells - 1;
	}
	SExtents ext = {};
	ext.mMinX = minX;
	ext.mMinY = minY;
	ext.mMaxX = maxX;
	ext.mMaxY = maxY;
	ext.mWaterLevel = MAP.mWaterLevel;
	static_assert(sizeof(ext) == sizeof(MAP.mUnknown000), "SExtents size mismatch");
	std::memcpy(MAP.mUnknown000, &ext, sizeof(ext));
}

// Validates that a packed retail .aya file exists and every zlib block unpacks
// cleanly without ever calling CONSOLE.FatalFileError.
bool VerifyPackedAyaFile(const char *gameRelPath, bool *outExists) {
	if (outExists) *outExists = false;
	if (!gameRelPath || !gameRelPath[0]) return false;

	HANDLE fh = CreateFile(gameRelPath, GENERIC_READ, FILE_SHARE_READ, NULL,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE) return false;
	if (outExists) *outExists = true;

	unsigned char *packed = static_cast<unsigned char *>(std::malloc(kAyaPackBuffer));
	unsigned char *unpacked = static_cast<unsigned char *>(std::malloc(kAyaPackBuffer));
	if (!packed || !unpacked) {
		std::free(packed);
		std::free(unpacked);
		CloseHandle(fh);
		return false;
	}

	bool ok = true;
	unsigned blocks = 0u;
	for (;;) {
		unsigned char head[4] = {};
		DWORD got = 0;
		if (!ReadFile(fh, head, 4, &got, NULL)) {
			ok = false;
			break;
		}
		if (got == 0) break;
		if (got != 4) { ok = false; break; }
		const unsigned long length =
			static_cast<unsigned long>(head[0]) |
			(static_cast<unsigned long>(head[1]) << 8) |
			(static_cast<unsigned long>(head[2]) << 16) |
			(static_cast<unsigned long>(head[3]) << 24);
		if (length == 0ul || length > kAyaPackBuffer) { ok = false; break; }
		DWORD bodyGot = 0;
		if (!ReadFile(fh, packed, static_cast<DWORD>(length), &bodyGot, NULL) ||
		    bodyGot != static_cast<DWORD>(length)) {
			ok = false;
			break;
		}
		unsigned long dstLen = kAyaPackBuffer;
		if (uncompress(unpacked, &dstLen, packed, length) != 0 ||
		    dstLen == 0ul || dstLen > kAyaUnpackBlock) {
			ok = false;
			break;
		}
		++blocks;
	}

	std::free(packed);
	std::free(unpacked);
	CloseHandle(fh);
	return ok && blocks > 0u;
}

void CanonicalizePath(const char *src, char *dst, size_t dstSize) {
	if (!dst || dstSize == 0u) return;
	dst[0] = '\0';
	if (!src) return;
	size_t i = 0u;
	for (; src[i] && i + 1u < dstSize; ++i) {
		char c = src[i];
		if (c == '\\') c = '/';
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
		dst[i] = c;
	}
	dst[i] = '\0';
}

void InferMeshTextureFormat(const char *texName, ETextureFormat *outFmt, const char **outSuffix) {
	ETextureFormat fmt = TEXFMT_A1R5G5B5;
	const char *suffix = "A1R5G5B5";
	if (texName) {
		const char *base = texName;
		for (const char *p = texName; *p; ++p) {
			if (*p == '\\' || *p == '/') base = p + 1;
		}
		if (strnicmp(base, "A8", 2) == 0) {
			fmt = TEXFMT_A8R8G8B8;
			suffix = "A8R8G8B8";
		} else if (strnicmp(base, "A4", 2) == 0) {
			fmt = TEXFMT_A4R4G4B4;
			suffix = "A4R4G4B4";
		}
	}
	if (outFmt) *outFmt = fmt;
	if (outSuffix) *outSuffix = suffix;
}

bool BuildTextureDiskPath(const char *texName, char *outPath, size_t outSize) {
	if (!texName || !texName[0] || !outPath || outSize < 64u) return false;
	const char *suffix = "A1R5G5B5";
	InferMeshTextureFormat(texName, NULL, &suffix);
	char mangled[160] = {};
	size_t i = 0u;
	for (; texName[i] && i + 1u < sizeof(mangled); ++i) {
		mangled[i] = (texName[i] == '\\') ? '%' : texName[i];
	}
	mangled[i] = '\0';
	std::snprintf(outPath, outSize, "data\\resources\\dxtntextures\\%s(0)%s.aya", mangled, suffix);
	return true;
}

bool PreloadRetailMeshTexture(const char *texName) {
	if (!texName || !texName[0] || !LT.GetDevice()) return false;
	ETextureFormat fmt = TEXFMT_A1R5G5B5;
	InferMeshTextureFormat(texName, &fmt, NULL);
	for (CDXTexture *o = CDXTexture::mFirst; o; o = o->CTexture::mNext) {
		if (stricmp(o->GetName(), texName) == 0 && o->mFormat == fmt &&
		    o->mLevels == -1 && o->mTextures[0] != NULL) {
			return true;
		}
	}
	CDXTexture *t = new(MEMTYPE_TEXTURE) CDXTexture;
	if (!t) return false;
	std::snprintf(t->mName, sizeof(t->mName), "%s", texName);
	t->mMode = 0;
	t->mOffsetU = 0.0f;
	t->mOffsetV = 0.0f;
	t->mScaleU = 1.0f;
	t->mScaleV = 1.0f;
	t->mUnknown94 = -1;
	t->mRefCount = 0;
	t->mFormat = fmt;
	t->mTexFormat = fmt;
	t->mWidth = 0;
	t->mHeight = 0;
	t->mLossShift = 0;
	t->mMipFilter = 1;
	t->mLevels = -1;
	t->mResourceLevels = 0;
	t->mNumFrames = 1;
	t->mPool = D3DPOOL_MANAGED;
	t->mCompressible = 1;
	t->mDisplayFormat = 0;
	t->mFlags155 = 0;
	t->mUnknown155b = 1;
	t->mLit = ContainsIgnoreCase(texName, "_lit") ? TRUE : FALSE;
	const HRESULT hr = t->LoadFrame(0);
	if (FAILED(hr) || !t->mTextures[0]) {
		t->ForceDelete();
		return false;
	}
	D3DSURFACE_DESC desc;
	std::memset(&desc, 0, sizeof(desc));
	if (SUCCEEDED(t->mTextures[0]->GetLevelDesc(0, &desc))) {
		t->mWidth = static_cast<SINT>(desc.Width);
		t->mHeight = static_cast<SWORD>(desc.Height);
	}
	return true;
}

bool LoadRetailMeshFromStandaloneAya(const char *meshName, int32_t runtimeId) {
	if (!meshName || !meshName[0]) return false;
	if (CMesh::FindLoadedUnique(meshName) != NULL) return true;

	char meshPath[160] = {};
	bool exists = false;
	std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya", meshName);
	if (!VerifyPackedAyaFile(meshPath, &exists) || !exists)
		return false;

	int32_t safeId = runtimeId;
	while (CMesh::GetMeshByResourceID(safeId) != NULL && safeId < runtimeId + 4096) {
		++safeId;
	}

	uint8_t stub[336] = {};
	unsigned stubLen = 0u;
	if (!CeCarrierClashWorld::BuildMeshStubChunk(meshName, safeId, true,
	                                             stub, sizeof(stub), &stubLen)) {
		return false;
	}

	CDXMemBuffer mem;
	if (!mem.InitOwnedRead(stub, stubLen, MEMTYPE_MEMBUFFER)) {
		return false;
	}
	CChunkReader reader;
	reader.Open(&mem);
	CMesh *loaded = CMesh::Deserialize(&reader);
	reader.Close();
	return loaded != NULL || CMesh::FindLoadedUnique(meshName) != NULL;
}

// Recomputes retail's sun-clamp + 2x color-table scaling identically to
// CHeightField::Load (src/HeightField.cpp:614-669) so terrain brightness and
// lighting match native maps cleanly.
void FinalizeHeightFieldLighting() {
	MAP.Unknown47e8e0();

	const DWORD ambient = MAP.mHFP.Ambient;
	const SINT ar = (ambient >> 16) & 0xff;
	const SINT ag = (ambient >> 8) & 0xff;
	const SINT ab = ambient & 0xff;
	const DWORD sun = MAP.mHFP.Sun;
	SINT r = ((sun >> 16) & 0xff) + ar;
	SINT g = ((sun >> 8) & 0xff) + ag;
	SINT b = (sun & 0xff) + ab;
	SINT m = r;
	if (b > m) m = b;
	if (g > m) m = g;
	if (m > 255) {
		r = (r * 255) / m;
		g = (g * 255) / m;
		b = (b * 255) / m;
	}
	MAP.mHFP.Sun = ((r - ar) << 16) | ((g - ag) << 8) | (b - ab) | 0xff000000u;

	for (int i = 0; i < 65; ++i) {
		MAP.mColors[i].red <<= 1;
		if (DWORD(MAP.mColors[i].red) > 0xf80000u) MAP.mColors[i].red = 0xf80000;
		MAP.mColors[i].red &= 0xf80000;
		MAP.mColors[i].green <<= 1;
		if (DWORD(MAP.mColors[i].green) > 0x7e000u) MAP.mColors[i].green = 0x7e000;
		MAP.mColors[i].green &= 0x7e000;
		MAP.mColors[i].blue <<= 1;
		if (DWORD(MAP.mColors[i].blue) > 0x1f00u) MAP.mColors[i].blue = 0x1f00;
		MAP.mColors[i].blue &= 0x1f00;
	}
}

bool BuildProceduralMixerMap(float sunX, float sunY, float sunZ) {
	MIXERMAP.Destroy();
	MIXERMAP.mSlots = new(MEMTYPE_MIXERMAP) SMixerSlot[MIXER_MAP_SLOTS];
	MIXERMAP.mMap = new(MEMTYPE_MIXERMAP) UBYTE[MIXER_MAP_SIZE];
	if (!MIXERMAP.mSlots || !MIXERMAP.mMap) {
		MIXERMAP.Destroy();
		return false;
	}

	// Lightmap: 512 x 512 directional sunset illumination in [14..62], with the
	// shadow each cliff and stack casts down its lee.
	for (int y = 0; y < CeCarrierClashWorld::kCells; ++y) {
		UBYTE *row = MIXERMAP.mMap + y * CeCarrierClashWorld::kCells;
		for (int x = 0; x < CeCarrierClashWorld::kCells; ++x) {
			row[x] = CeCarrierClashWorld::ComputeCellLightShadowed(x, y, sunX, sunY, sunZ);
		}
	}

	// Per-tile texture mixing over the base grass: a sand skirt along the
	// shoreline, basalt rock on steep faces, reefs and the stack. The alphas and
	// the signed 32-bit wrap proof CHeightField::Unknown47eff0 needs of them are
	// CeCarrierClashWorld::ComputeMixAlphas (sand and rock in [-62,+62], rock >=
	// max(0, sand) - 62). A tile where neither layer leaves -62 keeps no slot,
	// which draws the same as one that does, so tile edges cannot seam.
	for (int tileY = 0; tileY < CeCarrierClashWorld::kPatches; ++tileY) {
		for (int tileX = 0; tileX < CeCarrierClashWorld::kPatches; ++tileX) {
			SMixerSlot &slot = MIXERMAP.mSlots[tileY * CeCarrierClashWorld::kPatches + tileX];
			slot.mCount = 0;
			slot.mData = NULL;
			std::memset(slot.mTextures, 0, sizeof(slot.mTextures));

			signed char alphas[2 * 81];
			bool mixed = false;
			for (int sy = 0; sy < 9; ++sy) {
				const float wy = static_cast<float>(tileY * 8 + sy);
				for (int sx = 0; sx < 9; ++sx) {
					const float wx = static_cast<float>(tileX * 8 + sx);
					const CeCarrierClashWorld::MixAlphas a = CeCarrierClashWorld::ComputeMixAlphas(wx, wy);
					alphas[sy * 9 + sx] = static_cast<signed char>(a.sand);
					alphas[81 + sy * 9 + sx] = static_cast<signed char>(a.rock);
					if (a.sand > CeCarrierClashWorld::kMixAlphaMin ||
					    a.rock > CeCarrierClashWorld::kMixAlphaMin) {
						mixed = true;
					}
				}
			}
			if (!mixed) continue;

			slot.mData = new(MEMTYPE_MAP) UBYTE[2 * 81];
			if (!slot.mData) continue;
			slot.mCount = 2;
			slot.mTextures[0] = 2; // sand (shoreline skirt)
			slot.mTextures[1] = 1; // rock (basalt cliffs, reefs & the sea-stack)
			std::memcpy(slot.mData, alphas, sizeof(alphas));
		}
	}
	return true;
}

} // namespace

extern "C" void PortCeCarrierClashDropWorld(void) {
	if (gWorld.arenaInstalled) {
		MAP.mGeometryLoaded = FALSE;
		MAP.mPropertiesLoaded = FALSE;
	}
	std::memset(&gWorld, 0, sizeof(gWorld));
}

extern "C" void PortCeCarrierClashMarkRetryReset(void) {
	if (gWorld.ready && !gWorld.failed) {
		gWorld.readMask = 0u;
	}
}

extern "C" bool PortCeCarrierClashPreflightAndBuildWorld(void) {
	PortCeCarrierClashDropWorld();

	CeCarrierClashWorld::PreflightInput in = {};
	in.requestedBossUnit = CeCarrierClashWorld::kFenrirUnit;

	in.archiveValid = VerifyPackedAyaFile("data\\resources\\742_res_PC.aya", &in.archiveExists);
	for (unsigned i = 0; i < CeCarrierClashWorld::kPreloadMeshCount; ++i) {
		char meshPath[128] = {};
		std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya",
		              CeCarrierClashWorld::PreloadMeshName(i));
		bool exists = false;
		in.meshesValid[i] = VerifyPackedAyaFile(meshPath, &exists) && exists;
	}
	for (unsigned i = 0; i < CeCarrierClashWorld::kPreloadTextureCount; ++i) {
		char texPath[256] = {};
		bool exists = false;
		in.texturesValid[i] =
			BuildTextureDiskPath(CeCarrierClashWorld::PreloadTextureName(i), texPath, sizeof(texPath)) &&
			VerifyPackedAyaFile(texPath, &exists) && exists;
	}
	for (unsigned i = 0; i < CeCarrierClashWorld::kRequiredUnitCount; ++i) {
		const char *defName = CeCarrierClashWorld::RequiredUnitName(i);
		in.unitsPresent[i] =
			defName &&
			!CeCarrierClashWorld::IsForbiddenUnit(defName) &&
			(UPhysicsManager::GetUnitData(const_cast<char *>(defName)) != NULL);
	}

	const CeCarrierClashWorld::PreflightResult verdict = CeCarrierClashWorld::EvaluatePreflight(in);
	if (verdict.code != CeCarrierClashWorld::kPreflightOk) {
		PortCeCarrierClashSetMessage(verdict.message);
		PortSessionLog(PORT_SESSION_GAME, true,
		               "carrier clash preflight refused (code %d): %s",
		               static_cast<int>(verdict.code), verdict.message);
		return false;
	}

	CeCarrierClashWorld::UnitSpawnSpec specs[16] = {};
	const unsigned specCount = CeCarrierClashWorld::BuildDefaultOrderOfBattle(specs, 16u, false);
	const CeCarrierClashWorld::WorldPlan plan = CeCarrierClashWorld::BuildDefaultWorldPlan(specs, specCount);

	if (!CeCarrierClashWorld::WriteWorldBody(plan, false, gWorld.baseBytes, sizeof(gWorld.baseBytes), &gWorld.baseLen) ||
	    !CeCarrierClashWorld::WriteWorldBody(plan, true, gWorld.realBytes, sizeof(gWorld.realBytes), &gWorld.realLen)) {
		PortCeCarrierClashSetMessage("Carrier Clash could not generate its battle arena. Your career is unchanged.");
		PortSessionLog(PORT_SESSION_GAME, true, "carrier clash world generator failed");
		PortCeCarrierClashDropWorld();
		return false;
	}

	CeCarrierClashWorld::WorldSummary baseSummary = {}, realSummary = {};
	if (!CeCarrierClashWorld::VerifyWorldBody(gWorld.baseBytes, gWorld.baseLen, &baseSummary) ||
	    !CeCarrierClashWorld::VerifyWorldBody(gWorld.realBytes, gWorld.realLen, &realSummary) ||
	    baseSummary.thingCount != 0u ||
	    realSummary.startCount != 1u ||
	    !realSummary.sawVenturer ||
	    !realSummary.sawFenrir ||
	    realSummary.sawForbiddenUnit) {
		PortCeCarrierClashSetMessage("Carrier Clash failed arena self-check. Your career is unchanged.");
		PortSessionLog(PORT_SESSION_GAME, true, "carrier clash world verification failed");
		PortCeCarrierClashDropWorld();
		return false;
	}

	gWorld.ready = true;
	gWorld.failed = false;
	gWorld.readMask = 0u;
	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash preflight ok: base %u B, real %u B (%u units)",
	               gWorld.baseLen, gWorld.realLen, static_cast<unsigned>(realSummary.unitCount));
	return true;
}

extern "C" void PortCeCarrierClashInstallArena(void) {
	if (!PortCeCarrierClashActive()) return;

	const UBYTE archiveSkyCube = MAP.mHFP.mUnknown18[0];
	const UBYTE archiveWaterSet = MAP.mHFP.mUnknown18[5];

	MAP.CHeightField::Shutdown();
	MAP.mHeightData = new(MEMTYPE_HEIGHTFIELD) SWORD[CeCarrierClashWorld::kHeightSamples];
	if (!MAP.mHeightData) {
		gWorld.failed = true;
		PortCeCarrierClashSetMessage("Carrier Clash could not allocate terrain memory. Your career is unchanged.");
		return;
	}
	CeCarrierClashWorld::FillHeightSamples(MAP.mHeightData);

	MAP.mGridShiftX = CeCarrierClashWorld::kGridShift;
	MAP.mGridShiftY = CeCarrierClashWorld::kGridShift;
	MAP.mHeightScale = CeCarrierClashWorld::kHeightScale;
	MAP.mWaterLevel = CeCarrierClashWorld::kWaterLevel;

	// Golden sunset dusk maritime atmosphere: warm low amber sun, plum-violet dusk
	// horizon haze, golden water reflection, and cool indigo shadows so the Venturer
	// reads in dramatic silhouette against the western glow.
	const UBYTE targetSkyCube = static_cast<UBYTE>(CeCarrierClashWorld::kSkyCubeIndex);
	const UBYTE targetWaterSet = static_cast<UBYTE>(CeCarrierClashWorld::kWaterSetIndex);
	std::memset(&MAP.mHFP, 0, sizeof(MAP.mHFP));
	MAP.mHFP.Fog = 0xff5a4650u;
	MAP.mHFP.Sun = 0xffffb478u;
	MAP.mHFP.AntiSun = 0xff24223au;
	const DWORD sunReflect = 0xffffc890u;
	std::memcpy(&MAP.mHFP.mUnknown0c[0], &sunReflect, sizeof(sunReflect));
	MAP.mHFP.Ambient = 0xff2a2c44u;
	MAP.mHFP.mUnknown18[0] = targetSkyCube;
	MAP.mHFP.mUnknown18[1] = 1;
	MAP.mHFP.mUnknown18[2] = 129;
	MAP.mHFP.mUnknown18[3] = 255;
	MAP.mHFP.mUnknown18[4] = 0;
	MAP.mHFP.mUnknown18[5] = targetWaterSet;
	MAP.mHFP.FogDensity = 0.0018f;
	const DWORD waterCol = 0xff163b5eu;
	std::memcpy(&MAP.mHFP.mUnknown24[0], &waterCol, sizeof(waterCol));
	// One sunset key for everything the engine lights: the sun stands low over
	// the north-north-west horizon, so its flare, the units, the water glint and
	// the terrain lightmap below all agree. Cliffs that face it catch the light;
	// their lee falls into shadow.
	MAP.mHFP.VisibleSun = true;
	MAP.mHFP.SunPosX = CeCarrierClashWorld::kSunDirX;
	MAP.mHFP.SunPosY = CeCarrierClashWorld::kSunDirY;
	MAP.mHFP.SunPosZ = CeCarrierClashWorld::kSunDirZ;

	FinalizeHeightFieldLighting();
	RefreshProceduralMapExtents();
	if (!BuildProceduralMixerMap(MAP.mHFP.SunPosX, MAP.mHFP.SunPosY, MAP.mHFP.SunPosZ)) {
		gWorld.failed = true;
		PortCeCarrierClashSetMessage("Carrier Clash could not allocate terrain blend memory. Your career is unchanged.");
		return;
	}

	// ENGINE.Deserialize -> MAP.Deserialize already loaded archiveSkyCube & archiveWaterSet.
	// Avoid redundant SetKempyCube/SetWater calls (and release existing face textures / shaders /
	// VBufTexture before switching) so transitioning in/out of Carrier Clash leaks zero resources.
	CDXKempyCube *kc = EngineMediaAccess::KempyCube();
	if (archiveSkyCube != targetSkyCube || (kc && !kc->mTextures[0])) {
		if (kc) {
			for (int i = 0; i < 5; ++i) {
				if (kc->mTextures[i]) {
					kc->mTextures[i]->Release();
					kc->mTextures[i] = NULL;
				}
			}
		}
		ENGINE.SetKempyCube(targetSkyCube);
	}
	CDXWater *water = EngineMediaAccess::Water();
	if (archiveWaterSet != targetWaterSet || (water && !water->mReflection)) {
		if (water) {
			water->DeleteDeviceObjects();
			if (water->mVB) {
				water->mVB->Release();
				water->mVB = NULL;
			}
		}
		ENGINE.SetWater(targetWaterSet);
	}

	MAP.mMapNumber = CeCarrierClashWorld::kMapId;
	MAP.mGeometryLoaded = TRUE;
	MAP.mPropertiesLoaded = TRUE;
	MAP.InitQuickCollisionMap();
	gWorld.arenaInstalled = true;

	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash arena installed: 512x512 basalt archipelago strait, water %.1f, sky %u",
	               MAP.mWaterLevel, static_cast<unsigned>(CeCarrierClashWorld::kSkyCubeIndex));
}

extern "C" bool PortCeCarrierClashPreloadMeshes(void) {
	if (!PortCeCarrierClashActive()) return true;
	if (gWorld.failed) return false;

	for (unsigned i = 0; i < CeCarrierClashWorld::kPreloadTextureCount; ++i) {
		const char *texName = CeCarrierClashWorld::PreloadTextureName(i);
		if (!PreloadRetailMeshTexture(texName)) {
			gWorld.failed = true;
			PortCeCarrierClashSetMessage("Carrier Clash could not load retail carrier textures. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "carrier clash failed to preload texture: %s", texName);
			return false;
		}
	}

	for (unsigned i = 0; i < CeCarrierClashWorld::kPreloadMeshCount; ++i) {
		const char *meshName = CeCarrierClashWorld::PreloadMeshName(i);
		const int32_t runtimeId = 900 + static_cast<int32_t>(i);
		if (!LoadRetailMeshFromStandaloneAya(meshName, runtimeId)) {
			gWorld.failed = true;
			PortCeCarrierClashSetMessage("Carrier Clash could not load retail carrier models. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "carrier clash failed to preload mesh: %s", meshName);
			return false;
		}
	}
	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash preloaded %u textures and %u standalone retail meshes",
	               static_cast<unsigned>(CeCarrierClashWorld::kPreloadTextureCount),
	               static_cast<unsigned>(CeCarrierClashWorld::kPreloadMeshCount));

	// Support meshes the level archive lacks (rockets, escape pods, a downed
	// Wildcat): each one missing would be drawn as the engine's green "?" marker
	// cube. Optional: whatever an install does not have is skipped.
	unsigned supportLoaded = 0u;
	for (unsigned i = 0; i < CeCarrierClashWorld::kSupportMeshCount; ++i) {
		const CeCarrierClashWorld::SupportMeshSpec &spec = CeCarrierClashWorld::SupportMesh(i);
		if (CMesh::FindLoadedUnique(spec.mesh) != NULL) {
			++supportLoaded;
			continue;
		}
		bool ok = true;
		if (spec.texture) {
			char texPath[256] = {};
			bool exists = false;
			ok = BuildTextureDiskPath(spec.texture, texPath, sizeof(texPath)) &&
			     VerifyPackedAyaFile(texPath, &exists) && exists &&
			     PreloadRetailMeshTexture(spec.texture);
		}
		if (ok) {
			char meshPath[128] = {};
			bool exists = false;
			std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya", spec.mesh);
			ok = VerifyPackedAyaFile(meshPath, &exists) && exists &&
			     LoadRetailMeshFromStandaloneAya(spec.mesh, 908 + static_cast<int32_t>(i));
		}
		if (ok) {
			++supportLoaded;
		} else {
			PortSessionLog(PORT_SESSION_GAME, false,
			               "carrier clash support mesh unavailable (optional): %s", spec.mesh);
		}
	}
	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash support meshes: %u of %u loaded", supportLoaded,
	               static_cast<unsigned>(CeCarrierClashWorld::kSupportMeshCount));
	return true;
}

extern "C" int PortCeCarrierClashReadWorld(const char *gamePath, const void **bytes, unsigned *length) {
	if (bytes) *bytes = NULL;
	if (length) *length = 0u;
	if (!PortCeCarrierClashActive()) return 0;
	if (!bytes || !length || !gWorld.ready || gWorld.failed) {
		gWorld.failed = true;
		return -1;
	}

	char canon[96] = {};
	CanonicalizePath(gamePath, canon, sizeof(canon));
	char wantBase[64] = {}, wantReal[64] = {};
	std::snprintf(wantBase, sizeof(wantBase), "data/worlds/world%03d.wrd",
	              static_cast<int>(CeCarrierClashWorld::kBaseWorldId));
	std::snprintf(wantReal, sizeof(wantReal), "data/worlds/world%03d.wrd",
	              static_cast<int>(CeCarrierClashWorld::kArchiveLevel));

	if (std::strcmp(canon, wantBase) == 0) {
		*bytes = gWorld.baseBytes;
		*length = gWorld.baseLen;
		gWorld.readMask |= 1u;
		return 1;
	}
	if (std::strcmp(canon, wantReal) == 0) {
		*bytes = gWorld.realBytes;
		*length = gWorld.realLen;
		gWorld.readMask |= 2u;
		return 1;
	}

	// Never fall back to retail campaign worlds while Carrier Clash is active.
	gWorld.failed = true;
	PortCeCarrierClashSetMessage("Carrier Clash refused an unexpected world read. Your career is unchanged.");
	PortSessionLog(PORT_SESSION_GAME, true, "carrier clash unexpected world path: %s", canon);
	return -1;
}

#endif
