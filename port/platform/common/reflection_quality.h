// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_REFLECTION_QUALITY_H
#define PORT_REFLECTION_QUALITY_H

// Main-thread API for higher-resolution water reflections (256/512/1024) and
// dynamic unit shadow maps (64/128/256) in ordinary Onslaught CE sessions.
// Requests apply cleanly at the next whole render frame boundary, clamp to
// reported D3D9 MaxTextureWidth/Height, and roll back cleanly on allocation
// failure. Faithful, fenced, and non-CE runs always keep retail 256 / 64.
extern "C" void CeRenderTargetQualitySetHardwareCaps(unsigned maxTexWidth, unsigned maxTexHeight);
extern "C" unsigned CeRenderTargetQualityHardwareMaxDim();

extern "C" int CE_WaterReflectionResChoice();
extern "C" int CE_WaterReflectionChoice();
extern "C" int CE_WaterReflectionRequest(int res);
extern "C" int CE_WaterReflectionChoiceToRes(int choice);
extern "C" int CE_WaterReflectionResToChoice(int res);
extern "C" int CE_WaterReflectionAllocatedRes();

void CeWaterReflectionBeginFrame();
bool CeWaterReflectionQualityActive();
unsigned CeWaterReflectionTargetRes();
unsigned CeWaterReflectionNextFallback(unsigned failedRes);
void CeWaterReflectionSetAllocatedRes(unsigned allocatedRes, unsigned attemptedTarget);
void CeWaterReflectionResetAllocated();
unsigned CeWaterReflectionActiveRes();
bool CeWaterReflectionNeedsRebuild();
float CeWaterReflectionTapOffset();

extern "C" int CE_DynamicShadowResChoice();
extern "C" int CE_DynamicShadowChoice();
extern "C" int CE_DynamicShadowRequest(int res);
extern "C" int CE_DynamicShadowChoiceToRes(int choice);
extern "C" int CE_DynamicShadowResToChoice(int res);
extern "C" int CE_DynamicShadowAllocatedRes();

void CeDynamicShadowBeginFrame();
bool CeDynamicShadowQualityActive();
unsigned CeDynamicShadowTargetRes();
unsigned CeDynamicShadowNextFallback(unsigned failedRes);
void CeDynamicShadowSetAllocatedRes(unsigned allocatedRes, unsigned attemptedTarget);
void CeDynamicShadowResetAllocated();
unsigned CeDynamicShadowActiveRes();
bool CeDynamicShadowNeedsRebuild();
float CeDynamicShadowTapOffset1(int size);
float CeDynamicShadowTapOffset3(int size);

#endif
