// debugmarker.cpp: the debug markers, cuboids with a line of text that code can place in the world (reconstructed;
// Stuart's file is not in his release). CSoundManager shows each playing sound as one. The object also holds DBT.

#include	"common.h"
#include	"mathsconst.h"

#define DEBUGMARKER_CPP
#include	"debugmarker.h"
#include	"DebugText.h"
#include	"DXEngine.h"
#include	"Camera.h"
#include	"renderinfo.h"
#include	"viewport.h"
#include	"ltshell.h"
#include	"Platform.h"
#include	"text.h"
#include	<d3dx9.h>

// FUNCTION: 0x004423b0 init:ZERO_FVECTOR
// FUNCTION: 0x004423d0 init:ID_FMATRIX

CDebugMarkers	DEBUGMARKERS;						// 0066ffb0
// FUNCTION: 0x004424a0 init:DBT
CDebugText		DBT;								// 0066ffc8

//*********************************************************************************
void	CDebugMarkers::Initialise()	// FUNCTION: 0x00441e40
{
	mFirst = NULL;
}

//*********************************************************************************
void	CDebugMarkers::Shutdown()	// FUNCTION: 0x00441e50
{
	CDebugMarker	*m = mFirst;
	while (m)
	{
		CDebugMarker	*next = m->mNext;
		delete m;
		m = next;
	}
}

//*********************************************************************************
// STAND-IN (ids; review F 2026-10-15): this helper and the orientation snapshot class below; maths.h's GetColumnY called
// directly, with or without a matrix local, loses 00441ea0 and the object's Vec3 copy (12 -> 10).
// The matrix's Y column through three named floats, loaded z, y, x as retail's (00441ea0); maths.h's GetColumnY is
// the plain body that CMesh::Load and Read203 inline (004a915c, 004af665).
static inline FVector ColumnY(const FMatrix &m)
{
	float z = m.Row[2].Y, y = m.Row[1].Y, x = m.Row[0].Y;
	return FVector(x, y, z);
}

// An orientation snapshot supplies the camera's world-space forward axis.
class CDebugCameraOrientation
{
	FMatrix mOrientation;
public:
	CDebugCameraOrientation(CCamera *camera) : mOrientation(camera->GetOrientation()) {}
	FVector GetForward() const { return ColumnY(mOrientation); }
};

// each cuboid, and its text where it is in front of the camera and on the screen
void	CDebugMarkers::Render()	// FUNCTION: 0x00441ea0
{
	CViewport	vp = ENGINE.GetCurrentViewport();
	D3DXMATRIX		proj, world, view;
	RENDERINFO.GetProjection(&proj);
	RENDERINFO.GetWorld(&world);
	RENDERINFO.GetView(&view);
	D3DVIEWPORT9	v;
	v.Height = vp.Height;
	v.MaxZ = vp.MaxZ;
	v.MinZ = vp.MinZ;
	v.Width = vp.Width;
	v.X = vp.X;
	v.Y = vp.Y;
	FVector	cam_pos = ENGINE.GetCamera()->GetPos();
	CDebugCameraOrientation cam_ori(ENGINE.GetCamera());
	FVector cam_dir = cam_ori.GetForward();

	for (CDebugMarker *m = mFirst; m; m = m->mNext)
	{
		RENDERINFO.SetWorld(m->mFramePos, m->mFrameOri);
		ENGINE.DrawDebugCuboid(m->mColour, m->GetSize(), m->GetPos(), m->mOrientation, ENGINE.GetDefaultTexture());
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		if (strlen(m->mText))
		{
			FVector frame_pos = m->GetFramePos();
			FVector pos = frame_pos + (m->GetFrameOri() * m->GetPos());
			if ((pos - cam_pos) * cam_dir > 0)
			{
				FVector	out;
				D3DXVec3Project((D3DXVECTOR3 *)&out, (D3DXVECTOR3 *)&pos, &v, &proj, &view, &world);
				if ((out.X >= 0) && (out.X < PLATFORM.GetWindowWidth()) &&
					(out.Y >= 0) && (out.Y < PLATFORM.GetWindowHeight()))
				{
					DWORD colour = m->mColour;
					PLATFORM.DebugFont()->DrawText(out.X, out.Y, colour, ToWCHAR(m->mText), 0, 0, 1.f);
				}
			}
		}
	}
}

//*********************************************************************************
CDebugMarker::CDebugMarker()	// FUNCTION: 0x004422d0
{
	mNext = DEBUGMARKERS.mFirst;
	DEBUGMARKERS.mFirst = this;
	mPos.Set(0, 0, 0);
	mSize.Set(0.5f, 0.5f, 0.5f);
	mOrientation = ID_FMATRIX;
	mColour = 0xffffffff;
	mFramePos = ZERO_FVECTOR;
	mFrameOri = ID_FMATRIX;
	strcpy(mText, "");
}

//*********************************************************************************
CDebugMarker::~CDebugMarker()	// FUNCTION: 0x00442380
{
	if (DEBUGMARKERS.mFirst == this)
		DEBUGMARKERS.mFirst = mNext;
	else
	{
		CDebugMarker	*m = DEBUGMARKERS.mFirst;
		while ((m) && (m->mNext != this))
			m = m->mNext;
		m->mNext = mNext;
	}
}
// STAND-IN (link layout): retail's 4 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_debugmarker[4] = { 0 };
