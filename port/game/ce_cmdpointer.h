#ifndef CE_CMDPOINTER_H
#define CE_CMDPOINTER_H

// A mode boundary observes physical levels before accepting new button edges.
// Native trap order is left, middle, right. Negative samples mean unavailable.
// No platform or game object is retained here.
struct CeCmdPointerObservation {
    unsigned released;
    bool available;
};
class CeCmdPointerFence {
    unsigned blocked_ = 7;
    unsigned pressed_ = 0;
public:
    void Begin(const int held[3]) {
        Cancel();
        for (unsigned i = 0; i < 3; ++i)
            if (held[i] < 0) return;
        blocked_ = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (held[i] != 0) blocked_ |= 1u << i;
    }
    void Cancel() { blocked_ = 7; pressed_ = 0; }
    CeCmdPointerObservation Observe(const int held[3]) {
        for (unsigned i = 0; i < 3; ++i)
            if (held[i] < 0) { Cancel(); return {0, false}; }
        unsigned released = 0;
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned bit = 1u << i;
            if ((blocked_ & bit) && held[i] == 0) {
                blocked_ &= ~bit;
                released |= bit;
            }
        }
        return {released, true};
    }
    bool Press(unsigned button) {
        if (button > 2) return false;
        const unsigned bit = 1u << button;
        if ((blocked_ | pressed_) & bit) return false;
        pressed_ |= bit;
        return true;
    }
    bool Release(unsigned button) {
        if (button > 2) return false;
        const unsigned bit = 1u << button;
        const bool accepted = (pressed_ & bit) && !(blocked_ & bit);
        pressed_ &= ~bit;
        return accepted;
    }
    bool Blocked(unsigned button) const {
        return button > 2 || (blocked_ & (1u << button)) != 0;
    }
};
#endif
