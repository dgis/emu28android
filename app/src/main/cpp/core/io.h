/*
 *   io.h
 *
 *   This file is part of Emu28
 *
 *   Copyright (C) 2002 Christoph Gieﬂelink
 *
 */

// display/timer I/O addresses without mapping offset
#define DAREA_BEGIN	0x000					// start of display area
#define	DAREA_END	0x2DF					// end of display area

// LCD addresses without mapping offset
#define MDISP_BEGIN	0x000					// Display begin in master
#define MDISP_END	0x1DF					// Display end in master
#define MROW_BEGIN	0x1E0					// Display row definition block begin
#define MROW_END	0x2DF					// Display row definition block end
#define SDISP_BEGIN	0x078					// Display begin in slave
#define SDISP_END	0x2DF					// Display end in slave
#define SLA_BUSY	0x000					// Annunciator - Busy
#define SLA_ALPHA	0x008					// Annunciator - Alpha
#define SLA_BAT		0x010					// Annunciator - Battery
#define SLA_SHIFT	0x018					// Annunciator - Shift
#define SLA_RAD		0x020					// Annunciator - 2*Pi
#define SLA_HALT	0x028					// Annunciator - Halt
#define SLA_PRINTER	0x030					// Annunciator - Printer
#define SLA_ALL		0x038					// Annunciator - all

// register I/O addresses without mapping offset
#define LPD			0x300					// Low Power Detection
#define CONTRAST	0x301					// Display contrast control
#define DSPTEST		0x302					// Display test
#define DSPCTL		0x303					// Display control + BIN
#define TIMERCTL	0x30a					// Timer Control
#define RAMTST		0x30b					// RAM test
#define INPORT		0x30c
#define LEDOUT		0x30d
#define TIMER		0x3f8					// Timer (32 bit, LSB first)

// 0x300 Low Power Detection [LBION 0 0 LBI]
#define LBION		0x08					// Low Battery Indicator circuit ON
#define XTRA_2		0x04					// unused bit
#define XTRA_1		0x02					// unused bit
#define LBI			0x01					// Low Battery Indicator (Read only)

// 0x301 Display contrast [CONT3 CONT2 CONT1 CONT0]
#define CONT3		0x08					// Display CONTrast Bit3
#define CONT2		0x04					// Display CONTrast Bit2
#define CONT1		0x02					// Display CONTrast Bit1
#define CONT0		0x01					// Display CONTrast Bit0

// 0x302 Display test [IAM VTON CLTM1 CLTM0]
#define IAM			0x08					// I Am Master
#define VTON		0x04					// voltage select
#define CLTM1		0x02					// CoLumn Test Mode [1]
#define CLTM0		0x01					// CoLumn Test Mode [0]

// 0x303 Display control and DON [DON 0 0 0]
#define DON			0x08					// Display On
#define XTRA_2		0x04					// unused bit
#define XTRA_1		0x02					// unused bit
#define XTRA_0		0x01					// unused bit

// 0x30a Timer Control [SRQ WKE INT RUN]
#define SRQ			0x08					// Service request
#define WKE			0x04					// Wake up
#define INTR		0x02					// Interrupt
#define RUN			0x01					// Timer run

// 0x30b RAM test [LOCK LOCK DDP DPC]
#define PCL			0x08					// PreCarge the ram bit Lines
#define LOCK		0x04					// 
#define DDP			0x02					// Decrement the DP
#define DPC			0x01					// Decrement the PC

// 0x30c Input Port [RX SREQ ST1 ST0]
#define RX			0x08					// RX - LED pin state
#define SREQ		0x04					// Service REQest
#define ST1			0x02					// ST[1] - Service Type [1]
#define ST0			0x01					// ST[1] - Service Type [0]

// 0x30d LED output [XTRA EPD STL DRL]
#define XTRA_3		0x08					// unused bit
#define EPD			0x04					// Enable Pull Down
#define STL			0x02					// STrobe Low
#define DRL			0x01					// DRive Low
