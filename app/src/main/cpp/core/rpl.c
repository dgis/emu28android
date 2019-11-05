/*
 *   rpl.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "ops.h"
#include "io.h"

//| 28C  | 28S  | 42S  | Name
//#4F05A #C005A #500B8 =TEMPTOP
//#4F05F #C005F #500BD =RSKTOP   (B)
//#4F064 #C0064 #500C2 =DSKTOP   (D1)
//#4F069 #C0069 #500C7 =EDITLINE
//#4F0DC #C00DC #50117 =AVMEM    (D)
//#4F08C #C008C #500E0 =INTRPPTR (D0)
//#4F10B #C010F #5046B =USERFLAGS

// HP28C entries
#define TEMPTOP		0x4F05A
#define RSKTOP		0x4F05F
#define DSKTOP		0x4F064
#define EDITLINE	0x4F069
#define AVMEM		0x4F0DC
#define INTRPPTR	0x4F08C
#define USERFLAGS	0x4F10B

#define DOBINT		0x02911		// System Binary
#define DOREAL		0x02933		// Real
#define DOEREL		0x02955		// Long Real
#define DOCMP		0x02977		// Complex
#define DOECMP		0x0299D		// Long Complex
#define DOCHAR		0x029BF		// Character
#define DOEXT		0x029E1		// Reserved
#define DOARRY		0x02A0A		// Array
#define DOLNKARRY	0x02A2C		// Linked Array
#define DOCSTR		0x02A4E		// String
#define DOHSTR		0x02A70		// Binary Integer
#define DOLIST		0x02A96		// List
#define DORRP		0x02AB8		// Directory
#define DOSYMB		0x02ADA		// Algebraic
#define DOCOL		0x02C67		// Program
#define DOCODE		0x02C96		// Code
#define DOIDNT		0x02D12		// Global Name *
#define DOLAM		0x02D37		// Local Name *
#define DOROMP		0x02D5C		// XLIB Name *
#define SEMI		0x02F90		// ;

#define GARBAGECOL	0x0497D		// =GARBAGECOL

static DWORD RPL_GarbageCol(VOID)			// RPL variables must be in system RAM
{
	CHIPSET OrgChipset;
	DWORD   dwAVMEM;

	_ASSERT(cCurrentRomType == 'P');		// HP28C (Paladin)

	OrgChipset = Chipset;					// save original chipset

	// entry for =GARBAGECOL
	Chipset.P = 0;							// P=0
	Chipset.mode_dec = FALSE;				// hex mode
	Chipset.pc = GARBAGECOL;				// =GARBAGECOL entry
	rstkpush(0xFFFFF);						// return address for stopping

	while (Chipset.pc != 0xFFFFF)			// wait for stop address
	{
		EvalOpcode(FASTPTR(Chipset.pc));	// execute opcode
	}

	dwAVMEM = Npack(Chipset.C,5);			// available AVMEM
	Chipset = OrgChipset;					// restore original chipset
	return dwAVMEM;
}

BOOL RPL_GetSystemFlag(INT nFlag)
{
	DWORD dwAddr;
	BYTE byMask,byFlag;

	_ASSERT(nFlag > 0);						// first flag is 1

	// calculate memory address and bit mask
	dwAddr = USERFLAGS + (nFlag - 1) / 4;
	byMask = 1 << ((nFlag - 1) & 0x3);

	Npeek(&byFlag,dwAddr,sizeof(byFlag));
	return (byFlag & byMask) != 0;
}

DWORD RPL_SkipOb(DWORD d)
{
	BYTE X[8];
	DWORD n, l;

	Npeek(X,d,5);
	n = Npack(X, 5); // read prolog
	switch (n)
	{
	case DOBINT: l = 10; break; // System Binary
	case DOREAL: l = 21; break; // Real
	case DOEREL: l = 26; break; // Long Real
	case DOCMP:  l = 37; break; // Complex
	case DOECMP: l = 47; break; // Long Complex
	case DOCHAR: l =  7; break; // Character
	case DOROMP: l = 11; break; // XLIB Name
	case DOLIST: // List
	case DOSYMB: // Algebraic
	case DOCOL:  // Program
		n=d+5;
		do
		{
			d=n; n=RPL_SkipOb(d);
		} while (d!=n);
		return n+5;
	case SEMI: return d; // ;
	case DOIDNT: // Global Name
	case DOLAM:  // Local Name
		Npeek(X,d+5,2); n = 7 + Npack(X,2)*2;
		return RPL_SkipOb(d+n);
	case DORRP: // Directory
		d+=8;
		n = Read5(d);
		if (n==0)
		{
			return d+5;
		}
		else
		{
			d+=n;
			Npeek(X,d,2);
			n = Npack(X,2)*2 + 4;
			return RPL_SkipOb(d+n);
		}
	case DOEXT:     // Reserved
	case DOARRY:    // Array
	case DOLNKARRY: // Linked Array
	case DOCSTR:    // String
	case DOHSTR:    // Binary Integer
	case DOCODE:    // Code
		l = 5+Read5(d+5);
		break;
	default: return d+5;
	}
	return d+l;
}

DWORD RPL_ObjectSize(BYTE *o,DWORD s)
{
	DWORD n, l = 0;

	if (s < 5) return BAD_OB;				// size too small for prolog
	n = Npack(o, 5);						// read prolog
	switch (n)
	{
	case DOBINT: l = 10; break; // System Binary
	case DOREAL: l = 21; break; // Real
	case DOEREL: l = 26; break; // Long Real
	case DOCMP:  l = 37; break; // Complex
	case DOECMP: l = 47; break; // Long Complex
	case DOCHAR: l =  7; break; // Character
	case DOROMP: l = 11; break; // XLIB Name
	case DOLIST: // List
	case DOSYMB: // Algebraic
	case DOCOL:  // Program
		n = 5;								// prolog length
		do
		{
			l += n;
			if (l > s) return BAD_OB;		// prevent negative size argument
			n = RPL_ObjectSize(o+l,s-l);	// get new object
			if (n == BAD_OB) return BAD_OB;	// buffer overflow
		}
		while (n);
		l += 5;
		break;
	case SEMI: l = 0; break; // ;
	case DOIDNT: // Global Name
	case DOLAM:  // Local Name
		if (s < 5 + 2) return BAD_OB;
		l = 7 + Npack(o+5,2) * 2;			// prolog + name length
		if (l > s) return BAD_OB;			// prevent negative size argument
		n = RPL_ObjectSize(o+l,s-l);		// get new object
		if (n == BAD_OB) return BAD_OB;		// buffer overflow
		l += n;
		break;
	case DORRP: // RAMROMPAIR
		if (s < 8 + 5) return BAD_OB;
		n = Npack(o+8,5);
		if (n == 0)							// empty RAMROMPAIR
		{
			l = 13;
		}
		else
		{
			l = 8 + n;
			if (s < l + 2) return BAD_OB;
			n = Npack(o+l,2) * 2 + 4;
			l += n;
			if (l > s) return BAD_OB;		// prevent negative size argument
			n = RPL_ObjectSize(o+l,s-l);	// next rrp
			if (n == BAD_OB) return BAD_OB;	// buffer overflow
			l += n;
		}
		break;
	case DOEXT:     // Reserved
	case DOARRY:    // Array
	case DOLNKARRY: // Linked Array
	case DOCSTR:    // String
	case DOHSTR:    // Binary Integer
	case DOCODE:    // Code
		if (s < 5 + 5) return BAD_OB;
		l = 5 + Npack(o+5,5);
		break;
	default: l=5;
	}
	return (s >= l) ? l : BAD_OB;
}

DWORD RPL_CreateTemp(DWORD l)
{
	DWORD a, b, c;
	BYTE *p;

	l += 6;									// memory for link field (5) + marker (1) and end
	b = Read5(RSKTOP);						// tail address of rtn stack
	c = Read5(DSKTOP);						// top of data stack
	if ((b+l)>c)							// there's not enough memory to move DSKTOP
	{
		RPL_GarbageCol();					// do a garbage collection
		b = Read5(RSKTOP);					// reload tail address of rtn stack
		c = Read5(DSKTOP);					// reload top of data stack
	}
	if ((b+l)>c) return 0;					// check if now there's enough memory to move DSKTOP
	a = Read5(TEMPTOP);						// tail address of top object
	Write5(TEMPTOP, a+l);					// adjust new end of top object
	Write5(RSKTOP,  b+l);					// adjust new end of rtn stack
	Write5(AVMEM, (c-b-l)/5);				// calculate free memory (*5 nibbles)
	p = (LPBYTE) malloc(b-a);				// move down rtn stack
	Npeek(p,a,b-a);
	Nwrite(p,a+l,b-a);
	free(p);
	Write5(a+l-5,l);						// set object length field
	return (a+1);							// return base address of new object
}

UINT RPL_Depth(VOID)
{
	return (Read5(EDITLINE) - Read5(DSKTOP)) / 5 - 1;
}

DWORD RPL_Pick(UINT l)
{
	DWORD stkp;

	_ASSERT(l > 0);							// first stack element is one
	if (l == 0) return 0;
	if (RPL_Depth() < l) return 0;			// not enough elements on stack
	stkp = Read5(DSKTOP) + (l-1)*5;
	return Read5(stkp);
}

VOID RPL_Replace(DWORD n)
{
	DWORD stkp;

	stkp = Read5(DSKTOP);
	Write5(stkp,n);
	return;
}

VOID RPL_Push(UINT l,DWORD n)
{
	UINT  i;
	DWORD stkp, avmem;

	if (l > RPL_Depth() + 1) return;		// invalid stack level

	avmem = Read5(AVMEM);					// amount of free memory
	if (avmem == 0) return;					// no memory free
	avmem--;								// fetch memory
	Write5(AVMEM,avmem);					// save new amount of free memory

	stkp = Read5(DSKTOP) - 5;				// get pointer of new stack level 1
	Write5(DSKTOP,stkp);					// save it

	for (i = 1; i < l; ++i)					// move down stack level entries before insert pos
	{
		Write5(stkp,Read5(stkp+5));			// move down stack level entry
		stkp += 5;							// next stack entry
	}

	Write5(stkp,n);							// save pointer of new object on given stack level
	return;
}
