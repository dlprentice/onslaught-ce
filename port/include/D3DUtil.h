// SPDX-License-Identifier: GPL-3.0-only
// port/include/D3DUtil.h: the declarations the game takes from the DirectX 9 SDK sample framework's D3DUtil.h (not
// redistributable, and absent from MinGW), with the SDK's names and signatures. The body the game uses is its own
// src/d3dutil.cpp (a two-argument form). Both targets.
#ifndef PORT_D3DUTIL_H
#define PORT_D3DUTIL_H
#include <windows.h>
#include <d3d9.h>
#include <d3dx9math.h>

HRESULT D3DUtil_SetDeviceCursor(LPDIRECT3DDEVICE9 pd3dDevice, HCURSOR hCursor, BOOL bAddWatermark);
#endif
