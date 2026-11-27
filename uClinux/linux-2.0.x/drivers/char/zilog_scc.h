/*****************************************************************************/
/* zilog_scc.h: Zilog Z8530 SCC serial driver.
 *
 * uClinux/linux-2.0.x/drivers/char/zilog_scc.h
 *
 * Definitions for the Zilog Z8530 SCC serial driver.
 * The serial controller is present in the Macintosh Plus computer.
 *
 * Some of the source code in this file was derived from :
 * scc.c at uClinux/linux-2.0.x/drivers/char/scc.c
 * scc.h at uClinux/linux-2.0.x/include/linux/scc.h
 *
 * The reason why the source code was derived from these files is that the
 * original implementation is a KISS/HDLC driver and not a serial driver.
 * Since it's the same serial controller, some of the structure and macro
 * definitions were reused.
 *
 * The development of this driver was also assisted with the use of LLMs.
 *
 * Author :     Joerg Reuter DL1BKE
 *              Guido ten Dolle PE1NNZ
 *              Cédric Alexander Tremblay PlutoTramble
 *              Implementation written with claude, reviewed by PlutoTramble
 *
 *  This program is free software;  you can redistribute it and/or modify it
 *  under the  terms of the GNU  General Public License as  published by the
 *  Free Software Foundation.  See the file COPYING in the main directory of
 *  this archive for more details.
 *
 *  This program  is distributed  in the  hope that it  will be  useful, but
 *  WITHOUT   ANY   WARRANTY;  without   even   the   implied  warranty   of
 *  MERCHANTABILITY  or  FITNESS FOR  A  PARTICULAR  PURPOSE.   See the  GNU
 *  General Public License for more details.
 *
 */
/****************************************************************************/


#ifndef _ZILOG_SCC_H
#define _ZILOG_SCC_H

#include <linux/tqueue.h>

#include "linux/types.h"

#define SCC_NPORTS      2
#define SCC_XMIT_SIZE   512   /* must be a power of 2 */
#define SCC_WAKEUP      128   /* wake up writers below this many queued bytes */
#define SCC_MAGIC	    0x8530

/* Write Register 0 */
#define	R0	0		/* Register selects */
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15

#define	W0_NULLCODE	        0	    /* Null Code */
#define	W0_POINT_HIGH	    0x8	    /* Select upper half of registers */
#define	W0_RES_EXT_INT	    0x10	/* Reset Ext. Status Interrupts */
#define	W0_SEND_ABORT	    0x18	/* HDLC Abort */
#define	W0_RES_RxINT_FC	    0x20	/* Reset RxINT on First Character */
#define	W0_RES_Tx_P	        0x28	/* Reset TxINT Pending */
#define	W0_ERR_RES		    0x30	/* Error Reset */
#define	W0_RES_H_IUS	    0x38	/* Reset highest IUS */

#define	W0_RES_Rx_CRC	    0x40	/* Reset Rx CRC Checker */
#define	W0_RES_Tx_CRC	    0x80	/* Reset Tx CRC Checker */
#define	W0_RES_EOM_L	    0xC0	/* Reset EOM latch */

/* Write Register 1 */
#define	W1_EXT_INT_ENAB	    0x1	    /* Ext Int Enable */
#define	W1_TxINT_ENAB	    0x2	    /* Tx Int Enable */
#define	W1_PAR_SPEC	        0x4	    /* Parity is special condition */

#define	W1_RxINT_DISAB	    0	    /* Rx Int Disable */
#define	W1_RxINT_FCERR	    0x8	    /* Rx Int on First Character Only or Error */
#define	W1_INT_ALL_Rx	    0x10	/* Int on all Rx Characters or error */
#define	W1_INT_ERR_Rx	    0x18	/* Int on error only */

#define	W1_WT_RDY_RT	    0x20	/* Wait/Ready on R/T */
#define	W1_WT_FN_RDYFN	    0x40	/* Wait/FN/Ready FN */
#define	W1_WT_RDY_ENAB	    0x80	/* Wait/Ready Enable */

/* Write Register 2 (Interrupt Vector) */

/* Write Register 3 */
#define	W3_RxENABLE	        0x1	    /* Rx Enable */
#define	W3_SYNC_L_INH	    0x2	    /* Sync Character Load Inhibit */
#define	W3_ADD_SM		    0x4	    /* Address Search Mode (SDLC) */
#define	W3_RxCRC_ENAB	    0x8	    /* Rx CRC Enable */
#define	W3_ENT_HM		    0x10	/* Enter Hunt Mode */
#define	W3_AUTO_ENAB	    0x20	/* Auto Enables */
#define	W3_Rx5		        0x0 	/* Rx 5 Bits/Character */
#define	W3_Rx7		        0x40	/* Rx 7 Bits/Character */
#define	W3_Rx6		        0x80	/* Rx 6 Bits/Character */
#define	W3_Rx8		        0xc0	/* Rx 8 Bits/Character */

/* Write Register 4 */
#define	W4_PAR_ENA		    0x1	    /* Parity Enable */
#define	W4_PAR_EVEN		    0x2	    /* Parity Even/Odd* */

#define	W4_SYNC_ENAB	    0	    /* Sync Modes Enable */
#define	W4_SB1		        0x4	    /* 1 stop bit/char */
#define	W4_SB15		        0x8	    /* 1.5 stop bits/char */
#define	W4_SB2		        0xc	    /* 2 stop bits/char */

#define	W4_MONSYNC		    0	    /* 8 Bit Sync character */
#define	W4_BISYNC		    0x10	/* 16 bit sync character */
#define	W4_SDLC		        0x20	/* SDLC Mode (01111110 Sync Flag) */
#define	W4_EXTSYNC		    0x30	/* External Sync Mode */

#define	W4_X1CLK		    0x0	    /* x1 clock mode */
#define	W4_X16CLK		    0x40	/* x16 clock mode */
#define	W4_X32CLK		    0x80	/* x32 clock mode */
#define	W4_X64CLK		    0xC0	/* x64 clock mode */

/* Write Register 5 */
#define	W5_TxCRC_ENAB	    0x1	    /* Tx CRC Enable */
#define	W5_RTS		        0x2	    /* RTS */
#define	W5_SDLC_CRC	        0x4	    /* SDLC/CRC-16 */
#define	W5_TxENAB		    0x8	    /* Tx Enable */
#define	W5_SND_BRK		    0x10	/* Send Break */
#define	W5_Tx5		        0x0	    /* Tx 5 bits (or less)/character */
#define	W5_Tx7		        0x20	/* Tx 7 bits/character */
#define	W5_Tx6		        0x40	/* Tx 6 bits/character */
#define	W5_Tx8		        0x60	/* Tx 8 bits/character */
#define	W5_DTR		        0x80	/* DTR */

/* Write Register 6 (Sync bits 0-7/SDLC Address Field) */

/* Write Register 7 (Sync bits 8-15/SDLC 01111110) */

/* Write Register 8 (transmit buffer) */

/* Write Register 9 (Master interrupt control) */
#define	W9_VIS	            1	    /* Vector Includes Status */
#define	W9_NV	            2	    /* No Vector */
#define	W9_DLC	            4	    /* Disable Lower Chain */
#define	W9_MIE	            8	    /* Master Interrupt Enable */
#define	W9_STATHI	        0x10	/* Status high */
#define	W9_NORESET	        0	    /* No reset on write to R9 */
#define	W9_CHRB	            0x40	/* Reset channel B */
#define	W9_CHRA	            0x80	/* Reset channel A */
#define	W9_FHWRES	        0xc0	/* Force hardware reset */

/* Write Register 10 (misc control bits) */
#define	W10_BIT6	        1	    /* 6 bit/8bit sync */
#define	W10_LOOPMODE        2	    /* SDLC Loop mode */
#define	W10_ABUNDER	        4	    /* Abort/flag on SDLC xmit underrun */
#define	W10_MARKIDLE        8	    /* Mark/flag on idle */
#define	W10_GAOP	        0x10	/* Go active on poll */
#define	W10_NRZ	            0	    /* NRZ mode */
#define	W10_NRZI	        0x20	/* NRZI mode */
#define	W10_FM1	            0x40	/* FM1 (transition = 1) */
#define	W10_FM0	            0x60	/* FM0 (transition = 0) */
#define	W10_CRCPS	        0x80	/* CRC Preset I/O */

/* Write Register 11 (Clock Mode control) */
#define	W11_TRxCXT	        0	    /* TRxC = Xtal output */
#define	W11_TRxCTC	        1	    /* TRxC = Transmit clock */
#define	W11_TRxCBR	        2	    /* TRxC = BR Generator Output */
#define	W11_TRxCDP	        3	    /* TRxC = DPLL output */
#define	W11_TRxCOI	        4	    /* TRxC O/I */
#define	W11_TCRTxCP	        0	    /* Transmit clock = RTxC pin */
#define	W11_TCTRxCP	        8	    /* Transmit clock = TRxC pin */
#define	W11_TCBR	        0x10	/* Transmit clock = BR Generator output */
#define	W11_TCDPLL	        0x18	/* Transmit clock = DPLL output */
#define	W11_RCRTxCP	        0	    /* Receive clock = RTxC pin */
#define	W11_RCTRxCP	        0x20	/* Receive clock = TRxC pin */
#define	W11_RCBR	        0x40	/* Receive clock = BR Generator output */
#define	W11_RCDPLL	        0x60	/* Receive clock = DPLL output */
#define	W11_RTxCX	        0x80	/* RTxC Xtal/No Xtal */

/* Write Register 12 (lower byte of baud rate generator time constant) */

/* Write Register 13 (upper byte of baud rate generator time constant) */

/* Write Register 14 (Misc control bits) */
#define	W14_BRENABL	        1	    /* Baud rate generator enable */
#define	W14_BRSRC	        2	    /* Baud rate generator source */
#define	W14_DTRREQ	        4	    /* DTR/Request function */
#define	W14_AUTOECHO        8	    /* Auto Echo */
#define	W14_LOOPBAK	        0x10	/* Local loopback */
#define	W14_SEARCH	        0x20	/* Enter search mode */
#define	W14_RMC	            0x40	/* Reset missing clock */
#define	W14_DISDPLL	        0x60	/* Disable DPLL */
#define	W14_SSBR	        0x80	/* Set DPLL source = BR generator */
#define	W14_SSRTxC	        0xa0	/* Set DPLL source = RTxC */
#define	W14_SFMM	        0xc0	/* Set FM mode */
#define	W14_SNRZI	        0xe0	/* Set NRZI mode */

/* Write Register 15 (external/status interrupt control) */
#define	W15_ZCIE	        2	    /* Zero count IE */
#define	W15_DCDIE	        8	    /* DCD IE */
#define	W15_SYNCIE	        0x10	/* Sync/hunt IE */
#define	W15_CTSIE	        0x20	/* CTS IE */
#define	W15_TxUIE	        0x40	/* Tx Underrun/EOM IE */
#define	W15_BRKIE	        0x80	/* Break/Abort IE */


/* Read Register 0 */
#define	R0_Rx_CH_AV	        0x1	    /* Rx Character Available */
#define	R0_ZCOUNT		    0x2	    /* Zero count */
#define	R0_Tx_BUF_EMP	    0x4	    /* Tx Buffer empty */
#define	R0_DCD		        0x8	    /* DCD */
#define	R0_SYNC_HUNT	    0x10	/* Sync/hunt */
#define	R0_CTS		        0x20	/* CTS */
#define	R0_TxEOM		    0x40	/* Tx underrun */
#define	R0_BRK_ABRT	        0x80	/* Break/Abort */

/* Read Register 1 */
#define	R1_ALL_SNT		    0x1	    /* All sent */
/* Residue Data for 8 Rx bits/char programmed */
#define	R1_RES3		        0x8	    /* 0/3 */
#define	R1_RES4		        0x4	    /* 0/4 */
#define	R1_RES5		        0xc	    /* 0/5 */
#define	R1_RES6		        0x2	    /* 0/6 */
#define	R1_RES7		        0xa 	/* 0/7 */
#define	R1_RES8		        0x6 	/* 0/8 */
#define	R1_RES18		    0xe 	/* 1/8 */
#define	R1_RES28		    0x0 	/* 2/8 */
/* Special Rx Condition Interrupts */
#define	R1_PAR_ERR		    0x10	/* Parity error */
#define	R1_Rx_OVR		    0x20	/* Rx Overrun Error */
#define	R1_CRC_ERR		    0x40	/* CRC/Framing Error */
#define	R1_END_FR		    0x80	/* End of Frame (SDLC) */

/* Read Register 2 (channel B only) - Interrupt vector */
#define R2_VECTOR_MASK	    0x06

#define R2_TXINT            0x00
#define R2_EXINT            0x02
#define R2_RXINT            0x04
#define R2_SPINT            0x06


/* Read Register 3 (interrupt pending register) ch a only */
#define	R3_CHBEXT	        0x1		/* Channel B Ext/Stat IP */
#define	R3_CHBTxIP	        0x2		/* Channel B Tx IP */
#define	R3_CHBRxIP	        0x4		/* Channel B Rx IP */
#define	R3_CHAEXT	        0x8		/* Channel A Ext/Stat IP */
#define	R3_CHATxIP	        0x10	/* Channel A Tx IP */
#define	R3_CHARxIP	        0x20	/* Channel A Rx IP */

/* Read Register 8 (receive data register) */

/* Read Register 10  (misc status bits) */
#define	R10_ONLOOP	        2		/* On loop */
#define	R10_LOOPSEND        0x10	/* Loop sending */
#define	R10_CLK2MIS     	0x40	/* Two clocks missing */
#define	R10_CLK1MIS	        0x80	/* One clock missing */

/* Read Register 12 (lower byte of baud rate generator constant) */

/* Read Register 13 (upper byte of baud rate generator constant) */

/* Read Register 15 (value of WR 15) */


// -------------

struct zilog_scc_info
{
    int magic;		            /* magic word */
    int line;                   /* 0 = A/modem, 1 = B/printer */

    volatile u_char *ctl_rd, *ctl_wr, *data_rd, *data_wr;
    struct tty_struct *tty; /* link to tty control structure */
    char tty_opened;	        /* No. of open() calls... */
    int flags;                  /* ASYNC_* from linux/serial.h */

    unsigned char wreg[16];     /* Copy of last written value in WRx */
    unsigned char xmit_buf[SCC_XMIT_SIZE];
    int xmit_head, xmit_tail, xmit_cnt;

    int tx_active;              /* 1 = a Tx interrupt is expected */
    struct wait_queue *open_wait, *close_wait;
    struct tq_struct tqueue;    /* wakes writers outside the IRQ */
};

// -------------

void zilog_scc_interrupt(int irq, void *dev_id, struct pt_regs *regs);

#endif //_ZILOG_SCC_H
