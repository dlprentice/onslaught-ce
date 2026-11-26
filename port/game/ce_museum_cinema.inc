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
    IDirect3DDevice9 *textureDevice;
    uint64_t textureDeviceGen;
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
    bool padActive, mouseSeen, mouseSeqSeen;
    int lastMouseX, lastMouseY;
    unsigned lastMouseSeq;
    State() : film(0), texture(0), textureDevice(0), textureDeviceGen(0), bitmapW(0), bitmapH(0), frame(0),
        phase(Idle), inRoom(false), atConsole(false), captured(false),
        inDoorway(false), exhausted(false), fireHeld(false),
        catalogueLoaded(false), firstPlayable(true), playing(-1), controls(), output(),
        hit(CinemaConsole::NoHit()),
        viewValid(false), viewPresent(0), eye{0,0,0}, aim{0,0,0},
        padActive(false), mouseSeen(false), mouseSeqSeen(false),
        lastMouseX(0), lastMouseY(0), lastMouseSeq(0) { CinemaControls::Reset(&controls); }
} s;
const DWORD kFloorDark=0xff39444b,kCarpet=0xff4b3539,kUpholstery=0xff913f43,kSeatCap=0xffb16961;
const DWORD kBronze=0xffb99262,kStone=0xff69767a,kProscenium=0xff1c272d,kIdleScreen=0xff91a3a3;
// The idle screen's drawn curtain (the sconces and aisle strips are placed by CeCinemaRoom, ce_museum_cinema.h).
const unsigned kCurtainFolds = 14u;
const DWORD kCurtainValleyLow=0xff160d12, kCurtainCrestLow=0xff2a161d, kCurtainCrestHigh=0xff4a2630, kCurtainValleyHigh=0xff26141b;
}

extern "C" int PortCeTutorialPrintableKey(int key,char *out,int size);
extern "C" int PortMouseHeldNow(unsigned button);
extern "C" unsigned PortMouseActivitySeq(void);

// In the Grand Museum complex the Screening Room sits beyond the Concept Art Gallery's east wall.
static inline float CinemaOffX() { return gGrandMode ? CeMuseumWorld::kGrandCinemaOffsetX : 0.0f; }
static inline float CinemaOffY() { return gGrandMode ? CeMuseumWorld::kGrandCinemaOffsetY : 0.0f; }

// Film sound and playback (ce_museum_film.h holds the behaviour: the level, the crossfade with the music, the frame
// order and the hold while paused; this file forwards the game's Bink calls to it). The music duck is applied by patch
// 0258 (ce_museum_music_duck.h). Leaving the Museum drops the duck at once.
namespace Cinema {
CeMuseumMusicDuck::State duck = CeMuseumMusicDuck::Off();
CeMuseumFilm::Film film = CeMuseumFilm::None();
}
extern "C" int PortBinkHasSound(HBINK bink) __attribute__((weak));	// port/bink: whether the film's track was loaded
extern "C" int PortBinkPause(HBINK bink, int pause) __attribute__((weak));	// port/bink: hold a film, sound and clock
// The cinema stands still while the game is paused or Photo Mode holds the frame:
// the film's frame clock, its sound crossfade and the music duck all read one
// clock, and that clock does not run during a hold (CeMuseumCinemaHeldClock in
// ce_museum_cinema.h). Nothing catches up afterwards: the film resumes on the
// frame it stopped on and the duck resumes its ramp where it was.
bool CinemaHeld() {
    return GAME.IsPaused() || PortCePhotoOn();
}
double CinemaClock() {
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&rate);
    if (rate.QuadPart <= 0) return 0.0;
    static LONGLONG firstQpc = 0;
    static bool haveFirst = false;
    if (!haveFirst) { firstQpc = now.QuadPart; haveFirst = true; }
    static CeMuseumCinemaHeldClock held = CeMuseumCinemaHeldClockNew();
    return CeMuseumCinemaHeldClockRead(&held, double(now.QuadPart - firstQpc) / double(rate.QuadPart), CinemaHeld());
}
extern "C" long PortCeMuseumMusicDuck(void) {
    if (!PortCeMuseumVisitActive()) { Cinema::duck = CeMuseumMusicDuck::Off(); return 0; }
    const double now = CinemaClock();
    CeMuseumMusicDuck::Settle(&Cinema::duck, now);
    const long filmDuck = CeMuseumMusicDuck::Hundredths(CeMuseumMusicDuck::Gain(Cinema::duck, now));
    const long kioskDuck = CeMuseumAudioKiosk::RuntimeMusicDuckHundredths();
    return filmDuck < kioskDuck ? filmDuck : kioskDuck;
}
CeMuseumFilm::Calls CinemaFilmCalls() {
    CeMuseumFilm::Calls c = {};
    c.user = Cinema::s.film;
    c.setVolume = [](void *b, long v) { BinkSetVolume(static_cast<HBINK>(b), 0, (S32)v); };
    c.doFrame = [](void *b) { BinkDoFrame(static_cast<HBINK>(b)); };
    c.nextFrame = [](void *b) { BinkNextFrame(static_cast<HBINK>(b)); };
    c.wait = [](void *b) { return (int)BinkWait(static_cast<HBINK>(b)); };
    c.pause = nullptr;
    if (PortBinkPause) c.pause = [](void *b, int hold) { return PortBinkPause(static_cast<HBINK>(b), hold); };
    c.frameNum = [](void *b) { return (unsigned)static_cast<HBINK>(b)->FrameNum; };
    c.frames = [](void *b) { return (unsigned)static_cast<HBINK>(b)->Frames; };
    return c;
}
void CinemaFilmFade() {
    if (Cinema::s.film) CeMuseumFilm::Fade(&Cinema::film, CinemaFilmCalls(), CinemaClock());
}

void CinemaClose(Cinema::Phase next) {
    using namespace Cinema;
    if (s.film) {
        BinkClose(s.film);s.film=0;
        CeMuseumFilm::Closed(&film, CinemaClock(), &duck);
    }
    if (s.texture) { s.texture->Release();s.texture=0; }
    s.textureDevice=0;
    s.textureDeviceGen=0;
    s.frame=0;s.bitmapW=s.bitmapH=0;s.exhausted=false;s.phase=next;
    s.playing = -1;
}
void CinemaReset() {
    const CinemaControls::State retained = Cinema::s.controls;
    CinemaClose(Cinema::Idle);
    Cinema::duck = CeMuseumMusicDuck::Off();
    Cinema::film = CeMuseumFilm::None();
    Cinema::s = Cinema::State();
    Cinema::s.controls = retained;
}

// height is the hall's wall height for a planned hall (0 for the older hall): the
// theater's walls then rise to one roof with the hall, so no sky shows between them.
void CinemaShell(bool planned, float floor, float height) {
    using namespace CeCinemaRoom; using namespace Cinema;
    const DWORD kWall=CeMuseumWorld::kWall,kWallCap=CeMuseumWorld::kWallCap;
    const float ox = CinemaOffX(), oy = CinemaOffY();
    const CeHallPoint o={CeMuseumWorld::kEntranceX + ox, CeMuseumWorld::kEntranceY + oy};
    auto box=[&](float x,float y,float hw,float hd,float lift,float ht,DWORD a,DWORD b) {
        FittedBox(Offset(o,x,y),hw,hd,lift,ht,a,b);
    };
    g.current = kMatCarpet;
    Tile(Offset(o,(kLeft+kRight)*.5f,(kFront+kBack)*.5f),(kRight-kLeft)*.5f,(kBack-kFront)*.5f,.035f,kFloorDark);
    g.current = kMatAuto;
    if (!planned) {
        Walk(Offset(o,1.4f,kDoorY),Offset(o,kDoorX+2.2f,kDoorY));
        for (int i=-1;i<=1;i+=2) {
            box(kDoorX,kDoorY+i*kDoorHalf,.56f,.34f,0,5.35f,kBronze,kEdge);
            box(kDoorX-.41f,kDoorY+i*kDoorHalf,.24f,.49f,0,.42f,kIron,kSlate);
        }
        box(kDoorX,kDoorY,.56f,kDoorHalf+.40f,5.05f,.69f,kProscenium,kBronze);
        for(unsigned i=0;i<5;++i) box(kDoorX-.58f,kDoorY-2.15f+1.05f*i,.08f,.17f,5.31f,.24f,kLight,kLight);
    }
    const float roof = planned && height > 0 ? (height > 9.5f ? height : 9.5f) : 0;
    if (gGrandMode && roof > 0.0f) {
        // In Grand Mode clad the Screening Room's exposed exterior walls in sunlit coastal limestone
        // matching the Concept Art Gallery (no black exterior slab visible from the Coastal Courtyard),
        // keeping dark acoustic panelling strictly on the interior theater lining.
        const float wall = CeMuseumExhibition::kWallHalf;
        g.current = kMatCinemaWall;
        // Inner dark acoustic theater lining (East, South, and West returns framing Portal 6 inside x > kLeft + wall)
        // Trimmed cleanly to avoid overlapping the South lining, North proscenium wall, and Portal 6 jamb columns/architrave.
        box(kRight - 0.16f, (kFront + kBack) * 0.5f, 0.16f, (kBack - kFront) * 0.5f - 0.32f, 0, roof, kWall, kWallCap);
        box(0.5f * (kLeft + wall + kRight), kFront + 0.16f, 0.5f * (kRight - (kLeft + wall)), 0.16f, 0, roof, kWall, kWallCap);
        {
            const float westX = kLeft + wall + 0.14f;
            const float doorLo = kDoorY - kDoorHalf;
            const float doorHi = kDoorY + kDoorHalf;
            const float jambLo = doorLo - 1.02f;
            const float jambHi = doorHi + 1.02f;
            const float headerLo = 5.06f; // top of Portal 6 architrave (4.60 + 0.46)
            const float capLo = roof - 0.62f;
            if (jambLo > kFront + 0.32f)
                box(westX, (kFront + 0.32f + jambLo) * 0.5f, 0.14f, (jambLo - (kFront + 0.32f)) * 0.5f, 0, roof, kWall, kWallCap);
            if (kBack - 0.32f > jambHi)
                box(westX, (jambHi + kBack - 0.32f) * 0.5f, 0.14f, ((kBack - 0.32f) - jambHi) * 0.5f, 0, roof, kWall, kWallCap);
            if (capLo > headerLo)
                box(westX, kDoorY, 0.14f, kDoorHalf - 0.03f, headerLo, capLo - headerLo, kWall, kWallCap);
            if (roof > capLo)
                box(westX, kDoorY, 0.14f, kDoorHalf - 0.18f, capLo, roof - capLo, kWall, kWallCap);
        }
        // Outer coastal limestone facade cladding on the exposed South (y = 7.0) and East (x = 56.0) faces (mitered SE corner)
        g.current = kMatWall;
        box(0.5f * ((kLeft - wall) + (kRight + 0.36f)), kFront - 0.18f, 0.5f * ((kRight + 0.36f) - (kLeft - wall)), 0.18f, 0, roof, 0xfff5f1e8, 0xfff8f4eb);
        box(kRight + 0.18f, 0.5f * (kFront + kBack + 0.32f), 0.18f, 0.5f * ((kBack + 0.32f) - kFront), 0, roof, 0xfff5f1e8, 0xfff8f4eb);
        // Matching classical exterior cornice & brushed-brass frieze on the South facade facing the sea
        g.current = kMatPierCap;
        box((kLeft + kRight) * 0.5f, kFront - 0.25f, (kRight - kLeft) * 0.5f + 0.45f, 0.32f, roof - 0.58f, 0.68f, 0xfff8f4eb, 0xfff8f4eb);
        g.current = kMatTrim;
        box((kLeft + kRight) * 0.5f, kFront - 0.38f, (kRight - kLeft) * 0.5f + 0.34f, 0.10f, 5.20f, 0.38f, kBrass, kBrass);
        // Warm recessed theater wall sconces & ceiling cove light strips (dimmed smoothly when film plays)
        g.current = kMatCinemaGlow;
        const DWORD kWarmStrip = 0xff181004;
        box(kRight - 0.36f, (kFront + kBack) * 0.5f, 0.08f, (kBack - kFront) * 0.42f, roof - 0.38f, 0.16f, kWarmStrip, kWarmStrip);
        box(kLeft + wall + 0.38f, (kFront + kBack) * 0.5f, 0.08f, (kBack - kFront) * 0.42f, roof - 0.38f, 0.16f, kWarmStrip, kWarmStrip);
        // Three wall sconces on the east lining: a small amber lamp on a brass backplate (where a tall white slab
        // used to glare), each over a soft wash on the acoustic wall. The wash is four wall panels meeting at the
        // lamp, so the baked sconce light (BakeGrandVertexColour) falls off smoothly from their shared centre.
        for (unsigned s = 0; s < kSconceCount; ++s) {
            const float sy = SconceY(s);
            g.current = kMatCinemaWall;
            for (int qy = -1; qy <= 1; qy += 2)
                for (int qz = 0; qz < 2; ++qz)
                    box(kRight - 0.335f, sy + float(qy) * kSconceWashHalf * 0.5f, 0.012f, kSconceWashHalf * 0.5f,
                        kSconceHeight - kSconceWashHalf + float(qz) * kSconceWashHalf, kSconceWashHalf, kWall, kWallCap);
            g.current = kMatTrim;
            box(kRight - 0.37f, sy, 0.03f, 0.17f, kSconceHeight - 0.34f, 0.68f, kBrass, kBrass);
            g.current = kMatCinemaSconce;  // deeper amber diffuse + emissive, dimmed with cinema house lights during playback
            box(kRight - 0.43f, sy, 0.05f, 0.10f, kSconceHeight - 0.20f, 0.40f, 0xff883808, 0xff883808);
        }
        // Warm strip lights along both edges of the centre aisle, from the rear rows to the front of the house.
        g.current = kMatCinemaGlow;
        for (int side = -1; side <= 1; side += 2)
            box(kAisleX + float(side) * kAisleHalf, (kAisleFront + kAisleBack) * 0.5f, 0.035f,
                (kAisleBack - kAisleFront) * 0.5f, 0.036f, 0.022f, kWarmStrip, kWarmStrip);
    } else {
        g.current = kMatCinemaWall;
        box(kRight,(kFront+kBack)*.5f,.33f,(kBack-kFront)*.5f,0,roof ? roof : 6.8f,kWall,kWallCap);
        box((kLeft+kRight)*.5f,kFront,(kRight-kLeft)*.5f,.32f,0,roof ? roof : 7.1f,kWall,kWallCap);
    }
    g.current = kMatStone;
    box((kLeft+kRight)*.5f,kBack,(kRight-kLeft)*.5f,.32f,0,roof ? roof : 8.9f,kProscenium,kBronze);
    if (roof) {
        const float wall = CeMuseumExhibition::kWallHalf;
        g.current = kMatCinemaWall;
        if (!gGrandMode && roof > height) box(kLeft,(kFront+kBack)*.5f,wall,(kBack-kFront)*.5f+.32f,height,roof-height,kWall,kWallCap);
        g.current = kMatAuto;
        Ceiling(floor,kLeft+wall+ox,kFront-.32f+oy,kRight+.33f+ox,kBack+.32f+oy,roof,kMatCinemaCeiling);
    }
    // Screen frame in refined dark stone + warm brass accent fillet
    g.current = kMatScreenFrame;
    box(kScreenX,kScreenY+.20f,kScreenHalfW+.48f,.17f,-(kScreenZ+kScreenHalfH)-.36f,2*kScreenHalfH+.72f,kBronze,kStone);
    // A stopped screen is a drawn house curtain: deep wine folds, dim enough to sit behind the room, lighter at
    // the top where the proscenium light catches them. (The film's own quad is drawn in front of it.)
    g.current = kMatScreenIdle;
    box(kScreenX,kScreenY+.10f,kScreenHalfW,.10f,-(kScreenZ+kScreenHalfH),2*kScreenHalfH,0xff0e1016,0xff0e1016);
    {
        const CeHallPoint centre=Offset(o,kScreenX,kScreenY-.006f);
        float ground=0;
        if (g.ground(centre.x,centre.y,&ground)) {
            const float bottom=ground+(kScreenZ+kScreenHalfH),top=bottom-2*kScreenHalfH;
            const float fold = 2.0f * kScreenHalfW / float(kCurtainFolds);
            for (unsigned i = 0; i < kCurtainFolds; ++i) {
                const CeHallPoint a=Offset(centre,-kScreenHalfW+fold*float(i),0);
                const CeHallPoint m=Offset(centre,-kScreenHalfW+fold*(float(i)+0.5f),0);
                const CeHallPoint b=Offset(centre,-kScreenHalfW+fold*float(i+1),0);
                // Each fold: shadowed in its valleys (a, b), lit along its crest (m), wound so FaceNormal points (0, -1, 0) into the room.
                Quad(Point(a,bottom,kCurtainValleyLow),Point(a,top,kCurtainValleyHigh),
                     Point(m,top,kCurtainCrestHigh),Point(m,bottom,kCurtainCrestLow));
                Quad(Point(m,bottom,kCurtainCrestLow),Point(m,top,kCurtainCrestHigh),
                     Point(b,top,kCurtainValleyHigh),Point(b,bottom,kCurtainValleyLow));
            }
        }
    }
    g.current = kMatAuto;
    for(unsigned row=0;row<3;++row) {
        const float y=1.0f-row*1.6f,h=.16f+row*.30f;
        for(unsigned bank=0;bank<2;++bank) {
            g.current = kMatCarpet;
            box(SeatX(bank*3+1),y,2.35f,.73f,0,h,kCarpet,kCarpet);
            g.current = kMatSeat;
            for(unsigned n=0;n<3;++n) {
                const float x=SeatX(bank*3+n);
                box(x,y,.50f,.42f,h,.35f,kUpholstery,kSeatCap);
                box(x,y-.41f,.50f,.10f,h+.29f,.81f,kUpholstery,kSeatCap);
            }
        }
    }
    // Console pedestal centered beneath the West-facing casing (x = kConsoleX + 0.13) with cap flush at z = 1.29 m.
    const float pedX = kConsoleX + 0.13f;
    g.current = kMatCinemaBase;
    box(pedX,kConsoleY,.24f,.56f,0,.035f,kSlate,kSlate);
    box(pedX,kConsoleY,.21f,.52f,.035f,.22f,kSlate,kSlate);
    g.current = kMatCinemaPost;
    box(pedX,kConsoleY,.14f,.42f,.255f,.855f,kSlate,kBronze);
    g.current = kMatCinemaCap;
    box(pedX,kConsoleY,.16f,.48f,1.11f,.18f,kBronze,kLight);
    g.current = kMatAuto;
    for(unsigned i=0;i<3;++i)
        box(kWallX+5.9f,3.0f+2.2f*i,.16f,.36f,.02f,.10f,kLight,kLight);
}

bool CinemaProjectionTarget() {
    using namespace Cinema;
    if (!s.film) return false;
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (!dev || FAILED(dev->CreateTexture(s.film->Width,s.film->Height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&s.texture,0)) || !s.texture) return false;
    s.textureDevice=dev;
    s.textureDeviceGen=CurrentMuseumDeviceGen();
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
    BinkSetVolume(s.film, 0, 0);	// silent until CeMuseumFilm::Start
    // A film that cannot be shown is closed to Idle here; CinemaOpen decides what the
    // console says about it (only "no film at all can be shown" locks it).
    if (!s.film->Frames || !s.film->Width || !s.film->Height || s.film->Width>2048 || s.film->Height>2048 || !CinemaProjectionTarget()) { CinemaClose(Idle); return false; }
    const bool track = PortBinkHasSound ? PortBinkHasSound(s.film) != 0 : true;
    CeMuseumFilm::Start(&film, CinemaFilmCalls(), CAREER.GetSoundVolume(), track, CinemaClock(), &duck);
    s.phase = Playing;
    return true;
}
void CinemaOpen(unsigned selected) {
    using namespace Cinema;
    CinemaCatalogueLoad();
    const bool scanning = s.firstPlayable;
    const int opened = CeMuseumCinemaCatalogue::Play(s.catalogue, static_cast<int>(selected), s.firstPlayable, CinemaStartFilm, 0);
    if (opened < 0) {
        // Missing ("films cannot be shown") locks the console until the visitor
        // leaves the room, so it is kept for the one case it describes: the first
        // scan of the whole list found nothing playable. One chosen film that
        // will not open returns the console to Idle, ready for another choice.
        CinemaClose(CeMuseumCinemaAfterOpenFailure(scanning) ? Missing : Idle);
        s.playing = -1;
        s.firstPlayable = false;
        PortSessionLog(PORT_SESSION_GAME, false, "Museum cinema: %s",
                       scanning ? "no film in the list could be shown" : "the chosen film could not be shown");
        return;
    }
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
    const CeHallPoint origin = {CeMuseumWorld::kEntranceX + CinemaOffX(), CeMuseumWorld::kEntranceY + CinemaOffY()};
    const CeHallPoint centre = Offset(origin, CeCinemaRoom::kConsoleX, CeCinemaRoom::kConsoleY);
    float floor = 0;
    if (!g.ground(centre.x, centre.y, &floor)) return CinemaConsole::Mount{};
    const CinemaConsole::Vec3 at = {centre.x, centre.y, floor - 1.65f};
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
    in.paused = view.paused || view.photo;   // the same hold CinemaHeld() gives the clocks
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
bool CinemaPadConnected(int pad, CeTutorialPadInfo *info) {
    if (pad < 0 || pad >= LT.mJoypads || pad >= MAX_JOYPADS || !LT.mUnknown33474[pad]) return false;
    CeTutorialPadInfo local = {};
    if (!PortCeTutorialPadInfo(LT.pJoystick[pad], &local)) return false;
    if (info) *info = local;
    return true;
}
void CinemaTrackDevice() {
    Cinema::State &s = Cinema::s;
    bool anyPad = false, padInput = false;
    for (int pad = 0; pad < LT.mJoypads && pad < MAX_JOYPADS; ++pad) {
        if (!CinemaPadConnected(pad, 0)) continue;
        anyPad = true;
        const DIJOYSTATE2 *j = LT.mJoyState[pad];
        if (!j) continue;
        for (int b = 0; b < 32; ++b)
            if (j->rgbButtons[b] & 0x80) { padInput = true; break; }
        if ((j->rgdwPOV[0] & 0xFFFFu) != 0xFFFFu) padInput = true;
        if (std::abs(j->lX) > 250 || std::abs(j->lY) > 250 || std::abs(j->lZ) > 250 || std::abs(j->lRz) > 250)
            padInput = true;
    }
    bool kbmInput = LT.mMouseButtons != 0 || PortMouseHeldNow(0) == 1 || PortMouseHeldNow(1) == 1 || PortMouseHeldNow(2) == 1;
    if (!kbmInput) {
        for (int k = 0; k < 256; ++k)
            if (LT.KeyDown[k] || LT.KeyWasDown[k]) { kbmInput = true; break; }
    }
    if (s.mouseSeen && (LT.mMouseX != s.lastMouseX || LT.mMouseY != s.lastMouseY))
        kbmInput = true;
    s.lastMouseX = LT.mMouseX; s.lastMouseY = LT.mMouseY; s.mouseSeen = true;
    const unsigned mouseSeq = PortMouseActivitySeq();
    if (s.mouseSeqSeen && mouseSeq != s.lastMouseSeq)
        kbmInput = true;
    s.lastMouseSeq = mouseSeq; s.mouseSeqSeen = true;
    s.padActive = CeCinemaRoom::UpdatePadActive(s.padActive, anyPad, padInput, kbmInput);
}
void CinemaVisitor(float x,float y) {
    using namespace Cinema;using namespace CeCinemaRoom;
    const float side = x - CeMuseumWorld::kEntranceX - CinemaOffX();
    const float depth = y - CeMuseumWorld::kEntranceY - CinemaOffY();
    const bool room=PortCeMuseumAuthoredMap() && Interior(side,depth);
    if(s.inRoom && !room) CinemaClose(Idle);
    s.inRoom=room;s.atConsole = false;
    s.inDoorway=PortCeMuseumAuthoredMap() && Range(side,kDoorX-3.0f,kDoorX+.7f) && Range(depth,kDoorY-3.5f,kDoorY+3.5f);
    CinemaTrackDevice();
    CinemaStep();
    CinemaFilmFade();
}
bool CinemaAction(bool release, float value) {
    Cinema::s.fireHeld = !release && value > 0.0f;
    return CinemaStep();
}
bool CinemaFormatKbmInput(const SControlInput &input, char *out, unsigned cap) {
    if (input.mType == MOUSE_BUTTON_ON || input.mType == MOUSE_BUTTON_ONCE || input.mType == MOUSE_BUTTON_RELEASE) {
        const char *m[] = {"Left mouse","Right mouse","Middle mouse","Wheel up","Wheel down"};
        if (input.mKey >= 0 && input.mKey < 5) { std::snprintf(out, cap, "%s", m[input.mKey]); return true; }
    }
    if (input.mType == KEY_ON || input.mType == KEY_ONCE || input.mType == KEY_RELEASE) {
        const int special[] = {DIK_TAB,DIK_RETURN,DIK_SPACE,DIK_LEFT,DIK_RIGHT,DIK_UP,DIK_DOWN,DIK_LSHIFT,DIK_RSHIFT,DIK_LCONTROL,DIK_RCONTROL,DIK_LMENU,DIK_RMENU};
        const char *names[] = {"Tab","Enter","Space","Left arrow","Right arrow","Up arrow","Down arrow","Left Shift","Right Shift","Left Ctrl","Right Ctrl","Left Alt","Right Alt"};
        for (unsigned k = 0; k < sizeof(special)/sizeof(special[0]); ++k)
            if (input.mKey == special[k]) { std::snprintf(out, cap, "%s", names[k]); return true; }
        if (PortCeTutorialPrintableKey(input.mKey, out, cap)) return true;
        std::snprintf(out, cap, "Key %d", int(input.mKey)); return true;
    }
    return false;
}
bool CinemaFormatPadInput(const SControlInput &input, char *out, unsigned cap) {
    if (input.mType != BUTTON_ON && input.mType != BUTTON_ONCE && input.mType != BUTTON_RELEASE && input.mType != BUTTON_REPEAT)
        return false;
    CeTutorialPadInfo info = {};
    if (!CinemaPadConnected(input.mPad, &info)) return false;
    if (info.kind == CE_TUTORIAL_GAMEPAD) {
        if (input.mKey == 17) { std::snprintf(out, cap, "Right trigger"); return true; }
        if (input.mKey == 16) { std::snprintf(out, cap, "Left trigger"); return true; }
    } else if (info.kind == CE_TUTORIAL_STICK && input.mKey == 3) {
        std::snprintf(out, cap, "Stick trigger"); return true;
    }
    std::snprintf(out, cap, "Pad button %d", int(input.mKey) + 1);
    return true;
}
void CinemaFireName(char *out,unsigned cap) {
    if(!out||!cap)return;
    SControlBinding *binding=FindControlBinding(BUTTON_MECH_FIRE_GUN_POD);
    CeCinemaRoom::InputDevice slots[2] = {CeCinemaRoom::InputDevice::None, CeCinemaRoom::InputDevice::None};
    if (binding) {
        for (unsigned i = 0; i < 2; ++i) {
            const SControlInput &input = binding->mInput[i];
            if (input.mPad < 0) continue;
            if (input.mType == MOUSE_BUTTON_ON || input.mType == MOUSE_BUTTON_ONCE || input.mType == MOUSE_BUTTON_RELEASE ||
                input.mType == KEY_ON || input.mType == KEY_ONCE || input.mType == KEY_RELEASE)
                slots[i] = CeCinemaRoom::InputDevice::Kbm;
            else if ((input.mType == BUTTON_ON || input.mType == BUTTON_ONCE || input.mType == BUTTON_RELEASE || input.mType == BUTTON_REPEAT) &&
                     CinemaPadConnected(input.mPad, 0))
                slots[i] = CeCinemaRoom::InputDevice::Pad;
        }
    }
    if (Cinema::s.padActive) {
        const int picked = CeCinemaRoom::PickFireSlot(slots, true);
        if (picked >= 0 && slots[picked] == CeCinemaRoom::InputDevice::Pad &&
            CinemaFormatPadInput(binding->mInput[picked], out, cap)) return;
        for (int pad = 0; pad < LT.mJoypads && pad < MAX_JOYPADS; ++pad) {
            CeTutorialPadInfo info = {};
            if (!CinemaPadConnected(pad, &info)) continue;
            SControlInput extra[2];
            const int count = ::PortCeGamepadInputs(BUTTON_MECH_FIRE_GUN_POD, pad, extra);
            for (int e = 0; e < count; ++e)
                if (CinemaFormatPadInput(extra[e], out, cap)) return;
        }
        if (picked >= 0 && CinemaFormatKbmInput(binding->mInput[picked], out, cap)) return;
    } else {
        const int picked = CeCinemaRoom::PickFireSlot(slots, false);
        if (picked >= 0 && CinemaFormatKbmInput(binding->mInput[picked], out, cap)) return;
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
        std::snprintf(line, lineCap, "Through this doorway: use the projector to watch game films.");
        return true;
    }
    return false;
}
void CinemaScreen(IDirect3DDevice9 *dev) {
    using namespace Cinema;using namespace CeCinemaRoom;
    if(!dev || !s.inRoom || s.phase!=Playing || !s.film)return;
    if(!s.texture || s.textureDevice != dev || s.textureDeviceGen != CurrentMuseumDeviceGen()) {
        if(s.texture) { s.texture->Release(); s.texture = 0; }
        s.textureDevice = 0; s.textureDeviceGen = 0;
        if(!CinemaProjectionTarget()) { CinemaClose(Idle); return; }
        s.frame = 0;
        D3DLOCKED_RECT image = {};
        if(SUCCEEDED(s.texture->LockRect(0, &image, 0, 0)) && image.pBits) {
            BinkCopyToBuffer(s.film, image.pBits, image.Pitch, s.bitmapH, 0, 0,
                             BINKSURFACE32A | BINKCOPYNOSCALING | BINKCOPYALL);
            s.texture->UnlockRect(0);
            s.frame = film.shown ? film.shown : 1u;
        }
    }
    if(!s.texture)return;
    const CeMuseumFilm::Calls calls=CinemaFilmCalls();
    const CeMuseumFilm::Step step=CeMuseumFilm::Tick(&film,calls,CinemaHeld(),CinemaClock());
    if(step==CeMuseumFilm::kEnded) { CinemaClose(Finished);return; }
    if(step==CeMuseumFilm::kShow) {
        D3DLOCKED_RECT image={};
        // A frame that cannot be written ends this showing; the console stays usable.
        if(FAILED(s.texture->LockRect(0,&image,0,0))||!image.pBits) {CinemaClose(Idle);return;}
        BinkCopyToBuffer(s.film,image.pBits,image.Pitch,s.bitmapH,0,0,BINKSURFACE32A|BINKCOPYNOSCALING|BINKCOPYALL);
        s.texture->UnlockRect(0);
        CeMuseumFilm::Shown(&film,calls);s.frame=film.shown;
    }
    if(!s.frame)return;
    const float aspect=float(s.bitmapW)/s.bitmapH,box=kScreenHalfW/kScreenHalfH;
    const float wh=aspect>box?kScreenHalfW:kScreenHalfH*aspect,hh=aspect>box?kScreenHalfW/aspect:kScreenHalfH;
    const float x=CeMuseumWorld::kEntranceX+CinemaOffX()+kScreenX;
    const float y=CeMuseumWorld::kEntranceY+CinemaOffY()+kScreenY-.025f;
    float ground = gGrandFloorZ;
    if (gPtr && gPtr->ground) gPtr->ground(x, y, &ground);
    const float z = ground + kScreenZ;
    struct FilmVertex {float x,y,z,u,v;};
    const FilmVertex screen[]={{x-wh,y,z-hh,0,0},{x+wh,y,z-hh,1,0},{x-wh,y,z+hh,0,1},{x+wh,y,z+hh,1,1}};
    dev->SetTexture(0,s.texture);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,screen,sizeof(FilmVertex));
}
