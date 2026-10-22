// SPDX-License-Identifier: GPL-3.0-only
// D3DXAssembleShader: vs_1_x and ps_1_x/ps_2_0 assembly text to D3D9 token streams (port/d3dx/README.md).
// The umbrella header first, outside the guard, so any entry order defines everything once.
#include "d3dx9.h"

#ifndef PORT_D3DX9SHADER_H
#define PORT_D3DX9SHADER_H

#define D3DXSHADER_DEBUG (1 << 0)
#define D3DXSHADER_SKIPVALIDATION (1 << 2)
#define D3DXSHADER_SKIPOPTIMIZATION (1 << 3)
#define D3DXSHADER_PACKMATRIX_ROWMAJOR (1 << 4)
#define D3DXSHADER_PACKMATRIX_COLUMNMAJOR (1 << 5)

typedef struct _D3DXMACRO
{
    LPCSTR Name;
    LPCSTR Definition;
} D3DXMACRO, *LPD3DXMACRO;

typedef enum _D3DXINCLUDE_TYPE
{
    D3DXINC_LOCAL,
    D3DXINC_SYSTEM,
    D3DXINC_FORCE_DWORD = 0x7fffffff
} D3DXINCLUDE_TYPE;

#ifdef __cplusplus
struct ID3DXInclude
{
    virtual HRESULT Open(D3DXINCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData,
                         LPCVOID *ppData, UINT *pBytes) = 0;
    virtual HRESULT Close(LPCVOID pData) = 0;
};
#else
typedef struct ID3DXInclude ID3DXInclude;
#endif
typedef struct ID3DXInclude *LPD3DXINCLUDE;

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI D3DXAssembleShader(LPCSTR pSrcData, UINT SrcDataLen, CONST D3DXMACRO *pDefines,
                                  LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXBUFFER *ppShader,
                                  LPD3DXBUFFER *ppErrorMsgs);

#ifdef __cplusplus
}
#endif

#endif
