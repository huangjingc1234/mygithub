/*****************************************************************************
 *                                                                           *
 *      Copyright Mentor Graphics Corporation 2006                           *
 *                                                                           *
 *                All Rights Reserved.                                       *
 *                                                                           *
 *    THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION            *
 *  WHICH IS THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS              *
 *  LICENSORS AND IS SUBJECT TO LICENSE TERMS.                               *
 *                                                                           *
 ****************************************************************************/

/*
 * SFF-8070i client functions (driver interfacing, command transfer, and
 * command preparation)
 * $Revision: 1.5 $
 */

#include "mu_cdi.h"
#include "mu_mem.h"
#include "mu_bits.h"
#include "mu_descs.h"
#include "mu_stdio.h"
#include "mu_diag.h"
#include "mu_hfi.h"

#include "mu_8070i.h"

#include "mu_mpcsi.h"

/****************************** CONSTANTS ********************************/

/* how many time to retry TEST_UNIT_READY: */
#define MGC_SFF8070I_TEST_UNIT_RETRIES	100

/* general retry count: */
#define MGC_SFF8070I_RETRIES	100

/* how many times to retry READ_CAPACITY if it fails but READ(10) of block 0 passes: */
#define MGC_SFF8070I_CAPACITY_RETRIES	400

/* how many times to retry READ_FMT_CAPACITY: */
#define MGC_SFF8070I_FMT_CAPACITY_RETRIES	2

typedef enum
{
    MGC_SFF8070I_STATE_NONE,
    /* device discovery sequence: */
    MGC_SFF8070I_STATE_INQUIRY,
    MGC_SFF8070I_STATE_READ_FMT_CAPACITY,
    MGC_SFF8070I_STATE_READ_CAPACITY,
    MGC_SFF8070I_STATE_FIRST_TEST,
    MGC_SFF8070I_STATE_READ_BLOCK0,
    MGC_SFF8070I_STATE_MODE_SENSE_EXCEPTIONS,
    MGC_SFF8070I_STATE_MODE_SENSE_ALL,
    MGC_SFF8070I_STATE_TEST_UNIT_READY,
    /* mount sequence: */
    MGC_SFF8070I_STATE_PREVENT_MEDIUM_REMOVE,
    /* unmount sequence: */
    MGC_SFF8070I_STATE_ALLOW_MEDIUM_REMOVE,
    MGC_SFF8070I_STATE_STOP_UNIT
} MGC_Sff8070iState;

/******************************** TYPES **********************************/

typedef struct
{
    MGC_MsdProtocol* pProtocol;
    MUSB_pfHfiMountComplete pfMountComplete;
    void* pMountCompleteParam;
    uint8_t aBlock0[512];
    uint8_t aCmd[16];
    uint8_t aInquiryData[36];
    uint8_t aSenseData[20];
    uint8_t aFormatCapacityData[0xfc];
    uint8_t aCapacityData[8];
    uint8_t aData[64];
    uint32_t dwRetries;
    uint32_t dwCapacityRetries;
    uint8_t bFmtCapacityRetries;
    MGC_Sff8070iState bState;
    uint8_t bError;
    uint8_t bRemovable;
    uint8_t bAnnounced;
    uint8_t bLun;
    uint8_t bLunCount;
    uint8_t bLunScan;
    uint8_t bLunIndex;
    uint8_t bNoMedium;
} MGC_Sff8070iCmdSetData;

/******************************* FORWARDS ********************************/

static void* MGC_Sff8070iCmdSetCreateInstance(uint8_t bLunCount);

static void MGC_Sff8070iCmdSetDestroyInstance(void* pInstance);

static uint8_t MGC_Sff8070iCmdSetDiscoverDevice(void* pInstance, 
					    MGC_MsdProtocol* pProtocol,
					    uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetMountDevice(void* pInstance, 
					 MGC_pfMsdMountComplete pfMountComplete,
					 void* pPrivateData,
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetUnmountDevice(void* pInstance, 
					   MGC_MsdProtocol* pProtocol,
					   uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetGetReadCmd(void* pInstance, 
					uint8_t* pCmdBuffer,
					uint8_t bMaxCmdLength,
					uint32_t dwBlockLo,
					uint32_t dwBlockHi,
					uint16_t wBlockCount,
					uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetGetWriteCmd(void* pInstance, 
					 uint8_t* pCmdBuffer,
					 uint8_t bMaxCmdLength,
					 uint32_t dwBlockLo,
					 uint32_t dwBlockHi,
					 uint16_t wBlockCount,
					 uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetFlushDevice(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetFormatDevice(void* pInstance, 
					  MGC_MsdProtocol* pProtocol,
					  uint32_t dwBlockSize,
					  uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetAbortFormat(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun);

static void MGC_Sff8070iCmdSetParseInterrupt(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 const uint8_t* pMessage,
					 uint16_t wMessageLength,
					 uint8_t bLun);

static uint8_t MGC_Sff8070iCmdSetCheckMedium(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun);

/******************************* GLOBALS *********************************/

static MGC_MsdCmdSet MGC_Sff8070iCmdSet =
{
    MGC_Sff8070iCmdSetCreateInstance,
    MGC_Sff8070iCmdSetDestroyInstance,
    MGC_Sff8070iCmdSetDiscoverDevice,
    MGC_Sff8070iCmdSetMountDevice,
    MGC_Sff8070iCmdSetUnmountDevice,
    MGC_Sff8070iCmdSetGetReadCmd,
    MGC_Sff8070iCmdSetGetWriteCmd,
    MGC_Sff8070iCmdSetFlushDevice,
    MGC_Sff8070iCmdSetFormatDevice,
    MGC_Sff8070iCmdSetAbortFormat,
    MGC_Sff8070iCmdSetParseInterrupt,
    MGC_Sff8070iCmdSetCheckMedium
};

/****************************** FUNCTIONS ********************************/

MGC_MsdCmdSet* MGC_GetSff8070iCmdSet()
{
    return &MGC_Sff8070iCmdSet;
}

static void* MGC_Sff8070iCmdSetCreateInstance(uint8_t bLunCount)
{
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)MUSB_MemAlloc(
	sizeof(MGC_Sff8070iCmdSetData));
    if(pSff8070i)
    {
	MUSB_MemSet(pSff8070i, 0, sizeof(MGC_Sff8070iCmdSetData));
	pSff8070i->bLunCount = bLunCount;
    }
    return pSff8070i;
}

static void MGC_Sff8070iCmdSetDestroyInstance(void* pInstance)
{
    MUSB_MemFree(pInstance);
}

static void MGC_Sff8070iCmdComplete(void* pPrivateData,
				const uint8_t* pDataBuffer,
				uint32_t dwDataLength,
				uint8_t bUsbError,
				uint8_t bWrapperStatus,
				const uint8_t* pStatusBuffer,
				uint16_t wStatusLength)
{
    MUSB_HfiDeviceInfo DeviceInfo;
    MUSB_HfiMediumInfo MediumInfo;
    MUSB_HfiAccessType AccessType;
    uint8_t bIndex, bCount;
    uint32_t dwBlockCount;
    const MGC_Sff8070iInquiryData* pInquiry = NULL;
    uint8_t bSend = FALSE;
    uint8_t bMediumChange = FALSE;
    uint8_t bAnnounce = FALSE;
    uint8_t bOpcode = 0;
    uint8_t bCmdLength = 0;
    uint32_t dwLength = 0;
    uint8_t* pBuffer = NULL;
    uint8_t bSuccess = FALSE;
    uint8_t bReady = FALSE;
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pPrivateData;
    uint8_t bError = pSff8070i->bError;
    MGC_MsdProtocol* pProtocol = pSff8070i->pProtocol;
    uint8_t bLun = pSff8070i->bLun;

    /* prepare for next command */
    MUSB_MemSet(pSff8070i->aCmd, 0, 16);

    if(pSff8070i->bError)
    {
	pSff8070i->bError = FALSE;
	if(!bWrapperStatus)
	{
	    /* analyze sense codes */
	    if(MGC_SFF8070I_SC_NOT_READY == (pSff8070i->aSenseData[2] & 0xf) &&
		(MGC_SFF8070I_ASC_UNIT_NOT_READY == pSff8070i->aSenseData[12]))
	    {
		pSff8070i->bRemovable = TRUE;
		if(2 == pSff8070i->aSenseData[13])
		{
		    /* needs a start unit */
		    bCmdLength = 12;
		    bOpcode = MGC_SFF8070I_START_STOP_UNIT;
		    pSff8070i->aCmd[4] = 3;
		}
		else
		{
		    bAnnounce = TRUE;
		}
	    }
	    else if(MGC_SFF8070I_SC_UNIT_ATTENTION == (pSff8070i->aSenseData[2] & 0xf) &&
		(MGC_SFF8070I_ASC_MEDIUM_CHANGE == pSff8070i->aSenseData[12]))
	    {
		bMediumChange = TRUE;
	    }
	    else if(MGC_SFF8070I_SC_UNIT_ATTENTION == (pSff8070i->aSenseData[2] & 0xf) &&
		((MGC_SFF8070I_ASC_MEDIUM_NOT_PRESENT == pSff8070i->aSenseData[12]) ||
		(MGC_SFF8070I_ASC_MEDIUM_ERROR == pSff8070i->aSenseData[12])))
	    {
		pSff8070i->bNoMedium = TRUE;
	    }
	}
    }
    if(bWrapperStatus)
    {
	/* command failed: find out why */
	pSff8070i->bError = TRUE;

	/* send a REQUEST_SENSE */
	bCmdLength = 12;
	bOpcode = MGC_SFF8070I_REQUEST_SENSE;
	pBuffer = pSff8070i->aSenseData;
	dwLength = 20;
	pSff8070i->aCmd[0] = bOpcode;
	pSff8070i->aCmd[4] = (uint8_t)(dwLength & 0xff);
	bSuccess = pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
	    pSff8070i->aCmd, bCmdLength, pBuffer, dwLength, FALSE, 
	    MGC_Sff8070iCmdComplete, FALSE, 2);
	return;
    }

    /* process result of last action and setup next one */
    switch(pSff8070i->bState)
    {
    case MGC_SFF8070I_STATE_INQUIRY:
	MUSB_DIAG_STRING(2, "MSD: Standard Inquiry complete");
	/* many devices do not support the serial # inquiry, and some hang if it is sent */
	/* read format capacity */
	bCmdLength = 12;
	bOpcode = MGC_SFF8070I_RD_FMT_CAPC;
	pBuffer = pSff8070i->aFormatCapacityData;
	dwLength = sizeof(pSff8070i->aFormatCapacityData);
	pSff8070i->bState = MGC_SFF8070I_STATE_READ_FMT_CAPACITY;
	/* KLUDGE for certain devices */
	if(pSff8070i->bLunScan)
	{
	    bLun = pSff8070i->bLunIndex;
	}
	break;

    case MGC_SFF8070I_STATE_READ_FMT_CAPACITY:
	MUSB_DIAG_STRING(2, "MSD: Read Format Capacity complete");
	/* if error, re-zero data for check later */
	if(bError || bUsbError)
	{
	    MUSB_DIAG_STRING(2, "MSD: Read Format Capacity failed");
	    MUSB_MemSet(pSff8070i->aFormatCapacityData, 0, 
		sizeof(pSff8070i->aFormatCapacityData));
	    pSff8070i->bError = FALSE;
	}
	/* retry this command if needed, up to given count */
	if((bError || bUsbError) && (pSff8070i->bFmtCapacityRetries < MGC_SFF8070I_FMT_CAPACITY_RETRIES))
	{
	    pSff8070i->bFmtCapacityRetries++;
	    /* read format capacity again */
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_RD_FMT_CAPC;
	    pBuffer = pSff8070i->aFormatCapacityData;
	    dwLength = sizeof(pSff8070i->aFormatCapacityData);
	    pSff8070i->bState = MGC_SFF8070I_STATE_READ_FMT_CAPACITY;
	}
	else if(pSff8070i->bLunScan)
	{
	    /* KLUDGE for certain devices: do INQUIRY/READ_FMT_CAP on each LUN, then back to LUN 0 */
	    pSff8070i->bState = MGC_SFF8070I_STATE_INQUIRY;
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_INQUIRY;
	    pBuffer = pSff8070i->aInquiryData;
	    dwLength = 36;
	    if(pSff8070i->bLunIndex < (pSff8070i->bLunCount - 1))
	    {
		bLun = ++pSff8070i->bLunIndex;
	    }
	    else
	    {
		pSff8070i->bLunScan = FALSE;
	    }
	}
	else
	{
	    /* the normal next step: read capacity */
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_READ_CAPACITY;
	    pBuffer = pSff8070i->aCapacityData;
	    dwLength = sizeof(pSff8070i->aCapacityData);
	    pSff8070i->bState = MGC_SFF8070I_STATE_READ_CAPACITY;
	}
	break;

    case MGC_SFF8070I_STATE_READ_CAPACITY:
	MUSB_DIAG_STRING(2, "MSD: Read Capacity complete");
	/* if error, re-zero data for check later */
	if(bError || bUsbError)
	{
	    MUSB_DIAG_STRING(2, "MSD: Read Capacity failed");
	    MUSB_MemSet(pSff8070i->aCapacityData, 0,
		sizeof(pSff8070i->aCapacityData));
	}
	if(bMediumChange)
	{
	    /* if medium changed, re-read capacity */
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_READ_CAPACITY;
	    pBuffer = pSff8070i->aCapacityData;
	    dwLength = sizeof(pSff8070i->aCapacityData);
	    pSff8070i->bState = MGC_SFF8070I_STATE_READ_CAPACITY;
	}
	else if(pSff8070i->bNoMedium || bUsbError)
	{
	    /* skip read if definitive no-medium or stall from device */
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
	    pSff8070i->bState = MGC_SFF8070I_STATE_TEST_UNIT_READY;
	}
	else
	{
	    /* test unit ready */
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
	    pSff8070i->bState = MGC_SFF8070I_STATE_FIRST_TEST;
	}
	break;

    case MGC_SFF8070I_STATE_FIRST_TEST:
	if(bError || bUsbError)
	{
	    /* retry if needed */
	    if(pSff8070i->dwRetries < MGC_SFF8070I_TEST_UNIT_RETRIES)
	    {
		pSff8070i->dwRetries++;
		pSff8070i->bState = MGC_SFF8070I_STATE_FIRST_TEST;
	    }
	    else
	    {
		/* retry count exhausted; skip read of block 0 */
		pSff8070i->dwRetries = 0;
		pSff8070i->bState = MGC_SFF8070I_STATE_TEST_UNIT_READY;
	    }
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
	}
	else if(pSff8070i->aCapacityData[0] || pSff8070i->aCapacityData[1] ||
	    pSff8070i->aCapacityData[2] || pSff8070i->aCapacityData[3])
	{
	    /* if ready and we got good capacity data, we're done */
	    bAnnounce = TRUE;
	    bReady = TRUE;
	    pSff8070i->bState = MGC_SFF8070I_STATE_TEST_UNIT_READY;
	}
	else
	{
		pSff8070i->dwRetries = 0;
		pSff8070i->bState = MGC_SFF8070I_STATE_TEST_UNIT_READY;
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
#if 0
	    /* read block 0, as another way to check for medium present */
	    pSff8070i->dwRetries = 0;
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_READ10;
	    pBuffer = pSff8070i->aBlock0;
	    /* fool code below */
	    dwLength = 0;
	    MGC_Sff8070iCmdSetGetReadCmd(pSff8070i, pSff8070i->aCmd, 16, 0, 0, 1, bLun);
	    pSff8070i->bState = MGC_SFF8070I_STATE_READ_BLOCK0;
#endif
	}
	break;

    case MGC_SFF8070I_STATE_READ_BLOCK0:
	MUSB_DIAG_STRING(2, "MSD: Read block 0 complete");
	if(bError)
	{
	    pSff8070i->bRemovable = TRUE;
	}
	else if((pSff8070i->dwCapacityRetries < MGC_SFF8070I_CAPACITY_RETRIES) &&
	    !pSff8070i->aCapacityData[0] && !pSff8070i->aCapacityData[1] &&
	    !pSff8070i->aCapacityData[2] && !pSff8070i->aCapacityData[3])
	{
	    /* if read capacity failed but read passed, try read capacity again */
	    pSff8070i->dwCapacityRetries++;
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_READ_CAPACITY;
	    pBuffer = pSff8070i->aCapacityData;
	    dwLength = sizeof(pSff8070i->aCapacityData);
	    pSff8070i->bState = MGC_SFF8070I_STATE_READ_CAPACITY;
	    break;
	}
	/* test unit ready */
	bCmdLength = 12;
	bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
	pSff8070i->bState = MGC_SFF8070I_STATE_TEST_UNIT_READY;
	break;

    case MGC_SFF8070I_STATE_TEST_UNIT_READY:
	MUSB_DIAG_STRING(2, "MSD: Test Unit Ready complete");
	/* if ready, done; otherwise try again */
	if(!bError)
	{
	    bAnnounce = TRUE;
	    bReady = pSff8070i->bNoMedium ? FALSE : TRUE;
	}
	else if(pSff8070i->dwRetries > MGC_SFF8070I_TEST_UNIT_RETRIES)
	{
	    /* retries exhausted; assume removable medium */
	    pSff8070i->dwRetries = 0;
	    pSff8070i->bRemovable = TRUE;
	    bAnnounce = TRUE;
	}
	else
	{
	    pSff8070i->dwRetries++;
	    bCmdLength = 12;
	    bOpcode = MGC_SFF8070I_TEST_UNIT_READY;
	}
	break;

    case MGC_SFF8070I_STATE_PREVENT_MEDIUM_REMOVE:
	/* inform client */
	pSff8070i->pfMountComplete(pSff8070i->pMountCompleteParam, TRUE);
	return;

    case MGC_SFF8070I_STATE_ALLOW_MEDIUM_REMOVE:
	/* stop unit */
	bCmdLength = 12;
	bOpcode = MGC_SFF8070I_START_STOP_UNIT;
	pSff8070i->bState = MGC_SFF8070I_STATE_STOP_UNIT;
	break;

    case MGC_SFF8070I_STATE_STOP_UNIT:
	/* done */
	return;

    default:
	MUSB_DIAG1(1, "MSD/SFF-8070i: Internal error; bad state ", pSff8070i->bState,
	    16, 2);
	return;
    }

    /* set common fields */
    pSff8070i->aCmd[0] = bOpcode;
    switch(bCmdLength)
    {
    case 12:
	pSff8070i->aCmd[9] = (uint8_t)(dwLength & 0xff);
	break;
    }

    if(bAnnounce)
    {
	/* ready to announce volume */

	/* fill DeviceInfo */
	MUSB_MemSet(&DeviceInfo, 0, sizeof(DeviceInfo));
	switch(pSff8070i->aInquiryData[0] & 0x1f)
	{
	case MGC_SFF8070I_DEVICE_TYPE_DIRECT:
	    AccessType = MUSB_HFI_ACCESS_RANDOM;
	    DeviceInfo.bmAccessType = 1 << AccessType;
	    /* believe the RMB bit (this is a problem on some devices!) */
	    pSff8070i->bRemovable = (pSff8070i->aInquiryData[1] & MGC_M_SFF8070I_INQUIRY_RMB) ? TRUE : FALSE;
	    break;
	case MGC_SFF8070I_DEVICE_TYPE_SEQUENTIAL:
	case MGC_SFF8070I_DEVICE_TYPE_WORM:
	case MGC_SFF8070I_DEVICE_TYPE_OPTICAL:
	    AccessType = MUSB_HFI_ACCESS_RANDOM_WRITE_ONCE;
	    DeviceInfo.bmAccessType = 1 << AccessType;
	    /* correct removability in case we didn't infer it right */
	    pSff8070i->bRemovable = TRUE;
	    break;
	case MGC_SFF8070I_DEVICE_TYPE_CDROM:
	    AccessType = MUSB_HFI_ACCESS_RANDOM_READ;
	    DeviceInfo.bmAccessType = 1 << AccessType;
	    /* correct removability in case we didn't infer it right */
	    pSff8070i->bRemovable = TRUE;
	    break;
	default:
	    DeviceInfo.bmAccessType = 0;
	}
	DeviceInfo.MediaType = pSff8070i->bRemovable ? MUSB_HFI_MEDIA_REMOVABLE : MUSB_HFI_MEDIA_FIXED;

	/* TODO: set this correctly */
	DeviceInfo.bCanFormat = FALSE;
	/* TODO: set this correctly */
	DeviceInfo.bHasCache = FALSE;

	DeviceInfo.dwBlockSize = ((uint32_t)pSff8070i->aCapacityData[4] << 24) | 
	    ((uint32_t)pSff8070i->aCapacityData[5] << 16) | ((uint32_t)pSff8070i->aCapacityData[6] << 8) | 
	    pSff8070i->aCapacityData[7];
	if(!DeviceInfo.dwBlockSize)
	{
	    DeviceInfo.dwBlockSize = ((uint32_t)pSff8070i->aFormatCapacityData[10] << 8) |
		pSff8070i->aFormatCapacityData[11];
	}
	if(!DeviceInfo.dwBlockSize)
	{
	    DeviceInfo.dwBlockSize = 512;
	}

	/* generate awDiskVendor, awDiskProduct, awDiskRevision */
	pInquiry = (const MGC_Sff8070iInquiryData*)pSff8070i->aInquiryData;
	bCount = (uint8_t)MUSB_MIN(MUSB_HFI_MAX_DISK_VENDOR, 8);
	for(bIndex = 0; bIndex < bCount; bIndex++)
	{
	    DeviceInfo.awDiskVendor[bIndex] = pInquiry->aVendorId[bIndex];
	}
	DeviceInfo.awDiskVendor[bIndex] = 0;
	bCount = (uint8_t)MUSB_MIN(MUSB_HFI_MAX_DISK_PRODUCT, 8);
	for(bIndex = 0; bIndex < bCount; bIndex++)
	{
	    DeviceInfo.awDiskProduct[bIndex] = pInquiry->aProductId[bIndex];
	}
	DeviceInfo.awDiskProduct[bIndex] = 0;
	bCount = (uint8_t)MUSB_MIN(MUSB_HFI_MAX_DISK_REVISION, 8);
	for(bIndex = 0; bIndex < bCount; bIndex++)
	{
	    DeviceInfo.awDiskRevision[bIndex] = pInquiry->aProductRev[bIndex];
	}
	DeviceInfo.awDiskRevision[bIndex] = 0;

	/* generate dwBlockCount from READ_CAPACITY response */
	dwBlockCount = ((uint32_t)pSff8070i->aCapacityData[0] << 24) | 
	    ((uint32_t)pSff8070i->aCapacityData[1] << 16) | 
	    ((uint32_t)pSff8070i->aCapacityData[2] << 8) | 
	    pSff8070i->aCapacityData[3];
	if(!dwBlockCount)
	{
	    bReady = FALSE;
	}

	if(bReady)
	{
	    /* fill InitialMedium if medium is present */
	    DeviceInfo.InitialMedium.AccessType = AccessType;
	    DeviceInfo.InitialMedium.dwBlockSize = DeviceInfo.dwBlockSize;
	    DeviceInfo.InitialMedium.dwBlockCountLo = dwBlockCount;
	    DeviceInfo.InitialMedium.dwBlockCountHi = 0;
	    DeviceInfo.InitialMedium.awSerialNumber[0] = 0;
	}

	/* set device info */
	pSff8070i->pProtocol->pfSetDeviceInfo(pSff8070i->pProtocol->pProtocolData,
	    pSff8070i->bLun, &DeviceInfo);

	if(pSff8070i->bRemovable && bReady)
	{
	    /* prepare & set medium info */
	    MediumInfo.AccessType = AccessType;
	    MediumInfo.dwBlockSize = DeviceInfo.dwBlockSize;
	    MediumInfo.dwBlockCountLo = dwBlockCount;
	    MediumInfo.dwBlockCountHi = 0;
	    MediumInfo.awSerialNumber[0] = 0;
	    pSff8070i->pProtocol->pfSetMediumInfo(pSff8070i->pProtocol->pProtocolData,
		pSff8070i->bLun, &MediumInfo);
	}

	if(pSff8070i->bAnnounced)
	{
	    /* if already announced, we are updating medium status */
	    pSff8070i->pProtocol->pfSetReady(pSff8070i->pProtocol->pProtocolData, 
		pSff8070i->bLun, bReady);
	}
	else if ( pSff8070i->bLun == pSff8070i->bLunCount - 1 )
	{
	    /* not yet announced and all LUNs discovered, so announce all of them now */
	    pSff8070i->bAnnounced = TRUE;
	    
	    for (bLun = 0; bLun < pSff8070i->bLunCount; bLun++)
		{
	        pSff8070i->pProtocol->pfSetReady(pSff8070i->pProtocol->pProtocolData, bLun, TRUE);
	    }
	}
	else 
	{
	    pSff8070i->bAnnounced = TRUE; /* for each LUN < LunCount-1 mark as announced but do it only together with the last LUN */
	}
    }
    else
    {
	/* not announcing yet; send next command */
	bSuccess = pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
	    pSff8070i->aCmd, bCmdLength, pBuffer, dwLength, bSend, 
	    MGC_Sff8070iCmdComplete, FALSE, 2);

	if(!bSuccess)
	{
	    MUSB_DIAG_STRING(1, "MSD/SFF-8070i: Protocol SendCmd failed");
	}
    }
}

/*
 * Determine device characteristics with standard inquiry,
 * VPD S/N page, etc. and wait until device is ready
 */
static uint8_t MGC_Sff8070iCmdSetDiscoverDevice(void* pInstance,
					    MGC_MsdProtocol* pProtocol,
					    uint8_t bLun)
{
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;

    pSff8070i->pProtocol = pProtocol;
    pSff8070i->bLun = bLun;

    /* clear retry counters etc. */
    pSff8070i->dwRetries = 0;
    pSff8070i->dwCapacityRetries = 0;
    pSff8070i->bFmtCapacityRetries = 0;
    pSff8070i->bRemovable = FALSE;
    pSff8070i->bAnnounced = FALSE;
    pSff8070i->bNoMedium = FALSE;

    /* start things by sending INQUIRY command */
    pSff8070i->bState = MGC_SFF8070I_STATE_INQUIRY;
    MUSB_MemSet(pSff8070i->aCmd, 0, 16);
    pSff8070i->aCmd[0] = MGC_SFF8070I_INQUIRY;
    pSff8070i->aCmd[4] = 36;
    /* special handling for multi-LUN devices */
    if(!bLun && (pSff8070i->bLunCount > 1))
    {
	pSff8070i->bLunScan = TRUE;
	pSff8070i->bLunIndex = 0;
    }
    else
    {
	pSff8070i->bLunScan = FALSE;
    }
    return pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
	pSff8070i->aCmd, 12, pSff8070i->aInquiryData, 36, FALSE, MGC_Sff8070iCmdComplete, 
	FALSE, 0);
}

/*
 * FS wants to mount the device, so start unit & attempt to prevent medium removal
 */
static uint8_t MGC_Sff8070iCmdSetMountDevice(void* pInstance,
					 MGC_pfMsdMountComplete pfMountComplete,
					 void* pPrivateData,
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun)
{
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;
    MGC_Sff8070iInquiryData* pInquiryData = (MGC_Sff8070iInquiryData*)pSff8070i->aInquiryData;

    /* prevent medium removal to the maximum extent possible */
    if(pInquiryData->bRmb & MGC_M_SFF8070I_INQUIRY_RMB)
    {
	    pSff8070i->pfMountComplete = pfMountComplete;
	    pSff8070i->pMountCompleteParam = pPrivateData;
	    pSff8070i->bState = MGC_SFF8070I_STATE_PREVENT_MEDIUM_REMOVE;
	    MUSB_MemSet(pSff8070i->aCmd, 0, 16);
	    pSff8070i->aCmd[0] = MGC_SFF8070I_PREVENT_ALLOW_MED_REMOVE;
		pSff8070i->aCmd[1] = bLun << MGC_S_SFF8070I_INQUIRY_LUN;
	    pSff8070i->aCmd[4] = 1;
	    return pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
		pSff8070i->aCmd, 12, NULL, 0, TRUE, MGC_Sff8070iCmdComplete, FALSE, 2);
    }
    pfMountComplete(pPrivateData, TRUE);
    return TRUE;
}

/*
 * FS wants to unmount device, so allow medium removal & stop unit
 */
static uint8_t MGC_Sff8070iCmdSetUnmountDevice(void* pInstance,
					   MGC_MsdProtocol* pProtocol,
					   uint8_t bLun)
{
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;

    /* allow medium removal */
    pSff8070i->bState = MGC_SFF8070I_STATE_ALLOW_MEDIUM_REMOVE;
    MUSB_MemSet(pSff8070i->aCmd, 0, 16);
    pSff8070i->aCmd[0] = MGC_SFF8070I_PREVENT_ALLOW_MED_REMOVE;
    return pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
	pSff8070i->aCmd, 12, NULL, 0, TRUE, MGC_Sff8070iCmdComplete, FALSE, 2);
}

/* Implementation */
static uint8_t MGC_Sff8070iCmdSetGetReadCmd(void* pInstance,
					uint8_t* pCmdBuffer,
					uint8_t bMaxCmdLength,
					uint32_t dwBlockLo,
					uint32_t dwBlockHi,
					uint16_t wBlockCount,
					uint8_t bLun)
{
    uint8_t bIndex = 0;

    /* despite spec, use Read10 but claim 12 bytes */
    pCmdBuffer[bIndex++] = MGC_SFF8070I_READ10;
    pCmdBuffer[bIndex++] = bLun << MGC_S_SFF8070I_INQUIRY_LUN;
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 24) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 16) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 8) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)(dwBlockLo & 0xff);
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = (uint8_t)((wBlockCount >> 8) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)(wBlockCount & 0xff);
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = 0;

    return 12;
}

/* Implementation */
static uint8_t MGC_Sff8070iCmdSetGetWriteCmd(void* pInstance,
					 uint8_t* pCmdBuffer,
					 uint8_t bMaxCmdLength,
					 uint32_t dwBlockLo,
					 uint32_t dwBlockHi,
					 uint16_t wBlockCount,
					 uint8_t bLun)
{
    uint8_t bIndex = 0;

    /* despite spec, use Write10 but claim 12 bytes */
    pCmdBuffer[bIndex++] = MGC_SFF8070I_WRITE10;
    pCmdBuffer[bIndex++] = bLun << MGC_S_SFF8070I_INQUIRY_LUN;
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 24) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 16) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)((dwBlockLo >> 8) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)(dwBlockLo & 0xff);
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = (uint8_t)((wBlockCount >> 8) & 0xff);
    pCmdBuffer[bIndex++] = (uint8_t)(wBlockCount & 0xff);
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = 0;
    pCmdBuffer[bIndex++] = 0;

    return 12;
}

static uint8_t MGC_Sff8070iCmdSetFlushDevice(void* pInstance,
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun)
{
    /*MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;*/

    /* TODO: sync cache command, but it wants a range of blocks! */
    return TRUE;
}

static uint8_t MGC_Sff8070iCmdSetFormatDevice(void* pInstance,
					  MGC_MsdProtocol* pProtocol,
					  uint32_t dwBlockSize,
					  uint8_t bLun)
{
    /*MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;*/

    /* TODO: mode select to set block size, then format unit */
    return TRUE;
}

static uint8_t MGC_Sff8070iCmdSetAbortFormat(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun)
{
    /*MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;*/

    /* TODO */
    return TRUE;
}

static void MGC_Sff8070iCmdSetParseInterrupt(void* pInstance,
					 MGC_MsdProtocol* pProtocol,
					 const uint8_t* pMessage,
					 uint16_t wMessageLength,
					 uint8_t bLun)
{
    /* TODO: only relevant if CBI added in the future */
}

static uint8_t MGC_Sff8070iCmdSetCheckMedium(void* pInstance, 
					 MGC_MsdProtocol* pProtocol,
					 uint8_t bLun)
{
    MGC_Sff8070iCmdSetData* pSff8070i = (MGC_Sff8070iCmdSetData*)pInstance;

    /* clear retry counters etc. */
    pSff8070i->dwRetries = 0;
    pSff8070i->dwCapacityRetries = 0;
    pSff8070i->bFmtCapacityRetries = 0;
    pSff8070i->bRemovable = FALSE;
    pSff8070i->bNoMedium = FALSE;
    pSff8070i->bLunScan = FALSE;
    pSff8070i->bLun = bLun;

    /* start things by sending INQUIRY command */
    pSff8070i->bState = MGC_SFF8070I_STATE_INQUIRY;
    MUSB_MemSet(pSff8070i->aCmd, 0, 16);
    pSff8070i->aCmd[0] = MGC_SFF8070I_INQUIRY;
    pSff8070i->aCmd[4] = 36;
    return pProtocol->pfSendCmd(pProtocol->pProtocolData, pSff8070i, bLun,
	pSff8070i->aCmd, 12, pSff8070i->aInquiryData, 36, FALSE, MGC_Sff8070iCmdComplete, 
	FALSE, 0);
}
