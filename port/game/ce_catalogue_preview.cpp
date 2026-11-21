// SPDX-License-Identifier: GPL-3.0-only
// An installed image, framed above its native file details. The catalogue opens the exact selected file.
#ifdef CE_VERSION
#include "common.h"
#include "frontend.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "d3dx9.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_preview.h"
#include "ce_catalogue_sounds.h"
#include "SoundManager.h"
#include "PCSoundManager.h"
#include "AudibleThing.h"
#include "OggFileRead.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
IDirect3DTexture9* picture;
IDirect3DStateBlock9* saved;
unsigned sourceW, sourceH;
struct Vertex { float x, y, z, rhw; float u, v; };

int Upload(const unsigned char* dds, size_t bytes, const CeAssetTextureHeader* h, void* context) {
    IDirect3DDevice9* device = static_cast<IDirect3DDevice9*>(context);
    IDirect3DTexture9* candidate = NULL;
    // A single level; the original proportions are kept even on devices that pad texture dimensions.
    const unsigned side = h->width > h->height ? h->width : h->height;
    const float s = side > 512 ? 512.f / side : 1.f;
    UINT w = UINT(h->width * s + 0.5f), height = UINT(h->height * s + 0.5f);
    if (FAILED(D3DXCreateTextureFromFileInMemoryEx(device, dds, UINT(bytes), w ? w : 1, height ? height : 1,
               1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_TRIANGLE, D3DX_FILTER_NONE, 0, NULL, NULL, &candidate)) || !candidate) {
        if (candidate) candidate->Release();
        return 0;
    }
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) {
        candidate->Release();
        return 0;
    }
    picture = candidate;
    sourceW = h->width; sourceH = h->height;
    return 1;
}
}

void PortCeCataloguePreviewClose() {
    PortCeCatalogueSoundsStop();
    PortCeCataloguePictureClose();
}

void PortCeCataloguePictureClose() {
    if (saved) { saved->Release(); saved = NULL; }
    if (picture) { picture->Release(); picture = NULL; }
    sourceW = sourceH = 0;
}

bool PortCeCataloguePreviewOpen(unsigned slot) {
    PortCeCataloguePreviewClose();
    IDirect3DDevice9* device = LT.GetDevice();
    const bool ok = device && PortCeCatalogueReadTexture(slot, Upload, device);
    if (!ok) PortCeCataloguePreviewClose();
    return ok;
}

void PortCeCataloguePreviewDraw(float above) {
    if (!picture || !saved) return;
    IDirect3DDevice9* device = LT.GetDevice();
    if (!device) return;
    const float foot = above - 5.f, top = foot - 118.f > 106.f ? foot - 118.f : 106.f;
    if (foot - top < 12.f) return;
    float w = 228.f, h = w * sourceH / sourceW;
    if (h > foot - top - 8.f) { h = foot - top - 8.f; w = h * sourceW / sourceH; }
    float x = 320.f - w * 0.5f, y = (top + foot - h) * 0.5f;
    FRONTEND.DrawPanel(202.f, top, 438.f, foot, 0.55f, 0xe0000000);
    FRONTEND.DrawBox(202.f, top, 438.f, foot, 0xff807664, 1.f, 0.5f);
    // Match the front end's virtual 640 by 480 plane, including the centred wide-screen box.
    D3DVIEWPORT9 vp;
    if (FAILED(device->GetViewport(&vp))) return;
    float sx = vp.Width / 640.f, sy = vp.Height / 480.f, left = float(vp.X), up = float(vp.Y);
    if (PortWideActive()) { sx = sy = PortUiScale(); left = PortUiLeft(); up = PortUiTop(); }
    const float x0 = left + x*sx - 0.5f, x1 = left + (x+w)*sx - 0.5f;
    const float y0 = up + y*sy - 0.5f, y1 = up + (y+h)*sy - 0.5f;
    const Vertex quad[4] = {{x0,y0,0.f,1.f,0.f,0.f},{x1,y0,0.f,1.f,1.f,0.f},
                            {x0,y1,0.f,1.f,0.f,1.f},{x1,y1,0.f,1.f,1.f,1.f}};
    if (FAILED(saved->Capture())) return;
    device->SetVertexShader(NULL); device->SetPixelShader(NULL);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE); device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE); device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR); device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP); device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0); device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1); device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1); device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE); device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    device->SetTexture(0, picture);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    saved->Apply();
}

// The native sound adapter needs the same CE-only headers as the rest of this file.
#include "ce_catalogue_sound_engine.inc"

// ---- Installed spoken lines (Catalogue > Speech) -----------------------------------------------------------
// The game plays a radio message's line by reading data\sounds\<language>\messagebox\<name>.ogg with its own
// .ogg reader into a 1 MiB buffer (COggLoader, MessageBox.cpp), making a sample of the decoded 16-bit data
// (CreateSampleFromData) and playing it at the radio-message volume (CMessageBox::PlayPendingSamples). The
// catalogue does the same with its own reader, buffer and sample, never the message box's, and plays it
// through the catalogue's owner so Stop and Active above cover it.
namespace {
const unsigned kCatalogueSpeechBytes = 0x100000;          // the message box's VOICE_BUFFER_SIZE
const char kCatalogueSpeechName[] = "ce catalogue spoken line";
CSample* sCatalogueSpeech = NULL;

void CeCatalogueSpeechRelease()
{
    sCatalogueSampleRelease = 0;
    if (!sCatalogueSpeech) return;
    CSample* sample = sCatalogueSpeech;
    sCatalogueSpeech = NULL;
    delete sample;                                       // ~CPCSample ends every event of the sample first
}

void Say(char* why, unsigned size, const char* text) { if (why && size) snprintf(why, size, "%s", text); }

// One language's lines that have a spoken sample: each line's index and a hash of its sample's name without ASCII
// case, built once per text table (the game's headers define min and max, so no standard containers here). The running
// language reads the game's own TEXT; another language's folder reads that language's own data\LANGUAGE file, loaded
// into a second table only while the Catalogue builds its titles (PortCeCatalogueSpeechLinesEnd frees it).
struct SCatalogueLine { DWORD mHash; SINT mEntry; };
SCatalogueLine* sLines = NULL;
SINT sLineCount = 0;
const CText* sLinesTable = NULL;
int sLinesLanguage = -1;
CText sOtherText;
int sOtherLanguage = -1;

// The game's language numbers (CText::GetLanguageName), by folder name.
int LanguageOfFolder(const char* folder)
{
    static const char* const names[] = {"english", "french", "german", "spanish", "italian"};
    for (int i = 0; folder && i < 5; i++)
        if (!stricmp(folder, names[i])) return i;
    return -1;
}

// The text table that names this language folder's lines, or NULL when the game data has none for it.
CText* TableFor(const char* folder)
{
    if (TEXT.mLoaded && folder && !stricmp(folder, TEXT.GetLanguageName())) return &TEXT;
    const int language = LanguageOfFolder(folder);
    if (language < 0) return NULL;
    if (sOtherLanguage == language) return sOtherText.mLoaded ? &sOtherText : NULL;
    if (sLinesTable == &sOtherText) { sLinesTable = NULL; sLinesLanguage = -1; }
    sOtherText.Shutdown(); sOtherText.Init();
    sOtherLanguage = language;
    char name[64];
    static const char* const files[] = {"english", "french", "german", "spanish", "italian"};
    snprintf(name, sizeof name, "data\\LANGUAGE\\%s.DAT", files[language]);
    FILE* file = fopen(name, "rb");                      // the game's own loader treats a missing file as fatal: look first
    if (!file) return NULL;
    fclose(file);
    const int american = gAmericanText;                  // American text replaces English only; this asks for one language
    gAmericanText = 0;
    sOtherText.Load(language);
    gAmericanText = american;
    return sOtherText.mLoaded ? &sOtherText : NULL;
}

DWORD FoldHash(const char* text)
{
    DWORD hash = 2166136261u;
    for (; *text; ++text)
    {
        unsigned char c = (unsigned char)*text;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        hash = (hash ^ c) * 16777619u;
    }
    return hash;
}
}

bool PortCeCatalogueSpeechStart(const char* game_path, char* why, unsigned why_size)
{
    PortCeCatalogueSoundStop();
    CeCatalogueSpeechRelease();
    if (why && why_size) why[0] = 0;
    if (!SOUND.IsInitialised()) { Say(why, why_size, "Sound is not available in this session."); return false; }
    char path[260];
    if (!game_path || strlen(game_path) >= sizeof(path)) { Say(why, why_size, "This sound file could not be opened, so it was skipped."); return false; }
    strcpy(path, game_path);
    COggFileRead* reader = new COggFileRead;
    if (FAILED(reader->Open(path)) || !reader->HasFormat())
    {
        delete reader;
        Say(why, why_size, "This sound file could not be opened or decoded, so it was skipped.");
        return false;
    }
    if (reader->GetChannels() != 1 || reader->GetSampleRate() != 44100)
    {
        reader->Close(); delete reader;
        Say(why, why_size, "This sound file is not in the form the game plays spoken lines in, so it was skipped.");
        return false;
    }
    BYTE* pcm = new BYTE[kCatalogueSpeechBytes];
    UINT size = 0;
    const HRESULT read = reader->Read(kCatalogueSpeechBytes, pcm, &size);
    reader->Close(); delete reader;
    if (FAILED(read) || size < 2)
    {
        delete[] pcm;
        Say(why, why_size, "This sound file could not be decoded, so it was skipped.");
        return false;
    }
    CSample* sample = SOUND.CreateSampleFromData(pcm, int(size & ~1u), FALSE, NULL);
    delete[] pcm;                                        // the sample holds its own copy in its sound buffer
    if (!sample || !static_cast<CPCSample*>(sample)->mDSBuffer)
    {
        delete sample;
        Say(why, why_size, "The game could not make a sound from this file, so it was skipped.");
        return false;
    }
    strncpy(sample->mName, kCatalogueSpeechName, sizeof(sample->mName) - 1);
    sample->mName[sizeof(sample->mName) - 1] = 0;
    sCatalogueSpeech = sample;
    sCatalogueSampleRelease = CeCatalogueSpeechRelease;
    memcpy(sCatalogueSoundName, sample->mName, strlen(sample->mName) + 1);
    fprintf(stderr, "onslaught-ce: catalogue speech: playing %s (%u decoded bytes)\n", path, size);
    SOUND.PlaySample(sample, &sCatalogueSoundOwner, SOUND.GetRadioMessageVolume(), ST_NOTRACKING, false, 0.f, 0.f, -1.0f, false, 1.f, false, true, ST_GAME_SOUND);
    if (PortCeCatalogueSoundActive()) return true;
    PortCeCatalogueSoundStop();
    Say(why, why_size, "The game did not start this sound. Its sound volume may be off.");
    return false;
}

const char* PortCeCatalogueLanguage()
{
    return TEXT.mLoaded ? TEXT.GetLanguageName() : "";
}

bool PortCeCatalogueSpeechLine(const char* folder, const char* stem, char* utf8, unsigned size)
{
    if (!stem || !utf8 || !size) return false;
    CText* table = TableFor(folder);
    if (!table || !table->mLoaded || !table->mFile || !table->mStrings || (table->mFormat != 2 && table->mFormat != 3)) return false;
    if (sLinesTable != table || sLinesLanguage != table->GetLanguage())
    {
        free(sLines);
        sLines = (SCatalogueLine*)malloc(sizeof(SCatalogueLine) * (table->mNumEntries > 0 ? table->mNumEntries : 1));
        sLineCount = 0;
        sLinesTable = table;
        sLinesLanguage = table->GetLanguage();
        for (SINT n = 0; sLines && n < table->mNumEntries; n++)
        {
            const SINT offset = table->GetSoundOffset(n);
            if (offset < 0 || UINT(offset) >= table->mSize) continue;
            sLines[sLineCount].mHash = FoldHash(table->mStrings + offset);
            sLines[sLineCount].mEntry = n;
            sLineCount++;
        }
    }
    const DWORD hash = FoldHash(stem);
    SINT entry = -1;
    for (SINT i = 0; i < sLineCount && entry < 0; i++)
        if (sLines[i].mHash == hash && !stricmp(table->mStrings + table->GetSoundOffset(sLines[i].mEntry), stem))
            entry = sLines[i].mEntry;
    if (entry < 0) return false;
    const WCHAR* text = table->GetStringById(table->mFile->mEntries[entry].mID);
    if (!text || !*text) return false;
    unsigned n = 0;
    for (; *text && n + 4 < size; ++text)
    {
        unsigned c = *text;
        if (c < 0x80) utf8[n++] = char(c);
        else if (c < 0x800) { utf8[n++] = char(0xc0 | (c >> 6)); utf8[n++] = char(0x80 | (c & 63)); }
        else { utf8[n++] = char(0xe0 | (c >> 12)); utf8[n++] = char(0x80 | ((c >> 6) & 63)); utf8[n++] = char(0x80 | (c & 63)); }
    }
    utf8[n] = 0;
    return n != 0;
}

void PortCeCatalogueSpeechLinesEnd()
{
    free(sLines); sLines = NULL; sLineCount = 0; sLinesTable = NULL; sLinesLanguage = -1;
    sOtherText.Shutdown(); sOtherText.Init(); sOtherLanguage = -1;
}
#endif
