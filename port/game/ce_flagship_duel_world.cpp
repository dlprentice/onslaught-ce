// SPDX-License-Identifier: GPL-3.0-only
// Clean-Room Level 888 ("Flagship Duel"): retail PC zlib-verified preflight,
// standalone PMSH/PMS2 mesh & DXTN texture preloader over donor 857_res_PC.aya,
// and in-memory version-50 world-body provider for 1P (788) and 2P (888).
#ifdef CE_VERSION

#include "common.h"
#include "ce_project.h"
#include "membuffer.h"
#include "chunker.h"
#include "Texture.h"
#include "frontend.h"
#include "game.h"
#include "ltshell.h"
#include "mesh.h"
#include "oids.h"
#include "WorldPhysicsManager.h"
#include "../platform/common/portlog.h"

#include "ce_flagship_duel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int uncompress(unsigned char *dest, unsigned long *destLen,
               const unsigned char *source, unsigned long sourceLen);
}

static_assert(CeFlagshipDuelWorld::kOidUnit == OID_CUnit, "OID_CUnit mismatch");
static_assert(CeFlagshipDuelWorld::kOidStart == OID_CStart, "OID_CStart mismatch");
static_assert(CeFlagshipDuelWorld::kOidSpawnPoint == OID_CSpawnPoint, "OID_CSpawnPoint mismatch");

bool PortCeMenus();
bool PortCeLayer();
bool PortCeAuthoringSessionAllowed() noexcept;

extern "C" bool PortCeMuseumActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeEditorActive(void) __attribute__((weak));
extern "C" bool PortCeLivingSandboxActive(void) __attribute__((weak));
extern "C" bool PortCeCarrierClashActive(void) __attribute__((weak));

namespace {

constexpr unsigned long kAyaPackBuffer = 0x102927ul;
constexpr unsigned long kAyaUnpackBlock = 0x100000ul;

struct FdWorldStorage {
	uint8_t baseBytes[CeFlagshipDuelWorld::kMaxWorldBodyBytes];
	unsigned baseLen;
	uint8_t realBytes[CeFlagshipDuelWorld::kMaxWorldBodyBytes];
	unsigned realLen;
	int levelNumber;
	bool ready;
	bool failed;
};

FdWorldStorage gWorld = {};

bool ContainsIgnoreCase(const char *haystack, const char *needle) {
	if (!haystack || !needle || !needle[0]) return false;
	const size_t nlen = std::strlen(needle);
	for (const char *p = haystack; *p; ++p) {
		size_t i = 0u;
		while (i < nlen && p[i] &&
		       CeFlagshipDuelWorld::AsciiLower(static_cast<unsigned char>(p[i])) ==
		       CeFlagshipDuelWorld::AsciiLower(static_cast<unsigned char>(needle[i]))) {
			++i;
		}
		if (i == nlen) return true;
	}
	return false;
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

bool PreloadRetailMeshTexture(const char *texName) {
	if (!texName || !texName[0] || !LT.GetDevice()) return false;
	const char *suffix = CeFlagshipDuelWorld::InferTextureDiskSuffix(texName);
	ETextureFormat fmt = TEXFMT_A1R5G5B5;
	if (std::strcmp(suffix, "A8R8G8B8") == 0) fmt = TEXFMT_A8R8G8B8;
	else if (std::strcmp(suffix, "A4R4G4B4") == 0) fmt = TEXFMT_A4R4G4B4;
	for (CDXTexture *o = CDXTexture::mFirst; o; o = o->CTexture::mNext) {
		if (stricmp(o->GetName(), texName) == 0 && o->mFormat == fmt &&
		    o->mLevels == -1 && o->mTextures[0] != NULL) {
			return true;
		}
	}
	CDXTexture *t = new (MEMTYPE_TEXTURE) CDXTexture;
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

	// Validate the standalone mesh archive before CMesh::Deserialize so a missing
	// or truncated file returns cleanly instead of triggering FatalResourceFileError.
	char meshPath[160] = {};
	bool exists = false;
	std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya", meshName);
	if (!VerifyPackedAyaFile(meshPath, &exists) || !exists)
		return false;

	int32_t safeId = runtimeId;
	while (CMesh::GetMeshByResourceID(safeId) != NULL && safeId < runtimeId + 4096)
		++safeId;

	uint8_t stub[336] = {};
	unsigned stubLen = 0u;
	if (!CeFlagshipDuelWorld::BuildMeshStubChunk(meshName, safeId, true,
	                                             stub, sizeof(stub), &stubLen)) {
		return false;
	}

	CDXMemBuffer mem;
	if (!mem.InitOwnedRead(stub, stubLen, MEMTYPE_MEMBUFFER))
		return false;

	CChunkReader reader;
	reader.Open(&mem);
	CMesh *loaded = CMesh::Deserialize(&reader);
	reader.Close();
	return loaded != NULL || CMesh::FindLoadedUnique(meshName) != NULL;
}

} // namespace

extern "C" void PortCeFlagshipDuelDropWorld(void) {
	std::memset(&gWorld, 0, sizeof(gWorld));
}

extern "C" void PortCeFlagshipDuelMarkRetryReset(void) {
	if (gWorld.ready)
		gWorld.failed = false;
}

extern "C" const char *PortCeFlagshipDuelDonorArchivePath(void) {
	return CeFlagshipDuelWorld::kDonorArchivePath;
}

extern "C" bool PortCeFlagshipDuelPreflightAndBuildWorld(int modeRaw) {
	PortCeFlagshipDuelDropWorld();

	const CeFlagshipDuelMode mode = (modeRaw != 0)
		? CeFlagshipDuelMode::kSplitScreen2P
		: CeFlagshipDuelMode::kSolo1P;

	CeFlagshipDuelWorld::CeFdPreflightInput in{};
	in.ceMenusEnabled = PortCeMenus() && PortCeLayer() && FRONTEND.IsRunning();
	in.anotherSessionOpen =
		!PortCeAuthoringSessionAllowed() ||
		(PortCeMuseumActive && PortCeMuseumActive()) ||
		(PortCeMuseumVisitActive && PortCeMuseumVisitActive()) ||
		(PortCeEditorActive && PortCeEditorActive()) ||
		(PortCeLivingSandboxActive && PortCeLivingSandboxActive()) ||
		(PortCeCarrierClashActive && PortCeCarrierClashActive());

	bool archiveExists = false;
	in.hasDonorArchive857 = VerifyPackedAyaFile(CeFlagshipDuelWorld::kDonorArchivePath, &archiveExists) && archiveExists;

	in.hasAllRequiredMeshes = true;
	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdRequiredMeshCount; ++i) {
		char meshPath[160] = {};
		bool exists = false;
		std::snprintf(meshPath, sizeof(meshPath), "data\\resources\\meshes\\m_%s.aya",
		              CeFlagshipDuelWorld::kCeFdPreloadMeshes[i]);
		if (!VerifyPackedAyaFile(meshPath, &exists) || !exists) {
			in.hasAllRequiredMeshes = false;
			break;
		}
	}

	in.hasAllRequiredTextures = true;
	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdRequiredTextureCount; ++i) {
		char texPath[256] = {};
		bool exists = false;
		if (!CeFlagshipDuelWorld::BuildTextureDiskPath(
		        CeFlagshipDuelWorld::kCeFdPreloadTextures[i], texPath, sizeof(texPath)) ||
		    !VerifyPackedAyaFile(texPath, &exists) || !exists) {
			in.hasAllRequiredTextures = false;
			break;
		}
	}

	in.hasAllRequiredUnitDefs = true;
	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdRequiredUnitDefCount; ++i) {
		const char *defName = CeFlagshipDuelWorld::kCeFdRequiredUnitDefs[i];
		if (!UPhysicsManager::GetUnitData(const_cast<char *>(defName))) {
			in.hasAllRequiredUnitDefs = false;
			break;
		}
	}

	const CeFlagshipDuelWorld::CeFdWorldPlan plan = CeFlagshipDuelWorld::CeFlagshipDuelBuildWorldPlan(mode);
	in.usesFenrirMultiplayer = false;
	for (size_t i = 0; i < plan.actorCount; ++i) {
		const CeFlagshipDuelWorld::CeFdActorSpec &a = plan.actors[i];
		if (a.definition && CeFlagshipDuelWorld::IsForbiddenUnit(a.definition))
			in.usesFenrirMultiplayer = true;
	}

	const CeFlagshipDuelWorld::CeFdPreflightVerdict verdict =
		CeFlagshipDuelWorld::CeFlagshipDuelEvaluatePreflight(in);
	if (!verdict.allowed) {
		PortCeFlagshipDuelSetMessage(verdict.message);
		PortSessionLog(PORT_SESSION_GAME, false,
		               "flagship_duel: preflight refused (status=%u)",
		               unsigned(verdict.status));
		return false;
	}

	if (!CeFlagshipDuelWorld::CeFlagshipDuelSerializeWorld(
	        plan, false, gWorld.baseBytes, sizeof(gWorld.baseBytes), &gWorld.baseLen) ||
	    !CeFlagshipDuelWorld::CeFlagshipDuelSerializeWorld(
	        plan, true, gWorld.realBytes, sizeof(gWorld.realBytes), &gWorld.realLen)) {
		PortCeFlagshipDuelDropWorld();
		PortCeFlagshipDuelSetMessage("Flagship Duel could not generate its battle arena. Your career is unchanged.");
		return false;
	}

	const CeFlagshipDuelWorld::CeFdWorldSummary baseSum =
		CeFlagshipDuelWorld::CeFlagshipDuelInspectWorld(gWorld.baseBytes, gWorld.baseLen, false);
	const CeFlagshipDuelWorld::CeFdWorldSummary realSum =
		CeFlagshipDuelWorld::CeFlagshipDuelInspectWorld(gWorld.realBytes, gWorld.realLen, true);
	if (!baseSum.valid || !realSum.valid) {
		PortCeFlagshipDuelDropWorld();
		PortCeFlagshipDuelSetMessage("Flagship Duel failed world self-check. Your career is unchanged.");
		PortSessionLog(PORT_SESSION_GAME, false, "flagship_duel: world self-check failed");
		return false;
	}

	gWorld.levelNumber = plan.levelNumber;
	gWorld.ready = true;
	gWorld.failed = false;

	PortSessionLog(PORT_SESSION_GAME, true,
	               "flagship_duel: prepared clean-room world (%s, level=%d, base=%u bytes, real=%u bytes)",
	               mode == CeFlagshipDuelMode::kSplitScreen2P ? "2P-split" : "1P-solo",
	               int(plan.levelNumber), gWorld.baseLen, gWorld.realLen);
	return true;
}

extern "C" bool PortCeFlagshipDuelPreloadMeshes(void) {
	if (!PortCeFlagshipDuelActive()) return true;
	if (gWorld.failed) return false;

	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdRequiredTextureCount; ++i) {
		const char *texName = CeFlagshipDuelWorld::kCeFdPreloadTextures[i];
		if (!PreloadRetailMeshTexture(texName)) {
			gWorld.failed = true;
			PortCeFlagshipDuelSetMessage("Flagship Duel could not load retail flagship textures. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "flagship_duel: failed texture preload for '%s'", texName);
			return false;
		}
	}

	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdRequiredMeshCount; ++i) {
		const char *meshName = CeFlagshipDuelWorld::kCeFdPreloadMeshes[i];
		const int32_t runtimeId = 940 + static_cast<int32_t>(i);
		if (!LoadRetailMeshFromStandaloneAya(meshName, runtimeId)) {
			gWorld.failed = true;
			PortCeFlagshipDuelSetMessage("Flagship Duel could not load retail flagship models. Your career is unchanged.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "flagship_duel: failed mesh preload for '%s'", meshName);
			return false;
		}
	}

	(void)PreloadRetailMeshTexture("meshtex\\cockpit.tga");
	(void)PreloadRetailMeshTexture("meshtex\\cockpit_M.tga");

	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdExpectedDonorBeMeshCount; ++i) {
		const char *beMesh = CeFlagshipDuelWorld::kCeFdExpectedDonorBeMeshes[i];
		if (CMesh::FindLoadedUnique(beMesh) == NULL &&
		    !LoadRetailMeshFromStandaloneAya(beMesh, 960 + static_cast<int32_t>(i))) {
			gWorld.failed = true;
			PortCeFlagshipDuelSetMessage("Flagship Duel could not find Battle Engine models in level 857 resources.");
			PortSessionLog(PORT_SESSION_GAME, true,
			               "flagship_duel: missing expected donor BE mesh '%s'", beMesh);
			return false;
		}
	}

	for (size_t i = 0; i < CeFlagshipDuelWorld::kCeFdSupportMeshCount; ++i) {
		const auto &spec = CeFlagshipDuelWorld::kCeFdSupportMeshes[i];
		if (CMesh::FindLoadedUnique(spec.mesh) != NULL)
			continue;
		bool ok = true;
		if (spec.texture) {
			char texPath[256] = {};
			bool exists = false;
			ok = CeFlagshipDuelWorld::BuildTextureDiskPath(spec.texture, texPath, sizeof(texPath)) &&
			     VerifyPackedAyaFile(texPath, &exists) && exists &&
			     PreloadRetailMeshTexture(spec.texture);
		}
		if (ok) {
			(void)LoadRetailMeshFromStandaloneAya(spec.mesh, 970 + static_cast<int32_t>(i));
		}
	}

	return true;
}

extern "C" int PortCeFlagshipDuelReadWorld(const char *gamePath, const void **bytes, unsigned *length) {
	if (bytes) *bytes = NULL;
	if (length) *length = 0u;
	if (!PortCeFlagshipDuelActive()) return 0;
	if (!bytes || !length || !gWorld.ready || gWorld.failed) {
		gWorld.failed = true;
		return -1;
	}

	char canon[96] = {};
	CanonicalizePath(gamePath, canon, sizeof(canon));
	char wantBase[64] = {}, wantReal[64] = {};
	std::snprintf(wantBase, sizeof(wantBase), "data/worlds/world%03d.wrd",
	              CeFlagshipDuelWorld::kDonorBaseWorldId);
	std::snprintf(wantReal, sizeof(wantReal), "data/worlds/world%03d.wrd",
	              gWorld.levelNumber);

	if (std::strcmp(canon, wantBase) == 0) {
		*bytes = gWorld.baseBytes;
		*length = gWorld.baseLen;
		return 1;
	}
	if (std::strcmp(canon, wantReal) == 0) {
		*bytes = gWorld.realBytes;
		*length = gWorld.realLen;
		return 1;
	}

	gWorld.failed = true;
	PortCeFlagshipDuelSetMessage("Flagship Duel refused an unexpected world read. Your career is unchanged.");
	PortSessionLog(PORT_SESSION_GAME, true, "flagship_duel: unexpected world path '%s'", canon);
	return -1;
}

#endif // CE_VERSION
