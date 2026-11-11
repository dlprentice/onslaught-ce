// SPDX-License-Identifier: GPL-3.0-only
// Exact admitted native guard fragments, actual automatic-script/Refuse bodies and actual abort bridge.
// The surrounding load stage and compatibility providers remain controlled, not a complete game or file proof.
#define main existing_session_main
#include "network_session_driver.cpp"
#undef main
// The campaign state a refusal now clears lives in ce_network_boot.cpp, which this driver does not link: nothing to clear here.
extern "C" void CE_BootCampaignClear(void){}
namespace BootRefusalFixture {
static int prepared=1;
static char failure[192];
#include BOOT_REFUSE_SOURCE
}
using BootRefusalFixture::Refuse;
#include BOOT_AUTOMATIC_SOURCE
#include LOAD_ADMISSION_SOURCE

static void CallerCase(const std::string &mode) {
    bool offline=mode=="caller-offline";
    if(!offline)Configure(true);
    unsetenv("BEA_NET_COMPAT");
    Check(CE_SessionConfigureLevel(offline?100:852),"caller fixture begins with admitted native level configuration");
    if(offline)s.peer_epoch_loaded=true;else host_tick=57;
    boundary_refuse_at=mode=="caller-afterload-refuse"?1:(mode=="caller-prerun-refuse"||mode=="caller-prerun-native-close")?3:
        mode=="caller-script-refuse"||mode=="caller-script-native-close"?2:0;
    if(mode=="caller-script-native-close"||mode=="caller-prerun-native-close")boundary_quit_on_refusal=QT_QUIT_TO_SYSTEM;
    if(mode=="caller-afterload-native-close")CE_SessionGameSetQuit(QT_QUIT_TO_SYSTEM);
    GAME.mPlayer[0]=new int(7);GAME.mCurrentCamera[0]=new int(9);
    int result=NativeLoadAdmission();
    if(boundary_refuse_at||mode=="caller-afterload-native-close") {
        int expected=mode.find("native-close")!=std::string::npos?QT_QUIT_TO_SYSTEM:QT_LOAD_ERROR;
        Check(result==expected&&GAME.mQuit==expected,"native abort site returns the actual bridge quit result");
        Check(s.stage==DRAINING&&!GAME.mPlayer[0]&&!GAME.mCurrentCamera[0],"actual abort bridge retires admission and native player/camera objects");
        Check(!pre_runs&&!captures&&!applied&&!automatic_execs,"native refusal cannot fall through into script execution, PreRun or baseline effects");
        if(mode=="caller-afterload-refuse"||mode=="caller-afterload-native-close") {
            Check(!begun&&!boot_refusals,"after-load abort precedes the automatic-script and world-start stages");
        }else if(mode=="caller-prerun-refuse"||mode=="caller-prerun-native-close") {
            Check(begun==1&&!boot_refusals&&admission_boundaries==3,"pre-run refusal follows admitted world and nonexecuted automatic-script stage");
        }else {
            Check(begun==1&&boot_refusals==1&&BootRefusalFixture::prepared==0&&admission_boundaries==2,
                "actual automatic-script refusal invalidates its prepared proof before aborting PreRun");
        }
    } else {
        Check(result==QT_NONE&&GAME.mQuit==QT_NONE&&pre_runs==1&&updates==3,"normal native guards preserve genuine PreRun");
        Check(automatic_execs==(offline?1u:0u),"automatic script executes only on the ordinary offline route");
        Check(admission_boundaries==(offline?0u:3u),"native guard execution keeps load then script then pre-run ordering");
        delete GAME.mPlayer[0];delete GAME.mCurrentCamera[0];GAME.mPlayer[0]=GAME.mCurrentCamera[0]=nullptr;
    }
    Finish();
}
int main(int argc,char**argv) {
    if(argc!=2)return 1;CallerCase(argv[1]);
    printf("{\"case\":\"%s\",\"checks\":%u,\"boot_refusals\":%u,\"script_execs\":%u,\"game_or_devices_tested\":false}\n",argv[1],checks,boot_refusals,automatic_execs);return 0;
}
