// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/d3dutil.cpp: D3DUtil_SetDeviceCursor, written for the public build in place of
// src/d3dutil.cpp, in the two-argument form the game declares (d3dapp.cpp). It gives the Direct3D device a cursor
// made from a Windows cursor: the cursor's mask and colour bitmaps are read as 32-bit rows, composed into an
// A8R8G8B8 image (opaque in the cursor's colour where the mask is clear, transparent elsewhere; a monochrome cursor's
// mask holds its colour plane above the mask plane, so it is half as tall), and set with the cursor's hotspot. The
// working arrays come from the game's allocator.
#include "common.h"
#include "mathsconst.h"
#include <windows.h>
#include <d3d9.h>
#include "DX.h"

HRESULT D3DUtil_SetDeviceCursor(LPDIRECT3DDEVICE9 device, HCURSOR cursor)
{
	HRESULT				hr = E_FAIL;
	ICONINFO			icon;
	LPDIRECT3DSURFACE9	image = NULL;
	HDC					screen = NULL;
	HDC					maskDC = NULL;
	HDC					colourDC = NULL;
	DWORD				*maskBits = NULL;
	DWORD				*colourBits = NULL;

	ZeroMemory(&icon, sizeof(icon));
	if (GetIconInfo(cursor, &icon))
	{
		BITMAP	mask;
		if (GetObject((HGDIOBJ)icon.hbmMask, sizeof(BITMAP), (LPVOID)&mask) != 0)
		{
			const bool	mono = icon.hbmColor == NULL;
			const DWORD	width = mask.bmWidth;
			const DWORD	maskRows = mask.bmHeight;
			const DWORD	rows = mono ? maskRows / 2 : maskRows;

			hr = device->CreateOffscreenPlainSurface(width, rows, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &image, NULL);
			if (SUCCEEDED(hr))
			{
				maskBits = new DWORD[width * maskRows];

				BITMAPINFO	info;
				ZeroMemory(&info, sizeof(info));
				info.bmiHeader.biSize = sizeof(info.bmiHeader);
				info.bmiHeader.biWidth = width;
				info.bmiHeader.biHeight = maskRows;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;

				screen = GetDC(NULL);
				maskDC = CreateCompatibleDC(screen);
				if (!maskDC)
					hr = E_FAIL;
				else
				{
					HGDIOBJ	previous = SelectObject(maskDC, icon.hbmMask);
					GetDIBits(maskDC, icon.hbmMask, 0, maskRows, maskBits, &info, DIB_RGB_COLORS);
					SelectObject(maskDC, previous);

					if (!mono)
					{
						colourBits = new DWORD[width * rows];
						colourDC = CreateCompatibleDC(GetDC(NULL));
						if (!colourDC)
							hr = E_FAIL;
						else
						{
							SelectObject(colourDC, icon.hbmColor);
							GetDIBits(colourDC, icon.hbmColor, 0, rows, colourBits, &info, DIB_RGB_COLORS);
						}
					}

					if (SUCCEEDED(hr))
					{
						// The bitmaps' rows run bottom up; the surface's top down.
						D3DLOCKED_RECT	locked;
						image->LockRect(&locked, NULL, 0);
						DWORD	*out = (DWORD *)locked.pBits;
						for (UINT y = 0; y < rows; y++)
						{
							const DWORD	*colourRow = mono ? maskBits + width * (rows - 1 - y) : colourBits + width * (rows - 1 - y);
							const DWORD	*maskRow = maskBits + width * ((mono ? maskRows : rows) - 1 - y);
							for (UINT x = 0; x < width; x++)
								out[width * y + x] = maskRow[x] == 0 ? 0xff000000 | colourRow[x] : 0x00000000;
						}
						image->UnlockRect();

						hr = device->SetCursorProperties(icon.xHotspot, icon.yHotspot, image);
						if (SUCCEEDED(hr))
							hr = S_OK;
					}
				}
			}
		}
	}

	if (icon.hbmMask)
		DeleteObject(icon.hbmMask);
	if (icon.hbmColor)
		DeleteObject(icon.hbmColor);
	if (screen)
		ReleaseDC(NULL, screen);
	if (colourDC)
		DeleteDC(colourDC);
	if (maskDC)
		DeleteDC(maskDC);
	SAFE_DELETE_ARRAY(colourBits);
	SAFE_DELETE_ARRAY(maskBits);
	SAFE_RELEASE(image);
	return hr;
}
