// DX.h: the Direct3D side of the PC build (only what matched files need): Direct3D 9 and the shell.

#ifndef DX_H
#define DX_H

#include <d3d9.h>
#ifdef new						// MemoryManager.h's new(type) macro would rewrite D3DX's operator new declarations
#undef new
#include <d3dx9shader.h>
#define new(type) new(type, __FILE__, __LINE__)
#else
#include <d3dx9shader.h>
#endif
#include "ltshell.h"

// the DirectX sample utilities' form
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }

#endif
