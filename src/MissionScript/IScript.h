// IScript.h: a thing's mission script as it runs (reconstructed from the retail object; not in Stuart's release). vm.h
// keeps the small view the rest of the game uses.
//
// IScript (vtable 005e4f08: CMonitor's slots, Shutdown overridden; 0x3c bytes) runs its thing's copy of the object
// code on the shared VM. The notifications raise the script's events; the natives are the functions a script calls
// (the table gScriptFunctions, 0064ce20), each taking the popped arguments and returning through result.

#ifndef ISCRIPT_H
#define ISCRIPT_H

#define ISCRIPT_FULL
#include "vminternal.h"

class CComplexThing;

#define DT_ANY		7				// a native's argument of any type (Print)

// the events a script can handle, by the object code's event-table index (CVM::CallEvent 00539990)
enum EScriptEvent
{
	SCRIPT_EVENT_INIT = 0,
	SCRIPT_EVENT_ARRIVED,
	SCRIPT_EVENT_TIMER,
	SCRIPT_EVENT_DIED,
	SCRIPT_EVENT_HIT,
	SCRIPT_EVENT_STARTED_DYING,
	SCRIPT_EVENT_READY,
	SCRIPT_EVENT_SHUTDOWN
};

// the messages an IScript handles (HandleEvent 005385e0)
enum EScriptMessage
{
	SCRIPT_MESSAGE_FOLLOW_WAYPOINT = 2000,	// UpdateWaypointFollowing reschedules it every frame
	SCRIPT_MESSAGE_RESUME,					// a Wait native's suspended VM carries on
	SCRIPT_MESSAGE_TIMER					// SetTimer's timer went off
};

class IScript : public CMonitor
{
public:
	IScript(CComplexThing *thing, CMissionScriptObjectCode *code);		// 005333b0
	~IScript();															// 00533450
	virtual void	HandleEvent(CEvent *event);							// 005385e0
	virtual void	Shutdown();											// 00533810

	void			Init();												// 00533500
	void			Ready();											// 005335a0
	void			Arrived(int value);								// 005335d0
	void			StartedDying();										// 00533660
	void			Hit(CComplexThing *other);							// 00533690
	void			Died();												// 005337e0
	void			FinishedPlayingAnim();								// 00533840

	// the natives
	void			SetSpeed(CDataType **args, int num_args, CDataType **result);                    // shared no-op (00453ac0)
	void			SetPlayerLives(CDataType **args, int num_args, CDataType **result);              // 005338a0
	void			SetSlot(CDataType **args, int num_args, CDataType **result);                     // 005338d0
	void			SetSlotSave(CDataType **args, int num_args, CDataType **result);                 // 00533900
	void			SetLockable(CDataType **args, int num_args, CDataType **result);                 // 00533950
	void			ToggleCockpit(CDataType **args, int num_args, CDataType **result);               // 00533980
	void			GetSlot(CDataType **args, int num_args, CDataType **result);                     // 005339a0
	void			MPDeclarePlayerWon(CDataType **args, int num_args, CDataType **result);          // 00533a40
	void			MPDeclareGameDrawn(CDataType **args, int num_args, CDataType **result);          // 00533a60
	void			SetGoodieState(CDataType **args, int num_args, CDataType **result);              // 00533a70
	void			GetGoodieState(CDataType **args, int num_args, CDataType **result);              // 00533aa0
	void			AddHelpMessage(CDataType **args, int num_args, CDataType **result);              // 00533b30
	void			Goto3PointPanCamera(CDataType **args, int num_args, CDataType **result);         // 00533b70
	void			Goto4PointPanCamera(CDataType **args, int num_args, CDataType **result);         // 00533eb0
	void			GotoPlayerCamera(CDataType **args, int num_args, CDataType **result);            // 005342b0
	void			SwitchMessagesOn(CDataType **args, int num_args, CDataType **result);            // 005342c0
	void			SwitchMessagesOff(CDataType **args, int num_args, CDataType **result);           // 005342e0
	void			SetSegmentVulnerable(CDataType **args, int num_args, CDataType **result);        // 00534300
	void			SetVelocity(CDataType **args, int num_args, CDataType **result);                 // 00534340
	void			HalfDestroy(CDataType **args, int num_args, CDataType **result);                 // 00534370
	void			SetAllSegmentsVulnerable(CDataType **args, int num_args, CDataType **result);    // 00534390
	void			AddScore(CDataType **args, int num_args, CDataType **result);                    // 005343c0
	void			PrimaryObjectiveComplete(CDataType **args, int num_args, CDataType **result);    // 005343e0
	void			SecondaryObjectiveComplete(CDataType **args, int num_args, CDataType **result);  // 00534410
	void			PrimaryObjectiveFailed(CDataType **args, int num_args, CDataType **result);      // 00534440
	void			SecondaryObjectiveFailed(CDataType **args, int num_args, CDataType **result);    // 00534470
	void			Launch(CDataType **args, int num_args, CDataType **result);                      // 005344a0
	void			Normalise(CDataType **args, int num_args, CDataType **result);                   // 00534500
	void			Magnitude(CDataType **args, int num_args, CDataType **result);                   // 005345d0
	void			GetAngle(CDataType **args, int num_args, CDataType **result);                    // 00534680
	void			GameTime(CDataType **args, int num_args, CDataType **result);                    // 00534770
	void			IsNumberBetween(CDataType **args, int num_args, CDataType **result);             // 005347b0
	void			Damage(CDataType **args, int num_args, CDataType **result);                      // 005348c0
	void			SetVar(CDataType **args, int num_args, CDataType **result);                      // 005348f0
	void			CreatePosition(CDataType **args, int num_args, CDataType **result);              // 00534910
	void			GetComponent(CDataType **args, int num_args, CDataType **result);                // 005349b0
	void			GetMapHeight(CDataType **args, int num_args, CDataType **result);                // 00534ac0
	void			GetWaterHeight(CDataType **args, int num_args, CDataType **result);              // 00534b30
	void			GetX(CDataType **args, int num_args, CDataType **result);                        // 00534b80
	void			GetY(CDataType **args, int num_args, CDataType **result);                        // 00534c10
	void			GetZ(CDataType **args, int num_args, CDataType **result);                        // 00534ca0
	void			SetX(CDataType **args, int num_args, CDataType **result);                        // 00534d30
	void			SetY(CDataType **args, int num_args, CDataType **result);                        // 00534dc0
	void			SetZ(CDataType **args, int num_args, CDataType **result);                        // 00534e50
	void			SetGoalPoint(CDataType **args, int num_args, CDataType **result);                // 00534ee0
	void			Stop(CDataType **args, int num_args, CDataType **result);                        // 00534f30
	void			Deploy(CDataType **args, int num_args, CDataType **result);                      // 00534f70
	void			Undeploy(CDataType **args, int num_args, CDataType **result);                    // 00534f90
	void			EnableWeapon(CDataType **args, int num_args, CDataType **result);                // 00534fb0
	void			DisableWeapon(CDataType **args, int num_args, CDataType **result);               // 00534fe0
	void			EnableSpawner(CDataType **args, int num_args, CDataType **result);               // 00535010
	void			DisableSpawner(CDataType **args, int num_args, CDataType **result);              // 00535040
	void			EnableFlightMode(CDataType **args, int num_args, CDataType **result);            // 00535070
	void			DisableFlightMode(CDataType **args, int num_args, CDataType **result);           // 00535090
	void			GetSafePos(CDataType **args, int num_args, CDataType **result);                  // 005350b0
	void			PlayAnimation(CDataType **args, int num_args, CDataType **result);               // 00535160
	void			PlayAnimationWait(CDataType **args, int num_args, CDataType **result);           // 005351d0
	void			GetRatioBattleLineNodes(CDataType **args, int num_args, CDataType **result);     // 005353a0
	void			SetSegmentHealth(CDataType **args, int num_args, CDataType **result);            // 00535480
	void			ResetSegmentHealth(CDataType **args, int num_args, CDataType **result);          // 005354c0
	void			SetAllSegmentsHealth(CDataType **args, int num_args, CDataType **result);        // 00535500
	void			SetStealth(CDataType **args, int num_args, CDataType **result);                  // 00535530
	void			SetAllegiance(CDataType **args, int num_args, CDataType **result);               // 00535560
	void			GetNumUnits(CDataType **args, int num_args, CDataType **result);                 // 00535590
	void			GetWeaponAmmo(CDataType **args, int num_args, CDataType **result);               // 00535610
	void			GetWeaponName(CDataType **args, int num_args, CDataType **result);               // 00535670
	void			GetWeaponCharge(CDataType **args, int num_args, CDataType **result);             // 00535750
	void			GetConfiguration(CDataType **args, int num_args, CDataType **result);            // 005357b0
	void			PlayCutscene(CDataType **args, int num_args, CDataType **result);                // 00535890
	void			SetTimer(CDataType **args, int num_args, CDataType **result);                    // 005358e0
	void			GetHealth(CDataType **args, int num_args, CDataType **result);                   // 00535920
	void			GetNumber(CDataType **args, int num_args, CDataType **result);                   // 00535980
	void			GetRealHealth(CDataType **args, int num_args, CDataType **result);               // 005359d0
	void			GetInitialHealth(CDataType **args, int num_args, CDataType **result);            // 00535a30
	void			SpawnersEmpty(CDataType **args, int num_args, CDataType **result);               // 00535a90
	void			SpawnersInUse(CDataType **args, int num_args, CDataType **result);               // 00535af0
	void			IsFiring(CDataType **args, int num_args, CDataType **result);                    // 00535b50
	void			GetEnergy(CDataType **args, int num_args, CDataType **result);                   // 00535bb0
	void			SetHealth(CDataType **args, int num_args, CDataType **result);                   // 00535c10
	void			SetScript(CDataType **args, int num_args, CDataType **result);                   // 00535c50
	void			SetName(CDataType **args, int num_args, CDataType **result);                     // 00535c70
	void			SetSpawnScript(CDataType **args, int num_args, CDataType **result);              // 00535ca0
	void			Die(CDataType **args, int num_args, CDataType **result);                         // 00535cd0
	void			Shutdown(CDataType **args, int num_args, CDataType **result);                    // 00535d00
	void			Retreat(CDataType **args, int num_args, CDataType **result);                     // 00535d30
	void			Activate(CDataType **args, int num_args, CDataType **result);                    // 00535d50
	void			Deactivate(CDataType **args, int num_args, CDataType **result);                  // 00535d60
	void			NumContained(CDataType **args, int num_args, CDataType **result);                // 00535d70
	void			HighlightHudPart(CDataType **args, int num_args, CDataType **result);            // 00535e60
	void			UnHighlightHudPart(CDataType **args, int num_args, CDataType **result);          // 00535e80
	void			SetVisible(CDataType **args, int num_args, CDataType **result);                  // 00535ea0
	void			SetObjective(CDataType **args, int num_args, CDataType **result);                // 00535ed0
	void			UnsetObjective(CDataType **args, int num_args, CDataType **result);              // 00535ee0
	void			IsObjective(CDataType **args, int num_args, CDataType **result);                 // 00535ef0
	void			SetVulnerable(CDataType **args, int num_args, CDataType **result);               // 00535f70
	void			Attack(CDataType **args, int num_args, CDataType **result);                      // 00535fa0
	void			GetDistToObj(CDataType **args, int num_args, CDataType **result);                // 00536070
	void			SetAIState(CDataType **args, int num_args, CDataType **result);                  // 005361a0
	void			Land(CDataType **args, int num_args, CDataType **result);                        // 005361d0
	void			Dive(CDataType **args, int num_args, CDataType **result);                        // 005361f0
	void			Surface(CDataType **args, int num_args, CDataType **result);                     // 00536210
	void			InitVariable(CDataType **args, int num_args, CDataType **result);                // 00536230
	void			SetVariable(CDataType **args, int num_args, CDataType **result);                 // 00536260
	void			GetVariable(CDataType **args, int num_args, CDataType **result);                 // 005362a0
	void			ShutdownVariable(CDataType **args, int num_args, CDataType **result);            // 00536330
	void			IsA(CDataType **args, int num_args, CDataType **result);                         // 00536350
	void			GetPlayer(CDataType **args, int num_args, CDataType **result);                   // 005363e0
	void			GetSquad(CDataType **args, int num_args, CDataType **result);                    // 005365c0
	void			GetTarget(CDataType **args, int num_args, CDataType **result);                   // 005366c0
	void			GetThingRef(CDataType **args, int num_args, CDataType **result);                 // 005367c0
	void			Exists(CDataType **args, int num_args, CDataType **result);                      // 00536920
	void			Teleport(CDataType **args, int num_args, CDataType **result);                    // 005369b0
	void			TeleportOrientation(CDataType **args, int num_args, CDataType **result);         // 00536a60
	void			SpawnParticle(CDataType **args, int num_args, CDataType **result);               // 00536b70
	void			GetPos(CDataType **args, int num_args, CDataType **result);                      // 00536c00
	void			SetPos(CDataType **args, int num_args, CDataType **result);                      // 00536c70
	void			TriggerHitEffect(CDataType **args, int num_args, CDataType **result);            // 00536ca0
	void			SpawnThing(CDataType **args, int num_args, CDataType **result);                  // 00536cd0
	void			SpawnEscapePod(CDataType **args, int num_args, CDataType **result);              // 005371e0
	void			AddMessage(CDataType **args, int num_args, CDataType **result);                  // 00537410
	void			PlayCharMessage(CDataType **args, int num_args, CDataType **result);             // 00537500
	void			PlayCharMessageWait(CDataType **args, int num_args, CDataType **result);         // 005375f0
	void			PlayPCharMessage(CDataType **args, int num_args, CDataType **result);            // 005377e0
	void			PlayPCharMessageWait(CDataType **args, int num_args, CDataType **result);        // 005378e0
	void			Print(CDataType **args, int num_args, CDataType **result);                       // 00537ad0
	void			PrintText(CDataType **args, int num_args, CDataType **result);                   // 00537c40
	void			Pause(CDataType **args, int num_args, CDataType **result);                       // 00537c70
	void			FollowWaypoint(CDataType **args, int num_args, CDataType **result);              // 00537d70
	void			FollowWaypointWait(CDataType **args, int num_args, CDataType **result);          // 00537e40
	void			IsFriendly(CDataType **args, int num_args, CDataType **result);                  // 00537fd0
	void			IsEnemy(CDataType **args, int num_args, CDataType **result);                     // 00538060
	void			InJetMode(CDataType **args, int num_args, CDataType **result);                   // 005380f0
	void			IsOverWater(CDataType **args, int num_args, CDataType **result);                 // 00538150
	void			LevelLost(CDataType **args, int num_args, CDataType **result);                   // 005381a0
	void			LevelLostString(CDataType **args, int num_args, CDataType **result);             // 005381c0
	void			LevelWon(CDataType **args, int num_args, CDataType **result);                    // 005381e0
	void			PlaySample(CDataType **args, int num_args, CDataType **result);                  // 005381f0
	void			Rand(CDataType **args, int num_args, CDataType **result);                        // 00538230
	void			GetFloatRand(CDataType **args, int num_args, CDataType **result);                // 00538290
	void			SetWindVector(CDataType **args, int num_args, CDataType **result);               // 00538300
	void			SetRainDensity(CDataType **args, int num_args, CDataType **result);              // 00538360
	void			SetSnowDensity(CDataType **args, int num_args, CDataType **result);              // 00538380
	void			SetLightningDensity(CDataType **args, int num_args, CDataType **result);         // 005383a0
	void			PostEvent(CDataType **args, int num_args, CDataType **result);                   // 005383c0

	void			UpdateWaypointFollowing();							// 00538470

	CComplexThing	*mDefaultThing;			// +0x08: the script's own thing
	CMissionScriptObjectCode	*mCode;		// +0x0c
	CThing			*mThing;				// +0x10: the thing a native acts on; the POINTER instruction sets it
	class CWaypoint	*mWaypoint;				// +0x14: the point the thing is heading for
	BOOL			mFollowingWaypoints;	// +0x18
	BOOL			mWaitingForWaypoints;	// +0x1c: FollowWaypointWait's script resumes at the end
	CVM				*mWaypointVM;			// +0x20: the VM FollowWaypointWait suspended
	int				mArrivedValue;			// +0x24: Arrived passes it at the end (FollowWaypoint's second argument)
	SPtrSet<CVM>	mSavedVMs;				// +0x28: the VMs the Wait natives suspended
	CVM				*mAnimVM;				// +0x38: the VM PlayAnimationWait suspended
};

// the payload of a PostEvent: the event's name (vtable 005e4f34); the world owns it until it is delivered
class CPostEventData : public CMonitor
{
public:
	CPostEventData(CDataType *name);		// PostEvent inlines it
	~CPostEventData();						// 005386d0

	CDataType		*mName;					// +0x08
};

extern SScriptFunction	gScriptFunctions[];
extern int				gNumScriptFunctions;

#endif
