// SPDX-License-Identifier: GPL-3.0-only
#include "ce_project.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
const char* PortGameDir() { return "/unused-authored-game"; }
const char* PortUserDir() { return "/unused-authored-user"; }
static bool editorActive, museumActive;
extern "C" bool PortCeEditorActive(void) { return editorActive; }
extern "C" bool PortCeMuseumVisitActive(void) { return museumActive; }
static unsigned checks, failures;
static void Check(bool value) {
    ++checks; if (!value) { ++failures; std::fprintf(stderr,"editor document authored check %u failed\n",checks); }
}
static std::string Serialize(CeProjectDocument* document) {
    char error[200]; size_t n = PortCeProjectDocumentWrite(document,nullptr,0,error,sizeof(error));
    Check(n > 0); if (!n) return "";
    std::vector<char> bytes(n,'?');
    Check(PortCeProjectDocumentWrite(document,bytes.data(),bytes.size(),error,sizeof(error)) == n);
    Check(bytes.back() == 0); return std::string(bytes.data(),n-1);
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char error[200];
    Check(PortCeProjectCareerAllowed()); editorActive = true; Check(!PortCeProjectCareerAllowed());
    museumActive = true; Check(!PortCeProjectCareerAllowed()); editorActive = false; Check(!PortCeProjectCareerAllowed());
    museumActive = false; Check(PortCeProjectCareerAllowed());
    std::ifstream input(argv[1],std::ios::binary); std::string authored((std::istreambuf_iterator<char>(input)),{});
    CeProjectDocument* document = PortCeProjectDocumentRead(authored.data(),authored.size(),error,sizeof(error));
    if (!std::strcmp(argv[2],"--reject")) { Check(document == nullptr); PortCeProjectDocumentDelete(document); return failures ? 1 : 0; }
    Check(document != nullptr); if (!document) return 1;
    Check(PortCeProjectDocumentLevel(document) == 777 && PortCeProjectDocumentCount(document) == 1);
    CeProjectPosition value{}; Check(PortCeProjectDocumentPosition(document,0,&value));
    Check(!std::strcmp(value.source,"AuthoredAnchor") && value.x == 12.5f && value.y == -7.25f && value.z == -3);
    Check(!PortCeProjectDocumentPosition(document,1,&value));
    const std::string original = Serialize(document);
    CeProjectPosition changes[] = {{"AuthoredAnchor",42.25f,11.5f,-4},{"New object",8,6,-2}};
    Check(PortCeProjectDocumentUpdate(document,changes,2,error,sizeof(error)));
    Check(PortCeProjectDocumentCount(document) == 2);
    const std::string updated = Serialize(document);
    Check(updated != original);
    std::ofstream output(argv[2],std::ios::binary); output.write(updated.data(),updated.size()); output.close();
    CeProjectPosition duplicate[] = {{"AuthoredAnchor",1,2,3},{"AuthoredAnchor",7,8,9}};
    Check(!PortCeProjectDocumentUpdate(document,duplicate,2,error,sizeof(error)));
    Check(updated == Serialize(document));
    CeProjectPosition bad[] = {{"AuthoredAnchor",1,2,3},{"Bad position",std::numeric_limits<float>::quiet_NaN(),2,3}};
    Check(!PortCeProjectDocumentUpdate(document,bad,2,error,sizeof(error))); Check(updated == Serialize(document));
    bad[1].x = 1048577; Check(!PortCeProjectDocumentUpdate(document,bad,2,error,sizeof(error))); Check(updated == Serialize(document));
    bad[1] = CeProjectPosition{" \t",1,2,3};
    Check(!PortCeProjectDocumentUpdate(document,bad,2,error,sizeof(error))); Check(updated == Serialize(document));
    char small[5] = {'a','b','c','d','e'};
    Check(!PortCeProjectDocumentWrite(document,small,sizeof(small),error,sizeof(error))); Check(!std::memcmp(small,"abcde",5));
    Check(!PortCeProjectDocumentRead("{}",2,error,sizeof(error)));
    Check(!PortCeProjectDocumentRead(nullptr,1,error,sizeof(error)));
    Check(!PortCeProjectDocumentUpdate(document,nullptr,1,error,sizeof(error)));
    Check(!PortCeProjectDocumentUpdate(document,changes,0,error,sizeof(error)));
    PortCeProjectDocumentDelete(document);
    document = PortCeProjectDocumentNew(777,"Authored native project",error,sizeof(error)); Check(document != nullptr);
    Check(!PortCeProjectDocumentWrite(document,nullptr,0,error,sizeof(error)));
    Check(PortCeProjectDocumentUpdate(document,changes,2,error,sizeof(error)));
    const std::string fresh = Serialize(document);
    CeProjectDocument* roundtrip = PortCeProjectDocumentRead(fresh.data(),fresh.size(),error,sizeof(error));
    Check(roundtrip != nullptr && PortCeProjectDocumentCount(roundtrip) == 2);
    PortCeProjectDocumentDelete(roundtrip); PortCeProjectDocumentDelete(document);
    Check(!PortCeProjectDocumentNew(0,"bad",error,sizeof(error)));
    Check(!PortCeProjectDocumentNew(10000,"bad",error,sizeof(error)));
    Check(!PortCeProjectDocumentNew(777," ",error,sizeof(error)));
    Check(!PortCeProjectDocumentNew(777,nullptr,error,sizeof(error)));
    std::printf("native editor document: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
