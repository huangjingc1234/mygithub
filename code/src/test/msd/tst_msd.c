/******************************************************************
*                                                                *
*        Copyright Mentor Graphics Corporation 2006              *
*                                                                *
*                All Rights Reserved.                            *
*                                                                *
*    THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION *
*  WHICH IS THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS   *
*  LICENSORS AND IS SUBJECT TO LICENSE TERMS.                    *
*                                                                *
******************************************************************/

/*
 * Command-line (really menu-driven) simple disk read/write utility,
 * to test mass-storage class driver and everything below it
 * $Revision: 1.34 $
 */

#include "mu_cdi.h"
#include "mu_mem.h"
#include "mu_stdio.h"
#include "mu_strng.h"
#include "mu_hfi.h"

#include "class/mu_msd.h"

#include "mu_mapi.h"
#ifdef MUSB_HUB
#include "mu_hapi.h"
#endif


extern MUSB_FunctionClient MGC_MsdFunctionClient;

//#define MGC_TEST_MSD_BUFFER_SIZE    (64+32)*1024
#define MGC_TEST_MSD_BUFFER_SIZE    1024

/****************************** TYPES *****************************/

/**
 * @field dwFirstBlock first block
 * @field dwErrorCount error count
 * @field wTestCount how many test passes to do
 * @field wTestIndex current test pass
 * @field wTestErrors how many tests had errors
 * @field wBlockCount how many blocks to do
 * @field wBlockIndex current block
 * @field wBufferBlocks how many blocks fit into a buffer
 * @field bWriteTest TRUE if write/read test
 * @field bWriting TRUE if writing now
 * @field bInitialDatum initial datum
 * @field bNextDatum next datum (used if crossing buffer boundaries)
 */
typedef struct
{
    uint8_t aWriteBuffer[MGC_TEST_MSD_BUFFER_SIZE];
    uint8_t aReadBuffer[MGC_TEST_MSD_BUFFER_SIZE];
    uint32_t dwFirstBlock;
    uint32_t dwErrorCount;
    uint16_t wReqBlocks;
    uint16_t wTestCount;
    uint16_t wTestIndex;
    uint16_t wTestErrors;
    uint16_t wBlockCount;
    uint16_t wBlockIndex;
    uint16_t wBufferBlocks;
    uint8_t bWriteTest;
    uint8_t bWriting;
    uint8_t bInitialDatum;
    uint8_t bNextDatum;
    char aLine[80];
    char aNumber[12];
} MGC_TestFsMsdTest;

/**
 * @field DeviceInfo HFI device info
 * @field pDevice pointer to HFI dispatch table
 * @field Test current test
 * @field bKnown TRUE if device seen already
 * @field bTesting TRUE if test in progress
 * @field bMounted TRUE if mounted
 */
typedef struct
{
    MUSB_HfiDeviceInfo DeviceInfo;
    const MUSB_HfiDevice* pDevice;
    MGC_TestFsMsdTest Test;
    uint8_t bKnown;
    uint8_t bTesting;
    uint8_t bMounted;
    uint8_t bFormatting;
    uint8_t bFormatDone;
} MGC_TestFsDevice;


/**************************** FORWARDS ****************************/

static void MGC_MsdNewOtgState(void* hClient, MUSB_BusHandle hBus, 
			       MUSB_OtgState State);
static void MGC_MsdOtgError(void* hClient, MUSB_BusHandle hBus, 
			    uint32_t dwStatus);

static void MGC_TestMsdNewBlockSet(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdNewIteration(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdNewRun(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdFillBuffer(uint8_t bInitialDatum, uint8_t* pBuffer,
				  uint32_t dwBufferSize);
static void MGC_TestMsdMountComplete(MUSB_HfiVolumeHandle hVolume, 
				     uint8_t bSuccess);
static void MGC_TestMsdTransferComplete(MUSB_HfiVolumeHandle hVolume,
					uint16_t wActualBlocks);
static void MGC_TestMsdStart(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdPrintMediumInfo(MGC_TestFsDevice* pTestFsDevice,
				       MUSB_HfiMediumInfo* pInfo);
static void MGC_TestMsdCheckTest(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdEndTest(MGC_TestFsDevice* pTestFsDevice);
static void MGC_TestMsdMediumCheckComplete(MUSB_HfiVolumeHandle hVolume);

/**************************** GLOBALS *****************************/

static uint8_t MGC_bTimingMode = TRUE;

/* current OTG state */
static MUSB_OtgState MGC_eTestMsdOtgState = MUSB_AB_IDLE;

/* UCDI variables */
static MUSB_Port* MGC_pCdiPort = NULL;
static MUSB_BusHandle MGC_hCdiBus = NULL;
static uint8_t MGC_bDesireHostRole = TRUE;

static uint8_t MGC_aMsdPeripheralList[256];

#ifdef MUSB_HUB
static MUSB_DeviceDriver MGC_aDeviceDriver[3];
#else
static MUSB_DeviceDriver MGC_aDeviceDriver[2];
#endif

static MUSB_HostClient MGC_MsdHostClient = 
{
    MGC_aMsdPeripheralList,		/* peripheral list */
    0,			    /* filled in main */
    /*sizeof(MGC_aMsdPeripheralList),*/	/* peripheral list length */
    MGC_aDeviceDriver,
    0					/* filled in main */
};

static MUSB_OtgClient MGC_MsdOtgClient = 
{
    NULL,	/* no instance data; we are singleton */
    &MGC_bDesireHostRole,
    MGC_MsdNewOtgState,
    MGC_MsdOtgError
};

/*************************** FUNCTIONS ****************************/
/* OTG client */
static void MGC_MsdNewOtgState(void* hClient, MUSB_BusHandle hBus, 
			       MUSB_OtgState State)
{
    char aAnswer[4];
    MGC_eTestMsdOtgState = State;

    switch(State)
    {
	case MUSB_AB_IDLE:
		MUSB_PrintLine("S - Start Session");
		MUSB_GetLine(aAnswer, 4);
		if(('s' == aAnswer[0]) || ('S' == aAnswer[0]))
		{
			MUSB_RequestBus(MGC_hCdiBus);
		}
		break;
	case MUSB_A_SUSPEND:
		MUSB_PrintLine("R - Resume bus");
		MUSB_GetLine(aAnswer, 4);
		if(('r' == aAnswer[0]) || ('R' == aAnswer[0]))
		{
			MUSB_ResumeBus(MGC_hCdiBus);
		}
		break;
    }
}

static void MGC_MsdOtgError(void* hClient, MUSB_BusHandle hBus, 
			    uint32_t dwStatus)
{
    switch(dwStatus)
    {
    case MUSB_STATUS_UNSUPPORTED_DEVICE:
	MUSB_PrintLine("Device not supported");
	break;
    case MUSB_STATUS_UNSUPPORTED_HUB:
	MUSB_PrintLine("Hubs not supported");
	break;
    case MUSB_STATUS_OTG_VBUS_INVALID:
	MUSB_PrintLine("Vbus error");
	break;
    case MUSB_STATUS_OTG_NO_RESPONSE:
	MUSB_PrintLine("Device not responding");
	break;
    case MUSB_STATUS_OTG_SRP_FAILED:
	MUSB_PrintLine("Device not responding (SRP failed)");
	break;
    }
}

/* display current status */
static uint8_t MGC_TestDisplayStatus(MUSB_HfiStatus Status)
{
    /* TODO */
    return TRUE;
}

/* start a new test block-set */
static void MGC_TestMsdNewBlockSet(MGC_TestFsDevice* pTestFsDevice)
{
    uint16_t wRemainingBlocks;

    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);
    wRemainingBlocks = pTest->wBlockCount - pTest->wBlockIndex;
    pTest->wReqBlocks = pTest->wBufferBlocks < wRemainingBlocks ? pTest->wBufferBlocks : wRemainingBlocks;

    if(pTest->bWriteTest && pTest->bWriting)
    {
	MGC_TestMsdFillBuffer(pTest->bInitialDatum, pTest->aWriteBuffer, 
	    MGC_TEST_MSD_BUFFER_SIZE);
	pTestFsDevice->pDevice->pfWriteDevice(pTestFsDevice->pDevice->pPrivateData,
	    pTest->dwFirstBlock + pTest->wBlockIndex, 0, pTest->wReqBlocks, pTest->aWriteBuffer, FALSE,
	    MGC_TestMsdTransferComplete, TRUE);
    }
    else
    {
	pTestFsDevice->pDevice->pfReadDevice(pTestFsDevice->pDevice->pPrivateData,
	    pTest->dwFirstBlock + pTest->wBlockIndex, 0, pTest->wReqBlocks, pTest->aReadBuffer,
	    MGC_TestMsdTransferComplete, TRUE);
    }
}

/* start a new test iteration */
static void MGC_TestMsdNewIteration(MGC_TestFsDevice* pTestFsDevice)
{
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);
    uint8_t bDatum = pTest->bInitialDatum;

    bDatum = ((bDatum & 0x80) >> 7) | (bDatum << 1);
    pTest->bInitialDatum = bDatum;
    pTest->dwErrorCount = 0L;
    pTest->wBlockIndex = 0;
    if(pTest->bWriteTest)
    {
	pTest->bWriting = TRUE;
    }
    MGC_TestMsdNewBlockSet(pTestFsDevice);
}

/* start a new test run */
static void MGC_TestMsdNewRun(MGC_TestFsDevice* pTestFsDevice)
{
    MUSB_HfiStatus Status;
    MUSB_HfiDeviceInfo* pInfo = &(pTestFsDevice->DeviceInfo);
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);

    if(!pTestFsDevice->bMounted)
    {
	Status = pTestFsDevice->pDevice->pfMountVolume(
	    pTestFsDevice->pDevice->pPrivateData, MGC_TestMsdMountComplete);
	if(MUSB_HFI_SUCCESS == Status)
	{
	    pTestFsDevice->bMounted = TRUE;
	}
	else
	{
	    MUSB_PrintLine("Test aborted due to mount error");
	    return;
	}
    }

    pTest->wTestIndex = 0;
    pTestFsDevice->bTesting = TRUE;
    pTest->wBufferBlocks = (uint16_t)(MGC_TEST_MSD_BUFFER_SIZE / pInfo->InitialMedium.dwBlockSize);
    MGC_TestMsdNewIteration(pTestFsDevice);
}

/* compare two buffers */
static uint16_t MGC_TestMsdCompareData(uint32_t dwFirstBlock, uint16_t wBlockSize,
				   const uint8_t* pRefBuffer,
				   const uint8_t* pTestBuffer, uint16_t wBufferSize)
{
    char aLine[80];
    char aNumber[12];
    uint16_t wIndex, wByteIndex;
    uint32_t dwBlock = dwFirstBlock;
    uint8_t bOk;
    uint16_t wErrors = 0;

    if(MGC_bTimingMode)
    {
	return 0;
    }

    for(wIndex = 0; wIndex < wBufferSize; wIndex += 16)
    {
	if(0 == (wIndex % wBlockSize))
	{
	    /* print block # when we hit block boundary */
	    aLine[0] = (char)0;
	    MUSB_StringConcat(aLine, 80, "Block ");
	    MUSB_Stringize(aNumber, 12, dwBlock, 16, 0);
	    MUSB_StringConcat(aLine, 80, aNumber);
	    MUSB_PrintLine(aLine);
	    dwBlock++;
	}
	/* print offset in block */
	aLine[0] = (char)0;
	MUSB_Stringize(aNumber, 12, (wIndex % wBlockSize), 16, 4);
	MUSB_StringConcat(aLine, 80, aNumber);
	MUSB_StringConcat(aLine, 80, ":");

	/* compare 16 bytes of data */
	bOk = TRUE;
	for(wByteIndex = 0; wByteIndex < 16; wByteIndex++)
	{
	    if(pTestBuffer[wIndex+wByteIndex] != pRefBuffer[wIndex+wByteIndex])
	    {
		bOk = FALSE;
		wErrors++;
	    }
	}

	if(bOk)
	{
	    MUSB_StringConcat(aLine, 80, " OK");
	    MUSB_PrintLine(aLine);
	}
	else
	{
	    /* if any problem, print expected/actual data */
	    MUSB_PrintLine(aLine);
	    aLine[0] = (char)0;
	    MUSB_StringConcat(aLine, 80, "  Expected:");
	    for(wByteIndex = 0; wByteIndex < 16; wByteIndex++)
	    {
		MUSB_StringConcat(aLine, 80, " ");
		MUSB_Stringize(aNumber, 12, pRefBuffer[wIndex+wByteIndex], 16, 2);
		MUSB_StringConcat(aLine, 80, aNumber);
	    }
	    MUSB_PrintLine(aLine);

	    aLine[0] = (char)0;
	    MUSB_StringConcat(aLine, 80, "    Actual:");
	    for(wByteIndex = 0; wByteIndex < 16; wByteIndex++)
	    {
		MUSB_StringConcat(aLine, 80, " ");
		MUSB_Stringize(aNumber, 12, pTestBuffer[wIndex+wByteIndex], 16, 2);
		MUSB_StringConcat(aLine, 80, aNumber);
	    }
	    MUSB_PrintLine(aLine);
	}
    }

    return wErrors;
}

/* display a buffer */
static void MGC_TestMsdDisplayData(uint32_t dwFirstBlock, uint16_t wBlockSize,
				   const uint8_t* pBuffer, uint16_t wBufferSize)
{
    char aLine[80];
    char aNumber[12];
    uint16_t wIndex, wByteIndex;
    uint32_t dwBlock = dwFirstBlock;

    if(MGC_bTimingMode)
    {
	return;
    }

    for(wIndex = 0; wIndex < wBufferSize; wIndex += 16)
    {
	if(0 == (wIndex % wBlockSize))
	{
	    /* print block # when we hit block boundary */
	    aLine[0] = (char)0;
	    MUSB_StringConcat(aLine, 80, "Block ");
	    MUSB_Stringize(aNumber, 12, dwBlock, 16, 0);
	    MUSB_StringConcat(aLine, 80, aNumber);
	    MUSB_PrintLine(aLine);
	    dwBlock++;
	}
	/* print offset in block */
	aLine[0] = (char)0;
	MUSB_Stringize(aNumber, 12, (wIndex % wBlockSize), 16, 4);
	MUSB_StringConcat(aLine, 80, aNumber);
	MUSB_StringConcat(aLine, 80, ":");

	/* print 16 bytes of data */
	for(wByteIndex = 0; wByteIndex < 16; wByteIndex++)
	{
	    MUSB_StringConcat(aLine, 80, " ");
	    MUSB_Stringize(aNumber, 12, pBuffer[wIndex+wByteIndex], 16, 2);
	    MUSB_StringConcat(aLine, 80, aNumber);
	}
	MUSB_PrintLine(aLine);
    }
}

/* fill a buffer */
static void MGC_TestMsdFillBuffer(uint8_t bInitialDatum, uint8_t* pBuffer,
				  uint32_t dwBufferSize)
{
    uint32_t dwIndex;
    uint8_t bDatum = bInitialDatum;

    for(dwIndex = 0; dwIndex < dwBufferSize; dwIndex++)
    {
	pBuffer[dwIndex] = bDatum;
	bDatum = ((bDatum & 0x80) >> 7) | (bDatum << 1);
    }
}

/* print a summary of one test; assumed the one just completed */
static void MGC_TestMsdPrintSummary(MGC_TestFsMsdTest* pTest)
{
    if(MGC_bTimingMode)
    {
	return;
    }

    pTest->aLine[0] = (char)0;
    MUSB_StringConcat(pTest->aLine, 80, "Test iteration ");
    MUSB_Stringize(pTest->aNumber, 12, pTest->wTestIndex, 16, 4);
    MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
    MUSB_StringConcat(pTest->aLine, 80, " complete with ");
    MUSB_Stringize(pTest->aNumber, 12, pTest->dwErrorCount, 16, 8);
    MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
    MUSB_StringConcat(pTest->aLine, 80, " errors");
    MUSB_PrintLine(pTest->aLine);
}

/* mount completion callback */
static void MGC_TestMsdMountComplete(MUSB_HfiVolumeHandle hVolume, uint8_t bSuccess)
{
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;

    if(bSuccess)
    {
	pTestFsDevice->bMounted = TRUE;
	MGC_TestMsdStart(pTestFsDevice);
    }
    else
    {
	MUSB_PrintLine("Test aborted due to mount error");
    }
}

/* transfer completion callback */
static void MGC_TestMsdTransferComplete(MUSB_HfiVolumeHandle hVolume,
					uint16_t wActualBlocks)
{
    uint16_t wCount;
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);

    if(wActualBlocks < pTest->wReqBlocks)
    {
	wCount = (pTest->wBufferBlocks < pTest->wBlockCount) ? 
	    pTest->wBufferBlocks : pTest->wBlockCount;
	pTest->aLine[0] = (char)0;
	MUSB_StringConcat(pTest->aLine, 80, "Error: expected ");
	MUSB_Stringize(pTest->aNumber, 8, wCount, 16, 4);
	MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
	MUSB_StringConcat(pTest->aLine, 80, " blocks, got ");
	MUSB_Stringize(pTest->aNumber, 8, wActualBlocks, 16, 4);
	MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
	MUSB_PrintLine(pTest->aLine);
	pTest->dwErrorCount++;
    }

    if(pTest->bWriteTest)
    {
	if(pTest->bWriting)
	{
	    pTest->bWriting = FALSE;
	    MGC_TestMsdNewBlockSet(pTestFsDevice);
	    return;
	}
	else
	{
	    /* done; compare data */
	    pTest->bWriting = TRUE;
	    pTest->dwErrorCount += MGC_TestMsdCompareData(
		pTest->dwFirstBlock + pTest->wBlockIndex,
		(uint16_t)pTestFsDevice->DeviceInfo.dwBlockSize,
		pTest->aWriteBuffer, pTest->aReadBuffer, 
		(uint16_t)(wActualBlocks * pTestFsDevice->DeviceInfo.dwBlockSize));
	}
    }
    else
    {
	/* read test only; display data */
	MGC_TestMsdDisplayData(pTest->dwFirstBlock + pTest->wBlockIndex,
	    (uint16_t)pTestFsDevice->DeviceInfo.dwBlockSize,
	    pTest->aReadBuffer, 
	    (uint16_t)(wActualBlocks * pTestFsDevice->DeviceInfo.dwBlockSize));
    }

    pTest->wBlockIndex += pTest->wBufferBlocks;
    if(pTest->wBlockIndex >= pTest->wBlockCount)
    {
	/* test iteration is complete; log summary results */
	if(pTest->bWriteTest)
	{
	    MGC_TestMsdPrintSummary(pTest);
	}
	pTest->wTestIndex++;
	if(pTest->dwErrorCount)
	{
	    pTest->wTestErrors++;
	}
	pTest->dwErrorCount = 0;
	if(pTest->wTestCount &&
	    (pTest->wTestIndex >= pTest->wTestCount))
	{
	    /* test run is complete; back to menu */
	    MGC_TestMsdStart(pTestFsDevice);
	}
	else
	{
	    /* test run is not complete; start next iteration */
	    MGC_TestMsdNewIteration(pTestFsDevice);
	}
    }
    else
    {
	/* test iteration is not complete; start next block-set */
	MGC_TestMsdNewBlockSet(pTestFsDevice);
    }
}

/* medium check completion callback */
static void MGC_TestMsdMediumCheckComplete(MUSB_HfiVolumeHandle hVolume)
{
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;
}

int test_choice = 0;

/* start a test */
static void MGC_TestMsdStart(MGC_TestFsDevice* pTestFsDevice)
{
    char aAnswer[12];
    char aNumber[4];
    char* pEnd;
    uint8_t bProgress;
    MUSB_HfiStatus Status;
    uint8_t bValid = FALSE;
    uint8_t bCanWrite = FALSE;
    uint8_t bCanRewrite = FALSE;
    MUSB_HfiDeviceInfo* pInfo = &(pTestFsDevice->DeviceInfo);
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);

    pTestFsDevice->bFormatDone = FALSE;
    switch(pInfo->InitialMedium.AccessType)
    {
    case MUSB_HFI_ACCESS_RANDOM:
	bCanWrite = TRUE;
	bCanRewrite = TRUE;
	break;
    case MUSB_HFI_ACCESS_RANDOM_WRITE_ONCE:
	bCanWrite = TRUE;
	break;
    case MUSB_HFI_ACCESS_RANDOM_READ:
	break;
    default:
	MUSB_PrintLine("Sorry, unknown media type; cannot test");
	return;
    }

    while(!bValid)
    {
	MUSB_PrintLine("");
	if(pTestFsDevice->bFormatting)
	{
	    bProgress = pTestFsDevice->pDevice->pfGetFormatProgress(
		pTestFsDevice->pDevice->pPrivateData);
	    pTest->aLine[0] = (char)0;
	    MUSB_StringConcat(pTest->aLine, 64, "Formatting ");
	    MUSB_Stringize(aNumber, 4, bProgress, 10, 3);
	    MUSB_StringConcat(pTest->aLine, 64, aNumber);
	    MUSB_StringConcat(pTest->aLine, 64, "% complete");
	    MUSB_PrintLine(pTest->aLine);
	}
	if(pTestFsDevice->bFormatDone)
	{
	    pTestFsDevice->bFormatting = FALSE;
	}

	MUSB_PrintLine("Menu:");
	MUSB_PrintLine("  S: Request Session");
	MUSB_PrintLine("  E: rElinquish Host");

	if(!pTestFsDevice->bFormatting)
	{
	    /* quit and (un)mount options */
	    MUSB_PrintLine("  Q: Ignore this device");
	    if(pTestFsDevice->bMounted)
	    {
		MUSB_PrintLine("  M: Unmount medium");
	    }
	    else
	    {
		MUSB_PrintLine("  M: Mount medium");
	    }
	}

	if(pInfo->bCanFormat && pTestFsDevice->bMounted)
	{
	    /* formatting options */
	    if(pTestFsDevice->bFormatting)
	    {
		MUSB_PrintLine("  A: Abort Formatting");
	    }
	    else
	    {
		MUSB_PrintLine("  F: Format (low-level) medium");
	    }
	}

	if(!pTestFsDevice->bFormatting)
	{
	    /* testing options */
	    if(MGC_bTimingMode)
	    {
		MUSB_PrintLine("  D: Disable timing mode (enables displays & checks)");
	    }
	    else
	    {
		MUSB_PrintLine("  B: Benchmarking mode (disables displays & checks)");
	    }
		if(pTestFsDevice->bMounted)
		{
	    MUSB_PrintLine("  R: Read blocks");
	    if(bCanWrite)
	    {
		MUSB_PrintLine("  W: Write/Read/Verify blocks");
	    }
	    if(pInfo->bHasCache)
	    {
		MUSB_PrintLine("  U: Flush device cache");
	    }
		}
	}

	MUSB_PrintString("Please enter the letter of your choice: ");
	MUSB_GetLine(aAnswer, 12);
    switch (test_choice)
    {
    case 0:
		aAnswer[0] = 'R';	/*sunny add for test */
		test_choice = 2;
		break;
	case 1:
		aAnswer[0] = 'W';	/*sunny add for test */
		test_choice = 2;
		break;
	case 2:
		aAnswer[0] = 'E';	/*sunny add for test */
		test_choice = 3;
		break;
	case 3:
		bValid = TRUE;
	    return;
	default:
		break;
    }
	if(pTestFsDevice->bMounted && (('W' == aAnswer[0]) || ('w' == aAnswer[0])) && bCanWrite)
	{
	    pTest->bWriteTest = TRUE;
	    bValid = TRUE;
	}
	else if(('B' == aAnswer[0]) || ('b' == aAnswer[0]))
	{
	    MGC_bTimingMode = TRUE;
	}
	else if(('D' == aAnswer[0]) || ('d' == aAnswer[0]))
	{
	    MGC_bTimingMode = FALSE;
	}
	else if(('S' == aAnswer[0]) || ('s' == aAnswer[0]))
	{
	    MUSB_DeactivateClient(MGC_hCdiBus);
	    MGC_hCdiBus = MUSB_RegisterOtgClient(MGC_pCdiPort, 
		&MGC_MsdFunctionClient, &MGC_MsdHostClient, &MGC_MsdOtgClient);
	}
	else if(('E' == aAnswer[0]) || ('e' == aAnswer[0]))
	{
	    MUSB_RelinquishHost(MGC_hCdiBus);
	}
	else if(pTestFsDevice->bMounted && ('R' == aAnswer[0]) || ('r' == aAnswer[0]))
	{
	    pTest->bWriteTest = FALSE;
	    bValid = TRUE;
	}
	else if(('M' == aAnswer[0]) || ('m' == aAnswer[0]))
	{
	    /* mount or unmount */
	    if(pTestFsDevice->bMounted)
	    {
		Status = pTestFsDevice->pDevice->pfUnmountVolume(
		    pTestFsDevice->pDevice->pPrivateData);
		MGC_TestDisplayStatus(Status);
		if(MUSB_HFI_SUCCESS == Status)
		{
		    pTestFsDevice->bMounted = FALSE;
		}
		else
		{
			MUSB_PrintLine("unmount failed");
		}
	    }
	    else
	    {
		Status = pTestFsDevice->pDevice->pfMountVolume(
		    pTestFsDevice->pDevice->pPrivateData, 
		    MGC_TestMsdMountComplete);
		MGC_TestDisplayStatus(Status);
	    }
	}
	else if(pTestFsDevice->bFormatting && (('A' == aAnswer[0]) || ('a' == aAnswer[0])) && pInfo->bCanFormat)
	{
	    /* abort formatting */
	    Status = pTestFsDevice->pDevice->pfAbortFormat(
		pTestFsDevice->pDevice->pPrivateData);
	    MGC_TestDisplayStatus(Status);
	    if(MUSB_HFI_SUCCESS == Status)
	    {
		pTestFsDevice->bFormatting = FALSE;
	    }
	}
	else if(pTestFsDevice->bMounted && (('F' == aAnswer[0]) || ('f' == aAnswer[0])) && pInfo->bCanFormat)
	{
	    Status = pTestFsDevice->pDevice->pfFormatMedium(
		pTestFsDevice->pDevice->pPrivateData, 512, &(pTestFsDevice->bFormatDone));
	    MGC_TestDisplayStatus(Status);
	    if(MUSB_HFI_SUCCESS == Status)
	    {
		pTestFsDevice->bFormatting = TRUE;
	    }
	}
	else if(pTestFsDevice->bMounted && (('U' == aAnswer[0]) || ('u' == aAnswer[0])) && pInfo->bHasCache)
	{
	    MGC_TestDisplayStatus(
		pTestFsDevice->pDevice->pfFlushDevice(pTestFsDevice->pDevice->pPrivateData));
	}
	else if(('Q' == aAnswer[0]) || ('q' == aAnswer[0]))
	{
	    return;
	}
    }

    bValid = FALSE;
    while(!bValid)
    {
	MUSB_PrintString("First block (hex, 32-bit, starting @0)? ");
	MUSB_GetLine(aAnswer, 12);
	strcpy(aAnswer, "0x00000000");  /*sunny add for test */
	pTest->dwFirstBlock = MUSB_StringParse(aAnswer, &pEnd, 16);
	pTest->dwFirstBlock = 0;
	*pEnd = '\0';
	if(!*pEnd)
	{
	    bValid = TRUE;
	}
    }

    bValid = FALSE;
    while(!bValid)
    {
	MUSB_PrintString("Block count (hex, 16-bit)? ");
	MUSB_GetLine(aAnswer, 12);
	strcpy(aAnswer, "0x0002");  /*sunny add for test */
	pTest->wBlockCount = (uint16_t)MUSB_StringParse(aAnswer, &pEnd, 16);
	pTest->wBlockCount = 2;
	*pEnd = '\0';
	if(!*pEnd)
	{
	    bValid = TRUE;
	}
    }

    pTest->wTestCount = 2;
    if(pTest->bWriteTest)
    {
	bValid = FALSE;
	while(!bValid)
	{
	    MUSB_PrintString("Initial datum (hex, 8-bit, rotated left each byte)? ");
	    MUSB_GetLine(aAnswer, 12);
		strcpy(aAnswer, "0x01");	/*sunny add for test */
	    pTest->bInitialDatum = (uint8_t)MUSB_StringParse(aAnswer, &pEnd, 16);
		pTest->bInitialDatum = 0x1;
		*pEnd = '\0';
	    if(!*pEnd)
	    {
		bValid = TRUE;
	    }
	}
	bValid = FALSE;
	while(!bValid)
	{
	    MUSB_PrintString("Iteration count (hex, 16-bit, 0 for infinite)? ");
	    MUSB_GetLine(aAnswer, 12);
		strcpy(aAnswer, "0x0002");	/*sunny add for test */
	    pTest->wTestCount = (uint8_t)MUSB_StringParse(aAnswer, &pEnd, 16);
		pTest->wTestCount = 2;
		*pEnd = '\0';
	    if(!*pEnd)
	    {
		bValid = TRUE;
	    }
	}
    }

    MGC_TestMsdNewRun(pTestFsDevice);
}

/* print medium info */
static void MGC_TestMsdPrintMediumInfo(MGC_TestFsDevice* pTestFsDevice,
				       MUSB_HfiMediumInfo* pInfo)
{
    char aSerial[MUSB_HFI_MAX_VOLUME_SERIAL+1];
    uint8_t bIndex;
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);

    pTest->aLine[0] = (char)0;
    MUSB_StringConcat(pTest->aLine, 80, "\tMedium is ");
    switch(pInfo->AccessType)
    {
    case MUSB_HFI_ACCESS_RANDOM:
	MUSB_StringConcat(pTest->aLine, 80, " Read/Write");
	break;
    case MUSB_HFI_ACCESS_RANDOM_WRITE_ONCE:
	MUSB_StringConcat(pTest->aLine, 80, " WORM");
	break;
    case MUSB_HFI_ACCESS_RANDOM_READ:
	MUSB_StringConcat(pTest->aLine, 80, " Read-only");
	break;
    default:
	MUSB_StringConcat(pTest->aLine, 80, " Unknown");
    }
    MUSB_StringConcat(pTest->aLine, 80, ", block size=");
    MUSB_Stringize(pTest->aNumber, 12, pInfo->dwBlockSize, 16, 8);
    MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
    MUSB_StringConcat(pTest->aLine, 80, ", count=");
    MUSB_Stringize(pTest->aNumber, 12, pInfo->dwBlockCountHi, 16, 8);
    MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
    MUSB_Stringize(pTest->aNumber, 12, pInfo->dwBlockCountLo, 16, 8);
    MUSB_StringConcat(pTest->aLine, 80, pTest->aNumber);
    MUSB_PrintLine(pTest->aLine);

    pTest->aLine[0] = (char)0;
    MUSB_StringConcat(pTest->aLine, 80, "\tSerial#: ");
    for(bIndex = 0; bIndex < (uint8_t)MUSB_HFI_MAX_VOLUME_SERIAL; bIndex++)
    {
	aSerial[bIndex] = (uint8_t)(pInfo->awSerialNumber[bIndex] & 0xff);
    }
    MUSB_StringConcat(pTest->aLine, 80, aSerial);
    MUSB_PrintLine(pTest->aLine);
}

/* check whether to start a test */
static void MGC_TestMsdCheckTest(MGC_TestFsDevice* pTestFsDevice)
{
    char aSerial[MUSB_HFI_MAX_VOLUME_SERIAL+1];
    char aNumber[32];
    char aAnswer[4];
    uint8_t bIndex;
    MUSB_HfiStatus Status;
    uint8_t bHasMedium = FALSE;
    MUSB_HfiDeviceInfo* pInfo = &(pTestFsDevice->DeviceInfo);
    MGC_TestFsMsdTest* pTest = &(pTestFsDevice->Test);

    /* if device not seen before, print device info */
    if(!pTestFsDevice->bKnown)
    {
	/* print "New Device[<addr>]: Vendor=<vvvv>/Product=<pppp>/Version=<eeee>/BlockSize=<ssssssss>" */
	pTest->aLine[0] = (char)0;
	MUSB_StringConcat(pTest->aLine, 80, "New Device[");
	MUSB_Stringize(aNumber, 31, pInfo->bBusAddress, 16, 2);
	MUSB_StringConcat(pTest->aLine, 80, aNumber);
	MUSB_StringConcat(pTest->aLine, 80, "]: Vendor=");
	MUSB_Stringize(aNumber, 31, pInfo->wVendorId, 16, 4);
	MUSB_StringConcat(pTest->aLine, 80, aNumber);
	MUSB_StringConcat(pTest->aLine, 80, "/Product=");
	MUSB_Stringize(aNumber, 31, pInfo->wProductId, 16, 4);
	MUSB_StringConcat(pTest->aLine, 80, aNumber);
	MUSB_StringConcat(pTest->aLine, 80, "/Version=");
	MUSB_Stringize(aNumber, 31, pInfo->bcdDevice, 16, 4);
	MUSB_StringConcat(pTest->aLine, 80, aNumber);
	MUSB_StringConcat(pTest->aLine, 80, "/BlockSize=");
	MUSB_Stringize(aNumber, 31, pInfo->dwBlockSize, 16, 8);
	MUSB_StringConcat(pTest->aLine, 80, aNumber);
	MUSB_PrintLine(pTest->aLine);

	pTest->aLine[0] = (char)0;
	MUSB_StringConcat(pTest->aLine, 80, "Device can:");
	if(pInfo->bCanFormat)
	{
	    MUSB_StringConcat(pTest->aLine, 80, " Format");
	}
	if(pInfo->bHasCache)
	{
	    MUSB_StringConcat(pTest->aLine, 80, " Cache");
	}
	if((1 << MUSB_HFI_ACCESS_RANDOM) & pInfo->bmAccessType)
	{
	    MUSB_StringConcat(pTest->aLine, 80, " Read/Write");
	}
	if((1 << MUSB_HFI_ACCESS_RANDOM_WRITE_ONCE) & pInfo->bmAccessType)
	{
	    MUSB_StringConcat(pTest->aLine, 80, " WORM");
	}
	if((1 << MUSB_HFI_ACCESS_RANDOM_READ) & pInfo->bmAccessType)
	{
	    MUSB_StringConcat(pTest->aLine, 80, " Read-only");
	}
	switch(pInfo->MediaType)
	{
	case MUSB_HFI_MEDIA_FIXED:
	    MUSB_StringConcat(pTest->aLine, 80, " Fixed Medium");
	    break;
	case MUSB_HFI_MEDIA_REMOVABLE:
	    MUSB_StringConcat(pTest->aLine, 80, " Removable Media");
	    break;
	}
	MUSB_PrintLine(pTest->aLine);

	pTest->aLine[0] = (char)0;
	MUSB_StringConcat(pTest->aLine, 80, "Disk Vendor: ");
	for(bIndex = 0; bIndex < (uint8_t)MUSB_HFI_MAX_DISK_VENDOR; bIndex++)
	{
	    aSerial[bIndex] = (uint8_t)(pInfo->awDiskVendor[bIndex] & 0xff);
	}
	MUSB_StringConcat(pTest->aLine, 80, aSerial);
	MUSB_StringConcat(pTest->aLine, 80, " / Product: ");
	for(bIndex = 0; bIndex < (uint8_t)MUSB_HFI_MAX_DISK_PRODUCT; bIndex++)
	{
	    aSerial[bIndex] = (uint8_t)(pInfo->awDiskProduct[bIndex] & 0xff);
	}
	MUSB_StringConcat(pTest->aLine, 80, aSerial);
	MUSB_PrintLine(pTest->aLine);

	pTest->aLine[0] = (char)0;
	MUSB_StringConcat(pTest->aLine, 80, "Device Serial#: ");
	for(bIndex = 0; bIndex < (uint8_t)MUSB_HFI_MAX_VOLUME_SERIAL; bIndex++)
	{
	    aSerial[bIndex] = (uint8_t)(pInfo->awSerialNumber[bIndex] & 0xff);
	}
	MUSB_StringConcat(pTest->aLine, 80, aSerial);
	MUSB_StringConcat(pTest->aLine, 80, " / Revision: ");
	for(bIndex = 0; bIndex < (uint8_t)MUSB_HFI_MAX_DISK_REVISION; bIndex++)
	{
	    aSerial[bIndex] = (uint8_t)(pInfo->awDiskRevision[bIndex] & 0xff);
	}
	MUSB_StringConcat(pTest->aLine, 80, aSerial);
	MUSB_PrintLine(pTest->aLine);

	pTestFsDevice->bKnown = TRUE;
    }

    /* print medium info if appropriate */
    if((MUSB_HFI_MEDIA_FIXED == pInfo->MediaType) || 
	pInfo->InitialMedium.dwBlockSize || pInfo->InitialMedium.dwBlockCountLo || 
	pInfo->InitialMedium.dwBlockCountHi)
    {
	bHasMedium = TRUE;
	MGC_TestMsdPrintMediumInfo(pTestFsDevice, &(pInfo->InitialMedium));
    }

    if(bHasMedium)
    {
	MUSB_PrintLine("*** WARNING ***");
	MUSB_PrintLine("This application knows nothing about filesystems,");
	MUSB_PrintLine("so it WILL happily overwrite disk contents!");
	MUSB_PrintLine("*** END WARNING ***");

	MUSB_PrintString("Do you wish to test this medium? [y/N] ");
        MUSB_GetLine(aAnswer, 4);
	aAnswer[0] = 'y';   /*sunny add for test */
	if(('y' == aAnswer[0]) || ('Y' == aAnswer[0]))
	{
	    Status = pTestFsDevice->pDevice->pfMountVolume(
		pTestFsDevice->pDevice->pPrivateData, MGC_TestMsdMountComplete);
	    if(MUSB_HFI_SUCCESS != Status)
	    {
		MUSB_PrintLine("Test aborted due to mount error");
		return;
	    }
	}
    }
	else
	{
		/* try a medium check */
		pTestFsDevice->pDevice->pfCheckMedium(pTestFsDevice->pDevice->pPrivateData);
	}

}

/* stop a test */
static void MGC_TestMsdEndTest(MGC_TestFsDevice* pTestFsDevice)
{
    /* TODO: stop app */
    /* TODO: unmount volume */
}

static MUSB_HfiDevice* MGC_pHfiDevice = NULL;
static const MUSB_HfiMediumInfo* MGC_pHfiMediumInfo = NULL;

static void MGC_CheckMedium()
{
    /* for now, check the last device we saw */
    if(MGC_pHfiDevice && !MGC_pHfiMediumInfo)
    {
	MGC_pHfiDevice->pfCheckMediumNotify(MGC_pHfiDevice->pPrivateData, 
		MGC_TestMsdMediumCheckComplete);
    }
}

/* Implementation */
MUSB_HfiStatus 
MUSB_HfiAddDevice(MUSB_HfiVolumeHandle* phVolume,
		  const MUSB_HfiDeviceInfo* pInfo, 
		  MUSB_HfiDevice* pDevice)
{
	MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)MUSB_MemAlloc(
		sizeof(MGC_TestFsDevice));
	MGC_pHfiDevice = pDevice;
	if(!pTestFsDevice)
	{
		return MUSB_HFI_NO_MEMORY;
	}

	MUSB_MemSet(pTestFsDevice, 0, sizeof(MGC_TestFsDevice));
	MUSB_MemCopy(&(pTestFsDevice->DeviceInfo), pInfo,
	    sizeof(MUSB_HfiDeviceInfo));
	pTestFsDevice->pDevice = pDevice;
	*phVolume = pTestFsDevice;

	MGC_TestMsdCheckTest(pTestFsDevice);
	return MUSB_HFI_SUCCESS;

}

/* Implementation */
void 
MUSB_HfiMediumInserted(MUSB_HfiVolumeHandle 	 hVolume,
		       const MUSB_HfiMediumInfo* pMediumInfo)
{
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;

    MGC_pHfiMediumInfo = pMediumInfo;
    MUSB_MemCopy(&(pTestFsDevice->DeviceInfo.InitialMedium), pMediumInfo,
		sizeof(MUSB_HfiMediumInfo));

	MGC_TestMsdCheckTest(pTestFsDevice);
}

/* Implementation */
void MUSB_HfiMediumRemoved(MUSB_HfiVolumeHandle hVolume)
{
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;

    MGC_TestMsdEndTest(pTestFsDevice);
}

/* Implementation */
void MUSB_HfiDeviceRemoved(MUSB_HfiVolumeHandle hVolume)
{
    MGC_TestFsDevice* pTestFsDevice = (MGC_TestFsDevice*)hVolume;

    MGC_TestMsdEndTest(pTestFsDevice);
}


#ifndef WIN32

/* Entrypoint */
int msd_main(void)
{
    MUSB_DeviceDriver* pDriver;
    uint8_t* pList;
    uint16_t wCount, wSize, wRemain;
    uint8_t bDriver;
    uint32_t dwCounter = 0;
    uint8_t iResult = 0;

    /* fill driver table */
    bDriver = 0;
    wSize = wCount = 0;
    wRemain = (uint16_t)sizeof(MGC_aMsdPeripheralList);
    pList = MGC_aMsdPeripheralList;
#if 0
	wSize = MUSB_FillHidPeripheralList(bDriver, pList, wRemain);
    if(wSize < wRemain)
    {
	pDriver = MUSB_GetHidClassDriver();
	if(pDriver)
	{
	    MUSB_MemCopy(&(MGC_MsdHostClient.aDeviceDriverList[bDriver]),
		pDriver, sizeof(MUSB_DeviceDriver));
	    pList += wSize;
	    wCount += wSize;
	    wRemain -= wSize;
		bDriver++;
	}
    }
#endif
	wSize = MUSB_FillMsdPeripheralList(bDriver, pList, wRemain);
    if(wSize < wRemain)
    {
	pDriver = MUSB_GetStorageClassDriver();
	if(pDriver)
	{
	    MUSB_MemCopy(&(MGC_MsdHostClient.aDeviceDriverList[bDriver]),
		pDriver, sizeof(MUSB_DeviceDriver));
	    pList += wSize;
	    wCount += wSize;
	    wRemain -= wSize;
		bDriver++;
	}
    }
#ifdef MUSB_HUB
    wSize = MUSB_FillHubPeripheralList(bDriver, pList, wRemain);
    if(wSize < wRemain)
    {
	pDriver = MUSB_GetHubClassDriver();
	if(pDriver)
	{
	    MUSB_MemCopy(&(MGC_MsdHostClient.aDeviceDriverList[bDriver]),
		pDriver, sizeof(MUSB_DeviceDriver));
	    pList += wSize;
	    wCount += wSize;
	    wRemain -= wSize;
		bDriver++;
	}
    }
#endif
    MGC_MsdHostClient.wPeripheralListLength = wCount;
    MGC_MsdHostClient.bDeviceDriverListLength = bDriver;

    if(!MUSB_InitSystem(5))
    {
	MUSB_PrintLine("Could not initialize MUSBMicroSW");
	return -1;
    }

    /* find first CDI port */
    MGC_pCdiPort = MUSB_GetPort(0);
    if(!MGC_pCdiPort)
    {
	MUSB_DestroySystem();
	MUSB_PrintLine("Could not find a CDI port");
	return -1;
    }

    /* start session */
    MGC_hCdiBus = MUSB_RegisterOtgClient(MGC_pCdiPort, 
	&MGC_MsdFunctionClient, &MGC_MsdHostClient, &MGC_MsdOtgClient);
    if(!MGC_hCdiBus)
    {
	MUSB_DestroySystem();
	MUSB_PrintLine("Could not open a CDI session");
	return -1;
    }

    while(TRUE)
    {
        /* call our ISR polling*/
        iResult = MGC_NoneControllerIsrPoll();
        MUSB_NoneRunBackground();
	if(dwCounter++ == 100000)
	{
	    dwCounter = 0;
	    MGC_CheckMedium();
	}
    }

    return 0;
}
#endif
