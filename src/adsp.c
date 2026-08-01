/*
 * adsp.c — native ADSP-2105/DCS-2 audio engine for Encore's Unicorn build.
 *
 * The instruction interpreter below is ported from main:qemu/p2k-adsp2105-core.c
 * (BSD-3-Clause Aaron Giles/MAME-derived standalone core). The DCS glue is a
 * plain-C/pthread adaptation of main:qemu/p2k-dcs-adsp.c: it loads the original
 * DCS U109/U110 data already prepared by rom.c plus the update *_sf.rom sound
 * flash, executes the ADSP boot/runtime program, handles the host mailbox, and
 * exposes SPORT1 PCM. No ADSP opcodes are intentionally stubbed here; the full
 * ADSP-2105 interpreter from main is compiled into this file.
 */
#include "encore.h"
#include "adsp.h"

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <strings.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static inline uint16_t encore_lduw_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/*###################################################################################################
**
**
**		ADSP2100.c
**		Core implementation for the portable Analog ADSP-2100 emulator.
**		Written by Aaron Giles
**
**
**#################################################################################################*/


/* This standalone C adaptation follows the current BSD-licensed MAME
 * ADSP-21xx core.  The older flat-C layout is retained because it keeps the
 * core independent of MAME's device framework and suitable for embedding in
 * QEMU. */

#define HAS_ADSP2101 0
#define HAS_ADSP2104 1
#define HAS_ADSP2105 1
#define HAS_ADSP2115 0
#define TRACK_HOTSPOTS 0
#define INLINE inline
#define CALL_MAME_DEBUG do { } while (0)
#define CLEAR_LINE 0
#define ASSERT_LINE 1

typedef uint8_t UINT8;
typedef int8_t INT8;
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint8_t data8_t;
typedef uint32_t data32_t;
typedef P2KAdspRxCallback RX_CALLBACK;
typedef P2KAdspTxCallback TX_CALLBACK;

#define BYTE_XOR_LE(a) (a)
#define REG_PC (-1)
#define REG_SP (-2)
#define REG_PREVIOUSPC (-3)
#define REG_SP_CONTENTS (-100)

/* Keep the register numbering used by the original core. */
enum {
    ADSP2100_PC = 1,
    ADSP2100_AX0, ADSP2100_AX1, ADSP2100_AY0, ADSP2100_AY1,
    ADSP2100_AR, ADSP2100_AF,
    ADSP2100_MX0, ADSP2100_MX1, ADSP2100_MY0, ADSP2100_MY1,
    ADSP2100_MR0, ADSP2100_MR1, ADSP2100_MR2, ADSP2100_MF,
    ADSP2100_SI, ADSP2100_SE, ADSP2100_SB, ADSP2100_SR0, ADSP2100_SR1,
    ADSP2100_I0, ADSP2100_I1, ADSP2100_I2, ADSP2100_I3,
    ADSP2100_I4, ADSP2100_I5, ADSP2100_I6, ADSP2100_I7,
    ADSP2100_L0, ADSP2100_L1, ADSP2100_L2, ADSP2100_L3,
    ADSP2100_L4, ADSP2100_L5, ADSP2100_L6, ADSP2100_L7,
    ADSP2100_M0, ADSP2100_M1, ADSP2100_M2, ADSP2100_M3,
    ADSP2100_M4, ADSP2100_M5, ADSP2100_M6, ADSP2100_M7,
    ADSP2100_PX, ADSP2100_CNTR, ADSP2100_ASTAT, ADSP2100_SSTAT,
    ADSP2100_MSTAT, ADSP2100_PCSP, ADSP2100_CNTRSP, ADSP2100_STATSP,
    ADSP2100_LOOPSP, ADSP2100_IMASK, ADSP2100_ICNTL,
    ADSP2100_IRQSTATE0, ADSP2100_IRQSTATE1, ADSP2100_IRQSTATE2,
    ADSP2100_IRQSTATE3, ADSP2100_FLAGIN, ADSP2100_FLAGOUT,
    ADSP2100_FL0, ADSP2100_FL1, ADSP2100_FL2,
    ADSP2100_AX0_SEC, ADSP2100_AX1_SEC, ADSP2100_AY0_SEC,
    ADSP2100_AY1_SEC, ADSP2100_AR_SEC, ADSP2100_AF_SEC,
    ADSP2100_MX0_SEC, ADSP2100_MX1_SEC, ADSP2100_MY0_SEC,
    ADSP2100_MY1_SEC, ADSP2100_MR0_SEC, ADSP2100_MR1_SEC,
    ADSP2100_MR2_SEC, ADSP2100_MF_SEC, ADSP2100_SI_SEC,
    ADSP2100_SE_SEC, ADSP2100_SB_SEC, ADSP2100_SR0_SEC,
    ADSP2100_SR1_SEC,
};

#define ADSP2101_IRQ0 0
#define ADSP2101_IRQ1 1
#define ADSP2101_IRQ2 2
#define ADSP2101_SPORT0_RX 3
#define ADSP2101_SPORT0_TX 4
#define ADSP2100_IRQ0 0
#define ADSP2100_IRQ1 1
#define ADSP2100_IRQ2 2
#define ADSP2100_IRQ3 3

static uint16_t (*s_data_read)(uint16_t address);
static void (*s_data_write)(uint16_t address, uint16_t value);
static uint32_t (*s_program_read)(uint16_t address);
static void (*s_program_write)(uint16_t address, uint32_t value);


/*###################################################################################################
**	CONSTANTS
**#################################################################################################*/

/* stack depths */
#define	PC_STACK_DEPTH		16
#define CNTR_STACK_DEPTH	4
#define STAT_STACK_DEPTH	4
#define LOOP_STACK_DEPTH	4

/* chip types */
#define CHIP_TYPE_ADSP2100	0
#define CHIP_TYPE_ADSP2101	1
#define CHIP_TYPE_ADSP2104	2
#define CHIP_TYPE_ADSP2105	3
#define CHIP_TYPE_ADSP2115	4


/*###################################################################################################
**	STRUCTURES & TYPEDEFS
**#################################################################################################*/

/* 16-bit registers that can be loaded signed or unsigned */
typedef union
{
	UINT16	u;
	INT16	s;
} ADSPREG16;


/* the SHIFT result register is 32 bits */
typedef union
{
#ifdef MSB_FIRST
	struct { ADSPREG16 sr1, sr0; } srx;
#else
	struct { ADSPREG16 sr0, sr1; } srx;
#endif
	UINT32 sr;
} SHIFTRESULT;


/* the MAC result register is 40 bits */
typedef union
{
#ifdef MSB_FIRST
	struct { ADSPREG16 mrzero, mr2, mr1, mr0; } mrx;
	struct { UINT32 mr1, mr0; } mry;
#else
	struct { ADSPREG16 mr0, mr1, mr2, mrzero; } mrx;
	struct { UINT32 mr0, mr1; } mry;
#endif
	UINT64 mr;
} MACRESULT;

/* there are two banks of "core" registers */
typedef struct ADSPCORE
{
	/* ALU registers */
	ADSPREG16	ax0, ax1;
	ADSPREG16	ay0, ay1;
	ADSPREG16	ar;
	ADSPREG16	af;

	/* MAC registers */
	ADSPREG16	mx0, mx1;
	ADSPREG16	my0, my1;
	MACRESULT	mr;
	ADSPREG16	mf;

	/* SHIFT registers */
	ADSPREG16	si;
	ADSPREG16	se;
	ADSPREG16	sb;
	SHIFTRESULT	sr;

	/* dummy registers */
	ADSPREG16	zero;
} ADSPCORE;


/* ADSP-2100 Registers */
typedef struct
{
	/* Core registers, 2 banks */
	ADSPCORE	core;
	ADSPCORE	alt;

	/* Memory addressing registers */
	UINT32		i[8];
	INT32		m[8];
	UINT32		l[8];
	UINT32		lmask[8];
	UINT32		base[8];
	UINT8		px;

	/* other CPU registers */
	UINT32		pc;
	UINT32		ppc;
	UINT32		loop;
	UINT32		loop_condition;
	UINT32		cntr;

	/* status registers */
	UINT32		astat;
	UINT32		sstat;
	UINT32		mstat;
	UINT32		astat_clear;
	UINT32		idle;

	/* stacks */
	UINT32		loop_stack[LOOP_STACK_DEPTH];
	UINT32		cntr_stack[CNTR_STACK_DEPTH];
	UINT32		pc_stack[PC_STACK_DEPTH];
	UINT8		stat_stack[STAT_STACK_DEPTH][3];
	INT32		pc_sp;
	INT32		cntr_sp;
	INT32		stat_sp;
	INT32		loop_sp;

	/* external I/O */
	UINT8		flagout;
	UINT8		flagin;
	UINT8		fl0;
	UINT8		fl1;
	UINT8		fl2;

	/* interrupt handling */
	UINT8		imask;
	UINT8		icntl;
	UINT16		ifc;
    UINT8    	irq_state[5];
    UINT8    	irq_latch[5];
    INT32		interrupt_cycles;
    int			(*irq_callback)(int irqline);
} adsp2100_Regs;



/*###################################################################################################
**	PUBLIC GLOBAL VARIABLES
**#################################################################################################*/

int	adsp2100_icount=50000;


/*###################################################################################################
**	PRIVATE GLOBAL VARIABLES
**#################################################################################################*/

static adsp2100_Regs adsp2100;

static int chip_type = CHIP_TYPE_ADSP2100;
static int mstat_mask;
static int imask_mask;

static UINT16 *reverse_table = 0;
static UINT16 *mask_table = 0;
static UINT8 *condition_table = 0;

static RX_CALLBACK sport_rx_callback = 0;
static TX_CALLBACK sport_tx_callback = 0;

#if TRACK_HOTSPOTS
static UINT32 pcbucket[0x4000];
#endif


/*###################################################################################################
**	PRIVATE FUNCTION PROTOTYPES
**#################################################################################################*/

static int create_tables(void);
static void check_irqs(void);


/*###################################################################################################
**	MEMORY ACCESSORS
**#################################################################################################*/

static INLINE UINT32 RWORD_DATA(UINT32 addr)
{
	return s_data_read(addr & 0x3fff);
}

static INLINE void WWORD_DATA(UINT32 addr, UINT32 data)
{
	s_data_write(addr & 0x3fff, data);
}

static INLINE UINT32 RWORD_PGM(UINT32 addr)
{
	return s_program_read(addr & 0x3fff);
}

static INLINE void WWORD_PGM(UINT32 addr, UINT32 data)
{
	s_program_write(addr & 0x3fff, data & 0xffffff);
}

#define ROPCODE() RWORD_PGM(adsp2100.pc)


/*###################################################################################################
**	OTHER INLINES
**#################################################################################################*/

static INLINE void set_core_2100(void)
{
	chip_type = CHIP_TYPE_ADSP2100;
	mstat_mask = 0x0f;
	imask_mask = 0x0f;
}

#if (HAS_ADSP2101)
static INLINE void set_core_2101(void)
{
	chip_type = CHIP_TYPE_ADSP2101;
	mstat_mask = 0x7f;
	imask_mask = 0x3f;
}
#endif

#if (HAS_ADSP2104)
static INLINE void set_core_2104(void)
{
	chip_type = CHIP_TYPE_ADSP2104;
	mstat_mask = 0x7f;
	imask_mask = 0x3f;
}
#endif

#if (HAS_ADSP2105)
static INLINE void set_core_2105(void)
{
	chip_type = CHIP_TYPE_ADSP2105;
	mstat_mask = 0x7f;
	imask_mask = 0x3f;
}
#endif

#if (HAS_ADSP2115)
static INLINE void set_core_2115(void)
{
	chip_type = CHIP_TYPE_ADSP2115;
	mstat_mask = 0x7f;
	imask_mask = 0x3f;
}
#endif


/*###################################################################################################
**	IMPORT CORE UTILITIES
**#################################################################################################*/

/*===========================================================================
	ASTAT -- ALU/MAC status register
===========================================================================*/

#define ADSP_TWEAK

/* flag definitions */
#define SSFLAG			0x80
#define MVFLAG			0x40
#define QFLAG			0x20
#define SFLAG			0x10
#define CFLAG			0x08
#define VFLAG			0x04
#define NFLAG			0x02
#define ZFLAG			0x01

/* extracts flags */
#define GET_SS			(adsp2100.astat & SSFLAG)
#define GET_MV			(adsp2100.astat & MVFLAG)
#define GET_Q			(adsp2100.astat &  QFLAG)
#define GET_S			(adsp2100.astat &  SFLAG)
#define GET_C			(adsp2100.astat &  CFLAG)
#define GET_V			(adsp2100.astat &  VFLAG)
#define GET_N			(adsp2100.astat &  NFLAG)
#define GET_Z			(adsp2100.astat &  ZFLAG)

/* clears flags */
#define CLR_SS			(adsp2100.astat &= ~SSFLAG)
#define CLR_MV			(adsp2100.astat &= ~MVFLAG)
#define CLR_Q			(adsp2100.astat &=  ~QFLAG)
#define CLR_S			(adsp2100.astat &=  ~SFLAG)
#define CLR_C			(adsp2100.astat &=  ~CFLAG)
#define CLR_V			(adsp2100.astat &=  ~VFLAG)
#define CLR_N			(adsp2100.astat &=  ~NFLAG)
#define CLR_Z			(adsp2100.astat &=  ~ZFLAG)

/* sets flags */
#define SET_SS			(adsp2100.astat |= SSFLAG)
#define SET_MV			(adsp2100.astat |= MVFLAG)
#define SET_Q			(adsp2100.astat |=  QFLAG)
#define SET_S			(adsp2100.astat |=  SFLAG)
#define SET_C			(adsp2100.astat |=  CFLAG)
#define SET_V			(adsp2100.astat |=  VFLAG)
#define SET_Z			(adsp2100.astat |=  ZFLAG)
#define SET_N			(adsp2100.astat |=  NFLAG)

/* flag clearing; must be done before setting */
#define CLR_FLAGS		(adsp2100.astat &= adsp2100.astat_clear)

/* compute flags */
#define CALC_Z(r)		(adsp2100.astat |= ((r & 0xffff) == 0))
#define CALC_N(r)		(adsp2100.astat |= (r >> 14) & 0x02)
#define CALC_V(s,d,r)	(adsp2100.astat |= ((s ^ d ^ r ^ (r >> 1)) >> 13) & 0x04)
#define CALC_C(r)		(adsp2100.astat |= (r >> 13) & 0x08)
#define CALC_C_SUB(r)	(adsp2100.astat |= (~r >> 13) & 0x08)
#define CALC_NZ(r) 		CLR_FLAGS; CALC_N(r); CALC_Z(r)
#define CALC_NZV(s,d,r) CLR_FLAGS; CALC_N(r); CALC_Z(r); CALC_V(s,d,r)
#define CALC_NZVC(s,d,r) CLR_FLAGS; CALC_N(r); CALC_Z(r); CALC_V(s,d,r); CALC_C(r)
#define CALC_NZVC_SUB(s,d,r) CLR_FLAGS; CALC_N(r); CALC_Z(r); CALC_V(s,d,r); CALC_C_SUB(r)



/*===========================================================================
	MSTAT -- ALU/MAC control register
===========================================================================*/

/* flag definitions */
#define MSTAT_BANK		0x01			/* register bank select */
#define MSTAT_REVERSE	0x02			/* bit-reverse addressing enable (DAG1) */
#define MSTAT_STICKYV	0x04			/* sticky ALU overflow enable */
#define MSTAT_SATURATE	0x08			/* AR saturation mode enable */
#define MSTAT_INTEGER	0x10			/* MAC result placement; 0=fractional, 1=integer */
#define MSTAT_TIMER		0x20			/* timer enable */
#define MSTAT_GOMODE	0x40			/* go mode enable */

/* you must call this in order to change MSTAT */
static INLINE void set_mstat(int new_value)
{
	if ((new_value ^ adsp2100.mstat) & MSTAT_BANK)
	{
		ADSPCORE temp = adsp2100.core;
		adsp2100.core = adsp2100.alt;
		adsp2100.alt = temp;
	}
	if (new_value & MSTAT_STICKYV)
		adsp2100.astat_clear = ~(CFLAG | NFLAG | ZFLAG);
	else
		adsp2100.astat_clear = ~(CFLAG | VFLAG | NFLAG | ZFLAG);
	adsp2100.mstat = new_value;
}


/*===========================================================================
	SSTAT -- stack status register
===========================================================================*/

/* flag definitions */
#define PC_EMPTY		0x01			/* PC stack empty */
#define PC_OVER			0x02			/* PC stack overflow */
#define COUNT_EMPTY		0x04			/* count stack empty */
#define COUNT_OVER		0x08			/* count stack overflow */
#define STATUS_EMPTY	0x10			/* status stack empty */
#define STATUS_OVER		0x20			/* status stack overflow */
#define LOOP_EMPTY		0x40			/* loop stack empty */
#define LOOP_OVER		0x80			/* loop stack overflow */



/*===========================================================================
	PC stack handlers
===========================================================================*/

static INLINE UINT32 pc_stack_top(void)
{
	if (adsp2100.pc_sp > 0)
		return adsp2100.pc_stack[adsp2100.pc_sp - 1];
	else
		return adsp2100.pc_stack[0];
}

static INLINE void set_pc_stack_top(UINT32 top)
{
	if (adsp2100.pc_sp > 0)
		adsp2100.pc_stack[adsp2100.pc_sp - 1] = top;
	else
		adsp2100.pc_stack[0] = top;
}

static INLINE void pc_stack_push(void)
{
	if (adsp2100.pc_sp < PC_STACK_DEPTH)
	{
		adsp2100.pc_stack[adsp2100.pc_sp] = adsp2100.pc;
		adsp2100.pc_sp++;
		adsp2100.sstat &= ~PC_EMPTY;
	}
	else
		adsp2100.sstat |= PC_OVER;
}

static INLINE void pc_stack_push_val(UINT32 val)
{
	if (adsp2100.pc_sp < PC_STACK_DEPTH)
	{
		adsp2100.pc_stack[adsp2100.pc_sp] = val;
		adsp2100.pc_sp++;
		adsp2100.sstat &= ~PC_EMPTY;
	}
	else
		adsp2100.sstat |= PC_OVER;
}

static INLINE void pc_stack_pop(void)
{
	if (adsp2100.pc_sp > 0)
	{
		adsp2100.pc_sp--;
		if (adsp2100.pc_sp == 0)
			adsp2100.sstat |= PC_EMPTY;
	}
	adsp2100.pc = adsp2100.pc_stack[adsp2100.pc_sp];
}

static INLINE UINT32 pc_stack_pop_val(void)
{
	if (adsp2100.pc_sp > 0)
	{
		adsp2100.pc_sp--;
		if (adsp2100.pc_sp == 0)
			adsp2100.sstat |= PC_EMPTY;
	}
	return adsp2100.pc_stack[adsp2100.pc_sp];
}


/*===========================================================================
	CNTR stack handlers
===========================================================================*/

static INLINE UINT32 cntr_stack_top(void)
{
	if (adsp2100.cntr_sp > 0)
		return adsp2100.cntr_stack[adsp2100.cntr_sp - 1];
	else
		return adsp2100.cntr_stack[0];
}

static INLINE void cntr_stack_push(void)
{
	if (adsp2100.cntr_sp < CNTR_STACK_DEPTH)
	{
		adsp2100.cntr_stack[adsp2100.cntr_sp] = adsp2100.cntr;
		adsp2100.cntr_sp++;
		adsp2100.sstat &= ~COUNT_EMPTY;
	}
	else
		adsp2100.sstat |= COUNT_OVER;
}

static INLINE void cntr_stack_pop(void)
{
	if (adsp2100.cntr_sp > 0)
	{
		adsp2100.cntr_sp--;
		if (adsp2100.cntr_sp == 0)
			adsp2100.sstat |= COUNT_EMPTY;
	}
	adsp2100.cntr = adsp2100.cntr_stack[adsp2100.cntr_sp];
}


/*===========================================================================
	LOOP stack handlers
===========================================================================*/

static INLINE UINT32 loop_stack_top(void)
{
	if (adsp2100.loop_sp > 0)
		return adsp2100.loop_stack[adsp2100.loop_sp - 1];
	else
		return adsp2100.loop_stack[0];
}

static INLINE void loop_stack_push(UINT32 value)
{
	if (adsp2100.loop_sp < LOOP_STACK_DEPTH)
	{
		adsp2100.loop_stack[adsp2100.loop_sp] = value;
		adsp2100.loop_sp++;
		adsp2100.loop = value >> 4;
		adsp2100.loop_condition = value & 15;
		adsp2100.sstat &= ~LOOP_EMPTY;
	}
	else
		adsp2100.sstat |= LOOP_OVER;
}

static INLINE void loop_stack_pop(void)
{
	if (adsp2100.loop_sp > 0)
	{
		adsp2100.loop_sp--;
		if (adsp2100.loop_sp == 0)
		{
			adsp2100.loop = 0xffff;
			adsp2100.loop_condition = 0;
			adsp2100.sstat |= LOOP_EMPTY;
		}
		else
		{
			adsp2100.loop = adsp2100.loop_stack[adsp2100.loop_sp -1] >> 4;
			adsp2100.loop_condition = adsp2100.loop_stack[adsp2100.loop_sp - 1] & 15;
		}
	}
}


/*===========================================================================
	STAT stack handlers
===========================================================================*/

static INLINE void stat_stack_push(void)
{
	if (adsp2100.stat_sp < STAT_STACK_DEPTH)
	{
		adsp2100.stat_stack[adsp2100.stat_sp][0] = adsp2100.mstat;
		adsp2100.stat_stack[adsp2100.stat_sp][1] = adsp2100.imask;
		adsp2100.stat_stack[adsp2100.stat_sp][2] = adsp2100.astat;
		adsp2100.stat_sp++;
		adsp2100.sstat &= ~STATUS_EMPTY;
	}
	else
		adsp2100.sstat |= STATUS_OVER;
}

static INLINE void stat_stack_pop(void)
{
	if (adsp2100.stat_sp > 0)
	{
		adsp2100.stat_sp--;
		if (adsp2100.stat_sp == 0)
			adsp2100.sstat |= STATUS_EMPTY;
	}
	set_mstat(adsp2100.stat_stack[adsp2100.stat_sp][0]);
	adsp2100.imask = adsp2100.stat_stack[adsp2100.stat_sp][1];
	adsp2100.astat = adsp2100.stat_stack[adsp2100.stat_sp][2];
 	check_irqs();
}



/*===========================================================================
	condition code checking
===========================================================================*/

static INLINE int CONDITION(int c)
{
	if (c != 14)
		return condition_table[((c) << 8) | adsp2100.astat];
	else if ((INT32)--adsp2100.cntr > 0)
		return 1;
	else
	{
		cntr_stack_pop();
		return 0;
	}
}



/*===========================================================================
	register writing
===========================================================================*/

static void wr_inval(INT32 val)
{
   /*logerror( "ADSP %04x: Writing to an invalid register!", adsp2100.ppc );*/
}

static void wr_ax0(INT32 val)   { adsp2100.core.ax0.s = val; }
static void wr_ax1(INT32 val)   { adsp2100.core.ax1.s = val; }
static void wr_mx0(INT32 val)   { adsp2100.core.mx0.s = val; }
static void wr_mx1(INT32 val)   { adsp2100.core.mx1.s = val; }
static void wr_ay0(INT32 val)   { adsp2100.core.ay0.s = val; }
static void wr_ay1(INT32 val)   { adsp2100.core.ay1.s = val; }
static void wr_my0(INT32 val)   { adsp2100.core.my0.s = val; }
static void wr_my1(INT32 val)   { adsp2100.core.my1.s = val; }
static void wr_si(INT32 val)    { adsp2100.core.si.s = val; }
static void wr_se(INT32 val)    { adsp2100.core.se.s = (INT8)val; }
static void wr_ar(INT32 val)    { adsp2100.core.ar.s = val; }
static void wr_mr0(INT32 val)   { adsp2100.core.mr.mrx.mr0.s = val; }
static void wr_mr1(INT32 val)   { adsp2100.core.mr.mrx.mr1.s = val; adsp2100.core.mr.mrx.mr2.s = (INT16)val >> 15; }
static void wr_mr2(INT32 val)   { adsp2100.core.mr.mrx.mr2.s = (INT8)val; }
static void wr_sr0(INT32 val)   { adsp2100.core.sr.srx.sr0.s = val; }
static void wr_sr1(INT32 val)   { adsp2100.core.sr.srx.sr1.s = val; }
static void wr_i0(INT32 val)    { adsp2100.i[0] = val & 0x3fff; adsp2100.base[0] = val & adsp2100.lmask[0]; }
static void wr_i1(INT32 val)    { adsp2100.i[1] = val & 0x3fff; adsp2100.base[1] = val & adsp2100.lmask[1]; }
static void wr_i2(INT32 val)    { adsp2100.i[2] = val & 0x3fff; adsp2100.base[2] = val & adsp2100.lmask[2]; }
static void wr_i3(INT32 val)    { adsp2100.i[3] = val & 0x3fff; adsp2100.base[3] = val & adsp2100.lmask[3]; }
static void wr_i4(INT32 val)    { adsp2100.i[4] = val & 0x3fff; adsp2100.base[4] = val & adsp2100.lmask[4]; }
static void wr_i5(INT32 val)    { adsp2100.i[5] = val & 0x3fff; adsp2100.base[5] = val & adsp2100.lmask[5]; }
static void wr_i6(INT32 val)    { adsp2100.i[6] = val & 0x3fff; adsp2100.base[6] = val & adsp2100.lmask[6]; }
static void wr_i7(INT32 val)    { adsp2100.i[7] = val & 0x3fff; adsp2100.base[7] = val & adsp2100.lmask[7]; }
static void wr_m0(INT32 val)    { adsp2100.m[0] = (INT32)(val << 18) >> 18; }
static void wr_m1(INT32 val)    { adsp2100.m[1] = (INT32)(val << 18) >> 18; }
static void wr_m2(INT32 val)    { adsp2100.m[2] = (INT32)(val << 18) >> 18; }
static void wr_m3(INT32 val)    { adsp2100.m[3] = (INT32)(val << 18) >> 18; }
static void wr_m4(INT32 val)    { adsp2100.m[4] = (INT32)(val << 18) >> 18; }
static void wr_m5(INT32 val)    { adsp2100.m[5] = (INT32)(val << 18) >> 18; }
static void wr_m6(INT32 val)    { adsp2100.m[6] = (INT32)(val << 18) >> 18; }
static void wr_m7(INT32 val)    { adsp2100.m[7] = (INT32)(val << 18) >> 18; }
static void wr_l0(INT32 val)    { adsp2100.l[0] = val & 0x3fff; adsp2100.lmask[0] = mask_table[val & 0x3fff]; adsp2100.base[0] = adsp2100.i[0] & adsp2100.lmask[0]; }
static void wr_l1(INT32 val)    { adsp2100.l[1] = val & 0x3fff; adsp2100.lmask[1] = mask_table[val & 0x3fff]; adsp2100.base[1] = adsp2100.i[1] & adsp2100.lmask[1]; }
static void wr_l2(INT32 val)    { adsp2100.l[2] = val & 0x3fff; adsp2100.lmask[2] = mask_table[val & 0x3fff]; adsp2100.base[2] = adsp2100.i[2] & adsp2100.lmask[2]; }
static void wr_l3(INT32 val)    { adsp2100.l[3] = val & 0x3fff; adsp2100.lmask[3] = mask_table[val & 0x3fff]; adsp2100.base[3] = adsp2100.i[3] & adsp2100.lmask[3]; }
static void wr_l4(INT32 val)    { adsp2100.l[4] = val & 0x3fff; adsp2100.lmask[4] = mask_table[val & 0x3fff]; adsp2100.base[4] = adsp2100.i[4] & adsp2100.lmask[4]; }
static void wr_l5(INT32 val)    { adsp2100.l[5] = val & 0x3fff; adsp2100.lmask[5] = mask_table[val & 0x3fff]; adsp2100.base[5] = adsp2100.i[5] & adsp2100.lmask[5]; }
static void wr_l6(INT32 val)    { adsp2100.l[6] = val & 0x3fff; adsp2100.lmask[6] = mask_table[val & 0x3fff]; adsp2100.base[6] = adsp2100.i[6] & adsp2100.lmask[6]; }
static void wr_l7(INT32 val)    { adsp2100.l[7] = val & 0x3fff; adsp2100.lmask[7] = mask_table[val & 0x3fff]; adsp2100.base[7] = adsp2100.i[7] & adsp2100.lmask[7]; }
static void wr_astat(INT32 val) { adsp2100.astat = val & 0x00ff; }
static void wr_mstat(INT32 val) { set_mstat(val & mstat_mask); }
static void wr_sstat(INT32 val) { adsp2100.sstat = val & 0x00ff; }
static void wr_imask(INT32 val) { adsp2100.imask = val & imask_mask; check_irqs(); }
static void wr_icntl(INT32 val) { adsp2100.icntl = val & 0x001f; check_irqs(); }
static void wr_cntr(INT32 val)  { cntr_stack_push(); adsp2100.cntr = val & 0x3fff; }
static void wr_sb(INT32 val)    { adsp2100.core.sb.s = (INT32)(val << 27) >> 27; }
static void wr_px(INT32 val)    { adsp2100.px = val; }
static void wr_ifc(INT32 val)
{
	adsp2100.ifc = val;
	if (val & 0x002) adsp2100.irq_latch[ADSP2101_IRQ0] = 0;
	if (val & 0x004) adsp2100.irq_latch[ADSP2101_IRQ1] = 0;
	if (val & 0x008) adsp2100.irq_latch[ADSP2101_SPORT0_RX] = 0;
	if (val & 0x010) adsp2100.irq_latch[ADSP2101_SPORT0_TX] = 0;
	if (val & 0x020) adsp2100.irq_latch[ADSP2101_IRQ2] = 0;
	if (val & 0x080) adsp2100.irq_latch[ADSP2101_IRQ0] = 1;
	if (val & 0x100) adsp2100.irq_latch[ADSP2101_IRQ1] = 1;
	if (val & 0x200) adsp2100.irq_latch[ADSP2101_SPORT0_RX] = 1;
	if (val & 0x400) adsp2100.irq_latch[ADSP2101_SPORT0_TX] = 1;
	if (val & 0x800) adsp2100.irq_latch[ADSP2101_IRQ2] = 1;
	check_irqs();
}
static void wr_tx0(INT32 val)	{ if (sport_tx_callback) (*sport_tx_callback)(0, val); }
static void wr_tx1(INT32 val)	{ if (sport_tx_callback) (*sport_tx_callback)(1, val); }
static void wr_owrctr(INT32 val) { adsp2100.cntr = val & 0x3fff; }
static void wr_topstack(INT32 val) { pc_stack_push_val(val & 0x3fff); }

#define WRITE_REG(grp,reg,val) ((*wr_reg[grp][reg])(val))

static void (*wr_reg[4][16])(INT32) =
{
	{
		wr_ax0, wr_ax1, wr_mx0, wr_mx1, wr_ay0, wr_ay1, wr_my0, wr_my1,
		wr_si, wr_se, wr_ar, wr_mr0, wr_mr1, wr_mr2, wr_sr0, wr_sr1
	},
	{
		wr_i0, wr_i1, wr_i2, wr_i3, wr_m0, wr_m1, wr_m2, wr_m3,
		wr_l0, wr_l1, wr_l2, wr_l3, wr_inval, wr_inval, wr_inval, wr_inval
	},
	{
		wr_i4, wr_i5, wr_i6, wr_i7, wr_m4, wr_m5, wr_m6, wr_m7,
		wr_l4, wr_l5, wr_l6, wr_l7, wr_inval, wr_inval, wr_inval, wr_inval
	},
	{
		wr_astat, wr_mstat, wr_inval, wr_imask, wr_icntl, wr_cntr, wr_sb, wr_px,
		wr_inval, wr_tx0, wr_inval, wr_tx1, wr_ifc, wr_owrctr, wr_inval, wr_topstack
	}
};



/*===========================================================================
	register reading
===========================================================================*/

static INT32 rd_inval(void)
{
   /*logerror( "ADSP %04x: Writing to an invalid register!", adsp2100.ppc );*/
   return 0;
}
static INT32 rd_ax0(void)   { return adsp2100.core.ax0.s; }
static INT32 rd_ax1(void)   { return adsp2100.core.ax1.s; }
static INT32 rd_mx0(void)   { return adsp2100.core.mx0.s; }
static INT32 rd_mx1(void)   { return adsp2100.core.mx1.s; }
static INT32 rd_ay0(void)   { return adsp2100.core.ay0.s; }
static INT32 rd_ay1(void)   { return adsp2100.core.ay1.s; }
static INT32 rd_my0(void)   { return adsp2100.core.my0.s; }
static INT32 rd_my1(void)   { return adsp2100.core.my1.s; }
static INT32 rd_si(void)    { return adsp2100.core.si.s; }
static INT32 rd_se(void)    { return adsp2100.core.se.s; }
static INT32 rd_ar(void)    { return adsp2100.core.ar.s; }
static INT32 rd_mr0(void)   { return adsp2100.core.mr.mrx.mr0.s; }
static INT32 rd_mr1(void)   { return adsp2100.core.mr.mrx.mr1.s; }
static INT32 rd_mr2(void)   { return adsp2100.core.mr.mrx.mr2.s; }
static INT32 rd_sr0(void)   { return adsp2100.core.sr.srx.sr0.s; }
static INT32 rd_sr1(void)   { return adsp2100.core.sr.srx.sr1.s; }
static INT32 rd_i0(void)    { return adsp2100.i[0]; }
static INT32 rd_i1(void)    { return adsp2100.i[1]; }
static INT32 rd_i2(void)    { return adsp2100.i[2]; }
static INT32 rd_i3(void)    { return adsp2100.i[3]; }
static INT32 rd_i4(void)    { return adsp2100.i[4]; }
static INT32 rd_i5(void)    { return adsp2100.i[5]; }
static INT32 rd_i6(void)    { return adsp2100.i[6]; }
static INT32 rd_i7(void)    { return adsp2100.i[7]; }
static INT32 rd_m0(void)    { return adsp2100.m[0]; }
static INT32 rd_m1(void)    { return adsp2100.m[1]; }
static INT32 rd_m2(void)    { return adsp2100.m[2]; }
static INT32 rd_m3(void)    { return adsp2100.m[3]; }
static INT32 rd_m4(void)    { return adsp2100.m[4]; }
static INT32 rd_m5(void)    { return adsp2100.m[5]; }
static INT32 rd_m6(void)    { return adsp2100.m[6]; }
static INT32 rd_m7(void)    { return adsp2100.m[7]; }
static INT32 rd_l0(void)    { return adsp2100.l[0]; }
static INT32 rd_l1(void)    { return adsp2100.l[1]; }
static INT32 rd_l2(void)    { return adsp2100.l[2]; }
static INT32 rd_l3(void)    { return adsp2100.l[3]; }
static INT32 rd_l4(void)    { return adsp2100.l[4]; }
static INT32 rd_l5(void)    { return adsp2100.l[5]; }
static INT32 rd_l6(void)    { return adsp2100.l[6]; }
static INT32 rd_l7(void)    { return adsp2100.l[7]; }
static INT32 rd_astat(void) { return adsp2100.astat; }
static INT32 rd_mstat(void) { return adsp2100.mstat; }
static INT32 rd_sstat(void) { return adsp2100.sstat; }
static INT32 rd_imask(void) { return adsp2100.imask; }
static INT32 rd_icntl(void) { return adsp2100.icntl; }
static INT32 rd_cntr(void)  { return adsp2100.cntr; }
static INT32 rd_sb(void)    { return adsp2100.core.sb.s; }
static INT32 rd_px(void)    { return adsp2100.px; }
static INT32 rd_rx0(void)	{ if (sport_rx_callback) return (*sport_rx_callback)(0); else return 0; }
static INT32 rd_rx1(void)	{ if (sport_rx_callback) return (*sport_rx_callback)(1); else return 0; }
static INT32 rd_stacktop(void)	{ return pc_stack_pop_val(); }

#define READ_REG(grp,reg) ((*rd_reg[grp][reg])())

static INT32 (*rd_reg[4][16])(void) =
{
	{
		rd_ax0, rd_ax1, rd_mx0, rd_mx1, rd_ay0, rd_ay1, rd_my0, rd_my1,
		rd_si, rd_se, rd_ar, rd_mr0, rd_mr1, rd_mr2, rd_sr0, rd_sr1
	},
	{
		rd_i0, rd_i1, rd_i2, rd_i3, rd_m0, rd_m1, rd_m2, rd_m3,
		rd_l0, rd_l1, rd_l2, rd_l3, rd_inval, rd_inval, rd_inval, rd_inval
	},
	{
		rd_i4, rd_i5, rd_i6, rd_i7, rd_m4, rd_m5, rd_m6, rd_m7,
		rd_l4, rd_l5, rd_l6, rd_l7, rd_inval, rd_inval, rd_inval, rd_inval
	},
	{
		rd_astat, rd_mstat, rd_sstat, rd_imask, rd_icntl, rd_cntr, rd_sb, rd_px,
		rd_rx0, rd_inval, rd_rx1, rd_inval, rd_inval, rd_inval, rd_inval, rd_stacktop
	}
};



/*===========================================================================
	Modulus addressing logic
===========================================================================*/

static INLINE void modify_address(UINT32 ireg, UINT32 mreg)
{
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;
}



/*===========================================================================
	Data memory accessors
===========================================================================*/

static INLINE void data_write_dag1(UINT32 op, INT32 val)
{
	UINT32 ireg = (op >> 2) & 3;
	UINT32 mreg = op & 3;
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];

	if ( adsp2100.mstat & MSTAT_REVERSE )
	{
		UINT32 ir = reverse_table[ i & 0x3fff ];
		WWORD_DATA(ir, val);
	}
	else
		WWORD_DATA(i, val);

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;
}


static INLINE UINT32 data_read_dag1(UINT32 op)
{
	UINT32 ireg = (op >> 2) & 3;
	UINT32 mreg = op & 3;
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];
	UINT32 res;

	if (adsp2100.mstat & MSTAT_REVERSE)
	{
		UINT32 ir = reverse_table[i & 0x3fff];
		res = RWORD_DATA(ir);
	}
	else
		res = RWORD_DATA(i);

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;

	return res;
}

static INLINE void data_write_dag2(UINT32 op, INT32 val)
{
	UINT32 ireg = 4 + ((op >> 2) & 3);
	UINT32 mreg = 4 + (op & 3);
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];

	WWORD_DATA(i, val);

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;
}


static INLINE UINT32 data_read_dag2(UINT32 op)
{
	UINT32 ireg = 4 + ((op >> 2) & 3);
	UINT32 mreg = 4 + (op & 3);
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];

	UINT32 res = RWORD_DATA(i);

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;

	return res;
}

/*===========================================================================
	Program memory accessors
===========================================================================*/

static INLINE void pgm_write_dag2(UINT32 op, INT32 val)
{
	UINT32 ireg = 4 + ((op >> 2) & 3);
	UINT32 mreg = 4 + (op & 3);
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];

	WWORD_PGM(i, (val << 8) | adsp2100.px);

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;
}


static INLINE UINT32 pgm_read_dag2(UINT32 op)
{
	UINT32 ireg = 4 + ((op >> 2) & 3);
	UINT32 mreg = 4 + (op & 3);
	UINT32 base = adsp2100.base[ireg];
	UINT32 i = adsp2100.i[ireg];
	UINT32 l = adsp2100.l[ireg];
	UINT32 res;

	res = RWORD_PGM(i);
	adsp2100.px = res;
	res >>= 8;

	i += adsp2100.m[mreg];
	if (i < base) i += l;
	else if (i >= base + l) i -= l;
	adsp2100.i[ireg] = i;

	return res;
}



/*===========================================================================
	ALU register reading
===========================================================================*/

#define ALU_GETXREG_UNSIGNED(x) (*(UINT16 *)alu_xregs[x])
#define ALU_GETXREG_SIGNED(x)   (*( INT16 *)alu_xregs[x])
#define ALU_GETYREG_UNSIGNED(y) (*(UINT16 *)alu_yregs[y])
#define ALU_GETYREG_SIGNED(y)   (*( INT16 *)alu_yregs[y])

static const void *alu_xregs[8] =
{
	&adsp2100.core.ax0,
	&adsp2100.core.ax1,
	&adsp2100.core.ar,
	&adsp2100.core.mr.mrx.mr0,
	&adsp2100.core.mr.mrx.mr1,
	&adsp2100.core.mr.mrx.mr2,
	&adsp2100.core.sr.srx.sr0,
	&adsp2100.core.sr.srx.sr1
};

static const void *alu_yregs[4] =
{
	&adsp2100.core.ay0,
	&adsp2100.core.ay1,
	&adsp2100.core.af,
	&adsp2100.core.zero
};



/*===========================================================================
	MAC register reading
===========================================================================*/

#define MAC_GETXREG_UNSIGNED(x) (*(UINT16 *)mac_xregs[x])
#define MAC_GETXREG_SIGNED(x)   (*( INT16 *)mac_xregs[x])
#define MAC_GETYREG_UNSIGNED(y) (*(UINT16 *)mac_yregs[y])
#define MAC_GETYREG_SIGNED(y)   (*( INT16 *)mac_yregs[y])

static const void *mac_xregs[8] =
{
	&adsp2100.core.mx0,
	&adsp2100.core.mx1,
	&adsp2100.core.ar,
	&adsp2100.core.mr.mrx.mr0,
	&adsp2100.core.mr.mrx.mr1,
	&adsp2100.core.mr.mrx.mr2,
	&adsp2100.core.sr.srx.sr0,
	&adsp2100.core.sr.srx.sr1
};

static const void *mac_yregs[4] =
{
	&adsp2100.core.my0,
	&adsp2100.core.my1,
	&adsp2100.core.mf,
	&adsp2100.core.zero
};



/*===========================================================================
	SHIFT register reading
===========================================================================*/

#define SHIFT_GETXREG_UNSIGNED(x) (*(UINT16 *)shift_xregs[x])
#define SHIFT_GETXREG_SIGNED(x)   (*( INT16 *)shift_xregs[x])

static const void *shift_xregs[8] =
{
	&adsp2100.core.si,
	&adsp2100.core.si,
	&adsp2100.core.ar,
	&adsp2100.core.mr.mrx.mr0,
	&adsp2100.core.mr.mrx.mr1,
	&adsp2100.core.mr.mrx.mr2,
	&adsp2100.core.sr.srx.sr0,
	&adsp2100.core.sr.srx.sr1
};



/*===========================================================================
	ALU operations (result in AR)
===========================================================================*/

void alu_op_ar(int op)
{
	INT32 xop = (op >> 8) & 7;
	INT32 yop = (op >> 11) & 3;
	INT32 res;

	/*switch ((op >> 13) & 15)*/
	switch (op & (15<<13))  /*JB*/
	{
		case 0x00<<13:
			/* Y				Clear when y = 0 */
			res = ALU_GETYREG_UNSIGNED(yop);
			CALC_NZ(res);
			break;
		case 0x01<<13:
			/* Y + 1			PASS 1 when y = 0 */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop + 1;
			CALC_NZ(res);
			if (yop == 0x7fff) SET_V;
			else if (yop == 0xffff) SET_C;
			break;
		case 0x02<<13:
			/* X + Y + C */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			yop += GET_C >> 3;
			res = xop + yop;
			CALC_NZVC(xop, yop, res);
			break;
		case 0x03<<13:
			/* X + Y			X when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop + yop;
			CALC_NZVC(xop, yop, res);
			break;
		case 0x04<<13:
			/* NOT Y */
			res = ALU_GETYREG_UNSIGNED(yop) ^ 0xffff;
			CALC_NZ(res);
			break;
		case 0x05<<13:
			/* -Y */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = -yop;
			CALC_NZ(res);
			if (yop == 0x8000) SET_V;
			if (yop == 0x0000) SET_C;
			break;
		case 0x06<<13:
			/* X - Y + C - 1	X + C - 1 when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop - yop + (GET_C >> 3) - 1;
			CALC_NZVC_SUB(xop, yop, res);
			break;
		case 0x07<<13:
			/* X - Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop - yop;
			CALC_NZVC_SUB(xop, yop, res);
			break;
		case 0x08<<13:
			/* Y - 1			PASS -1 when y = 0 */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - 1;
			CALC_NZ(res);
			if (yop == 0x8000) SET_V;
			else if (yop == 0x0000) SET_C;
			break;
		case 0x09<<13:
			/* Y - X			-X when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - xop;
			CALC_NZVC_SUB(yop, xop, res);
			break;
		case 0x0a<<13:
			/* Y - X + C - 1	-X + C - 1 when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - xop + (GET_C >> 3) - 1;
			CALC_NZVC_SUB(yop, xop, res);
			break;
		case 0x0b<<13:
			/* NOT X */
			res = ALU_GETXREG_UNSIGNED(xop) ^ 0xffff;
			CALC_NZ(res);
			break;
		case 0x0c<<13:
			/* X AND Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop & yop;
			CALC_NZ(res);
			break;
		case 0x0d<<13:
			/* X OR Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop | yop;
			CALC_NZ(res);
			break;
		case 0x0e<<13:
			/* X XOR Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop ^ yop;
			CALC_NZ(res);
			break;
		case 0x0f<<13:
			/* ABS X */
			xop = ALU_GETXREG_UNSIGNED(xop);
			res = (xop & 0x8000) ? -xop : xop;
			if (xop == 0) SET_Z;
			if (xop == 0x8000) SET_N, SET_V;
			CLR_S;
			if (xop & 0x8000) SET_S;
			break;
		default:
			res = 0;	/* just to keep the compiler happy */
			break;
	}

	/* saturate */
	if ((adsp2100.mstat & MSTAT_SATURATE) && GET_V) res = GET_C ? -32768 : 32767;

	/* set the final value */
	adsp2100.core.ar.u = res;
}



/*===========================================================================
	ALU operations (result in AF)
===========================================================================*/

void alu_op_af(int op)
{
	INT32 xop = (op >> 8) & 7;
	INT32 yop = (op >> 11) & 3;
	INT32 res;

/*	switch ((op >> 13) & 15)*/
	switch (op & (15<<13))  /*JB*/
	{
		case 0x00<<13:
			/* Y				Clear when y = 0 */
			res = ALU_GETYREG_UNSIGNED(yop);
			CALC_NZ(res);
			break;
		case 0x01<<13:
			/* Y + 1			PASS 1 when y = 0 */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop + 1;
			CALC_NZ(res);
			if (yop == 0x7fff) SET_V;
			else if (yop == 0xffff) SET_C;
			break;
		case 0x02<<13:
			/* X + Y + C */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			yop += GET_C >> 3;
			res = xop + yop;
			CALC_NZVC(xop, yop, res);
			break;
		case 0x03<<13:
			/* X + Y			X when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop + yop;
			CALC_NZVC(xop, yop, res);
			break;
		case 0x04<<13:
			/* NOT Y */
			res = ALU_GETYREG_UNSIGNED(yop) ^ 0xffff;
			CALC_NZ(res);
			break;
		case 0x05<<13:
			/* -Y */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = -yop;
			CALC_NZ(res);
			if (yop == 0x8000) SET_V;
			if (yop == 0x0000) SET_C;
			break;
		case 0x06<<13:
			/* X - Y + C - 1	X + C - 1 when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop - yop + (GET_C >> 3) - 1;
			CALC_NZVC_SUB(xop, yop, res);
			break;
		case 0x07<<13:
			/* X - Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop - yop;
			CALC_NZVC_SUB(xop, yop, res);
			break;
		case 0x08<<13:
			/* Y - 1			PASS -1 when y = 0 */
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - 1;
			CALC_NZ(res);
			if (yop == 0x8000) SET_V;
			else if (yop == 0x0000) SET_C;
			break;
		case 0x09<<13:
			/* Y - X			-X when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - xop;
			CALC_NZVC_SUB(yop, xop, res);
			break;
		case 0x0a<<13:
			/* Y - X + C - 1	-X + C - 1 when y = 0 */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = yop - xop + (GET_C >> 3) - 1;
			CALC_NZVC_SUB(yop, xop, res);
			break;
		case 0x0b<<13:
			/* NOT X */
			res = ALU_GETXREG_UNSIGNED(xop) ^ 0xffff;
			CALC_NZ(res);
			break;
		case 0x0c<<13:
			/* X AND Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop & yop;
			CALC_NZ(res);
			break;
		case 0x0d<<13:
			/* X OR Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop | yop;
			CALC_NZ(res);
			break;
		case 0x0e<<13:
			/* X XOR Y */
			xop = ALU_GETXREG_UNSIGNED(xop);
			yop = ALU_GETYREG_UNSIGNED(yop);
			res = xop ^ yop;
			CALC_NZ(res);
			break;
		case 0x0f<<13:
			/* ABS X */
			xop = ALU_GETXREG_UNSIGNED(xop);
			res = (xop & 0x8000) ? -xop : xop;
			if (xop == 0) SET_Z;
			if (xop == 0x8000) SET_N, SET_V;
			CLR_S;
			if (xop & 0x8000) SET_S;
			break;
		default:
			res = 0;	/* just to keep the compiler happy */
			break;
	}

	/* set the final value */
	adsp2100.core.af.u = res;
}



/*===========================================================================
	MAC operations (result in MR)
===========================================================================*/

void mac_op_mr(int op)
{
	INT8 shift = ((adsp2100.mstat & MSTAT_INTEGER) >> 4) ^ 1;
	INT32 xop = (op >> 8) & 7;
	INT32 yop = (op >> 11) & 3;
	INT32 temp;
	INT64 res;

/*	switch ((op >> 13) & 15)*/
	switch (op & (15<<13))	/*JB*/
	{
		case 0x00<<13:
			/* no-op */
			return;
		case 0x01<<13:
			/* X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x02<<13:
			/* MR + X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x03<<13:
			/* MR - X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x04<<13:
			/* X * Y (SS)		Clear when y = 0 */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x05<<13:
			/* X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x06<<13:
			/* X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x07<<13:
			/* X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x08<<13:
			/* MR + X * Y (SS) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x09<<13:
			/* MR + X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0a<<13:
			/* MR + X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0b<<13:
			/* MR + X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0c<<13:
			/* MR - X * Y (SS) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0d<<13:
			/* MR - X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0e<<13:
			/* MR - X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0f<<13:
			/* MR - X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		default:
			res = 0;	/* just to keep the compiler happy */
			break;
	}

	/* set the final value */
	temp = (res >> 31) & 0x1ff;
	CLR_MV;
	if (temp != 0x000 && temp != 0x1ff) SET_MV;
	adsp2100.core.mr.mr = res;
}



/*===========================================================================
	MAC operations (result in MF)
===========================================================================*/

void mac_op_mf(int op)
{
	INT8 shift = ((adsp2100.mstat & MSTAT_INTEGER) >> 4) ^ 1;
	INT32 xop = (op >> 8) & 7;
	INT32 yop = (op >> 11) & 3;
	INT32 temp;
	INT64 res;

/*	switch ((op >> 13) & 15)*/
	switch (op & (15<<13))	/*JB*/
	{
		case 0x00<<13:
			/* no-op */
			return;
		case 0x01<<13:
			/* X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x02<<13:
			/* MR + X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x03<<13:
			/* MR - X * Y (RND) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
#ifdef ADSP_TWEAK
			if ((res & 0xffff) == 0x8000) res &= ~((UINT64)0x10000);
			else res += (res & 0x8000) << 1;
#else
			temp &= 0xffff;
			res += 0x8000;
			if ( temp == 0x8000 )
				res &= ~((UINT64)0x10000);
#endif
			break;
		case 0x04<<13:
			/* X * Y (SS)		Clear when y = 0 */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x05<<13:
			/* X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x06<<13:
			/* X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x07<<13:
			/* X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = (INT64)temp;
			break;
		case 0x08<<13:
			/* MR + X * Y (SS) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x09<<13:
			/* MR + X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0a<<13:
			/* MR + X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0b<<13:
			/* MR + X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr + (INT64)temp;
			break;
		case 0x0c<<13:
			/* MR - X * Y (SS) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0d<<13:
			/* MR - X * Y (SU) */
			xop = MAC_GETXREG_SIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0e<<13:
			/* MR - X * Y (US) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_SIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		case 0x0f<<13:
			/* MR - X * Y (UU) */
			xop = MAC_GETXREG_UNSIGNED(xop);
			yop = MAC_GETYREG_UNSIGNED(yop);
			temp = (xop * yop) << shift;
			res = adsp2100.core.mr.mr - (INT64)temp;
			break;
		default:
			res = 0;	/* just to keep the compiler happy */
			break;
	}

	/* set the final value */
	adsp2100.core.mf.u = (UINT32)res >> 16;
}



/*===========================================================================
	SHIFT operations (result in SR/SE/SB)
===========================================================================*/

void shift_op(int op)
{
	INT8 sc = adsp2100.core.se.s;
	INT32 xop = (op >> 8) & 7;
	UINT32 res;

/*	switch ((op >> 11) & 15)*/
	switch (op & (15<<11))	/*JB*/
	{
		case 0x00<<11:
			/* LSHIFT (HI) */
			xop = SHIFT_GETXREG_UNSIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? ((UINT32)xop >> -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x01<<11:
			/* LSHIFT (HI, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? ((UINT32)xop >> -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x02<<11:
			/* LSHIFT (LO) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x03<<11:
			/* LSHIFT (LO, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x04<<11:
			/* ASHIFT (HI) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr = res;
			break;
		case 0x05<<11:
			/* ASHIFT (HI, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr |= res;
			break;
		case 0x06<<11:
			/* ASHIFT (LO) */
			xop = SHIFT_GETXREG_SIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr = res;
			break;
		case 0x07<<11:
			/* ASHIFT (LO, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr |= res;
			break;
		case 0x08<<11:
			/* NORM (HI) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0)
			{
				xop = ((UINT32)xop >> 1) | ((adsp2100.astat & CFLAG) << 28);
				res = xop >> (sc - 1);
			}
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x09<<11:
			/* NORM (HI, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0)
			{
				xop = ((UINT32)xop >> 1) | ((adsp2100.astat & CFLAG) << 28);
				res = xop >> (sc - 1);
			}
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x0a<<11:
			/* NORM (LO) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop >> sc) : 0;
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x0b<<11:
			/* NORM (LO, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop >> sc) : 0;
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x0c<<11:
			/* EXP (HI) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			res = 0;
			if (xop < 0)
			{
				SET_SS;
				while ((xop & 0x40000000) != 0) res++, xop <<= 1;
			}
			else
			{
				CLR_SS;
				xop |= 0x8000;
				while ((xop & 0x40000000) == 0) res++, xop <<= 1;
			}
			adsp2100.core.se.s = -res;
			break;
		case 0x0d<<11:
			/* EXP (HIX) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (GET_V)
			{
				adsp2100.core.se.s = 1;
				if (xop < 0) CLR_SS;
				else SET_SS;
			}
			else
			{
				res = 0;
				if (xop < 0)
				{
					SET_SS;
					while ((xop & 0x40000000) != 0) res++, xop <<= 1;
				}
				else
				{
					CLR_SS;
					xop |= 0x8000;
					while ((xop & 0x40000000) == 0) res++, xop <<= 1;
				}
				adsp2100.core.se.s = -res;
			}
			break;
		case 0x0e<<11:
			/* EXP (LO) */
			if (adsp2100.core.se.s == -15)
			{
				xop = SHIFT_GETXREG_SIGNED(xop);
				res = 15;
				if (GET_SS)
					while ((xop & 0x8000) != 0) res++, xop <<= 1;
				else
				{
					xop = (xop << 1) | 1;
					while ((xop & 0x10000) == 0) res++, xop <<= 1;
				}
				adsp2100.core.se.s = -res;
			}
			break;
		case 0x0f<<11:
			/* EXPADJ */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			res = 0;
			if (xop < 0)
				while ((xop & 0x40000000) != 0) res++, xop <<= 1;
			else
			{
				xop |= 0x8000;
				while ((xop & 0x40000000) == 0) res++, xop <<= 1;
			}
			if (res < -adsp2100.core.sb.s)
				adsp2100.core.sb.s = -res;
			break;
	}
}



/*===========================================================================
	Immediate SHIFT operations (result in SR/SE/SB)
===========================================================================*/

void shift_op_imm(int op)
{
	INT8 sc = (INT8)op;
	INT32 xop = (op >> 8) & 7;
	UINT32 res;

/*	switch ((op >> 11) & 15)*/
	switch (op & (15<<11))	/*JB*/
	{
		case 0x00<<11:
			/* LSHIFT (HI) */
			xop = SHIFT_GETXREG_UNSIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? ((UINT32)xop >> -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x01<<11:
			/* LSHIFT (HI, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? ((UINT32)xop >> -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x02<<11:
			/* LSHIFT (LO) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x03<<11:
			/* LSHIFT (LO, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x04<<11:
			/* ASHIFT (HI) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr = res;
			break;
		case 0x05<<11:
			/* ASHIFT (HI, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr |= res;
			break;
		case 0x06<<11:
			/* ASHIFT (LO) */
			xop = SHIFT_GETXREG_SIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr = res;
			break;
		case 0x07<<11:
			/* ASHIFT (LO, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop << sc) : 0;
			else res = (sc > -32) ? (xop >> -sc) : (xop >> 31);
			adsp2100.core.sr.sr |= res;
			break;
		case 0x08<<11:
			/* NORM (HI) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0)
			{
				xop = ((UINT32)xop >> 1) | ((adsp2100.astat & CFLAG) << 28);
				res = xop >> (sc - 1);
			}
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x09<<11:
			/* NORM (HI, OR) */
			xop = SHIFT_GETXREG_SIGNED(xop) << 16;
			if (sc > 0)
			{
				xop = ((UINT32)xop >> 1) | ((adsp2100.astat & CFLAG) << 28);
				res = xop >> (sc - 1);
			}
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
		case 0x0a<<11:
			/* NORM (LO) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop >> sc) : 0;
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr = res;
			break;
		case 0x0b<<11:
			/* NORM (LO, OR) */
			xop = SHIFT_GETXREG_UNSIGNED(xop);
			if (sc > 0) res = (sc < 32) ? (xop >> sc) : 0;
			else res = (sc > -32) ? (xop << -sc) : 0;
			adsp2100.core.sr.sr |= res;
			break;
	}
}




/*###################################################################################################
**	IRQ HANDLING
**#################################################################################################*/

static INLINE int adsp2100_generate_irq(int which)
{
	/* skip if masked */
	if (!(adsp2100.imask & (1 << which)))
		return 0;

	/* clear the latch */
	adsp2100.irq_latch[which] = 0;

	/* push the PC and the status */
	pc_stack_push();
	stat_stack_push();

	/* vector to location & stop idling */
	adsp2100.pc = which;
	adsp2100.idle = 0;

	/* mask other interrupts based on the nesting bit */
	if (adsp2100.icntl & 0x10) adsp2100.imask &= ~((2 << which) - 1);
	else adsp2100.imask &= ~0xf;

	return 1;
}


static INLINE int adsp2101_generate_irq(int which, int indx)
{
	/* skip if masked */
	if (!(adsp2100.imask & (0x20 >> indx)))
		return 0;

	/* clear the latch */
	adsp2100.irq_latch[which] = 0;

	/* push the PC and the status */
	pc_stack_push();
	stat_stack_push();

	/* vector to location & stop idling */
	adsp2100.pc = 0x04 + indx * 4;
	adsp2100.idle = 0;

	/* mask other interrupts based on the nesting bit */
	if (adsp2100.icntl & 0x10) adsp2100.imask &= ~(0x3f >> indx);
	else adsp2100.imask &= ~0x3f;

	return 1;
}


static void check_irqs(void)
{
	UINT8 check;

	if (chip_type >= CHIP_TYPE_ADSP2101)
	{
		/* check IRQ2 */
		check = (adsp2100.icntl & 4) ? adsp2100.irq_latch[ADSP2101_IRQ2] : adsp2100.irq_state[ADSP2101_IRQ2];
		if (check && adsp2101_generate_irq(ADSP2101_IRQ2, 0))
			return;

		/* check SPORT0 transmit */
		check = adsp2100.irq_latch[ADSP2101_SPORT0_TX];
		if (check && adsp2101_generate_irq(ADSP2101_SPORT0_TX, 1))
			return;

		/* check SPORT0 receive */
		check = adsp2100.irq_latch[ADSP2101_SPORT0_RX];
		if (check && adsp2101_generate_irq(ADSP2101_SPORT0_RX, 2))
			return;

		/* check IRQ1/SPORT1 transmit */
		check = (adsp2100.icntl & 2) ? adsp2100.irq_latch[ADSP2101_IRQ1] : adsp2100.irq_state[ADSP2101_IRQ1];
		if (check && adsp2101_generate_irq(ADSP2101_IRQ1, 3))
			return;

		/* check IRQ0/SPORT1 receive */
		check = (adsp2100.icntl & 1) ? adsp2100.irq_latch[ADSP2101_IRQ0] : adsp2100.irq_state[ADSP2101_IRQ0];
		if (check && adsp2101_generate_irq(ADSP2101_IRQ0, 4))
			return;
	}
	else
	{
		/* check IRQ3 */
		check = (adsp2100.icntl & 8) ? adsp2100.irq_latch[ADSP2100_IRQ3] : adsp2100.irq_state[ADSP2100_IRQ3];
		if (check && adsp2100_generate_irq(ADSP2100_IRQ3))
			return;

		/* check IRQ2 */
		check = (adsp2100.icntl & 4) ? adsp2100.irq_latch[ADSP2100_IRQ2] : adsp2100.irq_state[ADSP2100_IRQ2];
		if (check && adsp2100_generate_irq(ADSP2100_IRQ2))
			return;

		/* check IRQ1 */
		check = (adsp2100.icntl & 2) ? adsp2100.irq_latch[ADSP2100_IRQ1] : adsp2100.irq_state[ADSP2100_IRQ1];
		if (check && adsp2100_generate_irq(ADSP2100_IRQ1))
			return;

		/* check IRQ0 */
		check = (adsp2100.icntl & 1) ? adsp2100.irq_latch[ADSP2100_IRQ0] : adsp2100.irq_state[ADSP2100_IRQ0];
		if (check && adsp2100_generate_irq(ADSP2100_IRQ0))
			return;
	}
}


void adsp2100_set_irq_line(int irqline, int state)
{
	if (irqline < 5)
	{
		/* update the latched state */
		if (state != CLEAR_LINE && adsp2100.irq_state[irqline] == CLEAR_LINE)
	    	adsp2100.irq_latch[irqline] = 1;

	    /* update the absolute state */
	    adsp2100.irq_state[irqline] = state;

		/* check for IRQs */
	    if (state != CLEAR_LINE)
	    	check_irqs();
	}
}


void adsp2100_set_irq_callback(int (*callback)(int irqline))
{
	adsp2100.irq_callback = callback;
}



/*###################################################################################################
**	CONTEXT SWITCHING
**#################################################################################################*/

unsigned adsp2100_get_context(void *dst)
{
	/* copy the context */
	if (dst)
		*(adsp2100_Regs *)dst = adsp2100;

	/* return the context size */
	return sizeof(adsp2100_Regs);
}


void adsp2100_set_context(void *src)
{
	/* copy the context */
	if (src)
		adsp2100 = *(adsp2100_Regs *)src;

	/* reset the chip type */
	set_core_2100();

	/* check for IRQs */
	check_irqs();
}



/*###################################################################################################
**	INITIALIZATION AND SHUTDOWN
**#################################################################################################*/

void adsp2100_init(void)
{
	/* create the tables */
	if (!create_tables())
		exit(-1);
}

void adsp2100_reset(void *param)
{
	/* ensure that zero is zero */
	adsp2100.core.zero.u = adsp2100.alt.zero.u = 0;

	/* recompute the memory registers with their current values */
	wr_l0(adsp2100.l[0]);  wr_i0(adsp2100.i[0]);
	wr_l1(adsp2100.l[1]);  wr_i1(adsp2100.i[1]);
	wr_l2(adsp2100.l[2]);  wr_i2(adsp2100.i[2]);
	wr_l3(adsp2100.l[3]);  wr_i3(adsp2100.i[3]);
	wr_l4(adsp2100.l[4]);  wr_i4(adsp2100.i[4]);
	wr_l5(adsp2100.l[5]);  wr_i5(adsp2100.i[5]);
	wr_l6(adsp2100.l[6]);  wr_i6(adsp2100.i[6]);
	wr_l7(adsp2100.l[7]);  wr_i7(adsp2100.i[7]);

	/* reset PC and loops */
	switch (chip_type)
	{
		case CHIP_TYPE_ADSP2100:
			adsp2100.pc = 4;
			break;

		case CHIP_TYPE_ADSP2101:
		case CHIP_TYPE_ADSP2104:
		case CHIP_TYPE_ADSP2105:
		case CHIP_TYPE_ADSP2115:
			adsp2100.pc = 0;
			break;

		default:
			/*logerror( "ADSP2100 core: Unknown chip type!. Defaulting to ADSP2100.\n" );*/
			adsp2100.pc = 4;
			chip_type = CHIP_TYPE_ADSP2100;
			break;
	}

	adsp2100.ppc = -1;
	adsp2100.loop = 0xffff;
	adsp2100.loop_condition = 0;

	/* reset status registers */
	adsp2100.astat_clear = ~(CFLAG | VFLAG | NFLAG | ZFLAG);
	adsp2100.mstat = 0;
	adsp2100.sstat = 0x55;
	adsp2100.idle = 0;

	/* reset stacks */
	adsp2100.pc_sp = 0;
	adsp2100.cntr_sp = 0;
	adsp2100.stat_sp = 0;
	adsp2100.loop_sp = 0;

	/* reset external I/O */
	adsp2100.flagout = 0;
	adsp2100.flagin = 0;
	adsp2100.fl0 = 0;
	adsp2100.fl1 = 0;
	adsp2100.fl2 = 0;

	/* reset interrupts */
	adsp2100.imask = 0;
	adsp2100.irq_state[0] = CLEAR_LINE;
	adsp2100.irq_state[1] = CLEAR_LINE;
	adsp2100.irq_state[2] = CLEAR_LINE;
	adsp2100.irq_state[3] = CLEAR_LINE;
	adsp2100.irq_latch[0] = CLEAR_LINE;
	adsp2100.irq_latch[1] = CLEAR_LINE;
	adsp2100.irq_latch[2] = CLEAR_LINE;
	adsp2100.irq_latch[3] = CLEAR_LINE;
	adsp2100.interrupt_cycles = 0;
}


static int create_tables(void)
{
	int i;

	/* allocate the tables */
	if (!reverse_table)
		reverse_table = (UINT16 *)malloc(0x4000 * sizeof(UINT16));
	if (!mask_table)
		mask_table = (UINT16 *)malloc(0x4000 * sizeof(UINT16));
	if (!condition_table)
		condition_table = (UINT8 *)malloc(0x1000 * sizeof(UINT8));

	/* handle errors */
	if (!reverse_table || !mask_table || !condition_table)
		return 0;

	/* initialize the bit reversing table */
	for (i = 0; i < 0x4000; i++)
	{
		UINT16 data = 0;

		data |= (i >> 13) & 0x0001;
		data |= (i >> 11) & 0x0002;
		data |= (i >> 9)  & 0x0004;
		data |= (i >> 7)  & 0x0008;
		data |= (i >> 5)  & 0x0010;
		data |= (i >> 3)  & 0x0020;
		data |= (i >> 1)  & 0x0040;
		data |= (i << 1)  & 0x0080;
		data |= (i << 3)  & 0x0100;
		data |= (i << 5)  & 0x0200;
		data |= (i << 7)  & 0x0400;
		data |= (i << 9)  & 0x0800;
		data |= (i << 11) & 0x1000;
		data |= (i << 13) & 0x2000;

		reverse_table[i] = data;
	}

	/* initialize the mask table */
	for (i = 0; i < 0x4000; i++)
	{
		     if (i > 0x2000) mask_table[i] = 0x0000;
		else if (i > 0x1000) mask_table[i] = 0x2000;
		else if (i > 0x0800) mask_table[i] = 0x3000;
		else if (i > 0x0400) mask_table[i] = 0x3800;
		else if (i > 0x0200) mask_table[i] = 0x3c00;
		else if (i > 0x0100) mask_table[i] = 0x3e00;
		else if (i > 0x0080) mask_table[i] = 0x3f00;
		else if (i > 0x0040) mask_table[i] = 0x3f80;
		else if (i > 0x0020) mask_table[i] = 0x3fc0;
		else if (i > 0x0010) mask_table[i] = 0x3fe0;
		else if (i > 0x0008) mask_table[i] = 0x3ff0;
		else if (i > 0x0004) mask_table[i] = 0x3ff8;
		else if (i > 0x0002) mask_table[i] = 0x3ffc;
		else if (i > 0x0001) mask_table[i] = 0x3ffe;
		else                 mask_table[i] = 0x3fff;
	}

	/* initialize the condition table */
	for (i = 0; i < 0x100; i++)
	{
		int az = ((i & ZFLAG) != 0);
		int an = ((i & NFLAG) != 0);
		int av = ((i & VFLAG) != 0);
		int ac = ((i & CFLAG) != 0);
		int mv = ((i & MVFLAG) != 0);
		int as = ((i & SFLAG) != 0);

		condition_table[i | 0x000] = az;
		condition_table[i | 0x100] = !az;
		condition_table[i | 0x200] = !((an ^ av) | az);
		condition_table[i | 0x300] = (an ^ av) | az;
		condition_table[i | 0x400] = an ^ av;
		condition_table[i | 0x500] = !(an ^ av);
		condition_table[i | 0x600] = av;
		condition_table[i | 0x700] = !av;
		condition_table[i | 0x800] = ac;
		condition_table[i | 0x900] = !ac;
		condition_table[i | 0xa00] = as;
		condition_table[i | 0xb00] = !as;
		condition_table[i | 0xc00] = mv;
		condition_table[i | 0xd00] = !mv;
		condition_table[i | 0xf00] = 1;
	}
	return 1;
}


void adsp2100_exit(void)
{
	if (reverse_table)
		free(reverse_table);
	reverse_table = NULL;

	if (mask_table)
		free(mask_table);
	mask_table = NULL;

	if (condition_table)
		free(condition_table);
	condition_table = NULL;

#if TRACK_HOTSPOTS
	{
		FILE *log = fopen("adsp.hot", "w");
		while (1)
		{
			int maxindex = 0, i;
			for (i = 1; i < 0x4000; i++)
				if (pcbucket[i] > pcbucket[maxindex])
					maxindex = i;
			if (pcbucket[maxindex] == 0)
				break;
			fprintf(log, "PC=%04X  (%10d hits)\n", maxindex, pcbucket[maxindex]);
			pcbucket[maxindex] = 0;
		}
		fclose(log);
	}
#endif

}



/*###################################################################################################
**	CORE EXECUTION LOOP
**#################################################################################################*/

/* execute instructions on this CPU until icount expires */
int adsp2100_execute(int cycles)
{
	/* reset the core */
	set_mstat(adsp2100.mstat);

	/* count cycles and interrupt cycles */
	adsp2100_icount = cycles;
	adsp2100_icount -= adsp2100.interrupt_cycles;
	adsp2100.interrupt_cycles = 0;

	/* core execution loop */
	do
	{
		UINT32 op, temp;

		/* debugging */
		adsp2100.ppc = adsp2100.pc;	/* copy PC to previous PC */
		CALL_MAME_DEBUG;

#if TRACK_HOTSPOTS
		pcbucket[adsp2100.pc & 0x3fff]++;
#endif

		/* instruction fetch */
		op = ROPCODE();

		/* advance to the next instruction */
		if (adsp2100.pc != adsp2100.loop)
			adsp2100.pc++;

		/* handle looping */
		else
		{
			/* condition not met, keep looping */
			if (CONDITION(adsp2100.loop_condition))
				adsp2100.pc = pc_stack_top();

			/* condition met; pop the PC and loop stacks and fall through */
			else
			{
				loop_stack_pop();
				pc_stack_pop_val();
				adsp2100.pc++;
			}
		}

		/* parse the instruction */
		switch (op >> 16)
		{
			case 0x00:
				/* 00000000 00000000 00000000  NOP */
				break;
			case 0x02:
				/* 00000010 0000xxxx xxxxxxxx  modify flag out */
				/* 00000010 10000000 00000000  idle */
				/* 00000010 10000000 0000xxxx  idle (n) */
				if (op & 0x008000)
				{
					adsp2100.idle = 1;
					adsp2100_icount = 0;
				}
				else
				{
					if (CONDITION(op & 15))
					{
						switch ((op >> 4) & 3)
						{
							case 1:	adsp2100.flagout = !adsp2100.flagout;
							case 2: adsp2100.flagout = 0;
							case 3: adsp2100.flagout = 1;
						}
						if (chip_type >= CHIP_TYPE_ADSP2101)
						{
							switch ((op >> 6) & 3)
							{
								case 1:	adsp2100.fl0 = !adsp2100.fl0;
								case 2: adsp2100.fl0 = 0;
								case 3: adsp2100.fl0 = 1;
							}
							switch ((op >> 8) & 3)
							{
								case 1:	adsp2100.fl1 = !adsp2100.fl1;
								case 2: adsp2100.fl1 = 0;
								case 3: adsp2100.fl1 = 1;
							}
							switch ((op >> 10) & 3)
							{
								case 1:	adsp2100.fl2 = !adsp2100.fl2;
								case 2: adsp2100.fl2 = 0;
								case 3: adsp2100.fl2 = 1;
							}
						}
					}
				}
				break;
			case 0x03:
				/* 00000011 xxxxxxxx xxxxxxxx  call or jump on flag in */
				if (op & 0x000002)
				{
					if (adsp2100.flagin)
					{
						if (op & 0x000001)
							pc_stack_push();
						adsp2100.pc = ((op >> 4) & 0x0fff) | ((op << 10) & 0x3000);
					}
				}
				else
				{
					if (!adsp2100.flagin)
					{
						if (op & 0x000001)
							pc_stack_push();
						adsp2100.pc = ((op >> 4) & 0x0fff) | ((op << 10) & 0x3000);
					}
				}
				break;
			case 0x04:
				/* 00000100 00000000 000xxxxx  stack control */
				if (op & 0x000010) pc_stack_pop_val();
				if (op & 0x000008) loop_stack_pop();
				if (op & 0x000004) cntr_stack_pop();
				if (op & 0x000002)
				{
					if (op & 0x000001) stat_stack_pop();
					else stat_stack_push();
				}
				break;
			case 0x05:
				/* 00000101 00000000 00000000  saturate MR */
				if (GET_MV)
				{
					if (adsp2100.core.mr.mrx.mr2.u & 0x80)
						adsp2100.core.mr.mrx.mr2.u = 0xffff, adsp2100.core.mr.mrx.mr1.u = 0x8000, adsp2100.core.mr.mrx.mr0.u = 0x0000;
					else
						adsp2100.core.mr.mrx.mr2.u = 0x0000, adsp2100.core.mr.mrx.mr1.u = 0x7fff, adsp2100.core.mr.mrx.mr0.u = 0xffff;
				}
				break;
			case 0x06:
				/* 00000110 000xxxxx 00000000  DIVS */
				{
					int xop = (op >> 8) & 7;
					int yop = (op >> 11) & 3;

					xop = ALU_GETXREG_UNSIGNED(xop);
					yop = ALU_GETYREG_UNSIGNED(yop);

					temp = xop ^ yop;
					adsp2100.astat = (adsp2100.astat & ~QFLAG) | ((temp >> 10) & QFLAG);
					adsp2100.core.af.u = (yop << 1) | (adsp2100.core.ay0.u >> 15);
					adsp2100.core.ay0.u = (adsp2100.core.ay0.u << 1) | (temp >> 15);
				}
				break;
			case 0x07:
				/* 00000111 00010xxx 00000000  DIVQ */
				{
					int xop = (op >> 8) & 7;
					int res;

					xop = ALU_GETXREG_UNSIGNED(xop);

					if (GET_Q)
						res = adsp2100.core.af.u + xop;
					else
						res = adsp2100.core.af.u - xop;

					temp = res ^ xop;
					adsp2100.astat = (adsp2100.astat & ~QFLAG) | ((temp >> 10) & QFLAG);
					adsp2100.core.af.u = (res << 1) | (adsp2100.core.ay0.u >> 15);
					adsp2100.core.ay0.u = (adsp2100.core.ay0.u << 1) | ((~temp >> 15) & 0x0001);
				}
				break;
			case 0x08:
				/* 00001000 00000000 0000xxxx  reserved */
				break;
			case 0x09:
				/* 00001001 00000000 000xxxxx  modify address register */
				temp = (op >> 2) & 4;
				modify_address(temp + ((op >> 2) & 3), temp + (op & 3));
				break;
			case 0x0a:
				/* 00001010 00000000 000xxxxx  conditional return */
				if (CONDITION(op & 15))
				{
					pc_stack_pop();

					/* RTI case */
					if (op & 0x000010)
						stat_stack_pop();
				}
				break;
			case 0x0b:
				/* 00001011 00000000 xxxxxxxx  conditional jump (indirect address) */
				if (CONDITION(op & 15))
				{
					if (op & 0x000010)
						pc_stack_push();
					adsp2100.pc = adsp2100.i[4 + ((op >> 6) & 3)] & 0x3fff;
				}
				break;
			case 0x0c:
				/* 00001100 xxxxxxxx xxxxxxxx  mode control */
				temp = adsp2100.mstat;
				if (chip_type >= CHIP_TYPE_ADSP2101)
				{
					if (op & 0x000008) temp = (temp & ~MSTAT_GOMODE) | ((op << 5) & MSTAT_GOMODE);
					if (op & 0x002000) temp = (temp & ~MSTAT_INTEGER) | ((op >> 8) & MSTAT_INTEGER);
					if (op & 0x008000) temp = (temp & ~MSTAT_TIMER) | ((op >> 9) & MSTAT_TIMER);
				}
				if (op & 0x000020) temp = (temp & ~MSTAT_BANK) | ((op >> 4) & MSTAT_BANK);
				if (op & 0x000080) temp = (temp & ~MSTAT_REVERSE) | ((op >> 5) & MSTAT_REVERSE);
				if (op & 0x000200) temp = (temp & ~MSTAT_STICKYV) | ((op >> 6) & MSTAT_STICKYV);
				if (op & 0x000800) temp = (temp & ~MSTAT_SATURATE) | ((op >> 7) & MSTAT_SATURATE);
				set_mstat(temp);
				break;
			case 0x0d:
				/* 00001101 0000xxxx xxxxxxxx  internal data move */
				WRITE_REG((op >> 10) & 3, (op >> 4) & 15, READ_REG((op >> 8) & 3, op & 15));
				break;
			case 0x0e:
				/* 00001110 0xxxxxxx xxxxxxxx  conditional shift */
				if (CONDITION(op & 15)) shift_op(op);
				break;
			case 0x0f:
				/* 00001111 0xxxxxxx xxxxxxxx  shift immediate */
				shift_op_imm(op);
				break;
			case 0x10:
				/* 00010000 0xxxxxxx xxxxxxxx  shift with internal data register move */
				shift_op(op);
				temp = READ_REG(0, op & 15);
				WRITE_REG(0, (op >> 4) & 15, temp);
				break;
			case 0x11:
				/* 00010001 xxxxxxxx xxxxxxxx  shift with pgm memory read/write */
				if (op & 0x8000)
				{
					pgm_write_dag2(op, READ_REG(0, (op >> 4) & 15));
					shift_op(op);
				}
				else
				{
					shift_op(op);
					WRITE_REG(0, (op >> 4) & 15, pgm_read_dag2(op));
				}
				break;
			case 0x12:
				/* 00010010 xxxxxxxx xxxxxxxx  shift with data memory read/write DAG1 */
				if (op & 0x8000)
				{
					data_write_dag1(op, READ_REG(0, (op >> 4) & 15));
					shift_op(op);
				}
				else
				{
					shift_op(op);
					WRITE_REG(0, (op >> 4) & 15, data_read_dag1(op));
				}
				break;
			case 0x13:
				/* 00010011 xxxxxxxx xxxxxxxx  shift with data memory read/write DAG2 */
				if (op & 0x8000)
				{
					data_write_dag2(op, READ_REG(0, (op >> 4) & 15));
					shift_op(op);
				}
				else
				{
					shift_op(op);
					WRITE_REG(0, (op >> 4) & 15, data_read_dag2(op));
				}
				break;
			case 0x14: case 0x15: case 0x16: case 0x17:
				/* 000101xx xxxxxxxx xxxxxxxx  do until */
				loop_stack_push(op & 0x3ffff);
				pc_stack_push();
				break;
			case 0x18: case 0x19: case 0x1a: case 0x1b:
				/* 000110xx xxxxxxxx xxxxxxxx  conditional jump (immediate addr) */
				if (CONDITION(op & 15)) adsp2100.pc = (op >> 4) & 0x3fff;
				/* check for a busy loop */
				if ( adsp2100.pc == adsp2100.ppc )
					adsp2100_icount = 0;
				break;
			case 0x1c: case 0x1d: case 0x1e: case 0x1f:
				/* 000111xx xxxxxxxx xxxxxxxx  conditional call (immediate addr) */
				if (CONDITION(op & 15))
				{
					pc_stack_push();
					adsp2100.pc = (op >> 4) & 0x3fff;
				}
				break;
			case 0x20: case 0x21:
				/* 0010000x xxxxxxxx xxxxxxxx  conditional MAC to MR */
				if (CONDITION(op & 15)) mac_op_mr(op);
				break;
			case 0x22: case 0x23:
				/* 0010001x xxxxxxxx xxxxxxxx  conditional ALU to AR */
				if (CONDITION(op & 15)) alu_op_ar(op);
				break;
			case 0x24: case 0x25:
				/* 0010010x xxxxxxxx xxxxxxxx  conditional MAC to MF */
				if (CONDITION(op & 15)) mac_op_mf(op);
				break;
			case 0x26: case 0x27:
				/* 0010011x xxxxxxxx xxxxxxxx  conditional ALU to AF */
				if (CONDITION(op & 15)) alu_op_af(op);
				break;
			case 0x28: case 0x29:
				/* 0010100x xxxxxxxx xxxxxxxx  MAC to MR with internal data register move */
				temp = READ_REG(0, op & 15);
				mac_op_mr(op);
				WRITE_REG(0, (op >> 4) & 15, temp);
				break;
			case 0x2a: case 0x2b:
				/* 0010101x xxxxxxxx xxxxxxxx  ALU to AR with internal data register move */
				temp = READ_REG(0, op & 15);
				alu_op_ar(op);
				WRITE_REG(0, (op >> 4) & 15, temp);
				break;
			case 0x2c: case 0x2d:
				/* 0010110x xxxxxxxx xxxxxxxx  MAC to MF with internal data register move */
				temp = READ_REG(0, op & 15);
				mac_op_mf(op);
				WRITE_REG(0, (op >> 4) & 15, temp);
				break;
			case 0x2e: case 0x2f:
				/* 0010111x xxxxxxxx xxxxxxxx  ALU to AF with internal data register move */
				temp = READ_REG(0, op & 15);
				alu_op_af(op);
				WRITE_REG(0, (op >> 4) & 15, temp);
				break;
			case 0x30: case 0x31: case 0x32: case 0x33:
				/* 001100xx xxxxxxxx xxxxxxxx  load non-data register immediate (group 0) */
				WRITE_REG(0, op & 15, (INT32)(op << 14) >> 18);
				break;
			case 0x34: case 0x35: case 0x36: case 0x37:
				/* 001101xx xxxxxxxx xxxxxxxx  load non-data register immediate (group 1) */
				WRITE_REG(1, op & 15, (INT32)(op << 14) >> 18);
				break;
			case 0x38: case 0x39: case 0x3a: case 0x3b:
				/* 001110xx xxxxxxxx xxxxxxxx  load non-data register immediate (group 2) */
				WRITE_REG(2, op & 15, (INT32)(op << 14) >> 18);
				break;
			case 0x3c: case 0x3d: case 0x3e: case 0x3f:
				/* 001111xx xxxxxxxx xxxxxxxx  load non-data register immediate (group 3) */
				WRITE_REG(3, op & 15, (INT32)(op << 14) >> 18);
				break;
			case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
			case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
				/* 0100xxxx xxxxxxxx xxxxxxxx  load data register immediate */
				WRITE_REG(0, op & 15, (op >> 4) & 0xffff);
				break;
			case 0x50: case 0x51:
				/* 0101000x xxxxxxxx xxxxxxxx  MAC to MR with pgm memory read */
				mac_op_mr(op);
				WRITE_REG(0, (op >> 4) & 15, pgm_read_dag2(op));
				break;
			case 0x52: case 0x53:
				/* 0101001x xxxxxxxx xxxxxxxx  ALU to AR with pgm memory read */
				alu_op_ar(op);
				WRITE_REG(0, (op >> 4) & 15, pgm_read_dag2(op));
				break;
			case 0x54: case 0x55:
				/* 0101010x xxxxxxxx xxxxxxxx  MAC to MF with pgm memory read */
				mac_op_mf(op);
				WRITE_REG(0, (op >> 4) & 15, pgm_read_dag2(op));
				break;
			case 0x56: case 0x57:
				/* 0101011x xxxxxxxx xxxxxxxx  ALU to AF with pgm memory read */
				alu_op_af(op);
				WRITE_REG(0, (op >> 4) & 15, pgm_read_dag2(op));
				break;
			case 0x58: case 0x59:
				/* 0101100x xxxxxxxx xxxxxxxx  MAC to MR with pgm memory write */
				pgm_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mr(op);
				break;
			case 0x5a: case 0x5b:
				/* 0101101x xxxxxxxx xxxxxxxx  ALU to AR with pgm memory write */
				pgm_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				alu_op_ar(op);
				break;
			case 0x5c: case 0x5d:
				/* 0101110x xxxxxxxx xxxxxxxx  ALU to MR with pgm memory write */
				pgm_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mf(op);
				break;
			case 0x5e: case 0x5f:
				/* 0101111x xxxxxxxx xxxxxxxx  ALU to MF with pgm memory write */
				pgm_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				alu_op_af(op);
				break;
			case 0x60: case 0x61:
				/* 0110000x xxxxxxxx xxxxxxxx  MAC to MR with data memory read DAG1 */
				mac_op_mr(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag1(op));
				break;
			case 0x62: case 0x63:
				/* 0110001x xxxxxxxx xxxxxxxx  ALU to AR with data memory read DAG1 */
				alu_op_ar(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag1(op));
				break;
			case 0x64: case 0x65:
				/* 0110010x xxxxxxxx xxxxxxxx  MAC to MF with data memory read DAG1 */
				mac_op_mf(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag1(op));
				break;
			case 0x66: case 0x67:
				/* 0110011x xxxxxxxx xxxxxxxx  ALU to AF with data memory read DAG1 */
				alu_op_af(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag1(op));
				break;
			case 0x68: case 0x69:
				/* 0110100x xxxxxxxx xxxxxxxx  MAC to MR with data memory write DAG1 */
				data_write_dag1(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mr(op);
				break;
			case 0x6a: case 0x6b:
				/* 0110101x xxxxxxxx xxxxxxxx  ALU to AR with data memory write DAG1 */
				data_write_dag1(op, READ_REG(0, (op >> 4) & 15));
				alu_op_ar(op);
				break;
			case 0x6c: case 0x6d:
				/* 0111110x xxxxxxxx xxxxxxxx  MAC to MF with data memory write DAG1 */
				data_write_dag1(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mf(op);
				break;
			case 0x6e: case 0x6f:
				/* 0111111x xxxxxxxx xxxxxxxx  ALU to AF with data memory write DAG1 */
				data_write_dag1(op, READ_REG(0, (op >> 4) & 15));
				alu_op_af(op);
				break;
			case 0x70: case 0x71:
				/* 0111000x xxxxxxxx xxxxxxxx  MAC to MR with data memory read DAG2 */
				mac_op_mr(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag2(op));
				break;
			case 0x72: case 0x73:
				/* 0111001x xxxxxxxx xxxxxxxx  ALU to AR with data memory read DAG2 */
				alu_op_ar(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag2(op));
				break;
			case 0x74: case 0x75:
				/* 0111010x xxxxxxxx xxxxxxxx  MAC to MF with data memory read DAG2 */
				mac_op_mf(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag2(op));
				break;
			case 0x76: case 0x77:
				/* 0111011x xxxxxxxx xxxxxxxx  ALU to AF with data memory read DAG2 */
				alu_op_af(op);
				WRITE_REG(0, (op >> 4) & 15, data_read_dag2(op));
				break;
			case 0x78: case 0x79:
				/* 0111100x xxxxxxxx xxxxxxxx  MAC to MR with data memory write DAG2 */
				data_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mr(op);
				break;
			case 0x7a: case 0x7b:
				/* 0111101x xxxxxxxx xxxxxxxx  ALU to AR with data memory write DAG2 */
				data_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				alu_op_ar(op);
				break;
			case 0x7c: case 0x7d:
				/* 0111110x xxxxxxxx xxxxxxxx  MAC to MF with data memory write DAG2 */
				data_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				mac_op_mf(op);
				break;
			case 0x7e: case 0x7f:
				/* 0111111x xxxxxxxx xxxxxxxx  ALU to AF with data memory write DAG2 */
				data_write_dag2(op, READ_REG(0, (op >> 4) & 15));
				alu_op_af(op);
				break;
			case 0x80: case 0x81: case 0x82: case 0x83:
				/* 100000xx xxxxxxxx xxxxxxxx  read data memory (immediate addr) to reg group 0 */
				WRITE_REG(0, op & 15, RWORD_DATA((op >> 4) & 0x3fff));
				break;
			case 0x84: case 0x85: case 0x86: case 0x87:
				/* 100001xx xxxxxxxx xxxxxxxx  read data memory (immediate addr) to reg group 1 */
				WRITE_REG(1, op & 15, RWORD_DATA((op >> 4) & 0x3fff));
				break;
			case 0x88: case 0x89: case 0x8a: case 0x8b:
				/* 100010xx xxxxxxxx xxxxxxxx  read data memory (immediate addr) to reg group 2 */
				WRITE_REG(2, op & 15, RWORD_DATA((op >> 4) & 0x3fff));
				break;
			case 0x8c: case 0x8d: case 0x8e: case 0x8f:
				/* 100011xx xxxxxxxx xxxxxxxx  read data memory (immediate addr) to reg group 3 */
				WRITE_REG(3, op & 15, RWORD_DATA((op >> 4) & 0x3fff));
				break;
			case 0x90: case 0x91: case 0x92: case 0x93:
				/* 1001xxxx xxxxxxxx xxxxxxxx  write data memory (immediate addr) from reg group 0 */
				WWORD_DATA((op >> 4) & 0x3fff, READ_REG(0, op & 15));
				break;
			case 0x94: case 0x95: case 0x96: case 0x97:
				/* 1001xxxx xxxxxxxx xxxxxxxx  write data memory (immediate addr) from reg group 1 */
				WWORD_DATA((op >> 4) & 0x3fff, READ_REG(1, op & 15));
				break;
			case 0x98: case 0x99: case 0x9a: case 0x9b:
				/* 1001xxxx xxxxxxxx xxxxxxxx  write data memory (immediate addr) from reg group 2 */
				WWORD_DATA((op >> 4) & 0x3fff, READ_REG(2, op & 15));
				break;
			case 0x9c: case 0x9d: case 0x9e: case 0x9f:
				/* 1001xxxx xxxxxxxx xxxxxxxx  write data memory (immediate addr) from reg group 3 */
				WWORD_DATA((op >> 4) & 0x3fff, READ_REG(3, op & 15));
				break;
			case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7:
			case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
				/* 1010xxxx xxxxxxxx xxxxxxxx  data memory write (immediate) DAG1 */
				data_write_dag1(op, (op >> 4) & 0xffff);
				break;
			case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
			case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
				/* 1011xxxx xxxxxxxx xxxxxxxx  data memory write (immediate) DAG2 */
				data_write_dag2(op, (op >> 4) & 0xffff);
				break;
			case 0xc0: case 0xc1:
				/* 1100000x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX0 & pgm read to AY0 */
				mac_op_mr(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xc2: case 0xc3:
				/* 1100001x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX0 & pgm read to AY0 */
				alu_op_ar(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xc4: case 0xc5:
				/* 1100010x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX1 & pgm read to AY0 */
				mac_op_mr(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xc6: case 0xc7:
				/* 1100011x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX1 & pgm read to AY0 */
				alu_op_ar(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xc8: case 0xc9:
				/* 1100100x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX0 & pgm read to AY0 */
				mac_op_mr(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xca: case 0xcb:
				/* 1100101x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX0 & pgm read to AY0 */
				alu_op_ar(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xcc: case 0xcd:
				/* 1100110x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX1 & pgm read to AY0 */
				mac_op_mr(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xce: case 0xcf:
				/* 1100111x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX1 & pgm read to AY0 */
				alu_op_ar(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.ay0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xd0: case 0xd1:
				/* 1101000x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX0 & pgm read to AY1 */
				mac_op_mr(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xd2: case 0xd3:
				/* 1101001x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX0 & pgm read to AY1 */
				alu_op_ar(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xd4: case 0xd5:
				/* 1101010x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX1 & pgm read to AY1 */
				mac_op_mr(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xd6: case 0xd7:
				/* 1101011x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX1 & pgm read to AY1 */
				alu_op_ar(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xd8: case 0xd9:
				/* 1101100x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX0 & pgm read to AY1 */
				mac_op_mr(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xda: case 0xdb:
				/* 1101101x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX0 & pgm read to AY1 */
				alu_op_ar(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xdc: case 0xdd:
				/* 1101110x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX1 & pgm read to AY1 */
				mac_op_mr(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xde: case 0xdf:
				/* 1101111x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX1 & pgm read to AY1 */
				alu_op_ar(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.ay1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xe0: case 0xe1:
				/* 1110000x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX0 & pgm read to MY0 */
				mac_op_mr(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xe2: case 0xe3:
				/* 1110001x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX0 & pgm read to MY0 */
				alu_op_ar(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xe4: case 0xe5:
				/* 1110010x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX1 & pgm read to MY0 */
				mac_op_mr(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xe6: case 0xe7:
				/* 1110011x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX1 & pgm read to MY0 */
				alu_op_ar(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xe8: case 0xe9:
				/* 1110100x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX0 & pgm read to MY0 */
				mac_op_mr(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xea: case 0xeb:
				/* 1110101x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX0 & pgm read to MY0 */
				alu_op_ar(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xec: case 0xed:
				/* 1110110x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX1 & pgm read to MY0 */
				mac_op_mr(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xee: case 0xef:
				/* 1110111x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX1 & pgm read to MY0 */
				alu_op_ar(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.my0.u = pgm_read_dag2(op >> 4);
				break;
			case 0xf0: case 0xf1:
				/* 1111000x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX0 & pgm read to MY1 */
				mac_op_mr(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xf2: case 0xf3:
				/* 1111001x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX0 & pgm read to MY1 */
				alu_op_ar(op);
				adsp2100.core.ax0.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xf4: case 0xf5:
				/* 1111010x xxxxxxxx xxxxxxxx  MAC to MR with data read to AX1 & pgm read to MY1 */
				mac_op_mr(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xf6: case 0xf7:
				/* 1111011x xxxxxxxx xxxxxxxx  ALU to AR with data read to AX1 & pgm read to MY1 */
				alu_op_ar(op);
				adsp2100.core.ax1.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xf8: case 0xf9:
				/* 1111100x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX0 & pgm read to MY1 */
				mac_op_mr(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xfa: case 0xfb:
				/* 1111101x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX0 & pgm read to MY1 */
				alu_op_ar(op);
				adsp2100.core.mx0.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xfc: case 0xfd:
				/* 1111110x xxxxxxxx xxxxxxxx  MAC to MR with data read to MX1 & pgm read to MY1 */
				mac_op_mr(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
			case 0xfe: case 0xff:
				/* 1111111x xxxxxxxx xxxxxxxx  ALU to AR with data read to MX1 & pgm read to MY1 */
				alu_op_ar(op);
				adsp2100.core.mx1.u = data_read_dag1(op);
				adsp2100.core.my1.u = pgm_read_dag2(op >> 4);
				break;
		}

		adsp2100_icount--;

	} while (adsp2100_icount > 0);

	adsp2100_icount -= adsp2100.interrupt_cycles;
	adsp2100.interrupt_cycles = 0;

	return cycles - adsp2100_icount;
}



/*###################################################################################################
**	REGISTER SNOOP
**#################################################################################################*/

unsigned adsp2100_get_reg(int regnum)
{
	switch (regnum)
	{
		case REG_PC:
		case ADSP2100_PC: return adsp2100.pc;

		case ADSP2100_AX0: return adsp2100.core.ax0.u;
		case ADSP2100_AX1: return adsp2100.core.ax1.u;
		case ADSP2100_AY0: return adsp2100.core.ay0.u;
		case ADSP2100_AY1: return adsp2100.core.ay1.u;
		case ADSP2100_AR: return adsp2100.core.ar.u;
		case ADSP2100_AF: return adsp2100.core.af.u;

		case ADSP2100_MX0: return adsp2100.core.mx0.u;
		case ADSP2100_MX1: return adsp2100.core.mx1.u;
		case ADSP2100_MY0: return adsp2100.core.my0.u;
		case ADSP2100_MY1: return adsp2100.core.my1.u;
		case ADSP2100_MR0: return adsp2100.core.mr.mrx.mr0.u;
		case ADSP2100_MR1: return adsp2100.core.mr.mrx.mr1.u;
		case ADSP2100_MR2: return adsp2100.core.mr.mrx.mr2.u;
		case ADSP2100_MF: return adsp2100.core.mf.u;

		case ADSP2100_SI: return adsp2100.core.si.u;
		case ADSP2100_SE: return adsp2100.core.se.u;
		case ADSP2100_SB: return adsp2100.core.sb.u;
		case ADSP2100_SR0: return adsp2100.core.sr.srx.sr0.u;
		case ADSP2100_SR1: return adsp2100.core.sr.srx.sr1.u;

		case ADSP2100_AX0_SEC: return adsp2100.alt.ax0.u;
		case ADSP2100_AX1_SEC: return adsp2100.alt.ax1.u;
		case ADSP2100_AY0_SEC: return adsp2100.alt.ay0.u;
		case ADSP2100_AY1_SEC: return adsp2100.alt.ay1.u;
		case ADSP2100_AR_SEC: return adsp2100.alt.ar.u;
		case ADSP2100_AF_SEC: return adsp2100.alt.af.u;

		case ADSP2100_MX0_SEC: return adsp2100.alt.mx0.u;
		case ADSP2100_MX1_SEC: return adsp2100.alt.mx1.u;
		case ADSP2100_MY0_SEC: return adsp2100.alt.my0.u;
		case ADSP2100_MY1_SEC: return adsp2100.alt.my1.u;
		case ADSP2100_MR0_SEC: return adsp2100.alt.mr.mrx.mr0.u;
		case ADSP2100_MR1_SEC: return adsp2100.alt.mr.mrx.mr1.u;
		case ADSP2100_MR2_SEC: return adsp2100.alt.mr.mrx.mr2.u;
		case ADSP2100_MF_SEC: return adsp2100.alt.mf.u;

		case ADSP2100_SI_SEC: return adsp2100.alt.si.u;
		case ADSP2100_SE_SEC: return adsp2100.alt.se.u;
		case ADSP2100_SB_SEC: return adsp2100.alt.sb.u;
		case ADSP2100_SR0_SEC: return adsp2100.alt.sr.srx.sr0.u;
		case ADSP2100_SR1_SEC: return adsp2100.alt.sr.srx.sr1.u;

		case ADSP2100_I0: return adsp2100.i[0];
		case ADSP2100_I1: return adsp2100.i[1];
		case ADSP2100_I2: return adsp2100.i[2];
		case ADSP2100_I3: return adsp2100.i[3];
		case ADSP2100_I4: return adsp2100.i[4];
		case ADSP2100_I5: return adsp2100.i[5];
		case ADSP2100_I6: return adsp2100.i[6];
		case ADSP2100_I7: return adsp2100.i[7];

		case ADSP2100_L0: return adsp2100.l[0];
		case ADSP2100_L1: return adsp2100.l[1];
		case ADSP2100_L2: return adsp2100.l[2];
		case ADSP2100_L3: return adsp2100.l[3];
		case ADSP2100_L4: return adsp2100.l[4];
		case ADSP2100_L5: return adsp2100.l[5];
		case ADSP2100_L6: return adsp2100.l[6];
		case ADSP2100_L7: return adsp2100.l[7];

		case ADSP2100_M0: return adsp2100.m[0];
		case ADSP2100_M1: return adsp2100.m[1];
		case ADSP2100_M2: return adsp2100.m[2];
		case ADSP2100_M3: return adsp2100.m[3];
		case ADSP2100_M4: return adsp2100.m[4];
		case ADSP2100_M5: return adsp2100.m[5];
		case ADSP2100_M6: return adsp2100.m[6];
		case ADSP2100_M7: return adsp2100.m[7];

		case ADSP2100_PX: return adsp2100.px;
		case ADSP2100_CNTR: return adsp2100.cntr;
		case ADSP2100_ASTAT: return adsp2100.astat;
		case ADSP2100_SSTAT: return adsp2100.sstat;
		case ADSP2100_MSTAT: return adsp2100.mstat;

		case REG_SP:
		case ADSP2100_PCSP: return adsp2100.pc_sp;
		case ADSP2100_CNTRSP: return adsp2100.cntr_sp;
		case ADSP2100_STATSP: return adsp2100.stat_sp;
		case ADSP2100_LOOPSP: return adsp2100.loop_sp;

		case ADSP2100_IMASK: return adsp2100.imask;
		case ADSP2100_ICNTL: return adsp2100.icntl;
		case ADSP2100_IRQSTATE0: return adsp2100.irq_state[0];
		case ADSP2100_IRQSTATE1: return adsp2100.irq_state[1];
		case ADSP2100_IRQSTATE2: return adsp2100.irq_state[2];
		case ADSP2100_IRQSTATE3: return adsp2100.irq_state[3];

		case ADSP2100_FLAGIN: return adsp2100.flagin;
		case ADSP2100_FLAGOUT: return adsp2100.flagout;
		case ADSP2100_FL0: return adsp2100.fl0;
		case ADSP2100_FL1: return adsp2100.fl1;
		case ADSP2100_FL2: return adsp2100.fl2;
		case REG_PREVIOUSPC: return adsp2100.ppc;
		default:
			if (regnum <= REG_SP_CONTENTS)
			{
				unsigned offset = REG_SP_CONTENTS - regnum;
				if (offset < PC_STACK_DEPTH)
					return adsp2100.pc_stack[offset];
			}
	}
	return 0;
}



/*###################################################################################################
**	REGISTER MODIFY
**#################################################################################################*/

void adsp2100_set_reg(int regnum, unsigned val)
{
	switch (regnum)
	{
		case REG_PC:
		case ADSP2100_PC: adsp2100.pc = val; break;

		case ADSP2100_AX0: wr_ax0(val); break;
		case ADSP2100_AX1: wr_ax1(val); break;
		case ADSP2100_AY0: wr_ay0(val); break;
		case ADSP2100_AY1: wr_ay1(val); break;
		case ADSP2100_AR: wr_ar(val); break;
		case ADSP2100_AF: adsp2100.core.af.u = val; break;

		case ADSP2100_MX0: wr_mx0(val); break;
		case ADSP2100_MX1: wr_mx1(val); break;
		case ADSP2100_MY0: wr_my0(val); break;
		case ADSP2100_MY1: wr_my1(val); break;
		case ADSP2100_MR0: wr_mr0(val); break;
		case ADSP2100_MR1: wr_mr1(val); break;
		case ADSP2100_MR2: wr_mr2(val); break;
		case ADSP2100_MF: adsp2100.core.mf.u = val; break;

		case ADSP2100_SI: wr_si(val); break;
		case ADSP2100_SE: wr_se(val); break;
		case ADSP2100_SB: wr_sb(val); break;
		case ADSP2100_SR0: wr_sr0(val); break;
		case ADSP2100_SR1: wr_sr1(val); break;

		case ADSP2100_AX0_SEC: adsp2100.alt.ax0.s = val; break;
		case ADSP2100_AX1_SEC: adsp2100.alt.ax1.s = val; break;
		case ADSP2100_AY0_SEC: adsp2100.alt.ay0.s = val; break;
		case ADSP2100_AY1_SEC: adsp2100.alt.ay1.s = val; break;
		case ADSP2100_AR_SEC: adsp2100.alt.ar.s = val; break;
		case ADSP2100_AF_SEC: adsp2100.alt.af.u = val; break;

		case ADSP2100_MX0_SEC: adsp2100.alt.mx0.s = val; break;
		case ADSP2100_MX1_SEC: adsp2100.alt.mx1.s = val; break;
		case ADSP2100_MY0_SEC: adsp2100.alt.my0.s = val; break;
		case ADSP2100_MY1_SEC: adsp2100.alt.my1.s = val; break;
		case ADSP2100_MR0_SEC: adsp2100.alt.mr.mrx.mr0.s = val; break;
		case ADSP2100_MR1_SEC: adsp2100.alt.mr.mrx.mr1.s = val; adsp2100.alt.mr.mrx.mr2.s = (INT16)val >> 15; break;
		case ADSP2100_MR2_SEC: adsp2100.alt.mr.mrx.mr2.s = (INT8)val; break;
		case ADSP2100_MF_SEC: adsp2100.alt.mf.u = val; break;

		case ADSP2100_SI_SEC: adsp2100.alt.si.s = val; break;
		case ADSP2100_SE_SEC: adsp2100.alt.se.s = (INT8)val; break;
		case ADSP2100_SB_SEC: adsp2100.alt.sb.s = (INT32)(val << 27) >> 27; break;
		case ADSP2100_SR0_SEC: adsp2100.alt.sr.srx.sr0.s = val; break;
		case ADSP2100_SR1_SEC: adsp2100.alt.sr.srx.sr1.s = val; break;

		case ADSP2100_I0: wr_i0(val); break;
		case ADSP2100_I1: wr_i1(val); break;
		case ADSP2100_I2: wr_i2(val); break;
		case ADSP2100_I3: wr_i3(val); break;
		case ADSP2100_I4: wr_i4(val); break;
		case ADSP2100_I5: wr_i5(val); break;
		case ADSP2100_I6: wr_i6(val); break;
		case ADSP2100_I7: wr_i7(val); break;

		case ADSP2100_L0: wr_l0(val); break;
		case ADSP2100_L1: wr_l1(val); break;
		case ADSP2100_L2: wr_l2(val); break;
		case ADSP2100_L3: wr_l3(val); break;
		case ADSP2100_L4: wr_l4(val); break;
		case ADSP2100_L5: wr_l5(val); break;
		case ADSP2100_L6: wr_l6(val); break;
		case ADSP2100_L7: wr_l7(val); break;

		case ADSP2100_M0: wr_m0(val); break;
		case ADSP2100_M1: wr_m1(val); break;
		case ADSP2100_M2: wr_m2(val); break;
		case ADSP2100_M3: wr_m3(val); break;
		case ADSP2100_M4: wr_m4(val); break;
		case ADSP2100_M5: wr_m5(val); break;
		case ADSP2100_M6: wr_m6(val); break;
		case ADSP2100_M7: wr_m7(val); break;

		case ADSP2100_PX: wr_px(val); break;
		case ADSP2100_CNTR: adsp2100.cntr = val; break;
		case ADSP2100_ASTAT: wr_astat(val); break;
		case ADSP2100_SSTAT: wr_sstat(val); break;
		case ADSP2100_MSTAT: wr_mstat(val); break;

		case REG_SP:
		case ADSP2100_PCSP: adsp2100.pc_sp = val; break;
		case ADSP2100_CNTRSP: adsp2100.cntr_sp = val; break;
		case ADSP2100_STATSP: adsp2100.stat_sp = val; break;
		case ADSP2100_LOOPSP: adsp2100.loop_sp = val; break;

		case ADSP2100_IMASK: wr_imask(val); break;
		case ADSP2100_ICNTL: wr_icntl(val); break;
		case ADSP2100_IRQSTATE0: adsp2100.irq_state[0] = val; break;
		case ADSP2100_IRQSTATE1: adsp2100.irq_state[1] = val; break;
		case ADSP2100_IRQSTATE2: adsp2100.irq_state[2] = val; break;
		case ADSP2100_IRQSTATE3: adsp2100.irq_state[3] = val; break;

		case ADSP2100_FLAGIN: adsp2100.flagin = val; break;
		case ADSP2100_FLAGOUT: adsp2100.flagout = val; break;
		case ADSP2100_FL0: adsp2100.fl0 = val; break;
		case ADSP2100_FL1: adsp2100.fl1 = val; break;
		case ADSP2100_FL2: adsp2100.fl2 = val; break;
		default:
			if (regnum <= REG_SP_CONTENTS)
			{
				unsigned offset = REG_SP_CONTENTS - regnum;
				if (offset < PC_STACK_DEPTH)
					adsp2100.pc_stack[offset] = val;
			}
    }
}

void p2k_adsp2105_init(uint16_t (*data_read)(uint16_t),
                       void (*data_write)(uint16_t, uint16_t),
                       uint32_t (*program_read)(uint16_t),
                       void (*program_write)(uint16_t, uint32_t))
{
    s_data_read = data_read;
    s_data_write = data_write;
    s_program_read = program_read;
    s_program_write = program_write;
    memset(&adsp2100, 0, sizeof(adsp2100));
    adsp2100_init();
    p2k_adsp2105_reset();
}

void p2k_adsp2105_reset(void)
{
    set_core_2104();
    adsp2100_reset(NULL);
}

int p2k_adsp2105_execute(int cycles)
{
    return adsp2100_execute(cycles);
}

void p2k_adsp2105_set_irq_line(int irqline, int state)
{
    adsp2100_set_irq_line(irqline, state);
}

uint32_t p2k_adsp2105_get_reg(int regnum)
{
    return adsp2100_get_reg(regnum);
}

void p2k_adsp2105_set_reg(int regnum, uint32_t value)
{
    adsp2100_set_reg(regnum, value);
}

void p2k_adsp2105_set_tx_callback(P2KAdspTxCallback callback)
{
    sport_tx_callback = callback;
}

void p2k_adsp2105_load_boot_data(const uint8_t *source, uint32_t *program)
{
    int words = (source[3] + 1) * 8;
    for (int i = 0; i < words; i++) {
        program[i] = ((uint32_t)source[i * 4] << 16) |
                     ((uint32_t)source[i * 4 + 1] << 8) |
                     source[i * 4 + 2];
    }
}

/* ===== Encore DCS/ADSP integration ===== */

#define P2K_DCS_SOUND_FLASH_SIZE (1024 * 1024)
#define P2K_DCS_REGION_WORDS     0x600000u
#define P2K_DCS_U109_WORD_OFFSET 0x200000u
#define P2K_DCS_U110_WORD_OFFSET 0x400000u
#define ADSP_PCM_RATE            31250
#define ADSP_HEADLESS_FRAMES     256

enum {
    S1_AUTOBUF_REG = 15,
    S1_RFSDIV_REG,
    S1_SCLKDIV_REG,
    S1_CONTROL_REG,
    S0_AUTOBUF_REG,
    S0_RFSDIV_REG,
    S0_SCLKDIV_REG,
    S0_CONTROL_REG,
    S0_MCTXLO_REG,
    S0_MCTXHI_REG,
    S0_MCRXLO_REG,
    S0_MCRXHI_REG,
    TIMER_SCALE_REG,
    TIMER_COUNT_REG,
    TIMER_PERIOD_REG,
    WAITSTATES_REG,
    SYSCONTROL_REG,
};

typedef struct {
    uint16_t data[0x4000];
    uint32_t program[0x4000];
    const uint8_t *sound_rom;
    uint16_t *sound_data;
    size_t sound_words;
    uint16_t control[32];
    uint16_t rom_bank;
    uint16_t sdrc[4];
    uint8_t sdrc_seed;
    uint16_t sram[0x10000];
    uint16_t commands[65536];
    unsigned command_head;
    unsigned command_count;
    uint16_t output_data;
    uint16_t output_control;
    bool output_full;
    uint16_t host_ack[2];
    unsigned host_ack_head;
    unsigned host_ack_count;
    bool initialized;
    bool sport_enabled;
    int ireg;
    int increment;
    int length;
    int base;
    int play_pos;
    int next_irq_pos;
    double source_phase;
    double source_rate;
    double cycle_phase;
    uint64_t cycles;
    uint64_t pcm_frames;
    uint64_t pcm_nonzero;
    uint64_t commands_enqueued;
    uint64_t commands_consumed;
    uint64_t commands_dropped_on_reset;
    unsigned runtime_host_resets;
    unsigned host_boot_completions;
    unsigned pcm_peak;
    int16_t last_sample[2];
    bool selftest_ready;
    bool host_boot;
    unsigned host_boot_pos;
    unsigned host_boot_words;
    uint8_t host_boot_triplet[3];
    bool host_boot_compare_logged;
    uint8_t echo;
    uint16_t flag_latch;
    pthread_mutex_t lock;
    pthread_mutex_t core_lock;
    pthread_t pcm_thread;
    bool locks_ready;
    bool pcm_thread_started;
    bool pcm_thread_run;
    SDL_AudioDeviceID audio_dev;
    int audio_rate;
    char sound_flash_path[1024];
} EncoreAdsp;

static EncoreAdsp s_adsp;
static uint8_t *s_sound_flash;

static void adsp_render_direct(int16_t *samples, int frames, int output_rate,
                               bool mailbox_pending);

static void adsp_assert_mailbox_irq_locked(void)
{
    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 1);
}

static uint16_t adsp_data_read(uint16_t address)
{
    if (address == 0x0400) {
        pthread_mutex_lock(&s_adsp.lock);
        uint16_t value = s_adsp.command_count ? s_adsp.commands[s_adsp.command_head] : 0;
        pthread_mutex_unlock(&s_adsp.lock);
        return value;
    }
    if (address == 0x0402) return s_adsp.output_control;
    if (address == 0x0403) {
        pthread_mutex_lock(&s_adsp.lock);
        bool full = s_adsp.command_count != 0;
        bool output_full = s_adsp.output_full;
        pthread_mutex_unlock(&s_adsp.lock);
        return (full ? 0x80 : 0) | (output_full ? 0 : 0x40);
    }
    if (address >= 0x0480 && address <= 0x0483) {
        unsigned reg = address - 0x0480;
        if (reg != 3) return s_adsp.sdrc[reg];
        static const uint16_t security[8] = {
            0x5a81, 0x5aa4, 0x5a00, 0x5ab9,
            0x5a03, 0x5a69, 0x5a20, 0x5aff,
        };
        unsigned mode = (s_adsp.sdrc[0] >> 13) & 7;
        return mode == 2 ? 0x5a00 | ((s_adsp.sdrc_seed & 0x3f) << 1)
                         : security[mode];
    }

    unsigned rom_st = s_adsp.sdrc[0] & 3;
    bool rom_enabled = rom_st != 3;
    unsigned rom_base = rom_st == 0 ? 0x0000 : rom_st == 1 ? 0x3000 : 0x3400;
    unsigned page_words = (rom_st != 0 && !(s_adsp.sdrc[0] & 0x10)) ? 4096 : 1024;
    if (rom_enabled && address >= rom_base && address < rom_base + page_words) {
        if (s_adsp.sdrc[0] & 0x20) {
            size_t word = ((size_t)(s_adsp.sdrc[2] & 0x1fff) * page_words +
                           address - rom_base) % s_adsp.sound_words;
            return s_adsp.sound_data[word];
        }
        unsigned page = (s_adsp.sdrc[0] >> 7) & 7;
        size_t word = (size_t)page * page_words + address - rom_base;
        return encore_lduw_le(s_sound_flash + ((word * 2) & (P2K_DCS_SOUND_FLASH_SIZE - 1)));
    }

    unsigned dm_st = s_adsp.sdrc[1] & 3;
    unsigned dm_base = dm_st == 1 ? 0x0000 : dm_st == 2 ? 0x3000 : 0x3400;
    if (dm_st && address >= dm_base && address < dm_base + 0x400) {
        size_t word = ((size_t)(s_adsp.sdrc[2] & 0x7ff) * 1024 +
                       address - dm_base) % s_adsp.sound_words;
        return s_adsp.sound_data[word];
    }

    bool sm_enabled = (s_adsp.sdrc[0] & 0x0800) != 0;
    bool sm_bank = (s_adsp.sdrc[0] & 0x1000) != 0;
    if (sm_enabled) {
        if (!sm_bank && address >= 0x0800 && address <= 0x17ff)
            return s_adsp.sram[address - 0x0800];
        if (address >= 0x1800 && address <= 0x27ff)
            return s_adsp.sram[(sm_bank ? 0x3000 : 0x1000) + address - 0x1800];
        if (address >= 0x2800 && address <= 0x37ff)
            return s_adsp.sram[0x2000 + address - 0x2800];
    }

    if (address >= 0x3800 && address <= 0x39ff) return s_adsp.data[address];
    if (address >= 0x2000 && address <= 0x2fff) {
        size_t offset = ((size_t)(s_adsp.rom_bank & 0x7ff) << 12) | (address - 0x2000);
        return s_adsp.sound_rom[offset & (DCS_BANK_SIZE - 1)];
    }
    if (address >= 0x3fe0) return s_adsp.control[address - 0x3fe0];
    return 0xffff;
}

static void adsp_data_write(uint16_t address, uint16_t value)
{
    if (address == 0x0400) {
        pthread_mutex_lock(&s_adsp.lock);
        if (s_adsp.command_count) {
            s_adsp.command_head = (s_adsp.command_head + 1) & 65535;
            s_adsp.command_count--;
            s_adsp.commands_consumed++;
        }
        bool more = s_adsp.command_count != 0;
        pthread_mutex_unlock(&s_adsp.lock);
        p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 0);
        if (more) p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ2, 1);
        return;
    }
    if (address == 0x0401) {
        pthread_mutex_lock(&s_adsp.lock);
        s_adsp.output_data = value;
        s_adsp.output_full = true;
        pthread_mutex_unlock(&s_adsp.lock);
        LOGV2("dcs-adsp", "DSP->host %04x\n", value);
        return;
    }
    if (address == 0x0402) { s_adsp.output_control = value; return; }
    if (address >= 0x0480 && address <= 0x0483) {
        unsigned reg = address - 0x0480;
        if (reg < 3) {
            s_adsp.sdrc[reg] = value;
        } else {
            switch ((s_adsp.sdrc[0] >> 13) & 7) {
            case 1: s_adsp.sdrc_seed = value; break;
            case 3: s_adsp.sdrc_seed = (s_adsp.sdrc_seed << 1) | 1; break;
            case 4: s_adsp.sdrc_seed += s_adsp.sdrc_seed >> 1; break;
            case 5: s_adsp.sdrc_seed ^= (s_adsp.sdrc_seed << 1) | 1; break;
            case 6:
                s_adsp.sdrc_seed = (((s_adsp.sdrc_seed << 7) ^
                    (s_adsp.sdrc_seed << 5) ^ (s_adsp.sdrc_seed << 4) ^
                    (s_adsp.sdrc_seed << 3)) & 0x80) | (s_adsp.sdrc_seed >> 1);
                break;
            case 7: s_adsp.sdrc_seed = ~s_adsp.sdrc_seed; break;
            }
        }
        return;
    }

    unsigned dm_st = s_adsp.sdrc[1] & 3;
    unsigned dm_base = dm_st == 1 ? 0x0000 : dm_st == 2 ? 0x3000 : 0x3400;
    if (dm_st && address >= dm_base && address < dm_base + 0x400) {
        size_t word = ((size_t)(s_adsp.sdrc[2] & 0x7ff) * 1024 +
                       address - dm_base) % s_adsp.sound_words;
        s_adsp.sound_data[word] = value;
        return;
    }

    bool sm_enabled = (s_adsp.sdrc[0] & 0x0800) != 0;
    bool sm_bank = (s_adsp.sdrc[0] & 0x1000) != 0;
    if (sm_enabled) {
        if (!sm_bank && address >= 0x0800 && address <= 0x17ff) {
            s_adsp.sram[address - 0x0800] = value; return;
        }
        if (address >= 0x1800 && address <= 0x27ff) {
            s_adsp.sram[(sm_bank ? 0x3000 : 0x1000) + address - 0x1800] = value; return;
        }
        if (address >= 0x2800 && address <= 0x37ff) {
            s_adsp.sram[0x2000 + address - 0x2800] = value; return;
        }
    }
    if (address >= 0x3800 && address <= 0x39ff) { s_adsp.data[address] = value; return; }
    if (address == 0x3000) { s_adsp.rom_bank = value & 0x7ff; return; }
    if (address >= 0x3fe0) {
        unsigned reg = address - 0x3fe0;
        s_adsp.control[reg] = value;
        if ((reg == SYSCONTROL_REG && !(value & 0x0800)) ||
            (reg == S1_AUTOBUF_REG && !(value & 0x0002)))
            s_adsp.sport_enabled = false;
    }
}

static uint32_t adsp_program_read(uint16_t address)
{
    if (address >= 0x0800) {
        size_t offset = 0x4800 + (address - 0x0800) * 2;
        return (s_adsp.sram[offset] | ((uint32_t)s_adsp.sram[offset + 1] << 16)) & 0xffffff;
    }
    return s_adsp.program[address] & 0xffffff;
}

static void adsp_program_write(uint16_t address, uint32_t value)
{
    if (address >= 0x0800) {
        size_t offset = 0x4800 + (address - 0x0800) * 2;
        s_adsp.sram[offset] = value;
        s_adsp.sram[offset + 1] = value >> 16;
        return;
    }
    s_adsp.program[address] = value & 0xffffff;
}

static void adsp_tx(int port, int32_t value)
{
    if (port != 1 || !(s_adsp.control[SYSCONTROL_REG] & 0x0800) ||
        !(s_adsp.control[S1_AUTOBUF_REG] & 0x0002))
        return;

    s_adsp.ireg = (s_adsp.control[S1_AUTOBUF_REG] >> 9) & 7;
    int mreg = (s_adsp.control[S1_AUTOBUF_REG] >> 7) & 3;
    mreg |= s_adsp.ireg & 4;
    s_adsp.increment = (int16_t)p2k_adsp2105_get_reg(P2K_ADSP_M0 + mreg);
    s_adsp.length = p2k_adsp2105_get_reg(P2K_ADSP_L0 + s_adsp.ireg);
    int source = p2k_adsp2105_get_reg(P2K_ADSP_I0 + s_adsp.ireg);
    source &= ~0xf;
    p2k_adsp2105_set_reg(P2K_ADSP_I0 + s_adsp.ireg, source);
    s_adsp.base = source & 0x3fff;
    s_adsp.play_pos = s_adsp.base;
    s_adsp.next_irq_pos = s_adsp.length / 2;
    s_adsp.source_rate = 8000000.0 /
        (2.0 * (s_adsp.control[S1_SCLKDIV_REG] + 1) * 16.0);
    s_adsp.sport_enabled = s_adsp.length > 0 && s_adsp.increment != 0;
    LOG("dcs-adsp", "SPORT1 autobuffer %s I%d=%04x M=%d L=%d rate=%.2fHz\n",
        s_adsp.sport_enabled ? "started" : "invalid",
        s_adsp.ireg, s_adsp.base, s_adsp.increment, s_adsp.length,
        s_adsp.source_rate);
    (void)value;
}

static bool has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static bool find_update_sound_flash_rec(const char *dir, char *out, size_t out_sz, int depth)
{
    if (!dir || !*dir || depth > 4) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISREG(st.st_mode) && has_suffix(de->d_name, "_sf.rom")) {
            strncpy(out, path, out_sz - 1);
            out[out_sz - 1] = '\0';
            closedir(d);
            return true;
        }
        if (S_ISDIR(st.st_mode) && find_update_sound_flash_rec(path, out, out_sz, depth + 1)) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

static bool load_exact_1m(const char *path, uint8_t **out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long size = ftell(fp);
    if (size != P2K_DCS_SOUND_FLASH_SIZE || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp); return false;
    }
    uint8_t *buf = malloc(P2K_DCS_SOUND_FLASH_SIZE);
    if (!buf) { fclose(fp); return false; }
    size_t got = fread(buf, 1, P2K_DCS_SOUND_FLASH_SIZE, fp);
    fclose(fp);
    if (got != P2K_DCS_SOUND_FLASH_SIZE) { free(buf); return false; }
    *out = buf;
    return true;
}

static void adsp_audio_callback(void *userdata, uint8_t *stream, int len)
{
    (void)userdata;
    int frames = len / (int)(2 * sizeof(int16_t));
    adsp_render_direct((int16_t *)stream, frames, s_adsp.audio_rate ? s_adsp.audio_rate : ADSP_PCM_RATE, false);
}

static void *adsp_headless_pcm_worker(void *opaque)
{
    (void)opaque;
#ifdef __linux__
    pthread_setname_np(pthread_self(), "dcs-adsp-pcm");
#endif
    int16_t buf[ADSP_HEADLESS_FRAMES * 2];
    const struct timespec ts = { .tv_sec = 0, .tv_nsec = 5000000 };
    while (s_adsp.pcm_thread_run) {
        pthread_mutex_lock(&s_adsp.lock);
        bool useful = s_adsp.command_count != 0 || s_adsp.sport_enabled || s_adsp.selftest_ready;
        pthread_mutex_unlock(&s_adsp.lock);
        if (useful)
            adsp_render_direct(buf, ADSP_HEADLESS_FRAMES, ADSP_PCM_RATE, true);
        else
            nanosleep(&ts, NULL);
    }
    return NULL;
}

static void adsp_start_headless_worker(void)
{
    if (s_adsp.pcm_thread_started || !g_emu.headless) return;
    s_adsp.pcm_thread_run = true;
    if (pthread_create(&s_adsp.pcm_thread, NULL, adsp_headless_pcm_worker, NULL) == 0) {
        s_adsp.pcm_thread_started = true;
        LOG("dcs-adsp", "headless PCM/DSP clock worker started\n");
    }
}

int adsp_init(void)
{
    if (!g_emu.dcs_rom || g_emu.dcs_rom_size < DCS_BANK_SIZE) {
        LOG("dcs-adsp", "DCS U109/U110 ROM unavailable; ADSP disabled\n");
        return -1;
    }

    char path[sizeof(s_adsp.sound_flash_path)] = {0};
    const char *override = getenv("P2K_DCS_SOUND_FLASH");
    if (override && *override) {
        snprintf(path, sizeof(path), "%s", override);
    } else if (!find_update_sound_flash_rec(g_emu.update_file, path, sizeof(path), 0)) {
        snprintf(path, sizeof(path), "%s/%s_28f800.rom", g_emu.roms_dir, g_emu.game_prefix);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            snprintf(path, sizeof(path), "%s/%s/28f800.rom", g_emu.roms_dir, g_emu.game_prefix);
    }

    free(s_sound_flash);
    s_sound_flash = NULL;
    if (!load_exact_1m(path, &s_sound_flash)) {
        LOG("dcs-adsp", "no valid 1 MiB sound flash at %s\n", path[0] ? path : "<none>");
        return -1;
    }

    if (!s_adsp.locks_ready) {
        pthread_mutex_init(&s_adsp.lock, NULL);
        pthread_mutex_init(&s_adsp.core_lock, NULL);
        s_adsp.locks_ready = true;
    }

    uint16_t *old_sound_data = s_adsp.sound_data;
    SDL_AudioDeviceID old_audio = s_adsp.audio_dev;
    memset(&s_adsp.data, 0, sizeof(s_adsp.data));
    memset(&s_adsp.program, 0, sizeof(s_adsp.program));
    memset(&s_adsp.control, 0, sizeof(s_adsp.control));
    memset(&s_adsp.sdrc, 0, sizeof(s_adsp.sdrc));
    memset(&s_adsp.sram, 0, sizeof(s_adsp.sram));
    s_adsp.audio_dev = old_audio;
    s_adsp.sound_data = old_sound_data;
    s_adsp.sound_rom = g_emu.dcs_rom;
    free(s_adsp.sound_data);
    s_adsp.sound_words = P2K_DCS_REGION_WORDS;
    s_adsp.sound_data = calloc(s_adsp.sound_words, sizeof(uint16_t));
    if (!s_adsp.sound_data) return -1;

    size_t flash_words = P2K_DCS_SOUND_FLASH_SIZE / 2;
    size_t chip_words = DCS_BANK_SIZE / 4;
    for (size_t i = 0; i < flash_words; i++)
        s_adsp.sound_data[i] = encore_lduw_le(s_sound_flash + i * 2);
    for (size_t i = 0; i < chip_words; i++) {
        s_adsp.sound_data[P2K_DCS_U109_WORD_OFFSET + i] = encore_lduw_le(g_emu.dcs_rom + i * 4);
        s_adsp.sound_data[P2K_DCS_U110_WORD_OFFSET + i] = encore_lduw_le(g_emu.dcs_rom + i * 4 + 2);
    }

    s_adsp.rom_bank = 0;
    s_adsp.command_head = 0;
    s_adsp.command_count = 0;
    s_adsp.output_data = 0;
    s_adsp.output_control = 0;
    s_adsp.output_full = false;
    s_adsp.host_ack_count = 0;
    s_adsp.sport_enabled = false;
    s_adsp.source_phase = 0;
    s_adsp.cycle_phase = 0;
    s_adsp.cycles = 0;
    s_adsp.pcm_frames = 0;
    s_adsp.pcm_nonzero = 0;
    s_adsp.commands_enqueued = 0;
    s_adsp.commands_consumed = 0;
    s_adsp.commands_dropped_on_reset = 0;
    s_adsp.runtime_host_resets = 0;
    s_adsp.host_boot_completions = 0;
    s_adsp.pcm_peak = 0;
    memset(s_adsp.last_sample, 0, sizeof(s_adsp.last_sample));
    s_adsp.selftest_ready = false;
    s_adsp.host_boot = false;
    s_adsp.echo = 0;
    s_adsp.flag_latch = 0;
    strncpy(s_adsp.sound_flash_path, path, sizeof(s_adsp.sound_flash_path) - 1);

    uint8_t boot_page[0x1000];
    for (size_t i = 0; i < sizeof(boot_page); i++)
        boot_page[i] = s_sound_flash[i * 2];
    p2k_adsp2105_init(adsp_data_read, adsp_data_write, adsp_program_read, adsp_program_write);
    p2k_adsp2105_set_tx_callback(adsp_tx);
    p2k_adsp2105_load_boot_data(boot_page, s_adsp.program);
    p2k_adsp2105_reset();
    s_adsp.initialized = true;

    if (!g_emu.headless) {
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            LOG("dcs-adsp", "SDL audio init failed: %s\n", SDL_GetError());
        } else {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof(want));
            want.freq = ADSP_PCM_RATE;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 512;
            want.callback = adsp_audio_callback;
            if (s_adsp.audio_dev) SDL_CloseAudioDevice(s_adsp.audio_dev);
            s_adsp.audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (s_adsp.audio_dev) {
                s_adsp.audio_rate = have.freq;
                SDL_PauseAudioDevice(s_adsp.audio_dev, 0);
                g_emu.sound_ready = true;
                LOG("dcs-adsp", "raw SDL audio opened (%d Hz, %d ch)\n", have.freq, have.channels);
            } else {
                LOG("dcs-adsp", "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            }
        }
    } else {
        adsp_start_headless_worker();
    }

    LOG("dcs-adsp", "original assets ready (u109/u110=%u MiB, sound-flash=%s)\n",
        (unsigned)(DCS_BANK_SIZE / (1024 * 1024)), s_adsp.sound_flash_path);
    return 0;
}

void adsp_write_cmd(uint16_t command)
{
    if (!s_adsp.initialized) return;

    if (s_adsp.host_boot) {
        pthread_mutex_lock(&s_adsp.core_lock);
        uint8_t byte = command;
        if (s_adsp.host_boot_pos == 0) {
            s_adsp.host_boot_words = ((unsigned)byte + 1) * 8;
            if (s_adsp.host_boot_words > ARRAY_SIZE(s_adsp.program)) {
                LOG("dcs-adsp", "invalid host boot length %u words\n", s_adsp.host_boot_words);
                s_adsp.host_boot = false;
            } else {
                s_adsp.host_boot_pos = 1;
            }
            pthread_mutex_unlock(&s_adsp.core_lock);
            return;
        }
        unsigned data_pos = s_adsp.host_boot_pos - 1;
        if (data_pos < s_adsp.host_boot_words * 3) {
            unsigned phase = data_pos % 3;
            s_adsp.host_boot_triplet[phase] = byte;
            if (phase == 2) {
                unsigned address = s_adsp.host_boot_words - 1 - data_pos / 3;
                s_adsp.program[address] = ((uint32_t)s_adsp.host_boot_triplet[0] << 16) |
                                          ((uint32_t)s_adsp.host_boot_triplet[2] << 8) |
                                          s_adsp.host_boot_triplet[1];
            }
            s_adsp.host_boot_pos++;
            pthread_mutex_unlock(&s_adsp.core_lock);
            return;
        }
        s_adsp.host_boot = false;
        s_adsp.host_boot_completions++;
        s_adsp.command_head = 0;
        s_adsp.command_count = 0;
        s_adsp.output_full = false;
        p2k_adsp2105_reset();
        for (unsigned cycles = 0; cycles < 200000; cycles += 100) {
            pthread_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            pthread_mutex_unlock(&s_adsp.lock);
            if (responded) break;
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
        }
        LOG("dcs-adsp", "accepted x86 host boot (%u PM words, entry=%06x)\n",
            s_adsp.host_boot_words, s_adsp.program[0] & 0xffffff);
        pthread_mutex_unlock(&s_adsp.core_lock);
        return;
    }

    pthread_mutex_lock(&s_adsp.lock);
    static unsigned command_logs;
    if (command == 0x8280) s_adsp.selftest_ready = true;
    if (command == 0x5800 || command == 0x5a00) {
        s_adsp.host_ack[0] = 0x1000;
        s_adsp.host_ack_head = 0;
        s_adsp.host_ack_count = 1;
    }
    if (command == 0xace1) {
        s_adsp.host_ack[0] = 0x0100;
        s_adsp.host_ack[1] = 0x000c;
        s_adsp.host_ack_head = 0;
        s_adsp.host_ack_count = 2;
    }
    unsigned queued_after = s_adsp.command_count;
    if (s_adsp.command_count < ARRAY_SIZE(s_adsp.commands)) {
        unsigned tail = (s_adsp.command_head + s_adsp.command_count) & 65535;
        s_adsp.commands[tail] = command;
        s_adsp.command_count++;
        s_adsp.commands_enqueued++;
        queued_after = s_adsp.command_count;
        if (command_logs++ < 96)
            LOG("dcs-adsp", "host->DSP %04x queued=%u\n", command, s_adsp.command_count);
    } else {
        LOG("dcs-adsp", "command FIFO overflow, dropping 0x%04x\n", command);
    }
    pthread_mutex_unlock(&s_adsp.lock);

    bool synchronous_diag = command == 0x003a || command == 0x001b || command == 0x00aa;
    if (!synchronous_diag) {
        pthread_mutex_lock(&s_adsp.core_lock);
        adsp_assert_mailbox_irq_locked();
        for (unsigned cycles = 0; cycles < 20000; cycles += 100) {
            pthread_mutex_lock(&s_adsp.lock);
            bool consumed = s_adsp.command_count < queued_after;
            pthread_mutex_unlock(&s_adsp.lock);
            if (consumed) break;
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
        }
        pthread_mutex_unlock(&s_adsp.core_lock);
    } else {
        unsigned limit = command == 0x001b ? 250000000u : 20000000u;
        pthread_mutex_lock(&s_adsp.core_lock);
        adsp_assert_mailbox_irq_locked();
        for (unsigned cycles = 0; cycles < limit; cycles += 1000) {
            p2k_adsp2105_execute(1000);
            s_adsp.cycles += 1000;
            pthread_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            pthread_mutex_unlock(&s_adsp.lock);
            if (responded) break;
        }
        pthread_mutex_unlock(&s_adsp.core_lock);
    }

    if (g_emu.headless && command == 0x8280)
        adsp_start_headless_worker();
}

void adsp_host_reset(void)
{
    if (!s_adsp.initialized) return;
    pthread_mutex_lock(&s_adsp.core_lock);
    pthread_mutex_lock(&s_adsp.lock);
    if (s_adsp.pcm_thread_started || s_adsp.audio_dev) s_adsp.runtime_host_resets++;
    s_adsp.commands_dropped_on_reset += s_adsp.command_count;
    pthread_mutex_unlock(&s_adsp.lock);
    for (unsigned cycles = 0; cycles < 2000000; cycles += 1000) {
        unsigned pc = p2k_adsp2105_get_reg(P2K_ADSP_PC) & 0x3fff;
        if (adsp_program_read(0x3980) != 0 && adsp_program_read(0x3deb) != 0 &&
            pc >= 0x3d9c && pc <= 0x3dc1)
            break;
        p2k_adsp2105_execute(1000);
        s_adsp.cycles += 1000;
    }
    if (adsp_program_read(0x3deb) == 0x0000ff)
        adsp_program_write(0x3deb, 0xffffff);
    pthread_mutex_lock(&s_adsp.lock);
    s_adsp.host_boot = true;
    s_adsp.host_boot_pos = 0;
    s_adsp.host_boot_words = 0;
    s_adsp.host_boot_compare_logged = false;
    s_adsp.command_head = 0;
    s_adsp.command_count = 0;
    s_adsp.output_full = false;
    pthread_mutex_unlock(&s_adsp.lock);
    pthread_mutex_unlock(&s_adsp.core_lock);
    LOG("dcs-adsp", "host reset accepted (awaiting x86 DSP boot stream)\n");
}

uint8_t adsp_flag_byte(void)
{
    pthread_mutex_lock(&s_adsp.lock);
    uint8_t flags = (s_adsp.command_count == 0 ? 0x40 : 0) |
                    ((s_adsp.output_full || s_adsp.host_ack_count) ? 0x80 : 0);
    pthread_mutex_unlock(&s_adsp.lock);
    return flags;
}

uint16_t adsp_read_response(void)
{
    static unsigned response_logs;
    pthread_mutex_lock(&s_adsp.core_lock);
    pthread_mutex_lock(&s_adsp.lock);
    uint16_t value;
    if (s_adsp.host_ack_count) {
        value = s_adsp.host_ack[s_adsp.host_ack_head++];
        s_adsp.host_ack_count--;
    } else {
        value = s_adsp.output_full ? s_adsp.output_data : 0;
        s_adsp.output_full = false;
    }
    pthread_mutex_unlock(&s_adsp.lock);
    if (value) {
        for (unsigned cycles = 0; cycles < 100000; cycles += 100) {
            p2k_adsp2105_execute(100);
            s_adsp.cycles += 100;
            pthread_mutex_lock(&s_adsp.lock);
            bool responded = s_adsp.output_full;
            pthread_mutex_unlock(&s_adsp.lock);
            if (responded) break;
        }
    }
    pthread_mutex_unlock(&s_adsp.core_lock);
    if (response_logs++ < 64) LOG("dcs-adsp", "host read %04x\n", value);
    return value;
}

static void adsp_render_direct(int16_t *samples, int frames, int output_rate,
                               bool mailbox_pending)
{
    if (!s_adsp.initialized || output_rate <= 0) {
        memset(samples, 0, frames * 2 * sizeof(*samples));
        return;
    }

    pthread_mutex_lock(&s_adsp.core_lock);
    if (mailbox_pending) adsp_assert_mailbox_irq_locked();
    if (s_adsp.host_boot) {
        memset(samples, 0, frames * 2 * sizeof(*samples));
        pthread_mutex_unlock(&s_adsp.core_lock);
        return;
    }
    for (int n = 0; n < frames; n++) {
        s_adsp.cycle_phase += 10000000.0 / output_rate;
        int cycles = (int)s_adsp.cycle_phase;
        s_adsp.cycle_phase -= cycles;
        p2k_adsp2105_execute(cycles);
        s_adsp.cycles += cycles;

        if (s_adsp.sport_enabled) {
            s_adsp.source_phase += s_adsp.source_rate;
            while (s_adsp.source_phase >= output_rate) {
                s_adsp.source_phase -= output_rate;
                for (int channel = 0; channel < 2; channel++) {
                    s_adsp.last_sample[channel] = (int16_t)adsp_data_read(s_adsp.play_pos & 0x3fff);
                    s_adsp.play_pos += s_adsp.increment;
                }
                int relative = s_adsp.play_pos - s_adsp.base;
                bool half_elapsed = relative >= s_adsp.next_irq_pos;
                bool wrapped = relative >= s_adsp.length || relative < 0;
                if (wrapped) {
                    s_adsp.play_pos = s_adsp.base;
                    relative = 0;
                    s_adsp.next_irq_pos = s_adsp.length / 2;
                } else if (half_elapsed) {
                    s_adsp.next_irq_pos = s_adsp.length;
                }
                if (half_elapsed || wrapped) {
                    p2k_adsp2105_set_reg(P2K_ADSP_I0 + s_adsp.ireg, s_adsp.play_pos);
                    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ1, 1);
                    p2k_adsp2105_execute(100);
                    s_adsp.cycles += 100;
                    p2k_adsp2105_set_irq_line(P2K_ADSP_IRQ1, 0);
                }
            }
        }
        for (int channel = 0; channel < 2; channel++) {
            int16_t sample = s_adsp.sport_enabled ? s_adsp.last_sample[channel] : 0;
            samples[n * 2 + channel] = sample;
            if (sample) {
                unsigned magnitude = sample == INT16_MIN ? 32768u : (unsigned)abs(sample);
                s_adsp.pcm_nonzero++;
                s_adsp.pcm_peak = MAX(s_adsp.pcm_peak, magnitude);
            }
        }
    }
    s_adsp.pcm_frames += frames;
    static uint64_t next_report = ADSP_PCM_RATE / 2;
    if (s_adsp.pcm_frames >= next_report) {
        pthread_mutex_lock(&s_adsp.lock);
        unsigned queued = s_adsp.command_count;
        pthread_mutex_unlock(&s_adsp.lock);
        unsigned pc = p2k_adsp2105_get_reg(P2K_ADSP_PC) & 0x3fff;
        LOG("dcs-adsp", "run pc=%04x op=%06x cycles=%llu sport=%d queued=%u pcm_frames=%llu nonzero=%llu peak=%u\n",
            pc, adsp_program_read(pc), (unsigned long long)s_adsp.cycles,
            s_adsp.sport_enabled, queued, (unsigned long long)s_adsp.pcm_frames,
            (unsigned long long)s_adsp.pcm_nonzero, s_adsp.pcm_peak);
        next_report += ADSP_PCM_RATE / 2;
    }
    pthread_mutex_unlock(&s_adsp.core_lock);
}

uint8_t adsp_get_echo(void) { return s_adsp.echo; }
void adsp_set_echo(uint8_t value) { s_adsp.echo = value; }
bool adsp_accepts_boot_byte(void) { return s_adsp.initialized && s_adsp.host_boot; }

uint32_t adsp_bar_read(uint32_t off, int size)
{
    if (off == 0) return size == 1 ? adsp_get_echo() : adsp_read_response();
    if (off == 2) return adsp_flag_byte() | (s_adsp.flag_latch & 0x3f);
    return 0;
}

void adsp_bar_write(uint32_t off, uint32_t value, int size)
{
    if (off == 0) {
        if (size == 1) {
            if (adsp_accepts_boot_byte())
                adsp_write_cmd(value & 0xff);
            else
                adsp_set_echo(value & 0xff);
        }
        else adsp_write_cmd(value & 0xffff);
        return;
    }
    if (off == 2) s_adsp.flag_latch = value & 0xffff;
}

void adsp_cleanup(void)
{
    if (s_adsp.pcm_thread_started) {
        s_adsp.pcm_thread_run = false;
        pthread_join(s_adsp.pcm_thread, NULL);
        s_adsp.pcm_thread_started = false;
    }
    if (s_adsp.audio_dev) {
        SDL_CloseAudioDevice(s_adsp.audio_dev);
        s_adsp.audio_dev = 0;
    }
    if (s_adsp.initialized) {
        LOG("dcs-adsp", "shutdown: queued=%u host_resets=%u host_boots=%u enqueued=%llu consumed=%llu dropped=%llu pcm_frames=%llu nonzero=%llu cycles=%llu peak=%u\n",
            s_adsp.command_count, s_adsp.runtime_host_resets, s_adsp.host_boot_completions,
            (unsigned long long)s_adsp.commands_enqueued,
            (unsigned long long)s_adsp.commands_consumed,
            (unsigned long long)s_adsp.commands_dropped_on_reset,
            (unsigned long long)s_adsp.pcm_frames,
            (unsigned long long)s_adsp.pcm_nonzero,
            (unsigned long long)s_adsp.cycles,
            s_adsp.pcm_peak);
    }
    free(s_adsp.sound_data);
    s_adsp.sound_data = NULL;
    free(s_sound_flash);
    s_sound_flash = NULL;
    s_adsp.initialized = false;
    g_emu.sound_ready = false;
}
