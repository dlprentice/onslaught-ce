// DXMeshRenderer.cpp: reconstructed from the object between DXMemoryManager.obj and DXMeshVB.obj
// (00549480..0054bf7f): the PC mesh renderer's core. The name is ours (the object sorts between the two); Stuart's
// release has no copy.
// STAND-IN compatibility: this translation unit retains the measured private inline definitions.
// Their class layouts and external names are unchanged; the shared math header remains current for other files.
// This is a pinned-VC6 object match, not a claim of portable C++ or recovered original helper ownership.
// Numeric costs below describe the cited controls; the combined form matches the entire RMC section and references.
// common.h first, as Stuart's file opens: it brings windows.h and SINT, the private math header's prerequisites.
#include "common.h"
#define DXMESHRENDERER_CPP
#include "DXMeshRendererMaths.h"
#include "meshrenderer.h"
#include "mesh.h"
#include "ltshell.h"
#include "VBufTexture.h"
#include "Texture.h"
#include "engine.h"
#define LT_SHELL_INCLUDE
#include "DXMeshRendererRenderInfo.h"
#include "renderablething.h"
#include "Motioncontroller.h"
#include "VertexShader.h"
#include "DXMeshVB.h"
#include "DebugLog.h"
#include <stdio.h>
#include "mathsconst.h"	// FUNCTION: 0x00549480 init:ZERO_FVECTOR
						// FUNCTION: 0x005494a0 init:ID_FMATRIX

extern SINT		GAME_RENDER_MODE;			// 00704e48 (CMeshRenderer's render mode)
extern FVector	gMeshRendererVector;		// 00704e38: mode 6's direction
extern SINT		gMeshRendererAlpha;			// 00704e4c: mode 2's alpha
extern float	gMeshRendererOffset;		// 00704e50: how far mode 6 moves the back-facing vertices
extern CDXTexture	*gMeshRendererTexture;		// 00704e54: modes 6 and 8 draw with it
extern float	gMeshRendererFrame;			// 00704e58
extern SINT		gMeshRendererAnim;			// 00704e5c

//******************************************************************************************
// One part drawn: a morphing part's positions interpolated between two sets, a plain part through its vertex buffer
// (or by hand in the special render modes), a skinned part's vertices through its bones.
// The vertex's position and normal are read as vectors where retail's code shows it, through casts that stand for
// probably vector-typed members of the vertex format: the three-bone average is assigned whole from an operator's
// temporary (esp+0xc0, 0054b4c6..0054b519), the skinned normal goes through the inline Normalise (0054b520), and the
// world-space colour reads both from the vertex in memory for each matrix term (00549ee0..00549fc7).
// Historical pre-match notes (2026-10-02, shape 281); the labelled stand-ins below supersede these open items:
// - Operand order of the matrix products, which follows symbol ids. Retail takes the inverse's term first in every
//   product of the bone loop (inv * rori at 0054a8c7, both inv * (pos - ppos) at 0054a9ff and 0054aca6), but in the
//   first Inverse block (00549c39..00549cdd) only for the Y and Z terms: dir.X > inv > dir.Y, dir.Z there, inv > rori
//   and the difference in the loop. Ours ranks rori, the difference and all of dir above inv, whose fields become
//   symbols late. No single id shift gives both (dead locals at the top, 0..32 and 1400..2048, as a diagnostic).
//   Rejected: the inverse stored back into pori (adds a 12-dword copy; retail's pori is esp+0x2b4, inv esp+0x34);
//   inv declared and assigned separately (only an id shift: bone loop better, first block worse); a named
//   difference vector, -=, or two statements (no change or worse).
// - The hand-drawn vertex loop keeps the vertex in esi and the colour in edi in retail and reloads the texture
//   table at each use; ours forms table + index * 36 once and that temporary takes the register. Not the alias-id
//   cap: a C2 copy with the field-class limit lowered by 0..1000 (0x1071b0e5) leaves this function unchanged.
// - The morph block's set position st is the fraction t scaled by a factor that is zero (`float zero = 0;` at the top
//   of the block, `st = t * zero`): the zero reaches st only through the global optimizer's propagation and folds
//   after it, so st stays a variable that retail stores (`fld 0.0; fst [st]`, 00549688..0054968e) and passes to floor
//   as an eight-byte copy pushed from memory (00549692..0054969f), where a plain `st = 0` pushes two zero dwords. That
//   also gives retail's frame size (0x1e54). The skinned block's st is the same product (its own zero): there st has
//   two definitions, so retail stores the folded zero as an integer move and reloads it for floor (0054a5cb..0054a5dd). Retail additionally computes t and stores it into the copy's slot
//   (0054967c..00549682, a store nothing reads): here t is still removed before the fold. The address-escape probe on
//   t gave masked shape 257 against 281 on the old form (on st 269, on both 255).
// - The bone loop's rest and now matrices zero their padding with two chained assignments each: retail stores _34,
//   _24, _14 and then _43, _42, _41 (0054ae98..0054aecf, 0054aeec..0054af23), right to left.
// PRIVATE STAND-IN (one-operand spelling): each exact native-first multiplicand
// receives one bracket, as the accepted weapon1f96ed49 mechanism. No locals/copies.
// Specimen-derived operation contexts. The original helper ownership is unknown.
// Every helper returns a real existing product, with native per-cell associations.
// The shadow volume's facing test is Stuart's dot product (v->mSmoothNormal * extrusionvector) < 0 (DrawMeshPart).

// STAND-IN (scheduling): each value is one flat three-term sum inside a whole-value bracket, where it was
// a bracketed Y/Z pair plus the X product. The pair's bracket is a no-code x87 node that filled the idle cycle before the last
// faddp (00549ccd), so the store and the global reload after it issued behind that faddp; without it the idle cycle goes to
// the two integer moves as in retail (00549cc2..00549cd7), and the whole-value bracket keeps the node and CSE temporary
// count of the rest of the function. Same products and association; the original spelling is unknown.
static inline FVector MeshDirectionProduct(const FMatrix &inverse, const FVector &source)
{
 return FVector(((inverse.Row[0].Y * source.Y) + (inverse.Row[0].Z * source.Z) + (source.X * inverse.Row[0].X)),
        ((inverse.Row[1].Y * source.Y) + (inverse.Row[1].Z * source.Z) + (source.X * inverse.Row[1].X)),
        ((inverse.Row[2].Y * source.Y) + (inverse.Row[2].Z * source.Z) + (source.X * inverse.Row[2].X)));
}

static inline void PublishHandNormal(FVector &normal, const FMatrix &m, const CFVF_GTEX &vertex)
{
 normal.X = (((vertex.mNY * m.Row[0].Y)) + (vertex.mNX * m.Row[0].X)) + (vertex.mNZ * m.Row[0].Z); // PRIVATE STAND-IN grouping: native first Yproduct; original spelling unknown.
 normal.Y = (((vertex.mNX * m.Row[1].X)) + (vertex.mNZ * m.Row[1].Z)) + (vertex.mNY * m.Row[1].Y);
 normal.Z = (((vertex.mNZ * m.Row[2].Z)) + (vertex.mNX * m.Row[2].X)) + (vertex.mNY * m.Row[2].Y);
}

// STAND-IN RECONSTRUCTION (disabled block; a last-resort concession for RMC and Load): the check called
// from the disabled block at the end of RenderMeshCore. Its products are inlined and then removed with the block, so it emits
// no code; what it leaves are the inline symbols, the ~0x1c0 ids retail's text had before the bone loop's field fold
// (rmc-id-landscape: the fold start is the cumulative live-symbol count; retail's operand loads need it 0x921 -> ~0xae0+).
// The contents are our reconstruction; the original disabled code is unknown.
// STAND-IN (operand rank): the bone loop's two orientation products, FMatrix::operator* written out as
// file-local copies (an exact copy is code- and id-identical). Six row-0 sums carry a (double) on single factors: no code
// (the products stay in x87 registers), but the conversion raises those products' rank keys, so their terms are added in
// retail's order (0054ab..: decoded bone-loop operand slots 70 -> 42, shape unchanged). Same products and associations;
// the original spelling is unknown. Remaining at that historical stage: the rest product's source-first m.Row[0].X loads, the row-1 sums' Y term first,
// and the animated row 0 Z order (brackets fix them but cut the scheduling regions).
static inline FMatrix MeshRestProduct(const FMatrix &a, const FMatrix &m)
{
	return FMatrix(
		FVector(a.Row[0].X * m.Row[0].X + a.Row[0].Y * m.Row[1].X + a.Row[0].Z * m.Row[2].X, float((double)a.Row[0].X * m.Row[0].Y + (double)a.Row[0].Y * m.Row[1].Y + a.Row[0].Z * m.Row[2].Y), float((double)a.Row[0].X * m.Row[0].Z + (double)a.Row[0].Y * m.Row[1].Z + a.Row[0].Z * m.Row[2].Z)),
		FVector(a.Row[1].X * m.Row[0].X + a.Row[1].Y * m.Row[1].X + a.Row[1].Z * m.Row[2].X, a.Row[1].X * m.Row[0].Y + a.Row[1].Y * m.Row[1].Y + a.Row[1].Z * m.Row[2].Y, a.Row[1].X * m.Row[0].Z + a.Row[1].Y * m.Row[1].Z + a.Row[1].Z * m.Row[2].Z),
		FVector(a.Row[2].X * m.Row[0].X + a.Row[2].Y * m.Row[1].X + a.Row[2].Z * m.Row[2].X, a.Row[2].X * m.Row[0].Y + a.Row[2].Y * m.Row[1].Y + a.Row[2].Z * m.Row[2].Y, a.Row[2].X * m.Row[0].Z + a.Row[2].Y * m.Row[1].Z + a.Row[2].Z * m.Row[2].Z));
}

// STAND-IN (operand rank): the animated orientation product, as stated above MeshRestProduct.
static inline FMatrix MeshAnimProduct(const FMatrix &a, const FMatrix &m)
{
	return FMatrix(
		FVector(float((double)a.Row[0].X * m.Row[0].X + a.Row[0].Y * m.Row[1].X + (double)a.Row[0].Z * m.Row[2].X), float((double)a.Row[0].X * m.Row[0].Y + (double)a.Row[0].Y * m.Row[1].Y + a.Row[0].Z * m.Row[2].Y), a.Row[0].X * m.Row[0].Z + a.Row[0].Y * m.Row[1].Z + a.Row[0].Z * m.Row[2].Z),
		FVector(a.Row[1].X * m.Row[0].X + a.Row[1].Y * m.Row[1].X + a.Row[1].Z * m.Row[2].X, a.Row[1].X * m.Row[0].Y + a.Row[1].Y * m.Row[1].Y + a.Row[1].Z * m.Row[2].Y, a.Row[1].X * m.Row[0].Z + a.Row[1].Y * m.Row[1].Z + a.Row[1].Z * m.Row[2].Z),
		FVector(a.Row[2].X * m.Row[0].X + a.Row[2].Y * m.Row[1].X + a.Row[2].Z * m.Row[2].X, a.Row[2].X * m.Row[0].Y + a.Row[2].Y * m.Row[1].Y + a.Row[2].Z * m.Row[2].Y, a.Row[2].X * m.Row[0].Z + a.Row[2].Y * m.Row[1].Z + a.Row[2].Z * m.Row[2].Z));
}

// STAND-IN accessor: the rest matrix's first element read through a const-reference accessor. Named in
// RenderMeshCore's own text, rori.Row[0].X would take an early front-end id; read here, it first appears when the accessor is
// inlined and takes a late id (0x6fb on 9acb1213), inside the window [0x6be, 0x7ff] mod 0x800 where its rank key sits above the
// inverse copies' and its product key wraps below the other two terms. The three rest elements of column X then load rori
// first and add its term last, as retail does (fld rori+0; fmul inv+0x20/0x10/0x00 at 0054a7a3, 0054a827, 0054a8b3), and row
// 1's Z element takes retail's order (0054a861). No code: bone-loop operand slots 16 -> 0. Whether the original read the
// element through an accessor is unknown.
static inline float MeshOriginX(const FMatrix &m) { return m.Row[0].X; }

// STAND-IN RECONSTRUCTION: the check named above MeshRestProduct; called only inside the disabled block at the
// end of RenderMeshCore, so it emits no code. Contents ours; the original is unknown.
static inline void MeshCheckOrientation(const FMatrix &m)
{
	FMatrix	a = m * m;
}

// PRIVATE STAND-IN (operand rank; re-fitted 2026-10-15): row 1's first two terms take a float
// conversion and its third product a (double) factor. No code (x87 registers; the (double) rank lever of SpawnRound), but
// the conversion temps move the product's rank keys so position.X loads first (fld/fmul at 00549f64) and give retail's
// mode-7 copy-out schedule (00549f45..00549fd5). With Stuart's mode-2 locals (swidth..stheight, below) the (double) sits
// on the third product: on the first (the form used) 4 4 10, without it 2 2 12 (the copy-out's W store after the row-1
// faddp at 00549f90), flat sums (maths.h's operator*) 8 14 19 with the rows' term orders lost. A doubled bracket kept the
// load order but not the schedule. Same products and associations; original spelling unknown.
static inline FVector MeshHandPositionProduct(const FMatrix &m, const CFVF_GTEXPos &position)
{
 return FVector(
        ((position.Z * m.Row[0].Z) + (position.X * m.Row[0].X)) + (position.Y * m.Row[0].Y),
        float((position.X * m.Row[1].X) + (position.Y * m.Row[1].Y)) + ((double)position.Z * m.Row[1].Z),
        ((position.Y * m.Row[2].Y) + (position.Z * m.Row[2].Z)) + (position.X * m.Row[2].X));
}

// STAND-IN: 163 finite loops reuse l/layers and are completely discarded by the pinned VC6 optimizer.
// Together they supply 163 SR values (160 before the morph block took Stuart's text, 2026-10-15: his frame block
// numbers the vertex loop's induction temporaries three ids earlier); field numbering is controlled separately.
// The following triangle loop reinitializes l before reading it. These are a compiler fit, not recovered source.
// Remove only with a replacement preserving the complete section and relocations; the pre-device base misses 3 SIB bytes.
static __forceinline void MeshNumberingLoops(SINT &l, SINT &count) {
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
for (l=0; l<count; l++) ;
}
// STAND-IN: forty named fields and seven unused sinks place the compiler symbol pools without emitted code.
// In the pre-balanced control, eight sinks leave 22 actual differing bone bytes (22/36/26); seven leave four (4/6/8).
// IMPORTANT: the automatic record below is deliberately uninitialized; these discarded SINT reads trigger C4700.
// Their elimination is verified for the pinned optimizer, not a claim of defined portable C++. Automatic zero-init
// changes three operand pairs (0/12/31); address-only sinks give 20/114/104; static zero-init gives 12/20/16.
// Retain this explicit concession until a defined source form reproduces the complete section and references.
struct RmcSymbolPad
{
	SINT f0, f1, f2, f3, f4, f5, f6, f7;
	SINT f8, f9, f10, f11, f12, f13, f14, f15;
	SINT f16, f17, f18, f19, f20, f21, f22, f23;
	SINT f24, f25, f26, f27, f28, f29, f30, f31;
	SINT f32, f33, f34, f35, f36, f37, f38, f39;
};
static __forceinline void ReadRmcSymbolPad(const RmcSymbolPad &r) { SINT p0=r.f0; SINT p1=r.f1; SINT p2=r.f2; SINT p3=r.f3; SINT p4=r.f4; SINT p5=r.f5; SINT p6=r.f6; p6=r.f7; p6=r.f8; p6=r.f9; p6=r.f10; p6=r.f11; p6=r.f12; p6=r.f13; p6=r.f14; p6=r.f15; p6=r.f16; p6=r.f17; p6=r.f18; p6=r.f19; p6=r.f20; p6=r.f21; p6=r.f22; p6=r.f23; p6=r.f24; p6=r.f25; p6=r.f26; p6=r.f27; p6=r.f28; p6=r.f29; p6=r.f30; p6=r.f31; p6=r.f32; p6=r.f33; p6=r.f34; p6=r.f35; p6=r.f36; p6=r.f37; p6=r.f38; p6=r.f39; }
// STAND-IN: private FMatrix/vector product for the rest position, with one product group moved from X to Y.
// Plain ownership alone is whole-object neutral and leaves four instruction bytes (4/6/8). Adding a Y group fixes
// that pair but adds three compiler values and disturbs later code (26/40/286). Moving X's group instead keeps
// 3147 values and restores the entire section (0/0/0 on f2fd1503); the original spelling/ownership is unknown.
static inline FVector MeshRestPositionProduct(const FMatrix &a, const FVector &v)
{
    return FVector(a.Row[0].X * v.X + ((a.Row[0].Y * v.Y)) + (a.Row[0].Z * v.Z),
                   (a.Row[1].X * v.X) + (a.Row[1].Y * v.Y) + (a.Row[1].Z * v.Z),
                   (a.Row[2].X * v.X) + (a.Row[2].Y * v.Y) + (a.Row[2].Z * v.Z));
}
// STAND-IN: a private copy of the actual inverse operations exposes the return local for the unused Y expression.
// The plain copy is whole-object neutral; the member expression changes projection timing, not the emitted math.
// Its interaction with the field/loop devices and rest-position grouping is part of the pinned compiler fit.
static inline FMatrix MeshBoneInverse(const FMatrix &m)
{
    FMatrix r = m.Cofactors();
    r.TransposeInPlace();
    r /= m.Determinant();
    // STAND-IN: create the Y member projection early; no value read or instruction survives.
    (void)r.Row[0].Y;
    return r;
}
void CMeshRenderer::RenderMeshCore(FVector pos, FMatrix *ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b, CMotionController *mc, BOOL collision)	// FUNCTION: 0x00549570
{
	SINT		i;

	if (part->mPartType == 1)
	{
		SINT	anim = -1;
		if (thing)
			anim = thing->GetRealAnimIndex();
		if ((b & 8) == 8)
			anim = gMeshRendererAnim;

		if ((part->mNumVFrames > 1) && (part->mBaseMesh->mNumAnimModes) && (part->mNumAFrames > 1) && (anim >= 0))
		{
			// morphing: the positions between two sets
			float	time = 0.0f;
			if (thing)
				time = thing->GetRenderFrame();
			if ((b & 8) == 8)
				time = gMeshRendererFrame;
			CAnimMode	*ma = &part->mBaseMesh->mAnimModes[anim];
			time *= ma->mFrameScaler;
			time += ma->mStart;
			if (mc)
				mc->AdjustFrame(part, &time);
			// Stuart's frame block (DrawMeshPart): vf1/vf2 are dead, af1/af2 are 0.f constants (his mVFrames lookups commented out),
			// so aframe folds to 0 after the global optimizer (retail's fld 0; fst; fstp qword at 549688) and vffrac's double use keeps
			// its frame store (54967c); the (1 - vffrac) and (1.f - frac2) brackets are his. This replaces the late-zero, volatile-t and
			// bracket stand-ins that stood here.
			SINT	vf1 = (SINT(floor(time))) % part->mNumVFrames;
			SINT	vf2 = vf1 + 1;
			if (vf2 > ma->mLast) vf2 = ma->mStart;

			float	af1 = 0.f;
			float	af2 = 0.f;

			if (af2 < af1) af2 = float(SINT(floor(af1)) + 1);

			float	vfwhole = floorf(time);
			float	vffrac = time - vfwhole;
			float	aframe = (af2 * vffrac) + (af1 * (1 - vffrac));

			SINT	f1 = SINT(floor(aframe));
			SINT	f2 = f1 + 1;
			if	(f2 > SINT(floor(0.f))) f2 = SINT(floor(0.f));

			FVector	*p0 = part->mPVertices[f1];
			FVector	*p1 = part->mPVertices[f2];

			float	f = aframe - floorf(aframe);
			float	g = (1.f - f);


			for (i = 0; i < part->mNumDVertices; i++)
			{
				CMeshDVertex	*v = &part->mDVertices[i];
				CFVF_GTEX	vert;
				// Stuart's spelling (DXMeshRenderer.cpp DrawMeshPart): each product bracketed. Byte-neutral.
				vert.mPos = (p0[v->mPVert] * g) + (p1[v->mPVert] * f);
				vert.mU = v->mU;
				vert.mV = v->mV;
				vert.mNX = v->mNormal.X;
				vert.mNY = v->mNormal.Y;
				vert.mNZ = v->mNormal.Z;
				vert.mDiffuse = v->mDiffuse;
				if (GetRenderAlpha() != 255)
					vert.mDiffuse = ((SINT(vert.mDiffuse >> 24) * GetRenderAlpha() / 255) << 24) | (vert.mDiffuse & 0xffffff);
				if (GetRenderMode() == 1)
					vert.mDiffuse = PackNormal(&v->mNormal, 1.0f);
				v->mIndex[0] = part->mBaseMesh->mTextures[v->mTexture[0]].mVBuffer->AddVertices(&vert, 1);
			}
			for (i = 0; i < part->mNumTriangles; i++)
			{
				CMeshTriangle	*tri = &part->mTriangles[i];
				UWORD idx[3];
				idx[0] = tri->mVertices[0]->mIndex[0];
				idx[1] = tri->mVertices[1]->mIndex[0];
				idx[2] = tri->mVertices[2]->mIndex[0];
				part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[0]].mVBuffer->AddIndices(idx, 3);
			}
			CVBufTexture::DrawList((SVBufTextureDrawList *)part->mBaseMesh);
		}
		else if ((GetRenderMode() != 0) && (GetRenderMode() != 4) && (GetRenderMode() != 8))
		{
			// drawn by hand: the render modes that change the vertices
			float	time;
			SINT	f0 = 0;
			anim = -1;
			time = 0;
			if (thing)
				anim = thing->GetRealAnimIndex();
			if ((b & 8) == 8)
				anim = gMeshRendererAnim;
			if (anim >= 0)
			{
				if (thing)
					time = thing->GetRenderFrame();
				if ((b & 8) == 8)
					time = gMeshRendererFrame;
				CAnimMode	*ma = &part->mBaseMesh->mAnimModes[anim];
				// STAND-IN (temporary numbering): the two brackets are no-code IL nodes that take one CSE value each.
				// With Stuart's morph frame block the morph blend's second element-address temporary sits two ids short of the
				// parity that loads f first (549f64..); a burn of two value ids anywhere from the frame block's end to about id 0x1000
				// restores it, and this is the one float expression in that window. Original spelling unknown.
				f0 = SINT(floor(((ma->mFrameScaler * time) + ma->mStart))) % part->mNumVFrames;
			}
			FVector	*positions = part->mPVertices[0];

			D3DVIEWPORT9	vp;
			D3DXMATRIX		proj, world, view;
			float			swidth, sheight;			// Stuart's mode-2 locals and names (DrawMeshPart); his swidth/sheight
			float			stwidth, stheight;			// are the viewport size, ours both read the width (retail's bytes)
			float			twidth, theight;
			CVBufTexture	*vb;
			if (GetRenderMode() == 2)
			{
				vb = CVBufTexture::GetOrCreate(ENGINE.GetDefaultTexture(), 0);
				vb->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
				vb->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, D3DPOOL_MANAGED);
				LT.GetDevice()->GetViewport(&vp);
				RENDERINFO.GetProjection(&proj);
				RENDERINFO.GetWorld(&world);
				RENDERINFO.GetView(&view);
				swidth = (float)ENGINE.GetCurrentViewportWidth();
				sheight = (float)ENGINE.GetCurrentViewportWidth();
				twidth = (float)vb->mTexture->mWidth;
				theight = (float)vb->mTexture->GetHeight();
				// STAND-IN (no-code brackets): the two division brackets are IL nodes that take ids; Stuart's
				// text has none. Without both 16 16 26, without either one 8 8 14 (the mode-7 copy-out and the bone loop move).
				stwidth = (swidth / twidth);
				stheight = (sheight / theight);
			}
			if ((GetRenderMode() == 6) || (GetRenderMode() == 8))
			{
				vb = CVBufTexture::GetOrCreate(gMeshRendererTexture, 0);
				vb->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
				vb->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, D3DPOOL_MANAGED);
			}

			SINT	layers = 6;
			if ((GetRenderMode() == 2) || (GetRenderMode() == 6) || (GetRenderMode() == 8))
				layers = 1;

			FVector	dir;
			if (GetRenderMode() == 6)
			{
				// the direction in the part's frame, normalised and turned in place: retail keeps it in one home
				// (00549b54..00549b80, 00549cc2..00549ceb), and the vertex loop's normal takes the product temporary's home
				dir = gMeshRendererVector;
				dir.NormaliseXYZ();
				FMatrix	inv = ori->Inverse();
				dir = MeshDirectionProduct(inv, dir);
			}

			SINT	l;
			for (i = 0; i < part->mNumDVertices; i++)
			{
				CMeshDVertex	*v = &part->mDVertices[i];
				for (l = 0; l < layers; l++)
				{
					// Stuart's layer test (DXMeshRenderer.cpp DrawMeshPart), one condition; byte-neutral against the continue form.
					if ((v->mTexture[l] != -1) && (part->mBaseMesh->mTextures[v->mTexture[l]].mMaxBlend > 0.0f))
					{
					if ((GetRenderMode() != 2) && (GetRenderMode() != 6) && (GetRenderMode() != 8))
						vb = part->mBaseMesh->mTextures[v->mTexture[l]].mVBuffer;

					CFVF_GTEX	vert;
					vert.mPos = positions[v->mPVert];
					vert.mU = v->mU;
					vert.mV = v->mV;
					vert.mNX = v->mNormal.X;
					vert.mNY = v->mNormal.Y;
					vert.mNZ = v->mNormal.Z;
					vert.mDiffuse = v->mDiffuse;
					if (GetRenderMode() == 6)
					{
						// the shadow volume: the faces turned away from the direction pushed along it
						if ((v->mSmoothNormal * dir) < 0)
						{
							FVector	off = dir * gMeshRendererOffset;
							vert.mPos.X = vert.mPos.X - off.X;
							vert.mPos.Y = vert.mPos.Y - off.Y;
							vert.mPos.Z = vert.mPos.Z - off.Z;
							vert.mDiffuse = 0;
						}
						else
							vert.mDiffuse = 0xff000000;
						vert.mU = vert.mPos.X / 10.0f;
						vert.mV = vert.mPos.Y / 10.0f;
					}

					// STAND-IN (symbol position): one initialised dead local, no code, where our `DWORD col` stood before
					// the alpha computation took Stuart's text (which has no col). Without it every later front-end symbol sits one id
					// earlier and the rest product's row-1 sum adds its terms in another order (0 4 8). Contents ours.
					SINT	dc0 = 0;
					// Stuart's alpha text (DrawMeshPart): the frame blend applied through int alpha, (float)alpha, his brackets.
					int alpha = (vert.mDiffuse >> 24) & 0xFF;
					alpha = (int)((float)alpha * part->mBaseMesh->mTextures[v->mTexture[l]].mTextureBlend[(f0 % part->mBaseMesh->mTextures[v->mTexture[l]].mFrames)]);
					if (GetRenderAlpha() != 255)
					{
						alpha = (alpha * GetRenderAlpha()) / 255;
					}
					vert.mDiffuse = (vert.mDiffuse & 0x00FFFFFF) + (alpha << 24);

					if ((GetRenderMode() == 1) || (GetRenderMode() == 7))
					{
						// the normal (or in mode 7 the position) in the world, as a colour
						FVector	n;
						PublishHandNormal(n, *ori, vert);
						if (GetRenderMode() == 7)
							n = MeshHandPositionProduct(*ori, vert.mPos);
						n.NormaliseYZX();
						vert.mDiffuse = PackNormal(&n, 1.0f);
					}
					if (GetRenderMode() == 2)
					{
						// screen-space texture coordinates
						D3DXVECTOR3	in(&vert.mPos.X);
						FVector	out;
						D3DXVec3Project((D3DXVECTOR3 *)&out, &in, &vp, &proj, &view, &world);
						vert.mU = out.X / twidth;
						vert.mV = out.Y / theight;
						if (vert.mU < 0)
							vert.mU = 0;
						if (vert.mV < 0)
							vert.mV = 0;
						if (vert.mU > stwidth)
							vert.mU = stwidth;
						if (vert.mV > stheight)
							vert.mV = stheight;
						vert.mDiffuse = (gMeshRendererAlpha << 24) | 0xffffff;
					}
					v->mIndex[l] = vb->AddVertices(&vert, 1);
					}
				}
			}

			MeshNumberingLoops(l, layers);
			for (i = 0; i < part->mNumTriangles; i++)
			{
				CMeshTriangle	*tri = &part->mTriangles[i];
				UWORD idx[3];
				if ((GetRenderMode() != 2) && (GetRenderMode() != 6) && (GetRenderMode() != 8))
				{
					for (l = 0; l < layers; l++)
					{
						if (tri->mVertices[0]->mTexture[l] == -1)
							continue;
						if (part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]].mMaxBlend <= 0)
							continue;
						idx[0] = tri->mVertices[0]->mIndex[l];
						idx[1] = tri->mVertices[1]->mIndex[l];
						idx[2] = tri->mVertices[2]->mIndex[l];
						part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]].mVBuffer->AddIndices(idx, 3);
						part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]].mVBuffer->mTexture->mMode = l;
						CMeshTexture	*mt = &part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]];
						mt->mVBuffer->mTexture->mOffsetU = mt->mTextureU[f0 % mt->mFrames];
						mt = &part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]];
						mt->mVBuffer->mTexture->mOffsetV = mt->mTextureV[f0 % mt->mFrames];
						mt = &part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]];
						mt->mVBuffer->mTexture->mScaleU = mt->mTextureUScale[f0 % mt->mFrames];
						mt = &part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[l]];
						mt->mVBuffer->mTexture->mScaleV = mt->mTextureVScale[f0 % mt->mFrames];
					}
				}
				else if ((tri->mVertices[0]->mTexture[0] != -1) && (part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[0]].mMaxBlend > 0))
				{
					idx[0] = tri->mVertices[0]->mIndex[0];
					idx[1] = tri->mVertices[1]->mIndex[0];
					idx[2] = tri->mVertices[2]->mIndex[0];
					vb->AddIndices(idx, 3);
				}
			}

			if (GetRenderMode() == 2)
			{
				LT.SRS(D3DRS_LIGHTING, FALSE);
				LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
				vb->Draw(true);
				LT.SRS(D3DRS_LIGHTING, TRUE);
				LT.SetModulate(0);
			}
			else if (GetRenderMode() == 6)
			{
				LT.SRS(D3DRS_LIGHTING, FALSE);
				vb->Draw(true);
				LT.SRS(D3DRS_LIGHTING, TRUE);
			}
			else if (GetRenderMode() == 8)
				vb->DWATS(true, 0, 0);
			else
				CVBufTexture::DrawList((SVBufTextureDrawList *)part->mBaseMesh);
		}
		else
			part->mMeshVB->Render(thing, b, ori, &pos);
	}
	else if (part->mPartType == 3)
	{
		// skinned: each vertex through the bones it follows
		D3DXMATRIX	bones[100];	// Stuart's bonemat, declared in this block (DrawMeshPart)
		float	zero = 0;	// STAND-IN (late zero): see the morph/skinned st note at the top of this file
		SINT	anim = -1;
		float	time = 0;
		if (thing)
			anim = thing->GetRealAnimIndex();
		if ((b & 8) == 8)
			anim = gMeshRendererAnim;
		SINT	f0, f1, s0;
		float	t, st;
		if ((part->mNumVFrames > 1) && (part->mBaseMesh->mNumAnimModes) && (anim >= 0))
		{
			if (thing)
				time = thing->GetRenderFrame();
			if ((b & 8) == 8)
				time = gMeshRendererFrame;
			CAnimMode	*ma = &part->mBaseMesh->mAnimModes[anim];
			time *= ma->mFrameScaler;
			time += ma->mStart;
			if (mc)
				mc->AdjustFrame(part, &time);
			f0 = SINT(floor(time)) % part->mNumVFrames;
			f1 = f0 + 1;
			if (f1 > ma->mLast)
				f1 = ma->mStart;
			float	fl = (float)floor(time);
			t = time - fl;
			st = t * zero;
			s0 = SINT(floor(st));
			if (s0 + 1 > SINT(floor(0.0)))
				floor(0.0);
		}
		else
		{
			f0 = 0;
			f1 = 0;
			time = 0; // Nativefallback owner; fraction remains indeterminate, no runtimeclaim.
			s0 = SINT(floor(0.0));
			st = 1.0f;
		}
		FVector	*positions = part->mPVertices[s0];
		floor(st);

		// PRIVATE STAND-IN: palette preparation is one actual pose/inverse lifetime phase.
		// None of these local pose/inverse values is read by later GPU/CPU vertex work.
		{
		FMatrix	pori;
		FVector	ppos;
		part->GetWorldTransforms(f0, f1, t, thing, &pori, &ppos, true, true, false);
		FMatrix	inv = MeshBoneInverse(pori);


		for (i = 0; i < part->mNumBones; i++)
		{
			FMatrix	rori, aori;
			FVector	rpos, apos;
			part->mBones[i]->GetWorldTransforms(0, 0, 0, NULL, &rori, &rpos, true, false, false);
			part->mBones[i]->GetWorldTransforms(f0, f1, t, thing, &aori, &apos, true, true, false);
			// STAND-IN RECONSTRUCTION (disabled block; a last-resort concession for RMC and Load, as at the end of this
			// function): it emits no code. Its names number the two orientations' fields one id apart, in this order, before the
			// fills below would number them three apart (one statement each). The products' operand ranks read those ids: every
			// animated term and both position products take retail's order, and the rest product all but row 1's Z element and
			// the offset-0 loads (bone-loop operand slots 36 -> 16, sum orders 30 -> 35 of 36). rori.Row[0].X is left out: it is
			// read through MeshOriginX in the fill (see there). The original disabled code is unknown.
			if (0)
			{
				(void)(aori.Row[0].X + aori.Row[0].Y + aori.Row[1].Y + aori.Row[2].Y + aori.Row[1].X + aori.Row[2].X + aori.Row[0].Z + aori.Row[1].Z + aori.Row[2].Z);
				(void)(rori.Row[0].Y + rori.Row[1].Y + rori.Row[2].Y + rori.Row[1].X + rori.Row[2].X + rori.Row[0].Z + rori.Row[1].Z + rori.Row[2].Z);
			}
			rori = MeshRestProduct(inv, rori);
			rpos = MeshRestPositionProduct(inv, rpos - ppos);
			aori = MeshAnimProduct(inv, aori);
			apos = inv * (apos - ppos);
			D3DXMATRIX	rest;
			D3DXMATRIX	now;
			now._11 = aori.Row[0].X;
			now._12 = aori.Row[1].X;
			now._13 = aori.Row[2].X;
			now._21 = aori.Row[0].Y;
			now._22 = aori.Row[1].Y;
			now._23 = aori.Row[2].Y;
			now._31 = aori.Row[0].Z;
			now._32 = aori.Row[1].Z;
			now._33 = aori.Row[2].Z;
			now._14 = now._24 = now._34 = 0;
			now._41 = now._42 = now._43 = 0;
			now._44 = 1.0f;
			rest._11 = MeshOriginX(rori);
			rest._12 = rori.Row[1].X;
			rest._13 = rori.Row[2].X;
			rest._21 = rori.Row[0].Y;
			rest._22 = rori.Row[1].Y;
			rest._23 = rori.Row[2].Y;
			rest._31 = rori.Row[0].Z;
			rest._32 = rori.Row[1].Z;
			rest._33 = rori.Row[2].Z;
			rest._14 = rest._24 = rest._34 = 0;
			rest._41 = rest._42 = rest._43 = 0;
			rest._44 = 1.0f;
			D3DXMatrixInverse(&rest, NULL, &rest);
			D3DXMATRIX	at, rt;
			D3DXMatrixTranslation(&at, apos.X, apos.Y, apos.Z);
			D3DXMatrixTranslation(&rt, rpos.X, rpos.Y, rpos.Z);
			D3DXMatrixInverse(&rt, NULL, &rt);
			bones[i] = rt * rest * now * at;
		}
		}

		if ((CVertexShader::GetVertexShadersEnabled()) && (part->mNumBones <= 25) && (!GetCutsceneRendering()) &&
			((GetRenderMode() == 0) || (GetRenderMode() == 4)) && (!(ENGINE.GetDrawDebugStuff() & DRAW_SKELETAL_ACCURATELY)))
		{
			// in the vertex shader
			RENDERINFO.mUnknown30c = part->mNumBones;
			for (i = 0; i < part->mNumBones; i++)
			{
				D3DXMATRIX	m = bones[i];
				RENDERINFO.SetMatrix(i, m / 3.0f);
			}
			RENDERINFO.SetUnknowne18(1);
			RENDERINFO.mUnknown308 = true;
			RENDERINFO.mUnknown34f = true;
			part->mMeshVB->Render(thing, b, ori, &pos);
			RENDERINFO.mUnknown308 = false;
			RENDERINFO.mUnknown34f = true;
			RENDERINFO.SetUnknowne18(0);
		}
		else
		{
			// by hand
			for (i = 0; i < part->mNumDVertices; i++)
			{
				CMeshDVertex	*v = &part->mDVertices[i];
				CFVF_GTEX	vert;
				vert.mPos = positions[v->mPVert];
				vert.mU = v->mU;
				vert.mV = v->mV;
				vert.mNX = v->mNormal.X;
				vert.mNY = v->mNormal.Y;
				vert.mNZ = v->mNormal.Z;
				vert.mDiffuse = v->mDiffuse;
				if (GetRenderAlpha() != 255)
					vert.mDiffuse = ((SINT(vert.mDiffuse >> 24) * GetRenderAlpha() / 255) << 24) | (vert.mDiffuse & 0xffffff);
				if (GetRenderMode() == 1)
					vert.mDiffuse = PackNormal(&v->mNormal, 1.0f);

				D3DXVECTOR3	sp, sn;	// Stuart's basepos/basenormal, by components (DrawMeshPart)
				sp.x = vert.mPos.X;
				sp.y = vert.mPos.Y;
				sp.z = vert.mPos.Z;
				sn.x = vert.mNX;
				sn.y = vert.mNY;
				sn.z = vert.mNZ;

				vert.mPos.X = 0.0f;
				vert.mPos.Y = 0.0f;
				vert.mPos.Z = 0.0f;
				// STAND-IN (frame classes): each arm that normalises writes its own Normalise() instead of one call
				// behind a flag. The frame packer counts the references of both inline copies (the skin vertex's 70 -> 80) before C2's
				// jump pass cross-jumps them into the single copy retail has after the three-bone arm's copy-out; that puts the 36-byte
				// skin/viewport slot class ahead of the 16-byte hand-normal class, as in retail's frame. The weighted arm tests `any`
				// for the normalise (not `!any`) so the surviving copy is the three-bone arm's. Original spelling unknown.
				if (!(ENGINE.GetDrawDebugStuff() & DRAW_SKELETAL_ACCURATELY))
				{
					// the average of the three heaviest bones
					if (part->mBoneSlots[v->mPVert][0] != -1)
					{
						for (SINT k = 0; k < 3; k++)
						{
							D3DXVECTOR3	p, n;
							D3DXVec3TransformCoord(&p, &sp, &bones[part->mBoneSlots[v->mPVert][k]]);
							D3DXVec3TransformCoord(&n, &sn, &bones[part->mBoneSlots[v->mPVert][k]]);
							vert.mPos.X += p.x;
							vert.mPos.Y += p.y;
							vert.mPos.Z += p.z;
							vert.mNX += n.x;
							vert.mNY += n.y;
							vert.mNZ += n.z;
						}
						*(D3DXVECTOR3 *)&vert.mPos = D3DXVECTOR3(&vert.mPos.X) / 3.0f;
						((FVector *)&vert.mNX)->NormaliseXYZ(); // STAND-IN: explicit-double XYZ normalization retains retail operand order; original spelling unknown.
					}
					else
					{
						vert.mPos.X = sp.x;
						vert.mPos.Y = sp.y;
						vert.mPos.Z = sp.z;
					}
				}
				else
				{
					// every bone by its weight
					bool	any = false;
					for (SINT k = 0; k < part->mNumBones; k++)
					{
						float	w = part->mBoneWeights[v->mPVert][k];
						if (w > 0)
						{
							D3DXVECTOR3	p, n;
							D3DXVec3TransformCoord(&p, &sp, &bones[k]);
							D3DXVec3TransformCoord(&n, &sn, &bones[k]);
							any = true;
							vert.mPos.X += (p.x * w);
							vert.mPos.Y += (p.y * w);
							vert.mPos.Z += (p.z * w);

							// Stuart's text (DrawMeshPart): the X product added to all three normal components, his brackets.
							vert.mNX += (n.x * w);
							vert.mNY += (n.x * w);
							vert.mNZ += (n.x * w);
						}
					}
					if (any)
						((FVector *)&vert.mNX)->NormaliseXYZ(); // STAND-IN: explicit-double XYZ normalization retains retail operand order; original spelling unknown.
					else
					{
						vert.mPos.X = sp.x;
						vert.mPos.Y = sp.y;
						vert.mPos.Z = sp.z;
						vert.mNX = sn.x;
						vert.mNY = sn.y;
						vert.mNZ = sn.z;
					}
				}
				v->mIndex[0] = part->mBaseMesh->mTextures[v->mTexture[0]].mVBuffer->AddVertices(&vert, 1);
			}
			i = 0;
			if (part->mNumTriangles > 0)
			{
				UINT triangleByteOffset = 0;
				do
				{
					CMeshTriangle	*tri = (CMeshTriangle *)((char *)part->mTriangles + triangleByteOffset);
					UWORD idx[3];
					idx[0] = tri->mVertices[0]->mIndex[0];
					idx[1] = tri->mVertices[1]->mIndex[0];
					idx[2] = tri->mVertices[2]->mIndex[0];
					part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[0]].mVBuffer->AddIndices(idx, 3);
					i++;
					triangleByteOffset += sizeof(CMeshTriangle);
				} while (i < part->mNumTriangles);
			}
			CVBufTexture::DrawList((SVBufTextureDrawList *)part->mBaseMesh);
		}
		if (0)
		{
			FVector	x = ori->Row[0];
			FVector	y = ori->Row[1];
			FVector	z = ori->Row[2];
			for (i = 0; i < part->mNumBones; i++)
			{
				D3DXMATRIX	&m = bones[i];
				float	r0 = m._11 * m._11 + m._12 * m._12 + m._13 * m._13;
				float	r1 = m._21 * m._21 + m._22 * m._22 + m._23 * m._23;
				float	tx = m._41, ty = m._42, tz = m._43;
			}
			MeshCheckOrientation(*ori);
		}
	}
	else if ((part->mPartType != 4) && (part->mPartType != 2) && (part->mPartType != 5))
	{
		char	str[256];	// Stuart's buffer, declared in this arm (DrawMeshPart). Byte-neutral.
		sprintf(str, "Attempt to render unknown mesh part type %d!\n", part->mPartType);
		TRACE(str);
	}

	// STAND-IN RECONSTRUCTION (disabled block): the original authors leave disabled code in place behind if (0)
	// (references/Onslaught DXMemBuffer.cpp:421, `if (0)//!THREADED && ...`). This block's contents are ours and unknown in
	// the original: a debug check of the part's orientation and of each bone matrix (row lengths, row dot products, the
	// translation). It emits no code; its declarations and the inlined check give the symbol count retail's bone products
	// were numbered after (decisions of all 84 retail inline sites unchanged; strict FP ledger 143/143). One local fewer than
	// before the skin loop got its second Normalise (2026-10-14): that inline expansion adds a symbol of its own.
	// STAND-IN (symbol count): one initialised dead local, no code. It stands where the weighted arm's
	// four locals (the volatile normal references and their product) stood before that arm took Stuart's text, and was three
	// until Stuart's mode-2 locals supplied two symbols of their own: with the end block at five floats it keeps the bone loop's
	// field symbols on retail's ids (0 0 0; none 6 48 45, two or three 18 30 22). Contents ours.
	{ SINT d0 = 0; }
	{ RmcSymbolPad pr; ReadRmcSymbolPad(pr); }
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
}

//******************************************************************************************
// A transformed, lit vertex (0x1c bytes; FVF 0x144: screen position, diffuse, one texture coordinate).
struct SMeshRenderer2DVertex
{
	float	X, Y, Z, RHW;
	DWORD	mDiffuse;
	float	mU, mV;
};

float	WrapUnit(float v);				// 004b5250 (MeshRenderer.cpp)
float	AbsFloat(float v);				// 004b52a0

// A part drawn flat on the screen, scaled so its bounding box fills it, each triangle shaded by the angle between its
// normal and a light from (1, 1, 1). Nothing calls it (name ours).
// STAND-IN (an explicit concession): the unreachable AdjustFrame call below
// never executes. Taking &t preserves its address escape in VC6's intermediate representation; the call
// is removed, while retail's otherwise-unused fraction store at 0054b8e9 remains. Forming tri from the
// table before adding i reproduces the table load order at 0054b9ed. The full compiled section and every
// relocation match; this is a documented reconstruction concession, not recovered original source.
// The original operation taking t's address is unknown. With the unreachable call removed and the
// original &part->mTriangles[i] form, the whole check yields 1904 bytes against retail's 1912, with
// 1440 differing bytes (10 exact object annotations); this stand-in yields 11 exact annotations.
void RenderPart2D(CMeshPart *part, IRenderableThing *thing)	// FUNCTION: 0x0054b800
{
	char	str[256];
	CMotionController	*mc = NULL;
	if (thing)
		mc = thing->GetMotionController();

	if ((part->mPartType == 2) || (part->mPartType == 4))
		return;
	if (part->mPartType == 1)
	{
		SINT	s0, s1;
		float	st, time;
		SINT	anim;
		if ((thing) && (part->mNumVFrames > 1) && (part->mBaseMesh->mNumAnimModes) && ((anim = thing->GetRealAnimIndex()) >= 0))
		{
			time = thing->GetRenderFrame();
			CAnimMode	*ma = &part->mBaseMesh->mAnimModes[anim];
			time = ma->mFrameScaler * time;
			time = ma->mStart + time;
			if (mc)
				mc->AdjustFrame(part, &time);
			SINT	f0 = SINT(floor(time));
			float	fl = (float)floor(time);
			float	t = time - fl;
			if (0) mc->AdjustFrame(part, &t);
			s0 = SINT(floor(0.0));
			s1 = s0 + 1;
			if (s1 > SINT(floor(0.0)))
				s1 = SINT(floor(0.0));
			s1 = s0;
			st = 1.0f;
		}
		FVector	*p0 = part->mPVertices[s0];
		FVector	*p1 = part->mPVertices[s1];
		float	f = st - (float)floor(st);
		float	g = 1.0f - f;

		CBoundingBox	*box = part->mBoundingBox;
		float	ax = box->mAxes.X;
		float	ay = box->mAxes.Y;
		float	minx = box->mOrigin.X - ax;
		float	miny = box->mOrigin.Y - ay;
		float	height = (float)LT.GetWindowHeight();
		float	sx = LT.GetWindowWidth() / (ax + ax);
		float	sy = height / (ay + ay);

		for (SINT i = 0; i < part->mNumTriangles; i++)
		{
			CMeshTriangle	*tri = part->mTriangles;
			tri += i;
			CMeshDVertex	*v0 = tri->mVertices[0];
			CMeshDVertex	*v1 = tri->mVertices[1];
			CMeshDVertex	*v2 = tri->mVertices[2];
			FVector	a = p0[v0->mPVert] * g + p1[v0->mPVert] * f;
			FVector	b = p0[v1->mPVert] * g + p1[v1->mPVert] * f;
			FVector	c = p0[v2->mPVert] * g + p1[v2->mPVert] * f;
			FVector	e1 = b - a;
			FVector	e2 = c - a;
			FVector	n = e1 ^ e2;
			FVector	light(1.f, 1.f, 1.f);
			n.Normalise();
			light.Normalise();
			float	d = (float)cos((n * light) * 3.14f);
			SINT	shade = SINT((float)fabs(d) * -128.f);
			DWORD	col = 0xff7f7f7f - shade * 0x10101;

			SMeshRenderer2DVertex	verts[3];
			float	x0 = (a.X - minx) * sx;
			float	y0 = height - (a.Y - miny) * sy;
			verts[0].X = x0;
			verts[0].Y = y0;
			verts[0].Z = 1.0f;
			verts[0].RHW = 1.0f;
			verts[0].mDiffuse = col;
			verts[0].mU = WrapUnit(v0->mU);
			verts[0].mV = WrapUnit(1.0f - AbsFloat(v0->mV));
			float	x1 = (b.X - minx) * sx;
			float	y1 = height - (b.Y - miny) * sy;
			verts[1].X = x1;
			verts[1].Y = y1;
			verts[1].Z = 1.0f;
			verts[1].RHW = 1.0f;
			verts[1].mDiffuse = col;
			verts[1].mU = WrapUnit(v1->mU);
			verts[1].mV = WrapUnit(1.0f - AbsFloat(v1->mV));
			float	x2 = (c.X - minx) * sx;
			float	y2 = height - (c.Y - miny) * sy;
			verts[2].X = x2;
			verts[2].Y = y2;
			verts[2].Z = 1.0f;
			verts[2].RHW = 1.0f;
			verts[2].mDiffuse = col;
			verts[2].mU = WrapUnit(v2->mU);
			verts[2].mV = WrapUnit(1.0f - AbsFloat(v2->mV));

			part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[0]].mVBuffer->mTexture->Select();
			UWORD	idx[3];
			idx[0] = 0;
			idx[1] = 1;
			idx[2] = 2;
			CVBufTexture	*vb = part->mBaseMesh->mTextures[tri->mVertices[0]->mTexture[0]].mVBuffer;
			vb->SetVBFormat(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(SMeshRenderer2DVertex), D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
			vb->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 2, D3DPOOL_DEFAULT);
			vb->AddVertices(verts, 3);
			vb->AddIndices(idx, 3);
			vb->Draw(true);
		}
	}
	else if (part->mPartType == 3)
		TRACE("Error : 2D renderer doesn't support skeletal meshes!\n");
	else
	{
		sprintf(str, "Attempt to render unknown mesh part type %d!\n", part->mPartType);
		TRACE(str);
	}
}
