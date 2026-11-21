#ifndef CE_HUD_RESERVATION_H
#define CE_HUD_RESERVATION_H

#include <cstdint>

// Read-only bounds of native dialogue sprites actually submitted this frame.
// The owner token prevents a closed/reopened view from using an earlier layout.
struct CeHudReservation {
    float left, top, right, bottom;
};

bool CeHudReservationBegin(std::uint64_t owner, std::uint64_t frame,
                           int width, int height);
void CeHudReservationEnd();
void CeHudReservationReset();
bool CeHudReservationRead(std::uint64_t owner, std::uint64_t frame,
                          int width, int height, CeHudReservation *out);
void CeHudReservationQuad(float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3,
                         std::uint32_t color);

// The ordinary sprite path only reads this false-by-default flag.
extern bool gCeHudReservationCollecting;

class CeHudReservationScope {
public:
    CeHudReservationScope(std::uint64_t owner, std::uint64_t frame,
                          int width, int height)
        : active_(owner != 0 && CeHudReservationBegin(owner, frame, width, height)) {}
    ~CeHudReservationScope() { if (active_) CeHudReservationEnd(); }
    CeHudReservationScope(const CeHudReservationScope &) = delete;
    CeHudReservationScope &operator=(const CeHudReservationScope &) = delete;
private:
    bool active_;
};

#endif
