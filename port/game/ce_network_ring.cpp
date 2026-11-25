// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_network_ring.cpp: the ring of a network mission (plan C4). In a co-op mission the game's own pause menu
// cannot open: pausing would stop the shared simulation for one player, so the lockstep takes the pause button and
// Escape did nothing. Here Escape (or a pad's Start) opens the pause menu's ring over the battle, drawn by the game's own
// CPauseMenu::Render (the two silver circles turning in, the darkened screen, the rows in the menu face, the pointer),
// with two rows: Continue and Leave Game. Leave Game asks the pause menu's own question in the ring, "Are you sure?" with
// No and Yes and the focus on No (Escape and B mean No); Yes quits to the front end as the end screen's
// Quit does, which closes the connection, so the friend's game is told at once that this player left (ce_network_lockstep.cpp
// Poll) and the lobby tells this player "You left the game." on the first page.
//
// The battle does not stop: the ring says so in one line, and while it is open this player's controls are held still in
// the shared simulation (an empty control sample, as while text chat is typed), so both games stay in step. The ring
// takes its keys from window.cpp's event pump before the game sees them: Escape, Up, Down, Enter (and the keypad's), and
// on a pad Start, the D-pad, A and B. The pointer works as on the pause menu. It is not offered on the end screen, which
// has its own menu, nor before the shared ticks start. Network builds only (build.py: a port/game file named ce_network_*).
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "text.h"
#include "mouse.h"
#include "Controller.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_network_lockstep.h"
#include "ce_network_ring.h"

extern "C" void PortCeNetRingMousePoint(float x, float y, int *menuX, int *menuY);
extern "C" int PortCeChatCapturingInput(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchLocalSlot(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotCount(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotIsHuman(int slot) __attribute__((weak));

#include <SDL3/SDL.h>
#include <stdio.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)

namespace {

const int	kRingItem = 0x4352;
enum { RA_CONTINUE = 1, RA_LEAVE };

// A row with CE's words (the game's text table has no "Leave Game"), drawn by CMenuItem::Render as every pause row is.
class CRingItem : public CMenuItem
{
public:
	CRingItem(const char *text, int action) : CMenuItem((EPCText)0, kRingItem), mAction(action)
	{
		int i = 0;
		for (; text[i] && i < 63; i++)
			mWide[i] = (WCHAR)(unsigned char)text[i];
		mWide[i] = 0;
	}
	virtual WCHAR	*GetText() { return mWide; }
	WCHAR	mWide[64];
	int		mAction;
};

bool	gLeft = false;					// this player chose Leave Game and Yes
SDL_Scancode	gReleaseKey = SDL_SCANCODE_UNKNOWN;	// the key that closed the ring: controls stay still until it is up
int		gReleaseButton = -1;			// the same for a pad button

class CCeNetRing : public CPauseMenu
{
public:
	CCeNetRing() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		static WCHAR title[] = L"Network Game";
		mMain = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 240.f, FALSE, 0xb0000000);
		mMain->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_CONTINUE, 2));	// the pause menu's own Continue
		mMain->AddItem(new (MEMTYPE_MENUITEM) CRingItem("Leave Game", RA_LEAVE));
		// The pause menu's own question and answers ("Are you sure?", No, Yes), as a list of the ring itself: the ring
		// stays drawn around it (CPauseMenu::Render draws the circles for the first list only), the focus starts on No.
		mQuestion = new (MEMTYPE_MENUITEM) CMenuOptionsList(TEXT.GetString(IG_SURE), 320.f, 240.f, FALSE, 0xb0000000);
		mQuestion->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)IG_NO, 2));
		mQuestion->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)IG_YES, 2));
		mLists.Append(mMain);
	}

	bool	Open() const { return mActive != FALSE; }
	bool	Asking() { return mLists.At(0) == mQuestion; }

	void	Use(CMenuOptionsList *list)
	{
		mLists.RemoveAll();
		mLists.Append(list);
		list->Reset();
	}

	void	Show()
	{
		mHavePointer = mPointerPressed = mPointerReleased = false;
		Use(mMain);
		ShutdownMouse();	// as the game's pause does (CGame::Pause): the pointer follows the system's, not the aim
		Activate(TRUE);		// the pause menu's own opening: the circles turn in, the rows follow after 0.4 s
		fprintf(stderr, "onslaught-ce: network game: the ring opened (the battle goes on)\n");
	}

	void	Hide(bool quiet)
	{
		DeActivate(TRUE);	// and fades out as the pause menu does
		InitMouse();		// and the mouse aims again, as CPauseMenu::Resume gives it back
		if (!quiet)
			CFrontEnd::PlaySound(FES_BACK);
		fprintf(stderr, "onslaught-ce: network game: the ring closed\n");
	}

	void	Leave()
	{
		CFrontEnd::PlaySound(FES_SELECT);
		gLeft = true;
		fprintf(stderr, "onslaught-ce: network game: this player left the game\n");
		GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND);	// as the end screen's Quit: the connection closes, the friend is told
		DeActivate(FALSE);
	}

	// Keys, pad buttons and clicks, as the pause menu takes them (CPauseMenu::ReceiveButtonAction): nothing in the first
	// 0.4 s of the ring.
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (!mActive || PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f)
			return;
		CMenuOptionsList *list = mLists.At(0);
		switch (button)
		{
		default:
			list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
		{
			CMenuItem *item = list->mItems.At(list->mSelected);
			if (Asking())
			{
				if (item->mText == IG_YES)
					Leave();
				else
				{
					CFrontEnd::PlaySound(FES_BACK);
					Use(mMain);
					fprintf(stderr, "onslaught-ce: network game: leave confirmation cancelled\n");
					mMain->mSelected = 1;	// back on Leave Game, the row that asked (house style F2)
				}
			}
			else if (item->mId == kRingItem && ((CRingItem *)item)->mAction == RA_LEAVE)
			{
				CFrontEnd::PlaySound(FES_SELECT);
				Use(mQuestion);
				fprintf(stderr, "onslaught-ce: network game: leave confirmation opened\n");
			}
			else
				Hide(false);
			break;
		}
		case BUTTON_FRONTEND_MENU_BACK:
			if (Asking())
			{
				CFrontEnd::PlaySound(FES_BACK);
				Use(mMain);
				fprintf(stderr, "onslaught-ce: network game: leave confirmation cancelled\n");
				mMain->mSelected = 1;
			}
			else
				Hide(false);
			break;
		}
	}

	// CPauseMenu::Render reads the static options-page switch, which the CE pages' constructor sets: the ring is drawn as
	// the in-mission menu (circles, dim, pointer) and the switch is put back. The circles are the game's pause menu's.

	void Pointer(float x, float y, int button)
	{
		int mx, my;
		PortCeNetRingMousePoint(x, y, &mx, &my);
		mHavePointer = true;
		mPointerX = mx;
		mPointerY = my;
		if (button > 0)
		{
			mPointerPressed = true;
			mPressRow = PointerRow(mx, my);
		}
		if (button < 0)
		{
			mPointerReleased = mPointerPressed && mPressRow >= 0 && mPressRow == PointerRow(mx, my);
			if (mPointerPressed && !mPointerReleased && mPressRow >= 0)
				fprintf(stderr, "onslaught-ce: network game: click released outside its row\n");
			mPointerPressed = false;
		}
	}

	int PointerRow(int x, int y)
	{
		// Match the centred two-row list, including the title's half-row offset.
		CMenuOptionsList *list = mLists.At(0);
		int h = 0;
		for (int i = 0; i < list->mItems.Size(); ++i) h += list->mItems.At(i)->GetHeight();
		float top = list->mY - 0.5f * h + 15.f;
		if (x < 0 || x >= 640) return -1;
		for (int i = 0; i < list->mItems.Size(); ++i)
		{
			const float bottom = top + list->mItems.At(i)->GetHeight();
			if (y >= top && y < bottom) return i;
			top = bottom;
		}
		return -1;
	}

	void KeyboardFocus()
	{
		mHavePointer = mPointerPressed = mPointerReleased = false;
	}
	void	Draw()
	{
		CPauseMenu *pause = GAME.GetPauseMenu();
		mCircle1 = pause ? pause->mCircle1 : NULL;
		mCircle2 = pause ? pause->mCircle2 : NULL;
		const bool options = mOptionsPage;
		mOptionsPage = false;
		// The mission recentres its aiming cursor. Keep its coordinates separate from
		// the local menu and give each completed click to the original list once.
		const int gameX = MOUSE_X, gameY = MOUSE_Y;
		const UBYTE alpha = MOUSE_CURSOR_ALPHA;
		const auto released = MOUSE_BUTTONS[0].mReleased;
		if (mHavePointer)
		{
			MOUSE_X = mPointerX;
			MOUSE_Y = mPointerY;
			const int hovered = PointerRow(mPointerX, mPointerY);
			if (hovered >= 0) mLists.At(0)->mSelected = hovered;
		}
		else
			MOUSE_X = MOUSE_Y = -1;
		MOUSE_BUTTONS[0].mReleased = mPointerReleased;
		MOUSE_CURSOR_ALPHA = mHavePointer ? 255 : 0;
		Render();
		mPointerReleased = false;
		MOUSE_BUTTONS[0].mReleased = released;
		MOUSE_CURSOR_ALPHA = alpha;
		MOUSE_X = gameX;
		MOUSE_Y = gameY;
		mOptionsPage = options;
		if (mActive && PLATFORM.GetSysTimeFloat() - mOpenTime > 0.4f)
		{
			// one line under the rows, in the description colour: what the ring does not do, or what Yes will do
			const char *line = "The battle goes on.";
			if (Asking())
			{
				if (PortCeOnlineMatchActive && PortCeOnlineMatchActive())
				{
					const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : 0;
					const int slots = PortCeOnlineMatchSlotCount ? PortCeOnlineMatchSlotCount() : 2;
					int humans = 0;
					for (int s = 0; s < slots && s < 4; ++s)
						if (!PortCeOnlineMatchSlotIsHuman || PortCeOnlineMatchSlotIsHuman(s))
							++humans;
					if (localSlot == 0)
						line = "Leaving ends the online match for all players.";
					else if (humans > 2)
						line = "An AI pilot will take over your slot.";
					else
						line = "Your friend's game ends too.";
				}
				else
				{
					line = "Your friend's game ends too.";
				}
			}
			static WCHAR wide[64];
			int i = 0;
			for (; line[i] && i < 63; i++)
				wide[i] = (WCHAR)(unsigned char)line[i];
			wide[i] = 0;
			CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
			SIZE size;
			font->GetTextExtent(wide, &size);
			font->DrawTextWithShadow(320.f - float(size.cx / 2), 274.f, 0xff7f6f5f, wide, 0, 0, 1.f, 1.f);	// cream at half strength: the stage doubles it (as the network pages' hints)
		}
	}

	CMenuOptionsList	*mMain, *mQuestion;
	int mPointerX = 0, mPointerY = 0, mPressRow = -1;
	bool mHavePointer = false, mPointerPressed = false, mPointerReleased = false;
};

CCeNetRing	*gRing = NULL;	// made at the first Escape of a mission and kept for the process (a network game's process
							// plays one level): its base destructor would release the pause menu's shared textures

// The ring is offered while this player's mission runs in step: not before the shared ticks, not on the end screen.
bool	Offered()
{
	return CE_LockstepTicking() && GAME.GetGameState() == GAME_STATE_PLAYING && !gLeft;
}

void	Press(int button)
{
	if (gRing)
		gRing->ReceiveButtonAction(NULL, button, 1.f);
}

void	Toggle()
{
	if (!gRing)
	{
		const bool options = CPauseMenu::mOptionsPage;
		gRing = new (MEMTYPE_MENUITEM) CCeNetRing();
		CPauseMenu::mOptionsPage = options;
	}
	if (gRing->Open())
	{
		if (gRing->Asking())
			Press(BUTTON_FRONTEND_MENU_BACK);	// Escape answers the question No, as the pause menu's question takes Back
		else
			gRing->Hide(false);
	}
	else
		gRing->Show();
}

} // namespace

extern "C" int PortCeNetWaitEvent(const union SDL_Event *event);	// ce_network_wait.cpp: the co-op start's waiting screen
extern "C" int PortCeNetRingEvent(const union SDL_Event *event)
{
	if (!event)
		return 0;
	if (PortCeNetWaitEvent(event))
		return 1;	// the start's waiting screen is up and took it
	if (event->type == SDL_EVENT_KEY_UP && event->key.scancode == gReleaseKey)
	{
		gReleaseKey = SDL_SCANCODE_UNKNOWN;
		return 0;
	}
	if (event->type == SDL_EVENT_GAMEPAD_BUTTON_UP && event->gbutton.button == gReleaseButton)
	{
		gReleaseButton = -1;
		return 0;
	}
	if (!Offered())
	{
		if (gRing && gRing->Open())
			gRing->Hide(true);
		return 0;
	}
	const bool open = gRing && gRing->Open();
	if (open && event->type == SDL_EVENT_MOUSE_MOTION)
	{
		gRing->Pointer(event->motion.x, event->motion.y, 0);
		return 1;
	}
	if (open && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP))
	{
		if (event->button.button == SDL_BUTTON_LEFT)
			gRing->Pointer(event->button.x, event->button.y, event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : -1);
		return 1;
	}
	if (open && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN))
		gRing->KeyboardFocus();
	if (event->type == SDL_EVENT_KEY_DOWN)
	{
		const SDL_Scancode key = event->key.scancode;
		if (key == SDL_SCANCODE_ESCAPE)
		{
			if (!open && PortCeChatCapturingInput && PortCeChatCapturingInput())
				return 0;
			if (!event->key.repeat)
			{
				Toggle();
				if (!gRing->Open())
					gReleaseKey = key;
			}
			return 1;
		}
		if (!open)
			return 0;
		if (key == SDL_SCANCODE_UP)
			Press(BUTTON_FRONTEND_MENU_UP);
		else if (key == SDL_SCANCODE_DOWN)
			Press(BUTTON_FRONTEND_MENU_DOWN);
		else if ((key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) && !event->key.repeat)
		{
			Press(BUTTON_PAUSE_MENU_SELECT);
			if (!gRing->Open())
				gReleaseKey = key;
		}
		else
			return 0;
		return 1;
	}
	if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
	{
		const int button = event->gbutton.button;
		if (button == SDL_GAMEPAD_BUTTON_START)
			Toggle();
		else if (!open)
			return 0;
		else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP)
			Press(BUTTON_FRONTEND_MENU_UP);
		else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
			Press(BUTTON_FRONTEND_MENU_DOWN);
		else if (button == SDL_GAMEPAD_BUTTON_SOUTH)
			Press(BUTTON_PAUSE_MENU_SELECT);
		else if (button == SDL_GAMEPAD_BUTTON_EAST)
			Press(BUTTON_FRONTEND_MENU_BACK);
		else
			return 0;
		if (!gRing->Open())
			gReleaseButton = button;
		return 1;
	}
	return 0;
}

extern "C" int PortCeNetRingCapturing(void)
{
	return (gRing && gRing->Open()) || gReleaseKey != SDL_SCANCODE_UNKNOWN || gReleaseButton >= 0;
}

extern "C" int PortCeNetRingLeft(void)
{
	return (gLeft || CE_LockstepLeftWaiting()) ? 1 : 0;	// the ring's Leave Game, or Leave on the start's waiting screen
}

void PortCeNetRingRender()
{
	if (!gRing)
		return;
	if (!Offered() && gRing->Open())
		gRing->Hide(true);
	gRing->Draw();
}

#endif
