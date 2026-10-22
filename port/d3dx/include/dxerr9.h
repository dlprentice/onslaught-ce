// SPDX-License-Identifier: GPL-3.0-only
// DXGetErrorString9: the name of an HRESULT, as DxErr9 reports it (the game logs these; it never asks for the
// descriptions, which the port does not provide).
#ifndef PORT_DXERR9_H
#define PORT_DXERR9_H

#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif
const char *WINAPI DXGetErrorString9A(HRESULT hr);
#ifdef __cplusplus
}
#endif

#define DXGetErrorString9 DXGetErrorString9A

#endif
