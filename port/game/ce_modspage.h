// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modspage.h: the Onslaught CE page's Mods page (ce_modspage.cpp), as the CE page (ce_frontend.cpp) drives it: the hub
// lists its row when it is offered, opens it, and while it is open hands it the page's input, update and drawing. Public builds only.
#ifndef CE_MODSPAGE_H
#define CE_MODSPAGE_H

bool	PortCeModsPageOffered();							// the hub lists the Mods row
void	PortCeModsPageOpen();								// the row was chosen
bool	PortCeModsPageButton(SINT button, float val);		// true when the Mods page is open and took the input
void	PortCeModsPageProcess();							// once an update
bool	PortCeModsPageRender(float transition, EFrontEndPage dest);	// true when the Mods page is open and drew the page
void	PortCeModsPageShutdown();							// the CE page is shut down
bool	PortCeModsPageReturn(int level);					// back from a level a mod adds that the page started: open again, its mod's row highlighted
void	PortCeModsPageMainMenu();							// the main menu is being drawn: the once-a-start notice of mods that did not load

// What the page asks of the CE page (ce_frontend.cpp): the game's message box with a Continue button, no higher than `top_least` (the Details
// box of a long text is kept below the title bar), and the user folder (or a folder in it) as the CE page's own boxes name it.
void	PortCeShowMessage(const char *text, float top_least = 104.f);
char	*PortCeFolderText(char *out, int n, const char *sub);
void	PortCeLeaveCePage();								// the page starts a level: the CE page closes as it does for a bonus level

#endif
