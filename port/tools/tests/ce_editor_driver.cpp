// SPDX-License-Identifier: GPL-3.0-only
#include "ce_editor.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
unsigned checks = 0, failures = 0;
void Check(bool ok) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "editor authored check %u failed\n", checks); }
}
bool Equal(CeEditorPoint a, CeEditorPoint b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
const CeEditorPoint start{3, 5, -7};
const CeEditorPoint moved{13, 15, -17};
void Fresh(CeEditorState& state) {
    state.Reset(); Check(state.Add("placed object", start, 7)); Check(state.Seal()); Check(state.Select(0));
}
void Collection() {
    CeEditorState state;
    Check(!state.Seal()); Check(!state.Select(0)); Check(!state.BeginMove()); Check(!state.Dirty());
    Check(state.Add("first", start, 7)); Check(!state.Select(0));
    Check(state.Add("second", moved, 8)); Check(state.Seal()); Check(state.Seal());
    Check(!state.Add("later", start, 9)); Check(state.Count() == 2);
    Check(state.Cycle(1) && state.Selected() == 0); Check(state.Cycle(1) && state.Selected() == 1);
    Check(state.Cycle(1) && state.Selected() == 0); Check(state.Cycle(-1) && state.Selected() == 1);
    Check(!state.Cycle(0)); Check(!state.Cycle(2)); Check(!state.Select(-2)); Check(!state.Select(2));
    Check(state.Select(-1)); Check(state.Cycle(-1) && state.Selected() == 1);
    Check(state.Object(-1) == 0 && state.Object(2) == 0);
    Check(state.Object(0)->identity == 7 && !std::strcmp(state.Object(0)->source, "first"));

    for (int reverse = 0; reverse != 2; ++reverse) {
        state.Reset();
        Check(state.Add("same", reverse ? moved : start, 1));
        Check(state.Add("unique", start, 2));
        Check(state.Add("same", reverse ? start : moved, 3));
        Check(state.Seal()); Check(state.Ambiguous() == 2);
        Check(!state.Select(0) && !state.Select(2)); Check(state.Cycle(1) && state.Selected() == 1);
    }
    state.Reset(); Check(state.Add("same", start, 1)); Check(state.Add("same", start, 2));
    Check(!state.Seal()); Check(!state.Select(0));

    const char* invalid[] = {0, "", " ", "\tbad", "bad\n", "\177", "\200"};
    for (unsigned i = 0; i != sizeof(invalid)/sizeof(*invalid); ++i) {
        state.Reset(); Check(state.Add("valid", start, 1)); Check(!state.Add(invalid[i], start, 2));
        Check(!state.Seal()); Check(!state.Select(0));
    }
    char name[97]; std::memset(name, 'x', sizeof(name)); name[96] = 0;
    state.Reset(); Check(!state.Add(name, start, 1)); Check(!state.Seal());
    name[95] = 0; state.Reset(); Check(state.Add(name, start, 1)); Check(state.Seal());
    const float values[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), 1048577.0f, -1048577.0f};
    for (unsigned i = 0; i != sizeof(values)/sizeof(*values); ++i) for (int axis = 0; axis != 3; ++axis) {
        CeEditorPoint point = start; (axis == 0 ? point.x : axis == 1 ? point.y : point.z) = values[i];
        Check(!CeEditorState::Valid(point)); state.Reset(); Check(state.Add("same", start, 1));
        Check(!state.Add("same", point, 2)); Check(!state.Seal()); Check(!state.Select(0));
    }
    Check(CeEditorState::Valid(CeEditorPoint{1048576, -1048576, 0}));
    state.Reset();
    for (int i = 0; i != CeEditorState::ObjectLimit; ++i) {
        std::snprintf(name, sizeof(name), "instance-%d", i); Check(state.Add(name, start, unsigned(i)));
    }
    Check(!state.Add("overflow", start, 1)); Check(!state.Seal());
}
void History() {
    CeEditorState state; Fresh(state);
    Check(!state.Move(moved)); Check(!state.FinishMove(true)); Check(!state.Undo()); Check(!state.Redo());
    Check(state.BeginMove()); Check(!state.BeginMove()); Check(!state.Select(-1)); Check(!state.Cycle(1));
    Check(!state.Undo() && !state.Redo()); Check(state.Move(moved)); Check(state.Dirty());
    state.MarkSaved(); Check(state.Dirty()); Check(state.FinishMove(false));
    Check(Equal(state.Object(0)->position, start) && !state.Dirty()); Check(!state.Undo());
    Check(state.BeginMove()); Check(state.Move(start)); Check(state.FinishMove(true)); Check(!state.Undo());
    Check(state.BeginMove()); Check(state.Move(moved)); Check(state.Move(CeEditorPoint{4, 6, -8}));
    Check(state.FinishMove(true)); Check(state.Undo()); Check(Equal(state.Object(0)->position, start));
    Check(!state.Undo()); Check(state.Redo()); Check(Equal(state.Object(0)->position, CeEditorPoint{4, 6, -8}));
    state.MarkSaved(); Check(!state.Dirty()); Check(state.Undo()); Check(state.Dirty());
    Check(state.Redo()); Check(!state.Dirty());
    Check(state.Undo()); Check(state.BeginMove()); Check(state.Move(moved)); Check(state.FinishMove(true));
    Check(!state.Redo()); Check(state.Undo()); Check(Equal(state.Object(0)->position, start));
    Check(!state.Undo()); // the abandoned redo branch must not survive behind the new move
    Check(state.Redo()); Check(Equal(state.Object(0)->position, moved));

    Fresh(state);
    Check(state.BeginMove());
    Check(!state.Move(CeEditorPoint{std::numeric_limits<float>::quiet_NaN(), 2, 3}));
    Check(Equal(state.Object(0)->position, start)); Check(state.FinishMove(false));
    for (int i = 1; i <= CeEditorState::HistoryLimit + 10; ++i) {
        Check(state.BeginMove()); Check(state.Move(CeEditorPoint{float(i), 0, 0})); Check(state.FinishMove(true));
    }
    for (int i = 0; i != CeEditorState::HistoryLimit; ++i) Check(state.Undo());
    Check(!state.Undo()); Check(Equal(state.Object(0)->position, CeEditorPoint{10, 0, 0}));
    for (int i = 0; i != CeEditorState::HistoryLimit; ++i) Check(state.Redo());
    Check(!state.Redo()); Check(Equal(state.Object(0)->position, CeEditorPoint{float(CeEditorState::HistoryLimit + 10), 0, 0}));
    state.Reset(); Check(state.Count() == 0 && state.Selected() == -1 && !state.Moving() && !state.Sealed());
    Check(!state.Undo() && !state.Redo() && !state.Dirty());
}
void Rays() {
    CeEditorPoint point{77, 88, 99};
    Check(CeEditorRayPlane(CeEditorPoint{0, 0, -10}, CeEditorPoint{1, 2, 2}, 0, &point));
    Check(Equal(point, CeEditorPoint{5, 10, 0}));
    Check(CeEditorRayPlane(CeEditorPoint{0, 0, 10}, CeEditorPoint{1, 2, -2}, 0, &point));
    Check(Equal(point, CeEditorPoint{5, 10, 0}));
    Check(CeEditorRayPlane(CeEditorPoint{0, 0, 0}, CeEditorPoint{1, 2, 1}, 0, &point));
    Check(Equal(point, CeEditorPoint{0, 0, 0}));
    const CeEditorPoint invalid[] = {{1, 1, 0}, {1, 1, 0.0000001f}, {1, 1, -1},
        {1048576, 1048576, 0.000001f}, {0, 0, std::numeric_limits<float>::quiet_NaN()}};
    for (unsigned i = 0; i != sizeof(invalid)/sizeof(*invalid); ++i) {
        point = CeEditorPoint{77, 88, 99};
        Check(!CeEditorRayPlane(CeEditorPoint{0, 0, -10}, invalid[i], 0, &point));
        Check(Equal(point, CeEditorPoint{77, 88, 99}));
    }
    Check(!CeEditorRayPlane(start, moved, 0, 0));
    Check(!CeEditorRayPlane(start, moved, std::numeric_limits<float>::infinity(), &point));
}
}
int main() {
    Collection(); History(); Rays();
    std::printf("native editor authored state: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
