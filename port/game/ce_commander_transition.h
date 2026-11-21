#ifndef CE_COMMANDER_TRANSITION_H
#define CE_COMMANDER_TRANSITION_H

#include <cmath>

// A rendered pose only. This helper owns no player, camera, input or world state.
struct CeCommanderPresentedPose {
    float pos[3];
    float rows[9];
    float zoom;
};

namespace ce_commander_transition_detail {
struct Quaternion { double x, y, z, w; };
inline bool Valid(const CeCommanderPresentedPose& p)
{
    if (!std::isfinite(p.zoom) || p.zoom <= 0.0f) return false;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(p.pos[i])) return false;
    for (int i = 0; i < 9; ++i) if (!std::isfinite(p.rows[i])) return false;
    for (int row = 0; row < 3; ++row) {
        double length = 0.0;
        for (int k = 0; k < 3; ++k) length += double(p.rows[row*3+k])*p.rows[row*3+k];
        if (std::fabs(length - 1.0) > 0.02) return false;
        for (int other = row + 1; other < 3; ++other) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) dot += double(p.rows[row*3+k])*p.rows[other*3+k];
            if (std::fabs(dot) > 0.02) return false;
        }
    }
    const double determinant =
        double(p.rows[0])*(double(p.rows[4])*p.rows[8]-double(p.rows[5])*p.rows[7]) -
        double(p.rows[1])*(double(p.rows[3])*p.rows[8]-double(p.rows[5])*p.rows[6]) +
        double(p.rows[2])*(double(p.rows[3])*p.rows[7]-double(p.rows[4])*p.rows[6]);
    return determinant > 0.98 && determinant < 1.02;
}
inline bool Normalize(Quaternion* q)
{
    const double length = std::sqrt(q->x*q->x+q->y*q->y+q->z*q->z+q->w*q->w);
    if (!std::isfinite(length) || length < 1e-12) return false;
    q->x /= length; q->y /= length; q->z /= length; q->w /= length;
    return true;
}
inline bool Rotation(const float* m, Quaternion* q)
{
    const double trace = double(m[0])+m[4]+m[8];
    if (trace > 0.0) {
        const double s = std::sqrt(trace+1.0)*2.0;
        q->w = s*0.25; q->x = (double(m[7])-m[5])/s;
        q->y = (double(m[2])-m[6])/s; q->z = (double(m[3])-m[1])/s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const double s = std::sqrt(1.0+double(m[0])-m[4]-m[8])*2.0;
        q->w = (double(m[7])-m[5])/s; q->x = s*0.25;
        q->y = (double(m[1])+m[3])/s; q->z = (double(m[2])+m[6])/s;
    } else if (m[4] > m[8]) {
        const double s = std::sqrt(1.0+double(m[4])-m[0]-m[8])*2.0;
        q->w = (double(m[2])-m[6])/s; q->x = (double(m[1])+m[3])/s;
        q->y = s*0.25; q->z = (double(m[5])+m[7])/s;
    } else {
        const double s = std::sqrt(1.0+double(m[8])-m[0]-m[4])*2.0;
        q->w = (double(m[3])-m[1])/s; q->x = (double(m[2])+m[6])/s;
        q->y = (double(m[5])+m[7])/s; q->z = s*0.25;
    }
    return Normalize(q);
}
inline void Matrix(const Quaternion& q, float* m)
{
    m[0] = float(1.0-2.0*(q.y*q.y+q.z*q.z));
    m[1] = float(2.0*(q.x*q.y-q.z*q.w));
    m[2] = float(2.0*(q.x*q.z+q.y*q.w));
    m[3] = float(2.0*(q.x*q.y+q.z*q.w));
    m[4] = float(1.0-2.0*(q.x*q.x+q.z*q.z));
    m[5] = float(2.0*(q.y*q.z-q.x*q.w));
    m[6] = float(2.0*(q.x*q.z-q.y*q.w));
    m[7] = float(2.0*(q.y*q.z+q.x*q.w));
    m[8] = float(1.0-2.0*(q.x*q.x+q.y*q.y));
}
} // namespace ce_commander_transition_detail

inline bool CeCommanderBlendPresentedPose(const CeCommanderPresentedPose& from,
    const CeCommanderPresentedPose& to, double weight, CeCommanderPresentedPose* output)
{
    using namespace ce_commander_transition_detail;
    if (!output || !std::isfinite(weight) || !Valid(from) || !Valid(to)) return false;
    if (weight <= 0.0) { *output = from; return true; }
    if (weight >= 1.0) { *output = to; return true; }
    Quaternion a{}, b{};
    if (!Rotation(from.rows, &a) || !Rotation(to.rows, &b)) return false;
    double dot = a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (dot < 0.0) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; dot=-dot; }
    if (dot > 1.0) dot = 1.0;
    double left = 1.0-weight, right = weight;
    if (dot < 0.9995) {
        const double angle = std::acos(dot), sine = std::sin(angle);
        left = std::sin((1.0-weight)*angle)/sine;
        right = std::sin(weight*angle)/sine;
    }
    Quaternion q{a.x*left+b.x*right, a.y*left+b.y*right,
                 a.z*left+b.z*right, a.w*left+b.w*right};
    if (!Normalize(&q)) return false;
    CeCommanderPresentedPose result{};
    for (int i = 0; i < 3; ++i)
        result.pos[i] = float(double(from.pos[i])+(double(to.pos[i])-from.pos[i])*weight);
    result.zoom = float(double(from.zoom)+(double(to.zoom)-from.zoom)*weight);
    Matrix(q, result.rows);
    if (!Valid(result)) return false;
    *output = result;
    return true;
}

inline double CeCommanderCameraEase(double elapsed, double duration)
{
    if (!std::isfinite(elapsed) || !std::isfinite(duration) || duration <= 0.0) return 1.0;
    if (elapsed <= 0.0) return 0.0;
    if (elapsed >= duration) return 1.0;
    const double t = elapsed/duration;
    return t*t*(3.0-2.0*t);
}
#endif
