/*
 *   files.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gie�elink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "ops.h"
#include "io.h"								// I/O register definitions
#include "kml.h"
#include "debugger.h"
#include "stegano.h"
#include "lodepng.h"

#pragma intrinsic(abs,labs)

// external memory
#define EXTMEM (Chipset.Ext1Size + Chipset.Ext2Size)

TCHAR  szEmuDirectory[MAX_PATH];
TCHAR  szCurrentDirectory[MAX_PATH];
TCHAR  szCurrentKml[MAX_PATH];
TCHAR  szBackupKml[MAX_PATH];
TCHAR  szCurrentFilename[MAX_PATH];
TCHAR  szBackupFilename[MAX_PATH];
TCHAR  szBufferFilename[MAX_PATH];

BOOL   bDocumentAvail = FALSE;				// document not available

LPBYTE pbyRamExt = NULL;					// external RAM memory pointer

LPBYTE pbyRom = NULL;
DWORD  dwRomSize = 0;
WORD   wRomCrc = 0;							// fingerprint of patched ROM
BYTE   cCurrentRomType = 0;					// Model -> hardware
UINT   nCurrentClass = 0;					// Class -> derivate

BOOL   bBackup = FALSE;

// document signatures
static CONST BYTE bySignature[] = "Emu28 Document\xFE";
static HANDLE hCurrentFile = NULL;

static CHIPSET BackupChipset;
static LPBYTE  pbyBackupRamExt;

//################
//#
//#    Window Position Tools
//#
//################

VOID SetWindowLocation(HWND hWnd,INT nPosX,INT nPosY)
{
	WINDOWPLACEMENT wndpl;
	RECT *pRc = &wndpl.rcNormalPosition;

	wndpl.length = sizeof(wndpl);
	GetWindowPlacement(hWnd,&wndpl);
	pRc->right = pRc->right - pRc->left + nPosX;
	pRc->bottom = pRc->bottom - pRc->top + nPosY;
	pRc->left = nPosX;
	pRc->top = nPosY;
	SetWindowPlacement(hWnd,&wndpl);
	return;
}



//################
//#
//#    Filename Title Helper Tool
//#
//################

DWORD GetCutPathName(LPCTSTR szFileName, LPTSTR szBuffer, DWORD dwBufferLength, INT nCutLength)
{
	TCHAR cPath[_MAX_PATH];					// full file name
	TCHAR cDrive[_MAX_DRIVE];
	TCHAR cDir[_MAX_DIR];
	TCHAR cFname[_MAX_FNAME];
	TCHAR cExt[_MAX_EXT];

	_ASSERT(nCutLength >= 0);				// 0 = only drive and name

	// split original file name into parts
	_tsplitpath(szFileName,cDrive,cDir,cFname,cExt);

	if (*cDir != 0)							// contain directory part
	{
		LPTSTR lpFilePart;					// address of file name in path
		INT    nNameLen,nPathLen,nMaxPathLen;

		GetFullPathName(szFileName,ARRAYSIZEOF(cPath),cPath,&lpFilePart);
		_tsplitpath(cPath,cDrive,cDir,cFname,cExt);

		// calculate size of drive/name and path
		nNameLen = lstrlen(cDrive) + lstrlen(cFname) + lstrlen(cExt);
		nPathLen = lstrlen(cDir);

		// maximum length for path
		nMaxPathLen = nCutLength - nNameLen;

		if (nPathLen > nMaxPathLen)			// have to cut path
		{
			TCHAR cDirTemp[_MAX_DIR] = _T("");
			LPTSTR szPtr;

			// UNC name
			if (cDir[0] == _T('\\') && cDir[1] == _T('\\'))
			{
				// skip server
				if ((szPtr = _tcschr(cDir + 2,_T('\\'))) != NULL)
				{
					// skip share
					if ((szPtr = _tcschr(szPtr + 1,_T('\\'))) != NULL)
					{
						INT nLength = (INT) (szPtr - cDir);

						*szPtr = 0;			// set EOS behind share

						// enough room for \\server\\share and "\...\"
						if (nLength + 5 <= nMaxPathLen)
						{
							lstrcpyn(cDirTemp,cDir,ARRAYSIZEOF(cDirTemp));
							nMaxPathLen -= nLength;
						}

					}
				}
			}

			lstrcat(cDirTemp,_T("\\..."));
			nMaxPathLen -= 5;				// need 6 chars for additional "\..." + "\"
			if (nMaxPathLen < 0) nMaxPathLen = 0;

			// get earliest possible '\' character
			szPtr = &cDir[nPathLen - nMaxPathLen];
			szPtr = _tcschr(szPtr,_T('\\'));
			// not found
			if (szPtr == NULL) szPtr = _T("");

			lstrcat(cDirTemp,szPtr);		// copy path with preample to dir buffer
			lstrcpyn(cDir,cDirTemp,ARRAYSIZEOF(cDir));
		}
	}

	_tmakepath(cPath,cDrive,cDir,cFname,cExt);
	lstrcpyn(szBuffer,cPath,dwBufferLength);
	return lstrlen(szBuffer);
}

VOID SetWindowPathTitle(LPCTSTR szFileName)
{
	TCHAR cPath[MAX_PATH];
	RECT  rectClient;

	if (*szFileName != 0)					// set new title
	{
		_ASSERT(hWnd != NULL);
		VERIFY(GetClientRect(hWnd,&rectClient));
		GetCutPathName(szFileName,cPath,ARRAYSIZEOF(cPath),rectClient.right/11);
		SetWindowTitle(cPath);
	}
	return;
}



//################
//#
//#    BEEP Patch check
//#
//################

BOOL CheckForBeepPatch(VOID)
{
	typedef struct beeppatch
	{
		const DWORD dwAddress;				// patch address
		const BYTE  byPattern[4];			// patch pattern
	} BEEPPATCH, *PBEEPPATCH;

	// known beep patches
	const BEEPPATCH s28[] = { { 0x10186, { 0x8,0x1,0xB,0x1 } },		// 1BB
							  { 0x101BD, { 0x8,0x1,0xB,0x1 } } };	// 1CC

	const BEEPPATCH *psData;
	UINT nDataItems;
	BOOL bMatch;

	switch (cCurrentRomType)
	{
	case 'P':								// HP28C
		psData = s28;
		nDataItems = ARRAYSIZEOF(s28);
		break;
	default:
		psData = NULL;
		nDataItems = 0;
	}

	// check if one data set match
	for (bMatch = FALSE; !bMatch && nDataItems > 0; --nDataItems)
	{
		_ASSERT(pbyRom != NULL && psData != NULL);

		// pattern matching?
		bMatch =  (psData->dwAddress + ARRAYSIZEOF(psData->byPattern) < dwRomSize)
			   && (memcmp(&pbyRom[psData->dwAddress],psData->byPattern,ARRAYSIZEOF(psData->byPattern))) == 0;
		++psData;							// next data set
	}
	return bMatch;
}



//################
//#
//#    Patch
//#
//################

// checksum calculation
static WORD UpChk(WORD wChk, LPBYTE pbyROM)
{
	WORD wDat = ((WORD) pbyROM[3] << 12)
			  | ((WORD) pbyROM[2] <<  8)
			  | ((WORD) pbyROM[1] <<  4)
			  | ((WORD) pbyROM[0]);

	wDat += wChk;							// new checksum
	if (wDat < wChk) ++wDat;				// add carry
	return wDat;
}

static BYTE Checksum(LPBYTE pbyROM, DWORD dwStart)
{
	INT  i,j;

	WORD wChk = 0;							// reset checksum

	DWORD dwAddr1 = dwStart;
	DWORD dwAddr2 = dwAddr1 + 0x40;

	for (i = 0; i < 0x200; ++i)
	{
		for (j = 0; j < 16; ++j)
		{
			wChk = UpChk(wChk,&pbyROM[dwAddr2]);
			wChk = UpChk(wChk,&pbyROM[dwAddr1]);

			dwAddr1 += 4; dwAddr2 += 4;
		}

		dwAddr1 += 0x40; dwAddr2 += 0x40;
	}

	wChk = (wChk >> 8) + (wChk & 0xFF);		// word -> byte
	wChk = (wChk >> 8) + (wChk & 0xFF);		// add carry
	return (BYTE) wChk;
}

static VOID CorrectChecksum(DWORD dwAddress)
{
	BYTE byChk;

	pbyRom[dwAddress+0] = 0;				// clear old checksum
	pbyRom[dwAddress+1] = 0;

	// recalculate checksum
	byChk = Checksum(pbyRom,dwAddress & ~0xFFFF);

	if (byChk > 0)							// adjust checksum
		byChk = -byChk;						// last addition with carry set
	else
		byChk = 1;							// last addition with carry clear

	pbyRom[dwAddress+(0^(dwAddress & 1))] = byChk & 0xF;
	pbyRom[dwAddress+(1^(dwAddress & 1))] = byChk >> 4;
	return;
}

static __inline BYTE Asc2Nib(BYTE c)
{
	if (c<'0') return 0;
	if (c<='9') return c-'0';
	if (c<'A') return 0;
	if (c<='F') return c-'A'+10;
	if (c<'a') return 0;
	if (c<='f') return c-'a'+10;
	return 0;
}

BOOL PatchRom(LPCTSTR szFilename)
{
	HANDLE hFile = NULL;
	DWORD  dwFileSizeLow = 0;
	DWORD  dwFileSizeHigh = 0;
	DWORD  lBytesRead = 0;
	PSZ    lpStop,lpBuf = NULL;
	DWORD  dwAddress = 0;
	UINT   nPos = 0;
	BOOL   bSucc = TRUE;

	if (pbyRom == NULL) return FALSE;
	SetCurrentDirectory(szEmuDirectory);
	hFile = CreateFile(szFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	SetCurrentDirectory(szCurrentDirectory);
	if (hFile == INVALID_HANDLE_VALUE) return FALSE;
	dwFileSizeLow = GetFileSize(hFile, &dwFileSizeHigh);
	if (dwFileSizeHigh != 0 || dwFileSizeLow == 0)
	{ // file is too large or empty
		CloseHandle(hFile);
		return FALSE;
	}
	lpBuf = (PSZ) malloc(dwFileSizeLow+1);
	if (lpBuf == NULL)
	{
		CloseHandle(hFile);
		return FALSE;
	}
	ReadFile(hFile, lpBuf, dwFileSizeLow, &lBytesRead, NULL);
	CloseHandle(hFile);
	lpBuf[dwFileSizeLow] = 0;
	nPos = 0;
	while (lpBuf[nPos])
	{
		// skip whitespace characters
		nPos += (UINT) strspn(&lpBuf[nPos]," \t\n\r");

		if (lpBuf[nPos] == ';')				// comment?
		{
			do
			{
				nPos++;
				if (lpBuf[nPos] == '\n')
				{
					nPos++;
					break;
				}
			} while (lpBuf[nPos]);
			continue;
		}
		dwAddress = strtoul(&lpBuf[nPos], &lpStop, 16);
		nPos = (UINT) (lpStop - lpBuf);		// position of lpStop

		if (*lpStop != 0)					// data behind address
		{
			if (*lpStop != ':')				// invalid syntax
			{
				// skip to end of line
				while (lpBuf[nPos] != '\n' && lpBuf[nPos] != 0)
				{
					++nPos;
				}
				bSucc = FALSE;
				continue;
			}

			while (lpBuf[++nPos])
			{
				if (isxdigit(lpBuf[nPos]) == FALSE) break;
				if (dwAddress < dwRomSize)	// patch ROM
				{
					pbyRom[dwAddress] = Asc2Nib(lpBuf[nPos]);
				}
				++dwAddress;
			}
		}
	}
	_ASSERT(nPos <= dwFileSizeLow);			// buffer overflow?
	free(lpBuf);
	return bSucc;
}



//################
//#
//#    ROM
//#
//################

// lodepng allocators
void* lodepng_malloc(size_t size)
{
  return malloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
  return realloc(ptr, new_size);
}

void lodepng_free(void* ptr)
{
  free(ptr);
}

BOOL CrcRom(WORD *pwChk)					// calculate fingerprint of ROM
{
	DWORD *pdwData,dwSize;
	DWORD dwChk = 0;

	if (pbyRom == NULL) return TRUE;		// ROM CRC isn't available

	_ASSERT(pbyRom);						// view on ROM
	pdwData = (DWORD *) pbyRom;

	_ASSERT((dwRomSize % sizeof(*pdwData)) == 0);
	dwSize = dwRomSize / sizeof(*pdwData);	// file size in DWORD's

	// use checksum, because it's faster
	while (dwSize-- > 0)
	{
		CONST DWORD dwData = *pdwData++;
		if ((dwData & 0xF0F0F0F0) != 0)		// data packed?
			return FALSE;
		dwChk += dwData;
	}

	*pwChk = (WORD) ((dwChk >> 16) + (dwChk & 0xFFFF));
	return TRUE;
}

static enum ROMVER RomType(VOID)
{
	DWORD dwType;
	enum ROMVER eType;

	_ASSERT(pbyRom);						// view on ROM
	dwType =   pbyRom[0x0FFFE];				// calculate fingerprint from ROM checksums
	dwType <<= 4;
	dwType |=  pbyRom[0x0FFFF];
	dwType <<= 4;
	dwType |=  pbyRom[0x1FFFE];
	dwType <<= 4;
	dwType |=  pbyRom[0x1FFFF];
	if (dwRomSize == _KB(128))				// with 2nd ROM chip
	{
		dwType <<= 4;
		dwType |=  pbyRom[0x2FFFE];
		dwType <<= 4;
		dwType |=  pbyRom[0x2FFFF];
		dwType <<= 4;
		dwType |=  pbyRom[0x3FFFE];
		dwType <<= 4;
		dwType |=  pbyRom[0x3FFFF];
	}

	// detect ROM version
	switch (dwType)
	{
	case 0x9BAB267E: eType = V_28_1BB;  break;
	case 0x00AE722E: eType = V_28_1CC;  break;
	default:         eType = V_UNKNOWN; break;
	}
	return eType;
}

static __inline VOID UnpackRom(DWORD dwSrcOff, DWORD dwDestOff)
{
	LPBYTE pbySrc = pbyRom + dwSrcOff;		// source start address
	LPBYTE pbyDest = pbyRom + dwDestOff;	// destination start address
	while (pbySrc != pbyDest)				// unpack source
	{
		CONST BYTE byValue = *(--pbySrc);
		*(--pbyDest) = byValue >> 4;
		*(--pbyDest) = byValue & 0xF;
	}
	return;
}

BOOL MapRom(LPCTSTR szFilename)
{
	HANDLE hRomFile;
	DWORD  dwSize,dwFileSize,dwRead;

	if (pbyRom != NULL) return FALSE;
	SetCurrentDirectory(szEmuDirectory);
	hRomFile = CreateFile(szFilename,
						  GENERIC_READ,
						  FILE_SHARE_READ,
						  NULL,
						  OPEN_EXISTING,
						  FILE_FLAG_SEQUENTIAL_SCAN,
						  NULL);
	SetCurrentDirectory(szCurrentDirectory);
	if (hRomFile == INVALID_HANDLE_VALUE) return FALSE;
	dwFileSize = GetFileSize(hRomFile, NULL);

	// read the first 4 bytes
	ReadFile(hRomFile,&dwSize,sizeof(dwSize),&dwRead,NULL);
	if (dwRead < sizeof(dwSize))
	{ // file is too small.
		CloseHandle(hRomFile);
		hRomFile = NULL;
		dwRomSize = 0;
		return FALSE;
	}

	dwRomSize = dwFileSize;					// calculate ROM image buffer size
	if ((dwSize & 0xF0F0F0F0) != 0)			// packed ROM image ->
		dwRomSize *= 2;						// unpacked ROM image has double size

	pbyRom = (LPBYTE) malloc(dwRomSize);
	if (pbyRom == NULL)
	{
		CloseHandle(hRomFile);
		dwRomSize = 0;
		return FALSE;
	}

	*(DWORD *) pbyRom = dwSize;				// save first 4 bytes

	// load rest of file content
	ReadFile(hRomFile,&pbyRom[sizeof(dwSize)],dwFileSize - sizeof(dwSize),&dwRead,NULL);
	_ASSERT(dwFileSize - sizeof(dwSize) == dwRead);
	CloseHandle(hRomFile);

	if (dwRomSize != dwFileSize)			// packed ROM image
	{
		UnpackRom(dwFileSize,dwRomSize);	// unpack ROM data
	}
	return TRUE;
}

BOOL MapRomBmp(HBITMAP hBmp)
{
	BOOL bBitmapROM;

	_ASSERT(pbyRom == NULL);				// no ROM must be loaded

	// look for an integrated ROM image
	bBitmapROM = SteganoDecodeHBm(&pbyRom, &dwRomSize, 8, hBmp) == STG_NOERROR;
	if (bBitmapROM)							// has data inside
	{
		DWORD  dwDataSize;

		LPBYTE pbyOutData = NULL;
		size_t nOutData = 0;

		// try to decompress data
		if (lodepng_zlib_decompress(&pbyOutData, &nOutData, pbyRom, dwRomSize,
			&lodepng_default_decompress_settings) == 0)
		{
			// data decompress successful
			free(pbyRom);					// free compressed data
			pbyRom = pbyOutData;			// use decompressed instead
			dwRomSize = (DWORD)nOutData;
		}

		dwDataSize = dwRomSize;				// packed ROM image size
		dwRomSize *= 2;						// unpacked ROM image has double size
		pbyOutData = (LPBYTE)realloc(pbyRom, dwRomSize);
		if (pbyOutData != NULL)
		{
			pbyRom = pbyOutData;
			UnpackRom(dwDataSize, dwRomSize); // unpack ROM data
		}
		else
		{
			free(pbyRom);
			pbyRom = NULL;
			bBitmapROM = FALSE;
		}
	}
	return bBitmapROM;
}

VOID UnmapRom(VOID)
{
	free(pbyRom);
	pbyRom = NULL;
	dwRomSize = 0;
	wRomCrc = 0;
	return;
}



//################
//#
//#    Documents
//#
//################

static BOOL IsDataPacked(VOID *pMem, DWORD dwSize)
{
	DWORD *pdwMem = (DWORD *) pMem;

	_ASSERT((dwSize % sizeof(DWORD)) == 0);
	if ((dwSize % sizeof(DWORD)) != 0) return TRUE;

	for (dwSize /= sizeof(DWORD); dwSize-- > 0;)
	{
		if ((*pdwMem++ & 0xF0F0F0F0) != 0)
			return TRUE;
	}
	return FALSE;
}

VOID ResetDocument(VOID)
{
	DisableDebugger();
	if (szCurrentKml[0])
	{
		KillKML();
	}
	if (hCurrentFile)
	{
		CloseHandle(hCurrentFile);
		hCurrentFile = NULL;
	}
	szCurrentKml[0] = 0;
	szCurrentFilename[0] = 0;
	if (pbyRamExt)
	{
		free(pbyRamExt);
		pbyRamExt = NULL;
	}
	ZeroMemory(&Chipset,sizeof(Chipset));
	ZeroMemory(&RMap,sizeof(RMap));			// delete MMU mappings
	ZeroMemory(&WMap,sizeof(WMap));
	bDocumentAvail = FALSE;					// document not available
	return;
}

BOOL NewDocument(VOID)
{
	SaveBackup();
	ResetDocument();

	if (!DisplayChooseKml(0)) goto restore;
	if (!InitKML(szCurrentKml,FALSE)) goto restore;
	Chipset.type = cCurrentRomType;
	CrcRom(&Chipset.wRomCrc);				// save fingerprint of loaded ROM

	switch (nCurrentClass)					// decode external RAM
	{
	case 0:  // no module
		break;
	case 4:  // Educalc #82-420  $65.95
		Chipset.Ext1Size = _KB(4);
		break;
	case 32: // Educalc #71-656  $149.95
		Chipset.Ext1Size = _KB(32);
		break;
	case 64: // Educalc #71-656B $294.95
		Chipset.Ext1Size = _KB(32);
		Chipset.Ext2Size = _KB(32);
		break;
	}

	if (EXTMEM)								// allocate external memory
	{
		pbyRamExt = (LPBYTE) calloc(EXTMEM,sizeof(*pbyRamExt));
		if (pbyRamExt == NULL) goto restore;
	}

	LoadBreakpointList(NULL);				// clear debugger breakpoint list
	bDocumentAvail = TRUE;					// document available
	return TRUE;
restore:
	RestoreBackup();
	ResetBackup();
	return FALSE;
}

BOOL OpenDocument(LPCTSTR szFilename)
{
	#define CHECKAREA(s,e) (offsetof(CHIPSET,e)-offsetof(CHIPSET,s)+sizeof(((CHIPSET *)NULL)->e))

	HANDLE hFile = INVALID_HANDLE_VALUE;
	DWORD  lBytesRead,lSizeofChipset;
	BYTE   byFileSignature[sizeof(bySignature)];
	UINT   nLength;

	// Open file
	if (lstrcmpi(szCurrentFilename,szFilename) == 0)
	{
		if (YesNoMessage(_T("Do you want to reload this document?")) == IDNO)
			return TRUE;
	}

	SaveBackup();
	ResetDocument();

	hFile = CreateFile(szFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		AbortMessage(_T("This file is missing or already loaded in another instance of Emu28."));
		goto restore;
	}

	// Read and Compare Emu28 1.0 format signature
	ReadFile(hFile, byFileSignature, sizeof(byFileSignature), &lBytesRead, NULL);
	if (memcmp(byFileSignature,bySignature,sizeof(byFileSignature)) != 0)
	{
		AbortMessage(_T("This file is not a valid Emu28 document."));
		goto restore;
	}

	// read length of KML script name
	ReadFile(hFile,&nLength,sizeof(nLength),&lBytesRead,NULL);

	// KML script name too long for file buffer
	if (nLength >= ARRAYSIZEOF(szCurrentKml))
	{
		// skip heading KML script name characters until remainder fits into file buffer
		UINT nSkip = nLength - (ARRAYSIZEOF(szCurrentKml) - 1);
		SetFilePointer(hFile, nSkip, NULL, FILE_CURRENT);

		nLength = ARRAYSIZEOF(szCurrentKml) - 1;
	}
	#if defined _UNICODE
	{
		LPSTR szTmp = (LPSTR) malloc(nLength);
		if (szTmp == NULL)
		{
			AbortMessage(_T("Memory Allocation Failure."));
			goto restore;
		}
		ReadFile(hFile, szTmp, nLength, &lBytesRead, NULL);
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, szTmp, lBytesRead,
							szCurrentKml, ARRAYSIZEOF(szCurrentKml));
		free(szTmp);
	}
	#else
	{
		ReadFile(hFile, szCurrentKml, nLength, &lBytesRead, NULL);
	}
	#endif
	if (nLength != lBytesRead) goto read_err;
	szCurrentKml[nLength] = 0;

	// read chipset size inside file
	ReadFile(hFile, &lSizeofChipset, sizeof(lSizeofChipset), &lBytesRead, NULL);
	if (lBytesRead != sizeof(lSizeofChipset)) goto read_err;
	if (lSizeofChipset <= sizeof(Chipset))	// actual or older chipset version
	{
		// read chipset content
		ZeroMemory(&Chipset,sizeof(Chipset)); // init chipset
		ReadFile(hFile, &Chipset, lSizeofChipset, &lBytesRead, NULL);
	}
	else									// newer chipset version
	{
		// read my used chipset content
		ReadFile(hFile, &Chipset, sizeof(Chipset), &lBytesRead, NULL);

		// skip rest of chipset
		SetFilePointer(hFile, lSizeofChipset-sizeof(Chipset), NULL, FILE_CURRENT);
		lSizeofChipset = sizeof(Chipset);
	}
	if (lBytesRead != lSizeofChipset) goto read_err;

	if (!isModelValid(Chipset.type))		// check for valid model in emulator state file
	{
		AbortMessage(_T("Emulator state file with invalid calculator model."));
		goto restore;
	}

	if (EXTMEM)
	{
		pbyRamExt = (LPBYTE) malloc(EXTMEM);
		if (pbyRamExt == NULL)
		{
			AbortMessage(_T("Memory Allocation Failure."));
			goto restore;
		}

		ReadFile(hFile, pbyRamExt, EXTMEM, &lBytesRead, NULL);
		if (lBytesRead != EXTMEM) goto read_err;

		// check if external RAM data is packed
		if (IsDataPacked(pbyRamExt,EXTMEM)) goto read_err;
	}

	SetWindowLocation(hWnd,Chipset.nPosX,Chipset.nPosY);

	while (TRUE)
	{
		if (szCurrentKml[0])				// KML file name
		{
			BOOL bOK = InitKML(szCurrentKml,FALSE);
			bOK = bOK && (cCurrentRomType == Chipset.type);
			if (bOK) break;

			KillKML();
		}
		if (!DisplayChooseKml(Chipset.type))
			goto restore;
	}
	// reload old button state
	ReloadButtons(Chipset.Keyboard_Row,ARRAYSIZEOF(Chipset.Keyboard_Row));

	if (Chipset.wRomCrc != wRomCrc)			// ROM changed
	{
		CpuReset();
		Chipset.Shutdn = FALSE;				// automatic restart
	}

	// check CPU main registers
	if (IsDataPacked(Chipset.A,CHECKAREA(A,R4))) goto read_err;

	// check internal IO and RAM data area
	if (IsDataPacked(Chipset.IORamM,CHECKAREA(IORamM,RamS))) goto read_err;

	LoadBreakpointList(hFile);				// load debugger breakpoint list

	lstrcpy(szCurrentFilename, szFilename);
	_ASSERT(hCurrentFile == NULL);
	hCurrentFile = hFile;
	#if defined _USRDLL						// DLL version
		// notify main proc about current document file
		if (pEmuDocumentNotify) pEmuDocumentNotify(szCurrentFilename);
	#endif
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	bDocumentAvail = TRUE;					// document available
	return TRUE;

read_err:
	AbortMessage(_T("This file must be truncated, and cannot be loaded."));
restore:
	if (INVALID_HANDLE_VALUE != hFile)		// close if valid handle
		CloseHandle(hFile);
	RestoreBackup();
	ResetBackup();
	return FALSE;
	#undef CHECKAREA
}

BOOL SaveDocument(VOID)
{
	DWORD           lBytesWritten;
	DWORD           lSizeofChipset;
	UINT            nLength;
	WINDOWPLACEMENT wndpl;

	if (hCurrentFile == NULL) return FALSE;

	_ASSERT(hWnd);							// window open
	wndpl.length = sizeof(wndpl);			// update saved window position
	GetWindowPlacement(hWnd, &wndpl);
	Chipset.nPosX = (SWORD) wndpl.rcNormalPosition.left;
	Chipset.nPosY = (SWORD) wndpl.rcNormalPosition.top;

	SetFilePointer(hCurrentFile,0,NULL,FILE_BEGIN);

	if (!WriteFile(hCurrentFile, bySignature, sizeof(bySignature), &lBytesWritten, NULL))
	{
		AbortMessage(_T("Could not write into file !"));
		return FALSE;
	}

	CrcRom(&Chipset.wRomCrc);				// save fingerprint of ROM

	nLength = lstrlen(szCurrentKml);
	WriteFile(hCurrentFile, &nLength, sizeof(nLength), &lBytesWritten, NULL);
	#if defined _UNICODE
	{
		LPSTR szTmp = (LPSTR) malloc(nLength);
		if (szTmp != NULL)
		{
			WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK,
								szCurrentKml, nLength,
								szTmp, nLength, NULL, NULL);
			WriteFile(hCurrentFile, szTmp, nLength, &lBytesWritten, NULL);
			free(szTmp);
		}
	}
	#else
	{
		WriteFile(hCurrentFile, szCurrentKml, nLength, &lBytesWritten, NULL);
	}
	#endif
	lSizeofChipset = sizeof(Chipset);
	WriteFile(hCurrentFile, &lSizeofChipset, sizeof(lSizeofChipset), &lBytesWritten, NULL);
	WriteFile(hCurrentFile, &Chipset, lSizeofChipset, &lBytesWritten, NULL);
	if (EXTMEM) WriteFile(hCurrentFile, pbyRamExt, EXTMEM, &lBytesWritten, NULL);
	SaveBreakpointList(hCurrentFile);		// save debugger breakpoint list
	SetEndOfFile(hCurrentFile);				// cut the rest
	return TRUE;
}

BOOL SaveDocumentAs(LPCTSTR szFilename)
{
	HANDLE hFile;

	if (hCurrentFile)						// already file in use
	{
		CloseHandle(hCurrentFile);			// close it, even it's same, so data always will be saved
		hCurrentFile = NULL;
	}
	hFile = CreateFile(szFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)		// error, couldn't create a new file
	{
		AbortMessage(_T("This file must be currently used by another instance of Emu28."));
		return FALSE;
	}
	lstrcpy(szCurrentFilename, szFilename);	// save new file name
	hCurrentFile = hFile;					// and the corresponding handle
	#if defined _USRDLL						// DLL version
		// notify main proc about current document file
		if (pEmuDocumentNotify) pEmuDocumentNotify(szCurrentFilename);
	#endif
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	return SaveDocument();					// save current content
}



//################
//#
//#    Backup
//#
//################

BOOL SaveBackup(VOID)
{
	WINDOWPLACEMENT wndpl;

	BOOL bSucc = TRUE;

	if (!bDocumentAvail) return FALSE;

	_ASSERT(nState != SM_RUN);				// emulation engine is running

	// save window position
	_ASSERT(hWnd);							// window open
	wndpl.length = sizeof(wndpl);			// update saved window position
	GetWindowPlacement(hWnd, &wndpl);
	Chipset.nPosX = (SWORD) wndpl.rcNormalPosition.left;
	Chipset.nPosY = (SWORD) wndpl.rcNormalPosition.top;

	lstrcpy(szBackupFilename, szCurrentFilename);
	lstrcpy(szBackupKml, szCurrentKml);
	if (pbyBackupRamExt)
	{
		free(pbyBackupRamExt);
		pbyBackupRamExt = NULL;
	}
	CopyMemory(&BackupChipset, &Chipset, sizeof(Chipset));
	if (EXTMEM)								// external RAM
	{
		pbyBackupRamExt = (LPBYTE) malloc(EXTMEM);
		if (pbyBackupRamExt)
		{
			CopyMemory(pbyBackupRamExt,pbyRamExt,EXTMEM);
		}
		bSucc = bSucc && (pbyBackupRamExt != NULL);
	}
	CreateBackupBreakpointList();
	bBackup = bSucc;
	return bSucc;
}

BOOL RestoreBackup(VOID)
{
	BOOL bDbgOpen;

	BOOL bSucc = TRUE;

	if (!bBackup) return FALSE;

	bDbgOpen = (nDbgState != DBG_OFF);		// debugger window open?
	ResetDocument();
	// need chipset for contrast setting in InitKML()
	Chipset.contrast = BackupChipset.contrast;
	if (!InitKML(szBackupKml,TRUE))
	{
		InitKML(szCurrentKml,TRUE);
		return FALSE;
	}
	lstrcpy(szCurrentKml, szBackupKml);
	lstrcpy(szCurrentFilename, szBackupFilename);
	if (szCurrentFilename[0])
	{
		hCurrentFile = CreateFile(szCurrentFilename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hCurrentFile == INVALID_HANDLE_VALUE)
		{
			hCurrentFile = NULL;
			szCurrentFilename[0] = 0;
		}
	}
	CopyMemory(&Chipset, &BackupChipset, sizeof(Chipset));
	if (EXTMEM)								// external RAM
	{
		pbyRamExt = (LPBYTE) malloc(EXTMEM);
		if (pbyRamExt)
		{
			CopyMemory(pbyRamExt,pbyBackupRamExt,EXTMEM);
		}
		bSucc = bSucc && (pbyRamExt != NULL);
	}
	SetWindowPathTitle(szCurrentFilename);	// update window title line
	SetWindowLocation(hWnd,Chipset.nPosX,Chipset.nPosY);
	RestoreBackupBreakpointList();			// restore the debugger breakpoint list
	if (bDbgOpen) OnToolDebug();			// reopen the debugger
	if (!bSucc)								// restore not successful (memory allocation errors)
	{
		ResetDocument();					// cleanup remainders
	}
	bDocumentAvail = bSucc;					// document available
	return bSucc;
}

BOOL ResetBackup(VOID)
{
	if (!bBackup) return FALSE;
	szBackupFilename[0] = 0;
	szBackupKml[0] = 0;
	if (pbyBackupRamExt)
	{
		free(pbyBackupRamExt);
		pbyBackupRamExt = NULL;
	}
	ZeroMemory(&BackupChipset,sizeof(BackupChipset));
	bBackup = FALSE;
	return TRUE;
}



//################
//#
//#    Open File Common Dialog Boxes
//#
//################

static VOID InitializeOFN(LPOPENFILENAME ofn)
{
	ZeroMemory((LPVOID)ofn, sizeof(OPENFILENAME));
	ofn->lStructSize = sizeof(OPENFILENAME);
	ofn->hwndOwner = hWnd;
	ofn->Flags = OFN_EXPLORER|OFN_HIDEREADONLY;
	return;
}

BOOL GetOpenFilename(VOID)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	InitializeOFN(&ofn);
	ofn.lpstrFilter =
		_T("Emu28 Files (*.e18;*.e28;*.e28c)\0*.e18;*.e28;*.e28c\0")
		_T("HP-18C Files (*.e18)\0*.e18\0")
		_T("HP-28C Files (*.e28;*.e28c)\0*.e28;*.e28c\0")
		_T("All Files (*.*)\0*.*\0");
	ofn.nFilterIndex = 1;					// default
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
	if (GetOpenFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	return TRUE;
}

BOOL GetSaveAsFilename(VOID)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	LPCTSTR lpstrDefExt = NULL;

	InitializeOFN(&ofn);
	ofn.lpstrFilter =
		_T("HP-18C Files (*.e18)\0*.e18\0")
		_T("HP-28C Files (*.e28;*.e28c)\0*.e28;*.e28c\0")
		_T("All Files (*.*)\0*.*\0");
	ofn.nFilterIndex = 3;					// default
	ofn.lpstrDefExt = NULL;
	if (cCurrentRomType == 'C')				// Champion, HP18C
	{
		lpstrDefExt = _T("e18");
		ofn.nFilterIndex = 1;
	}
	if (cCurrentRomType == 'P')				// Paladin, HP28C
	{
		lpstrDefExt = _T("e28c");
		ofn.nFilterIndex = 2;
	}
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_CREATEPROMPT|OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	// given filename has no file extension
	if (lpstrDefExt && ofn.nFileExtension == 0)
	{
		// actual name length
		UINT nLength = lstrlen(szBufferFilename);

		// destination buffer has room for the default extension
		if (nLength + 1 + lstrlen(lpstrDefExt) < ARRAYSIZEOF(szBufferFilename))
		{
			// add default extension
			szBufferFilename[nLength++] = _T('.');
			lstrcpy(&szBufferFilename[nLength], lpstrDefExt);
		}
	}
	return TRUE;
}

BOOL GetLoadObjectFilename(LPCTSTR lpstrFilter,LPCTSTR lpstrDefExt)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	InitializeOFN(&ofn);
	ofn.lpstrFilter = lpstrFilter;
	ofn.lpstrDefExt = lpstrDefExt;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
	if (GetOpenFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	return TRUE;
}

BOOL GetSaveObjectFilename(LPCTSTR lpstrFilter,LPCTSTR lpstrDefExt)
{
	TCHAR szBuffer[ARRAYSIZEOF(szBufferFilename)];
	OPENFILENAME ofn;

	InitializeOFN(&ofn);
	ofn.lpstrFilter = lpstrFilter;
	ofn.lpstrDefExt = NULL;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = szBuffer;
	ofn.lpstrFile[0] = 0;
	ofn.nMaxFile = ARRAYSIZEOF(szBuffer);
	ofn.Flags |= OFN_CREATEPROMPT|OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn) == FALSE) return FALSE;
	_ASSERT(ARRAYSIZEOF(szBufferFilename) == ofn.nMaxFile);
	lstrcpy(szBufferFilename, ofn.lpstrFile);
	if (ofn.nFileExtension == 0)			// given filename has no extension
	{
		// actual name length
		UINT nLength = lstrlen(szBufferFilename);

		// destination buffer has room for the default extension
		if (nLength + 1 + lstrlen(lpstrDefExt) < ARRAYSIZEOF(szBufferFilename))
		{
			// add default extension
			szBufferFilename[nLength++] = _T('.');
			lstrcpy(&szBufferFilename[nLength], lpstrDefExt);
		}
	}
	return TRUE;
}



//################
//#
//#    Load and Save HP28C Objects
//#
//################

WORD WriteStack(UINT nStkLevel,LPBYTE lpBuf,DWORD dwSize)	// separated from LoadObject()
{
	BOOL  bBinary;
	DWORD dwAddress, i;

	// check for HP28C binary header
	bBinary = (memcmp(&lpBuf[dwSize],BINARYHEADER28C,8) == 0);

	for (dwAddress = 0, i = 0; i < dwSize; i++)
	{
		BYTE byTwoNibs = lpBuf[i+dwSize];
		lpBuf[dwAddress++] = (BYTE)(byTwoNibs&0xF);
		lpBuf[dwAddress++] = (BYTE)(byTwoNibs>>4);
	}

	dwSize = dwAddress;						// unpacked buffer size

	if (bBinary == TRUE)
	{ // load as binary
		dwSize = RPL_ObjectSize(lpBuf+16,dwSize-16);
		if (dwSize == BAD_OB) return S_ERR_OBJECT;
		dwAddress = RPL_CreateTemp(dwSize);
		if (dwAddress == 0) return S_ERR_BINARY;
		Nwrite(lpBuf+16,dwAddress,dwSize);
	}
	else
	{ // load as string
		dwAddress = RPL_CreateTemp(dwSize+10);
		if (dwAddress == 0) return S_ERR_ASCII;
		Write5(dwAddress,0x02A4E);			// String
		Write5(dwAddress+5,dwSize+5);		// length of String
		Nwrite(lpBuf,dwAddress+10,dwSize);	// data
	}
	RPL_Push(nStkLevel,dwAddress);
	return S_ERR_NO;
}

BOOL LoadObject(LPCTSTR szFilename)			// separated stack writing part
{
	HANDLE hFile;
	DWORD  dwFileSizeLow;
	DWORD  dwFileSizeHigh;
	LPBYTE lpBuf;
	WORD   wError;

	hFile = CreateFile(szFilename,
					   GENERIC_READ,
					   FILE_SHARE_READ,
					   NULL,
					   OPEN_EXISTING,
					   FILE_FLAG_SEQUENTIAL_SCAN,
					   NULL);
	if (hFile == INVALID_HANDLE_VALUE) return FALSE;
	dwFileSizeLow = GetFileSize(hFile, &dwFileSizeHigh);
	if (dwFileSizeHigh != 0)
	{ // file is too large.
		CloseHandle(hFile);
		return FALSE;
	}
	lpBuf = (LPBYTE) malloc(dwFileSizeLow*2);
	if (lpBuf == NULL)
	{
		CloseHandle(hFile);
		return FALSE;
	}
	ReadFile(hFile, lpBuf+dwFileSizeLow, dwFileSizeLow, &dwFileSizeHigh, NULL);
	CloseHandle(hFile);

	wError = WriteStack(1,lpBuf,dwFileSizeLow);

	if (wError == S_ERR_OBJECT)
		AbortMessage(_T("This isn't a valid binary file."));

	if (wError == S_ERR_BINARY)
		AbortMessage(_T("The calculator does not have enough\nfree memory to load this binary file."));

	if (wError == S_ERR_ASCII)
		AbortMessage(_T("The calculator does not have enough\nfree memory to load this text file."));

	free(lpBuf);
	return (wError == S_ERR_NO);
}

BOOL SaveObject(LPCTSTR szFilename)			// separated stack reading part
{
	HANDLE hFile;
	UINT   uStkLvl;
	DWORD  lBytesWritten;
	DWORD  dwAddress;
	INT    nLength;

	_ASSERT(cCurrentRomType == 'P');

	// write object at stack level 1, if the calculator is off the object is
	// at stack level 2, stack level 1 contain the FALSE object in this case
	uStkLvl = ((Chipset.IORamM[DSPCTL]&DON) != 0) ? 1 : 2;

	dwAddress = RPL_Pick(uStkLvl);
	if (dwAddress == 0)
	{
		AbortMessage(_T("Too Few Arguments."));
		return FALSE;
	}

	hFile = CreateFile(szFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		AbortMessage(_T("Cannot open file."));
		return FALSE;
	}

	WriteFile(hFile, BINARYHEADER28C, 8, &lBytesWritten, NULL);

	nLength = RPL_SkipOb(dwAddress) - dwAddress;

	for (; nLength > 0; nLength -= 2)
	{
		BYTE byByte = Read2(dwAddress);
		if (nLength == 1) byByte &= 0xF;
		WriteFile(hFile, &byByte, sizeof(byByte), &lBytesWritten, NULL);
		dwAddress += 2;
	}
	CloseHandle(hFile);
	return TRUE;
}



//################
//#
//#    Load Icon
//#
//################

BOOL LoadIconFromFile(LPCTSTR szFilename)
{
	HANDLE hIcon;

	SetCurrentDirectory(szEmuDirectory);
	// not necessary to destroy because icon is shared
	hIcon = LoadImage(NULL, szFilename, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE|LR_LOADFROMFILE|LR_SHARED);
	SetCurrentDirectory(szCurrentDirectory);

	if (hIcon)
	{
		SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM) hIcon);
		SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM) hIcon);
	}
	return hIcon != NULL;
}

VOID LoadIconDefault(VOID)
{
	// use window class icon
	SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM) NULL);
	SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM) NULL);
	return;
}



//################
//#
//#    Load Bitmap
//#
//################

#define WIDTHBYTES(bits) (((bits) + 31) / 32 * 4)

typedef struct _BmpFile
{
	DWORD  dwPos;							// actual reading pos
	DWORD  dwFileSize;						// file size
	LPBYTE pbyFile;							// buffer
} BMPFILE, FAR *LPBMPFILE, *PBMPFILE;

static __inline WORD DibNumColors(__unaligned BITMAPINFOHEADER CONST *lpbi)
{
	if (lpbi->biClrUsed != 0) return (WORD) lpbi->biClrUsed;

	/* a 24 bitcount DIB has no color table */
	return (lpbi->biBitCount <= 8) ? (1 << lpbi->biBitCount) : 0;
}

static HPALETTE CreateBIPalette(__unaligned BITMAPINFOHEADER CONST *lpbi)
{
	LOGPALETTE* pPal;
	HPALETTE    hpal = NULL;
	WORD        wNumColors;
	BYTE        red;
	BYTE        green;
	BYTE        blue;
	UINT        i;
	__unaligned RGBQUAD* pRgb;

	if (!lpbi || lpbi->biSize != sizeof(BITMAPINFOHEADER))
		return NULL;

	// Get a pointer to the color table and the number of colors in it
	pRgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);
	wNumColors = DibNumColors(lpbi);

	if (wNumColors)
	{
		// Allocate for the logical palette structure
		pPal = (PLOGPALETTE) malloc(sizeof(LOGPALETTE) + wNumColors * sizeof(PALETTEENTRY));
		if (!pPal) return NULL;

		pPal->palVersion    = 0x300;
		pPal->palNumEntries = wNumColors;

		// Fill in the palette entries from the DIB color table and
		// create a logical color palette.
		for (i = 0; i < pPal->palNumEntries; i++)
		{
			pPal->palPalEntry[i].peRed   = pRgb[i].rgbRed;
			pPal->palPalEntry[i].peGreen = pRgb[i].rgbGreen;
			pPal->palPalEntry[i].peBlue  = pRgb[i].rgbBlue;
			pPal->palPalEntry[i].peFlags = 0;
		}
		hpal = CreatePalette(pPal);
		free(pPal);
	}
	else
	{
		// create halftone palette for 16, 24 and 32 bitcount bitmaps

		// 16, 24 and 32 bitcount DIB's have no color table entries so, set the
		// number of to the maximum value (256).
		wNumColors = 256;
		pPal = (PLOGPALETTE) malloc(sizeof(LOGPALETTE) + wNumColors * sizeof(PALETTEENTRY));
		if (!pPal) return NULL;

		pPal->palVersion    = 0x300;
		pPal->palNumEntries = wNumColors;

		red = green = blue = 0;

		// Generate 256 (= 8*8*4) RGB combinations to fill the palette
		// entries.
		for (i = 0; i < pPal->palNumEntries; i++)
		{
			pPal->palPalEntry[i].peRed   = red;
			pPal->palPalEntry[i].peGreen = green;
			pPal->palPalEntry[i].peBlue  = blue;
			pPal->palPalEntry[i].peFlags = 0;

			if (!(red += 32))
				if (!(green += 32))
					blue += 64;
		}
		hpal = CreatePalette(pPal);
		free(pPal);
	}
	return hpal;
}

static HBITMAP DecodeBmp(LPBMPFILE pBmp,BOOL bPalette)
{
	DWORD dwFileSize;

	HBITMAP hBitmap = NULL;

	// map memory to BITMAPFILEHEADER and BITMAPINFO
	const LPBITMAPFILEHEADER pBmfh = (LPBITMAPFILEHEADER) pBmp->pbyFile;
	const __unaligned LPBITMAPINFO pBmi = (__unaligned LPBITMAPINFO) & pBmfh[1];

	// size of bitmap header information & check for bitmap
	dwFileSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	if (   pBmp->dwFileSize < dwFileSize	// minimum size to read data from BITMAPFILEHEADER + BITMAPINFOHEADER
		|| pBmfh->bfType != 0x4D42)			// "BM"
		return NULL;

	// size with color table
	if (pBmi->bmiHeader.biCompression == BI_BITFIELDS)
	{
		dwFileSize += 3 * sizeof(DWORD);
	}
	else
	{
		dwFileSize += DibNumColors(&pBmi->bmiHeader) * sizeof(RGBQUAD);
	}
	if (dwFileSize != pBmfh->bfOffBits) return NULL;

	// size with bitmap data
	if (pBmi->bmiHeader.biCompression != BI_RGB)
	{
		dwFileSize += pBmi->bmiHeader.biSizeImage;
	}
	else
	{
		dwFileSize += WIDTHBYTES(pBmi->bmiHeader.biWidth * pBmi->bmiHeader.biBitCount)
					* labs(pBmi->bmiHeader.biHeight);
	}
	if (pBmp->dwFileSize < dwFileSize) return NULL;

	VERIFY(hBitmap = CreateDIBitmap(
		hWindowDC,
		&pBmi->bmiHeader,
		CBM_INIT,
		pBmp->pbyFile + pBmfh->bfOffBits,
		pBmi, DIB_RGB_COLORS));
	if (hBitmap == NULL) return NULL;

	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette(&pBmi->bmiHeader);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}
	return hBitmap;
}

static BOOL ReadGifByte(LPBMPFILE pGif, INT *n)
{
	// outside GIF file
	if (pGif->dwPos >= pGif->dwFileSize)
		return TRUE;

	*n = pGif->pbyFile[pGif->dwPos++];
	return FALSE;
}

static BOOL ReadGifWord(LPBMPFILE pGif, INT *n)
{
	// outside GIF file
	if (pGif->dwPos + 1 >= pGif->dwFileSize)
		return TRUE;

	*n = pGif->pbyFile[pGif->dwPos++];
	*n |= (pGif->pbyFile[pGif->dwPos++] << 8);
	return FALSE;
}

static HBITMAP DecodeGif(LPBMPFILE pBmp,DWORD *pdwTransparentColor,BOOL bPalette)
{
	// this implementation base on the GIF image file
	// decoder engine of Free42 (c) by Thomas Okken

	HBITMAP hBitmap;

	typedef struct cmap
	{
		WORD    biBitCount;					// bits used in color map
		DWORD   biClrUsed;					// no of colors in color map
		RGBQUAD bmiColors[256];				// color map
	} CMAP;

	BOOL    bHasGlobalCmap;
	CMAP    sGlb;							// data of global color map

	INT     nWidth,nHeight,nInfo,nBackground,nZero;
	LONG    lBytesPerLine;

	LPBYTE  pbyPixels;

	BITMAPINFO bmi;							// global bitmap info

	BOOL bDecoding = TRUE;

	hBitmap = NULL;

	pBmp->dwPos = 6;						// position behind GIF header

	/* Bits 6..4 of info contain one less than the "color resolution",
	 * defined as the number of significant bits per RGB component in
	 * the source image's color palette. If the source image (from
	 * which the GIF was generated) was 24-bit true color, the color
	 * resolution is 8, so the value in bits 6..4 is 7. If the source
	 * image had an EGA color cube (2x2x2), the color resolution would
	 * be 2, etc.
	 * Bit 3 of info must be zero in GIF87a; in GIF89a, if it is set,
	 * it indicates that the global colormap is sorted, the most
	 * important entries being first. In PseudoColor environments this
	 * can be used to make sure to get the most important colors from
	 * the X server first, to optimize the image's appearance in the
	 * event that not all the colors from the colormap can actually be
	 * obtained at the same time.
	 * The 'zero' field is always 0 in GIF87a; in GIF89a, it indicates
	 * the pixel aspect ratio, as (PAR + 15) : 64. If PAR is zero,
	 * this means no aspect ratio information is given, PAR = 1 means
	 * 1:4 (narrow), PAR = 49 means 1:1 (square), PAR = 255 means
	 * slightly over 4:1 (wide), etc.
	 */

	if (   ReadGifWord(pBmp,&nWidth)
		|| ReadGifWord(pBmp,&nHeight)
		|| ReadGifByte(pBmp,&nInfo)
		|| ReadGifByte(pBmp,&nBackground)
		|| ReadGifByte(pBmp,&nZero))
		goto quit;

	ZeroMemory(&bmi,sizeof(bmi));			// init bitmap info
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = nWidth;
	bmi.bmiHeader.biHeight = nHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;			// create a true color DIB
	bmi.bmiHeader.biCompression = BI_RGB;

	ZeroMemory(&sGlb,sizeof(sGlb));			// init global color map
	bHasGlobalCmap = (nInfo & 0x80) != 0;

	sGlb.biBitCount = (nInfo & 7) + 1;		// bits used in global color map
	sGlb.biClrUsed = (1 << sGlb.biBitCount); // no of colors in global color map

	// color table should not exceed 256 colors
	_ASSERT(sGlb.biClrUsed <= ARRAYSIZEOF(sGlb.bmiColors));

	if (bHasGlobalCmap)						// global color map
	{
		DWORD i;

		for (i = 0; i < sGlb.biClrUsed; ++i)
		{
			int r, g, b;

			if (ReadGifByte(pBmp,&r) || ReadGifByte(pBmp,&g) || ReadGifByte(pBmp,&b))
				goto quit;

			sGlb.bmiColors[i].rgbRed   = r;
			sGlb.bmiColors[i].rgbGreen = g;
			sGlb.bmiColors[i].rgbBlue  = b;
		}
	}
	else									// no color map
	{
		DWORD i;

		for (i = 0; i < sGlb.biClrUsed; ++i)
		{
			BYTE k = (BYTE) ((i * sGlb.biClrUsed) / (sGlb.biClrUsed - 1));

			sGlb.bmiColors[i].rgbRed   = k;
			sGlb.bmiColors[i].rgbGreen = k;
			sGlb.bmiColors[i].rgbBlue  = k;
		}
	}

	// bitmap dimensions
	lBytesPerLine = WIDTHBYTES(bmi.bmiHeader.biWidth * bmi.bmiHeader.biBitCount);
	bmi.bmiHeader.biSizeImage = lBytesPerLine * bmi.bmiHeader.biHeight;

	// create top-down DIB
	bmi.bmiHeader.biHeight = -bmi.bmiHeader.biHeight;

	// allocate buffer for pixels
	VERIFY(hBitmap = CreateDIBSection(hWindowDC,
									  &bmi,
									  DIB_RGB_COLORS,
									  (VOID **)&pbyPixels,
									  NULL,
									  0));
	if (hBitmap == NULL) goto quit;

	// fill pixel buffer with background color
	for (nHeight = 0; nHeight < labs(bmi.bmiHeader.biHeight); ++nHeight)
	{
		LPBYTE pbyLine = pbyPixels + nHeight * lBytesPerLine;

		for (nWidth = 0; nWidth < bmi.bmiHeader.biWidth; ++nWidth)
		{
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbBlue;
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbGreen;
			*pbyLine++ = sGlb.bmiColors[nBackground].rgbRed;
		}

		_ASSERT((DWORD) (pbyLine - pbyPixels) <= bmi.bmiHeader.biSizeImage);
	}

	while (bDecoding)
	{
		INT nBlockType;

		if (ReadGifByte(pBmp,&nBlockType)) goto quit;

		switch (nBlockType)
		{
		case ',': // image
			{
				CMAP sAct;					// data of actual color map

				INT  nLeft,nTop,nWidth,nHeight;
				INT  i,nInfo;

				BOOL bInterlaced;
				INT h,v;
				INT nCodesize;				// LZW codesize in bits
				INT nBytecount;

				SHORT prefix_table[4096];
				SHORT code_table[4096];

				INT  nMaxcode;
				INT  nClearCode;
				INT  nEndCode;

				INT  nCurrCodesize;
				INT  nCurrCode;
				INT  nOldCode;
				INT  nBitsNeeded;
				BOOL bEndCodeSeen;

				// read image dimensions
				if (   ReadGifWord(pBmp,&nLeft)
					|| ReadGifWord(pBmp,&nTop)
					|| ReadGifWord(pBmp,&nWidth)
					|| ReadGifWord(pBmp,&nHeight)
					|| ReadGifByte(pBmp,&nInfo))
					goto quit;

				if (   nTop + nHeight > labs(bmi.bmiHeader.biHeight)
					|| nLeft + nWidth > bmi.bmiHeader.biWidth)
					goto quit;

				/* Bit 3 of info must be zero in GIF87a; in GIF89a, if it
				 * is set, it indicates that the local colormap is sorted,
				 * the most important entries being first. In PseudoColor
				 * environments this can be used to make sure to get the
				 * most important colors from the X server first, to
				 * optimize the image's appearance in the event that not
				 * all the colors from the colormap can actually be
				 * obtained at the same time.
				 */

				if ((nInfo & 0x80) == 0)	// using global color map
				{
					sAct = sGlb;
				}
				else						// using local color map
				{
					DWORD i;

					sAct.biBitCount = (nInfo & 7) + 1;	// bits used in color map
					sAct.biClrUsed = (1 << sAct.biBitCount); // no of colors in color map

					for (i = 0; i < sAct.biClrUsed; ++i)
					{
						int r, g, b;

						if (ReadGifByte(pBmp,&r) || ReadGifByte(pBmp,&g) || ReadGifByte(pBmp,&b))
							goto quit;

						sAct.bmiColors[i].rgbRed   = r;
						sAct.bmiColors[i].rgbGreen = g;
						sAct.bmiColors[i].rgbBlue  = b;
					}
				}

				// interlaced image
				bInterlaced = (nInfo & 0x40) != 0;

				h = 0;
				v = 0;
				if (   ReadGifByte(pBmp,&nCodesize)
					|| ReadGifByte(pBmp,&nBytecount))
					goto quit;

				nMaxcode = (1 << nCodesize);

				// preset LZW table
				for (i = 0; i < nMaxcode + 2; ++i)
				{
					prefix_table[i] = -1;
					code_table[i] = i;
				}
				nClearCode = nMaxcode++;
				nEndCode = nMaxcode++;

				nCurrCodesize = nCodesize + 1;
				nCurrCode = 0;
				nOldCode = -1;
				nBitsNeeded = nCurrCodesize;
				bEndCodeSeen = FALSE;

				while (nBytecount != 0)
				{
					for (i = 0; i < nBytecount; ++i)
					{
						INT nCurrByte;
						INT nBitsAvailable;

						if (ReadGifByte(pBmp,&nCurrByte))
							goto quit;

						if (bEndCodeSeen) continue;

						nBitsAvailable = 8;
						while (nBitsAvailable != 0)
						{
							INT nBitsCopied = (nBitsNeeded < nBitsAvailable)
											? nBitsNeeded
											: nBitsAvailable;

							INT nBits = nCurrByte >> (8 - nBitsAvailable);

							nBits &= 0xFF >> (8 - nBitsCopied);
							nCurrCode |= nBits << (nCurrCodesize - nBitsNeeded);
							nBitsAvailable -= nBitsCopied;
							nBitsNeeded -= nBitsCopied;

							if (nBitsNeeded == 0)
							{
								BYTE byExpanded[4096];
								INT  nExplen;

								do
								{
									if (nCurrCode == nEndCode)
									{
										bEndCodeSeen = TRUE;
										break;
									}

									if (nCurrCode == nClearCode)
									{
										nMaxcode = (1 << nCodesize) + 2;
										nCurrCodesize = nCodesize + 1;
										nOldCode = -1;
										break;
									}

									if (nCurrCode < nMaxcode)
									{
										if (nMaxcode < 4096 && nOldCode != -1)
										{
											INT c = nCurrCode;
											while (prefix_table[c] != -1)
												c = prefix_table[c];
											c = code_table[c];
											prefix_table[nMaxcode] = nOldCode;
											code_table[nMaxcode] = c;
											nMaxcode++;
											if (nMaxcode == (1 << nCurrCodesize) && nCurrCodesize < 12)
												nCurrCodesize++;
										}
									}
									else
									{
										INT c;

										if (nCurrCode > nMaxcode || nOldCode == -1) goto quit;

										_ASSERT(nCurrCode == nMaxcode);

										/* Once maxcode == 4096, we can't get here
										 * any more, because we refuse to raise
										 * nCurrCodeSize above 12 -- so we can
										 * never read a bigger code than 4095.
										 */

										c = nOldCode;
										while (prefix_table[c] != -1)
											c = prefix_table[c];
										c = code_table[c];
										prefix_table[nMaxcode] = nOldCode;
										code_table[nMaxcode] = c;
										nMaxcode++;

										if (nMaxcode == (1 << nCurrCodesize) && nCurrCodesize < 12)
											nCurrCodesize++;
									}
									nOldCode = nCurrCode;

									// output nCurrCode!
									nExplen = 0;
									while (nCurrCode != -1)
									{
										_ASSERT(nExplen < ARRAYSIZEOF(byExpanded));
										byExpanded[nExplen++] = (BYTE) code_table[nCurrCode];
										nCurrCode = prefix_table[nCurrCode];
									}
									_ASSERT(nExplen > 0);

									while (--nExplen >= 0)
									{
										// get color map index
										BYTE byColIndex = byExpanded[nExplen];

										LPBYTE pbyRgbr = pbyPixels + (lBytesPerLine * (nTop + v) + 3 * (nLeft + h));

										_ASSERT(pbyRgbr + 2 < pbyPixels + bmi.bmiHeader.biSizeImage);
										_ASSERT(byColIndex < sAct.biClrUsed);

										*pbyRgbr++ = sAct.bmiColors[byColIndex].rgbBlue;
										*pbyRgbr++ = sAct.bmiColors[byColIndex].rgbGreen;
										*pbyRgbr   = sAct.bmiColors[byColIndex].rgbRed;

										if (++h == nWidth)
										{
											h = 0;
											if (bInterlaced)
											{
												switch (v & 7)
												{
												case 0:
													v += 8;
													if (v < nHeight)
														break;
													/* Some GIF en/decoders go
													 * straight from the '0'
													 * pass to the '4' pass
													 * without checking the
													 * height, and blow up on
													 * 2/3/4 pixel high
													 * interlaced images.
													 */
													if (nHeight > 4)
														v = 4;
													else
														if (nHeight > 2)
															v = 2;
														else
															if (nHeight > 1)
																v = 1;
															else
																bEndCodeSeen = TRUE;
													break;
												case 4:
													v += 8;
													if (v >= nHeight)
														v = 2;
													break;
												case 2:
												case 6:
													v += 4;
													if (v >= nHeight)
														v = 1;
													break;
												case 1:
												case 3:
												case 5:
												case 7:
													v += 2;
													if (v >= nHeight)
														bEndCodeSeen = TRUE;
													break;
												}
												if (bEndCodeSeen)
													break; // while (--nExplen >= 0)
											}
											else // non interlaced
											{
												if (++v == nHeight)
												{
													bEndCodeSeen = TRUE;
													break; // while (--nExplen >= 0)
												}
											}
										}
									}
								}
								while (FALSE);

								nCurrCode = 0;
								nBitsNeeded = nCurrCodesize;
							}
						}
					}

					if (ReadGifByte(pBmp,&nBytecount))
						goto quit;
				}
			}
			break;

		case '!': // extension block
			{
				INT i,nFunctionCode,nByteCount,nDummy;

				if (ReadGifByte(pBmp,&nFunctionCode)) goto quit;
				if (ReadGifByte(pBmp,&nByteCount)) goto quit;

				// Graphic Control Label & correct Block Size
				if (nFunctionCode == 0xF9 && nByteCount == 0x04)
				{
					INT nPackedFields,nColorIndex;

					// packed fields
					if (ReadGifByte(pBmp,&nPackedFields)) goto quit;

					// delay time
					if (ReadGifWord(pBmp,&nDummy)) goto quit;

					// transparent color index
					if (ReadGifByte(pBmp,&nColorIndex)) goto quit;

					// transparent color flag set
					if ((nPackedFields & 0x1) != 0)
					{
						if (pdwTransparentColor != NULL)
						{
							*pdwTransparentColor = RGB(sGlb.bmiColors[nColorIndex].rgbRed,
													   sGlb.bmiColors[nColorIndex].rgbGreen,
													   sGlb.bmiColors[nColorIndex].rgbBlue);
						}
					}

					// block terminator (0 byte)
					if (!(!ReadGifByte(pBmp,&nDummy) && nDummy == 0)) goto quit;
				}
				else // other function
				{
					while (nByteCount != 0)
					{
						for (i = 0; i < nByteCount; ++i)
						{
							if (ReadGifByte(pBmp,&nDummy)) goto quit;
						}

						if (ReadGifByte(pBmp,&nByteCount)) goto quit;
					}
				}
			}
			break;

		case ';': // terminator
			bDecoding = FALSE;
			break;

		default: goto quit;
		}
	}

	_ASSERT(bDecoding == FALSE);			// decoding successful

	// normal decoding exit
	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette((PBITMAPINFOHEADER) &bmi);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}

quit:
	if (hBitmap != NULL && bDecoding)		// creation failed
	{
		DeleteObject(hBitmap);				// delete bitmap
		hBitmap = NULL;
	}
	return hBitmap;
}

static HBITMAP DecodePng(LPBMPFILE pBmp,BOOL bPalette)
{
	// this implementation use the PNG image file decoder
	// engine of Copyright (c) 2005-2018 Lode Vandevenne

	HBITMAP hBitmap;

	UINT    uError,uWidth,uHeight;
	INT     nWidth,nHeight;
	LONG    lBytesPerLine;

	LPBYTE  pbyImage;						// PNG RGB image data
	LPBYTE  pbySrc;							// source buffer pointer
	LPBYTE  pbyPixels;						// BMP buffer

	BITMAPINFO bmi;

	hBitmap = NULL;
	pbyImage = NULL;

	// decode PNG image
	uError = lodepng_decode_memory(&pbyImage,&uWidth,&uHeight,pBmp->pbyFile,pBmp->dwFileSize,LCT_RGB,8);
	if (uError) goto quit;

	ZeroMemory(&bmi,sizeof(bmi));			// init bitmap info
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = (LONG) uWidth;
	bmi.bmiHeader.biHeight = (LONG) uHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;			// create a true color DIB
	bmi.bmiHeader.biCompression = BI_RGB;

	// bitmap dimensions
	lBytesPerLine = WIDTHBYTES(bmi.bmiHeader.biWidth * bmi.bmiHeader.biBitCount);
	bmi.bmiHeader.biSizeImage = lBytesPerLine * bmi.bmiHeader.biHeight;

	// allocate buffer for pixels
	VERIFY(hBitmap = CreateDIBSection(hWindowDC,
									  &bmi,
									  DIB_RGB_COLORS,
									  (VOID **)&pbyPixels,
									  NULL,
									  0));
	if (hBitmap == NULL) goto quit;

	pbySrc = pbyImage;						// init source loop pointer
	pbyPixels += bmi.bmiHeader.biSizeImage;	// end of destination bitmap

	// fill bottom up DIB pixel buffer with color information
	for (nHeight = 0; nHeight < bmi.bmiHeader.biHeight; ++nHeight)
	{
		LPBYTE pbyLine;

		pbyPixels -= lBytesPerLine;			// begin of previous row
		pbyLine = pbyPixels;				// row working copy

		for (nWidth = 0; nWidth < bmi.bmiHeader.biWidth; ++nWidth)
		{
			*pbyLine++ = pbySrc[2];			// blue
			*pbyLine++ = pbySrc[1];			// green
			*pbyLine++ = pbySrc[0];			// red
			pbySrc += 3;
		}
	}

	if (bPalette && hPalette == NULL)
	{
		hPalette = CreateBIPalette((PBITMAPINFOHEADER) &bmi);
		// save old palette
		hOldPalette = SelectPalette(hWindowDC, hPalette, FALSE);
		RealizePalette(hWindowDC);
	}

quit:
	free(pbyImage);							// free allocated PNG image data
	return hBitmap;
}

HBITMAP LoadBitmapFile(LPCTSTR szFilename,BOOL bPalette)
{
	HANDLE  hFile;
	HANDLE  hMap;
	BMPFILE Bmp;
	HBITMAP hBitmap;

	SetCurrentDirectory(szEmuDirectory);
	hFile = CreateFile(szFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	SetCurrentDirectory(szCurrentDirectory);
	if (hFile == INVALID_HANDLE_VALUE) return NULL;
	Bmp.dwFileSize = GetFileSize(hFile, NULL);
	hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (hMap == NULL)
	{
		CloseHandle(hFile);
		return NULL;
	}
	Bmp.pbyFile = (LPBYTE) MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (Bmp.pbyFile == NULL)
	{
		CloseHandle(hMap);
		CloseHandle(hFile);
		return NULL;
	}

	do
	{
		// check for bitmap file header "BM"
		if (Bmp.dwFileSize >= 2 && *(WORD *) Bmp.pbyFile == 0x4D42)
		{
			hBitmap = DecodeBmp(&Bmp,bPalette);
			break;
		}

		// check for GIF file header
		if (   Bmp.dwFileSize >= 6
			&& (memcmp(Bmp.pbyFile,"GIF87a",6) == 0 || memcmp(Bmp.pbyFile,"GIF89a",6) == 0))
		{
			hBitmap = DecodeGif(&Bmp,&dwTColor,bPalette);
			break;
		}

		// check for PNG file header
		if (Bmp.dwFileSize >= 8 && memcmp(Bmp.pbyFile,"\x89PNG\r\n\x1a\n",8) == 0)
		{
			hBitmap = DecodePng(&Bmp,bPalette);
			break;
		}

		// unknown file type
		hBitmap = NULL;
	}
	while (FALSE);

	UnmapViewOfFile(Bmp.pbyFile);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return hBitmap;
}

static BOOL AbsColorCmp(DWORD dwColor1,DWORD dwColor2,DWORD dwTol)
{
	DWORD dwDiff;

	dwDiff =  (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	dwDiff += (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	dwDiff += (DWORD) abs((INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF));

	return dwDiff > dwTol;					// FALSE = colors match
}

static BOOL LabColorCmp(DWORD dwColor1,DWORD dwColor2,DWORD dwTol)
{
	DWORD dwDiff;
	INT   nDiffCol;

	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff = (DWORD) (nDiffCol * nDiffCol);
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff += (DWORD) (nDiffCol * nDiffCol);
	dwColor1 >>= 8;
	dwColor2 >>= 8;
	nDiffCol = (INT) (dwColor1 & 0xFF) - (INT) (dwColor2 & 0xFF);
	dwDiff += (DWORD) (nDiffCol * nDiffCol);
	dwTol *= dwTol;

	return dwDiff > dwTol;					// FALSE = colors match
}

static DWORD EncodeColorBits(DWORD dwColorVal,DWORD dwMask)
{
	#define MAXBIT 32
	UINT uLshift = MAXBIT;
	UINT uRshift = 8;
	DWORD dwBitMask = dwMask;

	dwColorVal &= 0xFF;						// the color component using the lowest 8 bit

	// position of highest bit
	while ((dwBitMask & (1<<(MAXBIT-1))) == 0 && uLshift > 0)
	{
		dwBitMask <<= 1;					// next bit
		--uLshift;							// next position
	}

	if (uLshift > 24)						// avoid overflow on 32bit mask
	{
		uLshift -= uRshift;					// normalize left shift
		uRshift = 0;
	}

	return ((dwColorVal << uLshift) >> uRshift) & dwMask;
	#undef MAXBIT
}

HRGN CreateRgnFromBitmap(HBITMAP hBmp,COLORREF color,DWORD dwTol)
{
	#define ADD_RECTS_COUNT  256

	BOOL (*fnColorCmp)(DWORD dwColor1,DWORD dwColor2,DWORD dwTol);

	DWORD dwRed,dwGreen,dwBlue;
	LPRGNDATA pRgnData;
	LPBITMAPINFO bi;
	LPBYTE pbyBits;
	LPBYTE pbyColor;
	DWORD dwAlignedWidthBytes;
	DWORD dwBpp;
	DWORD dwRectsCount;
	LONG x,y,xleft;
	BOOL bFoundLeft;
	BOOL bIsMask;

	HRGN hRgn = NULL;						// no region defined

	if (dwTol >= 1000)						// use CIE L*a*b compare
	{
		fnColorCmp = LabColorCmp;
		dwTol -= 1000;						// remove L*a*b compare selector
	}
	else									// use Abs summation compare
	{
		fnColorCmp = AbsColorCmp;
	}

	// allocate memory for extended image information incl. RGBQUAD color table
	if ((bi = (LPBITMAPINFO) calloc(1,sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD))) == NULL)
	{
		return hRgn;						// no region
	}
	bi->bmiHeader.biSize = sizeof(bi->bmiHeader);
	_ASSERT(bi->bmiHeader.biBitCount == 0); // for query without color table

	// get information about image
	GetDIBits(hWindowDC,hBmp,0,0,NULL,bi,DIB_RGB_COLORS);

	// DWORD aligned bitmap width in BYTES
	dwAlignedWidthBytes = WIDTHBYTES(  bi->bmiHeader.biWidth
									 * bi->bmiHeader.biPlanes
									 * bi->bmiHeader.biBitCount);

	// biSizeImage is empty
	if (bi->bmiHeader.biSizeImage == 0 && bi->bmiHeader.biCompression == BI_RGB)
	{
		bi->bmiHeader.biSizeImage = dwAlignedWidthBytes * bi->bmiHeader.biHeight;
	}

	// allocate memory for image data (colors)
	if ((pbyBits = (LPBYTE) malloc(bi->bmiHeader.biSizeImage)) == NULL)
	{
		free(bi);							// free bitmap info
		return hRgn;						// no region
	}

	// fill bits buffer
	GetDIBits(hWindowDC,hBmp,0,bi->bmiHeader.biHeight,pbyBits,bi,DIB_RGB_COLORS);

	// convert color if current DC is 16-bit/32-bit bitfield coded
	if (bi->bmiHeader.biCompression == BI_BITFIELDS)
	{
		dwRed   = *(LPDWORD) &bi->bmiColors[0];
		dwGreen = *(LPDWORD) &bi->bmiColors[1];
		dwBlue  = *(LPDWORD) &bi->bmiColors[2];
	}
	else // RGB coded
	{
		// convert color if current DC is 16-bit RGB coded
		if (bi->bmiHeader.biBitCount == 16)
		{
			// for 15 bit (5:5:5)
			dwRed   = 0x00007C00;
			dwGreen = 0x000003E0;
			dwBlue  = 0x0000001F;
		}
		else
		{
			// convert COLORREF to RGBQUAD color
			dwRed   = 0x00FF0000;
			dwGreen = 0x0000FF00;
			dwBlue  = 0x000000FF;
		}
	}
	color = EncodeColorBits((color >> 16), dwBlue)
		  | EncodeColorBits((color >>  8), dwGreen)
		  | EncodeColorBits((color >>  0), dwRed);

	dwBpp = bi->bmiHeader.biBitCount >> 3;	// bytes per pixel

	// DIB is bottom up image so we begin with the last scanline
	pbyColor = pbyBits + (bi->bmiHeader.biHeight - 1) * dwAlignedWidthBytes;

	dwRectsCount = bi->bmiHeader.biHeight;	// number of rects in allocated buffer

	bFoundLeft = FALSE;						// set when mask has been found in current scan line

	// allocate memory for region data
	pRgnData = (PRGNDATA) malloc(sizeof(RGNDATAHEADER) + dwRectsCount * sizeof(RECT));
	if (pRgnData)
	{
		// fill it by default
		ZeroMemory(&pRgnData->rdh,sizeof(pRgnData->rdh));
		pRgnData->rdh.dwSize = sizeof(pRgnData->rdh);
		pRgnData->rdh.iType = RDH_RECTANGLES;
		SetRect(&pRgnData->rdh.rcBound,MAXLONG,MAXLONG,0,0);
	}

	for (y = 0; pRgnData && y < bi->bmiHeader.biHeight; ++y)
	{
		LPBYTE pbyLineStart = pbyColor;

		for (x = 0; pRgnData && x < bi->bmiHeader.biWidth; ++x)
		{
			// get color
			switch (bi->bmiHeader.biBitCount)
			{
			case 8:
				bIsMask = fnColorCmp(*(LPDWORD)(&bi->bmiColors)[*pbyColor],color,dwTol);
				break;
			case 16:
				// it makes no sense to allow a tolerance here
				bIsMask = (*(LPWORD)pbyColor != (WORD) color);
				break;
			case 24:
				bIsMask = fnColorCmp((*(LPDWORD)pbyColor & 0x00ffffff),color,dwTol);
				break;
			case 32:
				bIsMask = fnColorCmp(*(LPDWORD)pbyColor,color,dwTol);
			}
			pbyColor += dwBpp;				// shift pointer to next color

			if (!bFoundLeft && bIsMask)		// non transparent color found
			{
				xleft = x;
				bFoundLeft = TRUE;
			}

			if (bFoundLeft)					// found non transparent color in scanline
			{
				// transparent color or last column
				if (!bIsMask || x + 1 == bi->bmiHeader.biWidth)
				{
					// non transparent color and last column
					if (bIsMask && x + 1 == bi->bmiHeader.biWidth)
						++x;

					// save current RECT
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].left = xleft;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].top  = y;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].right = x;
					((LPRECT) pRgnData->Buffer)[pRgnData->rdh.nCount].bottom = y + 1;
					pRgnData->rdh.nCount++;

					if (xleft < pRgnData->rdh.rcBound.left)
						pRgnData->rdh.rcBound.left = xleft;

					if (y < pRgnData->rdh.rcBound.top)
						pRgnData->rdh.rcBound.top = y;

					if (x > pRgnData->rdh.rcBound.right)
						pRgnData->rdh.rcBound.right = x;

					if (y + 1 > pRgnData->rdh.rcBound.bottom)
						pRgnData->rdh.rcBound.bottom = y + 1;

					// if buffer full reallocate it with more room
					if (pRgnData->rdh.nCount >= dwRectsCount)
					{
						LPRGNDATA pNewRgnData;

						dwRectsCount += ADD_RECTS_COUNT;
						pNewRgnData = (LPRGNDATA) realloc(pRgnData,sizeof(RGNDATAHEADER) + dwRectsCount * sizeof(RECT));
						if (pNewRgnData)
						{
							pRgnData = pNewRgnData;
						}
						else
						{
							free(pRgnData);
							pRgnData = NULL;
						}
					}

					bFoundLeft = FALSE;
				}
			}
		}

		// previous scanline
		pbyColor = pbyLineStart - dwAlignedWidthBytes;
	}
	// release image data
	free(pbyBits);
	free(bi);

	if (pRgnData)							// has region data, create region
	{
		hRgn = ExtCreateRegion(NULL,sizeof(RGNDATAHEADER) + pRgnData->rdh.nCount * sizeof(RECT),pRgnData);
		free(pRgnData);
	}
	return hRgn;
	#undef ADD_RECTS_COUNT
}
