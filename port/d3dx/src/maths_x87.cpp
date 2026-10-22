// SPDX-License-Identifier: GPL-3.0-only
// The D3DX maths retail runs on the x87, compiled here for the x87 (the default port flags): their results follow
// the precision control, which Direct3D 9 lowers to 24 bits when the game creates its device. Checked at 24 and 53
// bits against retail d3dx9 (port/d3dx/tests).
#include <d3dx9.h>

extern "C" {

D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DVIEWPORT9 *pViewport,
                                    CONST D3DXMATRIX *pProjection, CONST D3DXMATRIX *pView, CONST D3DXMATRIX *pWorld)
{
    D3DXMATRIX m;
    D3DXMatrixMultiply(&m, pWorld, pView);
    D3DXMatrixMultiply(&m, &m, pProjection);
    D3DXVECTOR3 t;
    D3DXVec3TransformCoord(&t, pV, &m);
    if (pViewport)
    {
        t.x = pViewport->X + (1.0f + t.x) * pViewport->Width / 2.0f;
        t.y = pViewport->Y + (1.0f - t.y) * pViewport->Height / 2.0f;
        t.z = pViewport->MinZ + t.z * (pViewport->MaxZ - pViewport->MinZ);
    }
    *pOut = t;
    return pOut;
}

}
