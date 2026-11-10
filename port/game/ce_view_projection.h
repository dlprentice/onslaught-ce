// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_VIEW_PROJECTION_H
#define CE_VIEW_PROJECTION_H

// Camera-independent projection for CE's interactive 3D tools. Supply the actual
// rendered world matrices, before an overlay changes them. Matrices are row-major
// D3D row-vector transforms: point * view * projection, clip depth in [0, 1].
// The caller owns camera lifetime, viewport freshness and terrain intersection.
#include <cmath>
#include <cfloat>

struct CeViewPoint { float x, y, z; };
struct CeViewViewport { float x, y, width, height, minDepth, maxDepth; };

namespace CeViewDetail {
inline bool Finite(double value) { return std::isfinite(value); }
inline bool Valid(const CeViewViewport& v) {
    return Finite(v.x) && Finite(v.y) && Finite(v.width) && Finite(v.height) &&
           Finite(v.minDepth) && Finite(v.maxDepth) && v.width > 0 && v.height > 0 &&
           v.minDepth >= 0 && v.maxDepth <= 1 && v.maxDepth > v.minDepth;
}
inline bool Combined(const float* view, const float* projection, double* result) {
    if (!view || !projection) return false;
    for (int i = 0; i < 16; ++i)
        if (!Finite(view[i]) || !Finite(projection[i])) return false;
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
        double sum = 0;
        for (int k = 0; k < 4; ++k) sum += double(view[row * 4 + k]) * projection[k * 4 + col];
        if (!Finite(sum)) return false;
        result[row * 4 + col] = sum;
    }
    return true;
}
inline void Transform(const double* matrix, const double* point, double* result) {
    for (int col = 0; col < 4; ++col) {
        result[col] = 0;
        for (int row = 0; row < 4; ++row) result[col] += point[row] * matrix[row * 4 + col];
    }
}
inline bool Inverse(const double* matrix, double* result) {
    double a[4][8];
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 8; ++col)
        a[row][col] = col < 4 ? matrix[row * 4 + col] : double(col - 4 == row);
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        if (!Finite(a[pivot][col]) || std::fabs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) for (int k = 0; k < 8; ++k) {
            const double temp = a[col][k]; a[col][k] = a[pivot][k]; a[pivot][k] = temp;
        }
        const double divisor = a[col][col];
        for (int k = 0; k < 8; ++k) a[col][k] /= divisor;
        for (int row = 0; row < 4; ++row) if (row != col) {
            const double factor = a[row][col];
            for (int k = 0; k < 8; ++k) a[row][k] -= factor * a[col][k];
        }
    }
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
        const double value = a[row][col + 4];
        if (!Finite(value)) return false;
        result[row * 4 + col] = value;
    }
    return true;
}
inline bool Divide(const double* homogeneous, CeViewPoint* result) {
    if (!result || !Finite(homogeneous[3]) || homogeneous[3] <= 1e-12) return false;
    double value[3];
    for (int i = 0; i < 3; ++i) {
        value[i] = homogeneous[i] / homogeneous[3];
        if (!Finite(value[i]) || std::fabs(value[i]) > double(FLT_MAX)) return false;
    }
    result->x = float(value[0]); result->y = float(value[1]); result->z = float(value[2]);
    return true;
}
}

// Failure leaves the caller's output unchanged. Behind-camera or clipped points
// are not usable world markers; depth is the viewport's depth, not world height.
inline bool CeViewProject(const float* view, const float* projection,
                          const CeViewViewport& viewport, const CeViewPoint& world,
                          CeViewPoint* pixel) {
    if (!pixel || !CeViewDetail::Valid(viewport) || !CeViewDetail::Finite(world.x) ||
        !CeViewDetail::Finite(world.y) || !CeViewDetail::Finite(world.z)) return false;
    double matrix[16], clip[4];
    if (!CeViewDetail::Combined(view, projection, matrix)) return false;
    const double point[4] = {world.x, world.y, world.z, 1};
    CeViewDetail::Transform(matrix, point, clip);
    CeViewPoint ndc;
    if (!CeViewDetail::Divide(clip, &ndc) || ndc.x < -1 || ndc.x > 1 ||
        ndc.y < -1 || ndc.y > 1 || ndc.z < 0 || ndc.z > 1) return false;
    CeViewPoint mapped = {viewport.x + (ndc.x + 1) * .5f * viewport.width,
                          viewport.y + (1 - ndc.y) * .5f * viewport.height,
                          viewport.minDepth + ndc.z * (viewport.maxDepth - viewport.minDepth)};
    if (!CeViewDetail::Finite(mapped.x) || !CeViewDetail::Finite(mapped.y) || !CeViewDetail::Finite(mapped.z)) return false;
    *pixel = mapped; return true;
}

// Both endpoints are on the forward ray through this pixel, at the rendered
// camera's near/far clip planes. The caller intersects that segment with its own
// terrain/geometry. A viewport or matrix from a different frame is not valid.
inline bool CeViewRay(const float* view, const float* projection,
                      const CeViewViewport& viewport, float pixelX, float pixelY,
                      CeViewPoint* nearPoint, CeViewPoint* farPoint) {
    if (!nearPoint || !farPoint || nearPoint == farPoint || !CeViewDetail::Valid(viewport) ||
        !CeViewDetail::Finite(pixelX) || !CeViewDetail::Finite(pixelY) ||
        pixelX < viewport.x || pixelY < viewport.y ||
        double(pixelX) > double(viewport.x) + viewport.width ||
        double(pixelY) > double(viewport.y) + viewport.height) return false;
    double matrix[16], inverse[16];
    if (!CeViewDetail::Combined(view, projection, matrix) || !CeViewDetail::Inverse(matrix, inverse)) return false;
    double clip[4] = {2 * (double(pixelX) - viewport.x) / viewport.width - 1,
                      1 - 2 * (double(pixelY) - viewport.y) / viewport.height, 0, 1};
    double world[4]; CeViewPoint nearValue, farValue;
    CeViewDetail::Transform(inverse, clip, world);
    if (!CeViewDetail::Divide(world, &nearValue)) return false;
    clip[2] = 1; CeViewDetail::Transform(inverse, clip, world);
    if (!CeViewDetail::Divide(world, &farValue)) return false;
    const double dx = double(farValue.x) - nearValue.x, dy = double(farValue.y) - nearValue.y,
                 dz = double(farValue.z) - nearValue.z;
    if (!CeViewDetail::Finite(dx * dx + dy * dy + dz * dz) || dx * dx + dy * dy + dz * dz <= 1e-12) return false;
    *nearPoint = nearValue; *farPoint = farValue; return true;
}
#endif
