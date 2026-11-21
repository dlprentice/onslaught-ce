// Native-font painter for the projector console's Presentation, in the HUD's own capsules.
namespace {
void CinemaNativeText(const char *source, WCHAR *out, unsigned capacity, CDXBitmapFont *font) {
    if (!out || !capacity) return;
    unsigned used = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(source ? source : "");
    while (*p && used + 1 < capacity) {
        unsigned code = *p++, extra = 0, minimum = 0;
        if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; extra = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; extra = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; extra = 3; minimum = 0x10000; }
        else if (code >= 0x80) code = '?';
        bool valid = true;
        for (unsigned i = 0; i < extra; ++i) {
            if (!p[i] || (p[i] & 0xc0) != 0x80) { valid = false; break; }
            code = (code << 6) | (p[i] & 0x3f);
        }
        if (valid) p += extra;
        if (!valid || code < minimum || code > 0xffff || (code >= 0xd800 && code <= 0xdfff)) code = '?';
        if (code < 32) code = ' ';
        const WCHAR c = static_cast<WCHAR>(code);
        out[used++] = font && font->CanDisplay(c) ? c : static_cast<WCHAR>('?');
    }
    out[used] = 0;
}
float CinemaNativeMeasure(const char *text, void *user) {
    CDXBitmapFont *font = static_cast<CDXBitmapFont *>(user);
    if (!font) return 0;
    WCHAR native[256];
    CinemaNativeText(text, native, sizeof(native)/sizeof(native[0]), font);
    SIZE extent = {};
    font->GetTextExtent(native, &extent);
    return static_cast<float>(extent.cx);
}
DWORD CinemaStyleColour(CeMuseumCinemaConsole::Style style) {
    const CeMuseumCinemaConsole::Rgba c = CeMuseumCinemaConsole::StyleColour(style);
    return D3DCOLOR_ARGB(c.a, c.r, c.g, c.b);
}
// Flat rectangles (a pressed control's glow, the bound Fire label's chip, a control that can do nothing, and the whole panel
// where the HUD's capsule art is missing), in the Presentation's 640x480 units.
void CinemaFlatRects(const CeMuseumCinemaConsole::PanelRect *rects, unsigned count,
                     float scale, float offsetX, float offsetY) {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev || !count) return;
    IDirect3DStateBlock9 *saved = 0;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }
    struct PanelVertex { float x, y, z, rhw; DWORD colour; };
    PanelVertex vertices[(CeMuseumCinemaConsole::kMaxRects + 4u)*6];
    unsigned used = 0;
    for (unsigned i = 0; i < count && used + 6 <= sizeof(vertices)/sizeof(vertices[0]); ++i) {
        const CeMuseumCinemaConsole::PanelRect &r = rects[i];
        const float x = offsetX + r.x*scale, y = offsetY + r.y*scale;
        const float right = x + r.w*scale, bottom = y + r.h*scale;
        const DWORD colour = CinemaStyleColour(r.style);
        const PanelVertex a = {x,y,0,1,colour}, b = {right,y,0,1,colour};
        const PanelVertex c = {right,bottom,0,1,colour}, d = {x,bottom,0,1,colour};
        vertices[used++] = a; vertices[used++] = b; vertices[used++] = c;
        vertices[used++] = a; vertices[used++] = c; vertices[used++] = d;
    }
    dev->SetVertexShader(0); dev->SetPixelShader(0);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetTexture(0, 0);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    if (used) dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used/3, vertices, sizeof(PanelVertex));
    saved->Apply(); saved->Release();
}
// Sprite and text state after the HUD's bar: no fog or lighting, alpha blended, and text at one times, so the
// Presentation's colours show as written (the editor's capsule text does the same).
void CinemaPrepare() {
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SRS(D3DRS_ALPHAREF, 8);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    RENDERINFO.Apply();
}
bool CinemaCapsuleArt() {
    return HUD.mObjectiveLeft && HUD.mObjectiveRight && HUD.mObjectiveInnerLeft && HUD.mObjectiveInnerCentre &&
           HUD.mObjectiveInnerRight;
}
// The HUD's own capsule (CHud::RenderObjectiveBar, the bar of the objective panel and the radio messages) filling a
// rectangle in Presentation units: the dark body between the cyan end caps, which stand half a height out from each
// end of the body, so the body is the rectangle's width less its height. The bar is laid twice: the HUD's body is a light
// veil made for the dark lower view, and over the hall's pale sky one veil leaves grey notes on grey.
void CinemaCapsule(float x, float y, float w, float h, float scale, float offsetX, float offsetY) {
    if (w <= 0 || h <= 0) return;
    const float W = w*scale, H = h*scale, body = W - H;
    for (int layer = 0; layer < 2; ++layer)
        HUD.RenderObjectiveBar(offsetX + (x + 0.5f*w)*scale, offsetY + (y + 0.5f*h)*scale, body > 0 ? body : 0, H, 1.0f);
    CinemaPrepare();
}
bool CinemaInfoStyle(CeMuseumCinemaConsole::Style s) {
    return s == CeMuseumCinemaConsole::kStyleHeading || s == CeMuseumCinemaConsole::kStyleFilmCount ||
           s == CeMuseumCinemaConsole::kStyleTitle || s == CeMuseumCinemaConsole::kStyleStatus ||
           s == CeMuseumCinemaConsole::kStylePlaying || s == CeMuseumCinemaConsole::kStyleWarning;
}
bool CinemaCueStyle(CeMuseumCinemaConsole::Style s) {
    return s == CeMuseumCinemaConsole::kStyleBinding || s == CeMuseumCinemaConsole::kStyleAction ||
           s == CeMuseumCinemaConsole::kStyleHint;
}
// The panel as the HUD draws its own information: a capsule for what is chosen and playing, one per control (lit end
// caps; a control that can do nothing is a plain dark slot without them), and one for the cue, the bound Fire label on a
// cyan chip inside it. Focus is the yellow label; a held press lights the control's body.
void CinemaPanelShapes(const CeMuseumCinemaConsole::Presentation &panel, float scale, float offsetX, float offsetY) {
    typedef CeMuseumCinemaConsole::PanelRect Rect;
    const unsigned rects = panel.rectCount < CeMuseumCinemaConsole::kMaxRects ? panel.rectCount : CeMuseumCinemaConsole::kMaxRects;
    const unsigned lines = panel.lineCount < CeMuseumCinemaConsole::kMaxLines ? panel.lineCount : CeMuseumCinemaConsole::kMaxLines;
    if (!CinemaCapsuleArt()) {
        CinemaFlatRects(panel.rects, rects, scale, offsetX, offsetY);
        return;
    }
    const float pad = 5.0f, cuePad = 2.0f;
    float infoTop = 1e9f, infoBottom = -1e9f, cueTop = 1e9f, cueBottom = -1e9f;
    for (unsigned i = 0; i < lines; ++i) {
        const CeMuseumCinemaConsole::TextLine &l = panel.lines[i];
        float *top = CinemaInfoStyle(l.style) ? &infoTop : CinemaCueStyle(l.style) ? &cueTop : 0;
        float *bottom = top == &infoTop ? &infoBottom : &cueBottom;
        if (!top) continue;
        if (l.y < *top) *top = l.y;
        if (l.y + l.height > *bottom) *bottom = l.y + l.height;
    }
    Rect flat[CeMuseumCinemaConsole::kMaxRects + 4u];
    unsigned flats = 0;
    for (unsigned i = 0; i < rects; ++i) {
        const Rect &r = panel.rects[i];
        if (r.style == CeMuseumCinemaConsole::kStyleBindingCap) {
            if (r.y < cueTop) cueTop = r.y;
            if (r.y + r.h > cueBottom) cueBottom = r.y + r.h;
        }
    }
    const float left = panel.panel.x, width = panel.panel.w;
    const float top = panel.panel.y, bottom = panel.panel.y + panel.panel.h;
    if (infoBottom > infoTop) {
        const float y = infoTop - pad > top ? infoTop - pad : top;
        CinemaCapsule(left, y, width, infoBottom + pad - y, scale, offsetX, offsetY);
    }
    if (cueBottom > cueTop) {
        const float y = cueTop - cuePad, b = cueBottom + cuePad < bottom ? cueBottom + cuePad : bottom;
        CinemaCapsule(left, y, width, b - y, scale, offsetX, offsetY);
    }
    for (unsigned i = 0; i < rects; ++i) {
        const Rect &r = panel.rects[i];
        if (r.style == CeMuseumCinemaConsole::kStyleBindingCap) {
            flat[flats++] = r;
        } else if (r.region != CeMuseumCinemaControls::kRegionNone) {
            if (r.style == CeMuseumCinemaConsole::kStyleKeyDisabled) {
                Rect slot = r;
                slot.x += 0.5f*r.h; slot.w -= r.h;   // the body only: no caps on a control that can do nothing
                if (slot.w > 0) flat[flats++] = slot;
                continue;
            }
            CinemaCapsule(r.x, r.y, r.w, r.h, scale, offsetX, offsetY);
            if (r.style == CeMuseumCinemaConsole::kStyleKeyPressed) {
                Rect glow = r;
                glow.x += 0.5f*r.h; glow.w -= r.h; glow.y += 0.15f*r.h; glow.h -= 0.3f*r.h;
                if (glow.w > 0) flat[flats++] = glow;
            }
        }
    }
    CinemaFlatRects(flat, flats, scale, offsetX, offsetY);
}
void CinemaPanelDraw() {
    if (!PortCeMuseumVisitActive() || GAME.IsMultiplayer()) return;
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font || HUD.mViewportWidth <= 0 || HUD.mViewportHeight <= 0) return;
    CeMuseumCinemaConsole::Presentation panel = {};
    if (!PortCeMuseumCinemaPanel(&panel, CinemaNativeMeasure, font, static_cast<float>(font->GetHeight()))) return;
    const float sx = HUD.mViewportWidth/640.0f, sy = HUD.mViewportHeight/480.0f;
    const float scale = sx < sy ? sx : sy;
    const float offsetX = HUD.mViewportX + (HUD.mViewportWidth - 640.0f*scale)*0.5f;
    const float offsetY = HUD.mViewportY + (HUD.mViewportHeight - 480.0f*scale)*0.5f;
    CeHudScope scope(0, HUD.mViewportWidth, HUD.mViewportHeight, 0, 0);
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    RENDERINFO.Apply();
    CinemaPanelShapes(panel, scale, offsetX, offsetY);
    CinemaPrepare();
    for (unsigned i = 0; i < panel.lineCount && i < sizeof(panel.lines)/sizeof(panel.lines[0]); ++i) {
        const CeMuseumCinemaConsole::TextLine &line = panel.lines[i];
        WCHAR native[256];
        CinemaNativeText(line.text, native, sizeof(native)/sizeof(native[0]), font);
        font->DrawTextWithShadow(offsetX + line.x*scale, offsetY + line.y*scale,
            CinemaStyleColour(line.style), native, 0, 0, scale, scale);
    }
}
} // namespace
