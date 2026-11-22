// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_multibe.cpp: strictly opt-in gated Multi-Battle-Engine (3-4 simultaneous Battle Engines) prototype spike.
//
// Activation: ce.ini `multi_be=3` or `multi_be=4` (or `multi_be=1` for 4 BEs), plus optional `multi_be_cam=1` for an
// elevated panoramic camera framing all 3-4 Battle Engines and `multi_be_hud_slot=0..3` to test hud_focus redirection.
// Disabled by default and gated behind CE_GATE_LAYER in ce_hooks.cpp so faithful replays, recordings, probes,
// perturbation_sweep.py, and standard 1P/2P sessions are completely untouched.
//
// Memory-safety invariant (see docs/multi-battle-engine-architecture.md): auxiliary Battle Engines are bound to
// dedicated CPlayer(2) instances (so mNumber - 1 == 1 remains within retail [2] CAREER and CONTROL_BINDINGS bounds)
// with PLAYER_3RD_PERSON_VIEW active and per-BE cameras swapped transiently around FireWeapon() without mutating or
// freeing any real player's camera.
#include "ce_hooks.h"
#include "ce_multibe.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "player.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "Camera.h"
#include "InitThing.h"
#include "ThingType.h"
#include "unit.h"
#include "UnitAI.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int CE_SettingGetText(const char *key, char *out, int n);
extern "C" int CE_SettingIs(const char *key, const char *value);
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SessionActive(void) __attribute__((weak));
extern "C" int CE_LockstepRequested(void) __attribute__((weak));
extern "C" int CE_LockstepSharing(void) __attribute__((weak));
bool PortCeNetActive() __attribute__((weak));

namespace
{

bool NetworkOrLockstepActive()
{
	return (CE_BootRequested && CE_BootRequested()) ||
	       (CE_SessionActive && CE_SessionActive()) ||
	       (CE_LockstepRequested && CE_LockstepRequested()) ||
	       (CE_LockstepSharing && CE_LockstepSharing()) ||
	       (PortCeNetActive && PortCeNetActive());
}

struct MultiBeGameAccess : CGame
{
	static CCamera *&CameraSlot(int index) { return (GAME.*(&MultiBeGameAccess::mCurrentCamera))[index]; }
	static BOOL &FreeCameraSlot(int index) { return (GAME.*(&MultiBeGameAccess::mFreeCameraOn))[index]; }
};

class CeMultiBeOverviewCamera : public CCamera
{
public:
	CeMultiBeOverviewCamera() : mPos(0.0f, 0.0f, 0.0f), mOldPos(0.0f, 0.0f, 0.0f),
	                            mOrientation(ID_FMATRIX), mOldOrientation(ID_FMATRIX),
	                            mInitialised(false) {}

	virtual FVector GetPos() { return mPos; }
	virtual FMatrix GetOrientation() { return mOrientation; }
	virtual FVector GetOldPos() { return mOldPos; }
	virtual FMatrix GetOldOrientation() { return mOldOrientation; }
	virtual const float GetZoom() { return 1.0f; }
	virtual bool GetShowHUD() { return true; }

	void Reset() { mInitialised = false; }

	void UpdatePose(const FVector &pos, const FVector &lookAt)
	{
		FVector dir = lookAt - pos;
		FMatrix ori = mOrientation;
		if (dir.MagnitudeSq() > 1e-4f)
			ori = FMatrix(dir.Azimuth(), dir.Elevation(), 0.0f);
		if (!mInitialised)
		{
			mPos = pos;
			mOldPos = pos;
			mOrientation = ori;
			mOldOrientation = ori;
			mInitialised = true;
			return;
		}
		mOldPos = mPos;
		mOldOrientation = mOrientation;
		// Smooth damping avoids jerky framing while Battle Engines walk and turn.
		mPos = mPos + (pos - mPos) * 0.25f;
		mOrientation = ori;
	}

	bool Ready() const { return mInitialised; }

private:
	FVector	mPos;
	FVector	mOldPos;
	FMatrix	mOrientation;
	FMatrix	mOldOrientation;
	bool	mInitialised;
};

CeMultiBeConfig			gConfig = {0, false, -1};
bool					gActive = false;
bool					gSpawned = false;
bool					gReportedMilestone = false;
int						gLevel = 0;
int						gEngineCount = 0;
int						gAuxSpawned = 0;
int						gSimTicks = 0;
int						gNanCount = 0;
int						gTotalShots = 0;
int						gAuxShots = 0;
int						gAiTargetedCount = 0;
int						gHudFocusCalls = 0;
int						gUnitSpawnEvents = 0;
int						gUnitDestroyedEvents = 0;

CBattleEngine			*gEngines[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
bool					gIsAux[CE_MULTIBE_MAX] = {false, false, false, false};
CPlayer					*gAuxPlayers[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
CCamera					*gAuxCameras[CE_MULTIBE_MAX] = {NULL, NULL, NULL, NULL};
FVector					gStartPos[CE_MULTIBE_MAX];
bool					gMoved[CE_MULTIBE_MAX] = {false, false, false, false};
float					gAnchorFwdX = 0.0f;
float					gAnchorFwdY = 1.0f;
CeMultiBeOverviewCamera	gOverviewCam;

int IndexOfEngine(const CUnit *unit)
{
	if (!unit)
		return -1;
	for (int i = 0; i < gEngineCount; i++)
		if (gEngines[i] == unit)
			return i;
	return -1;
}

bool UnitExistsInWorld(const CUnit *needle)
{
	if (!needle)
		return false;
	// Stack-local iterator: strictly read-only, never mutates WORLD.GetUnitNB()'s shared intrusive mIterator.
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (u == needle)
			return true;
	return false;
}

int CountLiveEngines()
{
	int live = 0;
	for (int i = 0; i < gEngineCount; i++)
		if (gEngines[i] && UnitExistsInWorld(gEngines[i]) && !gEngines[i]->IsDying())
			live++;
	return live;
}

int CountMovedEngines()
{
	int moved = 0;
	for (int i = 0; i < gEngineCount; i++)
		if (gMoved[i])
			moved++;
	return moved;
}

void EmitTelemetry(const char *phase)
{
	fprintf(stderr,
	        "ce-multibe: phase=%s level=%d target=%d active=%d spawned=%d ticks=%d nan=%d shots=%d aux_shots=%d ai_targeted=%d hud_focus=%d moved=%d spawns=%d destroyed=%d\n",
	        phase, gLevel, gConfig.target_count, CountLiveEngines(), gAuxSpawned,
	        gSimTicks, gNanCount, gTotalShots, gAuxShots, gAiTargetedCount,
	        gHudFocusCalls, CountMovedEngines(), gUnitSpawnEvents, gUnitDestroyedEvents);
	fflush(stderr);
}

void ResetState()
{
	for (int i = 0; i < CE_MULTIBE_MAX; i++)
	{
		if (gAuxCameras[i])
		{
			delete gAuxCameras[i];
			gAuxCameras[i] = NULL;
		}
		if (gAuxPlayers[i])
		{
			delete gAuxPlayers[i];
			gAuxPlayers[i] = NULL;
		}
		gEngines[i] = NULL;
		gIsAux[i] = false;
		gMoved[i] = false;
	}
	gActive = false;
	gSpawned = false;
	gReportedMilestone = false;
	gLevel = 0;
	gEngineCount = 0;
	gAuxSpawned = 0;
	gSimTicks = 0;
	gNanCount = 0;
	gTotalShots = 0;
	gAuxShots = 0;
	gAiTargetedCount = 0;
	gHudFocusCalls = 0;
	gUnitSpawnEvents = 0;
	gUnitDestroyedEvents = 0;
	gOverviewCam.Reset();
}

float WrapAngle(float a)
{
	const float kPi = 3.14159265f;
	const float kTwoPi = 6.28318530f;
	while (a > kPi) a -= kTwoPi;
	while (a < -kPi) a += kTwoPi;
	return a;
}

CUnit *FindNearestHostile(CBattleEngine *self)
{
	if (!self)
		return NULL;
	const FVector myPos = self->GetPos();
	CUnit *best = NULL;
	float bestDistSq = 280.0f * 280.0f;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!u || u == self || u->IsDying() || !u->Active())
			continue;
		if (!self->IsTargetAlligence(u->GetAllegiance()))
			continue;
		const FVector delta = u->GetPos() - myPos;
		const float dSq = delta.MagnitudeSq();
		if (dSq > 4.0f && dSq < bestDistSq)
		{
			bestDistSq = dSq;
			best = u;
		}
	}
	return best;
}

void UpdateOverviewCamera()
{
	if (!gConfig.panoramic_cam || gEngineCount <= 0)
		return;
	const bool hasAnchor = gEngines[0] && UnitExistsInWorld(gEngines[0]);
	const FVector anchorPos = hasAnchor ? gEngines[0]->GetPos() : FVector(0.0f, 0.0f, 0.0f);
	float positions[CE_MULTIBE_MAX][3];
	int count = 0;
	for (int i = 0; i < gEngineCount; i++)
	{
		if (!gEngines[i] || !UnitExistsInWorld(gEngines[i]))
			continue;
		const FVector p = gEngines[i]->GetPos();
		// Keep panoramic framing locked on the local cluster around slot 0 (ignoring a far-away opposite base).
		if (hasAnchor && i > 0 && (p - anchorPos).MagnitudeSq() > 65.0f * 65.0f)
			continue;
		positions[count][0] = p.X;
		positions[count][1] = p.Y;
		positions[count][2] = p.Z;
		count++;
	}
	if (count == 0)
		return;
	float camArr[3], lookArr[3];
	if (!CeMultiBeComputeOverviewCam(positions, count, gAnchorFwdX, gAnchorFwdY, 27.0f, 9.0f, camArr, lookArr))
		return;
	FVector camPos(camArr[0], camArr[1], camArr[2]);
	const float groundAtCam = MAP.Collide(camPos);
	if (camPos.Z > groundAtCam - 4.0f)
		camPos.Z = groundAtCam - 4.0f;
	gOverviewCam.UpdatePose(camPos, FVector(lookArr[0], lookArr[1], lookArr[2]));
}

void SpawnAuxiliaryBattleEngines()
{
	CPlayer *p0 = GAME.GetPlayer(0);
	CBattleEngine *anchor = p0 ? p0->GetBattleEngine() : NULL;
	if (!anchor || anchor->IsDying())
		return;

	// Put P1 in 3rd-person view when panoramic showcase camera is active so P1 renders its full 3D exterior mesh
	// and does not overlay the internal first-person cockpit frame.
	if (gConfig.panoramic_cam && p0->GetIsInFPV())
		p0->Goto3rdPersonView();

	// Register existing player Battle Engines first.
	const int nPlayers = GAME.GetNPlayers();
	for (int i = 0; i < nPlayers && i < MAX_PLAYERS && gEngineCount < gConfig.target_count; i++)
	{
		CPlayer *pl = GAME.GetPlayer(i);
		CBattleEngine *be = pl ? pl->GetBattleEngine() : NULL;
		if (!be)
			continue;
		gEngines[gEngineCount] = be;
		gIsAux[gEngineCount] = false;
		gStartPos[gEngineCount] = be->GetPos();
		gEngineCount++;
	}

	const FVector anchorPos = anchor->GetPos();
	const FMatrix anchorOri = anchor->GetOrientation();
	FVector fwd = anchorOri * FVector(0.0f, 1.0f, 0.0f);
	fwd.Z = 0.0f;
	if (fwd.MagnitudeSq() < 1e-4f)
		fwd = FVector(0.0f, 1.0f, 0.0f);
	else
		fwd.Normalise();
	gAnchorFwdX = fwd.X;
	gAnchorFwdY = fwd.Y;
	FVector right(fwd.Y, -fwd.X, 0.0f);
	const float anchorYaw = anchor->GetCurrentEulerOrientation().mYaw;
	const bool mpLevel = GAME.IsMultiplayer() != FALSE;

	// Spawn auxiliary Battle Engines up to target_count (3 or 4), strictly on dry terrain above water level.
	// Retail DeclareInWater triggers at mPos.Z > waterLevel - 0.2f; 0.6f margin stays dry even on coastal airfields.
	const float waterZ = MAP.GetWaterLevel();
	const float dryMargin = 0.6f;
	int formationSlot = 1;
	while (gEngineCount < gConfig.target_count && gEngineCount < CE_MULTIBE_MAX)
	{
		float offRight = 0.0f, offFwd = 0.0f;
		CeMultiBeFormationOffset(formationSlot, gConfig.target_count, 11.0f, &offRight, &offFwd);
		formationSlot++;

		FVector spawnPos = anchorPos + right * offRight + fwd * offFwd;
		float groundZ = MAP.Collide(spawnPos);
		if (!CeMultiBeIsAboveWater(groundZ, waterZ, dryMargin))
		{
			// Probe dry fallback positions progressively closer/rotated around the anchor so aux BEs never spawn in water.
			bool foundDry = false;
			const float scales[3] = { 0.65f, 0.45f, 0.30f };
			const float angles[6] = { 0.0f, 1.0472f, -1.0472f, 2.0944f, -2.0944f, 3.14159f };
			for (int si = 0; si < 3 && !foundDry; si++)
			{
				for (int ai = 0; ai < 6 && !foundDry; ai++)
				{
					const float ca = cosf(angles[ai]), sa = sinf(angles[ai]);
					const float r = (offRight * ca - offFwd * sa) * scales[si];
					const float f = (offRight * sa + offFwd * ca) * scales[si];
					FVector cand = anchorPos + right * r + fwd * f;
					const float candGround = MAP.Collide(cand);
					if (CeMultiBeIsAboveWater(candGround, waterZ, dryMargin))
					{
						spawnPos = cand;
						groundZ = candGround;
						foundDry = true;
					}
				}
			}
			if (!foundDry)
				break;
		}
		spawnPos.Z = groundZ - 0.5f;
		if (!CeMultiBeIsFinitePos(spawnPos.X, spawnPos.Y, spawnPos.Z))
			break;

		// On Versus multiplayer maps both Forseti (f_be*) and Muspell (m_be*) meshes are packed: spawn 2v2!
		// On 1P campaign maps use Forseti (f_be*) so the mesh is guaranteed loaded in the level archive.
		EAllegiance side = kForsetiAllegiance;
		if (mpLevel && (gEngineCount & 1))
			side = kMuspellAllegiance;

		CBattleEngine *be = (CBattleEngine *)SpawnThing(OID_CBattleEngine);
		if (!be)
			break;

		CBattleEngineInitThing init;
		init.mPos = spawnPos;
		init.mYaw = anchorYaw;
		init.mPitch = 0.0f;
		init.mRoll = 0.0f;
		init.mAllegiance = side;
		init.mConfigurationId = (side == kMuspellAllegiance)
			? GAME.GetFrontEndSettings()->mPlayer2Configuration
			: GAME.GetFrontEndSettings()->mPlayer1Configuration;
		init.mPlaneMode = FALSE;
		be->Init(&init);

		// Bind a dedicated CPlayer(2) so mNumber - 1 == 1 stays inside retail [2] CAREER and CONTROL_BINDINGS arrays.
		CPlayer *auxPlayer = new(MEMTYPE_PLAYER) CPlayer(2);
		auxPlayer->AssignBattleEngine(be);
		// Prevent lethal combat death on auxiliary BEs so CBattleEngine::StartDieProcess() cannot call
		// GAME.DeclarePlayerDead(2) (which would NULL-deref mPlayer[1] in 1P or hijack real P2 in 2P).
		be->SetVulnerable(FALSE);

		// Enter 3rd-person view cleanly without deleting or overwriting P2's camera slot (even if free-cam is on).
		CCamera *savedCam = MultiBeGameAccess::CameraSlot(1);
		const BOOL savedFreeCam = MultiBeGameAccess::FreeCameraSlot(1);
		MultiBeGameAccess::FreeCameraSlot(1) = FALSE;
		MultiBeGameAccess::CameraSlot(1) = NULL;
		auxPlayer->Goto3rdPersonView();
		CCamera *auxCam = MultiBeGameAccess::CameraSlot(1);
		MultiBeGameAccess::CameraSlot(1) = savedCam;
		MultiBeGameAccess::FreeCameraSlot(1) = savedFreeCam;

		const int idx = gEngineCount++;
		gEngines[idx] = be;
		gIsAux[idx] = true;
		gAuxPlayers[idx] = auxPlayer;
		gAuxCameras[idx] = auxCam;
		gStartPos[idx] = be->GetPos();
		gAuxSpawned++;
	}

	gSpawned = true;
	UpdateOverviewCamera();
	EmitTelemetry("spawn");
}

void DriveAuxiliaryEngine(int idx)
{
	CBattleEngine *be = gEngines[idx];
	if (!be || !UnitExistsInWorld(be) || be->IsDying())
		return;

	// Enforce invulnerability and keep life positive so burst damage cannot invoke GAME.DeclarePlayerDead(2).
	if (be->GetVulnerable())
		be->SetVulnerable(FALSE);
	const float maxLife = be->GetMaxLife();
	if (maxLife > 0.0f && be->mLife < maxLife * 0.5f)
		be->mLife = maxLife * 0.85f;

	CBattleEngineWalkerPart *walker = be->GetWalkerPart();
	if (!walker || be->GetState() != BATTLE_ENGINE_STATE_WALKER)
		return;

	const float waterZ = MAP.GetWaterLevel();
	const float waterMargin = 0.6f;
	const FVector myPos = be->GetPos();
	const FVector toHome = gStartPos[idx] - myPos;
	const float homeDistSq = toHome.X * toHome.X + toHome.Y * toHome.Y;

	// Stay strictly above water level: if near water or drifted far from our dry anchor, steer back toward gStartPos[idx].
	const bool nearWater = !CeMultiBeIsAboveWater(MAP.Collide(myPos), waterZ, waterMargin) ||
	                       !CeMultiBeIsAboveWater(myPos.Z, waterZ, waterMargin);
	const bool returnHome = nearWater || homeDistSq > 18.0f * 18.0f;

	if (returnHome && homeDistSq > 1.0f)
	{
		const float desiredYaw = toHome.Azimuth();
		const float curYaw = be->GetCurrentEulerOrientation().mYaw;
		const float diff = WrapAngle(desiredYaw - curYaw);
		if (fabsf(diff) > 0.08f)
			walker->Rotate(diff > 0.0f ? -0.7f : 0.7f);
		if (fabsf(diff) < 0.8f)
			walker->Forward(0.40f);
	}
	else
	{
		CUnit *target = FindNearestHostile(be);
		if (target)
		{
			FVector toTarget = target->GetPos() - myPos;
			if (toTarget.MagnitudeSq() > 1.0f)
			{
				const float desiredYaw = toTarget.Azimuth();
				const float curYaw = be->GetCurrentEulerOrientation().mYaw;
				const float diff = WrapAngle(desiredYaw - curYaw);
				if (fabsf(diff) > 0.08f)
					walker->Rotate(diff > 0.0f ? -0.6f : 0.6f);
			}
		}
		else
		{
			// Gentle alternating turn so idle formation patrols cleanly.
			walker->Rotate(((gSimTicks / 45 + idx) & 1) ? 0.18f : -0.18f);
		}

		// Probe terrain 8m in the planned motion direction before stepping so aux BEs never walk into water.
		const FMatrix ori = be->GetOrientation();
		FVector fwd = ori * FVector(0.0f, 1.0f, 0.0f);
		FVector right = ori * FVector(1.0f, 0.0f, 0.0f);
		fwd.Z = 0.0f;
		right.Z = 0.0f;
		if (fwd.MagnitudeSq() > 1e-4f) fwd.Normalise();
		if (right.MagnitudeSq() > 1e-4f) right.Normalise();

		const int phase = (gSimTicks + idx * 17) % 120;
		FVector stepDir(0.0f, 0.0f, 0.0f);
		if (phase < 40) stepDir = fwd;
		else if (phase < 70) stepDir = right * -1.0f;
		else if (phase < 100) stepDir = right;
		else stepDir = fwd * -1.0f;

		const FVector probe = myPos + stepDir * 8.0f;
		if (CeMultiBeIsAboveWater(MAP.Collide(probe), waterZ, waterMargin))
		{
			if (phase < 40)
				walker->Forward(0.35f);
			else if (phase < 70)
				walker->StrafeLeft(0.28f);
			else if (phase < 100)
				walker->StrafeRight(0.28f);
			else
				walker->Backward(0.25f);
		}
	}

	// Periodically fire primary weapon using this auxiliary BE's own camera transiently in slot 1.
	if ((gSimTicks + idx * 7) % 14 == 0 && gAuxCameras[idx])
	{
		CCamera *savedCam = MultiBeGameAccess::CameraSlot(1);
		MultiBeGameAccess::CameraSlot(1) = gAuxCameras[idx];
		be->FireWeapon();
		MultiBeGameAccess::CameraSlot(1) = savedCam;
	}
}

void ProbeAiTargeting()
{
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!u || u->IsDying())
			continue;
		CUnitAI *ai = u->GetAI();
		if (!ai)
			continue;
		CUnit *tgt = ai->mTarget.ToRead();
		const int hitIdx = IndexOfEngine(tgt);
		if (hitIdx >= 0 && gIsAux[hitIdx])
			gAiTargetedCount++;
	}
}

} // namespace

extern "C" void PortCeMultiBeLevelBegin(int level)
{
	ResetState();
	// Never activate inside an online network or lockstep session (preserves strict multiplayer parity).
	if (NetworkOrLockstepActive())
		return;
	char buf[32] = {0};
	if (!CE_SettingGetText("multi_be", buf, (int)sizeof(buf)))
		return;
	const int count = CeMultiBeParseCount(buf);
	if (count < 3)
		return;
	gConfig.target_count = count;
	gConfig.panoramic_cam = CE_SettingIs("multi_be_cam", "1") != 0;
	char hudBuf[16] = {0};
	gConfig.hud_focus_slot = CE_SettingGetText("multi_be_hud_slot", hudBuf, (int)sizeof(hudBuf))
		? CeMultiBeParseHudSlot(hudBuf)
		: -1;
	gLevel = level;
	gActive = true;
}

extern "C" void PortCeMultiBeLevelEnd(int, int)
{
	if (!gActive)
		return;
	EmitTelemetry("end");
	ResetState();
}

extern "C" void PortCeMultiBeTickPre()
{
	if (!gActive)
		return;

	const EGameState state = GAME.GetGameState();
	if (state == GAME_STATE_PANNING && gConfig.panoramic_cam)
	{
		// Skip intro pan immediately in showcase mode across all active players so none linger in CPanCamera.
		bool ready = false;
		const int nPlayers = GAME.GetNPlayers();
		for (int i = 0; i < nPlayers && i < MAX_PLAYERS; i++)
		{
			CPlayer *pl = GAME.GetPlayer(i);
			if (pl && pl->GetBattleEngine())
			{
				if (i == 0)
					pl->Goto3rdPersonView();
				else
					pl->GotoControlView();
				ready = true;
			}
		}
		if (ready)
			GAME.StartPlayingState();
	}
	if (GAME.GetGameState() != GAME_STATE_PLAYING)
		return;

	if (!gSpawned)
	{
		SpawnAuxiliaryBattleEngines();
		if (!gSpawned)
			return;
	}

	gSimTicks++;

	// Validate positions & drive auxiliary Battle Engines.
	for (int i = 0; i < gEngineCount; i++)
	{
		CBattleEngine *be = gEngines[i];
		if (!be || !UnitExistsInWorld(be))
			continue;
		const FVector pos = be->GetPos();
		if (!CeMultiBeIsFinitePos(pos.X, pos.Y, pos.Z))
			gNanCount++;
		else if ((pos - gStartPos[i]).MagnitudeSq() > 0.25f)
			gMoved[i] = true;

		if (gIsAux[i])
			DriveAuxiliaryEngine(i);
	}

	// Periodically probe hostile AI units to observe real CUnitAI::Unknown11 targeting of auxiliary BEs.
	if (gSimTicks % 10 == 0)
		ProbeAiTargeting();

	UpdateOverviewCamera();

	if (!gReportedMilestone && gSimTicks >= 200)
	{
		gReportedMilestone = true;
		EmitTelemetry("tick200");
	}
}

extern "C" CCamera *PortCeMultiBeRenderCamera(CCamera *camera, int viewpoint, CPlayer *)
{
	if (!gActive || !gConfig.panoramic_cam || viewpoint != 0 || !gOverviewCam.Ready())
		return camera;
	return &gOverviewCam;
}

extern "C" CBattleEngine *PortCeMultiBeHudFocus(CBattleEngine *be, CPlayer *player)
{
	if (!gActive)
		return be;
	gHudFocusCalls++;
	if (gConfig.hud_focus_slot >= 0 && gConfig.hud_focus_slot < gEngineCount)
	{
		CBattleEngine *focused = gEngines[gConfig.hud_focus_slot];
		if (focused && UnitExistsInWorld(focused) && !focused->IsDying() && focused->GetPlayer())
		{
			// In 2P split-screen, retail CHud/CCompass anchor widgets off mBattleEngine->GetPlayer()->GetNumber() == 1:
			// only redirect a viewpoint when player numbers match (or in single-player full-window HUD).
			if (GAME.GetNPlayers() <= 1 || !player || focused->GetPlayer()->GetNumber() == player->GetNumber())
				return focused;
		}
	}
	return be;
}

extern "C" void PortCeMultiBeUnitSpawn(CUnit *, CInitThing *)
{
	if (gActive)
		gUnitSpawnEvents++;
}

extern "C" void PortCeMultiBeUnitDestroyed(CUnit *unit)
{
	if (!gActive)
		return;
	gUnitDestroyedEvents++;
	const int idx = IndexOfEngine(unit);
	if (idx >= 0)
		gEngines[idx] = NULL;
}

extern "C" void PortCeMultiBeWeaponFire(CWeapon *, CUnit *owner, CRound *)
{
	if (!gActive || !owner)
		return;
	const int idx = IndexOfEngine(owner);
	if (idx >= 0)
	{
		gTotalShots++;
		if (gIsAux[idx])
			gAuxShots++;
	}
}

#else	// private build: completely inert

extern "C" void PortCeMultiBeLevelBegin(int) {}
extern "C" void PortCeMultiBeLevelEnd(int, int) {}
extern "C" void PortCeMultiBeTickPre() {}
extern "C" CCamera *PortCeMultiBeRenderCamera(CCamera *camera, int, CPlayer *) { return camera; }
extern "C" CBattleEngine *PortCeMultiBeHudFocus(CBattleEngine *be, CPlayer *) { return be; }
extern "C" void PortCeMultiBeUnitSpawn(CUnit *, CInitThing *) {}
extern "C" void PortCeMultiBeUnitDestroyed(CUnit *) {}
extern "C" void PortCeMultiBeWeaponFire(CWeapon *, CUnit *, CRound *) {}

#endif
