// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "ce_commander_camera.h"
#include <cmath>

CeCommanderCameraOwner::CeCommanderCameraOwner(CeCommanderCameraConsumer& consumer) noexcept
    : consumer_(consumer), state_(State::Inactive) {}

CeCommanderCameraOwner::~CeCommanderCameraOwner() noexcept { Reset(); }

bool CeCommanderCameraOwner::Admitted(const CeCommanderCameraContext& context) noexcept {
    return context.admitted_ordinary_solo && context.live_pilot &&
           context.camera_available && !context.competing_camera_owner &&
           context.binding.mission != 0 && context.binding.pilot != 0 &&
           context.binding.camera != 0;
}

bool CeCommanderCameraOwner::ValidInput(const CeCommanderCameraInput& input) noexcept {
    return std::isfinite(input.elapsed_seconds) && input.elapsed_seconds >= 0 &&
           std::isfinite(input.pan_horizontal) && std::isfinite(input.pan_vertical) &&
           std::isfinite(input.orbit_horizontal) && std::isfinite(input.orbit_vertical) &&
           std::isfinite(input.zoom);
}

bool CeCommanderCameraOwner::Matches(const CeCommanderCameraContext& context) const noexcept {
    return binding_.mission == context.binding.mission &&
           binding_.pilot == context.binding.pilot && binding_.camera == context.binding.camera;
}

void CeCommanderCameraOwner::Clear() noexcept {
    state_ = State::Inactive;
    binding_ = CeCommanderCameraBinding();
}

CeCommanderCameraResult CeCommanderCameraOwner::RestoreOrRetain(
    CeCommanderCameraResult success) noexcept {
    state_ = State::RestorePending;
    if (!consumer_.Restore(binding_))
        return CeCommanderCameraResult::RestorePending;
    Clear();
    return success;
}

CeCommanderCameraResult CeCommanderCameraOwner::Enter(
    const CeCommanderCameraContext& context) noexcept {
    if (OwnsCapture()) {
        if (!Admitted(context) || !Matches(context)) {
            Reset();
            return CeCommanderCameraResult::ContextLost;
        }
        return Active() ? CeCommanderCameraResult::AlreadyActive
                        : CeCommanderCameraResult::RestorePending;
    }
    if (!Admitted(context))
        return CeCommanderCameraResult::Refused;
    if (!consumer_.Capture(context.binding))
        return CeCommanderCameraResult::CaptureFailed;
    binding_ = context.binding;
    state_ = State::RestorePending;
    if (!consumer_.BeginStrategicView(binding_))
        return RestoreOrRetain(CeCommanderCameraResult::ConsumerFailed);
    state_ = State::Active;
    return CeCommanderCameraResult::Entered;
}

CeCommanderCameraResult CeCommanderCameraOwner::Update(
    const CeCommanderCameraContext& context, const CeCommanderCameraInput& input) noexcept {
    if (!OwnsCapture())
        return CeCommanderCameraResult::Inactive;
    if (!Admitted(context) || !Matches(context)) {
        Reset();
        return CeCommanderCameraResult::ContextLost;
    }
    if (RestorePending())
        return CeCommanderCameraResult::RestorePending;
    if (!ValidInput(input))
        return CeCommanderCameraResult::Refused;
    if (!consumer_.UpdateStrategicView(binding_, input))
        return RestoreOrRetain(CeCommanderCameraResult::ConsumerFailed);
    return CeCommanderCameraResult::Updated;
}

CeCommanderCameraResult CeCommanderCameraOwner::Leave(
    const CeCommanderCameraContext& context) noexcept {
    if (!OwnsCapture())
        return CeCommanderCameraResult::Inactive;
    if (!Admitted(context) || !Matches(context)) {
        Reset();
        return CeCommanderCameraResult::ContextLost;
    }
    return RestoreOrRetain(CeCommanderCameraResult::Left);
}

void CeCommanderCameraOwner::Reset() noexcept {
    if (OwnsCapture())
        consumer_.Abandon(binding_);
    Clear();
}

bool CeCommanderCameraOwner::Active() const noexcept { return state_ == State::Active; }
bool CeCommanderCameraOwner::OwnsCapture() const noexcept { return state_ != State::Inactive; }
bool CeCommanderCameraOwner::RestorePending() const noexcept { return state_ == State::RestorePending; }
