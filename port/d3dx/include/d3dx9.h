// SPDX-License-Identifier: GPL-3.0-only
// Native port: the D3DX 9 surface Battle Engine Aquila uses, over dxvk-native's d3d9.h.
// Written for the port from the public API; behaviour is validated against retail d3dx9 (port/d3dx/README.md).
#ifndef PORT_D3DX9_H
#define PORT_D3DX9_H

#include <limits.h>
#include <float.h>
#include <d3d9.h>

#define D3DX_DEFAULT ((UINT)-1)
#define D3DX_DEFAULT_NONPOW2 ((UINT)-2)
#define D3DX_DEFAULT_FLOAT FLT_MAX

#ifndef D3DXINLINE
#define D3DXINLINE inline
#endif

#define _FACDD 0x876
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, _FACDD, code)

enum _D3DXERR
{
    D3DXERR_CANNOTMODIFYINDEXBUFFER = MAKE_DDHRESULT(2900),
    D3DXERR_INVALIDMESH = MAKE_DDHRESULT(2901),
    D3DXERR_CANNOTATTRSORT = MAKE_DDHRESULT(2902),
    D3DXERR_SKINNINGNOTSUPPORTED = MAKE_DDHRESULT(2903),
    D3DXERR_TOOMANYINFLUENCES = MAKE_DDHRESULT(2904),
    D3DXERR_INVALIDDATA = MAKE_DDHRESULT(2905),
    D3DXERR_LOADEDMESHASNODATA = MAKE_DDHRESULT(2906),
    D3DXERR_DUPLICATENAMEDFRAGMENT = MAKE_DDHRESULT(2907)
};

#include "d3dx9math.h"
#include "d3dx9core.h"
#include "d3dx9tex.h"
#include "d3dx9shader.h"

#endif
