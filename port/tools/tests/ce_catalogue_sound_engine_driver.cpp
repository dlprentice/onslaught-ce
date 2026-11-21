// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <vector>
#include "../../game/ce_catalogue_sounds.h"
#define stricmp strcasecmp
struct FVector { FVector(float, float, float) {} };
struct FMatrix { FMatrix(FVector, FVector, FVector) {} };
struct CSoundEvent {};
struct IAudibleThing {
    virtual FVector GetSoundPos() = 0;
    virtual FMatrix GetSoundOrientation() = 0;
    virtual FVector GetSoundVelocity() = 0;
    virtual void SampleFinishedPlaying(CSoundEvent*) = 0;
};
struct CSample { char mName[64]; CSample* mNextSample; };
struct CPCSample : public CSample { void* mDSBuffer; };
enum { ST_FOLLOWDONTDIE = 0 };
enum { ST_GAME_SOUND = 1 };
struct Event { IAudibleThing* owner; CSample* sample; };
struct FakeSound {
    CSample* first;
    bool accept;
    unsigned starts, kills;
    bool nonpositional, repeats, callback;
    std::vector<Event> events;
    FakeSound() : first(0), accept(true), starts(0), kills(0), nonpositional(false), repeats(true), callback(true) {}
    CSample* GetFirstSample() { return first; }
    void PlaySample(CSample* sample, IAudibleThing* owner, float, int, bool,
                    float, float, float, bool repeat, float, bool notify,
                    bool ignore_position, int) {
        ++starts; nonpositional = ignore_position; repeats = repeat; callback = notify;
        if (accept) events.push_back(Event{owner, sample});
    }
    void KillSamplesForThing(IAudibleThing* owner) {
        ++kills;
        events.erase(std::remove_if(events.begin(), events.end(), [owner](const Event& e) { return e.owner == owner; }), events.end());
    }
    bool IsSamplePlaying(char* name, IAudibleThing* owner) {
        for (const Event& e : events) if (e.owner == owner && !strcasecmp(e.sample->mName, name)) return true;
        return false;
    }
} sound;
#define SOUND sound
#include "../../game/ce_catalogue_sound_engine.inc"

static unsigned checks = 0;
static void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "native sound adapter check %u failed\n", checks); std::exit(1); } }
static CPCSample Sample(const char* name, void* data = reinterpret_cast<void*>(1)) {
    CPCSample out = {}; std::snprintf(out.mName, sizeof(out.mName), "%s", name); out.mDSBuffer = data; return out;
}
static void Visit(const char*, void* context) { ++*static_cast<unsigned*>(context); }

int main() {
    CPCSample first = Sample("Authored_Chime"), second = Sample("Authored_Drum");
    CPCSample unloaded = Sample("Unavailable", 0), malformed = {};
    std::memset(malformed.mName, 'x', sizeof(malformed.mName)); malformed.mDSBuffer = reinterpret_cast<void*>(1);
    first.mNextSample = &second; second.mNextSample = &unloaded; unloaded.mNextSample = &malformed; sound.first = &first;
    unsigned visited = 0; PortCeCatalogueSoundVisit(Visit, &visited); Check(visited == 2);
    PortCeCatalogueSoundVisit(0, 0); Check(sound.starts == 0);
    Check(!PortCeCatalogueSoundActive()); PortCeCatalogueSoundStop(); Check(sound.kills == 0);
    sound.events.push_back(Event{0, &first});
    Check(PortCeCatalogueSoundStart("authored_chime")); Check(sound.events.size() == 2);
    Check(sound.events.back().owner != 0); Check(sound.events.back().sample == &first);
    Check(sound.nonpositional && !sound.repeats && !sound.callback);
    Check(PortCeCatalogueSoundActive());
    PortCeCatalogueSoundStop(); Check(sound.events.size() == 1 && sound.events.front().owner == 0);
    Check(!PortCeCatalogueSoundActive());
    unsigned killed = sound.kills; PortCeCatalogueSoundStop(); Check(sound.kills == killed);
    Check(PortCeCatalogueSoundStart("Authored_Chime"));
    Check(PortCeCatalogueSoundStart("Authored_Drum"));
    Check(sound.events.size() == 2 && sound.events.back().sample == &second);
    Check(sound.events.front().owner == 0 && sound.events.front().sample == &first);
    sound.events.pop_back(); Check(!PortCeCatalogueSoundActive());
    killed = sound.kills; PortCeCatalogueSoundStop(); Check(sound.kills == killed);
    sound.accept = false; Check(!PortCeCatalogueSoundStart("Authored_Drum")); Check(sound.events.size() == 1);
    sound.accept = true; Check(!PortCeCatalogueSoundStart("Unavailable")); Check(sound.events.size() == 1);
    Check(!PortCeCatalogueSoundStart("Missing")); Check(!PortCeCatalogueSoundStart(0)); Check(!PortCeCatalogueSoundStart(""));
    char long_name[257]; std::memset(long_name, 'q', sizeof(long_name)); long_name[256] = 0;
    Check(!PortCeCatalogueSoundStart(long_name));
    Check(PortCeCatalogueSoundStart("Authored_Drum"));
    second.mDSBuffer = 0; Check(!PortCeCatalogueSoundStart("Authored_Drum")); Check(sound.events.size() == 1);
    second.mDSBuffer = reinterpret_cast<void*>(1); Check(PortCeCatalogueSoundStart("Authored_Drum"));
    first.mNextSample = 0; Check(!PortCeCatalogueSoundStart("Authored_Drum")); Check(sound.events.size() == 1);
    first.mNextSample = &first; visited = 0; PortCeCatalogueSoundVisit(Visit, &visited); Check(visited == kCatalogueSoundWalkLimit);
    Check(!PortCeCatalogueSoundStart("Missing"));
    first.mNextSample = 0; sound.first = 0; visited = 0; PortCeCatalogueSoundVisit(Visit, &visited); Check(!visited);
    PortCeCatalogueSoundStop(); Check(sound.events.size() == 1);
    std::printf("native sound adapter: %u checks passed\n", checks);
    return 0;
}
