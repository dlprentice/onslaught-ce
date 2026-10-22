// D3DUtils.cpp: Direct3D helpers (not in Stuart's release; rebuilt from the retail object 00440ac0..00440b67). The
// file name is a reconstruction: Stuart's DXEngine.cpp includes D3DUtils.h, and the object links between
// Cylinder.obj and damage.obj. Its only contents are the shared builder of index lists for grids of quads
// (the water, DXWater.cpp) and the global that holds it.

#include "common.h"
#include <d3d9types.h>

// The builder of index lists for a grid of quads (the names are ours).
class CGridIndices
{
public:
	CGridIndices() {}
	void	Build(UWORD *out, SINT w, SINT h, SINT flip);
};

// FUNCTION: 0x00440ac0 initorder:1
CGridIndices	GRID_INDICES;					// 0066ea68

// The texture transform the landscape, water and shadow passes share (00628258), identity at first.
D3DMATRIX	gTextureMatrix = { 1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f };

//******************************************************************************************
// Two triangles per quad of a w x h grid of (w + 1) x (h + 1) vertices, row by row; flip reverses every
// triangle's winding by swapping its first two indices.
// Each triangle's stores run first, third, second: of the 720 orders of the six stores only this one (and p[0], p[2],
// p[3], p[1], p[5], p[4]) schedules the quad's i + row + 1 before the p[1] store and i + 1 after it, as retail does
// (00440af8..00440b03); the other orders and the expression forms tried place the three LEAs differently.
void	CGridIndices::Build(UWORD *out, SINT w, SINT h, SINT flip)	// FUNCTION: 0x00440ad0
{
	SINT	row = w + 1;
	UWORD	*p = out;

	for (SINT y = 0; y < h; y++)
	{
		SINT	i = y * row;
		for (SINT x = w; x > 0; x--)
		{
			p[0] = i;
			p[2] = i + row + 1;
			p[1] = i + row;
			p[3] = i;
			p[5] = i + 1;
			p[4] = i + row + 1;
			p += 6;
			i++;
		}
	}

	if (flip)
	{
		p = out;
		for (SINT n = w * h * 2; n; n--)
		{
			UWORD	t = p[0];
			p[0] = p[1];
			p[1] = t;
			p += 3;
		}
	}
}
