// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_TYPEINFO_BRIDGE_H
#define PORT_TYPEINFO_BRIDGE_H
#include <typeinfo>

// Compare through the native C++ runtime/header configuration. Game translation units retain their VC6 parsing mode.
// This preserves std::type_info equality, including platforms where equal types have distinct descriptor/name addresses.
extern "C" bool PortTypeInfoEqual(const std::type_info &left, const std::type_info &right) noexcept;
#endif
