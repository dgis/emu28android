/*
 *   dispnum.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2022 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "ops.h"
#include "io.h"

// #define DEBUG_PATTERN					// switch for pattern output of segments

// translation table for Pioneer/Clamshell graphic image type
static CONST struct
{
	QWORD qwImage;							// segment image
	TCHAR cChar;							// corresponding character
} sGraphictabLewis[] =
{
	CLL(0x000000000000), _T(' '),
	CLL(0x0000F5000000), _T('!'),
	CLL(0x007000700000), _T('\"'),
	CLL(0x41F741F74100), _T('#'),
	CLL(0x42A2F7A22100), _T('$'),
	CLL(0x323180462600), _T('%'),
	CLL(0x639465020500), _T('&'),
	CLL(0x000070000000), _T('\''),
	CLL(0x00C122140000), _T('('),
	CLL(0x001422C10000), _T(')'),
	CLL(0x80A2C1A28000), _T('*'),
	CLL(0x8080E3808000), _T('+'),
	CLL(0x000B07000000), _T(','),
	CLL(0x808080800000), _T('-'),
	CLL(0x000606000000), _T('.'),
	CLL(0x020180402000), _T('/'),
	CLL(0xE3159454E300), _T('0'),
	CLL(0x0024F7040000), _T('1'),
	CLL(0x261594946400), _T('2'),
	CLL(0x229494946300), _T('3'),
	CLL(0x814121F70100), _T('4'),
	CLL(0x725454549300), _T('5'),
	CLL(0xC3A494940300), _T('6'),
	CLL(0x101790503000), _T('7'),
	CLL(0x639494946300), _T('8'),
	CLL(0x60949492E100), _T('9'),
	CLL(0x006363000000), _T(':'),
	CLL(0x820000000000), _T(':'),			// in stack
	CLL(0x006B67000000), _T(';'),
	CLL(0x804122140000), _T('<'),
	CLL(0x414141414100), _T('='),
	CLL(0x142241800000), _T('>'),
	CLL(0x201015906000), _T('?'),
	CLL(0xE314D555E500), _T('@'),
	CLL(0xE314D594E400), _T('@'),			// alternate
	CLL(0xE7909090E700), _T('A'),
	CLL(0xF79494946300), _T('B'),
	CLL(0xE31414142200), _T('C'),
	CLL(0xF7141422C100), _T('D'),
	CLL(0xF79494941400), _T('E'),
	CLL(0xE3A2A2220000), _T('E'),			// exponent
	CLL(0xF79090901000), _T('F'),
	CLL(0xE31414152700), _T('G'),
	CLL(0xF7808080F700), _T('H'),
	CLL(0x0014F7140000), _T('I'),
	CLL(0x03040404F300), _T('J'),
	CLL(0xF78041221400), _T('K'),
	CLL(0xF70404040400), _T('L'),
	CLL(0xF720C020F700), _T('M'),
	CLL(0xF7408001F700), _T('N'),
	CLL(0xE3141414E300), _T('O'),
	CLL(0xF79090906000), _T('P'),
	CLL(0xE3141512E500), _T('Q'),
	CLL(0xF79091926400), _T('R'),
	CLL(0x629494942300), _T('S'),
	CLL(0x1010F7101000), _T('T'),
	CLL(0xF3040404F300), _T('U'),
	CLL(0x708106817000), _T('V'),
	CLL(0xF7028102F700), _T('W'),
	CLL(0x364180413600), _T('X'),
	CLL(0x304087403000), _T('Y'),
	CLL(0x161594543400), _T('Z'),
	CLL(0x00F714140000), _T('['),
	CLL(0x204080010200), _T('\\'),
	CLL(0x001414F70000), _T(']'),
	CLL(0x402010204000), _T('^'),
	CLL(0x080808080800), _T('_'),
	CLL(0x003040000000), _T('`'),
	CLL(0x024545458700), _T('a'),
	CLL(0xF74444448300), _T('b'),
	CLL(0x834444444400), _T('c'),
	CLL(0x83444444F700), _T('d'),
	CLL(0x834545458500), _T('e'),
	CLL(0x0080E7902000), _T('f'),
	CLL(0x80E790200000), _T('f'),			// alternate
	CLL(0x814A4A4A8700), _T('g'),
	CLL(0xF74040408700), _T('h'),
	CLL(0x0044D7040000), _T('i'),
	CLL(0x00040848D700), _T('j'),
	CLL(0x040848D70000), _T('j'),			// alternate
	CLL(0xF70182440000), _T('k'),
	CLL(0x0014F7040000), _T('l'),
	CLL(0xC7408340C700), _T('m'),
	CLL(0xC74040408700), _T('n'),
	CLL(0x834444448300), _T('o'),
	CLL(0xCF4242428100), _T('p'),
	CLL(0x81424242CF00), _T('q'),
	CLL(0xC78040404000), _T('r'),
	CLL(0x844545454200), _T('s'),
	CLL(0x0040F3440200), _T('t'),
	CLL(0x40F344020000), _T('t'),			// alternate
	CLL(0xC3040404C700), _T('u'),
	CLL(0xC1020402C100), _T('v'),
	CLL(0xC3040304C300), _T('w'),
	CLL(0x448201824400), _T('x'),
	CLL(0xC10A0A0AC700), _T('y'),
	CLL(0x408007804000), _T('y'),			// in stack
	CLL(0x444645C44400), _T('z'),
	CLL(0x806314140000), _T('{'),
	CLL(0x0000F7000000), _T('|'),
	CLL(0x001414638000), _T('}'),
	CLL(0x804080018000), _T('~'),
	CLL(0x84E794142000), _T('£'),
	CLL(0x8041A2412200), _T('«'),
	CLL(0x007050700000), _T('°'),
	CLL(0x00E0A0E00000), _T('°'),			// alternate
	CLL(0xCF0404C30400), _T('µ'),
	CLL(0x2241A2418000), _T('»'),
	CLL(0x038454040200), _T('¿'),
	CLL(0x876151619700), _T('Ã'),
	CLL(0xC7312131C700), _T('Ä'),
	CLL(0x876151618700), _T('Å'),
	CLL(0xE790F7941400), _T('Æ'),
	CLL(0xE11216122100), _T('Ç'),
	CLL(0xC7A01122D700), _T('Ñ'),
	CLL(0xC3342434C300), _T('Ö'),
	CLL(0x224180412200), _T('×'),
	CLL(0xE2159454A300), _T('Ø'),
	CLL(0xC3140414C300), _T('Ü'),
	CLL(0x8080A2808000), _T('÷')
};

//################
//#
//#    Clamshell
//#
//################

//
// decode character of Centipede display
//
static TCHAR DecodeCharLewis(QWORD qwImage)
{
	UINT i;

	// search for character image in table
	for (i = 0; i < ARRAYSIZEOF(sGraphictabLewis); ++i)
	{
		// found known image
		if (sGraphictabLewis[i].qwImage == qwImage)
		{
			return sGraphictabLewis[i].cChar;
		}
	}

	#if defined DEBUG_PATTERN
	{
		TCHAR buffer[256];
		_sntprintf(buffer,ARRAYSIZEOF(buffer),_T("Segment Pattern = 0x%012I64X\n"),qwImage);
		OutputDebugString(buffer);
	}
	#endif
	return '·';								// unknown character
}

//
// interprete one character of Centipede Clamshell display
//
static __inline TCHAR GetCharClamshell(INT nXPos, INT nYPos)
{
	QWORD qwImage;
	DWORD dwAddr;
	INT   i;

	nXPos *= 6;								// 6 pixel width

	// build character image
	qwImage = 0;
	for (i = 0; i < 6; ++i)					// pixel width
	{
		dwAddr = (nXPos+i) << 3;

		if (dwAddr >= SDISP_END-SDISP_BEGIN+1)	// master
		{
			dwAddr -= (SDISP_END-SDISP_BEGIN+1);
			dwAddr |= (nYPos * 2);

			if (dwAddr <= MDISP_END)		// visible area
			{
				_ASSERT(dwAddr >= MDISP_BEGIN && dwAddr <= MDISP_END);
				qwImage <<= 4;
				qwImage |= Chipset.IORamM[dwAddr];
				qwImage <<= 4;
				qwImage |= Chipset.IORamM[dwAddr+1];
			}
			else							// hidden, fill with 0
			{
				qwImage <<= 8;
			}
		}
		else								// slave
		{
			dwAddr += SDISP_BEGIN;
			dwAddr |= (nYPos * 2);
			_ASSERT(dwAddr >= SDISP_BEGIN && dwAddr <= SDISP_END);

			qwImage <<= 4;
			qwImage |= Chipset.IORamS[dwAddr];
			qwImage <<= 4;
			qwImage |= Chipset.IORamS[dwAddr+1];
		}
	}
	return DecodeCharLewis(qwImage);
}

//
// interprete characters of Centipede Clamshell display
//
static VOID GetLcdNumberClamshell(LPTSTR szContent)
{
	INT i,x,y;

	i = 0;
	for (y = 0; y < 4; ++y)					// row
	{
		for (x = 0; x < 23; ++x)			// column
		{
			szContent[i++] = GetCharClamshell(x,y);
		}
		szContent[i++] = _T('\r');
		szContent[i++] = _T('\n');
	}
	szContent[i] = 0;						// EOS
	return;
}


//################
//#
//#    Public
//#
//################

//
// get text display string
//
VOID GetLcdNumber(LPTSTR szContent)
{
	GetLcdNumberClamshell(szContent);
	return;
}
