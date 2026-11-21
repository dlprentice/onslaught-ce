namespace CinemaControls = CeMuseumCinemaControls;
namespace CinemaConsole = CeMuseumCinemaConsole;
// SPDX-License-Identifier: GPL-3.0-only
// The theater is owned by the current Museum visit. No projector operation opens
// a window, replaces the camera, changes career unlocks or starts a frontend film.
namespace Cinema {
enum Phase { Idle, Playing, Finished, Missing };
struct State {
    HBINK film;
    IDirect3DTexture9 *texture;
    unsigned int bitmapW, bitmapH, frame;
    Phase phase;
    bool inRoom, atConsole, captured, inDoorway, exhausted, fireHeld;
    CeMuseumCinemaCatalogue::List catalogue;
    bool catalogueLoaded, firstPlayable;
    int playing;
    CinemaControls::State controls;
    CinemaControls::Output output;
    CinemaConsole::Hit hit;
    bool viewValid;
    unsigned viewPresent;
    float eye[3], aim[3];
    State() : film(0), texture(0), bitmapW(0), bitmapH(0), frame(0),
        phase(Idle), inRoom(false), atConsole(false), captured(false),
        inDoorway(false), exhausted(false), fireHeld(false),
        catalogueLoaded(false), firstPlayable(true), playing(-1), controls(), output(),
        hit(CinemaConsole::NoHit()),
        viewValid(false), viewPresent(0), eye{0,0,0}, aim{0,0,0} { CinemaControls::Reset(&controls); }
} s;
const DWORD kFloorDark=0xff39444b,kCarpet=0xff4b3539,kUpholstery=0xff913f43,kSeatCap=0xffb16961;
const DWORD kBronze=0xffb99262,kStone=0xff69767a,kProscenium=0xff1c272d,kIdleScreen=0xff91a3a3;
}

extern "C" int PortCeTutorialPrintableKey(int key,char *out,int size);

void CinemaClose(Cinema::Phase next) {
    using namespace Cinema;
    if (s.film) { BinkClose(s.film);s.film=0; }
    if (s.texture) { s.texture->Release();s.texture=0; }
    s.frame=0;s.bitmapW=s.bitmapH=0;s.exhausted=false;s.phase=next;
    s.playing = -1;
}
void CinemaReset() {
    const CinemaControls::State retained = Cinema::s.controls;
    CinemaClose(Cinema::Idle);
    Cinema::s = Cinema::State();
    Cinema::s.controls = retained;
}

// height is the hall's wall height for a planned hall (0 for the older hall): the
// theater's walls then rise to one roof with the hall, so no sky shows between them.
void CinemaShell(bool planned, float floor, float height) {
    using namespace CeCinemaRoom; using namespace Cinema;
    // The side walls are clad as the hall's walls are (the Forseti hangar band), so
    // the two rooms match; the screen wall is the same band, tinted dark.
    const DWORD kWall=CeMuseumWorld::kWall,kWallCap=CeMuseumWorld::kWallCap;
    const CeHallPoint o={CeMuseumWorld::kEntranceX,CeMuseumWorld::kEntranceY};
    auto box=[&](float x,float y,float hw,float hd,float lift,float ht,DWORD a,DWORD b) {
        FittedBox(Offset(o,x,y),hw,hd,lift,ht,a,b);
    };
    Tile(Offset(o,(kLeft+kRight)*.5f,(kFront+kBack)*.5f),(kRight-kLeft)*.5f,(kBack-kFront)*.5f,.035f,kFloorDark);
    // A planned exhibition frames the portal itself (piers, header, accent) and its
    // walking surface already reaches the forecourt, so only the room is built here.
    if (!planned) {
        // A gold aisle branches from the nearest cross corridor. The portal's clapper
        // has a perforated top edge and three raised bars: a cue readable in monochrome.
        Walk(Offset(o,1.4f,kDoorY),Offset(o,kDoorX+2.2f,kDoorY));
        for (int i=-1;i<=1;i+=2) {
            box(kDoorX,kDoorY+i*kDoorHalf,.56f,.34f,0,5.35f,kBronze,kEdge);
            box(kDoorX-.41f,kDoorY+i*kDoorHalf,.24f,.49f,0,.42f,kIron,kSlate);
        }
        box(kDoorX,kDoorY,.56f,kDoorHalf+.40f,5.05f,.69f,kProscenium,kBronze);
        for(unsigned i=0;i<5;++i) box(kDoorX-.58f,kDoorY-2.15f+1.05f*i,.08f,.17f,5.31f,.24f,kLight,kLight);
    }
    // The screen wall needs 8.9 m; a roofed theater rises at least that far everywhere.
    const float roof = planned && height > 0 ? (height > 8.9f ? height : 8.9f) : 0;
    box(kRight,(kFront+kBack)*.5f,.33f,(kBack-kFront)*.5f,0,roof ? roof : 6.8f,kWall,kWallCap);
    box((kLeft+kRight)*.5f,kFront,(kRight-kLeft)*.5f,.32f,0,roof ? roof : 7.1f,kWall,kWallCap);
    box((kLeft+kRight)*.5f,kBack,(kRight-kLeft)*.5f,.32f,0,roof ? roof : 8.9f,kProscenium,kBronze);
    if (roof) {
        // Above the hall's roof, the shared wall continues up to the theater's roof.
        const float wall = CeMuseumExhibition::kWallHalf;
        if (roof > height) box(kLeft,(kFront+kBack)*.5f,wall,(kBack-kFront)*.5f+.32f,height,roof-height,kWall,kWallCap);
        Ceiling(floor,kLeft+wall,kFront-.32f,kRight+.33f,kBack+.32f,roof);
    }
    box(kScreenX,kScreenY+.20f,kScreenHalfW+.42f,.17f,-(kScreenZ+kScreenHalfH)-.32f,2*kScreenHalfH+.64f,kBronze,kStone);
    box(kScreenX,kScreenY+.10f,kScreenHalfW,.10f,-(kScreenZ+kScreenHalfH),2*kScreenHalfH,kIdleScreen,kIdleScreen);
    // The seats and tiers rise away from the screen. The center aisle is level,
    // continuous and walkable; occupied seat rectangles share the visitor boundary.
    for(unsigned row=0;row<3;++row) {
        const float y=1.0f-row*1.6f,h=.16f+row*.30f;
        for(unsigned bank=0;bank<2;++bank) {
            box(SeatX(bank*3+1),y,2.35f,.73f,0,h,kCarpet,kCarpet);
            for(unsigned n=0;n<3;++n) {
                const float x=SeatX(bank*3+n);
                box(x,y,.50f,.42f,h,.35f,kUpholstery,kSeatCap);
                box(x,y-.41f,.50f,.10f,h+.29f,.81f,kUpholstery,kSeatCap);
            }
        }
    }
    box(kConsoleX,kConsoleY,.38f,.28f,0,1.15f,kSlate,kBronze);
    box(kConsoleX,kConsoleY,.47f,.34f,1.15f,.22f,kBronze,kLight);
    // Small aisle lights point to the screen, even when the projector is off.
    for(unsigned i=0;i<3;++i)
        box(kWallX+5.9f,3.0f+2.2f*i,.16f,.36f,.02f,.10f,kLight,kLight);
}

bool CinemaProjectionTarget() {
    using namespace Cinema;
    if (!s.film) return false;
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (!dev || FAILED(dev->CreateTexture(s.film->Width,s.film->Height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&s.texture,0)) || !s.texture) return false;
    s.bitmapW=s.film->Width;s.bitmapH=s.film->Height;return true;
}
void CinemaCaptureView(const float *view, unsigned present) {
    Cinema::State &s = Cinema::s;
    s.viewValid = PortCeMuseumAuthoredMap() && CeMuseumCinemaView::CentreRay(view, s.eye, s.aim);
    if (s.viewValid) s.viewPresent = present;
}
bool CinemaFilmPath(const char *stem, char *out, unsigned capacity) {
    if (!out || !capacity || !CeMuseumCinemaCatalogue::SafeStem(stem, CeMuseumCinemaCatalogue::kStemCapacity)) return false;
    const int size = std::snprintf(out, capacity, "data/video/%s.vid", stem);
    return size > 0 && static_cast<unsigned>(size) < capacity;
}
bool CinemaCatalogueName(void *, unsigned index, char *stem, unsigned capacity) {
    return PortCeMuseumCinemaFilmName(index, stem, capacity);
}
bool CinemaCatalogueAvailable(void *, const char *stem) {
    char path[512];
    if (!CinemaFilmPath(stem, path, sizeof(path))) return false;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}
void CinemaCatalogueLoad() {
    Cinema::State &s = Cinema::s;
    if (s.catalogueLoaded) return;
    s.catalogue = CeMuseumCinemaCatalogue::Build(CinemaCatalogueName, CinemaCatalogueAvailable, 0, 512);
    s.catalogueLoaded = true;
}
bool CinemaStartFilm(void *, const CeMuseumCinemaCatalogue::Entry &entry) {
    using namespace Cinema;
    CinemaClose(Idle);
    char path[512];
    if (!CinemaFilmPath(entry.stem, path, sizeof(path))) return false;
    s.film = BinkOpen(path, BINKPRELOADALL);
    if (!s.film) return false;
    BinkSetVolume(s.film, 0, 0); // The in-world screening room is silent.
    if (!s.film->Frames || !s.film->Width || !s.film->Height || s.film->Width>2048 || s.film->Height>2048 || !CinemaProjectionTarget()) { CinemaClose(Missing); return false; }
    s.phase = Playing;
    return true;
}
void CinemaOpen(unsigned selected) {
    using namespace Cinema;
    CinemaCatalogueLoad();
    const int opened = CeMuseumCinemaCatalogue::Play(s.catalogue, static_cast<int>(selected), s.firstPlayable, CinemaStartFilm, 0);
    if (opened < 0) { CinemaClose(Missing); s.playing = -1; return; }
    CinemaControls::Select(&s.controls, s.catalogue.count, opened);
    s.playing = opened;
    s.firstPlayable = false;
    PortSessionLog(PORT_SESSION_GAME, false, "Museum cinema selected film started");
}
CinemaControls::Decoder CinemaDecoder() {
    switch (Cinema::s.phase) {
    case Cinema::Playing: return CinemaControls::kDecoderPlaying;
    case Cinema::Finished: return CinemaControls::kDecoderEnded;
    case Cinema::Missing: return CinemaControls::kDecoderUnavailable;
    default: return CinemaControls::kDecoderStopped;
    }
}
CinemaConsole::Mount CinemaConsoleMount() {
    if (!g.valid || !g.ground) return CinemaConsole::Mount{};
    const CeHallPoint origin = {CeMuseumWorld::kEntranceX, CeMuseumWorld::kEntranceY};
    const CeHallPoint centre = Offset(origin, CeCinemaRoom::kConsoleX, CeCinemaRoom::kConsoleY);
    float floor = 0;
    if (!g.ground(centre.x, centre.y, &floor)) return CinemaConsole::Mount{};
    const CinemaConsole::Vec3 at = {centre.x, centre.y, floor - 1.65f};
    // the visitor faces the panel looking along tour.right; in this world (Z grows down) the visitor's right is then -forward
    const CinemaConsole::Vec3 right = {-g.tour.forward.x, -g.tour.forward.y, 0};
    const CinemaConsole::Vec3 up = {0, 0, -1};
    const CinemaConsole::Vec3 front = {-g.tour.right.x, -g.tour.right.y, 0};
    return CinemaConsole::MakeMount(at, right, up, front);
}
CinemaControls::Input CinemaControllerInput() {
    Cinema::State &s = Cinema::s;
    CinemaConsole::View view = {};
    view.museumActive = PortCeMuseumAuthoredMap() && PortCeMuseumVisitActive() && g.valid;
    view.inRoom = s.inRoom;
    view.focused = PortGetFocus() != 0;
    view.paused = GAME.IsPaused();
    view.photo = PortCePhotoOn();
    view.origin = CinemaConsole::Vec3{s.eye[0], s.eye[1], s.eye[2]};
    view.aim = CinemaConsole::Vec3{s.aim[0], s.aim[1], s.aim[2]};
    const bool currentView = s.viewValid && unsigned(PortPresentCount() - s.viewPresent) <= 1;
    s.hit = currentView ? CinemaConsole::Pick(CinemaConsoleMount(), view) :
        CinemaConsole::NoHit();
    s.atConsole = s.hit.atConsole;
    if (view.museumActive && view.inRoom) CinemaCatalogueLoad();
    CinemaControls::Input in = {};
    in.museumActive = view.museumActive;
    in.inRoom = view.inRoom;
    in.fireDown = s.fireHeld;
    in.paused = view.paused || view.photo;
    in.focused = view.focused;
    in.films = s.catalogue.count;
    in.decoder = CinemaDecoder();
    in.playing = s.playing;
    CinemaConsole::Feed(s.hit, &in);
    return in;
}
bool CinemaStep() {
    Cinema::State &s = Cinema::s;
    s.output = CinemaControls::Update(&s.controls, CinemaControllerInput());
    const bool consumed = s.output.consumeFire;
    const CinemaConsole::HostAction action = CinemaConsole::ResolveCommand(s.output, s.catalogue.count);
    if (action.select) s.firstPlayable = false;
    if (action.stop) CinemaClose(Cinema::Idle);
    if (action.play && action.hasIndex) CinemaOpen(action.index);
    if (action.select || action.stop || action.play)
        s.output = CinemaControls::Update(&s.controls, CinemaControllerInput());
    return consumed;
}
void CinemaConsoleGeometry(IDirect3DDevice9 *dev) {
    if (!dev || !PortCeMuseumAuthoredMap()) return;
    CinemaStep();
    Cinema::State &s = Cinema::s;
    CinemaConsole::Geometry shape = {};
    if (!CinemaConsole::Build(CinemaConsoleMount(),
        CinemaConsole::MakeDisplay(s.hit, s.output, s.catalogue.count, CinemaDecoder(), s.playing), &shape)) return;
    Vertex vertices[240];
    unsigned used = 0;
    for (unsigned i = 0; i < shape.faceCount && i < sizeof(shape.faces)/sizeof(shape.faces[0]); ++i) {
        const CinemaConsole::Face &face = shape.faces[i];
        if (face.vertexCount < 3 || face.vertexCount > 4) continue;
        const CinemaConsole::Rgba colour = CinemaConsole::MaterialColour(face.material);
        const DWORD packed = D3DCOLOR_ARGB(colour.a, colour.r, colour.g, colour.b);
        for (unsigned corner = 1; corner + 1 < face.vertexCount && used + 3 <= 240; ++corner) {
            const unsigned triangle[3] = {0, corner, corner + 1};
            for (unsigned v = 0; v < 3; ++v) {
                const CinemaConsole::Vec3 &point = face.vertices[triangle[v]];
                vertices[used++] = Vertex{point.x, point.y, point.z, packed};
            }
        }
    }
    if (used) {
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        dev->SetTexture(0, 0);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3, vertices, sizeof(Vertex));
    }
}
void CinemaVisitor(float x,float y) {
    using namespace Cinema;using namespace CeCinemaRoom;
    const float side=x-CeMuseumWorld::kEntranceX,depth=y-CeMuseumWorld::kEntranceY;
    const bool room=PortCeMuseumAuthoredMap() && Interior(side,depth);
    if(s.inRoom && !room) CinemaClose(Idle);
    s.inRoom=room;s.atConsole = false;
    s.inDoorway=PortCeMuseumAuthoredMap() && Range(side,kDoorX-3.0f,kDoorX+.7f) && Range(depth,kDoorY-3.5f,kDoorY+3.5f);
    CinemaStep();
}
bool CinemaAction(bool release, float value) {
    Cinema::s.fireHeld = !release && value > 0.0f;
    return CinemaStep();
}
void CinemaFireName(char *out,unsigned cap) {
    if(!out||!cap)return;
    SControlBinding *binding=FindControlBinding(BUTTON_MECH_FIRE_GUN_POD);
    if(binding) for(unsigned i=0;i<2;++i) {
        const SControlInput &input=binding->mInput[i];
        if(input.mPad<0)continue;
        if(input.mType==MOUSE_BUTTON_ON||input.mType==MOUSE_BUTTON_ONCE||input.mType==MOUSE_BUTTON_RELEASE) {
            const char *m[]={"Left mouse","Right mouse","Middle mouse","Wheel up","Wheel down"};
            if(input.mKey>=0&&input.mKey<5) {std::snprintf(out,cap,"%s",m[input.mKey]);return;}
        }
        if(input.mType==KEY_ON||input.mType==KEY_ONCE||input.mType==KEY_RELEASE) {
            const int special[]={DIK_TAB,DIK_RETURN,DIK_SPACE,DIK_LEFT,DIK_RIGHT,DIK_UP,DIK_DOWN,DIK_LSHIFT,DIK_RSHIFT,DIK_LCONTROL,DIK_RCONTROL,DIK_LMENU,DIK_RMENU};
            const char *names[]={"Tab","Enter","Space","Left arrow","Right arrow","Up arrow","Down arrow","Left Shift","Right Shift","Left Ctrl","Right Ctrl","Left Alt","Right Alt"};
            for(unsigned k=0;k<sizeof(special)/sizeof(special[0]);++k) if(input.mKey==special[k]) {std::snprintf(out,cap,"%s",names[k]);return;}
            if(PortCeTutorialPrintableKey(input.mKey,out,cap))return;
            std::snprintf(out,cap,"Key %d",int(input.mKey));return;
        }
        if(input.mType==BUTTON_ON||input.mType==BUTTON_ONCE||input.mType==BUTTON_RELEASE||input.mType==BUTTON_REPEAT) {
            std::snprintf(out,cap,"Pad button %d",int(input.mKey)+1);return;
        }
    }
    std::snprintf(out,cap,"Fire");
}
bool CinemaPresentation(CinemaConsole::Presentation *out, CinemaConsole::MeasureText measure,
                        void *user, float lineHeight) {
    if (!out) return false;
    CinemaStep();
    Cinema::State &s = Cinema::s;
    CinemaConsole::PresentInput input = CinemaConsole::MakePresentInput();
    input.focus = s.hit;
    input.controls = s.output;
    input.films = s.catalogue.count;
    input.titleIndex = s.output.selected;
    const CeMuseumCinemaCatalogue::Entry *selected = CeMuseumCinemaCatalogue::At(s.catalogue, s.output.selected);
    const CeMuseumCinemaCatalogue::Entry *playing = CeMuseumCinemaCatalogue::At(s.catalogue, s.playing);
    input.title = selected ? selected->title : 0;
    input.playingTitle = playing ? playing->title : 0;
    char binding[96];
    CinemaFireName(binding, sizeof(binding));
    input.fireLabel = binding;
    input.decoder = CinemaDecoder();
    input.playing = s.playing;
    input.measure = measure;
    input.measureUser = user;
    input.lineHeight = lineHeight;
    *out = CinemaConsole::Present(input);
    return out->visible;
}
bool CinemaCue(char *title, unsigned titleCap, char *line, unsigned lineCap) {
    const Cinema::State &s = Cinema::s;
    if (!PortCeMuseumAuthoredMap() || !title || !titleCap || !line || !lineCap) return false;
    if (s.inRoom) {
        std::snprintf(title, titleCap, "Screening room");
        std::snprintf(line, lineCap, "%s", s.atConsole ?
            "Aim at Previous, Play / Stop, or Next." :
            "Approach the projector to choose a film. Keep to the centre aisle.");
        return true;
    }
    if (s.inDoorway) {
        std::snprintf(title, titleCap, "Screening room");
        std::snprintf(line, lineCap, "Through this doorway: the projector chooses a film.");
        return true;
    }
    return false;
}
void CinemaScreen(IDirect3DDevice9 *dev) {
    using namespace Cinema;using namespace CeCinemaRoom;
    if(!s.inRoom || s.phase!=Playing || !s.texture || !s.film)return;
    // Keep the last projected image on the wall while the normal pause menu is open.
    if(!GAME.IsPaused() && !BinkWait(s.film)) {
        if(s.exhausted) { CinemaClose(Finished);return; }
        BinkDoFrame(s.film);
        D3DLOCKED_RECT image={};
        if(FAILED(s.texture->LockRect(0,&image,0,0))||!image.pBits) {CinemaClose(Missing);return;}
        BinkCopyToBuffer(s.film,image.pBits,image.Pitch,s.bitmapH,0,0,BINKSURFACE32A|BINKCOPYNOSCALING|BINKCOPYALL);
        s.texture->UnlockRect(0);s.frame=s.film->FrameNum;
        s.exhausted=s.frame>=s.film->Frames;BinkNextFrame(s.film);
    }
    if(!s.frame)return;
    const float aspect=float(s.bitmapW)/s.bitmapH,box=kScreenHalfW/kScreenHalfH;
    const float wh=aspect>box?kScreenHalfW:kScreenHalfH*aspect,hh=aspect>box?kScreenHalfW/aspect:kScreenHalfH;
    const float x=CeMuseumWorld::kEntranceX+kScreenX,y=CeMuseumWorld::kEntranceY+kScreenY-.025f,z=kScreenZ;
    struct FilmVertex {float x,y,z,u,v;};
    const FilmVertex screen[]={{x-wh,y,z-hh,0,0},{x+wh,y,z-hh,1,0},{x-wh,y,z+hh,0,1},{x+wh,y,z+hh,1,1}};
    dev->SetTexture(0,s.texture);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,screen,sizeof(FilmVertex));
}
