// FrontEndPage.h: the page base class and the page numbers (reconstructed; Stuart's file is not in his release).
// CFrontEndPage's vtable (005dba94) has nine slots: Init returns TRUE (004014a0), Shutdown and DeActiveNotification
// are empty (00459990), Process/TransitionNotification/ActiveNotification take one argument (004014c0) and
// ButtonPressed/RenderPreCommon/Render two (00405db0).

#ifndef FRONTENDPAGE_H
#define FRONTENDPAGE_H

// The page numbers index CFrontEnd::mPages; CFrontEnd::Init (004662a0) stores each page's address in the slot of
// its number, in Stuart's order. Slot 19 holds the PC's screen-position page where Stuart had the controller page.
enum EFrontEndPage
{
	FEP_MAIN = 0,
	FEP_DEVELOPMENT,
	FEP_COMMON,
	FEP_BE_CONFIG,
	FEP_WINGMEN,
	FEP_BRIEFING,
	FEP_DEBRIEFING,
	FEP_LEVEL_SELECT,
	FEP_GOODIES,
	FEP_DEVSELECT,
	FEP_LOADGAME,				// 10
	FEP_SAVEGAME,
	FEP_INTRO,
	FEP_VIRTUAL_KEYBOARD,
	FEP_DIRECTORY,
	FEP_MULTIPLAYER_START,
	FEP_MULTIPLAYER,
	FEP_OPTIONS,
	FEP_CREDITS,
	FEP_SCREEN_POS,
	FEP_E3_LEVEL_SELECT,		// 20
	FEP_LANGUAGE_TEST,
	FEP_DEMOMAIN,
	FEP_UNKNOWN,				// 23: CFrontEnd::Init fills every slot with it first

	FEP_NUM_PAGES,				// 24

	FEP_TRANSITION = -1,		// mActivePage while changing pages (CFrontEnd::SetPage 00466ae3)
	FEP_NONE = -2				// the destination passed when not changing (CFrontEnd::Render 00468364)
};

// A page's state as CFrontEnd::Process passes it (00466d8d..00466dba).
enum EFEPState
{
	FEPS_ACTIVE = 0,
	FEPS_TRANSITIONING_TO,
	FEPS_TRANSITIONING_FROM,
	FEPS_INACTIVE
};

class CFrontEndPage
{
public:
	virtual BOOL	Init() { return TRUE; }
	virtual void	Shutdown() {}
	virtual void	Process(EFEPState state) {}
	virtual void	ButtonPressed(SINT button, float val) {}
	virtual void	RenderPreCommon(float transition, EFrontEndPage dest) {}
	virtual void	Render(float transition, EFrontEndPage dest) {}
	virtual void	TransitionNotification(EFrontEndPage from) {}
	virtual void	ActiveNotification(EFrontEndPage from) {}
	virtual void	DeActiveNotification() {}
};

// The front end's buttons (CFrontEndPage::ButtonPressed) are Stuart's ids in Controller.h (42..46, 54, 55), from
// CFEPLoadGame::ButtonPressed's switch (00461c60, jump table 00461d28, index bytes 00461d44).
#include "Controller.h"

#endif
