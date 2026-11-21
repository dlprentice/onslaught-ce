// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_editor_testkit.cpp: the test kit's view of the map editor (ce_testkit.h; port/journeys/README.md): its screen
// as page ed:map (the box is its notice), the state keys editor.phase, editor.view and editor.selected, and the TESTWRITE verb
// editor-pose. Compiled only by build.py --test-kit; it reads the editor through ce_editor.h's plain accessors.
#include "ce_editor.h"
#include "../platform/common/ce_testkit.h"
#include <stdio.h>
#include <stdlib.h>

namespace
{
// editor-pose X Y [DISTANCE [YAW [PITCH]]]: puts the editor's view over a map point, as the keys and the orbit drag would, but
// in one write. The view's own limits (the level's edges, the pitch range) apply at the next frame, as they do to the keys.
// TESTWRITE: a journey that uses it still has to reach its outcome with keys alone for acceptance.
CeTestResult VerbPose(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: editor-pose X Y [DISTANCE [YAW [PITCH]]]"); return CE_TEST_FAILED; }
	if (!PortCeEditorAuthoringShown()) { CeTestReplyf(r, "waiting for the editor to be open"); return CE_TEST_PENDING; }
	CeEditorPoint *focus = PortCeEditorViewFocus();
	focus->x = float(atof(a.word[1]));
	focus->y = float(atof(a.word[2]));
	if (a.count > 3) *PortCeEditorViewDistance() = float(atof(a.word[3]));
	if (a.count > 4) *PortCeEditorViewYaw() = float(atof(a.word[4]));
	if (a.count > 5) *PortCeEditorViewPitch() = float(atof(a.word[5]));
	CeTestReplyf(r, "focus %.1f %.1f distance %.1f yaw %.3f pitch %.3f", focus->x, focus->y, *PortCeEditorViewDistance(),
				 *PortCeEditorViewYaw(), *PortCeEditorViewPitch());
	return CE_TEST_DONE;
}
bool	StatePhase(char *out, size_t size)
{
	static const char *const names[] = {"authoring", "load-playtest", "playing", "return", "refused"};
	const int phase = PortCeEditorPhaseNow();
	if (phase < 0) return false;
	snprintf(out, size, "%s", phase <= 4 ? names[phase] : "other");
	return true;
}
bool	StateView(char *out, size_t size)
{
	if (!PortCeEditorAuthoringShown()) return false;
	const CeEditorPoint *focus = PortCeEditorViewFocus();
	snprintf(out, size, "%.0f,%.0f,%.0f", focus->x, focus->y, *PortCeEditorViewDistance());
	return true;
}
bool	StateSelected(char *out, size_t size)
{
	if (!PortCeEditorAuthoringShown()) return false;
	const int selected = PortCeEditorSelection();
	snprintf(out, size, "%s", selected == CeEditorState::PlayerStartSelection ? "player-start" : selected < 0 ? "none" : "object");
	return true;
}
void	PageId(char *out, size_t size) { snprintf(out, size, "ed:map"); }
void	PageTitle(char *out, size_t size) { if (size) out[0] = 0; }
int		PageRows(CeTestRow *, int) { return 0; }
int		PageFocused() { return -1; }
void	PageBox(char *out, size_t size) { snprintf(out, size, "%s", PortCeEditorNoticeShown()); }
}

CE_TEST_PAGE("ed", 50, PortCeEditorAuthoringShown, PageId, PageTitle, PageRows, PageFocused, PageBox);
CE_TEST_VERB("editor-pose", "editor-pose X Y [DISTANCE [YAW [PITCH]]]: the editor's view over a map point", CE_TEST_TESTWRITE,
			 VerbPose);
CE_TEST_STATE("editor.phase", StatePhase);
CE_TEST_STATE("editor.view", StateView);
CE_TEST_STATE("editor.selected", StateSelected);
