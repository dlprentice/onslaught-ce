// SPDX-License-Identifier: GPL-3.0-only
// The production label code (port/game/ce_museum_label.cpp) and the visit's place tracking (port/game/ce_museum_visit_places.h), run together
// over a real exhibition plan against a faked game boundary (ce_museum_label_fake.h): what each frame draws as the visitor arrives, leaves
// a place, walks its edge and enters the screening room. Built and mutated by ce_museum_label_tests.py.
#include "ce_museum_label_fake.h"
#include "hud_scale.h"
#include "ce_museum_visit_places.h"
#include "ce_museum_label.h"
#include <cstdio>
#include <cstdlib>

std::vector<FakeText> gFakeTexts;
std::vector<FakeBar> gFakeBars;
FakeShell LT;
FakeGame GAME;
FakeEngine ENGINE;
FakeHud HUD;
FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO;
bool gMirrorPass = false;
float gMessageBoxX = 0, gMessageBoxY = 0, gMessageBoxWidth = 0, gMessageTextY = 0;
CeHudScope::CeHudScope(int, float, float, float, float) : mPreviousScale(1), mPreviousX(0), mPreviousY(0), mPreviousActive(false) {}
CeHudScope::~CeHudScope() {}
float CeHudScaleFactor() { return 1.0f; }	// the host draws at the HUD's original size

static unsigned long long ticks = 100000;
static unsigned presents = 1;
extern "C" unsigned PortPresentCount(void) { return presents; }
extern "C" unsigned long long CE_ScreenshotTicks(void) { return ticks; }
extern "C" bool PortCeMuseumVisitActive(void) { return true; }

// The visit's frame as ce_museum_visit_engine.cpp runs it: the hall names a place on the way in (CeMuseumHallVisitor; here the test says
// when), then the place tracking steps with the visitor's position.
static CeMuseumVisitPlaces places;
static const char *hallNames = nullptr;
extern "C" void PortCeMuseumVisitFrame(void)
{
	if (hallNames)
		PortCeMuseumLabelsWayfinding(hallNames, "Display 1 is on your left.");
	hallNames = nullptr;
	const FVector at = GAME.player.engine.position;
	CeMuseumVisitPlacesStep(&places, at.X, at.Y);
}
static bool inCinema = false;
extern "C" bool PortCeMuseumCinemaCue(char *title, unsigned titleCap, char *line, unsigned lineCap)
{
	if (!inCinema)
		return false;
	std::snprintf(title, titleCap, "Screening room");
	std::snprintf(line, lineCap, "Approach the projector.");
	return true;
}
extern "C" void PortCeMuseumCinemaView(const float *, unsigned) {}
static int sign = -1;
int CeMuseumHallSignLabel(float, float, int, int *number)
{
	if (number)
		*number = sign >= 0 ? sign + 1 : 0;
	return sign;
}
bool CeMuseumHallFacesExit(float, float, float, float, bool) { return false; }

static void Need(bool value, const char *message)
{
	if (!value)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(2);
	}
}
static bool Drew(const wchar_t *text)
{
	for (const FakeText &t : gFakeTexts)
		if (t.text.find(text) != std::wstring::npos)
			return true;
	return false;
}
// One frame 0.4 s after the last (a capsule is open after its 0.35 s growth), the visitor at (x, y) facing (fx, fy).
static void Frame(float x, float y, float fx = 0.0f, float fy = 1.0f)
{
	GAME.player.engine.position = FVector{x, y, 0};
	GAME.player.engine.orientation.forward = FVector{fx, fy, 0};
	gFakeTexts.clear();
	gFakeBars.clear();
	ticks += 400;
	++presents;
	PortCeMuseumLabelsRender();
}
static void Visit()
{
	using namespace CeMuseumExhibition;
	const DisplayInput displays[] = {{0, 1.6f, 3}, {1, 1.2f, 2}, {2, 0.8f, 2}, {3, 1.0f, 2}, {4, 1.2f, 3}, {5, 1.5f, 3}};
	Plan plan = {};
	Need(CeMuseumExhibitionHost::Build(displays, 6, &plan) && plan.bayCount == 6, "the hall plan builds");
	PortCeMuseumLabelsClear();
	places = CeMuseumVisitPlaces{};
	sign = -1;
	inCinema = false;
	hallNames = nullptr;
	char name[32];
	for (unsigned i = 0; i != 6; ++i)
	{
		const Bay *bay = nullptr;
		for (unsigned b = 0; b != plan.bayCount; ++b)
			if (plan.bays[b].stableLabel == int(i))
				bay = &plan.bays[b];
		Need(bay != nullptr, "every display has its bay");
		const CeHallPoint at = CeMuseumExhibitionHost::World(Vec2{bay->model.x, bay->model.y});
		std::snprintf(name, sizeof name, "Exhibit %u", i);
		Need(PortCeMuseumLabelsAdd(name, "A machine", "Its detail", at.x, at.y, -4.0f, 1.0f), "a label is added");
	}
	CeMuseumVisitPlacesArm(&places, plan);	// as GenerateInstalled does once the hall is built
}

int main()
{
	// The entrance's arrival zone in this hall: x 253 to 259, y 236.42 to 241.80 (world).
	// Arriving: facing down the runner, far displays (~25-27 m across the hall) are out of close-inspection range;
	// turning toward Display 1 (~10.2 m away, inside range) still shows no card until the visitor stands at a brass sign.
	Visit();
	Frame(256.0f, 240.5f, 0.0f, 1.0f);
	Frame(256.0f, 240.5f, 0.0f, 1.0f);
	Need(PortCeMuseumLabelsNearest(256.0f, 240.5f, 0.0f, 1.0f) == -1, "far displays down the hall are out of close-inspection range");
	Need(!Drew(L"Exhibit"), "arriving facing down the hall shows no exhibit card");
	Frame(256.0f, 240.5f, -0.79f, 0.61f);
	Frame(256.0f, 240.5f, -0.79f, 0.61f);
	Need(PortCeMuseumLabelsNearest(256.0f, 240.5f, -0.79f, 0.61f) == 0, "the arrival case is real: Display 1 is in view and in range from the entrance");
	Need(!Drew(L"Exhibit"), "arriving shows no card for a machine merely in view");
	sign = 3;
	Frame(256.0f, 240.5f, 0.0f, 1.0f);
	Frame(256.0f, 240.5f, 0.0f, 1.0f);
	Need(Drew(L"4: Exhibit 3"), "at a sign its own numbered card shows");

	// Leaving a place: the hall names the entrance on the way in; walking out closes it with time left, and it stays shut until named again.
	Visit();
	hallNames = "Gallery Entrance";
	Frame(256.0f, 240.5f);
	Frame(256.0f, 240.5f);
	Need(Drew(L"Gallery Entrance"), "the entrance's name is up while the visitor stands in it");
	Frame(256.0f, 246.0f);
	Need(!Drew(L"Gallery Entrance"), "walking out of the entrance closes its name, time left or not");
	Frame(256.0f, 240.5f);
	Need(!Drew(L"Gallery Entrance"), "it stays shut until the hall names the place again");
	hallNames = "Gallery Entrance";
	Frame(256.0f, 240.5f);
	Frame(256.0f, 240.5f);
	Need(Drew(L"Gallery Entrance"), "named again on the way back in, it opens");

	// The edge: a visitor in the entrance may stray a little past its box (walking along it) without the name closing; well past, it closes.
	Visit();
	hallNames = "Gallery Entrance";
	Frame(256.0f, 241.5f);
	Frame(256.0f, 241.5f);
	Need(Drew(L"Gallery Entrance"), "up at the entrance's edge");
	Frame(256.0f, 242.2f);
	Frame(258.5f, 242.2f);
	Need(Drew(L"Gallery Entrance"), "a step past the edge keeps the name up");
	Frame(256.0f, 243.0f);
	Need(!Drew(L"Gallery Entrance"), "well past the edge it closes");

	// The screening room: the cinema's card replaces any title at once.
	Visit();
	hallNames = "Gallery Entrance";
	Frame(256.0f, 240.5f);
	Frame(256.0f, 240.5f);
	Need(Drew(L"Gallery Entrance"), "the name is up before the cinema");
	inCinema = true;
	Frame(256.0f, 240.5f);
	Need(!Drew(L"Gallery Entrance") && Drew(L"Screening room"), "in the screening room the title closes and the cinema's card shows");
	std::puts("PASS label and place frames");
	return 0;
}
