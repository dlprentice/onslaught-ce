// SPDX-License-Identifier: GPL-3.0-only
// Real graphics host and policy, with the existing synthetic D3D provider.
#define main RetainedGraphicsHostCases
#include "public_d3dapp_driver.cpp"
#undef main
#include "portlog.h"
#include <algorithm>

CTweakSLONG GEFORCE_FX_POWER("legacy eligibility", 0), EXTRA_GEFORCE_FX_FEATURES("extras", 1), R32F_WORKS("float target", 0);
CTweakSLONG MY_READABLE_Z_BUFFER("selected depth path", 1);
namespace { unsigned policyReads, capabilityBeforeObjects, errors, policyLogs; }
bool PortCeLayer() { return true; }
void PortCeGraphicsApplyFreshDefaults() {}
int PortUserFileState(const char *) { return 1; }	// the host cases never open the options file
extern "C" int CE_SettingGetBool(const char *, int fallback) { ++policyReads; return fallback; }
extern "C" int CE_SettingSet(const char *, int) { return 1; }
extern "C" int CE_FullModelsRequest(int) { ++errors; return 0; }	// the fresh defaults are never taken in the host cases
extern "C" int CE_TerrainDetailRequest(int) { ++errors; return 0; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) { ++policyLogs; }
struct GraphicsApplication : Application
{
    HRESULT InitDeviceObjects() override
    {
        capabilityBeforeObjects = GEFORCE_FX_POWER.mValue;
        return Application::InitDeviceObjects();
    }
};
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::string name = argv[1];
    auto check = [&](bool value, const char *label) { if (!value) { ++errors; std::cout << "FAIL " << label << '\n'; } };
    modes = {{640,480,60,D3DFMT_X8R8G8B8}};
    if (name == "old-pixel") actualPixelVersion = 0xffff0104u;
    if (name == "old-vertex") actualVertexVersion = 0xfffe0100u;
    if (name == "float-fallback" || name == "no-float" || name == "float-unused") blockedRenderTargets.push_back(D3DFMT_R32F);
    if (name == "no-float" || name == "float-unused") blockedRenderTargets.push_back(D3DFMT_A32B32G32R32F);
    if (name == "float-unused") MY_READABLE_Z_BUFFER.mValue = 0;
    if (name == "no-mirror") blockedRenderTargets.push_back(D3DFMT_R5G6B5);
    if (name == "no-depth") blockedDepth.push_back(D3DFMT_D16);
    if (name == "bad-depth-match") blockedTargetDepthMatches.push_back(D3DFMT_R5G6B5);
    if (name == "physical-adapter") skipPrimary = true;
    if (name == "faithful") setenv("BEA_PORT_CLOCK", "1", 1);
    const bool eligible = name == "modern" || name == "float-fallback" || name == "physical-adapter" || name == "float-unused";
    GraphicsApplication app; app.m_bUseDepthBuffer = TRUE;
    const HRESULT result = app.Create(nullptr);
    check(SUCCEEDED(result), "unsupported optional effects never prevent ordinary graphics startup");
    check(capabilityBeforeObjects == static_cast<unsigned>(eligible), "actual device caps and formats gate resources before initialization");
    check(GEFORCE_FX_POWER.mValue == static_cast<int>(eligible), "completed host retains truthful graphics eligibility");
    check(EXTRA_GEFORCE_FX_FEATURES.mValue == static_cast<int>(eligible || name == "faithful"), "unsupported effects fall back without changing faithful settings");
    if (name == "faithful") check(policyReads == 0 && policyLogs == 0 && renderTargetAdapters.empty(), "faithful host avoids policy queries and settings");
    else check(policyReads == 1 && policyLogs > 0, "ordinary host completes one capability policy generation");
    if (eligible) check(R32F_WORKS.mValue == (name != "float-fallback" && name != "float-unused"), "float target choice follows the accepted format");
    if (name == "physical-adapter") check(!renderTargetAdapters.empty() && std::all_of(renderTargetAdapters.begin(), renderTargetAdapters.end(), [](UINT a){return a == 1;}), "queries use the actual physical adapter");
    app.Cleanup3DEnvironment();
    check(DeviceRefs() == 0 && QueryRefs() == 0 && d3d.refs == 0, "policy adds no native resource ownership");
    std::cout << "graphics-host " << name << " failures=" << errors << '\n';
    return errors ? 1 : 0;
}
