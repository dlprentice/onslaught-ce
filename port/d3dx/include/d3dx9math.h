// SPDX-License-Identifier: GPL-3.0-only
// D3DX vector and matrix types and the maths functions Battle Engine Aquila calls.
// Layouts are the SDK's (game code reads .x/.m/_11 and casts to FLOAT*). The inline operators keep the SDK's
// arithmetic, including its scalar division as one reciprocal followed by multiplies, because game code compiled
// them inline and the results must agree to the bit.
// The umbrella header first, outside the guard, so any entry order defines everything once.
#include "d3dx9.h"

#ifndef PORT_D3DX9MATH_H
#define PORT_D3DX9MATH_H
#include <string.h>

#define D3DX_PI ((FLOAT)3.141592654f)
#define D3DX_1BYPI ((FLOAT)0.318309886f)
#define D3DXToRadian(degree) ((degree) * (D3DX_PI / 180.0f))
#define D3DXToDegree(radian) ((radian) * (180.0f / D3DX_PI))

#ifdef __cplusplus

struct D3DXVECTOR2
{
    FLOAT x, y;

    D3DXVECTOR2() {}
    D3DXVECTOR2(CONST FLOAT *pf) { x = pf[0]; y = pf[1]; }
    D3DXVECTOR2(FLOAT fx, FLOAT fy) { x = fx; y = fy; }

    operator FLOAT *() { return &x; }
    operator CONST FLOAT *() const { return &x; }

    D3DXVECTOR2 &operator+=(CONST D3DXVECTOR2 &v) { x += v.x; y += v.y; return *this; }
    D3DXVECTOR2 &operator-=(CONST D3DXVECTOR2 &v) { x -= v.x; y -= v.y; return *this; }
    D3DXVECTOR2 &operator*=(FLOAT f) { x *= f; y *= f; return *this; }
    D3DXVECTOR2 &operator/=(FLOAT f) { FLOAT r = 1.0f / f; x *= r; y *= r; return *this; }

    D3DXVECTOR2 operator+() const { return *this; }
    D3DXVECTOR2 operator-() const { return D3DXVECTOR2(-x, -y); }

    D3DXVECTOR2 operator+(CONST D3DXVECTOR2 &v) const { return D3DXVECTOR2(x + v.x, y + v.y); }
    D3DXVECTOR2 operator-(CONST D3DXVECTOR2 &v) const { return D3DXVECTOR2(x - v.x, y - v.y); }
    D3DXVECTOR2 operator*(FLOAT f) const { return D3DXVECTOR2(x * f, y * f); }
    D3DXVECTOR2 operator/(FLOAT f) const { FLOAT r = 1.0f / f; return D3DXVECTOR2(x * r, y * r); }

    friend D3DXVECTOR2 operator*(FLOAT f, CONST D3DXVECTOR2 &v) { return D3DXVECTOR2(f * v.x, f * v.y); }

    BOOL operator==(CONST D3DXVECTOR2 &v) const { return x == v.x && y == v.y; }
    BOOL operator!=(CONST D3DXVECTOR2 &v) const { return x != v.x || y != v.y; }
};

struct D3DXVECTOR3 : public D3DVECTOR
{
    D3DXVECTOR3() {}
    D3DXVECTOR3(CONST FLOAT *pf) { x = pf[0]; y = pf[1]; z = pf[2]; }
    D3DXVECTOR3(CONST D3DVECTOR &v) { x = v.x; y = v.y; z = v.z; }
    D3DXVECTOR3(FLOAT fx, FLOAT fy, FLOAT fz) { x = fx; y = fy; z = fz; }

    operator FLOAT *() { return &x; }
    operator CONST FLOAT *() const { return &x; }

    D3DXVECTOR3 &operator+=(CONST D3DXVECTOR3 &v) { x += v.x; y += v.y; z += v.z; return *this; }
    D3DXVECTOR3 &operator-=(CONST D3DXVECTOR3 &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    D3DXVECTOR3 &operator*=(FLOAT f) { x *= f; y *= f; z *= f; return *this; }
    D3DXVECTOR3 &operator/=(FLOAT f) { FLOAT r = 1.0f / f; x *= r; y *= r; z *= r; return *this; }

    D3DXVECTOR3 operator+() const { return *this; }
    D3DXVECTOR3 operator-() const { return D3DXVECTOR3(-x, -y, -z); }

    D3DXVECTOR3 operator+(CONST D3DXVECTOR3 &v) const { return D3DXVECTOR3(x + v.x, y + v.y, z + v.z); }
    D3DXVECTOR3 operator-(CONST D3DXVECTOR3 &v) const { return D3DXVECTOR3(x - v.x, y - v.y, z - v.z); }
    D3DXVECTOR3 operator*(FLOAT f) const { return D3DXVECTOR3(x * f, y * f, z * f); }
    D3DXVECTOR3 operator/(FLOAT f) const { FLOAT r = 1.0f / f; return D3DXVECTOR3(x * r, y * r, z * r); }

    friend D3DXVECTOR3 operator*(FLOAT f, CONST D3DXVECTOR3 &v) { return D3DXVECTOR3(f * v.x, f * v.y, f * v.z); }

    BOOL operator==(CONST D3DXVECTOR3 &v) const { return x == v.x && y == v.y && z == v.z; }
    BOOL operator!=(CONST D3DXVECTOR3 &v) const { return x != v.x || y != v.y || z != v.z; }
};

struct D3DXVECTOR4
{
    FLOAT x, y, z, w;

    D3DXVECTOR4() {}
    D3DXVECTOR4(CONST FLOAT *pf) { x = pf[0]; y = pf[1]; z = pf[2]; w = pf[3]; }
    D3DXVECTOR4(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) { x = fx; y = fy; z = fz; w = fw; }

    operator FLOAT *() { return &x; }
    operator CONST FLOAT *() const { return &x; }

    D3DXVECTOR4 &operator+=(CONST D3DXVECTOR4 &v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    D3DXVECTOR4 &operator-=(CONST D3DXVECTOR4 &v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    D3DXVECTOR4 &operator*=(FLOAT f) { x *= f; y *= f; z *= f; w *= f; return *this; }
    D3DXVECTOR4 &operator/=(FLOAT f) { FLOAT r = 1.0f / f; x *= r; y *= r; z *= r; w *= r; return *this; }

    D3DXVECTOR4 operator+() const { return *this; }
    D3DXVECTOR4 operator-() const { return D3DXVECTOR4(-x, -y, -z, -w); }

    D3DXVECTOR4 operator+(CONST D3DXVECTOR4 &v) const { return D3DXVECTOR4(x + v.x, y + v.y, z + v.z, w + v.w); }
    D3DXVECTOR4 operator-(CONST D3DXVECTOR4 &v) const { return D3DXVECTOR4(x - v.x, y - v.y, z - v.z, w - v.w); }
    D3DXVECTOR4 operator*(FLOAT f) const { return D3DXVECTOR4(x * f, y * f, z * f, w * f); }
    D3DXVECTOR4 operator/(FLOAT f) const
    {
        FLOAT r = 1.0f / f;
        return D3DXVECTOR4(x * r, y * r, z * r, w * r);
    }

    friend D3DXVECTOR4 operator*(FLOAT f, CONST D3DXVECTOR4 &v)
    {
        return D3DXVECTOR4(f * v.x, f * v.y, f * v.z, f * v.w);
    }

    BOOL operator==(CONST D3DXVECTOR4 &v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    BOOL operator!=(CONST D3DXVECTOR4 &v) const { return x != v.x || y != v.y || z != v.z || w != v.w; }
};

struct D3DXMATRIX;

extern "C" D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2);

struct D3DXMATRIX : public D3DMATRIX
{
    D3DXMATRIX() {}
    D3DXMATRIX(CONST FLOAT *pf) { memcpy(&_11, pf, sizeof(D3DMATRIX)); }
    D3DXMATRIX(CONST D3DMATRIX &mat) { memcpy(&_11, &mat, sizeof(D3DMATRIX)); }
    D3DXMATRIX(FLOAT f11, FLOAT f12, FLOAT f13, FLOAT f14,
               FLOAT f21, FLOAT f22, FLOAT f23, FLOAT f24,
               FLOAT f31, FLOAT f32, FLOAT f33, FLOAT f34,
               FLOAT f41, FLOAT f42, FLOAT f43, FLOAT f44)
    {
        _11 = f11; _12 = f12; _13 = f13; _14 = f14;
        _21 = f21; _22 = f22; _23 = f23; _24 = f24;
        _31 = f31; _32 = f32; _33 = f33; _34 = f34;
        _41 = f41; _42 = f42; _43 = f43; _44 = f44;
    }

    FLOAT &operator()(UINT row, UINT col) { return m[row][col]; }
    FLOAT operator()(UINT row, UINT col) const { return m[row][col]; }

    operator FLOAT *() { return &_11; }
    operator CONST FLOAT *() const { return &_11; }

    D3DXMATRIX &operator*=(CONST D3DXMATRIX &mat) { D3DXMatrixMultiply(this, this, &mat); return *this; }
    D3DXMATRIX &operator+=(CONST D3DXMATRIX &mat)
    {
        for (int i = 0; i < 16; i++)
            (&_11)[i] += (&mat._11)[i];
        return *this;
    }
    D3DXMATRIX &operator-=(CONST D3DXMATRIX &mat)
    {
        for (int i = 0; i < 16; i++)
            (&_11)[i] -= (&mat._11)[i];
        return *this;
    }
    D3DXMATRIX &operator*=(FLOAT f)
    {
        for (int i = 0; i < 16; i++)
            (&_11)[i] *= f;
        return *this;
    }
    D3DXMATRIX &operator/=(FLOAT f)
    {
        FLOAT r = 1.0f / f;
        for (int i = 0; i < 16; i++)
            (&_11)[i] *= r;
        return *this;
    }

    D3DXMATRIX operator+() const { return *this; }
    D3DXMATRIX operator-() const
    {
        D3DXMATRIX t;
        for (int i = 0; i < 16; i++)
            (&t._11)[i] = -(&_11)[i];
        return t;
    }

    D3DXMATRIX operator*(CONST D3DXMATRIX &mat) const { D3DXMATRIX t; D3DXMatrixMultiply(&t, this, &mat); return t; }
    D3DXMATRIX operator+(CONST D3DXMATRIX &mat) const { D3DXMATRIX t(*this); t += mat; return t; }
    D3DXMATRIX operator-(CONST D3DXMATRIX &mat) const { D3DXMATRIX t(*this); t -= mat; return t; }
    D3DXMATRIX operator*(FLOAT f) const { D3DXMATRIX t(*this); t *= f; return t; }
    D3DXMATRIX operator/(FLOAT f) const { D3DXMATRIX t(*this); t /= f; return t; }

    friend D3DXMATRIX operator*(FLOAT f, CONST D3DXMATRIX &mat) { D3DXMATRIX t(mat); t *= f; return t; }

    // The SDK compares matrices bytewise (so -0.0f differs from 0.0f and NaN equals itself).
    BOOL operator==(CONST D3DXMATRIX &mat) const { return 0 == memcmp(this, &mat, sizeof(D3DXMATRIX)); }
    BOOL operator!=(CONST D3DXMATRIX &mat) const { return 0 != memcmp(this, &mat, sizeof(D3DXMATRIX)); }
};

#else

typedef struct D3DXVECTOR2 { FLOAT x, y; } D3DXVECTOR2;
typedef struct _D3DVECTOR D3DXVECTOR3;
typedef struct D3DXVECTOR4 { FLOAT x, y, z, w; } D3DXVECTOR4;
typedef struct _D3DMATRIX D3DXMATRIX;

#endif

typedef D3DXVECTOR2 *LPD3DXVECTOR2;
typedef D3DXVECTOR3 *LPD3DXVECTOR3;
typedef D3DXVECTOR4 *LPD3DXVECTOR4;
typedef D3DXMATRIX *LPD3DXMATRIX;

#ifdef __cplusplus
extern "C" {
#endif

D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2);
D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM);
D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM);
D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z);

D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM);
D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM);
D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM);
D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DVIEWPORT9 *pViewport,
                                    CONST D3DXMATRIX *pProjection, CONST D3DXMATRIX *pView, CONST D3DXMATRIX *pWorld);

#ifdef __cplusplus
}
#endif

// The SDK defines Identity inline in its header, so game code compiled it inline.
D3DXINLINE D3DXMATRIX *D3DXMatrixIdentity(D3DXMATRIX *pOut)
{
    pOut->m[0][1] = pOut->m[0][2] = pOut->m[0][3] =
    pOut->m[1][0] = pOut->m[1][2] = pOut->m[1][3] =
    pOut->m[2][0] = pOut->m[2][1] = pOut->m[2][3] =
    pOut->m[3][0] = pOut->m[3][1] = pOut->m[3][2] = 0.0f;
    pOut->m[0][0] = pOut->m[1][1] = pOut->m[2][2] = pOut->m[3][3] = 1.0f;
    return pOut;
}

D3DXINLINE BOOL D3DXMatrixIsIdentity(CONST D3DXMATRIX *pM)
{
    return pM->m[0][0] == 1.0f && pM->m[0][1] == 0.0f && pM->m[0][2] == 0.0f && pM->m[0][3] == 0.0f &&
           pM->m[1][0] == 0.0f && pM->m[1][1] == 1.0f && pM->m[1][2] == 0.0f && pM->m[1][3] == 0.0f &&
           pM->m[2][0] == 0.0f && pM->m[2][1] == 0.0f && pM->m[2][2] == 1.0f && pM->m[2][3] == 0.0f &&
           pM->m[3][0] == 0.0f && pM->m[3][1] == 0.0f && pM->m[3][2] == 0.0f && pM->m[3][3] == 1.0f;
}

#endif
