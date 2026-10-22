// FEPControls.cpp: the PC controls page's key-binding editor, CControllerDefinition (a menu item, RTTI; vtable
// 005db404). Not in Stuart's release: reconstructed from retail. The object runs from its maths-constant pair
// (00453370) to 0045668f; the file name is ours, from its place between FEPCommon.obj and FEPDebriefing.obj.

#include "common.h"
#include "MenuItem.h"
#include "Controller.h"
#include "Platform.h"
#include "Texture.h"
#include "DebugLog.h"		// init:ZERO_FVECTOR, init:ID_FMATRIX
#include "DX.h"
#include "ltshell.h"
#include "Career.h"
#include "game.h"
#include "frontend.h"
#include "SpriteRenderer.h"
#include "mouse.h"
#include "TransitionHelpers.h"

#include "ControllerDefinition.h"

// FUNCTION: 0x00453370 init:ZERO_FVECTOR
// FUNCTION: 0x00453390 init:ID_FMATRIX

// The keyboard and mouse presets, each ended by an empty binding; an empty preset means the joystick (00677af0).
// FUNCTION: 0x00453460 init:sKeyPresets
static SControlBinding sKeyPresets[] =
{
	SControlBinding(true, 25, 12, 0, 9, 36, 0, 74),
	SControlBinding(true, 27, 11, 0, 9, 38, 0, 76),
	SControlBinding(true, 26, 11, 1, 9, 23, 0, 73),
	SControlBinding(true, 28, 12, 1, 9, 37, 0, 75),
	SControlBinding(true, 29, 9, 203, 9, 30, 0, 65),
	SControlBinding(true, 30, 9, 205, 9, 32, 0, 68),
	SControlBinding(true, 31, 9, 200, 9, 17, 0, 87),
	SControlBinding(true, 32, 9, 208, 9, 31, 0, 83),
	SControlBinding(true, 59, 8, 54, 8, 15, 0, 0),
	SControlBinding(true, 33, 8, 82, 8, 57, 0, 0),
	SControlBinding(true, 19, 15, 0, 9, 58, 0, 0),
	SControlBinding(true, 18, 17, 0, 10, 58, 0, 0),
	SControlBinding(true, 20, 16, 2, 8, 39, 0, 0),
	SControlBinding(true, 16, 16, 4, 8, 13, 0, 43),
	SControlBinding(true, 17, 16, 3, 8, 12, 0, 45),
	SControlBinding(true, 21, 9, 157, 9, 42, 0, 0),
	SControlBinding(),
	SControlBinding(),
};

// The joystick's bindings, copied into either input of the matching controls (006778d0).
// FUNCTION: 0x00453630 init:sJoyBindings
static SControlBinding sJoyBindings[] =
{
	SControlBinding(true, 25, 5, -3, 0),
	SControlBinding(true, 27, 4, -3, 0),
	SControlBinding(true, 26, 4, -4, 0),
	SControlBinding(true, 28, 5, -4, 0),
	SControlBinding(true, 29, 5, -1, 0),
	SControlBinding(true, 30, 4, -1, 0),
	SControlBinding(true, 31, 5, -2, 0),
	SControlBinding(true, 32, 4, -2, 0),
	SControlBinding(true, 59, 1, 0, 0),
	SControlBinding(true, 33, 1, 2, 0),
	SControlBinding(true, 19, 0, 7, 0),
	SControlBinding(true, 18, 2, 7, 0),
	SControlBinding(true, 20, 1, 6, 0),
	SControlBinding(true, 16, 0, 12, 0),
	SControlBinding(true, 17, 0, 14, 0),
	SControlBinding(true, 21, 0, 1, 0),
	SControlBinding(),
};

static SControlBinding	*sPresetPtr = sKeyPresets;			// 006290b0
int		gControlPreset = 0;										// 00677d70 (name ours)

inline void ApplyJoyBindings(int input, int pad)
{
	for (SControlBinding *e = sJoyBindings; e->mId != -1; e++)
	{
		SControlBinding *b = FindControlBinding(e->mId);
		if (b)
		{
			b->mInput[input] = e->mInput[0];
			b->mInput[input].mPad = pad;
		}
	}
}

void ApplyControlPreset(int preset);
void ClearDuplicateBinding(int pad, short key, int type);
void ForEachFixedKey(int action, void *context, void (*callback)(void *context, int key, int type));
int GetActionGroup(int action);
static void RecordFixedKey(void *context, int button, int type);
static void RemapFixedKey(void *context, int button, int type);
void RemapKey(int action, int pad);
void SetControlBindingInput(int index, int button, int pad, int type, short key, short vkey);


bool	gKeytrapReleased = true;							// 006290b4 (name ours): cleared while a key is captured

// FUNCTION: 0x004539b0 ??_GCControllerDefinition@@UAEPAXI@Z






// The remap capture's state, and ForEachFixedKey's report for the bindings list: the button of the last fixed key
// (0067787c) and its type (00677878). Defined in this order and sKeyDown initialized: retail's .bss order
// (placement evidence).
static WCHAR	sKeyName[40];								// 00677880 (name ours): a key's name built from parts (retail's slot is 80 bytes)
static int		sFixedButton;
static int		sFixedType;
// 00677874: the captured key; one object, since retail loads the character as a dword but the key as a word (004540c0)
static struct
{
	WORD	mChar;											// +0: its character
	WORD	mKey;											// +2
} sRemapKey;
static int		sRemapType;									// 00677870: the type of the input being remapped
static int		sRemapAction;								// 0067786c
static int		sRemapInput;								// 00677868
static float	sAxisRest[MAX_JOYPADS][6];					// 00677808: each pad's axes when the capture began
static bool		sKeyDown = false;									// 00677d74
extern WORD		gCharScanCode;								// 00855420 (ltshell.cpp): the scan code of the WM_CHAR being trapped


// Binds the captured input (sRemapKey, sRemapType) to an action: RemapKey's directions (its default writes these three
// calls out) and the key trap's last step. Render's button capture (00453ad0) does the same inline; written as this
// call there, one address operand swaps (1 byte), so it stays written out. The (DWORD) cast is part of RemapKey's
// size fit (see there).
inline void BindCapturedInput(int action, int pad)
{
	ClearDuplicateBinding(pad, sRemapKey.mKey, sRemapType);
	ForEachFixedKey(action, (void *)(DWORD)pad, RemapFixedKey);
	ApplyControlPreset(0);
}


static bool RemapKeyTrap(WORD key, KeyEventType event);


// Sets one input of a binding, first clearing the same key's other bindings in the action's group.
inline void ClearAndSetBindingInput(int index, int button, int pad, int type, short key, short vkey)
{
	ClearDuplicateBinding(pad, key, type);
	SetControlBindingInput(index, button, pad, type, key, vkey);
}









// The list's section headings (actions 55, 58, 63, 68, 71; 1 is never a row).
inline BOOL IsBindingsHeading(int action)
{
	switch (action)
	{
	case 1:
	case 55:
	case 58:
	case 63:
	case 68:
	case 71:
		return TRUE;
	}
	return FALSE;
}

// Never defined or called: it stands in for the joystick product-name query the list once made. VC6 keeps the
// DIPROPSTRING header stores (004557c3, 0045586b) only while the struct's address is taken somewhere.
void UseProperty(DIPROPHEADER *h);



// Loads the numbered control preset, then gives the joysticks whichever input the preset left free.
// FUNCTION: 0x00453780
void ApplyControlPreset(int preset)
{
	gControlPreset = preset;
	if (!preset)
		return;

	sPresetPtr = sKeyPresets;
	bool second = false;
	do
	{
		if (sPresetPtr->mId == -1)
		{
			sPresetPtr = sKeyPresets;
			for (int pad = 0; pad < LT.mJoypads; pad++)
			{
				if (LT.mUnknown33474[pad])
				{
					ApplyJoyBindings(0, pad);
					for (pad++; pad < LT.mJoypads; pad++)
					{
						if (LT.mUnknown33474[pad])
						{
							ApplyJoyBindings(1, pad);
							return;
						}
					}
					for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
						b->mInput[1].mPad = -1;
					return;
				}
			}
		}
		for (; sPresetPtr->mId != -1; sPresetPtr++)
		{
			SControlBinding *b = FindControlBinding(sPresetPtr->mId);
			if (b)
			{
				*b = *sPresetPtr;
				if (b->mInput[1].mPad != -1)
					second = true;
			}
		}
		sPresetPtr++;
	} while (preset-- > 1);

	if (!second)
	{
		for (int pad = 0; pad < LT.mJoypads; pad++)
		{
			if (LT.mUnknown33474[pad])
			{
				ApplyJoyBindings(1, pad);
				return;
			}
		}
	}
}
// FUNCTION: 0x00453970
CControllerDefinition::CControllerDefinition() : CMenuItem((EPCText)0x32, 3)
{
	mExpanded = false;
	mUnknown20 = 0;
	mTexture = NULL;
}
// FUNCTION: 0x004539d0
CControllerDefinition::~CControllerDefinition()
{
	if (!gKeytrapReleased)
	{
		gKeytrapReleased = true;
		PLATFORM.SetKeytrap(NULL);
	}
	SAFE_RELEASE(mTexture);
}
// FUNCTION: 0x00453ac0
void CControllerDefinition::ReceiveButtonAction(CController *from_controller, int button, float val)
{
}
//******************************************************************************************
// The page: the bindings in three columns, arrows (blinking) to scroll them, and while a remap is waiting, the first
// joystick button pressed or axis pushed past 0.6 from where it rested.
// FUNCTION: 0x00453ad0
void CControllerDefinition::Render(float x, float y, BOOL selected)
{
	float	top = y + 5.0f;
	for (int col = 0; col < 3; col++)
		RenderBindingsList(col, (col - 1.0f) / 0.00625f + x, top, selected);	// spelling chosen to give retail's reciprocal-named 160 (placement evidence); Stuart's release has no such line

	if (!mTexture)
		mTexture = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Arrow.tga", 0, 0, 1, 0, 1);

	if (mUnknown20 > 0)
	{
		if ((float)fmod(PLATFORM.GetSysTimeFloat(), 2.0) < 1.5f)
			CSPRITERENDERER::DrawColouredSprite(x, y + 15.0f, 0, mTexture, 0xffffffff, 1.0f, 1.0f, SA_CENTRE, SPT_NONE, 1.0f, -PI / 2);
		if ((selected) && (::MouseClickedInRect(x - 16.0f, top - 6.0f, x + 16.0f, y + 31.0f)))
		{
			CFrontEnd::PlaySound(FES_MOVE);
			mUnknown20--;
		}
	}
	if (mUnknown20 < -0.5f)
	{
		if ((float)fmod(PLATFORM.GetSysTimeFloat(), 2.0) < 1.5f)
			CSPRITERENDERER::DrawColouredSprite(x, y + 230.0f - 10.0f, 0, mTexture, 0xffffffff, 1.0f, 1.0f, SA_CENTRE, SPT_NONE, 1.0f, PI / 2);
		if ((selected) && (::MouseClickedInRect(x - 16.0f, y + 230.0f - 26.0f, x + 16.0f, y + 236.0f)))
		{
			CFrontEnd::PlaySound(FES_MOVE);
			mUnknown20++;
		}
	}

	if (gKeytrapReleased)
		return;

	for (int pad = 0; pad < LT.mJoypads; pad++)
	{
		for (int b = 0; b < 128; b++)
		{
			if (LT.JoyButtonOnce(pad, b))
			{
				sRemapKey.mKey = b;
				sRemapType = BUTTON_ONCE;
				for (SControlBinding *cb = CONTROL_BINDINGS; cb->mId != -1; cb++)
				{
					if (cb->mConfigurable)
					{
						for (int i = 0; i < 2; i++)
						{
							if (cb->mInput[i].mPad == pad && cb->mInput[i].mKey == (short)b &&
								GetActionGroup(BUTTON_ONCE) == GetActionGroup(cb->mInput[i].mType))
								cb->mInput[i].mPad = -1;
						}
					}
				}
				ForEachFixedKey(sRemapAction, (void *)pad, RemapFixedKey);
				ApplyControlPreset(0);
				gKeytrapReleased = true;
				PLATFORM.SetKeytrap(NULL);
				return;
			}
		}

		CController	*c = FRONTEND.mControllers[0];
		if (!c)
			c = GAME.GetController(0);
		float	axes[6];
		axes[0] = c->GetJoyAnalogueLeftX(pad);
		axes[1] = c->GetJoyAnalogueLeftY(pad);
		axes[2] = c->GetJoyAnalogueRightX(pad);
		axes[3] = c->GetJoyAnalogueRightY(pad);
		axes[4] = c->GetJoyPovX(pad);
		axes[5] = c->GetJoyPovY(pad);
		for (int key = -1; key >= -5; key -= 2)
		{
			float	d = axes[-1 - key] - sAxisRest[pad][-1 - key];
			if (d < -0.6f)
			{
				sRemapKey.mKey = key;
				sRemapType = ANALOGUE_MINUS;
				RemapKey(sRemapAction, pad);
				gKeytrapReleased = true;
				PLATFORM.SetKeytrap(NULL);
				return;
			}
			if (d > 0.6f)
			{
				sRemapKey.mKey = key;
				sRemapType = ANALOGUE_PLUS;
				RemapKey(sRemapAction, pad);
				gKeytrapReleased = true;
				PLATFORM.SetKeytrap(NULL);
				return;
			}
			d = axes[-key] - sAxisRest[pad][-key];
			if (d < -0.6f)
			{
				sRemapKey.mKey = key - 1;
				sRemapType = ANALOGUE_MINUS;
				RemapKey(sRemapAction, pad);
				gKeytrapReleased = true;
				PLATFORM.SetKeytrap(NULL);
				return;
			}
			if (d > 0.6f)
			{
				sRemapKey.mKey = key - 1;
				sRemapType = ANALOGUE_PLUS;
				RemapKey(sRemapAction, pad);
				gKeytrapReleased = true;
				PLATFORM.SetKeytrap(NULL);
				return;
			}
		}
	}
}
// Calls back for each key a front-end action is hard-wired to (the remapper and the bindings list pass
// 004540c0).
// FUNCTION: 0x00453f50
void ForEachFixedKey(int action, void *context, void (*callback)(void *context, int key, int type))
{
	switch (action)
	{
	case 59:	callback(context, 0x1f, KEY_ON);	break;
	case 60:	callback(context, 0x20, KEY_ON);	break;
	case 61:	callback(context, 0x1d, KEY_ON);	break;
	case 62:	callback(context, 0x1e, KEY_ON);	break;
	case 64:	callback(context, 0x1a, KEY_ON);	break;
	case 65:	callback(context, 0x1c, KEY_ON);	break;
	case 66:	callback(context, 0x19, KEY_ON);	break;
	case 67:	callback(context, 0x1b, KEY_ON);	break;
	case 69:	callback(context, 0x10, KEY_ON);	break;
	case 70:	callback(context, 0x11, KEY_ON);	break;
	case 72:
		callback(context, 0x12, KEY_RELEASE);
		callback(context, 0x13, KEY_ON);
		break;
	case 73:	callback(context, 0x14, KEY_RELEASE);	break;
	case 74:	callback(context, 0x21, KEY_ONCE);	break;
	case 75:	callback(context, 0x15, KEY_ON);	break;
	case 76:	callback(context, 0x3b, KEY_ONCE);	break;
	}
}
// Binds a key the front end hard-wires (ForEachFixedKey's callback) to the input being remapped, turning the key
// event into the same family as the input's type.
// FUNCTION: 0x004540c0
static void RemapFixedKey(void *context, int button, int type)
{
	switch (sRemapType)
	{
	case MOUSE_BUTTON_ONCE:
		switch (type)
		{
		case KEY_ONCE:		type = MOUSE_BUTTON_ONCE;	break;
		case KEY_ON:		type = MOUSE_BUTTON_ON;	break;
		case KEY_RELEASE:	type = MOUSE_BUTTON_RELEASE;	break;
		}
		break;
	case BUTTON_ONCE:
		switch (type)
		{
		case KEY_ONCE:		type = BUTTON_ONCE;	break;
		case KEY_ON:		type = BUTTON_ON;	break;
		case KEY_RELEASE:	type = BUTTON_RELEASE;	break;
		}
		break;
	case ANALOGUE_PLUS:
		switch (type)
		{
		case KEY_ONCE:		type = ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT;	break;
		case KEY_ON:		type = ANALOGUE_PLUS;	break;
		case KEY_RELEASE:	type = ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT;	break;
		}
		break;
	case ANALOGUE_MINUS:
		switch (type)
		{
		case KEY_ONCE:		type = ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT;	break;
		case KEY_ON:		type = ANALOGUE_MINUS;	break;
		case KEY_RELEASE:	type = ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT;	break;
		}
		break;
	}
	SetControlBindingInput(sRemapInput, button, (int)context, type, sRemapKey.mKey, sRemapKey.mChar);
}
// Binds the captured input to the action. An axis captured for one of the four directions of the look (64..67) or
// move (59..62) actions binds all four, the other directions taking the same axes the way round it implies.
// VC6 places the direction blocks after the default (as retail does), so in IL order the default comes last. The
// inliner's nested budgets then give retail's choices exactly: the clear is expanded in the default and at 9 of the
// 16 move-direction binds (the 4th of the first orientation, the 2nd and 3rd of the second, the 1st, 2nd and 4th of
// the third and fourth) and called at the other 23 binds. The conditions read the captured globals: retail keeps
// them in ax and ecx and spills them only in the default.
// The (WORD) casts give retail's 16-bit compares: a WORD x changes the loop's test, and WORD locals compute x - 1 once
// per pass where retail recomputes it in each test (00454206, 00454233).
// The eight cases are one else-if chain that shares the final ApplyControlPreset: retail pops the last bind's
// arguments before that call (add esp,0x2c at 004545af, 0x10 at 00454ddc), so the call starts a block of its own.
// With a call and a return in each case, VC6 merges the pops after it, the first orientation's last bind pushes the
// clear's arguments before its two stores (retail stores first, 00454586) and the cases' tails cross-jump into
// another copy. A `goto done` in each case to a final `done: ApplyControlPreset(0);` compiles the same as the chain.
// The default writes BindCapturedInput's three calls out. Its clear, inlined at top level, binds the type to a low
// symbol id (5, from the free list), so VC6's rank pass puts the GetActionGroup call on the binding's type first, as
// retail does (00454497). Through BindCapturedInput the type takes the previous copy's pad (id 0x2c9 in a variant
// that keeps the pattern, 4/6 shape/instructions), whose hash ranks the captured type's call first (it would rank
// lower only at ids up to 0x200 or 0x37f..0x600).
// STAND-IN (code-neutral size fit): the (DWORD) in `(void *)(DWORD)pad`, here and in BindCapturedInput, and the
// `for (int x ...)` declaration emit no code. Why: retail's pattern of inlined clears needs BindCapturedInput's size
// and this function's own size (VC6 units) at 50 and 1597..1598, 51 and exactly 1612, or 49 and 1582..1585; with
// the default written out, plain `(void *)pad` gives 50 and 1611 (395/447 shape/instructions), the cast in only
// one place 51 and 1611 or 50 and 1612, and `int x;` declared first 1609 (67/169). How: each (DWORD) adds 1 unit to
// its function, `for (int x ...)` 3. Render keeps its plain `(void *)pad` (either compiles the same there). Which of
// these, or what other source, Stuart wrote is unknown.
// FUNCTION: 0x004541e0
void RemapKey(int action, int pad)
{
	for (int x = ANALOGUE_X1; x >= ANALOGUE_POV_X; x -= 2)
	{
		if (((action == 64) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 65) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 66) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 67) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)))
		{
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(64, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(65, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(66, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(67, pad);
		}
		else if (((action == 64) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 65) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 66) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 67) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)))
		{
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(64, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(65, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(66, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(67, pad);
		}
		else if (((action == 64) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 65) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 66) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 67) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)))
		{
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(64, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(65, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(66, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(67, pad);
		}
		else if (((action == 64) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 65) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 66) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 67) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)))
		{
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(64, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(65, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(66, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(67, pad);
		}
		else if (((action == 59) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 60) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 61) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 62) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)))
		{
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(59, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(60, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(61, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(62, pad);
		}
		else if (((action == 59) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 60) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 61) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 62) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)))
		{
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(59, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(60, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(61, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(62, pad);
		}
		else if (((action == 59) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 60) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 61) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 62) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)))
		{
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(59, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(60, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(61, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(62, pad);
		}
		else if (((action == 59) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_PLUS)) ||
			((action == 60) && (sRemapKey.mKey == (WORD)x) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 61) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_MINUS)) ||
			((action == 62) && (sRemapKey.mKey == (WORD)(x - 1)) && (sRemapType == ANALOGUE_PLUS)))
		{
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(59, pad);
			sRemapKey.mKey = x;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(60, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_MINUS;
			BindCapturedInput(61, pad);
			sRemapKey.mKey = x - 1;
			sRemapType = ANALOGUE_PLUS;
			BindCapturedInput(62, pad);
		}
		else
			continue;

		ApplyControlPreset(0);
		return;
	}

	ClearDuplicateBinding(pad, sRemapKey.mKey, sRemapType);
	ForEachFixedKey(action, (void *)(DWORD)pad, RemapFixedKey);
	ApplyControlPreset(0);
}
// Which group of actions a binding belongs to (called by the remapper, 0045449b).
// FUNCTION: 0x00454e00
int GetActionGroup(int action)
{
	switch (action)
	{
	case 5:
	case 7:
		return 2;
	case 4:
	case 6:
		return 3;
	case 8:
	case 9:
	case 10:
		return 4;
	case 11:
	case 13:
		return 5;
	case 12:
	case 14:
		return 6;
	case 15:
	case 16:
	case 17:
		return 7;
	case 0:
	case 1:
	case 2:
	case 3:
		return 1;
	}
	return 1;
}
// Unbinds other controls using the same key for the same action group, with the button capture's condition (00453ad0
// writes the same scan out, its group folded). Its size in VC6's units, 154, is at most 174, so VC6 offers it for
// inlining; RemapKey's ten inlined copies need exactly 154 (the brace layouts without the hoisted i give 153..159).
// FUNCTION: 0x00454e90
void ClearDuplicateBinding(int pad, short key, int type)
{
	int		i;
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
	{
		if (b->mConfigurable)
		{
			for (i = 0; i < 2; i++)
				if (b->mInput[i].mPad == pad && b->mInput[i].mKey == key &&
					GetActionGroup(type) == GetActionGroup(b->mInput[i].mType))
					b->mInput[i].mPad = -1;
		}
	}
}
//******************************************************************************************
// One column of the bindings list: the actions' names (column 1) or their first or second input (columns 0 and 2),
// from row mUnknown20 down. A binding clicked starts its remap; the one being captured blinks.
// FUNCTION: 0x00455010
void CControllerDefinition::RenderBindingsList(int column, float x, float y, BOOL selected)
{
	float	ry = y;
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

	for (int row = mUnknown20; (row < 22) && (ry < y + 225.0f); row++, ry += 10.0f)
	{
		WCHAR	text[300] = L"";
		DWORD	col = 0xff7f7f7f;
		bool	capturing = false;
		int		action = row + 55;

		if (IsBindingsHeading(row + 55) || (column == 1))
		{
			int		id = action;
			if (action == 55)
			{
				switch (column)
				{
				case 0:	id = 0x33;	break;
				case 2:	id = 0x34;	break;
				}
			}
			else if (column != 1)
				continue;
			wcscpy(text, Localization_GetStringById(id));
		}
		else if (action == 56)
		{
			if (CAREER.GetInvertYAxis(column / 2))
				wcscpy(text, Localization_GetStringById(0x36));
			else
				wcscpy(text, Localization_GetStringById(0x35));
		}
		else if (action == 57)
		{
			if (CAREER.GetInvertJetYAxis(column / 2))
				wcscpy(text, Localization_GetStringById(0x36));
			else
				wcscpy(text, Localization_GetStringById(0x35));
		}
		else if ((sRemapAction == action) && (column / 2 == sRemapInput) && (!gKeytrapReleased))
		{
			capturing = true;
			wcscpy(text, L"[----]");
		}
		else
		{
			ForEachFixedKey(action, NULL, RecordFixedKey);
			SControlInput	*in = &FindControlBinding(sFixedButton)->mInput[column / 2];
			if ((!in) || (in->mPad == -1))
				wcscat(text, Localization_GetStringById(0x78));
			else
			{
				int		type = in->mType;
				switch (type)
				{
				case KEY_ONCE:
				case KEY_ON:
				case KEY_RELEASE:
				{
					WCHAR	vkey = in->mVKey;
					int		key = in->mKey;
					char	keyname[40];
					WCHAR	*name;
					if (GetKeyNameTextA(key << 16, keyname, 40))
					{
						if (strlen(keyname) == 1)
						{
							wcscpy(sKeyName, Localization_GetStringById(0x51));
							wcscat(sKeyName, L" ");
							wcscat(sKeyName, ToWCHAR(keyname));
							name = sKeyName;
						}
						else
							name = ToWCHAR(keyname);
					}
					else
					{
						switch (key)
						{
				case 0xdd:	name = Localization_GetStringById(0x79);	break;
				case 0xdc:	name = Localization_GetStringById(0x7a);	break;
				case 0xdb:	name = Localization_GetStringById(0x7b);	break;
				case 0x45:	name = Localization_GetStringById(0x7c);	break;
				case 0x01:	name = Localization_GetStringById(0x7d);	break;
				case 0x0e:	name = Localization_GetStringById(0x7e);	break;
				case 0x0f:	name = Localization_GetStringById(0x7f);	break;
				case 0x1c:	name = Localization_GetStringById(0x80);	break;
				case 0x1d:	name = Localization_GetStringById(0x81);	break;
				case 0x3a:	name = Localization_GetStringById(0x82);	break;
				case 0x27:	name = Localization_GetStringById(0x83);	break;
				case 0x28:	name = Localization_GetStringById(0x84);	break;
				case 0x29:	name = Localization_GetStringById(0x85);	break;
				case 0x2a:	name = Localization_GetStringById(0x86);	break;
				case 0x36:	name = Localization_GetStringById(0x87);	break;
				case 0x38:	name = Localization_GetStringById(0x88);	break;
				case 0x39:	name = Localization_GetStringById(0x89);	break;
				case 0xb8:	name = Localization_GetStringById(0x8a);	break;
				case 0x9d:	name = Localization_GetStringById(0x8b);	break;
				case 0x3b:	name = Localization_GetStringById(0x8c);	break;
				case 0x3c:	name = Localization_GetStringById(0x8d);	break;
				case 0x3d:	name = Localization_GetStringById(0x8e);	break;
				case 0x3e:	name = Localization_GetStringById(0x8f);	break;
				case 0x3f:	name = Localization_GetStringById(0x90);	break;
				case 0x40:	name = Localization_GetStringById(0x91);	break;
				case 0x41:	name = Localization_GetStringById(0x92);	break;
				case 0x42:	name = Localization_GetStringById(0x93);	break;
				case 0x43:	name = Localization_GetStringById(0x94);	break;
				case 0x44:	name = Localization_GetStringById(0x95);	break;
				case 0x57:	name = Localization_GetStringById(0x96);	break;
				case 0x58:	name = Localization_GetStringById(0x97);	break;
				case 0xb7:	name = Localization_GetStringById(0x98);	break;
				case 0x46:	name = Localization_GetStringById(0x99);	break;
				case 0xd2:	name = Localization_GetStringById(0x9a);	break;
				case 0xc7:	name = Localization_GetStringById(0x9b);	break;
				case 0xc9:	name = Localization_GetStringById(0x9c);	break;
				case 0xd3:	name = Localization_GetStringById(0x9d);	break;
				case 0xcf:	name = Localization_GetStringById(0x9e);	break;
				case 0xd1:	name = Localization_GetStringById(0x9f);	break;
				case 0xcb:	name = Localization_GetStringById(0xa0);	break;
				case 0xc8:	name = Localization_GetStringById(0xa1);	break;
				case 0xcd:	name = Localization_GetStringById(0xa2);	break;
				case 0xd0:	name = Localization_GetStringById(0xa3);	break;
				case 0xc5:	name = Localization_GetStringById(0xa4);	break;
				case 0xb5:	name = Localization_GetStringById(0xa5);	break;
				case 0x37:	name = Localization_GetStringById(0xa6);	break;
				case 0x4a:	name = Localization_GetStringById(0xa7);	break;
				case 0x47:	name = Localization_GetStringById(0xa8);	break;
				case 0x48:	name = Localization_GetStringById(0xa9);	break;
				case 0x49:	name = Localization_GetStringById(0xaa);	break;
				case 0x4e:	name = Localization_GetStringById(0xab);	break;
				case 0x4b:	name = Localization_GetStringById(0xac);	break;
				case 0x4c:	name = Localization_GetStringById(0xad);	break;
				case 0x4d:	name = Localization_GetStringById(0xae);	break;
				case 0x4f:	name = Localization_GetStringById(0xaf);	break;
				case 0x50:	name = Localization_GetStringById(0xb0);	break;
				case 0x51:	name = Localization_GetStringById(0xb1);	break;
				case 0x9c:	name = Localization_GetStringById(0xb2);	break;
				case 0x52:	name = Localization_GetStringById(0xb3);	break;
				case 0x53:	name = Localization_GetStringById(0xb4);	break;
				default:
							wcscpy(sKeyName, Localization_GetStringById(0x51));
							wcscat(sKeyName, L"  ");
							if (vkey)
								sKeyName[wcslen(sKeyName) - 1] = vkey;
							else
								sKeyName[wcslen(sKeyName) - 1] = '?';
							name = sKeyName;
							break;
						}
					}
					wcscat(text, name);
					break;
				}
				case MOUSE_AXIS_PLUS_ANY:
				case MOUSE_AXIS_MINUS_ANY:
					wcscat(text, Localization_GetStringById(0x52));
					break;
				case MOUSE_AXIS_PLUS:
				case MOUSE_AXIS_MINUS:
					wcscat(text, Localization_GetStringById(0x53));
					break;
				case MOUSE_BUTTON_ON:
				case MOUSE_BUTTON_ONCE:
				case MOUSE_BUTTON_RELEASE:
					switch (in->mKey)
					{
					case 0:	wcscat(text, Localization_GetStringById(0x54));	break;
					case 3:	wcscat(text, Localization_GetStringById(0x73));	break;
					case 4:	wcscat(text, Localization_GetStringById(0x72));	break;
					case 1:	wcscat(text, Localization_GetStringById(0x55));	break;
					case 2:	wcscat(text, Localization_GetStringById(0x56));	break;
					}
					break;
				case BUTTON_ON:
				case BUTTON_ONCE:
				case BUTTON_RELEASE:
				{
					if ((in->mPad >= 0) && (in->mPad < LT.mJoypads))
					{
						// the joystick's own name was read here once
						DIPROPSTRING	dips;
						dips.diph.dwSize = sizeof(DIPROPSTRING);
						dips.diph.dwHeaderSize = sizeof(DIPROPHEADER);
						dips.diph.dwObj = 0;
						dips.diph.dwHow = DIPH_DEVICE;
						if (0)
							UseProperty(&dips.diph);	// STAND-IN (unreachable address escape): keeps the stores (004557c3): retail made them and never read them; the original reader is unknown
						wcscat(text, Localization_GetStringById(in->mPad + 0x58));
					}
					else
						wcscat(text, Localization_GetStringById(0x57));
					WCHAR	num[20];
					swprintf(num, L" %d", in->mKey);
					wcscat(text, num);
					break;
				}
				case ANALOGUE_PLUS:
				case ANALOGUE_MINUS:
				case ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT:
				case ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT:
					if (type == ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT)
						type = ANALOGUE_PLUS;
					else if (type == ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT)
						type = ANALOGUE_MINUS;
					if ((in->mPad >= 0) && (in->mPad < LT.mJoypads))
					{
						DIPROPSTRING	dips;
						dips.diph.dwSize = sizeof(DIPROPSTRING);
						dips.diph.dwHeaderSize = sizeof(DIPROPHEADER);
						dips.diph.dwObj = 0;
						dips.diph.dwHow = DIPH_DEVICE;
						if (0)
							UseProperty(&dips.diph);	// STAND-IN (unreachable address escape, as above)
						wcscat(text, Localization_GetStringById(in->mPad + 0x5e));
					}
					else
						wcscat(text, Localization_GetStringById(0x5d));
					wcscat(text, L" ");
					if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_X1))
						wcscat(text, Localization_GetStringById(0x66));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_X1))
						wcscat(text, Localization_GetStringById(0x67));
					else if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_Y1))
						wcscat(text, Localization_GetStringById(0x68));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_Y1))
						wcscat(text, Localization_GetStringById(0x69));
					else if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_X2))
						wcscat(text, Localization_GetStringById(0x6a));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_X2))
						wcscat(text, Localization_GetStringById(0x6b));
					else if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_Y2))
						wcscat(text, Localization_GetStringById(0x6c));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_Y2))
						wcscat(text, Localization_GetStringById(0x6d));
					else if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_POV_X))
						wcscat(text, Localization_GetStringById(0x6e));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_POV_X))
						wcscat(text, Localization_GetStringById(0x6f));
					else if ((type == ANALOGUE_PLUS) && (in->mKey == ANALOGUE_POV_Y))
						wcscat(text, Localization_GetStringById(0x70));
					else if ((type == ANALOGUE_MINUS) && (in->mKey == ANALOGUE_POV_Y))
						wcscat(text, Localization_GetStringById(0x71));
					break;
				}
			}
		}

		SIZE	size;
		PLATFORM.Font(FONT_DEBUG)->GetTextExtent(text, &size);
		float	tx;
		switch (column)
		{
		case 0:	tx = 50.0f;	break;
		case 1:	tx = 320.0f - size.cx * 0.5f;	break;
		case 2:	tx = 590.0f - size.cx;	break;
		}

		if (IsBindingsHeading(row + 55) || (column == 1))
			col = 0xff3f7f2f;
		else
		{
			if (selected)
			{
				// STAND-IN (volatile view): retail tests the flag again before the click (00455b43..00455b4e); a plain read lets VC6
				// jump past the second test when the first fails, so this volatile read stands in for whatever kept the two apart.
				if ((*(volatile bool *)&gKeytrapReleased) && (MouseInRect(tx, ry, size.cx + tx, size.cy + ry)))
				{
					mUnknown24 = column / 2;
					mUnknown28 = row;
				}
				if ((gKeytrapReleased) && (::MouseClickedInRect(tx, ry, size.cx + tx, size.cy + ry)))
				{
					mUnknown24 = column / 2;
					mUnknown28 = row;
					BeginRemapCapture();
				}
			}
			if ((row == mUnknown28) && (column / 2 == mUnknown24) && (selected))
				col = 0xff7f6f2f;
		}

		if (capturing)
		{
			col = BlendAlpha2(col, SINT((((float)cos((float)fmod(PLATFORM.GetSysTimeFloat(), 2.0) * (PI * 2)) + 1.0f) * 0.5f) * 255.0f));
			MOUSE_CURSOR_ALPHA = 0;
		}
		else
			col = BlendAlpha2(col, 255);

		// while another binding is being captured, the rest are dimmed
		if ((!gKeytrapReleased) && (!capturing))
			col = (DWORD((col >> 24) * 0.5f) << 24) + (DWORD(((col >> 16) & 0xff) * 0.5f) << 16) +
				  (DWORD(((col >> 8) & 0xff) * 0.5f) << 8) + DWORD((col & 0xff) * 0.5f);

		PLATFORM.Font(FONT_DEBUG)->DrawTextDynamic(tx, ry, 0.32f, 1.0f, 1.0f, col, text, 1000.0f, 0, 0);
	}

	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
}
// the bindings list's ForEachFixedKey callback (config/function-corrections.tsv adds it)
static void RecordFixedKey(void *context, int button, int type)	// FUNCTION: 0x00456060
{
	sFixedButton = button;
	sFixedType = type;
}
// FUNCTION: 0x00456080
void CControllerDefinition::BeginRemapCapture()
{
	int action = mUnknown28 + 55;
	if (action == 56)
	{
		CAREER.SetInvertYAxis(mUnknown24, !CAREER.GetInvertYAxis(mUnknown24));
		return;
	}
	if (action == 57)
	{
		CAREER.SetInvertJetYAxis(mUnknown24, !CAREER.GetInvertJetYAxis(mUnknown24));
		return;
	}

	gKeytrapReleased = false;
	sKeyDown = false;
	for (int pad = 0; pad < LT.mJoypads; pad++)
	{
		CController *c = FRONTEND.mControllers[0];
		if (!c)
			c = GAME.GetController(0);
		float *rest = sAxisRest[pad];
		rest[0] = c->GetJoyAnalogueLeftX(pad);
		rest[1] = c->GetJoyAnalogueLeftY(pad);
		rest[2] = c->GetJoyAnalogueRightX(pad);
		rest[3] = c->GetJoyAnalogueRightY(pad);
		rest[4] = c->GetJoyPovX(pad);
		rest[5] = c->GetJoyPovY(pad);
	}
	sRemapAction = action;
	sRemapInput = mUnknown24;
	sRemapKey.mChar = 0;
	PLATFORM.SetKeytrap((pKeyTrapper)RemapKeyTrap);
}
// The remap capture's key trap: a key's press and release (or a mouse button) binds it to the action being remapped.
// Retail captures the binding index before clearing (004562da, call 004562e5, setter 004562f3), and the final action
// before clearing (00456575, call 0045658a, dispatch 00456596): here they are helper arguments. With a bounded
// sentinel-terminated binding table, the clear writes only its pad fields, not either value. Every clear here stays
// out of line, as in retail (the helpers' nested budgets are below its size).
// Retail expands SetControlBindingInput (004565d0) at 5 of its 16 mouse sites: the 4th of 59-62 (0x1e), the 2nd of
// 64-67 (0x1c), the 1st of 69/key 3 (0x10), the 2nd of 69/key 4 (0x11) and the 2nd of 70/key 4 (0x11). VC6's rule gives
// exactly those with this wrapper (size 53) at an own size of 725 or 726.
// STAND-IN (code-neutral fit): eight spellings that emit no code raise the own size from 701 to 726: `int type = 0;`,
// `sKeyDown == false` in the three key tests, `else if` for the second key test of cases 69 and 70, `default: break;`
// and braces on the final else. Why: the own size sets the budget (twice the own size) and so those five decisions;
// with 701 every site expands differently (shape 206). How: none of them changes an instruction; throwaway dead stores
// give the same window (725 matches, 721 and 729 do not). Which of these, or what other source, Stuart wrote is
// unknown. RemapKey's bounded comparison does not establish this key trap's runtime equivalence.
// FUNCTION: 0x00456190
static bool RemapKeyTrap(WORD key, KeyEventType event)
{
	if (event == KEY_EVENT_UNKNOWN4)
		return true;

	sRemapType = ANALOGUE_PLUS;
	int		type = 0;
	if (event == KEY_EVENT_DOWN)
	{
		if (sKeyDown == false)
		{
			sRemapKey.mKey = key;
			sKeyDown = true;
		}
		return true;
	}
	else if (event == KEY_EVENT_CHAR)
	{
		if (sKeyDown == false || sRemapKey.mKey == gCharScanCode)
			sRemapKey.mChar = toupper(key);
		return true;
	}
	else if (event == KEY_EVENT_UP)
	{
		if (key == 1)					// Escape: cancel
		{
			gKeytrapReleased = true;
			PLATFORM.SetKeytrap(NULL);
			FRONTEND.PlaySound(FES_BACK);
			return true;
		}
		if (key == 0xdc || key == 0xdb)	// the Windows keys
			return true;
		if (sKeyDown == false || key != sRemapKey.mKey)
			return true;
		gKeytrapReleased = true;
		PLATFORM.SetKeytrap(NULL);
		FRONTEND.PlaySound(FES_SELECT);
		type = KEY_ONCE;
	}
	else if (event == KEY_EVENT_MOUSE)
	{
		gKeytrapReleased = true;
		PLATFORM.SetKeytrap(NULL);
		switch (sRemapAction)
		{
		case 59:
		case 60:
		case 61:
		case 62:
			ClearAndSetBindingInput(sRemapInput, 0x1f, 0, 11, 1, 0);
			ClearAndSetBindingInput(sRemapInput, 0x20, 0, 12, 1, 0);
			ClearAndSetBindingInput(sRemapInput, 0x1d, 0, 12, 0, 0);
			ClearAndSetBindingInput(sRemapInput, 0x1e, 0, 11, 0, 0);
			return true;
		case 64:
		case 65:
		case 66:
		case 67:
			ClearAndSetBindingInput(sRemapInput, 0x1a, 0, 11, 1, 0);
			ClearAndSetBindingInput(sRemapInput, 0x1c, 0, 12, 1, 0);
			ClearAndSetBindingInput(sRemapInput, 0x19, 0, 12, 0, 0);
			ClearAndSetBindingInput(sRemapInput, 0x1b, 0, 11, 0, 0);
			return true;
		case 69:
			if (key == 3)
			{
				ClearAndSetBindingInput(sRemapInput, 0x10, 0, 16, 3, 0);
				ClearAndSetBindingInput(sRemapInput, 0x11, 0, 16, 4, 0);
				return true;
			}
			else if (key == 4)
			{
				ClearAndSetBindingInput(sRemapInput, 0x10, 0, 16, 4, 0);
				ClearAndSetBindingInput(sRemapInput, 0x11, 0, 16, 3, 0);
				return true;
			}
			break;
		case 70:
			if (key == 4)
			{
				ClearAndSetBindingInput(sRemapInput, 0x10, 0, 16, 3, 0);
				ClearAndSetBindingInput(sRemapInput, 0x11, 0, 16, 4, 0);
				return true;
			}
			else if (key == 3)
			{
				ClearAndSetBindingInput(sRemapInput, 0x10, 0, 16, 4, 0);
				ClearAndSetBindingInput(sRemapInput, 0x11, 0, 16, 3, 0);
				return true;
			}
			break;
		default:
			break;
		}
		type = MOUSE_BUTTON_ONCE;
	}
	else
	{
		return true;
	}

	sRemapType = type;
	sRemapKey.mKey = key;
	BindCapturedInput(sRemapAction, 0);
	ApplyControlPreset(0);
	return true;
}
// Sets one input of a binding.
// FUNCTION: 0x004565d0
void SetControlBindingInput(int index, int button, int pad, int type, short key, short vkey)
{
	SControlBinding *b = FindControlBinding(button);
	b->mInput[index].mPad = pad;
	b->mInput[index].mType = type;
	b->mInput[index].mKey = key;
	b->mInput[index].mVKey = vkey;
}
// FUNCTION: 0x00456610
int CControllerDefinition::GetWidth()
{
	return 400;
}
// FUNCTION: 0x00456620
int CControllerDefinition::GetHeight()
{
	return 230;
}
// FUNCTION: 0x00456630
bool CControllerDefinition::IsExpanded()
{
	return mExpanded;
}
// FUNCTION: 0x00456640
void CControllerDefinition::Refresh()
{
	mExpanded = false;
}
// TRUE when no remappable binding leaves this input unset.
// FUNCTION: 0x00456650
bool AllControlBindingsSet(int index)
{
	for (SControlBinding *b = CONTROL_BINDINGS; b->mId != -1; b++)
		if (b->mConfigurable && b->mInput[index].mPad == -1)
			return false;
	return true;
}
