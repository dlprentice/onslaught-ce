// VertexShader.h: vertex shaders (VertexShader.obj, 00501650..00504369; not in Stuart's release). CVertexShader is a
// DeviceObject (vtable 005dfbc4, 0x5c bytes) kept in one list (00854e68) and shared by reference count; whether
// vertex shaders are in use is the static flag at 0063c108. A shader comes from assembly text or compiled code
// (LoadFromFile), from a declaration (a fixed-function shader built from render states), or from the resource file.

#ifndef VERTEXSHADER_H
#define VERTEXSHADER_H

#include "deviceobject.h"

class CChunkReader;
struct IDirect3DVertexShader9;

// A vertex-shader declaration token's name, for the debug menu (0x634070, 104 entries; DeserializeAll reads the
// table from the resource file and clears the names).
struct SVSDeclName
{
	SINT			mFlag;						// +0x00: only entries with 0 are shown
	char			*mName;						// +0x04
	DWORD			mToken;						// +0x08
};

class CVertexShader : public DeviceObject
{
public:
					CVertexShader();											// 00501800
	virtual			~CVertexShader();											// 005018b0
	virtual HRESULT	InitDeviceObjects();										// 005019c0 (folded)
	virtual HRESULT	RestoreDeviceObjects();										// 00501a10
	virtual HRESULT	InvalidateDeviceObjects();									// 00501b60
	virtual HRESULT	DeleteDeviceObjects()	{ return S_OK; }					// 005019c0 (folded)

	static bool		VertexShadersEnabled;		// 0063c108
	static bool		GetVertexShadersEnabled() { return VertexShadersEnabled; }
	static void		InitShaders();				// 005016b0
	static void		ClearOut();					// 00501730: deletes unreferenced shaders, reports and deletes the rest
	static void		CheckCaps();				// 005019d0
	static void		DeserializeAll(class CChunkReader *c);	// 005042f0: the PC reads the VSDS chunk (004d7776)
	static CVertexShader	*Load(CChunkReader *c, SINT index);				// 00503f90
	static CVertexShader	*Create(char *name, DWORD *function, SINT type, void *decl, UINT declsize, UINT size);	// 00502060
	static void		GetDeclarationNames(char *out, DWORD *decl);				// 00503dd0
	static CVertexShader	*BuildRenderInfoShader();						// 00503ac0: for RENDERINFO's states (Apply 00550f2d)
	DWORD			GetFVF();					// 00501ba0
	void			SetCustomConstants();		// 00502920: for a declaration shader
	void			Release();					// 00502290
	void			SetConstants();				// 00501cd0: the render state's matrices into the constants (CDXParticleTexture::RenderAll 0054ffcf)
	HRESULT			LoadFromFile(char *name, DWORD *function, SINT type, UINT size);	// 005022a0
	HRESULT			LoadFromDeclaration(char *name, SINT type, void *decl, UINT declsize);	// inline only
	void			CompileShader();			// 00502420
	HRESULT			LoadCompiled(char *name, DWORD usage, IDirect3DVertexShader9 **shader);	// 005027f0

	char			mName[32];					// +0x08
	IDirect3DVertexShader9*	mShader;			// +0x28 (PCLTShell::SetVertexShader)
	SINT			mType;						// +0x2c: the vertex format (GetFVF)
	SINT			mRefCount;					// +0x30
	bool			mFromDeclaration;			// +0x34
	DWORD			*mDeclaration;				// +0x38
	UINT			mDeclarationSize;			// +0x3c: bytes
	DWORD			**mConstants;				// +0x40: per declaration-name table entry (Load)
	SINT			*mConstantCounts;			// +0x44
	char			*mSource;					// +0x48: assembly text
	SINT			mSourceSize;				// +0x4c
	DWORD			*mFunction;					// +0x50: compiled code
	SINT			mFunctionSize;				// +0x54: -1 when mFunction is not ours
	CVertexShader	*mNext;						// +0x58

	static CVertexShader	*mFirst;			// 00854e68
	static bool		mSoftware;					// 00854e6c: the device's shader version is below 1.1
	static bool		mForce;						// 00854e6d: cg_forcevertexshaders
	static SINT		mNumDeclNames;				// 00854e74
	static SVSDeclName	mDeclNames[104];		// 00634070
};

#endif
