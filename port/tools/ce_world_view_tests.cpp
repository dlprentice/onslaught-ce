// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
#include <cstdio>
#include <limits>
#include "ce_world_view.h"

struct D3DMATRIX { float m[4][4]; };
struct D3DVIEWPORT9 { unsigned X,Y,Width,Height; float MinZ,MaxZ; };
#define FAILED(value) ((value) < 0)
static unsigned frame = 12;
bool gMirrorPass;
static std::uint64_t commanderToken;
extern "C" unsigned PortPresentCount() { return frame; }
extern "C" std::uint64_t PortCeCmdWorldViewToken() { return commanderToken; }
#ifndef CE_WORLD_VIEW_NO_EDITOR
static std::uint64_t editorToken;
extern "C" std::uint64_t PortCeEditorWorldViewToken() { return editorToken; }
#endif
struct Platform { int width=640,height=480; int GetWindowWidth(){return width;} int GetWindowHeight(){return height;} } PLATFORM;
struct Device {
    int calls=0, result=0; D3DVIEWPORT9 viewport={0,0,640,480,0,1};
    int GetViewport(D3DVIEWPORT9* out) { ++calls; *out=viewport; return result; }
} device;
struct Shell { Device* current=&device; Device* GetDevice(){return current;} } LT;
struct RenderInfo {
    int reads=0; D3DMATRIX view={},projection={};
    void GetView(D3DMATRIX* out){++reads;*out=view;}
    void GetProjection(D3DMATRIX* out){++reads;*out=projection;}
} RENDERINFO;
#include "ce_world_view_fixture.inc"

static int failed;
static void Check(bool value,const char* name){if(!value){std::fprintf(stderr,"FAILED: %s\n",name);++failed;}}
int main(){
    for(int n=0;n<4;++n)RENDERINFO.view.m[n][n]=RENDERINFO.projection.m[n][n]=1;
    CeWorldViewSample out={};
    PortCeWorldViewCapture();
    Check(device.calls==0 && RENDERINFO.reads==0,"inactive mode does not touch device or matrices");
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,1,&out),"inactive view unavailable");
    commanderToken=101;PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,101,&out),"active snapshot readable");
    Check(out.present==12 && out.view[0]==1 && out.viewport.width==640,"actual matrices and viewport retained");
    RENDERINFO.view.m[0][0]=9;
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,101,&out) && out.view[0]==1,"overlay cannot change captured world matrix");
    frame=13;Check(CeWorldViewRead(CeWorldViewOwner::Commander,101,&out),"displayed previous Present available");
    frame=14;Check(!CeWorldViewRead(CeWorldViewOwner::Commander,101,&out),"stale view refused");
    PortCeWorldViewCapture();commanderToken=102;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,102,&out),"new entry cannot use old session");
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,101,&out),"old token cannot survive new entry");
    PortCeWorldViewCapture();PLATFORM.width=800;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,102,&out),"resize refuses stale snapshot");
    PLATFORM.width=640;PortCeWorldViewCapture();
    CeWorldViewInvalidate(CeWorldViewOwner::Editor);
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,102,&out),"other owner cannot retire active snapshot");
    CeWorldViewInvalidate(CeWorldViewOwner::Commander);
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,102,&out),"own teardown invalidates snapshot");
#ifndef CE_WORLD_VIEW_NO_EDITOR
    commanderToken=0;editorToken=201;PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Editor,201,&out),"editor uses same provider");
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,201,&out),"wrong owner refused");
    commanderToken=103;PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out) && !CeWorldViewRead(CeWorldViewOwner::Editor,201,&out),"simultaneous owners refused");
    editorToken=0;
#endif
    commanderToken=103;PortCeWorldViewCapture();device.result=-1;PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out),"failed viewport query clears old view");
    device.result=0;device.viewport.Width=641;PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out),"viewport outside framebuffer refused");
    device.viewport.Width=640;PortCeWorldViewCapture();RENDERINFO.projection={};PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out),"singular projection clears view");
    for(int n=0;n<4;++n)RENDERINFO.projection.m[n][n]=1;
    PortCeWorldViewCapture();LT.current=nullptr;PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out),"missing device clears view");
    LT.current=&device;PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,0,&out),"zero session refused");
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,nullptr),"null output refused");
    const CeWorldViewSample unchanged=out;commanderToken=0;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,103,&out) && std::memcmp(&out,&unchanged,sizeof(out))==0,"inactive read preserves output");

    // A mirror-only entry cannot publish a view, even when its owner is valid.
    commanderToken=301;frame=30;gMirrorPass=true;
    const int beforeMirrorCalls=device.calls, beforeMirrorReads=RENDERINFO.reads;
    PortCeWorldViewCapture();gMirrorPass=false;
    Check(device.calls==beforeMirrorCalls && RENDERINFO.reads==beforeMirrorReads,"mirror capture never reads reflected matrices or device");
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,301,&out),"mirror-only entry has no interaction view");
    PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,301,&out),"ordinary capture recovers after mirror-only entry");

    // Refuse reads in mirror context even before its capture hook is reached.
    const CeWorldViewSample beforeMirror=out;
    gMirrorPass=true;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,301,&out) && std::memcmp(&out,&beforeMirror,sizeof(out))==0,"mirror-context read preserves output");
    RENDERINFO.view.m[0][0]=7;PortCeWorldViewCapture();gMirrorPass=false;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,301,&out),"ordinary then mirror invalidates previous ordinary view");
    ++frame;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,301,&out),"skipped following ordinary pass remains unavailable");
    RENDERINFO.view.m[0][0]=3;PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,301,&out) && out.view[0]==3,"following ordinary capture replaces reflected camera data");

    gMirrorPass=true;commanderToken=302;PortCeWorldViewCapture();gMirrorPass=false;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,301,&out) && !CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"mirror cannot carry a view across sessions");
    PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"new session ordinary capture recovers");
    gMirrorPass=true;PLATFORM.width=800;PortCeWorldViewCapture();gMirrorPass=false;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"mirror and resize leave no readable view");
    PLATFORM.width=640;PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"ordinary capture recovers at restored viewport");
    gMirrorPass=true;device.viewport.Width=641;PortCeWorldViewCapture();gMirrorPass=false;
    PortCeWorldViewCapture();
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"invalid ordinary viewport after mirror cannot revive old view");
    device.viewport.Width=640;

    // Unsigned Present wrap permits only the immediately displayed ordinary view.
    frame=std::numeric_limits<unsigned>::max();PortCeWorldViewCapture();frame=0;
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"ordinary view survives one Present across wrap");
    frame=1;Check(!CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"ordinary view expires across Present wrap");
    frame=std::numeric_limits<unsigned>::max();PortCeWorldViewCapture();gMirrorPass=true;
    PortCeWorldViewCapture();gMirrorPass=false;frame=0;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"mirror cannot retain an ordinary view across wrap");
    PortCeWorldViewCapture();
    Check(CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"ordinary capture recovers after mirrored wrap");
    frame=2;gMirrorPass=true;PortCeWorldViewCapture();gMirrorPass=false;
    Check(!CeWorldViewRead(CeWorldViewOwner::Commander,302,&out),"stale mirror-only frame cannot renew ordinary view");
    std::printf("native view provider authored checks: %s\n",failed?"FAIL":"PASS");return failed?1:0;
}
