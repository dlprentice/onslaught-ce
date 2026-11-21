// SPDX-License-Identifier: GPL-3.0-only
// The render fence: what an extra pass of the original's world drawing writes into the battle, saved before the pass and put back after it.
// In the original, drawing writes simulation state: CComponent::GetRenderPos refreshes a component's position and orientation from its parent's
// mesh (CComponent::RefreshCachedTransform, at most once a render frame), and drawing a thing advances its motion controller's pose (a mech's
// bones and legs, a buggy's wheels, a tentacle's parts, a turret's last drawn yaw and pitch), which projectile launch points, targetable positions
// and mesh collision read. The battle has one viewer (the pilot's Battle Engine in ordinary play): the original's world pass always runs with the
// viewer's camera, and a CE view that draws the world again from its own camera does it inside this fence, so the battle gets exactly the
// viewer's refreshes and none of the view's. The channel list is shared with co-op's lockstep (ce_network_world.cpp keeps the same kinds; there
// the restore is a policy of its own, network-only).
#ifndef CE_RENDER_FENCE_H
#define CE_RENDER_FENCE_H

// Saves every component's cached transform and render-frame stamp and every motion controller's pose bytes. False when it could not save all of
// them (out of memory): the caller then must not draw the extra pass.
bool CeRenderFenceSave();
// Puts back what the last CeRenderFenceSave kept.
void CeRenderFenceRestore();

#endif
