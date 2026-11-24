// SPDX-License-Identifier: GPL-3.0-only
// Production editor and placement consumer on authored engine/storage boundaries.
#include <string>
#include "editor-playtest-engine.private.inc"

enum {
    DIK_ESCAPE=1, DIK_RETURN, DIK_X, DIK_S, DIK_UP, DIK_DOWN, DIK_LCONTROL, DIK_RCONTROL,
    DIK_O, DIK_Z, DIK_Y, DIK_TAB, DIK_LSHIFT, DIK_RSHIFT, DIK_C, DIK_HOME, DIK_PRIOR,
    DIK_NEXT, DIK_F5, DIK_F6, DIK_W, DIK_F1, DIK_LEFT, DIK_RIGHT, DIK_T, DIK_R, DIK_H, DIK_P, DIK_DELETE, KEY_EVENT_UP=100, KEY_EVENT_DOWN
};
static std::string savedRevision;
static unsigned saves, reads;
static bool saveFailure, readFailure, corruptRead;
static int listedFiles;   // more than one: the folder lists that many of the editor's own saves, oldest first, as storage does
int PortCeEditorFiles(CeEditorFile* files,int capacity,char*,size_t) noexcept {
    if(listedFiles>1){
        const int n=listedFiles<capacity?listedFiles:capacity;
        // A friend's file and another level's save sort before the level's own in the folder; the editor puts its own first.
        std::snprintf(files[0].name,sizeof(files[0].name),"a-friends-map.ceproject");files[0].size=10;
        std::snprintf(files[1].name,sizeof(files[1].name),"level-0100-000009.ceproject");files[1].size=10;
        for(int i=2;i<n;++i){std::snprintf(files[i].name,sizeof(files[i].name),"level-0777-%06d.ceproject",i-1);files[i].size=10;}
        return n;
    }
    if(savedRevision.empty()||capacity<1)return 0;
    std::snprintf(files[0].name,sizeof(files[0].name),"authored-revision.ceproject");
    files[0].size=unsigned(savedRevision.size());return 1;
}
bool PortCeEditorSave(int,const char* bytes,size_t size,char* filename,size_t filenameCapacity,
                      char* error,size_t errorCapacity) noexcept {
    ++saves;
    if(saveFailure){std::snprintf(error,errorCapacity,"Authored save refusal.");return false;}
    savedRevision.assign(bytes,size);
    std::snprintf(filename,filenameCapacity,"authored-revision.ceproject");return true;
}
char* PortCeEditorRead(const char*,size_t* size,char* error,size_t errorCapacity) noexcept {
    ++reads;
    if(readFailure){std::snprintf(error,errorCapacity,"Authored read refusal.");return nullptr;}
    const std::string bytes=corruptRead?"{invalid":savedRevision;
    char* result=static_cast<char*>(std::malloc(bytes.size()+1));
    if(!result)return nullptr;
    std::memcpy(result,bytes.c_str(),bytes.size()+1);*size=bytes.size();return result;
}
void PortCeEditorFileFree(char* bytes) noexcept { std::free(bytes); }
static int EditorPick(float,float){return -1;}
#include "editor-lifecycle.private.inc"
#include "editor-playtest-common.private.inc"

class Pilot : public IController {
    void ReceiveButtonAction(CController*,int,float) override {}
    BOOL CanBeControlledWhenInPause() override {return FALSE;}
    EControlType GetControlType() override {return CONTROL_GAME_MENU;}
} pilot;
static void ResetFixture() {
    Reset();savedRevision.clear();saves=reads=0;saveFailure=readFailure=corruptRead=false;listedFiles=0;
}
static bool BeginEditor() { return PortCeEditorBegin(777); }

static void StartEditor() {
    Check(BeginEditor()&&Load(),"ordinary editor entry loads an authored map");
    PortCeEditorFrame();
    Check(gEditorInputOwned&&gEditorReady&&!PortCeEditorPlaying(),"authoring owns parked input");
}
static bool fixturePrimaryStart = false;
static bool Reload(bool missing=false,bool duplicate=false) {
    // Each engine restart owns fresh actor initialization, camera and pilot control.
    GAME.SetCurrentCamera(0,new CGenericCamera(),true);
    GAME.controller.target=&pilot;GAME.mPause=FALSE;GAME.mGameState=GAME_STATE_STARTING;GAME.quit=0;
    WORLD.things={&objectA,&objectB};
    if(!PortCeProjectBeginWorld(777,true)||!PortCeProjectEndWorld(true)||
       !PortCeProjectBeginWorld(777,false))return false;
    float x=1,y=2,z=3;
    if(!missing) {
        int side=0;
        if(!PortCeProjectApplyPlacement("AuthoredAnchor",15,false,&x,&y,&z,&side))return false;
        objectA.allegiance=side;
        // The authored Init stand-in adds a fixed offset, after the production placement hook.
        objectA.position=FVector(x+3,y+3,z+3);objectA.renderPosition=objectA.position;
        if(duplicate&&!PortCeProjectApplyPlacement("AuthoredAnchor",15,false,&x,&y,&z,&side))return false;
    }
    x=7;y=8;z=9;
    if(!PortCeProjectApplyPlacement("OtherAnchor",12,false,&x,&y,&z))return false;
    objectB.position=FVector(x,y,z);objectB.renderPosition=objectB.position;
    if (!PortCeProjectEndWorld(false)) return false;
    if (fixturePrimaryStart) {
        float x=4, y=7, z=-1;
        PortCeProjectApplyPlayerStart(1, false, x, y, z);
        GAME.pilot.engine.pos=GAME.pilot.engine.old=FVector(x,y,z);
    }
    return PortCeEditorAfterLoad();
}
static void Edit() {
    Check(gEditorState.Select(0)&&gEditorState.BeginMove()&&gEditorState.Move({2,4,6})&&
          gEditorState.FinishMove(true)&&ApplyOne(0),"authoring makes one persistent placement edit");
}
static void StartPlay() {
    Check(PortCeEditorKey(DIK_F5,false),"the ordinary F5 editor key is consumed");
    Check(gEditorPhase==LoadPlaytest&&GAME.quit==QT_RESTART_LEVEL&&PortCeEditorActive(),"F5 requests a draft preview");
    Check(saves==0&&reads==0&&savedRevision.empty(),"preview does not create or open a project file");
    Check(!PortCeProjectCareerAllowed()&&!PortCeEditorWorldViewToken(),"reload keeps careers fenced and invalidates the authoring view");
    PortCeEditorLevelEnd();
    Check(!gEditorReady&&!gEditorInputOwned&&!LT.trap&&!GAME.controller.target,"authoring camera and input are released before play");
    Check(Reload()&&PortCeEditorPlaying(),"saved native-editor project loads as gameplay");
 Check(!PortCeProjectCareerAllowed(), "career writes remain fenced during saved-revision gameplay");
    Check(!gEditorReady&&!GAME.IsPaused()&&GAME.controller.target==&pilot&&!LT.trap,"play retains normal unpaused pilot input and camera");
    Check(Near(objectA.position,FVector(5,7,9)),"authored placement reaches Init before gameplay");
    const unsigned before=warmups;Warmup();
    Check(warmups==before+1,"play runs the ordinary mission warmup");
    Check(!PortCeEditorKey(DIK_W,false)&&!PortCeEditorKey(DIK_ESCAPE,false),"ordinary flight and pause keys reach the game");
    PortCeEditorFrame();
    Check(!GAME.IsPaused()&&GAME.controller.target==&pilot&&!LT.trap,"per-frame editor hook never parks play");
}
static void ReturnAndCheck(const CCareer& career) {
    Check(CareerEquals(career), "return load discards play-test progress before reconstruction");
    PortCeEditorLevelEnd();
    Check(PortCeEditorActive()&&!PortCeProjectCareerAllowed(),"return load remains career independent");
    Check(Reload()&&gEditorReady&&!PortCeEditorPlaying()&&GAME.IsPaused(),"return reloads the paused authoring scene");
    const CeEditorObject* selected=gEditorState.Object(gEditorState.Selected());
    Check(selected&&!std::strcmp(selected->source,"AuthoredAnchor")&&selected->position.x==2&&
          selected->position.y==4&&selected->position.z==6&&gEditorState.Dirty(),"saved coordinates and selection return without gameplay changes");
    Check(Near(objectA.position,FVector(5,7,9)),"return displays the saved placement on fresh actors");
    Check(CareerEquals(career),"return discards play-test career memory");
    PortCeEditorFrame();
    Check(gEditorInputOwned&&LT.trap==EditorTrap&&GAME.controller.target==&gEditorInput,"returned view accepts normal editor input");
}
static void PlaybackControls() {
    char error[192];
    CeProjectDocument* document=PortCeProjectDocumentNew(777,"Authored native project",error,sizeof(error));
    const CeProjectPosition position={"AuthoredAnchor",42.25f,11.5f,-4};
    Check(document&&PortCeProjectDocumentUpdate(document,&position,1,error,sizeof(error)),"native document creates a complete saved link");
    const size_t size=PortCeProjectDocumentWrite(document,nullptr,0,error,sizeof(error));
    std::vector<char> bytes(size);
    Check(size&&PortCeProjectDocumentWrite(document,bytes.data(),size,error,sizeof(error))==size,"native document serializes through the runtime format");
    CeProjectDocument* read=PortCeProjectDocumentRead(bytes.data(),size-1,error,sizeof(error));
    PortCeProjectDocumentDelete(document);document=read;
    Check(document!=nullptr,"runtime reads the native-authored revision");
    auto fresh=[&](){return PortCeProjectPlaybackNew(document,error,sizeof(error));};
    CeProjectPlayback* p=fresh();float x=0,y=0,z=0;
    Check(p&&PortCeProjectPlaybackBegin(p,1,true,error,sizeof(error))&&
          PortCeProjectPlaybackApply(p,"AuthoredAnchor",15,true,&x,&y,&z,error,sizeof(error))&&
          PortCeProjectPlaybackEnd(p,true,error,sizeof(error))&&x==0,"base world cannot apply or satisfy a linked placement");
    Check(PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error))&&
          PortCeProjectPlaybackApply(p,"Unbound",8,false,&x,&y,&z,error,sizeof(error))&&x==0,"unbound objects retain installed coordinates");
    Check(PortCeProjectPlaybackApply(p,"AuthoredAnchor",15,false,&x,&y,&z,error,sizeof(error))&&x==42.25f&&y==11.5f&&z==-4&&
          PortCeProjectPlaybackEnd(p,false,error,sizeof(error)),"one complete linked load applies exact saved coordinates");
    Check(!PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error)),"completed playback rejects a second map load");
    PortCeProjectPlaybackDelete(p);p=fresh();
    Check(!PortCeProjectPlaybackBegin(p,778,false,error,sizeof(error))&&
          !PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error)),"wrong-map refusal remains sticky");
    PortCeProjectPlaybackDelete(p);p=fresh();
    Check(PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error))&&
          !PortCeProjectPlaybackEnd(p,false,error,sizeof(error)),"missing saved bindings refuse the whole load");
    PortCeProjectPlaybackDelete(p);p=fresh();
    Check(PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error))&&
          PortCeProjectPlaybackApply(p,"AuthoredAnchor",15,false,&x,&y,&z,error,sizeof(error))&&
          !PortCeProjectPlaybackApply(p,"AuthoredAnchor",15,false,&x,&y,&z,error,sizeof(error))&&
          !PortCeProjectPlaybackEnd(p,false,error,sizeof(error)),"duplicate names cannot produce partial success");
    PortCeProjectPlaybackDelete(p);p=fresh();
    const char unterminated[3]={'A','B','C'};
    Check(PortCeProjectPlaybackBegin(p,777,false,error,sizeof(error))&&
          !PortCeProjectPlaybackApply(p,unterminated,sizeof(unterminated),false,&x,&y,&z,error,sizeof(error))&&
          !PortCeProjectPlaybackEnd(p,false,error,sizeof(error)),"bounded object names cannot overrun their source");
    PortCeProjectPlaybackDelete(p);PortCeProjectDocumentDelete(document);
}
int main() {
    PlaybackControls();
    ResetFixture();const CCareer initial=CAREER;StartEditor();Edit();
    gEditorFocus={31,41,51};gEditorYaw=.3f;gEditorPitch=.8f;gEditorDistance=73;
    StartPlay();
    const std::string firstRevision=savedRevision;
    savedRevision="external changes do not replace a running revision";
    objectA.position=FVector(100,200,300);CAREER.progress[3]=999;
    Check(PortCeEditorKey(DIK_F5,false)&&GAME.quit==QT_RESTART_LEVEL&&gEditorPhase==ReturnToAuthoring,"F5 uses the ordinary game key path to return");
    ReturnAndCheck(initial);
    Check(gEditorFocus.x==31&&gEditorFocus.y==41&&gEditorFocus.z==51&&gEditorYaw==.3f&&
          gEditorPitch==.8f&&gEditorDistance==73,"authoring camera framing survives the test");
    {   // A named save remains explicit, and a clean preview also avoids storage.
        savedRevision=firstRevision;
        Check(EditorSave()&&saves==1&&!savedRevision.empty()&&!gEditorState.Dirty(), "explicit Save commits the returned draft");
        const std::string named=savedRevision;
        Check(EditorSave()&&saves==1, "saving an unchanged named project adds no file");
        Check(PortCeEditorKey(DIK_F5,false)&&gEditorPhase==LoadPlaytest&&saves==1&&reads==0&&named==savedRevision,
              "even a named clean preview leaves the file untouched");
    }
    Check(PortCeEditorResolveQuit(QT_QUIT_TO_SYSTEM)==QT_QUIT_TO_SYSTEM,"system exit is never converted into a reload");
    PortCeEditorLevelEnd();Check(PortCeEditorTakeReturn()&&CareerEquals(initial),"final editor exit restores the entry career");

    for(int quit:{QT_USER_QUIT_TO_FRONTEND,QT_QUIT_TO_FRONTEND,QT_USER_QUIT_TO_TITLE_SCREEN,QT_QUIT_TIMEOUT}) {
        ResetFixture();StartEditor();Edit();StartPlay();objectA.position=FVector(100,200,300);CAREER.progress[4]=999;
        PortCeEditorLevelEnd(); // The production result hook runs after level teardown.
        Check(PortCeEditorResolveQuit(quit)==QT_RESTART_LEVEL&&gEditorPhase==ReturnToAuthoring,"ordinary leave or mission ending returns to the editor");
        ReturnAndCheck(initial);PortCeEditorTakeReturn();
    }
    {   // Closing the window during a play test (the game's own close: to the front end, which then ends the program)
        // closes the game; it is never turned into a return to the editor, and the career stays fenced.
        ResetFixture();StartEditor();Edit();StartPlay();
        GAME.SetQuit(QT_QUIT_TO_FRONTEND);FRONTEND.SetQuit(-1);PortCeEditorLevelEnd();
        Check(PortCeEditorResolveQuit(QT_QUIT_TO_FRONTEND)==QT_QUIT_TO_FRONTEND&&gEditorPhase==Playing&&!PortCeProjectCareerAllowed(),
              "a window closed during a play test ends the game, with the career still fenced");
        FRONTEND.mQuit=0;PortCeEditorTakeReturn();
    }
    ResetFixture();StartEditor();Edit();StartPlay();
    PortCeEditorLevelEnd();
    Check(PortCeEditorResolveQuit(QT_RESTART_LEVEL)==QT_RESTART_LEVEL&&gEditorPhase==LoadPlaytest,"Retry requests the same immutable saved test");
    Check(Reload()&&PortCeEditorPlaying()&&Near(objectA.position,FVector(5,7,9)),"Retry applies the revision afresh");
    Check(PortCeEditorResolveQuit(QT_QUIT_TO_SYSTEM)==QT_QUIT_TO_SYSTEM&&PortCeEditorPlaying(),"closing a running test remains a system exit");
    PortCeEditorTakeReturn();

    for(int failure:{1,2,3}) {
        ResetFixture();StartEditor();Edit();saveFailure=failure==1;readFailure=failure==2;corruptRead=failure==3;
        Check(EditorStartPlaytest()&&gEditorPhase==LoadPlaytest&&saves==0&&reads==0, "preview works without readable or writable project storage");
        PortCeEditorLevelEnd();
        Check(Reload()&&PortCeEditorPlaying()&&!PortCeProjectCareerAllowed(),"unavailable file storage does not block preview or open a career path");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    ResetFixture();StartEditor();Edit();
    Check(gEditorState.BeginMove()&&!EditorStartPlaytest()&&saves==0,"unfinished placement moves cannot start play");
    gEditorState.FinishMove(false);network=true;
    Check(!EditorStartPlaytest()&&saves==0,"new network intent refuses the handoff before saving");network=false;PortCeEditorTakeReturn();

    for(bool duplicate:{false,true}) {
        ResetFixture();StartEditor();Edit();Check(EditorStartPlaytest(),"loader negative control starts from a real saved revision");PortCeEditorLevelEnd();
        Check(Reload(!duplicate,duplicate)&&!PortCeEditorPlaying()&&!PortCeProjectCareerAllowed(),
              "missing or duplicate installed objects never fail the engine's load and never begin gameplay");
        ExpectRefusal("The draft no longer matches this map, so it cannot be previewed. Your saves are kept.",initial,
                      "a play test whose project no longer matches the map returns to the page");
    }
    {   // A map load that arrives during a play test (nothing but a restart through the result hook may) is refused.
        ResetFixture();StartEditor();Edit();StartPlay();PortCeEditorLevelEnd();
        Check(Reload()&&!PortCeEditorPlaying()&&Near(objectA.position,FVector(4,5,6)),
              "a load during play never fails the engine's load and applies none of the project");
        ExpectRefusal("The play test stopped because another map started to load. Open the map again to go on.",initial,
                      "a load during play returns to the page");
    }
    {   // The return load finds the saved object gone: the editor closes the project and returns to the page.
        ResetFixture();StartEditor();Edit();StartPlay();
        Check(PortCeEditorKey(DIK_F5,false)&&gEditorPhase==ReturnToAuthoring,"return requested");PortCeEditorLevelEnd();
        Check(Reload(true),"a return load missing the saved object never fails the engine's load");
        ExpectRefusal("The draft no longer matches this map, so the editor closed it. Your saves are kept.",initial,
                      "a return whose project no longer matches the map returns to the page");
        Check(GAME.camera!=gEditorCamera,"the refused return gives the map its own camera back");
    }
    {   // Every action answers with one of the menu's cues, and the leave question opens on No (E2).
        ResetFixture();StartEditor();
        auto press=[&](unsigned key){const unsigned before=cues;PortCeEditorKey(key,false);EditorTrap(WORD(key),KEY_EVENT_UP);return cues>before;};
        Check(press(DIK_DOWN)&&gEditorToolFocus==1&&press(DIK_UP)&&gEditorToolFocus==0,"Up and Down move the tool focus, with a cue");
        Check(press(DIK_UP)&&gEditorToolFocus==9,"Up from Save wraps to Exit");
        press(DIK_DOWN);
        PortCeEditorKey(DIK_LCONTROL,false);
        Check(press(DIK_Z)&&!gEditorState.Dirty(),"an undo with nothing to undo is refused with a cue");
        EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);
        Check(press(DIK_TAB)&&gEditorState.Selected()>=0,"Tab selects the next object with a cue");
        Edit();
        Check(press(DIK_ESCAPE)&&gEditorDialog==DialogDiscardExit&&gEditorButtonFocus==0,"leaving with changes asks first, focus on No");
        Check(press(DIK_RIGHT)&&gEditorButtonFocus==1&&press(DIK_LEFT)&&gEditorButtonFocus==0,"Left and Right move between No and Yes");
        Check(press(DIK_RETURN)&&gEditorDialog==DialogNone&&GAME.quit==0&&gEditorState.Dirty(),"No keeps the editor and the changes");
        Check(press(DIK_ESCAPE)&&press(DIK_ESCAPE)&&gEditorDialog==DialogNone&&GAME.quit==0,"Esc answers the question No");
        Check(press(DIK_ESCAPE)&&press(DIK_RIGHT)&&press(DIK_RETURN)&&GAME.quit==QT_USER_QUIT_TO_FRONTEND,"Yes leaves without saving");
        Check(saves==0,"leaving without saving writes nothing");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
        ResetFixture();StartEditor();
        gEditorToolFocus=0;Check(press(DIK_RETURN)&&saves==1,"Enter uses the focused tool (Save), with a cue");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // The player's own leave from an unchanged scene: the page shows no message.
        ResetFixture();StartEditor();EditorNote("A passing notice inside the editor.");
        Check(PortCeEditorKey(DIK_ESCAPE,false)&&GAME.quit==QT_USER_QUIT_TO_FRONTEND&&!PortCeEditorMessage()[0],
              "leaving an unchanged scene leaves no message for the page");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // A notice goes with its cause, and a list, a question or the key help drops it; the Open list is newest first, the
        // wheel scrolls it, and a project is read before the player is asked to discard work for it.
        ResetFixture();StartEditor();
        auto press=[&](unsigned key){PortCeEditorKey(key,false);EditorTrap(WORD(key),KEY_EVENT_UP);};
        press(DIK_C);
        Check(!std::strcmp(gEditorNote,"Select an object first."),"C with nothing selected says so");
        PortCeEditorFrame();
        Check(gEditorNote[0]!=0,"the refusal stays while nothing is selected");
        press(DIK_TAB);PortCeEditorFrame();
        Check(gEditorState.Selected()>=0&&!gEditorNote[0],"selecting an object drops \"Select an object first.\"");
        Check(gEditorState.BeginMove(),"a move starts");
        press(DIK_F5);
        Check(!std::strcmp(gEditorNote,"Finish moving the object first.")&&gEditorPhase==Authoring,"F5 during a move says to finish it");
        PortCeEditorFrame();
        Check(gEditorNote[0]!=0,"that notice stays while the move goes on");
        gEditorState.FinishMove(false);PortCeEditorFrame();
        Check(!gEditorNote[0],"and goes when the move ends");
        EditorNote("Saved as Project 3. Earlier saves are kept.");PortCeEditorFrame();press(DIK_TAB);PortCeEditorFrame();
        Check(gEditorNote[0]!=0,"a passing notice is not dropped by a new selection");
        listedFiles=14;savedRevision="{}";
        PortCeEditorKey(DIK_LCONTROL,false);press(DIK_O);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);
        Check(gEditorDialog==DialogFiles&&gEditorFileCount==14,"Ctrl O lists the fourteen projects");
        PortCeEditorFrame();
        Check(!gEditorNote[0],"the open list drops the notice it would cover");
        Check(!std::strcmp(gEditorFiles[0].name,"level-0777-000012.ceproject")&&!std::strcmp(gEditorFiles[11].name,"level-0777-000001.ceproject")&&
              gEditorFileIndex==0,"the level's own projects come first, the newest first");
        Check(!std::strcmp(gEditorFiles[12].name,"a-friends-map.ceproject")&&!std::strcmp(gEditorFiles[13].name,"level-0100-000009.ceproject"),
              "every other project follows in the folder's order");
        EditorTrap(4,3);
        Check(gEditorFileTop==1&&gEditorFileIndex==1,"the wheel scrolls the list a row, and the focus stays on a row shown");
        for(int i=0;i<10;++i)EditorTrap(4,3);
        Check(gEditorFileTop==4&&gEditorFileIndex>=4&&gEditorFileIndex<14,"the list scrolls no further than its last row");
        for(int i=0;i<10;++i)EditorTrap(3,3);
        Check(gEditorFileTop==0,"and back to the top");
        press(DIK_ESCAPE);
        Check(gEditorDialog==DialogNone,"Esc closes the list");
        Edit();corruptRead=true;
        PortCeEditorKey(DIK_LCONTROL,false);press(DIK_O);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);press(DIK_RETURN);
        Check(gEditorDialog==DialogNone&&!std::strcmp(gEditorNote,"This project could not be read, so it was not opened.")&&
              gEditorState.Dirty(),"an unreadable project is refused before the player is asked to discard work");
        PortCeEditorKey(DIK_LCONTROL,false);press(DIK_O);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);
        Check(gEditorDialog==DialogFiles&&EditorFileGrey(0),"an unreadable project's row is grey in the list");
        press(DIK_ESCAPE);
        {   // Another level's readable project is refused the same way: before the question, with its own sentence.
            char error[192];
            CeProjectDocument* other=PortCeProjectDocumentNew(100,"Another level's project",error,sizeof(error));
            const CeProjectPosition position={"AuthoredAnchor",1.f,2.f,3.f};
            const size_t size=other&&PortCeProjectDocumentUpdate(other,&position,1,error,sizeof(error))?PortCeProjectDocumentWrite(other,nullptr,0,error,sizeof(error)):0;
            std::vector<char> bytes(size?size:1);
            Check(size&&PortCeProjectDocumentWrite(other,bytes.data(),size,error,sizeof(error))==size,"an authored project for level 100 is written");
            PortCeProjectDocumentDelete(other);
            corruptRead=false;savedRevision.assign(bytes.data(),size?size-1:0);
            const unsigned before=reads;
            PortCeEditorKey(DIK_LCONTROL,false);press(DIK_O);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);press(DIK_RETURN);
            Check(reads>before&&gEditorDialog==DialogNone&&!std::strcmp(gEditorNote,"This project is for another level. Open that level first.")&&
                  gEditorState.Dirty(),"another level's project is refused before the player is asked to discard work");
            PortCeEditorKey(DIK_LCONTROL,false);press(DIK_O);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);
            Check(gEditorDialog==DialogFiles&&EditorFileGrey(0),"another level's project's row is grey in the list");
            press(DIK_ESCAPE);
        }
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // A fresh editor shows a reachable object. Reopening a save after leaving shows the work we did.
        ResetFixture(); const CCareer career=CAREER;
        StartEditor();
        Check(gEditorState.Selected()==-1&&!gEditorState.Dirty()&&Near(Vector(gEditorFocus),objectA.position),
              "entry frames the nearest editable object without choosing or changing it");
        auto press=[](unsigned key){PortCeEditorKey(key,false);EditorTrap(WORD(key),KEY_EVENT_UP);};
        auto shortcut=[&](unsigned key){PortCeEditorKey(DIK_LCONTROL,false);press(key);EditorTrap(WORD(DIK_LCONTROL),KEY_EVENT_UP);};
        press(DIK_TAB);press(DIK_TAB);press(DIK_NEXT);shortcut(DIK_S);
        const FVector moved=objectB.position;
        Check(saves==1&&moved.Z>9&&!gEditorState.Dirty()&&!savedRevision.empty(),"moving and saving the second object writes one revision");
        const std::string revision=savedRevision;
        press(DIK_F5);PortCeEditorLevelEnd();
        Check(Reload()&&PortCeEditorPlaying()&&Near(objectB.position,moved)&&Near(objectA.position,FVector(4,5,6)),
              "the playable map uses the saved edit and leaves the nearer unchanged object alone");
        press(DIK_F5);PortCeEditorLevelEnd();Check(Reload(),"the editor returns from the play test");PortCeEditorFrame();
        Check(gEditorState.Selected()==1&&gEditorState.CanUndo()&&Near(objectB.position,moved),
              "the return keeps the edited selection and its undo history");
        shortcut(DIK_Z);Check(Near(objectB.position,FVector(7,8,9))&&gEditorState.CanRedo(),"undo after playing restores the original position");
        shortcut(DIK_Y);Check(Near(objectB.position,moved)&&!gEditorState.Dirty(),"redo restores the saved position");
        press(DIK_ESCAPE);PortCeEditorLevelEnd();Check(PortCeEditorTakeReturn()&&CareerEquals(career),"leaving restores the career");
        Reset();StartEditor();
        Check(Near(objectB.position,FVector(7,8,9))&&gEditorState.Selected()==-1,"the installed map still opens unchanged");
        shortcut(DIK_O);
        Check(gEditorDialog==DialogFiles&&!EditorFileGrey(0),"this level's save can be opened from the file list");
        press(DIK_RETURN);
        Check(gEditorDialog==DialogNone&&gEditorState.Selected()==1&&Near(Vector(gEditorFocus),moved),
              "reopening the project selects and frames the saved edit instead of the nearer unchanged object");
        Check(Near(objectB.position,moved)&&!gEditorState.Dirty()&&!gEditorState.CanUndo()&&
              !std::strcmp(gEditorFilename,"authored-revision.ceproject")&&savedRevision==revision&&saves==1,
              "reopening preserves the filename and bytes and leaves a clean project with fresh history");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // A valid but unframeable change must not pull the view off the map or select an invisible object.
        for(CeEditorPoint unreachable : {CeEditorPoint{100000,8,9}, CeEditorPoint{7,8,-100000}}) {
            ResetFixture();StartEditor();
            Check(gEditorState.Select(1)&&gEditorState.BeginMove()&&gEditorState.Move(unreachable)&&
                  gEditorState.FinishMove(true)&&EditorSave(),"the file format allows an object outside the view");
            PortCeEditorLevelEnd();PortCeEditorTakeReturn();Reset();StartEditor();
            EditorBrowse();EditorRequestOpen();
            Check(!gEditorState.Dirty()&&gEditorState.Selected()==-1&&Near(Vector(gEditorFocus),objectA.position)&&
                  Near(objectB.position,Vector(unreachable)),"opening keeps the far edit but frames the reachable object");
            PortCeEditorLevelEnd();PortCeEditorTakeReturn();
        }
    }
    {   // A project may move several objects. Choose the nearest reachable change, irrespective of edit order.
        ResetFixture();Check(BeginEditor(),"a map with several editable placements begins");
        CThing third{"ThirdAnchor",19,FVector(12,13,14)};WORLD.things.push_back(&third);
        Check(PortCeProjectBeginWorld(777,false),"the map's object list opens");
        for(CeProjectPosition v : {CeProjectPosition{"AuthoredAnchor",1,2,3}, CeProjectPosition{"OtherAnchor",7,8,9},
                                   CeProjectPosition{"ThirdAnchor",12,13,14}})
            Check(PortCeProjectApplyPlacement(v.source,std::strlen(v.source)+1,false,&v.x,&v.y,&v.z),"an editable source is observed");
        Check(PortCeProjectEndWorld(false)&&PortCeEditorAfterLoad(),"all three objects open for editing");PortCeEditorFrame();
        auto move=[](int i,CeEditorPoint p){return gEditorState.Select(i)&&gEditorState.BeginMove()&&
            gEditorState.Move(p)&&gEditorState.FinishMove(true)&&ApplyOne(i);};
        Check(move(1,{2,3,4})&&move(2,{100000,13,14})&&move(0,{60,61,62})&&EditorSave(),"the farther reachable object is edited last");
        EditorBrowse();EditorRequestOpen();
        Check(gEditorState.Selected()==1&&Near(Vector(gEditorFocus),objectB.position)&&
              Near(objectA.position,FVector(63,64,65))&&Near(third.position,FVector(100000,13,14)),
              "reopening chooses the nearest reachable change, not the last edit or first map row");
        Check(move(0,{-1,0,1})&&EditorSave(),"the first two changes now have the same world position");
        EditorBrowse();EditorRequestOpen();
        Check(gEditorState.Selected()==0&&Near(Vector(gEditorFocus),objectA.position),
              "equally near changed objects use stable map order");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // A gameplay side edit has its own undo step even though the unit has not moved.
        ResetFixture();StartEditor();const auto press=[](unsigned k){PortCeEditorKey(k,false);EditorTrap(WORD(k),KEY_EVENT_UP);};
        press(DIK_TAB);press(DIK_T);
        Check(gEditorState.Dirty()&&gEditorState.CanUndo(),"changing the selected unit's side makes an undoable edit");
        EditorUndo(false);Check(!gEditorState.Dirty(),"undo returns the side to its installed value");
        EditorUndo(true);Check(gEditorState.Dirty()&&EditorSave(),"redo saves the selected allegiance");
        Check(savedRevision.find("\"side\":\"muspell\"")!=std::string::npos&&savedRevision.find("\"version\":3")!=std::string::npos,
              "the saved CE revision carries the new side without changing the original unit");
        const auto saved=savedRevision;const CCareer entry=CAREER;
        Check(objectA.allegiance==0&&Near(objectA.position,FVector(4,5,6)),"authoring does not change live AI or move the unit");
        press(DIK_F5);PortCeEditorLevelEnd();
        Check(Reload()&&PortCeEditorPlaying()&&objectA.allegiance==1&&Near(objectA.position,FVector(4,5,6)),
              "the saved allegiance enters the unit's initialization in playable gameplay");
        press(DIK_F5);PortCeEditorLevelEnd();Check(Reload(),"side edit returns to the editor");PortCeEditorFrame();
        Check(gEditorState.Selected()==0&&gEditorState.Object(0)->side==1&&gEditorState.CanUndo()&&objectA.allegiance==0,
              "return retains side, selection and history and rebuilds the underlying original");
        EditorUndo(false);Check(gEditorState.Object(0)->side==-1&&gEditorState.Dirty(),"Undo after play restores installed allegiance");
        EditorUndo(true);Check(!gEditorState.Dirty()&&gEditorState.Object(0)->side==1,"Redo restores the saved allegiance as clean");
        press(DIK_ESCAPE);PortCeEditorLevelEnd();Check(PortCeEditorTakeReturn()&&CareerEquals(entry),"side edit restores the entering career");
        Reset();StartEditor();EditorBrowse();EditorRequestOpen();
        Check(gEditorState.Selected()==0&&gEditorState.Object(0)->side==1&&!gEditorState.Dirty()&&!gEditorState.CanUndo()&&savedRevision==saved,
              "fresh open frames a change of side alone while keeping the saved revision unchanged");
        gEditorState.Select(1);press(DIK_T);
        Check(!gEditorState.Dirty()&&gEditorState.Object(1)->side==-1,"an ordinary object cannot be assigned a unit side");
        gEditorState.Select(0);press(DIK_T);press(DIK_T);
        Check(gEditorState.Object(0)->side==-1&&EditorSave()&&savedRevision.find("\"side\":")==std::string::npos,
              "cycling to the installed side removes the override from disk");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // Author one ground goal without choosing or moving any object.
        ResetFixture();StartEditor();const CCareer entry=CAREER;
        const auto press=[](unsigned k){PortCeEditorKey(k,false);EditorTrap(WORD(k),KEY_EVENT_UP);};
        press(DIK_R);press(DIK_ESCAPE);
        Check(!gEditorChoosingReach&&!gEditorState.HasReach()&&!gEditorState.Dirty(),"canceling point placement leaves the project alone");
        press(DIK_R);MOUSE_X=-1;MOUSE_Y=0;EditorTrap(0,4);
        Check(gEditorChoosingReach&&!gEditorState.HasReach(),"a click without valid ground does not create a rule");
        MOUSE_X=45;MOUSE_Y=70;EditorTrap(0,4);
        Check(!gEditorChoosingReach&&gEditorState.Dirty()&&gEditorState.HasReach()&&gEditorState.Reach().x==45,
              "one world click enables the saved rule at the clicked point");
        EditorUndo(false);Check(gEditorReady&&gEditorPhase==Authoring&&!gEditorState.HasReach()&&!gEditorState.Dirty(),"undo an objective without selecting an object");
        EditorUndo(true);Check(gEditorState.HasReach()&&gEditorState.Reach().y==70,"redo restores the exact ground point");
        press(DIK_R); press(DIK_DOWN); press(DIK_DOWN); press(DIK_RETURN);Check(!gEditorState.HasReach(),"remove only the objective");
        EditorUndo(false);Check(gEditorState.HasReach()&&gEditorState.Reach().x==45,"undo restores a deleted rule");
        Check(EditorSave()&&savedRevision.find("\"version\":4")!=std::string::npos&&savedRevision.find("\"reach-point\"")!=std::string::npos,"saving a chosen rule opts this project into version four");
        const auto saved=savedRevision;press(DIK_F5);PortCeEditorLevelEnd();Check(Reload()&&PortCeEditorPlaying(),"saved goal enters normal playable mode");
        GAME.mGameState=GAME_STATE_PLAYING;GAME.pilot.engine.pos=FVector(45,50,0);PortCeEditorFrame();
        Check(!gEditorReached&&gEditorReachPlayerValid&&gEditorReachDistance==20&&gEditorReachBearing==0,"an outside pilot sees the correct distance and forward direction");
        GAME.pilot.engine.orient.MakeRotationYawF(3.14159265f);PortCeEditorFrame();
        Check(!gEditorReached&&gEditorReachBearing==4,"guidance follows the pilot's heading when turning in place");
        GAME.pilot.engine.pos=FVector(45,70,90);GAME.pilot.exists=false;PortCeEditorFrame();Check(!gEditorReached,"a missing pilot cannot complete the project");
        GAME.pilot.exists=true;GAME.pilot.engine.dead=true;PortCeEditorFrame();Check(!gEditorReached,"a destroyed pilot cannot complete the project");
        GAME.pilot.engine.dead=false;GAME.mPause=true;PortCeEditorFrame();Check(!gEditorReached,"a paused game cannot satisfy a goal");
        GAME.mPause=false;GAME.pilot.engine.pos=FVector(45,50,90);PortCeEditorFrame();
        GAME.pilot.engine.pos=FVector(45,90,90);PortCeEditorFrame();Check(!gEditorReached,"a fast high flyover does not complete the ground objective");
        GAME.pilot.engine.pos=FVector(45,70,90);PortCeEditorFrame();Check(!gEditorReached&&gEditorReachAtSurface&&gEditorReachDistance==90,"guidance leads a high pilot to the surface, and displays 3D distance");
        GAME.mPause=true;PortCeEditorFrame();GAME.mPause=false;
        GAME.pilot.engine.pos=FVector(45,50,0);PortCeEditorFrame();Check(!gEditorReached,"sampling resumes from the new position without connecting across a pause");
        GAME.pilot.engine.old=GAME.pilot.engine.pos=FVector(45,90,0);PortCeEditorFrame();Check(!gEditorReached,"a teleport past the goal cannot stand in for travelling through it");
        GAME.pilot.engine.old=GAME.pilot.engine.pos=FVector(45,50,90);PortCeEditorFrame();Check(!gEditorReached,"a relocation above the approach starts a new sample");
        GAME.pilot.engine.old=GAME.pilot.engine.pos;GAME.pilot.engine.pos=FVector(45,90,0);PortCeEditorFrame();Check(!gEditorReached,"a steep dive that passes high then descends beyond the goal misses the sphere");
        GAME.pilot.engine.old=GAME.pilot.engine.pos;GAME.pilot.engine.pos=FVector(45,50,0);PortCeEditorFrame();Check(gEditorReached&&!PortCeProjectCareerAllowed(),"fast movement crossing the sphere counts even with both sampled endpoints outside");
        GAME.pilot.engine.pos=FVector(1,1,0);PortCeEditorFrame();Check(gEditorReached,"the result remains available after leaving the point");
        press(DIK_F5);PortCeEditorLevelEnd();Check(Reload()&&!PortCeEditorPlaying()&&gEditorState.HasReach()&&gEditorState.CanUndo()&&CareerEquals(entry),"back restores the authored goal and its undo history without progress");PortCeEditorFrame();
        press(DIK_F5);PortCeEditorLevelEnd();Check(Reload()&&PortCeEditorPlaying()&&!gEditorReached,"starting a new test clears only its previous result");
        GAME.mGameState=GAME_STATE_PLAYING;GAME.pilot.engine.pos=FVector(45,70,0);PortCeEditorFrame();
        Check(gEditorReached,"starting inside the point legitimately completes on the first playable frame");
        press(DIK_F5);PortCeEditorLevelEnd();Check(Reload(),"second return succeeds");PortCeEditorFrame();
        press(DIK_ESCAPE);PortCeEditorLevelEnd();Check(PortCeEditorTakeReturn()&&CareerEquals(entry),"leave restores the entering career");
        Reset();StartEditor();EditorBrowse();EditorRequestOpen();
        Check(gEditorState.HasReach()&&gEditorState.Reach().x==45&&gEditorState.Reach().y==70&&gEditorFocus.x==45&&gEditorFocus.y==70&&
              !gEditorState.Dirty()&&savedRevision==saved,"fresh Open finds the point, frames it and keeps every saved byte");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    // The second stop is an explicit add, never an implicit replacement. Simulate only ordinary editor actions.
    {
        ResetFixture(); StartEditor(); const CCareer entering = CAREER;
        const auto key = [](unsigned k) { PortCeEditorKey(k, false); EditorTrap(WORD(k), KEY_EVENT_UP); };
        const auto ground = [](int x, int y) { MOUSE_X=x; MOUSE_Y=y; EditorTrap(0,4); };
        const auto choose = [&](int n) { key(DIK_R); for (int j=0;j<n;++j) key(DIK_DOWN); key(DIK_RETURN); };
        key(DIK_R); ground(45,40);
        key(DIK_R); Check(gEditorDialog==DialogRoute && !gEditorChoosingReach, "after stop one the editor offers a separate Add or Move choice");
        key(DIK_RETURN); ground(45,120);
        Check(gEditorState.ReachCount()==2 && gEditorState.Reach(0).y==40 && gEditorState.Reach(1).y==120, "adding a stop keeps the original and its order");
        choose(1); ground(45,130); Check(gEditorState.Reach(0).y==40 && gEditorState.Reach(1).y==130, "move Stop 2 changes only Stop 2");
        choose(2); Check(gEditorState.Reach(0).y==130 && gEditorState.Reach(1).y==40, "reverse exchanges both coordinates atomically");
        choose(3); Check(gEditorState.ReachCount()==1 && gEditorState.Reach().y==40, "removing the first stop promotes the second");
        EditorUndo(false); Check(gEditorState.ReachCount()==2 && gEditorState.Reach(0).y==130, "undo restores the first stop and order");
        choose(4); Check(gEditorState.ReachCount()==1 && gEditorState.Reach().y==130, "removing the second stop keeps the first");
        EditorUndo(false); EditorUndo(false); EditorUndo(true);
        Check(gEditorState.ReachCount()==2 && gEditorState.Reach(0).y==130 && gEditorState.Reach(1).y==40, "undo and redo of reverse keep order");
        Check(EditorSave() && savedRevision.find("\"version\":5")!=std::string::npos, "two stops serialize as a version five route");
        const std::string route=savedRevision;
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload() && PortCeEditorPlaying(), "F5 opens the saved route");
        GAME.mGameState=GAME_STATE_PLAYING;
        auto& engine=GAME.pilot.engine;
        auto visit=[&](float y) { engine.old=engine.pos; engine.pos=FVector(45,y,0); PortCeEditorFrame(); };
        visit(40); Check(gEditorReachStage==0 && !gEditorReached, "visiting Stop 2 first earns no progress");
        visit(40); Check(gEditorReachStage==0 && !gEditorReached, "waiting at Stop 2 does not advance the route");
        visit(130); Check(gEditorReachStage==1 && !gEditorReached && gEditorTestReach.y==40, "only reaching Stop 1 changes the active waypoint");
        visit(130); Check(!gEditorReached, "a previous out-of-order visit is not stored as credit");
        visit(40); Check(gEditorReached && !PortCeProjectCareerAllowed(), "only a later visit to Stop 2 completes the project without career credit");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "two-stop test returns to the editor"); PortCeEditorFrame();
        Check(gEditorState.Reach(0).y==130 && gEditorState.Reach(1).y==40 && CareerEquals(entering), "return preserves the ordered route and career");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload() && PortCeEditorPlaying(), "restart fast forward crossing");
        GAME.mGameState=GAME_STATE_PLAYING; visit(175); visit(10);
        Check(gEditorReached && gEditorReachStage==1, "one fast segment meeting Stop 1 then Stop 2 completes the entire ordered route");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "return after fast forward crossing"); PortCeEditorFrame();
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload() && PortCeEditorPlaying(), "restart reverse crossing");
        GAME.mGameState=GAME_STATE_PLAYING; visit(10); visit(175);
        Check(!gEditorReached && gEditorReachStage==1, "one fast segment meeting Stop 2 before Stop 1 cannot retroactively earn Stop 2");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "return after reverse crossing"); PortCeEditorFrame();
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload() && PortCeEditorPlaying(), "restart crossing on adjacent ticks");
        GAME.mGameState=GAME_STATE_PLAYING; visit(175); visit(130); visit(10);
        Check(gEditorReached, "the tick immediately following Stop 1 also detects a complete sweep across Stop 2");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "return after adjacent fast crossings"); PortCeEditorFrame();
        // A second invalidly early entry which overlaps the first must be left and entered again.
        choose(1); ground(45,110); key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "overlap fixture reload");
        GAME.mGameState=GAME_STATE_PLAYING; visit(90); visit(120); visit(120);
        Check(gEditorReachStage==1 && gEditorSecondNeedsExit && !gEditorReached, "earlier entry into overlapping stop 2, followed by waiting, does not complete it");
        visit(150); visit(110); Check(gEditorReached, "leaving the shared volume and returning legitimately completes Stop 2");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "return after overlap"); PortCeEditorFrame();
        choose(1); ground(45,130); key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "identical-stops fixture reload");
        GAME.mGameState=GAME_STATE_PLAYING; visit(90); visit(130); visit(130);
        Check(gEditorReachStage==1 && gEditorSecondNeedsExit && !gEditorReached, "identical entry times do not complete both stops at once or by waiting");
        visit(160); visit(130); Check(gEditorReached, "coincident stops accept a distinct return visit");
        key(DIK_F5); PortCeEditorLevelEnd(); Check(Reload(), "return after coincident stops"); PortCeEditorFrame();
        choose(1); ground(45,40); Check(EditorSave() && savedRevision==route, "restore the original two-stop project before fresh reopen");
        key(DIK_ESCAPE); PortCeEditorLevelEnd(); Check(PortCeEditorTakeReturn(), "exit after route test");
        Reset(); StartEditor(); EditorBrowse(); EditorRequestOpen();
        Check(gEditorState.ReachCount()==2 && gEditorState.Reach(0).y==130 && gEditorState.Reach(1).y==40 && !gEditorState.Dirty() && route==savedRevision,
              "fresh Open restores both stops without rewriting the project");
        PortCeEditorLevelEnd(); (void)PortCeEditorTakeReturn();
    }

    {   // Undo and Redo survive a play test and the way back.
        ResetFixture();const CCareer career=CAREER;StartEditor();Edit();
        Check(gEditorState.CanUndo(),"the edit can be undone before the play test");
        StartPlay();
        Check(PortCeEditorKey(DIK_F5,false),"F5 in the play test asks to go back");
        ReturnAndCheck(career);
        Check(gEditorState.CanUndo()&&!gEditorState.CanRedo(),"back in the editor the edit can still be undone");
        Check(gEditorState.Undo()&&!gEditorState.Dirty()&&gEditorState.Object(0)->position.x==1,"Undo after the play test puts the object back");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {   // Last explicitly saved one-stop project, plus a second stop only the editor knows about.
        ResetFixture(); StartEditor();
        const CCareer career=CAREER;
        const CeEditorPoint one{20,30,0}, two{90,30,0};
        Check(gEditorState.SetReachAt(0,&one)&&EditorSave()&&saves==1, "explicit Save owns the one-stop project");
        const std::string file=savedRevision, name=gEditorFilename;
        CeProjectDocument* const authoring=gEditorDocument;
        Check(gEditorState.SetReachAt(1,&two)&&gEditorState.Select(1)&&gEditorState.Dirty(), "add an unsaved second stop with an object selected");
        gEditorFocus={23,27,7};gEditorYaw=.43f;gEditorPitch=.81f;gEditorDistance=63;
        const unsigned writes=saves, fileReads=reads;
        saveFailure=readFailure=true;
        Check(EditorStartPlaytest()&&saves==writes&&reads==fileReads&&savedRevision==file, "draft preview neither reads nor overwrites the saved one-stop project");
        PortCeEditorLevelEnd();
        Check(Reload()&&PortCeEditorPlaying()&&gEditorReachLength==2&&gEditorTestReach.x==one.x&&gEditorNextReach.x==two.x,
              "the playable world uses both draft stops despite unavailable project storage");
        GAME.mGameState=GAME_STATE_PLAYING; GAME.pilot.engine.pos=FVector(one.x,one.y,one.z); GAME.pilot.engine.old=GAME.pilot.engine.pos; PortCeEditorFrame();
        Check(gEditorReachStage==1&&!gEditorReached,"Stop 1 was genuinely earned before Reset");
        GAME.quit=0; Check(PortCeEditorKey(DIK_F6,true)&&GAME.quit==0, "a held Reset key cannot loop reloads");
        Check(PortCeEditorKey(DIK_F6,false)&&GAME.quit==QT_RESTART_LEVEL,"one F6 asks the existing gameplay owner to restart the preview");
        PortCeEditorLevelEnd();
        Check(PortCeEditorResolveQuit(GAME.quit)==QT_RESTART_LEVEL&&Reload()&&PortCeEditorPlaying()&&gEditorReachLength==2&&gEditorReachStage==0&&!gEditorReached,
              "Reset replays the same unsaved two stops with fresh progress");
        Check(PortCeEditorKey(DIK_F5,false)&&gEditorPhase==ReturnToAuthoring,"Back leaves the pilot and requests the authoring view");
        PortCeEditorLevelEnd();Check(Reload()&&gEditorReady&&!PortCeEditorPlaying()&&GAME.IsPaused(),"Back restores the editing world"); PortCeEditorFrame();
        Check(gEditorState.ReachCount()==2&&gEditorState.Dirty()&&gEditorState.Selected()==1&&gEditorState.CanUndo()&&
              gEditorInputOwned&&LT.trap==(pKeyTrapper)EditorTrap&&gEditorFileCurrent&&gEditorFilename==name&&gEditorDocument==authoring,
              "Back restores the dirty draft, selection, pointer and the original named document");
        Check(gEditorFocus.x==23&&gEditorFocus.y==27&&gEditorFocus.z==7&&gEditorYaw==.43f&&gEditorPitch==.81f&&gEditorDistance==63,
              "Back preserves deliberate camera focus, orientation and zoom");
        Check(gEditorState.Undo()&&gEditorState.ReachCount()==1&&!gEditorState.Dirty()&&gEditorState.Redo()&&gEditorState.ReachCount()==2&&gEditorState.Dirty(),
              "Undo returns to the real saved baseline and Redo restores the unsaved second stop");
        Check(saves==writes&&reads==fileReads&&savedRevision==file&&CareerEquals(career),"preview, Reset and Back leave the disk and career unchanged");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }
    {
        ResetFixture(); StartEditor(); const CCareer entering=CAREER;
        const auto key=[](unsigned k){PortCeEditorKey(k,false);EditorTrap(WORD(k),KEY_EVENT_UP);};
        const auto ground=[](int x,int y){MOUSE_X=x;MOUSE_Y=y;EditorTrap(0,4);};
        key(DIK_R); ground(45,40); key(DIK_H); ground(45,120);
        Check(gEditorDialog==DialogHoldInspector && gEditorState.Hold(1).seconds==3,
              "ground click appends the Hold Area and opens its inspector");
        key(DIK_RETURN); key(DIK_DOWN); key(DIK_RETURN); key(DIK_DOWN); key(DIK_RETURN);
        Check(gEditorDialog==DialogNone && gEditorState.Hold(1).seconds==5 && gEditorState.Hold(1).radius==30,
              "inspector controls change duration and radius independently");
        Check(EditorSave() && savedRevision.find("hold-area")!=std::string::npos, "explicit save writes the mixed rule");
        const std::string savedHold=savedRevision;
        key(DIK_H); key(DIK_RETURN); key(DIK_DOWN); key(DIK_DOWN); key(DIK_RETURN);
        Check(gEditorState.Hold(1).seconds==10 && gEditorState.Dirty(), "inspector makes an undoable change to the saved draft");
        key(DIK_F5);PortCeEditorLevelEnd();Check(Reload()&&PortCeEditorPlaying(),"mixed preview loads");GAME.mGameState=GAME_STATE_PLAYING;
        auto visit=[&](float y,unsigned milliseconds){auto& e=GAME.pilot.engine;e.old=e.pos;e.pos=FVector(45,y,0);fixtureEditorTimeNs+=milliseconds*1000000ULL;PortCeEditorFrame();};
        visit(120,10);visit(120,12000);Check(!gEditorReached && gEditorReachStage==0 && gEditorHoldElapsed==0,"early visit to later Hold Area earns nothing");
        visit(40,100);Check(gEditorReachStage==1 && !gEditorReached,"reaching the first point unlocks the Hold Area");
        visit(120,100);visit(120,4000);Check(gEditorHoldInside && std::fabs(gEditorHoldElapsed-4.0)<0.001,"active Hold Area reports elapsed time");
        visit(151,100);Check(!gEditorHoldInside && gEditorHoldElapsed==0 && !gEditorReached,"leaving the authored radius erases partial dwell");
        visit(149,100);visit(149,7000);Check(gEditorHoldInside && gEditorHoldElapsed==7 && !gEditorReached,"re-entry inside the selected radius starts from zero");
        GAME.*EditorGameAccess::PauseField()=TRUE;visit(149,10000);GAME.*EditorGameAccess::PauseField()=FALSE;visit(149,100);
        Check(gEditorHoldElapsed==7 && !gEditorReached,"paused time does not count");
        visit(149,3000);Check(gEditorReached && gEditorHoldElapsed==10 && !PortCeProjectCareerAllowed(),"continuous hold completes without career credit");
        key(DIK_F6);PortCeEditorLevelEnd();Check(PortCeEditorResolveQuit(GAME.quit)==QT_RESTART_LEVEL && Reload()&&PortCeEditorPlaying()&&gEditorReachStage==0 && gEditorHoldElapsed==0 && !gEditorReached,"F6 restarts order and elapsed time");
        key(DIK_F5);PortCeEditorLevelEnd();Check(Reload(),"mixed draft returns");PortCeEditorFrame();
        Check(gEditorInputOwned && gEditorState.Hold(1).seconds==10 && gEditorState.Dirty() && savedRevision==savedHold && CareerEquals(entering),"Back preserves the editable settings, explicit file and career");
        EditorUndo(false);Check(gEditorState.Hold(1).seconds==5 && !gEditorState.Dirty(),"undo restores the explicitly saved duration");
        EditorUndo(true);Check(gEditorState.Hold(1).seconds==10,"redo restores the draft duration");
        PortCeEditorLevelEnd();PortCeEditorTakeReturn();
    }

    // These controls exercise the production start dispatch and support query
    // against authored collision results; the native mesh journey is separate.
    ResetFixture(); fixturePrimaryStart=true; fixtureSupportExists=true;
    fixtureSupportMesh=true; fixtureSupportZ=0;
    Check(BeginEditor() && Reload(), "primary start enters authoring");
    PortCeEditorFrame();
    Check(gEditorState.PlayerStartAvailable() && !gEditorState.PlayerStartEdited(), "original player start is visible without a document override");
    Check(gEditorState.SetPlayerStart({20,30,-1}) && EditorSave(), "explicit Save persists authored start");
    const std::string startSaved=savedRevision;
    Check(gEditorState.SetPlayerStart({22,30,-1}), "place another unsaved start");
    Check(EditorPlayerStartSupported({22,30,-1}), "mesh deck is valid support");
    Check(!EditorPlayerStartSupported({22,30,-20}), "a far-below deck is not immediate start support");
    fixtureSupportMesh=false; fixtureSupportZ=11;
    Check(!EditorPlayerStartSupported({22,30,10}), "submerged ground is not walker support");
    fixtureSupportMesh=true; fixtureSupportZ=9.5f;   // a hull at the waterline (water 10), not a deck
    Check(!EditorPlayerStartSupported({22,30,8.5f}), "a hull at the waterline is not a deck");
    fixtureSupportMesh=false;
    fixtureSupportMesh=true; fixtureSupportZ=0;
    // Something above the start (a roof, a building's floor, a bridge): refused in plain words before play.
    fixtureCoverExists=true; fixtureCoverZ=-6;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartCovered && !EditorPlayerStartSupported({22,30,-1}), "a covered start is refused");
    fixtureSupportExists=false;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartCovered, "inside a building (a roof above, nothing found below) is called covered");
    fixtureSupportExists=true;
    {
        const CeEditorState covered=gEditorState;
        Check(!EditorStartPlaytest() && gEditorPhase==Authoring && std::strstr(gEditorNote, "under or inside"),
              "play says the start is under or inside something");
        gEditorState=covered;
    }
    fixtureCoverExists=false;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartFits, "the open deck fits again");
    // A tower's wall 1.5 m from a placed start: the walker would stand in it or lean into it.
    fixtureWallExists=true; fixtureWallX=23.5f;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartCrowded, "a placed start against a wall is refused");
    Check(std::strcmp(EditorStartRefusal(StartCrowded), "Too close to a wall or rail. Move it clear.")==0,
          "the wall refusal says what to do");
    Check(EditorPlayerStartFit({22,30,-1},false)==StartFits, "a map's own start is not judged by its surroundings");
    fixtureWallExists=false;
    // The deck ends 1 m from a placed start (a hull's edge or slope): part of the walker has nothing under it.
    fixtureSupportMesh=true; fixtureSupportMinX=21.f;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartEdge && !EditorPlayerStartSupported({22,30,-1}), "a start at a deck's edge is refused");
    Check(std::strcmp(EditorStartRefusal(StartEdge), "Player Start is on an edge or a slope. Put it on flat ground or a deck.")==0,
          "the edge refusal says so");
    fixtureSupportMinX=-1e9f;
    // A foot 1.5 m out finds the deck 1.6 m lower than the centre (a steep slope): refused as an edge or slope.
    fixtureStepX=23.f; fixtureStepZ=1.6f;
    Check(EditorPlayerStartFit({22,30,-1},true)==StartEdge, "a start on a steep slope is refused");
    fixtureStepX=1e9f;
    fixtureSupportExists=false;
    Check(!EditorStartPlaytest() && std::strstr(gEditorNote, "nothing under it"), "play says the start has nothing under it");
    fixtureSupportExists=true;
    gEditorChoosingPlayerStart=true;
    const CeEditorState startDraft=gEditorState;
    const CeEditorPoint startFocus=gEditorFocus;
    fixtureSupportExists=false;
    Check(!EditorStartPlaytest() && gEditorPhase==Authoring && gEditorInputOwned &&
          gEditorState.PlayerStartSelected() && gEditorState.Dirty() && savedRevision==startSaved,
          "missing deck refuses before play and preserves selected unsaved start");
    fixtureSupportExists=true;
    Check(EditorStartPlaytest(), "safe draft start begins preview");
    PortCeEditorLevelEnd();
    Check(Reload() && PortCeEditorPlaying() && Near(GAME.pilot.engine.pos,FVector(22,30,-1)),
          "preview receives unsaved authored position through the native-start dispatch");
    GAME.mGameState=GAME_STATE_PLAYING;
    Check(PortCeEditorKey(DIK_F6,false), "F6 accepts player-start reset");
    PortCeEditorLevelEnd();
    Check(PortCeEditorResolveQuit(GAME.quit)==QT_RESTART_LEVEL && Reload() && PortCeEditorPlaying() &&
          Near(GAME.pilot.engine.pos,FVector(22,30,-1)), "F6 resets to the same unsaved start");
    Check(PortCeEditorKey(DIK_F5,false), "F5 accepts return from player-start preview");
    gEditorChoosingPlayerStart=true;
    PortCeEditorLevelEnd(); Check(Reload(), "player-start draft returns"); PortCeEditorFrame();
    Check(gEditorState.PlayerStartSelected() && !gEditorChoosingPlayerStart && !gEditorChoosingReach && gEditorState.Dirty() && savedRevision==startSaved &&
          SamePoint(gEditorFocus,startFocus) && gEditorState.CanUndo(),
          "return retains start selection, draft camera, unsaved state and history, and clears placement mode");
    EditorUndo(false);
    Check(!gEditorState.Dirty() && SamePoint(gEditorState.PlayerStart(),{20,30,-1}), "return undo restores explicitly saved start");
    EditorUndo(true);
    Check(gEditorState.Dirty() && SamePoint(gEditorState.PlayerStart(),startDraft.PlayerStart()), "return redo restores the unsaved start");
    Check(EditorStartPlaytest(), "second safe start begins reload");
    PortCeEditorLevelEnd(); fixtureSupportExists=false;
    Check(Reload() && gEditorPhase==ReturnToAuthoring && !PortCeEditorPlaying(),
          "support lost during native initialization never admits gameplay");
    PortCeEditorLevelEnd(); Check(Reload(), "unsafe loaded start returns its draft"); PortCeEditorFrame();
    Check(gEditorInputOwned && gEditorState.PlayerStartSelected() && gEditorState.Dirty() && savedRevision==startSaved,
          "late safety refusal retains explicit save and editable draft");
    Check(std::strstr(gEditorNote, "nothing under it") != nullptr, "the late refusal names its reason");
    // The map's own start (not one that begins in flight) is checked the same way when the play test loads.
    fixtureSupportExists=true;
    Check(gEditorState.UseOriginalPlayerStart() && !gEditorState.PlayerStartEdited(), "back to the map's own start");
    Check(EditorStartPlaytest(), "the map's own start begins a preview");
    PortCeEditorLevelEnd(); fixtureSupportExists=false;
    Check(Reload() && gEditorPhase==ReturnToAuthoring && !PortCeEditorPlaying(),
          "the map's own start without footing never admits gameplay");
    PortCeEditorLevelEnd(); Check(Reload(), "unsafe map start returns its draft"); PortCeEditorFrame();
    Check(std::strstr(gEditorNote, "nothing under it") != nullptr && !gEditorState.PlayerStartEdited(),
          "the map start's refusal names its reason and keeps the draft");
    PortCeEditorLevelEnd(); PortCeEditorTakeReturn(); fixturePrimaryStart=false;
    fixtureSupportExists=true; fixtureSupportMesh=false;

    {   // P shortcut, Delete refusal/restore cues, Esc cancel cues, and reload-window F5/F6 consumption.
        ResetFixture(); fixturePrimaryStart=true; fixtureSupportExists=true; fixtureSupportMesh=true; fixtureSupportZ=0;
        Check(BeginEditor() && Reload(), "shortcut regression enters authoring with editable player start");
        PortCeEditorFrame();
        auto press=[&](unsigned k){const unsigned before=cues; PortCeEditorKey(k,false); EditorTrap(WORD(k),KEY_EVENT_UP); return cues>before;};
        Check(press(DIK_R) && gEditorChoosingReach, "R arms Reach Point with a cue");
        Check(press(DIK_P) && gEditorChoosingPlayerStart && !gEditorChoosingReach && gEditorState.PlayerStartSelected(),
              "P arms Player Start, disarms Reach Point, selects start and plays a cue");
        Check(press(DIK_ESCAPE) && !gEditorChoosingPlayerStart && !gEditorNote[0],
              "Esc cancels Player Start placement with a cue");
        Check(press(DIK_DELETE) && !std::strcmp(gEditorNote, "Player Start is already at its original place."),
              "Delete on an unedited Player Start refuses with a cue and clear notice");
        Check(gEditorState.SetPlayerStart({22,30,-1}) && gEditorState.PlayerStartEdited(), "edit Player Start before reset");
        // Modal dialogs and F1 help must block Delete from mutating the selected Player Start behind them.
        gEditorDialog = DialogFiles;
        (void)PortCeEditorKey(DIK_DELETE, false); EditorTrap(WORD(DIK_DELETE), KEY_EVENT_UP);
        Check(gEditorState.PlayerStartEdited(), "Delete behind Open Project dialog never resets Player Start");
        gEditorDialog = DialogNone; gEditorHelp = true;
        (void)PortCeEditorKey(DIK_DELETE, false); EditorTrap(WORD(DIK_DELETE), KEY_EVENT_UP);
        Check(gEditorState.PlayerStartEdited(), "Delete behind F1 Keys never resets Player Start");
        gEditorHelp = false;
        // In Reach Point move mode while Player Start is selected, Delete removes the stop rather than resetting Player Start.
        const CeEditorPoint stopPt{45, 70, 0};
        Check(gEditorState.SetReachAt(0, &stopPt) && gEditorState.ReachCount() == 1, "add Stop 1 while Player Start is edited");
        gEditorState.Select(CeEditorState::PlayerStartSelection);
        gEditorChoosingReach = true; gEditorPlaceIndex = 0;
        Check(press(DIK_DELETE) && !gEditorChoosingReach && gEditorState.ReachCount() == 0 && gEditorState.PlayerStartEdited(),
              "Delete during Reach Point move removes the stop without resetting selected Player Start");
        Check(press(DIK_DELETE) && !gEditorState.PlayerStartEdited() &&
              !std::strcmp(gEditorNote, "Original Player Start restored. Undo restores your edit."),
              "Delete on an edited Player Start restores original with cue and clear notice");
        Check(press(DIK_R) && gEditorChoosingReach && press(DIK_ESCAPE) && !gEditorChoosingReach,
              "Esc cancels Reach Point placement with a cue");
        // Placement flags clear across Save, Browse, Undo, Change Side, and Exit; Moving() guards Exit, Change Side, F1, and mouse release.
        gEditorChoosingReach = true; gEditorChoosingPlayerStart = true;
        (void)EditorSave();
        Check(!gEditorChoosingReach && !gEditorChoosingPlayerStart, "EditorSave clears placement flags");
        gEditorChoosingReach = true; gEditorChoosingPlayerStart = true;
        EditorBrowse();
        Check(!gEditorChoosingReach && !gEditorChoosingPlayerStart, "EditorBrowse clears placement flags");
        gEditorDialog = DialogNone;
        gEditorChoosingReach = true; gEditorChoosingPlayerStart = true;
        EditorUndo(false);
        Check(!gEditorChoosingReach && !gEditorChoosingPlayerStart, "EditorUndo clears placement flags");
        gEditorState.Select(0);
        gEditorChoosingReach = true; gEditorChoosingPlayerStart = true;
        EditorToolbar(5);
        Check(!gEditorChoosingReach && !gEditorChoosingPlayerStart, "Change Side clears placement flags");
        Check(EditorBeginMove() && gEditorState.Moving(), "EditorBeginMove starts object move");
        gEditorPadActive = true;
        EditorTrap(0, 3);
        Check(gEditorState.Moving() && gEditorPadActive, "spurious mouse-up during gamepad move keeps moving state");
        gEditorPadActive = false;
        gEditorHelp = true;
        EditorTrap(0, 3);
        Check(gEditorState.Moving(), "mouse-up while help is open does not commit move");
        gEditorHelp = false;
        PortCeEditorKey(DIK_F1, false); EditorTrap(WORD(DIK_F1), KEY_EVENT_UP);
        Check(!gEditorHelp && !std::strcmp(gEditorNote, "Finish moving the object first."), "F1 refused while moving");
        EditorToolbar(5);
        Check(gEditorState.Moving() && !std::strcmp(gEditorNote, "Finish moving the object first."), "Change Side refused while moving");
        EditorToolbar(9);
        Check(gEditorState.Moving() && gEditorDialog == DialogNone && !gEditorClosing &&
              !std::strcmp(gEditorNote, "Finish moving the object first."), "Exit refused while moving");
        (void)gEditorState.FinishMove(false);
        gEditorChoosingReach = true; gEditorChoosingPlayerStart = true;
        EditorToolbar(9);
        Check(!gEditorChoosingReach && !gEditorChoosingPlayerStart, "Exit clears placement flags");
        gEditorDialog = DialogNone; gEditorClosing = false; GAME.quit = 0;
        gEditorPadActive = true; gEditorLayoutValid = true;
        Check(PortCeEditorKey(DIK_F5,false) && gEditorPhase==LoadPlaytest && !gEditorPadActive && !gEditorLayoutValid,
              "F5 enters LoadPlaytest transition and clears gamepad mode and stale layout");
        Check(PortCeEditorKey(DIK_F5,false) && PortCeEditorKey(DIK_F6,false) && !PortCeEditorKey(DIK_W,false),
              "F5 and F6 are consumed during LoadPlaytest reload while ordinary keys are not");
        gEditorPadActive = true; gEditorLayoutValid = true;
        PortCeEditorLevelEnd();
        Check(!gEditorLayoutValid, "PortCeEditorLevelEnd clears stale layout");
        PortCeEditorTakeReturn();
        Check(!gEditorPadActive, "PortCeEditorCancel clears gamepad mode on session exit");
        fixturePrimaryStart=false; fixtureSupportMesh=false;
        ResetFixture(); StartEditor();
        Check(press(DIK_P) && !gEditorChoosingPlayerStart &&
              !std::strcmp(gEditorNote, "This map has no editable Player Start."),
              "P on a map without editable Player Start refuses cleanly with a cue");
        PortCeEditorLevelEnd(); PortCeEditorTakeReturn();
    }

    ResetFixture();GAME.SetCurrentCamera(0,nullptr,true);std::free(gEditorCareer);gEditorCareer=nullptr;
    std::printf("native editor play test: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
