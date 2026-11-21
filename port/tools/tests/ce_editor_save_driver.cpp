// Exercise the production serializer and editor lifecycle with authored data.
#include "editor-playtest-driver.private.inc"
#include "ce_hooks_host_stubs.h"	// the patched career serializer calls the CE hook points; their host no-ops
#include "ce_editor_save_fixture.h"

static void ChangeCareer(int generation) {
    for (unsigned& value : CAREER.progress)
        value ^= unsigned(37 + generation * 11);
    CAREER.mCareerInProgress = 1;
    SetFixtureOptions(CAREER, generation);
}

static void CheckLiveCareer(const CCareer& progress, const CCareer& options) {
    Check(SameFixtureProgress(CAREER, progress), "temporary progress is discarded");
    Check(SameFixtureOptions(CAREER, options), "current options survive the transition");
}

static void CloseEditor(const CCareer& progress, const CCareer& options) {
    PortCeEditorCancel();
    Check(!PortCeEditorActive(), "cancel closes the editor fence");
    CheckLiveCareer(progress, options);
    ProbeSavedCareer(progress, options, false);
    ChangeCareer(4);
    const CCareer ordinary = CAREER;
    ProbeSavedCareer(ordinary, ordinary, false);
}

int main(int argc, char** argv) {
    const std::string test = argc > 1 ? argv[1] : "playing";
    ResetFixture();
    SetFixtureOptions(CAREER, 0);
    const CCareer baseline = CAREER;
    clearSavedGodFlags = test != "saving-policy";

    if (test == "inactive") {
        ChangeCareer(1);
        const CCareer ordinary = CAREER;
        ProbeSavedCareer(ordinary, ordinary, false);
        return failures ? 1 : 0;
    }
    if (test == "rejected-entry") {
        network = true;
        Check(!BeginEditor() && !PortCeEditorActive(), "network entry is rejected");
        ChangeCareer(1);
        const CCareer ordinary = CAREER;
        ProbeSavedCareer(ordinary, ordinary, false);
        return failures ? 1 : 0;
    }
    if (test == "initial-loading") {
        Check(BeginEditor() && PortCeEditorActive(), "entry owns a snapshot before loading");
    } else {
        StartEditor();
        Edit();
        if (test == "failed-save" || test == "failed-read" || test == "invalid-document") {
            saveFailure = test == "failed-save";
            readFailure = test == "failed-read";
            corruptRead = test == "invalid-document";
            Check(PortCeEditorKey(DIK_F5,false), "preview handles unavailable project storage");
            Check(gEditorPhase == EditorPhase::LoadPlaytest && !PortCeEditorPlaying() && !saves && !reads,
                  "storage errors do not block draft preview or move career ownership");
        } else if (test != "authoring") {
            if (test == "toolbar") {
                const auto before = saves;
                EditorToolbar(editorPlayToolbarAction);
                Check(saves == before && gEditorPhase == EditorPhase::LoadPlaytest,
                      "the Preview Draft toolbar action plays without saving");
            } else if (test == "play-loading" || test == "invalid-world" || test == "duplicate-world") {
                Check(PortCeEditorKey(DIK_F5,false), "F5 starts a play load");
                Check(gEditorPhase == EditorPhase::LoadPlaytest, "play load remains active");
            } else {
                StartPlay();
            }
            if (test == "toolbar" || test == "invalid-world" || test == "duplicate-world") {
                PortCeEditorLevelEnd();
                // The engine's load never fails; a project that no longer matches the map is refused by the editor instead.
                const bool loaded = Reload(test == "invalid-world", test == "duplicate-world");
                const bool valid = loaded && PortCeEditorPlaying();
                Check(loaded && valid == (test == "toolbar"), "world bindings decide play admission");
                Check(PortCeEditorActive(), "a failed world load retains the save fence");
            }
        }
    }

    Check(PortCeEditorActive() && !PortCeProjectCareerAllowed(), "all active phases fence career writes");
    Check(gEditorCareer && SameFixtureProgress(*gEditorCareer, baseline),
          "entry snapshot contains the original campaign progress");
    ChangeCareer(1);
    CCareer options = CAREER;
    CCareer filtered = CAREER;
    PortCeEditorFilterSave(&CAREER, &filtered);
    Check(SameFixtureProgress(filtered, baseline), "direct filter protects the entry progress");
    ProbeSavedCareer(baseline, options, true);
    CheckFilterNullArguments();

    if (test == "retry") {
        Check(PortCeEditorResolveQuit(QT_RESTART_LEVEL) == QT_RESTART_LEVEL,
              "retry retains the editor reload lane");
        Check(gEditorPhase == EditorPhase::LoadPlaytest && !PortCeEditorPlaying(),
              "retry creates a fresh saved-revision load");
        CheckLiveCareer(baseline, options);
        ChangeCareer(2);
        options = CAREER;
        ProbeSavedCareer(baseline, options, true);
        PortCeEditorLevelEnd();
        Check(Reload() && PortCeEditorPlaying(), "retry reloads normal gameplay");
        ChangeCareer(3);
        options = CAREER;
        ProbeSavedCareer(baseline, options, true);
    }

    // level-end: the game's own quit to the frontend at the end of play (win, loss, outro), not a menu leave.
    const bool leave = test == "leave-frontend" || test == "leave-title" || test == "leave-timeout" ||
        test == "level-end";
    const bool returnToEditor = leave || test == "playing" || test == "retry" ||
        test == "toolbar" || test == "return-loading" || test == "return-authoring" ||
        test == "invalid-return" || test == "saving-policy";
    if (returnToEditor) {
        if (leave) {
            const int requested = test == "leave-frontend" ? QT_USER_QUIT_TO_FRONTEND :
                test == "leave-title" ? QT_USER_QUIT_TO_TITLE_SCREEN :
                test == "level-end" ? QT_QUIT_TO_FRONTEND : QT_QUIT_TIMEOUT;
            Check(PortCeEditorResolveQuit(requested) == QT_RESTART_LEVEL,
                  "ordinary leave returns to authoring");
        } else {
            Check(PortCeEditorKey(DIK_F5,false), "F5 returns from play");
        }
        Check(gEditorPhase == EditorPhase::ReturnToAuthoring, "return remains a fenced load");
        CheckLiveCareer(baseline, options);
        ChangeCareer(5);
        options = CAREER;
        ProbeSavedCareer(baseline, options, true);
        if (test != "return-loading") {
            PortCeEditorLevelEnd();
            if (test == "invalid-return") GAME.controllerAvailable = false;
            const bool loaded = Reload();
            const bool valid = loaded && gEditorReady && gEditorPhase != EditorPhase::Refused;
            Check(loaded && valid == (test != "invalid-return"), "return admission checks the controller");
            Check(PortCeEditorActive(), "return keeps the campaign fence until exit");
            if (valid) {
                Check(gEditorReady && !PortCeEditorPlaying() && GAME.IsPaused(),
                      "a successful return parks the authored scene");
                Check(gEditorState.Dirty(), "return displays the unsaved draft");
                CheckLiveCareer(baseline, options);
            }
            ProbeSavedCareer(baseline, options, true);
        }
    } else if (test == "system-quit") {
        Check(PortCeEditorResolveQuit(QT_QUIT_TO_SYSTEM) == QT_QUIT_TO_SYSTEM,
              "system quit remains a system quit");
        ProbeSavedCareer(baseline, options, true);
    }

    CloseEditor(baseline, options);
#ifdef EDITOR_MUSEUM_COMPOSED
    if (test == "museum-composition") {
        const CCareer museumBaseline = CAREER;
        std::memcpy(gMuseumCareer, &museumBaseline, sizeof(museumBaseline));
        museum = true;
        gMuseumCareerSaved = true;
        ChangeCareer(6);
        const CCareer museumOptions = CAREER;
        CCareer filteredMuseum = CAREER;
        PortCeMuseumVisitFilterSave(&CAREER, &filteredMuseum);
        Check(SameFixtureProgress(filteredMuseum, museumBaseline),
              "direct museum filter retains its entry snapshot");
        ProbeSavedCareer(museumBaseline, museumOptions, true);
        gMuseumCareerSaved = false;
        museum = false;
        const CCareer ordinary = CAREER;
        ProbeSavedCareer(ordinary, ordinary, false);
    }
#endif
    return failures ? 1 : 0;
}
