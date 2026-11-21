// Centre ray from a validated rigid D3D view, without moving the camera.
#ifndef CE_MUSEUM_CINEMA_VIEW_H
#define CE_MUSEUM_CINEMA_VIEW_H
#include <cmath>

namespace CeMuseumCinemaView {
inline bool CentreRay(const float *view, float *eye, float *aim) {
    if (!view || !eye || !aim) return false;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(view[i])) return false;
    for (unsigned col = 0; col < 3; ++col) {
        float length = 0;
        for (unsigned row = 0; row < 3; ++row) length += view[row*4+col] * view[row*4+col];
        if (std::fabs(length - 1.0f) > 0.01f) return false;
        for (unsigned other = col + 1; other < 3; ++other) {
            float dot = 0;
            for (unsigned row = 0; row < 3; ++row) dot += view[row*4+col] * view[row*4+other];
            if (std::fabs(dot) > 0.01f) return false;
        }
    }
    if (std::fabs(view[3]) > 0.001f || std::fabs(view[7]) > 0.001f ||
        std::fabs(view[11]) > 0.001f || std::fabs(view[15] - 1.0f) > 0.001f) return false;
    float origin[3], direction[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        origin[axis] = -(view[12]*view[axis*4] + view[13]*view[axis*4+1] + view[14]*view[axis*4+2]);
        direction[axis] = view[axis*4+2];
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return false;
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        eye[axis] = origin[axis];
        aim[axis] = direction[axis];
    }
    return true;
}
} // namespace CeMuseumCinemaView
#endif
