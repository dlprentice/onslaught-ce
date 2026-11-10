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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)
#define FEP_CE				FEP_LANGUAGE_TEST		// the CE page's slot (ce_frontend.cpp)

bool	PortCeMenus();							// ce_frontend.cpp

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

const DWORD	kWhite = 0xffffffff, kGrey = 0xffa8a8a8, kGood = 0xff80e080, kWarn = 0xffffcc00, kBad = 0xffff8060;

// The join code is one word of up to 28 wide letters: at the usual 400 pixels the word wrap cut it in pieces (a lone letter on a line of its own); on a line of this width the widest code still fits.
const float	kCodeWidth = 560.f;
// The notices of the Host, Join and lobby pages (a few sentences long, since the game cannot tell the causes apart) and the help page's paragraphs are wrapped wider than the default 400.
const float	kNoticeWidth = 520.f, kHelpWidth = 540.f;

// The words of the help page (port/tools/network_page_words_tests.py asserts them word for word). They tell the host what to do at the router, at the firewall and about the public address.
// The game can see none of these, so nothing here claims to know what is wrong with a particular connection.
const char *const kHelpForward = "1. Forward UDP port %u on your router to this PC (Port Forwarding or Virtual Server; this PC's address is on the host page).";
const char *const kHelpFirewall = "2. If Windows asks about the firewall, choose Allow access.";
const char *const kHelpAddress = "3. Give the other player the join code and your PUBLIC address (the WAN or Internet address on the router's status page). They type the code, then the address.";
const char *const kHelpPrivate = "An address starting 10., 192.168. or 172.16 to 172.31 is private, and one starting 100.64 to 100.127 is shared by your provider: another router is in front of yours, so a port "
								 "forward is not enough. Use a VPN or overlay network both players already use, or play on one network.";
const char *const kHelpUntested = "Not tested: play through routers over the real internet, more than two players, Windows on real hardware.";
// The host page's two optional lines under its code (dropped, the second first, when they do not fit above the list).
const char *const kHostForward = "Over the internet: forward UDP port %u on your router to this PC and give your public address too (see " CE_LOBBY_HELP_LABEL ").";
const char *const kHostFirewall = "If a firewall asks about Onslaught CE, choose Allow.";

bool RelayRequested() { return CE_NetGameIceRequested() != 0; }

bool MenuAvailable(char *why, size_t size)
{
#if !defined(CE_NETWORK_ICE)
    if (RelayRequested())
    {
        snprintf(why, size, "%s", "Experimental relay connections are unavailable in this build. Use a relay-enabled build, or restart without this connection setup.");
        return false;
    }
#endif
    return CE_LobbyAvailable(why, size) != 0;
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
	return float(font->GetHeight()) * float(font->WordWrap(&fill, buf, width));
}

// Where the words above a page's list must end: CMenuOptionsList::Render centres the rows on mY and draws the title 15 pixels above them.
float	ListTop(CMenuOptionsList *list)
{
	int height = 0;
	for (int i = 0; i < list->mItems.Size(); i++)
		height += list->mItems.At(i)->GetHeight();
	return list->mY - float(height) * 0.5f - 15.f;
}

// Text wrapped to the space between the page's borders, each line centred on the page, from y down; returns the next y.
float	Lines(const char *text, float y, DWORD colour, EFontType font_id = FONT_SMALL, float width = 400.f)
{
	if (!text || !*text)
		return y;
	static SWordWrapFill fill;
	static WCHAR buf[600];
	Widen(buf, 600, text);
	CDXBitmapFont *font = PLATFORM.Font(font_id);
	int n = font->WordWrap(&fill, buf, width);
	float line = float(font->GetHeight());
	for (int l = 0; l < n; l++)
	{
		SIZE size;
		font->GetTextExtent(fill.c[l], &size);
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	}
	return y + line * n;
}

enum EPage { NP_MENU = 0, NP_HOST_CHOOSE, NP_HOST, NP_JOIN, NP_LOBBY, NP_HELP, NP_CONNECTION, NP_SIGNAL_DISCLOSURE, NP_SIGNAL_STATUS, NP_INVITATION_INPUT, NP_INVITATION_PREVIEW, NP_COUNT };
enum EAction { NA_NONE = 0, NA_HOST, NA_JOIN, NA_BACK, NA_COPY, NA_CODE, NA_ADDRESS, NA_CONNECT, NA_MAP, NA_START, NA_LEAVE, NA_HELP, NA_HOST_ADDRESS, NA_HOST_BEGIN, NA_CAMPAIGN_START, NA_PASTE_CODE, NA_PASTE_ADDRESS, NA_CLEAR_FIELDS, NA_LOCAL, NA_ONLINE, NA_BROWSE, NA_SIGNAL_DISCLOSE, NA_SIGNAL_ACCEPT, NA_SIGNAL_PROCEED, NA_SIGNAL_RETRY, NA_SIGNAL_CLOSE, NA_IMPORT_OPEN, NA_IMPORT_ORIGIN, NA_IMPORT_PASTE, NA_IMPORT_HOST, NA_IMPORT_JOIN, NA_IMPORT_USE, NA_IMPORT_CLEAR };

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
        return "Choose Continue to prepare the selected Host or Join connection. Pair admission does not mean CE is connected yet.";
    case CeIceSignalStatus::AwaitingPairConsent:
        return "The other player must accept this approved pair. The fixed setup deadline continues to run.";
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
        return "The service asked this player to wait. A deliberate retry keeps the original setup deadline.";
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
	void	Set(const char *text) { Widen(mWide, 128, text); }
	virtual WCHAR	*GetText() { return mWide; }
	WCHAR	mWide[128];
	int		mAction;
};

// A text field: its label and value; while it is selected, a blinking cursor (the game's own: a coloured I, as the name
// entry page draws it).
class CNetField : public CNetItem
{
public:
	CNetField(const char *label, int action, int max) : CNetItem(label, action), mMax(max) { mLabel8 = label; mValue[0] = 0; Refresh(); }
	void	Refresh()
	{
		char t[160];
		FieldText(t, sizeof t, mLabel8, mValue[0] ? mValue : (mAction == NA_ADDRESS ? "(none)" : "(type or paste)"));
		Set(t);
	}
	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (selected && (fmod(PLATFORM.GetSysTimeFloat(), 1.f) < 0.5f))
		{
			CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
			SIZE all, upto;
			font->GetTextExtent(mWide, &all);
			WCHAR part[160];
			char t[160];
		FieldText(t, sizeof t, mLabel8, mValue);
			Widen(part, 160, t);
			font->GetTextExtent(part, &upto);
			static WCHAR cursor[] = L"I";
			font->DrawTextWithShadow(x - float(all.cx / 2) + float(upto.cx) + 1.f, y, 0xff00c0ff, cursor, 0, 0, 1.f, 1.f);
		}
	}
	bool	Add(const char *s)
	{
		size_t n = strlen(mValue);
		bool any = false;
		for (; *s && n < (size_t)mMax; s++)
		{
			char c = *s;
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

	const char	*mLabel8;
	char mValue[CE_INVITATION_ORIGIN_BYTES];
	int			mMax;
};

class CCeNetMenu;
CCeNetMenu	*sMenu = NULL;
bool		sActive = false;
bool		sOpenOnEntry = false;		// the return from a network game: open these pages as the CE page arrives
bool		sCtrl = false;
bool		sTrap = false;

void	NetKeyTrap(WORD key, KeyEventType event);

class CCeNetMenu : public CPauseMenu
{
public:
	CCeNetMenu() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		static WCHAR title[] = L"Network game";
		for (int p = 0; p < NP_COUNT; p++)
			mList[p] = NULL;
		CMenuOptionsList *l;
		l = mList[NP_MENU] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 420.f, FALSE, 0xb0000000);	// low on the page: its words above take up to 13 lines
		l->AddItem(mLocalItem = new (MEMTYPE_MENUITEM) CNetItem("Local / direct connection", NA_LOCAL));
		l->AddItem(mOnlineItem = new (MEMTYPE_MENUITEM) CNetItem("Online relay (experimental)", NA_ONLINE));
		l->AddItem(mBrowseItem = new (MEMTYPE_MENUITEM) CNetItem("Public lobbies (unavailable)", NA_BROWSE));
#if defined(CE_NETWORK_ICE)
		l->AddItem(mInvitationEntry = new (MEMTYPE_MENUITEM) CNetItem("Import Online invitation", NA_IMPORT_OPEN));
#else
        mInvitationEntry = NULL;
#endif
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));

		l = mList[NP_CONNECTION] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 420.f, FALSE, 0xb0000000);	// low on the page: its words above take up to 13 lines
		l->AddItem(mHostItem = new (MEMTYPE_MENUITEM) CNetItem("Host a game", NA_HOST));
		l->AddItem(mJoinItem = new (MEMTYPE_MENUITEM) CNetItem("Join a game", NA_JOIN));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));

        l = mList[NP_HOST_CHOOSE] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, .5f, .72f, FALSE, 0xb0000000);
        l->AddItem(mHostAddress = new (MEMTYPE_MENUITEM) CNetItem("Address", NA_HOST_ADDRESS));
        l->AddItem(mHostBegin = new (MEMTYPE_MENUITEM) CNetItem("Host on this address", NA_HOST_BEGIN));
        l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_HOST] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 415.f, FALSE, 0xb0000000);
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Copy private code", NA_COPY));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_JOIN] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 385.f, FALSE, 0xb0000000);
		l->AddItem(mCode = new (MEMTYPE_MENUITEM) CNetField("Code: ", NA_CODE, 40));
		l->AddItem(mAddress = new (MEMTYPE_MENUITEM) CNetField("Address (optional): ", NA_ADDRESS, 21));
		l->AddItem(mConnect = new (MEMTYPE_MENUITEM) CNetItem("Connect", NA_CONNECT));
		l->AddItem(mPasteCode = new (MEMTYPE_MENUITEM) CNetItem("Paste code", NA_PASTE_CODE));
		l->AddItem(mPasteAddress = new (MEMTYPE_MENUITEM) CNetItem("Paste address", NA_PASTE_ADDRESS));
		l->AddItem(mClearFields = new (MEMTYPE_MENUITEM) CNetItem("Clear fields", NA_CLEAR_FIELDS));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_LOBBY] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 358.f, FALSE, 0xb0000000);
		l->AddItem(mMap = new (MEMTYPE_MENUITEM) CNetItem("Map: Building Defence (level 856)", NA_MAP));
		l->AddItem(mStart = new (MEMTYPE_MENUITEM) CNetItem("Start", NA_START));
        l->AddItem(mCampaignStart = new (MEMTYPE_MENUITEM) CNetItem("Start campaign co-op", NA_CAMPAIGN_START));

		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Leave", NA_LEAVE));
		l = mList[NP_HELP] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 440.f, FALSE, 0xb0000000);
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		l = mList[NP_INVITATION_INPUT] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 420.f, FALSE, 0xb0000000);
        l->AddItem(mImportAddressLabel = new (MEMTYPE_MENUITEM) CNetItem("Service address", NA_NONE));
        mImportAddressLabel->mEnabled = FALSE;
        mImportOriginRow = l->mItems.Size();
		l->AddItem(mImportOrigin = new (MEMTYPE_MENUITEM) CNetField("", NA_IMPORT_ORIGIN, CE_INVITATION_ORIGIN_BYTES - 1));
		l->AddItem(mImportPaste = new (MEMTYPE_MENUITEM) CNetItem("Paste address", NA_IMPORT_PASTE));
		l->AddItem(mImportHost = new (MEMTYPE_MENUITEM) CNetItem("Choose Host invite", NA_IMPORT_HOST));
		l->AddItem(mImportJoin = new (MEMTYPE_MENUITEM) CNetItem("Choose Join invite", NA_IMPORT_JOIN));
		l->AddItem(mImportClearInput = new (MEMTYPE_MENUITEM) CNetItem("Clear current pair", NA_IMPORT_CLEAR));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Cancel import", NA_BACK));
		l = mList[NP_INVITATION_PREVIEW] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 440.f, FALSE, 0xb0000000);
		l->AddItem(mImportUse = new (MEMTYPE_MENUITEM) CNetItem("Use invitation", NA_IMPORT_USE));
		l->AddItem(mImportClearPreview = new (MEMTYPE_MENUITEM) CNetItem("Clear current pair", NA_IMPORT_CLEAR));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Choose another folder", NA_BACK));
		l = mList[NP_SIGNAL_DISCLOSURE] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 440.f, FALSE, 0xb0000000);
		l->AddItem(mSignalContinue = new (MEMTYPE_MENUITEM) CNetItem("Continue", NA_SIGNAL_DISCLOSE));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Cancel", NA_BACK));
		l = mList[NP_SIGNAL_STATUS] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 440.f, FALSE, 0xb0000000);
		l->AddItem(mSignalAccept = new (MEMTYPE_MENUITEM) CNetItem("Accept approved pair", NA_SIGNAL_ACCEPT));
		l->AddItem(mSignalProceed = new (MEMTYPE_MENUITEM) CNetItem("Continue", NA_SIGNAL_PROCEED));
		l->AddItem(mSignalRetry = new (MEMTYPE_MENUITEM) CNetItem("Retry", NA_SIGNAL_RETRY));
		l->AddItem(mSignalClose = new (MEMTYPE_MENUITEM) CNetItem("Close pair", NA_SIGNAL_CLOSE));
		l->AddItem(new (MEMTYPE_MENUITEM) CNetItem("Back", NA_BACK));
		for (int p = 0; p < NP_COUNT; p++)
			mLists.Append(mList[p]);
        mConnectionSelected = mConnectionRelay = false;
        mSignalApproved = mSignalBegan = false;
        mSignalAction = NA_NONE; mSignalReturnPage = NP_CONNECTION; mSignalOrigin[0] = 0;
        memset(&mInvitation, 0, sizeof mInvitation);
        mInvitationRole = CE_NET_HOST;
        snprintf(mImportOrigin->mValue, sizeof mImportOrigin->mValue, "https://");
        mImportOrigin->Refresh();

		mNotice[0] = mOutcome[0] = 0;
		mOutcomeGood = 1;
		mStartingAt = -1.f;
		mQuitPosted = false;
        mHostAddressCount = mHostAddressIndex = 0;
        mCampaignMission = 0;
        mCampaignMissionPending = 0;
        mCampaignMissionPendingKnown = false;
        mCampaignMissionKnown = false;
        mHostAddresses[0][0] = 0;
	}

	void	Show(int page)
	{
        if (page == NP_MENU) mConnectionSelected = false;
		mPage = page;
        mCampaignMissionKnown = false;
		mList[page]->mSelected = 0;
		mList[page]->Reset();
		mNotice[0] = 0;
		if (page == NP_JOIN)
			mList[page]->mSelected = mCode->mValue[0] ? 2 : 0;
		Trap(page == NP_JOIN || page == NP_INVITATION_INPUT);
        if (page == NP_INVITATION_INPUT) mList[page]->mSelected = mImportOriginRow;

        RefreshSignalControls();
        RefreshInvitationControls();
        if (page == NP_SIGNAL_STATUS) {
            mList[page]->mSelected = mSignalAccept->IsEnabled() ? 0 :
                mSignalProceed->IsEnabled() ? 1 : mSignalRetry->IsEnabled() ? 2 :
                mSignalClose->IsEnabled() ? 3 : 4;
        }
}

	void	Trap(bool on)
	{
		if (on && !sTrap)
		{
			LT.SetKeytrap((pKeyTrapper)NetKeyTrap);
			sTrap = true;
		}
		else if (!on && sTrap)
		{
			if (LT.GetKeytrap() == (pKeyTrapper)NetKeyTrap)
				LT.SetKeytrap(NULL);
			sTrap = false;
			sCtrl = false;
		}
	}

	void	Notice(const char *text) { snprintf(mNotice, sizeof mNotice, "%s", text ? text : ""); }

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
            problem = "Nothing to paste: copy the host's code or address first.";
        else if (strlen(paste) == sizeof paste - 1)
            problem = "Clipboard text is too long. Copy only the code or address.";
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
                problem = "Paste only the host's IPv4 address, with an optional port.";
            for (const char *p = first; *p && !problem; ++p)
                if (!((*p >= '0' && *p <= '9') || *p == '.' || *p == ':'))
                    problem = "Paste only the host's IPv4 address, with an optional port.";
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
        char text[96];
        if (mHostAddressCount)
            snprintf(text, sizeof(text), "Address: %s (%d of %d)", mHostAddresses[mHostAddressIndex], mHostAddressIndex + 1, mHostAddressCount);
        else
            snprintf(text, sizeof(text), "Address: give it to the joining player separately");
        mHostAddress->Set(text);
        mHostAddress->mEnabled = mHostAddressCount > 1;
        mHostBegin->Set(mHostAddressCount ? "Host on this address" : "Host (manual address)");
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
		if (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f || mStartingAt >= 0.f)
			return;
		CMenuOptionsList *list = mLists.At(mPage);
		if (list->PassToSelected(from_controller, button, val))
			return;
		switch (button)
		{
		default:
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
		CFrontEnd::PlaySound(FES_BACK);
		switch (mPage)
		{
		case NP_MENU:
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
        case NP_CONNECTION:
                Show(NP_MENU);
                break;
            case NP_HOST_CHOOSE:
            mHostAddressCount = mHostAddressIndex = 0;
            mHostAddresses[0][0] = 0;
            Show(mConnectionSelected ? NP_CONNECTION : NP_MENU);
            break;
        case NP_HOST:
        case NP_LOBBY:
			CE_LobbyLeave();
			Show(mConnectionSelected ? NP_CONNECTION : NP_MENU);
			break;
		case NP_JOIN:
			CE_LobbyLeave();
			Show(mConnectionSelected ? NP_CONNECTION : NP_MENU);
			break;
		case NP_HELP:
			Show(mConnectionSelected ? NP_CONNECTION : NP_MENU);
			mList[mPage]->mSelected = mPage == NP_CONNECTION ? 2 : 3;		// the row that opened the help
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
		mCode->Clear();
		mAddress->Clear();
		DeActivate(FALSE);
		sActive = false;
	}

	bool ConnectionAvailable(char *why, size_t size)
    {
        if (!mConnectionSelected)
        {
            snprintf(why, size, "Choose Local or Online before hosting or joining.");
            return false;
        }
        if (mConnectionRelay != RelayRequested())
        {
            snprintf(why, size, "The connection mode changed. Go back and choose Local or Online again; no new connection was started.");
            return false;
        }
        return MenuAvailable(why, size);
    }

    void ChooseConnection(bool relay)
    {
        if (mPage != NP_MENU) return;
        if (relay != RelayRequested())
        {
            Notice(relay
                ? "Online relay is not configured for this launch. Prepare the experimental relay setup and relaunch; see Connection help."
                : "This launch uses Online relay. Relaunch without the relay setup for a Local / direct connection.");
            return;
        }
        char why[192];
        if (!MenuAvailable(why, sizeof why)) { Notice(why); return; }
        mConnectionSelected = true;
        mConnectionRelay = relay;
        Show(NP_CONNECTION);
    }


    bool InvitationPreviewValid() const
    {
        const int64_t now = CE_HandoffNow();
        return now > 0 && mInvitation.state == CE_INVITATION_PREVIEW_READY && mInvitation.role == mInvitationRole &&
            mInvitation.expires_at > uint64_t(now) && mInvitation.origin[0] &&
            memchr(mInvitation.origin, 0, sizeof mInvitation.origin) != NULL;
    }
    void RefreshInvitationControls()
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
        mImportUse->mEnabled = available && InvitationPreviewValid() && !SignalingSelected();
        mImportClearInput->mEnabled = mImportClearPreview->mEnabled = available && !busy && SignalingSelected();
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
        RefreshInvitationControls();
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
        mSignalClose->mEnabled = selected && status != CeIceSignalStatus::Closed;
        mSignalProceed->Set(mSignalAction == NA_HOST ? "Prepare Host" : "Connect");
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
		case NA_IMPORT_OPEN:
            if (mPage != NP_MENU && mPage != NP_CONNECTION) break;
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
            ClearSignalUi(); Show(NP_MENU); ChooseConnection(true);
            if (mPage == NP_CONNECTION) {
                if (mInvitationRole == CE_NET_HOST) RequireSignaling(NA_HOST, NP_CONNECTION);
                else { Show(NP_JOIN); Notice("Join invitation selected. Enter the approved host's CE code; Connect opens network disclosure."); }
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
                Show(action == NA_HOST ? NP_CONNECTION : NP_JOIN);
                Choose(action == NA_HOST ? mHostItem : mConnect);
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
        case NA_LOCAL:
                ChooseConnection(false);
                break;
            case NA_ONLINE:
                ChooseConnection(true);
                break;
            case NA_BROWSE:
                Notice("Public lobby browsing is unavailable. Use Host or Join with a code; the Online pilot still needs manual private-file exchange.");
                break;
            case NA_BACK:
			Back();
			return;
        case NA_HOST:
                if (mPage != NP_CONNECTION) break;
        {
            char why[200];
            if (!ConnectionAvailable(why, sizeof why)) { Notice(why); break; }
            if (RequireSignaling(NA_HOST, NP_CONNECTION)) break;
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
            RefreshHostAddresses();
            Show(NP_HOST_CHOOSE);
            if (mHostAddressCount < 2) mList[NP_HOST_CHOOSE]->mSelected = 1;
            break;
        }
        case NA_HOST_ADDRESS:
            if (mPage != NP_HOST_CHOOSE || !mHostAddressCount) return;
            mHostAddressIndex = (mHostAddressIndex + 1) % mHostAddressCount;
            HostAddressText();
            break;
        case NA_HOST_BEGIN:
                if (mPage != NP_HOST_CHOOSE) break;
            {
                char connection_problem[192];
                if (!ConnectionAvailable(connection_problem, sizeof connection_problem))
                {
                    Notice(connection_problem);
                    break;
                }
            if (mPage != NP_HOST_CHOOSE) return;
            if (!mHostAddressCount)
            {
                RefreshHostAddresses();
                if (mHostAddressCount)
                {
                    if (mHostAddressCount < 2) mList[NP_HOST_CHOOSE]->mSelected = 1;
                    Notice("A network address became available. Confirm it before hosting.");
                    break;
                }
            }
            if (mHostAddressCount ? CE_LobbyHostAt(mHostAddresses[mHostAddressIndex]) : CE_LobbyHost())
                Show(NP_HOST);
            else
            {
                CE_LobbyStatus status;
                CE_LobbyStatusGet(&status);
                // Keep the rejected address fixed for this attempt. Refresh only after rejection,
                // so a changed interface list cannot silently select a different address.
                RefreshHostAddresses();
                if (mHostAddressCount < 2) mList[NP_HOST_CHOOSE]->mSelected = 1;
                Notice(status.notice);
            }
            break;

            }
            case NA_JOIN:
                if (mPage != NP_CONNECTION) break;
		{
			char why[200];
			if (!ConnectionAvailable(why, sizeof why)) { Notice(why); break; }
			mOutcome[0] = 0;
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
			Notice(s.code[0] && CE_LobbyCopy(s.code) ? "Private join code copied. Share it only through your agreed private channel." :
				   "Copy failed. If a code is shown, share it only through your private channel.");
			break;
		}
		case NA_CODE:
			((CMenuOptionsList *)mList[NP_JOIN])->mSelected = 1;
			break;
		case NA_ADDRESS:
			((CMenuOptionsList *)mList[NP_JOIN])->mSelected = 2;
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
		CE_LobbyUpdate();
		CE_LobbyStatus s;
		CE_LobbyStatusGet(&s);
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
			if (s.state == CE_LOBBY_READY)
				Show(NP_LOBBY);
			else if (s.state == CE_LOBBY_OFF || s.state == CE_LOBBY_FAILED)
			{
				if (s.state == CE_LOBBY_FAILED) CE_LobbyLeave();
				Show(NP_MENU);
				Notice(s.notice);
			}
			break;
		case NP_JOIN:
			if (s.state == CE_LOBBY_READY || s.state == CE_LOBBY_REFUSED)
				Show(NP_LOBBY);
			else if (s.state == CE_LOBBY_FAILED || s.state == CE_LOBBY_LEFT)
			{
				Notice(s.notice);
				CE_LobbyLeave();
			}
			break;
		case NP_LOBBY:
			if (s.host && s.state == CE_LOBBY_FAILED)
			{
			    CE_LobbyLeave();
			    Show(NP_MENU);
			    Notice(s.notice);
			}
			else if (s.host && (s.state == CE_LOBBY_HOST_WAITING || s.state == CE_LOBBY_HOST_CONNECTING))
				Show(NP_HOST);		// the other player left: wait for another, with the same code
			else if (!s.host && (s.state == CE_LOBBY_LEFT || s.state == CE_LOBBY_FAILED))
			{
				CE_LobbyLeave();
				Show(NP_JOIN);
				Notice(s.notice);
			}
			else if (s.state == CE_LOBBY_OFF)
				Show(NP_MENU);
			break;
		}
		bool ready = s.state == CE_LOBBY_READY;
		mStart->mEnabled = (ready && s.host) ? TRUE : FALSE;
        mCampaignStart->mEnabled = s.host && s.state == CE_LOBBY_READY;
        mCampaignMissionPendingKnown = mPage == NP_LOBBY;
        if (mCampaignMissionPendingKnown) mCampaignMissionPending = s.level;

		mStart->Set(s.host ? "Start" : "Start (the host starts the game)");
		// The host chooses the map among the co-op levels; the guest sees the host's choice.
		const char *map = s.level == 856 ? "Building Defence (level 856)" : s.level == 858 ? "Building Defence (level 858)" : s.level == 100 ? "Tutorial (level 100)" : "a level";
		char mapText[80];
		snprintf(mapText, sizeof mapText, s.host ? "Map: %s (select to change)" : "Map: %s (the host chooses)", map);
		mMap->Set(mapText);
		mMap->mEnabled = (s.host && s.state != CE_LOBBY_REFUSED) ? TRUE : FALSE;
		if (mPage == NP_LOBBY && !mList[NP_LOBBY]->mItems.At(mList[NP_LOBBY]->mSelected)->IsEnabled())
			mList[NP_LOBBY]->mSelected = s.host && ready ? 1 : (mList[NP_LOBBY]->mItems.Size() - 1);
		char why[200];
		BOOL available = MenuAvailable(why, sizeof why) ? TRUE : FALSE;
        const BOOL selected_available = available && mConnectionSelected
            && mConnectionRelay == RelayRequested() ? TRUE : FALSE;
        mHostItem->mEnabled = mJoinItem->mEnabled = selected_available;
        mLocalItem->mEnabled = available && !RelayRequested() ? TRUE : FALSE;
        mOnlineItem->mEnabled = available && RelayRequested() ? TRUE : FALSE;
        mBrowseItem->mEnabled = FALSE;
        if (mPage == NP_MENU && !mList[NP_MENU]->mItems.At(mList[NP_MENU]->mSelected)->IsEnabled())
            mList[NP_MENU]->mSelected = available ? (RelayRequested() ? 1 : 0) : 3;
		bool connecting = s.state == CE_LOBBY_JOIN_CONNECTING;
		mConnect->Set(connecting ? "Connecting..." : "Connect");
		mConnect->mEnabled = connecting ? FALSE : TRUE;
		mCode->mEnabled = mPasteCode->mEnabled = mClearFields->mEnabled = connecting ? FALSE : TRUE;
		mAddress->mEnabled = mPasteAddress->mEnabled = !connecting && !RelayRequested();
		if (RelayRequested()) mAddress->Set("Address: not used by private exchange");
		else mAddress->Refresh();
	}

	// The page's words above its list.
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
                y = Lines("Multiplayer", y, kWhite);
                y = Lines("Import an approved Online invitation to select native relay, or choose Local / direct. Import shows a public preview before selecting the private pair.", y, kGrey);
                y = Lines("Public lobby browsing is unavailable.", y, kWarn);
                break;
            }

            if (SignalingSelected() && RelayRequested()) {
                y = Lines("Multiplayer", y, kWhite);
                y = Lines("Online uses the selected approved private pair. Host or Join opens network disclosure and explicit pair consent before connecting.", y, kGrey);
                y = Lines("Public lobby browsing is unavailable in this build.", y, kWarn);
                if (mNotice[0]) Lines(mNotice, y, kWarn);
                break;
            }

		{
			// Something to tell first (what happened in the last game, a note, why the lobby is not available) takes the top of the page, and the page's own words shrink to the warning, so
			// that nothing runs into the list below: the full text is 13 lines, about 245 pixels of the 256 above the list.
			char why[200];
			const bool available = MenuAvailable(why, sizeof why);
			if (mOutcome[0])
				y = Lines(mOutcome, y, mOutcomeGood ? kGood : kBad) + 10.f;
			if (mNotice[0])
				y = Lines(mNotice, y, kWarn) + 10.f;
			if (!available)
				y = Lines(why, y, kWarn) + 10.f;
			y = Lines("Choose Local or Online for the connection prepared at launch.", y, kWhite);
                y = Lines(RelayRequested() ? "Online relay pilot is selected for this launch."
                                          : "Local / direct connection is selected for this launch.", y + 5.0f, kWhite);
                Lines("Public browsing is unavailable. Use Host or Join with a code.", y + 5.0f, kWarn);
                break;
            }
            case NP_CONNECTION:
            if (SignalingSelected()) {
                y = Lines("Online approved pair", y, kWhite);
                y = Lines("Host creates a private CE code; Join uses the approved host's code. Network disclosure and pair consent come before the connection.", y, kGrey);
                y = Lines("The selected setup fixes the transport and approved partner. Public lobby browsing is unavailable.", y, kGrey);
                if (mNotice[0]) Lines(mNotice, y, kWarn);
                break;
            }

		{
			// Something to tell first (what happened in the last game, a note, why the lobby is not available) takes the top of the page, and the page's own words shrink to the warning, so
			// that nothing runs into the list below: the full text is 13 lines, about 245 pixels of the 256 above the list.
			char why[200];
			const bool available = ConnectionAvailable(why, sizeof why);
			const bool said = mOutcome[0] || mNotice[0] || !available;
			if (mOutcome[0])
				y = Lines(mOutcome, y, mOutcomeGood ? kGood : kBad) + 10.f;
			if (mNotice[0])
				y = Lines(mNotice, y, kWarn) + 10.f;
			if (!available)
				y = Lines(why, y, kWarn) + 10.f;
			y = Lines("EXPERIMENTAL: co-op over a network is new and unfinished. Expect problems.", y, kWarn);
			if (RelayRequested())
			{
				y = Lines("Two-player relay pilot. The host's code and manual file exchange are required.", y + 4.f, kWhite);
				if (!said)
					Lines("Have the operator and your confidential authenticated file channel ready before Host or Connect. Each relay setup has a two-minute limit. See the help below.", y + 4.f, kWhite);
			}
			else if (!said)
			{
				y = Lines("Two players, each with their own copy of the game and the same Onslaught CE version. A co-op game does not change your careers. "
						  "There is no pause in a network game and Escape does nothing; to leave, close the window (the other player is told).", y + 4.f, kWhite);
				Lines("There is no matchmaking server: play on one network, or read " CE_LOBBY_HELP_LABEL " (below).", y + 4.f, kWhite);
			}
			break;
		}
        case NP_INVITATION_INPUT:
        {
            y = Lines("Online invitation", y, kWhite);
            y = Lines("Use the address and invite folder sent to you.", y, kGrey);
            y = Lines("Choose Host or Join, then review your invitation.", y, kGrey);
            if (mInvitation.state == CE_INVITATION_CHOOSING)
                y = Lines("Waiting for folder selection. Cancel import stops this attempt.", y, kWarn);
            else y = Lines("Import stays offline. Continue later contacts the service.", y, kGrey);
            if (mNotice[0]) Lines(mNotice, y, kWarn);
            break;
        }
        case NP_INVITATION_PREVIEW:
        {
            y = Lines("Review invitation metadata", y, kWhite);
            if (InvitationPreviewValid()) {
                y = Lines(mInvitation.role == CE_NET_HOST ? "Role: Host" : "Role: Join", y, kWarn);
                y = Lines(mInvitation.origin, y, kWarn);
                const uint64_t now = CE_HandoffNow();
                const uint64_t left = mInvitation.expires_at > now ? mInvitation.expires_at - now : 0;
                char expiry[128];
                snprintf(expiry, sizeof expiry, "Pair approval expires in %llu min.",
                    (unsigned long long)(left / 60 + (left % 60 != 0)));
                y = Lines(expiry, y, kGrey);
                y = Lines("Use invitation imports this role's private admission. The next disclosure Continue is the first service contact.", y, kGrey);
            } else y = Lines("This preview is unavailable or expired. Choose the folder again before importing.", y, kWarn);
            if (mNotice[0]) Lines(mNotice, y, kWarn);
            break;
        }
        case NP_SIGNAL_DISCLOSURE:
        {
            const char* origin = SignalingOrigin();
            const char* destination = origin[0] ? origin : "Approved HTTPS origin is unavailable.";
            const char* head = "Review Online network access";
            const char* body = "Online contacts the approved HTTPS signaling service below to bind this private pair and exchange protected connection records. Accepted connections use Cloudflare TURN relay access.";
            const char* steps = "Continue contacts this service. Accept on the next page permits relay setup. Cancel closes the selected pair.";
            if (Height(head) + Height(body) + Height(destination) + Height(steps) > ListTop(mList[mPage]) - y)
                body = "Continue contacts this HTTPS signaling service. Accept next permits this approved pair's Cloudflare TURN relay setup.";
            y = Lines(head, y, kWhite);
            y = Lines(body, y, kGrey);
            y = Lines(destination, y, kWarn);
            y = Lines(steps, y, kGrey);
            if (mNotice[0]) Lines(mNotice, y, kWarn);
            break;
        }
        case NP_SIGNAL_STATUS:
        {
            const CeIceSignalView view = SignalingView();
            y = Lines(SignalHeading(view.status), y, kWhite);
            y = Lines(SignalDetail(view.status), y, kGrey);
            if (view.pairExpiresAt) {
                const uint64_t now = CE_HandoffNow();
                const uint64_t pairLeft = view.pairExpiresAt > now ? view.pairExpiresAt - now : 0;
                const uint64_t setupLeft = view.setupExpiresAt > now ? view.setupExpiresAt - now : 0;
                char expiry[160];
                snprintf(expiry, sizeof expiry, "Pair approval: %llu min remaining. Setup window: %llu sec remaining.",
                    (unsigned long long)(pairLeft / 60 + (pairLeft % 60 != 0)), (unsigned long long)setupLeft);
                y = Lines(expiry, y, kGrey);
            }
            if (mNotice[0]) Lines(mNotice, y, kWarn);
            break;
        }
        case NP_HOST_CHOOSE:
        {
            const char* intro = "Choose the address the joining PC can reach. Select Address to change it, then host.";
            const char* manual = "If no address is listed, host with a manual address and give it to the joining player separately.";
            float y = ListTop(mList[NP_HOST_CHOOSE]) - Height(intro) - Height(manual) - Height(mNotice) - .04f;
            y = Lines(intro, y, DWORD(-1));
            y = Lines(manual, y, DWORD(-1));
            Lines(mNotice, y, DWORD(-1));
            break;
        }
		case NP_HOST:
		{
			if (RelayRequested())
			{
				const char *head = SignalingSelected() ? SignalHeading(SignalingView().status) : ("Two-player relay pilot (experimental)");
				const char *label = "Join code: share only with the intended player";
				const char *waiting = SignalingSelected() ? SignalConnectionDetail() : (s.state == CE_LOBBY_HOST_CONNECTING ? "Private connection setup in progress..." : "Waiting for private setup or the invited player...");
				const char *explain = SignalingSelected() ? "Share the private CE code only with the approved partner. Connection records exchange through the selected service." : ("Transfer the new lobby files between both PCs. The game does not send them. The game connection uses a separate prepared folder.");
				const char *note = mNotice[0] ? mNotice : s.notice;
				const float need = Height(head) + 8.f + Height(label) + 2.f + Height(s.code, FONT_NORMAL, kCodeWidth) + 8.f + Height(waiting) + (*note ? 4.f + Height(note, FONT_SMALL, kNoticeWidth) : 0.f);
				const bool explained = y + need + 8.f + Height(explain) <= ListTop(mList[NP_HOST]) - 6.f;
				y = Lines(head, y, kWarn);
				y = Lines(label, y + 8.f, kWhite);
				y = Lines(s.code, y + 2.f, kWarn, FONT_NORMAL, kCodeWidth);
				y = Lines(waiting, y + 8.f, kWhite);
				if (*note) y = Lines(note, y + 4.f, kWarn, FONT_SMALL, kNoticeWidth);
				if (explained) Lines(explain, y + 8.f, kGrey);
				break;
			}
			char addr[4][16];
			int n = CE_LobbyAddresses(addr, 4);
			snprintf(t, sizeof t, "This PC: %s%s%s%s%s%s%s, UDP port %u", n > 0 ? addr[0] : "no address", n > 1 ? ", " : "",
					 n > 1 ? addr[1] : "", n > 2 ? ", " : "", n > 2 ? addr[2] : "", n > 3 ? ", " : "", n > 3 ? addr[3] : "",
					 (unsigned)s.port);
			const char *head = "Experimental: both need the same version.";
			const char *waiting = s.state == CE_LOBBY_HOST_CONNECTING ? "A player is connecting..." : "Waiting for a player...";
			const char *note = mNotice[0] ? mNotice : s.notice;
			char explain[240];
			snprintf(explain, sizeof explain, kHostForward, (unsigned)s.port);
			// The code, who is awaited and the notice stay; the explanation of port forwarding is the first to go when they do not all fit above the list, and the firewall line goes before it.
			const float need = Height(head) + 4.f + Height(t) + 8.f + Height("Join code (give it to the other player)") + 2.f + Height(s.code, FONT_NORMAL, kCodeWidth) + 8.f + Height(waiting) + (*note ? 4.f + Height(note, FONT_SMALL, kNoticeWidth) : 0.f);
			const float room = ListTop(mList[NP_HOST]) - 6.f;
			const bool explained = y + need + 8.f + Height(explain) <= room;
			const bool warned = explained && y + need + 8.f + Height(explain) + 4.f + Height(kHostFirewall) <= room;
			y = Lines(head, y, kWarn);
			y = Lines(t, y + 4.f, kGrey);
			y = Lines("Join code (give it to the other player)", y + 8.f, kWhite);
			y = Lines(s.code, y + 2.f, kWarn, FONT_NORMAL, kCodeWidth);
			y = Lines(waiting, y + 8.f, kWhite);
			if (*note)
				y = Lines(note, y + 4.f, kWarn, FONT_SMALL, kNoticeWidth);
			if (explained)
				y = Lines(explain, y + 8.f, kGrey);
			if (warned)
				Lines(kHostFirewall, y + 4.f, kGrey);
			break;
		}
		case NP_JOIN:
		{
			const char *intro = SignalingSelected() ? "Enter the approved host's private CE code. Connecting first requires network disclosure and explicit approval of the pair." : (RelayRequested() ? "Two-player relay pilot: paste or type the host's code, then Connect. Transfer files between the prepared lobby folders. The address field is not used." :
							"Type the host's code, or choose Paste code. Over the internet, also type the host's public address "
								"(the code holds the host's home address).");
			const char *shorter = SignalingSelected() ? "Use the approved host's CE code. Online requires pair consent." : (RelayRequested() ? "Two-player relay pilot: enter the host's code, then Connect. Transfer the private lobby files." :
							  "Type the host's code, or choose Paste code. Over the internet, also type the host's public address.");
			const int r = CE_CodeDecode(mCode->mValue, NULL);
			const char *problem = mCode->mValue[0] ? CE_CodeProblem(r) : "";
			char connecting[120] = "";
			if (s.state == CE_LOBBY_JOIN_CONNECTING)
			{
				if (RelayRequested()) snprintf(connecting, sizeof connecting, "%s", "Waiting for lobby files and the other player...");
				else snprintf(connecting, sizeof connecting, "Connecting to %s...", s.endpoint);
			}
			// The words below the introduction (the code's problem, the connection, the notice) are what the player needs; the parenthesis of the introduction goes when they do not fit above the list.
			const float rest = (*problem ? 10.f + Height(problem) : 0.f) + (*connecting ? 10.f + Height(connecting) : 0.f) + (mNotice[0] ? 10.f + Height(mNotice, FONT_SMALL, kNoticeWidth) : 0.f);
			y = Lines(y + Height(intro) + rest <= ListTop(mList[NP_JOIN]) - 6.f ? intro : shorter, y, kWhite);
			if (*problem)
				y = Lines(problem, y + 10.f, r == CE_CODE_OK ? kGood : kWarn);
			if (*connecting)
				y = Lines(connecting, y + 10.f, kWhite);
			Lines(mNotice, y + 10.f, kWarn, FONT_SMALL, kNoticeWidth);
			break;
		}
		case NP_LOBBY:
		{
			if (s.state == CE_LOBBY_STARTING)
			{
				Lines("Starting...", 200.f, kWhite, FONT_NORMAL);
				Lines("The window closes and opens again as the game starts.", 240.f, kGrey);
				break;
			}
			const char *peer = s.peer_version[0] ? s.peer_version : "...";
			snprintf(t, sizeof t, "Player 1 (host%s): Onslaught CE %s", s.host ? ", you" : "", s.host ? s.local_version : peer);
			y = Lines(t, y, kWhite);
			snprintf(t, sizeof t, "Player 2%s: Onslaught CE %s", s.host ? "" : " (you)", s.host ? peer : s.local_version);
			y = Lines(t, y, kWhite);
			bool ok = s.state == CE_LOBBY_READY;
			y = Lines(s.compat[0] ? s.compat : "Checking...", y + 8.f, ok ? kGood : kBad);
			if (ok && !s.host)
				y = Lines("Waiting for the host to start.", y + 8.f, kWhite);
			if (s.state == CE_LOBBY_REFUSED)
				y = Lines("Both players need the same Onslaught CE download. Leave, update, and join again.", y + 8.f, kGrey);
			const char *note = mNotice[0] ? mNotice : s.notice;
			const char *standing = "Experimental. If the two games ever disagree, the game ends with a message. There is no pause in the game: to leave, close the window.";
			// The notice stays; the standing words are the first to go when both do not fit above the list.
			if (y + 8.f + Height(standing) + (*note ? 8.f + Height(note, FONT_SMALL, kNoticeWidth) : 0.f) <= ListTop(mList[NP_LOBBY]) - 6.f)
				y = Lines(standing, y + 8.f, kGrey);
			Lines(note, y + 8.f, kWarn, FONT_SMALL, kNoticeWidth);
			break;
		}
		case NP_HELP:
            if (SignalingSelected()) {
                y = Lines("Online approved-pair help", y, kWhite);
                y = Lines("Host or Join first shows the approved HTTPS service and Cloudflare TURN relay disclosure. Continue contacts that service; Accept permits this private pair's relay setup.", y, kGrey);
                y = Lines("Share the private CE code only with the approved partner. Connection records are exchanged through the selected service.", y, kGrey);
                y = Lines("Retry is deliberate and keeps the fixed deadline. Closed, expired or uncertain issuance needs a fresh reviewed setup. Public browsing is unavailable.", y, kWarn);
                break;
            }

		{
			if (RelayRequested())
			{
				y = Lines("Two-player relay pilot (manual file exchange)", y, kWarn, FONT_SMALL, kHelpWidth) + 6.f;
				y = Lines("Before launch, an operator needs two different, empty private folders per PC: lobby and game. Use the same build.", y, kWhite, FONT_SMALL, kHelpWidth) + 6.f;
				y = Lines("Host privately shares the join code. Guest enters it and chooses Connect. The operator transfers request, offer and answer files; repeat in the matching game folders after Start.", y, kWhite, FONT_SMALL, kHelpWidth) + 6.f;
				Lines("Offer parcels contain temporary credentials. Use a confidential authenticated channel: base64 is not encryption. A local export is not delivery. Copy each file completely before import. Keep filenames and files private. Another connection attempt or mission needs fresh folders and a relaunch. No files are sent automatically. Internet play has not been verified.", y, kGrey, FONT_SMALL, kHelpWidth);
				break;
			}
			y = Lines(CE_LOBBY_HELP_LABEL, y, kWarn, FONT_SMALL, kHelpWidth) + 6.f;
			snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());
			y = Lines(t, y, kWhite, FONT_SMALL, kHelpWidth) + 4.f;
			y = Lines(kHelpFirewall, y, kWhite, FONT_SMALL, kHelpWidth) + 4.f;
			y = Lines(kHelpAddress, y, kWhite, FONT_SMALL, kHelpWidth) + 8.f;
			y = Lines(kHelpPrivate, y, kGrey, FONT_SMALL, kHelpWidth) + 4.f;
			Lines(kHelpUntested, y, kGrey, FONT_SMALL, kHelpWidth);
			break;
		}
		}
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
		if (PLATFORM.GetSysTimeFloat() - mOpenTime > 0.2f)
		{
			// Commit the intent only after the mission row has been rendered.
			mCampaignMissionKnown = mPage == NP_LOBBY && mCampaignMissionPendingKnown;
			if (mCampaignMissionKnown) mCampaignMission = mCampaignMissionPending;
			DrawText();
			CE_LobbyStatus s;
			CE_LobbyStatusGet(&s);
			if (s.state != CE_LOBBY_STARTING)
				mLists.At(mPage)->Render(this);
		}
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
	}

	bool mConnectionSelected, mConnectionRelay;
    bool mSignalApproved, mSignalBegan;
    int mSignalAction, mSignalReturnPage;
    char mSignalOrigin[512];
    CE_InvitationView mInvitation;
    uint32_t mInvitationRole;
    CNetField* mImportOrigin;
    CNetItem* mImportAddressLabel;
    int mImportOriginRow;
    CNetItem *mInvitationEntry, *mImportPaste, *mImportHost, *mImportJoin, *mImportUse;
    CNetItem *mImportClearInput, *mImportClearPreview;

    CNetItem *mSignalContinue, *mSignalAccept, *mSignalProceed, *mSignalRetry, *mSignalClose;


	CNetItem *mLocalItem, *mOnlineItem, *mBrowseItem;

	CMenuOptionsList *mList[NP_COUNT];
	CNetItem	*mHostItem, *mJoinItem, *mConnect, *mMap, *mStart;
	CNetItem *mPasteCode, *mPasteAddress, *mClearFields;
    CNetItem *mHostAddress, *mHostBegin;
    CNetItem *mCampaignStart;
    uint32_t mCampaignMission;
    uint32_t mCampaignMissionPending;
    bool mCampaignMissionPendingKnown;
    bool mCampaignMissionKnown;

    char mHostAddresses[CE_LOBBY_ADDRESS_CHOICES][16];
    int mHostAddressCount, mHostAddressIndex;

	CNetField	*mCode, *mAddress;
	char		mNotice[320], mOutcome[256];
	int			mOutcomeGood;
	float		mStartingAt;
	bool		mQuitPosted;
};

// The join page's keyboard: typing into the selected field, Ctrl+V pasting into it, and the page's own keys (with a key
// trap set, the game's key bindings do not reach the menus: CFEPVirtualKeyboard's name entry works the same way).
void	NetKeyTrap(WORD key, KeyEventType event)
{
	if (!sMenu || !sActive)
		return;
	if (key == DIK_LCONTROL || key == DIK_RCONTROL)
	{
		if (event == KEY_EVENT_DOWN)
			sCtrl = true;
		else if (event == KEY_EVENT_UP)
			sCtrl = false;
		return;
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
}

}

//*********************************************************************************
// The CE page's side (ce_frontend.cpp).
bool	PortCeNetOffered()
{
	if (!CE_NETWORK_ENABLED || !PortCeMenus())
		return false;
	static const char *const names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
		"BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_RECORD", "BEA_NET_HOST", "BEA_NET_JOIN",
		"BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN"};
	for (const char *n : names)
	{
		const char *v = getenv(n);
		if (v && *v)
			return false;
	}
	return !CLIPARAMS.mRecordDemo && !CLIPARAMS.mPlaybackDemo && !CLIPARAMS.mAutoConfigTest;
}

bool	PortCeNetActive()
{
	return sActive && sMenu;
}

void	PortCeNetOpen()
{
	if (!PortCeNetOffered())
		return;
	if (!sMenu)
		sMenu = new (MEMTYPE_MENUITEM) CCeNetMenu();
	CPauseMenu::mOptionsPage = true;
	sMenu->Activate(0);
	sActive = true;
	sMenu->mNotice[0] = 0;
	char text[256];
	int good = 1;
	if (CE_LobbyTakeOutcome(text, sizeof text, &good))
	{
		snprintf(sMenu->mOutcome, sizeof sMenu->mOutcome, "%s", text);
		sMenu->mOutcomeGood = good;
	}
	sMenu->Show(NP_MENU);
}

void	PortCeNetButton(SINT button, float val)
{
	if (PortCeNetActive())
		sMenu->ReceiveButtonAction(NULL, button, val);
}

void	PortCeNetProcess()
{
	if (PortCeNetActive())
		sMenu->Update();
}

void	PortCeNetDraw()
{
	if (PortCeNetActive())
		sMenu->Draw();
}

// The CE page is going: nothing of the lobby outlives it.
void	PortCeNetShutdown()
{
	if (sMenu)
	{
		sMenu->Trap(false);
		CE_LobbyShutdown();
		SAFE_DELETE(sMenu);
	}
	sActive = false;
}

bool	PortCeNetOpenOnEntry()
{
	bool open = sOpenOnEntry;
	sOpenOnEntry = false;
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
