// SPDX-License-Identifier: GPL-3.0-only
// Host checks of the paused Commander map's terrain texture (port/game/ce_cmdmap.cpp: DrawTerrainFromTexture and FreeTerrainTexture, included from
// the generated ce_cmdmap_terrain.inc), with authored fakes for the game's texture, sprite renderer and device.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>

typedef unsigned int DWORD;			// the game's DWORD is 32 bits
typedef int BOOL;
typedef long HRESULT;
#define TRUE 1
#define FALSE 0
#define FAILED(h) ((h) < 0)
#define MEMTYPE_TEXTURE 7
#define TEXFMT_A8R8G8B8 21
#define D3DLOCK_NOSYSLOCK 0x800
#define D3DSAMP_MAGFILTER 5
#define D3DSAMP_MINFILTER 6
#define D3DTEXF_POINT 1
#define D3DTEXF_LINEAR 2

static const int kGrid = 128;
static int checks, failures;
#define CHECK(cond, label) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s (line %d)\n", label, __LINE__); } } while (0)

struct D3DLOCKED_RECT { int Pitch; void *pBits; };
static bool gFailLock, gFailCreate;
static int gLocks, gUnlocks, gCreates, gReleases, gSprites, gRenders;
struct SpriteCall { float x, y, z; DWORD colour; float sx, sy; const void *texture; bool batch; DWORD magAtDraw, minAtDraw; float u0, u1, v0, v1; };
enum ESpriteAlignment { SA_TOP_LEFT = 0 };
enum ESpriteType { SPT_NONE = 0 };
static std::vector<SpriteCall> gSpriteCalls;

struct FakeSurface
{
	std::vector<unsigned char> bytes;
	int pitch = 0, height = 0;
	HRESULT LockRect(int, D3DLOCKED_RECT *lr, const void *, DWORD flags)
	{
		++gLocks;
		if (gFailLock) return -1;
		if (flags != D3DLOCK_NOSYSLOCK) return -2;
		lr->Pitch = pitch;
		lr->pBits = bytes.data();
		return 0;
	}
	HRESULT UnlockRect(int) { ++gUnlocks; return 0; }
};

struct CDXTexture
{
	FakeSurface surface;
	bool ok = false;
	int width = 0, height = 0;
	static void *operator new(size_t n, int) { return ::operator new(n); }
	static void operator delete(void *p, int) { ::operator delete(p); }
	static void operator delete(void *p) { ::operator delete(p); }
	void InitEmpty(int w, int h, int format, int, int, int)
	{
		++gCreates;
		(void)format;
		ok = !gFailCreate;
		width = w;
		height = h;
		surface.pitch = w * 4 + 128;			// a pitch wider than the row: a copy that ignores it lands in the wrong place
		surface.height = h;
		surface.bytes.assign((size_t)surface.pitch * h, 0xcc);
	}
	FakeSurface *GetTexture() { return ok ? &surface : NULL; }
	int GetRealWidth() { return width; }
	int GetRealHeight() { return height; }
	void Release() { ++gReleases; delete this; }
};

struct FakeDevice
{
	DWORD mag = D3DTEXF_LINEAR, min = D3DTEXF_LINEAR;
	std::vector<DWORD> history;
	HRESULT GetSamplerState(int, int type, DWORD *value) { *value = type == D3DSAMP_MAGFILTER ? mag : min; return 0; }
	HRESULT SetSamplerState(int, int type, DWORD value) { (type == D3DSAMP_MAGFILTER ? mag : min) = value; history.push_back(value); return 0; }
} gDevice;
typedef FakeDevice IDirect3DDevice9;
typedef FakeSurface IDirect3DTexture9;
struct { IDirect3DDevice9 *GetDevice() { return &gDevice; } } LT;

struct CSPRITERENDERER
{
	static BOOL mBatch;
	static void DrawSprite(float x, float y, float z, CDXTexture *texture, ESpriteAlignment align, ESpriteType type, float a, float yaw, DWORD colour, float sx, float sy,
						   float u0, float u1, float v0, float v1)
	{
		++gSprites;
		gSpriteCalls.push_back({x, y, z, colour, sx, sy, texture, mBatch != 0, gDevice.mag, gDevice.min, u0, u1, v0, v1});
		(void)align; (void)type; (void)a; (void)yaw;
	}
};
BOOL CSPRITERENDERER::mBatch = FALSE;
struct { void Render() { ++gRenders; } } FASTVB;

struct { float mapX, mapY, mapSide; } L;
static DWORD gTerrain[kGrid * kGrid];
static CDXTexture *gTerrainTexture;
static bool gTerrainTextureFailed;

#include "ce_cmdmap_terrain.inc"

static void Fill()
{
	for (int j = 0; j < kGrid; ++j)
		for (int i = 0; i < kGrid; ++i)
			gTerrain[j * kGrid + i] = 0xff000000u | ((DWORD)i << 16) | ((DWORD)j << 8) | (DWORD)((i * 7 + j * 3) & 255);
}

int main(int argc, char **argv)
{
	Fill();
	L.mapX = 40; L.mapY = 30; L.mapSide = 384;
	if (argc > 1 && !strcmp(argv[1], "cells"))
	{
		// BEA_PORT_COMMANDER_TERRAIN=cells (set by the caller): the cells are drawn one by one, and no texture is made
		CHECK(!DrawTerrainFromTexture() && gCreates == 0 && gSprites == 0 && !gTerrainTexture, "the cells switch leaves the old drawing alone");
		printf("terrain texture checks=%d failures=%d\n", checks, failures);
		return failures ? 2 : 0;
	}
	CHECK(DrawTerrainFromTexture(), "the first frame draws from the texture");
	CHECK(gCreates == 1 && gTerrainTexture, "the texture is made once");
	CHECK(gSpriteCalls.size() == 1 && gRenders == 1, "one sprite replaces the cells, and the batch is flushed once");
	const SpriteCall &c = gSpriteCalls[0];
	CHECK(c.x == 40.f && c.y == 30.f && c.colour == 0xffffffffu, "it covers the map's square, on whole pixels, untinted");
	CHECK(c.u0 == 0.999f / 384.f && c.u1 == 1.f + 0.999f / 384.f && c.v0 == 0.999f / 384.f && c.v1 == 1.f + 0.999f / 384.f,
		  "its texture coordinates start a hair under a pixel along, so its cell edges fall where the sprites' do");
	CHECK(c.sx == 384.f / 128 && c.sy == 384.f / 128, "scaled from 128 cells to the map's side");
	CHECK(c.texture == gTerrainTexture, "the sprite is the terrain texture");
	CHECK(c.batch && CSPRITERENDERER::mBatch == FALSE, "batching is on for the draw and off after it");
	CHECK(c.magAtDraw == D3DTEXF_POINT && c.minAtDraw == D3DTEXF_POINT, "cells are not smoothed");
	CHECK(gDevice.mag == D3DTEXF_LINEAR && gDevice.min == D3DTEXF_LINEAR, "the sampler state is put back as it was");
	CHECK(gLocks == 1 && gUnlocks == 1, "one lock and its unlock");
	bool exact = true;
	for (int j = 0; j < kGrid && exact; ++j)
		for (int i = 0; i < kGrid && exact; ++i)
		{
			DWORD v;
			memcpy(&v, gTerrainTexture->surface.bytes.data() + (size_t)j * gTerrainTexture->surface.pitch + i * 4, 4);
			if (v != gTerrain[j * kGrid + i]) exact = false;
		}
	CHECK(exact, "every cell lands in its own place whatever the texture's pitch");
	// the padding beyond the row is untouched
	CHECK(gTerrainTexture->surface.bytes[(size_t)gTerrainTexture->surface.pitch - 1] == 0xcc, "the row padding is not written");

	// the next frame fills it again with the new colours, and makes no second texture
	gTerrain[5] = 0xff123456u;
	CHECK(DrawTerrainFromTexture() && gCreates == 1 && gSprites == 2, "a later frame reuses the texture");
	DWORD v;
	memcpy(&v, gTerrainTexture->surface.bytes.data() + 5 * 4, 4);
	CHECK(v == 0xff123456u, "and shows the new colours");

	// a failed lock: no sprite, the caller draws the cells, the texture stays
	gFailLock = true;
	const int spritesBefore = gSprites;
	CHECK(!DrawTerrainFromTexture() && gSprites == spritesBefore && gTerrainTexture, "a failed lock draws nothing from the texture");
	gFailLock = false;

	// release
	FreeTerrainTexture();
	CHECK(gReleases == 1 && !gTerrainTexture, "closing frees the texture once");
	FreeTerrainTexture();
	CHECK(gReleases == 1, "and freeing again does nothing");

	// a texture that cannot be made: the caller draws the cells, and does not keep asking
	gFailCreate = true;
	const int createsBefore = gCreates;
	CHECK(!DrawTerrainFromTexture() && gTerrainTextureFailed && !gTerrainTexture, "an unusable texture is refused");
	CHECK(gReleases == 2, "and released");
	CHECK(!DrawTerrainFromTexture() && gCreates == createsBefore + 1, "it is not made again every frame");
	gFailCreate = false;
	FreeTerrainTexture();
	CHECK(!gTerrainTextureFailed, "a later opening tries again");
	CHECK(DrawTerrainFromTexture() && gTerrainTexture, "and succeeds when the device does");

	printf("terrain texture checks=%d failures=%d\n", checks, failures);
	return failures ? 2 : 0;
}
