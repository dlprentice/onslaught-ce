// SPDX-License-Identifier: GPL-3.0-only
// Genuine native monitor/reader/list implementations with only allocation/log boundaries replaced.
#include "common.h"
#include "DebugLog.h"
#define SIMPLE_MESHES
#include NATIVE_SPTRSET
#include NATIVE_MONITOR
#include NATIVE_READER
#undef printf
static unsigned allocations,frees,checks;
CMemoryHeap::CMemoryHeap() {}
CMemoryHeap::~CMemoryHeap() {}
CDXMemoryManager::CDXMemoryManager() {}
CDXMemoryManager::~CDXMemoryManager() {}
void *CDXMemoryManager::Alloc(UINT size,EMemoryType,char *,UINT) { ++allocations; return malloc(size); }
void CDXMemoryManager::Free(void *p) { if(p) ++frees; free(p); }
CDXMemoryManager MEM_MANAGER;
CDebugLog LOG;
void CDebugLog::AddMessage(char *,...) {}
static void Check(bool ok) { ++checks; if(!ok) exit(2); }
int main() {
    GenericSPtrSet::Init(32);
    for(unsigned iteration=0;iteration<2048;++iteration) {
        CMonitor first,second;
        CActiveReader<CMonitor> a(&first),b(&first);
        a.SetReader(&second); first.Shutdown();
        Check(b.ToRead()==NULL&&a.ToRead()==&second);
        second.Shutdown(); Check(a.ToRead()==NULL);
    }
    GenericSPtrSet::Shutdown(); Check(allocations==frees);
    printf("{\"native_reader_checks\":%u,\"allocations\":%u,\"frees\":%u,\"actors_created\":false}\n",checks,allocations,frees);
}
