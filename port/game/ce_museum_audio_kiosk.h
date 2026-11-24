// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_audio_kiosk.h: Interactive brass Audio Kiosks for each wing of the
// Grand Museum. Plays curated retail speech dispatches with smooth CeMuseumMusicDuck
// crossfade, mutual exclusion against the Screening Room cinema, pause safety, and
// wing-exit cleanup. Pure C++ state machine verified by ce_museum_audio_kiosk_tests.py.
#ifndef CE_MUSEUM_AUDIO_KIOSK_H
#define CE_MUSEUM_AUDIO_KIOSK_H

#include "ce_museum_tour.h"
#include "ce_museum_music_duck.h"
#include <cmath>

namespace CeMuseumAudioKiosk {

enum { kKioskCount = 5, kTracksPerKiosk = 3 };
constexpr float kPickRadius = 0.42f;
constexpr float kPickMaxRange = 4.8f;
constexpr unsigned kMaxDecodeBytes = 0x100000u; // 1 MiB, matches game's VOICE_BUFFER_SIZE

struct TrackSpec {
    const char *stem;   // filename stem inside data/sounds/<lang>/MessageBox/<stem>.ogg
    const char *title;  // clean factual title shown on the kiosk plaque
};

struct KioskSpec {
    CeMuseumTour::Wing wing;
    const char *wingLabel;
    float localX, localY;
    float facingX, facingY;
    TrackSpec tracks[kTracksPerKiosk];
};

static const KioskSpec kKiosks[kKioskCount] = {
    {CeMuseumTour::kWingForseti, "Forseti Field Dispatches",
     -18.5f, 34.5f, 0.0f, 1.0f, {
         {"110_enemy_engaged", "First Contact - Tank Engagement"},
         {"200_defend_base",   "Forseti Base Defence Call"},
         {"221_convoy_safe",   "Supply Convoy Escort Clear"},
     }},
    {CeMuseumTour::kWingMuspell, "Frontline Combat Dispatches",
     18.5f, 34.5f, 0.0f, 1.0f, {
         {"211_fighter_attack", "Muspell Air Raid Alert"},
         {"300_briefing",       "Command Sector Briefing"},
         {"431_attack_warspite","Assault on Warspite"},
     }},
    {CeMuseumTour::kWingHangar, "Aviation and Naval Operations",
     -7.5f, 55.5f, 0.0f, 1.0f, {
         {"221_air_support_arrives",  "Air Support Inbound"},
         {"221_battleship_destroyed", "Battleship Neutralised"},
         {"422_briefing",             "Fleet Escort Briefing"},
     }},
    {CeMuseumTour::kWingConceptArt, "Tactical Radio Archive",
     19.0f, 15.0f, 0.0f, 1.0f, {
         {"200_all_safe",    "Sector Secured"},
         {"331_shoot_nexus", "Target Nexus Objective"},
         {"431_final_push",  "Final Advance Order"},
     }},
    {CeMuseumTour::kWingEarlyDesigns, "Archive Command Briefings",
     -19.0f, 15.0f, 0.0f, 1.0f, {
         {"003_east_sphere_a", "Training Range Calibration"},
         {"312_briefing",      "Field Operations Briefing"},
         {"411_beach_held",    "Beachhead Secured"},
     }},
};

enum Phase { kPhaseIdle = 0, kPhasePlaying, kPhaseUnavailable };

struct State {
    int activeKiosk;       // 0..kKioskCount-1, or -1
    int trackIndex[kKioskCount];
    Phase phase;
    bool fireHeld;
    bool fireCaptured;
    int hoveredKiosk;
    CeMuseumMusicDuck::State duck;
};

inline void Reset(State *s) {
    if (!s) return;
    s->activeKiosk = -1;
    for (unsigned i = 0; i < kKioskCount; ++i) s->trackIndex[i] = 0;
    s->phase = kPhaseIdle;
    s->fireHeld = false;
    s->fireCaptured = false;
    s->hoveredKiosk = -1;
    s->duck = CeMuseumMusicDuck::Off();
}

// Validate a safe audio stem (alphanumeric, underscore, hyphen only — never path traversal).
inline bool SafeStem(const char *stem) {
    if (!stem || !stem[0]) return false;
    unsigned n = 0;
    for (; stem[n]; ++n) {
        const unsigned char c = static_cast<unsigned char>(stem[n]);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok || n >= 63u) return false;
    }
    return n > 0u;
}

inline int PickKiosk(float eyeX, float eyeY, float eyeZ,
                     float aimX, float aimY, float aimZ,
                     CeMuseumTour::Wing currentWing) {
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    int best = -1;
    float bestT = 1e9f;
    for (unsigned i = 0; i < kKioskCount; ++i) {
        if (kKiosks[i].wing != currentWing) continue;
        const float px = kKiosks[i].localX, py = kKiosks[i].localY, pz = 1.25f;
        const float vx = px - eyeX, vy = py - eyeY, vz = pz - eyeZ;
        const float t = vx * ux + vy * uy + vz * uz;
        if (t <= 0.15f || t > kPickMaxRange) continue;
        const float cx = eyeX + t * ux - px;
        const float cy = eyeY + t * uy - py;
        const float cz = eyeZ + t * uz - pz;
        if (cx * cx + cy * cy + cz * cz <= kPickRadius * kPickRadius && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

struct Backend {
    void *user;
    bool (*startTrack)(void *user, const char *stem);
    void (*stopTrack)(void *user);
    bool (*isPlaying)(void *user);
};

inline void Stop(State *s, const Backend &be, double now) {
    if (!s) return;
    if (s->phase == kPhasePlaying || s->activeKiosk >= 0) {
        if (be.stopTrack) be.stopTrack(be.user);
        CeMuseumMusicDuck::To(&s->duck, 1.0f, CeMuseumMusicDuck::kFadeIn, now);
    }
    s->activeKiosk = -1;
    s->phase = kPhaseIdle;
}

// Activate a hovered kiosk: starts current track (or stops & advances to next on repeat press).
inline bool Operate(State *s, int kiosk, const Backend &be, double now, bool cinemaPlaying) {
    if (!s || kiosk < 0 || kiosk >= kKioskCount) return false;
    if (cinemaPlaying) return false; // mutually exclusive with Screening Room cinema
    if (s->phase == kPhasePlaying && s->activeKiosk == kiosk) {
        // Stop current clip and advance pointer so next press plays the next dispatch.
        Stop(s, be, now);
        s->trackIndex[kiosk] = (s->trackIndex[kiosk] + 1) % kTracksPerKiosk;
        return true;
    }
    Stop(s, be, now);
    const int idx = s->trackIndex[kiosk] % kTracksPerKiosk;
    const char *stem = kKiosks[kiosk].tracks[idx].stem;
    if (!SafeStem(stem) || !be.startTrack || !be.startTrack(be.user, stem)) {
        s->activeKiosk = kiosk;
        s->phase = kPhaseUnavailable;
        return false;
    }
    s->activeKiosk = kiosk;
    s->phase = kPhasePlaying;
    // Duck music cleanly under speech dispatch (soft -18 dB bed).
    CeMuseumMusicDuck::To(&s->duck, CeMuseumMusicDuck::kBed, CeMuseumMusicDuck::kFadeOut, now);
    return true;
}

inline void Step(State *s, CeMuseumTour::Wing currentWing, bool paused, bool cinemaPlaying,
                 const Backend &be, double now) {
    if (!s) return;
    if (s->activeKiosk >= 0 && (kKiosks[s->activeKiosk].wing != currentWing || cinemaPlaying)) {
        Stop(s, be, now);
        return;
    }
    if (s->phase == kPhasePlaying && !paused) {
        if (be.isPlaying && !be.isPlaying(be.user)) {
            const int k = s->activeKiosk;
            Stop(s, be, now);
            if (k >= 0 && k < kKioskCount)
                s->trackIndex[k] = (s->trackIndex[k] + 1) % kTracksPerKiosk;
        }
    }
    CeMuseumMusicDuck::Settle(&s->duck, now);
}

inline long DuckHundredths(const State &s, double now) {
    return CeMuseumMusicDuck::Hundredths(CeMuseumMusicDuck::Gain(s.duck, now));
}

// Runtime adapter functions implemented in ce_museum_audio_kiosk.cpp.
void RuntimeInit();
void RuntimeShutdown();
void RuntimeUpdate(CeMuseumTour::Wing wing, bool paused, bool cinemaPlaying,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ);
bool RuntimeHandleFire(bool release, float value, bool cinemaPlaying);
bool RuntimeIsAimingControl();
long RuntimeMusicDuckHundredths();
bool RuntimeCue(char *title, unsigned titleCap, char *line, unsigned lineCap,
                char *note, unsigned noteCap);

} // namespace CeMuseumAudioKiosk

#endif
