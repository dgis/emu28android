/*
 *   display.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gießelink
 *
 */
#include "pch.h"
#include "resource.h"
#include "Emu28.h"
#include "ops.h"
#include "io.h"
#include "kml.h"

#define DISPLAY_FREQ	16					// display update 1/frequency (1/64) in ms

#define B 0x00000000						// black
#define W 0x00FFFFFF						// white
#define I 0xFFFFFFFF						// ignore

#define LCD_WIDTH	137
#define LCD_HEIGHT	32

#define LCD_ROW		(36*4)					// max. pixel per line

#define ROP_PDSPxax	0x00D80745				// ternary ROP

// convert color from RGBQUAD to COLORREF
#define RGBtoCOLORREF(c) ((((c) & 0xFF0000) >> 16) \
						| (((c) & 0xFF)     << 16) \
						|  ((c) & 0xFF00))

UINT nBackgroundX = 0;
UINT nBackgroundY = 0;
UINT nBackgroundW = 0;
UINT nBackgroundH = 0;
UINT nLcdX = 0;
UINT nLcdY = 0;
UINT nLcdZoom = 1;
HDC  hLcdDC = NULL;
HDC  hMainDC = NULL;
HDC  hAnnunDC = NULL;						// annunciator DC

static HBITMAP hLcdBitmap;
static HBITMAP hMainBitmap;
static HBITMAP hAnnunBitmap;

static HDC     hBmpBkDC;					// current background
static HBITMAP hBmpBk;

static HDC     hMaskDC;						// mask bitmap
static HBITMAP hMaskBitmap;
static LPBYTE  pbyMask;

static HBRUSH  hBrush = NULL;				// current segment drawing brush

static UINT nLcdXSize = 0;					// display size
static UINT nLcdYSize = 0;

static UINT nLcdxZoom = (UINT) -1;			// x zoom factor

static UINT uLcdTimerId = 0;

static CONST DWORD dwRowTable[] =			// addresses of display row memory
{
	0x1E0, 0x1F0, 0x200, 0x210, 0x220, 0x230, 0x240, 0x250,
	0x260, 0x270, 0x280, 0x290, 0x2A0, 0x2B0, 0x2C0, 0x2D0,
	0x2D8, 0x2C8, 0x2B8, 0x2A8, 0x298, 0x288, 0x278, 0x268,
	0x258, 0x248, 0x238, 0x228, 0x218, 0x208, 0x1F8, 0x1E8
};

static DWORD dwKMLColor[64] =				// color table loaded by KML script
{
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,
	I,I,I,I,I,I,I,I,I,I,I,I,I,I,I,I
};

static struct
{
	BITMAPINFOHEADER Lcd_bmih;
	RGBQUAD bmiColors[2];
} bmiLcd =
{
	{sizeof(BITMAPINFOHEADER),0/*x*/,0/*y*/,1,8,BI_RGB,0,0,0,ARRAYSIZEOF(bmiLcd.bmiColors),0},
	{{0xFF,0xFF,0xFF,0x00},{0x00,0x00,0x00,0x00}}
};

static VOID (*WritePixel)(BYTE *p, BOOL a) = NULL;

static VOID WritePixelZoom4(BYTE *p, BOOL a);
static VOID WritePixelZoom3(BYTE *p, BOOL a);
static VOID WritePixelZoom2(BYTE *p, BOOL a);
static VOID WritePixelZoom1(BYTE *p, BOOL a);
static VOID WritePixelBYTE(BYTE *p, BOOL a);
static VOID WritePixelWORD(BYTE *p, BOOL a);
static VOID WritePixelDWORD(BYTE *p, BOOL a);

VOID UpdateContrast(BYTE byContrast)
{
	BOOL bOn = (Chipset.IORamM[DSPCTL] & DON) != 0;

	// update palette information
	EnterCriticalSection(&csGDILock);		// asynchronous display update!
	{
		// get original background from bitmap
		BitBlt(hBmpBkDC,
			   0, 0, nLcdXSize, nLcdYSize,
			   hMainDC,
			   nLcdX, nLcdY,
			   SRCCOPY);

		// display on and background contrast defined?
		if (bOn && dwKMLColor[byContrast+32] != I)
		{
			INT i;

			DWORD dwColor = RGBtoCOLORREF(dwKMLColor[byContrast+32]);
			HBRUSH hBrush = (HBRUSH) SelectObject(hBmpBkDC,CreateSolidBrush(dwColor));

			for (i = 0; i < ARRAYSIZEOF(dwRowTable); ++i)
			{
				// display row enabled
				if ((*(QWORD *) &Chipset.IORamM[dwRowTable[i]]) != 0)
				{
					PatBlt(hBmpBkDC, 0, i * nLcdZoom, nLcdXSize, nLcdZoom, PATCOPY);
				}
			}

			DeleteObject(SelectObject(hBmpBkDC,hBrush));
		}

		_ASSERT(hLcdDC);

		if (hBrush)							// has already a brush
		{
			// delete it first
			DeleteObject(SelectObject(hLcdDC,hBrush));
			hBrush = NULL;
		}

		if (dwKMLColor[byContrast] != I)	// have brush color?
		{
			// set brush for display pattern
			VERIFY(hBrush = (HBRUSH) SelectObject(hLcdDC,CreateSolidBrush(RGBtoCOLORREF(dwKMLColor[byContrast]))));
		}
		GdiFlush();
	}
	LeaveCriticalSection(&csGDILock);
	UpdateAnnunciators();					// adjust annunciator color
	return;
}

VOID SetLcdColor(UINT nId, UINT nRed, UINT nGreen, UINT nBlue)
{
	dwKMLColor[nId&0x3F] = ((nRed&0xFF)<<16)|((nGreen&0xFF)<<8)|(nBlue&0xFF);
	return;
}

VOID GetSizeLcdBitmap(INT *pnX, INT *pnY)
{
	*pnX = *pnY = 0;						// unknown

	if (hLcdBitmap)
	{
		*pnX = nLcdXSize;
		*pnY = nLcdYSize;
	}
	return;
}

VOID CreateLcdBitmap(VOID)
{
	UINT nPatSize;
	INT  i;
	BOOL bEmpty;

	// select pixel drawing routine
	switch (nLcdZoom)
	{
	case 1:
		WritePixel = WritePixelZoom1;
		break;
	case 2:
		WritePixel = WritePixelZoom2;
		break;
	case 3:
		WritePixel = WritePixelZoom3;
		break;
	case 4:
		WritePixel = WritePixelZoom4;
		break;
	default:
		// select pixel pattern size (BYTE, WORD, DWORD)
		nLcdxZoom = nLcdZoom;				// BYTE pattern size adjusted x-Zoom
		nPatSize = sizeof(BYTE);			// use BYTE pattern
		while ((nLcdxZoom & 0x1) == 0 && nPatSize < sizeof(DWORD))
		{
			nLcdxZoom >>= 1;
			nPatSize <<= 1;
		}
		switch (nPatSize)
		{
		case sizeof(BYTE):
			WritePixel = WritePixelBYTE;
			break;
		case sizeof(WORD):
			WritePixel = WritePixelWORD;
			break;
		case sizeof(DWORD):
			WritePixel = WritePixelDWORD;
			break;
		default:
			_ASSERT(FALSE);
		}
	}

	// all KML contrast palette fields undefined?
	for (bEmpty = TRUE, i = 0; bEmpty && i < ARRAYSIZEOF(dwKMLColor); ++i)
	{
		bEmpty = (dwKMLColor[i] == I);
	}
	if (bEmpty)								// preset KML contrast palette
	{
		// black on character
		for (i = 0; i < ARRAYSIZEOF(dwKMLColor) / 2; ++i)
		{
			_ASSERT(i < ARRAYSIZEOF(dwKMLColor));
			dwKMLColor[i] = B;
		}
	}

	nLcdXSize = LCD_WIDTH * nLcdZoom;
	nLcdYSize = LCD_HEIGHT * nLcdZoom;

	// initialize background bitmap
	VERIFY(hBmpBkDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hBmpBk = CreateCompatibleBitmap(hWindowDC,nLcdXSize,nLcdYSize));
	VERIFY(hBmpBk = (HBITMAP) SelectObject(hBmpBkDC,hBmpBk));

	// create mask bitmap
	bmiLcd.Lcd_bmih.biWidth = LCD_ROW * nLcdZoom;
	bmiLcd.Lcd_bmih.biHeight = -(LONG) nLcdYSize;
	VERIFY(hMaskDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hMaskBitmap = CreateDIBSection(hWindowDC,(BITMAPINFO*)&bmiLcd,DIB_RGB_COLORS,(VOID **)&pbyMask,NULL,0));
	VERIFY(hMaskBitmap = (HBITMAP) SelectObject(hMaskDC,hMaskBitmap));

	// create LCD bitmap
	_ASSERT(hLcdDC == NULL);
	VERIFY(hLcdDC = CreateCompatibleDC(hWindowDC));
	VERIFY(hLcdBitmap = CreateCompatibleBitmap(hWindowDC,nLcdXSize,nLcdYSize));
	VERIFY(hLcdBitmap = (HBITMAP) SelectObject(hLcdDC,hLcdBitmap));

	_ASSERT(hPalette != NULL);
	SelectPalette(hLcdDC,hPalette,FALSE);	// set palette for LCD DC
	RealizePalette(hLcdDC);					// realize palette

	UpdateContrast(Chipset.contrast);		// initialize background

	EnterCriticalSection(&csGDILock);		// solving NT GDI problems
	{
		_ASSERT(hMainDC);					// background bitmap must be loaded

		// get original background from bitmap
		BitBlt(hLcdDC,
			   0, 0,
			   nLcdXSize, nLcdYSize,
			   hMainDC,
			   nLcdX, nLcdY,
			   SRCCOPY);
	}
	LeaveCriticalSection(&csGDILock);
	return;
}

VOID DestroyLcdBitmap(VOID)
{
	// set contrast palette to startup colors
	WORD i = 0; while(i < ARRAYSIZEOF(dwKMLColor)) dwKMLColor[i++] = I;

	if (hLcdDC != NULL)
	{
		// destroy background bitmap
		DeleteObject(SelectObject(hBmpBkDC,hBmpBk));
		DeleteDC(hBmpBkDC);

		// destroy display pattern brush
		DeleteObject(SelectObject(hLcdDC,hBrush));

		// destroy mask bitmap
		DeleteObject(SelectObject(hMaskDC,hMaskBitmap));
		DeleteDC(hMaskDC);

		// destroy LCD bitmap
		DeleteObject(SelectObject(hLcdDC,hLcdBitmap));
		DeleteDC(hLcdDC);
		hBrush = NULL;
		hLcdDC = NULL;
		hLcdBitmap = NULL;
	}

	nLcdXSize = 0;
	nLcdYSize = 0;
	nLcdxZoom = (UINT) -1;
	WritePixel = NULL;
	return;
}

BOOL CreateMainBitmap(LPCTSTR szFilename)
{
	_ASSERT(hWindowDC != NULL);
	hMainDC = CreateCompatibleDC(hWindowDC);
	_ASSERT(hMainDC != NULL);
	if (hMainDC == NULL) return FALSE;		// quit if failed
	hMainBitmap = LoadBitmapFile(szFilename,TRUE);
	if (hMainBitmap == NULL)
	{
		DeleteDC(hMainDC);
		hMainDC = NULL;
		return FALSE;
	}
	hMainBitmap = (HBITMAP) SelectObject(hMainDC,hMainBitmap);
	_ASSERT(hPalette != NULL);
	VERIFY(SelectPalette(hMainDC,hPalette,FALSE));
	RealizePalette(hMainDC);
	return TRUE;
}

VOID DestroyMainBitmap(VOID)
{
	if (hMainDC != NULL)
	{
		// destroy Main bitmap
		DeleteObject(SelectObject(hMainDC,hMainBitmap));
		DeleteDC(hMainDC);
		hMainDC = NULL;
		hMainBitmap = NULL;
	}
	return;
}

//
// load annunciator bitmap
//
BOOL CreateAnnunBitmap(LPCTSTR szFilename)
{
	_ASSERT(hWindowDC != NULL);
	VERIFY(hAnnunDC = CreateCompatibleDC(hWindowDC));
	if (hAnnunDC == NULL) return FALSE;		// quit if failed
	hAnnunBitmap = LoadBitmapFile(szFilename,FALSE);
	if (hAnnunBitmap == NULL)
	{
		DeleteDC(hAnnunDC);
		hAnnunDC = NULL;
		return FALSE;
	}
	hAnnunBitmap = (HBITMAP) SelectObject(hAnnunDC,hAnnunBitmap);
	return TRUE;
}

//
// set annunciator bitmap
//
VOID SetAnnunBitmap(HDC hDC, HBITMAP hBitmap)
{
	hAnnunDC = hDC;
	hAnnunBitmap = hBitmap;
	return;
}

//
// destroy annunciator bitmap
//
VOID DestroyAnnunBitmap(VOID)
{
	if (hAnnunDC != NULL)
	{
		VERIFY(DeleteObject(SelectObject(hAnnunDC,hAnnunBitmap)));
		DeleteDC(hAnnunDC);
		hAnnunDC = NULL;
		hAnnunBitmap = NULL;
	}
	return;
}

//****************
//*
//* LCD functions
//*
//****************

static VOID WritePixelZoom4(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		// check alignment for ARM CPU
		_ASSERT((DWORD_PTR) p % sizeof(DWORD) == 0);

		*(DWORD *)&p[0*LCD_ROW] = *(DWORD *)&p[4*LCD_ROW]  =
		*(DWORD *)&p[8*LCD_ROW] = *(DWORD *)&p[12*LCD_ROW] = 0x01010101;
	}
	return;
}

static VOID WritePixelZoom3(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		p[0*LCD_ROW+0] = p[0*LCD_ROW+1] = p[0*LCD_ROW+2] =
		p[3*LCD_ROW+0] = p[3*LCD_ROW+1] = p[3*LCD_ROW+2] =
		p[6*LCD_ROW+0] = p[6*LCD_ROW+1] = p[6*LCD_ROW+2] = 0x01;
	}
	return;
}

static VOID WritePixelZoom2(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		// check alignment for ARM CPU
		_ASSERT((DWORD_PTR) p % sizeof(WORD) == 0);

		*(WORD *)&p[0*LCD_ROW] =
		*(WORD *)&p[2*LCD_ROW] = 0x0101;
	}
	return;
}

static VOID WritePixelZoom1(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		p[0*LCD_ROW] = 0x01;
	}
	return;
}

static VOID WritePixelDWORD(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor
		_ASSERT((DWORD_PTR) p % sizeof(DWORD) == 0); // check alignment for ARM CPU

		for (y = nLcdZoom; y > 0; --y)
		{
			LPDWORD pdwPixel = (LPDWORD) p;

			x = nLcdxZoom;
			do
			{
				*pdwPixel++ = 0x01010101;
			}
			while (--x > 0);
			p += sizeof(DWORD) * LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static VOID WritePixelWORD(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor
		_ASSERT((DWORD_PTR) p % sizeof(WORD) == 0); // check alignment for ARM CPU

		for (y = nLcdZoom; y > 0; --y)
		{
			LPWORD pwPixel = (LPWORD) p;

			x = nLcdxZoom;
			do
			{
				*pwPixel++ = 0x0101;
			}
			while (--x > 0);
			p += sizeof(WORD) * LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static VOID WritePixelBYTE(BYTE *p, BOOL a)
{
	if (a)									// pixel on
	{
		UINT x,y;

		_ASSERT(nLcdxZoom > 0);				// x-Zoom factor

		for (y = nLcdZoom; y > 0; --y)
		{
			LPBYTE pbyPixel = p;

			x = nLcdxZoom;
			do
			{
				*pbyPixel++ = 0x01;
			}
			while (--x > 0);
			p += LCD_ROW * nLcdxZoom;
		}
	}
	return;
}

static __inline VOID WriteDisplayCol(BYTE *p, QWORD w)
{
	INT  i;
	BOOL c;

	// next memory position in LCD bitmap
	INT nNextLine = LCD_ROW * nLcdZoom * nLcdZoom;

	// build display column base on the display ROW table
	for (i = 0; i < ARRAYSIZEOF(dwRowTable); ++i)
	{
		// set pixel state
		c = (w & *(QWORD *) &Chipset.IORamM[dwRowTable[i]]) != 0;
		WritePixel(p,c);					// write pixel zoom independent
		p += nNextLine;						// next memory position in LCD bitmap
	}
	return;
}

VOID UpdateMainDisplay(VOID)
{
	DWORD d;
	BYTE  *p;

	ZeroMemory(pbyMask, bmiLcd.Lcd_bmih.biWidth * -bmiLcd.Lcd_bmih.biHeight);

	if ((Chipset.IORamM[DSPCTL]&DON) != 0 && dwKMLColor[Chipset.contrast] != I)
	{
		p = pbyMask;						// 1st column memory position in LCD bitmap

		// scan complete display area of slave
		for (d = SDISP_BEGIN; d <= SDISP_END; d += ARRAYSIZEOF(dwRowTable) / 4)
		{
			WriteDisplayCol(p, *(QWORD *) &Chipset.IORamS[d]);
			p += nLcdZoom;
		}

		// scan complete display area of master
		for (d = MDISP_BEGIN; d <= MDISP_END; d += ARRAYSIZEOF(dwRowTable) / 4)
		{
			WriteDisplayCol(p, *(QWORD *) &Chipset.IORamM[d]);
			p += nLcdZoom;
		}
	}

	EnterCriticalSection(&csGDILock);		// solving NT GDI problems
	{
		// load lcd with mask bitmap
		BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hMaskDC, 0, 0, SRCCOPY);

		// mask segment mask with background and brush
		BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hBmpBkDC, 0, 0, ROP_PDSPxax);

		// redraw display area
		BitBlt(hWindowDC, nLcdX, nLcdY, nLcdXSize, nLcdYSize, hLcdDC, 0, 0, SRCCOPY);
		GdiFlush();
	}
	LeaveCriticalSection(&csGDILock);
	return;
}

VOID UpdateAnnunciators(VOID)
{
	const DWORD dwAnnAddr[] = { SLA_HALT, SLA_SHIFT, SLA_ALPHA, SLA_BUSY, SLA_BAT, SLA_RAD, SLA_PRINTER };

	INT i,j,nCount;

	BOOL bAnnOn = (Chipset.IORamM[DSPCTL] & DON) != 0 && (Chipset.IORamS[DSPCTL] & DON) != 0;

	// check all annuncators
	for (i = 0; i < ARRAYSIZEOF(dwAnnAddr);)
	{
		nCount = 0;

		if (bAnnOn)							// annunciators on
		{
			DWORD dwAnn = Npack(Chipset.IORamS+dwAnnAddr[i],8) ^ Npack(&Chipset.IORamS[SLA_ALL],8);

			// count the number of set bits
			for (;dwAnn != 0; ++nCount)
			{
				dwAnn &= (dwAnn - 1);
			}
		}

		// contrast table entry of annunciator
		j = Chipset.contrast + nCount - 19;
		if (j < 0)   j = 0;
		if (j >= 31) j = 31;

		DrawAnnunciator(++i, nCount > 0 && dwKMLColor[j] != I, dwKMLColor[j]);
	}
	return;
}

static VOID CALLBACK LcdProc(UINT uEventId, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	EnterCriticalSection(&csLcdLock);
	{
		if (uLcdTimerId)					// display update task still active
		{
			UpdateMainDisplay();
		}
	}
	LeaveCriticalSection(&csLcdLock);
	return;
	UNREFERENCED_PARAMETER(uEventId);
	UNREFERENCED_PARAMETER(uMsg);
	UNREFERENCED_PARAMETER(dwUser);
	UNREFERENCED_PARAMETER(dw1);
	UNREFERENCED_PARAMETER(dw2);
}

VOID StartDisplay(VOID)
{
	if (uLcdTimerId)						// LCD update timer running
		return;								// -> quit

	if (Chipset.IORamM[DSPCTL]&DON)			// display on?
	{
		UpdateAnnunciators();				// switch on annunciators
		VERIFY(uLcdTimerId = timeSetEvent(DISPLAY_FREQ,0,(LPTIMECALLBACK)&LcdProc,0,TIME_PERIODIC));
	}
	return;
}

VOID StopDisplay(VOID)
{
	if (uLcdTimerId == 0)					// timer stopped
		return;								// -> quit

	timeKillEvent(uLcdTimerId);				// stop display update
	uLcdTimerId = 0;						// set flag display update stopped

	EnterCriticalSection(&csLcdLock);
	{
		EnterCriticalSection(&csGDILock);	// solving NT GDI problems
		{
			// get original background from bitmap
			BitBlt(hLcdDC, 0, 0, nLcdXSize, nLcdYSize, hMainDC, nLcdX, nLcdY, SRCCOPY);
			GdiFlush();
		}
		LeaveCriticalSection(&csGDILock);
	}
	LeaveCriticalSection(&csLcdLock);
	InvalidateRect(hWnd,NULL,FALSE);
	return;
}

VOID ResizeWindow(VOID)
{
	if (hWnd != NULL)						// if window created
	{
		RECT rectWindow;
		RECT rectClient;

		rectWindow.left   = 0;
		rectWindow.top    = 0;
		rectWindow.right  = nBackgroundW;
		rectWindow.bottom = nBackgroundH;

		AdjustWindowRect(&rectWindow,
			(DWORD) GetWindowLongPtr(hWnd,GWL_STYLE),
			GetMenu(hWnd) != NULL || IsRectEmpty(&rectWindow));
		SetWindowPos(hWnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
			rectWindow.right  - rectWindow.left,
			rectWindow.bottom - rectWindow.top,
			SWP_NOMOVE);

		// check if menu bar wrapped to two or more rows
		GetClientRect(hWnd, &rectClient);
		if (rectClient.bottom < (LONG) nBackgroundH)
		{
			rectWindow.bottom += (nBackgroundH - rectClient.bottom);
			SetWindowPos (hWnd, NULL, 0, 0,
				rectWindow.right  - rectWindow.left,
				rectWindow.bottom - rectWindow.top,
				SWP_NOMOVE | SWP_NOZORDER);
		}

		EnterCriticalSection(&csGDILock);	// solving NT GDI problems
		{
			_ASSERT(hWindowDC);				// move origin of destination window
			VERIFY(SetWindowOrgEx(hWindowDC, nBackgroundX, nBackgroundY, NULL));
			GdiFlush();
		}
		LeaveCriticalSection(&csGDILock);
		InvalidateRect(hWnd,NULL,TRUE);
	}
	return;
}
