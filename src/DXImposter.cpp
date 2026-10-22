// DXImposter.cpp: reconstructed from DXImposter.obj (00542750..0054403f); Stuart's release has no copy. The PC side
// of the imposters: every imposter of a level shares one texture, and each is drawn as six textured quads (four
// around it, one above and one below) into two shared vertex buffers, lit by the sun from the billboard angle. The
// allocations' __LINE__ values are the retail ones; the #line directives place them, because the retail file's
// layout is unknown.
#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#define IMPOSTER_FULL
#include "mathsconst.h"			// FUNCTION: 0x00542750 init:ZERO_FVECTOR
								// FUNCTION: 0x00542770 init:ID_FMATRIX
#include "imposter.h"
#include "ltshell.h"
#include "state.h"
#include "renderinfo.h"
#include "renderablething.h"
#include "mesh.h"
#include "Texture.h"
#include "VBufTexture.h"
#include "VertexShader.h"
#include "chunker.h"
#include "spriterenderer.h"
#include "tweak.h"
#include "MemoryManager.h"
#include "meshrenderer.h"
DWORD	PackNormal(const FVector *n, float alpha);		// 004b52c0 (MeshRenderer.cpp)

// the billboards' rotations, a quarter turn apart from the billboard angle
static FMatrix		sRotations[4];				// 008aa790
// FUNCTION: 0x00542840 init:sRotations
FVector		sColumnZ;					// 008aa850 (the trees read both, DXTrees.cpp)
// FUNCTION: 0x00542870 initorder:4
FVector		sColumnX;					// 008aa860
// FUNCTION: 0x00542880 initorder:5
static FVector		sLightDir = ZERO_FVECTOR;	// 008aa780: towards the sun
// FUNCTION: 0x00542890 init:sLightDir
static FVector		sViewLight[4];				// 008aa730: the light in each rotation's frame
// FUNCTION: 0x005428c0 initorder:7
static DWORD		sViewLightPacked[4];		// 008aa770
static SINT			sUnknown8b0;				// 008aa8b0: -1 after each angle change

static D3DMATRIX	sMatrixA = { 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f };	// 00650848
static D3DMATRIX	sBillboard = { 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f };	// 00650888

// CDXImposter::sFirst, sLoaded and sAngle (0067a678..80) are defined in imposter.cpp: retail keeps them in the game group, after Hud.cpp's data.
CVBufTexture	*CDXImposter::sVB;
CDXTexture		*CDXImposter::sTexture;
SINT			CDXImposter::sCount;
SINT			CDXImposter::sUnknown8c0;
SINT			CDXImposter::sUnknown8c4;
SINT			CDXImposter::sUnknown8c8;
CVBufTexture	*CDXImposter::sVB2;
bool			CDXImposter::sShowTexture;

//******************************************************************************************
// The list is emptied before the matrices are cleared: the rep stos zero then serves sAngle alone, and the later
// zeros get their own register (edx, 005428f5).
BOOL CDXImposter::InitAll()	// FUNCTION: 0x005428d0
{
	sFirst = NULL;
	ZeroMemory(&sMatrixA, sizeof(sMatrixA));
	ZeroMemory(&sBillboard, sizeof(sBillboard));
	sAngle = 0;
	sLoaded = FALSE;
	sMatrixA._11 = sMatrixA._22 = sMatrixA._33 = sMatrixA._44 = 1.0f;
	sBillboard._11 = sBillboard._22 = sBillboard._33 = sBillboard._44 = 1.0f;
	sVB = NULL;
	sTexture = NULL;
	sCount = 0;
	sUnknown8c0 = 0;
	sUnknown8c4 = 0;
	if (!CVertexShader::VertexShadersEnabled)
		sUnknown8c8 = 0;
	return TRUE;
}
inline CDXImposter::~CDXImposter() { SAFE_DELETE(mFrames); sCount--; }	// not in the class: retail creates sCount with the other class statics (.bss)
//******************************************************************************************
void CDXImposter::ShutdownAll()	// FUNCTION: 0x00542990
{
	CDXImposter	*i = sFirst;
	while (i)
	{
		CDXImposter	*next = i->mNext;
		delete i;
		i = next;
	}
	sFirst = NULL;

	if (sVB2)
	{
		sVB2->Release();
		sVB2 = NULL;
	}
	if (sVB)
	{
		sVB->Release();
		sVB = NULL;
	}
	if (sTexture)
	{
		sTexture->Release();
		sTexture = NULL;
	}
	sUnknown8c0 = 0;
	sUnknown8c4 = 0;
}

//******************************************************************************************
CDXImposter::CDXImposter()	// FUNCTION: 0x00542a30
{
	mUnknown30 = 0;
	mUnknown38 = 0;
	mFrames = NULL;
	sCount++;
}

// The billboard matrix's rotation: the yaw rotation's transpose. The pinned DXEngine.cpp keeps a commented
// `D3DUtils.SetBillBoardMat(&bbori);` where it now calls SetBillBoardAngle, so the stores are written as a helper of that
// name. It sets the 3x3 part only and the caller pads it (sizes below).
static inline void SetBillBoardMat(const FMatrix &m)
{
	sBillboard._11 = m.Row[0].X;	sBillboard._12 = m.Row[1].X;	sBillboard._13 = m.Row[2].X;
	sBillboard._21 = m.Row[0].Y;	sBillboard._22 = m.Row[1].Y;	sBillboard._23 = m.Row[2].Y;
	sBillboard._31 = m.Row[0].Z;	sBillboard._32 = m.Row[1].Z;	sBillboard._33 = m.Row[2].Z;
}

// The direction towards the sun: NegateToOut, the copy into sLightDir, then NormalizeInPlace on it (00542dd7..00542e04).
// STAND-IN (concession, see SetBillBoardAngle): a helper inferred from the inline budget only.
static inline void SetLightDir(FVector &dir, const CLight &light)
{
	dir = -light.mDir;
	dir.Normalise();
}

//******************************************************************************************
// Matched with labelled STAND-INs. Retail's inline decisions, read from the code:
// m's MakeRotationYawF expands its first row and calls the other two (00542a9e, 00542acc); the first loop expands the
// matrix constructor and calls all three rows (00542bab..00542c0d); both columns are expanded (they read m's elements,
// 00542c5b..00542cdf) and the two operator+ constructors are called (00542c9b, 00542ce7); CLight's Set, the negation
// and Normalise are calls; the second loop calls everything (Inverse out of line, 00542e55). With those decisions
// retail's zero register comes back: ebp = 0 from 00542a69 to 00542e09 for every zero push, both `i = 0` and the seven
// zero sBillboard stores. SetBillBoardMat is the 3x3 part (148, inside the 131..164 the budget allows).
// - STAND-IN (budget): SetLightDir. Nothing in the pinned source shows such a helper; it is inferred from the budget
//   (a budget simulator and search) and its two statements inline exactly as
//   written in place. At size 39 it spends nothing, but it makes the negation and Normalise one site, so each earlier
//   site's share grows (the first loop's vector constructor iterator expands) and the negation, nested, is denied.
//   Written in place: 30/144.
// - STAND-IN (late zero): the centre as three float locals scaled by a zero (`float zero = 0;` after `sAngle = angle;`,
//   `c0 = m.Row[0].Y * zero` and so on). Retail adds three separate 4-byte slots (X, Y, Z at frame +0x14, +0x10, i's,
//   and +0x18) filled with `mov [slot],0` just before the adds (00542c5f..00542c74): values that were not known
//   constants when C2's global optimizer substituted and became 0 only in the later folding (as RenderMeshCore's
//   `t * zero`). As floats they are separate frame symbols packed beside i, a and the loop temporaries, as retail's;
//   an FVector centre is one 16-byte object, which moves every slot above it by 0x10. With plain zeros (folded at
//   once, the adds dropped): 109/329.
// - STAND-IN (allocation and budget): each column's components as float locals with a self-copy test, `if (!z) z =
//   m.Row[2].X;` and `if (!zz) zz = m.Row[2].Z;`. CSE turns each arm into a self-copy that reaches the allocator as
//   its own blocks and is deleted after allocation with its test (no code). The components stay separate webs, so each
//   element is loaded at its add (`fld [m]; fadd [slot]`, 00542c5b..00542c8f), where a constructor's parameters were
//   loaded early and nested on the x87 stack. The short tests also keep the own size where the second loop's Inverse
//   stays out of line (with `z < 0` tests it expands and calls its four parts: 10/10). Without the X arm 32/62,
//   without the Z arm 28/58.
// Loop 2's angle as a named float (`float a = i * (PI / 2) + sAngle;`) gives retail's second loop (without it 11/41).
// Plain form on main: 80/234 (mvar shape/instructions).
void CDXImposter::SetBillBoardAngle(float angle)	// FUNCTION: 0x00542a50
{
	sAngle = angle;
	float	zero = 0;

	FMatrix	m;
	m.MakeRotationYawF(angle);
	SetBillBoardMat(m);
	sBillboard._14 = sBillboard._24 = sBillboard._34 = 0;
	sBillboard._41 = 0;	sBillboard._42 = 0;	sBillboard._43 = 0;	sBillboard._44 = 1.0f;

	SINT	i;
	for (i = 0; i < 4; i++)
	{
		FMatrix	r;
		r.MakeRotationYawF(i * (PI / 2) + sAngle);
		sRotations[i] = r;
	}

	float	c0 = m.Row[0].Y * zero, c1 = m.Row[1].Y * zero, c2 = m.Row[2].Y * zero;
	float	x = m.Row[0].X, y = m.Row[1].X, z = m.Row[2].X;
	if (!z)
		z = m.Row[2].X;
	sColumnX = FVector(x, y, z) + FVector(c0, c1, c2);
	float	zx = m.Row[0].Z, zy = m.Row[1].Z, zz = m.Row[2].Z;
	if (!zz)
		zz = m.Row[2].Z;
	sColumnZ = FVector(zx, zy, zz) + FVector(c0, c1, c2);

	CLight	light;
	light = RENDERINFO.mLights[0];
	SetLightDir(sLightDir, light);		// STAND-IN (see the function comment)

	for (i = 0; i < 4; i++)
	{
		float	a = i * (PI / 2) + sAngle;
		FMatrix	r;
		r.MakeRotationYawF(a);
		r = r.Inverse();
		FVector	v = r * sLightDir;
		sViewLight[i] = v;
		sViewLightPacked[i] = PackNormal(&v, 1.0f);
	}
	sUnknown8b0 = -1;
}

// FUNCTION: 0x00542ee0 ?MakeRotationYawF@FMatrix@@QAEXM@Z

// A vertex of the imposter buffers (FVF 0x152).
struct SImposterVertex
{
	CFVF_GTEXPos	mPos;			// +0x00
	CFVF_GTEXPos	mNormal;		// +0x0c
	DWORD			mDiffuse;		// +0x18
	float			mU, mV;			// +0x1c
};

// Retail keeps all three pos-right components on the x87 stack until subtracting up (00543069..00543090).
// This helper preserves that boundary with the pinned VC6 compiler; the other corners round between sums.
static FVector LowerQuadCorner(const FVector &pos, const FVector &right, const FVector &up)
{
	float delta[3] = {(pos.X - right.X), (pos.Y - right.Y), (pos.Z - right.Z)};
	return FVector(delta[0] - up.X, delta[1] - up.Y, delta[2] - up.Z);
}

//******************************************************************************************
void CDXImposter::AddQuad(FVector *pos, FVector *right, FVector *up, DWORD col, DWORD col2, float u0, float v0, float u1, float v1, BOOL alt)	// FUNCTION: 0x00542f90
{
	FVector	normal = *right ^ *up;
	normal.Normalise();

	if (CMeshRenderer::GetRenderMode() == 4)
		return;

	SImposterVertex	*v;
	SINT	index;
	if (alt)
		index = sVB2->GetVertexPtr((void **)&v, 4);
	else
	{
		if (!sVB)
			return;
		index = sVB->GetVertexPtr((void **)&v, 4);
	}

	v[0].mPos = LowerQuadCorner(*pos, *right, *up);
	v[0].mU = u0;
	v[0].mV = v0;
	v[1].mPos = (*pos + *right) - *up;
	v[1].mU = u1;
	v[1].mV = v0;
	v[2].mPos = (*pos + *right) + *up;
	v[2].mU = u1;
	v[2].mV = v1;
	v[3].mPos = (*pos - *right) + *up;
	v[3].mU = u0;
	v[3].mV = v1;
	v[0].mDiffuse = col;
	v[1].mDiffuse = col;
	v[2].mDiffuse = col;
	v[3].mDiffuse = col;
	v[0].mNormal = normal;
	v[1].mNormal = normal;
	v[2].mNormal = normal;
	v[3].mNormal = normal;

	UWORD	*ind = alt ? sVB2->GetIndexPtr(6) : sVB->GetIndexPtr(6);
	ind[0] = index;
	ind[1] = index + 1;
	ind[2] = index + 2;
	ind[3] = index + 2;
	ind[4] = index + 3;
	ind[5] = index;
}

//******************************************************************************************
// Retail accumulates each bounding-box row as (X + Y) + Z (00543430..005434a2). The rows are read through references,
// which number their field symbols later and give the dot products retail's term order.
static FVector ImposterOffset(const FMatrix &ori, const FVector &offset)
{
	const FVector	&r0 = ori.Row[0];
	const FVector	&r1 = ori.Row[1];
	const FVector	&r2 = ori.Row[2];
	return FVector(((r0.X * offset.X) + (r0.Y * offset.Y)) + (r0.Z * offset.Z),
				   ((r1.X * offset.X) + (r1.Y * offset.Y)) + (r1.Z * offset.Z),
				   ((r2.X * offset.X) + (r2.Y * offset.Y)) + (r2.Z * offset.Z));
}

// A row scaled axis by axis. Retail multiplies every row of a view's matrix by one vector (width, 1, height): the
// vector has its own 16 bytes in the frame (sub esp,0x254; the height is its Z, stored at 0054374d), the Y column is
// read and written back unchanged (00543716..0054375f), and the vector and the three row sites are inline sites after
// the second pitch matrix, whose vector constructor iterator retail calls (00543612) where six scaling statements let
// ours expand it. The name and the signature are ours.
static inline void ScaleAxes(FVector &row, const FVector &scale)
{
	row.X *= scale.X;	row.Y *= scale.Y;	row.Z *= scale.Z;
}

// The scale vector's members are assigned directly from the frame sizes: named float locals
// for the sizes each cost a no-code x87 node in scheduler region 10, full at the 81-node cap, so without them its cut
// runs two nodes further into AddQuad's argument setup (0054384c..00543858 as retail), and without Set's inline
// expansion the first row's products take the row's operand first as retail (00543754, 0054377f).
void CDXImposter::Render(IRenderableThing *thing, SINT alpha, SINT frame)	// FUNCTION: 0x00543300
{
	if ((!sLoaded) || (!mUnknown38))
		return;

	FVector	pos = thing->GetRenderPos();
	FMatrix	ori = thing->GetRenderOrientation();
	FMatrix	turn;								// a quarter turn in yaw: cos/sin of -1.57, then of 0 (00543345..00543375)
	turn.MakeRotationF(-1.57f, 0.0f, 0.0f);

	CBoundingBox	*box = mMesh->mBoundingBox;
	pos += ImposterOffset(ori, *(FVector *)box);

	DWORD	col = (alpha << 24) | 0xffffff;
	for (SINT n = 0; n < 6; n++)
	{
		FMatrix	o = ori;
		if (n == 4)
		{
			FMatrix	pitch;
			pitch.MakeRotationPitchF(PI / 2);
			o = o * pitch;
		}
		else if (n == 5)
		{
			FMatrix	pitch;
			pitch.MakeRotationPitchF(PI * 1.5f);
			o = o * pitch;
		}

		FVector	scale;
		scale.X = mFrames[n].mWidth * 0.99f;	// frame 0's size for every frame (00543710..0054372f)
		scale.Y = 1.0f;
		scale.Z = mFrames[n].mHeight * 0.99f;
		ScaleAxes(o.Row[0], scale);
		ScaleAxes(o.Row[1], scale);
		ScaleAxes(o.Row[2], scale);
		FVector	right(1.0f, 0, 0);
		FVector	up(0, 0, 1.0f);
		right = o * right;
		up = o * up;
		SImposterFrame	*f = &mFrames[frame * mViews + n];
		AddQuad(&pos, &right, &up, col, col, f->mU0, f->mV0, f->mU1, f->mV1, mAlt);

		if (n < 4)
			ori = turn * ori;	// retail passes turn as the receiver at 00543888, ori as the argument at 00543878
	}
}

//******************************************************************************************
void CDXImposter::RenderAllImposters()	// FUNCTION: 0x005438c0
{
	if ((!sLoaded) || (!sFirst))
		return;

	if (sShowTexture)
	{
		RENDERINFO.SetFogEnabled(false);
		LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		RENDERINFO.Apply();
		CSPRITERENDERER::DrawSprite(400.0f, 300.0f, 0, sTexture, SA_CENTRE, SPT_NONE, 1.0f, 0, 0xffffffff, 1.0f, 1.0f, 0, 1.0f, 0, 1.0f);
	}
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	FVector	pos(0, 0, 0);
	FMatrix	ori(1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f);
	RENDERINFO.SetWorld(pos, ori);
	LT.D3D_SetMaterial(STATE.GetDefaultMaterial());

	RENDERINFO.EnableLighting(true);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.SetModulate(0);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.D3D_SetTexture(0, sTexture->GetTexture());
	if (sVB)
		sVB->DWATS(true, 0, 0);

	RENDERINFO.EnableLighting(false);
	LT.SRS(D3DRS_TEXTUREFACTOR, 0xffffffff);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	LT.SetModulate(0);
	LT.SRS(D3DRS_ALPHAREF, 8);
	sVB2->DWATS(true, 0, 0);

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.STS(1, D3DTSS_COLORARG0, D3DTA_TFACTOR);
	LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	LT.SSS_NoCache(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.D3D_SetTexture(2, NULL);
	LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.SetAlphaRef(8);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.EnableLighting(true);
}

//******************************************************************************************
void CDXImposter::DeserializeAll(CChunkReader *c)	// FUNCTION: 0x00543d90
{
	c->GetNext();
	c->GetNext();
	c->Read(&sUnknown8c0, 4, 1);
	c->Read(&sUnknown8c4, 4, 1);
	c->GetNext();
	if (sTexture)
	{
		sTexture->Release();
		sTexture = NULL;
	}
	sTexture = CDXTexture::Deserialize(false, c);
	sTexture->AddRef();

	c->GetNext();
	SINT	n;
	c->Read(&n, 4, 1);
	for (SINT i = 0; i < n; i++)
		Create(c);

#line 1857
	sVB = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(NULL);
	sVB2 = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(NULL);
	BOOL	managed = !CLAIM_WRITEONLY.mValue;
	sVB->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(SImposterVertex), D3DPT_TRIANGLELIST, managed);
	sVB->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, managed);
	sVB2->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(SImposterVertex), D3DPT_TRIANGLELIST, managed);
	sVB2->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, managed);
	sLoaded = TRUE;
}

//******************************************************************************************
CDXImposter *CDXImposter::Create(CChunkReader *c)	// FUNCTION: 0x00543f50
{
#line 2019
	CDXImposter	*imp = new(MEMTYPE_IMPOSTER) CDXImposter;
	CDXImposter	*next = imp->mNext;

	c->GetNext();
	c->GetNext();
	c->Read(imp, sizeof(CDXImposter), 1);
	c->GetNext();
	SINT	mesh;
	c->Read(&mesh, 4, 1);
	imp->mMesh = CMesh::GetMeshByResourceID(mesh);
	imp->mNext = NULL;
#line 2055
	imp->mFrames = new(MEMTYPE_IMPOSTER) SImposterFrame[imp->mNumFrames * imp->mViews];
	c->GetNext();
	c->Read(imp->mFrames, sizeof(SImposterFrame), imp->mNumFrames * imp->mViews);
	imp->mNext = next;
	imp->Link();
	return imp;
}
