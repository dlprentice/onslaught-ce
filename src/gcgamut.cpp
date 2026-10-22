// gcgamut.cpp: C3DGamut, the region of the map the camera can see (reconstructed; the file name is the object's
// __FILE__, 0062c968). The map is cut into 64 x 64 cells; each frame MakeIt finds the cells under the view and
// ComputePlanes the height span of each cell that the view's four side planes leave open.

#include	"common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include	"mathsconst.h"
#define GCGAMUT_CPP
#include	"gcgamut.h"
#include	"console.h"
#include	"DXEngine.h"
#include	"Camera.h"
#include	"viewport.h"
#include	"renderinfo.h"
#include	"thing.h"
#include	"world.h"
#include	"mapwho.h"
#include	"Shadows.h"

// FUNCTION: 0x004740c0 init:ZERO_FVECTOR
// FUNCTION: 0x004740e0 init:ID_FMATRIX

bool	gGamutLocked = false;								// 0067a070: cg_gamutlocked
bool	gShowGamut = false;									// 0067a071: cg_showgamut
bool	gRenderImposters = true;					// 0062c8c4: cg_renderimposters

extern float	TREE_MESH_DISTANCE;			// 006321a0: nearer than this a tree is its mesh

// FUNCTION: 0x00477b70 ?RequestShadow@CShadows@@QAEXPAVCUnit@@M@Z
// FUNCTION: 0x00477ba0 ?MagnitudeSq@FVector@@QBEMXZ

//*********************************************************************************
C3DGamut::C3DGamut(int size)	// FUNCTION: 0x004741b0
{
	mCellSize = 8;
	mGridSize = 0x40;
#line 57
	mSpans = new(MEMTYPE_GAMUT) char[0x2000];
	mVisible = new(MEMTYPE_GAMUT) UBYTE[0x1000];

	CONSOLE.RegisterVariable("cg_gamutlocked", "Determines if the gamut gets recalculated as the camera moves", CVar_bool, &gGamutLocked);
	CONSOLE.RegisterVariable("cg_showgamut", "Determines if the gamut is displayed", CVar_bool, &gShowGamut);
	CONSOLE.RegisterVariable("cg_renderimposters", "", CVar_bool, &gRenderImposters);
}

//*********************************************************************************
C3DGamut::~C3DGamut()	// FUNCTION: 0x00474260
{
	SAFE_DELETE_ARRAY(mSpans);
	SAFE_DELETE_ARRAY(mVisible);
}

//*********************************************************************************
// One plane of the view solved for height over the cell grid, z = x * dx + y * dy + z at the grid's first row, through
// its apex pushed out along its normal by half a cell's diagonal. GAMUT_DET is its normal's Z before normalising; a
// vertical plane bounds nothing (no slope, far below). PZA selects the first Z difference in each numerator: the
// fourth plane uses the first plane's already shifted apex there (00474d74, 00474d9f), but its own apex in the
// second differences (00474d83, 00474db1).
#define GAMUT_DET(PA, PB, PC)	((PC.Y - PA.Y) * (PB.X - PA.X) - (PB.Y - PA.Y) * (PC.X - PA.X))
#define GAMUT_SOLVE(PA, PB, PC, PZA, PO, PD, PDX, PDY, PZ)											\
	if (PD != 0)																			\
	{																						\
		PDX = ((PB.Z - PZA) * (PC.Y - PA.Y) - (PB.Y - PA.Y) * (PC.Z - PA.Z)) / PD;			\
		PDY = ((PB.X - PA.X) * (PC.Z - PZA) - (PB.Z - PA.Z) * (PC.X - PA.X)) / PD;			\
		PA += PO;																			\
		PZ = PA.Z - PDX * PA.X - PDY * PA.Y + PDY * mMinY;							\
	}																						\
	else																					\
	{																						\
		PDX = 0;																			\
		PDY = 0;																			\
		PD = 1.f;																			\
		PZ = -99999.f;																		\
	}

// A plane the view lies above raises the bottom of a cell's span; one it lies below lowers the top.
#define GAMUT_LOW(H)	if (H > low) low = H;
#define GAMUT_HIGH(H)	if (H < high) high = H;

// The spans for one combination of the planes' sides: each cell's plane heights, row by row, in 60/127ths of a unit.
#define GAMUT_FILL(TESTS)																	\
	{																						\
		SINT	rows = mMaxY - mMinY + 1;											\
		char	*row = &mSpans[mMinY * mGridSize * 2];							\
		for (; rows; rows--)																\
		{																					\
			fx = (float)mMinX;															\
			h0 = fx * dx0 + z0;																\
			h1 = fx * dx1 + z1;																\
			h2 = fx * dx2 + z2;																\
			h3 = fx * dx3 + z3;																\
			SINT	cols = mMaxX - mMinX + 1;										\
			char	*span = &row[mMinX * 2];											\
			for (; cols; cols--)															\
			{																				\
				float	low = -9999.f;														\
				float	high = 9999.f;														\
				TESTS																		\
				span[1] = Quantise(low);													\
				span[0] = Quantise(high);													\
				span += 2;																	\
				h0 += dx0;																	\
				h1 += dx1;																	\
				h2 += dx2;																	\
				h3 += dx3;																	\
			}																				\
			z0 += dy0;																		\
			z1 += dy1;																		\
			z2 += dy2;																		\
			z3 += dy3;																		\
			row += mGridSize * 2;															\
		}																					\
	}

static inline SINT Max(SINT a, SINT b)
{
	if (a < b)
		return b;
	return a;
}

static inline SINT Min(SINT a, SINT b)
{
	if (a > b)
		return b;
	return a;
}

static inline SINT Clamp(SINT v, SINT lo, SINT hi)
{
	return Min(Max(v, lo), hi);
}

// A height as a span limit: 60/127ths of a unit, clamped to a signed byte.
static inline char Quantise(float h)
{
	return (char)Clamp((SINT)((h * 127.f) / 60.f), -127, 127);
}

//*********************************************************************************
// The view's four side planes, each through the apex and two neighbouring far corners, give each cell in the view's
// bounds the height span the view can see there: the planes the view lies above set the bottom, the rest the top.
// Each normal is (c - a) ^ (b - a), and the offset apex moves along that normal. Retail retains the shifted first
// apex Z (00474b77) for the fourth solve.
// fx and the four heights are the function's, not each fill's: retail's frame gives one fx/h3 slot to d2's class and
// one h2 slot to d1's and the fild temporary's (00474eab..00474ee5), which sixteen block-scoped copies never share.
// The column count is read before the span pointer, so mMinX stays in edx (00474ea5).
// The operand order of the fourth Normalise sum (00474870) and of the a += o terms (00474b67..00474dde) follows C2's
// symbol ids, which count the inline expansions in the fills: the clamp as Min(Max()) under Clamp gives retail's.
void C3DGamut::ComputePlanes(FVector *points)	// FUNCTION: 0x004742a0
{
	FVector	a0 = points[0], b0 = points[2], c0 = points[1];
	FVector	a1 = points[0], b1 = points[3], c1 = points[2];
	FVector	a2 = points[0], b2 = points[4], c2 = points[3];
	FVector	a3 = points[0], b3 = points[1], c3 = points[4];

	FVector	n0 = (c0 - a0) ^ (b0 - a0);
	FVector	n1 = (c1 - a1) ^ (b1 - a1);
	FVector	n2 = (c2 - a2) ^ (b2 - a2);
	FVector	n3 = (c3 - a3) ^ (b3 - a3);
	n0.Normalise();
	n1.Normalise();
	n2.Normalise();
	n3.Normalise();
	FVector	o0 = n0 * 1.41f * 0.5f;
	FVector	o1 = n1 * 1.41f * 0.5f;
	FVector	o2 = n2 * 1.41f * 0.5f;
	FVector	o3 = n3 * 1.41f * 0.5f;

	float	d0 = GAMUT_DET(a0, b0, c0);
	float	d1 = GAMUT_DET(a1, b1, c1);
	float	d2 = GAMUT_DET(a2, b2, c2);
	float	d3 = GAMUT_DET(a3, b3, c3);
	float	dx0, dx1, dx2, dx3;
	float	dy0, dy1, dy2, dy3;
	float	z0, z1, z2, z3;
	float	fx, h0, h1, h2, h3;
	GAMUT_SOLVE(a0, b0, c0, a0.Z, o0, d0, dx0, dy0, z0)
	GAMUT_SOLVE(a1, b1, c1, a1.Z, o1, d1, dx1, dy1, z1)
	GAMUT_SOLVE(a2, b2, c2, a2.Z, o2, d2, dx2, dy2, z2)
	GAMUT_SOLVE(a3, b3, c3, a0.Z, o3, d3, dx3, dy3, z3)

	if ((d0 > 0) && (d1 > 0) && (d2 > 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_LOW(h1) GAMUT_LOW(h2) GAMUT_LOW(h3))
	else if ((d0 > 0) && (d1 > 0) && (d2 > 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_LOW(h1) GAMUT_LOW(h2) GAMUT_HIGH(h3))
	else if ((d0 > 0) && (d1 > 0) && (d2 < 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_LOW(h1) GAMUT_HIGH(h2) GAMUT_LOW(h3))
	else if ((d0 > 0) && (d1 > 0) && (d2 < 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_LOW(h1) GAMUT_HIGH(h2) GAMUT_HIGH(h3))
	else if ((d0 > 0) && (d1 < 0) && (d2 > 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_HIGH(h1) GAMUT_LOW(h2) GAMUT_LOW(h3))
	else if ((d0 > 0) && (d1 < 0) && (d2 > 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_HIGH(h1) GAMUT_LOW(h2) GAMUT_HIGH(h3))
	else if ((d0 > 0) && (d1 < 0) && (d2 < 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_HIGH(h1) GAMUT_HIGH(h2) GAMUT_LOW(h3))
	else if ((d0 > 0) && (d1 < 0) && (d2 < 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_LOW(h0) GAMUT_HIGH(h1) GAMUT_HIGH(h2) GAMUT_HIGH(h3))
	else if ((d0 < 0) && (d1 > 0) && (d2 > 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_LOW(h1) GAMUT_LOW(h2) GAMUT_LOW(h3))
	else if ((d0 < 0) && (d1 > 0) && (d2 > 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_LOW(h1) GAMUT_LOW(h2) GAMUT_HIGH(h3))
	else if ((d0 < 0) && (d1 > 0) && (d2 < 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_LOW(h1) GAMUT_HIGH(h2) GAMUT_LOW(h3))
	else if ((d0 < 0) && (d1 > 0) && (d2 < 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_LOW(h1) GAMUT_HIGH(h2) GAMUT_HIGH(h3))
	else if ((d0 < 0) && (d1 < 0) && (d2 > 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_HIGH(h1) GAMUT_LOW(h2) GAMUT_LOW(h3))
	else if ((d0 < 0) && (d1 < 0) && (d2 > 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_HIGH(h1) GAMUT_LOW(h2) GAMUT_HIGH(h3))
	else if ((d0 < 0) && (d1 < 0) && (d2 < 0) && (d3 > 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_HIGH(h1) GAMUT_HIGH(h2) GAMUT_LOW(h3))
	else if ((d0 < 0) && (d1 < 0) && (d2 < 0) && (d3 < 0))
		GAMUT_FILL(GAMUT_HIGH(h0) GAMUT_HIGH(h1) GAMUT_HIGH(h2) GAMUT_HIGH(h3))
}

//*********************************************************************************
// The view's apex (half a cell back from the camera) and the four corners of its far end; the second and fourth
// corners lie a tenth further out, so no two side planes share three points.
void C3DGamut::MakeIt(FMatrix *view, float far_distance, float zoomx, float zoomy, FVector *pos, bool a)	// FUNCTION: 0x00476a20
{
	if (gGamutLocked)
		return;

	if (pos)
		mCameraPos = *pos;

	float	fx = far_distance * zoomx;
	float	fy = far_distance * zoomy;
	float	nfar = far_distance + 0.1f;
	FVector	points[5];

	points[0].X = pos->X;
	points[0].Y = pos->Y;
	points[0].Z = pos->Z;
	points[0].X -= 4.f;
	points[0].Y -= 4.f;
	points[1] = view->Row[2] * far_distance + points[0] + view->Row[0] * fx + view->Row[1] * fy;
	points[2] = view->Row[2] * nfar + points[0] + view->Row[0] * fx - view->Row[1] * fy;
	points[3] = view->Row[2] * far_distance + points[0] - view->Row[0] * fx - view->Row[1] * fy;
	points[4] = view->Row[2] * nfar + points[0] - view->Row[0] * fx + view->Row[1] * fy;

	SINT	i;
	for (i = 0; i < 5; i++)
	{
		points[i].X = points[i].X / mCellSize;
		points[i].Y = points[i].Y / mCellSize;
	}

	mMinX = mMinY = 12340;
	mMaxY = mMaxX = -12340;
	for (i = 0; i < 5; i++)
	{
		SINT	x = (SINT)points[i].X;
		if (x > mMaxX)
			mMaxX = x;
		if (x < mMinX)
			mMinX = x;

		SINT	y = (SINT)points[i].Y;
		if (y > mMaxY)
			mMaxY = y;
		if (y < mMinY)
			mMinY = y;
	}

	mMaxX += 2;
	if (mMaxX >= mGridSize)
		mMaxX = mGridSize - 1;
	if (mMaxX < 0)
		mMaxX = 0;
	mMinX -= 2;
	if (mMinX < 0)
		mMinX = 0;
	if (mMinX >= mGridSize)
		mMinX = mGridSize - 1;
	mMaxY += 2;
	if (mMaxY >= mGridSize)
		mMaxY = mGridSize - 1;
	if (mMaxY <= 0)
		mMaxY = 0;
	mMinY -= 2;
	if (mMinY < 0)
		mMinY = 0;
	if (mMinY >= mGridSize)
		mMinY = mGridSize - 1;

	ComputePlanes(points);

	SINT	n = 0;
	for (SINT y = 0; y < 64; y++)
	{
		for (SINT x = 0; x < 64; x++)
		{
			if ((x >= mMinX) && (x < mMaxX) && (y >= mMinY) && (y < mMaxY))
			{
				if (mSpans[n * 2] >= mSpans[n * 2 + 1])
					mVisible[n] = 1;
				else
					mVisible[n] = 0;
			}
			else
				mVisible[n] = 0;
			n++;
		}
	}
}

// One big thing's visibility test, shadows and render (source helper). Retail expands it into RenderThings with its
// operators out of line (00477198..004772a0): as a nested body its sites are denied before the sector loop's copy.
static inline void RenderBigThing(CThing *thing, FVector &campos, FVector &right, D3DXMATRIX &wvp, float sx, float sy, bool imposters, bool shadows, bool blob_shadows)
{
		FVector		pos = thing->GetRenderPos();
		CBoundingBox	*box = thing->GetBoundingBox();
		if (box)
			pos += thing->GetOrientation() * box->mOrigin;

		float		dist2 = (pos - campos).MagnitudeSq();
		float		cue = thing->GetCueFactor() / (dist2 + 0.001f);

		if (shadows && (thing->IsA(THING_TYPE_UNIT) || thing->IsA(THING_TYPE_BUILDING)) && (!thing->IsA(THING_TYPE_BATTLE_ENGINE)) && (!thing->GetCanBeStaticallyShadowed()))
			SHADOWS.RequestShadow((CUnit *)thing, 1.0f / cue);

		D3DXVECTOR4	v1, v2;
		D3DXVec3Transform(&v1, (D3DXVECTOR3 *)&pos, &wvp);
		D3DXVec3Transform(&v2, (D3DXVECTOR3 *)&(pos + right * thing->GetRenderRadius()), &wvp);
		v1.x = v1.x / v1.w + 1.0f;
		v1.y = v1.y / v1.w + 1.0f;
		v2.x = v2.x / v2.w + 1.0f;
		v2.y = v2.y / v2.w + 1.0f;
		float		r = v2.x - v1.x;
		float		radius2 = thing->GetRenderRadius();
		radius2 *= radius2;
		if (!((v1.x > -r) && (r + sx > v1.x) && (v1.y > -r) && (r + sy > v1.y)))
		{
			if ((!(dist2 < radius2)) && (!thing->IsA(THING_TYPE_CUTSCENE)))
				return;
		}

		if (blob_shadows && (thing->IsA(THING_TYPE_UNIT | THING_TYPE_STATIONARY) || thing->IsA(THING_TYPE_BUILDING)) && (!thing->IsA(THING_TYPE_BATTLE_ENGINE)) && (!thing->GetCanBeStaticallyShadowed()))
			SHADOWS.AddBlobShadow(thing);
		if (imposters)
			thing->RenderImposter();
		thing->Render();
}

// One map-who thing's visibility test, shadows and render (source helper, as RenderBigThing). Retail expands it
// with every operator inline and their four FVector constructors out of line (00477758..0047789a), which only a
// nested body's shares give. Both bodies call the render interface on the thing itself (004776d3: no null test).
static inline void RenderSectorThing(CThing *thing, FVector &campos, FVector &right, D3DXMATRIX &wvp, float sx, float sy, BOOL partial, bool imposters, bool shadows, bool blob_shadows)
{
		FVector		pos = thing->GetRenderPos();
		CBoundingBox	*box = thing->GetBoundingBox();
		if (box)
			pos += thing->GetOrientation() * box->mOrigin;

		float		dist2 = (pos - campos).MagnitudeSq();
		float		cue = thing->GetCueFactor() / (dist2 + 0.001f);

		if (partial)
		{
			D3DXVECTOR4	v1, v2;
			D3DXVec3Transform(&v1, (D3DXVECTOR3 *)&pos, &wvp);
			D3DXVec3Transform(&v2, (D3DXVECTOR3 *)&(pos + right * thing->GetRenderRadius()), &wvp);
			v1.x = v1.x / v1.w + 1.0f;
			v1.y = v1.y / v1.w + 1.0f;
			v2.x = v2.x / v2.w + 1.0f;
			v2.y = v2.y / v2.w + 1.0f;
			float		r = v2.x - v1.x;
			float		radius2 = thing->GetRenderRadius();
			radius2 *= radius2;
			if (!((v1.x > -r) && (r + sx > v1.x) && (v1.y > -r) && (r + sy > v1.y)))
			{
				if ((!(dist2 < radius2)) && (!thing->IsA(THING_TYPE_CUTSCENE)))
				return;
			}
		}

		if (blob_shadows && (thing->IsA(THING_TYPE_UNIT | THING_TYPE_STATIONARY) || thing->IsA(THING_TYPE_BUILDING)) && (!thing->IsA(THING_TYPE_BATTLE_ENGINE)) && (!thing->GetCanBeStaticallyShadowed()))
			SHADOWS.AddBlobShadow(thing);
		if (imposters)
			thing->RenderImposter();
		if (shadows && (thing->IsA(THING_TYPE_UNIT) || thing->IsA(THING_TYPE_BUILDING)) && (!thing->IsA(THING_TYPE_COMPONENT)) && (!thing->IsA(THING_TYPE_BATTLE_ENGINE)) && (!thing->GetCanBeStaticallyShadowed()))
			SHADOWS.RequestShadow((CUnit *)thing, 1.0f / cue);
		thing->Render();
}

// The camera's right vector: the X column of an orientation, each component copied through a second named float.
// A reconstruction helper for RenderThings' stand-in below (name ours).
static inline FVector ColumnX(const FMatrix &m)
{
	float	z = m.Row[2].X, y = m.Row[1].X, x = m.Row[0].X;
	float	c = z, b = y, a = x;
	return FVector(a, b, c);
}

//*********************************************************************************
// The things in view. First the world's big things, wherever they are. Then every renderable thing the map-who
// holds in the sectors of layers 4 and 3 over the view's cells, when one of the nine cells round a sector is open.
// Where both open and closed cells meet, a thing whose screen extent misses the viewport is drawn only when the
// camera is inside its radius or it belongs to a cutscene. Trees, sorted to the end of a sector's list, are drawn
// all together when the sector's centre is near enough.
// Each row's first node comes from the map-who once and the pointer walks the row (004774e3..00477506, with the
// layer's width hoisted to 004774b0), so x only counts the row down (00477552).
// The entry loop is bottom-tested behind its own guard: retail's latch is je out, jmp back (00477a55), which a
// rotated while does not give (jne back, jmp out).
// The outer loop counts the layer itself down: retail steps layer * 4 and tests 4 - layer (00477b46..00477b55).
// The sector's centre is a vector and the camera's offset from it a vector difference: retail keeps the centre's
// Y in the vector's own slot (0047753e: esp+0x134 of a 16-byte object at esp+0x130), and their two inline sites
// are what leave the big-things helper a share of 169..224, where it calls the matrix product out of line
// (00477198) and expands += and - (0047719d..004771ee).
// The sector's lower corner is taken before its upper one: retail loads sy0 before sx1 (004775c0).
// STAND-IN (a labelled explicit concession, 2026-10-03; listed in the README
// under "Matches and steps that rely on a labelled stand-in"). What: the camera's right vector is read through the
// file-local ColumnX above, whose three components each pass through a second named float (`float c = z, b = y,
// a = x;`). Why: retail's first scheduling region holds three more forwarded-float nodes from this column read (one
// after the load of z, one after y, two after x) than maths.h's GetColumnX gives; without them two stores before the
// big-things loop schedule differently (right.X at 0047703d, the first screen scale at 0047711d): shape 10, 10
// instructions, 45 bytes differ. How: each copy forwards a float definition into a local, which adds the nodes and no
// code. The same body in maths.h's GetColumnX also matches this function but loses 004aede0, 004af110 and 004c5280,
// whose Transpose() needs today's count, so the copy is local, as debugmarker.cpp's ColumnY. Without the second
// floats a file-local copy gives the plain result. Concession: the copies exist only for the compiler; Stuart's
// source for this file and for maths.h is lost, so the original spelling of this read is unknown.
void C3DGamut::RenderThings(bool imposters, bool shadows, bool blob_shadows)	// FUNCTION: 0x00476fe0
{
	FVector		campos = ENGINE.GetCamera()->GetPos();
	FVector		right = ColumnX(ENGINE.GetCamera()->GetOrientation());	// stand-in: see above
	FVector		up = ENGINE.GetCamera()->GetOrientation().GetColumnY();
	FVector		forward = ENGINE.GetCamera()->GetOrientation().GetColumnZ();
	CViewport	vp = ENGINE.GetCurrentViewport();
	D3DXMATRIX	world, view, proj, worldview, wvp;

	RENDERINFO.GetProjection(&proj);
	RENDERINFO.GetWorld(&world);
	RENDERINFO.GetView(&view);
	D3DXMatrixMultiply(&worldview, &view, &world);
	D3DXMatrixMultiply(&wvp, &worldview, &proj);

	float		width = (float)ENGINE.GetCurrentViewportWidth();
	float		sx = width / (vp.Width / 2);
	float		sy = width / (vp.Height / 2);

	LT.mD3DErrorCount = 0;

	CThing		*thing;
	for (thing = WORLD.GetBigThingsNB().First(); thing; thing = WORLD.GetBigThingsNB().Next())
		RenderBigThing(thing, campos, right, wvp, sx, sy, imposters, shadows, blob_shadows);

	for (SINT layer = 4; layer >= 3; layer--)
	{
		SINT	scale = 1 << (4 - layer);
		SINT	width_cells = mGridSize / scale;
		SINT	x0 = mMinX / scale - 1;
		SINT	x1 = mMaxX / scale + 2;
		SINT	y0 = mMinY / scale - 1;
		SINT	y1 = mMaxY / scale + 2;
		if (x0 < 0)
			x0 = 0;
		if (x1 > width_cells)
			x1 = width_cells;
		if (y0 < 0)
			y0 = 0;
		if (y1 > width_cells)
			y1 = width_cells;

		SINT	size = scale * 8;
		float	sector_r2 = (float)(size * size * 2) * 0.5f;

		for (SINT y = y0; y < y1; y++)
		{
			CMapWhoNode	*node = MAP_WHO.GetSector(CSector(x0, y, layer));
			for (SINT x = x0; x < x1; x++, node++)
			{
				float	half = size * 0.5f;
				FVector	centre(x * size + half, y * size + half, 0);
				FVector	d = campos - centre;
				float	d2 = d.X * d.X + d.Y * d.Y - sector_r2;
				bool	visible = false;
				BOOL	partial = FALSE;

				if (!node->mFirst)
					continue;

				SINT	sx0 = (x - 1) * scale;
				SINT	sy0 = (y - 1) * scale;
				SINT	sx1 = (x + 2) * scale;
				SINT	sy1 = (y + 2) * scale;
				if (sx0 < mMinX)
					sx0 = mMinX;
				if (sx1 < mMinX)
					sx1 = mMinX;
				if (sx0 > mMaxX)
					sx0 = mMaxX;
				if (sx1 > mMaxX)
					sx1 = mMaxX;
				if (sy0 < mMinY)
					sy0 = mMinY;
				if (sy1 < mMinY)
					sy1 = mMinY;
				if (sy0 > mMaxY)
					sy0 = mMaxY;
				if (sy1 > mMaxY)
					sy1 = mMaxY;
				if (sy0 >= sy1)
					continue;

				for (SINT gy = sy0; gy < sy1; gy++)
				{
					for (SINT gx = sx0; gx < sx1; gx++)
					{
						char	*span = &mSpans[(gy * mGridSize + gx) * 2];
						if (span[0] >= span[1])
						{
							visible = true;
							if (partial)
							{
								gy = sy1;
								gx = sx1;
							}
						}
						else
							partial = TRUE;
					}
				}
				if (!visible)
					continue;

				CMapWhoEntry	*entry = MAP_WHO.GetFirstEntry(node);
				if (!entry)
					continue;
				do
				{
					thing = entry->GetThing();
					if (thing->IsA(THING_TYPE_RENDERABLE))
					{
						if (thing->IsA(THING_TYPE_TREE))
						{
							float	td = TREE_MESH_DISTANCE + 10.f;
							if (d2 < td * td)
							{
								while (entry)
								{
									CThing	*tree = thing;
									entry = MAP_WHO.GetNextEntry();
									if (entry)
										thing = entry->GetThing();
									tree->Render();
								}
							}
							break;
						}

						RenderSectorThing(thing, campos, right, wvp, sx, sy, partial, imposters, shadows, blob_shadows);
					}
					entry = MAP_WHO.GetNextEntry();
				} while (entry);
			}
		}
	}
}

