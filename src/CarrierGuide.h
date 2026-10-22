// CarrierGuide.h: CCarrierGuide (0x20 bytes, vtable 005d940c), the guide that steers a carrier in the air
// (CarrierGuide.obj). Its constructor is inline; CCarrier::Init builds it (00421ae8).

#ifndef CARRIERGUIDE_H
#define CARRIERGUIDE_H

#include "Guide.h"

class CCarrierGuide : public CGuide
{
public:
	CCarrierGuide(CUnit *unit) : CGuide(unit) {}
	virtual void	Unknown3();						// 3 (00421f90)
};

#endif
