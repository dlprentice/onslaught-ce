// IScript.cpp: a thing's mission script and the natives scripts call (reconstructed from the retail object,
// 0052fe30..0053873f; not in Stuart's release). The allocations' line numbers are retail's (__FILE__ 0064fa40).
#define ISCRIPT_CPP
#include "..\common.h"
#include "..\Listener.h"	// before vm.h: the declaration order decides the order of the RTTI records (retail: CVM's locator before IListener's)
#include "..\monitor.h"
#include "IScript.h"
#include "EventFunction.h"
#include "scripteventnb.h"
#include "..\game.h"
#include "..\career.h"
#include "..\player.h"
#include "..\BattleEngine.h"
#include "..\unit.h"
#include "..\WorldPhysicsManager.h"
#include "..\HelpText.h"
#include "..\FrontEndText.h"
#include "..\Camera.h"
#include "..\BSpline.h"
#include "..\MessageBox.h"
#include "..\DestructableSegmentsController.h"
#include "..\eventmanager.h"
#include "..\map.h"
#include "..\mesh.h"
#include "..\InfluenceMap.h"
#include "..\InfluenceNode.h"
#include "..\world.h"
#include "..\Cutscene.h"
#include "..\BattleEngineDataManager.h"
#include "..\Component.h"
#include "..\Hud.h"
#include "..\squad.h"
#include "..\ParticleManager.h"
#include "..\SoundManager.h"
#include "..\atmospherics.h"
#include "..\WaypointManager.h"
#include "..\scheduledevent.h"
#include "..\text.h"
#include "..\InitThing.h"
#include "..\RenderThing.h"
#include "..\EscapePod.h"
#include "..\DebugLog.h"

// the maths constants' initializers come first (mathsconst.h)
// FUNCTION: 0x0052fe30 init:ZERO_FVECTOR
// FUNCTION: 0x0052fe50 init:ID_FMATRIX

//******************************************************************************************
// the natives by the CALL instruction's index; the handlers make the initializer dynamic
// FUNCTION: 0x0052ff20 init:gScriptFunctions
// FUNCTION: 0x0052ff30 initbody:gScriptFunctions
SScriptFunction gScriptFunctions[] =
{
	{"FollowWaypoint",             2, FALSE, {DT_STRING, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::FollowWaypoint},
	{"FollowWaypointWait",         1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::FollowWaypointWait},
	{"SetSpeed",                   1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSpeed},
	{"SpawnThing",                 4, FALSE, {DT_STRING, DT_STRING, DT_INT, DT_STRING, 0, 0, 0}, DT_NONE, &IScript::SpawnThing},
	{"Pause",                      1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Pause},
	{"PostEvent",                  1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PostEvent},
	{"Rand",                       1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::Rand},
	{"IsFriendly",                 0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsFriendly},
	{"LevelLost",                  0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::LevelLost},
	{"LevelWon",                   0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::LevelWon},
	{"PlaySample",                 1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PlaySample},
	{"Print",                      1, FALSE, {DT_ANY, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Print},
	{"Exists",                     1, TRUE, {DT_THINGPTR, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::Exists},
	{"Die",                        0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Die},
	{"GetThingRef",                1, TRUE, {DT_THINGPTR, 0, 0, 0, 0, 0, 0}, DT_THINGPTR, &IScript::GetThingRef},
	{"Activate",                   0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Activate},
	{"PrintText",                  1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PrintText},
	{"AddMessage",                 2, FALSE, {DT_INT, DT_FLOAT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::AddMessage},
	{"GetHealth",                  0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetHealth},
	{"SetTimer",                   1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetTimer},
	{"GetDistToObj",               1, TRUE, {DT_THINGPTR, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetDistToObj},
	{"GetPlayer",                  1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_THINGPTR, &IScript::GetPlayer},
	{"SetVisible",                 1, FALSE, {DT_BOOL, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetVisible},
	{"SetObjective",               0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetObjective},
	{"SetAIState",                 1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::SetAIState},
	{"IsA",                        1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsA},
	{"Teleport",                   1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Teleport},
	{"PlayCutscene",               1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PlayCutscene},
	{"PlayCharMessage",            3, FALSE, {DT_INT, DT_INT, DT_FLOAT, 0, 0, 0, 0}, DT_NONE, &IScript::PlayCharMessage},
	{"Deactivate",                 0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Deactivate},
	{"UnsetObjective",             0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::UnsetObjective},
	{"IsObjective",                0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsObjective},
	{"SetVulnerable",              1, FALSE, {DT_BOOL, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetVulnerable},
	{"NumContained",               1, TRUE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::NumContained},
	{"HighlightHudPart",           1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::HighlightHudPart},
	{"UnHighlightHudPart",         1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::UnHighlightHudPart},
	{"PlayCharMessageWait",        3, FALSE, {DT_INT, DT_INT, DT_FLOAT, 0, 0, 0, 0}, DT_NONE, &IScript::PlayCharMessageWait},
	{"GetWeaponAmmo",              0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetWeaponAmmo},
	{"GetWeaponName",              0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_STRING, &IScript::GetWeaponName},
	{"GetWeaponCharge",            0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetWeaponCharge},
	{"GetNumUnits",                2, TRUE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_INT, &IScript::GetNumUnits},
	{"SetAllegiance",              1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetAllegiance},
	{"SetWindVector",              3, FALSE, {DT_FLOAT, DT_FLOAT, DT_FLOAT, 0, 0, 0, 0}, DT_NONE, &IScript::SetWindVector},
	{"SetRainDensity",             1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetRainDensity},
	{"SetSnowDensity",             1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSnowDensity},
	{"SetLightningDensity",        1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetLightningDensity},
	{"SetHealth",                  1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetHealth},
	{"GetRatioBattleLineNodes",    1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetRatioBattleLineNodes},
	{"SetScript",                  1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetScript},
	{"GetPos",                     0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::GetPos},
	{"SpawnParticle",              2, FALSE, {DT_STRING, DT_POSITION, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SpawnParticle},
	{"SetSegmentHealth",           2, FALSE, {DT_STRING, DT_FLOAT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSegmentHealth},
	{"SetAllSegmentsHealth",       1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetAllSegmentsHealth},
	{"PlayAnimation",              3, FALSE, {DT_STRING, DT_BOOL, DT_BOOL, 0, 0, 0, 0}, DT_NONE, &IScript::PlayAnimation},
	{"PlayAnimationWait",          3, FALSE, {DT_STRING, DT_BOOL, DT_BOOL, 0, 0, 0, 0}, DT_NONE, &IScript::PlayAnimationWait},
	{"GetMapHeight",               1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetMapHeight},
	{"GetWaterHeight",             0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetWaterHeight},
	{"GetX",                       1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetX},
	{"GetY",                       1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetY},
	{"GetZ",                       1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetZ},
	{"SetX",                       2, TRUE, {DT_POSITION, DT_FLOAT, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::SetX},
	{"SetY",                       2, TRUE, {DT_POSITION, DT_FLOAT, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::SetY},
	{"SetZ",                       2, TRUE, {DT_POSITION, DT_FLOAT, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::SetZ},
	{"SetGoalPoint",               1, FALSE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetGoalPoint},
	{"GetSafePos",                 0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::GetSafePos},
	{"GetComponent",               1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_THINGPTR, &IScript::GetComponent},
	{"CreatePosition",             3, TRUE, {DT_FLOAT, DT_FLOAT, DT_FLOAT, 0, 0, 0, 0}, DT_POSITION, &IScript::CreatePosition},
	{"GetFloatRand",               0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetFloatRand},
	{"SetVar",                     2, FALSE, {DT_STRING, DT_ANY, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetVar},
	{"Damage",                     1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Damage},
	{"GameTime",                   0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GameTime},
	{"IsEnemy",                    0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsEnemy},
	{"Land",                       0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::Land},
	{"SpawnersEmpty",              0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::SpawnersEmpty},
	{"Dive",                       0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::Dive},
	{"Surface",                    0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::Surface},
	{"InitVariable",               2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::InitVariable},
	{"SetVariable",                3, FALSE, {DT_INT, DT_FLOAT, DT_FLOAT, 0, 0, 0, 0}, DT_FLOAT, &IScript::SetVariable},
	{"ShutdownVariable",           1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::ShutdownVariable},
	{"Shutdown",                   0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Shutdown},
	{"GetEnergy",                  0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetEnergy},
	{"GetVariable",                1, TRUE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::GetVariable},
	{"Retreat",                    0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Retreat},
	{"PrimaryObjectiveComplete",   2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PrimaryObjectiveComplete},
	{"SecondaryObjectiveComplete", 2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SecondaryObjectiveComplete},
	{"AddScore",                   1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::AddScore},
	{"Attack",                     1, FALSE, {DT_THINGPTR, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Attack},
	{"PrimaryObjectiveFailed",     2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::PrimaryObjectiveFailed},
	{"SecondaryObjectiveFailed",   2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SecondaryObjectiveFailed},
	{"GetAngle",                   1, TRUE, {DT_THINGPTR, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetAngle},
	{"PlayPCharMessage",           4, FALSE, {DT_INT, DT_INT, DT_FLOAT, DT_INT, 0, 0, 0}, DT_NONE, &IScript::PlayPCharMessage},
	{"PlayPCharMessageWait",       4, FALSE, {DT_INT, DT_INT, DT_FLOAT, DT_INT, 0, 0, 0}, DT_NONE, &IScript::PlayPCharMessageWait},
	{"GetInitialHealth",           0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetInitialHealth},
	{"SpawnersInUse",              0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::SpawnersInUse},
	{"SetSpawnScript",             1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSpawnScript},
	{"Stop",                       0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Stop},
	{"Deploy",                     0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Deploy},
	{"Undeploy",                   0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Undeploy},
	{"EnableWeapon",               1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::EnableWeapon},
	{"DisableWeapon",              1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::DisableWeapon},
	{"EnableFlightMode",           0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::EnableFlightMode},
	{"DisableFlightMode",          0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::DisableFlightMode},
	{"GetSquad",                   0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_THINGPTR, &IScript::GetSquad},
	{"GetTarget",                  0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_THINGPTR, &IScript::GetTarget},
	{"Normalise",                  1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_POSITION, &IScript::Normalise},
	{"Magnitude",                  1, TRUE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::Magnitude},
	{"LevelLostString",            1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::LevelLostString},
	{"IsFiring",                   0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsFiring},
	{"IsNumberBetween",            3, TRUE, {DT_FLOAT, DT_FLOAT, DT_FLOAT, 0, 0, 0, 0}, DT_BOOL, &IScript::IsNumberBetween},
	{"SetSegmentVulnerable",       2, FALSE, {DT_STRING, DT_BOOL, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSegmentVulnerable},
	{"SetAllSegmentsVulnerable",   1, FALSE, {DT_BOOL, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetAllSegmentsVulnerable},
	{"GetRealHealth",              0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_FLOAT, &IScript::GetRealHealth},
	{"SwitchMessagesOn",           0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SwitchMessagesOn},
	{"SwitchMessagesOff",          0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SwitchMessagesOff},
	{"Goto3PointPanCamera",        5, FALSE, {DT_THINGPTR, DT_POSITION, DT_POSITION, DT_POSITION, DT_FLOAT, 0, 0}, DT_NONE, &IScript::Goto3PointPanCamera},
	{"Goto4PointPanCamera",        6, FALSE, {DT_THINGPTR, DT_POSITION, DT_POSITION, DT_POSITION, DT_POSITION, DT_FLOAT, 0}, DT_NONE, &IScript::Goto4PointPanCamera},
	{"GotoPlayerCamera",           0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::GotoPlayerCamera},
	{"GetConfiguration",           0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_STRING, &IScript::GetConfiguration},
	{"AddHelpMessage",             1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::AddHelpMessage},
	{"SetGoodieState",             2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetGoodieState},
	{"GetGoodieState",             1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::GetGoodieState},
	{"MPDeclarePlayerWon",         1, FALSE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::MPDeclarePlayerWon},
	{"MPDeclareGameDrawn",         0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::MPDeclareGameDrawn},
	{"SetSlot",                    2, FALSE, {DT_INT, DT_BOOL, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSlot},
	{"GetSlot",                    1, TRUE, {DT_INT, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::GetSlot},
	{"InJetMode",                  0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::InJetMode},
	{"SetPlayerLives",             2, FALSE, {DT_INT, DT_INT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetPlayerLives},
	{"Launch",                     1, FALSE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::Launch},
	{"HalfDestroy",                0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::HalfDestroy},
	{"SetVelocity",                1, FALSE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetVelocity},
	{"TriggerHitEffect",           1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::TriggerHitEffect},
	{"GetNumber",                  0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_INT, &IScript::GetNumber},
	{"SpawnEscapePod",             0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SpawnEscapePod},
	{"SetSlotSave",                2, FALSE, {DT_INT, DT_BOOL, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetSlotSave},
	{"ResetSegmentHealth",         2, FALSE, {DT_STRING, DT_FLOAT, 0, 0, 0, 0, 0}, DT_NONE, &IScript::ResetSegmentHealth},
	{"SetPos",                     1, FALSE, {DT_POSITION, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetPos},
	{"SetLockable",                1, FALSE, {DT_BOOL, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetLockable},
	{"ToggleCockpit",              0, FALSE, {0, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::ToggleCockpit},
	{"SetStealth",                 1, FALSE, {DT_FLOAT, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetStealth},
	{"TeleportOrientation",        3, FALSE, {DT_FLOAT, DT_FLOAT, DT_FLOAT, 0, 0, 0, 0}, DT_NONE, &IScript::TeleportOrientation},
	{"EnableSpawner",              1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::EnableSpawner},
	{"DisableSpawner",             1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::DisableSpawner},
	{"SetName",                    1, FALSE, {DT_STRING, 0, 0, 0, 0, 0, 0}, DT_NONE, &IScript::SetName},
	{"IsOverWater",                0, TRUE, {0, 0, 0, 0, 0, 0, 0}, DT_BOOL, &IScript::IsOverWater},
};

int	gNumScriptFunctions = sizeof(gScriptFunctions) / sizeof(gScriptFunctions[0]);	// 0064f220

CDataType	*gEventArg;				// 0089c528: the one argument an event passes

static WCHAR	sSpeaker[256];			// 0089c328: AddMessage's speaker


//******************************************************************************************
IScript::IScript(CComplexThing *thing, CMissionScriptObjectCode *code)	// FUNCTION: 0x005333b0
{
	mDefaultThing = thing;
	mCode = code;
	code->mScript = this;
	mWaypoint = NULL;
	mArrivedValue = 0;
	mFollowingWaypoints = FALSE;
	mWaitingForWaypoints = FALSE;
	mThing = thing;
	mAnimVM = NULL;
}
// FUNCTION: 0x00533430 ??_GIScript@@UAEPAXI@Z

//******************************************************************************************
IScript::~IScript()	// FUNCTION: 0x00533450
{
	delete mCode;
	mCode = NULL;
	mSavedVMs.DeleteAll();
}

//******************************************************************************************
void	IScript::Init()	// FUNCTION: 0x00533500
{
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_INIT, NULL, 0);

	// the event functions listen for their named events
	SPtrSet<CEventFunction>	*functions = &mCode->mEventFunctions;
	CEventFunction	*function;
	FOR_ALL_ITEMS_IN(*functions, function)
	{
		ListIterator<SEventParam>	it(&function->mParams);
		for (SEventParam *param = it.First(); param; param = it.Next())
			param->mEvent = SCRIPT_EVENT_NB.RegisterEventListener(param, function);
	}
}

//******************************************************************************************
void	IScript::Ready()	// FUNCTION: 0x005335a0
{
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_READY, &gEventArg, 0);
}

//******************************************************************************************
void	IScript::Arrived(int value)	// FUNCTION: 0x005335d0
{
	if (MISSION_SCRIPT_VM.mRunning || GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		return;

#line 269
	gEventArg = new (MT_SCRIPT) CIntDataType(value);
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
	{
		delete gEventArg;
		MISSION_SCRIPT_VM.Clean();
	}
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_ARRIVED, &gEventArg, 1);
}

//******************************************************************************************
void	IScript::StartedDying()	// FUNCTION: 0x00533660
{
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_STARTED_DYING, &gEventArg, 0);
}

//******************************************************************************************
void	IScript::Hit(CComplexThing *other)	// FUNCTION: 0x00533690
{
	if (MISSION_SCRIPT_VM.mRunning || GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		return;

#line 286
	gEventArg = new (MEMTYPE_SCRIPT_THING_PTR) CThingPtrDataType((CThing *)other);
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
	{
		delete gEventArg;
		MISSION_SCRIPT_VM.Clean();
	}
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_HIT, &gEventArg, 1);
}

//******************************************************************************************
void	IScript::Died()	// FUNCTION: 0x005337e0
{
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_DIED, &gEventArg, 0);
}

//******************************************************************************************
void	IScript::Shutdown()	// FUNCTION: 0x00533810
{
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_SHUTDOWN, &gEventArg, 0);
}

//******************************************************************************************
// PlayAnimationWait's script resumes where it stopped
void	IScript::FinishedPlayingAnim()	// FUNCTION: 0x00533840
{
	if (!mAnimVM)
		return;

	MISSION_SCRIPT_VM = *mAnimVM;
	mSavedVMs.Remove(mAnimVM);
	delete mAnimVM;
	mAnimVM = NULL;
	int	pc = MISSION_SCRIPT_VM.mPC;
	if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
		MISSION_SCRIPT_VM.Clean();
	else
		MISSION_SCRIPT_VM.GotoInstruction(pc);
}

//******************************************************************************************
// the natives
void	IScript::SetSpeed(CDataType **args, int num_args, CDataType **result)
{
}

//******************************************************************************************
void	IScript::SetPlayerLives(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005338a0
{
	GAME.SetPlayerLives(args[0]->GetInt(), args[1]->GetInt());
}

//******************************************************************************************
void	IScript::SetSlot(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005338d0
{
	GAME.SetSlot(args[0]->GetInt(), args[1]->IsTrue());
}

//******************************************************************************************
// the slot is saved with the career too
void	IScript::SetSlotSave(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533900
{
	GAME.SetSlot(args[0]->GetInt(), args[1]->IsTrue());
	CAREER.SetSlot(args[0]->GetInt(), args[1]->IsTrue());
}

//******************************************************************************************
void	IScript::SetLockable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533950
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CUnitData	*data = ((CUnit *)mThing)->mUnitData;
		if (data)
			data->mCanBeLocked = args[0]->IsTrue();
	}
}

//******************************************************************************************
void	IScript::ToggleCockpit(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533980
{
	CPlayer	*player = GAME.GetPlayer(0);
	if (player)
	{
		CBattleEngine	*be = player->GetBattleEngine();
		if (be)
			be->ToggleCockpit();
	}
}

//******************************************************************************************
void	IScript::GetSlot(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005339a0
{
#line 381
	*result = new (MT_SCRIPT) CBoolDataType(GAME.GetSlot(args[0]->GetInt()) != 0);
}

//******************************************************************************************
void	IScript::MPDeclarePlayerWon(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533a40
{
	GAME.MPDeclarePlayerWon(args[0]->GetInt());
}

//******************************************************************************************
void	IScript::MPDeclareGameDrawn(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533a60
{
	GAME.MPDeclareGameDrawn();
}

//******************************************************************************************
// the scripts number the goodies from 1
void	IScript::SetGoodieState(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533a70
{
	CAREER.SetGoodieState(args[0]->GetInt() - 1, (EGoodieState)args[1]->GetInt());
}

//******************************************************************************************
void	IScript::GetGoodieState(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533aa0
{
#line 406
	*result = new (MT_SCRIPT) CIntDataType(CAREER.GetGoodieState(args[0]->GetInt() - 1));
}

//******************************************************************************************
void	IScript::AddHelpMessage(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533b30
{
	CHelpTextDisplay	*help = GAME.GetHelpTextDisplay();
	if (help && args[0]->GetInt() == 2302408)
		help->AddMessage(Localization_GetStringById(0xdd));
}

//******************************************************************************************
// a pan along points given in the thing's frame (as CPlayer::GotoPanView builds one)
// The thing is held through a const reference to GetThing()'s result: each inlined CThing::GetOrientation then keeps
// its `this` parameter alive (an indirect argument), which numbers the inlined FMatrix * FVector's symbols as retail's
// and so gives each row's term order (X, Z, Y at 00533c4d; README "Pan cameras"). The camera goes straight to
// SetCurrentCamera.
void	IScript::Goto3PointPanCamera(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533b70
{
	CThing	*const &thing = args[0]->GetThing();
	if (thing == NULL)
	{
		LOG.AddMessage("FATAL ERROR: null thing passed to 'Create3PointPanCamera'");
		return;
	}

#line 485
	SPtrSet<FVector>	*points = new (MEMTYPE_PLAYER) SPtrSet<FVector>;
	points->Append(new (MEMTYPE_CAMERA) FVector(thing->GetOrientation() * args[1]->GetPosition()));
	points->Append(new (MEMTYPE_CAMERA) FVector(thing->GetOrientation() * args[2]->GetPosition()));
	points->Append(new (MEMTYPE_CAMERA) FVector(thing->GetOrientation() * args[3]->GetPosition()));
	CBSpline	*spline = new (MEMTYPE_PLAYER) CBSpline(points);
	GAME.SetCurrentCamera(0, new (MEMTYPE_CAMERA) CPanCamera(thing, spline, args[4]->GetFloat()));
}

//******************************************************************************************
// the thing's orientation, for the 4-point builder's stand-in below
static inline FMatrix &OrientationOf(CThing *thing)
{
	return thing->GetOrientation();
}

//******************************************************************************************
// STAND-IN (spelling, consumed values only): the points read the thing's orientation three
// ways. That numbers each inlined FMatrix * FVector's symbols as retail's, which gives retail's term order per point
// (rows X,Z,Y / Z,Y,X / X,Z,Y / Z,Y,X; the vector loaded first only in point 1's last product; README "Pan cameras"):
// point 1 through a const reference to the thing pointer (GetOrientation keeps its `this` alive), point 2 directly,
// points 3 and 4 through OrientationOf (one more inlined parameter around GetOrientation). No uniform spelling fits:
// the reference, the helper or the plain form at every point misses for any number of extra locals. Own size 603 keeps
// only the first two FVector constructors expanded (564..610; op* nested budgets 57, 53, 45, 31 against 47).
void	IScript::Goto4PointPanCamera(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00533eb0
{
	CCamera	*camera = NULL;
	CThing	*thing = args[0]->GetThing();
	CThing	*const &t = thing;
	if (thing == NULL)
	{
		LOG.AddMessage("FATAL ERROR: null thing passed to 'Create4PointPanCamera'");
		return;
	}

#line 508
	SPtrSet<FVector>	*points = new (MEMTYPE_PLAYER) SPtrSet<FVector>;
	points->Append(new (MEMTYPE_CAMERA) FVector(t->GetOrientation() * args[1]->GetPosition()));
	points->Append(new (MEMTYPE_CAMERA) FVector(thing->GetOrientation() * args[2]->GetPosition()));
	points->Append(new (MEMTYPE_CAMERA) FVector(OrientationOf(thing) * args[3]->GetPosition()));
	points->Append(new (MEMTYPE_CAMERA) FVector(OrientationOf(thing) * args[4]->GetPosition()));
	CBSpline	*spline = new (MEMTYPE_PLAYER) CBSpline(points);
	camera = new (MEMTYPE_CAMERA) CPanCamera(thing, spline, args[5]->GetFloat());
	GAME.SetCurrentCamera(0, camera);
}

//******************************************************************************************
void	IScript::GotoPlayerCamera(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005342b0
{
	GAME.GetPlayer(0)->GotoControlView();
}

//******************************************************************************************
void	IScript::SwitchMessagesOn(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005342c0
{
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
		box->SwitchMessagesOn();
}

//******************************************************************************************
void	IScript::SwitchMessagesOff(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005342e0
{
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
		box->SwitchMessagesOff();
}

//******************************************************************************************
void	IScript::SetSegmentVulnerable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534300
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CDestructableSegmentsController	*segments = ((CUnit *)mThing)->mSegments;
		if (segments)
			segments->SetSegmentVulnerable(args[0]->GetString(), args[1]->IsTrue());
	}
}

//******************************************************************************************
void	IScript::SetVelocity(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534340
{
	CThing	*thing = mThing;
	if (thing->IsA(THING_TYPE_UNIT))
		thing->SetVelocity(args[0]->GetPosition());
}

//******************************************************************************************
void	IScript::HalfDestroy(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534370
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->DamageRandomly();
}

//******************************************************************************************
void	IScript::SetAllSegmentsVulnerable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534390
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CDestructableSegmentsController	*segments = ((CUnit *)mThing)->mSegments;
		if (segments)
			segments->SetAllSegmentsVulnerable(args[0]->IsTrue());
	}
}

//******************************************************************************************
void	IScript::AddScore(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005343c0
{
	GAME.IncScore(args[0]->GetInt());
}

//******************************************************************************************
void	IScript::PrimaryObjectiveComplete(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005343e0
{
	GAME.SetPrimaryObjectiveComplete(args[0]->GetInt() - 1, args[1]->GetInt());
}

//******************************************************************************************
void	IScript::SecondaryObjectiveComplete(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534410
{
	GAME.SetSecondaryObjectiveComplete(args[0]->GetInt() - 1, args[1]->GetInt());
}

//******************************************************************************************
void	IScript::PrimaryObjectiveFailed(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534440
{
	GAME.SetPrimaryObjectiveFailed(args[0]->GetInt() - 1, args[1]->GetInt());
}

//******************************************************************************************
void	IScript::SecondaryObjectiveFailed(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534470
{
	GAME.SetSecondaryObjectiveFailed(args[0]->GetInt() - 1, args[1]->GetInt());
}

//******************************************************************************************
void	IScript::Launch(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005344a0
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CUnitData	*data = ((CUnit *)mThing)->mUnitData;
		if (data && data->mBehaviour == UB_POD)
			((CPod *)mThing)->Launch(args[0]->GetPosition());
	}
}

//******************************************************************************************
void	IScript::Normalise(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534500
{
	FVector	v = args[0]->GetPosition();
	v.Normalise();
#line 636
	*result = new (MT_SCRIPT) CPositionDataType(v);
}

//******************************************************************************************
void	IScript::Magnitude(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005345d0
{
#line 643
	*result = new (MT_SCRIPT) CFloatDataType(args[0]->GetPosition().Magnitude());
}

//******************************************************************************************
// how far the thing would have to turn to face another, in degrees
void	IScript::GetAngle(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534680
{
	float	angle = 0.f;

	if (args[0]->GetThing())
	{
		FVector	offset = args[0]->GetThing()->GetPos() - mThing->GetPos();
		float	yaw = ((CComplexThing *)mThing)->GetOrientation().GetColumnY().Azimuth();
		angle = (float)fabs(AngleDifference(yaw, offset.Azimuth())) * (180.f / PI);
	}

#line 659
	*result = new (MT_SCRIPT) CFloatDataType(angle);
}

//******************************************************************************************
void	IScript::GameTime(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534770
{
#line 666
	*result = new (MT_SCRIPT) CFloatDataType(EVENT_MANAGER.GetTime());
}

//******************************************************************************************
// either way round
void	IScript::IsNumberBetween(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005347b0
{
	if (args[0]->GetFloat() >= args[1]->GetFloat() && args[0]->GetFloat() <= args[2]->GetFloat())
	{
#line 676
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

	if (args[0]->GetFloat() <= args[1]->GetFloat() && args[0]->GetFloat() >= args[2]->GetFloat())
	{
#line 683
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

#line 687
	*result = new (MT_SCRIPT) CBoolDataType(false);
}

//******************************************************************************************
void	IScript::Damage(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005348c0
{
	mThing->Damage(args[0]->GetFloat(), mDefaultThing);
}

//******************************************************************************************
void	IScript::SetVar(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005348f0
{
	((CComplexThing *)mThing)->SetVar((CStringDataType *)args[0], args[1]);
}

//******************************************************************************************
void	IScript::CreatePosition(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534910
{
	FVector	pos(args[0]->GetFloat(), args[1]->GetFloat(), args[2]->GetFloat());
#line 710
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
// a unit's component by its number
void	IScript::GetComponent(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005349b0
{
	CUnit	*unit = (CUnit *)mThing;
	CUnit	*component = NULL;

	if (unit->IsA(THING_TYPE_UNIT))
	{
		CActiveReader<CUnit>	*reader = unit->FindChild(args[0]->GetInt());
		if (reader)
			component = reader->ToRead();
	}

	if (!component)
		LOG.AddMessage("ERROR: Can't find component %d, in call to GetComponent", args[0]->GetInt());

#line 731
	*result = new (MT_SCRIPT) CThingPtrDataType(component);
}

//******************************************************************************************
void	IScript::GetMapHeight(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534ac0
{
	float	height = MAP.Collide(args[0]->GetPosition());
#line 739
	*result = new (MT_SCRIPT) CFloatDataType(height);
}

//******************************************************************************************
void	IScript::GetWaterHeight(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534b30
{
	float	level = MAP.GetWaterLevel();	// copied through the FPU before the new (00534b31), as map.h's GetWaterLevel does
#line 748
	*result = new (MT_SCRIPT) CFloatDataType(level);
}

//******************************************************************************************
void	IScript::GetX(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534b80
{
#line 755
	*result = new (MT_SCRIPT) CFloatDataType(args[0]->GetPosition().X);
}

//******************************************************************************************
void	IScript::GetY(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534c10
{
#line 762
	*result = new (MT_SCRIPT) CFloatDataType(args[0]->GetPosition().Y);
}

//******************************************************************************************
void	IScript::GetZ(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534ca0
{
#line 767
	*result = new (MT_SCRIPT) CFloatDataType(args[0]->GetPosition().Z);
}

//******************************************************************************************
void	IScript::SetX(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534d30
{
	FVector	pos = args[0]->GetPosition();
	pos.X = args[1]->GetFloat();
#line 776
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
void	IScript::SetY(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534dc0
{
	FVector	pos = args[0]->GetPosition();
	pos.Y = args[1]->GetFloat();
#line 785
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
void	IScript::SetZ(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534e50
{
	FVector	pos = args[0]->GetPosition();
	pos.Z = args[1]->GetFloat();
#line 794
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
void	IScript::SetGoalPoint(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534ee0
{
	((CUnit *)mThing)->GoToPoint(args[0]->GetPosition());
}

//******************************************************************************************
void	IScript::Stop(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534f30
{
	CUnit	*unit = (CUnit *)mThing;
	unit->GoToPoint(unit->GetPos());
}

//******************************************************************************************
void	IScript::Deploy(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534f70
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->StartDeploying();
}

//******************************************************************************************
void	IScript::Undeploy(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534f90
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->StartUndeploying();
}

//******************************************************************************************
void	IScript::EnableWeapon(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534fb0
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->EnableWeapon(args[0]->GetString());
}

//******************************************************************************************
void	IScript::DisableWeapon(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00534fe0
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->DisableWeapon(args[0]->GetString());
}

//******************************************************************************************
void	IScript::EnableSpawner(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535010
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->EnableSpawner(args[0]->GetString());
}

//******************************************************************************************
void	IScript::DisableSpawner(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535040
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->DisableSpawner(args[0]->GetString());
}

//******************************************************************************************
void	IScript::EnableFlightMode(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535070
{
	if (mThing->IsA(THING_TYPE_BATTLE_ENGINE))
		((CBattleEngine *)mThing)->EnableFlightMode();
}

//******************************************************************************************
void	IScript::DisableFlightMode(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535090
{
	if (mThing->IsA(THING_TYPE_BATTLE_ENGINE))
		((CBattleEngine *)mThing)->DisableFlightMode();
}

//******************************************************************************************
void	IScript::GetSafePos(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005350b0
{
	FVector	pos(0.f, 0.f, 0.f);
	if (mThing->IsA(THING_TYPE_UNIT))
		pos = ((CUnit *)mThing)->Unknown110();
#line 876
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
void	IScript::PlayAnimation(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535160
{
	CRenderThing	*render = mThing->GetRenderThing();
	if (render)
	{
		CMESH	*mesh = render->GetRTMesh();
		if (mesh)
		{
			EAnimMode	mode = mesh->GetAnimModeByName(args[0]->GetString());
			((CComplexThing *)mThing)->SetAnimMode(mode, args[1]->IsTrue(), args[2]->IsTrue());
		}
	}
}

//******************************************************************************************
// the script waits for the animation to finish (FinishedPlayingAnim resumes it)
void	IScript::PlayAnimationWait(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005351d0
{
	CComplexThing	*thing = (CComplexThing *)mThing;
	CRenderThing	*render = thing->GetRenderThing();

	if (!thing->GetMissionScript() || thing->GetMissionScript() != this)
		LOG.AddMessage("FATAL ERROR: Called PlayAnimWait on the non base script object");

	if (!render)
		return;
	CMESH	*mesh = render->GetRTMesh();
	if (!mesh)
		return;

	if (mAnimVM)
	{
		mSavedVMs.Remove(mAnimVM);
		delete mAnimVM;
	}

	EAnimMode	mode = mesh->GetAnimModeByName(args[0]->GetString());
	((CComplexThing *)mThing)->SetAnimMode(mode, args[1]->IsTrue(), args[2]->IsTrue());

#line 918
	CVM	*vm = new (MT_SCRIPT) CVM(MISSION_SCRIPT_VM);
	mSavedVMs.Append(vm);
	MISSION_SCRIPT_VM.mStop = TRUE;
	mAnimVM = vm;
}
// CVM's implicit destructor is emitted here, after the first function that needs its vtable
// FUNCTION: 0x00535330 ??_GCVM@@UAEPAXI@Z
// FUNCTION: 0x00535350 ??1CVM@@UAE@XZ

//******************************************************************************************
// the share of the battle line's nodes a side holds (0 for side 2, 1 for side 1)
void	IScript::GetRatioBattleLineNodes(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005353a0
{
	int		side = args[0]->GetInt();
	SPtrSet<CInfluenceNode>	*nodes = &INFLUENCEMAP->mNodes;
	float	total = (float)nodes->Size();
	float	held = 0.f;

	CInfluenceNode	*node;
	FOR_ALL_ITEMS_IN(*nodes, node)
	{
		if ((node->mSide == 1 && side == 1) || (node->mSide == 2 && side == 0))
			held += 1.f;
	}

	float	ratio = 0.f;
	if (total != 0.f)
		ratio = held / total;
	else
		LOG.AddMessage("Warning: No nodes found!!!");

#line 958
	*result = new (MT_SCRIPT) CFloatDataType(ratio);
}

//******************************************************************************************
void	IScript::SetSegmentHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535480
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CDestructableSegmentsController	*segments = ((CUnit *)mThing)->mSegments;
		if (segments)
			segments->SetSegmentHealth(args[0]->GetString(), args[1]->GetFloat());
	}
}

//******************************************************************************************
void	IScript::ResetSegmentHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005354c0
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CDestructableSegmentsController	*segments = ((CUnit *)mThing)->mSegments;
		if (segments)
			segments->ResetSegmentHealth(args[0]->GetString(), args[1]->GetFloat());
	}
}

//******************************************************************************************
void	IScript::SetAllSegmentsHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535500
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CDestructableSegmentsController	*segments = ((CUnit *)mThing)->mSegments;
		if (segments)
			segments->SetAllSegmentsHealth(args[0]->GetFloat());
	}
}

//******************************************************************************************
void	IScript::SetStealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535530
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->Unknown114(args[0]->GetFloat());
}

//******************************************************************************************
void	IScript::SetAllegiance(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535560
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->SetAllegiance(args[0]->GetInt());
}

//******************************************************************************************
// the world's count of a unit type on a side (0 or 1)
void	IScript::GetNumUnits(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535590
{
	int	type = args[0]->GetInt();
	int	count = 0;

	int	side = args[1]->GetInt();
	if (side == 0)
		count = WORLD.mCountersA[type];
	else if (side == 1)
		count = WORLD.mCountersB[type];

#line 1036
	*result = new (MT_SCRIPT) CIntDataType(count);
}

//******************************************************************************************
void	IScript::GetWeaponAmmo(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535610
{
	float	ammo = 0.f;
	if (mThing->IsA(THING_TYPE_BATTLE_ENGINE))
		ammo = ((CBattleEngine *)mThing)->GetWeaponAmmoPercentage();
#line 1051
	*result = new (MT_SCRIPT) CFloatDataType(ammo);
}

//******************************************************************************************
void	IScript::GetWeaponName(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535670
{
	CBattleEngine	*be = (CBattleEngine *)mThing;
	if (be->IsA(THING_TYPE_BATTLE_ENGINE))
	{
#line 1060
		*result = new (MT_SCRIPT) CStringDataType(be->GetWeaponPhysicsName());
		return;
	}

#line 1064
	*result = new (MT_SCRIPT) CStringDataType("");
}

//******************************************************************************************
void	IScript::GetWeaponCharge(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535750
{
	float	charge = 0.f;
	if (mThing->IsA(THING_TYPE_BATTLE_ENGINE))
		charge = ((CBattleEngine *)mThing)->GetWeaponCharge();
#line 1079
	*result = new (MT_SCRIPT) CFloatDataType(charge);
}

//******************************************************************************************
void	IScript::GetConfiguration(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005357b0
{
	CBattleEngine	*be = (CBattleEngine *)mThing;
	if (be->IsA(THING_TYPE_BATTLE_ENGINE))
	{
#line 1088
		*result = new (MT_SCRIPT) CStringDataType(be->GetConfigurationName());
		return;
	}

#line 1092
	*result = new (MT_SCRIPT) CStringDataType("");
}

//******************************************************************************************
void	IScript::PlayCutscene(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535890
{
	char			*name = args[0]->GetString();
	CComplexThing	*thing = WORLD.FindThingByName(name);
	if (thing && thing->IsA(THING_TYPE_CUTSCENE))
		((CCutscene *)thing)->Start();
	else
		LOG.AddMessage("Error: Can't find cut scene '%s'", name);
}

//******************************************************************************************
void	IScript::SetTimer(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005358e0
{
	float	time = args[0]->GetFloat();
	EVENT_MANAGER.AddEvent(time, SCRIPT_MESSAGE_TIMER, this);
}

//******************************************************************************************
void	IScript::GetHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535920
{
	float	value = 0.f;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->GetLife();
#line 1132
	*result = new (MT_SCRIPT) CFloatDataType(value);
}

//******************************************************************************************
void	IScript::GetNumber(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535980
{
	int	value = 0;
	if (mThing->IsA(THING_TYPE_COMPONENT))
		value = ((CComponent *)mThing)->mNumber;
#line 1143
	*result = new (MT_SCRIPT) CIntDataType(value);
}

//******************************************************************************************
void	IScript::GetRealHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005359d0
{
	float	value = 0.f;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->GetLifeUnknown4f9a40();
#line 1158
	*result = new (MT_SCRIPT) CFloatDataType(value);
}

//******************************************************************************************
void	IScript::GetInitialHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535a30
{
	float	value = 0.f;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->GetMaxLife();
#line 1173
	*result = new (MT_SCRIPT) CFloatDataType(value);
}

//******************************************************************************************
void	IScript::SpawnersEmpty(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535a90
{
	BOOL	value = FALSE;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->AllSpawnersReady();
#line 1188
	*result = new (MT_SCRIPT) CBoolDataType(value);
}

//******************************************************************************************
void	IScript::SpawnersInUse(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535af0
{
	BOOL	value = FALSE;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->AnySpawnerUnknown4e4420();
#line 1203
	*result = new (MT_SCRIPT) CBoolDataType(value);
}

//******************************************************************************************
void	IScript::IsFiring(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535b50
{
	BOOL	value = FALSE;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->AnyWeaponUnknown50a290();
#line 1218
	*result = new (MT_SCRIPT) CBoolDataType(value);
}

//******************************************************************************************
void	IScript::GetEnergy(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535bb0
{
	float	value = 0.f;
	if (mThing->IsA(THING_TYPE_UNIT))
		value = ((CUnit *)mThing)->GetEnergy();
#line 1233
	*result = new (MT_SCRIPT) CFloatDataType(value);
}

//******************************************************************************************
// a fraction of the unit's full life
void	IScript::SetHealth(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535c10
{
	CUnit	*unit = (CUnit *)mThing;
	if (unit->IsA(THING_TYPE_UNIT))
	{
		float	fraction = args[0]->GetFloat();
		if (unit->mUnitData)
			unit->mLife = fraction * unit->mUnitData->mUnknownC0;
	}
}

//******************************************************************************************
void	IScript::SetScript(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535c50
{
	((CComplexThing *)mThing)->SetScript(args[0]->GetString());
}

//******************************************************************************************
void	IScript::SetName(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535c70
{
	mThing->SetName(args[0]->GetString());
}

//******************************************************************************************
void	IScript::SetSpawnScript(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535ca0
{
	((CComplexThing *)mThing)->SetSpawnScript(args[0]->GetString());
}

//******************************************************************************************
// the thing dies and shuts down on the next frame, through its own events
void	IScript::Die(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535cd0
{
	EVENT_MANAGER.AddEvent(START_DIE_PROCESS, mThing, NEXT_FRAME);
}

//******************************************************************************************
void	IScript::Shutdown(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535d00
{
	EVENT_MANAGER.AddEvent(SHUTDOWN, mThing, NEXT_FRAME);
}

//******************************************************************************************
void	IScript::Retreat(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535d30
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->Unknown100();
}

//******************************************************************************************
void	IScript::Activate(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535d50
{
	mThing->Activate();
}

//******************************************************************************************
void	IScript::Deactivate(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535d60
{
	mThing->Deactivate();
}

//******************************************************************************************
// how many units of the named kind the thing carries
void	IScript::NumContained(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535d70
{
	char	*name = args[0]->GetString();
	SPtrSet<CActiveReader<CUnit> >	*contents = (SPtrSet<CActiveReader<CUnit> > *)((CComplexThing *)mThing)->GetContainedInside();
	int		count = 0;

	if (contents)
	{
		CActiveReader<CUnit>	*reader;
		FOR_ALL_ITEMS_IN(*contents, reader)
		{
			CUnit	*unit = reader->ToRead();
			if (unit && unit->IsA(THING_TYPE_UNIT) && unit->mUnitData && unit->mUnitData->mName &&
				strcmp(unit->mUnitData->mName, name) == 0)
				count++;
		}
	}

#line 1329
	*result = new (MT_SCRIPT) CIntDataType(count);
}

//******************************************************************************************
void	IScript::HighlightHudPart(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535e60
{
	HUD.mPartState[args[0]->GetInt()] = 2;
}

//******************************************************************************************
void	IScript::UnHighlightHudPart(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535e80
{
	HUD.mPartState[args[0]->GetInt()] = 1;
}

//******************************************************************************************
void	IScript::SetVisible(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535ea0
{
	if (args[0]->IsTrue() == TRUE)
		mThing->MakeVisible();
	else
		mThing->MakeInvisible();
}

//******************************************************************************************
void	IScript::SetObjective(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535ed0
{
	mThing->SetObjective(TRUE);
}

//******************************************************************************************
void	IScript::UnsetObjective(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535ee0
{
	mThing->SetObjective(FALSE);
}

//******************************************************************************************
void	IScript::IsObjective(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535ef0
{
	if (mThing->IsObjective())
	{
#line 1381
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

#line 1385
	*result = new (MT_SCRIPT) CBoolDataType(false);
}

//******************************************************************************************
void	IScript::SetVulnerable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535f70
{
	CThing	*thing = mThing;
	thing->SetVulnerable(args[0]->IsTrue());
}

//******************************************************************************************
// a unit attacks a unit or a squad's leader; a squad attacks a squad or a unit's squad
void	IScript::Attack(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00535fa0
{
	if (!args[0]->GetThing())
	{
		LOG.AddMessage("Warning: Called Attack with NULL object");
		return;
	}

	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CUnit	*target = args[0]->GetThing()->IsA(THING_TYPE_UNIT) ? (CUnit *)args[0]->GetThing() :
						  args[0]->GetThing()->IsA(THING_TYPE_UNKNOWN_20000000) ? ((CSquad *)args[0]->GetThing())->Unknown74() : NULL;
		if (target)
			((CUnit *)mThing)->SetSquadTarget(target);
	}
	else if (mThing->IsA(THING_TYPE_UNKNOWN_20000000))
	{
		CSquad	*target = args[0]->GetThing()->IsA(THING_TYPE_UNIT) ? ((CUnit *)args[0]->GetThing())->GetSquad() :
						  args[0]->GetThing()->IsA(THING_TYPE_UNKNOWN_20000000) ? (CSquad *)args[0]->GetThing() : NULL;
		if (target)
			((CSquad *)mThing)->Unknown85((SINT)target);
	}
}

//******************************************************************************************
void	IScript::GetDistToObj(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536070
{
	FVector	pos = mThing->GetPos();

	if (!args[0]->GetThing())
	{
		LOG.AddMessage("Warning: Called GetDistToObj with NULL object");
#line 1440
		*result = new (MT_SCRIPT) CFloatDataType(0.f);
		return;
	}

	FVector	other = args[0]->GetThing()->GetPos();
	FVector	offset = other - pos;
#line 1446
	*result = new (MT_SCRIPT) CFloatDataType(offset.Magnitude());
}

//******************************************************************************************
void	IScript::SetAIState(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005361a0
{
	mThing->SetAIState((EAIState)args[0]->GetInt());
}

//******************************************************************************************
void	IScript::Land(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005361d0
{
	if (mThing->IsA(THING_TYPE_UNIT))
		((CUnit *)mThing)->Unknown93();
}

//******************************************************************************************
void	IScript::Dive(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005361f0
{
	if (mThing->IsA(THING_TYPE_UNKNOWN_10000000))
		((CSubmarine *)mThing)->Dive();
}

//******************************************************************************************
void	IScript::Surface(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536210
{
	if (mThing->IsA(THING_TYPE_UNKNOWN_10000000))
		((CSubmarine *)mThing)->Surface();
}

//******************************************************************************************
// the variables a script shows on screen are the world's text slots
void	IScript::InitVariable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536230
{
	WORLD.AddText(args[0]->GetInt(), args[1]->GetInt());
}

//******************************************************************************************
void	IScript::SetVariable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536260
{
	WORLD.SetTextTime(args[0]->GetInt(), args[1]->GetFloat(), args[2]->GetFloat());
}

//******************************************************************************************
void	IScript::GetVariable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005362a0
{
#line 1491
	*result = new (MT_SCRIPT) CFloatDataType(WORLD.GetTextTime(args[0]->GetInt()));
}

//******************************************************************************************
void	IScript::ShutdownVariable(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536330
{
	WORLD.RemoveText(args[0]->GetInt());
}

//******************************************************************************************
void	IScript::IsA(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536350
{
	if (mThing->IsA(args[0]->GetInt()))
	{
#line 1505
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

#line 1509
	*result = new (MT_SCRIPT) CBoolDataType(false);
}

//******************************************************************************************
// a player's battle engine, by the player's number from 1
void	IScript::GetPlayer(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005363e0
{
	CBattleEngine	*be = NULL;

	int	number = args[0]->GetInt() - 1;
	if (number < 0)
	{
		LOG.AddMessage("Warning: sorry no player %d returning default player", args[0]->GetInt());
		number = 1;
	}

	CPlayer	*player = GAME.GetPlayer(number);
	if (player)
		be = player->GetBattleEngine();
	else
	{
		LOG.AddMessage("Fatal error: Player %d has no battle engine!!!", args[0]->GetInt());
#line 1533
		*result = new (MT_SCRIPT) CThingPtrDataType(mThing);
	}

#line 1535
	*result = new (MT_SCRIPT) CThingPtrDataType(be);
}

//******************************************************************************************
void	IScript::GetSquad(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005365c0
{
	CSquad	*squad = NULL;
	if (mThing->IsA(THING_TYPE_UNIT))
		squad = ((CUnit *)mThing)->GetSquad();
#line 1546
	*result = new (MT_SCRIPT) CThingPtrDataType(squad);
}

//******************************************************************************************
void	IScript::GetTarget(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005366c0
{
	CUnit	*target = NULL;
	if (mThing->IsA(THING_TYPE_UNIT))
		target = ((CUnit *)mThing)->GetCurrentTarget();
#line 1557
	*result = new (MT_SCRIPT) CThingPtrDataType(target);
}

//******************************************************************************************
void	IScript::GetThingRef(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005367c0
{
	char			*name = args[0]->GetString();
	CComplexThing	*thing = WORLD.FindThingByName(name);
	if (thing)
	{
#line 1567
		*result = new (MT_SCRIPT) CThingPtrDataType(thing);
		return;
	}

	LOG.AddMessage("FATAL ERROR: Can't find object '%s' in call to 'GetThingRef'", name);
#line 1572
	*result = new (MT_SCRIPT) CThingPtrDataType((CThing *)NULL);
}

//******************************************************************************************
void	IScript::Exists(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536920
{
	if (!args[0]->GetThing())
	{
#line 1581
		*result = new (MT_SCRIPT) CBoolDataType(false);
		return;
	}

#line 1585
	*result = new (MT_SCRIPT) CBoolDataType(true);
}

//******************************************************************************************
// to a position, or to a named thing's
void	IScript::Teleport(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005369b0
{
	FVector	pos(0.f, 0.f, 0.f);

	if (args[0]->GetType() == DT_POSITION)
		pos = args[0]->GetPosition();
	else
	{
		char			*name = args[0]->GetString();
		CComplexThing	*thing = WORLD.FindThingByName(name);
		if (thing)
			pos = thing->GetPos();
		else
		{
			LOG.AddMessage("ERROR: Can't find thing '%s'", name);
			return;
		}
	}

	mThing->Teleport(pos);
}

//******************************************************************************************
// yaw, pitch and roll in degrees
void	IScript::TeleportOrientation(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536a60
{
	FMatrix	ori(args[0]->GetFloat() * (PI / 180.f), args[1]->GetFloat() * (PI / 180.f), args[2]->GetFloat() * (PI / 180.f));
	mThing->TeleportOrientation(ori);
}

//******************************************************************************************
void	IScript::SpawnParticle(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536b70
{
	char	*name = args[0]->GetString();
	FVector	pos = args[1]->GetPosition();

	CParticleDescriptor	*pd = PARTICLE_SET.GetPD(name);
	if (!pd)
	{
		LOG.AddMessage("Can't find particle effect %s", name);
		return;
	}

	PARTICLE_MANAGER.CreateEffect(pd, NULL, pos, 0, 0);
}

//******************************************************************************************
void	IScript::GetPos(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536c00
{
	FVector	pos = mThing->GetPos();
#line 1646
	*result = new (MT_SCRIPT) CPositionDataType(pos);
}

//******************************************************************************************
void	IScript::SetPos(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536c70
{
	FVector	pos = args[0]->GetPosition();
	mThing->Teleport(pos);
}

//******************************************************************************************
void	IScript::TriggerHitEffect(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536ca0
{
	if (mThing->IsA(THING_TYPE_UNIT))
	{
		float	amount = args[0]->GetFloat();
		((CUnit *)mThing)->Unknown107(amount);
	}
}


static inline CThing *SpawnThingByName(char *name)
{
	return UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(name));
}
//******************************************************************************************
// a thing of the named unit type at one of this thing's emitters (or where this thing is), with a script; a unit
// gets a squad of its own
// Spawning through the by-name helper puts retail's out-of-line GetUnitIndex call here (00536d36; the helper's body
// budget is 1537/18 = 85, under its size 91). The direction is two products, `(ori * FVector(0, 100, 0)).Azimuth()` and
// `.Elevation()`, as BattleEngine.cpp writes `(mOrientation * FVector(0, 1, 0)).Elevation()`: a shared local would hide
// one product's inline budget (about 280) and keep FVector::Magnitude inline where retail calls it out of line
// (004026b0). The pitch waits in a local until after the script's length: stored first, init.mPitch is ordered before
// strlen's `repnz scas` and the scheduler issues the store ahead of it, where retail's fstp follows the scas
// (00536e5b..61).
void	IScript::SpawnThing(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00536cd0
{
	char	*type = args[0]->GetString();
	char	*script = args[3]->GetString();
	char	*emitter = args[1]->GetString();
	int		emitter_index = args[2]->GetInt();

	CRenderThing	*render = mThing->GetRenderThing();
	if (!render)
		return;

	FVector	pos;
	FMatrix	ori;
	render->GetRTEmitter(emitter, emitter_index, pos, ori, FALSE, FALSE);

	CThing	*thing = SpawnThingByName(type);
	if (!thing)
		return;

	CUnitInitThing	init;
	init.mStats = UPhysicsManager::GetUnitData(type);

	init.mYaw = (ori * FVector(0.f, 100.f, 0.f)).Azimuth();
	float	pitch = (ori * FVector(0.f, 100.f, 0.f)).Elevation();

	int		len = strlen(script);
	init.mPitch = pitch;
	if (len)
		strncpy(init.mScript, script, len + 1);

	if (pos == FVector(0.f, 0.f, 0.f))
	{
		init.mPos = mThing->GetPos();
		CEulerAngles	angles(((CComplexThing *)mThing)->GetOrientation());
		init.mYaw = angles.mYaw;
		init.mPitch = angles.mPitch;
		init.mRoll = angles.mRoll;
	}
	else
		init.mPos = pos;

	init.mVelocity = FVector(0.f, 0.f, 0.f);
	init.mAllegiance = (EAllegiance)2;
	if (mThing->IsA(THING_TYPE_UNIT))
		init.mAllegiance = ((CUnit *)mThing)->GetAllegiance();
	init.mInitCST.mTimeBeforeStart = 1.5f;
	init.mSpawnedBy = (CUnit *)mThing;

	if (stricmp(emitter, "SpawnerA") == 0)
		init.mWaypointPath = (EEmitterType)15;
	else if (stricmp(emitter, "SpawnerB") == 0)
		init.mWaypointPath = (EEmitterType)16;
	else if (stricmp(emitter, "SpawnerC") == 0)
		init.mWaypointPath = (EEmitterType)17;
	else if (stricmp(emitter, "SpawnerD") == 0)
		init.mWaypointPath = (EEmitterType)18;
	else if (stricmp(emitter, "SpawnerE") == 0)
		init.mWaypointPath = (EEmitterType)19;

	thing->Init(&init);

	if (mThing->IsA(THING_TYPE_UNIT))
	{
		CSquadInitThing	squad_init;
		squad_init.mPos = thing->GetPos();
		squad_init.mOrientation = ((CComplexThing *)thing)->GetOrientation();
		squad_init.mAllegiance = ((CUnit *)thing)->GetAllegiance();
		strcpy(squad_init.mUnitName, type);

		CSquad	*squad = UPhysicsManager::SpawnSquad(UPhysicsManager::GetUnitData(type)->mBehaviour);
		if (squad)
		{
			squad->mUnknown84 = TRUE;
			squad->mUnknown80 = TRUE;
			squad->Init(&squad_init);
			squad->Unknown67((CUnit *)thing, 0, 0);
			squad->mUnknown80 = FALSE;
		}
	}
}

//******************************************************************************************
// an escape pod from the thing's SpawnerF emitter (or where the thing is)
// The direction is two products, as in SpawnThing, which is what makes retail's out-of-line CActor constructor
// (004bfd20, from CEscapePod's) and FVector::Magnitude (in Elevation) follow from the inline budget. The pod's set-up sits
// inside `if (pod)`: with an early `if (!pod) return;` the scheduler issued the allegiance and delay stores before the
// call's `push` (the vtable load waited for the delay store, so both stores outranked the push), retail after it. The
// arm order was found with an arm-swap scan; every order of the statements inside had been tried first.
void	IScript::SpawnEscapePod(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005371e0
{
	CRenderThing	*render = mThing->GetRenderThing();
	if (!render)
		return;

	FVector	pos;
	FMatrix	ori;
	render->GetRTEmitter("SpawnerF", 1, pos, ori, FALSE, FALSE);

	CEscapePod	*pod = new CEscapePod;
	if (pod)
	{
		CInitThing	init;
		init.mYaw = (ori * FVector(0.f, 100.f, 0.f)).Azimuth();
		init.mPitch = (ori * FVector(0.f, 100.f, 0.f)).Elevation();

		if (pos == FVector(0.f, 0.f, 0.f))
			init.mPos = mThing->GetPos();
		else
			init.mPos = pos;

		init.mVelocity = FVector(0.f, 0.f, 0.f);
		init.mAllegiance = (EAllegiance)2;
		init.mInitCST.mTimeBeforeStart = 1.5f;
		pod->Init(&init);
	}
}

//******************************************************************************************
// a message from nobody in particular
void	IScript::AddMessage(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537410
{
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
	{
		wcscpy(sSpeaker, ToWCHAR("(unknown)"));
		CUnit	*from = NULL;
		if (mThing->IsA(THING_TYPE_UNIT))
			from = (CUnit *)mThing;
		WCHAR	*text = TEXT.GetString(args[0]->GetInt());
		float	time = args[1]->GetFloat();
#line 1807
		box->AddMessage(new (MT_SCRIPT) CMessage(sSpeaker, text, args[0]->GetInt(), time, from, NULL, 10));
	}
}

//******************************************************************************************
void	IScript::PlayCharMessage(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537500
{
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
	{
		WCHAR	*speaker = TEXT.GetString(args[0]->GetInt());
		WCHAR	*text = TEXT.GetString(args[1]->GetInt());
		float	time = args[2]->GetFloat();
		CUnit	*from = NULL;
		if (mThing->IsA(THING_TYPE_UNIT))
			from = (CUnit *)mThing;
#line 1829
		box->AddMessage(new (MT_SCRIPT) CMessage(speaker, text, args[1]->GetInt(), time, from, NULL, 10));
	}
}

//******************************************************************************************
// the script carries on once the message has played
void	IScript::PlayCharMessageWait(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005375f0
{
	WCHAR	*speaker = TEXT.GetString(args[0]->GetInt());
	WCHAR	*text = TEXT.GetString(args[1]->GetInt());
	float	time = args[2]->GetFloat();

#line 1844
	CVM	*vm = new (MT_SCRIPT) CVM(MISSION_SCRIPT_VM);
	mSavedVMs.Append(vm);
	MISSION_SCRIPT_VM.mStop = TRUE;

	CScheduledEvent	*done = EVENT_MANAGER.GetNextFreeEvent();
	if (!done)
		return;
	done->Set(SCRIPT_MESSAGE_RESUME, 0.05f, this, vm);

	CUnit	*from = NULL;
	if (mThing->IsA(THING_TYPE_UNIT))
		from = (CUnit *)mThing;
#line 1857
	CMessage	*message = new (MT_SCRIPT) CMessage(speaker, text, args[1]->GetInt(), time, from, done, 10);
	if (GAME.GetMessageBox())
		GAME.GetMessageBox()->AddMessage(message);
	else
	{
		TRACE("SHIT this should never happen");
		delete message;
	}
}

//******************************************************************************************
// with a priority
void	IScript::PlayPCharMessage(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005377e0
{
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
	{
		WCHAR	*speaker = TEXT.GetString(args[0]->GetInt());
		WCHAR	*text = TEXT.GetString(args[1]->GetInt());
		float	time = args[2]->GetFloat();
		SINT	priority = args[3]->GetInt();
		CUnit	*from = NULL;
		if (mThing->IsA(THING_TYPE_UNIT))
			from = (CUnit *)mThing;
#line 1887
		box->AddMessage(new (MT_SCRIPT) CMessage(speaker, text, args[1]->GetInt(), time, from, NULL, priority));
	}
}

//******************************************************************************************
void	IScript::PlayPCharMessageWait(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005378e0
{
	WCHAR	*speaker = TEXT.GetString(args[0]->GetInt());
	WCHAR	*text = TEXT.GetString(args[1]->GetInt());
	float	time = args[2]->GetFloat();
	SINT	priority = args[3]->GetInt();

#line 1902
	CVM	*vm = new (MT_SCRIPT) CVM(MISSION_SCRIPT_VM);
	mSavedVMs.Append(vm);
	MISSION_SCRIPT_VM.mStop = TRUE;

	CScheduledEvent	*done = EVENT_MANAGER.GetNextFreeEvent();
	if (!done)
		return;
	done->Set(SCRIPT_MESSAGE_RESUME, 0.05f, this, vm);

	CUnit	*from = NULL;
	if (mThing->IsA(THING_TYPE_UNIT))
		from = (CUnit *)mThing;
	CMessageBox	*box = GAME.GetMessageBox();
	if (box)
#line 1917
		box->AddMessage(new (MT_SCRIPT) CMessage(speaker, text, args[1]->GetInt(), time, from, done, priority));
}

//******************************************************************************************
// a value of any type to the debug log
void	IScript::Print(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537ad0
{
	CDataType	*value = args[0];

	switch (value->GetType())
	{
	case DT_INT:
		LOG.AddMessage("%d", value->GetInt());
		break;
	case DT_FLOAT:
		LOG.AddMessage("%f", value->GetFloat());
		break;
	case DT_BOOL:
		if (value->IsTrue() == TRUE)
			LOG.AddMessage("TRUE");
		else
			LOG.AddMessage("FALSE");
		break;
	case DT_STRING:
		LOG.AddMessage("%s", value->GetString());
		break;
	case DT_THINGPTR:
		LOG.AddMessage("%08x", value->GetThing());
		break;
	case DT_POSITION:
		LOG.AddMessage("x = %.4f, y = %.4f, z = %.4f", value->GetPosition().X, value->GetPosition().Y, value->GetPosition().Z);
		break;
	default:
		LOG.AddMessage("unknown type");
		break;
	}
}

//******************************************************************************************
void	IScript::PrintText(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537c40
{
	LOG.AddMessage("%w", TEXT.GetString(args[0]->GetInt()));
}

//******************************************************************************************
// the script carries on after time seconds (HandleEvent resumes it)
void	IScript::Pause(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537c70
{
	float	time = args[0]->GetFloat();

#line 1960
	CVM	*vm = new (MT_SCRIPT) CVM(MISSION_SCRIPT_VM);
	mSavedVMs.Append(vm);
	MISSION_SCRIPT_VM.mStop = TRUE;
	EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_RESUME, this, EVENT_MANAGER.GetTime() + time, START_OF_FRAME, vm);
}

//******************************************************************************************
// the thing heads along a named path from its nearest point; value goes to the script's Arrived event at the end
void	IScript::FollowWaypoint(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537d70
{
	char		*name = args[0]->GetString();
	CWaypoint	*waypoint = CWaypointManager::FindNearest(name, &mThing->GetPos());
	if (!waypoint)
	{
		LOG.AddMessage("Cant find waypoint path '%s'", name);
		return;
	}

	mArrivedValue = args[1]->GetInt();
	((CUnit *)mThing)->GoToPoint(waypoint->GetPos());

	if (mThing == mDefaultThing)
	{
		mWaypoint = waypoint;
		mWaitingForWaypoints = FALSE;
		if (mFollowingWaypoints != TRUE)
		{
			mFollowingWaypoints = TRUE;
			EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_FOLLOW_WAYPOINT, this, NEXT_FRAME);
		}
	}
}

//******************************************************************************************
// the script waits until the thing reaches the end of the path
void	IScript::FollowWaypointWait(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537e40
{
	if (mFollowingWaypoints == TRUE)
		return;

	char	*name = args[0]->GetString();
	mWaypoint = CWaypointManager::FindNearest(name, &mThing->GetPos());
	if (!mWaypoint)
	{
		LOG.AddMessage("FATAL ERROR: Cant find waypoint path '%s'", name);
		return;
	}

	if (mThing != mDefaultThing)
	{
		LOG.AddMessage("FATAL ERROR:  Cant Follow waypoint way for other object");
		return;
	}

	((CUnit *)mDefaultThing)->GoToPoint(mWaypoint->GetPos());
	mWaitingForWaypoints = TRUE;

#line 2042
	CVM	*vm = new (MT_SCRIPT) CVM(MISSION_SCRIPT_VM);
	mSavedVMs.Append(vm);
	mWaypointVM = vm;
	MISSION_SCRIPT_VM.mStop = TRUE;
	mFollowingWaypoints = TRUE;
	EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_FOLLOW_WAYPOINT, this, NEXT_FRAME);
}

//******************************************************************************************
void	IScript::IsFriendly(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00537fd0
{
	if (mThing->IsA(THING_TYPE_UNIT) && ((CUnit *)mThing)->GetAllegiance() == 0)
	{
#line 2057
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

#line 2061
	*result = new (MT_SCRIPT) CBoolDataType(false);
}

//******************************************************************************************
void	IScript::IsEnemy(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538060
{
	if (mThing->IsA(THING_TYPE_UNIT) && ((CUnit *)mThing)->GetAllegiance() == 1)
	{
#line 2071
		*result = new (MT_SCRIPT) CBoolDataType(true);
		return;
	}

#line 2075
	*result = new (MT_SCRIPT) CBoolDataType(false);
}

//******************************************************************************************
void	IScript::InJetMode(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005380f0
{
	BOOL	jet = FALSE;
	if (mThing->IsA(THING_TYPE_BATTLE_ENGINE) && !((CBattleEngine *)mThing)->IsWalking())
		jet = TRUE;
#line 2090
	*result = new (MT_SCRIPT) CBoolDataType(jet != 0);
}

//******************************************************************************************
void	IScript::IsOverWater(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538150
{
	BOOL	over = mThing->IsOverWater();
#line 2098
	*result = new (MT_SCRIPT) CBoolDataType(over != 0);
}

//******************************************************************************************
void	IScript::LevelLost(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005381a0
{
	GAME.DeclareLevelLost();
}

//******************************************************************************************
// the reason is a string id
void	IScript::LevelLostString(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005381c0
{
	GAME.DeclareLevelLost(args[0]->GetInt());
}

//******************************************************************************************
void	IScript::LevelWon(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005381e0
{
	GAME.DeclareLevelWon();
}

//******************************************************************************************
void	IScript::PlaySample(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005381f0
{
	SOUND.PlayNamedSample(args[0]->GetString(), mThing);
}

//******************************************************************************************
// 0 to n - 1
void	IScript::Rand(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538230
{
	int	value = GAME.Random() % args[0]->GetInt();
#line 2132
	*result = new (MT_SCRIPT) CIntDataType(value);
}

//******************************************************************************************
// -1 to 1
void	IScript::GetFloatRand(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538290
{
	int	value = GAME.Random();
#line 2141
	*result = new (MT_SCRIPT) CFloatDataType(float(value % 65536) / 65536.f);
}

//******************************************************************************************
void	IScript::SetWindVector(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538300
{
	CAtmospherics::mWindVector = FVector(args[0]->GetFloat(), args[1]->GetFloat(), args[2]->GetFloat());
}

//******************************************************************************************
void	IScript::SetRainDensity(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538360
{
	CAtmospherics::mRainDensity = args[0]->GetFloat();
}

//******************************************************************************************
void	IScript::SetSnowDensity(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x00538380
{
	CAtmospherics::mSnowDensity = args[0]->GetFloat();
}

//******************************************************************************************
void	IScript::SetLightningDensity(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005383a0
{
	CAtmospherics::mLightningDensity = args[0]->GetFloat();
}

//******************************************************************************************
// the named event reaches its listeners next frame (CScriptEventNB::HandleEvent)
void	IScript::PostEvent(CDataType **args, int num_args, CDataType **result)	// FUNCTION: 0x005383c0
{
#line 2230
	CPostEventData	*data = new (MT_SCRIPT) CPostEventData(args[0]->Clone());
	EVENT_MANAGER.AddEvent(CScriptEventNB::SCRIPT_EVENT_NB_POST, &SCRIPT_EVENT_NB, NEXT_FRAME, START_OF_FRAME, data);
}

//******************************************************************************************
// each frame while following a path: on to the next point once close enough to this one
// Near miss (2 instructions): retail schedules the IsA flag test (`test al,0x10`) after the MagnitudeXY faddp, ours
// before it. Not a scheduling-region edge (0 to 24 extra tuples ahead leave it in place), and no tuple-only Magnitude
// form, local copy, BOOL for IsA or declaration order moves it; 1,179 permuter tries found nothing.
void	IScript::UpdateWaypointFollowing()	// FUNCTION: 0x00538470
{
	float	dist = (mWaypoint->GetPos() - mDefaultThing->GetPos()).MagnitudeXY();

	float	radius = 2.f;
	if (mDefaultThing->IsA(THING_TYPE_UNIT))
		radius = ((CUnit *)mDefaultThing)->Unknown94();
	else if (mDefaultThing->IsA(THING_TYPE_UNKNOWN_20000000))
		radius = 4.f;

	if (dist < radius)
	{
		CWaypoint	*next = (CWaypoint *)mWaypoint->mNext.ToRead();
		if (next == mWaypoint)
		{
			LOG.AddMessage("ERROR: Waypoint points to previous");
			mWaypoint = NULL;
		}
		else
			mWaypoint = next;

		if (!mWaypoint)
		{
			// the end of the path
			mFollowingWaypoints = FALSE;
			mDefaultThing->Stop();
			if (mWaitingForWaypoints)
			{
				mWaitingForWaypoints = FALSE;
				MISSION_SCRIPT_VM = *mWaypointVM;
				mSavedVMs.Remove(mWaypointVM);
				delete mWaypointVM;
				mWaypointVM = NULL;
				int	pc = MISSION_SCRIPT_VM.mPC;
				if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
					MISSION_SCRIPT_VM.Clean();
				else
					MISSION_SCRIPT_VM.GotoInstruction(pc);
				return;
			}

			Arrived(mArrivedValue);
			return;
		}

		((CUnit *)mDefaultThing)->GoToPoint(mWaypoint->GetPos());
	}

	EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_FOLLOW_WAYPOINT, this, NEXT_FRAME);
}

//******************************************************************************************
void	IScript::HandleEvent(CEvent *event)	// FUNCTION: 0x005385e0
{
	switch (event->GetEventNum())
	{
	case SCRIPT_MESSAGE_FOLLOW_WAYPOINT:
		UpdateWaypointFollowing();
		break;

	case SCRIPT_MESSAGE_RESUME:
		{
			// a Pause or a message Wait is over
			if (((CScheduledEvent *)event)->GetData())
			{
				CVM	*vm = (CVM *)((CScheduledEvent *)event)->GetData();
				MISSION_SCRIPT_VM = *vm;
				mSavedVMs.Remove(vm);
				delete vm;
				int	pc = MISSION_SCRIPT_VM.mPC;
				if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
					MISSION_SCRIPT_VM.Clean();
				else
					MISSION_SCRIPT_VM.GotoInstruction(pc);
			}
		}
		break;

	case SCRIPT_MESSAGE_TIMER:
		if (!MISSION_SCRIPT_VM.mRunning)
		{
			if (GAME.GetGameState() == GAME_STATE_LEVEL_LOST)
				MISSION_SCRIPT_VM.Clean();
			else
				MISSION_SCRIPT_VM.CallEvent(mCode, SCRIPT_EVENT_TIMER, &gEventArg, 0);
		}
		break;
	}
}

//******************************************************************************************
CPostEventData::CPostEventData(CDataType *name) : mName(name)
{
	WORLD.mOwned100.Add(this);
}
// FUNCTION: 0x005386b0 ??_GCPostEventData@@UAEPAXI@Z

//******************************************************************************************
CPostEventData::~CPostEventData()	// FUNCTION: 0x005386d0
{
	if (mName)
	{
		delete mName;
		mName = NULL;
	}
	WORLD.mOwned100.Remove(this);
}
