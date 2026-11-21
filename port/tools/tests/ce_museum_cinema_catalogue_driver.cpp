#include "ce_museum_cinema_catalogue.h"
#include "ce_museum_cinema_view.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <limits>

namespace C = CeMuseumCinemaCatalogue;
struct Fixture {
    std::vector<std::string> names;
    std::vector<std::string> missing;
    std::vector<unsigned> rejected;
    std::vector<unsigned> opened;
};
static unsigned checked = 0, failed = 0;
static void Check(bool ok, const char *name) {
    ++checked;
    if (!ok) { ++failed; std::fprintf(stderr, "FAIL: %s\n", name); }
}
static bool Read(void *context, unsigned index, char *out, unsigned cap) {
    Fixture &f = *static_cast<Fixture *>(context);
    if (index >= f.names.size()) return false;
    std::snprintf(out, cap, "%s", f.names[index].c_str());
    return true;
}
static bool Exists(void *context, const char *stem) {
    const Fixture &f = *static_cast<Fixture *>(context);
    for (std::size_t i = 0; i < f.missing.size(); ++i)
        if (f.missing[i] == stem) return false;
    return true;
}
static bool Start(void *context, const C::Entry &entry) {
    Fixture &f = *static_cast<Fixture *>(context);
    f.opened.push_back(entry.sourceIndex);
    for (std::size_t i = 0; i < f.rejected.size(); ++i)
        if (f.rejected[i] == entry.sourceIndex) return false;
    return true;
}
static bool Unterminated(void *, unsigned, char *out, unsigned cap) {
    std::memset(out, 'x', cap);
    return true;
}
int main() {
    Fixture f;
    C::List list = C::Build(Read, Exists, &f, 512);
    Check(list.count == 0 && !list.truncated, "empty catalogue");
    Check(!C::At(list, 0) && C::Play(list, 0, true, Start, &f) == -1,
          "empty catalogue never opens media");
    f.names = {"missing_first", "first_present", "second_present"};
    f.missing = {"missing_first"};
    list = C::Build(Read, Exists, &f, 512);
    Check(list.count == 2 && !list.truncated && list.entries[0].sourceIndex == 1,
          "compact list preserves original nonzero source index");
    Check(std::strcmp(list.entries[1].title, "second present") == 0,
          "display title is safe and separate from file stem");
    Check(C::Play(list, 1, false, Start, &f) == 1 && f.opened.size() == 1 && f.opened[0] == 2,
          "visitor selection opens its exact original entry");
    f.opened.clear(); f.rejected = {1};
    Check(C::Play(list, 0, true, Start, &f) == 1 && f.opened.size() == 2 &&
          f.opened[0] == 1 && f.opened[1] == 2, "initial play finds first playable in catalogue order");
    f.opened.clear();
    Check(C::Play(list, 0, false, Start, &f) == -1 && f.opened.size() == 1 && f.opened[0] == 1,
          "explicit unavailable selection does not play something else");
    f.opened.clear();
    Check(C::Play(list, -1, true, Start, &f) == -1 &&
          C::Play(list, 2, false, Start, &f) == -1 && f.opened.empty(),
          "invalid selection never reaches decoder");
    Check(!C::At(list, -1) && !C::At(list, 2), "selection bounds");
    f.names = {"../escape", "bad\\path", "bad:stream", ".", "..", "", "safe"};
    f.missing.clear(); f.rejected.clear();
    list = C::Build(Read, Exists, &f, 512);
    Check(list.count == 1 && list.entries[0].sourceIndex == 6, "unsafe stems never reach catalogue");
    list = C::Build(Unterminated, Exists, &f, 2);
    Check(list.count == 0 && list.truncated, "unterminated reader and bounded enumeration");
    f.names = {"single"}; list = C::Build(Read, Exists, &f, 512);
    Check(list.count == 1 && C::Play(list, 0, false, Start, &f) == 0, "one-film playback");
    f.names.assign(C::kCapacity + 1, "present"); list = C::Build(Read, Exists, &f, 512);
    Check(list.count == C::kCapacity && list.truncated, "catalogue capacity cannot overflow");
    Check(!C::At(list, C::kCapacity), "capacity index refusal");
    char title[8]; std::memset(title, '!', sizeof(title));
    C::DisplayTitle("  a__very_long_film  ", 0, title, sizeof(title));
    Check(title[sizeof(title)-1] == 0 && std::strlen(title) < sizeof(title), "bounded long title");
    C::DisplayTitle("___", 4, title, sizeof(title));
    Check(std::strcmp(title, "Film 5") == 0, "empty display fallback");
    C::DisplayTitle("film", 0, 0, 0);
    Check(C::Build(0, Exists, &f, 512).count == 0 &&
          C::Build(Read, 0, &f, 512).count == 0 &&
          C::Build(Read, Exists, &f, 0).count == 0, "missing host callbacks fail closed");
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, -3,-4,-5,1};
    float eye[3] = {}, aim[3] = {};
    Check(CeMuseumCinemaView::CentreRay(view, eye, aim) && eye[0] == 3 && eye[1] == 4 &&
          eye[2] == 5 && aim[0] == 0 && aim[1] == 0 && aim[2] == 1,
          "actual translated view centre ray");
    const float turned[16] = {0,0,1,0, 1,0,0,0, 0,1,0,0, -4,-5,-3,1};
    Check(CeMuseumCinemaView::CentreRay(turned, eye, aim) && eye[0] == 3 && eye[1] == 4 &&
          eye[2] == 5 && aim[0] == 1 && aim[1] == 0 && aim[2] == 0,
          "rotated view inverse and exact centre direction");
    view[0] = 0;
    Check(!CeMuseumCinemaView::CentreRay(view, eye, aim), "degenerate camera basis refused");
    view[0] = 1; view[4] = 1;
    Check(!CeMuseumCinemaView::CentreRay(view, eye, aim), "nonorthogonal camera basis refused");
    view[4] = 0; view[3] = 1;
    Check(!CeMuseumCinemaView::CentreRay(view, eye, aim), "nonaffine camera matrix refused");
    view[3] = 0; view[12] = std::numeric_limits<float>::quiet_NaN();
    Check(!CeMuseumCinemaView::CentreRay(view, eye, aim), "nonfinite camera matrix refused");
    Check(!CeMuseumCinemaView::CentreRay(0, eye, aim) &&
          !CeMuseumCinemaView::CentreRay(turned, 0, aim) &&
          !CeMuseumCinemaView::CentreRay(turned, eye, 0), "missing view output refused");
    std::printf("cinema catalogue: %u checks, %u failures\n", checked, failed);
    return failed ? 1 : 0;
}
