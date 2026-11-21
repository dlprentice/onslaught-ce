// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_frontend.h: plain reads of the Onslaught CE page (ce_frontend.cpp), for the test kit's adapter
// (ce_frontend_testkit.cpp). They change nothing.
#ifndef CE_FRONTEND_H
#define CE_FRONTEND_H
class CMenuOptionsList;
bool	PortCeFrontendMenuShown();					// the CE page's menu is up
int		PortCeFrontendPage();						// its page (ECePage in ce_frontend.cpp), or -1
int		PortCeFrontendPageCount();
const WCHAR	*PortCeFrontendPageTitle(int page);		// or NULL
CMenuOptionsList	*PortCeFrontendRows();			// the shown page's rows, or NULL
#endif
