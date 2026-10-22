// VertexShader.cpp: reconstructed from VertexShader.obj (00501650..00504369); Stuart's release has no copy.
// The allocations' __LINE__ values are the retail ones. The maths constants are included part-way down the file:
// their initializers (00501b80, 00501c00) sit between InvalidateDeviceObjects and GetFVF.
#include "common.h"
#define VERTEXSHADER_CPP
#include "ltshell.h"
#include "console.h"
#include "chunker.h"
#include "CLIParams.h"
#include "Platform.h"
#include "VertexShader.h"
#include "renderinfo.h"
#include "engine.h"
#include "Camera.h"
#include "atmospherics.h"
#include <d3dx9.h>

// The debug menu of vertex shaders (vtable 005dfba4); a selected declaration shader lists its declaration.
class CVertexShaderMenu : public CConsoleMenu
{
public:
	CVertexShaderMenu() { mSelected = -1; }
	virtual void	GetName(char *name);						// 00501670
	virtual int		GetNumEntries();							// 00503ed0
	virtual void	GetEntry(int num, char *text);				// 00503ef0
	virtual void	OnClick(int num);							// 005016a0
	virtual bool	GetShowSubmenus() { return true; }			// 00401480 (folded)

	int				mSelected;									// +0x14
};

SINT			CVertexShader::mNumDeclNames;	// placement evidence: retail's .bss has these four after the menu (created first)
bool			CVertexShader::mForce;
bool			CVertexShader::mSoftware;
CVertexShader	*CVertexShader::mFirst;
CVertexShaderMenu	VERTEX_SHADER_MENU;							// 00854e10
// FUNCTION: 0x00501650 init:VERTEX_SHADER_MENU


// Initial PE table at 00634070; DeserializeAll later replaces its rows and clears the names.
SVSDeclName CVertexShader::mDeclNames[104] =
{
	{ 0, "f_create_eyespace_vertex", 0 },
	{ 0, "f_create_eyespace_normal", 0 },
	{ 0, "f_create_eyespace_eyevector_local_viewer", 0 },
	{ 0, "f_calculate_eyespace_reflection", 0 },
	{ 0, "f_calculate_eyespace_directionallight_diffuse_intensity", 0 },
	{ 0, "f_calculate_eyespace_pointlight_diffuse_intensity", 0 },
	{ 0, "f_calculate_eyespace_directionallight_diffuse_and_specular_intensity_localviewer", 0 },
	{ 0, "f_calculate_eyespace_directionallight_diffuse_and_specular_intensity_nonlocalviewer", 0 },
	{ 0, "f_calculate_eyespace_directionallight_diffuse_and_specular_intensity_rdotl", 0 },
	{ 0, "f_calculate_eyespace_pointlight_vector", 0 },
	{ 0, "f_calculate_eyespace_pointlight_diffuse_and_specular_intensity_localviewer", 0 },
	{ 0, "f_calculate_eyespace_pointlight_diffuse_and_specular_intensity_nonlocalviewer", 0 },
	{ 0, "f_calculate_eyespace_pointlight_diffuse_and_specular_intensity_rdotl", 0 },
	{ 0, "f_set_power", 0 },
	{ 0, "f_clamp_diffuse_exponentiate_specular", 0 },
	{ 0, "f_pointlight_accumulate_diffuse", 0 },
	{ 0, "f_pointlight_accumulate_specular", 0 },
	{ 0, "f_light_accumulate_diffuse", 0 },
	{ 0, "f_dirlight_accumulate_specular", 0 },
	{ 0, "f_write_diffuse_result", 0 },
	{ 0, "f_write_specular_result", 0 },
	{ 0, "f_create_objectspace_eyevector_local_viewer", 0 },
	{ 0, "f_calculate_objectspace_reflection", 0 },
	{ 0, "f_calculate_objectspace_directionallight_diffuse_intensity", 0 },
	{ 0, "f_calculate_and_accumulate_objectspace_hemispherelight", 0 },
	{ 0, "f_calculate_objectspace_pointlight_diffuse_intensity", 0 },
	{ 0, "f_calculate_objectspace_directionallight_diffuse_and_specular_intensity_localviewer", 0 },
	{ 0, "f_calculate_objectspace_directionallight_diffuse_and_specular_intensity_nonlocalviewer", 0 },
	{ 0, "f_calculate_objectspace_directionallight_diffuse_and_specular_intensity_rdotl", 0 },
	{ 0, "f_calculate_objectspace_pointlight_vector", 0 },
	{ 0, "f_calculate_objectspace_pointlight_diffuse_and_specular_intensity_localviewer", 0 },
	{ 0, "f_calculate_objectspace_pointlight_diffuse_and_specular_intensity_nonlocalviewer", 0 },
	{ 0, "f_calculate_objectspace_pointlight_diffuse_and_specular_intensity_rdotl", 0 },
	{ 0, "f_set_defaults", 0 },
	{ 0, "f_set_ambient", 0 },
	{ 0, "f_set_default_texture", 0 },
	{ 0, "f_set_objectspace_normal", 0 },
	{ 0, "f_set_white_lighting", 0 },
	{ 0, "f_blend_vertex_colour", 0 },
	{ 0, "f_load_position", 0 },
	{ 0, "f_zero_position", 0 },
	{ 0, "f_xform_worldview", 0 },
	{ 0, "f_xform_projection", 0 },
	{ 0, "f_write_screenspace_position", 0 },
	{ 0, "f_calculate_cubereflection_vector", 0 },
	{ 0, "f_calculate_linear_range_fog", 0 },
	{ 0, "f_calculate_exp_fog", 0 },
	{ 0, "f_write_texture", 0 },
	{ 0, "f_write_default_fog", 0 },
	{ 0, "f_write_default_diffuse", 0 },
	{ 0, "f_write_default_specular", 0 },
	{ 0, "f_write_default_texture", 0 },
	{ 0, "f_tree_normal", 0 },
	{ 0, "f_tree_billboard", 0 },
	{ 0, "f_screenspace_shift", 0 },
	{ 0, "f_screenspace_squash", 0 },
	{ 0, "f_diffuse_blend", 0 },
	{ 0, "f_skeletal_animation", 0 },
	{ 0, "f_snow", 0 },
	{ 1, "c_worldview_0", 0 },
	{ 1, "c_worldview_1", 0 },
	{ 1, "c_worldview_2", 0 },
	{ 1, "c_worldview_3", 0 },
	{ 1, "c_worldviewit_0", 0 },
	{ 1, "c_worldviewit_1", 0 },
	{ 1, "c_worldviewit_2", 0 },
	{ 1, "c_dirlight_direction", 0 },
	{ 1, "c_pointlight_position", 0 },
	{ 1, "c_pointlight_attenuation2", 0 },
	{ 1, "c_dirlight_halfvector", 0 },
	{ 1, "c_eyespace_eyevector", 0 },
	{ 1, "c_mat_power", 0 },
	{ 1, "c_pointlight_diffuse", 0 },
	{ 1, "c_pointlight_specular", 0 },
	{ 1, "c_light_diffuse", 0 },
	{ 1, "c_dirlight_specular", 0 },
	{ 1, "c_objectspace_eyeposition", 0 },
	{ 1, "c_hemilight_skyvector", 0 },
	{ 1, "c_constants", 0 },
	{ 1, "c_hemilight_ground_colour", 0 },
	{ 1, "c_hemilight_sky_colour", 0 },
	{ 1, "c_hemilight_direction", 0 },
	{ 1, "c_objectspace_eyevector", 0 },
	{ 1, "c_ambient_light", 0 },
	{ 1, "c_projection_0", 0 },
	{ 1, "c_projection_1", 0 },
	{ 1, "c_projection_2", 0 },
	{ 1, "c_projection_3", 0 },
	{ 1, "c_cubematrix_0", 0 },
	{ 1, "c_cubematrix_1", 0 },
	{ 1, "c_cubematrix_2", 0 },
	{ 1, "c_fogconst", 0 },
	{ 1, "c_camera_position", 0 },
	{ 1, "c_tree_cutoff_squared", 0 },
	{ 1, "c_billboard_xvector", 0 },
	{ 1, "c_billboard_yvector", 0 },
	{ 1, "c_shift_factor", 0 },
	{ 1, "c_screenspace_centre", 0 },
	{ 1, "c_screenspace_squashfactor", 0 },
	{ 1, "c_diffuse_blend_factor", 0 },
	{ 1, "c_diffuse_blend_colour_premultiplied", 0 },
	{ 1, "c_snow_texscale", 0 },
	{ 1, "c_snow_colour", 0 },
	{ 1, "c_snow_upvector", 0 },
};

// Specimen data: NVIDIA shader-fragment library (no shipped source). Retail's .data holds five serialized NVIDIA vertex-shader
// fragment objects (NVFO: eyespace 00634550, lighting 00635908, objectspace 00635f28, miscvertex 00637818, custom 00638828; each
// names its .nvf source and shaders/fragments/constants.h) between the declaration-name table and VertexShadersEnabled. Nothing
// reads them (CVertexShader only walks the table); they are unreferenced data of this object. The game ships no copy, so
// The build writes the initializer from the pinned specimen into the ignored src/data/NVShaderFragments.inc.
unsigned char gNVShaderFragments[] =
{
#include "data\NVShaderFragments.inc"
};

bool CVertexShader::VertexShadersEnabled = true;	// retail 0063c108

//******************************************************************************************
void CVertexShaderMenu::GetName(char *name)	// FUNCTION: 0x00501670
{
	strcpy(name, "Vertex Shaders");
}

//******************************************************************************************
void CVertexShaderMenu::OnClick(int num)	// FUNCTION: 0x005016a0
{
	mSelected = num;
}

//******************************************************************************************
void CVertexShader::InitShaders()	// FUNCTION: 0x005016b0
{
	CheckCaps();
	if (VertexShadersEnabled)
	{
		CONSOLE.GetRootMenu()->AddSubmenu(&VERTEX_SHADER_MENU);
		CONSOLE.RegisterVariable("cg_forcevertexshaders", "Should vertex shaders be used wherever possible?", CVar_bool, &mForce);
	}
}

//******************************************************************************************
void CVertexShader::ClearOut()	// FUNCTION: 0x00501730
{
	char buf[256];
	CVertexShader *v = mFirst;
	while (v)
	{
		CVertexShader *next = v->mNext;
		if (!v->mRefCount)
			delete v;
		v = next;
	}
	v = mFirst;
	if (v)
	{
		TRACE("\n--------------------------------------------------\n");
		TRACE("Shader leaks (I'll be deleting them anyway though)\n");
		TRACE("--------------------------------------------------\n\n");
		do
		{
			sprintf(buf, "Shader '%s' leaked, refcount=%d\n", v->mName, v->mRefCount);
			TRACE(buf);
			v = v->mNext;
		} while (v);
		TRACE("\n------------\n\n");
	}
	else
		TRACE("No shader leaks\n");
	v = mFirst;
	while (v)
	{
		CVertexShader *next = v->mNext;
		delete v;
		v = next;
	}
}

//******************************************************************************************
CVertexShader::CVertexShader()	// FUNCTION: 0x00501800
{
	memset(mName, 0, sizeof(mName));
	mShader = NULL;
	mRefCount = 0;
	LT.AddDeviceObject(this);
	mNext = mFirst;
	mFirst = this;
	mFromDeclaration = false;
	mDeclaration = NULL;
	mDeclarationSize = 0;
	mConstants = NULL;
	mConstantCounts = NULL;
	mType = 9;
	mSource = NULL;
	mSourceSize = 0;
	mFunction = NULL;
	mFunctionSize = 0;
}
// FUNCTION: 0x00501890 ??_GCVertexShader@@UAEPAXI@Z

//******************************************************************************************
CVertexShader::~CVertexShader()	// FUNCTION: 0x005018b0
{
	for (CVertexShader *v = mFirst; v; v = v->mNext)
		if (v->mNext == this)
			v->mNext = mNext;
	if (mFirst == this)
		mFirst = mNext;
	LT.RemoveDeviceObject(this);
	SAFE_RELEASE(mShader);
	if (mConstants)
	{
		delete [] mConstants[0];
		delete [] mConstants;
	}
	if (mConstantCounts)
		delete [] mConstantCounts;
	SAFE_DELETE_ARRAY(mDeclaration);
	SAFE_DELETE_ARRAY(mSource);
	if (mFunctionSize != -1)
		SAFE_DELETE_ARRAY(mFunction);
}
//******************************************************************************************
HRESULT CVertexShader::InitDeviceObjects()	// FUNCTION: 0x005019c0
{
	return S_OK;
}

//******************************************************************************************
void CVertexShader::CheckCaps()	// FUNCTION: 0x005019d0
{
	if (LT.mD3DActive)
	{
		D3DCAPS9 caps;
		LT.GetDevice()->GetDeviceCaps(&caps);
		mSoftware = caps.VertexShaderVersion < D3DVS_VERSION(1, 1);
	}
}

//******************************************************************************************
HRESULT CVertexShader::RestoreDeviceObjects()	// FUNCTION: 0x00501a10
{
	if (LT.mD3DActive)
	{
		D3DCAPS9 caps;
		LT.GetDevice()->GetDeviceCaps(&caps);
		if (caps.VertexShaderVersion < D3DVS_VERSION(1, 1))
			mSoftware = true;
		else
			mSoftware = false;
		if (mSoftware)
			LT.SetSoftwareVertexProcessing(true);
		if (mFromDeclaration)
		{
			DWORD usage = 0;
			if (mSoftware)
				usage = D3DUSAGE_SOFTWAREPROCESSING;
			DWORD *code = mFunction;
			if (code)
			{
				if (FAILED(LT.D3D_CreateVertexShader(NULL, code, (DWORD *)&mShader, usage)))
				{
					TRACE("Could not create shader from code at 0x%08x", code);
					return E_FAIL;
				}
			}
			else
				return E_FAIL;
		}
		else
		{
			DWORD *code = mFunction;
			if (code)
			{
				DWORD usage = 0;
				if (mSoftware)
					usage = D3DUSAGE_SOFTWAREPROCESSING;
				if (FAILED(LT.D3D_CreateVertexShader(NULL, code, (DWORD *)&mShader, usage)))
				{
					TRACE("Could not create shader from code at 0x%08x", code);
					return E_FAIL;
				}
			}
			else if (strlen(mName))
			{
				DWORD usage = 0;
				if (mSoftware)
					usage = D3DUSAGE_SOFTWAREPROCESSING;
				if (FAILED(LoadCompiled(mName, usage, &mShader)))
					return E_FAIL;
			}
		}
		if (mSoftware)
			LT.SetSoftwareVertexProcessing(false);
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVertexShader::InvalidateDeviceObjects()	// FUNCTION: 0x00501b60
{
	SAFE_RELEASE(mShader);
	return S_OK;
}

#include "mathsconst.h"		// FUNCTION: 0x00501b80 init:ZERO_FVECTOR
// FUNCTION: 0x00501c00 init:ID_FMATRIX

//******************************************************************************************
DWORD CVertexShader::GetFVF()	// FUNCTION: 0x00501ba0
{
	switch (mType)
	{
	case 1:		return D3DFVF_XYZB3 | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	case 3:		return D3DFVF_XYZB1 | D3DFVF_DIFFUSE;
	case 4:
	case 7:		return D3DFVF_XYZ | D3DFVF_TEX1;
	case 6:
	case 8:		return D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	case 0:		return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	case 2:		return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	case 5:		return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	}
	return D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
}

//******************************************************************************************
void CVertexShader::SetConstants()	// FUNCTION: 0x00501cd0
{
	if (!VertexShadersEnabled)
		return;
	if (mFromDeclaration)
	{
		SetCustomConstants();
		return;
	}
	D3DXVECTOR4		c;
	D3DXVECTOR4		v;
	D3DXMATRIX		t;
	D3DXVECTOR4		eye;
	D3DXMATRIX		a, b, m1, cm, m3, m0, inv, inv1, inv2;

	RENDERINFO.GetBiasedProjection(&a);
	RENDERINFO.GetWorld(&b);
	RENDERINFO.GetView(&cm);
	D3DXMatrixMultiply(&m0, &a, &b);
	D3DXMatrixMultiply(&m1, &m0, &cm);
	D3DXMatrixMultiply(&m3, &a, &b);
	D3DXMatrixTranspose(&m1, &m1);
	LT.GetDevice()->SetVertexShaderConstantF(0, m1, 4);
	c.x = 0;
	c.y = 256.0f;
	c.z = 1.0f / 700.0f;
	c.w = 0.5f;
	LT.GetDevice()->SetVertexShaderConstantF(95, c, 1);
	c.x = 0;
	c.y = 0.25f;
	c.z = 1000.0f / 700.0f;
	c.w = 1.0f;
	LT.GetDevice()->SetVertexShaderConstantF(94, c, 1);

	RENDERINFO.GetBiasedProjection(&cm);
	RENDERINFO.GetWorld(&a);
	RENDERINFO.GetView(&b);
	D3DXMatrixInverse(&inv1, NULL, &a);
	D3DXMatrixInverse(&inv2, NULL, &a);
	D3DXMatrixMultiply(&m0, &a, &b);
	D3DXMatrixMultiply(&m1, &m0, &cm);
	D3DXMatrixMultiply(&m3, &a, &b);
	D3DXMatrixTranspose(&t, &m3);
	LT.GetDevice()->SetVertexShaderConstantF(4, t.m[0], 1);
	LT.GetDevice()->SetVertexShaderConstantF(5, t.m[1], 1);
	LT.GetDevice()->SetVertexShaderConstantF(6, t.m[2], 1);
	LT.GetDevice()->SetVertexShaderConstantF(7, t.m[3], 1);
	D3DXMatrixTranspose(&t, &cm);
	LT.GetDevice()->SetVertexShaderConstantF(8, t.m[0], 1);
	LT.GetDevice()->SetVertexShaderConstantF(9, t.m[1], 1);
	LT.GetDevice()->SetVertexShaderConstantF(10, t.m[2], 1);
	LT.GetDevice()->SetVertexShaderConstantF(11, t.m[3], 1);
	v.x = 0;
	v.y = 0;
	v.z = 0;
	D3DXMatrixInverse(&inv, NULL, &m3);
	D3DXVec3Transform(&eye, (D3DXVECTOR3 *)&v, &inv);
	LT.GetDevice()->SetVertexShaderConstantF(25, eye, 1);
	v.x = RENDERINFO.GetFogDensity();
	v.y = 2.71828f;
	v.z = 0;
	v.w = 0;
	LT.GetDevice()->SetVertexShaderConstantF(26, v, 1);
	if (_stricmp(mName, "ShadowShader") == 0)
	{
	}
}

//******************************************************************************************
CVertexShader *CVertexShader::Create(char *name, DWORD *function, SINT type, void *decl, UINT declsize, UINT size)	// FUNCTION: 0x00502060
{
	if (!VertexShadersEnabled)
		return NULL;
	CVertexShader *found = NULL;
	bool fromdecl = false;
	if (decl)
		fromdecl = true;
	for (CVertexShader *v = mFirst; v; v = v->mNext)
	{
		if (found)
			break;
		if (fromdecl == v->mFromDeclaration && type == v->mType)
		{
			if (fromdecl)
			{
				if (declsize == v->mDeclarationSize && memcmp(decl, v->mDeclaration, declsize) == 0)
					found = v;
			}
			else if (_stricmp(v->mName, name) == 0 && function == v->mFunction)
				found = v;
		}
	}
	if (!found)
	{
#line 701
		CVertexShader *s = new(MEMTYPE_VERTEXSHADER) CVertexShader;
		found = s;
		if (!fromdecl)
		{
			if (FAILED(s->LoadFromFile(name, function, type, size)))
			{
				delete s;
				return NULL;
			}
		}
		else if (FAILED(s->LoadFromDeclaration(name, type, decl, declsize)))
		{
			delete s;
			return NULL;
		}
	}
	found->mRefCount++;
	return found;
}

//******************************************************************************************
void CVertexShader::Release()	// FUNCTION: 0x00502290
{
	mRefCount--;
}

//******************************************************************************************
HRESULT CVertexShader::LoadFromFile(char *name, DWORD *function, SINT type, UINT size)	// FUNCTION: 0x005022a0
{
	if (FAILED(InvalidateDeviceObjects()))
		return E_FAIL;
	strcpy(mName, name);
	mFromDeclaration = false;
	mType = type;
	LPD3DXBUFFER shader = NULL;
	LPD3DXBUFFER errors = NULL;
	void *code;
	if (size == -1)
	{
		if (FAILED(D3DXAssembleShader((char *)function, strlen((char *)function), NULL, NULL, 0, &shader, &errors)))
		{
			if (errors)
				TRACE("%s\n", errors->GetBufferPointer());
			CONSOLE.FatalError(false, 0xd2, 500);
			return E_FAIL;
		}
		code = shader->GetBufferPointer();
		size = shader->GetBufferSize();
	}
	else
		code = function;
	if (size)
	{
#line 797
		mFunction = (DWORD *)new(MEMTYPE_VERTEXSHADER) UBYTE[size];
		memcpy(mFunction, code, size);
		mFunctionSize = size;
	}
	else
	{
		mFunction = (DWORD *)code;
		mFunctionSize = -1;
	}
	SAFE_RELEASE(shader);
	SAFE_RELEASE(errors);
	if (FAILED(RestoreDeviceObjects()))
		return E_FAIL;
	return S_OK;
}

//******************************************************************************************
inline HRESULT CVertexShader::LoadFromDeclaration(char *name, SINT type, void *decl, UINT declsize)
{
	if (FAILED(InvalidateDeviceObjects()))
		return E_FAIL;
	strcpy(mName, name);
	mType = type;
#line 838
	mDeclaration = new(MEMTYPE_VERTEXSHADER) DWORD[declsize / sizeof(DWORD)];
	memcpy(mDeclaration, decl, declsize);
	mDeclarationSize = declsize;
	mFromDeclaration = true;
	if (FAILED(RestoreDeviceObjects()))
		return E_FAIL;
	return S_OK;
}

//******************************************************************************************
void CVertexShader::CompileShader()	// FUNCTION: 0x00502420
{
	SAFE_DELETE_ARRAY(mFunction);
	LPD3DXBUFFER shader = NULL;
	LPD3DXBUFFER errors = NULL;
	char version[] = "vs.1.1";
	char header[100];
	char *start = strstr(mSource, version);
	if (!start)
	{
		start = strstr(mSource, "vs.1.0");
		CONSOLE.FatalError(start != NULL, 0xd2, 200);
	}
	strcpy(header, "vs.1.1\ndcl_position v0\n");
	if (strstr(mSource, "v11"))
		strcat(header, "dcl_blendweight0 v11\n");
	if (strstr(mSource, "v3"))
		strcat(header, "dcl_normal v3\n");
	if (strstr(mSource, "v5"))
		strcat(header, "dcl_color v5\n");
	if (strstr(mSource, "v7"))
		strcat(header, "dcl_texcoord v7");
#line 941
	char *source = new(MEMTYPE_SCRATCHPAD) char[mSourceSize - strlen(version) + strlen(header) + 1];
	strcpy(source, mSource);
	strcpy(source + (start - mSource), header);
	strcpy(source + (start - mSource) + strlen(header), start + strlen(version));
	delete [] mSource;
	mSource = source;
	mSourceSize = strlen(source);
	char *fog;
	while (TRUE)
	{
		fog = strstr(mSource, "oFog.x");
		if (fog == NULL)
			break;
		fog[4] = ' ';
		fog[5] = ' ';
	}
	HRESULT hr;
	if (FAILED(hr = D3DXAssembleShader(mSource, mSourceSize, NULL, NULL, 0, &shader, &errors)))
	{
		TRACE("D3DXAssembleShader failed for %s!\n\n", DXGetErrorString9(hr));
		TRACE("%s\n", errors->GetBufferPointer());
		TRACE("\n");
	}
	mFunctionSize = shader->GetBufferSize();
#line 996
	mFunction = (DWORD *)new(MEMTYPE_VERTEXSHADER) UBYTE[mFunctionSize];
	memcpy(mFunction, shader->GetBufferPointer(), mFunctionSize);
	SAFE_RELEASE(shader);
	SAFE_RELEASE(errors);
	SAFE_DELETE_ARRAY(mSource);
	mSourceSize = 0;
}

//******************************************************************************************
HRESULT CVertexShader::LoadCompiled(char *name, DWORD usage, IDirect3DVertexShader9 **shader)	// FUNCTION: 0x005027f0
{
	char path[256];
	char message[256];
	sprintf(path, ".\\Shaders\\%s.vso", name);
	HANDLE file = CreateFile(ToTCHAR(path), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		sprintf(message, "Could not find shader file %s", name);
		return E_FAIL;
	}
	DWORD size = GetFileSize(file, NULL);
	DWORD *code = (DWORD *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
	if (!code)
	{
		sprintf(message, "Failed to allocate memory for shader %s", name);
		return E_FAIL;
	}
	ReadFile(file, code, size, &size, NULL);
	CloseHandle(file);
	HRESULT hr = LT.D3D_CreateVertexShader(NULL, code, (DWORD *)shader, usage);
	if (FAILED(hr))
		TRACE("Could not create shader from code at 0x%08x", code);
	else
		hr = S_OK;
	HeapFree(GetProcessHeap(), 0, code);
	return hr;
}

// the fragments of the render-state shader, by their mDeclNames rows
static inline DWORD	Fragment(SINT n) { return CVertexShader::mDeclNames[n].mToken; }
#define FRAGMENT(n)	Fragment(n)
enum
{
	F_SET_POWER = 13,
	F_CLAMP_DIFFUSE_EXPONENTIATE_SPECULAR = 14,
	F_LIGHT_ACCUMULATE_DIFFUSE = 17,
	F_WRITE_DIFFUSE_RESULT = 19,
	F_CREATE_OBJECTSPACE_EYEVECTOR_LOCAL_VIEWER = 21,
	F_CALCULATE_OBJECTSPACE_DIRECTIONALLIGHT_DIFFUSE_INTENSITY = 23,
	F_CALCULATE_AND_ACCUMULATE_OBJECTSPACE_HEMISPHERELIGHT = 24,
	F_CALCULATE_OBJECTSPACE_POINTLIGHT_DIFFUSE_INTENSITY = 25,
	F_SET_DEFAULTS = 33,
	F_SET_AMBIENT = 34,
	F_SET_OBJECTSPACE_NORMAL = 36,
	F_SET_WHITE_LIGHTING = 37,
	F_BLEND_VERTEX_COLOUR = 38,
	F_LOAD_POSITION = 39,
	F_XFORM_WORLDVIEW = 41,
	F_XFORM_PROJECTION = 42,
	F_WRITE_SCREENSPACE_POSITION = 43,
	F_CALCULATE_LINEAR_RANGE_FOG = 45,
	F_CALCULATE_EXP_FOG = 46,
	F_WRITE_DEFAULT_DIFFUSE = 49,
	F_WRITE_DEFAULT_TEXTURE = 51,
	F_TREE_NORMAL = 52,
	F_TREE_BILLBOARD = 53,
	F_SCREENSPACE_SHIFT = 54,
	F_SCREENSPACE_SQUASH = 55,
	F_DIFFUSE_BLEND = 56,
	F_SKELETAL_ANIMATION = 57,
	F_SNOW = 58
};

// the constants the fragments read, by their mDeclNames rows
enum
{
	C_WORLDVIEW_0 = 59,
	C_DIRLIGHT_DIRECTION = 66,
	C_POINTLIGHT_POSITION = 67,
	C_POINTLIGHT_ATTENUATION2 = 68,
	C_MAT_POWER = 71,
	C_LIGHT_DIFFUSE = 74,
	C_OBJECTSPACE_EYEPOSITION = 76,
	C_HEMILIGHT_SKYVECTOR = 77,
	C_CONSTANTS = 78,
	C_HEMILIGHT_GROUND_COLOUR = 79,
	C_HEMILIGHT_SKY_COLOUR = 80,
	C_HEMILIGHT_DIRECTION = 81,
	C_AMBIENT_LIGHT = 83,
	C_PROJECTION_0 = 84,
	C_FOGCONST = 91,
	C_CAMERA_POSITION = 92,
	C_TREE_CUTOFF_SQUARED = 93,
	C_BILLBOARD_XVECTOR = 94,
	C_BILLBOARD_YVECTOR = 95,
	C_SHIFT_FACTOR = 96,
	C_SCREENSPACE_CENTRE = 97,
	C_SCREENSPACE_SQUASHFACTOR = 98,
	C_DIFFUSE_BLEND_FACTOR = 99,
	C_DIFFUSE_BLEND_COLOUR_PREMULTIPLIED = 100,
	C_SNOW_TEXSCALE = 101,
	C_SNOW_COLOUR = 102,
	C_SNOW_UPVECTOR = 103
};
// the register a declaration shader gave the k'th use of a constant; constants and fragments share the token accessor
#define CONSTANT(n, k)	(mConstants[FRAGMENT(n)][k])

extern float	TREE_MESH_DISTANCE;			// 006321a0: nearer than this a tree is its mesh

//******************************************************************************************
// A declaration shader's constants: each fragment in its declaration loads the constants it reads, the lights in
// turn from RENDERINFO's enabled ones. The token accessor keeps the light constructor's last FVector::Set out
// of line (0050342a), so the light type is reread after each call.
void CVertexShader::SetCustomConstants()	// FUNCTION: 0x00502920
{
	D3DXVECTOR4	c;
	D3DXMATRIX	a, b, cm, m0, m1, m3, inv, inv1, inv2, t;

	RENDERINFO.GetBiasedProjection(&cm);
	RENDERINFO.GetWorld(&a);
	RENDERINFO.GetView(&b);
	D3DXMatrixInverse(&inv1, NULL, &a);
	D3DXMatrixInverse(&inv2, NULL, &a);
	D3DXMatrixMultiply(&m0, &a, &b);
	D3DXMatrixMultiply(&m1, &m0, &cm);
	D3DXMatrixMultiply(&m3, &a, &b);

	c.x = 0;
	c.y = 1.0f;
	c.z = 0.5f;
	c.w = 2.0f;
	LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_CONSTANTS, 0), c, 1);

	SINT	light = 0;
	SINT	lights = 0;
	SINT	dirlights = 0;
	SINT	pointlights = 0;
	SINT	hemilights = 0;
	for (DWORD *d = mDeclaration; *d; d++)
	{
		if (*d == FRAGMENT(F_XFORM_WORLDVIEW))
		{
			D3DXMatrixTranspose(&t, &m3);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_WORLDVIEW_0, 0), t.m[0], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_WORLDVIEW_0 + 1, 0), t.m[1], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_WORLDVIEW_0 + 2, 0), t.m[2], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_WORLDVIEW_0 + 3, 0), t.m[3], 1);
		}
		if (*d == FRAGMENT(F_XFORM_PROJECTION))
		{
			D3DXMATRIX	pt;
			D3DXMatrixTranspose(&pt, &cm);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_PROJECTION_0, 0), pt.m[0], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_PROJECTION_0 + 1, 0), pt.m[1], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_PROJECTION_0 + 2, 0), pt.m[2], 1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_PROJECTION_0 + 3, 0), pt.m[3], 1);
		}
		if (*d == FRAGMENT(F_SCREENSPACE_SQUASH))
		{
			FVector	squash = RENDERINFO.GetScreenSquash();
			c.x = squash.X;
			c.y = squash.Y;
			c.z = squash.Z;
			c.w = 1.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SCREENSPACE_SQUASHFACTOR, 0), c, 1);
			D3DXVECTOR4	origin(0, 0, 0, 1.0f);
			D3DXVECTOR4	centre;
			D3DXVec4Transform(&centre, &origin, &m1);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SCREENSPACE_CENTRE, 0), centre, 1);
		}
		if (*d == FRAGMENT(F_DIFFUSE_BLEND))
		{
			float	blend = RENDERINFO.GetDiffuseBlend() / 255.0f;
			DWORD	col = RENDERINFO.GetDiffuseBlendColour();
			c.x = ((col >> 16) & 0xff) / 255.0f;
			c.y = ((col >> 8) & 0xff) / 255.0f;
			c.z = (col & 0xff) / 255.0f;
			c.w = (col >> 24) / 255.0f;
			c *= blend;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_DIFFUSE_BLEND_COLOUR_PREMULTIPLIED, 0), c, 1);
			c.x = 1.0f - blend;
			c.y = 0;
			c.z = 0;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_DIFFUSE_BLEND_FACTOR, 0), c, 1);
		}
		if (*d == FRAGMENT(F_SCREENSPACE_SHIFT))
		{
			FVector	shift = RENDERINFO.GetScreenShift();
			c.x = shift.X;
			c.y = shift.Y;
			c.z = shift.Z;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SHIFT_FACTOR, 0), c, 1);
		}
		if (*d == FRAGMENT(F_SKELETAL_ANIMATION))
		{
			// a third of each of the three bones, then the bones themselves from register 10
			D3DXMATRIX	third(1.0f / 3.0f, 0, 0, 0,
							  0, 1.0f / 3.0f, 0, 0,
							  0, 0, 1.0f / 3.0f, 0,
							  0, 0, 0, 1.0f / 3.0f);
			LT.GetDevice()->SetVertexShaderConstantF(7, third.m[0], 1);
			LT.GetDevice()->SetVertexShaderConstantF(8, third.m[1], 1);
			LT.GetDevice()->SetVertexShaderConstantF(9, third.m[2], 1);
			for (SINT i = 0; i < RENDERINFO.GetNumBones(); i++)
			{
				D3DXMATRIX	bone, bt;
				RENDERINFO.GetMatrix(i, &bone);
				D3DXMatrixTranspose(&bt, &bone);
				LT.GetDevice()->SetVertexShaderConstantF(10 + i * 3, bt.m[0], 1);
				LT.GetDevice()->SetVertexShaderConstantF(11 + i * 3, bt.m[1], 1);
				LT.GetDevice()->SetVertexShaderConstantF(12 + i * 3, bt.m[2], 1);
			}
		}
		if ((*d == FRAGMENT(F_WRITE_DEFAULT_DIFFUSE)) || (*d == FRAGMENT(F_SET_AMBIENT)))
		{
			DWORD	col = RENDERINFO.GetAmbient();
			c.x = ((col >> 16) & 0xff) / 255.0f;
			c.y = ((col >> 8) & 0xff) / 255.0f;
			c.z = (col & 0xff) / 255.0f;
			c.w = 1.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_AMBIENT_LIGHT, 0), c, 1);
		}
		if (*d == FRAGMENT(F_SET_POWER))
		{
			c.x = 0;
			c.y = 0;
			c.z = 0;
			c.w = 30.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_MAT_POWER, 0), c, 1);
		}
		if (*d == FRAGMENT(F_SNOW))
		{
			// the snow settles against the wind
			FVector	wind = CAtmospherics::mWindVector;
			wind.Z = 1.0f;
			// STAND-IN ((double) rank cast; review F 2026-10-15): retail squares one promoted Y value, then adds X squared and Z squared
			// (005031ba..005031e5); without the cast ApplyCustomRenderStateShaderConstants 00502920 loses its match.
			float squared = (double)wind.Y * wind.Y;
			squared += wind.X * wind.X;
			squared += wind.Z * wind.Z;
			float length = sqrtf(squared);
			if (length)
			{
				length = 1.f / length;
				wind.X *= length;
				wind.Y *= length;
				wind.Z *= length;
			}
			D3DXVECTOR4	up(-wind.X, -wind.Y, -wind.Z, 0);
			D3DXVECTOR4	oup;
			D3DXVec4Transform(&oup, &up, &inv1);
			c.x = oup.x;
			c.y = oup.y;
			c.z = oup.z;
			c.w = 1.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SNOW_UPVECTOR, 0), c, 1);
			c.x = 4.0f;
			c.y = 0;
			c.z = 0;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SNOW_TEXSCALE, 0), c, 1);
			c.x = 0.8f;
			c.y = 0.8f;
			c.z = 0.8f;
			c.w = 1.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_SNOW_COLOUR, 0), c, 1);
		}
		if ((*d == FRAGMENT(F_CALCULATE_OBJECTSPACE_DIRECTIONALLIGHT_DIFFUSE_INTENSITY)) ||
			(*d == FRAGMENT(F_CALCULATE_AND_ACCUMULATE_OBJECTSPACE_HEMISPHERELIGHT)) ||
			(*d == FRAGMENT(F_CALCULATE_OBJECTSPACE_POINTLIGHT_DIFFUSE_INTENSITY)))
		{
			// the next enabled light, in object space
			while (!RENDERINFO.GetLightEnable(light))
				light++;
			CLight	l;	// declared then assigned, Stuart's occasional habit (thing.cpp:462, :482); `CLight l = RENDERINFO.GetLight(light);` loses the match
			l = RENDERINFO.GetLight(light);
			c.x = l.mDiffuseR;
			c.y = l.mDiffuseG;
			c.z = l.mDiffuseB;
			c.w = 1.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_LIGHT_DIFFUSE, lights), c, 1);

			D3DXVECTOR4	v, ov;
			if (l.mType == LIGHT_DIRECTIONAL)
			{
				v.x = -l.mDir.X;
				v.y = -l.mDir.Y;
				v.z = -l.mDir.Z;
				v.w = 0;
			}
			else if (l.mType == 1)
			{
				v.x = l.mPos.X;
				v.y = l.mPos.Y;
				v.z = l.mPos.Z;
				v.w = 1.0f;
			}
			else if (l.mType == 2)
			{
				v.x = -l.mDir.X;
				v.y = -l.mDir.Y;
				v.z = -l.mDir.Z;
				v.w = 0;
			}
			D3DXVec4Transform(&ov, &v, &inv1);
			c.x = ov.x;
			c.y = ov.y;
			c.z = ov.z;
			c.w = 1.0f;
			if (l.mType == LIGHT_DIRECTIONAL)
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_DIRLIGHT_DIRECTION, dirlights), c, 1);
			else if (l.mType == 1)
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_POINTLIGHT_POSITION, pointlights), c, 1);
			else if (l.mType == 2)
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_HEMILIGHT_DIRECTION, hemilights), c, 1);
			if (l.mType == 2)
			{
				// the hemisphere's sky vector and its two colours
				v.x = l.mUnknown4c.X;
				v.y = l.mUnknown4c.Y;
				v.z = l.mUnknown4c.Z;
				v.w = 0;
				D3DXVec4Transform(&ov, &v, &inv1);
				c.x = ov.x;
				c.y = ov.y;
				c.z = ov.z;
				c.w = 1.0f;
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_HEMILIGHT_SKYVECTOR, hemilights), c, 1);
				c.x = l.mUnknown40;
				c.y = l.mUnknown44;
				c.z = l.mUnknown48;
				c.w = 1.0f;
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_HEMILIGHT_GROUND_COLOUR, hemilights), c, 1);
				c.x = l.mUnknown34;
				c.y = l.mUnknown38;
				c.z = l.mUnknown3c;
				c.w = 1.0f;
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_HEMILIGHT_SKY_COLOUR, hemilights), c, 1);
			}
			if (l.mType == 1)
			{
				c.x = 1.0f / (l.mUnknown30 * l.mUnknown30 * 0.05f);
				c.y = 0;
				c.z = 0;
				c.w = 0;
				LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_POINTLIGHT_ATTENUATION2, pointlights), c, 1);
			}
			if (l.mType == LIGHT_DIRECTIONAL)
				dirlights++;
			else if (l.mType == 1)
				pointlights++;
			else if (l.mType == 2)
				hemilights++;
			light++;
			lights++;
		}
		if (*d == FRAGMENT(F_CREATE_OBJECTSPACE_EYEVECTOR_LOCAL_VIEWER))
		{
			D3DXVECTOR4	eye;
			D3DXVECTOR3	v;
			v.x = 0;
			v.y = 0;
			v.z = 0;
			D3DXMatrixInverse(&inv, NULL, &m3);
			D3DXVec3Transform(&eye, &v, &inv);
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_OBJECTSPACE_EYEPOSITION, 0), eye, 1);
		}
		if (*d == FRAGMENT(F_CALCULATE_LINEAR_RANGE_FOG))
		{
			c.x = RENDERINFO.GetFogStart();
			c.y = RENDERINFO.GetFogEnd();
			c.z = 1.0f / (c.y - c.x);
			c.w = 255.0f;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_FOGCONST, 0), c, 1);
		}
		if (*d == FRAGMENT(F_CALCULATE_EXP_FOG))
		{
			c.x = RENDERINFO.GetFogDensity();
			c.y = 2.71828f;
			c.z = 0;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_FOGCONST, 0), c, 1);
		}
		if (*d == FRAGMENT(F_TREE_BILLBOARD))
		{
			FVector	cam = ENGINE.GetCamera()->GetPos();
			c.x = cam.X;
			c.y = cam.Y;
			c.z = cam.Z;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_CAMERA_POSITION, 0), c, 1);
			if ((TREE_MESH_DISTANCE >= 10.0f) && (!FORCE_OBJECT_IMPOSTERS))
			{
				float	cutoff = TREE_MESH_DISTANCE - 10.0f;
				c.x = cutoff * cutoff;
			}
			else
				c.x = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_TREE_CUTOFF_SQUARED, 0), c, 1);
			FVector	bx = RENDERINFO.GetBillboardX();
			c.x = bx.X;
			c.y = bx.Y;
			c.z = bx.Z;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_BILLBOARD_XVECTOR, 0), c, 1);
			FVector	bz = RENDERINFO.GetBillboardZ();
			c.x = bz.X;
			c.y = bz.Y;
			c.z = bz.Z;
			c.w = 0;
			LT.GetDevice()->SetVertexShaderConstantF(CONSTANT(C_BILLBOARD_YVECTOR, 0), c, 1);
		}
	}
}

//******************************************************************************************
// Assembles a shader from fragments for RENDERINFO's current states: at most four lights (two when skinning). The
// accessors spend the inline budget, so CLight's constructor calls two of its FVector::Set out of line (00503c42,
// 00503c7e), as retail does. An enabled light past the limit still counts.
CVertexShader *CVertexShader::BuildRenderInfoShader()	// FUNCTION: 0x00503ac0
{
	DWORD	fragments[128];

	SINT	n = 0;
	fragments[n++] = FRAGMENT(F_SET_DEFAULTS);
	if (RENDERINFO.mUnknown308)
		fragments[n++] = FRAGMENT(F_SKELETAL_ANIMATION);
	else
		fragments[n++] = FRAGMENT(F_LOAD_POSITION);
	if (RENDERINFO.mUnknown2f4)
		fragments[n++] = FRAGMENT(F_TREE_NORMAL);
	else
		fragments[n++] = FRAGMENT(F_SET_OBJECTSPACE_NORMAL);
	if (RENDERINFO.GetBillboard())
		fragments[n++] = FRAGMENT(F_TREE_BILLBOARD);
	fragments[n++] = FRAGMENT(F_XFORM_WORLDVIEW);
	if (RENDERINFO.mUnknown2f6)
		fragments[n++] = FRAGMENT(F_SCREENSPACE_SQUASH);
	fragments[n++] = FRAGMENT(F_XFORM_PROJECTION);
	if (RENDERINFO.mUnknown328)
		fragments[n++] = FRAGMENT(F_SCREENSPACE_SHIFT);
	fragments[n++] = FRAGMENT(F_SET_AMBIENT);

	SINT	maxlights = 4;
	if (RENDERINFO.mUnknown308)
		maxlights = 2;
	SINT	numlights = 0;

	if (RENDERINFO.mUnknown2f5)
		fragments[n++] = FRAGMENT(F_SNOW);
	else if (RENDERINFO.GetLightingEnabled())
	{
		for (SINT i = 0; i < 8; i++)
		{
			if (RENDERINFO.GetLightEnable(i))
			{
				if (numlights < maxlights)
				{
					CLight	light;	// declared then assigned, as SetCustomConstants' copy (see there)
					light = RENDERINFO.GetLight(i);
					if (light.mType == LIGHT_DIRECTIONAL)
					{
						fragments[n++] = FRAGMENT(F_CALCULATE_OBJECTSPACE_DIRECTIONALLIGHT_DIFFUSE_INTENSITY);
						fragments[n++] = FRAGMENT(F_CLAMP_DIFFUSE_EXPONENTIATE_SPECULAR);
						fragments[n++] = FRAGMENT(F_LIGHT_ACCUMULATE_DIFFUSE);
					}
					else if (light.mType == 1)
					{
						fragments[n++] = FRAGMENT(F_CALCULATE_OBJECTSPACE_POINTLIGHT_DIFFUSE_INTENSITY);
						fragments[n++] = FRAGMENT(F_CLAMP_DIFFUSE_EXPONENTIATE_SPECULAR);
						fragments[n++] = FRAGMENT(F_LIGHT_ACCUMULATE_DIFFUSE);
					}
					else if (light.mType == 2)
						fragments[n++] = FRAGMENT(F_CALCULATE_AND_ACCUMULATE_OBJECTSPACE_HEMISPHERELIGHT);
				}
				numlights++;
			}
		}
	}
	else
		fragments[n++] = FRAGMENT(F_SET_WHITE_LIGHTING);

	fragments[n++] = FRAGMENT(F_BLEND_VERTEX_COLOUR);
	if (RENDERINFO.mUnknown320)
		fragments[n++] = FRAGMENT(F_DIFFUSE_BLEND);
	fragments[n++] = FRAGMENT(F_CREATE_OBJECTSPACE_EYEVECTOR_LOCAL_VIEWER);
	fragments[n++] = FRAGMENT(F_CALCULATE_EXP_FOG);
	fragments[n++] = FRAGMENT(F_WRITE_SCREENSPACE_POSITION);
	fragments[n++] = FRAGMENT(F_WRITE_DIFFUSE_RESULT);
	if (!RENDERINFO.mUnknown2f5)
		fragments[n++] = FRAGMENT(F_WRITE_DEFAULT_TEXTURE);
	fragments[n] = 0;
	UINT	size = (n + 1) * sizeof(DWORD);

	if (!RENDERINFO.GetCustomDeclaration())
		return Create("RenderInfo shader", NULL, 0, fragments, size, 0);
	return Create("RenderInfo shader (custom declaration)", NULL, RENDERINFO.GetCustomDeclaration(), fragments, size, 0);
}

// retail calls GetDeclarationNames out of line from the menu's GetEntry (00503f2f), where /Ob2 would inline it.
#pragma auto_inline(off)
//******************************************************************************************
void CVertexShader::GetDeclarationNames(char *out, DWORD *decl)	// FUNCTION: 0x00503dd0
{
	char buf[256];
	strcpy(out, "");
	while (*decl)
	{
		sprintf(buf, "Unknown (%d)", *decl);
		for (int i = 0; i < sizeof(mDeclNames) / sizeof(mDeclNames[0]); i++)
			if (!mDeclNames[i].mFlag && mDeclNames[i].mToken == *decl && mDeclNames[i].mName)
				sprintf(buf, "%s (%d)", mDeclNames[i].mName, *decl);
		decl++;
		strcat(out, buf);
		strcat(out, "\n");
	}
}

#pragma auto_inline(on)
//******************************************************************************************
int CVertexShaderMenu::GetNumEntries()	// FUNCTION: 0x00503ed0
{
	int n = 0;
	for (CVertexShader *v = CVertexShader::mFirst; v; v = v->mNext)
		n++;
	return n;
}

//******************************************************************************************
void CVertexShaderMenu::GetEntry(int num, char *text)	// FUNCTION: 0x00503ef0
{
	char decl[4096];
	CVertexShader *v = CVertexShader::mFirst;
	for (int i = num; v && i > 0; i--)
		v = v->mNext;
	if (num == mSelected && v->mFromDeclaration)
	{
		CVertexShader::GetDeclarationNames(decl, v->mDeclaration);
		sprintf(text, "%s\n\n%s", v->mName, decl);
	}
	else
		sprintf(text, "%s", v->mName);
}

//******************************************************************************************
CVertexShader *CVertexShader::Load(CChunkReader *c, SINT index)	// FUNCTION: 0x00503f90
{
	char name[200];
#line 2458
	CVertexShader *v = new(MEMTYPE_VERTEXSHADER) CVertexShader;
	DeviceObject *devnext = v->DeviceObject::mNext;
	CVertexShader *next = v->mNext;
	void *vtable = *(void **)v;
	c->GetNext();
	c->GetNext();
	c->Read(v, sizeof(CVertexShader), 1);
	v->DeviceObject::mNext = devnext;
	v->mNext = next;
	*(void **)v = vtable;
	if (v->mDeclaration)
	{
#line 2502
		v->mDeclaration = new(MEMTYPE_VERTEXSHADER) DWORD[v->mDeclarationSize / sizeof(DWORD)];
		c->GetNext();
		c->Read(v->mDeclaration, v->mDeclarationSize, 1);
	}
	if (v->mConstantCounts)
	{
#line 2514
		v->mConstantCounts = new(MEMTYPE_VERTEXSHADER) SINT[mNumDeclNames];
		c->GetNext();
		c->Read(v->mConstantCounts, 4, mNumDeclNames);
	}
	if (v->mConstants)
	{
		c->GetNext();
#line 2531
		v->mConstants = new(MEMTYPE_VERTEXSHADER) DWORD *[mNumDeclNames];
		int total = 0;
		int i;
		for (i = 0; i < mNumDeclNames; i++)
			total += v->mConstantCounts[i];
#line 2543
		v->mConstants[0] = new(MEMTYPE_VERTEXSHADER) DWORD[total];
		DWORD *p = v->mConstants[0];
		for (i = 0; i < mNumDeclNames; i++)
		{
			v->mConstants[i] = p;
			c->Read(v->mConstants[i], 4, v->mConstantCounts[i]);
			p += v->mConstantCounts[i];
		}
	}
	SAFE_DELETE_ARRAY(v->mConstantCounts);
	if (v->mFunction)
	{
		c->GetNext();
#line 2571
		v->mFunction = (DWORD *)new(MEMTYPE_VERTEXSHADER) UBYTE[v->mFunctionSize];
		c->Read(v->mFunction, v->mFunctionSize, 1);
	}
	if (v->mSource)
	{
		c->GetNext();
		if (CLIPARAMS.mUnknown17d)
		{
			SINT size = v->mSourceSize * 2 + 10000;
#line 2590
			v->mSource = new(MEMTYPE_VERTEXSHADER) char[size];
			c->Read(v->mSource, v->mSourceSize, 1);
			sprintf(name, "shader%03d.i", index);
			FILE *f = fopen(name, "rb");
			if (f)
			{
				SINT n = fread(v->mSource, 1, size, f);
				if (n == size)
					CONSOLE.FatalError(false, 0xd2, 0xc9);
				v->mSource[n] = 0;
				v->mSourceSize = n;
				fclose(f);
			}
		}
		else
		{
#line 2621
			v->mSource = new(MEMTYPE_VERTEXSHADER) char[v->mSourceSize + 1];
			c->Read(v->mSource, v->mSourceSize, 1);
			v->mSource[v->mSourceSize] = 0;
		}
	}
	v->CompileShader();
	delete [] v->mSource;
	v->mSource = NULL;
	v->mSourceSize = 0;
	v->RestoreDeviceObjects();
	return v;
}

//******************************************************************************************
void CVertexShader::DeserializeAll(CChunkReader *c)	// FUNCTION: 0x005042f0
{
	SINT numdecls, numshaders;
	c->Read(&numdecls, 4, 1);
	c->Read(&numshaders, 4, 1);
	c->Read(mDeclNames, sizeof(mDeclNames), 1);
	for (int j = 0; j < sizeof(mDeclNames) / sizeof(mDeclNames[0]); j++)
		mDeclNames[j].mName = NULL;
	mNumDeclNames = numdecls;
	for (int i = 0; i < numshaders; i++)
		Load(c, i);
}
