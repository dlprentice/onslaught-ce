#ifndef PORT_CE_HOLD_H
#define PORT_CE_HOLD_H

// Zeroed settings describe a Reach Point. Hold areas use metres and seconds.
struct CeHoldSettings {
    int seconds = 0, radius = 0;
    bool Enabled() const { return seconds != 0; }
    bool Valid() const {
        return (seconds == 0 && radius == 0) ||
               (seconds >= 1 && seconds <= 30 && radius >= 6 && radius <= 120);
    }
    bool operator==(const CeHoldSettings &other) const {
        return seconds == other.seconds && radius == other.radius;
    }
};
#endif
