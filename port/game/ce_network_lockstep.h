// SPDX-License-Identifier: GPL-3.0-only
// Two-player lockstep co-op (prototype). Both peers run the genuine simulation; a 20 Hz tick runs only when both
// players' inputs for it are present. Periodic hashes compare selected state; equal hashes do not prove full equality.
#ifndef CE_NETWORK_LOCKSTEP_H
#define CE_NETWORK_LOCKSTEP_H
#include <stdint.h>
#include "ce_network_campaign.h"
#ifdef __cplusplus
extern "C" {
#endif
int CE_LockstepRequested(void);         // the process was started for a lockstep session (BEA_LOCKSTEP_HOST/JOIN)
int CE_LockstepActive(void);            // a lockstep world is shown (views and HUD follow the local player), to the level's end
int CE_LockstepTicking(void);           // its ticks are shared: input travels in samples and the simulation fences apply
int CE_LockstepSharing(void);           // from configuring the level to its end: load, pre-run and ticks are the same on both peers
uint32_t CE_LockstepLocalPlayer(void);  // 0 on the host, 1 on the joining peer
// Session bridge (ce_network_session.cpp forwards here while CE_LockstepRequested()).
int CE_LockstepConfigureLevel(uint32_t level);
int CE_LockstepAfterLoad(void);
int CE_LockstepPreparePlay(void);
int CE_LockstepBeforeUpdate(void);      // 0: this tick waits for the other player's input
void CE_LockstepAfterUpdate(void);
void CE_LockstepBeforeRender(void);
int CE_LockstepBeginTeardown(int quit);
void CE_LockstepAfterWorldShutdown(void);
const char *CE_LockstepNotice(void);
const char *CE_LockstepError(void);	// why the session stopped, in words for the player (empty when it did not fail): the lobby shows it
int CE_LockstepExitCode(int native_code, int native_quit_seen);
// Controller bridge (ce_network_world.cpp's hooks forward here while CE_LockstepActive()).
int CE_LockstepControllerFlush(uint32_t controller);
int CE_LockstepCaptureAction(int button, float value, int mouse_axis);
int CE_LockstepInvertPitch(uint32_t player, int grounded, int original);
int CE_LockstepWalkerAny(uint32_t player, int original_type);
// Test only (BEA_LOCKSTEP_DUMP): each processed event, from CEventManager::Flush (patch 0122).
void CE_LockstepTraceEvent(int event_num, void *listener, float time);
// Test only: a collision check's inputs and time as it is scheduled (patch 0128).
void CE_LockstepTraceCollision(const float *centre, const float *other, float radius, float other_radius, float speed, float time);
// Test only (BEA_LOCKSTEP_DUMP, patch 0154): where an explosion starts (an x line: tick, position, the game generator's value; the caller on a y line) and the
// vertex, part transform and point a random surface point is made of (an s line).
void CE_LockstepTraceSpawn(const float *pos, void *caller);
void CE_LockstepTraceSurface(const float *vertex, int part, const float *ori9, const float *offset, const float *result);
// ... and, on an i line right after it, the thing's render position and orientation the transform was built with, the game's render fraction, the thing's render
// animation frame and whether it has a motion controller.
void CE_LockstepTraceSurfaceInputs(const float *pos3, const float *ori9, float render_frame, int controller);
// Test only (BEA_LOCKSTEP_NO_LOSS): CGame::DeclareLevelLost does nothing (patch 0123), so a long run keeps playing.
int CE_LockstepTestNoLoss(void);
// Test only (BEA_LOCKSTEP_NO_PARTICLES): CParticleManager::CreateEffect creates nothing on this peer during a lockstep session
// (patch 0153), so its pool stays empty while the other peer's is untouched.
int CE_LockstepTestNoParticles(void);
// Test only (BEA_LOCKSTEP_EXTRA_RENDERS=K): during a lockstep session the main loop draws at least K+1 frames between two ticks on this peer, as a much faster
// machine would (patch 0155); rendered is the number of frames drawn so far in this pass of the loop.
int CE_LockstepTestExtraRender(int rendered);
// Test only (BEA_LOCKSTEP_POISON_STACK): called first in every CGame::Update by the port's update hook (updatedump.cpp); fills the dead stack below it with one byte.
void CE_LockstepUpdateEntry(void);
// A radio message's voice (patch 0124): its length in seconds, read from its Ogg file through the game's file layer
// whether or not this PC has sound, is the same on both peers, so a lockstep message ends after it instead of when this
// PC's audio finishes. NULL starts a message with no length (one without a voice ends by its text, as in retail).
void CE_LockstepVoiceFile(const char *path);
float CE_LockstepVoiceSeconds(void);
// The sound manager's and music's random picks (patch 0125): the C runtime's rand, except during a lockstep tick, where
// each peer plays its own sounds (its own player's cockpit and warnings), so their draws use this peer's own stream.
int CE_LockstepSoundRand(void);
// A solo campaign level played by two (the tutorial, level 100): LoadLevel makes two players (patch 0126) and the
// second player's start, which the level does not have, goes beside the first player's (init: a CStartInitThing).
int CE_LockstepCoopPlayers(void);
// CWeapon::MoveEmitter (patch 0129): during a lockstep tick a live muzzle effect is placed after the update instead of
// mid-tick (1: deferred; 0: place it now).
int CE_LockstepDeferEmitter(void *weapon, int orientate);
void CE_LockstepPartnerStart(int player_index, void *init);
// Test-only tutorial co-op: a host-targeted script operation may also address the partner.
void *CE_LockstepTutorialPartner(void *thing);
// Both random streams (the C runtime's and the game's) across presentation work inside a shared tick: the voice decode
// in PlayPendingSamples, particle creation (its pool follows each peer's view) and what only a created particle draws.
void CE_LockstepCrtEnter(unsigned *state);
void CE_LockstepCrtLeave(const unsigned *state);
/* The game supplies the real terminal outcome; display text is never evidence.
 * Agreement also requires the authenticated result exchange and final barrier. */
void CE_LockstepCampaignOutcome(CeCampaignOutcome outcome);
int CE_LockstepCampaignActive(void);
int CE_LockstepCampaignAgreedWin(void);

#ifdef __cplusplus
}
struct CE_LockstepCrtHold {
	unsigned state[4]; int on;
	CE_LockstepCrtHold() : on(CE_LockstepSharing()) { if (on) CE_LockstepCrtEnter(state); }
	~CE_LockstepCrtHold() { if (on) CE_LockstepCrtLeave(state); }
};
#endif
#endif
