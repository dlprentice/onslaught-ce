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
};

WorldBodyStorage gWorld = {};

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

bool BuildTextureDiskPath(const char *texName, char *outPath, size_t outSize) {
	if (!texName || !texName[0] || !outPath || outSize < 64u) return false;
	char mangled[160] = {};
	size_t i = 0u;
	for (; texName[i] && i + 1u < sizeof(mangled); ++i) {
		mangled[i] = (texName[i] == '\\') ? '%' : texName[i];
	}
	mangled[i] = '\0';
	std::snprintf(outPath, outSize, "data\\resources\\dxtntextures\\%s(0)A1R5G5B5.aya", mangled);
	return true;
}

bool PreloadRetailMeshTexture(const char *texName) {
	if (!texName || !texName[0]) return false;
	for (CDXTexture *o = CDXTexture::mFirst; o; o = o->CTexture::mNext) {
		if (stricmp(o->GetName(), texName) == 0 && o->mFormat == TEXFMT_A1R5G5B5 && o->mLevels == -1) {
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
	t->mFormat = TEXFMT_A1R5G5B5;
	t->mTexFormat = TEXFMT_A1R5G5B5;
	t->mLevels = -1;
	t->mResourceLevels = 0;
	t->mNumFrames = 1;
	t->mPool = D3DPOOL_MANAGED;
	t->mCompressible = 1;
	t->mDisplayFormat = 0;
	t->mUnknown155b = 1;
	t->mLit = (std::strstr(texName, "_lit") != NULL) ? TRUE : FALSE;
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

	uint8_t stub[336] = {};
	unsigned stubLen = 0u;
	if (!CeCarrierClashWorld::BuildMeshStubChunk(meshName, runtimeId, true,
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
	mem.Close();
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

void BuildProceduralMixerMap(float sunX, float sunY, float sunZ) {
	MIXERMAP.Destroy();
	MIXERMAP.mSlots = new(MEMTYPE_MIXERMAP) SMixerSlot[MIXER_MAP_SLOTS];
	MIXERMAP.mMap = new(MEMTYPE_MIXERMAP) UBYTE[MIXER_MAP_SIZE];
	if (!MIXERMAP.mSlots || !MIXERMAP.mMap) return;

	// Lightmap: 512 x 512 directional sun illumination in [14..62].
	for (int y = 0; y < CeCarrierClashWorld::kCells; ++y) {
		UBYTE *row = MIXERMAP.mMap + y * CeCarrierClashWorld::kCells;
		for (int x = 0; x < CeCarrierClashWorld::kCells; ++x) {
			row[x] = CeCarrierClashWorld::ComputeCellLight(x, y, sunX, sunY, sunZ);
		}
	}

	// Per-tile texture mixing: sand along shorelines, craggy basalt rock on
	// steep slopes and high spires, coastal grass on flatter islet crowns.
	for (int tileY = 0; tileY < CeCarrierClashWorld::kPatches; ++tileY) {
		for (int tileX = 0; tileX < CeCarrierClashWorld::kPatches; ++tileX) {
			SMixerSlot &slot = MIXERMAP.mSlots[tileY * CeCarrierClashWorld::kPatches + tileX];
			slot.mCount = 0;
			slot.mData = NULL;
			std::memset(slot.mTextures, 0, sizeof(slot.mTextures));

			bool hasDryOrShore = false;
			for (int sy = 0; sy < 9 && !hasDryOrShore; ++sy) {
				const float wy = static_cast<float>(tileY * 8 + sy);
				for (int sx = 0; sx < 9; ++sx) {
					const float wx = static_cast<float>(tileX * 8 + sx);
					if (CeCarrierClashWorld::HeightAt(wx, wy) < CeCarrierClashWorld::kWaterLevel + 4.5f) {
						hasDryOrShore = true;
						break;
					}
				}
			}
			if (!hasDryOrShore) continue;

			slot.mData = new(MEMTYPE_MAP) UBYTE[2 * 81];
			if (!slot.mData) continue;
			slot.mCount = 2;
			slot.mTextures[0] = 2; // sand (shoreline / seabed rim)
			slot.mTextures[1] = 1; // rock (basalt sea-stacks & cliffs)

			signed char *sandAlpha = reinterpret_cast<signed char *>(slot.mData);
			signed char *rockAlpha = sandAlpha + 81;
			for (int sy = 0; sy < 9; ++sy) {
				const float wy = static_cast<float>(tileY * 8 + sy);
				for (int sx = 0; sx < 9; ++sx) {
					const float wx = static_cast<float>(tileX * 8 + sx);
					const float h = CeCarrierClashWorld::HeightAt(wx, wy);
					const float aboveSea = CeCarrierClashWorld::kWaterLevel - h; // >0 above water
					const float hx = CeCarrierClashWorld::HeightAt(wx + 1.0f, wy) -
					                 CeCarrierClashWorld::HeightAt(wx > 0.0f ? wx - 1.0f : 0.0f, wy);
					const float hy = CeCarrierClashWorld::HeightAt(wx, wy + 1.0f) -
					                 CeCarrierClashWorld::HeightAt(wx, wy > 0.0f ? wy - 1.0f : 0.0f);
					const float slope = std::sqrt(hx * hx + hy * hy) * 0.5f;

					// Sand dominates near waterline (aboveSea in [-4..+4.5 m]).
					int sand = -96;
					if (aboveSea > -4.5f && aboveSea < 6.0f) {
						const float distFromShore = std::fabs(aboveSea - 0.5f);
						const float band = 1.0f - (distFromShore / 5.5f);
						sand = static_cast<int>(-80.0f + band * 185.0f);
					}
					if (sand < -120) sand = -120;
					if (sand > 110) sand = 110;

					// Basalt rock dominates steep cliffs (slope > 0.45) and high spires (> 10 m).
					int rock = -100;
					if (aboveSea > 2.5f) {
						float rf = (slope - 0.30f) * 1.4f + (aboveSea - 9.0f) * 0.06f;
						if (rf < 0.0f) rf = 0.0f;
						if (rf > 1.0f) rf = 1.0f;
						rock = static_cast<int>(-90.0f + rf * 210.0f);
					}
					if (rock < -120) rock = -120;
					if (rock > 120) rock = 120;

					sandAlpha[sy * 9 + sx] = static_cast<signed char>(sand);
					rockAlpha[sy * 9 + sx] = static_cast<signed char>(rock);
				}
			}
		}
	}
}

} // namespace

extern "C" void PortCeCarrierClashDropWorld(void) {
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

	// Sunlit open-ocean maritime atmosphere: crisp azure horizon, warm sunlight,
	// gentle haze so both flagships stay razor-sharp across the naval corridor.
	std::memset(&MAP.mHFP, 0, sizeof(MAP.mHFP));
	MAP.mHFP.Fog = 0xffb8d8eeu;
	MAP.mHFP.Sun = 0xffeef0dcu;
	MAP.mHFP.AntiSun = 0xff22304cu;
	const DWORD sunReflect = 0xffeff6ffu;
	std::memcpy(&MAP.mHFP.mUnknown0c[0], &sunReflect, sizeof(sunReflect));
	MAP.mHFP.Ambient = 0xff304258u;
	MAP.mHFP.mUnknown18[0] = CeCarrierClashWorld::kSkyCubeIndex;
	MAP.mHFP.mUnknown18[1] = 1;
	MAP.mHFP.mUnknown18[2] = 129;
	MAP.mHFP.mUnknown18[3] = 255;
	MAP.mHFP.mUnknown18[4] = 0;
	MAP.mHFP.mUnknown18[5] = CeCarrierClashWorld::kWaterSetIndex;
	MAP.mHFP.FogDensity = 0.0018f;
	const DWORD waterCol = 0xff163b5eu;
	std::memcpy(&MAP.mHFP.mUnknown24[0], &waterCol, sizeof(waterCol));
	MAP.mHFP.VisibleSun = true;
	MAP.mHFP.SunPosX = 0.56f;
	MAP.mHFP.SunPosY = 0.72f;
	MAP.mHFP.SunPosZ = -0.41f;

	FinalizeHeightFieldLighting();
	BuildProceduralMixerMap(MAP.mHFP.SunPosX, MAP.mHFP.SunPosY, MAP.mHFP.SunPosZ);

	ENGINE.SetKempyCube(CeCarrierClashWorld::kSkyCubeIndex);
	ENGINE.SetWater(CeCarrierClashWorld::kWaterSetIndex);

	MAP.mMapNumber = CeCarrierClashWorld::kMapId;
	MAP.mGeometryLoaded = TRUE;
	MAP.mPropertiesLoaded = TRUE;
	MAP.InitQuickCollisionMap();

	PortSessionLog(PORT_SESSION_GAME, false,
	               "carrier clash arena installed: 512x512 maritime corridor, water %.1f, sky %u",
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
