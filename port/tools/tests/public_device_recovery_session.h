// SPDX-License-Identifier: GPL-3.0-only
// The actual session service/active functions run over bounded authored dependency state, not sockets or a world.
#include "../../game/ce_network_session.h"
#include "public_device_recovery_session_types.h"
struct ServiceObservation { int ready,device,result; };
std::vector<ServiceObservation> serviceObservations;
unsigned peekReads,getReads;
int serviceQuit;
bool lockstepActive;
extern "C" int CE_LockstepActive(void){return lockstepActive?1:0;}
int CE_SessionGameQuitCode(){return serviceQuit?serviceQuit:quitCount?QT_QUIT_TO_FRONTEND:QT_NONE;}
void CE_SessionGameSetQuit(int quit){serviceQuit=quit;}
#include "public_device_recovery_service.h"
int CE_SessionSystemService(int ready,int device)
{
    if(!CE_SessionActive())throw std::runtime_error("offline shell entered network service");
    int result=ActualSessionSystemService(ready,device);
    serviceObservations.push_back({ready,device,result});return result;
}
