// SPDX-License-Identifier: GPL-3.0-only
#include "ce_editor_storage.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
static const char* userRoot;
const char* PortUserDir() { return userRoot; }
int main(int argc,char** argv) {
    if(argc<3)return 2;userRoot=argv[1];char error[240];
    if(!std::strcmp(argv[2],"list")) {
        CeEditorFile files[256];int count=PortCeEditorFiles(files,argc>3?std::atoi(argv[3]):256,error,sizeof(error));
        if(count<0){std::puts("REFUSED");return 1;}
        std::printf("COUNT %d\n",count);for(int i=0;i<count;++i)std::puts(files[i].name);return 0;
    }
    if(argc<4)return 2;
    if(!std::strcmp(argv[2],"read")) {
        size_t size=0;char* bytes=PortCeEditorRead(argv[3],&size,error,sizeof(error));
        if(!bytes){std::puts("REFUSED");return 1;}
        std::printf("SIZE %zu\n",size);if(argc>4){std::ofstream out(argv[4],std::ios::binary);out.write(bytes,size);}
        PortCeEditorFileFree(bytes);return 0;
    }
    if(!std::strcmp(argv[2],"save")) {
        std::ifstream input(argv[3],std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});char name[128];
        if(!PortCeEditorSave(argc>4?std::atoi(argv[4]):777,bytes.data(),bytes.size(),name,sizeof(name),error,sizeof(error))){std::puts("REFUSED");return 1;}
        std::puts(name);return 0;
    }
    return 2;
}
