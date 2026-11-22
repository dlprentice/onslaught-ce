// SPDX-License-Identifier: GPL-3.0-only
// Runtime loader for Play Any Unit's original GPL-3.0-only 1P weapon texture atlases
// (generated into ce_playunit_textures_data.h by port/tools/generate_playunit_textures.py).
// Textures upload into D3DPOOL_MANAGED with mipmaps for D3D9 Reset safety, with plain-colour fallback.
#ifndef PORT_CE_PLAYUNIT_TEXTURES_H
#define PORT_CE_PLAYUNIT_TEXTURES_H

enum CePlayUnitTexId
{
	CE_PLAYUNIT_TEX_FORSETI_RIFLE = 0,
	CE_PLAYUNIT_TEX_MUSPELL_RIFLE = 1,
	CE_PLAYUNIT_TEX_COUNT
};

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace CePlayUnitTextures
{
IDirect3DTexture9 *Ensure(IDirect3DDevice9 *device, CePlayUnitTexId id);
void ReleaseAll();
} // namespace CePlayUnitTextures

#endif
