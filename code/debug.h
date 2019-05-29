/**********************************************************************************************************/
//
//  Filename :          debug.h
//
//  COPYRIGHT, 2015
//  The contents of this file is the sole proprietary property of, confidential
//  to, and copyrighted by Brite Company.  It may not be copied or reproduced
//  in any manner or form without the prior written consent of Brite Company.
//
//  Original Author:    Billy
//  Date created:       06, 26, 2015
//
//  Description:
//      defined debug control items
//
//  Revision:
//  Date       		Author      	Description
//  06/26/15    	Billy   		Original
//
/*************************************************************************************************************/
#ifndef _DEBUG_H_
#define _DEBUG_H_

#include "data_type.h"

/*************************************************************************************************************/
// general debug definition
/*************************************************************************************************************/
#define ASIC_DEBUG	// suitable for FPGA debug, else for ASIC debug

/*************************************************************************************************************/
// sys debug definition
/*************************************************************************************************************/
#define CRYSTAL_CLK	(25*1000*1000)// (26*1000*1000)
#define SYS_APB_CLK	(75*1000*1000)// (78*1000*1000)

// drivers debug macro definition
#define GPIO_DEBUG
#define BLE_DEBUG
#define ADC_DEBUG
#define DAC_DEBUG

#endif

