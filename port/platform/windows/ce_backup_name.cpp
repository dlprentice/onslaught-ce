// The Windows memory card already provides the selected UTF-16 career name.
#include "../common/ce_backup_name.h"
#include "../common/save_names.h"
#include <cstring>

bool CeBackupSelectedName(const void *selection,char *file,unsigned capacity,void *display)
{
    char path[256];
    if (!selection || !file || !capacity || !display || PortSavePathUtf8(selection,path,sizeof(path)))
        return false;
    const char *leaf=std::strrchr(path,'\\');
    if (!leaf || std::strlen(++leaf)>=capacity || PortSaveNameUtf16(leaf,display,32))
        return false;
    std::memcpy(file,leaf,std::strlen(leaf)+1);
    return true;
}
