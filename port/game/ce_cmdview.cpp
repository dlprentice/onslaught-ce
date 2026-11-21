// SPDX-License-Identifier: GPL-3.0-only
// The Commander's live view, drawn by CE itself.
//
// Why it is not the game's own render from another camera. The game's render is not a picture taken of the battle: it is part of the battle. A
// render steps the particles, picks the units that get shadow textures, poses the meshes it draws, starts and stops their effects, chooses the
// landscape's detail, and the update that follows reads what it left (port/PARITY.md, "Commander live view"). An earlier form of this view rendered
// the world a second time from the strategic camera and put back what that render wrote; every writer it missed changed the battle, and there
// was no end to them. So the view never renders the world. While it is open the game draws the pilot's frame exactly as it does when the view is
// closed (the same camera, the same calls, the HUD too), and then this file draws its own picture over that frame:
//
// - It READS the world and nothing else: the height field's samples, the landscape's mixer textures, light map and static shadows, and of each
//   thing its type bits, flags, position, orientation and side. Every read is a field or a function that only reads (listed in READS below; the
//   source check in port/tools/ce_commander_tests.py holds this file to that list).
// - It DRAWS through the Direct3D device alone, with its own vertex and index buffers and its own texture. It calls no drawing routine of the
//   game (no sprite, font, render-state cache or vertex-buffer helper), so none of the game's render records change. The device's own state is
//   kept in a state block before the picture and applied again after it.
// - Its memory is the C library's, never the game's heaps (an allocation there would move the battle's own later allocations).
// - It draws no random number and reads no clock.
//
// The picture: the terrain as a mesh of the height samples, coloured as the game's own landscape texture is at its coarsest level (the mixer
// layers of each cell lit by the level's light map and static shadows; one texel a cell), the water as a sheet at its level, haze toward the
// horizon in the level's fog colour, and every unit as a mark in its side's colour, shaped by its kind and pointing where it faces. Rounds in
// flight and explosions are small bright marks. The squads' frames, the orders and the panels are drawn over it by the map (ce_cmdmap.cpp)
// through CeCmdViewBox and CeCmdViewText.
//
// A test build's footprint audit (ce_cmdview_audit.cpp) copies the game's heaps and the executable's data before a frame of this view and
// compares them after it: a byte changed outside the view's own records fails the check.
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "ltshell.h"
#include "map.h"
#include "mixermap.h"
#include "maptex.h"
#include "staticshadows.h"
#include "DXEngine.h"
#include "world.h"
#include "thing.h"
#include "unit.h"
#include "Platform.h"
#include "Texture.h"
#include "ce_cmdview.h"
#include "ce_cmdorders.h"
#include "ce_commander.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// READS (the whole of what this file takes from the game; each only reads):
//   MAP             mGridWidth, mGridHeight, GetHeight (a sample times the scale), Collide (an interpolated sample), GetWaterLevel,
//                   GetHFProperties (the water's colour), GetFog, mColors (the light gradient)
//   MIXERMAP        mSlots (each tile's layer count, textures and alphas), mMap (the light map)
//   STATICSHADOWS   mGrid (each tile's shadow bits)
//   ENGINE          GetMapTex (the mixer textures' texels and palettes)
//   WORLD           GetThingNB, walked node by node (the set's own iterator is state of the game's and is not used)
//   a thing         GetThingType, GetFlags, GetPos; a unit's GetOrientation and GetAllegiance
//   PLATFORM        Font(FONT_SMALL): the font's texture, its cell table and RemapChar
//   GAME            GetCurrentLevel
//   LT              GetDevice
// The memory is the C library's (operator new in a game file is the game's allocator: none is used here).

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps this file's own records in sections of their own, which the footprint audit leaves out (ce_cmdview_audit.cpp).
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

namespace
{
// GenericSPtrSet's storage (SPtrSet.h): first node, last, the iterator, size; a node is item, next (as updatedump.cpp reads it).
struct NodeView { void *item; NodeView *next; };
struct SetView { NodeView *first, *last, *iterator; int size; };

struct WorldVertex { float x, y, z; float u, v; };
const DWORD kWorldFvf = D3DFVF_XYZ | D3DFVF_TEX1;
struct FlatVertex { float x, y, z; DWORD colour; };
const DWORD kFlatFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
struct ScreenVertex { float x, y, z, rhw; DWORD colour; float u, v; };
const DWORD kScreenFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// The terrain: the height field is 512 cells a side. The mesh takes every second sample (257 a side) in 16 pieces of 64 x 64 quads, so one
// 16-bit index buffer serves every piece.
const int kCells = 512, kStep = 2, kPieceQuads = 64, kPieces = 4, kPieceVerts = (kPieceQuads + 1) * (kPieceQuads + 1);
const int kTerrainVerts = kPieces * kPieces * kPieceVerts, kTerrainIndices = kPieceQuads * kPieceQuads * 6;
const int kTexSide = 512;
// The water's sheet: squares of 256 units from 2,560 units before the map to as far past it.
const int kWaterSquares = 22, kWaterVerts = kWaterSquares * kWaterSquares * 6;
const float kWaterSquare = 256.f, kWaterFrom = -2560.f;

IDirect3DDevice9 *gDevice;			// the device the objects below belong to
IDirect3DVertexBuffer9 *gTerrainVb;
IDirect3DIndexBuffer9 *gTerrainIb;
IDirect3DTexture9 *gTerrainTex;
FlatVertex *gWater;				// the water sheet's corners, the C library's memory
FlatVertex *gSkirt;				// the ground carried on past the map's edges, the C library's memory
int gBuiltLevel = -1;
bool gBuildFailed;

IDirect3DStateBlock9 *gKept;		// the device's state, between Begin and End
bool gDrawing;
float gFade = 1.f;
CeWorldViewSample gView;
double gCombined[16];

// Boxes and text over the picture, in the order they were asked for: runs of plain and of font-textured triangles.
ScreenVertex *gScreen;
unsigned gScreenCount, gScreenRoom;
struct Run { unsigned first, count; bool font; };
Run *gRuns;
unsigned gRunCount, gRunRoom;

#ifdef CE_COMMANDER_TEST
CeCmdViewStats gStats;
#endif

const DWORD kFriendColour = 0xff62d862, kEnemyColour = 0xffe45656, kPilotColour = 0xff70e0ff, kNeutralColour = 0xffc9ced6, kRoundColour = 0xfffff0b0,
			kBlastColour = 0xffffa040, kEdgeColour = 0xff0a0e14;

DWORD FadeColour(DWORD colour)
{
	if (gFade >= 1.f) return colour;
	const DWORD a = (DWORD)(float(colour >> 24) * gFade);
	return (colour & 0x00ffffff) | (a << 24);
}

template <class T> void Drop(T *&object)
{
	if (object) { object->Release(); object = NULL; }
}

// ---- the terrain's colours ---------------------------------------------------------------------------------------------------------------

// One cell's colour as the landscape's texture has it at its coarsest level: the base mixer texture, each of the tile's layers laid over it where
// its alpha lifts it above what is there (blended across the last eighth), lit by the light map through the level's gradient, darkened where the
// static shadow map says so. The arithmetic is the game's (its 32-bit wrap included), written out here so that nothing of the game's is called:
// the game's own routine fills a scratch buffer inside the map object.
bool MixerUsable(const CMapTex *mt)
{
	return MIXERMAP.mSlots && MIXERMAP.mMap && mt && mt->mData && mt->mUnknown08 && mt->mSize == 16 && mt->mNum > 0 && mt->mNum <= 64 &&
		   mt->mTexSize >= 16 * 16;
}

DWORD MixerCell(const CMapTex *mt, int cx, int cy)
{
	const int tx = cx >> 3, ty = cy >> 3, x = cx & 7, y = cy & 7, tile = ty * 64 + tx;
	const SMixerSlot *slot = &MIXERMAP.mSlots[tile];
	const int texel = x + 16 * y + ((tile & 1) ? 8 : 0) + ((tile & 0x40) ? 16 * 8 : 0);
	uint32_t cur = mt->mUnknown08[mt->mData[texel]];
	const signed char *alphas = (const signed char *)slot->mData;
	int layers = alphas ? slot->mCount : 0;
	const int most = (int)(sizeof slot->mTextures / sizeof slot->mTextures[0]);
	if (layers > most) layers = most;
	for (int l = 0; l < layers; ++l)
	{
		const int t = slot->mTextures[l];
		if (t < 0 || t >= mt->mNum) continue;
		const uint32_t a = (uint32_t)(int32_t)alphas[l * 81 + y * 9 + x] << 24;
		const uint32_t c = a + mt->mUnknown08[(t << 8) + mt->mData[mt->mTexSize * t + texel]];
		const int32_t d = (int32_t)(c - cur);
		if (d <= 0) continue;
		if (d < 0x20000000)
		{
			const uint32_t f = (uint32_t)d >> 26;
			cur = ((((cur & 0xf8f8ff) * (7 - f)) + ((c & 0xf8f8ff) * f)) >> 3) + (c & 0xff000000);
		}
		else cur = c;
	}
	const int lx = cx + 1 < kCells ? cx + 1 : kCells - 1, ly = cy + 1 < kCells ? cy + 1 : kCells - 1;
	int level = MIXERMAP.mMap[ly * 512 + lx];
	if (const SShadowCell *shadow = STATICSHADOWS.mGrid[tile & 0x3f][(tile >> 6) & 0x3f])
		if (shadow->mBits[y * 8][x >> 2] & (1u << (8 * x - 32 * (x >> 2)))) level >>= 1;
	if (level > 64) level = 64;
	const CHeightField::ColorEntry &col = MAP.mColors[level];
	const uint32_t r5 = (((cur & 0xff) * (uint32_t)col.red) & 0xf8000000) >> 27;
	const uint32_t g6 = ((((cur >> 8) & 0xff) * (uint32_t)col.green) & 0x07e00000) >> 21;
	const uint32_t b5 = ((((cur >> 16) & 0xff) * (uint32_t)col.blue) & 0x001f0000) >> 16;
	return 0xff000000 | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) | ((b5 << 3) | (b5 >> 2));
}

// Where the mixer's records cannot be read: the ground by its height, the way the paused map shades it.
DWORD HeightCell(int cx, int cy, float lo, float hi, float water)
{
	const float h = MAP.GetHeight(cx, cy);
	if (h > water)
	{
		const float depth = (h - water) / (hi - water > 1.f ? hi - water : 1.f);
		const int v = (int)(90.f - 50.f * (depth > 1.f ? 1.f : depth));
		return 0xff000000 | (20 << 16) | ((v / 2 + 30) << 8) | (v + 60);
	}
	const float t = (water - h) / (water - lo > 1.f ? water - lo : 1.f);
	const float slope = MAP.GetHeight(cx + 1 < kCells ? cx + 1 : cx, cy) - h + MAP.GetHeight(cx, cy + 1 < kCells ? cy + 1 : cy) - h;
	float shade = 0.75f + slope * 0.12f;
	shade = shade < 0.45f ? 0.45f : (shade > 1.1f ? 1.1f : shade);
	const float tt = t > 1.f ? 1.f : t;
	const int r = (int)((70.f + 90.f * tt) * shade), g = (int)((95.f + 60.f * tt) * shade), b = (int)((60.f + 70.f * tt) * shade);
	return 0xff000000 | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

// The ground past the map: each edge of the map carried outward at the height it has there, in three widening bands, and each corner filled at
// the corner's height, in the colour the ground has along that stretch of the edge (the mean of the texture's texels there: single texels drawn
// out to the horizon would be stripes). The land or the sea floor at the map's edge goes on instead of ending in a square. Where the edge lies
// under water the floor also sinks as it goes out, so that far from the camera, where a depth buffer is coarse, the water's sheet and the floor
// under it stay apart.
const int kSkirtStep = 8, kSkirtPoints = kCells / kSkirtStep + 1, kSkirtBands = 3;
const int kSkirtVerts = (4 * (kCells / kSkirtStep) * kSkirtBands + 4 * kSkirtBands * kSkirtBands) * 6;
const float kSkirtReach[kSkirtBands + 1] = {0.f, 256.f, 1024.f, 3072.f};
const float kSkirtSink[kSkirtBands + 1] = {0.f, 16.f, 64.f, 192.f};
enum { kSouth, kNorth, kWest, kEast };
DWORD gEdgeColour[4][kSkirtPoints];		// the ground's colour at each skirt point of each edge (BuildTexture)

// The height of a skirt point `band` bands out from an edge point of height z (z grows downward: under water is greater than the level).
float SkirtZ(float z, int band, float water) { return z > water ? z + kSkirtSink[band] : z; }

// The mean of the texels around an edge point: sixteen cells along the edge, six in from it.
DWORD EdgeMean(const DWORD *texels, int edge, int point)
{
	unsigned long sum[3] = {0, 0, 0}, count = 0;
	for (int along = point * kSkirtStep - 8; along < point * kSkirtStep + 8; ++along)
		for (int in = 0; in < 6; ++in)
		{
			const int a = along < 0 ? 0 : (along > kTexSide - 1 ? kTexSide - 1 : along);
			const int x = edge == kWest ? in : edge == kEast ? kTexSide - 1 - in : a, y = edge == kSouth ? in : edge == kNorth ? kTexSide - 1 - in : a;
			const DWORD texel = texels[y * kTexSide + x];
			for (int c = 0; c < 3; ++c) sum[c] += (texel >> (8 * c)) & 0xff;
			++count;
		}
	return 0xff000000 | ((sum[2] / count) << 16) | ((sum[1] / count) << 8) | (sum[0] / count);
}

bool BuildTexture(IDirect3DDevice9 *device)
{
	DWORD *texels = (DWORD *)malloc(sizeof(DWORD) * kTexSide * kTexSide);
	if (!texels) return false;
	const CMapTex *maptexs = ENGINE.GetMapTex();
	const CMapTex *mt = maptexs ? &maptexs[4] : NULL;		// the 16-texel level: one texel a cell
	const bool mixer = MixerUsable(mt);
	float lo = 1e30f, hi = -1e30f;
	if (!mixer)
		for (int y = 0; y < kCells; y += 4)
			for (int x = 0; x < kCells; x += 4) { const float h = MAP.GetHeight(x, y); if (h < lo) lo = h; if (h > hi) hi = h; }
	const float water = MAP.GetWaterLevel();
	for (int y = 0; y < kCells; ++y)
		for (int x = 0; x < kCells; ++x)
			texels[y * kTexSide + x] = mixer ? MixerCell(mt, x, y) : HeightCell(x, y, lo, hi, water);
	for (int edge = 0; edge < 4; ++edge)
		for (int point = 0; point < kSkirtPoints; ++point) gEdgeColour[edge][point] = EdgeMean(texels, edge, point);
#ifdef CE_COMMANDER_TEST
	gStats.mixer = mixer; gStats.texels = kTexSide * kTexSide;
#endif
	bool ok = SUCCEEDED(device->CreateTexture(kTexSide, kTexSide, 0, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &gTerrainTex, NULL)) && gTerrainTex;
	// Every level of the chain: each texel the average of the four under it.
	int side = kTexSide;
	for (DWORD level = 0; ok && level < gTerrainTex->GetLevelCount(); ++level)
	{
		D3DLOCKED_RECT lr;
		if (FAILED(gTerrainTex->LockRect(level, &lr, NULL, 0))) { ok = false; break; }
		for (int y = 0; y < side; ++y) memcpy((unsigned char *)lr.pBits + y * lr.Pitch, texels + y * side, sizeof(DWORD) * side);
		gTerrainTex->UnlockRect(level);
		if (side == 1) break;
		const int half = side / 2;
		for (int y = 0; y < half; ++y)
			for (int x = 0; x < half; ++x)
			{
				const DWORD a = texels[(2 * y) * side + 2 * x], b = texels[(2 * y) * side + 2 * x + 1], c = texels[(2 * y + 1) * side + 2 * x],
							d = texels[(2 * y + 1) * side + 2 * x + 1];
				DWORD out = 0xff000000;
				for (int shift = 0; shift < 24; shift += 8)
					out |= ((((a >> shift) & 0xff) + ((b >> shift) & 0xff) + ((c >> shift) & 0xff) + ((d >> shift) & 0xff) + 2) >> 2) << shift;
				texels[y * half + x] = out;		// in place: the write is never ahead of a read of the same pass
			}
		side = half;
	}
	free(texels);
	return ok;
}

FlatVertex *SkirtQuad(FlatVertex *v, float x0, float y0, float z0, DWORD c0, float x1, float y1, float z1, DWORD c1, float x2, float y2, float z2, DWORD c2, float x3,
					  float y3, float z3, DWORD c3)
{
	const FlatVertex corners[6] = {{x0, y0, z0, c0}, {x1, y1, z1, c1}, {x2, y2, z2, c2}, {x0, y0, z0, c0}, {x2, y2, z2, c2}, {x3, y3, z3, c3}};
	memcpy(v, corners, sizeof corners);
	return v + 6;
}

bool BuildSkirt()
{
	gSkirt = (FlatVertex *)malloc(sizeof(FlatVertex) * kSkirtVerts);
	if (!gSkirt) return false;
	FlatVertex *v = gSkirt;
	const float side = float(kCells), water = MAP.GetWaterLevel();
	for (int i = 0; i < kCells; i += kSkirtStep)
	{
		const int p = i / kSkirtStep;
		const float a = float(i), b = float(i + kSkirtStep);
		const float south0 = MAP.GetHeight(i, 0), south1 = MAP.GetHeight(i + kSkirtStep, 0), north0 = MAP.GetHeight(i, kCells), north1 = MAP.GetHeight(i + kSkirtStep, kCells);
		const float west0 = MAP.GetHeight(0, i), west1 = MAP.GetHeight(0, i + kSkirtStep), east0 = MAP.GetHeight(kCells, i), east1 = MAP.GetHeight(kCells, i + kSkirtStep);
		const DWORD s0 = gEdgeColour[kSouth][p], s1 = gEdgeColour[kSouth][p + 1], n0 = gEdgeColour[kNorth][p], n1 = gEdgeColour[kNorth][p + 1];
		const DWORD w0 = gEdgeColour[kWest][p], w1 = gEdgeColour[kWest][p + 1], e0 = gEdgeColour[kEast][p], e1 = gEdgeColour[kEast][p + 1];
		for (int band = 0; band < kSkirtBands; ++band)
		{
			const float n = kSkirtReach[band], f = kSkirtReach[band + 1];
			const int o = band + 1;
			v = SkirtQuad(v, a, -n, SkirtZ(south0, band, water), s0, b, -n, SkirtZ(south1, band, water), s1, b, -f, SkirtZ(south1, o, water), s1, a, -f,
						  SkirtZ(south0, o, water), s0);
			v = SkirtQuad(v, a, side + n, SkirtZ(north0, band, water), n0, b, side + n, SkirtZ(north1, band, water), n1, b, side + f, SkirtZ(north1, o, water), n1, a,
						  side + f, SkirtZ(north0, o, water), n0);
			v = SkirtQuad(v, -n, a, SkirtZ(west0, band, water), w0, -n, b, SkirtZ(west1, band, water), w1, -f, b, SkirtZ(west1, o, water), w1, -f, a,
						  SkirtZ(west0, o, water), w0);
			v = SkirtQuad(v, side + n, a, SkirtZ(east0, band, water), e0, side + n, b, SkirtZ(east1, band, water), e1, side + f, b, SkirtZ(east1, o, water), e1,
						  side + f, a, SkirtZ(east0, o, water), e0);
		}
	}
	const float corner[4][3] = {{0.f, 0.f, MAP.GetHeight(0, 0)}, {side, 0.f, MAP.GetHeight(kCells, 0)}, {0.f, side, MAP.GetHeight(0, kCells)},
								{side, side, MAP.GetHeight(kCells, kCells)}};
	const DWORD cornerColour[4] = {gEdgeColour[kSouth][0], gEdgeColour[kSouth][kSkirtPoints - 1], gEdgeColour[kNorth][0], gEdgeColour[kNorth][kSkirtPoints - 1]};
	for (int k = 0; k < 4; ++k)
	{
		const float sx = corner[k][0] > 0.f ? 1.f : -1.f, sy = corner[k][1] > 0.f ? 1.f : -1.f, z = corner[k][2];
		const DWORD c = cornerColour[k];
		for (int bx = 0; bx < kSkirtBands; ++bx)
			for (int by = 0; by < kSkirtBands; ++by)
			{
				const float x0 = corner[k][0] + sx * kSkirtReach[bx], x1 = corner[k][0] + sx * kSkirtReach[bx + 1];
				const float y0 = corner[k][1] + sy * kSkirtReach[by], y1 = corner[k][1] + sy * kSkirtReach[by + 1];
				// each point as far down as the further of its two bands, which is what the edges' bands beside it have there
				v = SkirtQuad(v, x0, y0, SkirtZ(z, bx > by ? bx : by, water), c, x1, y0, SkirtZ(z, bx + 1 > by ? bx + 1 : by, water), c, x1, y1,
							  SkirtZ(z, (bx > by ? bx : by) + 1, water), c, x0, y1, SkirtZ(z, bx > by + 1 ? bx : by + 1, water), c);
			}
	}
	return v == gSkirt + kSkirtVerts;
}

bool BuildMesh(IDirect3DDevice9 *device)
{
	if (FAILED(device->CreateVertexBuffer(kTerrainVerts * sizeof(WorldVertex), D3DUSAGE_WRITEONLY, kWorldFvf, D3DPOOL_MANAGED, &gTerrainVb, NULL)) ||
		!gTerrainVb) return false;
	if (FAILED(device->CreateIndexBuffer(kTerrainIndices * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &gTerrainIb, NULL)) ||
		!gTerrainIb) return false;
	WorldVertex *v = NULL;
	if (FAILED(gTerrainVb->Lock(0, 0, (void **)&v, 0)) || !v) return false;
	for (int py = 0; py < kPieces; ++py)
		for (int px = 0; px < kPieces; ++px)
			for (int j = 0; j <= kPieceQuads; ++j)
				for (int i = 0; i <= kPieceQuads; ++i)
				{
					const int x = (px * kPieceQuads + i) * kStep, y = (py * kPieceQuads + j) * kStep;
					v->x = float(x); v->y = float(y); v->z = MAP.GetHeight(x, y);
					v->u = (float(x) + 0.5f) / float(kTexSide); v->v = (float(y) + 0.5f) / float(kTexSide);
					++v;
				}
	gTerrainVb->Unlock();
	WORD *index = NULL;
	if (FAILED(gTerrainIb->Lock(0, 0, (void **)&index, 0)) || !index) return false;
	for (int j = 0; j < kPieceQuads; ++j)
		for (int i = 0; i < kPieceQuads; ++i)
		{
			const WORD a = WORD(j * (kPieceQuads + 1) + i), b = WORD(a + 1), c = WORD(a + kPieceQuads + 1), d = WORD(c + 1);
			*index++ = a; *index++ = b; *index++ = c; *index++ = b; *index++ = d; *index++ = c;
		}
	gTerrainIb->Unlock();
	return true;
}

// The picture's device objects, made on the first frame of a level's view and kept until the view is released.
bool Ready(IDirect3DDevice9 *device)
{
	const int level = GAME.GetCurrentLevel();
	if (gDevice != device || gBuiltLevel != level) CeCmdViewRelease();
	if (gTerrainVb && gTerrainIb && gTerrainTex) return true;
	if (gBuildFailed) return false;
	gDevice = device; gBuiltLevel = level;
	if (MAP.mGridWidth != kCells || MAP.mGridHeight != kCells || !BuildMesh(device) || !BuildTexture(device) || !BuildSkirt())
	{
		Drop(gTerrainVb); Drop(gTerrainIb); Drop(gTerrainTex);
		free(gSkirt); gSkirt = NULL;
		gBuildFailed = true;
		return false;
	}
	return true;
}

// ---- the screen batch ----------------------------------------------------------------------------------------------------------------------

ScreenVertex *Room(unsigned vertices, bool font)
{
	if (gScreenCount + vertices > gScreenRoom)
	{
		unsigned room = gScreenRoom ? gScreenRoom * 2 : 4096;
		while (room < gScreenCount + vertices) room *= 2;
		ScreenVertex *grown = (ScreenVertex *)realloc(gScreen, room * sizeof *gScreen);
		if (!grown) return NULL;
		gScreen = grown; gScreenRoom = room;
	}
	if (!gRunCount || gRuns[gRunCount - 1].font != font)
	{
		if (gRunCount == gRunRoom)
		{
			const unsigned room = gRunRoom ? gRunRoom * 2 : 64;
			Run *grown = (Run *)realloc(gRuns, room * sizeof *gRuns);
			if (!grown) return NULL;
			gRuns = grown; gRunRoom = room;
		}
		gRuns[gRunCount].first = gScreenCount; gRuns[gRunCount].count = 0; gRuns[gRunCount].font = font;
		++gRunCount;
	}
	ScreenVertex *out = gScreen + gScreenCount;
	gScreenCount += vertices; gRuns[gRunCount - 1].count += vertices;
	return out;
}

void Set(ScreenVertex *v, float x, float y, DWORD colour, float tu = 0.f, float tv = 0.f)
{
	v->x = x - 0.5f; v->y = y - 0.5f; v->z = 0.f; v->rhw = 1.f; v->colour = colour; v->u = tu; v->v = tv;
}

bool gCollectOnly=false;
void Triangle(float ax, float ay, float bx, float by, float cx, float cy, DWORD colour)
{
    if(gCollectOnly) return;
	if (ScreenVertex *v = Room(3, false)) { Set(v, ax, ay, colour); Set(v + 1, bx, by, colour); Set(v + 2, cx, cy, colour); }
}

void Quad(float x0, float y0, float x1, float y1, DWORD colour)
{
	Triangle(x0, y0, x1, y0, x1, y1, colour);
	Triangle(x0, y0, x1, y1, x0, y1, colour);
}

void Segment(float ax, float ay, float bx, float by, float width, DWORD colour)
{
	float dx = bx - ax, dy = by - ay;
	const float n = sqrtf(dx * dx + dy * dy);
	if (n < 0.01f) return;
	dx = dx / n * width * 0.5f; dy = dy / n * width * 0.5f;
	Triangle(ax - dy, ay + dx, bx - dy, by + dx, bx + dy, by - dx, colour);
	Triangle(ax - dy, ay + dx, bx + dy, by - dx, ax + dy, ay - dx, colour);
}

void Flush(IDirect3DDevice9 *device)
{
	if (!gScreenCount) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	IDirect3DTexture9 *fontTexture = font && font->mTexture ? font->mTexture->GetTexture() : NULL;
	device->SetFVF(kScreenFvf);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	for (unsigned r = 0; r < gRunCount; ++r)
	{
		const Run &run = gRuns[r];
		if (run.count < 3) continue;
		if (run.font && fontTexture)
		{
			device->SetTexture(0, fontTexture);
			device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		}
		else
		{
			device->SetTexture(0, NULL);
			device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
			device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
		}
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, run.count / 3, gScreen + run.first, sizeof(ScreenVertex));
#ifdef CE_COMMANDER_TEST
		gStats.vertices += run.count;
#endif
	}
	gScreenCount = 0; gRunCount = 0;
}

// ---- the marks -----------------------------------------------------------------------------------------------------------------------------

// A world point on the window, with how many pixels a world unit takes there. False behind the camera or far outside the window.
bool Place(float wx, float wy, float wz, float *px, float *py, float *scale)
{
	const double point[4] = {wx, wy, wz, 1}; double clip[4];
	CeViewDetail::Transform(gCombined, point, clip);
	if (!(clip[3] > 1e-3)) return false;
	const double nx = clip[0] / clip[3], ny = clip[1] / clip[3];
	if (!(nx > -1.5 && nx < 1.5 && ny > -1.5 && ny < 1.5)) return false;
	*px = gView.viewport.x + float(nx + 1) * 0.5f * gView.viewport.width;
	*py = gView.viewport.y + float(1 - ny) * 0.5f * gView.viewport.height;
	*scale = float(0.5 * gView.viewport.width * gView.projection[0] / clip[3]);
	return true;
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// A chevron pointing along (dx, dy), a unit vector on the window.
void Chevron(float x, float y, float dx, float dy, float size, DWORD colour)
{
	const float nx = -dy, ny = dx;
	const float tipX = x + dx * size, tipY = y + dy * size, notchX = x - dx * size * 0.35f, notchY = y - dy * size * 0.35f;
	const float backX = x - dx * size * 0.8f, backY = y - dy * size * 0.8f, wing = size * 0.75f;
	Triangle(tipX, tipY, backX + nx * wing, backY + ny * wing, notchX, notchY, colour);
	Triangle(tipX, tipY, notchX, notchY, backX - nx * wing, backY - ny * wing, colour);
}

void Diamond(float x, float y, float size, DWORD colour)
{
	Triangle(x, y - size, x + size, y, x, y + size, colour);
	Triangle(x, y - size, x, y + size, x - size, y, colour);
}

// The layer a thing's mark is drawn in, lowest first: what stands, what drives and walks, what flies and what is fired, and the pilot over all.
enum { kLayerStanding, kLayerGround, kLayerAir, kLayerPilot, kLayers };

int LayerOf(ULONG type, bool isPilot)
{
	if (isPilot) return kLayerPilot;
	if (type & THING_TYPE_EXPLOSION) return kLayerAir;
	if ((type & THING_TYPE_AMMUNITION) && !(type & THING_TYPE_UNIT)) return kLayerAir;
	if (type & THING_TYPE_AIR_UNIT) return kLayerAir;
	if (type & (THING_TYPE_BUILDING | THING_TYPE_EMPLACEMENT)) return kLayerStanding;
	return kLayerGround;
}

CeCmdViewUnitMark *gUnitMarks;
unsigned gUnitMarkCount, gUnitMarkRoom;

void KeepUnitMark(CUnit* unit, int squad, float x, float y, float radius, const FVector &pos, float worldRadius, float ppu)
{
	if (gUnitMarkCount == gUnitMarkRoom) {
		if (gUnitMarkRoom > (~0u / sizeof(CeCmdViewUnitMark)) / 2) return;
		const unsigned room = gUnitMarkRoom ? gUnitMarkRoom * 2 : 64;
		void *storage = realloc(gUnitMarks, room * sizeof(CeCmdViewUnitMark));
		if (!storage) return;
		gUnitMarks = static_cast<CeCmdViewUnitMark *>(storage);
		gUnitMarkRoom = room;
	}
	CeCommandLifetimeKey key = {};
    if (!CeCmdUnitVisibleKey(unit, &key)) return;
    gUnitMarks[gUnitMarkCount++] = {squad, x, y, radius, key, pos.X, pos.Y, pos.Z, worldRadius, ppu};
}

void Mark(CThing *thing, int friendly, const void *pilot, int layer)
{
	const ULONG type = thing->GetThingType();
	const short flags = thing->GetFlags();
	if (flags & (TF_INVISIBLE | TF_DECLARED_SHUTDOWN)) return;
	if (LayerOf(type, (const void *)thing == pilot) != layer) return;
	const FVector &pos = thing->GetPos();
	float x, y, scale;
	if (type & THING_TYPE_EXPLOSION)
	{
		if (Place(pos.X, pos.Y, pos.Z, &x, &y, &scale)) { const float s = Clamp(scale * 3.f, 3.f, 12.f); Diamond(x, y, s, FadeColour(kBlastColour)); }
		return;
	}
	if ((type & THING_TYPE_AMMUNITION) && !(type & THING_TYPE_UNIT))
	{
		if (Place(pos.X, pos.Y, pos.Z, &x, &y, &scale)) Quad(x - 1.5f, y - 1.5f, x + 1.5f, y + 1.5f, FadeColour(kRoundColour));
		return;
	}
	if (!(type & THING_TYPE_UNIT) || (type & THING_TYPE_COMPONENT) || (flags & TF_DYING)) return;
	CUnit *unit = dynamic_cast<CUnit *>(thing);
	if (!unit || !Place(pos.X, pos.Y, pos.Z, &x, &y, &scale)) return;
	const int side = (int)unit->GetAllegiance();
	float markRadius, worldRadius;
	const bool isPilot = (const void *)thing == pilot;
	const DWORD colour = FadeColour(isPilot ? kPilotColour : side == friendly ? kFriendColour : side == (int)kNeutralAllegiance ? kNeutralColour : kEnemyColour);
	const DWORD edge = FadeColour(kEdgeColour);
	// Where it faces, on the window: the orientation's forward column, a few units ahead.
	const FMatrix &o = unit->GetOrientation();
	float fx, fy, fs, dx = 0.f, dy = -1.f;
	if (Place(pos.X + o.Row[0].Y * 4.f, pos.Y + o.Row[1].Y * 4.f, pos.Z + o.Row[2].Y * 4.f, &fx, &fy, &fs))
	{
		const float n = sqrtf((fx - x) * (fx - x) + (fy - y) * (fy - y));
		if (n > 0.05f) { dx = (fx - x) / n; dy = (fy - y) / n; }
	}
	if (isPilot)
	{
		const float s = Clamp(scale * 5.f, 8.f, 16.f);
		markRadius = s; worldRadius = 3.f;
		Diamond(x, y, s + 2.f, edge);
		Diamond(x, y, s, colour);
		Diamond(x, y, s * 0.55f, edge);
		Chevron(x, y, dx, dy, s * 0.5f, colour);
	}
	else if (type & THING_TYPE_AIR_UNIT)
	{
		// An aircraft: a line down to the ground under it, and a slimmer mark.
		float gx, gy, gs;
		if (Place(pos.X, pos.Y, MAP.Collide(FVector(pos.X, pos.Y, 0)), &gx, &gy, &gs))
		{
			Segment(x, y, gx, gy, 1.f, (colour & 0x00ffffff) | ((colour >> 25) << 24));
			Quad(gx - 1.5f, gy - 1.5f, gx + 1.5f, gy + 1.5f, edge);
		}
		const float s = Clamp(scale * 3.5f, 6.f, 13.f);
		markRadius = s; worldRadius = 1.6f;
		Chevron(x, y, dx, dy, s + 2.f, edge);
		Chevron(x, y, dx, dy, s, colour);
		Quad(x - 1.f, y - 1.f, x + 1.f, y + 1.f, 0xffffffff & FadeColour(0xffffffff));
	}
	else if (type & (THING_TYPE_BUILDING | THING_TYPE_EMPLACEMENT))
	{
		const float s = Clamp(scale * 4.f, 4.f, 12.f);
		markRadius = s; worldRadius = 4.f;	// what stands: the building's own footprint is larger; the ring marks its middle
		Quad(x - s - 1.5f, y - s - 1.5f, x + s + 1.5f, y + s + 1.5f, edge);
		Quad(x - s, y - s, x + s, y + s, colour);
		Quad(x - s * 0.5f, y - s * 0.5f, x + s * 0.5f, y + s * 0.5f, edge);
	}
	else if (type & THING_TYPE_INFANTRY)
	{
		const float s = Clamp(scale * 1.2f, 2.f, 4.f);
		markRadius = s; worldRadius = 0.6f;
		Quad(x - s - 1.f, y - s - 1.f, x + s + 1.f, y + s + 1.f, edge);
		Quad(x - s, y - s, x + s, y + s, colour);
	}
	else
	{
		const float s = Clamp(scale * ((type & THING_TYPE_MECH) ? 4.5f : 3.2f), 5.f, 13.f);
		markRadius = s; worldRadius = (type & THING_TYPE_MECH) ? 2.f : 1.4f;	// about a tank's half length (measured on 851's frames)
		Chevron(x, y, dx, dy, s + 2.f, edge);
		Chevron(x, y, dx, dy, s, colour);
	}
	if (!isPilot && side == friendly) {
		const int squad = CeCmdDrawnSquad(unit->GetSquad());
		if (squad >= 0) KeepUnitMark(unit, squad, x, y, markRadius, pos, worldRadius, scale);
	}
#ifdef CE_COMMANDER_TEST
	if(!gCollectOnly) ++gStats.markers;
#endif
}

DWORD FloatBits(float f) { DWORD d; memcpy(&d, &f, sizeof d); return d; }

void Text(const char *text, float x, float y, float scale, DWORD colour, CDXBitmapFont *font)
{
	const float texW = float(font->mTexWidth), texH = float(font->mTexHeight);
	x = floorf(x); y = floorf(y);
	for (const unsigned char *c = (const unsigned char *)text; *c; ++c)
	{
		const int cell = font->RemapChar(WCHAR(*c)) & 255;
		const float *t = font->mTexCoords[cell];
		const float w = (t[2] - t[0]) * texW * scale, h = (t[3] - t[1]) * texH * scale;
		if (ScreenVertex *v = Room(6, true))
		{
			Set(v, x, y, colour, t[0], t[1]); Set(v + 1, x + w, y, colour, t[2], t[1]); Set(v + 2, x + w, y + h, colour, t[2], t[3]);
			Set(v + 3, x, y, colour, t[0], t[1]); Set(v + 4, x + w, y + h, colour, t[2], t[3]); Set(v + 5, x, y + h, colour, t[0], t[3]);
		}
		x += w;
		if (!font->mSysFont) x += scale;
	}
}
}

// ---- the interface -------------------------------------------------------------------------------------------------------------------------

bool CeCmdViewMatrices(const CeCmdViewCamera &camera, float width, float height, float nearZ, float farZ, CeWorldViewSample *out)
{
	if (!out || !(width > 0.f) || !(height > 0.f) || !(camera.zoom > 1e-4f) || !(nearZ > 0.f) || !(farZ > nearZ)) return false;
	// The game's own construction (CEngine::GetViewMatrixFromCamera, CRenderInfo::SetView): a quarter turn about X times the camera's
	// orientation transposed, with the camera's place under it, inverted.
	FMatrix pitch;
	pitch.MakeRotationPitchF(1.570796f);
	FMatrix orientation;
	for (int r = 0; r < 3; ++r) orientation.Row[r] = FVector(camera.rows[r][0], camera.rows[r][1], camera.rows[r][2]);
	orientation.TransposeInPlace();
	const FMatrix rows = pitch * orientation;
	const double world[16] = {rows.Row[0].X, rows.Row[0].Y, rows.Row[0].Z, 0, rows.Row[1].X, rows.Row[1].Y, rows.Row[1].Z, 0,
							  rows.Row[2].X, rows.Row[2].Y, rows.Row[2].Z, 0, camera.pos[0], camera.pos[1], camera.pos[2], 1};
	double inverse[16];
	if (!CeViewDetail::Inverse(world, inverse)) return false;
	CeWorldViewSample sample = {};
	for (int i = 0; i < 16; ++i) sample.view[i] = float(inverse[i]);
	// CRenderInfo::SetProjection with a width of near times zoom, and square pixels.
	sample.projection[0] = 1.f / camera.zoom;
	sample.projection[5] = (width / height) / camera.zoom;
	sample.projection[10] = farZ / (farZ - nearZ);
	sample.projection[11] = 1.f;
	sample.projection[14] = -(nearZ * farZ) / (farZ - nearZ);
	sample.viewport.x = 0.f; sample.viewport.y = 0.f; sample.viewport.width = width; sample.viewport.height = height;
	sample.viewport.minDepth = 0.f; sample.viewport.maxDepth = 1.f;
	double combined[16], check[16];
	if (!CeViewDetail::Combined(sample.view, sample.projection, combined) || !CeViewDetail::Inverse(combined, check)) return false;
	*out = sample;
	return true;
}

bool CeCmdViewBegin(const CeWorldViewSample &view, float fade)
{
	if (gDrawing) return false;
	IDirect3DDevice9 *device = LT.GetDevice();
	if (!device || !CeViewDetail::Valid(view.viewport) || !CeViewDetail::Combined(view.view, view.projection, gCombined)) return false;
	if (!Ready(device)) return false;
	if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &gKept)) || !gKept) { gKept = NULL; return false; }
	gView = view;
	gFade = fade < 0.f ? 0.f : (fade > 1.f ? 1.f : fade);
	gScreenCount = 0; gRunCount = 0;
	gDrawing = true;
#ifdef CE_COMMANDER_TEST
	++gStats.frames; gStats.terrainChunks = 0; gStats.markers = 0; gStats.vertices = 0;
#endif

	D3DVIEWPORT9 viewport;
	viewport.X = (DWORD)view.viewport.x; viewport.Y = (DWORD)view.viewport.y; viewport.Width = (DWORD)view.viewport.width;
	viewport.Height = (DWORD)view.viewport.height; viewport.MinZ = 0.f; viewport.MaxZ = 1.f;
	device->SetViewport(&viewport);
	const DWORD fog = MAP.GetFog() | 0xff000000;
	const bool whole = gFade >= 1.f;
	device->Clear(0, NULL, whole ? (D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET) : D3DCLEAR_ZBUFFER, fog, 1.f, 0);

	// The fixed pipeline, every state the picture depends on set here (the state block puts the game's back).
	device->SetVertexShader(NULL);
	device->SetPixelShader(NULL);
	static const D3DMATRIX identity = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
	D3DMATRIX matrix;
	device->SetTransform(D3DTS_WORLD, &identity);
	memcpy(&matrix, view.view, sizeof matrix); device->SetTransform(D3DTS_VIEW, &matrix);
	memcpy(&matrix, view.projection, sizeof matrix); device->SetTransform(D3DTS_PROJECTION, &matrix);
	device->SetTransform(D3DTS_TEXTURE0, &identity);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	device->SetRenderState(D3DRS_CLIPPING, TRUE);
	device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
	device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, ((DWORD)(gFade * 255.f) << 24) | 0x00ffffff);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
	device->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
	device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
	if (!whole) Quad(view.viewport.x, view.viewport.y, view.viewport.x + view.viewport.width, view.viewport.y + view.viewport.height, FadeColour(fog));
	return true;
}

void CeCmdViewWorld(int friendlyAllegiance, const void *pilot)
{
	gUnitMarkCount = 0;
	if (!gDrawing) return;
	IDirect3DDevice9 *device = gDevice;
	Flush(device);		// the backdrop of an entering view, under the world
	const DWORD fog = MAP.GetFog() | 0xff000000;
	const bool whole = gFade >= 1.f;
	// Haze toward the horizon, from the camera's distance to the middle of the picture.
	double inverse[16];
	float reach = 200.f;
	{
		double view[16];
		for (int i = 0; i < 16; ++i) view[i] = gView.view[i];
		if (CeViewDetail::Inverse(view, inverse))
		{
			const float height = MAP.Collide(FVector(float(inverse[12]), float(inverse[13]), 0)) - float(inverse[14]);
			if (height > reach) reach = height;
		}
	}
	const float fogStart = reach * 1.6f, fogEnd = reach * 7.f;

	// The terrain.
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	device->SetRenderState(D3DRS_FOGENABLE, TRUE);
	device->SetRenderState(D3DRS_FOGCOLOR, fog);
	device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	device->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	device->SetRenderState(D3DRS_FOGSTART, FloatBits(fogStart));
	device->SetRenderState(D3DRS_FOGEND, FloatBits(fogEnd));
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, whole ? FALSE : TRUE);
	device->SetFVF(kWorldFvf);
	device->SetTexture(0, gTerrainTex);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetStreamSource(0, gTerrainVb, 0, sizeof(WorldVertex));
	device->SetIndices(gTerrainIb);
	for (int piece = 0; piece < kPieces * kPieces; ++piece)
	{
		device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, piece * kPieceVerts, 0, kPieceVerts, 0, kPieceQuads * kPieceQuads * 2);
#ifdef CE_COMMANDER_TEST
		++gStats.terrainChunks;
#endif
	}
	// The ground past the map's edges, in its own colours (the alpha is still the picture's fade).
	device->SetFVF(kFlatFvf);
	device->SetTexture(0, NULL);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, kSkirtVerts / 3, gSkirt, sizeof(FlatVertex));
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

	// The water: a sheet at its level, over the ground below it.
	if (!gWater) gWater = (FlatVertex *)malloc(sizeof(FlatVertex) * kWaterVerts);
	if (gWater)
	{
		const float level = MAP.GetWaterLevel();
		DWORD tint;		// the water's texture factor, as the game's own water reads it from the level's properties
		memcpy(&tint, MAP.GetHFProperties().mUnknown24, sizeof tint);
		const DWORD alpha = (DWORD)(150.f * gFade);
		const DWORD colour = (alpha << 24) | (((tint >> 16) & 0xff) / 2 + 20) << 16 | ((((tint >> 8) & 0xff) / 2 + 45) << 8) | ((tint & 0xff) / 2 + 70);
		// A grid of squares well past the map on every side, so that the sea runs to the horizon and the haze, worked out at each corner, has
		// corners near enough to mean something.
		device->SetFVF(kFlatFvf);
		device->SetTexture(0, NULL);
		device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
		device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
		FlatVertex *v = gWater;
		for (int j = 0; j < kWaterSquares; ++j)
			for (int i = 0; i < kWaterSquares; ++i)
			{
				const float x0 = kWaterFrom + float(i) * kWaterSquare, y0 = kWaterFrom + float(j) * kWaterSquare, x1 = x0 + kWaterSquare, y1 = y0 + kWaterSquare;
				const FlatVertex corners[6] = {{x0, y0, level, colour}, {x1, y0, level, colour}, {x1, y1, level, colour},
											   {x0, y0, level, colour}, {x1, y1, level, colour}, {x0, y1, level, colour}};
				memcpy(v, corners, sizeof corners);
				v += 6;
			}
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, kWaterVerts / 3, gWater, sizeof(FlatVertex));
	}

	// The things, layer by layer and in the world's own order within each: every unit by its side, rounds and explosions as small marks.
	const SetView *things = (const SetView *)&WORLD.GetThingNB();
	for (int layer = 0; layer < kLayers; ++layer)
	{
		int guard = 0;
		for (const NodeView *n = things->first; n && guard < 65536; n = n->next, ++guard)
			if (n->item) Mark((CThing *)n->item, friendlyAllegiance, pilot, layer);
	}
	Flush(device);
}
void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot)
{
    gUnitMarkCount=0;
    gView=sample;
    CeViewDetail::Combined(sample.view,sample.projection,gCombined);
    gCollectOnly=true;
	const SetView *things = (const SetView *)&WORLD.GetThingNB();
	for (int layer = 0; layer < kLayers; ++layer)
	{
		int guard = 0;
		for (const NodeView *n = things->first; n && guard < 65536; n = n->next, ++guard)
			if (n->item) Mark((CThing *)n->item, friendlyAllegiance, pilot, layer);
	}
	    gCollectOnly=false;
}


const CeCmdViewUnitMark *CeCmdViewUnitMarks(unsigned &count)
{
	count = gUnitMarkCount;
	return gUnitMarks;
}

void CeCmdViewBox(float x, float y, float w, float h, unsigned colour)
{
	if (!gDrawing || w <= 0.f || h <= 0.f) return;
	Quad(x, y, x + w, y + h, FadeColour(colour));
}

void CeCmdViewText(float x, float y, unsigned colour, const char *text, float scale)
{
	if (!gDrawing || !text) return;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || !font->mTexture || !font->mTexWidth || !font->mTexHeight) return;		// the font is made by the game's first use of it; never by this view
	Text(text, x + 1.f, y + 1.f, scale, FadeColour(colour & 0xff000000), font);
	Text(text, x, y, scale, FadeColour(colour), font);
}

float CeCmdViewTextWidth(const char *text)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font || !font->mTexture || !text) return 0.f;
	float width = 0.f;
	for (const unsigned char *c = (const unsigned char *)text; *c; ++c)
	{
		const float *t = font->mTexCoords[font->RemapChar(WCHAR(*c)) & 255];
		width += (t[2] - t[0]) * float(font->mTexWidth);
		if (!font->mSysFont) width += 1.f;
	}
	return width;
}

float CeCmdViewTextHeight()
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	return font ? float(font->GetHeight()) : 0.f;
}

void CeCmdViewEnd()
{
	if (!gDrawing) return;
	Flush(gDevice);
	gDrawing = false;
	if (gKept) { gKept->Apply(); gKept->Release(); gKept = NULL; }
}

bool CeCmdViewDrawing() { return gDrawing; }

void CeCmdViewRelease()
{
	if (gKept) { gKept->Release(); gKept = NULL; }
	free(gUnitMarks); gUnitMarks = NULL; gUnitMarkCount = gUnitMarkRoom = 0;
	gDrawing = false;
	Drop(gTerrainVb); Drop(gTerrainIb); Drop(gTerrainTex);
	free(gScreen); gScreen = NULL; gScreenCount = gScreenRoom = 0;
	free(gRuns); gRuns = NULL; gRunCount = gRunRoom = 0;
	free(gWater); gWater = NULL;
	free(gSkirt); gSkirt = NULL;
	gDevice = NULL; gBuiltLevel = -1; gBuildFailed = false;
}

#ifdef CE_COMMANDER_TEST
void CeCmdViewGetStats(CeCmdViewStats *out) { *out = gStats; }
void CeCmdViewCountFrame() { ++gStats.frames; }
#endif

#endif
