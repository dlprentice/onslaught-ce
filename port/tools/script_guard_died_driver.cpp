// SPDX-License-Identifier: GPL-3.0-only
// port/tools/script_guard_died_driver.cpp: what becomes of a script notification that patch 0176 defers, on the host.
//
// port/tools/script_guard_died_tests.py builds this with the game's own text of everything the answer depends on:
//   - compiled whole, from src/: SPtrSet, Monitor, activereader, event and scheduledevent (a monitor tells its readers when it is deleted, and
//     a scheduled event holds its target as such a reader);
//   - extracted whole from the patched MissionScript/IScript.cpp and IScript.h (script_guard_died_extracted.inc, written by the suite): the two
//     enumerations, IScript::~IScript, StartedDying, Died and HandleEvent;
//   - extracted whole from src/Unit.cpp: CUnit::Remove, one of the six callers of IScript::Died (the suite checks the other five have its shape);
//   - port/platform/common/ce_script_guard.cpp itself.
// Authored here, and nothing of the game's: the script VM (it only records which handler was entered, and whether the VM was already running),
// the event manager (it keeps scheduled events and, each frame, tests an event's target before calling it, as CEventManager::Flush does: the
// suite checks that text), the game state, the mod list and the log. No script is compiled or run.
//
// Usage: driver LEVEL SUPPLIED SCENARIO     (SUPPLIED: 1 when a mod supplies the level)
// Prints one line per thing that happened, in order; the suite compares the whole output.
#include "SPtrSet.h"
#include "Monitor.h"
#include "activereader.h"
#include "Event.h"
#include "scheduledevent.h"

#include "ce_script_guard.h"
#include "mods.h"
#include "portlog.h"

#include <stdarg.h>
#include <stdlib.h>
#include <vector>

#define NEXT_FRAME -1.0		// eventmanager.h's (the suite checks it)

static int gFrame;
static bool gSupplied;
static int gLevel;

bool PortModHas(const char *) { return gSupplied; }

void PortSessionLog(PortSessionCategory, bool, const char *format, ...)
{
	char text[512];
	va_list ap;
	va_start(ap, format);
	vsnprintf(text, sizeof(text), format, ap);
	va_end(ap);
	printf("LOG %s\n", text);
}

CLogStandIn LOG;

// The game state the hooks ask.
enum { GAME_STATE_PLAYING, GAME_STATE_LEVEL_LOST };
struct CGameStandIn
{
	int GetGameState() { return GAME_STATE_PLAYING; }
	int GetCurrentLevel() { return gLevel; }
} GAME;

// The event manager: scheduled events of the game's own class; a frame calls each event whose target is still there.
class CEventManager
{
public:
	void AddEvent(const int event_num, CMonitor *to_call, const float &time, const int start_or_end = 0, CMonitor *data = NULL, CScheduledEvent *re_use_event = NULL)
	{
		if (to_call == NULL)
			return;
		CScheduledEvent *event = new CScheduledEvent;
		event->Set(event_num, time, to_call, data);
		mEvents.push_back(event);
		printf("SCHEDULED event %d for %s\n", event_num, time < 0.0f ? "the next frame" : "later");
	}
	const float &GetTime() { return mTime; }
	void Frame()
	{
		gFrame++;
		mTime += 0.05f;
		printf("FRAME %d\n", gFrame);
		std::vector<CScheduledEvent *> due;
		std::vector<CScheduledEvent *> later;
		for (CScheduledEvent *event : mEvents)
			(event->GetTime() < mTime ? due : later).push_back(event);
		mEvents = later;
		for (CScheduledEvent *next_event : due)
		{
			IListener *to_call = next_event->GetToCall();
			if (to_call)
				to_call->HandleEvent(next_event);
			else
				printf("SKIPPED event %d: its target is gone\n", next_event->GetEventNum());
			delete next_event;
		}
	}

private:
	float mTime = 0.0f;
	std::vector<CScheduledEvent *> mEvents;
} EVENT_MANAGER;

// The script VM: one for every script, as in the game. A handler entered while another runs is the nesting patch 0176 is about.
class CDataType;
class CMissionScriptObjectCode
{
public:
	CMissionScriptObjectCode(const char *name, void (*body)() = NULL) : mName(name), mBody(body) {}
	~CMissionScriptObjectCode() { printf("DELETED the code of %s\n", mName); }
	const char *mName;
	void (*mBody)();		// what the handler does (a native that removes a unit, say)
};

static const char *EventName(int event);

class CVM
{
public:
	BOOL mRunning = FALSE;
	int mPC = 0;
	void Clean() { printf("VM cleaned\n"); }
	void GotoInstruction(int) {}
	void CallEvent(CMissionScriptObjectCode *code, int event, CDataType **, int)
	{
		printf("HANDLER %s of %s entered in frame %d%s\n", EventName(event), code->mName, gFrame, mRunning ? ", NESTED in a running handler" : "");
		BOOL was = mRunning;
		mRunning = TRUE;
		if (code->mBody)
			code->mBody();
		mRunning = was;
	}
} MISSION_SCRIPT_VM;

CDataType *gEventArg;

#define SCRIPT_GUARD_DIED_ENUMERATIONS
#include "script_guard_died_extracted.inc"
#undef SCRIPT_GUARD_DIED_ENUMERATIONS

static const char *EventName(int event)
{
	return event == SCRIPT_EVENT_DIED ? "Died" : event == SCRIPT_EVENT_STARTED_DYING ? "StartedDying" : event == SCRIPT_EVENT_INIT ? "Init" : "another event";
}

// The declarations the extracted functions belong to: only the members those functions name.
class IScript : public CMonitor
{
public:
	IScript(CMissionScriptObjectCode *code) : mCode(code) {}
	virtual ~IScript();
	virtual void HandleEvent(CEvent *event);
	void StartedDying();
	void Died();
	void UpdateWaypointFollowing() {}
	CMissionScriptObjectCode *mCode;
	SPtrSet<CVM> mSavedVMs;
};

enum { SHUTDOWN = 2000 };
class CUnit : public CMonitor
{
public:
	CUnit(IScript *script) : mMissionScript(script) {}
	virtual void HandleEvent(CEvent *event) { printf("UNIT got event %d\n", event->GetEventNum()); }
	void ReleaseChildren() {}
	void ClearSpawners() {}
	void SpawnExplosion() {}
	float Unknown112() { return 1.0f; }
	void Remove();
	IScript *mMissionScript;
};

#define SCRIPT_GUARD_DIED_FUNCTIONS
#include "script_guard_died_extracted.inc"
#undef SCRIPT_GUARD_DIED_FUNCTIONS

static CUnit *gVictim;

static void RemoveTheVictim() { gVictim->Remove(); }
static void StartTheVictimDying() { gVictim->mMissionScript->StartedDying(); }
static void StartTheVictimDyingThenRemoveIt()
{
	gVictim->mMissionScript->StartedDying();
	gVictim->Remove();
}

int main(int argc, char **argv)
{
	if (argc != 4)
		return 2;
	gLevel = atoi(argv[1]);
	gSupplied = atoi(argv[2]) != 0;
	const char *scenario = argv[3];
	GenericSPtrSet::Init(64);

	gVictim = new CUnit(new IScript(new CMissionScriptObjectCode("the victim's script")));
	void (*body)() = NULL;
	if (strcmp(scenario, "removed-in-a-handler") == 0)
		body = RemoveTheVictim;
	else if (strcmp(scenario, "starts-dying-in-a-handler") == 0)
		body = StartTheVictimDying;
	else if (strcmp(scenario, "starts-dying-and-removed-in-a-handler") == 0)
		body = StartTheVictimDyingThenRemoveIt;
	else if (strcmp(scenario, "removed-outside-a-handler") != 0)
		return 2;

	gFrame = 1;
	printf("FRAME 1\n");
	if (body)
	{
		// Another script's handler runs and, inside it, a native does something to the victim.
		IScript *other = new IScript(new CMissionScriptObjectCode("the other script", body));
		MISSION_SCRIPT_VM.CallEvent(other->mCode, SCRIPT_EVENT_INIT, NULL, 0);
		printf("the other script's handler returned\n");
	}
	else
		gVictim->Remove();
	EVENT_MANAGER.Frame();
	EVENT_MANAGER.Frame();
	printf("DEFERRED %d\n", PortCeScriptDeferred());
	return 0;
}
