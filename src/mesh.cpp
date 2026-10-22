// mesh.cpp: reconstructed from mesh.obj (004a4f30..004abd5f); Stuart's release has no copy. The allocations' __LINE__
// values are the retail ones; the #line directives place them, because the retail file's layout is unknown.
#define MESH_CPP	// before common.h: maths.h's FMatrix::Read
#include "MemoryManagerMesh.h"	// file-local copy of MemoryManager.h with the Load pad; must precede common.h
#include "common.h"
#include <d3d9.h>
#include "MeshMathsPrivate.h"
#include "mesh.h"
#include "DXMeshPart.h"
#include "membuffer.h"
#include "DXMemBuffer.h"
#include "mathsconst.h"
#include "Texture.h"
#include "DebugLog.h"
#include "console.h"
#include "game.h"
#include "chunker.h"
#include "PolyBucket.h"
#include "Motioncontroller.h"
#include "animation.h"
#include "engine.h"
#include "DXMeshVB.h"

// FUNCTION: 0x004a4f30 init:ZERO_FVECTOR
// FUNCTION: 0x004a4f50 init:ID_FMATRIX

CMesh			*sFirst = NULL;				// 00704ad8
CMeshTexture		*sDefaultTexture = NULL;	// 00704adc
BOOL				sReportMissing = FALSE;		// 00704ae0
bool				sReadingMeshFile = FALSE;	// 00704ae4: Deserialize opened the mesh's own resource file
SINT				sEmitterBytes = 0;			// 00704ae8

//******************************************************************************************
#line 43
CMesh::CMesh()	// FUNCTION: 0x004a5020
{
	mNumParts = 0;
	mParts = NULL;
	mNumEmitters = 0;
	mEmitters = NULL;
	mNumAnimModes = 0;
	mAnimModes = NULL;
	mNumTextures = 0;
	mTextures = NULL;
	mRadius = 0.5f;
	mLastFrameCollisionGenerated = -2;
	mNextInChain = NULL;
	mSequences = NULL;
	mNumSequences = 0;
	mRefCount = 0;
	mResourceID = -1;
	memset(mName, 0, sizeof(mName));
	mBoundingBox = new(MEMTYPE_MESH) CBoundingBox;

	mNextMesh = sFirst;
	sFirst = this;
}

//******************************************************************************************
CMesh::~CMesh()	// FUNCTION: 0x004a50b0
{
	CMesh	*prev = NULL;
	CMesh	*m = sFirst;
	while (m)
	{
		if (m == this)
			break;
		prev = m;
		m = m->mNextMesh;
	}
	if (m)
	{
		if (prev)
			prev->mNextMesh = mNextMesh;
		else
			sFirst = mNextMesh;
	}

	if (mTextures)
	{
		delete [] mTextures;
		mTextures = NULL;
	}

	if (mParts)
	{
		for (SINT i = 0; i < mNumParts; i++)
		{
			if (mParts[i])
			{
				delete mParts[i];
				mParts[i] = NULL;
			}
		}
		SAFE_DELETE(mParts);
	}
	SAFE_DELETE(mEmitters);
	SAFE_DELETE(mAnimModes);
	if (mNextInChain)
	{
		mNextInChain->Release();
		mNextInChain = NULL;
	}
	SAFE_DELETE(mBoundingBox);
}

//******************************************************************************************
#line 140
BOOL CMesh::InitAll()	// FUNCTION: 0x004a5200
{
	if (sDefaultTexture)
		delete sDefaultTexture;

	sDefaultTexture = new(MEMTYPE_MESHTEXTURE) CMeshTexture;
	sDefaultTexture->InitDefault();
	sDefaultTexture->mTexture = CDXTexture::FindTexture("meshtex\\default.tga", 0, 0, -1, 1, 1);
	return TRUE;
}

//******************************************************************************************
void CMesh::ReleaseAll()	// FUNCTION: 0x004a52b0
{
	for (CMesh *m = sFirst; m; m = m->mNextMesh)
		m->mRefCount = 0;
	ShutdownAll();
}

//******************************************************************************************
void CMesh::ShutdownAll()	// FUNCTION: 0x004a52d0
{
	SAFE_DELETE(sDefaultTexture);

	BOOL	done;
	do
	{
		done = TRUE;
		CMesh	*m = sFirst;
		while (m)
		{
			CMesh	*next = m->mNextMesh;
			if (!m->mRefCount)
			{
				done = FALSE;
				delete m;
			}
			m = next;
		}
	}
	while (!done);

	CMesh	*m = sFirst;
	if (m)
	{
		TRACE("\n-------------------\n");
		TRACE("Mesh resource leaks\n");
		TRACE("-------------------\n\n");
		for (; m; m = m->mNextMesh)
		{
			char	message[256];
			sprintf(message, "Mesh '%s' leaked : refcount=%d\n", m->mName, m->mRefCount);
			TRACE(message);
		}
		TRACE("\n-------------------\n\n");
	}
	else
	{
		TRACE("\n-------------------\n");
		TRACE("No mesh resource leaks!\n");
		TRACE("-------------------\n\n");
	}
}

//******************************************************************************************
void CMesh::LoadLevelResources(SINT level)	// FUNCTION: 0x004a53f0
{
	char	message[256];
	sprintf(message, "Loading mesh resources");
	CONSOLE.Status(message);
	CONSOLE.StatusDone(message, TRUE);
}

//******************************************************************************************
void CMesh::FreeLevelResources()	// FUNCTION: 0x004a5430
{
	sReportMissing = FALSE;

	BOOL	done;
	do
	{
		done = TRUE;
		CMesh	*m = sFirst;
		while (m)
		{
			CMesh	*next = m->mNextMesh;
			if (!m->mRefCount)
			{
				done = FALSE;
				delete m;
			}
			m = next;
		}
	}
	while (!done);

	CMesh	*m = sFirst;
	TRACE("\n-------------------------------\n");
	TRACE("Mesh end-of-level resource leaks\n");
	TRACE("-------------------------------\n\n");
	for (; m; m = m->mNextMesh)
	{
		char	message[256];
		sprintf(message, "Mesh '%s' leaked : refcount=%d\n", m->mName, m->mRefCount);
		TRACE(message);
	}
	TRACE("\n------------------------------\n\n");
}

//******************************************************************************************
void CMesh::CheckForAnimations(char *name, CAnimMode *anim)	// FUNCTION: 0x004a5500
{
	if (!stricmp(name, "NONE"))				anim->mMode = AM_NONE;
	else if (!stricmp(name, "WALK"))		anim->mMode = AM_WALK;
	else if (!stricmp(name, "RUN"))			anim->mMode = AM_RUN;
	else if (!stricmp(name, "STAND"))		anim->mMode = AM_STAND;
	else if (!stricmp(name, "SHOOT"))		anim->mMode = AM_SHOOT;
	else if (!stricmp(name, "SHOOT1"))		anim->mMode = AM_SHOOT1;
	else if (!stricmp(name, "SHOOT2"))		anim->mMode = AM_SHOOT2;
	else if (!stricmp(name, "FLY"))			anim->mMode = AM_FLY;
	else if (!stricmp(name, "HOVER"))		anim->mMode = AM_HOVER;
	else if (!stricmp(name, "SHOOTWALK"))	anim->mMode = AM_SHOOTWALK;
	else if (!stricmp(name, "IDLE"))		anim->mMode = AM_IDLE;
}

//******************************************************************************************
// Remaps vertices to the last earlier identical texture (same texture and frames); the textures stay.
void CMesh::OptimiseTextures()	// FUNCTION: 0x004a5670
{
	SINT	old_count = mNumTextures;
	SINT i = 0;
	while (i < mNumTextures)
	{
		SINT	found = -1;
		for (SINT j = 0; j < i; j++)
		{
			BOOL	same = TRUE;
			if (mTextures[i].mTexture != mTextures[j].mTexture)
				same = FALSE;
			if ((mTextures[i].mFrames == mTextures[j].mFrames) && same)
			{
				for (SINT f = 0; f < mTextures[i].mFrames; f++)
				{
					if (mTextures[i].mTextureBlend[f] != mTextures[j].mTextureBlend[f])
						same = FALSE;
					if (mTextures[i].mTextureU[f] != mTextures[j].mTextureU[f])
						same = FALSE;
					if (mTextures[i].mTextureV[f] != mTextures[j].mTextureV[f])
						same = FALSE;
					if (mTextures[i].mTextureUScale[f] != mTextures[j].mTextureUScale[f])
						same = FALSE;
					if (mTextures[i].mTextureVScale[f] != mTextures[j].mTextureVScale[f])
						same = FALSE;
				}
				if (same)
					found = j;
			}
		}

		if (found == -1)
		{
			i++;
			continue;
		}
		{
			for (SINT p = 0; p < mNumParts; p++)
				for (SINT v = 0; v < mParts[p]->mNumDVertices; v++)
					for (SINT t = 0; t < 6; t++)
						if (mParts[p]->mDVertices[v].mTexture[t] == i)
							mParts[p]->mDVertices[v].mTexture[t] = found;
		}
		i++;
	}

	char	message[256];
	sprintf(message, "OptimiseTextures() reduced texture count from %d to %d\n", old_count, mNumTextures);
	TRACE(message);
}

//******************************************************************************************
// FUNCTION: 0x004a5970 ?InitFromFile@CMesh@@QAEHPADH@Z
BOOL CMesh::InitFromFile(char *name, SINT flags)
{
	SINT	start = 0;
	char	message[256];
	sprintf(message, "Warning : Loading mesh %s manually\n", name);
	TRACE(message);

	char	status[256];
	sprintf(status, "Loading mesh %s", name);
	CONSOLE.Status(status);

	for (SINT i = 0; name[i]; i++)
		if (name[i] == '\\')
			start = i + 1;

	char	filename[256] = "data\\Meshes\\";
	strcat(filename, name + start);
	strcpy(mName, name + start);

	CDXMemBuffer	file;
	if (!file.InitFromFile(filename))
	{
		CONSOLE.StatusDone(status, FALSE);
		file.Close();
		return FALSE;
	}

	BOOL	ok = InitFromMem(file, flags);
	file.Close();
	CONSOLE.StatusDone(status, TRUE);
	return ok;
}

// FUNCTION: 0x004a5930 init:MESH_CHUNK
UINT	MESH_CHUNK = MKID("MESH");

extern BOOL	gNoMeshPartOptimise;		// 00662f48 (name ours): skips OptimiseParts

// The identity, row by row through FVector (004a8613..004a86a4).
static inline void SetIdentity(FMatrix &m)
{
	m.Row[0] = FVector(1, 0, 0);
	m.Row[1] = FVector(0, 1, 0);
	m.Row[2] = FVector(0, 0, 1);
}

//******************************************************************************************
// The parts' new-expressions in CMesh::Load (both formats) and CMesh::Deserialize. Stuart's source allocates `new CMESHPART`, and
// MeshPart.h:230 defines CMESHPART as CDXMeshPart, an empty class derived from CMeshPart (DXMeshPart.h, found
// 2026-10-14, in the original source Stuart published; ours: src/DXMeshPart.h). Its implicit inline constructor over the
// out-of-line CMeshPart constructor is what retail's code shows: the raw pointer stays in a callee-saved register across the call
// (Deserialize 004ab0c6..004ab0f8; Load 004a602e..004a6063 and 004a8acb..004a8b03: `mov esi,eax`, the call, `xor esi,esi` in the null
// arm, the index reloaded from its stack home), where a directly called out-of-line constructor stores its return in eax.
// Each use adds one inline constructor site to the function (Load: two top-level sites, +3 own size each). Without it in Load:
// 10846 bytes off, 191 calls; in Deserialize: 1454 bytes (the ablation table above Load).

//******************************************************************************************
// STAND-IN accessor: CMeshPart::GetHFrameOrientation with a named local, file-local. There is no evidence
// that Stuart's accessor had one; it is chosen for its effect on the inline ids. Its local takes one pool-A inline id per
// expansion, which the operator* expansions that follow in the same statement pop, and so moves the term order of the
// 1/3 block's matrix chain to retail's order. Used in that one chain statement only. Without it (the plain accessor
// there; at 9970fe15): CMesh::Load 29 bytes off instead of 13 (the extra hunk is 4a6ccd..4a6d3d, 16 lines), natural
// 14/48/43, 192 calls, strict ledger lost 1 (tree 53). Its other use, the 2456 block's post-loop orientation copy (c6481cee),
// changes nothing at this head and is gone; the plain accessor stays everywhere else.
static inline FMatrix &KeyOf(CMeshPart *part, SINT k) { FMatrix *p = &part->mHFrameOrientations[k]; return *p; }

//******************************************************************************************
// ABLATION OF LOAD'S STAND-INS AND CONCESSIONS (2026-10-15, at 9970fe15). Each row takes ONE device out of the text and compiles naturally with the typed new at
// its single `{ }` (MemoryManager.h); every number below is that measurement, and the older numbers in the comments further down are history. Reference: natural
// shape score 0/28/27, 192 calls, frame 0x83c, own size 9822 (window 9804..9841 at this typed new), strict FP ledger vs main 256 reproduced, lost 0;
// the byte check of src/mesh.cpp: CMesh::Load 13 bytes off (4a6f5c..4a6fba the 2456 chain's x-sm product issue order, 4a8da6 a base/index order), the
// other 39 functions match. "bytes" is the check's bytes off, "nat" the natural shape/ins/bytes with the call count, "lost" the strict-ledger trees lost on the natural object.
//  - CDXMeshPart (was FMeshPartNew) at Load's two part allocations -> plain CMeshPart: own 9816, 10846 bytes, nat 185/1367/10860 c191, lost 12. (Deserialize's site alone: Deserialize 1454 bytes.)
//  - KeyOf in the 1/3 chain -> the plain accessor: 29 bytes (4a6ccd..4a6d3d, 16 lines added), nat 14/48/43 c192, lost 1 (tree 53). The 2456 block's copy uses the plain accessor.
//  - the quaternion conversions in place -> the old inline QuatToMatrix(m, x, y, z, w) (ede89d29): own 8970, 15370 bytes, nat 905/3563/15564 c205, frame 0x848.
//  - SetIdentity(m) -> `m = ID_FMATRIX`: 14243 bytes, nat 763/2899/15797 c187, lost 3; -> `m = FMatrix(FVector(1,0,0), FVector(0,1,0), FVector(0,0,1))`: own 10123, 15472 bytes, c195, frame 0x968.
//  - the field-wise em->mPos update -> `em->mPos -= part->mOffsetPos`: 9256 bytes, nat 255/2025/9274 c193, lost 4 (the same with the own size restored by a dead chain).
//  - the one-trip loop around mFrameU: own 9793, 9281 bytes, nat 367/2093/9299 c193, lost 8; with the own size restored: 8900 bytes, nat 59/107/8902 c192.
//  - the second key loop's f counter -> the shared k: 12510 bytes (frame slots), nat 29/1187/12512 c192, lost 2.
//  - num_layers declared in the 2.04 branch -> at the function head: 5811 bytes (frame slots), nat 0/1428/5829, lost 0.
//  - `SINT n = 0;` in the 2.04+ animation loop -> `SINT n;`: 22 bytes (+9 at 4a8807..4a880b), nat 0/32/36.
//  - named results: the old format's IsMoreData result unnamed: 127 bytes (nat 48/84/141, lost 3); both AnimModeLoops results unnamed: 148 bytes. The FindTexture result is unnamed since 2026-10-15:
//    Stuart's `int numskins=0;` per old-format part (mesh.cpp:654) supplies the same one symbol and is passed to ReadVersion20n: 0 28 27, 13 bytes, frame 0x83c, ledger lost 0 (c3skins)
//    (nat 58/84/162, lost 65); the four 2.04+ texture arms' rtex unnamed: 9218 bytes, nat 313/2049/9236 c193, lost 5 (own size restored: 9001, nat 57/105/9003 c192);
//    all of them: 9259 bytes, nat 351/2089/9277 c193, lost 13 (restored: 8945, nat 91/165/8947 c192).
//  - frame buffers: path[352] -> path[256] 59 bytes (frame 0x7dc), name[MAX_PATH] -> name[256] 46 bytes, status[128] -> status[256] 203 bytes (nat 4/320/217, frame 0x8bc).
//  - corners and c declared after o -> ahead of box: 17 bytes (+4 at 4a98bd..4a98c4).
//  - the extremes' id order (minx miny, then the maxes with dead zeros, then minz) -> the plain declaration: 15 bytes (+2 at 4aa1af..4aa1b6), nat 0/32/29.
//  - the four dead operator statements (STAND-IN, below), natural / bytes / lost: without the 2.04 branch's 156/1114/7172 c191, 7142, 9; without one of the two after the
//    extremes 155/1113/9172 c191, 9142, 8; without both 217/1517/11953 c190, 11907, 9; without `ori * za` (was az) 164/1264/9171 c191, 9141, 8; without all four 335/1763/11933 c190, 11887, 12.
//  - the typed new's single `{ }` -> the plain body: 12217 bytes, nat 340/2356/12311 c188, lost 9. Extra empty blocks (K): 1 is clean (192 calls), 2..12 give 193, 13 gives 194, 14 gives 196.
// No cost at 9970fe15 (the text now takes the natural form): KeyOf in the 2456 block's orientation copy; `FMatrix sm;` and the nine products declared before the 1/3
// loop (and the nine before the 2456 loop): declared inside the loops they compile to the same bytes, shapes, calls and ledger (13 bytes, 0/28/27, 192, lost 0).
// Hunks left (13 bytes): 4a6f5c..4a6fba (12 lines) and 4a8da6 (1 line), held by F and B/D.

//******************************************************************************************
// STAND-IN (2026-10-14): the key blocks' quaternion conversions are written out in place.
// The #line spans say retail's key blocks are about 90 lines longer than a helper call, and retail's frame keeps ONE set of nine spilled quaternion
// products per key block, shared by the block's rotation and its scale rotation (packer classes {A.zz B.zz C.yy D.yy} ..., slots 128, 120, 112, 132, 4, 8):
// so each key block's nine products are locals of its loop (their declaration point changes nothing at 9970fe15), computed in place for the rotation and again for the scale
// rotation, and the matrix statements follow in place. With the box in retail's form the pasted conversions put the
// function's own size in the retail window (a pinned-window measurement) and the frame comes out exact. Member stores are unnamed (`member = new ...`, Stuart's own style,
// DXEngine.cpp:171, engine.cpp:310); the typed new keeps only a single `{ }`, in the file-local copy MemoryManagerMesh.h (the authorized header-copy concession; the shared MemoryManager.h keeps main's text, and its earlier thirteen-block pad is gone). Without the conversions in
// place (the inline QuatToMatrix helper of ede89d29 at the four sites; at 9970fe15): own size 8970, CMesh::Load 15370 bytes off, natural 905/3563/15564, 205 calls, frame 0x848.
// SYMBOL SOURCES (2026-10-14; found by a search over whole families, not by evidence): the import-time symbols that retail has and a minimal spelling lacks
// come from structure that changes no code. The frame loops declare their own counter (`for (SINT f = 0; ...)`); the second key loop counts with the f that
// the frame loop above it leaves in the block (VC6 leaks a for-init declaration into the enclosing block), which is the f counter without a function-level f
// (the old `SINT i, f, c;` f was dead: dropping it changes no byte). In the old format (2.01..2.03) call results are named before they are used (FindTexture,
// IsMoreData, AnimModeLoops); in 2.04+ each texture arm names its result `rtex` (the tex4 form) and the animation loop names AnimModeLoops' result (`loops`).
// Which results Stuart named is not known: the bytes cannot show it, and the old and the 2.04+ code are plausibly different eras of the file.
// Costs at 9970fe15 (the ablation table above Load): the second key loop on the shared k, 12510 bytes; the old format's FindTexture or IsMoreData result unnamed, 127 bytes
// each; both AnimModeLoops results unnamed, 148 bytes; the 2.04+ arms' rtex unnamed, 9218 bytes (9001 with the own size restored).
// INTEGRATED EXPLORATION, concessions over the rule base 599cc575: (1) the conversions in place (above); (2) corners and the counter c declared
// after o, not ahead of box (placement chosen by search, below); (3) the f counter in the second key loop (the leaked f);
// (4) the symbol-source rules above (named results; the tex4 texture arms); (5) STAND-IN: the one-trip loop around the texture frame store (a carrier,
// in the 2.07 frames loop). Which of these Stuart wrote is not known; the bytes cannot show it.

//******************************************************************************************
// A mesh from its file: the old-style format (2.01..2.03), or from 2.04 each part's type, key frames and geometry; then
// the emitters, the animations, the frame table and the lower levels of detail, and the parts made ready to draw.
// Part lookups index mParts directly (the frame parts too, since 2026-10-13). GetPart's inlined result is a named
// symbol, which takes eax without moving VC6's register rotation, so each lookup's registers show its form: retail's
// children, parents, emitters and the reference part load the part into the rotation's register (004a8e6a, 004a8eac,
// 004a83a6, 004a83ea, 004a72cc, 004a7310, 004a734c, 004a85c0), as an indexed read does. The key-frame blocks read the
// keys back through CMeshPart's accessors (GetHFrameOrientation/GetKeyPosition, as GetTransforms 004b0d00 does): those
// sites give back the inline budget the direct lookups free, and the 1/3 key block's registers then equal retail's.
// A key's scale matrix is the identity with each row scaled: the X products fold and six fmuls stay (004a6b44..
// 004a6bc4, 004a7ba1..004a7c3d). The scale rotation is built straight into its matrix; retail has no copy of it.
// The identity is set in place (SetIdentity): at a new-format emitter retail writes two rows from constructed
// vectors and the third through FVector's out-of-line constructor (004a868c) into em->mOrientation itself, so the
// three constructors are nested sites of one helper, not arguments of a matrix constructor.
// The mesh box's corners are pos + o + ori * FVector(...), o the turned origin, in retail's order (+++, ++-, +--,
// +-+, then the same with -x; 004a983f..004aa06c). The current stock compile has all seven FVector constructor
// calls in retail's sequence, including 004a6fff in the types 2/4/5/6 branch: the older extra-constructor lead is
// superseded. The animation loops walk a pointer with their own counter; the name buffer is 17 bytes.
// Box (2026-10-13, at 424 2886 13798): retail's corners are this source's form. Each corner is ori times a
// FVector(+-axes) temporary (nine products, each row summed (Z + Y) + X, 004a9867..004a98b5), and each negation is
// made once and reused: -axes.Z at corner 1 (fchs in place, 004a998e) stays on the stack into corner 2, and -axes.Y at
// corner 2 (004a9a8e) is copied for the corners after it. pos + o is one common subexpression from corner 0
// (004a98bd..004a98fa) that every corner adds its product to. Per-axis vectors summed, as in the sibling
// CDXEngine::DrawDebugCuboid (references/Onslaught/DXEngine.cpp:233), would show vector sums, not nine products.
// What is left is x87 colouring: retail gives axes.X and axes.Y memory homes, keeps axes.Z on the stack through o's
// rows into corner 0 (004a97df..004a9861) and makes copies at first use for later corners; ours stores all three axes
// and reloads them per corner, because its axes webs are coloured first and fail the death-point rule (all eight
// corners use them). Tried and worse: named floats, direct box->mAxes reads, axes copied or by reference, o as dot
// products, o + pos, the corners through m, other groupings and declaration orders, the negations made in place
// between corners (505..933). A labelled dead escape on o gave 406 on 801b8dc5 (box 272/38) and is not committed.
// The corners read box->mAxes directly (no axes local) reload the axes in every corner (13 or 14 loads against retail's 3),
// also with eight corner locals stored only after all the reads (897) or a braced corners initialiser (730): something
// inside each corner's arithmetic kills the axes' loads (the inlined operator* return object or the FVector temporaries
// are the candidates), while the negations stay common subexpressions across corners as in retail. Retail's
// fld [eax+0x10]; fstp [esp+0x41c] (004a97cb) reads as spilled webs of those loads, made before o's rows.
// Swapping the min and max initialisations keeps 424 2886 and gives 13786 bytes: frame slot order, for later.
// (CMesh::FindTexture stays a call through its own size, below.)
// Lead step (2026-10-10, private until a whole match): QuatToMatrix by const reference and the box's o in memory (a
// labelled dead escape; replaced with a reference to the product's temporary on 2026-10-13) took Load from 572
// to 478 (shape score; 494 with the escape alone, under retail's inline decisions). The inline-budget
// stand-in then was braces on the first six `if (...) return FALSE;` bodies (+2 each, own size 8705, window 8705..8713);
// the budget is now fitted in the two helpers instead (below), and those six braces are gone.
// Diagnostics (no source): dead block locals anywhere in Load shift every inline-time id by their count; 2 more give
// 444, 47 more put the headers, both key blocks and the tail near retail's (the alias cap and the product orders) but
// not the box. Tools: a forced-inline evaluator, own-size checker and scans.
// Chooser step (2026-10-13, private): three forms take the scratch-register rotation to retail's outside the
// box. (1) The texture chain stores in each arm (below): the identical tails are cross-jumped after allocation and their
// deleted copies still step the rotation. (2) The frame parts read mParts[n] directly. (3) CDXMeshPart, Stuart's CMESHPART (DXMeshPart.h)
// at both part allocations, for retail's raw new pointer in esi with the index in memory. Measured with Load's inline
// decisions held at the base (retail's, by the forced evaluator): shape score 478 -> 428, register-only lines
// outside the box 112 -> 10 (new header 63 -> 0, new tail 10 -> 0, old format 31 -> 2).
// Budget (superseded 2026-10-14): the braces that fitted Load's inline budget inside the helpers are gone; the conversions written out in place
// (above) put Load's own size inside the window where every inline decision is retail's. At 9970fe15 the typed new is the single `{ }` (K = 0) and the window for
// the aligned sites is own size 9804..9841 (a size simulator; the text is 9823); K=1 is also clean, K=2..12 give 193 calls.
// HISTORY (superseded by the table above Load: these paragraphs measured other texts, the alias arm and the disabled block are gone, and the typed new had thirteen
// extra blocks): W2, alias classes and ids.
// W2 (2026-10-14, intermediate): retail has about 70 more import-time symbols than a minimal spelling of Load. Measured:
// the Transpose ids of the old emitter product (004a9319 needs them about 57 higher), and a scan of unused locals with the alias
// arm below removed, where 69..76 symbols put every region outside the box at its best. This text supplies them with 30
// single-use named results (a `new`, FindTexture, IsMoreData, AnimModeLoops or cast result named before it is stored; each is
// one symbol and changes no code) and the disabled block at the end of Load; the arm is gone. Which results Stuart named is
// not known: the bytes cannot show it, so the chosen sites are a reconstruction. The block's names count as import-time ids
// but not as alias symbols (dead code is gone before the alias builder), so N needs 24 more classes: the alias arm now sits
// in the 2/4/5/6 branch of the part loop (N 994; unused-local S72 gives 992, the same 25 field classes). The box is still tuned for the old world.
// Alias classes (2026-10-13, private): C2 gives a function 1,024 alias ids, N = call classes + symbols + pointer classes
// (here 222 + 564 + 133 = 919), and builds field classes only in the room left (1,024 - N), in source order; a field without a
// class conflicts with its whole base, so its store keeps its place ahead of later loads (retail's `new` results stored right
// after the call, 004a6388). Retail has 53 to 88 more classes than this source: a dead-local scan (S = 1..100 unused locals at the
// end of Load, retail's inline decisions forced) fixes the new header, the 1/3 header, the new tail and the tail from S = 38 and 53
// and breaks the new tail again from 89 (an escape scan). The cheapest source for them is
// pointer classes, not symbols: a second definition of part in the 2.04 part loop joins a new pointer class at every block part flows
// through (+53 here: N 919 -> 972, symbols and ids unmoved), and an arm that never runs is enough (`if (0) part = NULL;`; a self
// copy, a constant-false test and part = mParts[i] give the same; a use in the arm, an empty arm and a real condition do not, or
// emit code). STAND-IN: the original source of these classes is unknown (a conditional second definition that emits no code); the
// plain form gives masked/register-only new header 14/0, 1/3 header 18/0, new tail 10/3, tail 4/0 and shape score 424;
// with the arm 2/0, 0/0, 0/3, 0/0 and 382 (own size +11 to 8747, the same inline decisions, mesh.cpp's other 39 matches unchanged).
// Ids (2026-10-13, private): with the alias classes supplied, what is left outside the box is operand order, which
// follows the pool-A inline ids. A uniform shift of 12-14 (unused locals at the end of Load; their position does not matter) puts
// the 2456 block's sums at 23/7 and the 1/3 block's at 2/1, but only counts, never positions, and the old format needs 57 or more.
// Positional sources do it block by block: the 2456 block's frame-key counter declared in its for-init (a scan of the
// per-loop counters; k2456 41/7 -> 23/7, a plain source form) and the STAND-IN KeyOf accessor in the 1/3 block's chain (k13 18/1 ->
// 2/1). Together: shape score 382 2846 13733 -> 348 2842 13738, own size 8748 (window 8708..8766), N 975, frame classes
// reproduced 34 of 166 -> 36 of 162. Not moved: the old format (29/6; a uniform 14 gives 17/6), the box, and k2456's last 23/7.
// Near miss (state on 2026-10-02):
// - Masked shape 572; 191 direct calls in both bodies. The ordered targets of 189 calls are independently aligned
//   through annotations or matched controls. The other two are the throwing vector-constructor iterator (??_L),
//   paired with retail's 0055dc20; its library identity remains unresolved. The frame is 0x860 against retail's 0x83c.
// - Masked/register-only differing lines by region:
//   - the box corners: 412/41;
//   - the new-format header: 29/63 (the texture chain's path pushes are tail-merged in ours; the part-allocation
//     loop: retail keeps i in memory and the new part in esi);
//   - the 2/4/5/6 keys: 41/7;
//   - the old format: 24/33;
//   - the 1/3 part header: 18/0;
//   - the 1/3 keys: 30/1.
// - Retail stores each new's result right after the call (004a6388, 004a63f5): VC6 runs out of alias classes there
//   (1,024 a function, C2 1071b0e5), and ours has more room left.
// - The frame-part lookup's registers (004a88c8) point to a direct read too, but that changes two inline decisions.
// - Ruled out:
//   - the quaternion conversions written out (no own size fits);
//   - the texture chain as a ternary;
//   - more alias base classes alone (a C2 copy with a lower cap gains about 36 lines).
// - Next: the box corners (retail keeps each corner temporary's Y in memory and copies pos + o per corner), then the
//   ids behind the product orders.
// - Frame. Tools: an esp simulator follows retail's esp through
//   the code, a call-argument mapper maps our address-taken objects to retail's slots through the aligned calls, and
//   a packer replay replays our packer, which here packs without parameter homes and gives our layout exactly.
//   - Retail's class holding the box corners (FVector[8]) and the part loop's status buffer is 128 bytes (slots
//     1240..1368; the next object starts at 1368), so status is at most 128 bytes there; ours is 256.
//   - The name buffer's class is 260 bytes (1448..1708), MAX_PATH's size, against our 256.
//   - Above path (1756) retail has 96 more bytes than a 256-byte path, which no instruction touches, up to the EH
//     record at 0x83c.
//   - The other classes: retail's below the scale vectors take 8 bytes more, those between them and status 16 fewer.
//   - status[128] and name[MAX_PATH] are in (2026-10-13: the classes above them then sit 8 bytes off
//     retail's and the EH record at 2020 against retail's 2108, the path's missing 96 bytes less those 8; shape unchanged).
//     path[352] is in too: its home in retail's frame spans 352 bytes up to the EH record (nothing touches the last 96);
//     the size is measured, no original source is known.
//   - Retail keeps num_layers in the old format's num_sets class (116). Declared in the 2.04 branch, num_layers leaves
//     the EH class of the old format's calls and joins that class here too, but every offset above it moves (3080 ->
//     3576 differing instructions) and the frame stays 0x860.
// FUNCTION: 0x004a5b70 ?InitFromMem@CMesh@@QAEHAAVCDXMemBuffer@@H@Z
BOOL CMesh::InitFromMem(CDXMemBuffer &file, SINT flags)
{
	UINT	version;
	file.Read(&version, 4);
	if (version != MESH_CHUNK)
		return FALSE;
	file.Read(&version, 4);

	SINT	i, c;
	char	name[MAX_PATH];
	char	path[352];		// size measured from retail's frame (path's home spans 352 bytes up to the EH record); no original source is known. At 256: 59 bytes off, frame 0x7dc (at 9970fe15; name[256] 46, status[256] 203)
	if ((version == MKID("2.01")) || (version == MKID("2.02")) || (version == MKID("2.03")))
	{
		SINT	num_textures;
		file.Read(&num_textures, 4);
		mNumTextures = num_textures;
		if (num_textures)
		{
#line 565
			mTextures = new(MEMTYPE_MESHTEXTURE) CMeshTexture[num_textures];
			for (i = 0; i < num_textures; i++)
			{
				file.Read(name, 256);
				CMeshTexture	*tex = &mTextures[i];
				tex->InitDefault();
				sprintf(path, "meshtex\\%s", name);
				tex->mTexture = FindTexture(path);
				mTextures[i].CreateVBuffer();
			}
		}

		file.Read(&mNumParts, 4);
#line 585
		mParts = new(MEMTYPE_MESH) CMeshPart *[mNumParts];
		if (!mParts)
			return FALSE;
		for (i = 0; i < mNumParts; i++)
		{
#line 593
			mParts[i] = new(MEMTYPE_MESH) CDXMeshPart;
			if (!mParts[i])
				return FALSE;
		}

		CONSOLE.Status("Loading mesh parts (old-style)");
		for (i = 0; i < mNumParts; i++)
		{
			CMeshPart	*part = mParts[i];
			part->mPartNumber = i;
			part->mBaseMesh = this;
			strcpy(part->mName, "");
			SINT	num_vertices, num_positions, num_triangles, num_sets, num_frames;
			file.Read(&num_vertices, 4);
			file.Read(&num_positions, 4);
			file.Read(&num_frames, 4);
			if ((version == MKID("2.02")) || (version == MKID("2.03")))
				file.Read(&num_sets, 4);
			else
				num_sets = num_frames;
			file.Read(&num_triangles, 4);
			if (!part->InitMemory(num_vertices, num_positions, num_triangles, num_frames, num_sets))
				return FALSE;
			part->mOffsetPos.Read(&file);
			part->mOrientation.Read(&file);
			if (!i)
				part->mOrientation = ID_FMATRIX;

			part->mNumHFrames = 1;
			part->mPartType = 1;
#line 652
			part->mVHFrameMap = new(MEMTYPE_MESH) UBYTE[part->mNumVFrames];
#line 653
			part->mHFrameOrientations = new(MEMTYPE_MESH) FMatrix[part->mNumHFrames];
#line 654
			part->mHFramePositions = new(MEMTYPE_MESH) FVector[part->mNumHFrames];
			for (int i = 0; i < part->mNumVFrames; i++)		// Stuart's `for (int i=0;...)` over the VFrame map (VC6 leaks i into the part block)
				part->mVHFrameMap[i] = 0;
			part->mHFrameOrientations[0] = part->mOrientation;
			part->mHFramePositions[0] = part->mOffsetPos;

			file.Read(&part->mNumChildren, 4);
			if (part->mNumChildren)
			{
#line 678
				part->mChildren = new(MEMTYPE_MESH) CMeshPart *[part->mNumChildren];
			}
			else
				part->mChildren = NULL;
			SINT	n;
			for (c = 0; c < part->mNumChildren; c++)
			{
				file.Read(&n, 4);
				if ((n >= 0) && (n < mNumParts))
					part->mChildren[c] = mParts[n];
				else
					part->mChildren[c] = NULL;
			}
			file.Read(&n, 4);
			if ((n >= 0) && (n < mNumParts))
				part->mParent = mParts[n];
			else
				part->mParent = NULL;

			int	numskins = 0;		// Stuart's local (mesh.cpp:654, `int numskins=0;` per part), passed to the part readers; it replaces the old FindTexture named result
			if (version == MKID("2.01"))
				part->Read201(&file, this, mTextures, num_textures, numskins);
			else if (version == MKID("2.02"))
				part->Read202(&file, this, mTextures, num_textures, numskins);
			else
				part->Read203(&file, this, mTextures, num_textures, numskins);
			SINT	end;
			file.Read(&end, 4);
		}
		CONSOLE.StatusDone("Loading mesh parts (old-style)", TRUE);

		file.Read(&mNumEmitters, 4);
		if (mNumEmitters)
		{
#line 725
			mEmitters = new(MEMTYPE_MESH) CEmitter[mNumEmitters];
			if (!mEmitters)
				return FALSE;
			MEM_MANAGER.TagMem(mEmitters, mNumEmitters * sizeof(CEmitter), "Meshes/%s/Emitters", mName);
			for (i = 0; i < mNumEmitters; i++)
			{
				CEmitter	*em = &mEmitters[i];
				em->mNumber = i;
				em->mSubpartEmitter = 0;
				strcpy(em->mName, "Emit");
				SINT	n;
				file.Read(&n, 4);
				if ((n >= 0) && (n < mNumParts))
					em->mMeshPart = mParts[n];
				else
					em->mMeshPart = 0;
				em->mPos.Read(&file);
				em->mOrientation.Read(&file);
				CMeshPart	*part = em->mMeshPart;
				if (part)
				{
					// relative to the part
					// STAND-IN (spelling): retail's position update is an inline FVector::operator-= (004a9127..004a9141, the same
					// fld/fsub/fstp triples); written field by field no expansion runs before the orientation product, so the product's expansions
					// pop the same free-list ids as in f3358eda's swapped statement order (which this replaces, with retail's instruction order
					// back) and its last row sums (X + Z) + Y as retail's does (004a92c8, 004a92ed; origin/main reproduces them, 8f94dea6 did not).
					// Without it (`em->mPos -= part->mOffsetPos`; at 9970fe15): CMesh::Load 9256 bytes off, natural 255/2025/9274, 193 calls, strict ledger lost 4 (trees 53, 56-58).
					em->mPos.X -= part->mOffsetPos.X;
					em->mPos.Y -= part->mOffsetPos.Y;
					em->mPos.Z -= part->mOffsetPos.Z;
					em->mOrientation = em->mOrientation * part->mOrientation.Transpose();
					// em->mMeshPart, not part: retail reads it again for the call (004a935d), because the orientation copy into *em killed
					// the CSE; through part VC6 keeps the pointer in a stack slot across the block.
					em->mTrackingVertex = em->mMeshPart->FindVertex(em->mPos);
				}
			}
		}

		BOOL	more = file.IsMoreData();
		if (!more)
			mNumAnimModes = 0;
		else
		{
			char	aname[17];
			aname[16] = 0;
			file.Read(&mNumAnimModes, 4);
			if (mNumAnimModes)
			{
#line 776
				mAnimModes = new(MEMTYPE_MESH) CAnimMode[mNumAnimModes];
				CAnimMode	*anim = mAnimModes;
				for (SINT i = 0; i < mNumAnimModes; anim++, i++)
				{
					anim->mMode = AM_INVALID;
					file.Read(aname, 16);
					file.Read(&anim->mStart, 4);
					file.Read(&anim->mLast, 4);
					SINT	rate;
					file.Read(&rate, 4);
					CheckForAnimations(aname, anim);
					anim->mFrameScaler = anim->mLast - anim->mStart;
					BOOL	loops = AnimModeLoops(anim->mMode);
					if (loops)
						anim->mFrameScaler++;
					SINT	n = anim->mFrameScaler;
					if (n)
					anim->mRate = (float)fabs(1.0f / ((float)n * rate));
					else
						anim->mRate = 0;
				}
			}
		}

		mParts[0]->HierarchyAdjust(ZERO_FVECTOR, ID_FMATRIX, NULL);
	}
	else if (version >= MKID("2.04"))
	{
		// EXPLORATION (2026-10-14): num_layers declared in this branch (tried at the head: "every offset above it moves"): the old
		// format's i and num_sets no longer conflict with it, which gives retail's class 116 {i, num_sets, num_layers} and frees num_frames/T74208.
		// Declared at the function head instead (at 9970fe15): CMesh::Load 5811 bytes off (every frame slot above it moves), natural 0/1428/5829, ledger unchanged.
		SINT	num_layers;
		SINT	c0, c1;		// Stuart's branch-scope counters (InitFromMem 482); the 1/3 key loop counts with c1
		// STAND-IN (dead padding, an authorized concession for Load's last rows): this statement and three more
		// (two `(void)(ID_FMATRIX * ZERO_FVECTOR);` after the box's extremes are declared, `(void)(ori * za);` after corners[7]) are inline operator expansions
		// whose results nothing reads. They compile to no code and exist for the ids the compiler spends on them: C2 numbers the temporaries and field
		// symbols of every expansion before it removes the dead ones. Against e1b9eb2f (thirteen-block typed new) they move the pass-9 field symbols, the late
		// matrix symbols and every value id after them by 224 (seven 32-id blocks) while the box's X (pass-2) field symbols move by 48, with the typed new at its
		// single `{ }` (MemoryManager.h; K=1..12 flips one inline site, the thirteen blocks seventeen). Measured with the rank dump, that supplies:
		//  - the box corners' Y/Z fields 176 ids later against their X fields (retail's operand orders need 56..188): the six rows c4 r1-2, c5 r1-2,
		//    c6 r0, c7 r0 that were wrong (X added first where retail adds it last); all c4..c7 cells now equal retail's x87 values;
		//  - the key loops' induction temporaries t24e7 -> t25c7 (id mod 0x400 = 0x1c7: the 2456 loop's address add keeps the IND operand first, which
		//    needs <= 0x1ff) and t2523 -> t2603 (0x203: the 1/3 loop's wants the temporary first, 0x200 and up; the two sit 60 apart): the k13 and new-tail residues;
		//  - the frame slots and the other key-block orders of retail (natural score shape/m+r/bytes 67/113/8845 -> 0/28/27, frame 0x83c, 192 calls, all 256
		//    retail FP trees reproduced).
		// Each of the four is load-bearing. Without it, natural 0/28/27 becomes: this one 156/1114/7172 (forced regions k2456 26, k13 28, old-format 22, box 34
		// lines); one of the two after the extremes 155/1113/9172, neither 217/1517/11953; the one after corners[7] 164/1264/9171; all four 335/1763/11933.
		// Re-measured at 9970fe15 (check bytes off / strict-ledger trees lost on the natural object): 7142 / 9, 9142 / 8, 11907 / 9, 9141 / 8, all four 11887 / 12.
		// Any dead local (SINT or float, 1..16, at the head or the end of Load) breaks the cell at once (40/68/121 and worse): the symbol count mod 32 is fixed.
		// What retail's source held in their place is not knowable from the bytes (a line map: retail's Load has 1.6 times our lines; W2: about 70 more
		// import-time symbols than a minimal spelling), so these are placeholders for code that emits nothing.
		(void)(ZERO_FVECTOR + ZERO_FVECTOR);
		// Stuart's global data block (InitFromMem, mesh.cpp:773-783): his names and braces compile as before. His
		// `globaldatasize -= sizeof(numtexturelayers);` (a dead store) costs 29 bytes (14/48/43, one strict-ledger tree), so retail's text lacked it.
		if (version >= MKID("2.05"))
		{
			SINT	globaldatasize;
			file.Read(&globaldatasize, 4);
			if (globaldatasize > 0)
			{
				file.Read(&num_layers, 4);
			}
		}
		else
			num_layers = 1;

		// Stuart's spelling (InitFromMem 787-899): numt, the `== 0` compares, and his c0 as the texture, allocation and part
		// loops' counter with the children read through `SINT c` on c1. c0 costs one symbol id (it was unreferenced), which flips the 1/3 key
		// product's first element (004a6d51, trees 56-58: 109 bytes, 3 lost); the first file-texture arm's (A8) result stored directly, as Stuart writes all four,
		// gives that symbol back (0/28/27, object identical). One arm only: two 127 bytes, three 148 / 65 lost, the ScreenContents arm 123.
		// Measured and left out: c0 in the texture or allocation loop alone (15558 / 271 bytes), the 2.07 `frames = 1` hack with framesinfile.
		SINT	numt;
		file.Read(&numt, 4);
		mNumTextures = numt;
		if (numt)
		{
#line 841
			mTextures = new(MEMTYPE_MESHTEXTURE) CMeshTexture[numt];
			for (c0 = 0; c0 < numt; c0++)
			{
				file.Read(name, 256);
				mTextures[c0].InitDefault();
				sprintf(path, "meshtex\\%s", name);
				// Each arm stores its texture: retail's four tails (a load of mTextures and the store) are identical in
				// registers, so VC6 cross-jumps them after allocation, and the three deleted copies still take their
				// steps in the scratch-register rotation (a store through one local `tex` makes none; 004a5e19..004a5e86).
				if (stricmp(name, "ScreenContents.tga") == 0)
				{
					CDXTexture	*rtex = ENGINE.GetDefaultTexture();
					mTextures[c0].mTexture = rtex;
				}
				else if (strnicmp(name, "A8", 2) == 0)
				{
					mTextures[c0].mTexture = CDXTexture::FindTexture(path, TEXFMT_A8R8G8B8, 0, -1, 1, 1);
				}
				else if (strnicmp(name, "A4", 2) == 0)
				{
					CDXTexture	*rtex = CDXTexture::FindTexture(path, TEXFMT_A4R4G4B4, 0, -1, 1, 1);
					mTextures[c0].mTexture = rtex;
				}
				else
				{
					CDXTexture	*rtex = CDXTexture::FindTexture(path, TEXFMT_A1R5G5B5, 0, -1, 1, 1);
					mTextures[c0].mTexture = rtex;
				}
				if (version >= MKID("2.07"))
				{
					SINT	frames;
					file.Read(&frames, 4);
					mTextures[c0].SetNumFrames(1);
					mTextures[c0].mMaxBlend = 0;
					for (SINT f = 0; f < frames; f++)
					{
						float	alpha, u, v, w, h;
						file.Read(&alpha, 4);
						file.Read(&u, 4);
						file.Read(&v, 4);
						file.Read(&w, 4);
						file.Read(&h, 4);
						if (f < 1)
						{
							mTextures[c0].mTextureBlend[f] = alpha;
							// STAND-IN (a carrier, kept to reach 100% first): a one-trip loop around this store. The counter z is in both
							// indices, so the loop optimizer gets strength-reduction candidates here and the textures loop's induction temporaries are numbered later; the
							// header's `mTextures + i * 36` sums then come out in retail's order (retail adds the index first). The original has no such loop; the natural
							// review replaces it with the original's real structure. Without it (at 9970fe15): own 9793, CMesh::Load 9281 bytes off, natural 367/2093/9299, 193 calls, ledger lost 8;
							// with the own size restored by a dead chain: 8900 bytes, natural 59/107/8902, 192 calls.
							for (SINT z = 0; z < 1; z++)
								mTextures[c0 + z].mTextureU[f + z] = u;
							mTextures[c0].mTextureV[f] = v;
							mTextures[c0].mTextureUScale[f] = w;
							mTextures[c0].mTextureVScale[f] = h;
							if (alpha > mTextures[c0].mMaxBlend)
								mTextures[c0].mMaxBlend = alpha;
						}
					}
				}
				mTextures[c0].CreateVBuffer();
			}
		}

		file.Read(&mNumParts, 4);
#line 922
		mParts = new(MEMTYPE_MESH) CMeshPart *[mNumParts];
		if (!mParts)
			return FALSE;
		for (c0 = 0; c0 < mNumParts; c0++)
		{
#line 930
			mParts[c0] = new(MEMTYPE_MESH) CDXMeshPart;
			if (!mParts[c0])
				return FALSE;
		}

		CONSOLE.Status("Loading mesh parts");
		for (c0 = 0; c0 < mNumParts; c0++)
		{
			CMeshPart	*mp = mParts[c0];
			mp->mBaseMesh = this;
			mp->mPartNumber = c0;
			strcpy(mp->mName, "");
			file.Read(&mp->mPartType, 4);
			char	statusmessage[128];
			sprintf(statusmessage, "[%d%%] ", c0 * 100 / mNumParts);
			if (mp->mPartType == 1)
				strcat(statusmessage, "[normal]");
			if (mp->mPartType == 3)
				strcat(statusmessage, "[skeletal]");
			if (mp->mPartType == 2)
				strcat(statusmessage, "[null]");
			if (mp->mPartType == 4)
				strcat(statusmessage, "[camera]");
			if (mp->mPartType == 5)
				strcat(statusmessage, "[emitter]");
			if (mp->mPartType == 6)
				strcat(statusmessage, "[reference]");
			if (version >= MKID("2.05"))
				file.Read(mp->mName, 32);

			if ((mp->mPartType == 1) || (mp->mPartType == 3))
			{
				SINT	num_vertices, num_positions, num_frames, num_sets, num_triangles;
				file.Read(&num_vertices, 4);
				file.Read(&num_positions, 4);
				file.Read(&num_frames, 4);
				file.Read(&num_sets, 4);
				file.Read(&num_triangles, 4);
				if (!mp->InitMemory(num_vertices, num_positions, num_triangles, num_frames, num_sets))
					return FALSE;
				file.Read(&mp->mNumHFrames, 4);
#line 1003
				mp->mVHFrameMap = new(MEMTYPE_MESH) UBYTE[mp->mNumVFrames];
#line 1004
				mp->mHFrameOrientations = new(MEMTYPE_MESH) FMatrix[mp->mNumHFrames];
#line 1005
				mp->mHFramePositions = new(MEMTYPE_MESH) FVector[mp->mNumHFrames];
				mp->mHFrameFOVs = NULL;
				SINT	i;
				float	lVHFrameMap;
				for (i = 0; i < mp->mNumVFrames; i++)
				{
					file.Read(&lVHFrameMap, 4);
					SASSERT((lVHFrameMap < 256.i) && (lVHFrameMap >= 0.i), "VHFrameMap out of bounds!!");
					mp->mVHFrameMap[i] = (UBYTE)lVHFrameMap;
				}
				for (c1 = 0; c1 < mp->mNumHFrames; c1++)
				{
					// Stuart's declarations, together after the position read, and his names x..w, m, stm (InitFromMem 984-1083 and 1182-1270 in the other key block); object identical
					mp->mHFramePositions[c1].Read(&file);
					float	x, y, z, w;
					float	sx, sy, sz, sw;
					FVector	scale;
					file.Read(&x, 4);
					file.Read(&y, 4);
					file.Read(&z, 4);
					file.Read(&w, 4);
					scale.Read(&file);
					file.Read(&sx, 4);
					file.Read(&sy, 4);
					file.Read(&sz, 4);
					file.Read(&sw, 4);
					z = -z;
					float	xx, xy, xz, xw, yy, yz, yw, zz, zw;
					FMatrix	m;
					SetIdentity(m);
					xx = x * x; xy = x * y; xz = x * z; xw = x * w;
					yy = y * y; yz = y * z; yw = y * w;
					zz = z * z; zw = z * w;
					m.Row[0].X = 1.0f - 2.0f * (yy + zz);
					m.Row[0].Y = 2.0f * (xy - zw);
					m.Row[0].Z = 2.0f * (xz + yw);
					m.Row[1].X = 2.0f * (xy + zw);
					m.Row[1].Y = 1.0f - 2.0f * (xx + zz);
					m.Row[1].Z = 2.0f * (yz - xw);
					m.Row[2].X = 2.0f * (xz - yw);
					m.Row[2].Y = 2.0f * (yz + xw);
					m.Row[2].Z = 1.0f - 2.0f * (xx + yy);
					mp->mHFrameOrientations[c1] = m;
					sz = -sz;
					SetIdentity(m);
					xx = sx * sx; xy = sx * sy; xz = sx * sz; xw = sx * sw;
					yy = sy * sy; yz = sy * sz; yw = sy * sw;
					zz = sz * sz; zw = sz * sw;
					m.Row[0].X = 1.0f - 2.0f * (yy + zz);
					m.Row[0].Y = 2.0f * (xy - zw);
					m.Row[0].Z = 2.0f * (xz + yw);
					m.Row[1].X = 2.0f * (xy + zw);
					m.Row[1].Y = 1.0f - 2.0f * (xx + zz);
					m.Row[1].Z = 2.0f * (yz - xw);
					m.Row[2].X = 2.0f * (xz - yw);
					m.Row[2].Y = 2.0f * (yz + xw);
					m.Row[2].Z = 1.0f - 2.0f * (xx + yy);
					FMatrix	stm;
					SetIdentity(stm);
					stm.Row[0] *= scale.X;
					stm.Row[1] *= scale.Y;
					stm.Row[2] *= scale.Z;
					KeyOf(mp, c1) = KeyOf(mp, c1) * m.Inverse() * stm * m;
				}
				mp->mOrientation = mp->GetHFrameOrientation(0);
				mp->mOffsetPos = mp->GetKeyPosition(0);

				file.Read(&mp->mNumChildren, 4);
				if (mp->mNumChildren)
				{
#line 1144
					mp->mChildren = new(MEMTYPE_MESH) CMeshPart *[mp->mNumChildren];
				}
				else
					mp->mChildren = NULL;
				SINT	c;
			for (c1 = 0; c1 < mp->mNumChildren; c1++)
				{
					file.Read(&c, 4);
					if ((c >= 0) && (c < mNumParts))
						mp->mChildren[c1] = mParts[c];
					else
						mp->mChildren[c1] = NULL;
				}
				file.Read(&c, 4);
				if ((c >= 0) && (c < mNumParts))
					mp->mParent = mParts[c];
				else
					mp->mParent = NULL;
				mp->Read204(&file, this, mTextures, numt, 1, num_layers, version);
				SINT	end;
				file.Read(&end, 4);
			}
			else if ((mp->mPartType == 2) || (mp->mPartType == 4) || (mp->mPartType == 5) || (mp->mPartType == 6))
			{
				SINT	num_frames;
				file.Read(&num_frames, 4);
				if (!mp->InitMemory(0, 0, 0, num_frames, 0))
					return FALSE;
				file.Read(&mp->mNumHFrames, 4);
#line 1196
				mp->mVHFrameMap = new(MEMTYPE_MESH) UBYTE[mp->mNumVFrames];
#line 1197
				mp->mHFrameOrientations = new(MEMTYPE_MESH) FMatrix[mp->mNumHFrames];
#line 1198
				mp->mHFramePositions = new(MEMTYPE_MESH) FVector[mp->mNumHFrames];
				if (mp->mPartType == 4)
				{
#line 1207
					mp->mHFrameFOVs = new(MEMTYPE_MESH) float[mp->mNumHFrames];
				}
				else
					mp->mHFrameFOVs = NULL;
				SINT	i;
				float	lVHFrameMap;
				for (i = 0; i < mp->mNumVFrames; i++)
				{
					file.Read(&lVHFrameMap, 4);
					SASSERT((lVHFrameMap < 256.i) && (lVHFrameMap >= 0.i), "VHFrameMap out of bounds!!");
					mp->mVHFrameMap[i] = (UBYTE)lVHFrameMap;
				}
				// EXPLORATION (2026-10-14): this key loop counts with i (the one the frame loop above leaves in the block), not k: with k shared by
				// both key loops (w7) the packer sorts it before num_textures (w7) and takes its class; retail has num_textures with qz (slot 20) and k alone
				// (slot 172). No symbol is added or removed. On the shared k (at 9970fe15): CMesh::Load 12510 bytes off (the frame slots), natural 29/1187/12512, 192 calls, ledger lost 2.
				for (i = 0; i < mp->mNumHFrames; i++)
				{
					mp->mHFramePositions[i].Read(&file);
					float	x, y, z, w;
					float	sx, sy, sz, sw;
					FVector	scale;
					file.Read(&x, 4);
					file.Read(&y, 4);
					file.Read(&z, 4);
					file.Read(&w, 4);
					scale.Read(&file);
					file.Read(&sx, 4);
					file.Read(&sy, 4);
					file.Read(&sz, 4);
					file.Read(&sw, 4);
					if (mp->mPartType == 4)
					{
						float	fov;
						file.Read(&fov, 4);
						mp->mHFrameFOVs[i] = fov;
					}
					z = -z;
					float	xx, xy, xz, xw, yy, yz, yw, zz, zw;
					FMatrix	m;
					SetIdentity(m);
					xx = x * x; xy = x * y; xz = x * z; xw = x * w;
					yy = y * y; yz = y * z; yw = y * w;
					zz = z * z; zw = z * w;
					m.Row[0].X = 1.0f - 2.0f * (yy + zz);
					m.Row[0].Y = 2.0f * (xy - zw);
					m.Row[0].Z = 2.0f * (xz + yw);
					m.Row[1].X = 2.0f * (xy + zw);
					m.Row[1].Y = 1.0f - 2.0f * (xx + zz);
					m.Row[1].Z = 2.0f * (yz - xw);
					m.Row[2].X = 2.0f * (xz - yw);
					m.Row[2].Y = 2.0f * (yz + xw);
					m.Row[2].Z = 1.0f - 2.0f * (xx + yy);
					mp->mHFrameOrientations[i] = m;
					sz = -sz;
					SetIdentity(m);
					xx = sx * sx; xy = sx * sy; xz = sx * sz; xw = sx * sw;
					yy = sy * sy; yz = sy * sz; yw = sy * sw;
					zz = sz * sz; zw = sz * sw;
					m.Row[0].X = 1.0f - 2.0f * (yy + zz);
					m.Row[0].Y = 2.0f * (xy - zw);
					m.Row[0].Z = 2.0f * (xz + yw);
					m.Row[1].X = 2.0f * (xy + zw);
					m.Row[1].Y = 1.0f - 2.0f * (xx + zz);
					m.Row[1].Z = 2.0f * (yz - xw);
					m.Row[2].X = 2.0f * (xz - yw);
					m.Row[2].Y = 2.0f * (yz + xw);
					m.Row[2].Z = 1.0f - 2.0f * (xx + yy);
					FMatrix	stm;
					SetIdentity(stm);
					stm.Row[0] *= scale.X;
					stm.Row[1] *= scale.Y;
					stm.Row[2] *= scale.Z;
					// STAND-IN: private maths source-shape candidate; see MeshMathsPrivate.h.
					mp->GetHFrameOrientation(i) = (mp->GetHFrameOrientation(i) * m.Inverse()).MeshLoadSecondProduct(stm).MeshLoadCoupledProduct(m);
				}
				mp->mOrientation = mp->GetHFrameOrientation(0);
				mp->mOffsetPos = mp->GetKeyPosition(0);

				file.Read(&mp->mNumChildren, 4);
				if (mp->mNumChildren)
				{
#line 1353
					mp->mChildren = new(MEMTYPE_MESH) CMeshPart *[mp->mNumChildren];
				}
				else
					mp->mChildren = NULL;
				SINT	c;
			for (c1 = 0; c1 < mp->mNumChildren; c1++)
				{
					file.Read(&c, 4);
					if ((c >= 0) && (c < mNumParts))
						mp->mChildren[c1] = mParts[c];
					else
						mp->mChildren[c1] = NULL;
				}
				file.Read(&c, 4);
				if ((c >= 0) && (c < mNumParts))
					mp->mParent = mParts[c];
				else
					mp->mParent = NULL;
				if (mp->mPartType == 6)
				{
					SINT	ref;
					file.Read(&ref, 4);
					mp->mReferenceTo = mParts[ref];
				}
				mp->mBaseMesh = this;
				SINT	end;
				file.Read(&end, 4);
			}
			else
			{
				sprintf(name, "Unknown mesh part type %s (%d)\n", mp->mName, mp->mPartType);
				TRACE(name);
			}
		}
		CONSOLE.StatusDone("Loading mesh parts", TRUE);

		file.Read(&mNumEmitters, 4);
		if (mNumEmitters)
		{
#line 1412
			mEmitters = new(MEMTYPE_MESH) CEmitter[mNumEmitters];
			if (!mEmitters)
				return FALSE;
			for (i = 0; i < mNumEmitters; i++)
			{
				CEmitter	*e = &mEmitters[i];
				SINT	part;
				SINT	number;
				file.Read(name, 256);
				file.Read(&number, 4);
				file.Read(&part, 4);
				if ((part >= 0) && (part < mNumParts))
					e->mMeshPart = mParts[part];
				else
					e->mMeshPart = 0;
				e->mSubpartEmitter = 1;
				e->mPos.X = 0;
				e->mPos.Y = 0;
				e->mPos.Z = 0;
				strcpy(e->mName, name);
				e->mNumber = number;
				SetIdentity(e->mOrientation);
			}
		}

		if (!file.IsMoreData())
			mNumAnimModes = 0;
		else
		{
			char	animdesc[17];
			animdesc[16] = 0;
			file.Read(&mNumAnimModes, 4);
			if (mNumAnimModes)
			{
#line 1464
				mAnimModes = new(MEMTYPE_MESH) CAnimMode[mNumAnimModes];
				CAnimMode	*amode = mAnimModes;
				for (SINT i = 0; i < mNumAnimModes; amode++, i++)
				{
					// STAND-IN: n is initialised at the loop head (a redundant store, no code). Its first reference then comes before irate's,
					// so n is numbered before irate and `(float)irate * n` loads irate first (fild irate, fimul n: retail's 004a8807). There is no evidence beyond the
					// order; the natural review replaces it with the original's own order of first references. Without it (at 9970fe15): CMesh::Load 22 bytes off (+9 at 4a8807..4a880b), natural 0/32/36.
					SINT	n = 0;
					amode->mMode = AM_INVALID;
					file.Read(animdesc, 16);
					file.Read(&amode->mStart, 4);
					file.Read(&amode->mLast, 4);
					SINT	irate;
					file.Read(&irate, 4);
					strcpy(amode->mModeName, animdesc);
					CheckForAnimations(animdesc, amode);
					if (amode->mMode == AM_INVALID)
						amode->mMode = (EAnimMode)(i + 1000);
					amode->mFrameScaler = amode->mLast - amode->mStart;
					BOOL	loops = AnimModeLoops(amode->mMode);
					if (loops)
						amode->mFrameScaler++;
					n = amode->mFrameScaler;
					if (n)
					amode->mRate = (float)fabs(1.0f / ((float)irate * n));
					else
						amode->mRate = 0;
				}
			}
		}

		file.Read(&mNumSequences, 4);
		if (mNumSequences > 0)
		{
#line 1506
			mSequences = new(MEMTYPE_MESH) CCutsceneSequence[mNumSequences];
			for (SINT i = 0; i < mNumSequences; i++)
			{
				SINT	n;
				file.Read(&n, 4);
				file.Read(&mSequences[i].mStart, 4);
				file.Read(&mSequences[i].mEnd, 4);
				mSequences[i].mCamera = mParts[n];		// a direct read: retail's index load comes before the frame-part base (004a88be)
			}
		}

		if (file.IsMoreData())
		{
#line 1534
			mNextInChain = new(MEMTYPE_MESH) CMesh;
			if (!mNextInChain->InitFromMem(file, flags))
			{
				TRACE("Error loading chained mesh!");
				return FALSE;
			}
			mNextInChain->AddRef();
		}
	}
	else
		return FALSE;

	OptimiseTextures();
	for (i = 0; i < mNumParts; i++)
		mParts[i]->BuildChain();
	if (!gNoMeshPartOptimise)
		OptimiseParts();

	if (mParts)
	{
		for (i = 0; i < mNumParts; i++)
		{
			if (mParts[i])
			{
				mParts[i]->GenerateSimpleBoundingBox();
				if (mParts[i]->mPartType == 4)
				{
					// a camera: a small box
					mParts[i]->mBoundingBox->mOrigin.Set(0, 0, 0);
					mParts[i]->mBoundingBox->mAxes.Set(0.05f, 0.05f, 0.1f);
					mParts[i]->mBoundingBox->CalculateRadius();
				}
				if (mParts[i]->mPartType == 5)
				{
					// an emitter
					mParts[i]->mBoundingBox->mOrigin.Set(0, 0, 0);
					mParts[i]->mBoundingBox->mAxes.Set(0.1f, 0.1f, 0.1f);
					mParts[i]->mBoundingBox->CalculateRadius();
				}
			}
		}
	}
	for (i = 0; i < mNumParts; i++)
		mParts[i]->mMeshVB->InitDeviceObjects();
	for (i = 0; i < mNumParts; i++)
		mParts[i]->BuildCache();

	if (mParts)
	{
		// the mesh's box: every part's box at its rest pose
		// STAND-IN (id order): the six extremes are declared in the id order that gives retail's operand order in the origin sums (max loaded
		// first), with the stores in retail's order (minx miny minz maxx maxy maxz, 004a9700..004a9739). With the dead padding before the box the locals'
		// symbols start at slot 27 of a 32-id block, so a plain declaration (min x y z, max x y z) puts maxz in the next block and its hash below minz's
		// (minz first). Declaring minz after the maxes keeps every max hash above its min's; the maxes' first values are overwritten before any read, so the
		// zeros are dead stores and compile to nothing (plain declarations: natural 0/32/29, the z origin sum 4aa1af..4aa1b6 wrong; the check: 15 bytes off against 13).
		float	minx = 999999.f, miny = 999999.f;
		float	maxx = 0, maxy = 0, maxz = 0;
		float	minz = 999999.f;
		maxx = -999999.f;
		maxy = -999999.f;
		maxz = -999999.f;
		float	radius = 0;
		float	median = 0;
		// STAND-IN (dead padding; see the first one, in the 2.04 branch): two FMatrix * FVector expansions on the file's own constants that
		// nothing reads, after the extremes' declarations (before them they would move the locals' ids). With one of them natural is 155/1113/9172, with neither
		// 217/1517/11953 (the other two statements kept). Any of the three early statements may sit elsewhere before corners[0] (the 2.04 head, the old
		// branch's end, the function head, before OptimiseTextures()) with the same result; after the 1/3 chain (line 892) the shape is 14 and the bytes 56.
		(void)(ID_FMATRIX * ZERO_FVECTOR);
		(void)(ID_FMATRIX * ZERO_FVECTOR);
		mBoundingBox->mValid = FALSE;
		for (i = 0; i < mNumParts; i++)
		{
			if (!mParts[i])
				continue;
			CMeshPart	*datapart = mParts[i]->GetMeshDataPart();
			if ((!datapart) || (!datapart->mBoundingBox->mValid) || (datapart->mPartType == 5))
				continue;
			FMatrix	ori;
			FVector	pos;
			mParts[i]->GetWorldTransforms(0, 0, 0, NULL, &ori, &pos, false, true, false);
			// The box (from retail's x87 webs): three axis vectors with zero components, xa = (X, 0, 0), ya = (0, Y, 0), za = (0, 0, Z), and each corner
			// is pos + v + ori * (+-xa +-ya +-za) in retail's sign order (+++ ++- +-- +-+ -++ -+- --- --+). The zero components fold late, after the expressions are shared,
			// which makes the Y, Z, -Y and -X webs, the X duplicate into corner 1 and the -Z carried from corner 1 to corner 6. corners and the counter c are declared after o (a placement
			// found by search over where corners, c, xa, ya, za and o go; ahead of box instead: 17 bytes off against 13, at 9970fe15): the symbol numbers of the locals around the box decide how the IND(box) products of
			// v = ori * box->mOrigin rank (the box pointer's number must be 6 or 7 mod 8 in 0x800..0xfff), and so the order of each row of o's sum.
			CBoundingBox	*box = datapart->mBoundingBox;
			FVector	xa = FVector(box->mAxes.X, 0, 0);
			FVector	ya = FVector(0, box->mAxes.Y, 0);
			FVector	za = FVector(0, 0, box->mAxes.Z);
			FVector	v = ori * box->mOrigin;
			FVector	vp[8];
			vp[0] = pos + v + ori * (xa + ya + za);
			vp[1] = pos + v + ori * (xa + ya - za);
			vp[2] = pos + v + ori * (xa - ya - za);
			vp[3] = pos + v + ori * (xa - ya + za);
			vp[4] = pos + v + ori * (-xa + ya + za);
			vp[5] = pos + v + ori * (-xa + ya - za);
			vp[6] = pos + v + ori * (-xa - ya - za);
			vp[7] = pos + v + ori * (-xa - ya + za);
			// STAND-IN (dead padding; see the first one, in the 2.04 branch): an FMatrix * FVector expansion nothing reads. It sits after the
			// corner X fields were numbered, so everything after it (the Y/Z fields, the late matrix, the values) moves and the X fields do not; placed before
			// vp[4] it moves the X fields too and the rows break again. Without it: natural 164/1264/9171, box 19 lines, k2456 52, k13 65.
			(void)(ori * za);
			for (int j = 0; j < 8; j++)
			{
				if (vp[j].X < minx)
					minx = vp[j].X;
				if (vp[j].Y < miny)
					miny = vp[j].Y;
				if (vp[j].Z < minz)
					minz = vp[j].Z;
				if (vp[j].X > maxx)
					maxx = vp[j].X;
				if (vp[j].Y > maxy)
					maxy = vp[j].Y;
				if (vp[j].Z > maxz)
					maxz = vp[j].Z;
				float	r = vp[j].MagnitudeSq();
				if (r > radius)
					radius = r;
			}
			if (mParts[i]->mCueFactor > median)
				median = mParts[i]->mCueFactor;
			mBoundingBox->mValid = TRUE;
		}
		mBoundingBox->mOrigin = FVector(maxx + minx, maxy + miny, maxz + minz) * 0.5f;
		mBoundingBox->mAxes = FVector(minx - maxx, miny - maxy, minz - maxz) * 0.5f;
		float	ax = mBoundingBox->mAxes.X;
		if (ax <= 0)
			ax = -ax;
		mBoundingBox->mAxes.X = ax;
		float	ay = mBoundingBox->mAxes.Y;
		if (ay <= 0)
			ay = -ay;
		mBoundingBox->mAxes.Y = ay;
		float	az = mBoundingBox->mAxes.Z;
		if (az <= 0)
			az = -az;
		mBoundingBox->mAxes.Z = az;
		mRadius = (float)sqrt(radius);
		if (!mBoundingBox->mValid)
		{
			mBoundingBox->mOrigin.Set(0, 0, 0);
			mBoundingBox->mAxes.Set(0, 0, 0);
			mRadius = 0;
		}
		mBoundingBox->CalculateRadius();
		mCueFactor = median;

		if (mParts)
			for (i = 0; i < mNumParts; i++)
				mParts[i]->GetWorldTransforms(0, 0, 0, NULL, &mParts[i]->mBaseOrientation, &mParts[i]->mBasePosition, true, false, false);
	}
	return TRUE;
}

//******************************************************************************************
// A mesh texture by its path ("meshtex\\" and the name); names starting A8 or A4 pick a texture format.
CDXTexture *CMesh::FindTexture(char *path)	// FUNCTION: 0x004aa410
{
	CDXTexture	*tex = NULL;
	char		*name = path + 8;
	if (strlen(name) != 0)
	{
		if (strnicmp(name, "A8", 2) == 0)
			tex = CDXTexture::FindTexture(path, 4, 0, -1, 1, 1);
		else if (strnicmp(name, "A4", 2) == 0)
			tex = CDXTexture::FindTexture(path, 2, 0, -1, 1, 1);
		else
			tex = CDXTexture::FindTexture(path, 1, 0, -1, 1, 1);
	}
	else
	{
		char	message[256];
		sprintf(message, "Warning! Mesh contains a null texture name!\n");
		TRACE(message);
	}
	return tex;
}

//******************************************************************************************
SINT CMesh::GetNumEmitters()	// FUNCTION: 0x004aa4e0
{
	if (mNextInChain)
		return mNextInChain->GetNumEmitters() + mNumEmitters;
	return mNumEmitters;
}

//******************************************************************************************
void CMesh::GetEmitterNameByID(SINT n, char *name, SINT *id)	// FUNCTION: 0x004aa500
{
	if (n < mNumEmitters)
	{
		strcpy(name, mEmitters[n].mName);
		*id = mEmitters[n].mNumber;
	}
	else if (mNextInChain)
		mNextInChain->GetEmitterNameByID(n - mNumEmitters, name, id);
	else
	{
		strcpy(name, "");
		*id = -1;
	}
}

//******************************************************************************************
CMeshPart *CMesh::GetEmitterSubpartByID(SINT n)	// FUNCTION: 0x004aa5a0
{
	if (n < mNumEmitters)
		return mEmitters[n].mMeshPart;
	if (mNextInChain)
		return mNextInChain->GetEmitterSubpartByID(n - mNumEmitters);
	return 0;
}

//******************************************************************************************
CMeshPart *CMesh::GetMovieCameraSubpart(SINT frame)	// FUNCTION: 0x004aa5e0
{
	for (SINT i = 0; i < mNumSequences; i++)
		if ((mSequences[i].mStart <= frame) && (mSequences[i].mEnd >= frame))
			return mSequences[i].mCamera;
	if (mNextInChain)
		return mNextInChain->GetMovieCameraSubpart(frame);
	return NULL;
}

//******************************************************************************************
EAnimMode CMesh::GetAnimModeByName(char *name)	// FUNCTION: 0x004aa630
{
	CAnimMode	*anim = mAnimModes;
	for (SINT i = 0; i < mNumAnimModes; anim++, i++)
		if (!stricmp(name, anim->mModeName))
			return anim->mMode;
	return AM_INVALID;
}

//******************************************************************************************
CAnimMode *CMesh::GetAnimModePtr(EAnimMode mode)	// FUNCTION: 0x004aa680
{
	CAnimMode	*anim = mAnimModes;
	for (SINT i = 0; i < mNumAnimModes; anim++, i++)
		if (anim->mMode == mode)
			return anim;
	return NULL;
}

//******************************************************************************************
char *CMesh::GetName()	// FUNCTION: 0x004aa6b0
{
	for (CMesh *m = sFirst; m; m = m->mNextMesh)
		if (m == this)
			return m->mName;
	return "unknown mesh name";
}

//******************************************************************************************
#line 1865
CMesh *CMesh::GetMesh(char *name, SINT flags)	// FUNCTION: 0x004aa6e0
{
	CMesh	*m;
	for (m = sFirst; m; m = m->mNextMesh)
	{
		if (!stricmp(name, m->mName))
		{
			m->AddRef();
			return m;
		}
	}

	m = new(MEMTYPE_MESH) CMesh;
	if (m)
	{
		if (m->InitFromFile(name, flags))
		{
			if (sReportMissing)
				LOG.AddMessage("Mesh '%s' not found in level resource file", name);
			m->AddRef();
			return m;
		}
		delete m;
	}
	return NULL;
}

//******************************************************************************************
float CMesh::GetFrameIncrement(EAnimMode am, int *realindex)	// FUNCTION: 0x004aa7e0
{
	CAnimMode	*anim = mAnimModes;
	for (SINT i = 0; i < mNumAnimModes; anim++, i++)
	{
		if (anim->mMode == am)
		{
			*realindex = i;
			return anim->mRate;
		}
	}
	*realindex = -1;
	return 0;
}

//******************************************************************************************
CMeshPart *CMesh::GetEmitterSubpart(char *name, SINT id)	// FUNCTION: 0x004aa820
{
	for (SINT i = 0; i < mNumEmitters; i++)
		if ((!stricmp(mEmitters[i].mName, name)) && (mEmitters[i].mNumber == id))
			return mEmitters[i].mMeshPart;
	if (mNextInChain)
		return mNextInChain->GetEmitterSubpart(name, id);
	return 0;
}

//******************************************************************************************
CMeshPart *CMesh::GetMeshSubpart(char *name)	// FUNCTION: 0x004aa8a0
{
	for (SINT i = 0; i < mNumParts; i++)
		if (!stricmp(mParts[i]->mName, name))
			return mParts[i];
	return NULL;
}

//******************************************************************************************
void CMesh::BuildAllPolyBuckets()	// FUNCTION: 0x004aa900
{
	if (mParts)
		for (SINT i = 0; i < mNumParts; i++)
			mParts[i]->BuildPolyBucket();
}

//******************************************************************************************
// A random point on a random collision part, the parts weighted by their bounding radii.
FVector CMesh::GetRandomVertexPos(CMeshPart **part)	// FUNCTION: 0x004aa940
{
	SINT	tries = 0;
	if (mNumParts == 0)
	{
		LOG.AddMessage("WARNING: trying to get random vertex pos on a mesh that has no vertices");
		return FVector(0, 0, 0);
	}

	if (mNumParts < 16)
	{
		CMeshPart	*collision;
		SINT		n = 15;
		do
		{
			*part = mParts[GAME.Random() % mNumParts];
			collision = (*part)->GetMeshDataPart();
			n--;
		}
		while (((!collision) || (collision->mPartType != 1)) && (n > 0));

		if (n == 0)
			return FVector(0, 0, 0);
		return collision->GetRandomVertexPos();
	}

	CMeshPart	*parts[150];
	float		weights[150];
	float		total = 0;
	SINT		count = 0;
	SINT		i;
	for (i = 0; i < mNumParts; i++)
	{
		*part = mParts[i];
		if (((*part)->GetMeshDataPart()) && ((*part)->GetMeshDataPart()->mPartType == 1) && ((*part)->mBoundingBox))
		{
			if (count < 150)
			{
				parts[count] = *part;
				weights[count] = (*part)->mBoundingBox->mRadius;
				total += weights[count];
				count++;
			}
			else
				LOG.AddMessage("WARNING: Not enough mp allocated to calc random mp");
		}
	}

	float	sum = 0;
	for (i = 0; i < count; i++)
	{
		weights[i] /= total;
		sum += weights[i];
	}

	for (; tries < 100; tries++)
	{
		float	r = GAME.FloatRandom();
		for (SINT j = 0; j < count; j++)
		{
			SINT	k = GAME.Random() % count;
			CMeshPart	*chosen = parts[k];	*part = chosen;		// a local read twice: retail keeps the part in a register
			if (weights[k] >= r)
				return chosen->GetRandomVertexPos();
		}
	}

	if (count > 0)
		return parts[0]->GetRandomVertexPos();

	LOG.AddMessage("WARNING: trying to get random vertex pos on a mesh that has no vertices");
	return FVector(0, 0, 0);
}

// (CDXMeshPart, Stuart's CMESHPART, is declared in DXMeshPart.h.)

//******************************************************************************************
// A mesh from the level resources: its header from c, its data from data (the same reader, or the mesh's own
// resource file when one is named and none is open yet). Meshes the game does not want are skipped.
// Matched with two labelled stand-ins.
// - Natural: retail has no count local: its part loops re-read mNumParts (004ab09f, 004ab0fc, 004ab106); the bounding
//   box is cleared before next is read (retail's store order at 004aac15).
// - Stuart's `new CMESHPART` (CMESHPART = CDXMeshPart): the parts are allocated as CDXMeshPart (DXMeshPart.h), for retail's raw new pointer.
// - STAND-IN 2: the frame parts' test is spelled `0 < mesh->mNumSequences`. It compiles to the same `test; jle` as
//   `> 0` but numbers the frame-part address temporary one higher, which puts the index first in retail's
//   [edi+ecx] at 004ab19e and 004ab1cb (`> 0` gives [ecx+edi]; `!= 0` also fixes it but tests with je).
#line 2429
CMesh *CMesh::Deserialize(CChunkReader *c, CChunkReader *data)	// FUNCTION: 0x004aab90
{
	if (!data)
		data = c;

	CMesh			*mesh = new(MEMTYPE_MESH) CMesh;
	CBoundingBox	*box = mesh->mBoundingBox;
	mesh->mBoundingBox = NULL;
	CMesh			*next = mesh->mNextMesh;

	c->GetNext();
	data->GetNext();

	char	name[300];
	char	text[256];
	SINT	runtime_id;
	SINT	optional;
	c->Read(name, 300, 1);
	c->Read(&runtime_id, 4, 1);
	c->Read(&optional, 4, 1);
	if ((optional == 1) && (!GAME.DoWeWantMesh(name)))
	{
		sprintf(text, "Skipping deserialisation of mesh %s as it is unnecessary\n", name);
		TRACE(text);
		data->Skip();
		if (box)
			delete box;
		return NULL;
	}

	bool	keep_buckets;
	c->Read(&keep_buckets, 1, 1);

	if ((!sReadingMeshFile) && (name[0]))
	{
		char	filename[256];
		sprintf(filename, "data\\resources\\meshes\\m_%s.aya", name);
#line 2508
		data = new(MEMTYPE_UNKNOWN) CChunkReader;
		if (!data->Open(filename))
		{
			CONSOLE.FatalResourceFileError(filename);
			delete data;
			return NULL;
		}
		sReadingMeshFile = TRUE;
	}

	data->GetNext();
	data->Read(mesh, sizeof(CMesh), 1);
	mesh->mResourceID = runtime_id;
	mesh->mNextMesh = next;

	if (mesh->mNumAnimModes > 0)
#line 2551
		mesh->mAnimModes = new(MEMTYPE_MESH) CAnimMode[mesh->mNumAnimModes];
	else
		mesh->mAnimModes = NULL;

	if (mesh->mNumEmitters > 0)
#line 2556
		mesh->mEmitters = new(MEMTYPE_MESH) CEmitter[mesh->mNumEmitters];
	else
		mesh->mEmitters = NULL;

	sEmitterBytes += mesh->mNumEmitters * sizeof(CEmitter);
	TRACE("%dK total in emitters so far\n", sEmitterBytes / 1024);

	if (mesh->mNumTextures > 0)
#line 2565
		mesh->mTextures = new(MEMTYPE_MESH) CMeshTexture[mesh->mNumTextures];
	else
		mesh->mTextures = NULL;

	if (mesh->mNumSequences > 0)
#line 2570
		mesh->mSequences = new(MEMTYPE_MESH) CCutsceneSequence[mesh->mNumSequences];
	else
		mesh->mSequences = NULL;

	data->GetNext();
	data->Read(mesh->mTextures, sizeof(CMeshTexture), mesh->mNumTextures);

	SINT	i;
	for (i = 0; i < mesh->mNumTextures; i++)
	{
		data->GetNext();
		mesh->mTextures[i].mTextureBlend = NULL;
		mesh->mTextures[i].mTextureU = NULL;
		mesh->mTextures[i].mTextureV = NULL;
		mesh->mTextures[i].mTextureUScale = NULL;
		mesh->mTextures[i].mTextureVScale = NULL;
		mesh->mTextures[i].mVBuffer = NULL;
		mesh->mTextures[i].SetNumFrames(mesh->mTextures[i].mFrames);
		data->GetNext();
		data->Read(mesh->mTextures[i].mTextureBlend, 4, mesh->mTextures[i].mFrames);
		data->Read(mesh->mTextures[i].mTextureU, 4, mesh->mTextures[i].mFrames);
		data->Read(mesh->mTextures[i].mTextureV, 4, mesh->mTextures[i].mFrames);
		data->Read(mesh->mTextures[i].mTextureUScale, 4, mesh->mTextures[i].mFrames);
		data->Read(mesh->mTextures[i].mTextureVScale, 4, mesh->mTextures[i].mFrames);

		data->Read(text, 128, 1);
		mesh->mTextures[i].mTexture = FindTexture(text);
		mesh->mTextures[i].mVBuffer = NULL;
		mesh->mTextures[i].CreateVBuffer();
	}

#line 2640
	mesh->mParts = new(MEMTYPE_MESH) CMeshPart *[mesh->mNumParts];
	for (i = 0; i < mesh->mNumParts; i++)
#line 2648
		mesh->mParts[i] = new(MEMTYPE_MESH) CDXMeshPart;

	for (i = 0; i < mesh->mNumParts; i++)
	{
		CMeshPart::Deserialize(data, mesh->mParts[i], mesh);
		if (!keep_buckets)
			SAFE_DELETE(mesh->mParts[i]->mPolyBucket);
	}

	if (0 < mesh->mNumSequences)		// STAND-IN 2 (see the function comment)
	{
		data->GetNext();
		data->Read(mesh->mSequences, sizeof(CCutsceneSequence), mesh->mNumSequences);
		for (i = 0; i < mesh->mNumSequences; i++)
		{
			if (mesh->mSequences[i].mCamera)
			{
				data->GetNext();
				SINT	part;
				data->Read(&part, 4, 1);
				mesh->mSequences[i].mCamera = mesh->mParts[part];
			}
		}
	}

	if (mesh->mNumAnimModes > 0)
	{
		data->GetNext();
		data->Read(mesh->mAnimModes, sizeof(CAnimMode), mesh->mNumAnimModes);
	}

	data->GetNext();
	mesh->mBoundingBox = CMeshPart::ReadBoundingBox(data, box);

	if (mesh->mNumEmitters > 0)
	{
		data->GetNext();
		SINT	size;
		data->Read(&size, 4, 1);
		if (size != sizeof(CEmitter))
			TRACE("Size=%d, sizeof=%d\n", size, sizeof(CEmitter));
		for (i = 0; i < mesh->mNumEmitters; i++)
		{
			data->Read(&mesh->mEmitters[i], size, 1);
			if (mesh->mEmitters[i].mMeshPart)
			{
				SINT	part;
				data->Read(&part, 4, 1);
				mesh->mEmitters[i].mMeshPart = mesh->mParts[part];
			}
		}
	}

	if (mesh->mNextInChain)
	{
		mesh->mNextInChain = Deserialize(c, data);
		mesh->mNextInChain->AddRef();
	}
	mesh->mRefCount = 0;

	if (sReadingMeshFile)
	{
		data->Close();
		delete data;
		sReadingMeshFile = FALSE;
	}
	return mesh;
}

//******************************************************************************************
CMesh *CMesh::GetMeshByResourceID(SINT id)	// FUNCTION: 0x004ab330
{
	for (CMesh *m = sFirst; m; m = m->mNextMesh)
		if (m->mResourceID == id)
			return m;
	return NULL;
}

static SINT	sPartsSeen;			// 00704af0: every part OptimiseParts has looked at
static SINT	sPartsRemoved;		// 00704af4

//******************************************************************************************
// The parts optimiser: static parts one is an ancestor of the other merged into one, then empty parts (the merged-away
// ones) removed with their children moved up to the parent, until nothing changes.
// Matched with one labelled stand-in (was 6/26, then 0/18, then 0/6): a
// child key's position is the removed part's position plus its orientation times the key (retail computes the key's
// address before loading mParts[i] at 004ab808, which the translation-first sum gives) and the orientation product is
// the plain M*M; its this and m then come from the pool-A free list as 0x1e and 0x2a3 (retail's operand ranks need
// this & 7 in {5, 6}, m & 7 = 3 and m > this; a simulator), and the root branch reuses the loop's child
// pointer and c.
// Retail's frame (+0x14 the first loop's j and the children counter, +0x24 done and the key counter; a variable map) has
// the children loop count with j and the keys with m. j * 4 is then one CSE expression with the first loop's, which
// numbers `child + 200`'s address temporary one lower (t5ad to t5ac: its IND key falls from 0x4007 to 0x0007 and the
// key sums turn round, the rank tool); the removal loop as a guarded do-while takes one id more before it (t55a to t55b)
// with the same code. A for loop's test written `mNumParts > i` did that too, but flipped the cmp.
// STAND-IN: the root arm reads mParts[0] through `if (!parent) parent = mParts[0];`. parent is
// always null there (the else of `if (parent)`), so the test is folded before register allocation and emits no code,
// but the conditional assignment keeps parent as a register web for mParts[0] (coloured eax before the scratch
// chooser) where a plain read, or plain `parent = mParts[0];`, is forward-substituted into a chooser temporary.
// Retail's root arm (004abc5c `mov eax,[ecx]`, `mov edx,[eax+0x94]`, `mov edx,[edx]`) needs the chooser cursor on edx
// at the mChildren load, where the parent arm's last pick (its removal loop's second mParts read, ecx) leaves it; the
// plain form spends one rotation on mParts[0] (pick 90 of 101, a C2 chooser log) and swaps eax and edx in those three
// instructions. Its other spellings (GetPart, dereferences, casts, -= and prefix decrements in either removal loop,
// 1 + c) leave them unchanged; the arm swap changes the layout; interleaving the root arm between the parent arm's
// copy loops (re-tested parent) gives retail's layout but recolours mParts; a new local for mParts[0] (two-definition
// ternary) fixes the root arm but its symbol shifts the key products' operand ranks (004ab8c8). Also giving retail's
// bytes: `parent = (parent) ? parent : mParts[0];` and `child = (parent) ? mParts[0] : mParts[0];` before
// `child = child->mChildren[0];`. Which source Stuart wrote is unknown.
void CMesh::OptimiseParts()	// FUNCTION: 0x004ab360
{
	char	str[256];
	sPartsSeen += mNumParts;
	sprintf(str, "Optimising mesh %s parts\n", mName);
	TRACE(str);

	SINT	i, j;
	for (i = 0; i < mNumParts; i++)
	{
		for (j = 0; j < mNumParts; j++)
		{
			if ((GetPart(i)->mPartType == 1) && (GetPart(j)->mPartType == 1) && (i != j))
			{
				BOOL	ok;
				if ((mParts[i]->mNumHFrames == 1) || (mParts[j]->mNumHFrames == 1))
				{
					// one must be the other's ancestor, through parts that are allowed to merge
					ok = FALSE;
					for (CMeshPart *a = mParts[i]; a; a = a->mParent)
					{
						for (CMeshPart *b = mParts[j]; b; b = b->mParent)
						{
							if (a == b)
								ok = TRUE;
							if ((!CMotionController::CanMergePart(b)) || (b->mNumHFrames > 1))
								break;
						}
						if ((!CMotionController::CanMergePart(a)) || (a->mNumHFrames > 1))
							break;
					}
				}
				else
					ok = FALSE;

				if (mParts[i]->mIsChained)
					ok = FALSE;
				if (mParts[j]->mIsChained)
					ok = FALSE;
				if (mParts[i]->mNumAFrames != 1)
					ok = FALSE;
				if (mParts[j]->mNumAFrames != 1)
					ok = FALSE;
				for (SINT k = 0; k < mNumParts; k++)
				{
					if (mParts[k]->mPartType == 6)
					{
						if (mParts[k]->mReferenceTo == mParts[i])
							ok = FALSE;
						if (mParts[k]->mReferenceTo == mParts[j])
							ok = FALSE;
					}
					if (mParts[k]->mPartType == 3)
					{
						for (SINT b = 0; b < mParts[k]->mNumBones; b++)
						{
							if (mParts[k]->mBones[b] == mParts[i])
								ok = FALSE;
							if (mParts[k]->mBones[b] == mParts[j])
								ok = FALSE;
						}
					}
				}
				if (!CMotionController::CanOptimisePart(mParts[j]))
					ok = FALSE;
				if (!CMotionController::CanMergePart(mParts[i]))
					ok = FALSE;
				if (!stricmp(mParts[i]->mName, "Nexus"))
					ok = FALSE;
				if ((stricmp(mParts[j]->mName, "Nexus")) && (ok))
				{
					mParts[i]->MergeVertices(mParts[j]);
					mParts[j]->mNumPVertices = 0;
					mParts[j]->mNumDVertices = 0;
					mParts[j]->mNumTriangles = 0;
					mParts[j]->mPartType = 2;
				}
			}
		}
	}

	BOOL	done;
	do
	{
		done = TRUE;
		i = 0;
		if (mNumParts > 0)
		do
		{
			if ((mParts[i]->mPartType != 2) || ((!mParts[i]->mParent) && (mParts[i]->mNumChildren != 1)))
				continue;

			BOOL	ok = TRUE;
			if (mParts[i]->mIsChained)
				ok = FALSE;
			SINT	k;
			for (k = 0; k < mNumParts; k++)
			{
				if ((mParts[k]->mPartType == 6) && (mParts[k]->mReferenceTo == mParts[i]))
					ok = FALSE;
				if (mParts[k]->mPartType == 3)
				{
					for (SINT b = 0; b < mParts[k]->mNumBones; b++)
						if (mParts[k]->mBones[b] == mParts[i])
							ok = FALSE;
				}
			}
			if (!stricmp(mParts[i]->mName, "Nexus"))
				ok = FALSE;
			if ((mParts[i]->mNumHFrames > 1) && (mParts[i]->mNumChildren > 0))
				ok = FALSE;
			for (k = 0; k < mNumEmitters; k++)
				if (mEmitters[k].mMeshPart == mParts[i])
					ok = FALSE;
			if (!CMotionController::CanOptimisePart(mParts[i]))
				ok = FALSE;
			if ((!mParts[i]->mParent) && (CMotionController::HasMovingParts(this)))
				continue;
			if (!ok)
				continue;

			char	msg[256];
			sprintf(msg, "Removing part %s\n", mParts[i]->mName);
			TRACE(msg);

			// the children keep their place: their keys move into the removed part's frame
			SINT	c, m;
			CMeshPart	*child;
			for (j = 0; j < mParts[i]->mNumChildren; j++)
			{
				child = mParts[i]->mChildren[j];
				for (m = 0; m < child->mNumHFrames; m++)
				{
					child->mHFramePositions[m] = mParts[i]->mHFramePositions[0] + mParts[i]->mHFrameOrientations[0] * child->mHFramePositions[m];
					child->mHFrameOrientations[m] = mParts[i]->mHFrameOrientations[0] * child->mHFrameOrientations[m];
				}
			}
			for (c = 0; c < mParts[i]->mNumChildren; c++)
				mParts[i]->mChildren[c]->mParent = mParts[i]->mParent;

			CMeshPart	*parent = mParts[i]->mParent;
			if (parent)
			{
				// the part out of its parent's children, its own children in
				for (c = 0; c < parent->mNumChildren; c++)
				{
					if (parent->mChildren[c] == mParts[i])
					{
						for (m = c + 1; m < parent->mNumChildren; m++)
							parent->mChildren[m - 1] = parent->mChildren[m];
						parent->mNumChildren--;
						c--;
					}
				}
#line 3670
				CMeshPart	**children = new(MEMTYPE_MESH) CMeshPart*[parent->mNumChildren + mParts[i]->mNumChildren];
				for (m = 0; m < parent->mNumChildren; m++)
					children[m] = parent->mChildren[m];
				for (m = 0; m < mParts[i]->mNumChildren; m++)
					children[parent->mNumChildren + m] = mParts[i]->mChildren[m];
				SAFE_DELETE(parent->mChildren);
				parent->mChildren = children;
				parent->mNumChildren += mParts[i]->mNumChildren;

				for (m = i + 1; m < mNumParts; m++)
				{
					mParts[m - 1] = mParts[m];
					mParts[m - 1]->mPartNumber--;
				}
			}
			else
			{
				// the root: its only child takes its place
				if (!parent)		// STAND-IN (see the function comment): always true here, folds away
					parent = mParts[0];
				child = parent->mChildren[0];
				c = child->mPartNumber;
				mParts[0] = child;
				mParts[0]->mPartNumber = 0;
				for (m = c + 1; m < mNumParts; m++)
				{
					mParts[m - 1] = mParts[m];
					mParts[m - 1]->mPartNumber--;
				}
			}
			done = FALSE;
			sPartsRemoved++;
			mNumParts--;
			i--;
		} while (++i < mNumParts);
	} while (!done);

	sprintf(str, "OptimiseParts() removed %d of %d parts so far\n", sPartsRemoved, sPartsSeen);
	TRACE(str);
}
