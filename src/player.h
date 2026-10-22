// Player.h: interface for the CPlayer class.
//
//////////////////////////////////////////////////////////////////////

#ifndef PLAYER_INCLUDE
#define PLAYER_INCLUDE

#include "action.h"
#include "battleengine.h"
#include "controller.h"

enum EPlayerStats
{
	PS_UNITSDESTROYED,
	PS_ROUNDSFIRED,
	PS_ROUNDSHIT,
	PS_CHEATED,
	PS_TIMEASJET,
	PS_TIMEASWALKER,
	PS_DAMAGETAKEN,		// *256 as damage is floating-point 

	PS_NUM_PLAYERSTATS
};



enum EKilledType
{
	TK_AIRCRAFT,
	TK_VEHICLES,
	TK_EMPLACEMENTS,
	TK_INFANTY,
	TK_MECHS,
	TK_TOTAL,		// SRG important note.  adding new types here means career structure will change

	TK_HACK_AGRADES,
	TK_HACK_SGRADES,
};



enum EPlayerEvent
{
	GOTO_CONTROL_VIEW = 4000
};


enum EPlayerCameraView
{
	PLAYER_PAN_VIEW,
	PLAYER_FP_VIEW,
	PLAYER_3RD_PERSON_VIEW
};


class CEvent ;

class CPlayer : public IController
{
public:
					CPlayer(int number);
	virtual			~CPlayer();
			void	Init() ;
	void			ReceiveButtonAction(CController* from_controller, int button, float val) ;
	virtual BOOL	CanBeControlledWhenInPause() { return FALSE ; }
	virtual EControlType  GetControlType() { return CONTROL_MECH ; }
	void			AssignBattleEngine(CBattleEngine* be);
	int				GetNumber() {return mNumber;}
	void			KilledEnemyThing(CUnit* thing);
	int				GetNumEnemyThingKilled(EKilledType type) { return mThingsKilled[type] ; }
	CBattleEngine*	GetBattleEngine() { return mBattleEngine.ToRead() ; }
	BOOL			IsGod() { return mIsGod; }
	BOOL			ShouldRenderInternalCockpit() { return mCurrentViewMode == PLAYER_FP_VIEW ; }
	BOOL			GetIsInFPV() { return mCurrentViewMode == PLAYER_FP_VIEW ; }
	EPlayerCameraView GetPreferredCurrentViewMode() { return mPreferedControlView ; }
	void			SetPreferedControlViewMode( EPlayerCameraView mode) ;
	FVector			GetCurrentViewPoint();
	FMatrix			GetCurrentViewOrientation();
	FVector			GetOldCurrentViewPoint();
	FMatrix			GetOldCurrentViewOrientation();
	void			GotoControlView();


	void			SetIsGod(BOOL val) ;
 
	void			SetStat(EPlayerStats s,int v) { mStat[s]=v; };
	int				GetStat(EPlayerStats s) { return(mStat[s]); };
	void			IncStat(EPlayerStats s,int v=1) { mStat[s]+=v; };
	void			WipeStats();
	virtual	void	HandleEvent(CEvent* event) ;
	void			GotoPanView(float time) ;
	void			GotoFPView();
	void			Goto3rdPersonView();
	void			KilledThing(EKilledType type) { mThingsKilled[type]++ ; }

	float			GetTimeoutTime() {return mTimeoutTime;};

private:

	CSArray<int, TK_TOTAL>			mThingsKilled;
	CActiveReader<CBattleEngine>	mBattleEngine ;
	BOOL							mIsGod ;
	EPlayerCameraView				mCurrentViewMode;
	EPlayerCameraView				mPreferedControlView;
	int								mNumber;
	int								mStat[PS_NUM_PLAYERSTATS];
	float							mTimeoutTime;
};

#endif 


