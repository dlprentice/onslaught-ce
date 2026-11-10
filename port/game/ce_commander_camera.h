// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only

#ifndef CE_COMMANDER_CAMERA_H
#define CE_COMMANDER_CAMERA_H

#include <cstdint>

// Local lifetime identities supplied by the engine adapter. Each nonzero token
// must change when its object is replaced, including at a reused address.
// Controller indices, raw addresses and network roles are not lifetime IDs.
struct CeCommanderCameraBinding {
    std::uint64_t mission = 0;
    std::uint64_t pilot = 0;
    std::uint64_t camera = 0;
};

struct CeCommanderCameraContext {
    CeCommanderCameraBinding binding;
    // Derive this from the existing ordinary single-player commander gate:
    // feature/layer enabled, playing, no recording, replay, virtual clock,
    // diagnostic, network, frontend or menu path. It grants no team role.
    bool admitted_ordinary_solo = false;
    bool live_pilot = false;
    bool camera_available = false;
    bool competing_camera_owner = true;
};

// Input deltas, not world coordinates. The adapter supplies existing input
// scaling and camera geometry; this owner introduces no movement constants.
struct CeCommanderCameraInput {
    double elapsed_seconds = 0;
    double pan_horizontal = 0;
    double pan_vertical = 0;
    double orbit_horizontal = 0;
    double orbit_vertical = 0;
    double zoom = 0;
};

class CeCommanderCameraConsumer {
public:
    virtual ~CeCommanderCameraConsumer() noexcept {}
    // Capture owns one complete restorable view on success, none on failure.
    virtual bool Capture(const CeCommanderCameraBinding& binding) noexcept = 0;
    virtual bool BeginStrategicView(const CeCommanderCameraBinding& binding) noexcept = 0;
    virtual bool UpdateStrategicView(const CeCommanderCameraBinding& binding,
                                    const CeCommanderCameraInput& input) noexcept = 0;
    // Successful restore releases the capture. Failure retains it for Leave.
    virtual bool Restore(const CeCommanderCameraBinding& binding) noexcept = 0;
    // Release capture resources only. Never write the camera, pilot, clock or
    // input owner after a lifetime change or a transition into a fenced mode.
    virtual void Abandon(const CeCommanderCameraBinding& binding) noexcept = 0;
};

enum class CeCommanderCameraResult {
    Entered, Updated, Left, Inactive, AlreadyActive, Refused, ContextLost,
    CaptureFailed, ConsumerFailed, RestorePending
};

// Serialized game-thread owner. The consumer outlives it, does not re-enter it,
// and rechecks the real lifetime binding before each camera operation. Context
// is an adapter observation, not proof of engine state.
//
// Integration with the commander map:
// - Reuse its SetView/FitView and existing camera/input helpers; keep the
//   engine-specific complete snapshot and transform adapter beside them.
// - Enter before handing input to this view. Supply fresh authoritative context
//   for every Update/Leave; Update reapplies the view even without movement.
// - Keep the battle running and the real pilot present and vulnerable. Do not
//   inherit the paused map's Open/Close simulation-pause side effects.
// - Leave before a planned handoff while the binding and ordinary gate hold.
//   After replacement or a fenced transition, Reset discards the stale capture
//   without writing over the new owner's view.
// - Retain a RestorePending capture; refuse another view until restoration
//   succeeds or context is abandoned. Report failures through the caller's UI.
// - Wire mission end, pilot/camera replacement, feature disable and input-owner
//   changes explicitly. This component has no engine hooks or team-role APIs.
class CeCommanderCameraOwner {
public:
    explicit CeCommanderCameraOwner(CeCommanderCameraConsumer& consumer) noexcept;
    ~CeCommanderCameraOwner() noexcept;
    CeCommanderCameraOwner(const CeCommanderCameraOwner&) = delete;
    CeCommanderCameraOwner& operator=(const CeCommanderCameraOwner&) = delete;
    CeCommanderCameraOwner(CeCommanderCameraOwner&&) = delete;
    CeCommanderCameraOwner& operator=(CeCommanderCameraOwner&&) = delete;

    CeCommanderCameraResult Enter(const CeCommanderCameraContext& context) noexcept;
    CeCommanderCameraResult Update(const CeCommanderCameraContext& context,
                                  const CeCommanderCameraInput& input) noexcept;
    CeCommanderCameraResult Leave(const CeCommanderCameraContext& context) noexcept;
    void Reset() noexcept;
    bool Active() const noexcept;
    bool OwnsCapture() const noexcept;
    bool RestorePending() const noexcept;

private:
    enum class State { Inactive, Active, RestorePending };
    static bool Admitted(const CeCommanderCameraContext& context) noexcept;
    static bool ValidInput(const CeCommanderCameraInput& input) noexcept;
    bool Matches(const CeCommanderCameraContext& context) const noexcept;
    void Clear() noexcept;
    CeCommanderCameraResult RestoreOrRetain(CeCommanderCameraResult success) noexcept;
    CeCommanderCameraConsumer& consumer_;
    CeCommanderCameraBinding binding_;
    State state_;
};

#endif
