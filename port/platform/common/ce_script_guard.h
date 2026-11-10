// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_script_guard.h: the decision behind patch 0176, the script VM guard for levels a mod supplies.
//
// The game runs every mission script on one global VM. A native a script calls can start a unit dying at once (the Damage native
// does: CUnit::Damage starts the die process, which tells the unit's own script, IScript::StartedDying), and that notification
// enters the same VM while it is still running the outer handler. The VM keeps its code and program counter in two fields the
// nested entry overwrites, so the outer handler either crashes at the next fetch (the unit's script has no handler for the event)
// or goes on running the other script's handler in its place. No shipped content reaches the first, and one shipped script
// reaches the second (a level's own unit damaging itself inside its handler, which the original executes in place), so the
// original's behaviour is kept for every level the game ships. A level a mod supplies is another matter: its scripts are new,
// and a script that damages a unit must not be able to crash the game. There the notification waits for the next frame, when the
// VM is idle, and runs then (IScript::HandleEvent re-enters it); if the unit's script is gone by then it is dropped (by code reading; no run shows it).
#ifndef PORT_CE_SCRIPT_GUARD_H
#define PORT_CE_SCRIPT_GUARD_H

// Whether the notification `entry` ("startedDying" or "died") of the script of level `level`, entered while the script VM is
// running, is to be deferred a frame: yes when an active mod supplies the level's archive (data/resources/NNN_res_PC.aya) or its
// world file (data/worlds/worldNNN.wrd), whether it replaces the game's or adds one; no for every other level (and whenever mods
// are off, fenced or refused). A yes is counted and logged once for each entry.
bool PortCeScriptDeferNested(int level, const char *entry);

// How many notifications were deferred in this run.
int PortCeScriptDeferred();

#endif
