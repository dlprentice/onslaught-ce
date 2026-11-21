// SPDX-License-Identifier: GPL-3.0-only
#include "../common/ce_backup_name.h"
#include "../common/save_names.h"
#include <cstring>
#include <stdint.h>

// The Linux card first decodes UTF-8 names representable by its legacy character
// set. Other UTF-8 names appear as the original bytes widened one by one.
// Recover only names that round-trip from that exact list entry.
extern "C" unsigned PortSaveNameFromFileCp1252(const char *,void *,unsigned);
extern "C" unsigned PortSaveNameFromLegacyCp1252(const char *,void *,unsigned);
extern "C" int PortSaveLegacyFromNameCp1252(const void *,char *,unsigned);
extern "C" int PortSaveFileFromNameCp1252(const void *,char *,unsigned);
namespace {
bool equal(const uint16_t *a,const uint16_t *b) {
    for(unsigned i=0;i<256;++i) { if(a[i]!=b[i])return false; if(!a[i])return true; }
    return false;
}
}
bool CeBackupSelectedName(const void *selection,char *file,unsigned capacity,void *display) {
    if(!selection||!file||capacity<5||!display)return false;
    file[0]=0;
    const uint16_t *selected=static_cast<const uint16_t *>(selection);
    char legacy[256]={}, utf8[256]={};uint16_t round[256]={}, decoded[256]={};
    bool native=false;
    if(PortSaveLegacyFromNameCp1252(selected,legacy,sizeof(legacy)) &&
       !PortSaveNameFromFileCp1252(legacy,round,256) &&
       PortSaveNameFromLegacyCp1252(legacy,round,256) && equal(round,selected)) {
        size_t n=strlen(legacy);
        if(n+5<=sizeof(utf8)) {
            memcpy(utf8,legacy,n);memcpy(utf8+n,".bes",5);
            native=PortSaveNameUtf16(utf8,decoded,256)==0;
        }
    }
    if(!native) {
        char path[256];if(PortSavePathUtf8(selection,path,sizeof(path)))return false;
        const char *base=strrchr(path,'\\');if(!base || strlen(base+1)>=sizeof(utf8))return false;
        strcpy(utf8,base+1);if(PortSaveNameUtf16(utf8,decoded,256))return false;
    }
    const size_t n=strlen(utf8);
    if(n>=capacity)return false;
    memcpy(file,utf8,n+1);memcpy(display,decoded,sizeof(decoded));return true;
}
