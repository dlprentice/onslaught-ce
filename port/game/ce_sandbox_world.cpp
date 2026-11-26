// SPDX-License-Identifier: GPL-3.0-only
// Engine adapter for Living Sandbox's clean-room 512 x 512 m multi-biome
// combined-arms theater, Sanctuary Citadel plateau, strict retail 700_res_PC.aya
// preflight, optional F.S. Venturer preload, and version-50 world bodies.
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
#include "ce_sandbox_director.h"
#include "../platform/common/portlog.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int uncompress(unsigned char *dest, unsigned long *destLen,
               const unsigned char *source, unsigned long sourceLen);
}

static_assert(CeSandboxWorld::kOidUnit == OID_CUnit, "OID_CUnit mismatch");
static_assert(CeSandboxWorld::kOidStart == OID_CStart, "OID_CStart mismatch");
static_assert(CeSandboxWorld::kOidSquad == OID_CSquad, "OID_CSquad mismatch");
static_assert(CeSandboxWorld::kAllegianceForseti == kForsetiAllegiance, "Forseti allegiance mismatch");
static_assert(CeSandboxWorld::kAllegianceMuspell == kMuspellAllegiance, "Muspell allegiance mismatch");

namespace {

const unsigned long kAyaPackBuffer = 0x102927ul;
const unsigned long kAyaUnpackBlock = 0x100000ul;

struct SandboxWorldStorage {
	uint8_t baseBytes[CeSandboxWorld::kMaxWorldBodyBytes];
	unsigned baseLen;
	uint8_t realBytes[CeSandboxWorld::kMaxWorldBodyBytes];
	unsigned realLen;
	unsigned readMask;
	bool ready;
	bool failed;
	bool venturerReady;
	bool arenaInstalled;
};

SandboxWorldStorage gWorld = {};

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
		       CeSandboxWorld::AsciiLower(static_cast<unsigned char>(p[i])) ==
		       CeSandboxWorld::AsciiLower(static_cast<unsigned char>(needle[i]))) {
			++i;
		}
		if (i == nlen) return true;
	}
	return false;
}

constexpr int kHeightGridSide = CeSandboxWorld::kCells + 1;
const float *gActiveHeightGrid = NULL;

void RefreshProceduralMapExtents() {
	SINT minX = CeSandboxWorld::kCells;
	SINT minY = CeSandboxWorld::kCells;
	SINT maxX = -1;
	SINT maxY = -1;
	for (SINT y = 0; y < CeSandboxWorld::kCells; ++y) {
		const float *row = gActiveHeightGrid ? (gActiveHeightGrid + y * kHeightGridSide) : NULL;
		for (SINT x = 0; x < CeSandboxWorld::kCells; ++x) {
			const float h = row ? row[x] : CeSandboxWorld::HeightAt(static_cast<float>(x), static_cast<float>(y));
			if (h < MAP.mWaterLevel) {
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
		maxX = CeSandboxWorld::kCells - 1;
		maxY = CeSandboxWorld::kCells - 1;
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
		if (blocks == 0u) {
			unsigned long dstLen = kAyaPackBuffer;
			if (uncompress(unpacked, &dstLen, packed, length) != 0 ||
			    dstLen == 0ul || dstLen > kAyaUnpackBlock) {
				ok = false;
				break;
			}
		} else {
			if (length < 2ul || (packed[0] & 0x0fu) != 8u ||
			    ((((unsigned)packed[0] << 8) | (unsigned)packed[1]) % 31u) != 0u) {
				ok = false;
				break;
			}
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
		// Mirrors retail CMesh::FindTexture (src/mesh.cpp:1367-1375):
		// filename prefix "A8" -> TEXFMT_A8R8G8B8, "A4" -> TEXFMT_A4R4G4B4, else A1R5G5B5.
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
	if (!CeSandboxWorld::BuildMeshStubChunk(meshName, safeId, true,
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

// Multi-biome texture blending across 700_res_PC.aya's six mixer textures:
//   0 = grass (central valley), 1 = grey granite stone (cliffs, ridges & Citadel terrace),
//   2 = pale coastal sand (shoreline & harbor seabed), 5 = dark basalt asphalt (roads,
//   grand ramp, faction HQ aprons, and contested capture node pads).
bool BuildProceduralMixerMap(float sunX, float sunY, float sunZ, const float *hGrid) {
	MIXERMAP.Destroy();
	MIXERMAP.mSlots = new(MEMTYPE_MIXERMAP) SMixerSlot[MIXER_MAP_SLOTS];
	MIXERMAP.mMap = new(MEMTYPE_MIXERMAP) UBYTE[MIXER_MAP_SIZE];
	if (!MIXERMAP.mSlots || !MIXERMAP.mMap) {
		MIXERMAP.Destroy();
		return false;
	}

	auto sampleH = [hGrid](int ix, int iy) -> float {
		if (ix < 0) ix = 0;
		if (ix > CeSandboxWorld::kCells) ix = CeSandboxWorld::kCells;
		if (iy < 0) iy = 0;
		if (iy > CeSandboxWorld::kCells) iy = CeSandboxWorld::kCells;
		return hGrid ? hGrid[iy * kHeightGridSide + ix]
		             : CeSandboxWorld::HeightAt(static_cast<float>(ix), static_cast<float>(iy));
	};

	float slen = std::sqrt(sunX * sunX + sunY * sunY + sunZ * sunZ);
	float lx = sunX, ly = sunY, lz = sunZ;
	if (slen > 1e-4f) { lx /= slen; ly /= slen; lz /= slen; }

	for (int y = 0; y < CeSandboxWorld::kCells; ++y) {
		UBYTE *row = MIXERMAP.mMap + y * CeSandboxWorld::kCells;
		const int y0 = (y > 0) ? (y - 1) : 0;
		const int y1 = (y < CeSandboxWorld::kCells - 1) ? (y + 1) : (CeSandboxWorld::kCells - 1);
		const float dySpan = static_cast<float>(y1 - y0);
		const float invDy = (dySpan > 0.5f) ? (1.0f / dySpan) : 1.0f;
		for (int x = 0; x < CeSandboxWorld::kCells; ++x) {
			if (!hGrid) {
				row[x] = CeSandboxWorld::ComputeCellLight(x, y, sunX, sunY, sunZ);
				continue;
			}
			const int x0 = (x > 0) ? (x - 1) : 0;
			const int x1 = (x < CeSandboxWorld::kCells - 1) ? (x + 1) : (CeSandboxWorld::kCells - 1);
			const float dxSpan = static_cast<float>(x1 - x0);
			const float dzdx = (sampleH(x1, y) - sampleH(x0, y)) / ((dxSpan > 0.5f) ? dxSpan : 1.0f);
			const float dzdy = (sampleH(x, y1) - sampleH(x, y0)) * invDy;
			float nx = dzdx, ny = dzdy, nz = -1.0f;
			const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (nlen > 1e-4f) { nx /= nlen; ny /= nlen; nz /= nlen; }
			float ndotl = nx * lx + ny * ly + nz * lz;
			if (ndotl < 0.0f) ndotl = 0.0f;
			if (ndotl > 1.0f) ndotl = 1.0f;
			const int shade = static_cast<int>(14.0f + ndotl * 16.0f + 0.5f);
			row[x] = static_cast<UBYTE>(shade < 14 ? 14 : (shade > 30 ? 30 : shade));
		}
	}

	auto computeWeights = [&sampleH](int ix, int iy, float &wSand, float &wRock, float &wAsphalt) {
		const float wx = static_cast<float>(ix);
		const float wy = static_cast<float>(iy);
		const float h = sampleH(ix, iy);
		const float aboveSea = CeSandboxWorld::kWaterLevel - h;
		const float hx = sampleH(ix + 1, iy) - sampleH(ix > 0 ? ix - 1 : 0, iy);
		const float hy = sampleH(ix, iy + 1) - sampleH(ix, iy > 0 ? iy - 1 : 0);
		const float slope = std::sqrt(hx * hx + hy * hy) * 0.5f;

		// Coastal sand near waterline and shallow harbor seabed
		wSand = (aboveSea < 5.0f) ? CeSandboxWorld::Clamp01(1.0f - (aboveSea + 1.5f) / 6.5f) : 0.0f;

		// Grey granite stone on steep slopes, alpine highlands, and the Citadel plateau terrace
		const float citDx = (wx < CeSandboxWorld::kCitadelMinX) ? (CeSandboxWorld::kCitadelMinX - wx)
		                  : (wx > CeSandboxWorld::kCitadelMaxX) ? (wx - CeSandboxWorld::kCitadelMaxX) : 0.0f;
		const float citDy = (wy < CeSandboxWorld::kCitadelMinY) ? (CeSandboxWorld::kCitadelMinY - wy)
		                  : (wy > CeSandboxWorld::kCitadelMaxY) ? (wy - CeSandboxWorld::kCitadelMaxY) : 0.0f;
		const float citPad = 1.0f - CeSandboxWorld::SmoothStepRange(0.0f, 8.0f, std::sqrt(citDx * citDx + citDy * citDy));
		const float cliffRock = CeSandboxWorld::Clamp01((slope - 0.24f) * 1.55f + (aboveSea - 14.0f) * 0.055f);
		wRock = (citPad > cliffRock) ? citPad : cliffRock;

		// Dark basalt asphalt on strategic transit roads, the SE grand ramp, HQ pads, and node circles
		const float roadW = CeSandboxWorld::RoadBlendAt(wx, wy);
		float padW = 0.0f;
		if (citPad < 0.05f) {
			padW = CeSandboxWorld::ConcretePadBlendAt(wx, wy);
		}
		wAsphalt = (roadW > padW) ? roadW : padW;
		if (citPad > 0.5f) {
			// Keep Citadel stone terrace clean except where the grand ramp departs at the SE rim
			wAsphalt *= (1.0f - CeSandboxWorld::SmoothStepRange(350.0f, 358.0f, wy));
		}
	};

	for (int tileY = 0; tileY < CeSandboxWorld::kPatches; ++tileY) {
		for (int tileX = 0; tileX < CeSandboxWorld::kPatches; ++tileX) {
			SMixerSlot &slot = MIXERMAP.mSlots[tileY * CeSandboxWorld::kPatches + tileX];
			slot.mCount = 0;
			slot.mData = NULL;
			std::memset(slot.mTextures, 0, sizeof(slot.mTextures));

			float wsArr[81], wrArr[81], waArr[81];
			bool needSand = false, needRock = false, needAsphalt = false;
			for (int sy = 0; sy < 9; ++sy) {
				const int iy = tileY * 8 + sy;
				for (int sx = 0; sx < 9; ++sx) {
					const int ix = tileX * 8 + sx;
					const int idx = sy * 9 + sx;
					computeWeights(ix, iy, wsArr[idx], wrArr[idx], waArr[idx]);
					if (wsArr[idx] > 0.02f) needSand = true;
					if (wrArr[idx] > 0.02f) needRock = true;
					if (waArr[idx] > 0.02f) needAsphalt = true;
				}
			}

			UWORD layers[3] = {};
			int count = 0;
			if (needSand) layers[count++] = 2;     // pale coastal sand
			if (needRock) layers[count++] = 1;     // highland grey granite stone
			if (needAsphalt) layers[count++] = 5;  // dark basalt road & tactical pads
			if (count == 0) continue;

			slot.mData = new(MEMTYPE_MAP) UBYTE[count * 81];
			if (!slot.mData) continue;
			slot.mCount = count;
			for (int l = 0; l < count; ++l) slot.mTextures[l] = layers[l];

			for (int idx = 0; idx < 81; ++idx) {
				const float ws = wsArr[idx];
				const float wr = wrArr[idx];
				const float wa = waArr[idx];

				// CHeightField::Unknown47eff0 adds pal[*tsrc] top byte [-32,+31] to aa and
				// compares signed 32-bit d = c - *tp. Keep each layer's signed alpha in
				// [-62,+62] and within 62 below any stronger earlier layer at this vertex so
				// neither aa+pal nor c-*tp can wrap signed 32-bit across bilinear spans.
				int maxPrev = 0;
				for (int l = 0; l < count; ++l) {
					const float w = (layers[l] == 2) ? ws
					              : (layers[l] == 1) ? wr
					                                 : wa;
					int val = static_cast<int>(-62.0f + w * 124.0f);
					const int floorVal = maxPrev - 62;
					if (val < floorVal) val = floorVal;
					if (val < -62) val = -62;
					if (val > 62) val = 62;
					if (val > maxPrev) maxPrev = val;
					reinterpret_cast<signed char *>(slot.mData)[l * 81 + idx] =
						static_cast<signed char>(val);
				}
			}
		}
	}
	return true;
}

} // namespace

extern "C" void PortCeLivingSandboxDropWorld(void) {
	if (gWorld.arenaInstalled) {
		MAP.mGeometryLoaded = FALSE;
		MAP.mPropertiesLoaded = FALSE;
	}
	std::memset(&gWorld, 0, sizeof(gWorld));
}

extern "C" void PortCeLivingSandboxMarkRetryReset(void) {
	if (gWorld.ready && !gWorld.failed) {
		gWorld.readMask = 0u;
	}
}

extern "C" bool PortCeLivingSandboxVenturerReady(void) {
	return gWorld.venturerReady;
}

extern "C" bool PortCeLivingSandboxPreflightAndBuildWorld(void) {
	PortCeLivingSandboxDropWorld();

	CeSandboxWorld::PreflightInput in = {};
	in.archiveValid = VerifyPackedAyaFile("data\\resources\\700_res_PC.aya", &in.archiveExists);
	for (unsigned i = 0; i < CeSandboxWorld::kRequiredUnitCount; ++i) {
		const char *defName = CeSandboxWorld::RequiredUnitName(i);
		const bool present =
			defName &&
			!CeSandboxWorld::IsForbiddenUnit(defName) &&
			(UPhysicsManager::GetUnitData(const_cast<char *>(defName)) != NULL);
		in.unitsPresent[i] = present;
		if (!present && defName) {
			PortSessionLog(PORT_SESSION_GAME, true,
			               "living sandbox preflight missing unit def[%u]: %s", i, defName);
		}
	}

	const CeSandboxWorld::PreflightResult verdict = CeSandboxWorld::EvaluatePreflight(in);
	if (verdict.code != CeSandboxWorld::kPreflightOk) {
		PortCeLivingSandboxSetMessage(verdict.message);
		PortSessionLog(PORT_SESSION_GAME, true,
		               "living sandbox preflight refused (code %d): %s",
		               static_cast<int>(verdict.code), verdict.message);
		return false;
	}

	// Verify standalone retail files for the Forseti Pulse Tank Factory HQ before level load.
	for (unsigned i = 0; i < CeSandboxWorld::kForsetiHqTextureCount; ++i) {
		char texPath[256] = {};
		bool exists = false;
		const char *texName = CeSandboxWorld::ForsetiHqTextureName(i);
		if (!BuildTextureDiskPath(texName, texPath, sizeof(texPath)) ||
		    !VerifyPackedAyaFile(texPath, &exists) || !exists) {
			PortCeLivingSandboxSetMessage(
				"Living Sandbox requires Forseti base resources from your retail install. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "living sandbox preflight missing/corrupt Forseti HQ texture: %s", texPath);
			return false;
		}
	}
	for (unsigned i = 0; i < CeSandboxWorld::kForsetiHqMeshCount; ++i) {
		char meshPath[128] = {};
		bool exists = false;
		const char *meshName = CeSandboxWorld::ForsetiHqMeshName(i);
		std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya", meshName);
		if (!VerifyPackedAyaFile(meshPath, &exists) || !exists) {
			PortCeLivingSandboxSetMessage(
				"Living Sandbox requires Forseti base resources from your retail install. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "living sandbox preflight missing/corrupt Forseti HQ mesh: %s", meshPath);
			return false;
		}
	}

	CeSandboxWorld::UnitSpawnSpec specs[CeSandboxWorld::kMaxActors] = {};
	const unsigned specCount = CeSandboxWorld::BuildDefaultOrderOfBattle(specs, CeSandboxWorld::kMaxActors);
	const CeSandboxWorld::WorldPlan plan = CeSandboxWorld::BuildDefaultWorldPlan(specs, specCount);

	if (!CeSandboxWorld::WriteWorldBody(plan, false, gWorld.baseBytes, sizeof(gWorld.baseBytes), &gWorld.baseLen) ||
	    !CeSandboxWorld::WriteWorldBody(plan, true, gWorld.realBytes, sizeof(gWorld.realBytes), &gWorld.realLen)) {
		PortCeLivingSandboxSetMessage("Living Sandbox could not generate its theater. Your career is unchanged.");
		PortSessionLog(PORT_SESSION_GAME, true, "living sandbox world generator failed");
		PortCeLivingSandboxDropWorld();
		return false;
	}

	CeSandboxWorld::WorldSummary baseSummary = {}, realSummary = {};
	if (!CeSandboxWorld::VerifyWorldBody(gWorld.baseBytes, gWorld.baseLen, &baseSummary) ||
	    !CeSandboxWorld::VerifyWorldBody(gWorld.realBytes, gWorld.realLen, &realSummary) ||
	    baseSummary.thingCount != 0u ||
	    realSummary.startCount != 1u ||
	    realSummary.unitCount < 20u ||
	    realSummary.sawForbiddenUnit ||
	    realSummary.sawUnitInsideKeepOut) {
		PortCeLivingSandboxSetMessage("Living Sandbox failed theater self-check. Your career is unchanged.");
		PortSessionLog(PORT_SESSION_GAME, true, "living sandbox world verification failed");
		PortCeLivingSandboxDropWorld();
		return false;
	}

	gWorld.ready = true;
	gWorld.failed = false;
	gWorld.readMask = 0u;
	PortSessionLog(PORT_SESSION_GAME, false,
	               "living sandbox preflight ok: base %u B, real %u B (%u units)",
	               gWorld.baseLen, gWorld.realLen, static_cast<unsigned>(realSummary.unitCount));
	return true;
}

extern "C" void PortCeLivingSandboxInstallArena(void) {
	if (!PortCeLivingSandboxActive()) return;

	const UBYTE archiveSkyCube = MAP.mHFP.mUnknown18[0];
	const UBYTE archiveWaterSet = MAP.mHFP.mUnknown18[5];

	MAP.CHeightField::Shutdown();
	MAP.mHeightData = new(MEMTYPE_HEIGHTFIELD) SWORD[CeSandboxWorld::kHeightSamples];
	if (!MAP.mHeightData) {
		gWorld.failed = true;
		PortCeLivingSandboxSetMessage("Living Sandbox could not allocate terrain memory. Your career is unchanged.");
		return;
	}
	float *hGrid = static_cast<float *>(std::malloc(sizeof(float) * static_cast<size_t>(kHeightGridSide) * static_cast<size_t>(kHeightGridSide)));
	if (hGrid) {
		for (int y = 0; y < kHeightGridSide; ++y) {
			float *row = hGrid + y * kHeightGridSide;
			const float wy = static_cast<float>(y);
			for (int x = 0; x < kHeightGridSide; ++x) {
				row[x] = CeSandboxWorld::HeightAt(static_cast<float>(x), wy);
			}
		}
		for (int tx = 0; tx < CeSandboxWorld::kPatches; ++tx) {
			for (int ty = 0; ty < CeSandboxWorld::kPatches; ++ty) {
				int16_t *patch = MAP.mHeightData + ((tx * CeSandboxWorld::kPatches + ty) * CeSandboxWorld::kPatchSamples * CeSandboxWorld::kPatchSamples);
				for (int sy = 0; sy < CeSandboxWorld::kPatchSamples; ++sy) {
					const float *row = hGrid + (ty * CeSandboxWorld::kPatchCells + sy) * kHeightGridSide + tx * CeSandboxWorld::kPatchCells;
					for (int sx = 0; sx < CeSandboxWorld::kPatchSamples; ++sx) {
						patch[sy * CeSandboxWorld::kPatchSamples + sx] = CeSandboxWorld::QuantizeHeight(row[sx]);
					}
				}
			}
		}
	} else {
		CeSandboxWorld::FillHeightSamples(MAP.mHeightData);
	}

	MAP.mGridShiftX = CeSandboxWorld::kGridShift;
	MAP.mGridShiftY = CeSandboxWorld::kGridShift;
	MAP.mHeightScale = CeSandboxWorld::kHeightScale;
	MAP.mWaterLevel = CeSandboxWorld::kWaterLevel;

	// Cool moonlit night atmosphere aligned with the starry 700_res_PC.aya skybox
	// and Sanctuary Citadel's south-east moon key, keeping clear sightlines across the valley.
	const UBYTE targetSkyCube = static_cast<UBYTE>(CeSandboxWorld::kSkyCubeIndex);
	const UBYTE targetWaterSet = static_cast<UBYTE>(CeSandboxWorld::kWaterSetIndex);
	std::memset(&MAP.mHFP, 0, sizeof(MAP.mHFP));
	MAP.mHFP.Fog = 0xff1a2436u;
	MAP.mHFP.Sun = 0xff8fa6d0u;
	MAP.mHFP.AntiSun = 0xff141c2eu;
	const DWORD sunReflect = 0xff9cb4dcu;
	std::memcpy(&MAP.mHFP.mUnknown0c[0], &sunReflect, sizeof(sunReflect));
	MAP.mHFP.Ambient = 0xff1c2438u;
	MAP.mHFP.mUnknown18[0] = targetSkyCube;
	MAP.mHFP.mUnknown18[1] = 1;
	MAP.mHFP.mUnknown18[2] = 129;
	MAP.mHFP.mUnknown18[3] = 255;
	MAP.mHFP.mUnknown18[4] = 0;
	MAP.mHFP.mUnknown18[5] = targetWaterSet;
	MAP.mHFP.FogDensity = 0.0014f;
	const DWORD waterCol = 0xff0f243cu;
	std::memcpy(&MAP.mHFP.mUnknown24[0], &waterCol, sizeof(waterCol));
	MAP.mHFP.VisibleSun = true;
	MAP.mHFP.SunPosX = 0.620f;
	MAP.mHFP.SunPosY = -0.500f;
	MAP.mHFP.SunPosZ = -0.604f;

	FinalizeHeightFieldLighting();
	gActiveHeightGrid = hGrid;
	RefreshProceduralMapExtents();
	gActiveHeightGrid = NULL;
	const bool mixerOk = BuildProceduralMixerMap(MAP.mHFP.SunPosX, MAP.mHFP.SunPosY, MAP.mHFP.SunPosZ, hGrid);
	std::free(hGrid);
	if (!mixerOk) {
		gWorld.failed = true;
		PortCeLivingSandboxSetMessage("Living Sandbox could not allocate terrain blend memory. Your career is unchanged.");
		return;
	}

	// ENGINE.Deserialize -> MAP.Deserialize already loaded archiveSkyCube & archiveWaterSet.
	// Avoid redundant SetKempyCube/SetWater calls (and release existing face textures / shaders /
	// VBufTexture before switching) so transitioning in/out of Living Sandbox leaks zero resources.
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

	MAP.mMapNumber = CeSandboxWorld::kMapId;
	MAP.mGeometryLoaded = TRUE;
	MAP.mPropertiesLoaded = TRUE;
	MAP.InitQuickCollisionMap();
	gWorld.arenaInstalled = true;

	PortSessionLog(PORT_SESSION_GAME, false,
	               "living sandbox theater installed: 512x512 multi-biome, water %.1f, citadel Z=%.1f",
	               MAP.mWaterLevel, CeSandboxWorld::kCitadelFloorZ);
}

extern "C" bool PortCeLivingSandboxPreloadMeshes(void) {
	if (!PortCeLivingSandboxActive()) return true;
	if (gWorld.failed) return false;

	// Required standalone preload for Forseti Pulse Tank Factory HQ (fb_tank_factory.msh).
	for (unsigned i = 0; i < CeSandboxWorld::kForsetiHqTextureCount; ++i) {
		const char *texName = CeSandboxWorld::ForsetiHqTextureName(i);
		if (!PreloadRetailMeshTexture(texName)) {
			gWorld.failed = true;
			PortCeLivingSandboxSetMessage(
				"Living Sandbox could not load Forseti base textures. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "living sandbox preload failed Forseti HQ texture: %s", texName ? texName : "");
			return false;
		}
	}
	for (unsigned i = 0; i < CeSandboxWorld::kForsetiHqMeshCount; ++i) {
		const char *meshName = CeSandboxWorld::ForsetiHqMeshName(i);
		const int32_t runtimeId = 916 + static_cast<int32_t>(i);
		if (!LoadRetailMeshFromStandaloneAya(meshName, runtimeId)) {
			gWorld.failed = true;
			PortCeLivingSandboxSetMessage(
				"Living Sandbox could not load Forseti base geometry. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "living sandbox preload failed Forseti HQ mesh: %s", meshName ? meshName : "");
			return false;
		}
	}

	// Optional standalone preload of F.S. Venturer for Naval/Carrier Escalation.
	// If any optional file is unavailable, degrade gracefully to FSV Marshall.
	bool allVenturerOk = (UPhysicsManager::GetUnitData(const_cast<char *>(CeSandboxWorld::kUnitVenturer)) != NULL);
	for (unsigned i = 0; allVenturerOk && i < CeSandboxWorld::kOptionalTextureCount; ++i) {
		char texPath[256] = {};
		bool exists = false;
		const char *texName = CeSandboxWorld::OptionalTextureName(i);
		if (!BuildTextureDiskPath(texName, texPath, sizeof(texPath)) ||
		    !VerifyPackedAyaFile(texPath, &exists) || !exists ||
		    !PreloadRetailMeshTexture(texName)) {
			allVenturerOk = false;
		}
	}
	for (unsigned i = 0; allVenturerOk && i < CeSandboxWorld::kOptionalMeshCount; ++i) {
		char meshPath[128] = {};
		bool exists = false;
		const char *meshName = CeSandboxWorld::OptionalMeshName(i);
		std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya", meshName);
		const int32_t runtimeId = 920 + static_cast<int32_t>(i);
		if (!VerifyPackedAyaFile(meshPath, &exists) || !exists ||
		    !LoadRetailMeshFromStandaloneAya(meshName, runtimeId)) {
			allVenturerOk = false;
		}
	}
	gWorld.venturerReady = allVenturerOk;
	PortSessionLog(PORT_SESSION_GAME, false,
	               "living sandbox escalation flagship: %s",
	               allVenturerOk ? "F.S. Venturer preloaded" : "FSV Marshall fallback");

	// Support meshes the level archive lacks (rockets, escape pods, wreckage): each
	// one missing would be drawn as the engine's green "?" marker cube. Optional:
	// whatever an install does not have is skipped, and the level still starts.
	unsigned supportLoaded = 0u;
	for (unsigned i = 0; i < CeSandboxWorld::kSupportMeshCount; ++i) {
		const CeSandboxWorld::SupportMeshSpec &spec = CeSandboxWorld::SupportMesh(i);
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
			     LoadRetailMeshFromStandaloneAya(spec.mesh, 924 + static_cast<int32_t>(i));
		}
		if (ok) {
			++supportLoaded;
		} else {
			PortSessionLog(PORT_SESSION_GAME, false,
			               "living sandbox support mesh unavailable (optional): %s", spec.mesh);
		}
	}
	PortSessionLog(PORT_SESSION_GAME, false,
	               "living sandbox support meshes: %u of %u loaded", supportLoaded,
	               static_cast<unsigned>(CeSandboxWorld::kSupportMeshCount));
	return true;
}

extern "C" int PortCeLivingSandboxReadWorld(const char *gamePath, const void **bytes, unsigned *length) {
	if (bytes) *bytes = NULL;
	if (length) *length = 0u;
	if (!PortCeLivingSandboxActive()) return 0;
	if (!bytes || !length || !gWorld.ready || gWorld.failed) {
		gWorld.failed = true;
		return -1;
	}

	char canon[96] = {};
	CanonicalizePath(gamePath, canon, sizeof(canon));
	char wantBase[64] = {}, wantReal[64] = {};
	std::snprintf(wantBase, sizeof(wantBase), "data/worlds/world%03d.wrd",
	              static_cast<int>(CeSandboxWorld::kBaseWorldId));
	std::snprintf(wantReal, sizeof(wantReal), "data/worlds/world%03d.wrd",
	              static_cast<int>(CeSandboxWorld::kArchiveLevel));

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

	gWorld.failed = true;
	PortCeLivingSandboxSetMessage("Living Sandbox refused an unexpected world read. Your career is unchanged.");
	PortSessionLog(PORT_SESSION_GAME, true, "living sandbox unexpected world path: %s", canon);
	return -1;
}

#endif
