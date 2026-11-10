// SPDX-License-Identifier: GPL-3.0-only
// Stand-ins for the display-mode references of port/platform/public/d3dapp.cpp in a CE_VERSION build, for the fixtures that link it
// with the shared public_d3dapp_driver.cpp (which does not define them): no saved window size, no wide start size on the desktop.
extern "C" int CE_DisplayModeUseSavedSize() { return 0; }
int PortWideStartSizeOnDesktop(unsigned, unsigned, int, unsigned, unsigned, unsigned *, unsigned *) { return 0; }
