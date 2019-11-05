/*
 *   external.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gießelink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "ops.h"

VOID External(CHIPSET* w)					// =makebeep patch
{
	DWORD freq,dur;

	freq = Npack(w->D,5);					// frequency in Hz
	dur = Npack(w->C,5);					// duration in ms

	if (freq > 4400) freq = 4400;			// high limit of HP

	SoundBeep(freq,dur);					// wave output over sound card

	w->cycles += dur * 640;					// estimate cpu cycles for beeping time (640kHz)

	// original routine return with...
	w->P = 0;								// P=0
	w->intk = TRUE;							// INTON
	w->carry = FALSE;						// RTNCC
	w->pc = rstkpop();
	return;
}
