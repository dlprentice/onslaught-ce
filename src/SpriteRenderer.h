// SpriteRenderer.h: the sprite renderer (DXSpriteRenderer.cpp; only what matched files need). DrawColouredSprite is 005563d0, a cdecl
// function of eleven arguments.

#ifndef SPRITERENDERER_H
#define SPRITERENDERER_H

class CDXTexture;

enum ESpriteAlignment
{
	SA_TOP_LEFT = 0,				// CFrontEnd::DrawPanel (00467041)
	SA_CENTRE = 4,					// CFrontEnd::DrawLine (00466e13)
	SA_8 = 8						// CScaleMenuItem::Render's bar segments (004a3a3b); name open
};

// The sprite types DrawSprite switches on (00555d87). SPT_SCROLL_RIGHT is Stuart's (references/Onslaught FEPGoodies.cpp:1904,
// the goodie ring that CFEPGoodies::Render 0045e0d0 draws with type 4); the other names are ours.
enum ESpriteType
{
	SPT_NONE = 0,
	SPT_FILL_RIGHT = 1,				// the left part a of the width, u from 0 to a
	SPT_FILL_LEFT = 2,				// the right part a of the width, u from a to 0
	SPT_SCROLL_V = 3,				// v offset by fmod(a, 1)
	SPT_SCROLL_RIGHT = 4			// u offset by fmod(a, 1)
};

class CDXSpriteRenderer
{
public:
	static void	DrawColouredSprite(float x, float y, float z, CDXTexture *tex, DWORD col, float xscale = 1.f, float yscale = 1.f,
								   ESpriteAlignment align = SA_TOP_LEFT, ESpriteType type = SPT_NONE, float a = 1.f, float yaw = 0.f);
	// 00555be0: cdecl, fifteen arguments (CScaleMenuItem::Render 004a38ee)
	static void	DrawSprite(float x, float y, float z, CDXTexture *tex, ESpriteAlignment align, ESpriteType type, float a, float yaw,
						   DWORD col, float xscale, float yscale, float u0 = 0.f, float u1 = 1.f, float v0 = 0.f, float v1 = 1.f);

	static UBYTE	mBatch;			// 009c7fc8: nonzero adds each quad to FASTVB's batch instead of drawing it (CDXCompass::Render
									// raises it, 004272b8..00427a6c; name ours)
};

#define CSPRITERENDERER CDXSpriteRenderer

#endif
