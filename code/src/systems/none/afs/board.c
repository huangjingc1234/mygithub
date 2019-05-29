/******************************************************************
 *                                                                *
 *      Copyright Mentor Graphics Corporation 2003-2004           *
 *                                                                *
 *                All Rights Reserved.                            *
 *                                                                *
 *    THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION *
 *  WHICH IS THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS   *
 *  LICENSORS AND IS SUBJECT TO LICENSE TERMS.                    *
 *                                                                *
 ******************************************************************/

/*
 * Non-OS board-specific code for any target supported by the ARM Firmware Suite
 * $Revision: 1.21 $
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "mu_diag.h"
#include "mu_mem.h"
#include "mu_none.h"
#include "brd_cnf.h"
#include "board.h"


/* 
 * Define this to log diagnostics to a RAM buffer and upload later with your debugger, etc.
#define MUSB_MSG_BUF
 */

/***************************** TYPES ******************************/

/**
 * @field iVector uHAL's vector for reverse-lookup
 * @field iIndex uHAL's timer index
 * @field pfExpired expiration callback
 * @field pParam expiration callback parameter
 * @field dwTime remaining time, due to uHAL's MAX_PERIOD limitation
 * @field bPeriodic whether currently set for periodic
 */
typedef struct
{
    unsigned int iVector;
    unsigned int iIndex;
    MUSB_pfTimerExpired pfExpired;
    void* pParam;
    uint32_t dwTime;
    uint8_t bPeriodic;
} MGC_AfsTimerWrapper;

/**
 * MGC_AfsUds.
 * Board-specific UDS instance data.
 * @param pNonePrivateData non-OS UDS instance data
 * @param pfNoneIsr non-OS UDS ISR
 * @field aTimerWrapper timer wrappers
 * @field wTimerCount how many wrappers
 * @field bIndex our index into the global array
 */
typedef struct
{
    char aIsrName[8];
    void* pNonePrivateData;
    MUSB_NoneIsr pfNoneIsr;
    void* pPciAck;
    MGC_AfsTimerWrapper* aTimerWrapper;
    unsigned int dwIrq;
    uint16_t wTimerCount;
    uint8_t bIndex;
} MGC_AfsUds;

/*************************** FORWARDS *****************************/

static void MGC_AfsUdsIsr(unsigned int dwInterruptNumber);
static void MGC_AfsTimerExpired(unsigned int iVector);

/**************************** GLOBALS *****************************/

/** since AFS doesn't allow for instance data on ISRs or timer callbacks */
static MGC_AfsUds* MGC_apAfsUds[sizeof(MUSB_aNoneController) / sizeof(MUSB_NoneController)];

/** since AFS doesn't allow for instance data on ISRs or timer callbacks */
static uint8_t MGC_bAfsUdsCount = 0;

static uint8_t MGC_bBoardInitDone = FALSE;


#ifdef MUSB_MSG_BUF
unsigned int MGC_iMsgBufSize = 16*1024*1024;
char* MGC_pMsgBuf = NULL;
unsigned int MGC_iMsgBuf = 0;
unsigned int MGC_iWraps = 0;
#endif

/*************************** FUNCTIONS ****************************/

#ifdef MUSB_MSG_BUF
static void MGC_MsgChar(const char c)
{
    if((MGC_iMsgBuf + 3) >= MGC_iMsgBufSize)
    {
	MGC_iMsgBuf = 0;
	MGC_iWraps++;
    }
    MGC_pMsgBuf[MGC_iMsgBuf++] = c;
    MGC_pMsgBuf[MGC_iMsgBuf] = '>';
    MGC_pMsgBuf[MGC_iMsgBuf+1] = '>';
}

static void MGC_MsgString(const char* pString)
{
    size_t len = strlen(pString);
    if((MGC_iMsgBuf + len + 4) >= MGC_iMsgBufSize)
    {
	MGC_pMsgBuf[MGC_iMsgBuf] = 0;
	MGC_iMsgBuf = 0;
	MGC_iWraps++;
    }
    strcpy(&(MGC_pMsgBuf[MGC_iMsgBuf]), pString);
    MGC_iMsgBuf += len;
    MGC_pMsgBuf[MGC_iMsgBuf++] = '\r';
    MGC_pMsgBuf[MGC_iMsgBuf++] = '\n';
    MGC_pMsgBuf[MGC_iMsgBuf] = '>';
    MGC_pMsgBuf[MGC_iMsgBuf+1] = '>';
}
#endif

#if MUSB_DIAG >= 3

uint8_t MGC_NoneRead8(uint8_t* pBase, uint16_t wOffset)
{
    uint8_t bDatum = *(volatile uint8_t*)(pBase + wOffset);
#ifdef MUSB_MSG_BUF
    MUSB_DIAG2(3, "Read8(", wOffset, ") = ", bDatum, 16, 4);
#else
    printf("Read8(%lx, %04x) = %02x\n", pBase, wOffset, bDatum);
#endif
    return bDatum;
}

uint16_t MGC_NoneRead16(uint8_t* pBase, uint16_t wOffset)
{
    uint16_t wDatum = *(volatile uint16_t*)(pBase + wOffset);
#ifdef MUSB_MSG_BUF
    MUSB_DIAG2(3, "Read16(", wOffset, ") = ", wDatum, 16, 4);
#else
    printf("Read16(%lx, %04x) = %04x\n", pBase, wOffset, wDatum);
#endif
    return wDatum;
}

uint32_t MGC_NoneRead32(uint8_t* pBase, uint16_t wOffset)
{
    uint32_t dwDatum = *(volatile uint32_t*)(pBase + wOffset);
#ifdef MUSB_MSG_BUF
    MUSB_DIAG2(3, "Read32(", wOffset, ") = ", dwDatum, 16, 8);
#else
    printf("Read32(%lx, %04x) = %08x\n", pBase, wOffset, dwDatum);
#endif
    return dwDatum;
}

void MGC_NoneWrite8(uint8_t* pBase, uint16_t wOffset, uint8_t bDatum)
{
    printf("Write8(%lx, %04x, %02x)\n", pBase, wOffset, bDatum);

    *(volatile uint8_t*)(pBase + wOffset) = bDatum;
}

void MGC_NoneWrite16(uint8_t* pBase, uint16_t wOffset, uint16_t wDatum)
{

    printf("Write16(%lx, %04x, %04x)\n", pBase, wOffset, wDatum);
    *(volatile uint16_t*)(pBase + wOffset) = wDatum;
}

void MGC_NoneWrite32(uint8_t* pBase, uint16_t wOffset, uint32_t dwDatum)
{
    printf("Write32(%lx, %04x, %08x)\n", pBase, wOffset, dwDatum);
    *(volatile uint32_t*)(pBase + wOffset) = dwDatum;
}

#endif	/* MUSB_DIAG >= 3 */

char MUSB_ReadConsole()
{
    char bData;
#if 0
    bData = getchar();
    /* Echo back the data entered by user */
    putchar(bData);
    if('\r' == bData)
    {
	putchar('\n');
    }
#endif
    return bData;
}  
       
void MUSB_WriteConsole(const char bChar) 
{
   // putchar(bChar);
}

/* Reallocate memory */
void* MGC_AfsMemRealloc(void* pBlock, size_t iSize)
{
    /* no realloc */
    void* pNewBlock = MUSB_MemAlloc(iSize);
    if(pNewBlock)
    {
        MUSB_MemCopy(pNewBlock, pBlock, iSize);
        MUSB_MemFree(pBlock);
    }
    return (pNewBlock);
}

uint8_t MUSB_BoardMessageString(char* pMsg, uint16_t wBufSize, const char* pString)
{
    if((strlen(pMsg) + strlen(pString)) >= wBufSize)
    {
	return FALSE;
    }
    strcat(pMsg, pString);
    return TRUE;
}

uint8_t MUSB_BoardMessageNumber(char* pMsg, uint16_t wBufSize, uint32_t dwNumber, 
			      uint8_t bBase, uint8_t bJustification)
{
    char type;
    char format[8];
    char fmt[16];
    char number[32];

    switch(bBase)
    {
    case 8:
	type = 'i';
	break;
    case 10:
	type = 'd';
	break;
    case 16:
	type = 'x';
	break;
    default:
	return FALSE;
    }
    if(bJustification)
    {
	sprintf(format, "0%d%c", bJustification, type);
    }
    else
    {
	sprintf(format, "%c", type);
    }
    fmt[0] = '%';
    fmt[1] = (char)0;
    strcat(fmt, format);
    sprintf(number, fmt, dwNumber);

    return MUSB_BoardMessageString(pMsg, wBufSize, number);
}

uint32_t MUSB_BoardGetTime()
{
    return 0L;
}


/*usec max:267*1000*/
void udelay(unsigned long usec)
{
	unsigned int ticks_per_usec = 60;
	unsigned int ticks = ticks_per_usec * usec;
	unsigned int val = 0;
	unsigned int ctrl = 0;
	if (usec == 0)
		return;
	/*set load count and enable timer*/
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_VAL, 0);
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_LOAD, ticks);
	ctrl = MGC_Read32(TIMER_CTRL_BASE_ADDR, SYSTICK_CTRL);
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_CTRL, (ctrl|(1<<0)|(1<< 2)));

	while (!(MGC_Read32(TIMER_CTRL_BASE_ADDR, SYSTICK_CTRL) & (1<<16))) {
		val = MGC_Read32(TIMER_CTRL_BASE_ADDR, SYSTICK_VAL);
	}

	/*clear flag and disable timer*/
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_VAL, 0);
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_CTRL, 4);
	MGC_Write32(TIMER_CTRL_BASE_ADDR, SYSTICK_LOAD, 0);

}

void mdelay(unsigned long msec)
{
	unsigned long mass_mdelay_cnt;
	mass_mdelay_cnt = msec / 250;
	msec -= mass_mdelay_cnt*250;
	
	while (mass_mdelay_cnt--)
		udelay(250*1000);
	
	udelay(msec*1000);
}

static void MGC_AfsUdsIsr(unsigned int dwInterruptNumber)
{
    uint8_t bIndex;
    MGC_AfsUds* pUds;

    for(bIndex = 0; bIndex < MGC_bAfsUdsCount; bIndex++)
    {
	pUds = MGC_apAfsUds[bIndex];
	if(pUds && (dwInterruptNumber == pUds->dwIrq))
	{
#ifdef MUSB_MSG_BUF
	    MUSB_DIAG1(3, "[[[ Calling ISR for ", dwInterruptNumber, 10, 0);
#endif
	    pUds->pfNoneIsr(pUds->pNonePrivateData);
#ifdef MUSB_MSG_BUF
	    MUSB_DIAG_STRING(3, "ISR return ]]]");
#endif
	    if(pUds->pPciAck)
	    {
		*((uint32_t*)pUds->pPciAck) = 3;
	    }
	    return;
	}
    }
}

static void MGC_BoardInit()
{
    MUSB_MemSet(MGC_apAfsUds, 0, sizeof(MGC_apAfsUds));

#if 0
    /* init the MMU library */
    uHALr_InitMMU(IC_ON | DC_ON | WB_ON | EnableMMU);

    uHALr_ResetMMU();

    /* init the interrupt library */
    uHALr_InitInterrupts();

    /* init timers */
    uHALr_InitTimers();

    /* Initialize UART (default is 38400 baud for UART 0) */
    uHALr_ResetPort();
    
    printf("\n\r\n\r MUSBStack-S v2.0 on ARM Integrator\n\r");
    printf("Mentor Graphics Inventra Division \n\r");
#endif
#ifdef MUSB_MSG_BUF
    MGC_pMsgBuf = MUSB_MemAlloc(MGC_iMsgBufSize);
#endif

    MGC_bBoardInitDone = TRUE;
}


void* MUSB_BoardInitController(void* pPrivateData, MUSB_NoneIsr pfIsr,
			       const MUSB_NoneController* pControllerInfo,
			       uint8_t** ppBaseIsr, uint8_t** ppBaseBsr)
{
    MGC_AfsUds* pUds;

    if(!MGC_bBoardInitDone)
    {
	MGC_BoardInit();
    }

    pUds = (MGC_AfsUds*)MUSB_MemAlloc(sizeof(MGC_AfsUds));
    if(!pUds)
    {
	/* no memory */
	return NULL;
    }
    MUSB_MemSet(pUds, 0, sizeof(MGC_AfsUds));

    pUds->dwIrq = pControllerInfo->dwInfo;
    pUds->pNonePrivateData = pPrivateData;
    pUds->pfNoneIsr = pfIsr;

#if 0  /*configure interrupt*/
    uHALr_DisableInterrupt(pUds->dwIrq);

    /* assign the interrupt */
    strcpy(pUds->aIsrName, "MUSB-");
    pUds->aIsrName[5] = '0' + MGC_bAfsUdsCount;
    pUds->aIsrName[6] = (char)0;
    uHALr_RequestInterrupt (pUds->dwIrq, MGC_AfsUdsIsr, (unsigned char *)pUds->aIsrName);

    uHALr_EnableInterrupt(pUds->dwIrq);
#endif
    pUds->bIndex = MGC_bAfsUdsCount;
    MGC_apAfsUds[MGC_bAfsUdsCount++] = pUds;
    return pUds;
}

uint8_t MUSB_BoardInitTimers(void* pPrivateData, uint16_t wTimerCount, 
			     const uint32_t* adwTimerResolutions)
{
    int iTimerState;
    unsigned int iTimerCount;
    unsigned int iTimerIndex;
    int iIndex;
    unsigned int iTimerAvail = 0;
    MGC_AfsUds* pUds = (MGC_AfsUds*)pPrivateData;
    iTimerCount = 1;
    //iTimerCount = uHALir_CountTimers();
    if(iTimerCount < wTimerCount)
    {
	/* insufficient # timers */
	return FALSE;
    }
    pUds->aTimerWrapper = (MGC_AfsTimerWrapper*)MUSB_MemAlloc(wTimerCount * 
	sizeof(MGC_AfsTimerWrapper));
    if(!pUds->aTimerWrapper)
    {
	/* no memory */
	return FALSE;
    }
    /* ensure enough free timers */
    for(iTimerIndex = 0; 
	(iTimerAvail < wTimerCount) && (iTimerIndex < iTimerCount); 
	iTimerIndex++)
    {
	//iTimerState = uHALr_GetTimerState(iTimerIndex);
	iTimerState = 1;
	if(1 == iTimerState)
	{
	    pUds->aTimerWrapper[iTimerAvail++].iIndex = iTimerIndex;
	}
    }
    if(iTimerAvail < wTimerCount)
    {
	/* insufficient good timers */
	MUSB_MemFree(pUds->aTimerWrapper);
	return FALSE;
    }
    /* allocate timers now */
    for(iTimerIndex = 0; iTimerIndex < wTimerCount; iTimerIndex++)
    {
	//iIndex = uHALr_RequestTimer(MGC_AfsTimerExpired, (unsigned char*)"timer");
	if(iIndex >= 0)
	{
	    pUds->aTimerWrapper[iTimerIndex].iIndex = iIndex;
	    //pUds->aTimerWrapper[iTimerIndex].iVector = uHALir_GetTimerInterrupt(iIndex);
	    pUds->aTimerWrapper[iTimerIndex].pfExpired = NULL;
	    //uHALir_DisableTimer(iIndex);
	    //uHALr_InstallTimer(iIndex);
	}
	else
	{
	    /* TODO: back out */
	}
    }

    pUds->wTimerCount = wTimerCount;
    return TRUE;
}

void MUSB_BoardDestroyController(void* pPrivateData)
{
    MGC_AfsUds* pUds = (MGC_AfsUds*)pPrivateData;

    //uHALr_FreeInterrupt(pUds->dwIrq);

    /* TODO: timers? */

    MGC_apAfsUds[pUds->bIndex] = NULL;
    MUSB_MemFree(pPrivateData);
}

void MUSB_BoardRunBackground(void* pPrivateData)
{
    /* nothing to do */
}

static void MGC_AfsTimerExpired(unsigned int iVector)
{
    uint8_t bIndex;
    uint16_t wIndex;
    MGC_AfsUds* pUds;
    MGC_AfsTimerWrapper* pWrapper;
    unsigned int dwInterval;
    int status;

    /* uHAL provides us only a vector number, so we must find the system and timer */
    for(bIndex = 0; bIndex < MGC_bAfsUdsCount; bIndex++)
    {
	pUds = MGC_apAfsUds[bIndex];
	if(pUds)
	{
	    for(wIndex = 0; wIndex < pUds->wTimerCount; wIndex++)
	    {
		pWrapper = &(pUds->aTimerWrapper[wIndex]);
		if((iVector == pWrapper->iVector) && pWrapper->pfExpired)
		{
		    udelay(pWrapper->dwTime);
		    pWrapper->dwTime = 0;
		    if(pWrapper->dwTime)
		    {
			/* time remains, so continue */
			dwInterval = MUSB_MIN(pWrapper->dwTime, 500);
			pWrapper->dwTime -= dwInterval;
			//status = uHALr_SetTimerInterval(pWrapper->iIndex, dwInterval);
			if(status)
			{
			    MUSB_DIAG1(1, "SetTimerInterval returned ", status, 10, 0);
			}
			//uHALr_EnableTimer(pWrapper->iIndex);
		    }
		    else
		    {
			/* if one-shot, disable since uHAL's one-shot does NOT work */
			if(!pWrapper->bPeriodic)
			{
			    //uHALir_DisableTimer(pWrapper->iIndex);
			}
			/* callback */
			pWrapper->pfExpired(pWrapper->pParam, wIndex);
		    }
		    return;
		}
	    }
	}
    }
}

uint8_t MUSB_BoardArmTimer(void* pPrivateData, uint16_t wIndex, 
			     uint32_t dwTime, uint8_t bPeriodic, 
			     MUSB_pfTimerExpired pfExpireCallback,
			     void* pParam)
{
    int status;
    unsigned int dwInterval;
    MGC_AfsUds* pUds = (MGC_AfsUds*)pPrivateData;
    MGC_AfsTimerWrapper* pWrapper = &(pUds->aTimerWrapper[wIndex]);
    unsigned int iIndex = pWrapper->iIndex;

    pWrapper->pParam = pParam;
    pWrapper->pfExpired = pfExpireCallback;
    pWrapper->dwTime = 1 * dwTime;
    pWrapper->bPeriodic = bPeriodic;
#if 0
    /*status = uHALr_SetTimerState(iIndex, pWrapper->bPeriodic ? T_INTERVAL : T_ONESHOT);*/
    /* T_ONESHOT does NOT work */
    //status = uHALr_SetTimerState(iIndex, T_INTERVAL);
    if(status)
    {
	MUSB_DIAG1(1, "SetTimerState returned ", status, 10, 0);
    }

    dwInterval = MUSB_MIN(pWrapper->dwTime, 500);
    pWrapper->dwTime -= dwInterval;
    //status = uHALr_SetTimerInterval(iIndex, dwInterval);
    if(status)
    {
	MUSB_DIAG1(1, "SetTimerInterval returned ", status, 10, 0);
    }
    //uHALr_EnableTimer(iIndex);
#endif
    mdelay(pWrapper->dwTime);
    pWrapper->pfExpired(pWrapper->pParam, wIndex);

    return TRUE;
}

uint8_t MUSB_BoardCancelTimer(void* pPrivate, uint16_t wIndex)
{
    MGC_AfsUds* pUds = (MGC_AfsUds*)pPrivate;
    MGC_AfsTimerWrapper* pWrapper = &(pUds->aTimerWrapper[wIndex]);
    unsigned int iIndex = pWrapper->iIndex;

    //uHALir_DisableTimer(iIndex);
    pWrapper->pfExpired = NULL;

    return TRUE;
}

/*
* Controller calls this to print a diagnostic message
*/
uint8_t MUSB_BoardPrintDiag(void* pPrivate, const char* pMessage)
{
    printf("%s\n", pMessage);
    return TRUE;
}

/*
* Controller calls this to get a bus address (for DMA) from a system address
*/
void* MUSB_BoardSystemToBusAddress(void* pPrivate, const void* pSysAddr)
{
    MGC_AfsUds* pUds = (MGC_AfsUds*)pPrivate;

    if(pUds->pPciAck)
    {
	return (void*)((uint8_t*)pSysAddr + 0);
    }
    else
    {
	return (void*)((uint8_t*)pSysAddr + 0);
    }
}

