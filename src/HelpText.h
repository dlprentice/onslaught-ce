// HelpText.h: the help text display, the script's help messages shown over the HUD (reconstructed; 0x1c bytes).

#ifndef HELPTEXT_H
#define HELPTEXT_H

#define MAX_HELP_TEXT_MESSAGES	2				// CHelpTextDisplay::AddMessage's scan (0047fb0f)

struct SHelpTextMessage
{
	SHelpTextMessage() { mActive = FALSE; }	// built before the vtable is set (0047fabb)

	WCHAR	*mText;								// +0x0
	float	mTime;								// +0x4: EVENT_MANAGER's time when it was added
	BOOL	mActive;							// +0x8
};

class CHelpTextDisplay
{
public:
	CHelpTextDisplay();							// 0047fab0 (0x1c bytes: CGame::InitRestartLoop 0046c741)
	virtual ~CHelpTextDisplay() {}				// RTTI: vtable 005dbdf8, its only slot

	void	AddMessage(WCHAR *text);			// 0047fb00 (IScript::AddHelpMessage 00533b5d)
	void	Render();							// 0047fb50 (CHud::RenderOverlayForViewpoint 00487b83)

	SHelpTextMessage	mMessages[MAX_HELP_TEXT_MESSAGES];	// +0x04
};

#endif
