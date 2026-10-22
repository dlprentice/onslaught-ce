// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/portlog.h: the port's console messages. Public builds (CE_VERSION) say "onslaught-ce:", private
// builds "bea-port:"; port/tools/run.py and winrun.py read either. Developer chatter (the frame counter, input-mode
// retries, the game's own debugging printf lines) is printed only when PortDevLog() says so.
#ifndef PORT_PORTLOG_H
#define PORT_PORTLOG_H

#if defined(CE_VERSION)
#define PORT_LOG_PREFIX "onslaught-ce: "
#else
#define PORT_LOG_PREFIX "bea-port: "
#endif

// Private builds: always. Public builds: only with BEA_PORT_VERBOSE=1 (ce.cpp).
bool PortDevLog();

#endif
