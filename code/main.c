/**********************************************************************************************************/
//
//  Filename :          main.c
//
//  COPYRIGHT, 2015
//  The contents of this file is the sole proprietary property of, confidential
//  to, and copyrighted by Brite Company.  It may not be copied or reproduced
//  in any manner or form without the prior written consent of Brite Company.
//
//  Original Author:    Billy
//  Date created:       03, 23, 2016
//
//  Description:
//      defined main operation 
//
//  Revision:
//  Date       		Author      	Description
//  03/23/16    	Billy   		Original
//
/*************************************************************************************************************/
// including drv head files

/*************************************************************************************************************/
// macro definition
/*************************************************************************************************************/
#include "data_type.h"
#include "mem_cfg.h"
#include "mu_arch.h"

/*************************************************************************************************************/
// global variable definition
/*************************************************************************************************************/

/*************************************************************************************************************/
// function prototype
/*************************************************************************************************************/

/*************************************************************************************************************/
// function implementation
/*************************************************************************************************************/
#define BLE_BASE 0x58000000


void SystemInit()
{

}

#if 0
void ulpl_test(void)
{
	volatile struct musb_ulpi_reg *ulpi;
	uint8_t vid_l;
	uint8_t vid_h;
	volatile uint32_t* ulpi_reg= (volatile uint32_t*) (USB_CTRL_BASE_ADDR + 0x74);
	uint32_t result =0;

	//just for usb debug
	result =*ulpi_reg;
	*ulpi_reg= result|(0x00000400L);
	result =*ulpi_reg;
	*ulpi_reg= result|(0x00050000L);
	result =*ulpi_reg;

	ulpi = (volatile struct musb_ulpi_reg *)USB_ULPI_BASE_ADDR;
	musb_ulpi_init(ulpi);
	vid_l = musb_ulpi_reg_read(ulpi, ULPI_VID_L);
	vid_h = musb_ulpi_reg_read(ulpi, ULPI_VID_H);

}
#endif

#if 1
void MUSB_pll_init(void)
{
    //set controller pll to 60M, the ref clock is 16M
    volatile unsigned long *ptr;
    MGC_Write32(PLL_CTRL_BASE_ADDR, 0x2c, 0x562000f1);
    MGC_Write32(PLL_CTRL_BASE_ADDR, 0x2c, 0x522000f1);
    while((MGC_Read32(PLL_CTRL_BASE_ADDR, 0x28)&0x00000004) != 0x00000004) {
	;
    }
    /*clock selection*/
    MGC_Write32(PLL_CTRL_BASE_ADDR, 0x24, 0x5a690001);
#if 0
    //MGC_Write32(PLL_CTRL_BASE_ADDR, 0x4, (MGC_Read32(PLL_CTRL_BASE_ADDR, 0x4)|0x100));
    ptr = ((volatile unsigned long *)(PLL_CTRL_BASE_ADDR + 0x4));
    *ptr = *ptr | 0x100;
#endif

    //phy clock set to 480M
    MGC_Write32(PLL_CTRL_BASE_ADDR, 0x78, (MGC_Read32(PLL_CTRL_BASE_ADDR, 0x78)|0x82));		//10682-ok, 140-error
}
#endif

#define IOCFGAL   (*((volatile unsigned int*)(SYSCON_BASE + 0x40)))
#define PxPMS   (*((volatile unsigned int*)(0x52000000 + 0x00)))
#define PxDO   (*((volatile unsigned int*)(0x52000000 + 0x08)))

void gpio_test()
{
    IOCFGAL &= ~0xFF000000;
    PxPMS = 0x55000000;
    PxDO |= (1<<6);
    udelay(260 * 1000);   // maximum 279ms if processor clock is 60M, systick 0~0x00FFFFFF
    
    udelay(260 * 1000);
    
    udelay(260 * 1000);
    PxDO &= ~(1<<6);
    udelay(260 * 1000);
    
    udelay(260 * 1000);
    udelay(260 * 1000);
    PxDO |= (1<<6);
    mdelay(1000);
    PxDO &= ~(1<<6);
    mdelay(2000);
    PxDO |= (1<<6);
    
    mdelay(1000);
}
extern int msd_main(void);

int main(void)
{
//    int count = 0;
    //ulpl_test();
    MUSB_pll_init();
#if 0
    MGC_Write32(PLL_CTRL_BASE_ADDR, 0x78, (MGC_Read32(PLL_CTRL_BASE_ADDR, 0x78)|0x82)); //10682-ok, 140-error
    while(1) {
	count++;
	if(count == 20)
		break;
    }
#endif

//    gpio_test();

    msd_main();
    return 0;
}
