#include "common.h"

#include "controller.h"
#include "player.h"
#include "game.h"
//#include "frontend.h"
#include "debuglog.h"
#include "cliparams.h"
#include "reconnectinterface.h"
#include "Platform.h"
#include "mouse.h"
#include "DX.h"

// retail: the PC build's controls. The bindings the player can remap (CONTROL_BINDINGS, PCController.cpp)
// replace Stuart's fixed mapping table, and the mouse is read here through DirectInput.

// FUNCTION: 0x0042d170 init:ZERO_FVECTOR
// FUNCTION: 0x0042d190 init:ID_FMATRIX

float CController::mLastTimeAnythingPressed = 0.f;
bool CController::mNonInteractiveSection = false;

#define ANALOGUE_DEAD_ZONE 0.15f		// retail: one dead zone for every axis (Stuart's Xbox used 0.36)
#define ANALOGUE_DEAD_ZONE_SCALE ((float)(1.0 / (1.0 - 0.15)))	// retail's constant is 1/0.85 rounded once

#define INITIAL_REPEAT_DELAY 0.5f
#define REPEAT_DELAY 0.12f

float			gUnknownOption6254f0 = 0.7f;	// 006254f0: an option the career saves before the mouse sensitivity (use open)
float			gMouseSensitivity = 7.0f;		// 006254f4: the options' mouse sensitivity
int CController::mNumMappings= -1;			// 006254f8: retail defines it after the two options (the zero dword after it pads the next 8-aligned type descriptor)
static DIMOUSESTATE2	sMouseState;			// 0066e8f0
BOOL			gMouseAxisLocked[2];			// 0066e8cc: per player, the locked mouse axes (13, 14) are ignored
static bool		sSendingMouseRest;				// 0066e8c8: a centred mouse axis is being sent (not activity)
static bool		sMouseAxisRead = false;			// 0066e94d: an axis was read since the cursor was last drawn back
bool			E3_BUILD = false;				// 0066e94e: set by -e3; quits on inactivity outside the playable demo too
static LPDIRECTINPUTDEVICE8	sMouse = NULL;		// 0066e950

//******************************************************************************************
SControlBinding::SControlBinding(bool configurable, int button, int type, short key, short vkey)	// FUNCTION: 0x0042d260 ??0SControlBinding@@QAE@_NHHFF@Z
{
	mConfigurable = configurable;
	mId = button;
	mInput[0].mPad = 0;
	mInput[0].mType = type;
	mInput[0].mKey = key;
	mInput[0].mVKey = vkey;
	mInput[1].mPad = -1;
	mInput[1].mType = 0;
	mInput[1].mKey = 0;
	mInput[1].mVKey = 0;
}

SControlBinding::SControlBinding(bool configurable, int button, int type, short key, int type2, short key2,	// FUNCTION: 0x0042d2b0
								 short vkey, short vkey2)
{
	mConfigurable = configurable;
	mId = button;
	mInput[0].mPad = 0;
	mInput[0].mType = type;
	mInput[0].mKey = key;
	mInput[0].mVKey = vkey;
	mInput[1].mPad = 0;
	mInput[1].mType = type2;
	mInput[1].mKey = key2;
	mInput[1].mVKey = vkey2;
}

SControlBinding::SControlBinding()	// FUNCTION: 0x0042d300 ??0SControlBinding@@QAE@XZ
{
	mConfigurable = false;
	mId = -1;
}

//******************************************************************************************
HRESULT InitMouse()	// FUNCTION: 0x0042d310
{
	HRESULT hr;
	if (FAILED(hr = LT.pDI->CreateDevice(GUID_SysMouse, &sMouse, NULL)))
		return hr;
	if (FAILED(hr = sMouse->SetDataFormat(&c_dfDIMouse2)))
		return hr;
	sMouse->SetCooperativeLevel(LT.m_hWnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
	ZeroMemory(&sMouseState, sizeof(sMouseState));
	ResetMouseTransientState();
	MOUSE_X = PLATFORM.GetWindowWidth() >> 1;
	MOUSE_Y = PLATFORM.GetWindowHeight() >> 1;
	SetMouseInputActive(TRUE);
	return S_OK;
}

HRESULT ShutdownMouse()	// FUNCTION: 0x0042d3b0
{
	POINT pt;
	if (sMouse)
	{
		sMouse->Unacquire();
		SetMouseInputActive(FALSE);
		SAFE_RELEASE(sMouse);
	}
	if (!CLIPARAMS.mAutoConfigTest)
	{
		if (GetCursorPos(&pt))
		{
			MOUSE_X = pt.x;
			MOUSE_Y = pt.y;
		}
	}
	ResetMouseTransientState();
	return S_OK;
}

HRESULT PollMouseMotion()	// FUNCTION: 0x0042d420
{
	if (sMouse)
	{
		ZeroMemory(&sMouseState, sizeof(sMouseState));
		if (FAILED(sMouse->GetDeviceState(sizeof(DIMOUSESTATE2), &sMouseState)))
		{
			HRESULT hr = sMouse->Acquire();
			while (hr == DIERR_INPUTLOST)
				hr = sMouse->Acquire();
			return -1;
		}
		if (!CLIPARAMS.mAutoConfigTest)
		{
			MOUSE_X += sMouseState.lX;
			MOUSE_Y += sMouseState.lY;
			MOUSE_WHEEL += sMouseState.lZ;
		}
	}
	return S_OK;
}

HRESULT PollMouseState()	// FUNCTION: 0x0042d4d0
{
	if (!sMouse || CLIPARAMS.mAutoConfigTest)
		return S_OK;

	ZeroMemory(&sMouseState, sizeof(sMouseState));
	if (FAILED(sMouse->GetDeviceState(sizeof(DIMOUSESTATE2), &sMouseState)))
	{
		HRESULT hr = sMouse->Acquire();
		while (hr == DIERR_INPUTLOST)
			hr = sMouse->Acquire();
		return S_OK;
	}
	if (!CLIPARAMS.mAutoConfigTest)
	{
		MOUSE_X += sMouseState.lX;
		MOUSE_Y += sMouseState.lY;
		MOUSE_WHEEL += sMouseState.lZ;
	}

	// DirectInput's buttons 0, 1 and 2 are left, right and middle
	if (sMouseState.rgbButtons[0] & 0x80)
	{
		MOUSE_BUTTONS[0].mDown = TRUE;
		MOUSE_HELD[0] = true;
	}
	else
	{
		MOUSE_BUTTONS[0].mDown = FALSE;
		MOUSE_BUTTONS[0].mReleased |= MOUSE_HELD[0];
		MOUSE_HELD[0] = false;
	}
	if (sMouseState.rgbButtons[1] & 0x80)
	{
		MOUSE_BUTTONS[2].mDown = TRUE;
		MOUSE_HELD[2] = true;
	}
	else
	{
		MOUSE_BUTTONS[2].mDown = FALSE;
		MOUSE_BUTTONS[2].mReleased |= MOUSE_HELD[2];
		MOUSE_HELD[2] = false;
	}
	if (sMouseState.rgbButtons[2] & 0x80)
	{
		MOUSE_BUTTONS[1].mDown = TRUE;
		MOUSE_HELD[1] = true;
	}
	else
	{
		MOUSE_BUTTONS[1].mDown = FALSE;
		MOUSE_BUTTONS[1].mReleased |= MOUSE_HELD[1];
		MOUSE_HELD[1] = false;
	}
	return S_OK;
}

//******************************************************************************************
CController::CController(IController* to_control, int padnumber, int configuration)	// FUNCTION: 0x0042d640
{
	SetToControl(to_control) ;
	mRecording=false;
	mPlaying=false;
	mPadNumber=padnumber;
	mPausedBeforeMap =FALSE ;
	mConfigurationNum = configuration;
	mLastRepeatTime = -1.0f;
	mNextDelayTime =0.1f ;
	mButtons1 = 0 ;
	mButtons2 = 0; 
	mButtons3 = 0 ;
	mButtons1Old =0 ;
	mButtons2Old =0 ;
	mButtons3Old = 0;
}

// Compiler-generated: the deleting destructor.
// FUNCTION: 0x0042d780 ??_GCController@@UAEPAXI@Z

//******************************************************************************************
// retail: the timer stands at zero through a non-interactive section
void CController::ResetInactivityTimer()	// FUNCTION: 0x0042d7a0
{
	if (mNonInteractiveSection)
		mLastTimeAnythingPressed = 0.f;
	else
		mLastTimeAnythingPressed = PLATFORM.GetSysTimeFloat();
}

//******************************************************************************************
void CController::SetNonInteractiveSection(bool onoff)	// FUNCTION: 0x0042d7d0
{
	if (mNonInteractiveSection == onoff)
	{
		// nothing interesting going on.
		return;
	}

	if (onoff)
	{
		// We're entering a non-interactive section
		if (CLIPARAMS.mAttractMode)
		{
			// the timer keeps ticking in the background unaffected
		}
		else
		{
			// we replace the timer with an indication of how much timeout is left, so that we
			// can restart it when the FMV is finished.
			mLastTimeAnythingPressed = PLATFORM.GetSysTimeFloat() - mLastTimeAnythingPressed;
		}
	}
	else
	{
		// We're coming back out again.
		if (CLIPARAMS.mAttractMode)
		{
			// The timer has just carried on ticking in the background, so nothing to do here.
		}
		else
		{
			// We restart the timer from where it was before the non-interactive bit.
			mLastTimeAnythingPressed = PLATFORM.GetSysTimeFloat() - mLastTimeAnythingPressed;
		}
	}

	mNonInteractiveSection = onoff;
}

//******************************************************************************************
bool CController::InactivityMeansQuitGame()	// FUNCTION: 0x0042d810
{
	// this only happens in playable demo (retail: or when asked)
	if (PLAYABLE_DEMO || E3_BUILD)
	{
		// if we're in something non-interactive, never quit game. Either the timer is paused or we're
		// waiting until the end of the FMV anyway.
		if (mNonInteractiveSection)
			return false;

		// or if this is all disabled, don't quit.
		if (CLIPARAMS.mInactiveTimeout <= 0)
			return false;

		// otherwise, we're just waiting for the timeout
		float time_passed = PLATFORM.GetSysTimeFloat() - mLastTimeAnythingPressed;
		if ((time_passed * 1000.f) > CLIPARAMS.mInactiveTimeout)
		{
			TRACE("Inactivity timeout hit (last=%f,now=%f,time passed=%f, to=%d)\n",mLastTimeAnythingPressed,PLATFORM.GetSysTimeFloat(),time_passed,CLIPARAMS.mInactiveTimeout);
			// time to go!
			return true;
		}

		return false;
	}
	else
	{
		// outside playable demo, you never quit because of inactivity.
		return false;
	}
}

//******************************************************************************************
void CController::StartRecording(char *filename)	// FUNCTION: 0x0042d8a0
{
	mRecording=true;
	mDataFile.InitFromMem(filename);
}

//******************************************************************************************

void CController::StartPlayback(char *filename)	// FUNCTION: 0x0042d8c0
{
	mPlaying=true;
	mDataFile.InitFromFile(filename);
}

//******************************************************************************************

CController::~CController()	// FUNCTION: 0x0042d8e0
{
	if (mPlaying || mRecording)
	{
		mDataFile.Close();
	}

	mToControlStack.DeleteAll() ;
}


//******************************************************************************************
void CController::Flush()	// FUNCTION: 0x0042d9d0
{
	// clear buttons in virtual controller

	// ### SRG (fixme ) stop doing this moho idea,  and use that binary block class
	mButtons1Old =mButtons1 ;
	mButtons2Old =mButtons2;
	mButtons3Old =mButtons3;

	mButtons1 =0 ;
	mButtons2 =0;
	mButtons3 =0;


	// get platform specific child to fill in virtual buttons
	DoMappings() ;
}

//******************************************************************************************
// retail: after the mouse steered, draw the cursor 7/17 of the way back towards the centre of the window,
// at least one pixel
// Retail keeps the mouse snapshot as adjacent integer homes. The reciprocal at 005d97c4 is
// 0x3f169697: compute 1/1.7 before rounding to float, rather than reciprocating 1.7f
// (which emits 0x3f169696). This form matches all 272 bytes and relocations at 0042da00.
void UpdateMouseCentre(bool reset)	// FUNCTION: 0x0042da00
{
	if (CLIPARAMS.mAutoConfigTest)
		return;

	if (reset)
	{
		MOUSE_X = PLATFORM.GetWindowWidth() / 2;
		MOUSE_Y = PLATFORM.GetWindowHeight() / 2;
	}

	if (sMouseAxisRead)
	{
		int cx, cy;
		POINT mouse = {MOUSE_X, MOUSE_Y};
		cx = PLATFORM.GetWindowWidth() >> 1;
		cy = PLATFORM.GetWindowHeight() >> 1;
		int dx = (int)((cx * 0.7f + mouse.x) * (float)(1.0 / 1.7) - mouse.x);
		int dy = (int)((cy * 0.7f + mouse.y) * (float)(1.0 / 1.7) - mouse.y);
		if (cx != mouse.x && dx == 0) dx = (mouse.x > cx) ? -1 : 1;
		if (cy != mouse.y && dy == 0) dy = (mouse.y > cy) ? -1 : 1;
		MOUSE_X = mouse.x + dx;
		MOUSE_Y = mouse.y + dy;
		sMouseAxisRead = false;
	}
}

//******************************************************************************************
SControlBinding *FindControlBinding(int button)	// FUNCTION: 0x0042db10
{
	for (SControlBinding *binding = CONTROL_BINDINGS; binding->mId != -1; binding++)
		if (binding->mId == button)
			return binding;
	return NULL;
}

//******************************************************************************************
void CController::CalcNumMappings()
{
	int i =0 ;
	BOOL found = FALSE ;
	while (found==FALSE)
	{
		SControlBinding* mapping =  &CONTROL_BINDINGS[i] ;
		if (mapping->mId==-1)
		{
			found = TRUE ;
		}
		else
		{
			i++;
		}
	}
	mNumMappings = i ;
	LOG.AddMessage("Controller: Number of mappings found = %d", mNumMappings) ;
}


//******************************************************************************************
BOOL CController::RepeatOccured(BOOL repeat_on, SControlBinding* mapping)
{
	if (IsButtonOldSet(mapping->mId) == FALSE)
	{
		// setup intital repeat delay
		mNextDelayTime = INITIAL_REPEAT_DELAY;
		mLastRepeatTime = PLATFORM.GetSysTimeFloat() ;
		return TRUE;
	}
	else 
	{
		if (repeat_on)
		{
			mNextDelayTime = REPEAT_DELAY;
			return TRUE ;
		}
		else	
		{
			// make sure we still mark that we are in a repeat
			SetButton(mapping->mId) ;
		}
	}
	return FALSE ;
}

// retail: a held digital control stands for a full deflection, negative for the left, up and forward ones
inline float DigitalValueForButton(int button)
{
	switch (button)
	{
		case BUTTON_MECH_YAW_LEFT:
		case BUTTON_MECH_PITCH_UP:
		case BUTTON_MECH_STRAFE_LEFT:
		case BUTTON_MECH_FORWARD:
		case BUTTON_CAMERA_PITCH_DOWN:
		case BUTTON_CAMERA_YAW_LEFT:
		case BUTTON_CAMERA_MOVE_FORAWRD:
		case BUTTON_CAMERA_STRAFE_LEFT:
			return -1.0f;
	}
	return 1.0f;
}

//******************************************************************************************

void CController::DoMappings()	// FUNCTION: 0x0042db40
{
	int pad = mPadNumber;

	if (mNumMappings ==-1)
	{
		CalcNumMappings() ;
	}

	if (mPlaying)
	{
		ReadControllerState() ;
	}

	// auto repeat stuff (note only 1 repeat button is accepted at once )
	float sys_time = PLATFORM.GetSysTimeFloat() ;
	BOOL repeat_on = FALSE ;
	if (PLATFORM.GetSysTimeFloat() - mLastRepeatTime  > mNextDelayTime)
	{
		mLastRepeatTime = sys_time ;
		repeat_on = TRUE ;
	}


	mPausedBeforeMap = GAME.IsPaused() ;

	//map buttons from platform specific to buttons in virtual controller
	for (int i=0; i < mNumMappings; i++)
	{
		SControlBinding* mapping =  &CONTROL_BINDINGS[i] ;

		// retail: each player reads its own input; alone, player 1 reads both
		int input_num = pad;
		while (TRUE)
		{
			SControlInput* input = &mapping->mInput[input_num];
			int input_pad = input->mPad;

			if ((input_pad != -1) && (!CLIPARAMS.mAutoConfigTest) && (!mPlaying) && (IsButtonSet(mapping->mId) == FALSE))
			{
				switch (input->mType)
				{
					case BUTTON_ONCE:	
					{
						if (GetJoyButtonOnce(input_pad, input->mKey) == TRUE)
						{
							SendButtonAction(mapping->mId, 1.0f) ;
						}
						break ;
					}

					case BUTTON_ON:
					{
						if (GetJoyButtonOn(input_pad, input->mKey) == TRUE)
						{
							float val = DigitalValueForButton(mapping->mId);
							SendButtonAction(mapping->mId, val) ;
						}
						break;
					}

					case BUTTON_REPEAT:
					{
						if (GetJoyButtonOn(input_pad, input->mKey) == TRUE)
						{
							if (RepeatOccured(repeat_on, mapping)) 	
							{
								// send out button action
								 SendButtonAction(mapping->mId, 1.0f) ;
							}
						}
						
						break;
					}

					case BUTTON_RELEASE:
					{
						if (GetJoyButtonRelease(input_pad, input->mKey) == TRUE)
						{
							SendButtonAction(mapping->mId, 1.0f) ;
						}
						break ;
					}

					case ANALOGUE_PLUS:
					{
						float ana_val = GetMappedInputValue(input_pad, input->mKey) ;
						if (ana_val > 0.0f) 
						{
							SendButtonAction(mapping->mId, ana_val) ;
						}
						break;
					}

					case ANALOGUE_MINUS:
					{
						float ana_val = GetMappedInputValue(input_pad, input->mKey) ;
						if (ana_val < 0.0f) 
						{
							SendButtonAction(mapping->mId, ana_val) ;
						}
						break ;
					}


					case ANALOGUE_PLUS_ACT_AS_BUTTON_REPEAT:
					{
						if(	GetMappedInputValue(input_pad, input->mKey) > ANALOGUE_ACT_AS_DIGITAL_THRESHOLD)
						{
							if (RepeatOccured(repeat_on, mapping))
							{
								// send out button action
								SendButtonAction(mapping->mId, 1.0f) ;
							}
						}

						break ;
					}

					case ANALOGUE_MINUS_ACT_AS_BUTTON_REPEAT:
					{
						if (GetMappedInputValue(input_pad, input->mKey) < -ANALOGUE_ACT_AS_DIGITAL_THRESHOLD)
						{
							if (RepeatOccured(repeat_on, mapping))
							{
								// send out button action
								SendButtonAction(mapping->mId, 1.0f) ;
							}
						}

						break ;
					}

					case KEY_ONCE:
					{
						if (GetKeyOnce(input->mKey) == TRUE)
						{
							SendButtonAction(mapping->mId, 1.0f) ;
						}
						break ;
					}

					case KEY_RELEASE:
					{
						if (GetKeyRelease(input->mKey))
						{
							SendButtonAction(mapping->mId, 1.0f) ;
						}
						break ;
					}

					case KEY_ON:
					{
						if (GetKeyOn(input->mKey) == TRUE)
						{
							float val = DigitalValueForButton(mapping->mId);
							SendButtonAction(mapping->mId, val) ;
						}
						break ;
					}

					case MOUSE_AXIS_PLUS:
					case MOUSE_AXIS_MINUS:
						if (gMouseAxisLocked[pad])
							break;
					case MOUSE_AXIS_PLUS_ANY:
					case MOUSE_AXIS_MINUS_ANY:
					{
						// the cursor's distance from the centre of the window, scaled by the sensitivity
						int x = MOUSE_X;
						int y = MOUSE_Y;
						float val;
						if (input->mKey == 0)
							val = (float)(x - (PLATFORM.GetWindowWidth() >> 1)) * 0.0043333333f;
						else
							val = (float)(y - (PLATFORM.GetWindowHeight() >> 1)) * 0.0043333333f;
						val = gMouseSensitivity * val;
						if (val < -1.0f) val = -1.0f;
						else if (val > 1.0f) val = 1.0f;
						if ((input->mType == MOUSE_AXIS_PLUS_ANY || input->mType == MOUSE_AXIS_PLUS) && val < 0.0f) val = 0.0f;
						if ((input->mType == MOUSE_AXIS_MINUS_ANY || input->mType == MOUSE_AXIS_MINUS) && val > 0.0f) val = 0.0f;
						if (sMouse)
						{
							if (val == 0.0f) sSendingMouseRest = true;
							SendButtonAction(mapping->mId, val) ;
							sSendingMouseRest = false;
						}
						sMouseAxisRead = true;
						break ;
					}

					case MOUSE_BUTTON_ONCE:
					case MOUSE_BUTTON_RELEASE:
					{
						switch (input->mKey)
						{
							case 0: if (MOUSE_BUTTONS[0].mReleased) SendButtonAction(mapping->mId, 1.0f); break;
							case 1: if (MOUSE_BUTTONS[1].mReleased) SendButtonAction(mapping->mId, 1.0f); break;
							case 2: if (MOUSE_BUTTONS[2].mReleased) SendButtonAction(mapping->mId, 1.0f); break;
							case 3: if (MOUSE_WHEEL > 0) SendButtonAction(mapping->mId, 1.0f); break;
							case 4: if (MOUSE_WHEEL < 0) SendButtonAction(mapping->mId, 1.0f); break;
						}
						break ;
					}

					case MOUSE_BUTTON_ON:
					{
						switch (input->mKey)
						{
							case 0: if (MOUSE_HELD[0]) SendButtonAction(mapping->mId, 1.0f); break;
							case 1: if (MOUSE_HELD[1]) SendButtonAction(mapping->mId, 1.0f); break;
							case 2: if (MOUSE_HELD[2]) SendButtonAction(mapping->mId, 1.0f); break;
						}
						break ;
					}

					default:
					{
						LOG.AddMessage("FATAL ERROR: Uknown 'push_type' in controller");
						break;
					}
				}
			}

			// playing
			if (mPlaying && IsButtonSet(mapping->mId) == TRUE)
			{
				float ana_val = GetMappedInputValue(input->mPad, input->mKey) ;
				SendButtonAction(mapping->mId, ana_val);

				// make sure button can only happen once per flush (i.e. if there is two mappings to it)
				UnsetButton(mapping->mId);
			}

			if (GAME.IsMultiplayer() || input_num != 0)
				break;
			input_num = 1;
		}
	}

	// retail: both shift keys
	if (PLATFORM.KeyOn(DIK_LSHIFT) && PLATFORM.KeyOn(DIK_RSHIFT))
	{
		SendButtonAction(BUTTON_FRONTEND_CHEAT, 0.0f);
	}

	if (mRecording)
	{
		RecordControllerState() ;
	}

}


//******************************************************************************************
// retail: the sticks and hat through a dead zone of 0.15, the rest scaled to reach 1; buttons give 0 or 1
float CController::GetMappedInputValue(int pad_number, int button)	// FUNCTION: 0x0042e3d0
{
	float val;
	switch (button)
	{
		case ANALOGUE_X1: val = GetJoyAnalogueLeftX(pad_number); break;
		case ANALOGUE_X2: val = GetJoyAnalogueRightX(pad_number); break;
		case ANALOGUE_Y1: val = GetJoyAnalogueLeftY(pad_number); break;
		case ANALOGUE_Y2: val = GetJoyAnalogueRightY(pad_number); break;
		case ANALOGUE_POV_X: val = GetJoyPovX(pad_number); break;
		case ANALOGUE_POV_Y: val = GetJoyPovY(pad_number); break;
		default:
			if (GetJoyButtonOn(pad_number, button)) return 1.0f;
			return 0.0f;
	}
	if (val < -ANALOGUE_DEAD_ZONE) return (val + 1.0f) * ANALOGUE_DEAD_ZONE_SCALE - 1.0f;
	if (val > ANALOGUE_DEAD_ZONE) return 1.0f - (1.0f - val) * ANALOGUE_DEAD_ZONE_SCALE;
	return 0.0f;
}


//******************************************************************************************

IController *CController::GetToControl()	// FUNCTION: 0x0042e4b0
{
	return  mToControlStack.First()->ToRead();
}

//******************************************************************************************
void	CController::SendButtonAction(int button, float ana_val)	// FUNCTION: 0x0042e4d0
{
	SetButton(button) ;

	if (mToControlStack.First() == NULL)
	{
		LOG.AddMessage("Nothing to Control !!") ;
		return ;
	}

	IController* to_send = mToControlStack.First()->ToRead() ;

	// A button has been pressed! Let's reset our counting of inactivity (retail: not for a centred mouse)
	if (!sSendingMouseRest)
		ResetInactivityTimer();

	if (to_send == FALSE) return;

	// resolve hardwired debug buttons

	if (button < START_ACTION_BUTTONS)
	{
		if (to_send->GetControlType()!=CONTROL_FRONTEND)
		{
			GAME.ReceiveButtonAction(this,button, ana_val) ;
		}
		else
			to_send->ReceiveButtonAction(this,button, ana_val) ;
	}
	else
	{
		if ((RECONNECT_INTERFACE[0].GetState() != CReconnectInterface::OK) ||
			(RECONNECT_INTERFACE[1].GetState() != CReconnectInterface::OK))
		{
			if ((to_send != &RECONNECT_INTERFACE[0]) &&
				(to_send != &RECONNECT_INTERFACE[1]))
				return;
		}

		if ((mPausedBeforeMap == FALSE) || (to_send->CanBeControlledWhenInPause() == TRUE ))
		{
			to_send->ReceiveButtonAction(this,button, ana_val) ;
		}

	}
}


//******************************************************************************************
void		CController::SetToControl(IController* to_control)	// FUNCTION: 0x0042e610
{
#line 967
	CActiveReader<IController>* to_control_r = new (MEMTYPE_CONTROLLER) CActiveReader<IController>(to_control);
	mToControlStack.Add(to_control_r) ;
}


//******************************************************************************************
void		CController::RelinquishControl()	// FUNCTION: 0x0042e6e0
{
	CActiveReader<IController>* first = mToControlStack.First() ;
	if (first)
	{
		mToControlStack.Remove(first) ;
		delete first;
	}
	else
	{
		LOG.AddMessage("FATAL ERROR: Controller stack empty to call to 'RelinquishControl'") ;
		return ;
	}

	first = mToControlStack.First() ;
	if (first == NULL)
	{
		LOG.AddMessage("FATAL ERROR: stack empty to call to 'RelinquishControl'") ;
		return ;
	}

}

void CController::SetVibration(float inValue, int player)	// FUNCTION: 0x0042e750
{
	ASSERT(player == 0 || player == 1);

	if (GAME.GetGameState() != GAME_STATE_PLAYING)
	{
		if (inValue != 0.f)
			return;
	}

	if (CAREER.GetVibration(player)) DeviceSetVibration(inValue);
	else							 DeviceSetVibration(0.f    );
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// STAND-IN (link layout): retail's 28 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_Controller[28] = { 0 };
// FUNCTION: 0x0042d9b0 ?RemoveDeletionEvent@CMonitor@@QAEXPAVCGenericActiveReader@@@Z
