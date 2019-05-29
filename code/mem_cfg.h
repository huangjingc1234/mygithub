/**********************************************************************************************************/
//
//  Filename :          MEM_cfg.h
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
//      defined the memory layout
//
//  Revision:
//  Date       		Author      	Description
//  06/26/15    	Billy   		Original
//
/*************************************************************************************************************/
#ifndef _MEM_CFG_H_
#define _MEM_CFG_H_

/*************************************************************************************************************/
// macro definition
/*************************************************************************************************************/

// base address for each module
#define ADC_CTRL_BASE_ADDR			0x47000000
#define BLE_CTRL_BASE_ADDR			0x58000000
#define DMAC_CTRL_BASE_ADDR			0x51000000
#define EFLASH_CTRL_BASE_ADDR		0x50800000
#define GPIO_CTRL_BASE_ADDR			0x52000000
#define I2C_CTRL_BASE_ADDR			0x4c000000
#define I2S_CTRL_BASE_ADDR			0x46000000
#define PLL_CTRL_BASE_ADDR			0x50000000
#define QSPI_APB_BASE_ADDR			0x41000000
#define QSPI_AHB_BASE_ADDR			0x38000000
#define SDIO_CTRL_BASE_ADDR			0x00000000
#define SPI_CTRL_BASE_ADDR			0x45000000
#define SYS_CTRL_BASE_ADDR			0x50000000
#define TIMER_CTRL_BASE_ADDR		0xe000e000
#define UART_CTRL_BASE_ADDR			0x42000000
#define USB_CTRL_BASE_ADDR			0x51800000
#define USB_ULPI_BASE_ADDR			(USB_CTRL_BASE_ADDR+0x70)
#define WDT_CTRL_BASE_ADDR			0x00000000

#define SYSCON_BASE                 0x50000000
//***************************************************************************************************************


//***************************************************************************************************************


//***************************************************************************************************************


//***************************************************************************************************************


//***************************************************************************************************************


#endif

