// DXSpriteRenderer.cpp: the PC sprite renderer (CDXSpriteRenderer, CSPRITERENDERER to the platform-independent code).
// Not in Stuart's release; reconstructed from the retail code (00555be0..00556420). A sprite is one textured quad in
// screen space, drawn through the shared screen-quad buffer.


#include	"Common.h"
#define DXSPRITERENDERER_CPP
#include	"mathsconst.h"	// FUNCTION: 0x00555af0 init:ZERO_FVECTOR
						// FUNCTION: 0x00555b10 init:ID_FMATRIX
#include	"SpriteRenderer.h"
#include	"Platform.h"
#include	"Texture.h"
#include	"FastVB.h"
#include	"renderinfo.h"
#include	"ltshell.h"
#include	"frontend.h"

// the screen-quad vertex (FASTVB_FVF)
struct SPRITEVERTEX { D3DXVECTOR4 p; DWORD color; FLOAT tu, tv; };

UBYTE	CDXSpriteRenderer::mBatch;

//******************************************************************************************
// Retail keeps tex in esi from its first read (a byte load of the shift, 00555c4e) and pushes ebx/ebp only around the
// vertex switch: the shifted size comes through CTexture's accessors, whose +8 `this` adjustment makes the base a
// value of its own. Each vertex loop sets i before pos, so i (edx) is live across the pos setup and the x/y/z copies
// take retail's scratch registers.
void	CDXSpriteRenderer::DrawSprite(float x, float y, float z, CDXTexture *tex, ESpriteAlignment align, ESpriteType type,	// FUNCTION: 0x00555be0
									  float a, float yaw, DWORD col, float xscale, float yscale, float u0, float u1, float v0, float v1)
{
	if (z > 0.996f)
		z = (z - 0.996f) * 0.5f + 0.996f;

	if (LT.mTextureStageStates[0][D3DTSS_COLOROP] == D3DTOP_MODULATE2X)
		col = (col & 0xff000000) + ((col >> 1) & 0x7f7f7f);

	float	w = float(tex->GetRealWidth()) * xscale;
	float	h = float(tex->GetRealHeight()) * yscale;

	switch (align)
	{
	case 1:	x -= w;									break;
	case 2:	y -= h;									break;
	case 3:	x -= w;			y -= h;					break;
	case 4:	x -= w * 0.5f;	y -= h * 0.5f;			break;
	case 5:	x -= w * 0.5f;	y -= h;					break;
	case 6:	x -= w * 0.5f;							break;
	case 7:	x -= w;			y -= h * 0.5f;			break;
	case 8:					y -= h * 0.5f;			break;
	}

	float	rhw = 1.f / z;
	if (rhw > 4.f)
		rhw = 4.f;

	D3DXVECTOR4		pos;
	SPRITEVERTEX	verts[4];
	SINT			i;

	switch (type)
	{
	case SPT_NONE:
		i = 0;
		pos.x = x;	pos.y = y;	pos.z = z;
		while (i < 4)
		{
			verts[i].p = pos;
			verts[i].p.x += (i & 1) ? w : 0.f;
			verts[i].p.y += (i & 2) ? h : 0.f;
			verts[i].p.w = rhw;
			verts[i].color = col;
			verts[i].tu = (i & 1) ? u1 : u0;
			verts[i].tv = (i & 2) ? v1 : v0;
			i++;
		}
		break;

	case SPT_FILL_RIGHT:
		i = 0;
		pos.x = x;	pos.y = y;	pos.z = z;
		while (i < 4)
		{
			verts[i].p = pos;
			verts[i].p.x += (i & 1) ? w * a : 0.f;
			verts[i].p.y += (i & 2) ? h : 0.f;
			verts[i].p.w = rhw;
			verts[i].color = col;
			verts[i].tu = (i & 1) ? a : 0.f;
			verts[i].tv = (i & 2) ? v1 : v0;
			i++;
		}
		break;

	case SPT_FILL_LEFT:
		i = 0;
		pos.x = w + x;	pos.y = y;	pos.z = z;
		while (i < 4)
		{
			verts[i].p = pos;
			verts[i].p.x -= (i & 1) ? 0.f : w * a;
			verts[i].p.y += (i & 2) ? h : 0.f;
			verts[i].p.w = rhw;
			verts[i].color = col;
			verts[i].tu = (i & 1) ? 0.f : a;
			verts[i].tv = (i & 2) ? v1 : v0;
			i++;
		}
		break;

	case SPT_SCROLL_V:
	{
		float	off = float(fmod(a, 1.0));
		i = 0;
		pos.x = x;	pos.y = y;	pos.z = z;
		while (i < 4)
		{
			verts[i].p = pos;
			verts[i].p.x += (i & 1) ? w : 0.f;
			verts[i].p.y += (i & 2) ? h : 0.f;
			verts[i].p.w = rhw;
			verts[i].color = col;
			verts[i].tu = (i & 1) ? u1 : u0;
			verts[i].tv = ((i & 2) ? v1 : v0) + off;
			i++;
		}
		break;
	}

	case SPT_SCROLL_RIGHT:
	{
		float	off = float(fmod(a, 1.0));
		i = 0;
		pos.x = x;	pos.y = y;	pos.z = z;
		while (i < 4)
		{
			verts[i].p = pos;
			verts[i].p.x += (i & 1) ? w : 0.f;
			verts[i].p.y += (i & 2) ? h : 0.f;
			verts[i].p.w = rhw;
			verts[i].color = col;
			verts[i].tu = (i & 1) ? u1 : u0;
			verts[i].tv = (i & 2) ? v1 : v0;
			verts[i].tu += off;
			i++;
		}
		break;
	}
	}

	if (yaw != 0.f)
	{
		float	hw = w * 0.5f;
		float	ox = hw + x;
		float	hh = h * 0.5f;
		float	oy = hh + y;
		float	c = float(cos(yaw));

		for (i = 0; i < 4; i++)
		{
			verts[i].p.x -= ox;
			verts[i].p.y -= oy;
			pos = verts[i].p;
			verts[i].p.x = c * pos.x - float(sin(yaw)) * pos.y + hw + x;
			verts[i].p.y = float(sin(yaw)) * pos.x + c * pos.y + hh + y;
		}
	}

	tex->Select();
	RENDERINFO.Apply();
	tex->Activate();
	FASTVB.Create();

	SPRITEVERTEX	*out;
	if (FASTVB.Lock((void **)&out, 4) != 0xffff)
	{
		if (FRONTEND.IsRunning())
		{
			float	sx = float(PLATFORM.GetWindowWidth()) * (1.f / 640.f);
			float	sy = float(PLATFORM.GetWindowHeight()) * (1.f / 480.f);
			for (i = 0; i < 4; i++)
			{
				verts[i].p.x *= sx;
				verts[i].p.y *= sy;
			}
		}

		if (!mBatch)
		{
			memcpy(out, verts, sizeof(verts));
			FASTVB.RenderStrip();
		}
		else
		{
			// a strip's order to a quad's
			out[0] = verts[0];
			out[1] = verts[1];
			out[3] = verts[2];
			out[2] = verts[3];
		}
	}

	LT.SetModulate(0);
}

//******************************************************************************************
void	CDXSpriteRenderer::DrawColouredSprite(float x, float y, float z, CDXTexture *tex, DWORD col, float xscale, float yscale,	// FUNCTION: 0x005563d0
											  ESpriteAlignment align, ESpriteType type, float a, float yaw)
{
	DrawSprite(x, y, z, tex, align, type, a, yaw, col, xscale, yscale, 0.f, 1.f, 0.f, 1.f);
}
