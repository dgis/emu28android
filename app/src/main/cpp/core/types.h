/*
 *   types.h
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gieﬂelink
 *
 */

// HST bits
#define XM 1
#define SB 2
#define SR 4
#define MP 8

#define	SWORD SHORT							// signed   16 Bit variable
#define	QWORD ULONGLONG						// unsigned 64 Bit variable

typedef struct
{
	SWORD	nPosX;							// position of window
	SWORD	nPosY;
	BYTE	type;							// calculator type

	DWORD	pc;
	DWORD	d0;
	DWORD	d1;
	DWORD	rstkp;
	DWORD	rstk[8];
	BYTE	A[16];
	BYTE	B[16];
	BYTE	C[16];
	BYTE	D[16];
	BYTE	R0[16];
	BYTE	R1[16];
	BYTE	R2[16];
	BYTE	R3[16];
	BYTE	R4[16];
	BYTE	ST[4];
	BYTE	HST;
	BYTE	P;
	WORD	out;
	WORD	in;
	BOOL	SoftInt;
	BOOL	Shutdn;
	BOOL	mode_dec;
	BOOL	inte;							// interrupt status flag (FALSE = int in service)
	BOOL	intk;							// 1 ms keyboard scan flag (TRUE = enable)
	BOOL	intd;							// keyboard interrupt pending (TRUE = int pending)
	BOOL	carry;

#if defined _USRDLL							// DLL version
	QWORD	cycles;							// oscillator cycles
#else										// EXE version
	DWORD	cycles;							// oscillator cycles
	DWORD	cycles_reserved;				// reserved for MSB of oscillator cycles
#endif

	BOOL	bShutdnWake;					// flag for wake up from SHUTDN mode

	WORD	wRomCrc;						// fingerprint of ROM

	BOOL	RamCfigM, IOCfigM;				// RAM and IO address master configuration flag
	BOOL	RamCfigS, IOCfigS;				// RAM and IO address slave  configuration flag
	WORD	RamBaseM, IOBaseM;				// RAM and IO address of master
	WORD	RamBaseS, IOBaseS;				// RAM and IO address of slave

	BYTE	IORamM[0x400];					// master display/timer/control registers
	BYTE	IORamS[0x400];					// slave  display/timer/control registers
	BYTE	RamM[0x800];					// master user RAM
	BYTE	RamS[0x800];					// slave  user RAM

	DWORD	tM;								// master timer content
	DWORD	tS;								// slave  timer content

	WORD	Keyboard_Row[10];				// keyboard Out lines
	WORD	IR15X;							// ON-key state

	BYTE	contrast;						// display constrast setting
	BOOL	bLcdSyncErr;					// master/slave LCD controller synchronization error

	DWORD	dwUnused;						// not used, was memory pointer RamExt

	DWORD	Ext1Size;						// size of external module 1
	WORD	Ext1Base;						// base address of module
	BOOL	Ext1Cfig;						// module configuration flag

	DWORD	Ext2Size;						// size of external module 2
	WORD	Ext2Base;						// base address of module
	BOOL	Ext2Cfig;						// module configuration flag
} CHIPSET;
