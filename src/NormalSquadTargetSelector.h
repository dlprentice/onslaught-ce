// The stateless selector called by CNormalSquad. Its original class name remains open.
#ifndef NORMAL_SQUAD_TARGET_SELECTOR_H
#define NORMAL_SQUAD_TARGET_SELECTOR_H

class CThing;
class CSquad;

class CNormalSquadTargetSelector
{
public:
	CThing *Unknown477cb0(CSquad *squad);
};

// Preserve world.cpp's provisional global types; retail allocates one byte for each.
extern char *INFLUENCE_BUFFER_A;	// 0067a07c
extern char *INFLUENCE_BUFFER_B;	// 0067a078

#endif
