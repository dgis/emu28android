/*
 *   timer.c
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gieﬂelink
 *
 */
#include "pch.h"
#include "Emu28.h"
#include "io.h"								// I/O definitions

#define AUTO_OFF	10						// Time in minutes for 'auto off'

// Ticks for 01.01.1970 00:00:00
#define UNIX_0_TIME	0x0001cf2e8f800000

// memory address for clock and auto off

// HP28C = 0x4F003-0x4F00E
#define PALADIN_TIME	0x00003				// clock
// #define PALADIN_OFFCNT	0x0015A				// one minute off counter

#define T_FREQ		8192					// Timer frequency

// typecast define for easy access
#define IOReg(c,o) ((pIORam[c])[o])			// independent I/O access

static TIMECAPS tc;							// timer information
static UINT uTMaxTicks = 0;					// max. timer ticks handled by one timer event

static BOOL bAccurateTimer = FALSE;			// flag if accurate timer is used

static BOOL  bStarted[]  = { FALSE, FALSE };
static BOOL  bOutRange[] = { FALSE, FALSE }; // flag if timer value out of range
static UINT  uTimerId[]  = { 0, 0 };

static LARGE_INTEGER lTRef[2];				// counter value at timer start
static DWORD dwTRef[2];						// timer value at last timer access
static DWORD dwTCyc[2];						// cpu cycle counter at last timer access

// tables for timer access
static DWORD  *pT[]    = { &Chipset.tM,    &Chipset.tS };
static LPBYTE pIORam[] = { Chipset.IORamM, Chipset.IORamS };

static void CALLBACK TimeProc(UINT uEventId, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);

static DWORD CalcT(enum CHIP eChip)			// calculate timer value
{
	DWORD dwT = *pT[eChip];					// get value from chipset
	if (bStarted[eChip])					// timer running
	{
		LARGE_INTEGER lTAct;
		DWORD         dwTDif;

		// timer should run a little bit faster (10%) than maschine in authentic speed mode
		DWORD dwCycPerTick = (9 * T2CYCLES) / 5;

		QueryPerformanceCounter(&lTAct);	// actual time
		// calculate realtime timer ticks since reference point
		dwT -= (DWORD)
			   (((lTAct.QuadPart - lTRef[eChip].QuadPart) * T_FREQ)
			   / lFreq.QuadPart);

		dwTDif = dwTRef[eChip] - dwT;		// timer ticks since last request

		// checking if the MSB of dwTDif can be used as sign flag
		_ASSERT((DWORD) tc.wPeriodMax < ((1<<(sizeof(dwTDif)*8-1))/8192)*1000);

		// 2nd timer call in a 32ms time frame or elapsed time is negative (Win2k bug)
		if (!Chipset.Shutdn && ((dwTDif > 0x01 && dwTDif <= 0x100) || (dwTDif & 0x80000000) != 0))
		{
			DWORD dwTTicks = ((DWORD) (Chipset.cycles & 0xFFFFFFFF) - dwTCyc[eChip]) / dwCycPerTick;

			// estimated < real elapsed timer ticks or negative time
			if (dwTTicks < dwTDif || (dwTDif & 0x80000000) != 0)
			{
				// real time too long or got negative time elapsed
				dwT = dwTRef[eChip] - dwTTicks; // estimated timer value from CPU cycles
				dwTCyc[eChip] += dwTTicks * dwCycPerTick; // estimated CPU cycles for the timer ticks
			}
			else
			{
				// reached actual time -> new synchronizing
				dwTCyc[eChip] = (DWORD) (Chipset.cycles & 0xFFFFFFFF) - dwCycPerTick;
			}
		}
		else
		{
			// valid actual time -> new synchronizing
			dwTCyc[eChip] = (DWORD) (Chipset.cycles & 0xFFFFFFFF) - dwCycPerTick;
		}

		// check if timer interrupt is active -> no timer value below 0xFFFFFFFF
		if (   Chipset.inte
			&& (dwT & 0x80000000) != 0
			&& (!Chipset.Shutdn || (IOReg(eChip,TIMERCTL)&WKE))
			&& (IOReg(eChip,TIMERCTL)&INTR)
		   )
		{
			dwT = 0xFFFFFFFF;
			dwTCyc[eChip] = (DWORD) (Chipset.cycles & 0xFFFFFFFF) - dwCycPerTick;
		}

		dwTRef[eChip] = dwT;				// new reference time
	}
	return dwT;
}

static VOID CheckT(enum CHIP eChip, DWORD dwT)
{
	if ((dwT&0x80000000) == 0)				// timer MSB not set
	{
		IOReg(eChip,TIMERCTL) &= ~SRQ;		// clear SRQ bit
		return;
	}

	_ASSERT((dwT&0x80000000) != 0);			// timer MSB set

	// timer MSB is one and either INT or WAKE is set
	if ((IOReg(eChip,TIMERCTL)&(WKE|INTR)) != 0)
		IOReg(eChip,TIMERCTL) |= SRQ;		// set SRQ
	// cpu not sleeping and T -> Interrupt
	if (   (!Chipset.Shutdn || (IOReg(eChip,TIMERCTL)&WKE))
		&& (IOReg(eChip,TIMERCTL)&INTR))
	{
		Chipset.SoftInt = TRUE;
		bInterrupt = TRUE;
	}
	// cpu sleeping and T -> Wake Up
	if (Chipset.Shutdn && (IOReg(eChip,TIMERCTL)&WKE))
	{
		IOReg(eChip,TIMERCTL) &= ~WKE;		// clear WKE bit
		Chipset.bShutdnWake = TRUE;			// wake up from SHUTDN mode
		SetEvent(hEventShutdn);				// wake up emulation thread
	}
	return;
}

static VOID RescheduleT(enum CHIP eChip, BOOL bRefPoint)
{
	UINT uDelay;
	_ASSERT(uTimerId[eChip] == 0);			// timer must stopped
	if (bRefPoint)							// save reference time
	{
		dwTRef[eChip] = *pT[eChip];		// timer value at last timer access
		dwTCyc[eChip] = (DWORD) (Chipset.cycles & 0xFFFFFFFF); // cpu cycle counter at last timer access
		QueryPerformanceCounter(&lTRef[eChip]);// time of corresponding Chipset.t value
		uDelay = *pT[eChip];				// timer value for delay
	}
	else									// called without new refpoint, restart t with actual value
	{
		uDelay = CalcT(eChip);				// actual timer value for delay
	}
	if ((bOutRange[eChip] = uDelay > uTMaxTicks)) // delay greater maximum delay
		uDelay = uTMaxTicks;				// wait maximum delay time
	uDelay = (uDelay * 125 + 1023) / 1024;	// timer delay in ms (1000/8192 = 125/1024)
	uDelay = __max(tc.wPeriodMin,uDelay);	// wait minimum delay of timer
	_ASSERT(uDelay <= tc.wPeriodMax);		// inside maximum event delay
	// start timer; schedule event, when Chipset.t will be zero
	VERIFY(uTimerId[eChip] = timeSetEvent(uDelay,0,(LPTIMECALLBACK)&TimeProc,2,TIME_ONESHOT));
	return;
}

static VOID AbortT(enum CHIP eChip)
{
	_ASSERT(uTimerId[eChip]);
	timeKillEvent(uTimerId[eChip]);			// kill event
	uTimerId[eChip] = 0;					// then reset var
	return;
}

static void CALLBACK TimeProc(UINT uEventId, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	if (uEventId == 0) return;				// illegal EventId

	if (uEventId == uTimerId[MASTER])		// called from master timer event, Chipset.tM should be zero
	{
		EnterCriticalSection(&csTLock);
		{
			uTimerId[MASTER] = 0;			// single shot timer master timer stopped
			if (!bOutRange[MASTER])			// timer event elapsed
			{
				// timer overrun, test timer control bits else restart timer
				Chipset.tM = CalcT(MASTER);	// calculate new timer value
				CheckT(MASTER,Chipset.tM);	// test master timer control bits
			}
			RescheduleT(MASTER,!bOutRange[MASTER]); // restart master timer
		}
		LeaveCriticalSection(&csTLock);
		return;
	}
	if (uEventId == uTimerId[SLAVE])		// called from slave timer event, Chipset.tS should be zero
	{
		EnterCriticalSection(&csTLock);
		{
			uTimerId[SLAVE] = 0;			// single shot timer slave timer stopped
			if (!bOutRange[SLAVE])			// timer event elapsed
			{
				// timer overrun, test timer control bits else restart timer
				Chipset.tS = CalcT(SLAVE);	// calculate new timer value
				CheckT(SLAVE,Chipset.tS);	// test slave timer control bits
			}
			RescheduleT(SLAVE,!bOutRange[SLAVE]); // restart slave timer
		}
		LeaveCriticalSection(&csTLock);
		return;
	}
	return;
	UNREFERENCED_PARAMETER(uMsg);
	UNREFERENCED_PARAMETER(dwUser);
	UNREFERENCED_PARAMETER(dw1);
	UNREFERENCED_PARAMETER(dw2);
}

VOID SetHPTime(VOID)						// set date and time
{
	SYSTEMTIME ts;
	LONGLONG   ticks;
	DWORD      dw;
	WORD       i;

	_ASSERT(sizeof(LONGLONG) == 8);			// check size of datatype

	GetLocalTime(&ts);						// local time, _ftime() cause memory/resource leaks

	// calculate days until 01.01.0000 (Erlang BIF localtime/0)
	dw = (DWORD) ts.wMonth;
	if (dw > 2)
		dw -= 3L;
	else
	{
		dw += 9L;
		--ts.wYear;
	}
	dw = (DWORD) ts.wDay + (153L * dw + 2L) / 5L;
	dw += (146097L * (((DWORD) ts.wYear) / 100L)) / 4L;
	dw +=   (1461L * (((DWORD) ts.wYear) % 100L)) / 4L;
	dw += (-719469L + 719528L);				// days from year 0

	ticks = (ULONGLONG) dw;					// convert to 64 bit

	// convert into seconds and add time
	ticks = ticks * 24L + (ULONGLONG) ts.wHour;
	ticks = ticks * 60L + (ULONGLONG) ts.wMinute;
	ticks = ticks * 60L + (ULONGLONG) ts.wSecond;

	// create timerticks = (s + ms) * 8192
	ticks = (ticks << 13) | (((ULONGLONG) ts.wMilliseconds << 10) / 125);

	ticks += (LONG) Chipset.tM;				// add actual timer value

	if (Chipset.type == 'P')				// Paladin, HP28C
	{
		dw = PALADIN_TIME;					// HP address for clock (=NEXTIRQ) in RAM

		for (i = 0; i < 12; ++i, ++dw)		// write date and time
		{
			// always store in slave RAM
			Chipset.RamS[dw] = (BYTE) ticks & 0xf;
			ticks >>= 4;
		}

		// HP address for timeout 1 minute counter
//		Chipset.RamS[PALADIN_OFFCNT] = AUTO_OFF;
		return;
	}
	return;
}

VOID StartTimers(enum CHIP eChip)
{
	// master chip not set
	// or try to start slave timer when master timer stopped
	if (   (Chipset.IORamM[DSPTEST] & IAM) == 0
		|| (eChip == SLAVE && (Chipset.IORamM[TIMERCTL] & RUN) == 0))
		return;

	if (IOReg(eChip,TIMERCTL)&RUN)			// start timer ?
	{
		if (bStarted[eChip])				// timer running
			return;							// -> quit

		bStarted[eChip] = TRUE;				// flag timer running

		if (uTMaxTicks == 0)				// init once
		{
			timeGetDevCaps(&tc,sizeof(tc));	// get timer resolution

			// max. timer ticks that can be handled by one timer event
			uTMaxTicks = __min((0xFFFFFFFF / 1024),tc.wPeriodMax);
			uTMaxTicks = __min((0xFFFFFFFF - 1023) / 125,uTMaxTicks * 1024 / 125);
		}

		CheckT(eChip,*pT[eChip]);			// check for timer interrupts
		// set timer resolution to 1 ms, if failed don't use "Accurate timer"
		if (bAccurateTimer == FALSE)
			bAccurateTimer = (timeBeginPeriod(tc.wPeriodMin) == TIMERR_NOERROR);
		// set timer with given period
		RescheduleT(eChip,TRUE);			// start timer
	}
	return;
}

VOID StopTimers(enum CHIP eChip)
{
	if (eChip == MASTER && bStarted[SLAVE])	// want to stop master timer but slave timer is running
		StopTimers(SLAVE);					// stop slave timer first

	if (uTimerId[eChip] != 0)				// timer running
	{
		EnterCriticalSection(&csTLock);
		{
			*pT[eChip] = CalcT(eChip);		// update chipset timer value
		}
		LeaveCriticalSection(&csTLock);
		AbortT(eChip);						// stop outside critical section
	}
	bStarted[eChip] = FALSE;

	// "Accurate timer" running and timer stopped
	if (bAccurateTimer && !bStarted[MASTER] && !bStarted[SLAVE])
	{
		timeEndPeriod(tc.wPeriodMin);		// finish service
		bAccurateTimer = FALSE;
	}
	return;
}

DWORD ReadT(enum CHIP eChip)
{
	DWORD dwT;
	EnterCriticalSection(&csTLock);
	{
		dwT = CalcT(eChip);					// calculate timer value or if stopped last timer value
		CheckT(eChip,dwT);					// update timer control bits
	}
	LeaveCriticalSection(&csTLock);
	return dwT;
}

VOID SetT(enum CHIP eChip, DWORD dwValue)
{
	// calling AbortT() inside Critical Section handler may cause a dead lock
	if (uTimerId[eChip] != 0)				// timer running
		AbortT(eChip);						// stop timer
	EnterCriticalSection(&csTLock);
	{
		*pT[eChip] = dwValue;				// set new value
		CheckT(eChip,*pT[eChip]);			// test timer control bits
		if (bStarted[eChip])				// timer running
			RescheduleT(eChip,TRUE);		// restart timer
	}
	LeaveCriticalSection(&csTLock);
	return;
}
