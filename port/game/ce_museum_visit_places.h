// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_visit_places.h: the visitor's place in a hall of numbered displays, as the visit engine tracks it once a visit frame
// (ce_museum_visit_engine.cpp), and what it tells the labels (ce_museum_label.h): that cards are a sign's own, and whether the visitor still
// stands in the place the title at the top names. No engine type, so a host test runs it with the real ce_museum_label.cpp
// (ce_museum_label_tests.py).
//
// A place is the entrance's arrival zone (0) or a numbered display's viewing zone (its number); -1 is elsewhere. The test is the hall's own
// (ExhibitionZone in ce_museum_hall.cpp), which names a place only on the way in. The display zones keep a seam (StandingBay's kZoneKeep);
// the arrival zone is a box, and a visitor already in it stays in it until kCeMuseumArrivalKeep past its edge, so walking along the edge does not
// close and reopen the title at the top.
#ifndef CE_MUSEUM_VISIT_PLACES_H
#define CE_MUSEUM_VISIT_PLACES_H

#include "ce_museum_exhibition_host.h"
#include "ce_museum_label.h"

static const float kCeMuseumArrivalKeep = 0.6f;	// metres past the arrival box a visitor already in it may go before it is left

struct CeMuseumVisitPlaces
{
	CeMuseumExhibition::Plan plan;
	bool armed;
	int place;
};

// The place at world (x, y), given the place before (previous).
inline int CeMuseumVisitPlaceOf(const CeMuseumExhibition::Plan &plan, float x, float y, int previous)
{
	const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
	const int number = CeMuseumExhibition::StandingBay(plan, local, previous > 0 ? previous : 0);
	if (number > 0)
		return number;
	const CeMuseumExhibition::Zone *arrival = CeMuseumExhibition::ZoneFor(plan, CeMuseumExhibition::kZoneArrival);
	if (!arrival)
		return -1;
	const float keep = previous == 0 ? kCeMuseumArrivalKeep : 0.0f;
	return local.x >= arrival->min.x - keep && local.x <= arrival->max.x + keep &&
	       local.y >= arrival->min.y - keep && local.y <= arrival->max.y + keep ? 0 : -1;
}

// The hall of numbered displays is built: cards become a sign's own, and the visitor starts in the entrance, as the hall's own tracking does.
inline void CeMuseumVisitPlacesArm(CeMuseumVisitPlaces *places, const CeMuseumExhibition::Plan &plan)
{
	places->plan = plan;
	places->armed = true;
	places->place = 0;
	PortCeMuseumLabelsSigns(true);
}

inline void CeMuseumVisitPlacesDrop(CeMuseumVisitPlaces *places)
{
	places->armed = false;
}

// Once a visit frame, with the visitor's world position: its place, and whether the labels' place title still stands.
inline void CeMuseumVisitPlacesStep(CeMuseumVisitPlaces *places, float x, float y)
{
	if (!places->armed)
		return;
	places->place = CeMuseumVisitPlaceOf(places->plan, x, y, places->place);
	PortCeMuseumLabelsPlace(places->place >= 0);
}

#endif
