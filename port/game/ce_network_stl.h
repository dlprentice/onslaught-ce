// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_STL_H
#define CE_NETWORK_STL_H
// Keep the standard library outside the game's legacy allocation/math macros.
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("new")
#pragma push_macro("__GCC_ATOMIC_TEST_AND_SET_TRUEVAL")
#undef min
#undef max
#undef new
// Clang's Microsoft compatibility mode omits this GCC atomic_flag constant.
// The measured native 32-bit x86 target stores one for true.
#if defined(__clang__) && defined(__i386__) && !defined(__GCC_ATOMIC_TEST_AND_SET_TRUEVAL)
#define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>
#pragma pop_macro("__GCC_ATOMIC_TEST_AND_SET_TRUEVAL")
#pragma pop_macro("new")
#pragma pop_macro("max")
#pragma pop_macro("min")
#endif
