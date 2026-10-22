// SPDX-License-Identifier: GPL-3.0-only
// D3DX texture loading: the DDS paths Battle Engine Aquila uses (port/d3dx/README.md).
// The umbrella header first, outside the guard, so any entry order defines everything once.
#include "d3dx9.h"

#ifndef PORT_D3DX9TEX_H
#define PORT_D3DX9TEX_H

#define D3DX_FILTER_NONE (1 << 0)
#define D3DX_FILTER_POINT (2 << 0)
#define D3DX_FILTER_LINEAR (3 << 0)
#define D3DX_FILTER_TRIANGLE (4 << 0)
#define D3DX_FILTER_BOX (5 << 0)
#define D3DX_FILTER_MIRROR_U (1 << 16)
#define D3DX_FILTER_MIRROR_V (2 << 16)
#define D3DX_FILTER_MIRROR_W (4 << 16)
#define D3DX_FILTER_MIRROR (7 << 16)
#define D3DX_FILTER_DITHER (1 << 19)
#define D3DX_FILTER_SRGB_IN (1 << 20)
#define D3DX_FILTER_SRGB_OUT (2 << 20)
#define D3DX_FILTER_SRGB (3 << 20)

typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

typedef struct _D3DXIMAGE_INFO
{
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    D3DXIMAGE_FILEFORMAT ImageFileFormat;
} D3DXIMAGE_INFO;

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(
    LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize, UINT Width, UINT Height, UINT MipLevels,
    DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey,
    D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture);

HRESULT WINAPI D3DXCreateTextureFromFileExA(
    LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile, UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey,
    D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture);

// Only the debug screen dump (DebugLog.cpp) calls these two; the port returns D3DERR_NOTAVAILABLE.
HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
                                      LPDIRECT3DSURFACE9 pSrcSurface, CONST PALETTEENTRY *pSrcPalette,
                                      CONST RECT *pSrcRect);
HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE9 pDestSurface, CONST PALETTEENTRY *pDestPalette,
                                        CONST RECT *pDestRect, LPCSTR pSrcFile, CONST RECT *pSrcRect,
                                        DWORD Filter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo);

#ifdef __cplusplus
}
#endif

#define D3DXCreateTextureFromFileEx D3DXCreateTextureFromFileExA
#define D3DXSaveSurfaceToFile D3DXSaveSurfaceToFileA
#define D3DXLoadSurfaceFromFile D3DXLoadSurfaceFromFileA

#endif
