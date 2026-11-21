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

namespace {
// The picture at x, y, w by h on the front end's virtual 640 by 480 plane (the wide-screen box included).
void DrawPicture(float x, float y, float w, float h) {
    IDirect3DDevice9* device = LT.GetDevice();
    if (!device || !picture) return;
    D3DVIEWPORT9 vp;
    if (FAILED(device->GetViewport(&vp))) return;
    float sx = vp.Width / 640.f, sy = vp.Height / 480.f, left = float(vp.X), up = float(vp.Y);
    if (PortWideActive()) { sx = sy = PortUiScale(); left = PortUiLeft(); up = PortUiTop(); }
    const float x0 = left + x*sx - 0.5f, x1 = left + (x+w)*sx - 0.5f;
    const float y0 = up + y*sy - 0.5f, y1 = up + (y+h)*sy - 0.5f;
    const Vertex quad[4] = {{x0,y0,0.f,1.f,0.f,0.f},{x1,y0,0.f,1.f,1.f,0.f},
                            {x0,y1,0.f,1.f,0.f,1.f},{x1,y1,0.f,1.f,1.f,1.f}};
    IDirect3DStateBlock9* state = NULL;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state)) || !state) return;
    if (FAILED(state->Capture())) { state->Release(); return; }
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
    state->Apply();
    state->Release();
}
}

void PortCeCataloguePreviewDraw(float above) {
    if (!picture || !LT.GetDevice()) return;
    const float foot = above - 5.f, top = foot - 118.f > 106.f ? foot - 118.f : 106.f;
    if (foot - top < 12.f) return;
    float w = 228.f, h = w * sourceH / sourceW;
    if (h > foot - top - 8.f) { h = foot - top - 8.f; w = h * sourceW / sourceH; }
    float x = 320.f - w * 0.5f, y = (top + foot - h) * 0.5f;
    FRONTEND.DrawPanel(202.f, top, 438.f, foot, 0.55f, 0xe0000000);
    FRONTEND.DrawBox(202.f, top, 438.f, foot, 0xff807664, 1.f, 0.5f);
    DrawPicture(x, y, w, h);
}

// The picture as large as the rectangle allows with its shape kept, on a subtle checkerboard when it has transparency;
// the rectangle's own panel and outline match the list box beside it. False when there is no picture to draw.
bool PortCeCataloguePreviewDrawIn(float left, float top, float right, float bottom, bool with_picture, bool checker) {
    FRONTEND.DrawPanel(left, top, right, bottom, 0.55f, 0xa0000000);
    FRONTEND.DrawBox(left, top, right, bottom, 0x80ffffff, 2.f, 0.5f);
    if (!with_picture || !picture || !LT.GetDevice() || !sourceW || !sourceH) return false;
    const float room_w = right - left - 12.f, room_h = bottom - top - 12.f;
    float w = room_w, h = w * sourceH / sourceW;
    if (h > room_h) { h = room_h; w = h * sourceW / sourceH; }
    const float x = (left + right - w) * 0.5f, y = (top + bottom - h) * 0.5f;
    FRONTEND.DrawPanel(x, y, x + w, y + h, 0.55f, 0xff404040);
    if (checker)
    {
        // a subtle checkerboard of two greys behind a picture with transparency, so both white and dark translucent textures read
        const float cell = 8.f;
        for (int row = 0; y + row * cell < y + h; ++row)
        {
            const float cy = y + row * cell;
            const float cy1 = cy + cell < y + h ? cy + cell : y + h;
            for (int col = 0; x + col * cell < x + w; ++col)
            {
                if (!((row + col) & 1)) continue;
                const float cx = x + col * cell;
                const float cx1 = cx + cell < x + w ? cx + cell : x + w;
                FRONTEND.DrawPanel(cx, cy, cx1, cy1, 0.55f, 0xff626262);
            }
        }
    }
    DrawPicture(x, y, w, h);
    return true;
}

// One or two lines of the smaller face, centred between left and right, from y down.
void PortCeCataloguePaneText(const char* first, const char* second, float left, float right, float y) {
    CDXBitmapFont* font = PLATFORM.Font(FONT_SMALL);
    const char* lines[2] = {first, second};
    for (int i = 0; i < 2; i++) {
        if (!lines[i] || !*lines[i]) continue;
        WCHAR wide[128]; int n = 0;
        for (; lines[i][n] && n < 127; n++) wide[n] = WCHAR((unsigned char)lines[i][n]);
        wide[n] = 0;
        // a line wider than the pane wraps at its last space that fits, so the whole text shows (two lines at most)
        WCHAR* start = wide;
        for (int part = 0; part < 2 && *start; part++) {
            SIZE size; font->GetTextExtent(start, &size);
            WCHAR* rest = NULL;
            if (part == 0 && float(size.cx) > right - left) {
                WCHAR* end = start; while (*end) end++;
                for (WCHAR* at = end; at > start; at--) {
                    if (*at != WCHAR(' ')) continue;
                    *at = 0; font->GetTextExtent(start, &size);
                    if (float(size.cx) <= right - left) { rest = at + 1; break; }
                    *at = WCHAR(' ');
                }
                if (!rest) font->GetTextExtent(start, &size);
            }
            font->DrawTextWithShadow((left + right - float(size.cx)) * 0.5f, y, i ? 0xff808080 : 0xffffffff, start, 0, 0, 1.f, 1.f);
            y += float(font->GetHeight()) + 2.f;
            if (!rest) break;
            start = rest;
        }
    }
}

float PortCeCatalogueNow() { return PLATFORM.GetSysTimeFloat(); }

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

// The Goodies' own names (patch 0251): each model goodie's model file and each picture goodie's picture file, with its title.
extern "C" bool PortCeGoodieFiles(int goodie, const char **mesh, const char **picture, WCHAR *title);
namespace
{
bool SameFold(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char x = a[i], y = b[i];
        if (x == '\\') x = '%';
        if (y == '\\') y = '%';
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
void Utf8(const WCHAR* text, char* utf8, unsigned size)
{
    unsigned n = 0;
    for (; *text && n + 4 < size; ++text)
    {
        unsigned c = *text;
        if (c < 0x80) utf8[n++] = char(c);
        else if (c < 0x800) { utf8[n++] = char(0xc0 | (c >> 6)); utf8[n++] = char(0x80 | (c & 63)); }
        else { utf8[n++] = char(0xe0 | (c >> 12)); utf8[n++] = char(0x80 | ((c >> 6) & 63)); utf8[n++] = char(0x80 | (c & 63)); }
    }
    utf8[n] = 0;
}
}

int PortCeCatalogueGoodieModel(const char* file)
{
    if (!file) return -1;
    const size_t length = strlen(file);
    for (int goodie = 0; goodie < 78; goodie++)
    {
        const char *mesh = NULL, *picture = NULL;
        WCHAR title[256];
        if (!PortCeGoodieFiles(goodie, &mesh, &picture, title) || !mesh) continue;
        const size_t n = strlen(mesh);
        if (length == n + 6 && SameFold(file, "m_", 2) && SameFold(file + 2, mesh, n) && SameFold(file + 2 + n, ".aya", 4))
            return goodie;
    }
    return -1;
}

// The model pane (patch 0251: the Goodies page's own loading and lighting), for ce_catalogue_list.cpp.
extern "C" bool PortCeGoodieModelLoad(int goodie);
extern "C" void PortCeGoodieModelFree();
extern "C" bool PortCeGoodieModelDraw(float left, float top, float right, float bottom, float yaw);

bool PortCeCatalogueModelLoad(int goodie)
{
    return PortCeGoodieModelLoad(goodie);
}
void PortCeCatalogueModelFree()
{
    PortCeGoodieModelFree();
}
bool PortCeCatalogueModelDraw(float left, float top, float right, float bottom, float yaw)
{
    FRONTEND.DrawPanel(left, top, right, bottom, 0.55f, 0xb80e1522);
    FRONTEND.DrawBox(left, top, right, bottom, 0x80ffffff, 2.f, 0.5f);
    return PortCeGoodieModelDraw(left + 3.f, top + 3.f, right - 3.f, bottom - 3.f, yaw);
}

void PortCeCatalogueTextWords(void (*add)(const char* word, void* context), void* context)
{
    if (!add || !TEXT.mLoaded || !TEXT.mFile) return;
    for (SINT n = 0; n < TEXT.mNumEntries; n++)
    {
        const WCHAR* text = TEXT.GetStringById(TEXT.mFile->mEntries[n].mID);
        char word[32]; unsigned length = 0;
        for (; text; ++text)
        {
            const WCHAR c = *text;
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (letter && length < sizeof(word) - 1) { word[length++] = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); continue; }
            if (!letter && length >= 3) { word[length] = 0; add(word, context); }
            if (!letter) length = 0;
            if (!c) break;
        }
    }
}

bool PortCeCatalogueGoodieName(int kind, const char* file, char* utf8, unsigned size)
{
    if (!file || !utf8 || size < 8 || !TEXT.mLoaded) return false;
    const size_t length = strlen(file);
    for (int goodie = 0; goodie < 78; goodie++)		// the goodies with titles (get_goodie_title)
    {
        const char *mesh = NULL, *picture = NULL;
        WCHAR title[256];
        if (!PortCeGoodieFiles(goodie, &mesh, &picture, title) || !title[0]) continue;
        bool match = false;
        if (kind == 0 && mesh)
        {
            // a model file is "m_" + the mesh's name + ".aya"
            const size_t n = strlen(mesh);
            match = length == n + 6 && SameFold(file, "m_", 2) && SameFold(file + 2, mesh, n) && SameFold(file + 2 + n, ".aya", 4);
        }
        else if (kind == 1 && picture)
        {
            // a picture file is its path with "%" for each folder mark, then "(n)" and its format
            const size_t n = strlen(picture);
            match = length > n && file[n] == '(' && SameFold(file, picture, n);
        }
        if (!match) continue;
        Utf8(title, utf8, size);
        return utf8[0] != 0;
    }
    return false;
}

void PortCeCatalogueSpeechLinesEnd()
{
    free(sLines); sLines = NULL; sLineCount = 0; sLinesTable = NULL; sLinesLanguage = -1;
    sOtherText.Shutdown(); sOtherText.Init(); sOtherLanguage = -1;
}
#else
// A private build has no Catalogue (it is a CE page); these let ce_catalogue_menu.cpp and ce_catalogue_sounds.cpp link there.
#include "ce_catalogue_sounds.h"
void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor, void*) {}
bool PortCeCatalogueSoundStart(const char*) { return false; }
void PortCeCatalogueSoundStop() {}
bool PortCeCatalogueSoundActive() { return false; }
bool PortCeCatalogueSpeechStart(const char*, char* why, unsigned why_size) { if (why && why_size) why[0] = 0; return false; }
const char* PortCeCatalogueLanguage() { return ""; }
bool PortCeCatalogueSpeechLine(const char*, const char*, char*, unsigned) { return false; }
void PortCeCatalogueSpeechLinesEnd() {}
bool PortCeCatalogueGoodieName(int, const char*, char*, unsigned) { return false; }
void PortCeCatalogueTextWords(void (*)(const char*, void*), void*) {}
int PortCeCatalogueGoodieModel(const char*) { return -1; }
bool PortCeCatalogueModelLoad(int) { return false; }
void PortCeCatalogueModelFree() {}
bool PortCeCatalogueModelDraw(float, float, float, float, float) { return false; }
#endif
