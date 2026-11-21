// SPDX-License-Identifier: GPL-3.0-only
// A stand-in for the game's frontend.h (see common.h here).
#ifndef CE_FAKE_FRONTEND_H
#define CE_FAKE_FRONTEND_H
#include "common.h"
class CFrontEnd
{
public:
	int	GetMemoryCardNumber() { return 0; }
};
extern CFrontEnd FRONTEND;
#endif
