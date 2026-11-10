// SPDX-License-Identifier: GPL-3.0-only
// The Museum preview's visitor controller (port/game/ce_museum.cpp) on its own, against authored page and state labels.
#include "ce_museum.h"
#include <cstdio>
#include <cstring>

namespace {
int checks = 0;
int failures = 0;
void check(bool ok) {
    ++checks;
    if (!ok) ++failures;
}
struct CareerWitness {
    int progress;
    int unlocks[5];
    unsigned char unknown[19];
};
void witness_unchanged(const CareerWitness& before, const CareerWitness& after) {
    check(std::memcmp(&before, &after, sizeof(before)) == 0);
}
}

int main() {
    // These are authored page/state labels, not game or save-format values.
    const int ce = 1101, gallery = 1102, back = 1103, other = 1104;
    const int viewed = 2101, unavailable = 2102, ordinary = 2103;
    CareerWitness career;
    std::memset(&career, 0x5a, sizeof(career));
    CareerWitness before;
    std::memcpy(&before, &career, sizeof(career));
    PortCeMuseumEnd();
    check(!PortCeMuseumActive());
    check(!PortCeMuseumBegin(false));
    check(!PortCeMuseumActive());
    check(PortCeMuseumCanChangeUnlocks());
    check(PortCeMuseumCanLoad(false));
    check(PortCeMuseumReadUnlock(ordinary, true, viewed, unavailable) == ordinary);
    check(PortCeMuseumPage(gallery, back, ce, gallery, back) == back);
    witness_unchanged(before, career);

    check(PortCeMuseumBegin(true));
    check(!PortCeMuseumBegin(true));
    check(PortCeMuseumActive());
    check(PortCeMuseumPage(ce, gallery, ce, gallery, back) == gallery);
    check(PortCeMuseumActive());
    check(PortCeMuseumCanLoad(true));
    check(!PortCeMuseumCanLoad(false));
    check(!PortCeMuseumCanChangeUnlocks());
    check(PortCeMuseumReadUnlock(ordinary, true, viewed, unavailable) == viewed);
    check(PortCeMuseumReadUnlock(ordinary, false, viewed, unavailable) == unavailable);
    check(PortCeMuseumPage(gallery, gallery, ce, gallery, back) == gallery);
    check(PortCeMuseumActive());
    witness_unchanged(before, career);
    check(PortCeMuseumPage(gallery, back, ce, gallery, back) == ce);
    check(!PortCeMuseumActive());
    check(PortCeMuseumCanChangeUnlocks());
    check(PortCeMuseumReadUnlock(ordinary, true, viewed, unavailable) == ordinary);
    witness_unchanged(before, career);

    check(PortCeMuseumBegin(true));
    check(PortCeMuseumPage(ce, gallery, ce, gallery, back) == gallery);
    check(PortCeMuseumPage(gallery, other, ce, gallery, back) == other);
    check(!PortCeMuseumActive());
    witness_unchanged(before, career);

    check(PortCeMuseumBegin(true));
    check(PortCeMuseumPage(other, gallery, ce, gallery, back) == gallery);
    check(!PortCeMuseumActive());
    check(PortCeMuseumPage(gallery, back, ce, gallery, back) == back);
    witness_unchanged(before, career);

    check(PortCeMuseumBegin(true));
    check(PortCeMuseumPage(ce, gallery, ce, gallery, back) == gallery);
    check(PortCeMuseumPage(other, back, ce, gallery, back) == back);
    check(!PortCeMuseumActive());
    witness_unchanged(before, career);

    for (int n = 0; n < 20; ++n) {
        check(PortCeMuseumBegin(true));
        check(PortCeMuseumPage(ce, gallery, ce, gallery, back) == gallery);
        PortCeMuseumEnd();
        check(!PortCeMuseumActive());
        check(PortCeMuseumPage(gallery, back, ce, gallery, back) == back);
        witness_unchanged(before, career);
    }
    std::printf("museum_controls=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
