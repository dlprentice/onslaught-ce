#ifndef CE_CMDORDERS_H
#define CE_CMDORDERS_H

#include "ce_command_lifetime.h"

class CUnit;
class CGuide;

// Native pointers stay in this adapter. UI selections retain only lifetime keys.
struct CeCmdUnitAuthority {
    const void* squad;
    int ordinal;
    int why;
    int number;
    unsigned kind;
    bool found;
    bool commandable;
    bool groundPoint;
    bool friendly;
    bool scripted;
};

// This intermediate record never leaves the native adapter. Collection checks
// current membership before reading a unit, once per snapshot.
struct CeCmdUnitNativeRecord {
    CUnit* unit;
    CeCmdUnitAuthority authority;
    int squadNumber;
    unsigned squadKind;
    int members;
    float position[3];
    char name[32];
};

// Outbound UI facts contain no native addresses. Both the unit and its squad
// use destruction-aware lifetime keys, independent of list ordinals.
struct CeCmdUnitSnapshot {
    CeCommandLifetimeKey key;
    CeCommandLifetimeKey squadKey;
    int ordinal;
    int members;
    int why;
    float position[3];
    char name[32];
    bool friendly;
    bool scripted;
    bool commandable;
    bool groundPoint;
    bool orderOwned;
};

enum CeCmdUnitOrderKind {
    CE_UNIT_ORDER_NONE,
    CE_UNIT_ORDER_MOVE,
    CE_UNIT_ORDER_HOLD
};

struct CeCmdUnitOrderView {
    CeCommandLifetimeKey key;
    CeCmdUnitOrderKind order;
    float destination[3];
    bool owned;
};

bool CeCmdUnitWatch(CUnit*, CeCommandLifetimeKey*, CeCmdUnitAuthority*);
// The world renderer supplies a live unit from its current registry traversal.
// This records identity only; it grants no command or selection authority.
bool CeCmdUnitVisibleKey(CUnit*, CeCommandLifetimeKey*);
uint64_t CeCmdUnitWorld();
int CeCmdUnitCollect(CeCmdUnitSnapshot*, int capacity, bool* complete);
bool CeCmdUnitInspect(const CeCommandLifetimeKey&, CeCmdUnitSnapshot*);
// A synchronous native bridge only. The visitor must not retain the pointer.
bool CeCmdUnitVisit(const CeCommandLifetimeKey&,
    bool (*visitor)(CUnit*, int number, void*), void* user);
bool CeCmdUnitOrderRead(const CeCommandLifetimeKey&, CeCmdUnitOrderView*);
// Re-read after any refusal: pre-dispatch refusals preserve an old order,
// while a superseding callback may retire or replace it during dispatch.
bool CeCmdUnitMove(const CeCommandLifetimeKey&, float x, float y);
bool CeCmdUnitHold(const CeCommandLifetimeKey&);
bool CeCmdUnitReturnAI(const CeCommandLifetimeKey&);
void CeCmdUnitOrderRelease(CUnit*);
void CeCmdUnitOrdersWorldEnd();

// The caller supplies the existing ordinary-solo gate, without camera ownership.
bool CeCmdUnitOrdersEligible();

// Native hooks are inert when no CE lifetime or order is being tracked.
extern "C" void PortCeCmdThingGone(const void*);
extern "C" bool PortCeCmdGuidePoint(CGuide*, bool nativeOverride);

#endif
