/*
 *   mops.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "ops.h"
#include "opcodes.h"
#include "io.h"

// #define DEBUG_IO							// switch for I/O debug purpose
// #define DEBUG_MEMACC						// switch for MEMORY access debug purpose

// defines for reading an open data bus
#define READEVEN	0x0A
#define READODD		0x03

#define MAPMASK(s)	((~((s)-1)&0xFFFFF)>>ADDR_BITS)
#define MAPSIZE(s)	(((s)-1)>>ADDR_BITS)

#define MAPMASKs(s)	MAPMASK(sizeof(s))		// Centipede chip
#define MAPSIZEs(s)	MAPSIZE(sizeof(s))

BOOL bLowBatDisable = FALSE;

// function prototypes
static VOID ReadIO(BYTE *a, DWORD b, DWORD s, BOOL bUpdate);
static VOID WriteIO(BYTE *a, DWORD b, DWORD s);
static VOID ReadSlaveIO(BYTE *a, DWORD d, DWORD s, BOOL bUpdate);
static VOID WriteSlaveIO(BYTE *a, DWORD d, DWORD s);

static __inline UINT MIN(UINT a, UINT b)
{
	return (a<b)?a:b;
}

static __inline UINT MAX(UINT a, UINT b)
{
	return (a>b)?a:b;
}

static DWORD ModuleId(DWORD dwSize)			// module size value must be a power of 2
{
	DWORD dwID = 0x8000F;					// ID for RAM with 512 bytes

	// nibble 0: F = 1K nibble
	//           E = 2
	//           D = 4
	//           C = 8
	//           B = 16
	//           A = 32
	//           9 = 64  (max RAM)
	//           8 = 128
	//           7 = 256 (max)

	dwSize >>= (10 + 1);					// no. of 1K nibble pages - 1
	while (dwSize)							// generate nibble 0
	{
		dwSize >>= 1;						// next bit
		--dwID;								// next ID
	}

	_ASSERT(dwID >= 0x80007);				// ID for <= 128KB
	return dwID;
}

// port mapping
LPBYTE RMap[1<<PAGE_BITS] = {NULL,};
LPBYTE WMap[1<<PAGE_BITS] = {NULL,};

// values for chip enables
enum NCEPORT { MASTER_RAM = 0, MASTER_IO, SLAVE_RAM, SLAVE_IO };
enum NCEEXT  { EXT1_RAM = 0, EXT2_RAM };

typedef struct
{
	BOOL	*pbCfg;							// config
	WORD	*pwBase;						// base adress
	WORD	wMask;							// don't care mask
	WORD	wSize;							// real size of module in pages - 1
	LPBYTE	pNCE;							// pointer to memory
} NCEDATA;

typedef struct
{
	BOOL	*pbCfg;							// config
	WORD	*pwBase;						// base adress
	DWORD	*pdwSize;						// real size of module in Nibbles
	LPBYTE	*ppNCE;							// pointer to memory
} EXTDATA;

static const NCEDATA NCE[] =
{
	#define w Chipset
	{ &w.RamCfigM, &w.RamBaseM, MAPMASKs(w.RamM),   MAPSIZEs(w.RamM),   w.RamM },
	{ &w.IOCfigM,  &w.IOBaseM,  MAPMASKs(w.IORamM), MAPSIZEs(w.IORamM), w.IORamM },
	{ &w.RamCfigS, &w.RamBaseS, MAPMASKs(w.RamS),   MAPSIZEs(w.RamS),   w.RamS },
	{ &w.IOCfigS,  &w.IOBaseS,  MAPMASKs(w.IORamS), MAPSIZEs(w.IORamS), w.IORamS }
	#undef w
};

static const EXTDATA EXT[] =
{
	#define w Chipset
	{ &w.Ext1Cfig, &w.Ext1Base, &w.Ext1Size, &pbyRamExt },
	{ &w.Ext2Cfig, &w.Ext2Base, &w.Ext2Size, &pbyRamExt }
	#undef w
};

static VOID MapNCE(enum NCEPORT s, WORD a, WORD b)
{
	UINT i;
	DWORD p, m;

	_ASSERT(s >= 0 && s < ARRAYSIZEOF(NCE)); // valid table entry
	if (*NCE[s].pbCfg == FALSE) return;		// chip isn't configured

	a = (WORD)MAX(a,*NCE[s].pwBase & NCE[s].wMask);
	b = (WORD)MIN(b,(*NCE[s].pwBase & NCE[s].wMask) + NCE[s].wSize);
	m = ((NCE[s].wSize + 1) << ADDR_BITS) - 1;
	p = (a << ADDR_BITS) & m;				// offset to begin in nibbles
	for (i=a; i<=b; ++i)
	{
		if (RMap[i] == NULL)
		{
			RMap[i] = NCE[s].pNCE + p;
			WMap[i] = NCE[s].pNCE + p;
		}
		p = (p + ADDR_SIZE) & m;
	}
	return;
}

static VOID MapEXT(enum NCEEXT s, WORD a, WORD b, DWORD *pdwRamOff)
{
	LPBYTE pBase;
	UINT   i;
	DWORD  p, m;

	_ASSERT(s >= 0 && s < ARRAYSIZEOF(EXT)); // valid table entry
	if (*EXT[s].pbCfg == FALSE) return;		// chip isn't configured

	a = (WORD)MAX(a,*EXT[s].pwBase & MAPMASK(*EXT[s].pdwSize));
	b = (WORD)MIN(b,(*EXT[s].pwBase & MAPMASK(*EXT[s].pdwSize)) + MAPSIZE(*EXT[s].pdwSize));
	m = *EXT[s].pdwSize - 1;				// external RAM address mask for mirroring
	p = (a << ADDR_BITS) & m;				// offset to begin in nibbles

	pBase = *EXT[s].ppNCE + *pdwRamOff;		// base of memory
	*pdwRamOff += *EXT[s].pdwSize;			// offset for next RAM module

	for (i=a; i<=b; ++i)
	{
		if (RMap[i] == NULL)
		{
			RMap[i] = pBase + p;
			WMap[i] = pBase + p;
		}
		p = (p + ADDR_SIZE) & m;
	}
	return;
}

static VOID MapROM(WORD a, WORD b)
{
	UINT i;
	DWORD p, m;

	_ASSERT(dwRomSize > 0);					// ROM loaded
	m = dwRomSize - 1;						// ROM address mask for mirroring
	b = (WORD)MIN(b,m >> ADDR_BITS);
	p = (a << ADDR_BITS) & m;
	for (i=a; i<=b; ++i)					// scan each 1K nibble page
	{
		RMap[i] = pbyRom + p;				// save page address for read
		p = (p + ADDR_SIZE) & m;
	}
	return;
}

VOID Map(WORD a, WORD b)					// map pages
{
	UINT i;

	DWORD dwOffset = 0;						// external RAM offset

	for (i=a; i<=b; ++i)					// clear area
	{
		RMap[i]=NULL;
		WMap[i]=NULL;
	}

	MapROM(a,b);							// ROM
	MapNCE(MASTER_RAM,a,b);					// RAM (master)
	MapNCE(MASTER_IO,a,b);					// I/O (master)
	MapNCE(SLAVE_RAM,a,b);					// RAM (slave)
	MapNCE(SLAVE_IO,a,b);					// I/O (slave)
	MapEXT(EXT2_RAM,a,b,&dwOffset);			// RAM (external module)
	MapEXT(EXT1_RAM,a,b,&dwOffset);			// RAM (external module)
	return;
}

////////////////////////////////////////////////////////////////////////////////
//
// Bus Commands
//
////////////////////////////////////////////////////////////////////////////////

VOID Config(VOID)							// configure modules in fixed order
{
	WORD p=(WORD)(Npack(Chipset.C,5)>>ADDR_BITS); // page address

	if (!Chipset.RamCfigS)					// RAM address, slave
	{
		Chipset.RamCfigS = TRUE;
		p &= MAPMASKs(Chipset.RamS);		// adjust base to mapping boundary
		Chipset.RamBaseS = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZEs(Chipset.RamS)));
		return;
	}
	if (!Chipset.IOCfigS)					// IORAM address, slave
	{
		Chipset.IOCfigS = TRUE;
		p &= MAPMASKs(Chipset.IORamS);		// adjust base to mapping boundary
		Chipset.IOBaseS = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZEs(Chipset.IORamS)));
		return;
	}
	if (!Chipset.RamCfigM)					// RAM address, master
	{
		Chipset.RamCfigM = TRUE;
		p &= MAPMASKs(Chipset.RamM);		// adjust base to mapping boundary
		Chipset.RamBaseM = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZEs(Chipset.RamM)));
		return;
	}
	if (!Chipset.IOCfigM)					// IORAM address, master
	{
		Chipset.IOCfigM = TRUE;
		p &= MAPMASKs(Chipset.IORamM);		// adjust base to mapping boundary
		Chipset.IOBaseM = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZEs(Chipset.IORamM)));
		return;
	}
	if (Chipset.Ext1Size && !Chipset.Ext1Cfig) // external memory 1
	{
		Chipset.Ext1Cfig = TRUE;
		p &= MAPMASK(Chipset.Ext1Size);		// adjust base to mapping boundary
		Chipset.Ext1Base = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZE(Chipset.Ext1Size)));
		return;
	}
	if (Chipset.Ext2Size && !Chipset.Ext2Cfig) // external memory 2
	{
		Chipset.Ext2Cfig = TRUE;
		p &= MAPMASK(Chipset.Ext2Size);		// adjust base to mapping boundary
		Chipset.Ext2Base = p;				// save first page address
		Map(p,(WORD)(p+MAPSIZE(Chipset.Ext2Size)));
		return;
	}
	return;
}

VOID Uncnfg(VOID)
{
	WORD p=(WORD)(Npack(Chipset.C,5)>>ADDR_BITS); // page address

	if ((Chipset.Ext2Cfig)&&((p&MAPMASK(Chipset.Ext2Size))==Chipset.Ext2Base))
	{
		Chipset.Ext2Cfig = FALSE;
		Map(Chipset.Ext2Base,(WORD)(Chipset.Ext2Base+MAPSIZE(Chipset.Ext2Size)));
		return;
	}
	if ((Chipset.Ext1Cfig)&&((p&MAPMASK(Chipset.Ext1Size))==Chipset.Ext1Base))
	{
		Chipset.Ext1Cfig = FALSE;
		Map(Chipset.Ext1Base,(WORD)(Chipset.Ext1Base+MAPSIZE(Chipset.Ext1Size)));
		return;
	}
	if ((Chipset.RamCfigM)&&((p&MAPMASKs(Chipset.RamM))==Chipset.RamBaseM))
	{
		Chipset.RamCfigM = FALSE;
		Map(Chipset.RamBaseM,(WORD)(Chipset.RamBaseM+MAPSIZEs(Chipset.RamM)));
		return;
	}
	if ((Chipset.IOCfigM)&&((p&MAPMASKs(Chipset.IORamM))==Chipset.IOBaseM))
	{
		Chipset.IOCfigM = FALSE;
		Map(Chipset.IOBaseM,(WORD)(Chipset.IOBaseM+MAPSIZEs(Chipset.IORamM)));
		return;
	}
	if ((Chipset.RamCfigS)&&((p&MAPMASKs(Chipset.RamS))==Chipset.RamBaseS))
	{
		Chipset.RamCfigS = FALSE;
		Map(Chipset.RamBaseS,(WORD)(Chipset.RamBaseS+MAPSIZEs(Chipset.RamS)));
		return;
	}
	if ((Chipset.IOCfigS)&&((p&MAPMASKs(Chipset.IORamM))==Chipset.IOBaseS))
	{
		Chipset.IOCfigS = FALSE;
		Map(Chipset.IOBaseS,(WORD)(Chipset.IOBaseS+MAPSIZEs(Chipset.IORamS)));
		return;
	}
	return;
}

VOID Reset()
{
	Chipset.RamCfigM = FALSE; Chipset.RamBaseM = 0;
	Chipset.IOCfigM  = FALSE; Chipset.IOBaseM  = 0;
	Chipset.RamCfigS = FALSE; Chipset.RamBaseS = 0;
	Chipset.IOCfigS  = FALSE; Chipset.IOBaseS  = 0;
	Chipset.Ext1Cfig = FALSE; Chipset.Ext1Base = 0;
	Chipset.Ext2Cfig = FALSE; Chipset.Ext2Base = 0;
	Map(0,ARRAYSIZEOF(RMap)-1);				// refresh mapping
	return;
}

VOID C_Eq_Id()
{
	if (!Chipset.RamCfigS) {Nunpack(Chipset.C,0x8000E,5);return;}
	if (!Chipset.IOCfigS)  {Nunpack(Chipset.C,0x81F09,5);return;}
	if (!Chipset.RamCfigM) {Nunpack(Chipset.C,0x8000E,5);return;}
	if (!Chipset.IOCfigM)  {Nunpack(Chipset.C,0x81F09,5);return;}
	if (Chipset.Ext1Size && !Chipset.Ext1Cfig) {Nunpack(Chipset.C,ModuleId(Chipset.Ext1Size),5);return;}
	if (Chipset.Ext2Size && !Chipset.Ext2Cfig) {Nunpack(Chipset.C,ModuleId(Chipset.Ext2Size),5);return;}
	memset(Chipset.C, 0, 5);				// clear C[A]
	return;
}

VOID CpuReset(VOID)							// register setting after Cpu Reset
{
	StopTimers(SLAVE);						// stop timer, do here because functions change Chipset.t2
	StopTimers(MASTER);

	Chipset.pc = 0;
	Chipset.rstkp = 0;
	ZeroMemory(Chipset.rstk,sizeof(Chipset.rstk));
	Chipset.HST = 0;
	Chipset.SoftInt = FALSE;
	Chipset.Shutdn = TRUE;
	Chipset.inte = TRUE;					// enable interrupts
	Chipset.intk = TRUE;					// INTON
	Chipset.intd = FALSE;					// no keyboard interrupts pending
	Reset();								// reset MMU
	ZeroMemory(Chipset.IORamM,sizeof(Chipset.IORamM));
	ZeroMemory(Chipset.IORamS,sizeof(Chipset.IORamS));
	Chipset.tM = 0;
	Chipset.tS = 0;
	Chipset.contrast = 0;					// contrast
	UpdateContrast(Chipset.contrast);		// update contrast
	return;
}

enum MMUMAP MapData(DWORD d)				// check MMU area
{
	d >>= ADDR_BITS;						// page address
	if (Chipset.IOCfigS  && (d & MAPMASKs(Chipset.IORamS))  == Chipset.IOBaseS)  return M_DISPS;
	if (Chipset.RamCfigS && (d & MAPMASKs(Chipset.RamS))    == Chipset.RamBaseS) return M_RAMS;
	if (Chipset.IOCfigM  && (d & MAPMASKs(Chipset.IORamM))  == Chipset.IOBaseM)  return M_DISPM;
	if (Chipset.RamCfigM && (d & MAPMASKs(Chipset.RamM))    == Chipset.RamBaseM) return M_RAMM;
	if (Chipset.Ext1Cfig && (d & MAPMASK(Chipset.Ext1Size)) == Chipset.Ext1Base) return M_EXT1;
	if (Chipset.Ext2Cfig && (d & MAPMASK(Chipset.Ext2Size)) == Chipset.Ext2Base) return M_EXT2;
	if ((d & ((~(dwRomSize-1)&0xFFFFF)>>ADDR_BITS))         == 0)                return M_ROM;
	return M_NONE;
}

static VOID NreadEx(BYTE *a, DWORD d, UINT s, BOOL bUpdate)
{
	enum MMUMAP eMap;
	DWORD u, v;
	UINT  c;
	BYTE *p;

	#if defined DEBUG_MEMACC
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: Mem %s : %02x,%u\n"),
				 Chipset.pc,(bUpdate) ? _T("read") : _T("peek"),d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		eMap = MapData(d);					// get active memory controller

		do
		{
			if (M_DISPM == eMap)			// display/timer/control registers (master)
			{
				v = d & (sizeof(Chipset.IORamM)-1);
				c = MIN(s,sizeof(Chipset.IORamM)-v);
				ReadIO(a,v,c,bUpdate);
				break;
			}

			if (M_DISPS == eMap)			// display/timer/control registers (slave)
			{
				v = d & (sizeof(Chipset.IORamS)-1);
				c = MIN(s,sizeof(Chipset.IORamS)-v);
				ReadSlaveIO(a,v,c,bUpdate);
				break;
			}

			u = d >> ADDR_BITS;
			v = d & (ADDR_SIZE-1);
			c = MIN(s,ADDR_SIZE-v);
			if ((p=RMap[u]) != NULL)		// module mapped
			{
				memcpy(a, p+v, c);
			}
			// simulate open data bus
			else							// open data bus
			{
				if (M_NONE != eMap)			// open data bus
				{
					for (u=0; u<c; ++u)		// fill all nibbles
					{
						if ((v+u) & 1)		// odd address
							a[u] = READODD;
						else				// even address
							a[u] = READEVEN;
					}
				}
				else
				{
					memset(a, 0x00, c);		// fill with 0
				}
			}
		}
		while (FALSE);

		a+=c;
		d=(d+c)&0xFFFFF;
	} while (s-=c);
	return;
}

__forceinline VOID Npeek(BYTE *a, DWORD d, UINT s)
{
	NreadEx(a, d, s, FALSE);
	return;
}

__forceinline VOID Nread(BYTE *a, DWORD d, UINT s)
{
	NreadEx(a, d, s, TRUE);
	return;
}

VOID Nwrite(BYTE *a, DWORD d, UINT s)
{
	enum MMUMAP eMap;
	DWORD u, v;
	UINT  c;
	BYTE *p;

	#if defined DEBUG_MEMACC
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: Mem write: %02x,%u\n"),Chipset.pc,d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		eMap = MapData(d);					// get active memory controller

		do
		{
			if (   M_DISPM == eMap			// display/timer/control registers (master)
				|| M_DISPS == eMap)			// display/timer/control registers (slave)
			{
				v = d & (sizeof(Chipset.IORamM)-1);
				c = MIN(s,sizeof(Chipset.IORamM)-v);
				if (M_DISPM == eMap)
					WriteIO(a, v, c);
				else
					WriteSlaveIO(a, v, c);
				break;
			}

			u = d >> ADDR_BITS;
			v = d & (ADDR_SIZE-1);
			c = MIN(s,ADDR_SIZE-v);
			if ((p=WMap[u]) != NULL) memcpy(p+v, a, c);
		}
		while(0);

		a+=c;
		d=(d+c)&0xFFFFF;
	} while (s-=c);
	return;
}

DWORD Read5(DWORD d)
{
	BYTE p[5];

	Npeek(p,d,5);
	return Npack(p,5);
}

BYTE Read2(DWORD d)
{
	BYTE p[2];

	Npeek(p,d,2);
	return (BYTE)(p[0]|(p[1]<<4));
}

VOID Write5(DWORD d, DWORD n)
{
	BYTE p[5];

	Nunpack(p,n,5);
	Nwrite(p,d,5);
	return;
}

VOID Write2(DWORD d, BYTE n)
{
	BYTE p[2];

	Nunpack(p,n,2);
	Nwrite(p,d,2);
	return;
}

static BYTE GetLPD(BYTE *r, BOOL bUpdate)
{
	BYTE byLPD = *r;						// get current content

	_ASSERT((*r & (XTRA_2 | XTRA_1)) == 0);	// bit 1+2 must be zero

	if (bUpdate)							// update register content
	{
		byLPD &= ~LBI;						// clear LBI bit from last reading

		if ((*r & LBION) != 0)				// low battery circuit enabled
		{
			SYSTEM_POWER_STATUS sSps;

			VERIFY(GetSystemPowerStatus(&sSps));

			// low bat emulation enabled, battery powered and low battery condition on host
			if (   !bLowBatDisable
				&& sSps.ACLineStatus == AC_LINE_OFFLINE
				&& (sSps.BatteryFlag & (BATTERY_FLAG_CRITICAL | BATTERY_FLAG_LOW)) != 0)
			{
				byLPD |= LBI;				// set Low Battery Indicator
			}

			*r &= ~LBION;					// clear LBION bit after reading
		}
	}
	return byLPD;
}

static DWORD ReadTAcc(enum CHIP eChip)
{
	static DWORD dwCyc[2] = { 0, 0 };		// CPU cycle counter at last timer2 read access

	DWORD dwCycDif;

	// CPU cycles since last call
	dwCycDif = (DWORD) (Chipset.cycles & 0xFFFFFFFF) - dwCyc[eChip];
	dwCyc[eChip] = (DWORD) (Chipset.cycles & 0xFFFFFFFF);

	// maybe CPU speed measurement, slow down the next 10 CPU opcodes
	if (dwCycDif < 150)
	{
		InitAdjustSpeed();					// init variables if necessary
		EnterCriticalSection(&csSlowLock);
		{
			nOpcSlow = 10;					// slow down next 10 opcodes
		}
		LeaveCriticalSection(&csSlowLock);
	}
	return ReadT(eChip);
}

static VOID ReadIO(BYTE *a, DWORD d, DWORD s, BOOL bUpdate)
{
	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: IO read : %02x,%u\n"),Chipset.pc,d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		switch (d)
		{
		case 0x300:
			// get current LPD state
			*a = GetLPD(&Chipset.IORamM[d],bUpdate);
			break;
		case 0x30C:
			*a = RX;						// RX-Line always high
			break;
		case 0x30A:
			ReadT(MASTER);					// dummy read for update timer2 control register
			*a = Chipset.IORamM[d];
			break;
		case 0x3F8: Nunpack(a, ReadTAcc(MASTER)    , s); return;
		case 0x3F9: Nunpack(a, ReadTAcc(MASTER)>> 4, s); return;
		case 0x3FA: Nunpack(a, ReadTAcc(MASTER)>> 8, s); return;
		case 0x3FB: Nunpack(a, ReadTAcc(MASTER)>>12, s); return;
		case 0x3FC: Nunpack(a, ReadTAcc(MASTER)>>16, s); return;
		case 0x3FD: Nunpack(a, ReadTAcc(MASTER)>>20, s); return;
		case 0x3FE: Nunpack(a, ReadTAcc(MASTER)>>24, s); return;
		case 0x3FF: Nunpack(a, ReadTAcc(MASTER)>>28, s); return;
		default:
			if (d > DAREA_END && d < LPD)	// undefined area 1
			{
				*a = 0;
				break;
			}
			if (d > DSPCTL && d < TIMERCTL)	// undefined area 2
			{
				*a = 0;
				break;
			}
			if (d > LEDOUT && d < TIMER)	// undefined area 3
			{
				*a = 3;
				break;
			}
			*a = Chipset.IORamM[d];
		}

		d++; a++;
	} while (--s);
	return;
}

static VOID WriteIO(BYTE *a, DWORD d, DWORD s)
{
	BYTE c;
	BOOL bRow = FALSE;						// no row driver access

	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		DWORD j;
		int   i;

		i = wsprintf(buffer,_T("%.5lx: IO write: %02x,%u = "),Chipset.pc,d,s);
		for (j = 0;j < s;++j,++i)
		{
			buffer[i] = a[j];
			if (buffer[i] > 9) buffer[i] += _T('a') - _T('9') - 1;
			buffer[i] += _T('0');
		}
		buffer[i++] = _T('\n');
		buffer[i] = 0;
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		c = *a;
		switch (d)
		{
// 00300 =  LPD
// 00300 @  Low Power Detection [LBION 0 0 LBI]
// 00300 @  LBI = read only
		case 0x300:
			Chipset.IORamM[d] = (c & LBION);
			break;

// 00301 =  CONTRAST
// 00301 @  Contrast Control [CONT3 CONT2 CONT1 CONT0]
// 00301 @  Higher value = darker screen
		case 0x301:
			if (c!=Chipset.IORamM[d])
			{
				Chipset.IORamM[d]=c;
				Chipset.contrast = (Chipset.contrast&0x10)|c;
				UpdateContrast(Chipset.contrast);
			}
			break;

// 00302 =  DSPTEST
// 00302 @  Display test [IAM VTON CLTM1 CLTM0]
		case 0x302:
			// IAM bit changed
			if ((c^Chipset.IORamM[d]) & IAM)
			{
				if (c & IAM)
				{
					Chipset.IORamM[d] |= IAM;
					StartTimers(MASTER);	// try to start the master timer
					StartTimers(SLAVE);		// is there a waiting slave timer, if so start
				}
				else
				{
					Chipset.IORamM[d] &= ~IAM;
					StopTimers(SLAVE);		// master timer stops also slave timer
					StopTimers(MASTER);		// now stops the master
				}
			}
			// VTON bit changed
			if ((c^Chipset.IORamM[d]) & VTON)
			{
				Chipset.contrast = (Chipset.contrast&0x0f)|((c&VTON)<<2);
				UpdateContrast(Chipset.contrast);
			}
			Chipset.IORamM[d] = c;
			break;

// 00303 =  DSPCTL
// 00303 @  Display control [DON 0 0 0]
// 00303 @  DON=Display ON
		case 0x303:
			if ((c^Chipset.IORamM[d])&DON)	// DON bit changed
			{
				if ((c & DON) != 0)			// set display on
				{
					// DON bit of slave not set
					Chipset.bLcdSyncErr = (Chipset.IORamS[DSPCTL]&DON) == 0;

					Chipset.IORamM[d] |= DON;
					StartDisplay();			// start display update
				}
				else						// display is off
				{
					Chipset.IORamM[d] &= ~DON;
					StopDisplay();			// stop display update
				}
				UpdateContrast(Chipset.contrast);
			}
			Chipset.IORamM[d] = c;
			break;

// 0030A =  NS:TIMERCTRL
// 0030A @  TIMER Control [SRQ WKE INT RUN]
		case 0x30A:
			Chipset.IORamM[d]=c;
			ReadT(MASTER);					// dummy read for checking control bits
			if (c & RUN)
			{
				StartTimers(MASTER);		// start the master timer
				StartTimers(SLAVE);			// is there a waiting slave timer, if so start
			}
			else
			{
				StopTimers(SLAVE);			// master timer stops also slave timer
				StopTimers(MASTER);			// now stops the master
			}
			break;

// 0030C =  INPUT_PORT
// 0030C @  Input Port [RX SREQ ST1 ST0]
		case 0x30C:
			if (c & SREQ)					// SREQ bit set
			{
				c &= ~SREQ;					// clear SREQ bit
				Chipset.SoftInt = TRUE;
				bInterrupt = TRUE;
			}
			Chipset.IORamM[d]=c;
			break;

// 0030D =  LEDOUT
// 0030D @  LED output [XTRA EPD STL DRL]
		case 0x30D:
			if ((c^Chipset.IORamM[d]) & STL) // STL bit changed
			{
				IrPrinter((BYTE)(c & STL));
			}
			Chipset.IORamM[d]=c;
			break;

// 003F8 =  HP:TIMER
// 003F8 @  hardware timer (38-3F), decremented 8192 times/s
		// nothing - fall through to default

		default:
			Chipset.IORamM[d]=c;			// write data

			// display row definition area
			bRow = bRow || (d >= MROW_BEGIN && d <= MROW_END);

			if (d >= TIMER)					// timer update
			{
				Nunpack(Chipset.IORamM+TIMER, ReadT(MASTER), 8);
				memcpy(Chipset.IORamM+d, a, s);
				SetT(MASTER,Npack(Chipset.IORamM+TIMER, 8));
				return;
			}
		}
		a++; d++;
	} while (--s);

	if (bRow) UpdateContrast(Chipset.contrast);
	return;
}

static VOID ReadSlaveIO(BYTE *a, DWORD d, DWORD s, BOOL bUpdate)
{
	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		wsprintf(buffer,_T("%.5lx: IO Slave read : %02x,%u\n"),Chipset.pc,d,s);
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		switch (d)
		{
		case 0x300:
			// get current LPD state
			*a = GetLPD(&Chipset.IORamS[d],bUpdate);
			break;
		case 0x30A:
			ReadT(SLAVE);					// dummy read for update timer control register
			*a = Chipset.IORamS[d];
			break;
		case 0x3F8: Nunpack(a, ReadTAcc(SLAVE)    , s); return;
		case 0x3F9: Nunpack(a, ReadTAcc(SLAVE)>> 4, s); return;
		case 0x3FA: Nunpack(a, ReadTAcc(SLAVE)>> 8, s); return;
		case 0x3FB: Nunpack(a, ReadTAcc(SLAVE)>>12, s); return;
		case 0x3FC: Nunpack(a, ReadTAcc(SLAVE)>>16, s); return;
		case 0x3FD: Nunpack(a, ReadTAcc(SLAVE)>>20, s); return;
		case 0x3FE: Nunpack(a, ReadTAcc(SLAVE)>>24, s); return;
		case 0x3FF: Nunpack(a, ReadTAcc(SLAVE)>>28, s); return;
		default:
			if (d > DAREA_END && d < LPD)	// undefined area 1
			{
				*a = 0;
				break;
			}
			if (d > DSPCTL && d < TIMERCTL)	// undefined area 2
			{
				*a = 0;
				break;
			}
			if (d > LEDOUT && d < TIMER)	// undefined area 3
			{
				*a = 3;
				break;
			}
			*a = Chipset.IORamS[d];
		}

		d++; a++;
	} while (--s);
	return;
}

static VOID WriteSlaveIO(BYTE *a, DWORD d, DWORD s)
{
	BYTE  c;
	DWORD dwAnnunciator = 0;				// no annunciator write

	#if defined DEBUG_IO
	{
		TCHAR buffer[256];
		DWORD j;
		int   i;

		i = wsprintf(buffer,_T("%.5lx: IO Slave write: %02x,%u = "),Chipset.pc,d,s);
		for (j = 0;j < s;++j,++i)
		{
			buffer[i] = a[j];
			if (buffer[i] > 9) buffer[i] += _T('a') - _T('9') - 1;
			buffer[i] += _T('0');
		}
		buffer[i++] = _T('\n');
		buffer[i] = 0;
		OutputDebugString(buffer);
	}
	#endif

	do
	{
		c = *a;
		switch (d)
		{
// 00300 =  LPD
// 00300 @  Low Power Detection [LBION 0 0 LBI]
// 00300 @  LBI = read only
		case 0x300:
			Chipset.IORamS[d] = (c & LBION);
			break;

// 00301 =  CONTRAST
// 00301 @  Contrast Control [CONT3 CONT2 CONT1 CONT0]
// 00301 @  not working on slave chip, just a memory cell

// 00303 =  DSPCTL
// 00303 @  Display control [DON 0 0 0]
// 00303 @  DON=Display ON
		case 0x303:
			if ((c^Chipset.IORamS[d])&DON)	// DON bit changed
			{
				if ((c & DON) == 0)			// set display off
				{
					// DON bit of master not cleared
					Chipset.bLcdSyncErr = (Chipset.IORamM[DSPCTL]&DON) != 0;
				}
				dwAnnunciator = 0x7F;		// update all annunciators
			}
			Chipset.IORamS[d] = c;
			break;

// 0030A =  NS:TIMERCTRL
// 0030A @  TIMER Control [SRQ WKE INT RUN]
		case 0x30A:
			Chipset.IORamS[d]=c;
			ReadT(SLAVE);					// dummy read for checking control bits
			if (c&1)
				StartTimers(SLAVE);
			else
				StopTimers(SLAVE);
			break;

// 003F8 =  HP:TIMER
// 003F8 @  hardware timer (38-3F), decremented 8192 times/s
		// nothing - fall through to default
		default:
			// Clamshell annunciator area changed
			if (d >= SLA_BUSY && d <= SLA_ALL + 7 && (Chipset.IORamS[d] ^ c) != 0)
			{
				// annunciator conversation table from memory position to annunciator order
				// bit 0 = SLA_HALT, bit 1 = SLA_SHIFT, bit 2 = SLA_ALPHA, bit 3 = SLA_BUSY
				// bit 4 = SLA_BAT,  bit 5 = SLA_RAD,   bit 6 = SLA_PRINTER
				const BYTE byUpdateMask[] = { 1<<3, 1<<2, 1<<4, 1<<1, 1<<5, 1<<0, 1<<6, 0x7F };

				// get annunciator mask from table
				_ASSERT(d / 8 < ARRAYSIZEOF(byUpdateMask));
				dwAnnunciator |= byUpdateMask[d / 8];
			}

			Chipset.IORamS[d]=c;			// write data

			if (d >= TIMER)					// timer update
			{
				Nunpack(Chipset.IORamS+TIMER, ReadT(SLAVE), 8);
				memcpy(Chipset.IORamS+d, a, s);
				SetT(SLAVE,Npack(Chipset.IORamS+TIMER, 8));
				goto finish;
			}
		}
		a++; d++;
	} while (--s);

finish:
	if (dwAnnunciator) UpdateAnnunciators(dwAnnunciator);
	return;
}
