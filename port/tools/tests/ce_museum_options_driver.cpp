// SPDX-License-Identifier: GPL-3.0-only
// Authored career stand-in only: no game object, no installed content. The visit's own keep and restore code (ce_museum_options.h) runs on it.
#include <cstdio>
#include <cstdlib>
#include "ce_museum_options.h"
static void Need(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}
// A career-shaped object: the eight option fields the pause menu writes, and a progress half (levels, grades, goodies, kills) that a visit must not carry over.
struct FakeCareer {
    float sound, music;
    int invert[2], invertJet[2], vibration[2], controller[2], screenX, screenY;
    int progress[4];
    int god[2];
    int options_writes, progress_writes;
    float GetSoundVolume() { return sound; }                 void SetSoundVolume(float v) { sound = v; ++options_writes; }
    float GetMusicVolume() { return music; }                 void SetMusicVolume(float v) { music = v; ++options_writes; }
    int GetInvertYAxis(int p) { return invert[p]; }          void SetInvertYAxis(int p, int v) { invert[p] = v; ++options_writes; }
    int GetInvertJetYAxis(int p) { return invertJet[p]; }    void SetInvertJetYAxis(int p, int v) { invertJet[p] = v; ++options_writes; }
    int GetVibration(int p) { return vibration[p]; }         void SetVibration(int p, int v) { vibration[p] = v; ++options_writes; }
    int GetControllerConfigurationNum(int p) { return controller[p]; }  void SetControllerConfigurationNum(int p, int v) { controller[p] = v; ++options_writes; }
    int GetScreenPosX() { return screenX; }                  void SetScreenPosX(int v) { screenX = v; ++options_writes; }
    int GetScreenPosY() { return screenY; }                  void SetScreenPosY(int v) { screenY = v; ++options_writes; }
    int GetIsGod(int p) { return god[p]; }                   void SetIsGod(int p, int v) { god[p] = v; }
};
// Every option field a different number, so that a value carried to the wrong field cannot pass.
static FakeCareer Distinct(int seed) {
    FakeCareer c = FakeCareer();
    c.sound = 0.1f * seed + 0.01f; c.music = 0.1f * seed + 0.02f;
    c.invert[0] = seed + 1; c.invert[1] = seed + 2; c.invertJet[0] = seed + 3; c.invertJet[1] = seed + 4;
    c.vibration[0] = seed + 5; c.vibration[1] = seed + 6; c.controller[0] = seed + 7; c.controller[1] = seed + 8;
    c.screenX = seed + 9; c.screenY = seed + 10;
    for (int i = 0; i != 4; ++i) c.progress[i] = 1000 * seed + i;
    c.god[0] = seed + 11; c.god[1] = seed + 12;
    return c;
}
static bool SameOptions(const FakeCareer &a, const FakeCareer &b) {
    return a.sound == b.sound && a.music == b.music && a.invert[0] == b.invert[0] && a.invert[1] == b.invert[1] && a.invertJet[0] == b.invertJet[0] &&
           a.invertJet[1] == b.invertJet[1] && a.vibration[0] == b.vibration[0] && a.vibration[1] == b.vibration[1] && a.controller[0] == b.controller[0] &&
           a.controller[1] == b.controller[1] && a.screenX == b.screenX && a.screenY == b.screenY;
}
int main() {
    // What a visit does: entry copies the career; the visitor changes options in the pause menu (and the visit changes progress); the end keeps the options,
    // puts the entry copy back and gives the options back over it.
    const FakeCareer entry = Distinct(1);
    FakeCareer live = Distinct(2);                                // the visitor's own values for every option, and the visit's progress
    const CeMuseumKeptOptions kept = CeMuseumKeepOptions(live);
    const FakeCareer visitors = live;
    live = entry;                                                 // the whole-record restore
    live.options_writes = 0;
    CeMuseumRestoreOptions(live, kept);
    Need(SameOptions(live, visitors), "every option the visitor set is theirs after the visit, each in its own field");
    for (int i = 0; i != 4; ++i) Need(live.progress[i] == entry.progress[i], "the progress half of the record is exactly as it was");
    Need(live.options_writes == 12, "exactly the eight option fields (twelve writes: two players of five) are written, and nothing else");
    // Keeping reads only: it writes nothing into the career
    FakeCareer reader = Distinct(3); reader.options_writes = 0;
    (void)CeMuseumKeepOptions(reader);
    Need(reader.options_writes == 0, "keeping the options does not write the career");
    // An option the visitor left alone comes back as it was, and a different seed is a different answer
    FakeCareer untouched = Distinct(4); const CeMuseumKeptOptions same = CeMuseumKeepOptions(untouched);
    FakeCareer after = Distinct(5); CeMuseumRestoreOptions(after, same);
    Need(SameOptions(after, untouched), "an option left alone comes back as it was");
    Need(!SameOptions(Distinct(4), Distinct(5)), "the stand-in tells two careers apart");
    // What a save writes during a visit (patch 0279): the entry copy's progress, the visitor's options now and the god flags the save is writing.
    FakeCareer saved = entry;
    const FakeCareer during = Distinct(6);
    FakeCareer liveNow = during;
    CeMuseumCareerToSave(saved, liveNow);
    Need(SameOptions(saved, during), "a save during a visit writes the visitor's options, each in its own field");
    for (int i = 0; i != 4; ++i) Need(saved.progress[i] == entry.progress[i], "a save during a visit writes the progress the visit found, not the hall's");
    Need(saved.god[0] == during.god[0] && saved.god[1] == during.god[1], "a save during a visit writes the god flags the save is writing");
    Need(liveNow.options_writes == during.options_writes && liveNow.progress[0] == during.progress[0] && liveNow.god[0] == during.god[0],
         "a save during a visit leaves the live career alone");
    std::puts("PASS options kept and restored field by field");
    return 0;
}
