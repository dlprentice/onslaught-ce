// SPDX-License-Identifier: GPL-3.0-only
#include "ce_catalogue_menu.h"
#include <cstdio>
#include <cstring>
#include <string>

static const char* root;
void PortPathsNamesOnly() {}
const char* PortGameDir() { return root; }
static unsigned checks;
static void Check(bool value, const char* why) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static std::string Title(unsigned slot) { char b[128]; PortCeCatalogueTitle(slot,b,sizeof b); return b; }
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    root=argv[1]; PortCeCatalogueOpen(); PortCeCatalogueBrowse(1);
    Check(PortCeCatalogueCount(1)==39 && PortCeCataloguePageCount()==4,"all installed textures initially visible");
    PortCeCatalogueTurn(); PortCeCatalogueTurn();
    PortCeCatalogueSelect(4); const std::string chosen=Title(4);
    Check(chosen=="Textures: Panel 28","known authored item on third page");
    PortCeCatalogueFilter("pAnEl");
    Check(PortCeCatalogueMatches()==36,"case insensitive search across every page");
    Check(PortCeCataloguePageNumber()==3 && PortCeCatalogueSelectedSlot()==4 && Title(4)==chosen,"selection identity retained by filter");
    PortCeCatalogueFilter("panel 28");
    Check(PortCeCatalogueMatches()==1 && PortCeCatalogueVisible()==1 && PortCeCataloguePageCount()==1,"spaces find filename underscores");
    Check(Title(0)==chosen,"exact item survives sparse results");
    PortCeCatalogueFilter("not present");
    Check(PortCeCatalogueVisible()==0 && PortCeCatalogueMatches()==0 && PortCeCataloguePageNumber()==1,"empty query result has no phantom rows");
    Check(Title(0).empty() && Title(500).empty(),"empty selection is bounded");
    PortCeCatalogueTurn();
    PortCeCatalogueFilter("");
    Check(PortCeCataloguePageNumber()==3 && PortCeCatalogueSelectedSlot()==4 && Title(4)==chosen,"clear after no-match returns exact identity and page");
    PortCeCatalogueFilter("panel 3");
    Check(PortCeCatalogueMatches()==6,"query narrows to filename words");
    PortCeCatalogueSelect(4); const std::string later=Title(4);
    PortCeCatalogueBrowse(0);
    Check(PortCeCatalogueCount(0)==3 && PortCeCatalogueMatches()==3 && !*PortCeCatalogueQuery(),"model collection independently unfiltered");
    PortCeCatalogueFilter("scout");
    Check(PortCeCatalogueMatches()==1 && Title(0)=="Models: Scout","native model search");
    PortCeCatalogueBrowse(1);
    Check(std::string(PortCeCatalogueQuery())=="panel 3" && PortCeCatalogueSelectedSlot()==4 && Title(4)==later,"collection return keeps its own query and selected asset");
    char detail[1024];PortCeCatalogueDetails(4,detail,sizeof detail);
    Check(std::strncmp(detail,"Panel 34\n",9)==0,"details correspond to selected filtered source");
    PortCeCatalogueFilter("reticle.tga(1)");
    Check(PortCeCatalogueMatches()==1,"stored full filename is searchable");
    Check(PortCeCatalogueIsTexture(0) && !PortCeCatalogueIsTexture(1),"filtered detail slots bounded");
    PortCeCatalogueFilter("x");
    PortCeCatalogueFilter("                                   ");
    Check(PortCeCatalogueMatches()==39 && !*PortCeCatalogueQuery(),"whitespace clears the filter");
    PortCeCatalogueFilter(std::string(80, 'x').c_str());
    Check(std::strlen(PortCeCatalogueQuery())==CE_CATALOGUE_QUERY_MAX,"typed length bounded");
    PortCeCatalogueClose();PortCeCatalogueOpen();PortCeCatalogueBrowse(1);
    Check(PortCeCataloguePageNumber()==1 && !*PortCeCatalogueQuery() && PortCeCatalogueMatches()==39,"leave and reopen snapshot resets only transient search");
    PortCeCatalogueClose();
    std::printf("%u/%u catalogue search and identity checks passed\n",checks,checks);return 0;
}
