// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_MODEL_QUALITY_H
#define PORT_MODEL_QUALITY_H

// Main-thread API. The caller owns persistence and any session-only feedback.
// A valid request remains usable for this session even if saving fails.
// Queued choices apply atomically at the next complete game-render frame,
// including all mirror and split-screen views, and never change mid-frame.
// Unit/vehicle/building detail (full_unit_models, default ON in ordinary CE)
// and distant 3D tree replacement (full_tree_models, opt-in OFF by default)
// are controlled independently; legacy full_models=1 enables both when neither
// split key is set.
extern "C" int CE_FullUnitModelsChoice();
extern "C" int CE_FullUnitModelsRequest(int value);
extern "C" int CE_FullTreeModelsChoice();
extern "C" int CE_FullTreeModelsRequest(int value);
extern "C" int CE_FullModelsChoice();
extern "C" int CE_FullModelsRequest(int value);
void CeModelQualityBeginFrame();
bool CeUnitModelsQualityEnabled();
bool CeTreeModelsQualityEnabled();
bool CeModelQualityEnabled();

// Bounded diagnostics count renderer submissions, not successful GPU draws.
enum CeModelQualityEvent {
    CE_MODEL_MESH, CE_MODEL_TREE, CE_MODEL_IMPOSTOR, CE_MODEL_TREE_IMPOSTOR,
    CE_MODEL_TREE_BATCH, CE_MODEL_REDUCED_MESH, CE_MODEL_EVENTS
};
void CeModelQualityCount(CeModelQualityEvent event);

#endif
