// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION
#include <cstring>
#include "common.h"
#include "Platform.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "ce_world_view.h"
extern "C" unsigned PortPresentCount();
extern bool gMirrorPass;

// CE_WORLD_VIEW_IMPL_BEGIN: compiled unchanged by the authored provider checks.
namespace {
CeWorldViewSample gWorldView;
CeWorldViewOwner gWorldViewOwner = CeWorldViewOwner::Commander;
std::uint64_t gWorldViewToken;
int gWorldViewWidth, gWorldViewHeight;
bool gWorldViewValid;

bool WorldViewOwnerNow(CeWorldViewOwner* owner, std::uint64_t* token) {
    const std::uint64_t commander = PortCeCmdWorldViewToken ? PortCeCmdWorldViewToken() : 0;
    const std::uint64_t editor = PortCeEditorWorldViewToken ? PortCeEditorWorldViewToken() : 0;
    if ((commander != 0) == (editor != 0)) return false;
    *owner = commander ? CeWorldViewOwner::Commander : CeWorldViewOwner::Editor;
    *token = commander ? commander : editor;
    return true;
}
}

extern "C" void PortCeWorldViewCapture() {
    // Invalidate before every attempted capture; failure cannot expose an older
    // view from a different camera, including a failed device viewport query.
    gWorldViewValid = false;
    // A reflected pass must never become an interaction view. Retiring the old
    // sample also makes a skipped or failed following ordinary pass unavailable.
    if (gMirrorPass) return;
    CeWorldViewOwner owner;
    std::uint64_t token;
    if (!WorldViewOwnerNow(&owner, &token)) return;
    const int width = PLATFORM.GetWindowWidth(), height = PLATFORM.GetWindowHeight();
    if (width <= 0 || height <= 0 || !LT.GetDevice()) return;
    D3DVIEWPORT9 viewport;
    if (FAILED(LT.GetDevice()->GetViewport(&viewport)) || !viewport.Width || !viewport.Height ||
        double(viewport.X) + viewport.Width > width || double(viewport.Y) + viewport.Height > height) return;
    CeWorldViewSample sample = {};
    sample.viewport = {float(viewport.X), float(viewport.Y), float(viewport.Width), float(viewport.Height), viewport.MinZ, viewport.MaxZ};
    if (!CeViewDetail::Valid(sample.viewport)) return;
    D3DMATRIX view, projection;
    RENDERINFO.GetView(&view); RENDERINFO.GetProjection(&projection);
    static_assert(sizeof(view) == sizeof(sample.view), "D3D view must contain sixteen floats");
    static_assert(sizeof(projection) == sizeof(sample.projection), "D3D projection must contain sixteen floats");
    std::memcpy(sample.view, &view, sizeof(view));
    std::memcpy(sample.projection, &projection, sizeof(projection));
    double combined[16], inverse[16];
    if (!CeViewDetail::Combined(sample.view, sample.projection, combined) || !CeViewDetail::Inverse(combined, inverse)) return;
    sample.present = PortPresentCount();
    gWorldView = sample; gWorldViewOwner = owner; gWorldViewToken = token;
    gWorldViewWidth = width; gWorldViewHeight = height; gWorldViewValid = true;
}

bool CeWorldViewRead(CeWorldViewOwner owner, std::uint64_t token, CeWorldViewSample* out) {
    if (gMirrorPass || !out || !token || !gWorldViewValid || owner != gWorldViewOwner || token != gWorldViewToken) return false;
    CeWorldViewOwner current;
    std::uint64_t currentToken;
    if (!WorldViewOwnerNow(&current, &currentToken) || current != owner || currentToken != token ||
        PortPresentCount() - gWorldView.present > 1 ||
        PLATFORM.GetWindowWidth() != gWorldViewWidth || PLATFORM.GetWindowHeight() != gWorldViewHeight) return false;
    *out = gWorldView; return true;
}

void CeWorldViewInvalidate(CeWorldViewOwner owner) {
    if (gWorldViewValid && gWorldViewOwner == owner) gWorldViewValid = false;
}
// CE_WORLD_VIEW_IMPL_END
#endif
