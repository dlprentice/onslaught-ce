// Player.cpp: implementation of the CPlayer class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"	// FUNCTION: 0x004d2690 init:ZERO_FVECTOR

#include "Player.h"	// FUNCTION: 0x004d26b0 init:ID_FMATRIX
#include "game.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "GameInterface.h"
#include "Camera.h"
#include "EventManager.h"
#include "BSpline.h"
#include "debuglog.h"
#include "career.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


//**********************************************************************************************
CPlayer::CPlayer(int number):	// FUNCTION: 0x004d2780
	mNumber(number)
{
  mBattleEngine.SetReader(NULL);
  WipeStats();
 // mIsGod = TRUE;
  mCurrentViewMode = PLAYER_FP_VIEW;
  mPreferedControlView = PLAYER_FP_VIEW;
  mThingsKilled.SetAll(0);
  mIsGod = CAREER.GetIsGod(mNumber-1) ;
}
 

//**********************************************************************************************
CPlayer::~CPlayer()	// FUNCTION: 0x004d2830
{

}



//**********************************************************************************************
void CPlayer::Init()	// FUNCTION: 0x004d28a0
{	
	GotoFPView();

	mTimeoutTime = PLATFORM.GetSysTimeFloat();
}

//**********************************************************************************************
void CPlayer::GotoFPView()	// FUNCTION: 0x004d28c0
{
	if (mBattleEngine.ToRead() == NULL) return ;
	mCurrentViewMode = PLAYER_FP_VIEW;
	GAME.SetCurrentCamera( mNumber-1, new( MEMTYPE_CAMERA ) CThingCamera( mBattleEngine.ToRead() ) );
}


//**********************************************************************************************
void CPlayer::Goto3rdPersonView()	// FUNCTION: 0x004d29c0
{
	if (mBattleEngine.ToRead() == NULL) return ;
	mCurrentViewMode = PLAYER_3RD_PERSON_VIEW;
	GAME.SetCurrentCamera( mNumber-1, new( MEMTYPE_CAMERA ) CThing3rdPersonCamera( mBattleEngine.ToRead() ) );
}


//**********************************************************************************************
void CPlayer::GotoControlView()	// FUNCTION: 0x004d2a50
{
	if (mPreferedControlView == PLAYER_FP_VIEW )
	{
		GotoFPView() ;
	}

	if (mPreferedControlView == PLAYER_3RD_PERSON_VIEW )
	{
		Goto3rdPersonView() ;
	}
}



//**********************************************************************************************
FVector CPlayer::GetCurrentViewPoint()	// FUNCTION: 0x004d2a70
{
	FVector r(0.0f,0.0f,0.0f); 
	CCamera* cam = GAME.GetCurrentCamera(mNumber-1) ;
	if (cam)
	{
		r = cam->GetPos() ;
	}
	return r ;
}


//**********************************************************************************************
FMatrix CPlayer::GetCurrentViewOrientation()	// FUNCTION: 0x004d2ae0
{
	FMatrix m = ID_FMATRIX ;
	CCamera* cam = GAME.GetCurrentCamera(mNumber-1) ;
	if (cam)
	{
		m = cam->GetOrientation() ;
	}
	return m ;

}


//**********************************************************************************************
FVector CPlayer::GetOldCurrentViewPoint()	// FUNCTION: 0x004d2b40
{
	FVector r(0.0f,0.0f,0.0f); 
	CCamera* cam = GAME.GetCurrentCamera(mNumber-1) ;
	if (cam)
	{
		r = cam->GetOldPos() ;
	}
	return r ;
}


//**********************************************************************************************
FMatrix CPlayer::GetOldCurrentViewOrientation()	// FUNCTION: 0x004d2bb0
{
	FMatrix m = ID_FMATRIX ;
	CCamera* cam = GAME.GetCurrentCamera(mNumber-1) ;
	if (cam)
	{
		m = cam->GetOldOrientation() ;
	}
	return m ;

}


//**********************************************************************************************
void CPlayer::SetPreferedControlViewMode( EPlayerCameraView mode)
{
	mPreferedControlView = mode ;
	if (mCurrentViewMode == PLAYER_PAN_VIEW) return ;

	EVENT_MANAGER.AddEvent(GOTO_CONTROL_VIEW, this, NEXT_FRAME) ;
}
	

//**********************************************************************************************
void CPlayer::GotoPanView(float for_time)	// FUNCTION: 0x004d2c10
{
	if (mBattleEngine.ToRead() == NULL) return ;

	mCurrentViewMode = PLAYER_PAN_VIEW;

	float pan_interval = for_time;

	SPtrSet<FVector>* points = new (MEMTYPE_PLAYER)	SPtrSet<FVector> ;
	if (points==NULL)
		return;

	FMatrix ori = mBattleEngine->GetOrientation() ;
	
	if (GAME.GetCurrentlyRunningLevelNum() == 231 ||
		GAME.GetCurrentlyRunningLevelNum() == 232 ||
		GAME.GetCurrentlyRunningLevelNum() == 331 ||
		GAME.GetCurrentlyRunningLevelNum() == 221 ||
		GAME.GetCurrentlyRunningLevelNum() == 222 ||
		GAME.GetCurrentlyRunningLevelNum() == 524 ||
		GAME.GetCurrentlyRunningLevelNum() == 523 ||
		GAME.GetCurrentlyRunningLevelNum() == 332 )
	{
		// We start the level standing on something, so only do a half-pan
		
		points->Append(new (MEMTYPE_CAMERA) FVector(ori * FVector(5.0f, 0.0f, 0.0f)));		
	}
	else
	{
		points->Append(new (MEMTYPE_CAMERA) FVector(ori * FVector(0.0f, 10.0f, -4.3f))) ;
		points->Append(new (MEMTYPE_CAMERA) FVector(ori * FVector(5.0f, 0.0f, 1.3f)));
	}

	points->Append(new (MEMTYPE_CAMERA) FVector(ori * FVector(0.0f, -9.0f, -1.3f))) ;
	points->Append(new (MEMTYPE_CAMERA) FVector(ori * FVector(0.0f, -2.5f, 0.0f)));

	CBSpline* spline = new (MEMTYPE_PLAYER) CBSpline(points);

	CCamera* c = new( MEMTYPE_CAMERA ) CPanCamera( mBattleEngine.ToRead(), spline, pan_interval) ;

	GAME.SetCurrentCamera( mNumber-1, c );

	if (pan_interval > 0.0f) pan_interval-=0.05f;
	EVENT_MANAGER.AddEvent(pan_interval, GOTO_CONTROL_VIEW, this) ;
}

//**********************************************************************************************
void CPlayer::HandleEvent(CEvent* event)	// FUNCTION: 0x004d2fe0
{
	switch( (EPlayerEvent)event->GetEventNum() )
	{
		case GOTO_CONTROL_VIEW:
		{
			if (mBattleEngine.ToRead() && mCurrentViewMode == PLAYER_PAN_VIEW)
			{
				GotoControlView() ;
			}
			break ;
		}
		default:
		{
			IController::HandleEvent(event) ;
			break;
		}
	}
}


//**********************************************************************************************
void	CPlayer::SetIsGod(BOOL val)	// FUNCTION: 0x004d3020
{
	mIsGod= val ; 
	CAREER.SetIsGod(mNumber-1, val) ;
	if (mBattleEngine.ToRead())
	{
		if (mIsGod == TRUE)
		{
			mBattleEngine->SetVulnerable(FALSE) ;
			mBattleEngine->SetInfinateEnergy(TRUE);
		}
		else
		{
			mBattleEngine->SetVulnerable(TRUE) ;
			mBattleEngine->SetInfinateEnergy(FALSE);
		}
	}
	
	if (val)
		IncStat(PS_CHEATED,1);


}

//**********************************************************************************************

void	CPlayer::WipeStats()
{
	for (int i=0;i<PS_NUM_PLAYERSTATS;i++)
		mStat[i]=0;
}

//**********************************************************************************************
void	CPlayer::AssignBattleEngine(CBattleEngine* be)	// FUNCTION: 0x004d3080
{
	mBattleEngine.SetReader(be); 
	
	mBattleEngine->SetPlayer(this);

	// make sure thing we control has same vulnerbility as us
	if (mIsGod) 
	{
		mBattleEngine->SetVulnerable(FALSE) ;
		mBattleEngine->SetInfinateEnergy(TRUE);
	}
}


//**********************************************************************************************
void	CPlayer::KilledEnemyThing(CUnit* thing)	// FUNCTION: 0x004d30d0
{
//	LOG.AddMessage("Confirm kill on unit %08x", thing);
	if (thing->IsA(THING_TYPE_AIR_UNIT) ) mThingsKilled[TK_AIRCRAFT]++;
	if (thing->IsA(THING_TYPE_VEHICLE) ) mThingsKilled[TK_VEHICLES]++;
	if (thing->IsA(THING_TYPE_EMPLACEMENT) ) mThingsKilled[TK_EMPLACEMENTS]++;
	if (thing->IsA(THING_TYPE_INFANTRY)) mThingsKilled[TK_INFANTY]++;
	if (thing->IsA(THING_TYPE_MECH)) mThingsKilled[TK_MECHS]++;
}



//**********************************************************************************************
void	CPlayer::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x004d3110
{
	// if game has finished then don't allow any control to player class
	if (GAME.GetGameState() > GAME_STATE_PLAYING ||
		GAME.GetGameState() <= GAME_STATE_PRE_RUNNING) return ;

	// reset timeout time
	mTimeoutTime = PLATFORM.GetSysTimeFloat();


	// Go into pause?
	if (button == BUTTON_PAUSE)
	{
	//	from_controller->SetToControl(&GAMEINTERFACE) ;
	//	GAMEINTERFACE.ToggleMenuDisplay();
		GAME.Pause(TRUE, from_controller);
		return ;
	}

	if (button == BUTTON_HELP)
	{
	//	GotoPanView(6.0f);
	//	from_controller->SetToControl(&GAMEINTERFACE) ;
	//	GAMEINTERFACE.ToggleHelpDisplay();
	//	GAME.Pause();
		return;
	}

	if (button == BUTTON_SKIP_PANNING && mCurrentViewMode == PLAYER_PAN_VIEW && GAME.GetGameState() == GAME_STATE_PANNING)
	{
		GotoControlView() ;
		GAME.StartPlayingState() ;
	}

		
	// if game is pre running or panning don't allow control of movement
	if (GAME.GetGameState() < GAME_STATE_PLAYING) return ;

	if (mBattleEngine.ToRead() == NULL ||
	    mBattleEngine->IsDying() ||
		mBattleEngine->GetIsPoweredUp() == FALSE) return ;

	// deal with axis inversion
	if ((button == BUTTON_MECH_PITCH_UP) || (button == BUTTON_MECH_PITCH_DOWN))
	{
		// retail: the career's options, one for the walker on the ground and one otherwise (004d31d3)
		if (mBattleEngine->IsWalkerOnGroundOrObject())
		{
			if (CAREER.GetInvertYAxis(mNumber-1))
				val = -val;
		}
		else if (CAREER.GetInvertJetYAxis(mNumber-1))
		{
			val = -val;
		}
	}

	// if look then make into non linear
	if (button == BUTTON_MECH_YAW_RIGHT ||
		button == BUTTON_MECH_YAW_LEFT ||
		button == BUTTON_MECH_PITCH_UP ||
		button == BUTTON_MECH_PITCH_DOWN)
	{
		// should give a curve so 50% before would result in 25% after
		// (note 0% before = 0% after and 100% before = 100% after )

		float t1;
		if (val > 0.0f)
		{
			t1 = ((float)tan(val*1.2f) *3.0f) ;
		}
		else
		{
			t1 = -((float)tan(-val*1.2f) *3.0f) ;
		}

		static float t2 = ((float)tan(1.2f) *3.0f);
			
		val = t1 / t2 ;


	//	LOG.AddMessage("val = %2.8f", val) ;
	}
	
	if ((mBattleEngine->GetState() != BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER) &&
		(mBattleEngine->GetState() != BATTLE_ENGINE_STATE_MORPHING_INTO_JET))
	{
		switch(button)
		{
			case BUTTON_MECH_MORPH:
			{
				mBattleEngine->Morph() ;
				break ;
			}
			case BUTTON_MECH_CHANGE_ZOOM_IN:
			{
				mBattleEngine->ZoomIn() ;
				break ;
			}
			case BUTTON_MECH_CHANGE_ZOOM_OUT:
			{
				mBattleEngine->ZoomOut() ;
				break;
			}
			case BUTTON_MECH_CONFIGURATION_UP:
			{
				mBattleEngine->ConfigurationUp() ;
				break ;
			}
			case BUTTON_MECH_CONFIGURATION_DOWN:
			{
				mBattleEngine->ConfigurationDown() ;
				break;
			}
			case BUTTON_MECH_CHARGE_GUN_POD:
			{
				mBattleEngine->ChargeWeapon() ;
				break ;
			}
			case BUTTON_MECH_FIRE_GUN_POD:
			{
				mBattleEngine->FireWeapon() ;
				break ;
			}
			case BUTTON_MECH_CHANGE_WEAPON:
			{
				mBattleEngine->ChangeWeapon() ;
				break ;
			}
			case BUTTON_MECH_CLOAK:
			{
				mBattleEngine->HandleCloak();
				break;
			}
		}
	}


	if (mBattleEngine->GetState() == BATTLE_ENGINE_STATE_WALKER)
	{
		switch (button)
		{
			case BUTTON_MECH_FORWARD:
			{
				mBattleEngine->GetWalkerPart()->Forward(val) ;
				break ;
			}
			case BUTTON_MECH_BACKWARD:
			{
				mBattleEngine->GetWalkerPart()->Backward(val) ;
				break ;
			}
			case BUTTON_MECH_STRAFE_LEFT: 
			{
				mBattleEngine->GetWalkerPart()->StrafeLeft(val) ;
				break ;
			}
			case BUTTON_MECH_LANDING_JETS:
			{
				mBattleEngine->GetWalkerPart()->ActivateLandingJets() ;
				break ;
			}
			case BUTTON_MECH_STRAFE_RIGHT:
			{
				mBattleEngine->GetWalkerPart()->StrafeRight(val);
				break ;
			}
			case BUTTON_MECH_YAW_LEFT:
			case BUTTON_MECH_YAW_RIGHT:
			{
				mBattleEngine->GetWalkerPart()->Rotate(val) ;
				break ;
			}
			case BUTTON_MECH_PITCH_UP:
			case BUTTON_MECH_PITCH_DOWN:
			{
				mBattleEngine->GetWalkerPart()->Pitch(val) ;
				break ;
			}
			
		}
		return ;
	}


	if (mBattleEngine->GetState() == BATTLE_ENGINE_STATE_JET)
	{

		switch (button)
		{
		
			case BUTTON_MECH_YAW_LEFT:
			case BUTTON_MECH_YAW_RIGHT:
			{
				mBattleEngine->GetJetPart()->Turn(val) ;
				break ;
			}
			case BUTTON_MECH_PITCH_UP:
			case BUTTON_MECH_PITCH_DOWN:
			{
				mBattleEngine->GetJetPart()->Pitch(val) ;
				break ;
			}
			case BUTTON_MECH_STRAFE_LEFT:
			{
				mBattleEngine->GetJetPart()->YawLeft(val) ;
				break;
			}
			case BUTTON_MECH_STRAFE_RIGHT:
			{
				mBattleEngine->GetJetPart()->YawRight(val) ;
				break ;
			}
			case BUTTON_MECH_JET_AFTERBURNER:
			{
				//mBattleEngine->GetJetPart()->Afterburner() ;
				break ;
			}

			case BUTTON_MECH_FORWARD:
			{
				mBattleEngine->GetJetPart()->Thrust(val) ;
				break ;
			}
			
			case BUTTON_MECH_BACKWARD:
			{
				mBattleEngine->GetJetPart()->Thrust(val) ;
				break ;
			}
		}
		return ;
	
	}
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004d2810 ??_GCPlayer@@UAEPAXI@Z
