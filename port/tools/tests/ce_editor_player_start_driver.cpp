#include "ce_editor.h"
#include "ce_project.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

const char* PortGameDir() { return ""; }
const char* PortUserDir() { return ""; }
static bool editorActive = false;
static int startDispatches = 0;
extern "C" bool PortCeEditorActive() { return editorActive; }
extern "C" bool PortCeEditorApplyPlayerStart(float& x, float& y, float& z, bool flying) {
    ++startDispatches; x = 31; y = 46; z = flying ? -90 : -9; return true;
}

namespace {
int failures = 0;
void Check(bool ok, const char* label) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); ++failures; } }
bool Same(CeEditorPoint a, CeEditorPoint b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
std::string Write(CeProjectDocument* document) {
    char error[256]{};
    const size_t length = PortCeProjectDocumentWrite(document, nullptr, 0, error, sizeof(error));
    Check(length > 0, "document serialization has bytes");
    std::string bytes(length, '\0');
    Check(PortCeProjectDocumentWrite(document, &bytes[0], bytes.size(), error, sizeof(error)) == length,
          "document serialization is complete");
    if (!bytes.empty() && bytes.back() == '\0') bytes.pop_back();
    return bytes;
}
void State() {
    CeEditorState state;
    const CeEditorPoint original{4, 7, -3}, authored{31, 46, -9}, carrier{4, 7, 0};
    Check(state.Add("carrier", carrier, 1), "collect carrier");
    Check(state.InitializePlayerStart(original), "collect independent primary start");
    Check(state.Seal(), "seal authored scene");
    Check(!state.Dirty() && Same(state.PlayerStart(), original), "original start is clean");
    Check(state.Select(0) && state.BeginMove() && state.Move({100, 200, 0}) && state.FinishMove(true), "move carrier");
    Check(Same(state.PlayerStart(), original), "an object the start does not stand on never moves it");
    state.MarkSaved();
    Check(state.SetPlayerStart(authored), "place independent player start");
    Check(state.Dirty() && state.PlayerStartEdited() && state.PlayerStartSelected(), "start edit is selected and dirty");
    Check(state.Undo() && !state.Dirty() && !state.PlayerStartEdited() && Same(state.PlayerStart(), original), "undo start returns original and clean");
    Check(state.Redo() && state.Dirty() && Same(state.PlayerStart(), authored), "redo restores start and dirty");
    state.MarkSaved();
    Check(state.BeginMove() && state.Move({32, 48, -11}), "drag start");
    Check(state.FinishMove(false) && Same(state.PlayerStart(), authored) && !state.Dirty(), "cancel start drag preserves saved start");
    Check(state.BeginMove() && state.Move({32, 48, -11}) && state.FinishMove(true), "accept start drag");
    Check(state.Undo() && Same(state.PlayerStart(), authored) && !state.Dirty(), "drag undo restores saved start");
    Check(state.UseOriginalPlayerStart() && !state.PlayerStartEdited() && state.Dirty(), "remove override restores original and marks dirty");
    Check(state.Undo() && state.PlayerStartEdited() && Same(state.PlayerStart(), authored) && !state.Dirty(), "undo removed override");
    Check(!state.SetPlayerStart({std::numeric_limits<float>::quiet_NaN(), 0, 0}) && !state.Dirty(), "invalid start leaves state intact");
    CeEditorState returned;
    Check(returned.Add("carrier", {100, 200, 0}, 99), "reloaded carrier identity may change");
    Check(returned.InitializePlayerStart(original) && returned.RestorePlayerStart(true, authored) && returned.Seal(), "reload same start");
    Check(returned.KeepHistory(state), "return retains player-start history");
    Check(!returned.Dirty() && returned.CanUndo(), "return retains saved state and undo");
    Check(returned.Undo() && !returned.PlayerStartEdited() && returned.Dirty(), "return undo reaches original start");
    Check(returned.Redo() && Same(returned.PlayerStart(), authored) && !returned.Dirty(), "return redo restores saved authored start");
    // The start stands on the carrier's deck (the caller's geometry says so): it moves with the carrier, and one
    // cancel, Undo or Redo puts both back together. This is the spawn boat a player moves.
    CeEditorState carried;
    Check(carried.Add("carrier", carrier, 1) && carried.InitializePlayerStart(original) && carried.Seal(), "carrier scene");
    Check(carried.Select(0) && carried.BeginMove(true) && carried.MoveCarriesStart(), "carrier move takes its start along");
    Check(carried.Move({14, 27, 0}) && Same(carried.PlayerStart(), {14, 27, -3}) && carried.PlayerStartEdited(),
          "start rides with the carrier while dragging");
    Check(carried.FinishMove(false) && Same(carried.Object(0)->position, carrier) && !carried.PlayerStartEdited() &&
          Same(carried.PlayerStart(), original) && !carried.Dirty(), "cancel puts carrier and start back");
    Check(carried.Select(0) && carried.BeginMove(true) && carried.Move({104, 207, 0}) && carried.FinishMove(true),
          "move carrier with its start");
    Check(Same(carried.PlayerStart(), {104, 207, -3}) && carried.PlayerStartEdited() && carried.Dirty(),
          "carried start keeps its place on the deck");
    Check(carried.Undo() && Same(carried.Object(0)->position, carrier) && !carried.PlayerStartEdited() &&
          Same(carried.PlayerStart(), original) && !carried.Dirty() && !carried.CanUndo(), "one undo returns both");
    Check(carried.Redo() && Same(carried.Object(0)->position, {104, 207, 0}) && Same(carried.PlayerStart(), {104, 207, -3}),
          "one redo moves both again");
    Check(carried.SetPlayerStart({110, 210, -3}), "player places the start elsewhere on the moved deck");
    Check(carried.Select(0) && carried.BeginMove(true) && carried.Move({114, 217, 0}) && carried.FinishMove(true) &&
          Same(carried.PlayerStart(), {120, 220, -3}), "an authored deck start rides too");
    Check(carried.Undo() && Same(carried.PlayerStart(), {110, 210, -3}) && Same(carried.Object(0)->position, {104, 207, 0}),
          "undo the second ride");
    Check(carried.Select(0) && carried.BeginMove(true) && carried.Move({104, 207, 0}) && carried.FinishMove(true) &&
          Same(carried.PlayerStart(), {110, 210, -3}), "a move back to the same place changes nothing");
    CeEditorState startOnly;
    Check(startOnly.InitializePlayerStart(original) && startOnly.Seal() && startOnly.SetPlayerStart(authored),
          "a map with a primary start but no named actors remains editable");
}
void Documents() {
    char error[256]{};
    CeProjectPosition carrier{"carrier", 4, 7, 0, -1};
    CeProjectPlayerStart start{true, 31, 46, -9};
    CeProjectDocument* document = PortCeProjectDocumentNew(221, "Authored start", error, sizeof(error));
    Check(document && PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error)), "legacy placement document");
    const std::string legacy = Write(document);
    Check(!PortCeProjectDocumentPlayerStart(document).enabled, "legacy has no start override");
    Check(PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error), nullptr, 0, &start), "add v7 start without objectives");
    const std::string saved = Write(document);
    Check(saved.find("\"version\":7") != std::string::npos, "start explicitly upgrades version");
    CeProjectDocument* opened = PortCeProjectDocumentRead(saved.data(), saved.size(), error, sizeof(error));
    const CeProjectPlayerStart loaded = PortCeProjectDocumentPlayerStart(opened);
    Check(opened && loaded.enabled && loaded.x == start.x && loaded.y == start.y && loaded.z == start.z, "reopen exact authored start");
    Check(Write(opened) == saved, "start project round trips unchanged");
    carrier.x = 400;
    Check(PortCeProjectDocumentUpdate(opened, &carrier, 1, error, sizeof(error)), "ordinary object update retains optional start");
    Check(PortCeProjectDocumentPlayerStart(opened).x == start.x, "carrier document edit does not drag start");
    const std::string beforeInvalid = Write(opened);
    CeProjectPlayerStart invalid{true, std::numeric_limits<float>::infinity(), 0, 0};
    Check(!PortCeProjectDocumentUpdate(opened, &carrier, 1, error, sizeof(error), nullptr, 0, &invalid), "reject nonfinite authored start");
    Check(Write(opened) == beforeInvalid, "invalid start is an atomic refusal");
    CeProjectPlayerStart disabled{};
    Check(PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error), nullptr, 0, &disabled), "remove optional start");
    Check(!PortCeProjectDocumentPlayerStart(document).enabled, "removed start is absent");
    Check(Write(document).find("player_start") == std::string::npos, "no stale start payload");
    Check(Write(document).find("\"version\":2") != std::string::npos, "removing only new feature returns legacy schema");
    CeProjectDocument* legacyOpened = PortCeProjectDocumentRead(legacy.data(), legacy.size(), error, sizeof(error));
    Check(legacyOpened && Write(legacyOpened) == legacy, "old project bytes round trip unchanged");
    PortCeProjectDocumentDelete(legacyOpened);
    PortCeProjectDocumentDelete(opened);
    PortCeProjectDocumentDelete(document);
    document = PortCeProjectDocumentNew(221, "Start only", error, sizeof(error));
    Check(PortCeProjectDocumentUpdate(document, nullptr, 0, error, sizeof(error), nullptr, 0, &start),
          "a start-only project needs no invented object placement");
    const std::string startOnly = Write(document);
    opened = PortCeProjectDocumentRead(startOnly.data(), startOnly.size(), error, sizeof(error));
    Check(opened && PortCeProjectDocumentCount(opened) == 0 && PortCeProjectDocumentPlayerStart(opened).enabled,
          "start-only saved project reopens");
    PortCeProjectDocumentDelete(opened); PortCeProjectDocumentDelete(document);
    // Every older supported shape stays separate from the opt-in start schema.
    for (int version = 3; version <= 6; ++version) {
        document = PortCeProjectDocumentNew(221, "Compatibility", error, sizeof(error));
        carrier.side = 0;
        CeProjectReach goals[2]{};
        const size_t count = version == 3 ? 0 : version == 5 ? 2 : 1;
        for (size_t i = 0; i < count; ++i) { goals[i].enabled = true; goals[i].x = 10 + float(i) * 20; goals[i].z = -2; }
        if (version == 6) { goals[0].hold.seconds = 10; goals[0].hold.radius = 10; }
        Check(PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error), goals, count), "older supported document created");
        const std::string old = Write(document);
        Check(old.find("\"version\":" + std::to_string(version)) != std::string::npos, "compatibility fixture has intended version");
        Check(PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error), goals, count, &start), "add start alongside existing objective or side");
        const std::string upgraded = Write(document);
        opened = PortCeProjectDocumentRead(upgraded.data(), upgraded.size(), error, sizeof(error));
        Check(opened && Write(opened) == upgraded, "new format with existing features round trips");
        Check(PortCeProjectDocumentReach(opened, 0).enabled == (count > 0), "upgrade preserves objective availability");
        Check(PortCeProjectDocumentUpdate(document, &carrier, 1, error, sizeof(error), goals, count, &disabled), "remove start from upgraded shape");
        Check(Write(document) == old, "removing start restores exact older schema and metadata");
        PortCeProjectDocumentDelete(opened); PortCeProjectDocumentDelete(document);
    }
}
}
int main() {
    State(); Documents();
    float x = 1, y = 2, z = 3;
    Check(!PortCeProjectApplyPlayerStart(1, false, x, y, z) && x == 1 && y == 2 && z == 3,
          "ordinary launch preserves original native coordinates");
    editorActive = true;
    Check(!PortCeProjectApplyPlayerStart(2, false, x, y, z) && startDispatches == 0 && x == 1,
          "secondary player start never takes the solo override");
    Check(PortCeProjectApplyPlayerStart(1, false, x, y, z) && startDispatches == 1 && x == 31 && z == -9,
          "primary start receives authored initialization");
    Check(!PortCeProjectApplyPlayerStart(1, true, x, y, z) && startDispatches == 1,
          "inverted side does not target wrong start");
    Check(PortCeProjectApplyPlayerStart(2, true, x, y, z, true) && startDispatches == 2 && z == -90,
          "inverted primary and original flight state are forwarded");
    return failures ? 1 : 0;
}
