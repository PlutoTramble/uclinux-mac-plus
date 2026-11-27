/*****************************************************************************/
/* zilog_scc.c: Zilog Z8530 SCC serial driver.
 *
 * uClinux/linux-2.0.x/drivers/char/zilog_scc.c
 *
 * Serial driver for the two channels of the Z8530 SCC of the Macintosh Plus:
 *   channel A (modem port)   -> /dev/ttyS0, /dev/cua0
 *   channel B (printer port) -> /dev/ttyS1, /dev/cua1
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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/serial.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
#include <asm/traps.h>          /* VEC_INT2, VEC_SPUR */

#include "../../arch/m68knommu/platform/68000/MacPlus/mac_hw.h"
#include "zilog_scc.h"

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL  1
#define SERIAL_TYPE_CALLOUT 2

/* From the manual : https://www.alldatasheet.com/datasheet-pdf/download/96934/ZILOG/Z8530.html */
/* baud rate generator time constant: PCLK / (2 * 16 * baud) - 2, rounded */
#define SCC_TC(baud) ((((MAC_SCC_PCLK) + 16 * (baud)) / (32 * (baud))) - 2)

/* the 8 MHz 68000 can be faster than the SCC: wait a bit after every access */
#define SCC_DELAY() __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop")

static struct tty_driver scc_driver, scc_callout_driver;
static int scc_refcount;
static struct tty_struct *scc_table[SCC_NPORTS];
static struct termios *scc_termios[SCC_NPORTS];
static struct termios *scc_termios_locked[SCC_NPORTS];
static struct zilog_scc_info scc_ports[SCC_NPORTS];
static int scc_ports_open;          /* how many ports are started (for WR9 MIE) */
static volatile int scc_in_irq;     /* re-entry guard */

/* caller must have interrupts disabled (cli) */
static u_char scc_read(const struct zilog_scc_info *info, const u_char reg)
{
    u_char val;

    if (reg != 0) {
        *info->ctl_wr = reg;   /* select register */
        SCC_DELAY();
    }
    val = *info->ctl_rd;       /* read it; the pointer goes back to 0 */
    SCC_DELAY();
    return val;
}

/* caller must have interrupts disabled (cli). reg 0 = send a WR0 command */
static void scc_write(struct zilog_scc_info *info, u_char reg, u_char val)
{
    if (reg != 0) {
        *info->ctl_wr = reg;
        SCC_DELAY();
    }
    *info->ctl_wr = val;
    SCC_DELAY();
    if (reg < 16)
        info->wreg[reg] = val;
}

/* returns the baud rate generator time constant for the termios baud rate */
static int scc_baud_tc(unsigned int cflag)
{
    switch (cflag & CBAUD) {
    case B300:   return SCC_TC(300);
    case B1200:  return SCC_TC(1200);
    case B2400:  return SCC_TC(2400);
    case B4800:  return SCC_TC(4800);
    case B19200: return SCC_TC(19200);
    case B38400: return SCC_TC(38400);
    case B57600: return SCC_TC(57600);
    case B9600:
    default:     return SCC_TC(9600);
    }
}

/* programs baud rate, character size, parity and stop bits from termios */
static void zilog_scc_change_speed(struct zilog_scc_info *info)
{
    unsigned int cflag;
    unsigned long flags;
    u_char wr3, wr4, wr5;
    int tc;

    if (!info->tty || !info->tty->termios)
        return;
    cflag = info->tty->termios->c_cflag;
    tc = scc_baud_tc(cflag);

    wr4 = W4_X16CLK | ((cflag & CSTOPB) ? W4_SB2 : W4_SB1);
    if (cflag & PARENB) {
        wr4 |= W4_PAR_ENA;
        if (!(cflag & PARODD))
            wr4 |= W4_PAR_EVEN;
    }

    /* clear the size bits, keep the enable bits */
    wr3 = info->wreg[R3] & ~W3_Rx8;
    wr5 = info->wreg[R5] & ~W5_Tx8;
    switch (cflag & CSIZE) {
    case CS5: break;
    case CS6: wr3 |= W3_Rx6; wr5 |= W5_Tx6; break;
    case CS7: wr3 |= W3_Rx7; wr5 |= W5_Tx7; break;
    default:  wr3 |= W3_Rx8; wr5 |= W5_Tx8; break;
    }

    save_flags(flags); cli();
    (void)*info->ctl_rd;                    /* register pointer back to 0 */
    SCC_DELAY();
    scc_write(info, R4, wr4);               /* WR4 first, as the manual asks */
    scc_write(info, R3, wr3);
    scc_write(info, R5, wr5);
    scc_write(info, R11, W11_RCBR | W11_TCBR);
    scc_write(info, R14, 0);                /* BRG off while changing */
    scc_write(info, R12, tc & 0xff);
    scc_write(info, R13, (tc >> 8) & 0xff);
    scc_write(info, R14, W14_BRSRC | W14_BRENABL);
    restore_flags(flags);
}

/* runs on first open: brings the channel up and lets it interrupt */
static int zilog_scc_startup(struct zilog_scc_info *info)
{
    unsigned long flags;
    int i;

    // Is already initialized
    if (info->flags & ASYNC_INITIALIZED)
        return 0;

    save_flags(flags); cli();
    (void)*info->ctl_rd;            /* make sure the register pointer is at 0 */
    SCC_DELAY();
    info->xmit_head = info->xmit_tail = info->xmit_cnt = 0;
    info->tx_active = 0;

    scc_write(info, R1, 0);         /* no interrupts while configuring */
    // Configuration : 8 bit characters Rx & Tx
    info->wreg[R3] = W3_Rx8;
    info->wreg[R5] = W5_Tx8 | W5_DTR | W5_RTS;
    restore_flags(flags);

    zilog_scc_change_speed(info);

    save_flags(flags); cli();
    // Enable transmission and reception
    scc_write(info, R3, info->wreg[R3] | W3_RxENABLE);
    scc_write(info, R5, info->wreg[R5] | W5_TxENAB);
    scc_write(info, R15, 0);        /* no ext/status sources: the mouse is ignored */
    scc_write(info, R0, W0_RES_EXT_INT);
    scc_write(info, R0, W0_RES_EXT_INT);
    for (i = 0; i < 4 && (scc_read(info, R0) & R0_Rx_CH_AV); i++) {
        (void)*info->data_rd;       /* throw away old garbage */
        SCC_DELAY();
    }
    scc_write(info, R0, W0_ERR_RES);
    scc_write(info, R1, W1_INT_ALL_Rx | W1_TxINT_ENAB);
    scc_ports_open++;

    // re-enable interrupts
    scc_write(info, R9, W9_MIE | W9_NV);
    if (info->tty)
        clear_bit(TTY_IO_ERROR, &info->tty->flags);
    info->flags |= ASYNC_INITIALIZED;
    restore_flags(flags);

    return 0;
}

/* runs on last close */
static void zilog_scc_shutdown(struct zilog_scc_info *info)
{
    unsigned long flags;
    u_char wr5;

    if (!(info->flags & ASYNC_INITIALIZED))
        return;

    save_flags(flags); cli();
    scc_write(info, R1, 0);
    scc_write(info, R3, info->wreg[R3] & ~W3_RxENABLE);
    wr5 = info->wreg[R5] & ~W5_TxENAB;
    if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
        wr5 &= ~(W5_DTR | W5_RTS);
    scc_write(info, R5, wr5);
    if (--scc_ports_open <= 0) {
        scc_ports_open = 0;
        scc_write(info, R9, W9_NV);         /* MIE off */
    }
    info->tx_active = 0;
    if (info->tty)
        set_bit(TTY_IO_ERROR, &info->tty->flags);
    info->flags &= ~ASYNC_INITIALIZED;
    restore_flags(flags);
}

/* runs later, outside the interrupt: wakes up processes waiting to write */
static void zilog_scc_softint(void *private_)
{
    struct zilog_scc_info *info = private_;
    struct tty_struct *tty = info->tty;

    if (!tty)
        return;
    if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
        (tty->ldisc.write_wakeup)(tty);
    wake_up_interruptible(&tty->write_wait);
}

/* receive path of the interrupt handler (interrupts are already blocked) */
static void zilog_scc_rx(struct zilog_scc_info *info)
{
    struct tty_struct *tty = info->tty;
    u_char status, ch, flag;
    int n = 0;

    while (n++ < 4 && (scc_read(info, R0) & R0_Rx_CH_AV)) {
        status = scc_read(info, R1);    /* errors of THIS char: read before the data */
        ch = *info->data_rd;
        SCC_DELAY();
        flag = TTY_NORMAL;
        if (status & (R1_PAR_ERR | R1_Rx_OVR | R1_CRC_ERR)) {
            scc_write(info, R0, W0_ERR_RES);
            if (status & R1_Rx_OVR)
                flag = TTY_OVERRUN;
            else if (status & R1_CRC_ERR)
                flag = TTY_FRAME;
            else
                flag = TTY_PARITY;
        }
        if (!tty || !(info->flags & ASYNC_INITIALIZED))
            continue;                   /* port not open: drop */
        if (tty->flip.count >= TTY_FLIPBUF_SIZE)
            continue;                   /* flip buffer full: drop */
        tty->flip.count++;
        *tty->flip.flag_buf_ptr++ = flag;
        *tty->flip.char_buf_ptr++ = ch;
    }
    if (tty)
        queue_task_irq_off(&tty->flip.tqueue, &tq_timer);   /* the tty layer processes it soon */
}

/* transmit path of the interrupt handler */
static void zilog_scc_tx(struct zilog_scc_info *info)
{
    struct tty_struct *tty = info->tty;

    if (info->xmit_cnt <= 0 || !tty || tty->stopped || tty->hw_stopped) {
        scc_write(info, R0, W0_RES_Tx_P);   /* nothing to send: silence the Tx interrupt */
        info->tx_active = 0;
        return;
    }
    *info->data_wr = info->xmit_buf[info->xmit_tail];
    SCC_DELAY();
    info->xmit_tail = (info->xmit_tail + 1) & (SCC_XMIT_SIZE - 1);
    info->xmit_cnt--;
    if (info->xmit_cnt < SCC_WAKEUP) {
        queue_task_irq_off(&info->tqueue, &tq_immediate);
        mark_bh(IMMEDIATE_BH);
    }
}

/*
 * SCC interrupt handler. Called for IPL2 (SCC alone) and from the IPL3 handler in config.c
 */
void zilog_scc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct zilog_scc_info *a = &scc_ports[0];
    struct zilog_scc_info *b = &scc_ports[1];
    u_char pending;
    int loops;

    if (scc_in_irq)
        return;     /* we interrupted ourselves through IPL3: the outer loop finishes the work */
    scc_in_irq = 1;

    for (loops = 0; loops < 32; loops++) {
        pending = scc_read(a, R3);          /* RR3 only exists on channel A */
        if (!pending)
            break;
        if (pending & R3_CHARxIP)  zilog_scc_rx(a);
        if (pending & R3_CHATxIP)  zilog_scc_tx(a);
        if (pending & R3_CHAEXT)   scc_write(a, R0, W0_RES_EXT_INT);
        if (pending & R3_CHBRxIP)  zilog_scc_rx(b);
        if (pending & R3_CHBTxIP)  zilog_scc_tx(b);
        if (pending & R3_CHBEXT)   scc_write(b, R0, W0_RES_EXT_INT);
        scc_write(a, R0, W0_RES_H_IUS);
    }
    scc_in_irq = 0;
}

/* start sending if the transmitter is idle. caller must have interrupts disabled */
static void zilog_scc_start_tx(struct zilog_scc_info *info)
{
    if (info->tx_active || info->xmit_cnt <= 0 || !info->tty ||
        info->tty->stopped || info->tty->hw_stopped)
        return;
    info->tx_active = 1;
    if (!(scc_read(info, R0) & R0_Tx_BUF_EMP))
        return;     /* still busy: a Tx interrupt comes when it is empty */
    /* the SCC only raises a Tx interrupt AFTER a byte was sent: send the first one here */
    *info->data_wr = info->xmit_buf[info->xmit_tail];
    SCC_DELAY();
    info->xmit_tail = (info->xmit_tail + 1) & (SCC_XMIT_SIZE - 1);
    info->xmit_cnt--;
}

static int zilog_scc_write(struct tty_struct *tty, int from_user,
                           const unsigned char *buf, int count)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long flags;
    int c, total = 0;

    if (!info || info->magic != SCC_MAGIC)
        return 0;

    save_flags(flags);
    while (count > 0) {
        cli();
        c = SCC_XMIT_SIZE - 1 - info->xmit_cnt;         /* free space */
        if (c > SCC_XMIT_SIZE - info->xmit_head)        /* stop at the wrap */
            c = SCC_XMIT_SIZE - info->xmit_head;
        if (c > count)
            c = count;
        if (c > 64)
            c = 64;         /* keep cli() short: the keyboard needs quick interrupts */
        if (c <= 0) {
            restore_flags(flags);
            break;          /* ring full: the tty layer retries after the wakeup */
        }
        if (from_user)
            memcpy_fromfs(info->xmit_buf + info->xmit_head, buf, c);
        else
            memcpy(info->xmit_buf + info->xmit_head, buf, c);
        info->xmit_head = (info->xmit_head + c) & (SCC_XMIT_SIZE - 1);
        info->xmit_cnt += c;
        zilog_scc_start_tx(info);
        restore_flags(flags);
        buf += c;
        count -= c;
        total += c;
    }
    return total;
}

static void zilog_scc_put_char(struct tty_struct *tty, unsigned char ch)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long flags;

    if (!info || info->magic != SCC_MAGIC)
        return;
    save_flags(flags); cli();
    if (info->xmit_cnt < SCC_XMIT_SIZE - 1) {
        info->xmit_buf[info->xmit_head] = ch;
        info->xmit_head = (info->xmit_head + 1) & (SCC_XMIT_SIZE - 1);
        info->xmit_cnt++;
    }
    restore_flags(flags);
}

static void zilog_scc_flush_chars(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long flags;

    if (!info || info->magic != SCC_MAGIC)
        return;
    save_flags(flags); cli();
    zilog_scc_start_tx(info);
    restore_flags(flags);
}

static int zilog_scc_write_room(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    int ret;

    if (!info || info->magic != SCC_MAGIC)
        return 0;
    ret = SCC_XMIT_SIZE - 1 - info->xmit_cnt;
    return ret < 0 ? 0 : ret;
}

static int zilog_scc_chars_in_buffer(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;

    if (!info || info->magic != SCC_MAGIC)
        return 0;
    return info->xmit_cnt;
}

static void zilog_scc_flush_buffer(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long flags;

    if (!info || info->magic != SCC_MAGIC)
        return;
    save_flags(flags); cli();
    info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
    restore_flags(flags);
    wake_up_interruptible(&tty->write_wait);
    if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
        (tty->ldisc.write_wakeup)(tty);
}

/* switches the RTS and/or DTR line on or off */
static void zilog_scc_set_lines(struct zilog_scc_info *info, u_char mask, int on)
{
    unsigned long flags;
    u_char wr5;

    save_flags(flags); cli();
    wr5 = info->wreg[R5];
    if (on)
        wr5 |= mask;
    else
        wr5 &= ~mask;
    scc_write(info, R5, wr5);
    restore_flags(flags);
}

static void zilog_scc_throttle(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;

    if (info && info->magic == SCC_MAGIC)
        zilog_scc_set_lines(info, W5_RTS, 0);
}

static void zilog_scc_unthrottle(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;

    if (info && info->magic == SCC_MAGIC)
        zilog_scc_set_lines(info, W5_RTS, 1);
}

static void zilog_scc_stop(struct tty_struct *tty)
{
    /* nothing to do: zilog_scc_tx() checks tty->stopped (Ctrl-S) */
}

static void zilog_scc_start(struct tty_struct *tty)
{
    zilog_scc_flush_chars(tty);     /* Ctrl-Q: restart sending */
}

static void zilog_scc_set_termios(struct tty_struct *tty, struct termios *old)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;

    if (!info || info->magic != SCC_MAGIC)
        return;
    if (old && tty->termios->c_cflag == old->c_cflag)
        return;
    zilog_scc_change_speed(info);
}

static int zilog_scc_get_lines(struct zilog_scc_info *info)
{
    unsigned long flags;
    u_char rr0;
    int val;

    save_flags(flags); cli();
    rr0 = scc_read(info, R0);
    restore_flags(flags);
    val = 0;
    if (info->wreg[R5] & W5_RTS) val |= TIOCM_RTS;
    if (info->wreg[R5] & W5_DTR) val |= TIOCM_DTR;
    if (rr0 & R0_DCD)            val |= TIOCM_CAR;
    if (rr0 & R0_CTS)            val |= TIOCM_CTS;
    return val;
}

static int zilog_scc_ioctl(struct tty_struct *tty, struct file *file,
                           unsigned int cmd, unsigned long arg)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long val;
    int retval;

    if (!info || info->magic != SCC_MAGIC)
        return -ENODEV;

    switch (cmd) {
    case TCSBRK:
    case TCSBRKP:
        retval = tty_check_change(tty);
        if (retval)
            return retval;
        tty_wait_until_sent(tty, 0);
        return 0;
    case TIOCMGET:
        retval = verify_area(VERIFY_WRITE, (void *)arg, sizeof(unsigned int));
        if (retval)
            return retval;
        put_fs_long(zilog_scc_get_lines(info), (unsigned long *)arg);
        return 0;
    case TIOCMBIS:
    case TIOCMBIC:
    case TIOCMSET:
        val = get_fs_long((unsigned long *)arg);
        if (cmd == TIOCMSET) {
            zilog_scc_set_lines(info, W5_RTS, val & TIOCM_RTS);
            zilog_scc_set_lines(info, W5_DTR, val & TIOCM_DTR);
        } else {
            if (val & TIOCM_RTS)
                zilog_scc_set_lines(info, W5_RTS, cmd == TIOCMBIS);
            if (val & TIOCM_DTR)
                zilog_scc_set_lines(info, W5_DTR, cmd == TIOCMBIS);
        }
        return 0;
    default:
        return -ENOIOCTLCMD;    /* the tty layer handles TCGETS/TCSETS (stty) itself */
    }
}

static int zilog_scc_block_til_ready(struct tty_struct *tty, struct file *filp, struct zilog_scc_info *info)
{
    // Is closing
    if (info->flags & ASYNC_CLOSING) {
        interruptible_sleep_on(&info->close_wait);
        return -EAGAIN; // Try again
    }

    if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
        if (info->flags & ASYNC_NORMAL_ACTIVE)
            return -EBUSY;
        info->flags |= ASYNC_CALLOUT_ACTIVE;
        return 0;
    }

    if (info->flags & ASYNC_CALLOUT_ACTIVE)
        return -EBUSY;

    info->flags |= ASYNC_NORMAL_ACTIVE;
    return 0;
}


static int zilog_scc_open(struct tty_struct *tty, struct file *filp)
{
    struct zilog_scc_info *info;
    int line, ret;

    line = MINOR(tty->device) - tty->driver.minor_start;
    if (line < 0 || line >= SCC_NPORTS)
        return -ENODEV;
    info = &scc_ports[line];

    info->tty_opened++;
    tty->driver_data = info;
    info->tty = tty;

    ret = zilog_scc_startup(info);
    return ret ? ret : zilog_scc_block_til_ready(tty, filp, info);
}

static void zilog_scc_close(struct tty_struct *tty, struct file *filp)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;
    unsigned long flags;

    if (!info || info->magic != SCC_MAGIC)
        return;
    save_flags(flags); cli();
    if (tty_hung_up_p(filp)) {
        restore_flags(flags);
        return;
    }
    if (tty->count == 1 && info->tty_opened != 1)
        info->tty_opened = 1;
    if (--info->tty_opened < 0)
        info->tty_opened = 0;
    if (info->tty_opened) {         /* still opened by someone else */
        restore_flags(flags);
        return;
    }
    info->flags |= ASYNC_CLOSING;
    restore_flags(flags);

    tty->closing = 1;
    tty_wait_until_sent(tty, 3 * HZ);   /* let the ring drain, at most 3 s */
    zilog_scc_shutdown(info);
    zilog_scc_flush_buffer(tty);
    if (tty->ldisc.flush_buffer)
        (tty->ldisc.flush_buffer)(tty);
    tty->closing = 0;
    info->tty = 0;
    info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE | ASYNC_CLOSING);
    wake_up_interruptible(&info->open_wait);
    wake_up_interruptible(&info->close_wait);
}

static void zilog_scc_hangup(struct tty_struct *tty)
{
    struct zilog_scc_info *info = (struct zilog_scc_info *)tty->driver_data;

    if (!info || info->magic != SCC_MAGIC)
        return;
    zilog_scc_flush_buffer(tty);
    zilog_scc_shutdown(info);
    info->tty_opened = 0;
    info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE);
    info->tty = 0;
    wake_up_interruptible(&info->open_wait);
}

/* zilog_scc_init: called from tty_init() in tty_io.c during system init */
int zilog_scc_init()
{
    struct zilog_scc_info *info;
    unsigned long flags;
    int i;

    memset(&scc_driver, 0, sizeof(struct tty_driver));
    scc_driver.magic = TTY_DRIVER_MAGIC;
    scc_driver.name = "ttyS";
    scc_driver.major = TTY_MAJOR;           /* 4 */
    scc_driver.minor_start = 64;            /* ttyS0 = 4,64  ttyS1 = 4,65 */
    scc_driver.num = SCC_NPORTS;
    scc_driver.type = TTY_DRIVER_TYPE_SERIAL;
    scc_driver.subtype = SERIAL_TYPE_NORMAL;
    scc_driver.init_termios = tty_std_termios;
    scc_driver.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
    scc_driver.flags = TTY_DRIVER_REAL_RAW;
    scc_driver.refcount = &scc_refcount;
    scc_driver.table = scc_table;
    scc_driver.termios = scc_termios;
    scc_driver.termios_locked = scc_termios_locked;

    scc_driver.open = zilog_scc_open;
    scc_driver.close = zilog_scc_close;
    scc_driver.write = zilog_scc_write;
    scc_driver.put_char = zilog_scc_put_char;
    scc_driver.flush_chars = zilog_scc_flush_chars;
    scc_driver.write_room = zilog_scc_write_room;
    scc_driver.chars_in_buffer = zilog_scc_chars_in_buffer;
    scc_driver.flush_buffer = zilog_scc_flush_buffer;
    scc_driver.ioctl = zilog_scc_ioctl;
    scc_driver.throttle = zilog_scc_throttle;
    scc_driver.unthrottle = zilog_scc_unthrottle;
    scc_driver.set_termios = zilog_scc_set_termios;
    scc_driver.stop = zilog_scc_stop;
    scc_driver.start = zilog_scc_start;
    scc_driver.hangup = zilog_scc_hangup;

    /* the callout device is the same except for major number and subtype */
    scc_callout_driver = scc_driver;
    scc_callout_driver.name = "cua";
    scc_callout_driver.major = TTYAUX_MAJOR;    /* 5 */
    scc_callout_driver.subtype = SERIAL_TYPE_CALLOUT;

    if (tty_register_driver(&scc_driver))
        panic("zilog_scc: couldn't register serial driver\n");
    if (tty_register_driver(&scc_callout_driver))
        panic("zilog_scc: couldn't register callout driver\n");

    save_flags(flags); cli();
    for (i = 0; i < SCC_NPORTS; i++) {
        info = &scc_ports[i];
        memset(info, 0, sizeof(*info));
        info->magic = SCC_MAGIC;
        info->line = i;
        if (i == 0) {
            info->ctl_rd  = &MAC_SCC_A_CTL_RD;
            info->ctl_wr  = &MAC_SCC_A_CTL_WR;
            info->data_rd = &MAC_SCC_A_DATA_RD;
            info->data_wr = &MAC_SCC_A_DATA_WR;
        } else {
            info->ctl_rd  = &MAC_SCC_B_CTL_RD;
            info->ctl_wr  = &MAC_SCC_B_CTL_WR;
            info->data_rd = &MAC_SCC_B_DATA_RD;
            info->data_wr = &MAC_SCC_B_DATA_WR;
        }
        info->tqueue.routine = zilog_scc_softint;
        info->tqueue.data = info;
        (void)*info->ctl_rd;        /* sync the register pointer */
        SCC_DELAY();
        scc_write(info, R1, 0);     /* no interrupts from this channel yet */
    }
    scc_write(&scc_ports[0], R9, W9_NV);    /* MIE off until a port is opened */
    restore_flags(flags);

    if (request_irq(VEC_INT2 - VEC_SPUR, zilog_scc_interrupt, IRQ_FLG_LOCK, "scc", NULL))
        panic("zilog_scc: IPL2 is already taken (old scc_interrupt still in config.c?)\n");

    for (i = 0; i < SCC_NPORTS; i++)
        printk("ttyS%d at 0x%06lx (irq 2) is a Z8530 SCC channel %c (%s)\n",
               i, (unsigned long)scc_ports[i].ctl_rd,
               i == 0 ? 'A' : 'B', i == 0 ? "modem" : "printer");
    return 0;
}
