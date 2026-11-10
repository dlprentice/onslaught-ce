// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_MODEL_QUALITY_H
#define PORT_MODEL_QUALITY_H

// Main-thread API. The caller owns persistence and any session-only feedback.
// A valid request remains usable for this session even if saving fails.
// A queued choice applies at the next complete game-render frame, including all
// mirror and split-screen views. It never changes midway through those views.
extern "C" int CE_FullModelsChoice();
extern "C" int CE_FullModelsRequest(int value);
void CeModelQualityBeginFrame();
bool CeModelQualityEnabled();

// Bounded diagnostics count renderer submissions, not successful GPU draws.
enum CeModelQualityEvent {
    CE_MODEL_MESH, CE_MODEL_TREE, CE_MODEL_IMPOSTOR, CE_MODEL_TREE_IMPOSTOR,
    CE_MODEL_TREE_BATCH, CE_MODEL_REDUCED_MESH, CE_MODEL_EVENTS
};
void CeModelQualityCount(CeModelQualityEvent event);

#endif
