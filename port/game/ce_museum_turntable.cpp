// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_turntable.cpp: On-demand wing-scoped Goodie 3D exhibit loader and
// rotating turntable renderer for the Grand Museum. Uses CChunkReader to load standalone
// goodie_NN_res_PC.aya assets into isolated slots (without touching CFEPGoodies or Level 100
// meshes) and renders via CMESHRENDERER::Render inside a local per-draw IDirect3DStateBlock9
// + LT.InvalidateAllStates() guard for full D3D9 Reset safety.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "state.h"
#include "Lights.h"
#include "chunker.h"
#include "resourceaccumulator.h"
#include "Texture.h"
#include "mesh.h"
#include "meshrenderer.h"
#include "ce_museum_turntable.h"
#include "ce_museum_cinema.h"
#include "ce_museum_world.h"
#include "../platform/common/portlog.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" bool PortCeGoodieFiles(int goodie, const char **mesh, const char **picture, WCHAR *title);

namespace CeMuseumTurntable {
namespace {

enum { kGoodieTypeMesh = 1 }; // GT_MESH in FEPgoodies.h

struct LoadedGoodie {
    CMesh *mesh;
    CTEXTURE **textures;
    int numTextures;
    bool attempted;
};

BankState gBank = {};
LoadedGoodie gLoaded[kManifestCount] = {};
bool gHasResidentWing = false;

bool FileReadable(const char *path) {
    if (!path || !*path) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

void ForceDeleteMeshChain(CMesh *mesh) {
    while (mesh) {
        CMesh *next = mesh->mNextInChain;
        if (next) {
            // Detach LOD child before deleting parent so ~CMesh() does not double-Release,
            // and delete every link in the chain before slot.textures[] are torn down.
            mesh->mNextInChain = NULL;
            next->Release();
        }
        mesh->ForceDelete();
        mesh = next;
    }
}

void FreeSlot(unsigned index) {
    if (index >= kManifestCount) return;
    LoadedGoodie &slot = gLoaded[index];
    if (slot.mesh) {
        CMesh *root = slot.mesh;
        slot.mesh = NULL;
        root->Release();
        ForceDeleteMeshChain(root);
    }
    if (slot.textures) {
        for (int i = 0; i < slot.numTextures; ++i) {
            if (!slot.textures[i]) continue;
            slot.textures[i]->Release();
            slot.textures[i]->ForceDeleteVBuffer();
            CTEXTURE::mForceDeleting = TRUE;
            slot.textures[i]->ForceDelete();
            CTEXTURE::mForceDeleting = FALSE;
            slot.textures[i] = NULL;
        }
        delete[] slot.textures;
        slot.textures = NULL;
    }
    slot.numTextures = 0;
    slot.attempted = false;
    gBank.slots[index].loaded = false;
    gBank.slots[index].missing = false;
}

void FreeAllSlots() {
    for (unsigned i = 0; i < kManifestCount; ++i) FreeSlot(i);
    gHasResidentWing = false;
}

bool LoadSlot(unsigned index) {
    if (index >= kManifestCount) return false;
    LoadedGoodie &slot = gLoaded[index];
    if (slot.mesh) return true;
    if (slot.attempted) return false;
    slot.attempted = true;

    const Spec &sp = kManifest[index];
    // Pre-check both the goodie archive and its external mesh file so a missing retail file
    // degrades gracefully to a stone plaque instead of triggering a fatal resource error.
    const char *meshName = NULL, *picName = NULL;
    WCHAR titleBuf[256] = {};
    if (!PortCeGoodieFiles(sp.goodieId, &meshName, &picName, titleBuf) || !meshName || !meshName[0]) {
        gBank.slots[index].missing = true;
        return false;
    }
    char goodieFile[MAX_PATH] = {};
    CResourceAccumulator::GetFileName(goodieFile, -1000 - sp.goodieId, PC);
    char meshFile[MAX_PATH] = {};
    std::snprintf(meshFile, sizeof meshFile, "data\\resources\\meshes\\m_%s.aya", meshName);
    if (!FileReadable(goodieFile) || !FileReadable(meshFile)) {
        PortSessionLog(PORT_SESSION_GAME, false,
                       "museum turntable: %s (goodie %d) asset unavailable; showing plaque",
                       sp.title, sp.goodieId);
        gBank.slots[index].missing = true;
        return false;
    }

    CChunkReader *reader = new(MEMTYPE_UNKNOWN) CChunkReader;
    if (!reader) {
        gBank.slots[index].missing = true;
        return false;
    }
    if (!reader->Open(goodieFile)) {
        delete reader;
        gBank.slots[index].missing = true;
        return false;
    }

    UINT tag = 0;
    bool ok = false;
    while ((tag = reader->GetNext()) != 0) {
        if (tag == MKID("GDIE")) {
            reader->GetNext();
            int number = -1;
            int gtype = -1;
            reader->Read(&number, sizeof(number), 1);
            reader->Read(&gtype, sizeof(gtype), 1);
            if (gtype == kGoodieTypeMesh) {
                reader->GetNext();
                int numTex = 0;
                reader->Read(&numTex, sizeof(numTex), 1);
                if (numTex >= 0 && numTex <= 64) {
                    if (numTex > 0) {
                        slot.textures = new(MEMTYPE_TEXTURE) CTEXTURE *[numTex];
                        slot.numTextures = numTex;
                        for (int t = 0; t < numTex; ++t) {
                            slot.textures[t] = NULL;
                            reader->GetNext();
                            CTEXTURE *tex = CTEXTURE::Deserialize(0, reader);
                            if (tex) {
                                tex->AddRef();
                                slot.textures[t] = tex;
                            }
                        }
                    }
                    reader->GetNext();
                    CMesh *mesh = CMesh::Deserialize(reader, 0);
                    if (mesh && mesh->mNumParts > 0 && mesh->mBoundingBox && mesh->mBoundingBox->mValid) {
                        mesh->AddRef();
                        slot.mesh = mesh;
                        ok = true;
                    } else if (mesh) {
                        mesh->AddRef();
                        mesh->Release();
                        ForceDeleteMeshChain(mesh);
                    }
                }
            }
            break;
        } else {
            reader->Skip();
        }
    }
    reader->Close();
    delete reader;

    if (!ok) {
        FreeSlot(index);
        slot.attempted = true;
        gBank.slots[index].missing = true;
        return false;
    }
    gBank.slots[index].loaded = true;
    gBank.slots[index].missing = false;
    return true;
}

void SyncWingResidency(CeMuseumTour::Wing wing) {
    if (gHasResidentWing && gBank.residentWing == wing) return;
    // Release any slot no longer needed before loading new ones so peak 32-bit RAM stays bounded.
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, wing) && (gLoaded[i].mesh || gLoaded[i].textures || gLoaded[i].attempted))
            FreeSlot(i);
    }
    gBank.residentWing = wing;
    gHasResidentWing = true;
    unsigned loadedCount = 0;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, wing)) continue;
        if (loadedCount >= static_cast<unsigned>(kMaxResidentGoodies)) break;
        if (LoadSlot(i)) ++loadedCount;
    }
    PortSessionLog(PORT_SESSION_GAME, false,
                   "museum turntables: %s resident count %u",
                   CeMuseumTour::Meta(wing).title, loadedCount);
}

struct StudVertex { float x, y, z; DWORD colour; };

void DrawStudIndicator(IDirect3DDevice9 *dev, unsigned index, float floorZ, bool hovered, bool paused) {
    float sx, sy, sz;
    StudPosition(index, &sx, &sy, &sz);
    const float wx = CeMuseumWorld::kEntranceX + sx;
    const float wy = CeMuseumWorld::kEntranceY + sy;
    const float wz = floorZ - sz;
    const float r = hovered ? 0.14f : 0.11f;
    const DWORD col = hovered ? 0xffffeb90 : (paused ? 0xff62a4dc : 0xffe4bc64);
    const DWORD rim = 0xff4c5864;
    const DWORD brassBase = 0xffb8924c;

    auto emitBox = [&](float cx, float cy, float cz, float hx, float hy, float hz, DWORD c) {
        const float x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
        const StudVertex tris[30] = {
            {x0,y0,z0,c},{x1,y0,z0,c},{x1,y1,z0,c}, {x0,y0,z0,c},{x1,y1,z0,c},{x0,y1,z0,c},
            {x0,y0,z1,c},{x1,y0,z1,c},{x1,y0,z0,c}, {x0,y0,z1,c},{x1,y0,z0,c},{x0,y0,z0,c},
            {x1,y0,z1,c},{x1,y1,z1,c},{x1,y1,z0,c}, {x1,y0,z1,c},{x1,y1,z0,c},{x1,y0,z0,c},
            {x1,y1,z1,c},{x0,y1,z1,c},{x0,y1,z0,c}, {x1,y1,z1,c},{x0,y1,z0,c},{x1,y1,z0,c},
            {x0,y1,z1,c},{x0,y0,z1,c},{x0,y0,z0,c}, {x0,y1,z1,c},{x0,y0,z0,c},{x0,y1,z0,c},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 10, tris, sizeof(StudVertex));
    };
    // Dressed slate stanchion with brass foot collar and interactive brass stud cap
    emitBox(wx, wy, floorZ - 0.06f, 0.12f, 0.12f, 0.06f, brassBase);
    emitBox(wx, wy, floorZ - 0.54f, 0.065f, 0.065f, 0.44f, rim);
    emitBox(wx, wy, wz, r, r, 0.06f, col);
}

} // namespace

void RuntimeInit() {
    FreeAllSlots();
    Reset(&gBank);
}

void RuntimeShutdown() {
    FreeAllSlots();
    Reset(&gBank);
}

void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ) {
    if (!gBank.initialized) Reset(&gBank);
    SyncWingResidency(wing);
    StepRotation(&gBank, dtSeconds, paused);
    gBank.hoveredIndex = PickStud(eyeX, eyeY, eyeZ, aimX, aimY, aimZ, wing);
}

bool RuntimeHandleFire(bool release, float value) {
    if (!gBank.initialized) return false;
    const bool usable = gBank.hoveredIndex >= 0 && static_cast<unsigned>(gBank.hoveredIndex) < kManifestCount;
    const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
        release || value <= 0.0f, usable, gBank.fireHeld, gBank.fireCaptured);
    if (intent == CeCinemaRoom::FireIntent::Operate && usable) {
        TogglePause(&gBank, gBank.hoveredIndex);
        PortSessionLog(PORT_SESSION_GAME, false,
                       "museum turntable: %s %s",
                       kManifest[gBank.hoveredIndex].title,
                       gBank.slots[gBank.hoveredIndex].paused ? "paused" : "resumed");
        return true;
    }
    return intent != CeCinemaRoom::FireIntent::Ordinary;
}

bool RuntimeIsAimingControl() {
    return gBank.initialized && gBank.hoveredIndex >= 0;
}

bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap) {
    if (!gBank.initialized || !title || !titleCap || !line || !lineCap) return false;
    int idx = gBank.hoveredIndex;
    const bool atStud = idx >= 0 && static_cast<unsigned>(idx) < kManifestCount;
    if (!atStud) idx = FocusExhibit(visitorX, visitorY, fwdX, fwdY, wing);
    if (idx < 0 || static_cast<unsigned>(idx) >= kManifestCount) return false;
    const Spec &sp = kManifest[idx];
    std::snprintf(title, titleCap, "%s", sp.title);
    std::snprintf(line, lineCap, "%s", atStud ? sp.category : sp.caption);
    if (note && noteCap) {
        if (gBank.slots[idx].missing)
            std::snprintf(note, noteCap, "3D display model unavailable in this install.");
        else if (atStud)
            std::snprintf(note, noteCap, "%s",
                          gBank.slots[idx].paused ? "Turntable paused. Press Fire to resume rotation."
                                                  : "Turntable rotating. Press Fire to pause rotation.");
        else
            std::snprintf(note, noteCap, "%s - aim at the brass stud to %s rotation.",
                          sp.category, gBank.slots[idx].paused ? "resume" : "pause");
    }
    return true;
}

void RuntimeDraw(float floorZ) {
    if (!gBank.initialized || !gHasResidentWing) return;
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;

    // 1. Draw interactive brass control studs (simple unlit geometry)
    {
        IDirect3DStateBlock9 *studState = NULL;
        if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &studState)) && studState) {
            if (SUCCEEDED(studState->Capture())) {
                D3DMATRIX world = {}, view, proj;
                world._11 = world._22 = world._33 = world._44 = 1.0f;
                RENDERINFO.GetView(&view);
                RENDERINFO.GetProjection(&proj);
                dev->SetVertexShader(NULL);
                dev->SetPixelShader(NULL);
                dev->SetTransform(D3DTS_WORLD, &world);
                dev->SetTransform(D3DTS_VIEW, &view);
                dev->SetTransform(D3DTS_PROJECTION, &proj);
                dev->SetTexture(0, NULL);
                dev->SetRenderState(D3DRS_LIGHTING, FALSE);
                dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
                dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
                dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
                dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
                for (unsigned i = 0; i < kManifestCount; ++i) {
                    if (!ShouldBeResident(i, gBank.residentWing)) continue;
                    DrawStudIndicator(dev, i, floorZ, gBank.hoveredIndex == static_cast<int>(i), gBank.slots[i].paused);
                }
                studState->Apply();
            }
            studState->Release();
        }
    }

    // 2. Render each resident 3D Goodie mesh inside a local per-draw D3D9 state block + LT.InvalidateAllStates()
    bool anyMesh = false;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (ShouldBeResident(i, gBank.residentWing) && gLoaded[i].mesh) { anyMesh = true; break; }
    }
    if (!anyMesh) return;

    IDirect3DStateBlock9 *savedState = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &savedState)) || !savedState) return;
    if (FAILED(savedState->Capture())) { savedState->Release(); return; }

    // Save RENDERINFO cached lighting/fog state alongside the D3D9 state block
    D3DMATRIX savedWorld;
    RENDERINFO.GetWorld(&savedWorld);
    const DWORD savedAmbient = RENDERINFO.mAmbient;
    const bool savedLighting = RENDERINFO.mUnknown2ed;
    const bool savedFog = RENDERINFO.mFogEnabled;
    CLight savedLights[8];
    bool savedLightEnable[8];
    for (SINT li = 0; li < 8; ++li) {
        savedLights[li] = RENDERINFO.mLights[li];
        savedLightEnable[li] = RENDERINFO.mLightEnable[li];
    }

    // Balanced 3-point gallery lighting (warm key + cool sky fill + subtle rim)
    CLight light;
    RENDERINFO.SetAmbient(0x005c616a);
    FVector dir1(-0.55f, 0.55f, 0.62f);
    dir1.Normalise();
    light.Init(LIGHT_DIRECTIONAL, dir1, 0.92f, 0.84f, 0.72f);
    RENDERINFO.SetLight(0, &light);
    RENDERINFO.SetLightEnable(0, TRUE);

    FVector dir2(0.65f, -0.45f, 0.55f);
    dir2.Normalise();
    light.Init(LIGHT_DIRECTIONAL, dir2, 0.52f, 0.62f, 0.78f);
    RENDERINFO.SetLight(1, &light);
    RENDERINFO.SetLightEnable(1, TRUE);

    FVector dir3(0.0f, 0.2f, 0.95f);
    dir3.Normalise();
    light.Init(LIGHT_DIRECTIONAL, dir3, 0.45f, 0.46f, 0.48f);
    RENDERINFO.SetLight(2, &light);
    RENDERINFO.SetLightEnable(2, TRUE);
    for (SINT li = 3; li < 8; ++li) RENDERINFO.SetLightEnable(li, FALSE);

    RENDERINFO.SetLightingEnabled(TRUE);
    RENDERINFO.SetFogEnabled(false);

    LT.D3D_SetMaterial(STATE.GetDefaultMaterial());
    LT.SRS(D3DRS_ZENABLE, D3DZB_TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
    LT.SRS(D3DRS_FILLMODE, D3DFILL_SOLID);
    LT.SRS(D3DRS_NORMALIZENORMALS, TRUE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SetAlphaRef(8);
    LT.SRS(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.SRS(D3DRS_COLORVERTEX, TRUE);
    LT.SRS(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    LT.SRS(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.SetModulate(0);
    LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
    RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
    RENDERINFO.Apply();

    const SINT alphaWas = CMESHRENDERER::GetRenderAlpha();
    const ERenderMode modeWas = CMESHRENDERER::GetRenderMode();
    CMESHRENDERER::SetRenderAlpha(255);
    CMESHRENDERER::SetRenderMode(R_Normal);

    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, gBank.residentWing)) continue;
        CMesh *mesh = gLoaded[i].mesh;
        if (!mesh || !mesh->mBoundingBox || !mesh->mBoundingBox->mValid) continue;

        const Spec &sp = kManifest[i];
        const CBoundingBox *box = mesh->mBoundingBox;
        const float spanXY = 2.0f * std::sqrt(box->mAxes.X * box->mAxes.X + box->mAxes.Y * box->mAxes.Y);
        const float span = spanXY > (2.0f * box->mAxes.Z) ? spanXY : (2.0f * box->mAxes.Z);
        const float scale = ComputeScale(span, sp.targetSpan);

        FMatrix rot;
        rot.MakeRotationF(gBank.slots[i].yawRad, 0.0f, sp.bankRad);
        FMatrix scaledOri = rot * scale;

        // Place bounding box bottom cleanly on top of the plinth (native Z counts downward!).
        const float lowestZ = box->mOrigin.Z + box->mAxes.Z;
        const float halfH = box->mAxes.Z * scale;
        const FVector localCentre(box->mOrigin.X, box->mOrigin.Y, lowestZ - box->mAxes.Z);
        const FVector offset = scaledOri * localCentre;

        const float wx = CeMuseumWorld::kEntranceX + sp.localX;
        const float wy = CeMuseumWorld::kEntranceY + sp.localY;
        const float plinthZ = floorZ - sp.plinthTopZ;
        FVector pos(wx - offset.X, wy - offset.Y, plinthZ - halfH - offset.Z);

        CMESHRENDERER::Render(&pos, &scaledOri, mesh, NULL, NULL, 0, FALSE, 0);
    }

    CMESHRENDERER::SetRenderAlpha(alphaWas);
    CMESHRENDERER::SetRenderMode(modeWas);

    savedState->Apply();
    savedState->Release();
    LT.InvalidateAllStates();
    RENDERINFO.SetAmbient(savedAmbient);
    for (SINT li = 0; li < 8; ++li) {
        RENDERINFO.SetLight(li, &savedLights[li]);
        RENDERINFO.SetLightEnable(li, savedLightEnable[li]);
    }
    RENDERINFO.SetLightingEnabled(savedLighting);
    RENDERINFO.SetFogEnabled(savedFog);
    RENDERINFO.mWorld = *(SRenderMatrix *)&savedWorld;
    // savedState->Apply() reverts D3D9 transforms/fog/FVF to before our Apply() ran; mark
    // RENDERINFO dirty so subsequent WORLD.Render() native exhibits receive the live view/proj.
    RENDERINFO.mWorldChanged = true;
    RENDERINFO.mViewChanged = true;
    RENDERINFO.mProjectionChanged = true;
    RENDERINFO.mFogStartChanged = true;
    RENDERINFO.mFogEndChanged = true;
    RENDERINFO.mFogDensityChanged = true;
    RENDERINFO.mFVFChanged = true;
    RENDERINFO.Apply();
}

} // namespace CeMuseumTurntable
#else
#include "ce_museum_turntable.h"
namespace CeMuseumTurntable {
void RuntimeInit() {}
void RuntimeShutdown() {}
void RuntimeUpdate(CeMuseumTour::Wing, float, bool, float, float, float, float, float, float) {}
bool RuntimeHandleFire(bool, float) { return false; }
bool RuntimeIsAimingControl() { return false; }
bool RuntimeCue(float, float, float, float, CeMuseumTour::Wing, char *, unsigned, char *, unsigned, char *, unsigned) { return false; }
void RuntimeDraw(float) {}
} // namespace CeMuseumTurntable
#endif
