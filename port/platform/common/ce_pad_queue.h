// SPDX-License-Identifier: GPL-3.0-only
// A bounded sequence of complete button states for one CE controller. Different buttons may change together on an
// update; a second transition of the same button waits, preserving press/release order. Overflow needs a new baseline.
#ifndef ONSLAUGHT_CE_PAD_QUEUE_H
#define ONSLAUGHT_CE_PAD_QUEUE_H

#include <stdint.h>

struct CePadQueue
{
    static const unsigned Capacity = 64;
    uint32_t states[Capacity] = {};
    uint32_t observed = 0, delivered = 0;
    unsigned head = 0, count = 0;
    bool active = false;

    void Reset() { active = false; head = count = 0; }
    void Arm(uint32_t value) { head = count = 0; observed = delivered = value; active = true; }
    bool Snapshot(uint32_t next)
    {
        if (!active || next == observed) return true;
        if (count == Capacity) { Reset(); return false; }
        states[(head + count) % Capacity] = next;
        ++count;
        observed = next;
        return true;
    }
    bool Change(unsigned button, bool down)
    {
        if (!active || button >= 32) return true;
        uint32_t mask = uint32_t(1) << button;
        return Snapshot(down ? observed | mask : observed & ~mask);
    }
    uint32_t Next()
    {
        uint32_t changed = 0;
        while (count)
        {
            uint32_t next = states[head];
            uint32_t difference = delivered ^ next;
            if (changed & difference) break;
            changed |= difference;
            delivered = next;
            head = (head + 1) % Capacity;
            --count;
        }
        return delivered;
    }
};
#endif
