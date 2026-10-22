// SPDX-License-Identifier: GPL-3.0-only
// D3DX maths for the port. Every function is checked bit for bit against retail d3dx9 under Wine
// (port/d3dx/tests: maths_cases.py, wine/harness.c, maths_driver.cpp, maths_compare.py). Retail dispatches these
// to its SSE implementations on a modern CPU (their results do not change with the x87 precision control), so this
// file is compiled with SSE arithmetic (port/d3dx/flags.txt) and each function keeps the operation order that
// reproduces retail's results.
#include <d3dx9.h>
#include <xmmintrin.h>

// Scalar SSE operations with a fixed operand order. x86 propagates the first operand's NaN, so operand order is
// observable, and the compiler may commute C operators and even scalar intrinsics.
static inline FLOAT add(FLOAT a, FLOAT b) { __asm__("addss %1, %0" : "+x"(a) : "x"(b)); return a; }
static inline FLOAT sub(FLOAT a, FLOAT b) { __asm__("subss %1, %0" : "+x"(a) : "x"(b)); return a; }
static inline FLOAT mul(FLOAT a, FLOAT b) { __asm__("mulss %1, %0" : "+x"(a) : "x"(b)); return a; }
// A load and store through the x87, as retail's copies of some elements are: a signalling NaN comes out quiet.
static inline FLOAT x87(FLOAT a) { FLOAT b; __asm__("flds %1\n\tfstps %0" : "=m"(b) : "m"(a)); return b; }

extern "C" {

// Rows 0 and 3 sum their products pairwise, rows 1 and 2 left to right, and row 3 multiplies in the other operand
// order: retail's order, fitted element by element on NaN payloads (port/d3dx/README.md).
D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2)
{
    D3DXMATRIX t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            FLOAT p[4];
            for (int k = 0; k < 4; k++)
                p[k] = i == 3 ? mul(pM2->m[k][j], pM1->m[i][k]) : mul(pM1->m[i][k], pM2->m[k][j]);
            FLOAT p0 = p[0], p1 = p[1], p2 = p[2], p3 = p[3];
            t.m[i][j] = (i == 0 || i == 3) ? add(add(p0, p1), add(p2, p3)) : add(add(add(p0, p1), p2), p3);
        }
    *pOut = t;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM)
{
    // Retail copies elements 1, 4, 8, 9, 12, 13 and 14 through the x87, which quiets a signalling NaN, and the rest
    // as integers.
    static const bool viaX87[16] = {0, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0};
    D3DXMATRIX t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t.m[i][j] = viaX87[i * 4 + j] ? x87(pM->m[j][i]) : pM->m[j][i];
    *pOut = t;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z)
{
    D3DXMatrixIdentity(pOut);
    pOut->_41 = x;
    pOut->_42 = y;
    pOut->_43 = z;
    return pOut;
}

// Cramer's rule in the evaluation order of Intel's SSE inverse (application note AP-928), which retail's SSE path
// follows; the reciprocal of the determinant is rcpss refined by one Newton-Raphson step.
D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM)
{
    const float *src = &pM->_11;
    __m128 minor0, minor1, minor2, minor3, row0, row1, row2, row3, det, tmp1;
    tmp1 = row1 = row3 = _mm_setzero_ps();
    tmp1 = _mm_loadh_pi(_mm_loadl_pi(tmp1, (const __m64 *)(src)), (const __m64 *)(src + 4));
    row1 = _mm_loadh_pi(_mm_loadl_pi(row1, (const __m64 *)(src + 8)), (const __m64 *)(src + 12));
    row0 = _mm_shuffle_ps(tmp1, row1, 0x88);
    row1 = _mm_shuffle_ps(row1, tmp1, 0xDD);
    tmp1 = _mm_loadh_pi(_mm_loadl_pi(tmp1, (const __m64 *)(src + 2)), (const __m64 *)(src + 6));
    row3 = _mm_loadh_pi(_mm_loadl_pi(row3, (const __m64 *)(src + 10)), (const __m64 *)(src + 14));
    row2 = _mm_shuffle_ps(tmp1, row3, 0x88);
    row3 = _mm_shuffle_ps(row3, tmp1, 0xDD);

    tmp1 = _mm_mul_ps(row2, row3);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    minor0 = _mm_mul_ps(row1, tmp1);
    minor1 = _mm_mul_ps(row0, tmp1);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor0 = _mm_sub_ps(_mm_mul_ps(row1, tmp1), minor0);
    minor1 = _mm_sub_ps(_mm_mul_ps(row0, tmp1), minor1);
    minor1 = _mm_shuffle_ps(minor1, minor1, 0x4E);

    tmp1 = _mm_mul_ps(row1, row2);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    minor0 = _mm_add_ps(_mm_mul_ps(row3, tmp1), minor0);
    minor3 = _mm_mul_ps(row0, tmp1);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor0 = _mm_sub_ps(minor0, _mm_mul_ps(row3, tmp1));
    minor3 = _mm_sub_ps(_mm_mul_ps(row0, tmp1), minor3);
    minor3 = _mm_shuffle_ps(minor3, minor3, 0x4E);

    tmp1 = _mm_mul_ps(_mm_shuffle_ps(row1, row1, 0x4E), row3);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    row2 = _mm_shuffle_ps(row2, row2, 0x4E);
    minor0 = _mm_add_ps(_mm_mul_ps(row2, tmp1), minor0);
    minor2 = _mm_mul_ps(row0, tmp1);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor0 = _mm_sub_ps(minor0, _mm_mul_ps(row2, tmp1));
    minor2 = _mm_sub_ps(_mm_mul_ps(row0, tmp1), minor2);
    minor2 = _mm_shuffle_ps(minor2, minor2, 0x4E);

    tmp1 = _mm_mul_ps(row0, row1);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    minor2 = _mm_add_ps(_mm_mul_ps(row3, tmp1), minor2);
    minor3 = _mm_sub_ps(_mm_mul_ps(row2, tmp1), minor3);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor2 = _mm_sub_ps(_mm_mul_ps(row3, tmp1), minor2);
    minor3 = _mm_sub_ps(minor3, _mm_mul_ps(row2, tmp1));

    tmp1 = _mm_mul_ps(row0, row3);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    minor1 = _mm_sub_ps(minor1, _mm_mul_ps(row2, tmp1));
    minor2 = _mm_add_ps(_mm_mul_ps(row1, tmp1), minor2);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor1 = _mm_add_ps(_mm_mul_ps(row2, tmp1), minor1);
    minor2 = _mm_sub_ps(minor2, _mm_mul_ps(row1, tmp1));

    tmp1 = _mm_mul_ps(row0, row2);
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0xB1);
    minor1 = _mm_add_ps(_mm_mul_ps(row3, tmp1), minor1);
    minor3 = _mm_sub_ps(minor3, _mm_mul_ps(row1, tmp1));
    tmp1 = _mm_shuffle_ps(tmp1, tmp1, 0x4E);
    minor1 = _mm_sub_ps(minor1, _mm_mul_ps(row3, tmp1));
    minor3 = _mm_add_ps(_mm_mul_ps(row1, tmp1), minor3);

    det = _mm_mul_ps(row0, minor0);
    det = _mm_add_ps(_mm_shuffle_ps(det, det, 0x4E), det);
    det = _mm_add_ss(_mm_shuffle_ps(det, det, 0xB1), det);
    FLOAT d = _mm_cvtss_f32(det);
    if (d == 0.0f || d != d) // retail's comiss/je: an unordered (NaN) determinant also fails, writing nothing
        return NULL;
    if (pDeterminant)
        *pDeterminant = d;
    tmp1 = _mm_rcp_ss(det);
    det = _mm_sub_ss(_mm_add_ss(tmp1, tmp1), _mm_mul_ss(det, _mm_mul_ss(tmp1, tmp1)));
    det = _mm_shuffle_ps(det, det, 0x00);
    float *dst = &pOut->_11;
    _mm_storel_pi((__m64 *)(dst), minor0 = _mm_mul_ps(det, minor0));
    _mm_storeh_pi((__m64 *)(dst + 2), minor0);
    _mm_storel_pi((__m64 *)(dst + 4), minor1 = _mm_mul_ps(det, minor1));
    _mm_storeh_pi((__m64 *)(dst + 6), minor1);
    _mm_storel_pi((__m64 *)(dst + 8), minor2 = _mm_mul_ps(det, minor2));
    _mm_storeh_pi((__m64 *)(dst + 10), minor2);
    _mm_storel_pi((__m64 *)(dst + 12), minor3 = _mm_mul_ps(det, minor3));
    _mm_storeh_pi((__m64 *)(dst + 14), minor3);
    return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM)
{
    D3DXVECTOR4 t;
    t.x = add(add(mul(pM->_11, pV->x), mul(pM->_21, pV->y)), add(mul(pM->_31, pV->z), pM->_41));
    t.y = add(add(mul(pM->_12, pV->x), mul(pM->_22, pV->y)), add(mul(pM->_32, pV->z), pM->_42));
    t.z = add(add(mul(pM->_13, pV->x), mul(pM->_23, pV->y)), add(mul(pM->_33, pV->z), pM->_43));
    t.w = add(add(mul(pM->_14, pV->x), mul(pM->_24, pV->y)), add(mul(pM->_34, pV->z), pM->_44));
    *pOut = t;
    return pOut;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM)
{
    D3DXVECTOR4 t;
    D3DXVec3Transform(&t, pV, pM);
    // rcpss refined by one Newton-Raphson step, as retail (port/d3dx/README.md).
    FLOAT r = _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(t.w)));
    r = sub(add(r, r), mul(mul(t.w, r), r));
    pOut->x = mul(t.x, r);
    pOut->y = mul(t.y, r);
    pOut->z = mul(t.z, r);
    return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM)
{
    D3DXVECTOR4 t;
    t.x = add(add(mul(pM->_11, pV->x), mul(pM->_21, pV->y)), add(mul(pM->_31, pV->z), mul(pM->_41, pV->w)));
    t.y = add(add(mul(pM->_12, pV->x), mul(pM->_22, pV->y)), add(mul(pM->_32, pV->z), mul(pM->_42, pV->w)));
    t.z = add(add(mul(pM->_13, pV->x), mul(pM->_23, pV->y)), add(mul(pM->_33, pV->z), mul(pM->_43, pV->w)));
    t.w = add(add(mul(pM->_14, pV->x), mul(pM->_24, pV->y)), add(mul(pM->_34, pV->z), mul(pM->_44, pV->w)));
    *pOut = t;
    return pOut;
}

}
