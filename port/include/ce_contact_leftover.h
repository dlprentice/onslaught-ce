// SPDX-License-Identifier: GPL-3.0-only
// The contact normals a collision report does not set, as the original leaves them (patch 0280; port/PARITY.md, "The
// unset contact normals").
//
// CCollisionSeekingThing::Unknown6 builds its CCollisionReport on the stack and writes only the normals its contacts
// set; CUnit::Hit then reads the others (the third, with two contacts). In the original these slots hold whatever was
// last stored at that stack address. On the scheduled path (CEventManager::Flush > CHLCollisionDetector::HandleEvent >
// CheckCollision > Unknown6) the address is fixed, and measured against recorded play of levels 720 and 741 the value
// there is either an emitter matrix that CAirUnit::Move left (CUnit::GetEmitter's result or Move's own `ori`, at an
// offset fixed by the Move that called it), the previous scheduled report's own normals, or, after a thing's SHUTDOWN
// or a falling tree's TREE_FALL update, pointers and small integers that read as zero. This keeps a shadow of those six normals and feeds a scheduled
// report from it; every other report starts at zero. Simulation state only, so lockstep peers compute the same shadow.
#ifndef CE_CONTACT_LEFTOVER_H
#define CE_CONTACT_LEFTOVER_H

class CEvent;
class FMatrix;
struct CCollisionReport;

// Where CAirUnit::Move's emitter matrix lands, by the Move that entered it (static offsets of the original's frames):
// DIRECT: CAirUnit::Move entered at the handler's depth (CBigAirUnit, and the tail calls of CDiveBomber and
// CGroundAttackAircraft): CUnit::GetEmitter's multiplied result covers normals 1 to 3. PLANE: CPlane::Move's call puts
// Move's `ori` on normals 2 to 4 and `smoke_ori`'s first row on normal 5. NONE: CBomber and CCarver (one frame deeper:
// each GetEmitter call's arguments and return address land on normals 2 and 1, which read as zero), and CCarrier,
// CDropship and CFenrir, whose frames nobody has derived: normals 1 and 2 become zero.
enum { CE_AIR_MOVE_DIRECT = 0, CE_AIR_MOVE_PLANE = 1, CE_AIR_MOVE_NONE = 2 };

void CE_ContactLeftoverReset();								// CEventManager::Init: a level starts with zeros
void CE_ContactLeftoverDispatch(const CEvent *event);		// CEventManager::Flush around each delivery (NULL after)
void CE_ContactLeftoverShutdown(const CEvent *event);		// CThing::HandleEvent (SHUTDOWN) and CTree::HandleEvent (TREE_FALL)
void CE_ContactLeftoverCheckBegin(const CEvent *event);		// CHLCollisionDetector::HandleEvent around CheckCollision
void CE_ContactLeftoverCheckEnd();
bool CE_ContactLeftoverStart(CCollisionReport &report);		// Unknown6: fill the normals; true on the scheduled path
void CE_ContactLeftoverEnd(const CCollisionReport &report, bool scheduled);	// Unknown6, after both Hit callbacks
void CE_ContactLeftoverMoveBegin(const CEvent *event);		// CActor::HandleEvent's MOVE, around Move()
void CE_ContactLeftoverMoveEnd();
void CE_ContactLeftoverAirDepth(int depth);					// a subclass Move around its call of CAirUnit::Move
void CE_ContactLeftoverEmitterBegin();						// CUnit::GetEmitter: entry
void CE_ContactLeftoverMultiplied();						// CUnit::GetEmitter: the cached pair's matrix product
void CE_ContactLeftoverEmitter(const FMatrix &ori, bool moveOri);	// CAirUnit::Move after each GetEmitter call

#endif
