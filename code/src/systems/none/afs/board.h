/******************************************************************
 *                                                                *
 *        Copyright Mentor Graphics Corporation 2005              *
 *                                                                *
 *                All Rights Reserved.                            *
 *                                                                *
 *    THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION *
 *  WHICH IS THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS   *
 *  LICENSORS AND IS SUBJECT TO LICENSE TERMS.                    *
 *                                                                *
 ******************************************************************/

#ifndef __MUSB_NONE_BOARD_H__
#define __MUSB_NONE_BOARD_H__

#include "mem_cfg.h"
#include "mu_arch.h"

#define SYSTICK_CTRL    0x10
#define SYSTICK_LOAD    0x14
#define SYSTICK_VAL     0x18
#define SYSTICK_CALIB   0x1c

void udelay(unsigned long usec);
void mdelay(unsigned long usec);

/*
 * AFS-specific controller list
 * $Revision: 1.6 $
 */

MUSB_NoneController MUSB_aNoneController[] =
{
#ifdef MUSB_HDRC
    { MUSB_CONTROLLER_HDRC, (void*)USB_CTRL_BASE_ADDR, 0x0000, FALSE },
#endif
#ifdef MUSB_MHDRC
    { MUSB_CONTROLLER_MHDRC, (void*)USB_CTRL_BASE_ADDR, 0x0000, FALSE },
#endif
#ifdef MUSB_HSFC
    { MUSB_CONTROLLER_HSFC, (void*)USB_CTRL_BASE_ADDR, 0x0000, TRUE },
#endif
#ifdef MUSB_FDRC
    { MUSB_CONTROLLER_FDRC, (void*)USB_CTRL_BASE_ADDR, 9, FALSE },
#endif
};

#endif	/* multiple inclusion protection */
