// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_netpages.cpp: the CE page's "Network game" pages (public builds only): Host or Join, the host's code page,
// the join page with its code and address fields, and the lobby; then "Starting..." and the restart into the game
// process (port/platform/common/ce_netlobby.h). Built as a game file so it uses the game's menu classes, drawn inside the
// CE page (ce_frontend.cpp) as its other lists are.
//
// Never offered while the CE menus are off (replays, recordings, the test harness, -autoconfigtest, the playable demo)
// or under any replay, recording, probe or pilot switch, even with the CE layer forced on for screenshots: those runs
// see exactly the CE page they saw before (and retail's menus where the CE menus are off).
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "FEMessBox.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "text.h"
#include "TransitionHelpers.h"
#include "keytrap.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_network_stl.h"
#include "../platform/common/ce_netcode.h"
#include "../platform/common/ce_netlobby.h"
#include "../network/ice_game.h"
#include "ce_network_boot.h"
#include "ce_network_invitation.h"
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
#include "../platform/common/private_ui_input.h"
#endif
#include "../platform/common/runmode.h"
#include "ce_menu_fit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)
#define FEP_CE				FEP_LANGUAGE_TEST		// the CE page's slot (ce_frontend.cpp)

bool	PortCeMenus();							// ce_frontend.cpp
void	PortCeBoxOpened() __attribute__((weak));	// ce_frontend.cpp: mark FEMESSBOX as CE-owned so Esc/B dismisses it
// The pointer (src/mouse.h): its place in the front end's 640x480 screen, and a click inside a rectangle (consumed).
extern int	MOUSE_X, MOUSE_Y;
bool	MouseClickedInRect(float tlx, float tly, float brx, float bry);
// The lobby's player slot: the original's level orb (port/game/ce_network_lobby_slots.cpp).
void	PortCeNetDrawSlot(float x, float y, bool present);

#ifndef CE_NETWORK_OFF
// Online multiplayer lobby UI (port/game/ce_online_ui.cpp): weak hooks so standalone direct-LAN
// menu fixtures compile and keep their exact row order when ce_online_ui.cpp is not linked.
extern "C" {
void PortCeOnlineUiOpen(void) __attribute__((weak));
int  PortCeOnlineUiActive(void) __attribute__((weak));
int  PortCeOnlineUiOffered(void) __attribute__((weak));
void PortCeOnlineUiUpdate(void) __attribute__((weak));
void PortCeOnlineUiRender(void) __attribute__((weak));
void PortCeOnlineUiButton(int button, float val) __attribute__((weak));
void PortCeOnlineUiClose(void) __attribute__((weak));
int  PortCeOnlineUiTakePendingEntry(void) __attribute__((weak));
const char *PortCeOnlineUiTitleUtf8(void) __attribute__((weak));
void PortCeOnlineUiSetOutcomeNotice(const char *text, int good) __attribute__((weak));
int  CE_LobbyOutcomeOnline(void) __attribute__((weak));
}
#endif

namespace {

// The game font's characters only (no underscore, backquote, ~ \ $ @ [ ] ^ { | }): anything else becomes a space, once
// reported, so a page never shows the font's unknown-character box.
void	Widen(WCHAR *out, int n, const char *in)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	static char reported[256];
	int i = 0;
	for (; in[i] && i < n - 1; i++)
	{
		unsigned char c = (unsigned char)in[i];
		WCHAR w = (WCHAR)c;
		if (!font->CanDisplay(w))
		{
			if (!reported[c])
			{
				reported[c] = 1;
				fprintf(stderr, "onslaught-ce: network page character 0x%02x is not in the game's font (shown as a space)\n", c);
			}
			w = ' ';
		}
		out[i] = w;
	}
	out[i] = 0;
}

const DWORD	kWhite = 0xffffffff, kGrey = 0xffa8a8a8, kGood = 0xff2eb030, kWarn = 0xffffcc00, kBad = 0xffdc3c3c;
// Cream, the original's colour for words that describe (its Multiplayer page, 253,221,189 on the screen): white in the
// row face means "you can choose this" (house rule 6). These pages draw their words after the list with the plain
// modulate stage (Draw), so the colour is given as it shows, not at the half strength of the doubling row stage.
const DWORD	kCream = 0xfffdddbd;
const DWORD	kCreamAfterList = 0xff7f6f5f;
// kWarn is the game's focus colour (CMenuItem::Render): the player's pages draw no words in it. The relay pilot's pages still do.

// The join code is one word of up to 28 wide letters: at the usual 400 pixels the word wrap cut it in pieces (a lone letter on a line of its own); on a line of this width the widest code still fits.
const float	kCodeWidth = 560.f;
// The notices of the Host, Join and lobby pages (a few sentences long, since the game cannot tell the causes apart) and the help page's paragraphs are wrapped wider than the default 400.
const float	kNoticeWidth = 520.f, kHelpWidth = 540.f;
// The player's pages wrap their words to the band between the page's two arcs (house style L5 and L7: x 110 to 530).
const float	kPageWidth = 420.f;

// The words of Connection Help (port/tools/network_page_words_tests.py asserts them word for word). They tell the host what to
// do at the router and the firewall, and what the public address is. The game can see none of these, so nothing here claims to
// know what is wrong with a particular connection.
const char *const kHelpNetwork = "On the same network, Host Game and Join Game need nothing else.";
const char *const kHelpForward = "Over the internet, the host's router must send the game's port, %u (UDP), to the host's PC. Look for Port Forwarding in the router's settings.";
const char *const kHelpAddress = "The host then gives the friend the code and the router's public address. The friend types both.";
const char *const kHelpFirewall = "If Windows asks about the firewall, choose Allow Access.";

bool RelayRequested() { return CE_NetGameIceRequested() != 0; }

const char *DisplayServiceOrigin(const char *origin)
{
	if (!origin || !origin[0]) return "";
#ifdef CE_ONLINE_LOBBY_DEFAULT_URL
	if (CE_ONLINE_LOBBY_DEFAULT_URL[0] && !strcmp(origin, CE_ONLINE_LOBBY_DEFAULT_URL))
		return "Official Online Service (HTTPS)";
#endif
	if (strstr(origin, ".workers.dev") != NULL)
		return "Official Online Service (HTTPS)";
	return origin;
}

void ClearTransientSignaling()
{
#if defined(CE_NETWORK_ICE)
	if (CE_LobbySignalingSelected() && !RelayRequested())
		CE_LobbySignalingClear();
#endif
}

extern "C" int CE_CheatOn(int n);	// port/platform/common/ce.cpp: the Cheats page's switches

bool MenuAvailable(char *why, size_t size)
{
	// A cheat changes the battle on one side only, and the restarted network game refuses to start with one on
	// (ce_network_boot.cpp); say so here, before anything is hosted or joined, in the player's words.
	for (int cheat = 0; cheat < 6; cheat++)
		if (CE_CheatOn(cheat))
		{
			snprintf(why, size, "%s", "Network games need every cheat off. Turn them off on the Cheats page, then try again.");
			return false;
		}
#if !defined(CE_NETWORK_ICE)
    if (RelayRequested())
    {
        snprintf(why, size, "%s", "Experimental relay connections are unavailable in this build. Use a relay-enabled build, or restart without this connection setup.");
        return false;
    }
#endif
    return CE_LobbyAvailable(why, size) != 0;
}

// The text wrapped into fill at width, or a little narrower when that would leave one word alone on the last line (house
// rule 6: no line of a hint or paragraph ends on a single word); returns the number of lines.
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
			font->WordWrap(fill, text, w + 16.f);	// one line more is worse than one word alone: keep the last good wrap
			break;
		}
	}
	return n;
}

// The height Lines() would take for the text, so that a page can see before it draws whether its words fit above its list.
float	Height(const char *text, EFontType font_id = FONT_SMALL, float width = 400.f)
{
	if (!text || !*text)
		return 0.f;
	static SWordWrapFill fill;
	static WCHAR buf[600];
	Widen(buf, 600, text);
	CDXBitmapFont *font = PLATFORM.Font(font_id);
	return float(font->GetHeight()) * float(Wrap(font, &fill, buf, width));
}

// Where the words above a page's list must end: CMenuOptionsList::Render centres the rows on mY and draws the title 15 pixels above them.
float	ListTop(CMenuOptionsList *list)
{
	int height = 0;
	for (int i = 0; i < list->mItems.Size(); i++)
		height += list->mItems.At(i)->GetHeight();
	return list->mY - float(height) * 0.5f - 15.f;
}

// One line centred on x (a player slot's caption): no wrap, since a caption is one or two words.
void	LinesAt(const char *text, float x, float y, DWORD colour)
{
	static WCHAR buf[64];
	Widen(buf, 64, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	SIZE size;
	font->GetTextExtent(buf, &size);
	font->DrawTextWithShadow(x - float(size.cx / 2), y, colour, buf, 0, 0, 1.f, 1.f);
}

// The lobby's two player slots: centred on the page, 140 apart, on the band between the title bar and the rows.
const float	kSlotLeftX = 250.f, kSlotRightX = 390.f, kSlotY = 170.f;

// Text wrapped to the space between the page's borders, each line centred on the page, from y down; returns the next y.
float	Lines(const char *text, float y, DWORD colour, EFontType font_id = FONT_SMALL, float width = 400.f)
{
	if (!text || !*text)
		return y;
	static SWordWrapFill fill;
	static WCHAR buf[600];
	Widen(buf, 600, text);
	CDXBitmapFont *font = PLATFORM.Font(font_id);
	int n = Wrap(font, &fill, buf, width);
	float line = float(font->GetHeight());
	for (int l = 0; l < n; l++)
	{
		SIZE size;
		font->GetTextExtent(fill.c[l], &size);
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	}
	return y + line * n;
}

// A lobby notice, a failure or the end of a network game on the player's pages: the game's message box (house style E1 and
// rule 8), as the CE page's ShowMessage puts it up: black panel, grey edge, green OK, 400 wide, its top at or below y 122,
// FES_SELECT as it opens. It holds every sentence the lobby gives, so nothing a player must read is dropped for room.
void	Box(const char *text)
{
	if (!text || !*text)
		return;
	static WCHAR wide[400], probe[400];
	Widen(wide, 400, text);
	// House rule 6 for the box too: it wraps at its width less 16 (FEMessBox.cpp), so the box narrows in steps (to 70% at most)
	// while that leaves one word alone on its last line, and keeps its width when narrowing would add a line instead.
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	static SWordWrapFill fill;
	float width = 400.f;
	memcpy(probe, wide, sizeof probe);
	int n = font->WordWrap(&fill, probe, width - 16.f);
	for (float w = width - 16.f; n >= 2 && w >= 400.f * 0.7f; w -= 16.f)
	{
		bool alone = true;
		for (const WCHAR *c = fill.c[n - 1]; *c && alone; c++)
			alone = *c != ' ';
		if (!alone)
			break;
		memcpy(probe, wide, sizeof probe);
		if (font->WordWrap(&fill, probe, w - 16.f) != n)
			break;
		width = w;
	}
	FEMESSBOX.Kill();
	FEMESSBOX.Create(320.f, 240.f, width, FO(100), wide, font, 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, 0);
	if (FEMESSBOX.mY - FEMESSBOX.mHeight * 0.5f < 122.f)
		FEMESSBOX.mY = 122.f + FEMESSBOX.mHeight * 0.5f;
	FEMESSBOX.SetOnDismiss(FEP_NONE, 0);
	if (PortCeBoxOpened)
		PortCeBoxOpened();
	CFrontEnd::PlaySound(FES_SELECT);
}

enum EPage { NP_MENU = 0, NP_HOST_ADDRESS, NP_HOST, NP_JOIN, NP_LOBBY, NP_HELP, NP_SIGNAL_DISCLOSURE, NP_SIGNAL_STATUS, NP_INVITATION_INPUT, NP_INVITATION_PREVIEW, NP_COUNT };
enum EAction { NA_NONE = 0, NA_HOST, NA_JOIN, NA_BACK, NA_COPY, NA_CODE, NA_ADDRESS, NA_CONNECT, NA_MAP, NA_START, NA_LEAVE, NA_HELP, NA_HOST_ADDRESS, NA_CAMPAIGN_START, NA_PASTE_CODE, NA_PASTE_ADDRESS, NA_CLEAR_FIELDS, NA_SIGNAL_DISCLOSE, NA_SIGNAL_ACCEPT, NA_SIGNAL_PROCEED, NA_SIGNAL_RETRY, NA_SIGNAL_CLOSE, NA_IMPORT_OPEN, NA_IMPORT_ORIGIN, NA_IMPORT_PASTE, NA_IMPORT_HOST, NA_IMPORT_JOIN, NA_IMPORT_PROFILE_HOST, NA_IMPORT_PROFILE_JOIN, NA_IMPORT_USE, NA_IMPORT_CLEAR, NA_ONLINE_LOBBIES };

const char* SignalRouteStage(CeIceSignalStatus status, int lobbyState, bool signalingSelected, bool relayRequested)
{
    if (!relayRequested && !signalingSelected) return "Direct LAN";
    if (lobbyState == CE_LOBBY_READY) return "Connected via Relay";
    if (lobbyState == CE_LOBBY_JOIN_CONNECTING || lobbyState == CE_LOBBY_HOST_CONNECTING)
        return "Relay: Connecting via TURN";
    if (!signalingSelected) return "Relay: Waiting for pair invitation";
    switch (status) {
    case CeIceSignalStatus::NeedsConsent:
    case CeIceSignalStatus::AwaitingPairConsent:
    case CeIceSignalStatus::AwaitingPeerRequest:
    case CeIceSignalStatus::AwaitingPeerRecord:
        return "Relay: Waiting for partner";
    case CeIceSignalStatus::Admitted:
        return lobbyState == CE_LOBBY_HOST_WAITING ? "Relay: Waiting for partner" : "Relay: Connecting via TURN";
    case CeIceSignalStatus::Issuing:
    case CeIceSignalStatus::Exchanging:
    case CeIceSignalStatus::ServiceDeliveryComplete:
        return "Relay: Connecting via TURN";
    case CeIceSignalStatus::RateLimited:
        return "Relay: Daily free-tier limit reached";
    case CeIceSignalStatus::AuthenticationRequired:
    case CeIceSignalStatus::NotAdmitted:
        return "Relay: Pair invitation missing or invalid";
    case CeIceSignalStatus::SetupExpired:
    case CeIceSignalStatus::GrantExpired:
    case CeIceSignalStatus::Expired:
    case CeIceSignalStatus::Closed:
        return "Relay: Pair invitation expired";
    default:
        return "Relay: Waiting for pair invitation";
    }
}

const char* SignalHeading(CeIceSignalStatus status)
{
    switch (status) {
    case CeIceSignalStatus::NeedsConsent: return "Accept the approved pair";
    case CeIceSignalStatus::Admitted: return "Both players accepted";
    case CeIceSignalStatus::AwaitingPairConsent: return "Waiting for pair consent";
    case CeIceSignalStatus::AwaitingPeerRequest: return "Waiting for the other player";
    case CeIceSignalStatus::AwaitingPeerRecord: return "Waiting for the host offer";
    case CeIceSignalStatus::Exchanging: return "Exchanging connection records";
    case CeIceSignalStatus::Issuing: return "Preparing relay access";
    case CeIceSignalStatus::ServiceDeliveryComplete: return "Records delivered; connecting";
    case CeIceSignalStatus::RateLimited: return "Please wait before retrying";
    case CeIceSignalStatus::Closed: return "Approved pair closed";
    case CeIceSignalStatus::SetupExpired: return "Setup window ended";
    case CeIceSignalStatus::GrantExpired: return "Relay access expired";
    case CeIceSignalStatus::Expired: return "Approved pair expired";
    case CeIceSignalStatus::IssuanceUnknown: return "Relay issuance needs review";
    default: return "Approved signaling setup unavailable";
    }
}

const char* SignalDetail(CeIceSignalStatus status)
{
    switch (status) {
    case CeIceSignalStatus::NeedsConsent:
        return "Accept only if you want to play with the approved partner. Accepting permits relay setup for this private pair.";
    case CeIceSignalStatus::Admitted:
        return "Both players have accepted. Choose the enabled action to return to your match setup.";
    case CeIceSignalStatus::AwaitingPairConsent:
        return "Stay here while the other player accepts. Cancel invitation leaves this setup.";
    case CeIceSignalStatus::AwaitingPeerRequest:
        return "The other player must begin the same connection stage. Keep this page open while waiting.";
    case CeIceSignalStatus::AwaitingPeerRecord:
        return "Waiting for the approved host's connection offer before guest relay setup.";
    case CeIceSignalStatus::Exchanging:
        return "Protected connection records are being exchanged with the approved player.";
    case CeIceSignalStatus::Issuing:
        return "Relay access is being prepared. Wait for the result; starting another attempt does not extend this setup.";
    case CeIceSignalStatus::ServiceDeliveryComplete:
        return "Record delivery is complete. CE must still establish and validate the connection before the lobby is ready.";
    case CeIceSignalStatus::RateLimited:
        return "Daily free-tier relay limit or rate limit reached. Wait and retry, or try again after 00:00 UTC. Retry keeps the original setup deadline.";
    case CeIceSignalStatus::AuthenticationRequired:
        return "Approved pair admission is missing or no longer valid. Return and load a fresh approved setup.";
    case CeIceSignalStatus::NotAdmitted:
        return "This player is not admitted to the approved pair. Check the selected approved setup.";
    case CeIceSignalStatus::InvalidRequest:
        return "The selected connection setup was refused. Return and prepare the approved setup again.";
    case CeIceSignalStatus::IssuanceUnknown:
        return "Relay issuance could not be confirmed. Ask the operator to inspect this setup before trying another.";
    case CeIceSignalStatus::SetupExpired:
        return "The fixed setup window ended. Close this pair and prepare a fresh approved setup.";
    case CeIceSignalStatus::GrantExpired:
    case CeIceSignalStatus::Expired:
        return "Permission for this setup expired. Close this pair and prepare a fresh approved setup.";
    case CeIceSignalStatus::Closed:
        return "This approved pair is closed. Load a fresh approved setup before connecting again.";
    default:
        return "Check connectivity and the selected approved setup. Retry is explicit and never extends its deadline.";
    }
}

bool SignalRetryable(CeIceSignalStatus status)
{
    return status == CeIceSignalStatus::Unavailable || status == CeIceSignalStatus::RateLimited;
}

bool SignalTerminal(CeIceSignalStatus status)
{
    return status == CeIceSignalStatus::Closed || status == CeIceSignalStatus::Expired ||
        status == CeIceSignalStatus::GrantExpired || status == CeIceSignalStatus::SetupExpired ||
        status == CeIceSignalStatus::IssuanceUnknown;
}


// Keep the same bounded prefix for field text and cursor measurement.
void FieldText(char* text, size_t capacity, const char* label, const char* value)
{
    if (!capacity) return;
    size_t used = 0;
    const char* parts[] = {label, value};
    for (unsigned i = 0; i != 2; ++i) {
        const char* source = parts[i];
        while (source && used + 1 < capacity && *source) text[used++] = *source++;
    }
    text[used] = 0;
}

// An item with its own text.
class CNetItem : public CMenuItem
{
public:
	enum { ID = 0x4346 };
	CNetItem(const char *text, int action) : CMenuItem((EPCText)0, ID), mAction(action) { Set(text); }
	void	Set(const char *text) { Widen(mWide, CE_INVITATION_ORIGIN_BYTES + 64, text); }
	virtual WCHAR	*GetText() { return mWide; }
	// An absent row takes no room, is not drawn and cannot take the focus (the list skips rows that are not enabled).
	virtual int		GetHeight() { return mHidden ? 0 : CMenuItem::GetHeight(); }
	virtual BOOL	IsEnabled() { return !mHidden && mEnabled; }
	virtual void	Render(float x, float y, BOOL selected) { if (!mHidden) CMenuItem::Render(x, y, selected); }
	WCHAR	mWide[CE_INVITATION_ORIGIN_BYTES + 64];
	int		mAction;
	bool	mHidden = false;
};

// Prominent top row on the Network Game page opening the Online Lobby Browser (ce_online_ui.cpp).
class COnlineEntryItem : public CNetItem
{
public:
	COnlineEntryItem() : CNetItem("Play Online (Lobbies)", NA_ONLINE_LOBBIES) {}
	virtual void	Render(float x, float y, BOOL selected)
	{
		if (mHidden)
			return;
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		const DWORD col = !mEnabled ? 0x50505050 : (selected ? 0xffffcc00 : 0xff3f687f);
		SIZE size;
		font->GetTextExtent(mWide, &size);
		font->DrawTextWithShadow(x - float(size.cx / 2), y, col, mWide, 0, 0, 1.f, 1.f);
	}
};

// The colour of a row as CMenuItem::Render gives it: white, the game's focus yellow, or the dim grey of a row that cannot be
// chosen.
DWORD	RowColour(BOOL selected, BOOL enabled)
{
	if (!enabled)
		return 0x50505050;
	return selected ? 0xffffcc00 : 0xffffffff;
}

const float	kNetValueRight = 575.f;		// where a value must end on these pages: the right arc at mid-page (house style L7)

// A label and its value on one row, as the game's options rows draw them (CMenuMenuItem::RenderList): the label ends a little
// left of the list's centre with a colon, the value starts a little right of it and is squeezed, then cut, to end by
// kCeValueRight (port/game/ce_menu_fit.h).
class CNetValue : public CNetItem
{
public:
	CNetValue(const char *label, int action) : CNetItem(label, action)
	{
		// The game's WCHAR is 2 bytes (-fshort-wchar) and the C library's wide-string functions take 4: the colon goes on in char.
		char text[64];
		snprintf(text, sizeof text, label[0] ? "%s:" : "%s", label);
		mLabel8 = label; Widen(mLabelWide, 64, text); mValueWide[0] = 0;
	}
	void	SetValue(const char *value) { Widen(mValueWide, CE_INVITATION_ORIGIN_BYTES, value); char t[CE_INVITATION_ORIGIN_BYTES + 64]; snprintf(t, sizeof t, mLabel8[0] ? "%s: %s" : "%s%s", mLabel8, value); Set(t); }
	virtual void	Render(float x, float y, BOOL selected)
	{
		if (mHidden)
			return;
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		const DWORD col = RowColour(selected, mEnabled);
		SIZE label, value;
		if (!mLabel8[0])
		{
			// centred, and no wider than the band between the arcs (L7): a longer value shows its end, where the cursor is
			WCHAR *shown = Fitted(font, mValueWide, 2.f * (kNetValueRight - x), 1.f);
			font->GetTextExtent(shown, &value);
			font->DrawTextWithShadow(x - float(value.cx / 2), y, col, shown, 0, 0, 1.f, 1.f);
			mValueEnd = x - float(value.cx / 2) + float(value.cx);
			return;
		}
		// "Label:value" as the original's options rows (house rule 3): the label ends at the centre, the value starts just
		// right of it and ends by x 575, where the right arc begins at mid-page (L7), squeezed no tighter than 0.75.
		font->GetTextExtent(mLabelWide, &label);
		font->DrawTextWithShadow(x - float(label.cx), y, col, mLabelWide, 0, 0, 1.f, 1.f);
		const float vx = x + 2.f;
		WCHAR *shown = Fitted(font, mValueWide, kNetValueRight - vx, kCeValueMinScale);
		font->GetTextExtent(shown, &value);
		const float scale = PortCeFitScale(vx, float(value.cx), kNetValueRight, kCeValueMinScale);
		font->DrawTextWithShadow(vx, y, col, shown, 0, 0, scale, 1.f);
		mValueEnd = vx + float(value.cx) * scale;
	}
	// The value as it can be drawn in width pixels at the given squeeze or less: whole when it fits, else its last
	// characters (a typed value's end is what the player is looking at), so nothing is ever drawn past the arc (class of
	// review R2 finding 2: a value the field accepts must fit, whatever its length).
	static WCHAR	*Fitted(CDXBitmapFont *font, WCHAR *value, float width, float squeeze)
	{
		SIZE size;
		WCHAR *from = value;
		for (;;)
		{
			font->GetTextExtent(from, &size);
			if (!*from || float(size.cx) * squeeze <= width)
				return from;
			from++;
		}
	}
	const char	*mLabel8;
	WCHAR	mLabelWide[64], mValueWide[CE_INVITATION_ORIGIN_BYTES];
	float	mValueEnd = 0.f;
};

// A text field: a label and the typed value as one CNetValue row; while it is selected, a blinking cursor after the value
// (the game's own: a coloured I, as the name entry page draws it).
class CNetField : public CNetValue
{
public:
	CNetField(const char *label, int action, int max) : CNetValue(label, action), mMax(max) { mValue[0] = 0; Refresh(); }
	void	Refresh()
	{
		if (mAction == NA_CODE)
		{
			// As the host's page prints it: groups of four, from the first letter (dashes typed or pasted do not count).
			char shown[64];
			size_t n = 0, symbols = 0;
			for (const char *c = mValue; *c && n + 2 < sizeof shown; c++)
			{
				if (*c == '-')
					continue;
				if (symbols && symbols % 4 == 0)
					shown[n++] = '-';
				shown[n++] = *c;
				symbols++;
			}
			shown[n] = 0;
			SetValue(shown);
			return;
		}
		SetValue(mValue[0] ? mValue : (mAction == NA_ADDRESS ? "None" : ""));
	}
	virtual void	Render(float x, float y, BOOL selected)
	{
		CNetValue::Render(x, y, selected);
		if (selected && mEnabled && (fmod(PLATFORM.GetSysTimeFloat(), 1.f) < 0.5f))
		{
			CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
			static WCHAR cursor[] = L"I";
			const float cx = mValue[0] ? mValueEnd + 1.f : x + 2.f;
			font->DrawTextWithShadow(cx, y, 0xff00c0ff, cursor, 0, 0, 1.f, 1.f);
		}
	}
	bool	Add(const char *s)
	{
		size_t n = strlen(mValue);
		bool any = false;
		// The Code row takes the code's 23 letters and digits and no more (dashes and spaces do not count): what is typed
		// past them is not taken, so a full code row always fits between the arcs (review R2 finding 2).
		int symbols = 0;
		for (const char *c = mValue; *c; c++)
			symbols += *c != '-';
		for (; *s && n < (size_t)mMax; s++)
		{
			char c = *s;
			if (mAction == NA_CODE && c != '-' && c != ' ' && symbols >= CE_CODE_SYMBOLS)
				continue;
			if (c == '\r' || c == '\n' || c == '\t')
				continue;
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
				continue;
			if (mAction == NA_ADDRESS && !((c >= '0' && c <= '9') || c == '.' || c == ':'))
				continue;
			if (mAction == NA_CODE && c == ' ')
				continue;
			if (mAction == NA_CODE && c >= 'a' && c <= 'z')
				c = char(c - 'a' + 'A');
			symbols += mAction == NA_CODE && c != '-';
			mValue[n++] = c;
			any = true;
		}
		mValue[n] = 0;
		Refresh();
		return any;
	}
	void	Back()
	{
		size_t n = strlen(mValue);
		if (n)
			mValue[n - 1] = 0;
		Refresh();
	}
	void	Clear() { CE_CodeWipe(mValue, sizeof mValue); Refresh(); }

	char mValue[CE_INVITATION_ORIGIN_BYTES];
	int			mMax;
};

// Whether typed text can never become an address the lobby joins (four numbers 0 to 255 with dots, then an optional
// :port from 1 to 65535), however it goes on; an empty or unfinished value ("192.168.", "10.0.0.1:") is not.
bool	AddressImpossible(const char *text)
{
	int parts = 1, digits = 0, value = 0, port = -1;
	for (const char *c = text; *c; c++)
	{
		if (*c >= '0' && *c <= '9')
		{
			value = value * 10 + (*c - '0');
			if (++digits > (port >= 0 ? 5 : 3) || (port < 0 && value > 255) || (port >= 0 && value > 65535))
				return true;
		}
		else if (*c == '.' && port < 0)
		{
			if (!digits || ++parts > 4)
				return true;
			digits = value = 0;
		}
		else if (*c == ':' && port < 0)
		{
			if (!digits || parts != 4)
				return true;
			port = 0;
			digits = value = 0;
		}
		else
			return true;
	}
	return false;
}

// What each row does, drawn under Back while the row has the focus (house style L5: one or two plain sentences).
const char *RowHint(int action)
{
	switch (action)
	{
	case NA_ONLINE_LOBBIES:	return "Browse public lobbies, create a room, or join by code.";
	case NA_HOST:			return "Start a game and get a code for your friend.";
	case NA_JOIN:			return "Join a friend's game with their code.";
	case NA_HELP:			return "What to set up to play over the internet.";
	case NA_IMPORT_OPEN:	return "Opens the invitation folder you were sent.";
	case NA_HOST_ADDRESS:	return "Your friend connects to this address. Changing it makes a new code.";
	case NA_COPY:			return "Copies the code so you can paste it in a message.";
	case NA_CODE:			return "Letters and digits. You can leave out the dashes.";
	case NA_ADDRESS:		return "Only for play over the internet. Your friend tells you this address.";
	case NA_CONNECT:		return "Joins your friend's game.";
	case NA_PASTE_CODE:		return "Puts a copied code in the Code row.";
	case NA_PASTE_ADDRESS:	return "Puts a copied address in the Address row.";
	case NA_CLEAR_FIELDS:	return "Empties the Code and Address rows.";
	case NA_MAP:			return "Select changes the mission.";
	case NA_START:			return "Starts the mission for you and your friend.";
	case NA_LEAVE:			return "Leaves this game.";
	case NA_CAMPAIGN_START:	return "Starts the campaign mission for you and your friend.";
	default:				return NULL;
	}
}

class CCeNetMenu;
CCeNetMenu	*sMenu = NULL;
bool		sActive = false;
bool		sOpenOnEntry = false;		// the return from a network game: open these pages as the CE page arrives
bool		sOpenOnlineOnEntry = false;	// Multiplayer -> Play Online entry: open Online UI directly
bool		sDirectFromOnline = false;	// Direct LAN / IP opened from Online UI: Back returns to Online UI
bool		sCtrl = false;
bool		sTrap = false;

bool	NetKeyTrap(WORD key, KeyEventType event);
// The key trap as the shell stores it: its type returns nothing, but the game reads a bool from it for every mouse button
// (src/mouse.cpp MOUSE_TRAP), as the name entry's VKKeyTrap gives. The cast goes through a plain function type.
pKeyTrapper	NetTrap() { return (pKeyTrapper)(void (*)(void))NetKeyTrap; }

class CCeNetMenu : public CPauseMenu
{
public:
	CCeNetMenu() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		static WCHAR title[] = L"Network Game";
		for (int p = 0; p < NP_COUNT; p++)
			mList[p] = NULL;
		CMenuOptionsList *l;
		// The first page: Host Game, Join Game, Connection Help. The connection is the one this launch was prepared for
		// (direct, or the relay pilot when the game was started with its setup); there is no page to choose it.
		l = mList[NP_MENU] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 330.f, FALSE, 0xb0000000);
#ifndef CE_NETWORK_OFF
		if (PortCeOnlineUiOpen)
			l->AddItem(mOnlineEntry = new (MEMTYPE_MENUITEM) COnlineEntryItem());
		else
			mOnlineEntry = NULL;
#else
		mOnlineEntry = NULL;
#endif
		l->AddItem(mHostItem = new (MEMTYPE_MENUITEM) CNetItem("Host Game", NA_HOST));
		l->AddItem(mJoinItem = new (MEMTYPE_MENUITEM) CNetItem("Join Game", NA_JOIN));
#if defined(CE_NETWORK_ICE)
		l->AddItem(mInvitationEntry = new (MEMTYPE_MENUITEM) CNetItem("Import Online Invitation", NA_IMPORT_OPEN));
#else
		mInvitationEntry = NULL;
#endif
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));

		// The host's page, as NP_HOST, with the address row first: used when this PC has more than one address.
		l = mList[NP_HOST_ADDRESS] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 380.f, FALSE, 0xb0000000);	// 640x480 screen coordinates, as every list here
		l->AddItem(mHostAddress = new (MEMTYPE_MENUITEM) CNetValue("Address", NA_HOST_ADDRESS));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Copy Code", NA_COPY));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_HOST] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 380.f, FALSE, 0xb0000000);
		l->AddItem(mRoomCopy = new (MEMTYPE_MENUITEM) CNetItem("Copy Code", NA_COPY));
		l->AddItem(mRoomBack = new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_JOIN] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 340.f, FALSE, 0xb0000000);
		l->AddItem(mCode = new (MEMTYPE_MENUITEM) CNetField("Code", NA_CODE, 40));
		l->AddItem(mAddress = new (MEMTYPE_MENUITEM) CNetField("Address", NA_ADDRESS, 21));
		l->AddItem(mConnect = new (MEMTYPE_MENUITEM) CNetItem("Connect", NA_CONNECT));
		l->AddItem(mPasteCode = new (MEMTYPE_MENUITEM) CNetItem("Paste Code", NA_PASTE_CODE));
		l->AddItem(mPasteAddress = new (MEMTYPE_MENUITEM) CNetItem("Paste Address", NA_PASTE_ADDRESS));
		l->AddItem(mClearFields = new (MEMTYPE_MENUITEM) CNetItem("Clear", NA_CLEAR_FIELDS));
		l->AddItem(mJoinBack = new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_LOBBY] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 320.f, FALSE, 0xb0000000);
		l->AddItem(mMap = new (MEMTYPE_MENUITEM) CNetValue("Mission", NA_MAP));
		l->AddItem(mStart = new (MEMTYPE_MENUITEM) CNetItem("Start", NA_START));
        l->AddItem(mCampaignStart = new (MEMTYPE_MENUITEM) CNetItem("Start Campaign", NA_CAMPAIGN_START));

		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Leave", NA_LEAVE));
		l = mList[NP_HELP] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 440.f, FALSE, 0xb0000000);
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_INVITATION_INPUT] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 365.f, FALSE, 0xb0000000);
        l->AddItem(mImportAddressLabel = new (MEMTYPE_MENUITEM) CNetItem("Service address", NA_NONE));
        mImportAddressLabel->mEnabled = FALSE;
        mImportOriginRow = l->mItems.Size();
		l->AddItem(mImportOrigin = new (MEMTYPE_MENUITEM) CNetField("", NA_IMPORT_ORIGIN, CE_INVITATION_ORIGIN_BYTES - 1));
		l->AddItem(mImportPaste = new (MEMTYPE_MENUITEM) CNetItem("Paste address", NA_IMPORT_PASTE));
		l->AddItem(mImportHost = new (MEMTYPE_MENUITEM) CNetItem("Choose Host invite", NA_IMPORT_HOST));
		l->AddItem(mImportJoin = new (MEMTYPE_MENUITEM) CNetItem("Choose Join invite", NA_IMPORT_JOIN));
		l->AddItem(mImportProfileHost = new (MEMTYPE_MENUITEM) CNetItem("Load Profile Host Invite", NA_IMPORT_PROFILE_HOST));
		mImportProfileHost->mHidden = true;
		l->AddItem(mImportProfileJoin = new (MEMTYPE_MENUITEM) CNetItem("Load Profile Join Invite", NA_IMPORT_PROFILE_JOIN));
		mImportProfileJoin->mHidden = true;
		l->AddItem(mImportClearInput = new (MEMTYPE_MENUITEM) CNetItem("Clear current pair", NA_IMPORT_CLEAR));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Cancel import", NA_BACK));
		l = mList[NP_INVITATION_PREVIEW] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 400.f, FALSE, 0xb0000000);
		l->AddItem(mImportUse = new (MEMTYPE_MENUITEM) CNetItem("Use invitation", NA_IMPORT_USE));
		l->AddItem(mImportClearPreview = new (MEMTYPE_MENUITEM) CNetItem("Clear current pair", NA_IMPORT_CLEAR));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Choose another folder", NA_BACK));
		l = mList[NP_SIGNAL_DISCLOSURE] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 420.f, FALSE, 0xb0000000);
		l->AddItem(mSignalContinue = new (MEMTYPE_MENUITEM) CNetItem("Continue", NA_SIGNAL_DISCLOSE));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Cancel", NA_BACK));
		l = mList[NP_SIGNAL_STATUS] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 380.f, FALSE, 0xb0000000);
		l->AddItem(mSignalAccept = new (MEMTYPE_MENUITEM) CNetItem("Accept approved pair", NA_SIGNAL_ACCEPT));
		l->AddItem(mSignalProceed = new (MEMTYPE_MENUITEM) CNetItem("Continue", NA_SIGNAL_PROCEED));
		l->AddItem(mSignalRetry = new (MEMTYPE_MENUITEM) CNetItem("Retry", NA_SIGNAL_RETRY));
		l->AddItem(mSignalClose = new (MEMTYPE_MENUITEM) CNetItem("Close pair", NA_SIGNAL_CLOSE));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Cancel invitation", NA_BACK));
		for (int p = 0; p < NP_COUNT; p++)
			mLists.Append(mList[p]);
        mConnectionSelected = mConnectionRelay = false;
        mSignalApproved = mSignalBegan = false;
        mSignalAction = NA_NONE; mSignalReturnPage = NP_MENU; mSignalOrigin[0] = 0;
        memset(&mInvitation, 0, sizeof mInvitation);
        mInvitationRole = CE_NET_HOST;
        snprintf(mImportOrigin->mValue, sizeof mImportOrigin->mValue, "https://");
        mImportOrigin->Refresh();

		mNotice[0] = mOutcome[0] = 0;
		mOutcomeGood = 1;
		mReturningRoom = false;
		mStartingAt = -1.f;
		mQuitPosted = false;
        mHostAddressCount = mHostAddressIndex = 0;
        mCampaignMission = 0;
        mCampaignMissionPending = 0;
        mCampaignMissionPendingKnown = false;
        mCampaignMissionKnown = false;
        mHostAddresses[0][0] = 0;
	}

#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
    void PracticeFields()
    {
        if (FEMESSBOX.BeingDisplayed()) return;
        CNetField *field = NULL;
        unsigned tag = 0;
        if (mPage == NP_INVITATION_INPUT && mImportOrigin->IsEnabled() &&
            (!mImportOrigin->mValue[0] || !strcmp(mImportOrigin->mValue, "https://")) &&
            mInvitation.state != CE_INVITATION_CHOOSING && !SignalingSelected()) {
            field = mImportOrigin; tag = PORT_PRIVATE_UI_ORIGIN;
        } else if (mPage == NP_JOIN && mInvitationRole == CE_NET_GUEST && mSignalApproved &&
            mSignalBegan && mSignalAction == NA_CONNECT && SignalOriginMatches() &&
            SignalingSelected() && SignalingView().status == CeIceSignalStatus::Admitted &&
            !JoinPending() && mCode->IsEnabled() && !mCode->mValue[0]) {
            field = mCode; tag = PORT_PRIVATE_UI_JOIN_CODE;
        }
        if (!field || !PortPrivateUiFieldWaiting(tag)) return;
        char value[CE_INVITATION_ORIGIN_BYTES] = {};
        if (!PortPrivateUiTakeField(tag, value, sizeof(value))) return;
        if (tag == PORT_PRIVATE_UI_ORIGIN) {
            const size_t n = strlen(value);
            if (n > 8 && n <= size_t(field->mMax) && !strncmp(value, "https://", 8) &&
                !strpbrk(value + 8, "/?#@ \t\r\n") && !strchr(value + 8, '\\')) {
                snprintf(field->mValue, sizeof(field->mValue), "%s", value);
                field->Refresh();
                Notice("Local practice address filled. Choose Join invite to review your invitation.");
            }
        } else {
            CE_JoinCode decoded = {};
            if (CE_CodeDecode(value, &decoded) == CE_CODE_OK &&
                CE_CodeEncode(&decoded, field->mValue, sizeof(field->mValue))) {
                field->Refresh();
                Notice("Host code filled. Choose Connect when you are ready.");
            }
            CE_CodeWipe(&decoded, sizeof(decoded));
        }
        CE_CodeWipe(value, sizeof(value));
    }
#endif
	void	Show(int page)
	{
        if (page == NP_MENU) mConnectionSelected = false;
		ClearTransientSignaling();
		mStartingAt = -1.f;
		mQuitPosted = false;
		mPage = page;
        mCampaignMissionKnown = false;
		mList[page]->mSelected = 0;
		mList[page]->Reset();
		if (PlayerPages() && page == NP_HOST)
		{
			mRoomCopy->mHidden = mReturningRoom;
			mRoomBack->Set(mReturningRoom ? "Leave" : "Back");
			if (mReturningRoom) mList[NP_HOST]->mSelected = mList[NP_HOST]->mItems.Size() - 1;
		}
		mNotice[0] = 0;
		mNoticeBoxed = false;
		mPointerX = MOUSE_X;
		mPointerY = MOUSE_Y;
		if (page == NP_JOIN)
			mList[page]->mSelected = mCode->mValue[0] ? 2 : 0;
		if (page == NP_HOST_ADDRESS)
			mList[page]->mSelected = 1;		// Copy Code: the address is already chosen
		Trap(page == NP_JOIN || page == NP_INVITATION_INPUT);
        if (page == NP_INVITATION_INPUT) mList[page]->mSelected = mImportOriginRow;

        RefreshSignalControls();
        RefreshInvitationControls(page == NP_MENU || page == NP_INVITATION_INPUT);
        if (page == NP_SIGNAL_STATUS) {
            mList[page]->mSelected = mSignalAccept->IsEnabled() ? 0 :
                mSignalProceed->IsEnabled() ? 1 : mSignalRetry->IsEnabled() ? 2 :
                mSignalClose->IsEnabled() ? 3 : 4;
        }
}

	// The co-op missions by name (a level number is not a name: house style principle). The second Building Defence map
	// needs a name of the owner's choosing; "Building Defence 2" tells the two apart until then.
	static const char	*MissionName(uint32_t level)
	{
		return level == 856 ? "Building Defence" : level == 858 ? "Building Defence 2" : level == 100 ? "Tutorial" : "Mission";
	}
	static bool	CampaignRowWanted() { const char *v = getenv("BEA_NET_LOBBY_CAMPAIGN"); return v && !strcmp(v, "1"); }

	// The host's page: with the address row when this PC has more than one address.
	void	ShowHost() { Show(mHostAddressCount > 1 && !RelayRequested() ? NP_HOST_ADDRESS : NP_HOST); }
	static bool	HostPage(int page) { return page == NP_HOST || page == NP_HOST_ADDRESS; }

	// The first page with the focus on the row of the action that left it (house style F2).
	void	ShowMenu(int action)
	{
		Show(NP_MENU);
		CMenuOptionsList *list = mList[NP_MENU];
		for (int i = 0; i < list->mItems.Size(); i++)
			if (list->mItems.At(i)->mId == CNetItem::ID && ((CNetItem *)list->mItems.At(i))->mAction == action)
				list->mSelected = i;
	}

	// Hosting on the address shown (the first, a private LAN address when there is one), or with no address in the code
	// when this PC has none to list. Stays on the page it was chosen from, with the lobby's reason, when it cannot.
	bool	HostHere()
	{
		if (mHostAddressCount ? CE_LobbyHostAt(mHostAddresses[mHostAddressIndex]) : CE_LobbyHost())
		{
			ShowHost();
			return true;
		}
		CE_LobbyStatus status;
		CE_LobbyStatusGet(&status);
		RefreshHostAddresses();
		LobbyNotice(status.notice);
		return false;
	}

	void	Trap(bool on)
	{
		if (on && !sTrap)
		{
			LT.SetKeytrap(NetTrap());
			sTrap = true;
		}
		else if (!on && sTrap)
		{
			if (LT.GetKeytrap() == NetTrap())
				LT.SetKeytrap(NULL);
			sTrap = false;
			sCtrl = false;
		}
	}

	void	Notice(const char *text) { snprintf(mNotice, sizeof mNotice, "%s", text ? text : ""); mNoticeBoxed = false; }

	// The pages a player meets on a direct connection (the relay pilot's pages keep their own operator words).
	bool	PlayerPages() const { return !RelayRequested() && !SignalingSelected(); }

	// What the lobby says (a failure, a friend who left, nobody answering): on the player's pages always in the message
	// box, never fitted above a list (port/tools/network_player_menu_tests.py: notice-box cases).
	void	LobbyNotice(const char *text)
	{
		Notice(text);
		if (PlayerPages() && mNotice[0])
		{
			mNoticeBoxed = true;
			Trap(false);
			Box(mNotice);
		}
	}

	// Follows the lobby's own notice on a page that stays up (host and lobby pages): a new one is shown once.
	void	FollowLobbyNotice(const char *notice)
	{
		if (!notice[0])
			mLastLobbyNotice[0] = 0;
		else if (strcmp(notice, mLastLobbyNotice))
		{
			snprintf(mLastLobbyNotice, sizeof mLastLobbyNotice, "%s", notice);
			LobbyNotice(notice);
		}
	}

	// With a key trap set (the Join page types into its fields) the game's own pointer hover is off (MouseInRect) and its
	// clicks ask the trap first (MOUSE_TRAP, which NetKeyTrap answers false). Here a pointer that moved puts the focus on
	// the enabled row under it, and a click on a text row selects that row for typing (the list's own click would choose
	// it, which moves the focus on); a click on any other row is the list's, as on every other page.
	void	PointerOnTrappedPage()
	{
		if (!sTrap)
			return;
		const bool moved = MOUSE_X != mPointerX || MOUSE_Y != mPointerY;
		mPointerX = MOUSE_X;
		mPointerY = MOUSE_Y;
		CMenuOptionsList *list = mList[mPage];
		float height = 0.f;
		for (int i = 0; i < list->mItems.Size(); i++)
			height += float(list->mItems.At(i)->GetHeight());
		float ty = list->mY - height * 0.5f - 15.f;
		for (int i = 0; i < list->mItems.Size(); i++)
		{
			CMenuItem *item = list->mItems.At(i);
			const float h = float(item->GetHeight());
			const bool over = MOUSE_X >= 0 && MOUSE_X < 640 && float(MOUSE_Y) >= ty && float(MOUSE_Y) < ty + h;
			if (over && item->IsEnabled())
			{
				if (moved && list->mSelected != i)
					list->mSelected = i;
				if ((item == mCode || item == mAddress || item == mImportOrigin) && MouseClickedInRect(0.f, ty, 640.f, ty + h))
				{
					list->mSelected = i;
					CFrontEnd::PlaySound(FES_SELECT);
				}
			}
			ty += h;
		}
	}

    bool JoinPending() const
    {
        CE_LobbyStatus status;
        CE_LobbyStatusGet(&status);
        return status.state == CE_LOBBY_JOIN_CONNECTING;
    }

    // Clipboard entry is explicit and replaces a field only after validating
    // the complete bounded text. It never connects or appends a second code.
    void PasteField(CNetField *field)
    {
        if (field == mImportOrigin) {
            if (mPage != NP_INVITATION_INPUT || !field->IsEnabled()) return;
            char text[CE_INVITATION_ORIGIN_BYTES] = {};
            if (!CE_LobbyPaste(text, sizeof text)) {
                CE_CodeWipe(text, sizeof text); Notice("The clipboard could not be read."); return;
            }
            if (strncmp(text, "https://", 8) != 0 || strpbrk(text + 8, "/?#@ \t\r\n") || strchr(text + 8, '\\') ||
                strlen(text) > size_t(field->mMax)) {
                CE_CodeWipe(text, sizeof text);
                Notice("Paste an HTTPS origin with only its host and optional port, without a path or user information."); return;
            }
            field->Clear();
            snprintf(field->mValue, sizeof field->mValue, "%s", text);
            field->Refresh(); CE_CodeWipe(text, sizeof text);
            mList[NP_INVITATION_INPUT]->mSelected = mImportOriginRow;
            Notice("Origin pasted. Review the recipient before importing."); return;
        }

        if (mPage != NP_JOIN || (field != mCode && field != mAddress) || !field->IsEnabled() || JoinPending() ||
            (field == mAddress && RelayRequested())) return;
        char paste[128] = {}, value[64] = {};
        CE_JoinCode code = {};
        const char *problem = NULL;
        if (!CE_LobbyPaste(paste, sizeof paste))
            problem = "Nothing to paste. Copy your friend's code or address first.";
        else if (strlen(paste) == sizeof paste - 1)
            problem = "That text is too long. Copy only the code or address.";
        else if (field == mCode)
        {
            for (char *p = paste; *p; ++p)
                if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
            const int result = CE_CodeDecode(paste, &code);
            if (result != CE_CODE_OK) problem = CE_CodeProblem(result);
            else if (!CE_CodeEncode(&code, value, sizeof value))
                problem = "The code could not be entered. Type it instead.";
        }
        else
        {
            char *first = paste, *end = paste + strlen(paste);
            while (*first == ' ' || *first == '\r' || *first == '\n' || *first == '\t') ++first;
            while (end > first && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n' || end[-1] == '\t')) --end;
            *end = 0;
            if (first == end || strlen(first) > (size_t)field->mMax || strlen(first) >= sizeof value)
                problem = "That is not an address. Copy only the address your friend sent.";
            for (const char *p = first; *p && !problem; ++p)
                if (!((*p >= '0' && *p <= '9') || *p == '.' || *p == ':'))
                    problem = "That is not an address. Copy only the address your friend sent.";
            if (!problem) memcpy(value, first, strlen(first) + 1);
        }
        if (problem) Notice(problem);
        else
        {
            field->Clear();
            snprintf(field->mValue, sizeof field->mValue, "%s", value);
            field->Refresh();
            Notice("");
            mList[NP_JOIN]->mSelected = 2; // Connect still needs the player's confirmation.
        }
        CE_CodeWipe(&code, sizeof code);
        CE_CodeWipe(value, sizeof value);
        CE_CodeWipe(paste, sizeof paste);
    }

    void HostAddressText()
    {
        char text[64];
        if (mHostAddressCount > 1)
            snprintf(text, sizeof(text), "%s (%d of %d)", mHostAddresses[mHostAddressIndex], mHostAddressIndex + 1, mHostAddressCount);
        else
            snprintf(text, sizeof(text), "%s", mHostAddressCount ? mHostAddresses[0] : "None");
        mHostAddress->SetValue(text);
    }

    void RefreshHostAddresses()
    {
        char selected[16] = {};
        if (mHostAddressCount)
            snprintf(selected, sizeof(selected), "%s", mHostAddresses[mHostAddressIndex]);
        mHostAddressCount = CE_LobbyAddresses(mHostAddresses, CE_LOBBY_ADDRESS_CHOICES);
        if (mHostAddressCount < 0) mHostAddressCount = 0;
        if (mHostAddressCount > CE_LOBBY_ADDRESS_CHOICES) mHostAddressCount = CE_LOBBY_ADDRESS_CHOICES;
        mHostAddressIndex = 0;
        for (int i = 0; i < mHostAddressCount; ++i)
            if (!strcmp(selected, mHostAddresses[i])) mHostAddressIndex = i;
        HostAddressText();
    }

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f || mStartingAt >= 0.f || FEMESSBOX.BeingDisplayed())
			return;
		CMenuOptionsList *list = mLists.At(mPage);
		if (list->PassToSelected(from_controller, button, val))
			return;
		switch (button)
		{
		default:
			list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_FRONTEND_MENU_LEFT:
		case BUTTON_FRONTEND_MENU_RIGHT:
			if (mPage == NP_HOST_ADDRESS && list->mItems.At(list->mSelected) == mHostAddress)
				NextHostAddress(button == BUTTON_FRONTEND_MENU_LEFT ? -1 : 1);
			else
				list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
			Choose(list->mItems.At(list->mSelected));
			break;
		case BUTTON_FRONTEND_MENU_BACK:
			Back();
			break;
		}
	}

	void	Back()
	{
		{
			// A start both games have agreed (STARTING) cannot be taken back: the other game is already starting, and the
			// page is closing to start this one. Back, Leave and Esc do nothing in that moment.
			CE_LobbyStatus now;
			CE_LobbyStatusGet(&now);
			if (now.state == CE_LOBBY_STARTING)
				return;
		}
		CFrontEnd::PlaySound(FES_BACK);
		// Cancel while a join is trying: stop, and stay on the Join page with the code kept.
		mReturningRoom = false;
		if (mPage == NP_JOIN && PlayerPages() && JoinPending())
		{
			CE_LobbyLeave();
			ClearTransientSignaling();
			Show(NP_JOIN);
			return;
		}
		switch (mPage)
		{
		case NP_MENU:
			mStartingAt = -1.f;
			mQuitPosted = false;
			Close();
			break;
        case NP_INVITATION_PREVIEW:
#if defined(CE_NETWORK_ICE)
            CE_InvitationCancel(); CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
#endif
            Show(NP_INVITATION_INPUT); break;
        case NP_INVITATION_INPUT:
#if defined(CE_NETWORK_ICE)
            CE_InvitationCancel(); CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
#endif
            Show(NP_MENU); break;
        case NP_SIGNAL_DISCLOSURE:
        case NP_SIGNAL_STATUS:
            {
                const int returnPage = mSignalReturnPage;
                CE_LobbyLeave(); ClearSignalUi(); Show(returnPage);
                Notice("The approved pair is closed. Load a fresh setup to connect again.");
            }
            break;
		case NP_HOST:
		case NP_HOST_ADDRESS:
			CE_LobbyLeave();
			mHostAddressCount = mHostAddressIndex = 0;
			mHostAddresses[0][0] = 0;
			ShowMenu(NA_HOST);
			break;
		case NP_LOBBY:
		{
			CE_LobbyStatus s;
			CE_LobbyStatusGet(&s);
			CE_LobbyLeave();
			ShowMenu(s.host ? NA_HOST : NA_JOIN);
			break;
		}
		case NP_JOIN:
			CE_LobbyLeave();
			ShowMenu(NA_JOIN);
			break;
		case NP_HELP:
			ShowMenu(NA_HELP);
			break;
		}
	}

	void	Close()
	{
#if defined(CE_NETWORK_ICE)
        CE_InvitationCancel();
#endif

        ClearSignalUi();
		Trap(false);
		CE_LobbyLeave();
#if defined(CE_NETWORK_ICE)
        CE_LobbySignalingClear();
#endif
		mCode->Clear();
		mAddress->Clear();
		DeActivate(FALSE);
		sActive = false;
#ifndef CE_NETWORK_OFF
		if (sDirectFromOnline)
		{
			sDirectFromOnline = false;
			if (PortCeOnlineUiOpen)
				PortCeOnlineUiOpen();
		}
#endif
	}

	bool ConnectionAvailable(char *why, size_t size)
    {
        if (!mConnectionSelected)
        {
            snprintf(why, size, "Choose Host Game or Join Game first.");
            return false;
        }
        if (mConnectionRelay != RelayRequested())
        {
            snprintf(why, size, "The connection mode changed. Go back and choose Local or Online again; no new connection was started.");
            return false;
        }
        return MenuAvailable(why, size);
    }

    // Host Game and Join Game use the connection this launch was prepared for: direct, or the relay pilot when the game was
    // started with its setup. relay says which the caller expects; a mismatch is told and nothing is started.
    bool SelectConnection(bool relay)
    {
        if (relay != RelayRequested())
        {
            Notice(relay ? "Online play is not set up for this launch." : "This launch is set up for Online play only.");
            return false;
        }
        char why[192];
        if (!MenuAvailable(why, sizeof why)) { Notice(why); return false; }
        mConnectionSelected = true;
        mConnectionRelay = relay;
        return true;
    }

    // The host's address row: the next or previous address; the lobby hosts again there with a new code.
    void NextHostAddress(int step)
    {
        CE_LobbyStatus s;
        CE_LobbyStatusGet(&s);
        if (!HostPage(mPage) || mHostAddressCount < 2 || s.state != CE_LOBBY_HOST_WAITING) return;
        CFrontEnd::PlaySound(FES_MOVE);
        mHostAddressIndex = (mHostAddressIndex + mHostAddressCount + step) % mHostAddressCount;
        HostAddressText();
        CE_LobbyLeave();
        if (HostHere() && mPage == NP_HOST_ADDRESS)
            mList[NP_HOST_ADDRESS]->mSelected = 0;
        else if (!HostPage(mPage))
            Notice("This address cannot be used now. Choose Host Game again.");
    }

    bool InvitationPreviewValid() const
    {
        const int64_t now = CE_HandoffNow();
        return now > 0 && mInvitation.state == CE_INVITATION_PREVIEW_READY && mInvitation.role == mInvitationRole &&
            mInvitation.expires_at > uint64_t(now) && mInvitation.origin[0] &&
            memchr(mInvitation.origin, 0, sizeof mInvitation.origin) != NULL;
    }
    void RefreshProfileInviteCache(bool force = false)
    {
#if defined(CE_NETWORK_ICE)
        const float nowSys = PLATFORM.GetSysTimeFloat();
        if (!force && mProfileProbeTime >= 0.f && nowSys >= mProfileProbeTime && (nowSys - mProfileProbeTime) < 0.5f)
            return;
        mProfileProbeTime = nowSys;
        const int64_t now = CE_HandoffNow();
        mProfileHostAvailable = now > 0 && CE_InvitationProfileAvailable(CE_NET_HOST, uint64_t(now)) != 0;
        mProfileJoinAvailable = now > 0 && CE_InvitationProfileAvailable(CE_NET_GUEST, uint64_t(now)) != 0;
#else
        (void)force;
        mProfileHostAvailable = false;
        mProfileJoinAvailable = false;
#endif
    }
    void EnsureEnabledFocus(int page, int preferred = -1)
    {
        if (page < 0 || page >= NP_COUNT) return;
        CMenuOptionsList *list = mList[page];
        if (!list || list->mItems.Size() <= 0) return;
        if (list->mSelected >= 0 && list->mSelected < list->mItems.Size() &&
            list->mItems.At(list->mSelected)->IsEnabled())
            return;
        if (preferred >= 0 && preferred < list->mItems.Size() &&
            list->mItems.At(preferred)->IsEnabled())
        {
            list->mSelected = preferred;
            return;
        }
        for (int i = 0; i < list->mItems.Size(); i++)
        {
            if (list->mItems.At(i)->IsEnabled())
            {
                list->mSelected = i;
                return;
            }
        }
    }
    void RefreshInvitationControls(bool forceProfile = false)
    {
#if defined(CE_NETWORK_ICE)
        const bool available = true;
#else
        const bool available = false;
#endif
        const bool busy = mInvitation.state == CE_INVITATION_CHOOSING;
        const bool preview = mInvitation.state == CE_INVITATION_PREVIEW_READY;
        if (mInvitationEntry) mInvitationEntry->mEnabled = available;
        mImportOrigin->mEnabled = mImportPaste->mEnabled = available && !busy && !preview;
        mImportHost->mEnabled = mImportJoin->mEnabled = available && !busy && !preview;
        if (available && !busy && !preview && (mPage == NP_MENU || mPage == NP_INVITATION_INPUT))
            RefreshProfileInviteCache(forceProfile);
        const bool profHost = available && !busy && !preview && mProfileHostAvailable;
        const bool profJoin = available && !busy && !preview && mProfileJoinAvailable;
        mImportProfileHost->mHidden = !profHost;
        mImportProfileHost->mEnabled = profHost;
        mImportProfileJoin->mHidden = !profJoin;
        mImportProfileJoin->mEnabled = profJoin;
        mImportUse->mEnabled = available && InvitationPreviewValid() && !SignalingSelected();
        mImportClearInput->mEnabled = mImportClearPreview->mEnabled = available && !busy && SignalingSelected();
        if (mPage == NP_INVITATION_INPUT)
            EnsureEnabledFocus(NP_INVITATION_INPUT, mImportOriginRow);
        else if (mPage == NP_INVITATION_PREVIEW)
            EnsureEnabledFocus(NP_INVITATION_PREVIEW, 0);
    }
    void PollInvitationUi()
    {
        if (mPage == NP_INVITATION_INPUT || mPage == NP_INVITATION_PREVIEW) {
#if defined(CE_NETWORK_ICE)
            const uint32_t before = mInvitation.state, error = mInvitation.error;
            CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
            if (mInvitation.state == CE_INVITATION_PREVIEW_READY && mPage == NP_INVITATION_INPUT)
                Show(NP_INVITATION_PREVIEW);
            if ((before != mInvitation.state || error != mInvitation.error) && mInvitation.state == CE_INVITATION_REFUSED)
                Notice(CE_InvitationErrorText(mInvitation.error));
#endif
        }
        RefreshInvitationControls();
    }
    void BeginInvitation(uint32_t role)
    {
        if (mPage != NP_INVITATION_INPUT || !mImportOrigin->IsEnabled()) return;
        mInvitationRole = role;
#if defined(CE_NETWORK_ICE)
        CE_InvitationBeginImport(mImportOrigin->mValue, role);
        CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
        if (mInvitation.state == CE_INVITATION_REFUSED) Notice(CE_InvitationErrorText(mInvitation.error));
        else Notice("");
#endif
        RefreshInvitationControls(true);
    }
    void BeginProfileInvitation(uint32_t role)
    {
        if (mPage != NP_INVITATION_INPUT || !mImportOrigin->IsEnabled()) return;
        mInvitationRole = role;
#if defined(CE_NETWORK_ICE)
        CE_InvitationBeginProfileImport(mImportOrigin->mValue, role);
        CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
        if (mInvitation.state == CE_INVITATION_PREVIEW_READY) {
            Show(NP_INVITATION_PREVIEW);
            return;
        }
        if (mInvitation.state == CE_INVITATION_REFUSED) Notice(CE_InvitationErrorText(mInvitation.error));
        else Notice("");
#endif
        RefreshInvitationControls(true);
    }

    bool SignalingSelected() const
    {
#if defined(CE_NETWORK_ICE)
        return CE_LobbySignalingSelected();
#else
        return false;
#endif
    }
    CeIceSignalView SignalingView() const
    {
#if defined(CE_NETWORK_ICE)
        return CE_LobbySignalingView();
#else
        return {};
#endif
    }
    const char* SignalingOrigin() const
    {
#if defined(CE_NETWORK_ICE)
        const char* origin = CE_LobbySignalingOrigin();
        return origin ? origin : "";
#else
        return "";
#endif
    }
    bool SignalOriginMatches() const
    { return mSignalOrigin[0] && strcmp(mSignalOrigin, SignalingOrigin()) == 0; }
    void ClearSignalUi()
    {
        mSignalApproved = mSignalBegan = false;
        mSignalAction = NA_NONE; mSignalOrigin[0] = 0;
    }
    bool RequireSignaling(int action, int returnPage)
    {
        if (!SignalingSelected()) return false;
        if (!RelayRequested()) { Notice("The approved pair requires Online relay selected at startup."); return true; }
        const CeIceSignalView view = SignalingView();
        if (mSignalApproved && SignalOriginMatches() && view.status == CeIceSignalStatus::Admitted) return false;
        mSignalAction = action; mSignalReturnPage = returnPage;
        Show(mSignalApproved || SignalTerminal(view.status) ? NP_SIGNAL_STATUS : NP_SIGNAL_DISCLOSURE);
        return true;
    }
    const char* SignalConnectionDetail() const
    {
        const CeIceSignalStatus status = SignalingView().status;
        return status == CeIceSignalStatus::Admitted ?
            "Preparing the approved connection. Wait for CE to establish and validate the peer connection." : SignalDetail(status);
    }
    void RefreshSignalControls()
    {
        const bool selected = SignalingSelected();
        const CeIceSignalStatus status = SignalingView().status;
        const char* origin = SignalingOrigin();
        const bool validOrigin = origin[0] && strlen(origin) < sizeof mSignalOrigin;
        const bool disclosed = mSignalApproved && SignalOriginMatches();
        mSignalContinue->mEnabled = selected && validOrigin && mConnectionSelected && mConnectionRelay && RelayRequested();
        mSignalAccept->mEnabled = selected && disclosed && mSignalBegan && status == CeIceSignalStatus::NeedsConsent;
        mSignalProceed->mEnabled = selected && disclosed && status == CeIceSignalStatus::Admitted &&
            (mSignalAction == NA_HOST || mSignalAction == NA_CONNECT);
        mSignalRetry->mEnabled = selected && disclosed && SignalRetryable(status);
        if (mPage == NP_SIGNAL_STATUS && (mSignalAction == NA_CONNECT || mSignalAction == NA_HOST) &&
            mSignalProceed->IsEnabled() && !mProceedWasReady)
        {
            CMenuItem *cur = mList[NP_SIGNAL_STATUS]->mItems.At(mList[NP_SIGNAL_STATUS]->mSelected);
            if (cur == mSignalAccept || cur == mSignalClose || !cur->IsEnabled())
                mList[NP_SIGNAL_STATUS]->mSelected = 1;
        }
        mProceedWasReady = mSignalProceed->IsEnabled();
        mSignalClose->mEnabled = selected && status != CeIceSignalStatus::Closed;
        mSignalProceed->Set(mSignalAction == NA_HOST ? "Prepare Host" :
            (mCode->mValue[0] ? "Join match" : "Enter host code"));
        if (mPage == NP_SIGNAL_STATUS)
            EnsureEnabledFocus(NP_SIGNAL_STATUS, mSignalProceed->IsEnabled() ? 1 : -1);
        else if (mPage == NP_SIGNAL_DISCLOSURE)
            EnsureEnabledFocus(NP_SIGNAL_DISCLOSURE, 0);
    }
    void PumpSignalUi()
    {
        if (mPage == NP_SIGNAL_STATUS && mSignalBegan) {
            if (!SignalingSelected() || !SignalOriginMatches()) {
                mSignalApproved = mSignalBegan = false; mSignalOrigin[0] = 0;
                Show(NP_SIGNAL_DISCLOSURE);
                Notice("The approved setup changed. Review the network disclosure again.");
            } else {
#if defined(CE_NETWORK_ICE)
                CE_LobbySignalingPump();
#endif
            }
        }
        RefreshSignalControls();
    }

    void	Choose(CMenuItem *item)
	{
		if ((item->mId != CNetItem::ID) || !item->IsEnabled())
			return;
		int action = ((CNetItem *)item)->mAction;
		switch (action)
		{
		case NA_ONLINE_LOBBIES:
			if (mPage != NP_MENU) break;
#ifndef CE_NETWORK_OFF
			if (PortCeOnlineUiOpen && PortCeOnlineUiOffered && PortCeOnlineUiOffered())
			{
				CFrontEnd::PlaySound(FES_SELECT);
				// Close owns Direct LAN cleanup. Suppress its automatic return so this opens Online once.
				sDirectFromOnline = false;
				Close();
				PortCeOnlineUiOpen();
				return;
			}
#endif
			Notice("Online lobbies are unavailable in this build.");
			break;
		case NA_IMPORT_OPEN:
            if (mPage != NP_MENU) break;
#if defined(CE_NETWORK_ICE)
            CE_InvitationCancel(); CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
#endif
            Show(NP_INVITATION_INPUT); break;
        case NA_IMPORT_ORIGIN:
            if (mPage == NP_INVITATION_INPUT && mImportOrigin->IsEnabled()) mList[NP_INVITATION_INPUT]->mSelected = mImportOriginRow;
            break;
        case NA_IMPORT_PASTE:
            PasteField(mImportOrigin); break;
        case NA_IMPORT_HOST:
            BeginInvitation(CE_NET_HOST); break;
        case NA_IMPORT_JOIN:
            BeginInvitation(CE_NET_GUEST); break;
        case NA_IMPORT_PROFILE_HOST:
            BeginProfileInvitation(CE_NET_HOST); break;
        case NA_IMPORT_PROFILE_JOIN:
            BeginProfileInvitation(CE_NET_GUEST); break;
        case NA_IMPORT_USE:
            if (mPage != NP_INVITATION_PREVIEW || SignalingSelected()) break;
#if defined(CE_NETWORK_ICE)
            CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
            if (!InvitationPreviewValid()) { Notice("The invitation preview expired or changed. Choose the folder again."); break; }
            if (!CE_InvitationInstall(CE_HandoffNow())) {
                CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
                Notice(CE_InvitationErrorText(mInvitation.error)); RefreshInvitationControls(); break;
            }
            CE_InvitationPoll(CE_HandoffNow(), &mInvitation);
            if (mInvitation.state != CE_INVITATION_SELECTED || !SignalingSelected()) {
                Notice("The approved invitation was not selected. Check the setup before continuing."); break;
            }
            ClearSignalUi(); Show(NP_MENU);
            if (SelectConnection(true)) {
                if (mInvitationRole == CE_NET_HOST) RequireSignaling(NA_HOST, NP_MENU);
                else { Show(NP_JOIN); Notice("Join invitation selected. Choose Connect to review and accept. The host sends a code after you both accept."); }
            }
#endif
            break;
        case NA_IMPORT_CLEAR:
            if ((mPage != NP_INVITATION_INPUT && mPage != NP_INVITATION_PREVIEW) ||
                mInvitation.state == CE_INVITATION_CHOOSING || !SignalingSelected()) break;
#if defined(CE_NETWORK_ICE)
            CE_LobbySignalingClear();
#endif
            ClearSignalUi(); RefreshInvitationControls();
            Notice(SignalingSelected() ? "The previous connection is still closing. Clear again after it stops." :
                "The selected pair was cleared. Use invitation still requires explicit confirmation.");
            break;
        case NA_SIGNAL_DISCLOSE:
            if (mPage != NP_SIGNAL_DISCLOSURE || !SignalingSelected()) break;
            {
                char why[256]; const char* origin = SignalingOrigin();
                if (!ConnectionAvailable(why, sizeof why)) { Notice(why); break; }
                if (!origin[0] || strlen(origin) >= sizeof mSignalOrigin) {
                    Notice("The approved HTTPS signaling origin is unavailable. Load a valid approved setup."); break;
                }
                snprintf(mSignalOrigin, sizeof mSignalOrigin, "%s", origin);
                mSignalApproved = true;
#if defined(CE_NETWORK_ICE)
                mSignalBegan = CE_LobbySignalingBeginPreflight();
#endif
                Show(NP_SIGNAL_STATUS);
                if (!mSignalBegan) Notice("Preflight could not begin. Check the approved setup before retrying.");
            }
            break;
        case NA_SIGNAL_ACCEPT:
            if (mPage != NP_SIGNAL_STATUS || !SignalingSelected() || !mSignalApproved ||
                !mSignalBegan || !SignalOriginMatches() || SignalingView().status != CeIceSignalStatus::NeedsConsent) break;
#if defined(CE_NETWORK_ICE)
            Notice(CE_LobbySignalingAccept() ? "Pair consent submitted. Wait for the other player." :
                "Pair consent could not be submitted. Check the approved setup.");
#endif
            RefreshSignalControls(); break;
        case NA_SIGNAL_PROCEED:
            if (mPage != NP_SIGNAL_STATUS || !SignalingSelected() || !mSignalApproved ||
                !SignalOriginMatches() || SignalingView().status != CeIceSignalStatus::Admitted) break;
            {
                const int action = mSignalAction;
                if (action != NA_HOST && action != NA_CONNECT) break;
                Show(action == NA_HOST ? NP_MENU : NP_JOIN);
                if (action == NA_CONNECT && !mCode->mValue[0]) {
                    Notice("Invitation accepted. Enter the host's code when it arrives, then choose Connect.");
                } else {
                    Choose(action == NA_HOST ? mHostItem : mConnect);
                }
            }
            break;
        case NA_SIGNAL_RETRY:
            if (mPage != NP_SIGNAL_STATUS || !SignalingSelected() || !mSignalApproved ||
                !SignalOriginMatches() || !SignalRetryable(SignalingView().status)) break;
#if defined(CE_NETWORK_ICE)
            if (CE_LobbySignalingRetry()) {
                mSignalBegan = true; Notice("Retry requested with the original setup deadline.");
            } else Notice("This fixed setup cannot be retried. Return and prepare a fresh approved pair.");
#endif
            RefreshSignalControls(); break;
        case NA_SIGNAL_CLOSE:
            if (mPage != NP_SIGNAL_STATUS || !SignalingSelected()) break;
            CE_LobbyLeave(); ClearSignalUi(); Show(NP_SIGNAL_STATUS);
            Notice("The approved pair is closed. Back returns without switching transport.");
            break;
            case NA_BACK:
			Back();
			return;
        case NA_HOST:
            if (mPage != NP_MENU) break;
        {
            char why[200];
            if (!SelectConnection(RelayRequested())) break;
            if (!ConnectionAvailable(why, sizeof why)) { Notice(why); break; }
            if (RequireSignaling(NA_HOST, NP_MENU)) break;
            mOutcome[0] = 0;
			mReturningRoom = false;
            if (RelayRequested())
            {
                if (CE_LobbyHost()) Show(NP_HOST);
                else
                {
                    CE_LobbyStatus status;
                    CE_LobbyStatusGet(&status);
                    Notice(status.notice);
                }
                break;
            }
            // One press: host at once on the first address (a private LAN address when there is one); a PC with several
            // gets the address as a row of the host's page.
            RefreshHostAddresses();
            HostHere();
            break;
        }
        case NA_HOST_ADDRESS:
            NextHostAddress(1);
            return;
            case NA_JOIN:
                if (mPage != NP_MENU) break;
		{
			char why[200];
			if (!SelectConnection(RelayRequested())) break;
			if (!ConnectionAvailable(why, sizeof why)) { Notice(why); break; }
			mOutcome[0] = 0;
			mReturningRoom = false;
			Show(NP_JOIN);
			break;
		}
		case NA_HELP:
			Show(NP_HELP);
			break;
		case NA_COPY:
		{
			CE_LobbyStatus s;
			CE_LobbyStatusGet(&s);
			Notice(s.code[0] && CE_LobbyCopy(s.code) ? "Code copied." : "The code could not be copied. Read it out instead.");
			break;
		}
		case NA_CODE:
			mList[NP_JOIN]->mSelected = mAddress->IsEnabled() ? 1 : (mConnect->IsEnabled() ? 2 : mList[NP_JOIN]->mItems.Size() - 1);
			break;
		case NA_ADDRESS:
			mList[NP_JOIN]->mSelected = mConnect->IsEnabled() ? 2 : mList[NP_JOIN]->mItems.Size() - 1;
			break;
		case NA_PASTE_CODE:
			PasteField(mCode);
			break;
		case NA_PASTE_ADDRESS:
			PasteField(mAddress);
			break;
		case NA_CLEAR_FIELDS:
			if (mPage != NP_JOIN || JoinPending()) return;
			mCode->Clear();
			mAddress->Clear();
			Notice("");
			mList[NP_JOIN]->mSelected = 0;
			break;
		case NA_CONNECT:
                if (mPage != NP_JOIN) break;
		{
			if (mPage != NP_JOIN || JoinPending()) return;
			char why[256];
			if (!ConnectionAvailable(why, sizeof(why))) { Notice(why); break; }
			// The host publishes its code after both invited players consent. An empty field may open
			// consent, but it never bypasses the join-code check below.
			if (!mCode->mValue[0] && SignalingSelected() && RequireSignaling(NA_CONNECT, NP_JOIN)) break;
			// A code that is not complete is said once, on its own line under the introduction (CodeLine), not again as
			// a notice; Connect makes that line say what to do. No sound: the error is a sentence (house style S1).
			if (CE_CodeDecode(mCode->mValue, NULL) != CE_CODE_OK)
			{
				snprintf(mTriedCode, sizeof mTriedCode, "%s", mCode->mValue);
				mTried = true;
				Notice("");
				return;
			}
			const char *problem = NULL;
			if (RequireSignaling(NA_CONNECT, NP_JOIN)) break;
			if (CE_LobbyJoin(mCode->mValue, RelayRequested() ? "" : mAddress->mValue, &problem))
				Notice("");
			else
				Notice(problem);
			break;
		}
		case NA_MAP:
			if (!CE_LobbyNextLevel())
				return;
			break;
		case NA_START:
			if (!CE_LobbyStart())
				Notice("The game could not be started yet: wait until both players are shown.");
			break;
        case NA_CAMPAIGN_START:
        {
            if (mPage != NP_LOBBY || !mCampaignMissionKnown) return;
            const CE_LobbyCampaignStartResult result = CE_LobbyCampaignStart(mCampaignMission);
            switch (result)
            {
            case CE_LOBBY_CAMPAIGN_START_QUEUED:
                Notice("Campaign start requested. Waiting for the other player.");
                break;
            case CE_LOBBY_CAMPAIGN_START_UNAVAILABLE:
                Notice("Campaign co-op is unavailable for this build or mission.");
                break;
            case CE_LOBBY_CAMPAIGN_START_NOT_HOST:
                Notice("Only the host can start the campaign.");
                break;
            case CE_LOBBY_CAMPAIGN_START_NOT_READY:
                Notice("Wait until both players are ready, then confirm again.");
                break;
            case CE_LOBBY_CAMPAIGN_START_STALE_SELECTION:
                Notice("The selected mission changed. Check it, then confirm again.");
                break;
            case CE_LOBBY_CAMPAIGN_START_FAILED:
            default:
                Notice("The campaign could not start. You can retry or leave the lobby.");
                break;
            }
            break;
        }
		case NA_LEAVE:
			Back();
			return;
		default:
			return;
		}
		CFrontEnd::PlaySound(FES_SELECT);
	}

	// Follows the lobby: pages change as it connects, starts, fails or the other player leaves.
	void	Update()
	{
        PollInvitationUi();
        PumpSignalUi();
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
        PracticeFields();
#endif
		CE_LobbyUpdate();
		CE_LobbyStatus s;
		CE_LobbyStatusGet(&s);
		if (mReturningRoom && (s.state == CE_LOBBY_OFF || s.state == CE_LOBBY_FAILED ||
		    s.state == CE_LOBBY_LEFT || s.state == CE_LOBBY_REFUSED ||
		    (mPage == NP_LOBBY && s.state != CE_LOBBY_READY && s.state != CE_LOBBY_STARTING)))
			mReturningRoom = false;
		if (s.state == CE_LOBBY_STARTING)
		{
			if (mPage != NP_LOBBY)
				Show(NP_LOBBY);
			if (mStartingAt < 0.f)
				mStartingAt = PLATFORM.GetSysTimeFloat();
			// "Starting..." shows for a moment, then the game quits as the window's close button would; the exit starts it
			// again (ce_netlobby.cpp).
			if (!mQuitPosted && PLATFORM.GetSysTimeFloat() - mStartingAt > 0.35f)
			{
				mQuitPosted = true;
				Trap(false);
				PostMessage(LT.GetHWnd(), WM_CLOSE, 0, 0);
			}
			return;
		}
		switch (mPage)
		{
		case NP_HOST:
		case NP_HOST_ADDRESS:
			if (s.state == CE_LOBBY_READY)
				Show(NP_LOBBY);
			else if (s.state == CE_LOBBY_OFF || s.state == CE_LOBBY_FAILED)
			{
				if (s.state == CE_LOBBY_FAILED) { CE_LobbyLeave(); ClearTransientSignaling(); }
				ShowMenu(NA_HOST);
				LobbyNotice(s.notice);
			}
			else
				mLastLobbyNotice[0] = 0;	// while the host waits, what the lobby says is a line under the code (DrawText)
			break;
		case NP_JOIN:
			if (s.state == CE_LOBBY_READY || s.state == CE_LOBBY_REFUSED)
			{
				Show(NP_LOBBY);
				if (s.state == CE_LOBBY_REFUSED && PlayerPages())
				{
					// Both versions in the player's words when they differ; one download label for two builds otherwise.
					char refused[sizeof s.local_version + sizeof s.peer_version + 160];
					if (s.peer_version[0] && strcmp(s.peer_version, "unknown") && strcmp(s.peer_version, s.local_version))
						snprintf(refused, sizeof refused, "You have Onslaught CE %s and your friend has %s. You both need the same version. "
						         "Update, then join again.", s.local_version, s.peer_version);
					else
						snprintf(refused, sizeof refused, "You and your friend have different Onslaught CE downloads. You both need the same "
						         "one. Update, then join again.");
					LobbyNotice(refused);
				}
			}
			else if (s.state == CE_LOBBY_FAILED || s.state == CE_LOBBY_LEFT)
			{
				LobbyNotice(s.notice);
				CE_LobbyLeave();
				ClearTransientSignaling();
			}
			break;
		case NP_LOBBY:
			if (s.host && s.state == CE_LOBBY_FAILED)
			{
			    CE_LobbyLeave();
			    ClearTransientSignaling();
			    ShowMenu(NA_HOST);
			    LobbyNotice(s.notice);
			}
			else if (s.host && (s.state == CE_LOBBY_HOST_WAITING || s.state == CE_LOBBY_HOST_CONNECTING))
				ShowHost();		// the other player left: wait for another, with the same code
			else if (!s.host && (s.state == CE_LOBBY_LEFT || s.state == CE_LOBBY_FAILED))
			{
				CE_LobbyLeave();
				ClearTransientSignaling();
				Show(NP_JOIN);
				LobbyNotice(s.notice);
			}
			else if (s.state == CE_LOBBY_OFF)
				Show(NP_MENU);
			else
				FollowLobbyNotice(s.notice);
			break;
		}
		// The key trap types into the Join page's fields; it is off while a message box is up, so the box takes Enter.
		Trap((mPage == NP_JOIN || mPage == NP_INVITATION_INPUT) && !FEMESSBOX.BeingDisplayed());
		bool ready = s.state == CE_LOBBY_READY;
		mStart->mEnabled = (ready && s.host) ? TRUE : FALSE;
        mCampaignStart->mEnabled = s.host && s.state == CE_LOBBY_READY;
        mCampaignMissionPendingKnown = mPage == NP_LOBBY;
        if (mCampaignMissionPendingKnown) mCampaignMissionPending = s.level;

		// The host chooses the mission among the co-op levels; the guest sees the host's choice as a line, not a row, and
		// has no Start row (guide 3.6: an action a player cannot take is absent). Campaign co-op is not proven, so its row
		// is absent unless a tester asks for it (BEA_NET_LOBBY_CAMPAIGN=1).
		mStart->Set("Start");
		mMap->SetValue(MissionName(s.level));
		mMap->mEnabled = (s.host && s.state != CE_LOBBY_REFUSED) ? TRUE : FALSE;
		mMap->mHidden = PlayerPages() && !s.host;
		mStart->mHidden = PlayerPages() && !s.host;
		mCampaignStart->mHidden = PlayerPages() && !CampaignRowWanted();
		if (mPage == NP_LOBBY && !mList[NP_LOBBY]->mItems.At(mList[NP_LOBBY]->mSelected)->IsEnabled())
			mList[NP_LOBBY]->mSelected = s.host && ready ? 1 : (mList[NP_LOBBY]->mItems.Size() - 1);
		mHostItem->mEnabled = mJoinItem->mEnabled = TRUE;		// told why on choosing, when the lobby cannot start
		bool connecting = s.state == CE_LOBBY_JOIN_CONNECTING;
		// While a join is trying, the page holds the code (dark) and Cancel only (guide F5: one disabled row at most), with
		// the focus on Cancel; when it stops, the focus goes back to Connect.
		const bool trying = connecting && PlayerPages();
		mAddress->mHidden = mConnect->mHidden = mPasteCode->mHidden = mPasteAddress->mHidden = mClearFields->mHidden = trying;
		mCode->mHidden = trying && mReturningRoom;
		mJoinBack->Set(trying ? "Cancel" : "Back");
		if (mPage == NP_JOIN && trying != mTrying)
			mList[NP_JOIN]->mSelected = trying ? mList[NP_JOIN]->mItems.Size() - 1 : (mCode->mValue[0] ? 2 : 0);
		mTrying = trying;
		mConnect->Set(connecting && !PlayerPages() ? "Connecting..." : "Connect");
		mConnect->mEnabled = connecting ? FALSE : TRUE;
		mCode->mEnabled = mPasteCode->mEnabled = mClearFields->mEnabled = connecting ? FALSE : TRUE;
		mAddress->mEnabled = mPasteAddress->mEnabled = !connecting && !RelayRequested();
		if (RelayRequested()) mAddress->SetValue("Not Used");
		else mAddress->Refresh();
		if (mPage == NP_JOIN)
			EnsureEnabledFocus(NP_JOIN, connecting ? mList[NP_JOIN]->mItems.Size() - 1 : (mConnect->IsEnabled() ? 2 : 0));
	}

	// The line under the Join page's introduction: how much of the code is typed while it is short (grey), that it is
	// complete (green), or what is wrong with it; after Connect with a code that is not complete, what to do instead.
	const char	*CodeLine(DWORD *colour)
	{
		static char text[96];
		const int r = CE_CodeDecode(mCode->mValue, NULL);
		const bool tried = mTried && !strcmp(mTriedCode, mCode->mValue);
		*colour = kWhite;
		switch (r)
		{
		case CE_CODE_OK:
			*colour = kGood;
			return "The code is complete.";
		case CE_CODE_EMPTY:
			return tried ? "Type your friend's code first." : "";
		case CE_CODE_SHORT:
		{
			if (tried)
			{
				snprintf(text, sizeof text, "The code is not complete. It has %d letters and digits.", CE_CODE_SYMBOLS);
				return text;
			}
			int n = 0;
			for (const char *c = mCode->mValue; *c; c++)
				n += (*c != '-' && *c != ' ');
			snprintf(text, sizeof text, "%d of %d.", n, CE_CODE_SYMBOLS);
			*colour = kGrey;
			return text;
		}
		case CE_CODE_LONG:
			snprintf(text, sizeof text, "The code is too long. It has %d letters and digits.", CE_CODE_SYMBOLS);
			return text;
		case CE_CODE_BAD_CHAR:
			return "Codes use only digits and the letters A to Z, except U.";
		case CE_CODE_CHECK:
			return "The code has a typo. Check it with your friend.";
		default:
			return "This is not an Onslaught CE code. Check it with your friend.";
		}
	}

	// The page's words above its list. A note of the page's own is always drawn (it is one line); what the lobby says is in
	// the message box (LobbyNotice); only the page's standing sentences give way when there is no room for them.
	void	DrawText()
	{
		CE_LobbyStatus s;
		CE_LobbyStatusGet(&s);
		char t[400];
		float y = 124.f;
		switch (mPage)
		{
		case NP_MENU:
            if (mInvitationEntry && !RelayRequested()) {
#if defined(CE_NETWORK_ICE) && !defined(_WIN32)
                if (PortPrivateUiFieldWaiting(PORT_PRIVATE_UI_ORIGIN)) {
                    y = Lines("Local practice: you fly with an automated host.", y, kWhite);
                    y = Lines("Choose Import Online Invitation, then Choose Join invite. You decide whether to accept.", y, kGrey);
                    break;
                }
#endif
                const float limit = ListTop(mList[NP_MENU]) - 6.f;
                y = Lines("Online Co-op", y, kWhite, FONT_SMALL, kPageWidth);
                y = Lines("Direct LAN ready, or Relay: Waiting for pair invitation.", y + 4.f, kCream, FONT_SMALL, kPageWidth);
                if (mNotice[0] && !mNoticeBoxed && y + 4.f + Height(mNotice, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(mNotice, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
                const bool hasProfile = mProfileHostAvailable || mProfileJoinAvailable;
                const char *summary = hasProfile
                    ? "Private profile invitation found. Open Import Online Invitation to review it."
                    : "Choose Host or Join for Direct LAN, or Import Online Invitation for private Cloudflare TURN relay.";
                if (y + 4.f + Height(summary, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(summary, y + 4.f, hasProfile ? kGood : kGrey, FONT_SMALL, kPageWidth);
#if !defined(CE_NETWORK_SIGNALING)
                if (y + 4.f + Height("Public lobby browsing is unavailable.", FONT_SMALL, kPageWidth) <= limit)
                    Lines("Public lobby browsing is unavailable.", y + 4.f, kWarn, FONT_SMALL, kPageWidth);
#endif
                break;
            }

            if (SignalingSelected() && RelayRequested()) {
                const float limit = ListTop(mList[NP_MENU]) - 6.f;
                y = Lines("Online Co-op", y, kWhite, FONT_SMALL, kPageWidth);
                y = Lines(SignalRouteStage(SignalingView().status, s.state, true, true), y + 4.f, kGood, FONT_SMALL, kPageWidth);
                const char *onlineSummary = "Host Game or Join Game opens network disclosure and pair consent before connecting.";
                if (y + 4.f + Height(onlineSummary, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(onlineSummary, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
                if (mNotice[0] && y + 4.f + Height(mNotice, FONT_SMALL, kPageWidth) <= limit)
                    Lines(mNotice, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
                break;
            }

		{
			// What happened in the last game, a note, or why the lobby cannot start comes first; the page's own two sentences
			// follow when they still fit above the list.
			char why[200];
			const bool available = MenuAvailable(why, sizeof why);
			const float top = ListTop(mList[NP_MENU]) - 6.f;
			if (mNotice[0] && !mNoticeBoxed)
				y = Lines(mNotice, y, kWhite, FONT_SMALL, kPageWidth) + 10.f;
			if (!available)
				y = Lines(why, y, kWhite, FONT_SMALL, kPageWidth) + 10.f;
			const char *about = RelayRequested() ? "This launch is set up for the two-player relay pilot. Each of you needs the same version of Onslaught CE."
												 : "Play a mission with a friend on another PC. You both need the same version of Onslaught CE.";
			const char *careers = "A network game does not change your careers.";
			if (y + Height(about, FONT_SMALL, kPageWidth) <= top)
				y = Lines(about, y, kCream, FONT_SMALL, kPageWidth) + 6.f;
			if (y + Height(careers, FONT_SMALL, kPageWidth) <= top)
				Lines(careers, y, kGrey, FONT_SMALL, kPageWidth);
			break;
		}
        case NP_INVITATION_INPUT:
        {
            const float limit = ListTop(mList[NP_INVITATION_INPUT]) - 6.f;
            y = Lines("Online Invitation", y, kWhite, FONT_SMALL, kPageWidth);
            y = Lines("Relay: Waiting for pair invitation", y + 4.f, kCream, FONT_SMALL, kPageWidth);
            if (mNotice[0]) {
                y = Lines(mNotice, y + 6.f, kWarn, FONT_SMALL, kPageWidth);
            } else {
                const char *guide = "Enter the HTTPS service address, then choose Host or Join to review your private invitation.";
                if (y + 4.f + Height(guide, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(guide, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
            }
            const bool hasProfile = mProfileHostAvailable || mProfileJoinAvailable;
            const char *detail = (mInvitation.state == CE_INVITATION_CHOOSING)
                ? "Waiting for folder selection. Cancel import stops this attempt."
                : (hasProfile
                    ? "Private profile invitation ready below. Import stays offline."
                    : "Import stays offline. Continue later contacts the service.");
            const DWORD detailColour = (mInvitation.state == CE_INVITATION_CHOOSING) ? kWarn
                : (hasProfile ? kGood : kGrey);
            if (y + 4.f + Height(detail, FONT_SMALL, kPageWidth) <= limit)
                Lines(detail, y + 4.f, detailColour, FONT_SMALL, kPageWidth);
            break;
        }
        case NP_INVITATION_PREVIEW:
        {
            const float limit = ListTop(mList[NP_INVITATION_PREVIEW]) - 6.f;
            if (y + Height("Review invitation metadata", FONT_SMALL, kPageWidth) <= limit)
                y = Lines("Review invitation metadata", y, kWhite, FONT_SMALL, kPageWidth);
            if (InvitationPreviewValid()) {
                if (y + 4.f + Height("Relay: Waiting for pair invitation", FONT_SMALL, kPageWidth) <= limit)
                    y = Lines("Relay: Waiting for pair invitation", y + 4.f, kCream, FONT_SMALL, kPageWidth);
                const char *roleText = mInvitation.role == CE_NET_HOST ? "Role: Host" : "Role: Join";
                if (y + 4.f + Height(roleText, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(roleText, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
                const char *originText = DisplayServiceOrigin(mInvitation.origin);
                if (originText[0] && y + 2.f + Height(originText, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(originText, y + 2.f, kWarn, FONT_SMALL, kPageWidth);
                const uint64_t now = CE_HandoffNow();
                const uint64_t left = mInvitation.expires_at > now ? mInvitation.expires_at - now : 0;
                char expiry[128];
                snprintf(expiry, sizeof expiry, "Pair approval expires in %llu min.",
                    (unsigned long long)(left / 60 + (left % 60 != 0)));
                if (y + 4.f + Height(expiry, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(expiry, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
                const char *standing = "Use invitation imports this role's private admission. The next disclosure Continue is the first service contact.";
                if (mNotice[0]) {
                    if (y + 4.f + Height(mNotice, FONT_SMALL, kPageWidth) <= limit)
                        y = Lines(mNotice, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
                    if (y + 4.f + Height(standing, FONT_SMALL, kPageWidth) <= limit)
                        Lines(standing, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
                } else if (y + 4.f + Height(standing, FONT_SMALL, kPageWidth) <= limit) {
                    Lines(standing, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
                }
            } else {
                const char *expired = "This preview is unavailable or expired. Export a fresh pair invitation and choose its folder again.";
                if (y + 4.f + Height(expired, FONT_SMALL, kPageWidth) <= limit)
                    y = Lines(expired, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
                if (mNotice[0] && y + 4.f + Height(mNotice, FONT_SMALL, kPageWidth) <= limit)
                    Lines(mNotice, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
            }
            break;
        }
        case NP_SIGNAL_DISCLOSURE:
        {
            const float limit = ListTop(mList[NP_SIGNAL_DISCLOSURE]) - 6.f;
            const char* origin = DisplayServiceOrigin(SignalingOrigin());
            const char* destination = origin[0] ? origin : "Approved HTTPS origin is unavailable.";
            const char* head = "Review Online network access";
            const char* body = "Online contacts the approved HTTPS signaling service below to bind this private pair and exchange protected connection records. Accepted connections use Cloudflare TURN relay access.";
            const char* steps = "Continue contacts this service. Accept on the next page permits relay setup. Cancel closes the selected pair.";
            const float noticeH = mNotice[0] ? Height(mNotice) : 0.f;
            if (y + Height(head) + Height(body) + Height(destination) + Height(steps) + noticeH > limit)
                body = "Continue contacts this HTTPS signaling service. Accept next permits this approved pair's Cloudflare TURN relay setup.";
            if (y + Height(head) <= limit)
                y = Lines(head, y, kWhite);
            if (y + Height(body) <= limit)
                y = Lines(body, y, kGrey);
            if (y + Height(destination) <= limit)
                y = Lines(destination, y, kWarn);
            if (mNotice[0]) {
                if (y + Height(mNotice) <= limit)
                    y = Lines(mNotice, y, kWarn);
                if (y + Height(steps) <= limit)
                    Lines(steps, y, kGrey);
            } else if (y + Height(steps) <= limit) {
                Lines(steps, y, kGrey);
            }
            break;
        }
        case NP_SIGNAL_STATUS:
        {
            const CeIceSignalView view = SignalingView();
            const float top = ListTop(mList[NP_SIGNAL_STATUS]) - 6.f;
            y = Lines(SignalHeading(view.status), y, kWhite, FONT_SMALL, kPageWidth);
            const char* stage = SignalRouteStage(view.status, s.state, true, true);
            const DWORD stageColour = view.status == CeIceSignalStatus::Admitted ? kGood : kWarn;
            y = Lines(stage, y + 4.f, stageColour, FONT_SMALL, kPageWidth);
            const char* detail = SignalDetail(view.status);
            if (y + 4.f + Height(detail, FONT_SMALL, kPageWidth) <= top)
                y = Lines(detail, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
            if (view.pairExpiresAt) {
                const uint64_t now = CE_HandoffNow();
                const uint64_t pairLeft = view.pairExpiresAt > now ? view.pairExpiresAt - now : 0;
                const uint64_t setupLeft = view.setupExpiresAt > now ? view.setupExpiresAt - now : 0;
                char expiry[160];
                snprintf(expiry, sizeof expiry, "Pair approval: %llu min remaining. Setup window: %llu sec remaining.",
                    (unsigned long long)(pairLeft / 60 + (pairLeft % 60 != 0)), (unsigned long long)setupLeft);
                if (y + 4.f + Height(expiry, FONT_SMALL, kPageWidth) <= top)
                    y = Lines(expiry, y + 4.f, kGrey, FONT_SMALL, kPageWidth);
            }
            if (mNotice[0] && y + 4.f + Height(mNotice, FONT_SMALL, kPageWidth) <= top)
                Lines(mNotice, y + 4.f, kWarn, FONT_SMALL, kPageWidth);
            break;
        }
		case NP_HOST:
		case NP_HOST_ADDRESS:
		{
			if (PlayerPages() && mReturningRoom && s.host &&
			    (s.state == CE_LOBBY_HOST_WAITING || s.state == CE_LOBBY_HOST_CONNECTING))
			{
				y = Lines("Match finished.", y, kWhite, FONT_NORMAL);
				Lines("Waiting for your friend to return.", y + 10.0f, kCream);
				break;
			}
			if (RelayRequested())
			{
				const float limit = ListTop(mList[mPage]) - 6.f;
				const char *head = SignalingSelected() ? SignalRouteStage(SignalingView().status, s.state, true, true) : ("Two-player relay pilot (experimental)");
				const char *label = "Join code: share only with the intended player";
				const char *waiting = SignalingSelected() ? SignalConnectionDetail() : (s.state == CE_LOBBY_HOST_CONNECTING ? "Private connection setup in progress..." : "Waiting for private setup or the invited player...");
				const char *explain = SignalingSelected() ? "Share the private CE code only with the approved partner. Connection records exchange through the selected service." : ("Transfer the new lobby files between both PCs. The game does not send them. The game connection uses a separate prepared folder.");
				const char *note = mNotice[0] ? mNotice : s.notice;
				const float need = Height(head) + 8.f + Height(label) + 2.f + Height(s.code, FONT_NORMAL, kCodeWidth) + 8.f + Height(waiting) + (*note ? 4.f + Height(note, FONT_SMALL, kNoticeWidth) : 0.f);
				const bool explained = y + need + 8.f + Height(explain) <= limit;
				if (y + Height(head) <= limit) y = Lines(head, y, kWarn);
				if (y + 8.f + Height(label) <= limit) y = Lines(label, y + 8.f, kWhite);
				if (y + 2.f + Height(s.code, FONT_NORMAL, kCodeWidth) <= limit) y = Lines(s.code, y + 2.f, kWarn, FONT_NORMAL, kCodeWidth);
				if (y + 8.f + Height(waiting) <= limit) y = Lines(waiting, y + 8.f, kWhite);
				if (*note && y + 4.f + Height(note, FONT_SMALL, kNoticeWidth) <= limit) y = Lines(note, y + 4.f, kWarn, FONT_SMALL, kNoticeWidth);
				if (explained && y + 8.f + Height(explain) <= limit) Lines(explain, y + 8.f, kGrey);
				break;
			}
			const char *waiting = s.state == CE_LOBBY_HOST_CONNECTING ? "Your friend is connecting..." : "Waiting for your friend to join.";
			const char *note = mNoticeBoxed ? "" : mNotice;
			// What the lobby says while the host waits (a friend who could not join or left, a new code, nobody yet) is one
			// line under the waiting line, so the code the friend must read stays in view (review R2 finding 1). The block
			// starts lower on the page when everything fits, higher when the lines need the room; it never drops a line.
			const char *lobby = s.notice;
			const float need = Height("Your Join Code", FONT_SMALL, 400.f) + 4.f + Height(s.code, FONT_NORMAL, kCodeWidth) + 6.f +
							   Height("Read it out or send it to your friend.", FONT_SMALL, kPageWidth) + 14.f + Height(waiting, FONT_SMALL, kPageWidth) +
							   (*lobby ? 8.f + Height(lobby, FONT_SMALL, kPageWidth) : 0.f) + (*note ? 8.f + Height(note, FONT_SMALL, kPageWidth) : 0.f);
			const float limit = ListTop(mList[mPage]) - 6.f;
			const float room = limit - y;
			const float drop = need + 36.f <= room ? 36.f : (need < room ? room - need : 0.f);
			y = Lines("Your Join Code", y + drop, kGrey) + (need > room ? 2.f : 4.f);
			y = Lines(s.code, y, kWhite, FONT_NORMAL, kCodeWidth) + (need > room ? 2.f : 6.f);
			if (y + Height("Read it out or send it to your friend.", FONT_SMALL, kPageWidth) <= limit)
				y = Lines("Read it out or send it to your friend.", y, kGrey, FONT_SMALL, kPageWidth) + (need > room ? 6.f : 14.f);
			if (y + Height(waiting, FONT_SMALL, kPageWidth) <= limit)
				y = Lines(waiting, y, kCream, FONT_SMALL, kPageWidth);
			mHostLineBottom = y;
			if (*lobby && y + 8.f + Height(lobby, FONT_SMALL, kPageWidth) > limit)
				lobby = "";
			if (*lobby)
				y = mHostLineBottom = Lines(lobby, y + 8.f, kWhite, FONT_SMALL, kPageWidth);
			if (*note && y + 8.f + Height(note, FONT_SMALL, kPageWidth) <= limit)
				mHostLineBottom = Lines(note, y + 8.f, kWhite, FONT_SMALL, kPageWidth);
			break;
		}
		case NP_JOIN:
		{
			if (PlayerPages() && mReturningRoom && !s.host && s.state == CE_LOBBY_JOIN_CONNECTING)
			{
				y = Lines("Match finished.", y, kWhite, FONT_NORMAL);
				Lines("Rejoining your friend...", y + 10.0f, kCream);
				break;
			}
			const char *intro = SignalingSelected() ?
                (mSignalApproved && SignalOriginMatches() && SignalingView().status == CeIceSignalStatus::Admitted ?
                    (mCode->mValue[0] ? "Choose Connect to join your host." :
                     "Invitation accepted. Wait for the host's code, then enter it and choose Connect.") :
                 "Choose Connect to review the invitation and accept it. The host sends a code after you both accept.") : (RelayRequested() ? "Two-player relay pilot: paste or type the host's code, then Connect. Transfer files between the prepared lobby folders. The address field is not used." :
							"Type the code your friend's game shows, then choose Connect.");
			const char *shorter = SignalingSelected() ? "Use the approved host's CE code. Online requires pair consent." : (RelayRequested() ? "Two-player relay pilot: enter the host's code, then Connect. Transfer the private lobby files." :
							  "Type your friend's code, then choose Connect.");
			DWORD codeColour;
			const char *problem = CodeLine(&codeColour);
			const char *note = mNoticeBoxed ? "" : mNotice;
			char connecting[120] = "";
			if (s.state == CE_LOBBY_JOIN_CONNECTING)
			{
				if (SignalingSelected()) snprintf(connecting, sizeof connecting, "%s", "Relay: Connecting via TURN...");
				else if (RelayRequested()) snprintf(connecting, sizeof connecting, "%s", "Waiting for lobby files and the other player...");
				else snprintf(connecting, sizeof connecting, "%s", "Connecting...");
			}
			// The words below the introduction (the code's problem, the connection, the notice) are what the player needs; the parenthesis of the introduction goes when they do not fit above the list.
			const float limit = ListTop(mList[NP_JOIN]) - 6.f;
			const float rest = (*problem ? 10.f + Height(problem, FONT_SMALL, kPageWidth) : 0.f) + (*connecting ? 10.f + Height(connecting, FONT_SMALL, kPageWidth) : 0.f) + (*note ? 10.f + Height(note, FONT_SMALL, kPageWidth) : 0.f);
			const char *introShown = !PlayerPages() && y + Height(intro, FONT_SMALL, kPageWidth) + rest <= limit ? intro : shorter;
			if (y + Height(introShown, FONT_SMALL, kPageWidth) <= limit)
				y = Lines(introShown, y, PlayerPages() ? kCream : kWhite, FONT_SMALL, kPageWidth);
			const float gap = (y + rest <= limit) ? 10.f : 4.f;
			if (*problem && y + gap + Height(problem, FONT_SMALL, kPageWidth) <= limit)
				y = Lines(problem, y + gap, codeColour, FONT_SMALL, kPageWidth);
			if (*connecting && y + gap + Height(connecting, FONT_SMALL, kPageWidth) <= limit)
				y = Lines(connecting, y + gap, kWhite, FONT_SMALL, kPageWidth);
			if (*note && y + gap + Height(note, FONT_SMALL, kPageWidth) <= limit)
				Lines(note, y + gap, kWhite, FONT_SMALL, kPageWidth);
			break;
		}
		case NP_LOBBY:
		{
			if (s.state == CE_LOBBY_STARTING)
			{
				// The first moment of the start shows the page with its two player slots and no words: that frame is kept as
				// the background of the restarted game's waiting screen (ce_network_wait.cpp; ce_frontend.cpp writes it). Then
				// "Starting..." until the game quits.
				if (mStartingAt < 0.f || PLATFORM.GetSysTimeFloat() - mStartingAt < 0.05f)
				{
					if (PlayerPages())
						for (int slot = 0; slot < 2; slot++)
						{
							const float sx = slot ? kSlotRightX : kSlotLeftX;
							PortCeNetDrawSlot(sx, kSlotY, true);	// the kept frame's slots
							LinesAt(slot ? "Player 2" : "Player 1", sx, kSlotY - 38.f, kGrey);
							LinesAt((slot == 0) == (s.host != 0) ? "You" : "Your Friend", sx, kSlotY + 26.f, kWhite);
						}
					break;
				}
				Lines("Starting...", 220.f, kWhite, FONT_NORMAL);
				break;
			}
			const float limit = ListTop(mList[NP_LOBBY]) - 6.f;
			if (PlayerPages())
			{
				// Two player slots as the original's orbs (guide 3.6), each with its player under it and, for the friend,
				// the state as a word and a colour; then, for the guest, the mission and who starts it.
				const bool ready = s.state == CE_LOBBY_READY;
				const char *state = ready ? "Ready" : s.state == CE_LOBBY_REFUSED ? "Different Version" : "Checking...";
				const DWORD stateColour = ready ? kGood : s.state == CE_LOBBY_REFUSED ? kBad : kWhite;
				for (int slot = 0; slot < 2; slot++)
				{
					const float sx = slot ? kSlotRightX : kSlotLeftX;
					const bool you = (slot == 0) == (s.host != 0);
					PortCeNetDrawSlot(sx, kSlotY, true);
					LinesAt(slot ? "Player 2" : "Player 1", sx, kSlotY - 38.f, kGrey);
					LinesAt(you ? "You" : "Your Friend", sx, kSlotY + 26.f, kWhite);
					LinesAt(you && s.state == CE_LOBBY_REFUSED ? "" : state, sx, kSlotY + 44.f, stateColour);
				}
				y = kSlotY + 62.f;
				if (!s.host)
				{
					const char *waitHost = "Waiting for your friend to start.";
					snprintf(t, sizeof t, "Mission: %s", MissionName(s.level));
					const float mGap = (y + 12.f + Height(t) + 4.f + Height(waitHost) <= limit) ? 12.f : 4.f;
					if (y + mGap + Height(t) <= limit)
						y = Lines(t, y + mGap, kCream);
					if (ready && !mReturningRoom && y + 4.f + Height(waitHost) <= limit)
						y = Lines(waitHost, y + 4.f, kCream);
				}
				const float tGap = 6.f;
				if (mNotice[0] && !mNoticeBoxed)
				{
					if (y + tGap + Height(mNotice, FONT_SMALL, kPageWidth) <= limit)
						Lines(mNotice, y + tGap, kWhite, FONT_SMALL, kPageWidth);
				}
				else if (mReturningRoom && ready)
				{
					// One line: the host's rows (Mission, Start, Leave) begin at y 275 and the guest's Leave at y 295, so a
					// second line ran into them at 640x480. The host's rows say what to do; the guest, whose page already
					// names the mission, is told who starts it (a longer guest line wrapped into Leave).
					const float retH = Height(s.host ? "Back in the lobby together." : "Your friend starts the next mission.", FONT_SMALL, kPageWidth);
					if (y + tGap + retH <= limit)
						Lines(s.host ? "Back in the lobby together." : "Your friend starts the next mission.",
						      y + tGap, kWhite, FONT_SMALL, kPageWidth);
				}
				else if (mOutcome[0] && mOutcomeGood)
				{
					if (y + tGap + Height(mOutcome, FONT_SMALL, kPageWidth) <= limit)
						Lines(mOutcome, y + tGap, kWhite, FONT_SMALL, kPageWidth);
				}
				break;
			}
			const char *peer = s.peer_version[0] ? s.peer_version : "...";
			bool ok = s.state == CE_LOBBY_READY;
			const char *stage = ok ? "Connected via Relay" : SignalRouteStage(SignalingView().status, s.state, SignalingSelected(), RelayRequested());
			if (y + Height(stage) <= limit)
				y = Lines(stage, y, ok ? kGood : kWarn) + 4.f;
			snprintf(t, sizeof t, "Player 1 (host%s): Onslaught CE %s", s.host ? ", you" : "", s.host ? s.local_version : peer);
			if (y + Height(t) <= limit)
				y = Lines(t, y, kWhite);
			snprintf(t, sizeof t, "Player 2%s: Onslaught CE %s", s.host ? "" : " (you)", s.host ? peer : s.local_version);
			if (y + Height(t) <= limit)
				y = Lines(t, y, kWhite);
			const char *compat = s.compat[0] ? s.compat : "Checking...";
			if (y + 8.f + Height(compat) <= limit)
				y = Lines(compat, y + 8.f, ok ? kGood : kBad);
			if (ok && !s.host && y + 8.f + Height("Waiting for the host to start.") <= limit)
				y = Lines("Waiting for the host to start.", y + 8.f, kWhite);
			if (s.state == CE_LOBBY_REFUSED && y + 8.f + Height("Both players need the same Onslaught CE download. Leave, update, and join again.") <= limit)
				y = Lines("Both players need the same Onslaught CE download. Leave, update, and join again.", y + 8.f, kGrey);
			const char *note = PlayerPages() ? (mNoticeBoxed ? "" : mNotice) : (mNotice[0] ? mNotice : s.notice);
			// What happens when the games disagree, and how to leave, is in the README, not standing prose on the page.
			if (*note && y + 8.f + Height(note, FONT_SMALL, kNoticeWidth) <= limit)
				Lines(note, y + 8.f, kWarn, FONT_SMALL, kNoticeWidth);
			break;
		}
		case NP_HELP:
            if (SignalingSelected()) {
                const float limit = ListTop(mList[NP_HELP]) - 6.f;
                const char *h0 = "Online approved-pair help";
                const char *h1 = "Host or Join first shows the approved HTTPS service and Cloudflare TURN relay disclosure. Continue contacts that service; Accept permits this private pair's relay setup.";
                const char *h2 = "Share the private CE code only with the approved partner. Connection records are exchanged through the selected service.";
                const char *h3 = "Retry is deliberate and keeps the fixed deadline. Closed, expired or uncertain issuance needs a fresh reviewed setup. Public browsing is unavailable.";
                if (y + Height(h0, FONT_SMALL, kHelpWidth) <= limit)
                    y = Lines(h0, y, kWhite, FONT_SMALL, kHelpWidth) + 6.f;
                if (y + Height(h1, FONT_SMALL, kHelpWidth) <= limit)
                    y = Lines(h1, y, kGrey, FONT_SMALL, kHelpWidth) + 6.f;
                if (y + Height(h2, FONT_SMALL, kHelpWidth) <= limit)
                    y = Lines(h2, y, kGrey, FONT_SMALL, kHelpWidth) + 6.f;
                if (y + Height(h3, FONT_SMALL, kHelpWidth) <= limit)
                    Lines(h3, y, kWarn, FONT_SMALL, kHelpWidth);
                break;
            }

		{
			if (RelayRequested())
			{
				const float limit = ListTop(mList[NP_HELP]) - 6.f;
				const char *r0 = "Two-player relay pilot (manual file exchange)";
				const char *r1 = "Before launch, an operator needs two different, empty private folders per PC: lobby and game. Use the same build.";
				const char *r2 = "Host privately shares the join code. Guest enters it and chooses Connect. The operator transfers request, offer and answer files; repeat in the matching game folders after Start.";
				const char *r3 = "Offer parcels contain temporary credentials. Use a confidential authenticated channel: base64 is not encryption. A local export is not delivery. Copy each file completely before import. Keep filenames and files private. Another connection attempt or mission needs fresh folders and a relaunch. No files are sent automatically. Internet play has not been verified.";
				if (y + Height(r0, FONT_SMALL, kHelpWidth) <= limit)
					y = Lines(r0, y, kWarn, FONT_SMALL, kHelpWidth) + 6.f;
				if (y + Height(r1, FONT_SMALL, kHelpWidth) <= limit)
					y = Lines(r1, y, kWhite, FONT_SMALL, kHelpWidth) + 6.f;
				if (y + Height(r2, FONT_SMALL, kHelpWidth) <= limit)
					y = Lines(r2, y, kWhite, FONT_SMALL, kHelpWidth) + 6.f;
				if (y + Height(r3, FONT_SMALL, kHelpWidth) <= limit)
					Lines(r3, y, kGrey, FONT_SMALL, kHelpWidth);
				break;
			}
			// Four short paragraphs in cream, a blank line between them, ending well above Back (list centre 440).
			y = Lines(kHelpNetwork, y + 16.f, kCream, FONT_SMALL, kPageWidth) + 14.f;
			snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());
			y = Lines(t, y, kCream, FONT_SMALL, kPageWidth) + 14.f;
			y = Lines(kHelpAddress, y, kCream, FONT_SMALL, kPageWidth) + 14.f;
			Lines(kHelpFirewall, y, kCream, FONT_SMALL, kPageWidth);
			break;
		}
		}
	}

	// The focused row's hint, under Back (house style L5: 28 pixels below the top of Back, wrapped to 420, ending by y 449).
	void	DrawHint()
	{
		if (FEMESSBOX.BeingDisplayed() || RelayRequested() || SignalingSelected())
			return;
		CMenuOptionsList *list = mList[mPage];
		const int n = list->mItems.Size();
		if (n < 1 || list->mSelected < 0 || list->mSelected >= n)
			return;
		CMenuItem *item = list->mItems.At(list->mSelected);
		const char *hint = item->mId == CNetItem::ID ? RowHint(((CNetItem *)item)->mAction) : NULL;
		if (item == mJoinBack && mTrying)
			hint = "Stops trying to connect.";
		// While the Address row has the focus, a value that can never become an address says so at once, in the hint's
		// place (review R2 polish 3); a value still being typed ("192.168.") is not scolded.
		if (item == mAddress && AddressImpossible(mAddress->mValue))
			hint = "That is not an address. Check it with your friend.";
		if (!hint)
			return;
		float height = 0.f;
		for (int i = 0; i < n; i++)
			height += float(list->mItems.At(i)->GetHeight());
		const float back = list->mY - height * 0.5f - 15.f + height - float(list->mItems.At(n - 1)->GetHeight());
		// Drawn after the list, whose row stage doubles text colours (measured: the plain cream drew white), so the hint
		// gives cream at half strength, as the CE page's own hints do (FIXLIST rule 6; 253,221,189 on the screen).
		Lines(hint, back + 28.f, kCreamAfterList, FONT_SMALL, kPageWidth);
	}

	// CPauseMenu::Render for an options-style page, as the CE page draws its own (ce_frontend.cpp).
	void	Draw()
	{
		if (!mActive)
			return;
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
		// The message box is translucent: nothing of the page is drawn behind it (as the CE page's own lists).
		// A click on the box's OK must not also reach the row under it (review R2 note 4): the clicks of the moment after a
		// box closes are the box's.
		const bool box = FEMESSBOX.BeingDisplayed() != 0;
		if (mBoxWasUp && !box)
			mSwallowClicksUntil = PLATFORM.GetSysTimeFloat() + 0.25f;
		mBoxWasUp = box;
		if (!box && PLATFORM.GetSysTimeFloat() < mSwallowClicksUntil)
			(void)MouseClickedInRect(0.f, 0.f, 640.f, 480.f);
		if (PLATFORM.GetSysTimeFloat() - mOpenTime > 0.2f && !box)
		{
			// Commit the intent only after the mission row has been rendered.
			mCampaignMissionKnown = mPage == NP_LOBBY && mCampaignMissionPendingKnown;
			if (mCampaignMissionKnown) mCampaignMission = mCampaignMissionPending;
			DrawText();
			CE_LobbyStatus s;
			CE_LobbyStatusGet(&s);
			if (s.state != CE_LOBBY_STARTING)
			{
				PointerOnTrappedPage();
				mLists.At(mPage)->Render(this);
				DrawHint();
			}
		}
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
	}

	bool mConnectionSelected, mConnectionRelay;
    bool mSignalApproved, mSignalBegan;
    int mSignalAction, mSignalReturnPage;
    bool mProceedWasReady = false;
    char mSignalOrigin[512];
    CE_InvitationView mInvitation;
    uint32_t mInvitationRole;
    CNetField* mImportOrigin;
    CNetItem* mImportAddressLabel;
    int mImportOriginRow;
    CNetItem *mInvitationEntry, *mImportPaste, *mImportHost, *mImportJoin, *mImportProfileHost, *mImportProfileJoin, *mImportUse;
    CNetItem *mImportClearInput, *mImportClearPreview;

    CNetItem *mSignalContinue, *mSignalAccept, *mSignalProceed, *mSignalRetry, *mSignalClose;



	CMenuOptionsList *mList[NP_COUNT];
	CNetItem	*mOnlineEntry;
	CNetItem	*mHostItem, *mJoinItem, *mConnect, *mStart;
	CNetItem *mPasteCode, *mPasteAddress, *mClearFields, *mJoinBack;
	CNetItem *mRoomCopy, *mRoomBack;
	CNetValue	*mMap;
	bool		mTrying = false;			// a join is trying: the Join page shows the code and Cancel only
    CNetValue *mHostAddress;
    CNetItem *mCampaignStart;
    uint32_t mCampaignMission;
    uint32_t mCampaignMissionPending;
    bool mCampaignMissionPendingKnown;
    bool mCampaignMissionKnown;

    char mHostAddresses[CE_LOBBY_ADDRESS_CHOICES][16];
    int mHostAddressCount, mHostAddressIndex;

	CNetField	*mCode, *mAddress;
	char		mNotice[320], mOutcome[256];
	bool		mNoticeBoxed = false;			// mNotice is shown in the message box, not on the page
	char		mLastLobbyNotice[320] = "";		// the lobby notice last shown on a page that stays up
	int			mPointerX = -1, mPointerY = -1;	// the pointer at the last frame (PointerOnTrappedPage)
	float		mHostLineBottom = 0.f;			// where the host page's words ended at the last frame (a check reads it)
	bool		mBoxWasUp = false;				// a message box was up at the last frame
	float		mSwallowClicksUntil = 0.f;		// clicks until then were the closed box's
	bool		mTried = false;					// Connect was chosen with the code mTriedCode, which was not complete
	char		mTriedCode[CE_INVITATION_ORIGIN_BYTES] = "";
	int			mOutcomeGood;
	bool mReturningRoom;
	float		mStartingAt;
	bool		mQuitPosted;
	bool		mProfileHostAvailable = false;
	bool		mProfileJoinAvailable = false;
	float		mProfileProbeTime = -1.f;
};

// The join page's keyboard: typing into the selected field, Ctrl+V pasting into it, and the page's own keys (with a key
// trap set, the game's key bindings do not reach the menus: CFEPVirtualKeyboard's name entry works the same way).
// The game also asks the key trap about every mouse button (src/mouse.cpp MOUSE_TRAP: a true answer swallows the button),
// so like the name entry's VKKeyTrap it returns false: the pointer's clicks still reach the page.
bool	NetKeyTrap(WORD key, KeyEventType event)
{
	if (!sMenu || !sActive || (event != KEY_EVENT_DOWN && event != KEY_EVENT_UP && event != KEY_EVENT_CHAR))
		return false;
	if (key == DIK_LCONTROL || key == DIK_RCONTROL)
	{
		if (event == KEY_EVENT_DOWN)
			sCtrl = true;
		else if (event == KEY_EVENT_UP)
			sCtrl = false;
		return false;
	}
	CMenuOptionsList *list = sMenu->mList[sMenu->mPage];
	CMenuItem *item = list->mItems.At(list->mSelected);
	CNetField *field = (item == sMenu->mCode) ? sMenu->mCode : (item == sMenu->mAddress) ? sMenu->mAddress : NULL;
    if (sMenu->mPage == NP_INVITATION_INPUT) {
        field = list->mItems.At(list->mSelected) == sMenu->mImportOrigin ? sMenu->mImportOrigin : NULL;
    }

	if (field && (!field->IsEnabled() || sMenu->JoinPending() || (field == sMenu->mAddress && RelayRequested()))) field = NULL;
	if (event == KEY_EVENT_DOWN)
	{
		if (key == DIK_UP)
			list->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_UP, 1.f);
		else if (key == DIK_DOWN || key == DIK_TAB)
			list->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_DOWN, 1.f);
		else if (key == DIK_ESCAPE)
			sMenu->Back();
		else if (key == DIK_RETURN || key == DIK_NUMPADENTER)
			sMenu->Choose((sMenu->mPage == NP_JOIN && sMenu->mConnect->mEnabled) && field ? (CMenuItem *)sMenu->mConnect : item);
		else if (key == DIK_BACK && field)
			field->Back();
		else if (key == DIK_V && field && (sCtrl || CE_LobbyCtrlHeld()))
			sMenu->PasteField(field);
	}
	else if (event == KEY_EVENT_CHAR && field && key >= 0x20 && key < 0x7f && !sCtrl)
	{
		char c[2] = {(char)key, 0};
		field->Add(c);
	}
	return false;
}

}

//*********************************************************************************
// The CE page's side (ce_frontend.cpp).
bool	PortCeNetOffered()
{
	if (!CE_NETWORK_ENABLED || !PortCeMenus())
		return false;
	// The run mode (runmode.h) decides from the environment, once: no recording, replay, engineering session or other test
	// run offers Host and Join; ordinary play and a test-kit journey (driven by the player's input, on any clock) do.
	if (!PortRunOffersNetworkPages())
		return false;
	return !CLIPARAMS.mRecordDemo && !CLIPARAMS.mPlaybackDemo && !CLIPARAMS.mAutoConfigTest;
}

bool	PortCeNetActive()
{
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiActive && PortCeOnlineUiActive())
		return true;
#endif
	return sActive && sMenu;
}

// The title bar of each page names that page, one level down, as the original's do ("Sound Options" under OPTIONS); the
// row that opens a page and its title match word for word (ce_frontend.cpp draws it).
WCHAR	*PortCeNetTitle()
{
	static WCHAR network[] = L"Network Game", host[] = L"Host Game", join[] = L"Join Game", help[] = L"" CE_LOBBY_HELP_LABEL;
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiActive && PortCeOnlineUiActive() && PortCeOnlineUiTitleUtf8)
	{
		static WCHAR onlineTitle[64];
		const char *utf8 = PortCeOnlineUiTitleUtf8();
		Widen(onlineTitle, 64, (utf8 && *utf8) ? utf8 : "Play Online");
		return onlineTitle;
	}
#endif
	if (!sMenu)
		return network;
	switch (sMenu->mPage)
	{
	case NP_HOST:
	case NP_HOST_ADDRESS:	return host;
	case NP_JOIN:			return join;
	case NP_HELP:			return help;
	default:				return network;
	}
}

void	PortCeNetOpen()
{
	if (!PortCeNetOffered())
		return;
	sDirectFromOnline = false;
#ifndef CE_NETWORK_OFF
	if (sOpenOnlineOnEntry)
	{
		sOpenOnlineOnEntry = false;
		if (PortCeOnlineUiOpen)
		{
			PortCeOnlineUiOpen();
			return;
		}
	}
	if (CE_LobbyOutcomePending() && CE_LobbyOutcomeOnline && CE_LobbyOutcomeOnline() &&
		PortCeOnlineUiOpen && PortCeOnlineUiOffered && PortCeOnlineUiOffered())
	{
		char text[256];
		int good = 1;
		const bool outcome = CE_LobbyTakeOutcome(text, sizeof text, &good) != 0;
		PortCeOnlineUiOpen();
		if (outcome && PortCeOnlineUiSetOutcomeNotice)
			PortCeOnlineUiSetOutcomeNotice(text, good);
		return;
	}
#endif
	if (!sMenu)
		sMenu = new (MEMTYPE_MENUITEM) CCeNetMenu();
	CPauseMenu::mOptionsPage = true;
	sMenu->Activate(0);
	sActive = true;
	sMenu->mNotice[0] = 0;
	sMenu->mStartingAt = -1.f;
	sMenu->mQuitPosted = false;
	char text[256];
	int good = 1;
	const int rematch = CE_LobbyResumeMatch();
	const bool outcome = CE_LobbyTakeOutcome(text, sizeof text, &good) != 0;
	if (outcome)
	{
		snprintf(sMenu->mOutcome, sizeof sMenu->mOutcome, "%s", text);
		sMenu->mOutcomeGood = good;
	}
	sMenu->mReturningRoom = outcome && rematch != 0;
	sMenu->Show(rematch == 1 ? NP_HOST : rematch == 2 ? NP_JOIN : NP_MENU);
	if (rematch == 1 || rematch == 2)
	{
		sMenu->mConnectionSelected = true;
		sMenu->mConnectionRelay = RelayRequested();
	}
	// Back from a network game: what happened, in the message box over the first page (house style E1).
	if (outcome && !rematch)
		Box(sMenu->mOutcome);
}

#ifndef CE_NETWORK_OFF
extern "C" void	PortCeNetOpenDirectFromOnline(void)
{
	sOpenOnlineOnEntry = false;
	PortCeNetOpen();
	if (sActive)
		sDirectFromOnline = true;
}
#endif

void	PortCeNetButton(SINT button, float val)
{
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiActive && PortCeOnlineUiActive() && PortCeOnlineUiButton)
	{
		PortCeOnlineUiButton(button, val);
		return;
	}
#endif
	if (PortCeNetActive())
		sMenu->ReceiveButtonAction(NULL, button, val);
}

void	PortCeNetProcess()
{
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiActive && PortCeOnlineUiActive() && PortCeOnlineUiUpdate)
	{
		PortCeOnlineUiUpdate();
		return;
	}
#endif
	if (PortCeNetActive())
		sMenu->Update();
}

void	PortCeNetDraw()
{
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiActive && PortCeOnlineUiActive() && PortCeOnlineUiRender)
	{
		PortCeOnlineUiRender();
		return;
	}
#endif
	if (PortCeNetActive())
		sMenu->Draw();
}

// The CE page is going: nothing of the lobby outlives it.
void	PortCeNetShutdown()
{
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiClose)
		PortCeOnlineUiClose();
#endif
	if (sMenu)
	{
		sMenu->Trap(false);
		CE_LobbyShutdown();
		SAFE_DELETE(sMenu);
	}
	sActive = false;
	sOpenOnlineOnEntry = false;
	sDirectFromOnline = false;
}

bool	PortCeNetOpenOnEntry()
{
	bool open = sOpenOnEntry;
	sOpenOnEntry = false;
#ifndef CE_NETWORK_OFF
	if (PortCeOnlineUiTakePendingEntry && PortCeOnlineUiTakePendingEntry())
	{
		sOpenOnlineOnEntry = true;
		open = true;
	}
#endif
	return open;
}

// The title page, each frame (patch 0141): a front end started again after a network game goes straight to these pages
// with the outcome, as if the player had pressed a key on the title and chosen Onslaught CE, then Network game.
void	PortCeNetIntro(float idle)
{
	static bool done = false;
	if (done || !CE_LobbyOutcomePending() || idle < 0.3f)
		return;
	done = true;
	if (!PortCeNetOffered())
		return;
	CLIPARAMS.mAttractMode = FALSE;
	sOpenOnEntry = true;
	FRONTEND.SetPage(FEP_CE, 50);
}

#else

// Builds without the CE layer: the title page's hook (patch 0141) does nothing.
void	PortCeNetIntro(float idle) {}

#endif
