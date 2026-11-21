// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// This is the game/platform boundary. The game has a different allocator; no
// vector, string or owned pointer may cross it.
enum CePreviewPhase {
  CE_PREVIEW_IDLE,
  CE_PREVIEW_PICKING,
  CE_PREVIEW_WORKING,
  CE_PREVIEW_VERIFIED,
  CE_PREVIEW_ERROR
};
// proposal is the reader's generic free-slot suggestion, kept for the
// standalone format. It is not the name Restore as New uses: that one comes
// from the storage interface and is shown through ce_career_restore_ui.h.
struct CePreviewDisplay {
  CePreviewPhase phase;
  int choices, careers, members, complete, goodies;
  char message[192], date[65], reason[201], name[201], proposal[80];
};
void CePreviewOpen(const char *user, const char *game, const char *root);
void CePreviewLeave();
void CePreviewChoose();
void CePreviewAgain();
void CePreviewSelectSet(unsigned index);
void CePreviewDisplayRead(unsigned career, CePreviewDisplay *out);
bool CePreviewDisplaySetName(unsigned index, char *out, unsigned capacity);
// What a listed set holds, read when the list was made: verified is 1 when the
// set and every file in it verified then (the page still verifies it again on
// selection), with its careers, whether it holds the options file and its
// manifest's Reason. False when there is no such row.
struct CePreviewSetHolds {
  int verified, careers, options;
  char reason[201];
};
bool CePreviewDisplaySetHolds(unsigned index, CePreviewSetHolds *out);
