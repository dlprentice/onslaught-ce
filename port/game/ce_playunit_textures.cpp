// SPDX-License-Identifier: GPL-3.0-only
// Decompresses and uploads Play Any Unit's original GPL-3.0-only 1P weapon textures
// into D3DPOOL_MANAGED textures with mipmap chains; falls back cleanly to NULL on error.
#ifdef CE_VERSION

#include "common.h"
#include "ltshell.h"
#include "d3dx9.h"
#include "ce_playunit_textures.h"
#include "ce_playunit_textures_data.h"
#include "../platform/common/portlog.h"

extern "C" int uncompress(unsigned char *dest, unsigned long *destLen,
                          const unsigned char *source, unsigned long sourceLen);

namespace CePlayUnitTextures
{
namespace
{
static_assert(static_cast<unsigned>(CE_PLAYUNIT_TEX_COUNT) == static_cast<unsigned>(CePlayUnitTexturesData::kCount),
              "CePlayUnitTexId must match CePlayUnitTexturesData::kCount");

IDirect3DDevice9 *gDevice = NULL;
IDirect3DTexture9 *gTextures[CE_PLAYUNIT_TEX_COUNT] = {};
bool gBadAsset[CE_PLAYUNIT_TEX_COUNT] = {};
unsigned gRetryCooldown[CE_PLAYUNIT_TEX_COUNT] = {};
bool gLoggedFail[CE_PLAYUNIT_TEX_COUNT] = {};
} // namespace

void ReleaseAll()
{
	for (unsigned i = 0; i < static_cast<unsigned>(CE_PLAYUNIT_TEX_COUNT); ++i)
	{
		if (gTextures[i])
		{
			gTextures[i]->Release();
			gTextures[i] = NULL;
		}
		gBadAsset[i] = false;
		gRetryCooldown[i] = 0;
		gLoggedFail[i] = false;
	}
	gDevice = NULL;
}

IDirect3DTexture9 *Ensure(IDirect3DDevice9 *device, CePlayUnitTexId id)
{
	const unsigned idx = static_cast<unsigned>(id);
	if (!device || idx >= static_cast<unsigned>(CE_PLAYUNIT_TEX_COUNT))
		return NULL;
	if (gDevice != device)
	{
		ReleaseAll();
		gDevice = device;
	}
	if (gTextures[idx])
		return gTextures[idx];
	if (gBadAsset[idx])
		return NULL;
	if (gRetryCooldown[idx] > 0)
	{
		--gRetryCooldown[idx];
		return NULL;
	}

	const CePlayUnitTexturesData::Entry &entry = CePlayUnitTexturesData::kEntries[idx];
	if (!entry.packed || !entry.packedSize || !entry.rawSize || entry.rawSize > (4u << 20))
	{
		gBadAsset[idx] = true;
		return NULL;
	}

	unsigned char *raw = new unsigned char[entry.rawSize];
	if (!raw)
	{
		gRetryCooldown[idx] = 120;
		return NULL;
	}

	unsigned long outLen = entry.rawSize;
	const int rc = uncompress(raw, &outLen, entry.packed, entry.packedSize);
	if (rc == 0 && outLen == entry.rawSize)
	{
		IDirect3DTexture9 *tex = NULL;
		const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
			device, raw, static_cast<UINT>(outLen),
			static_cast<UINT>(entry.width), static_cast<UINT>(entry.height),
			0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
			D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX, 0, NULL, NULL, &tex);
		if (SUCCEEDED(hr) && tex)
		{
			gTextures[idx] = tex;
		}
		else
		{
			if (tex) tex->Release();
			gRetryCooldown[idx] = 120;	// retry after transient device loss without stalling every frame
			if (!gLoggedFail[idx])
			{
				gLoggedFail[idx] = true;
				PortSessionLog(PORT_SESSION_GAME, false,
				               "playunit: weapon texture %s failed upload; using plain colour",
				               entry.name);
			}
		}
	}
	else
	{
		gBadAsset[idx] = true;
	}
	delete[] raw;
	return gTextures[idx];
}

} // namespace CePlayUnitTextures

#else
#include "ce_playunit_textures.h"
namespace CePlayUnitTextures
{
IDirect3DTexture9 *Ensure(IDirect3DDevice9 *, CePlayUnitTexId) { return 0; }
void ReleaseAll() {}
} // namespace CePlayUnitTextures
#endif
