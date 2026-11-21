// Real native pointer callback and real controller; display/input facts are fakes.
#define main dispatch_control_main
#define RefreshUnitFrame DispatchRefreshUnitFrame
#include "ce_cmdunit_consumer_driver.cpp"
#undef RefreshUnitFrame
#undef main
#include "ce_cmdpointer.h"
using WORD=unsigned;
static const int kMouseDown=1,kMouseUp=2;
static const int DIK_LSHIFT=0,DIK_RSHIFT=1,DIK_LCONTROL=2,DIK_RCONTROL=3;
static bool gDown[4],gBoxing,gStrategicDragging,gArmed,gUnitPointerAvailable;
static float MOUSE_X=20,MOUSE_Y=20,gBoxX,gBoxY,gStrategicDragX,gStrategicDragY,gStrategicWheel;
static int gStrategicClickOrder,physical[3],gSelected;
static struct {float s=1;} L;
static struct {bool valid;} gStrategicUi;
static struct {int n;} gSel;
static CeCmdPointerFence gUnitPointerFence;
static const int kFactoryFocus=-2;
static const int CE_FACTORY_READY=2;
struct CeWorldViewSample {};
struct CeFactoryView {float x,y;};
struct CeViewPoint {float x,y,z;};
static int PortMouseHeldNow(unsigned b){return physical[b];}
static bool StrategicUiReady(){return usable;}
static void CeHudReservationReset(){}
static bool StrategicWorldArea(float x,float y){return x>=0&&x<400&&y>=0&&y<90;}
static int FactoryCardAt(float,float){return -1;}
static bool CeFactorySelect(){return false;}
static void SelectOnly(int n){gSelected=n;gSel.n=n<0?0:1;}
static void StartStrategicFocus(const CeCmdSquad&){}
static void CeFactoryQueue(){}
static void CeFactoryCancel(){}
static void CeFactoryLeaveSelection(){}
int CeCmdDeliveredSquad(){return -1;}
static bool CePlayUnitCanTakeDelivery(){return false;}
static void CePlayUnitTakeDelivery(){}
static bool StrategicViewRead(CeWorldViewSample*){return true;}
static bool CeFactoryRead(CeFactoryView&){return false;}
static bool StrategicPixel(const CeWorldViewSample&,float,float,CeViewPoint*){return false;}
static bool StrategicGround(const CeWorldViewSample&,float x,float y,CeViewPoint* p){*p={x,y,0};return true;}
static int StrategicButtonAt(float,float){return -1;}
static void StrategicArm(int){}
static void Order(int,float,float){++groups;}
static UnitPanel::TextSize Measure(void*,const char* text,float scale){return {float(std::strlen(text))*5*scale,10*scale};}
static void RefreshUnitFrame(){DispatchRefreshUnitFrame();UnitPanel::LayoutInput l={};l.safe={0,100,400,250};l.scale=1;l.measure=Measure;gUnitUi.UpdateLayout(l);}
// The map shows a refused click beside the pointer and lights an order button for a moment: stand-ins (this driver judges the gestures only).
static uint64_t gButtonPressedNs[4];
static void NoteHere(const char* s, float, float){Note(s);}
#include "pointer-under-test.inc"
static void PointerReset(){Reset();physical[0]=physical[1]=physical[2]=0;gDown[0]=gDown[1]=gDown[2]=gDown[3]=false;ResetStrategicGesture();UnitPointerSeed();RefreshUnitFrame();}
static void Edge(unsigned button,int event,float x=20,float y=20){MOUSE_X=x;MOUSE_Y=y;StrategicPointer(button,event);}
int main(){
 PointerReset();physical[0]=1;UnitPointerSeed();Edge(0,kMouseDown);Edge(0,kMouseUp);Edge(0,kMouseDown);Edge(0,kMouseUp);
 Check(gUnitUi.SelectionCount()==0,"held entry cannot be released by queued edges");
 physical[0]=0;UnitPointerObserve();Edge(0,kMouseDown);Edge(0,kMouseUp);
 Check(gUnitUi.SelectionCount()==1&&gUnitUi.IsSelected(live[0].key),"fresh release then press selects through actual callback");
 PointerReset();physical[0]=1;UnitPointerSeed();physical[0]=0;UnitPointerObserve();Edge(0,kMouseDown);Edge(0,kMouseUp);
 Check(gUnitUi.SelectionCount()==1,"sampled release clears suppression without a release event");
 PointerReset();Edge(0,kMouseDown);gUnitUi.PointerMove(150,30);UnitPanel::Rect drag={};Check(gUnitUi.DragRect(&drag),"owned controller drag exists before focus loss");physical[1]=-1;UnitPointerObserve();
 Check(!gUnitUi.DragRect(&drag),"focus loss cancels owned controller drag");
 Check(!gUnitPointerAvailable&&!gStrategicDragging&&!gBoxing,"unavailable sample cancels native gestures");
 Edge(0,kMouseUp);Edge(0,kMouseDown,60,20);Edge(0,kMouseUp,60,20);
 Check(gUnitUi.SelectionCount()==0,"focus-loss edges cannot finish old drag");
 physical[1]=0;UnitPointerObserve();Edge(0,kMouseDown,60,20);Edge(0,kMouseUp,60,20);
 Check(gUnitUi.SelectionCount()==1&&gUnitUi.IsSelected(live[1].key),"physical focus release clears controller suppression");
 PointerReset();physical[1]=1;UnitPointerSeed();Edge(1,kMouseDown);
 Check(!gStrategicDragging,"held middle cannot begin pan on entry");
 Edge(1,kMouseUp);Edge(1,kMouseDown);Check(!gStrategicDragging,"queued middle release does not reopen pan");
 physical[1]=0;UnitPointerObserve();Edge(1,kMouseDown);Check(gStrategicDragging,"fresh middle press begins pan");
 Edge(1,kMouseUp);Check(!gStrategicDragging,"paired middle release ends pan");
 PointerReset();Pick(0);RefreshUnitFrame();physical[2]=1;UnitPointerSeed();Edge(2,kMouseDown,200,20);Edge(2,kMouseUp,200,20);Edge(2,kMouseDown,200,20);
 Check(individuals==0&&groups==0,"held right cannot issue move on reopen");
 physical[2]=0;UnitPointerObserve();Edge(2,kMouseDown,200,20);
 Check(individuals==1&&groups==0,"fresh right click uses exact current arm and native dispatch");
 PointerReset();Pick(0);RefreshUnitFrame();const auto rect=gUnitUi.Panel().Layout().panel;
 Check(gUnitUi.Panel().Layout().visible,"selected panel has actual owned bounds");
 Edge(0,kMouseDown,rect.x+2,rect.y+2);Edge(0,kMouseUp,60,20);
 Check(gUnitUi.SelectionCount()==1&&gUnitUi.IsSelected(live[0].key)&&individuals==0&&groups==0,"panel press cannot become a battlefield click");
 Edge(0,kMouseDown,60,20);Edge(0,kMouseUp,rect.x+2,rect.y+2);
 Check(individuals==0&&groups==0&&closes==0,"world press cannot activate panel on release");
 PointerReset();Edge(0,kMouseDown);Edge(0,kMouseUp,410,95);Edge(0,kMouseDown,60,20);Edge(0,kMouseUp,60,20);
 Check(gUnitUi.SelectionCount()==1&&gUnitUi.IsSelected(live[1].key),"release outside owned regions retires old press");
 gStrategicWheel=4;gStrategicDragging=gBoxing=gArmed=true;gUnitFocusCount=1;const auto context=gUnitContextGeneration;ResetStrategicGesture();
 Check(gStrategicWheel==0&&!gStrategicDragging&&!gBoxing&&!gArmed&&!gUnitFocusCount&&gUnitContextGeneration!=context,"mode boundary clears wheel drag target and focus");
 std::printf("checks=%d failures=0 skips=0\n",checks);return 0;
}
