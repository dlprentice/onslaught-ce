#include "ce_hud_reservation.h"

#include <cstdio>
#include <limits>

namespace {
int checks = 0, failures = 0;
void Check(bool ok) { ++checks; if (!ok) ++failures; }
void Quad(float x, float y, float w, float h, unsigned color = 0xffffffffu) {
    CeHudReservationQuad(x, y, x + w, y, x, y + h, x + w, y + h, color);
}
bool Read(CeHudReservation &r, unsigned owner = 3, unsigned frame = 10,
          int width = 640, int height = 480) {
    return CeHudReservationRead(owner, frame, width, height, &r);
}
}

int main() {
    CeHudReservation r{};
    CeHudReservationReset();
    Quad(1, 2, 3, 4);
    Check(!gCeHudReservationCollecting && !Read(r));
    { CeHudReservationScope inactive(0, 10, 640, 480); Quad(1, 2, 3, 4); }
    Check(!gCeHudReservationCollecting && !Read(r));
    {
        CeHudReservationScope caption(3, 10, 640, 480);
        Check(gCeHudReservationCollecting);
        Quad(60, 340, 500, 100);
        Check(!Read(r)); // An unfinished scope is never a complete layout.
        {
            CeHudReservationScope portrait(3, 10, 640, 480);
            Quad(5, 320, 70, 120);
            Check(gCeHudReservationCollecting);
        }
        Check(gCeHudReservationCollecting);
    }
    Check(!gCeHudReservationCollecting && Read(r));
    Check(r.left == 5 && r.top == 320 && r.right == 560 && r.bottom == 440);
    Check(!Read(r, 4) && !Read(r, 3, 11) && !Read(r, 3, 10, 800, 600));
    Check(!CeHudReservationRead(3, 10, 640, 480, nullptr));
    { CeHudReservationScope empty(3, 11, 640, 480); }
    Check(!Read(r, 3, 11)); // Closing dialogue does not retain last frame's bounds.
    {
        CeHudReservationScope clipping(3, 12, 640, 480);
        Quad(-10, 450, 700, 60);
        Quad(1, 1, 500, 400, 0x00ffffffu);
        Quad(std::numeric_limits<float>::quiet_NaN(), 1, 2, 3);
        Quad(800, 1, 2, 3);
        Quad(1, 1, 0, 0);
    }
    Check(Read(r, 3, 12));
    Check(r.left == 0 && r.top == 450 && r.right == 640 && r.bottom == 480);
    {
        CeHudReservationScope active(3, 13, 640, 480);
        Check(!CeHudReservationBegin(9, 14, 800, 600));
        Quad(20, 30, 40, 50);
    }
    Check(Read(r, 3, 13) && r.left == 20 && r.top == 30);
    Check(!CeHudReservationBegin(3, 13, 0, 480));
    Check(Read(r, 3, 13));
    { CeHudReservationScope new_owner(4, 13, 640, 480); }
    Check(!Read(r, 3, 13) && !Read(r, 4, 13));
    CeHudReservationReset(); CeHudReservationEnd();
    Check(!gCeHudReservationCollecting && !Read(r, 4, 13));
    std::printf("{\"checks\":%d,\"failures\":%d,\"skipped\":0}\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
