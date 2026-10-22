// HeightField.cpp: base lifecycle and tiled signed-height sampling from the pristine retail bytes.
#include "common.h"
#include "heightfield.h"
#include "chunker.h"
#include "mathsconst.h"
#include "DebugLog.h"
#include "maptex.h"
#include "mixermap.h"
#include "staticshadows.h"

// FUNCTION: 0x0047e780 init:ZERO_FVECTOR
// FUNCTION: 0x0047e7a0 init:ID_FMATRIX

// FUNCTION: 0x0047e870
CHeightField::CHeightField()
{
	mUnknown20 = 0;
	mOwned24 = NULL;
	memset(mUnknown28, 0, sizeof(mUnknown28));
	mHeightData = NULL;
}

// FUNCTION: 0x0047e8a0
CHeightField::~CHeightField()
{
	SAFE_DELETE_ARRAY(mOwned24);
	SAFE_DELETE_ARRAY(mHeightData);
}

// The grid masks, then the 64-step colour ramp from the ambient/sun ratio to white (8.8 fixed point). Blue's
// byte is a BYTE cast: with it VC6 computes the divisor first as for red and green (inc ecx at 0047e978).
// FUNCTION: 0x0047e8e0
void CHeightField::Unknown47e8e0()
{
	mGridWidth = 1 << mGridShiftX;
	mGridHeight = 1 << mGridShiftY;
	mGridMaskX = mGridWidth - 1;
	mGridMaskY = mGridHeight - 1;
	mGridRowMask = mGridMaskY << mGridShiftX;

	DWORD	sun = mHFP.Sun;
	DWORD	ambient = mHFP.Ambient;
	SINT	red = (((ambient >> 16) & 0xff) << 8) / (((sun >> 16) & 0xfe) + 1);
	SINT	green = (ambient & 0xff00) / (((sun >> 8) & 0xfe) + 1);
	SINT	blue = ((BYTE)ambient << 8) / ((sun & 0xfe) + 1);
	SINT	stepRed = 0xff - red;
	SINT	stepGreen = 0xff - green;
	SINT	stepBlue = 0xff - blue;
	red <<= 8;
	green <<= 8;
	blue <<= 8;
	stepRed <<= 2;
	stepGreen <<= 2;
	stepBlue <<= 2;
	for (SINT i = 0; i < 64; i++)
	{
		mColors[i].red = (red & 0xffffff00) << 8;
		mColors[i].green = (green & 0xffffff00) << 3;
		mColors[i].blue = (blue >> 3) & 0xffffffe0;
		red += stepRed;
		green += stepGreen;
		blue += stepBlue;
	}
	mColors[64] = mColors[63];
}

// FUNCTION: 0x0047ea20
SWORD __fastcall CHeightField::GetHeightSample(SINT x, SINT y)
{
	if (((x | y) & 0x3ffe00) == 0)
		return mHeightData[((((x >> 3) & 63) * 64 + ((y >> 3) & 63)) * 9 + (y & 7)) * 9 + (x & 7)];
	if ((x & 0x3ffe00) == 512)
	{
		if ((y & 0x3ffe00) == 512)
			return mHeightData[64 * 64 * 9 * 9 - 1];
		// Retail 0047ea98 shifts y by EIGHT here, unlike the interior tile coordinate.
		return mHeightData[63 * 64 * 9 * 9 + 8 + (((y >> 8) & 63) * 9 + (y & 7)) * 9];
	}
	if ((y & 0x3ffe00) == 512)
		return mHeightData[(((x >> 3) & 63) * 9 * 9) * 64 + (x & 7) + 63 * 9 * 9 + 8 * 9];
	return 0;
}

// FUNCTION: 0x0047eb00
SINT __fastcall CHeightField::Unknown47eb00(SINT packedX, SINT packedY)
{
	SINT x = packedX & 255;
	SINT y = packedY & 255;
	SWORD *sample = &mHeightData[((((packedX >> 11) & 63) * 64 + ((packedY >> 11) & 63)) * 9
								+ ((packedY >> 8) & 7)) * 9 + ((packedX >> 8) & 7)];
	SINT top = sample[0] + (((sample[1] - sample[0]) * x) >> 8);
	SINT bottom = sample[9] + (((sample[10] - sample[9]) * x) >> 8);
	return top + (((bottom - top) * y) >> 8);
}

// FUNCTION: 0x0047eb80
float __fastcall CHeightField::Collide(const FVector &pos)
{
	// The bias here is 3afffeb0, distinct from the map trace's 3afff2e5.
	float packedX = pos.X - (0.49999f / 256) + 49152.0f;
	float packedY = pos.Y - (0.49999f / 256) + 49152.0f;
	SINT x = *(SINT *)&packedX;
	SINT y = *(SINT *)&packedY;
	if (((x | y) & 0x3e0000) == 0)
		return Unknown47eb00(x, y) * mHeightScale;
	return 0;
}

// The ground's normal under a position, from the triangle of the sample square it is over, as a cross product;
// the upper triangle's X and Y come from the opposite neighbours (retail).
// FUNCTION: 0x0047ec60
FVector __fastcall CHeightField::Normal(const FVector &pos)
{
	float	fx = floorf(pos.X);
	SINT	ix = SINT(fx);
	float	fy = floorf(pos.Y);
	SINT	iy = SINT(fy);
	if ((ix < 0) || (ix + 1 > 511) || (iy < 0) || (iy + 1 > 511))
		return FVector(0, 0, 1.f);

	SINT	cx = ix % 8;
	SINT	cy = iy % 8;
	float	d = (pos.Y - fy) + (pos.X - fx);
	SWORD	*p = &mHeightData[((ix / 8) * 64 + iy / 8) * 81];
	FVector	n;
	if (d <= 1.f)
	{
		SINT	h = p[cy * 9 + cx];
		FVector	a(1.f, 0, float(p[cy * 9 + cx + 1] - h) * mHeightScale);
		FVector	b(0, 1.f, float(p[(cy + 1) * 9 + cx] - h) * mHeightScale);
		n = b ^ a;
		n.Normalise();
		return n;
	}
	else
	{
		SINT	h = p[cy * 9 + cx + 10];
		FVector	a(-1.f, 0, float(p[(cy + 1) * 9 + cx] - h) * mHeightScale);
		FVector	b(0, -1.f, float(p[cy * 9 + cx + 1] - h) * mHeightScale);
		n = b ^ a;
		n.Normalise();
		return n;
	}
}

//******************************************************************************************
// The cells above water: their bounds and height range, measured once per water level (the level reads -5318008
// until then). The record is the first 0x20 bytes of the object.
// STAND-IN: the whole 0x20-byte record as GetExtents writes it, overlaid on `this` (heightfield.h's SExtents names the part
// its readers use). Naming the rest in heightfield.h (SExtents completed, CHeightField::mExtents in place of the byte array,
// GetExtents returning &mExtents) is the natural form and compiles to the same code here, but it changes CGame::DrawDebugStuff
// in game.cpp (same size, other bytes: its declaration-count threshold) and shifts the labels of 55 other objects.
struct SExtentsRecord
{
	SINT	mMinX, mMinY, mMaxX, mMaxY;
	float	mWaterLevel;
	void	*mCells;			// +0x14: the grid it was measured on (0047efdd)
	float	mMaxHeight;			// +0x18
	float	mMinHeight;			// +0x1c
};

// FUNCTION: 0x0047ef20
SExtents	*CHeightField::GetExtents()
{
	SExtentsRecord	*e = (SExtentsRecord *)this;
	if (e->mWaterLevel == -5318008.f)
	{
		e->mMinX = 100000;
		e->mMaxX = -100000;
		e->mMinY = 100000;
		e->mMaxY = -100000;
		e->mMaxHeight = -100000.f;
		e->mMinHeight = 100000.f;
		float	*cell = (float *)mUnknown20;
		for (SINT y = 0; y < mGridHeight; y++)
		{
			for (SINT x = 0; x < mGridWidth; x++)
			{
				if (*cell < mWaterLevel)
				{
					if (x < e->mMinX)
						e->mMinX = x;
					else if (x > e->mMaxX)
						e->mMaxX = x;
					if (y < e->mMinY)
						e->mMinY = y;
					else if (y > e->mMaxY)
						e->mMaxY = y;
				}
				if (*cell < e->mMinHeight)
					e->mMinHeight = *cell;
				if (*cell > e->mMaxHeight)
					e->mMaxHeight = *cell;
				cell += 6;
			}
		}
		e->mCells = (void *)mUnknown20;
		e->mWaterLevel = mWaterLevel;
	}
	return (SExtents *)e;
}

//******************************************************************************************
// A tile's lit texels at a mip shift into a 565 surface: each 8-metre cell's texels are its base mixer texture's,
// with each of the slot's mixer layers laid over them where its alpha (bilinear over the slot's 9x9 grid per layer)
// is higher; then lit by the mixer map's light (bilinear over the cell's corners), halved where the static shadows
// fall, through the colour table. The first cell of a tile's 16-texel block starts half way along when the tile
// number is odd (x) or has bit 6 set (y).
// The cell's row step is in bytes: retail scales it at its definition, inside the x loop before the shadow test
// (0047f4c2..0047f4d2: sub, shl, store), where an element count is scaled at each use and hoisted out of the loop.
// So is the tile's row skip (`imul eax,ebp; shl eax,1`, 0047f0c3..0047f0cd; out steps by it as bytes at 0047f6ff).
// cell is dead in retail too: stored (0047f0e1) and advanced by cols in the x loop's preheader (0047f1b6..0047f1c8),
// never read. That update is the x loop's final value of an increment inside it, and it reuses cols' slot only when
// cols is still a value of its own at VC6's copy propagation: a local used once is forward-substituted into skip
// (`pitch - x1 + x0`) and the final value recomputes x1 - x0. Retail computes cols first and keeps it (0047f0ad..
// 0047f0bf), so its source used cols once more in code VC6 then removed. An earlier reconstruction was a dead counter
// (`done += cols` in the y loop, removed with the y loop's useless induction variables); it is replaced (
// 2026-10-13) by a conditional expression with identical arms in skip's definition below: the test and both arms are
// uses of cols at the copy propagation, so cols stays a value, and VC6 folds the conditional away afterwards (any such
// conditional works: a test on cols, e or x0, or `(cols < 0 ? pitch : pitch) - cols`). STAND-IN: redundant source, but
// every value in it is consumed by skip and no dead variable or statement remains. Without it Mixing is 16/382/1924
// (skip becomes `pitch - x1 + x0` and the cell's final value recomputes x1 - x0), with it 6/6/42.
// What is left (measured with a logging and forcing C2, the rank tool and synthetic code):
// - Most of the distance is allocation. Forcing 19 webs' priorities, deferrals or registers (a coordinate descent over
//   all 79 webs) makes the prologue, the setup and the y preheader retail's instruction for instruction (step ebp, size
//   eax, back ecx, tx edi, ty ebx, y0 esi; only frame offsets and alphas, below, differ), but the search stops at
//   149/431/1546: the x loop's registers cascade from the inner loops' webs. Retail keeps texel (edi) and light (ebx)
//   across the x body with step in memory there, reloading both after each inner loop (0047f1ce, 0047f21b,
//   0047f463); ours gives step's x-loop piece ebx (priority 715), and with that piece deferred light's pieces have
//   +0x3c < 0 and stay in memory. Retail keeps shadow in ebp through the shadowed rows (0047f4c4..0047f5cc); ours never
//   pops a node of shadow or of ty: both drop out in the split rounds.
// - The shadowed j loop: retail reloads sy from its slot and shifts it at every texel (0047f544). VC6's global optimizer
//   hoists (sy >> 1) * 2 whenever it sees sy invariant in the j loop (2026-10-13: the loop must look as if it
//   may write sy, and the cause must leave no code). A volatile sy view did that here as a stand-in (221/669/1706 against
//   203/631/1761, 6/6/42 on the final line: it is a scheduling barrier); a struct, array or pointer view of sy, a helper
//   taking const references, a row local in the j loop, a row pointer, a real call taking &sy, self-copy arms and
//   identical-arm conditionals on sy all keep the hoist. It is now a statically false guard around a redefinition of
//   sy in the j loop (below): 0/0/0.
// - mTexSize * t (0047f2eb: `mov ebx,[esi+0x10]; imul ebx,edx`): retail evaluates mTexSize first. Ours sorts t (key
//   0.1.15a0) ahead of IND(maptex + 0x10), whose address temporary (t2bc) has id 0 mod 4 (key 0x0007); retail needs
//   1..3 there while maptex + 0's stays at 0..1 (mData added after texel, 0047f1e8). The edits that flip it (a9
//   unnamed, an indexed base-texture store, a textures pointer stepped per layer, tsrc defined first) change other
//   code.
// - alphas: retail loads slot->mData right after slot (0047f0f7) into a slot of its own ([esp+0x6c], reloaded at
//   0047f14f); ours leaves the web without a register and rematerialises the load in the y preheader.
// The split round, decoded for shadow:
// - When a popped web's colouring brings a tuple's count to 7 (0x107329e9: registers of the coloured live webs with
//   the popped one, pending operand temporaries, the tuple's own register operands), 0x10721b26 takes the live
//   uncoloured webs there minus that tuple's register operands, and 0x10721be6 splits them around it. A web is safe
//   only at a tuple that uses it.
// - Ours: the run loop's split (aa) leaves shadow one piece reloaded on the blend block's out-edges, +0x3c -376,
//   so it defers even at a forced priority of 5000. When sx is coloured the shadowed j loop's first full tuple is the
//   bits load, where shadow is an operand and survives; lv's j-loop piece (1392, ebp) then fills another tuple and
//   splits it. Its pieces turn positive only after later splits, and then take edi and eax.
// - Retail: shadow's piece starts with a reload in the x body (0047f4c4) and keeps ebp through the shadowed rows;
//   lv has only an edx piece (the head to the copy shifted by 8) and sx only an ecx piece around the latch.
// - The first pops that differ: word (3200, edx; retail esi; forced, o takes retail's edi but sx then takes ebp);
//   t (2880, ebx through t * mTexSize, above); step's x-body piece (715) is one piece with the setup (only ebx free),
//   where retail splits step at the x loop (setup ebp, x body memory); the base j counter (192 = 16 * 2 + 80 * 2)
//   pops after light's piece (224), whose ecx fills the base loop and splits it (retail edi; forced to 300, edi).
// - Unchanged IL: lv >> 8 stays after the bits load (one expression, a named level before or after word, the shift
//   in the update or applied in place); shadow declared last (its piece is still -376). The setup's declaration
//   order (greedy over 13 positions) reaches 206/592/1731 with no retail fact behind it.
// Reload placement, decoded (logging hooks at 0x10720579, 0x10720590, 0x10720f4f, 0x10721633):
// - 0x107204d6 takes a split set in ascending web index, 32 webs per batch. Per batch, 0x1072074e builds each block's
//   sets (uses before a kill, kills at the full tuples' spill markers, definitions), 0x10720a0d iterates them, and
//   0x10720e2a puts a reload before a use where the web is in memory, unless the block has an edge list (+0x68): then
//   the reload goes on its out-edges (0x10721633). 0x107211b8 then marks an edge reload for every web named in a full
//   tuple's spill marker of a block with an edge list, and stores each split web after its definitions.
// - The run loop's split puts shadow in the first batch (26th of 50), reloads it after its definition and marks 6 edge
//   reloads on the blend block's out-edges: reloads inside the run loop, hence -376. Retail has that edge block too
//   (0047f40c reloads pal there), so this does not separate the two builds.
// - Retail's reload at 0047f4c4 is a before-use reload: one split had shadow in memory up to the lighting setup and
//   reloaded it before the test (x is split there too: eax and a copy in edi at 0047f46d). Ours fills that setup only
//   when step's piece is coloured (the 27th pop: the lxy and shadow-test tuples reach seven), and shadow, split there,
//   gets no node again; its queue nodes before that all have +0x3c < 0 (+42 at the start, then -376, -416, -424, -16).
// - Forcing retail's registers for 16 webs by symbol cannot steer the rest: light, texel and p find their retail
//   register taken when they pop (311/681/1717). A coordinate descent from word = esi drifts away from retail's
//   registers (bit ebx, l10 edi; 179/623/1727 after 5 facts).
// Pop-order replay (each pop's tuples by source line against retail's register per symbol and
// region, forcing the first pop that differs):
// - Pops 10 and 11 differ first: word (edx; retail esi) and t (ebx; retail edx, the mTexSize * t order above). With
//   both forced, o takes retail's edi by itself (220/676/1735).
// - Pop 15: sx cannot take retail's ecx as one piece (bit and col hold ecx in the j loop). Retail's sx, lx and lv are
//   already split when they pop: seven registers are busy at the bits load (0047f557) before sx is coloured, the
//   seventh being shadow's ebp piece. Ours has six there before sx. Even with shadow read inside the x loop, its piece
//   (+0x3c 16) ranks 704 against sx's 2096, so priority order alone cannot colour it first.
// - Deferring sx and lx in place of retail's splits gives 198/672/1759; continuing pop by pop (step's x-body piece,
//   lxy, row, maptex, the base loop, light, p, skip, texel) does not converge (20 facts, 227/607/1756): a deferral is
//   not the split retail made.
// - Declaration scope is no lever here: nothing in Mixing escapes (it makes no call), so l00, lx, l10, lxy, ly, o,
//   next, q, sy, sx, row, word or bit at function scope, or one lv or col shared by both branches, compiles identically,
//   and a dead `if (0) Probe(&shadow)` changes nothing (202/642/1755: the escape needs a real call in the function).
// Round 6 (CSE value ids burned by a diagnostic C2, as for UpdateWheel):
// - t (pop 11): the order of mTexSize * t follows the CSE value id of the mTexSize load (0x2bd here; its address
//   temporary is t2bc). Moving that one id by 1, 2 or 3 gives retail's `mov ebx,[maptex+0x10]; imul ebx,edx`, and t
//   then takes edx by itself; later ids' residues and the first CSE walk change nothing. The dead counter carries it:
//   `done += cols * 2` (or cols + 1, cols << 1, an extra done++) adds one value before it and compiles exactly as the
//   burn (185/641/1879), but it moves the setup's allocation (step esi, shift one ebx piece over the x body, maptex
//   edi), so it stays out until the rest matches.
// - word (pop 10): its choice costs are all 0 with edx, esi, edi, ebx and ebp free, so it takes edx, the first in class
//   order; no preference makes esi cheaper. Retail's esi needs edx held first by lv: lv prefers edx (cost -1 through
//   its copy into the colour index, which takes edx), and retail's lv piece (0047f528..0047f54a) ends at lv >> 8, so it
//   shares edx with the colour index. That needs lv split after lv >> 8, before word pops.
// - With both (word forced), o takes edi by itself, but sx still finds the bits load at six registers and cannot take
//   ecx, so retail's split history does not appear by itself. Colouring o (edi) and shadow (ebp) before bit fills the
//   bits load at bit's pop: sx's piece then takes retail's ecx and lxy ebx, l10 eax, but lv is split at the load,
//   before lv >> 8, its piece overlaps the colour index, and word keeps edx. Shadow has no positive node that early.
// Round 7: the shadowed j loop's IL, in order: lv += lx; word = sx >> 6; word << 5; sx >> 1;
// bit; the index (row * 2 hoisted to the row loop, + word); 1 << bit; the bits load; & and >> bit; then lv >> 8 (a
// copy of lv, shifted); >> the new bit; * 3; col; q += 4; the three products and sums; the store; o++; sx += inc; j.
// Across it live q, o, shadow, sx, lv, lx, inc, j and the hoisted row * 2.
// - Retail's whole j loop follows when q, o and shadow are coloured before bit and the bits load fills at bit's pop
//   (forced: sx's ecx piece reloaded for sx += inc, bit ecx, shadow ebp, o edi, the products, inc edx), except lv and
//   the scratch registers around it (word, j, this): retail keeps lv in edx across the load (sar edx,8 is only
//   scheduled ahead of it), so its piece must be coloured before the load fills and end at lv >> 8, which needs a
//   split after lv >> 8 first; word then takes esi.
// - No source form moves the deciding pops: shadow copied before the j loop or in its for-init, a row pointer, sx, lv
//   and row set in the for-init, sy set in the row loop's for-init (all identical, or 192/614/1908 with row first,
//   which moves pops outside the j loop only), and lv >> 8 named, set at the loop head, shifted in place or written
//   in one expression (identical: VC6 substitutes the single-use level back after the bits load).
// Round 8 (stand-ins allowed for a whole match; variants, tools and the candidate source in
// working notes, not kept here; numbers are shape score):
// - The shadowed rows: lv >> 8 has to be computed before the bits load and held across it, so that lv's piece ends
//   there. Written inline, VC6 evaluates the heavier operand (the shadow bit) first, and a named single-use level is
//   substituted back after the load. Passed by const reference to a file-local helper it stays first, because the
//   arguments bind right to left: `&mColors[ShadeLevel((shadow->mBits[...][word] & (1 << bit)) >> bit, lv >> 8)]` with
//   `static inline SINT ShadeLevel(SINT bit, const SINT &level) { return level >> bit; }` (replaced on 2026-10-13 by a
//   named `const SINT &level = lv >> 8;` at the loop head, see the shadowed rows; natural; an unreferenced
//   label after `SINT level = lv >> 8;`, or a self-copy arm on level, does the same). Level then pops third (3840,
//   edx), word takes esi, o edi, and shadow is reloaded into ebp before the test and held through the rows.
// - That needs sy reloaded at each texel: `shadow->mBits[*(volatile SINT *)&sy >> 1][word]` written inline (a volatile
//   view then; a named row, or the row local, is hoisted). With both, the shadowed rows are retail's register
//   for register; only three instructions are scheduled differently (retail issues the sy reload before `sar edx,8`,
//   and `add edi,2; add eax,4` after the bits load). Replaced on 2026-10-13, see the shadowed rows.
// - alphas: `(char *)*(UBYTE *volatile *)&slot->mData` gave retail's load after slot, into a slot of its own
//   (a stand-in then; a plain load replaced it on 2026-10-13, see alphas below). mTexSize * t: with the two volatile views (now one) the dead counter as it was (`done += cols`, since replaced by the conditional in skip) gives retail's
//   order (t edx). The base copy loop as `*d = palette[*s++]; d++;` defines s first, so s pops first at their tie
//   (s eax, d ecx, as retail).
// - Steps: inline volatile sy 209/669/1707; + helper 179/519/1635; + alphas 145/453/1590; + base loop 145/443/1590
//   (this form 203/631/1761). Instructions differing per aligned region of the last: setup 23, y preheader 43, base
//   loop 34, layer setup 65, run loop 20, lighting setup 42, shadowed rows 8, unshadowed rows 20, latches 11 and 38.
// - A greedy forcing search over every symbol's priority and deferral from there (1,190 compiles) stops at
//   119/323/1649 with seven facts (this + 0x28 at 1000, s 1, the offset IV 400, rowsize deferred, maptex 400, the
//   unshadowed colour index 1, l00 1): the run loop becomes retail's and the layer setup's spills appear, but the
//   setup, the y loop and the lighting setup keep differing instructions (retail keeps step in ebp to the setup's end,
//   recomputes y0 * 9 in the y preheader instead of keeping the CSE temporary, reloads x into eax and copies it to
//   edi): those are not colouring order.
// FUNCTION: 0x0047eff0
void	CHeightField::Unknown47eff0(SINT shift, CMapTex *maptex, void *dst, SINT pitch, SINT tile, SINT e, SINT x0, SINT y0, SINT x1, SINT y1)
{
	// step is declared before size and back before rowsize: retail multiplies size by step in size's own register
	// (`imul eax,ebp`, 0047f024) after copying size for back (`mov ecx,eax; ... sub ecx,ebp`), so size ranks first
	// (the later declaration) and back comes first in the IL.
	SINT	step = 1 << shift;
	// out's byte step as a value of its own (found with `out += step` written this way): VC6 then keeps
	// `step * 2` as one temporary and recomputes it from step at the x latch (`lea eax,[ecx+ecx*1]; add edi,eax`, 0047f6ac..
	// 0047f6b2), as retail does; `out += step` alone folds into one lea. The same device as rowbytes in the x body (p's step)
	// and as GetCacheTexels' step4.
	SINT	ostride = step * 2;
	SINT	size = maptex->mSize;
	SINT	base = 0;
	SINT	back = size - step;
	SINT	rowsize = size * step;
	if (tile & 1)
		base = step * 8;
	if (tile & 0x40)
		base += rowsize * 8;
	SINT	tx = (tile % 64) << 3;
	SINT	ty = (tile / 64) << 3;
	UWORD	*out = (UWORD *)dst + (pitch * y0 + x0) * step;
	SINT	cols = x1 - x0;
	// STAND-IN (see the notes before this function): the conditional's arms are identical, so it is cols; its test and arms keep
	// cols a value until VC6's copy propagation, and the conditional is folded away after it.
	SINT	skip = (pitch - (cols < 0 ? cols : cols)) * step * 2;
	SINT	cell = y0 * 9 + x0;		// dead, as retail's
	SMixerSlot	*slot = &MIXERMAP.mSlots[tile];
	// A plain load: an earlier volatile view of slot->mData was needed while the arms (since removed) stood in, and
	// is now a hindrance. A volatile load is a scheduling barrier: every earlier definition without a consumer in the
	// block (here ty's shift) is held ahead of it, and the shadow-grid index chain cannot rise above it. Retail issues
	// ty's shift after the load (`shl ebx,3` at 0047f110) and part of the grid index before it (0047f0ee), so the plain
	// load reproduces the whole setup block (0047eff0..0047f148) but its two relocated operands.
	char	*alphas = (char *)slot->mData;
	SShadowCell	*shadow = STATICSHADOWS.mGrid[tile & 0x3f][(tile >> 6) & 0x3f];
	SINT	inc = 0x80 / (step * 8);
	SINT	ypos = y0 << 4;
	UBYTE	*map = MIXERMAP.mMap;

	for (SINT y = y0; y < y1; y++)
	{
		SINT	offset = rowsize * y;
		SINT	xpos = x0 << 4;
		for (SINT x = x0; x < x1; x++)
		{
			cell++;
			// p's row step as a value of its own, at the top of the x body (from a reading of retail's
			// `step * 4` as one shared value): VC6 keeps it as a hoisted temporary and recomputes it from step at the base copy's
			// latch (`mov ecx,[step]; lea eax,[ecx*4+0]; add ebp,eax`, 0047f227..0047f234), where `p += step` folds into one lea. With it
			// step's references in the x loop fall, its register piece shrinks to retail's and the rest of the x body falls into
			// place: the palette arm and the lv arm that stood in for the missing temporary are gone (the first of them left a dead
			// load of shift, the second a dead load of e). Any position before the copy loop works; declared in the y body or at
			// function scope it does not (22/56, collapse), and `p += rowbytes / 4` collapses too.
			SINT	rowbytes = step * 4;
			SINT	texel = step * x + offset + base;
			// the base texture (the palette is read first: 0047f1df, then mData 0047f1e2)
			DWORD	*palette = maptex->mUnknown08;
			UBYTE	*src = maptex->mData + texel;
			DWORD	*p = mUnknown28;
			SINT	i, j;
			for (i = step; i; i--)
			{
				UBYTE	*s = src;
				DWORD	*d = p;
				for (j = step; j > 0; j--)
				{
					*d = palette[*s++];
					d++;
				}
				src += size;
				p = (DWORD *)((UBYTE *)p + rowbytes);
			}

			// the mixer layers. alpha comes from the row base: with a row pointer set in the y loop, VC6's global
			// pass rewrote `arow + x` as `(light + x) + (arow - light)` and built &light[x] once; retail adds x to
			// arow here (0047f251) and to light at each read (0047f471..0047f485). alpha[9] << 24 is shifted once
			// for axy and then ay (0047f2a3..0047f2ca), as GetCacheTexels' i10. t is an int: retail widens the
			// texture number as it loads it (`xor edx,edx; mov dx,[eax]`, 0047f281) and multiplies mTexSize by it
			// (`mov ebx,[esi+0x10]; imul ebx,edx`, 0047f2eb); a UWORD t is widened at its use (`and reg,0xffff`).
			char	*alpha = alphas + y * 9 + x;
			for (SINT l = 0; l < slot->mCount; l++)
			{
				// The volatile view of maptex that stood here (a stand-in for maptex's reload per layer, and
				// a scheduling barrier that put its load ahead of the texture cursor's) is gone: with the natural copy rows and
				// rowbytes above the plain copy gives retail's layer setup (LAYSET 7/5 -> 0/0).
				CMapTex	*mt = maptex;
				SINT	t = slot->mTextures[l];
				DWORD	*pal = mt->mUnknown08 + (t << 8);
				SINT	a = alpha[0] << 24;
				SINT	ax = (alpha[1] << 24) - a;
				SINT	a9 = alpha[9] << 24;
				SINT	axy = ((alpha[10] << 24) - a9) - ax;
				SINT	ay = a9 - a;
				ax >>= shift;
				ay >>= shift;
				axy >>= shift * 2;
				UBYTE	*tsrc = mt->mData + mt->mTexSize * t + texel;
				SINT	*tp = (SINT *)mUnknown28;
				for (i = step; i; i--)
				{
					SINT	aa = a;
					a += ay;
					for (j = step; j; j--)
					{
						SINT	c = (aa & 0xff000000) + pal[*tsrc];
						SINT	d = c - *tp;
						// Retail starts a run only for d > 0, then blends equal texels
						// until a negative difference or the row end (0047f33e..0047f404).
						// Blend and copy each step on by themselves and leave for the row's end
						// directly: retail has three copies of the step (0047f356, 0047f3bd, 0047f3f1).
						// STAND-IN (layout): `goto skip` and `goto next_row` leave the run as retail does; breaking out with an
						// `ended` flag and a final test is 817 bytes off.
						if (d > 0)
						{
							for (;;)
							{
								while (d < 0x20000000)
								{
									if (d < 0)
										goto skip;
									SINT	f = d >> 26;
									*tp = ((((*tp & 0xf8f8ff) * (7 - f)) + ((c & 0xf8f8ff) * f)) >> 3) + (c & 0xff000000);
									tp++;
									tsrc++;
									aa += ax;
									if (--j == 0)
										goto next_row;
									c = (aa & 0xff000000) + pal[*tsrc];
									d = c - *tp;
								}
								*tp = c;
								tp++;
								tsrc++;
								aa += ax;
								if (--j == 0)
									goto next_row;
								c = (aa & 0xff000000) + pal[*tsrc];
								d = c - *tp;
							}
						}
skip:
						tp++;
						tsrc++;
						aa += ax;
					}
next_row:
					ax += axy;				// before tsrc's step, as retail adds them (0047f423, 0047f425)
					tsrc += back;
				}
				alpha += 81;
			}

			// The light row is computed here, at the lit section. Defined at the top of the y body,
			// its (ty + y) term became the first derived induction variable of strength reduction, so light's step came
			// first in the y latch; here it comes after offset's and the alpha row's, in retail's order (0047f6f8..0047f72d).
			UBYTE	*light = map + (ty + y) * 512 + tx;
			// lit, and shadowed. The light differences are taken as their corners are read, as the alpha's are:
			// retail reads the corners in order (0047f471..0047f485) and subtracts l10 before lx for lxy (0047f4a1,
			// 0047f4a5). With l01 and l11 named, lx is referenced after l10, ranks above it and is subtracted first.
			SINT	l00 = light[x] << 8;
			SINT	lx = (light[x + 1] << 8) - l00;
			SINT	l10 = light[x + 0x200] << 8;
			SINT	lxy = ((light[x + 0x201] << 8) - l10) - lx;
			SINT	ly = l10 - l00;
			lx >>= shift;
			ly >>= shift;
			lxy >>= shift * 2;
			UWORD	*o = out;
			UBYTE	*q = (UBYTE *)mUnknown28;
			SINT	next = (pitch - step) * 2;	// bytes to the next row of the cell
			if (shadow)
			{
				// The shadow row: retail recomputes sy >> 1 from sy's slot at every texel and folds the * 2 into an lea
				// (`mov ebx,[esp+0x40]; sar ebx,1; ... lea esi,[esi+ebx*2]`, 0047f544..0047f54d). Indexed with sy >> 1
				// in the j loop, VC6's global optimizer hoists (sy >> 1) * 2 as one temporary (a two-op chain, never
				// rematerialised) and spills it; it keeps both in the loop only when it sees the j loop write sy. STAND-IN
				// (on a reading of the optimizer): the guarded redefinition of sy at the head of
				// the j loop below. Its guard is false (shadow is not NULL inside this branch), which VC6 only learns after the
				// global optimizer, so the optimizer sees a write of sy in the loop, leaves `sy >> 1` where it is, and the
				// redefinition then folds away without leaving code.
				SINT	sy = ypos;
				for (i = step; i; i--)
				{
					l00 += ly;
					lx += lxy;
					SINT	sx = xpos;
					SINT	lv = l00;
					for (j = step; j; j--)
					{
						lv += lx;
						// The shifted level as a named const-reference temporary: what an earlier helper parameter made (lv >> 8 held
						// from here to the index, so that lv's piece ends there). Taken at the head it is no longer the node right
						// before the sy read (a volatile one then, whose order edges had pinned `sar edx,8` ahead of the reload;
						// 2026-10-13, scheduler log): shadowed rows 8/8 -> 6/6, and the helper is gone.
						const SINT	&level = lv >> 8;
						SINT	word = sx >> 6;
						if (!shadow)		// STAND-IN (see above): never true here; keeps sy a possible write in this loop
							sy = ypos;
						SINT	bit = (sx >> 1) - (word << 5);
						ColorEntry	*col = &mColors[level >> ((shadow->mBits[sy >> 1][word] & (1 << bit)) >> bit)];
						*o++ = (UWORD)((((q[2] * col->blue) & 0x1f0000) + ((q[1] * col->green) & 0x7e00000) + ((q[0] * col->red) & 0xf8000000)) >> 16);
						q += 4;
						sx += inc;
					}
					sy += inc;
					o = (UWORD *)((UBYTE *)o + next);
				}
			}
			else
			{
				// lv steps at the loop's head, as in the shadowed loop (0047f620..0047f626)
				for (i = step; i; i--)
				{
					l00 += ly;
					lx += lxy;
					SINT	lv = l00;
					for (j = step; j; j--)
					{
						lv += lx;
						ColorEntry	*col = &mColors[lv >> 8];
						*o++ = (UWORD)((((q[2] * col->blue) & 0x1f0000) + ((q[1] * col->green) & 0x7e00000) + ((q[0] * col->red) & 0xf8000000)) >> 16);
						q += 4;
					}
					o = (UWORD *)((UBYTE *)o + next);
				}
			}
			// retail's order at the latch (0047f6af..0047f6b2): xpos steps first, then out by its stride
			xpos += 16;
			out = (UWORD *)((UBYTE *)out + ostride);
		}
		ypos += 16;
		out = (UWORD *)((UBYTE *)out + skip);
	}
}

//******************************************************************************************
// Reads the object raw (sizeof(CHeightField) is checked first), then the heights block by block, and scales the
// gradient to 565 colour. The sun is scaled so that sun plus ambient never passes white.
// Matched. Two source facts decide the colour block's registers (logging C2, a register-allocation logger):
// - The ambient channels are read ar, ag, ab and the sums r, b, g (only that order of the 36 compiles to retail).
// - The heights are indexed from four loop counters. VC6 strength-reduces the index into a compiler induction
//   temporary (esi, first pop) and tests the outer loop on it. With a named byte offset `n = 0` instead, n joined the
//   zero constant web of the NULL stores at the top, passed it esi when coloured first, and that web, live across the
//   colour block, added +1 to esi when g chose (C2 0x1073301e): g ebx, r esi. Now g takes esi, r is left with ebx
//   alone, which the byte rule cannot take away (0x10732d0d: three byte registers in use at g's `mov al,dh`
//   restrict a web to ebp/esi/edi only while it still has one of them), and m is deferred (memory only).
// FUNCTION: 0x0047f750
void	CHeightField::Load(CChunkReader *c)
{
	SAFE_DELETE_ARRAY(mOwned24);
	SAFE_DELETE_ARRAY(mHeightData);

	c->GetNext();
	c->GetNext();
	if (c->GetSize() != sizeof(CHeightField))
	{
		char	buf[256];
		sprintf(buf, "Got size %d, expected %d\n", c->GetSize(), sizeof(CHeightField));
		TRACE(buf);
	}
	c->Read(this, sizeof(CHeightField), 1);
	c->GetNext();
	Unknown47e8e0();

	DWORD	ambient = mHFP.Ambient;
	SINT	ar = (ambient >> 16) & 0xff;
	SINT	ag = (ambient >> 8) & 0xff;
	SINT	ab = ambient & 0xff;
	DWORD	sun = mHFP.Sun;
	SINT	r = ((sun >> 16) & 0xff) + ar;
	SINT	b = (sun & 0xff) + ab;
	SINT	g = ((sun >> 8) & 0xff) + ag;
	SINT	m = r;
	if (b > m)
		m = b;
	if (g > m)
		m = g;
	if (m > 255)
	{
		r = (r * 255) / m;
		g = (g * 255) / m;
		b = (b * 255) / m;
	}
	mHFP.Sun = ((r - ar) << 16) | ((g - ag) << 8) | (b - ab) | 0xff000000;

#line 2176
	mHeightData = new(MEMTYPE_HEIGHTFIELD) SWORD[64 * 64 * 81];
	// The heights come tile by tile, 9 x 9 samples each, in GetHeightSample's order. VC6 strength-reduces the index
	// into a byte offset (add esi,2 at 0047f90e) and tests the outer loop on it (cmp esi,0xa2000 at 0047f92a).
	for (SINT tx = 0; tx < 64; tx++)
	{
		for (SINT ty = 0; ty < 64; ty++)
		{
			for (SINT sy = 0; sy < 9; sy++)
			{
				for (SINT sx = 0; sx < 9; sx++)
				{
					SWORD	h;
					c->Read(&h, 2, 1);
					mHeightData[((tx * 64 + ty) * 9 + sy) * 9 + sx] = h;
				}
			}
		}
	}

	for (ColorEntry *p = mColors; p < mColors + 65; p++)
	{
		p->red <<= 1;
		if (DWORD(p->red) > 0xf80000)
			p->red = 0xf80000;
		p->red &= 0xf80000;
		p->green <<= 1;
		if (DWORD(p->green) > 0x7e000)
			p->green = 0x7e000;
		p->green &= 0x7e000;
		p->blue <<= 1;
		if (DWORD(p->blue) > 0x1f00)
			p->blue = 0x1f00;
		p->blue &= 0x1f00;
	}
}

//******************************************************************************************
// Shutdown frees what the destructor frees; the bodies are the same, so the linker folds Shutdown into the destructor
// (/OPT:ICF) and CMap::Shutdown (00490f40) calls 0047e8a0.
// FUNCTION: 0x0047e8a0
void CHeightField::Shutdown()
{
	SAFE_DELETE_ARRAY(mOwned24);
	SAFE_DELETE_ARRAY(mHeightData);
}

//******************************************************************************************
// The height field's set-up (nothing to do) that no code above defined. Each compiles to a body another function
// already has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose
// address each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x004fdc10
BOOL CHeightField::Init()
{
	return TRUE;
}
