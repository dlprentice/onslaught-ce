// SPDX-License-Identifier: GPL-3.0-only
// ID3DXBuffer: the result object of D3DXAssembleShader.
// The umbrella header first, outside the guard, so any entry order defines everything once.
#include "d3dx9.h"

#ifndef PORT_D3DX9CORE_H
#define PORT_D3DX9CORE_H

#ifdef __cplusplus
struct ID3DXBuffer : public IUnknown
{
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};
#else
typedef struct ID3DXBuffer ID3DXBuffer;
#endif
typedef struct ID3DXBuffer *LPD3DXBUFFER;

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer);
#ifdef __cplusplus
}
#endif

#endif
