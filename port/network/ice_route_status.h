// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_ICE_ROUTE_STATUS_H
#define CE_ICE_ROUTE_STATUS_H
#if defined(CE_NETWORK_ICE)
#include "network.h"
#ifdef __cplusplus
extern "C" {
#endif
int CE_NetIceUsesRelay(const CE_NetSession *);
/* Active transport interruption, distinct from provider and authenticated peer closure. */
int CE_NetIceRouteInterrupted(const CE_NetSession *);
#ifdef __cplusplus
}
#endif
#endif
#endif
