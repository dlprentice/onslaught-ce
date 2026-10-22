// Controller.h: the virtual controller. Stuart's Controller.h is the console original; this is the retail PC
// build's, reconstructed as matched code needs it: key bindings the player can remap (CONTROL_BINDINGS) replace
// Stuart's fixed mapping table, the analogue fields are gone and the class is 0x178 bytes (its constructor
// 0042d640).

#ifndef CONTROLLER_INCLUDE
#define CONTROLLER_INCLUDE

#include "monitor.h"
#include "Activereader.h"
#include "MemBuffer.h"

#define ANALOGUE_ACT_AS_DIGITAL_THRESHOLD 0.9f	// Stuart's: an analogue control past this acts as a button

#define ANALOGUE_X1 -1
#define ANALOGUE_Y1 -2
#define ANALOGUE_X2 -3
#define ANALOGUE_Y2 -4
#define ANALOGUE_POV_X -5		// retail: the hat switch
#define ANALOGUE_POV_Y -6

enum EJoyButtonPushType
{
	BUTTON_ON,
	BUTTON_ONCE,
	BUTTON_RELEASE,
	BUTTON_REPEAT,
	ANALOGUE_PLUS,
	ANALOGUE_MINUS,
	ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT,
	ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT,
	KEY_ONCE,
	KEY_ON,
	// retail: a key's release, then the mouse; named from the bindings that use them (yaw/pitch on the axes, the gun pod's charge
	// and fire on a button held and released, weapon change and zoom on single presses)
	KEY_RELEASE,
	MOUSE_AXIS_PLUS_ANY,			// as 13 and 14, without the per-input lock
	MOUSE_AXIS_MINUS_ANY,
	MOUSE_AXIS_PLUS,
	MOUSE_AXIS_MINUS,
	MOUSE_BUTTON_ON,
	MOUSE_BUTTON_ONCE,
	MOUSE_BUTTON_RELEASE
};

enum EControlType
{
	CONTROL_FRONTEND,
	CONTROL_MECH,
	CONTROL_CAMERA,
	CONTROL_GAMEINTERFACE,
	CONTROL_GAME,
	CONTROL_MESSAGE_LOG,
	CONTROL_LEVEL_BRIEFING_LOG,
	CONTROL_GAME_MENU
};

class CController ;

// object which inherent from iController can be controlled from a controller
// IController adds nothing to CMonitor's 8 bytes (CReconnectInterface, which derives from it, is 12); its vtable
// (005d9388) has CMonitor's three slots and these three, pure (_purecall).
class IController : public CMonitor
{
public:
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float ana_val) =0; ;
	virtual BOOL	CanBeControlledWhenInPause() =0; ;
	virtual EControlType    GetControlType() =0; ;
};

// Stuart's button ids that the retail bindings use (the numbers are unchanged)
#define BUTTON_TOGGLE_GOD_MODE			0
#define BUTTON_TOGGLE_FREE_CAMERA		1
#define BUTTON_ADVANCE_ONE_FRAME		2
#define BUTTON_TOGGLE_DEBUG_SQUAD_FORWARD		3
#define BUTTON_TOGGLE_DEBUG_SQUAD_BACKWARD		4
#define BUTTON_TOGGLE_DEBUG_UNIT_FORWARD		5
#define BUTTON_TOGGLE_DEBUG_UNIT_BACKWARD		6
#define BUTTON_SKIP_CUTSCENE			7
#define BUTTON_CONSOLE_MENU_UP			8
#define BUTTON_CONSOLE_MENU_DOWN		9
#define BUTTON_CONSOLE_MENU_SELECT		10
#define BUTTON_WIN_LEVEL				11
#define BUTTON_LOOSE_LEVEL				12
#define BUTTON_LOG_CAREER				13
#define BUTTON_COMPLETE_ALL_OBJECTIVES	14
#define BUTTON_MECH_CHANGE_ZOOM_IN		16
#define BUTTON_MECH_CHANGE_ZOOM_OUT		17
#define BUTTON_MECH_FIRE_GUN_POD		18
#define BUTTON_MECH_CHARGE_GUN_POD		19
#define BUTTON_MECH_CHANGE_WEAPON		20
#define BUTTON_MECH_LANDING_JETS		21
#define BUTTON_MECH_JET_AFTERBURNER		22
#define BUTTON_MECH_YAW_LEFT			25
#define BUTTON_MECH_PITCH_UP			26
#define BUTTON_MECH_YAW_RIGHT			27
#define BUTTON_MECH_PITCH_DOWN			28
#define BUTTON_MECH_STRAFE_LEFT			29
#define BUTTON_MECH_STRAFE_RIGHT		30
#define BUTTON_MECH_FORWARD				31
#define BUTTON_MECH_BACKWARD			32
#define BUTTON_MECH_MORPH				33		// Stuart's (bound to space)
#define BUTTON_CAMERA_PITCH_UP			34
#define BUTTON_CAMERA_PITCH_DOWN		35
#define BUTTON_CAMERA_YAW_LEFT			36
#define BUTTON_CAMERA_YAW_RIGHT			37
#define BUTTON_CAMERA_MOVE_FORAWRD		38
#define BUTTON_CAMERA_MOVE_BACKWARDS	39
#define BUTTON_CAMERA_STRAFE_LEFT		40
#define BUTTON_CAMERA_STRAFE_RIGHT		41
#define BUTTON_FRONTEND_MENU_UP			42
#define BUTTON_FRONTEND_MENU_DOWN		43
#define BUTTON_FRONTEND_MENU_SELECT		44
#define BUTTON_FRONTEND_CHEAT			45
#define BUTTON_FRONTEND_MENU_BACK		46
#define BUTTON_SAVE_CAREER				47
#define BUTTON_LOAD_CAREER				48
#define BUTTON_PAUSE_MENU_UP			49
#define BUTTON_PAUSE_MENU_DOWN			50
#define BUTTON_PAUSE_MENU_SELECT		51
#define BUTTON_LEFT1_FOR_TOGGLE			52
#define BUTTON_RIGHT1_FOR_TOGGLE		53
#define BUTTON_FRONTEND_MENU_LEFT		54
#define BUTTON_FRONTEND_MENU_RIGHT		55
#define BUTTON_PAUSE					56
#define BUTTON_HELP						57
#define BUTTON_SKIP_PANNING				58
#define BUTTON_MECH_CLOAK				59
#define BUTTON_MECH_CONFIGURATION_DOWN	61
#define BUTTON_MECH_CONFIGURATION_UP	62
#define BUTTON_FRONTEND_MENU_SKIP		60		// Stuart's; CFEPLanguageTest::ButtonPressed (0051c248 index 18)
#define BUTTON_FRONTEND_MENU_SPECIAL	63		// Stuart's; (index 21)
#define BUTTON_SELECT					65
#define BUTTON_BREAK_ATTRACT_MODE		66
#define BUTTON_START					64		// Stuart's; CFEPGoodies::ButtonPressed's case 64 (index 30 at 0045d72e)

#define START_ACTION_BUTTONS			16		// below this, the hardwired debug buttons (Stuart's rule)

// retail: one input of a binding, 12 bytes: the pad it reads (0 for the keyboard and mouse; -1 for none), how, and
// which key, axis or button.
struct SControlInput
{
	int		mPad;				// +0x00
	int		mType;				// +0x04: EJoyButtonPushType
	short	mKey;				// +0x08: a DirectInput scan code, joystick button, axis, or mouse axis or button
	short	mVKey;				// +0x0a: the key's character, for display
};

// retail: one control's bindings, 32 bytes, saved after the career. Each player reads the input with its own
// number; in a single-player game player 1 reads both.
struct SControlBinding
{
	SControlBinding(bool configurable, int button, int type, short key, short vkey);				// 0042d260
	SControlBinding(bool configurable, int button, int type, short key, int type2, short key2,	// 0042d2b0
					short vkey, short vkey2);
	SControlBinding();																			// 0042d300: the table's end

	bool			mConfigurable;		// +0x00: offered for remapping
	int				mId;				// +0x04: the button id, -1 ends the table
	SControlInput	mInput[2];			// +0x08
};

extern SControlBinding CONTROL_BINDINGS[];				// 008892d8

class CController
{
public:
					CController(IController* to_control, int padnumber, int configuration = 1);	// 0042d640
	virtual			~CController();

	virtual void	DeviceSetVibration(float inValue) = 0;
	virtual	void	Flush() ;

	// the controllers know about user inactivity:
	static void			ResetInactivityTimer();						// 0042d7a0
	static void			SetNonInteractiveSection(bool onoff);		// 0042d7d0
	static bool			InactivityMeansQuitGame();					// 0042d810

	void			SetToControl(IController* to_control);			// 0042e610
	IController*	GetToControl();									// 0042e4b0
	void			RelinquishControl();							// 0042e6e0
	void			SetVibration(float inValue, int zero_based_player_number);	// 0042e750
	void			StartRecording(char *filename);					// 0042d8a0
	void			StartPlayback(char *filename);					// 0042d8c0

	void	SetButton(SINT n)
	{
		if (n >= 64)
			mButtons3 |= (1 << (n - 64));
		else if (n >= 32)
			mButtons2 |= (1 << (n - 32));
		else
			mButtons1 |= (1 <<  n     );
	}

	void	UnsetButton(SINT n)
	{
		if (n >= 64)
			mButtons3 &= ~(1 << (n - 64));
		else if (n >= 32)
			mButtons2 &= ~(1 << (n - 32));
		else
			mButtons1 &= ~ (1 <<  n     );
	}

	BOOL	IsButtonSet(SINT n)
	{
		ULONG b = 0 ;

		if (n >= 64)
			b= mButtons3 & (1 << (n - 64));
		else if (n >= 32)
			b= mButtons2 & (1 << (n - 32));
		else
			b= mButtons1 & (1 <<  n      );
		if (b==0) return FALSE; else return TRUE ;
	}

	BOOL	IsButtonOldSet(SINT n)
	{
		ULONG b= 0 ;
		if (n >= 64)
			b= mButtons3Old & (1 << (n - 64));
		if (n >= 32)
			b= mButtons2Old & (1 << (n - 32));
		else
			b= mButtons1Old & (1 <<  n      );
		if (b==0) return FALSE; else return TRUE;
	}

	int GetPadNumber() {return mPadNumber;}

	friend class CControllerDefinition;		// retail PC: the controls page reads the axes (00456109)

protected:

	void	SendButtonAction(int button, float ana_val);				// 0042e4d0
	float	GetMappedInputValue(int pad_number, int button);			// 0042e3d0 retail
	BOOL	RepeatOccured(BOOL repeat_on, SControlBinding* mapping);
	void	CalcNumMappings() ;

	virtual BOOL	GetJoyButtonOnce(int pad_number, int button) =0;
	virtual BOOL	GetJoyButtonOn(int pad_number, int button) =0;
	virtual BOOL	GetJoyButtonRelease(int pad_number, int button) =0;

	virtual BOOL	GetKeyOnce(int key) =0;
	virtual BOOL	GetKeyOn(int key) =0;
	virtual BOOL	GetKeyRelease(int key) =0;							// retail

	virtual float	GetJoyAnalogueLeftX(int pad_number) =0;
	virtual float	GetJoyAnalogueLeftY(int pad_number) =0;
	virtual	float	GetJoyAnalogueRightX(int pad_number) =0;
	virtual float	GetJoyAnalogueRightY(int pad_number) =0;
	virtual float	GetJoyPovX(int pad_number) =0;						// retail
	virtual float	GetJoyPovY(int pad_number) =0;						// retail

	virtual	void	DoMappings() ;
	virtual void	RecordControllerState() =0;
	virtual void	ReadControllerState() =0;

	SPtrSet<CActiveReader<IController> >		mToControlStack ;		// +0x04

	ULONG		mButtons1;												// +0x14
	ULONG		mButtons2;
	ULONG		mButtons3;

	ULONG		mButtons1Old;											// +0x20
	ULONG		mButtons2Old;
	ULONG		mButtons3Old;

	CMEMBUFFER	mDataFile;												// +0x2c
	bool		mRecording;												// +0x160
	bool		mPlaying;												// +0x161
	float		mLastRepeatTime;										// +0x164
	float		mNextDelayTime;											// +0x168

	int			mPadNumber;												// +0x16c
	BOOL		mPausedBeforeMap;										// +0x170
	int			mConfigurationNum;										// +0x174

	static float	mLastTimeAnythingPressed; // not always true			// 0066e948
	static bool		mNonInteractiveSection;									// 0066e94c

	static		int mNumMappings;											// 006254f8
};

// retail: the DirectInput mouse (0066e950) and its per-frame reads, in Controller.cpp
HRESULT	InitMouse();								// 0042d310
HRESULT	ShutdownMouse();							// 0042d3b0
HRESULT	PollMouseMotion();							// 0042d420
HRESULT	PollMouseState();							// 0042d4d0
extern BOOL	gMouseAxisLocked[2];				// 0066e8cc (Controller.cpp; CGame::Update sets it per player)
void	UpdateMouseCentre(bool reset);				// 0042da00: draws the cursor back towards the centre
SControlBinding	*FindControlBinding(int button);	// 0042db10

// Stuart's Controller.h ends with the platform's controller
#include "PCController.h"
#include "mouse.h"			// the PC's mouse state, reset each game tick (CGame::MainLoop 0046eee9)

#endif
