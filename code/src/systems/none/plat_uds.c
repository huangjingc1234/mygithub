/******************************************************************
 *                                                                *
 *        Copyright Mentor Graphics Corporation 2004              *
 *                                                                *
 *                All Rights Reserved.                            *
 *                                                                *
 *    THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION *
 *  WHICH IS THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS   *
 *  LICENSORS AND IS SUBJECT TO LICENSE TERMS.                    *
 *                                                                *
 ******************************************************************/

/*
 * No-OS implementation of a system for Controller Driver
 * $Revision: 1.17 $
 */

#include "mu_none.h"
#include "mu_stat.h"

#include "mu_mem.h"

#include "plat_cnf.h"

#ifndef MUSB_MAX_CONTROLLERS
#define MUSB_MAX_CONTROLLERS 1
#endif

/***************************** TYPES ******************************/

/* System */
typedef struct _MGC_NoneSystem
{
    MUSB_Controller* pController;
    void* pBoardPrivateData;
    uint16_t wNextQueueItem;
    uint16_t wNextDequeueItem;
    uint8_t* pQueueData;
    MUSB_SystemServices Services;
    uint8_t bBsrRequest;
    uint8_t bBsrOver;
    uint8_t bBsrRan;
} MGC_NoneSystem;

/**************************** GLOBALS *****************************/

static MGC_NoneSystem* MGC_apNoneSystem[MUSB_MAX_CONTROLLERS];

static uint8_t MGC_bNoneSystemCount = 0;

/*************************** FUNCTIONS ****************************/

/*
* Controller calls this to enqueue a background item
*/
static uint8_t MGC_NoneQueueBackgroundItem(void* pPrivate, const void* pItem)
{
    MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivate;

    uint8_t* pQueueItem = pSystem->pQueueData + 
    	(pSystem->pController->wQueueItemSize * pSystem->wNextQueueItem++);
    MUSB_MemCopy(pQueueItem, pItem, pSystem->pController->wQueueItemSize);
    if(pSystem->wNextQueueItem >= pSystem->pController->wQueueLength)
    {
	pSystem->wNextQueueItem = 0;
    }
    return TRUE;
}

/*
* Controller calls this to dequeue a background item
*/
static uint8_t MGC_NoneDequeueBackgroundItem(void* pPrivate, void* pItem)
{
    MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivate;
    uint8_t* pQueueItem = NULL;

    if(pSystem->wNextDequeueItem != pSystem->wNextQueueItem)
    {
	pQueueItem = pSystem->pQueueData + 
	(pSystem->pController->wQueueItemSize * pSystem->wNextDequeueItem++);
    }

    if(pSystem->wNextDequeueItem >= pSystem->pController->wQueueLength)
    {
	pSystem->wNextDequeueItem = 0;
    }

    if(pQueueItem)
    {
	MUSB_MemCopy(pItem, pQueueItem, pSystem->pController->wQueueItemSize);
	return TRUE;
    }
    return FALSE;
}

/*
 * Controller calls this to flush the ISR/BSR queue
 */
static void MGC_NoneFlushBackgroundQueue(void* pPrivate)
{
    MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivate;
    pSystem->wNextQueueItem = 0;
    pSystem->wNextDequeueItem = 0;
}

/*
* Controller calls this to enter a lock
*/
static uint8_t MGC_NoneLock(void* pPrivate, uint16_t wIndex)
{
    /* 
     * No locking needed in non-concurrent environment 
     * (ISR restores any "damage" it does)
     */
    return TRUE;
}

/*
* Controller calls this to exit a lock
*/
static uint8_t MGC_NoneUnlock(void* pPrivate, uint16_t wIndex)
{
    /* 
     * No locking needed in non-concurrent environment 
     * (ISR restores any "damage" it does)
     */
    return TRUE;
}

/*
* Utilities provided to controller
*/
static MUSB_SystemUtils MGC_gNoneUtils = {
    MUSB_SYSTEM_UTILS_VERSION,
    MUSB_BoardMessageString,
    MUSB_BoardMessageNumber,
    MUSB_BoardGetTime
};

/*
 * non-OS ISR, arranged to be called by board-specific code.
 */
static uint8_t MGC_NoneControllerIsr(void* pPrivateData)
{
    int isrValue;
    volatile MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivateData;

    isrValue = pSystem->pController->pfIsr(pSystem->pController->pIsrParam);

    if(isrValue < 0)
    {
    	return FALSE;
    }
    
    if(isrValue > 0)
    {
    	/* flag for main loop */
    	pSystem->bBsrRequest++;
    	if(!pSystem->bBsrRequest)
    	{
    	    pSystem->bBsrOver++;
    	}
    }
    return TRUE;
}

/*
 * Arm a timer.
 * Call the board-specific function.
 */
uint8_t MGC_NoneArmTimer(void* pPrivateData, uint16_t wIndex, 
			 uint32_t dwTime, uint8_t bPeriodic, 
			 MUSB_pfTimerExpired pfExpireCallback)
{
    MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivateData;

    return MUSB_BoardArmTimer(pSystem->pBoardPrivateData, wIndex, dwTime,
	bPeriodic, pfExpireCallback, pSystem->pController->pPrivateData);
}

/*
 * Cancel a timer.
 * Call the board-specific function.
 */
uint8_t MGC_NoneCancelTimer(void* pPrivateData, uint16_t wIndex)
{
    MGC_NoneSystem* pSystem = (MGC_NoneSystem*)pPrivateData;

    return MUSB_BoardCancelTimer(pSystem->pBoardPrivateData, wIndex);
}

/*
 * non-OS ISR, polling interrupt register
 */
uint8_t MGC_NoneControllerIsrPoll(void)
{
    int isrValue;
    volatile MGC_NoneSystem* pSystem;
    uint8_t bController;

    for(bController = 0; bController < MGC_bNoneSystemCount; bController++) {
        pSystem = MGC_apNoneSystem[bController];
        isrValue = pSystem->pController->pfIsr(pSystem->pController->pIsrParam);
        if(isrValue < 0) {
            return FALSE;
        }

        if(isrValue > 0) {
            /* flag for main loop */
            pSystem->bBsrRequest++;
            if(!pSystem->bBsrRequest) {
                pSystem->bBsrOver++;
            }
        }
        return TRUE;
    }
}

/*
 * Implementation
 */
void MUSB_NoneRunBackground()
{
    uint8_t bBsrOver;
    volatile MGC_NoneSystem* pSystem;
    uint8_t bController;
    
    for(bController = 0; bController < MGC_bNoneSystemCount; bController++)
    {
    	pSystem = MGC_apNoneSystem[bController];
		MUSB_BoardRunBackground(pSystem->pBoardPrivateData);
    	bBsrOver = pSystem->bBsrOver;
    	if((pSystem->bBsrRequest > pSystem->bBsrRan) || bBsrOver)
    	{
    	    pSystem->bBsrRan++;
    	    pSystem->pController->pfBsr(pSystem->pController->pBsrParam);
	    if(!pSystem->bBsrRan) pSystem->bBsrOver = 0;
    	}
    }
}

static void* MGC_NoneInitController(const MUSB_NoneController* pControllerInfo)
{
    MGC_NoneSystem* pSystem;
    uint32_t dwStatus;
    uint8_t* pBaseIsr;
    uint8_t* pBaseBsr;

    pSystem = (MGC_NoneSystem*)MUSB_MemAlloc(sizeof(MGC_NoneSystem));
    if(pSystem)
    {
    	MUSB_MemSet(pSystem, 0, sizeof(MGC_NoneSystem));
    	pSystem->Services.wVersion = MUSB_SYSTEM_SERVICES_VERSION;
    	pSystem->Services.pPrivateData = (void*)pSystem;
    	pSystem->Services.pfSystemToBusAddress = MUSB_BoardSystemToBusAddress;
    	pSystem->Services.pfQueueBackgroundItem = MGC_NoneQueueBackgroundItem;
    	pSystem->Services.pfDequeueBackgroundItem = MGC_NoneDequeueBackgroundItem;
    	pSystem->Services.pfFlushBackgroundQueue = MGC_NoneFlushBackgroundQueue;
    	pSystem->Services.pfArmTimer = MGC_NoneArmTimer;
    	pSystem->Services.pfCancelTimer = MGC_NoneCancelTimer;
    	pSystem->Services.pfLock = MGC_NoneLock;
    	pSystem->Services.pfUnlock = MGC_NoneUnlock;
    	pSystem->Services.pfPrintDiag = MUSB_BoardPrintDiag;
    
	/* for structured error handling: */
	do
	{
	    /* try target-specific init */
	    pBaseIsr = pBaseBsr = pControllerInfo->pBase;
	    pSystem->pBoardPrivateData = MUSB_BoardInitController(pSystem,
		MGC_NoneControllerIsr, pControllerInfo, &pBaseIsr, &pBaseBsr);
	    if(!pSystem->pBoardPrivateData)
	    {
		MUSB_MemFree(pSystem);
		pSystem = NULL;
		break;
	    }

	    /* try UCD init */
	    pSystem->pController = MUSB_NewController(&MGC_gNoneUtils,
	        pControllerInfo->wType, pBaseIsr, pBaseBsr);
	    if(!pSystem->pController)
	    {
		MUSB_BoardDestroyController(pSystem->pBoardPrivateData);
		MUSB_MemFree(pSystem);
		pSystem = NULL;
		break;
	    }

	    /* try target-specific timer init now that we know the requirements */
	    if(!MUSB_BoardInitTimers(pSystem->pBoardPrivateData,
		pSystem->pController->wTimerCount,
		pSystem->pController->adwTimerResolutions))
	    {
		MUSB_DestroyController(pSystem->pController);
		MUSB_BoardDestroyController(pSystem->pBoardPrivateData);
		MUSB_MemFree(pSystem);
		pSystem = NULL;
		break;
	    }

	    /* prepare queue */
	    pSystem->pQueueData = MUSB_MemAlloc(
		pSystem->pController->wQueueLength *
		pSystem->pController->wQueueItemSize);
	    if(!pSystem->pQueueData)
	    {
		MUSB_DestroyController(pSystem->pController);
		MUSB_BoardDestroyController(pSystem->pBoardPrivateData);
		MUSB_MemFree(pSystem);
		pSystem = NULL;
		break;
	    }
	    MUSB_MemSet(pSystem->pQueueData, 0,
		pSystem->pController->wQueueLength *
		pSystem->pController->wQueueItemSize);

	    /* finally, start the controller */
	    dwStatus = MUSB_StartController(pSystem->pController, 
		&(pSystem->Services));
	    if(MUSB_STATUS_OK != dwStatus)
	    {
		MUSB_DestroyController(pSystem->pController);
		MUSB_BoardDestroyController(pSystem->pBoardPrivateData);
		MUSB_MemFree(pSystem->pQueueData);
		MUSB_MemFree(pSystem);
		pSystem = NULL;
		break;
	    }
	} while(FALSE);
    }
    
    return pSystem;

}

static void MGC_NoneDestroyController(MGC_NoneSystem* pSystem)
{
    /* stop controller */    
    MUSB_StopController(pSystem->pController);

    /* destroy controller */
    MUSB_DestroyController(pSystem->pController);

    /* destroy ISR/BSR queue */
    MUSB_MemFree(pSystem->pQueueData);

    MUSB_MemFree(pSystem);
}

/*
 * Startup code calls this.
 * Loop through all controllers and initialize them.
 */
uint8_t MUSB_InitSystem(unsigned long dwBsrPriority)
{
    MGC_NoneSystem* pSystem;
    uint8_t bIndex;

    MGC_bNoneSystemCount = 0;
    MUSB_MemSet(MGC_apNoneSystem, 0, sizeof(MGC_apNoneSystem));

    for(bIndex = 0; 
	bIndex < (uint8_t)MUSB_MAX_CONTROLLERS;
	bIndex++)
    {
	pSystem = MGC_NoneInitController(&(MUSB_aNoneController[bIndex]));
	if(pSystem)
	{
	    MGC_apNoneSystem[MGC_bNoneSystemCount++] = pSystem;
	}
    }
    if(MGC_bNoneSystemCount < (uint8_t)MUSB_MAX_CONTROLLERS)
    {
	MUSB_DestroySystem();
	return FALSE;
    }
    return TRUE;
}

/*
 * Loop through all initialized controller and destroy them.
 */
uint8_t MUSB_DestroySystem(void)
{
    uint8_t bIndex;

    for(bIndex = 0; bIndex < MGC_bNoneSystemCount; bIndex++)
    {
	MGC_NoneDestroyController(MGC_apNoneSystem[bIndex]);
    }
    return TRUE;
}

#ifdef MUSB_SYSTEM_TEST

/*
 * System test instrumentation
 */
 
static volatile uint8_t MGC_bRunTest = FALSE;
static const uint8_t* MGC_abInterruptCounts;

static void MGC_TestTask(void* pParam)
{
    uint8_t bController;
    uint8_t bIndex, bCount;
    MGC_NoneSystem* pSystem;
    
    while(MGC_bRunTest)
    {
    	/* for each controller... */
    	for(bController = 0; bController < MGC_bNoneSystemCount; bController++)
    	{
    	    pSystem = MGC_apNoneSystem[bController];
    	    bCount = MGC_abInterruptCounts[bController];
    	    /* for the requested # interrupts... */
    	    for(bIndex = 0; bIndex < bCount; bIndex++)
    	    {
    	    	/* call our ISR */
		MGC_NoneControllerIsr(pSystem);
    	    }
    	}
    }
}

uint8_t MUSB_SystemTestStart(const uint8_t* abInterruptCounts, 
    uint8_t bCountArrayLength)
{
    uint8_t bSuccess = FALSE;
    
    if(MGC_bNoneSystemCount == bCountArrayLength)
    {
	MGC_abInterruptCounts = abInterruptCounts;
	MGC_bRunTest = TRUE;
	/* TODO */
	MGC_TestTask(NULL);
    }
    
    return bSuccess;
}

uint8_t MUSB_SystemTestStop()
{
    MGC_bRunTest = FALSE;

    /* TODO: kill test task */

    return TRUE;
}

uint32_t MUSB_Sleep(uint32_t dwTime)
{
    /* TODO */

    return 0;
}

#endif
