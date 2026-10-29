// SPDX-License-Identifier: GPL-3.0-only
// CE save error notice (port/game/ce_notice.cpp, patch 0137): what the player is told when a save the game makes on its own
// (the pause menu's Continue, a language change) did not happen.
#ifndef PORT_CE_NOTICE_H
#define PORT_CE_NOTICE_H

// CFEPOptions::WriteDefaultOptionsFile calls this where the options file was not written, and the two quiet save sites ask
// whether that happened since they last asked (the function returns void, as in the retail code).
void	PortCeOptionsWriteFailed();
bool	PortCeTakeOptionsFailure();

// The front end's own save error line (FETX_SAVEERROR in the game's text file for the language the player chose: "An error occurred
// during saving." in English) copied into out (n WCHARs). False, with out empty, where the CE layer is off: a tape, a
// recording, probes or the virtual clocks.
bool	PortCeSaveErrorMessage(WCHAR *out, int n);

// In a level: the line as a note at the bottom of the screen for a few seconds (nothing where the CE layer is off).
void	PortCeSaveFailedNotice();
void	PortCeNoticeRender();		// after the console (CDXEngine::PostRender), never in a frame being saved as a screenshot

#endif
