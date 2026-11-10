// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_GRAPHICS_POLICY_SUPPORT_H
#define CE_GRAPHICS_POLICY_SUPPORT_H
#ifdef BEA_PUBLIC_D3DAPP_TEST
#include "public_d3dapp_support.h"
#else
#include "portlog.h"
struct CTweakSLONG
{
    int mValue;
    explicit CTweakSLONG(int value) : mValue(value) {}
    void Set(float value) { mValue = static_cast<int>(value); }
};
#endif
#include "portlog.h"
#endif
