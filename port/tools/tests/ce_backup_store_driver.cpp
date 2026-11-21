// SPDX-License-Identifier: GPL-3.0-only
#include "../../platform/common/ce_career_backup.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char **argv) {
    if(argc!=7)return 2;
    FILE *f=fopen(argv[6],"rb"); if(!f)return 3;
    std::vector<unsigned char> bytes(10005);
    size_t n=fread(bytes.data(),1,bytes.size(),f);if(ferror(f)||fclose(f))return 3;
    const char *reason=nullptr;char location[4352];
    bool ok=CeBackupStore({argv[1],argv[2],argv[3],argv[4],argv[5],bytes.data(),n},location,sizeof(location),reason);
    if(ok)printf("%s\n",location);
    else if(reason)fprintf(stderr,"%s\n",reason);
    return ok?0:1;
}
