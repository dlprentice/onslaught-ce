// SPDX-License-Identifier: GPL-3.0-only
#include "typeinfo_bridge.h"

// Native flags matter here: VC6-compatible parsing suppresses __GXX_WEAK__, so GCC10's header requests an out-of-line
// equality symbol that its ELF runtime does not export. Use the platform's ordinary standard-library implementation;
// comparing public name() strings or descriptor addresses would change RTTI semantics.
extern "C" bool PortTypeInfoEqual(const std::type_info &left, const std::type_info &right) noexcept
{
	return left == right;
}
