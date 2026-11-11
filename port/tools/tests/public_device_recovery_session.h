// SPDX-License-Identifier: GPL-3.0-only
// The actual session service/active functions run over bounded authored dependency state, not sockets or a world.
#include "../../game/ce_network_session.h"
#include "public_device_recovery_session_types.h"
struct RecoveryServiceState
{
    Stage stage=OFF;
    WaitPhase wait_phase=WAIT_NONE;
    bool requested=false,device_unready=false,service_ready_seen=false,device_ready_seen=false;
    bool local_blocked=false,native_unready=false,host=false,shared_wait=false;
} s;
struct ServiceObservation { int ready,device,result; };
std::vector<ServiceObservation> serviceObservations;
unsigned servicePolls,serviceAvailability,serviceWaits,serviceFailures,peekReads,getReads;
int serviceQuit;
uint32_t serviceWindowReasons;
bool serviceAdmission=true,servicePollResult=true,serviceDriveResult=true;
bool serviceRepair=false,serviceRepairReady=false,serviceRepairGate=true;
bool serviceTerminal=false,serviceLifecycleExpired=false,serviceStateExpired=false;
uint64_t serviceNow;
int CE_SessionGameQuitCode(){return serviceQuit?serviceQuit:quitCount?QT_QUIT_TO_FRONTEND:QT_NONE;}
void CE_SessionGameSetQuit(int quit){serviceQuit=quit;}
uint32_t CE_SessionWindowStatus(){return serviceWindowReasons;}
uint64_t CE_NetNowMs(){return serviceNow;}
bool AdmissionCurrent(){return serviceAdmission;}
void RuntimeFailure(){++serviceFailures;if(!CE_SessionGameQuitCode())CE_SessionGameSetQuit(QT_USER_QUIT_TO_FRONTEND);}
bool TerminalTransport(){return serviceTerminal;}
bool LifecycleExpired(uint64_t){return serviceLifecycleExpired;}
bool StateExpired(uint64_t){return serviceStateExpired;}
bool DriveAvailability(){++serviceAvailability;return serviceDriveResult;}
bool DriveWait(){++serviceWaits;return serviceDriveResult;}
bool Poll(){++servicePolls;return servicePollResult;}
bool RepairHold(){return serviceRepair;}
bool RepairLocalReady(){return serviceRepairReady;}
bool HostUnavailable(){return serviceWindowReasons!=0||s.native_unready||s.device_unready;}
bool RepairHeldGate(){return serviceRepairGate;}
bool Fail(const char *){++serviceFailures;s.stage=FAILED;return false;}
#include "public_device_recovery_service.h"
int CE_SessionSystemService(int ready,int device)
{
    if(!CE_SessionActive())throw std::runtime_error("offline shell entered network service");
    int result=ActualSessionSystemService(ready,device);
    serviceObservations.push_back({ready,device,result});return result;
}
