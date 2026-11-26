// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_network_wait.cpp: the waiting screen of a co-op start, in the lobby's own frame. Network builds only (build.py:
// a port/game file named ce_network_*), built as a game file so it draws with the game's fonts and menu rows.
//
// Between the restart into the level and the first shared tick each game waits for the other to load and to agree the start
// (ce_network_lockstep.cpp's start barriers, which call CE_SessionGameWaitFrame once a pass); nothing else draws then. The
// restarted game has no front end: the lobby's background video, borders, emblem and title bar are the front end's, and its
// data is not loaded in a level's process. So the lobby page keeps its own last frame instead: as the start begins it draws
// one frame of itself with the two player slots and no words (ce_netpages.cpp), which is written before the pointer is drawn
// (PortCeNetKeepStartFrame, from ce_frontend.cpp). The restarted game reads that frame before its level loads
// (CE_WAIT_PREPARE) and draws it under the waiting screen's words, which follow the lobby page: the slots' captions and
// states (the friend's state is the pulse: "Starting" and its dots), the page's line under the slots, the one row, Leave,
// drawn by the game's menu list as the lobby's rows are, and that row's hint. Without the kept frame the same words are drawn
// on black. Leave is chosen with Escape, Enter or a click on the row, or a pad's A, B or Start (a plain stick's first
// button); the keys come from the window's event pump through PortCeNetWaitEvent (ce_network_ring.cpp forwards it).
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "ltshell.h"
#include "state.h"
#include "renderinfo.h"
#include "ce_network_session.h"
#include "ce_network_stl.h"
#include "ce_online_match.h"
#include "../network/ice_game.h"
#include "../platform/common/ce_netlobby.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)

extern bool	gInFrontEnd;				// src/FrontEnd.cpp: the fonts' front-end scale
extern "C" int PortWriteFrameTo(IDirect3DDevice9 *dev, const char *path);	// d3d9hooks.cpp
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));	// d3dapp.cpp
extern "C" void PortCeNetRingMousePoint(float x, float y, int *menuX, int *menuY);	// window.cpp
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotCount(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchLocalSlot(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotIsHuman(int slot) __attribute__((weak));
extern "C" int PortCeOnlineMatchSlotLabel(int sim_slot, char *out, size_t cap) __attribute__((weak));
extern "C" int PortCeOnlineMatchGetTicket(CE_OnlineMatchTicket *out_ticket) __attribute__((weak));
void	PortCeNetDrawSlot(float x, float y, bool present);	// ce_network_lobby_slots.cpp

namespace {
// ce_netpages.cpp's colours: words before the list are given as they show; the hint after the list at half strength, since
// the list's row stage doubles text colours (the pages' kCreamAfterList).
const DWORD	kWhite = 0xffffffff, kGood = 0xff2eb030, kCreamAfterList = 0xff7f6f5f;
const float	kPageWidth = 420.f;			// the band between the page's two arcs (house style L5 and L7)
const float	kSlotLeftX = 250.f, kSlotRightX = 390.f, kSlotY = 170.f;	// the lobby's player slots (ce_netpages.cpp)

// ce_netpages.cpp's Widen: the game font's characters only.
void	Widen(WCHAR *out, int n, const char *in)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	int i = 0;
	for (; in[i] && i < n - 1; i++)
		out[i] = font->CanDisplay((WCHAR)(unsigned char)in[i]) ? (WCHAR)(unsigned char)in[i] : (WCHAR)' ';
	out[i] = 0;
}

// ce_netpages.cpp's Wrap: a little narrower when the last line would hold one word alone.
int	Wrap(CDXBitmapFont *font, SWordWrapFill *fill, WCHAR *text, float width)
{
	int n = font->WordWrap(fill, text, width);
	for (float w = width - 16.f; n >= 2 && w >= width * 0.7f; w -= 16.f)
	{
		bool alone = true;
		for (const WCHAR *c = fill->c[n - 1]; *c && alone; c++)
			alone = *c != ' ';
		if (!alone)
			break;
		const int narrower = font->WordWrap(fill, text, w);
		if (narrower != n)
		{
			font->WordWrap(fill, text, w + 16.f);
			break;
		}
	}
	return n;
}

// ce_netpages.cpp's Lines: the small font, wrapped, each line centred on the page, from y down; returns the next y.
float	Lines(const char *text, float y, DWORD colour)
{
	static SWordWrapFill fill;
	static WCHAR buf[600];
	Widen(buf, 600, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	int n = Wrap(font, &fill, buf, kPageWidth);
	float line = float(font->GetHeight());
	for (int l = 0; l < n; l++)
	{
		SIZE size;
		font->GetTextExtent(fill.c[l], &size);
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	}
	return y + line * n;
}

// ce_netpages.cpp's LinesAt: one line centred on x (a slot's caption); returns its width.
float	LinesAt(const char *text, float x, float y, DWORD colour)
{
	static WCHAR buf[64];
	Widen(buf, 64, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	SIZE size;
	font->GetTextExtent(buf, &size);
	font->DrawTextWithShadow(x - float(size.cx / 2), y, colour, buf, 0, 0, 1.f, 1.f);
	return float(size.cx);
}

// The lobby's row, as ce_netpages.cpp's CNetItem: CE's words drawn by CMenuItem::Render.
class CWaitItem : public CMenuItem
{
public:
	CWaitItem(const char *text) : CMenuItem((EPCText)0, 0x4357) { Widen(mWide, 32, text); }
	virtual WCHAR	*GetText() { return mWide; }
	WCHAR	mWide[32];
};

// The one list, centred where the lobby centres its rows (y 320: a single row stands where the guest's Leave stands).
class CWaitMenu : public CPauseMenu
{
public:
	CWaitMenu() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		static WCHAR title[] = L"Network Game";
		mList = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 320.f, FALSE, 0xb0000000);
		mList->AddItem(new (MEMTYPE_MENUITEM) CWaitItem("Leave"));
		mLists.Append(mList);
	}
	float	RowTop() { return mList->mY - float(mList->mItems.At(0)->GetHeight()) * 0.5f - 15.f; }
	float	RowBottom() { return RowTop() + float(mList->mItems.At(0)->GetHeight()); }
	CMenuOptionsList	*mList;
};

CWaitMenu	*sMenu = NULL;			// made at the first wait and kept for the process (a co-op process plays one level)
bool		sShown = false;			// the waiting screen is up: its keys are taken
bool		sLeave = false;			// Leave was chosen
bool		sPressed = false;		// a left button went down on the row
float		sDrawn = -1.f;
IDirect3DTexture9	*sFrame = NULL;	// the lobby's kept frame
IDirect3DDevice9	*sFrameDevice = NULL;	// owning D3D9 device for sFrame
uint64_t			sFrameGen = 0u;			// D3D9 device generation for sFrame
unsigned char		*sFrameRgb = NULL;	// kept RGB bytes for recreating sFrame across device change
unsigned	sFrameW = 0, sFrameH = 0;

uint64_t	CurrentFrameDeviceGen()
{
	return PortCeD3dDeviceGeneration ? PortCeD3dDeviceGeneration() : 0u;
}

void	ReleaseFrameTexture()
{
	if (sFrame)
	{
		sFrame->Release();
		sFrame = NULL;
	}
	sFrameDevice = NULL;
	sFrameGen = 0u;
}

void	EnsureFrameTexture(IDirect3DDevice9 *dev)
{
	if (!dev || !sFrameRgb || !sFrameW || !sFrameH)
		return;
	const uint64_t gen = CurrentFrameDeviceGen();
	if (sFrame && sFrameDevice == dev && sFrameGen == gen)
		return;
	ReleaseFrameTexture();
	IDirect3DTexture9 *tex = NULL;
	D3DLOCKED_RECT lr;
	if (SUCCEEDED(dev->CreateTexture(sFrameW, sFrameH, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &tex, NULL)) &&
		SUCCEEDED(tex->LockRect(0, &lr, NULL, 0)))
	{
		for (unsigned y = 0; y < sFrameH; y++)
		{
			unsigned *row = (unsigned *)((unsigned char *)lr.pBits + y * lr.Pitch);
			const unsigned char *in = sFrameRgb + size_t(y) * sFrameW * 3;
			for (unsigned x = 0; x < sFrameW; x++, in += 3)
				row[x] = 0xff000000u | (unsigned(in[0]) << 16) | (unsigned(in[1]) << 8) | in[2];
		}
		tex->UnlockRect(0);
		sFrame = tex;
		sFrameDevice = dev;
		sFrameGen = gen;
	}
	else if (tex)
	{
		tex->Release();
	}
}

bool	OnRow(float x, float y)
{
	int mx = 0, my = 0;
	PortCeNetRingMousePoint(x, y, &mx, &my);
	return sMenu && mx >= 0 && mx < 640 && float(my) >= sMenu->RowTop() && float(my) < sMenu->RowBottom();
}

// The kept frame, from the lobby's file into a texture, before the level loads (recreated if D3D9 device changes).
void	Prepare()
{
	if (sFrameRgb)
	{
		EnsureFrameTexture(LT.GetDevice());
		return;
	}
	unsigned w = 0, h = 0;
	unsigned char *rgb = NULL;
	if (!CE_LobbyWaitingFrameTake(&w, &h, &rgb))
	{
		fprintf(stderr, "onslaught-ce: co-op: no lobby frame was kept; the waiting screen draws on black\n");
		return;
	}
	sFrameRgb = rgb;
	sFrameW = w;
	sFrameH = h;
	EnsureFrameTexture(LT.GetDevice());
	if (sFrame)
		fprintf(stderr, "onslaught-ce: co-op: the lobby's last frame is kept for the waiting screen (%ux%u)\n", w, h);
	else
		fprintf(stderr, "onslaught-ce: co-op: the lobby's last frame could not be made a texture; the waiting screen draws on black\n");
}

void	DrawFrame()
{
	IDirect3DDevice9 *dev = LT.GetDevice();
	if (!dev)
		return;
	if (sFrame && (sFrameDevice != dev || sFrameGen != CurrentFrameDeviceGen()))
		ReleaseFrameTexture();
	if (!sFrame && sFrameRgb)
		EnsureFrameTexture(dev);
	if (!sFrame)
		return;
	struct SVertex { float x, y, z, rhw, u, v; };
	const float w = float(PLATFORM.GetWindowWidth()), h = float(PLATFORM.GetWindowHeight());
	SVertex v[4] = {{-0.5f, -0.5f, 0.f, 1.f, 0.f, 0.f}, {w - 0.5f, -0.5f, 0.f, 1.f, 1.f, 0.f},
					{-0.5f, h - 0.5f, 0.f, 1.f, 0.f, 1.f}, {w - 0.5f, h - 0.5f, 0.f, 1.f, 1.f, 1.f}};
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	LT.D3D_SetTexture(0, sFrame);
	LT.SetVertexShader(NULL);
	LT.SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	LT.GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(SVertex));
	LT.D3D_SetTexture(0, NULL);
}

// The page's render states before its words and list, as ce_netpages.cpp's Draw sets them.
void	PageStates()
{
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetUnknown2ed(false);
	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.Apply();
}

const char	*WaitRouteText(bool here, DWORD *colour)
{
	if (PortCeOnlineMatchActive && PortCeOnlineMatchActive())
	{
		CE_OnlineMatchTicket ticket = {};
		const bool haveTicket = PortCeOnlineMatchGetTicket && PortCeOnlineMatchGetTicket(&ticket) != 0;
		const bool relayOnly = haveTicket && ticket.require_relay != 0;
		if (here)
		{
			*colour = kGood;
			return relayOnly ? "Connected via Relay" : "Connected (Online)";
		}
		*colour = kWhite;
		return relayOnly ? "Online Match: Connecting via TURN" : "Online Match: Connecting (P2P / TURN)";
	}
	if (!CE_NetGameIceRequested())
	{
		*colour = here ? kGood : kWhite;
		return "Direct LAN";
	}
	if (here)
	{
		*colour = kGood;
		return "Connected via Relay";
	}
	*colour = kWhite;
#if defined(CE_NETWORK_ICE)
	if (CE_LobbySignalingSelected())
	{
		switch (CE_LobbySignalingView().status)
		{
		case CeIceSignalStatus::NeedsConsent:
		case CeIceSignalStatus::AwaitingPairConsent:
		case CeIceSignalStatus::AwaitingPeerRequest:
		case CeIceSignalStatus::AwaitingPeerRecord:
			return "Relay: Waiting for partner";
		case CeIceSignalStatus::RateLimited:
			return "Relay: Daily free-tier limit reached";
		case CeIceSignalStatus::SetupExpired:
		case CeIceSignalStatus::GrantExpired:
		case CeIceSignalStatus::Expired:
		case CeIceSignalStatus::Closed:
			return "Relay: Pair invitation expired";
		default:
			return "Relay: Connecting via TURN";
		}
	}
#endif
	return "Relay: Connecting via TURN";
}

void	Draw(int mode, float now)
{
	STATE.UseDefault();
	if (!PLATFORM.BeginScene())
		return;
	PLATFORM.ClearScreen(0);
	DrawFrame();
	PageStates();
	const bool front = gInFrontEnd;
	gInFrontEnd = true;			// the fonts' front-end scale, as the lobby page has them
	// The slots' states, where the lobby shows them (the kept frame holds the slots and their captions; without it the
	// captions are drawn here): this player is ready; the friend's state is the pulse until the friend is connected.
	const bool online = PortCeOnlineMatchActive && PortCeOnlineMatchActive() != 0;
	const bool here = mode != CE_WAIT_FRIEND;
	if (online)
	{
		int slotCount = PortCeOnlineMatchSlotCount ? PortCeOnlineMatchSlotCount() : 2;
		if (slotCount < 2) slotCount = 2;
		if (slotCount > 4) slotCount = 4;
		const int localSlot = PortCeOnlineMatchLocalSlot ? PortCeOnlineMatchLocalSlot() : (int)CE_LockstepLocalPlayer();
		static const float kX2[2] = {250.f, 390.f};
		static const float kX3[3] = {180.f, 320.f, 460.f};
		static const float kX4[4] = {135.f, 258.f, 382.f, 505.f};
		const float *xs = (slotCount == 2) ? kX2 : (slotCount == 3 ? kX3 : kX4);
		for (int slot = 0; slot < slotCount; slot++)
		{
			const float sx = xs[slot];
			const bool you = (slot == localSlot);
			const bool human = !PortCeOnlineMatchSlotIsHuman || PortCeOnlineMatchSlotIsHuman(slot) != 0;
			char hdr[16];
			snprintf(hdr, sizeof hdr, "Player %d", slot + 1);
			char label[32] = {};
			if (!PortCeOnlineMatchSlotLabel || !PortCeOnlineMatchSlotLabel(slot, label, sizeof label) || !label[0])
				snprintf(label, sizeof label, "%s", you ? "You" : (human ? "Player" : "AI Pilot"));
			else if (strlen(label) > 14)
				label[14] = 0;
			LinesAt(hdr, sx, kSlotY - 38.f, 0xffa8a8a8);
			LinesAt(label, sx, kSlotY + 26.f, kWhite);
			if (you || !human || here)
				LinesAt(human ? "Ready" : "AI", sx, kSlotY + 44.f, kGood);
			else
			{
				const float width = LinesAt("Starting", sx - 9.f, kSlotY + 44.f, kWhite);
				static WCHAR dots[4][4] = {L"", L".", L"..", L"..."};
				PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(sx - 9.f + width * 0.5f, kSlotY + 44.f, kWhite,
															  dots[1 + int(now * 3.f) % 3], 0, 0, 1.f, 1.f);
			}
		}
		const char *waitMsg = here
			? "All players are here. Starting the match."
			: (slotCount > 2
				? "Waiting for the other players to start. On a slower PC this can take a minute."
				: "Waiting for the other player to start. On a slower PC this can take a minute.");
		const float afterWait = Lines(waitMsg, kSlotY + 72.f, kWhite);
		DWORD routeColour = kWhite;
		const char *route = WaitRouteText(here, &routeColour);
		Lines(route, afterWait + 4.f, routeColour);
	}
	else
	{
		const bool host = CE_LockstepLocalPlayer() == 0;
		for (int slot = 0; slot < 2; slot++)
		{
			const float sx = slot ? kSlotRightX : kSlotLeftX;
			const bool you = (slot == 0) == host;
			if (!sFrame)
			{
				LinesAt(slot ? "Player 2" : "Player 1", sx, kSlotY - 38.f, 0xffa8a8a8);
				LinesAt(you ? "You" : "Your Friend", sx, kSlotY + 26.f, kWhite);
			}
			if (you || here)
				LinesAt("Ready", sx, kSlotY + 44.f, kGood);
			else
			{
				// "Starting" stays put and its three dots fill in turn, so a long wait never looks like a stopped game.
				const float width = LinesAt("Starting", sx - 9.f, kSlotY + 44.f, kWhite);
				static WCHAR dots[4][4] = {L"", L".", L"..", L"..."};
				PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(sx - 9.f + width * 0.5f, kSlotY + 44.f, kWhite,
															  dots[1 + int(now * 3.f) % 3], 0, 0, 1.f, 1.f);
			}
		}
		const float afterWait = Lines(here ? "Your friend is here. Starting the game." :
			  "Waiting for your friend's game to start. On a slower PC this can take a minute.", kSlotY + 72.f, kWhite);
		DWORD routeColour = kWhite;
		const char *route = WaitRouteText(here, &routeColour);
		Lines(route, afterWait + 4.f, routeColour);
	}
	if (mode != CE_WAIT_PRERUN)
	{
		const bool options = CPauseMenu::mOptionsPage;
		CPauseMenu::mOptionsPage = true;	// the CE pages' rows: no title over the list, the options pages' look
		sMenu->mList->mSelected = 0;
		sMenu->mList->Render(sMenu);
		CPauseMenu::mOptionsPage = options;
		Lines("Leaves this game.", sMenu->RowTop() + 28.f, kCreamAfterList);	// the lobby's hint for Leave (ce_netpages.cpp RowHint)
	}
	gInFrontEnd = front;
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	PLATFORM.EndScene();
	PLATFORM.Flip(FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SetModulate(0);
}
}

// The lobby page, each frame it is drawn (ce_frontend.cpp, after the title bar and logo, before the pointer): on the first frame
// of a start, which the page draws with its slots and no words (ce_netpages.cpp), that frame is written for the restarted game.
void	PortCeNetKeepStartFrame()
{
	static bool kept = false;
	CE_LobbyStatus s;
	CE_LobbyStatusGet(&s);
	if (s.state != CE_LOBBY_STARTING)
	{
		kept = false;
		return;
	}
	if (kept)
		return;
	kept = true;
	char path[4096];
	if (!CE_LobbyWaitingFramePath(path, sizeof path) || !PortWriteFrameTo(LT.GetDevice(), path))
		fprintf(stderr, "onslaught-ce: network lobby: the page's frame could not be kept for the waiting screen\n");
}

// The window's events while the waiting screen is up (ce_network_ring.cpp's event hook asks here first): 1 when taken.
extern "C" int PortCeNetWaitEvent(const union SDL_Event *event)
{
	if (!sShown || !event)
		return 0;
	switch (event->type)
	{
	case SDL_EVENT_KEY_DOWN:
		if (event->key.repeat)
			return 1;
		if (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_RETURN ||
			event->key.scancode == SDL_SCANCODE_KP_ENTER)
			sLeave = true;
		return 1;
	case SDL_EVENT_KEY_UP:
		return 1;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		if (event->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || event->gbutton.button == SDL_GAMEPAD_BUTTON_EAST ||
			event->gbutton.button == SDL_GAMEPAD_BUTTON_START)
			sLeave = true;
		return 1;
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		if (!SDL_IsGamepad(event->jbutton.which) && event->jbutton.button == 0)
			sLeave = true;
		return 0;	// a gamepad's own button events follow as gamepad events; nothing else of the stick is taken
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button == SDL_BUTTON_LEFT)
			sPressed = OnRow(event->button.x, event->button.y);
		return 1;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button == SDL_BUTTON_LEFT && sPressed && OnRow(event->button.x, event->button.y))
			sLeave = true;
		sPressed = false;
		return 1;
	default:
		return 0;
	}
}

extern "C" int CE_SessionGameWaitFrame(int mode)
{
	if (mode == CE_WAIT_PREPARE)
	{
		Prepare();
		return 0;
	}
	if (mode == CE_WAIT_END)
	{
		sShown = sLeave = sPressed = false;
		sDrawn = -1.f;
		ReleaseFrameTexture();
		free(sFrameRgb);
		sFrameRgb = NULL;
		sFrameW = sFrameH = 0;
		return 0;
	}
	if (!sMenu)
	{
		const bool options = CPauseMenu::mOptionsPage;
		sMenu = new (MEMTYPE_MENUITEM) CWaitMenu();
		CPauseMenu::mOptionsPage = options;
	}
	const float now = PLATFORM.GetSysTimeFloat();
	if (mode == CE_WAIT_PRERUN)
	{
		// The last frame before this game's own pre-run, which draws nothing for a few seconds: no row it could not answer.
		sShown = false;
		Draw(mode, now);
		sDrawn = now;
		return 0;
	}
	if (!sShown)
	{
		sShown = true;
		sLeave = sPressed = false;
		fprintf(stderr, "onslaught-ce: co-op: the waiting screen is shown (%s)\n", mode == CE_WAIT_FRIEND ? "waiting for the other player" : "starting");
	}
	if (sLeave)
	{
		sShown = sLeave = false;
		CFrontEnd::PlaySound(FES_SELECT);
		return 1;
	}
	if (sDrawn < 0.f || now < sDrawn || now - sDrawn >= 1.f / 30.f)
	{
		Draw(mode, now);
		sDrawn = now;
	}
	return 0;
}

extern "C" void PortCeNetworkWaitDeleteDeviceObjects(void)
{
	ReleaseFrameTexture();
}

#else

// No CE layer: no waiting screen (the wait itself is unchanged).
union SDL_Event;
extern "C" int CE_SessionGameWaitFrame(int mode) { (void)mode; return 0; }
extern "C" int PortCeNetWaitEvent(const union SDL_Event *event) { (void)event; return 0; }
void	PortCeNetKeepStartFrame() {}

#endif
