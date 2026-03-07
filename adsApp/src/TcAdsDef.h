///////////////////////////////////////////////////////////////////////////////
// This is a part of the Beckhoff TwinCAT Software Development Kit.
// Copyright (C) Beckhoff Automation GmbH
// All rights reserved.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Prototypes and Definitions for ADS communication
////////////////////////////////////////////////////////////////////////////////
#include "AdsLib.h"          //error codes

#define	ADS_FIXEDNAMESIZE				16

/* #define	AMSPORT_ROUTER					1
#define	AMSPORT_LOGGER					100
#define	AMSPORT_R0_RTIME				200
#define	AMSPORT_R0_TRACE				(AMSPORT_R0_RTIME+90)
#define	AMSPORT_R0_IO					300
#define	AMSPORT_R0_SPS					400
#define	AMSPORT_R0_NC					500
#define	AMSPORT_R0_ISG					550
#define	AMSPORT_R0_PCS					600
#define	AMSPORT_R0_PLC					801
#define	AMSPORT_R0_PLC_RTS1			801
#define	AMSPORT_R0_PLC_RTS2			811
#define	AMSPORT_R0_PLC_RTS3			821
#define	AMSPORT_R0_PLC_RTS4			831
#define	AMSPORT_R0_RTS					850 */

////////////////////////////////////////////////////////////////////////////////
// ADS reserved index groups
#define ADSIGRP_SYMTAB						0xF000
#define ADSIGRP_SYMNAME						0xF001
#define ADSIGRP_SYMVAL						0xF002

#define ADSIGRP_SYM_HNDBYNAME				0xF003
#define ADSIGRP_SYM_VALBYNAME				0xF004
#define ADSIGRP_SYM_VALBYHND				0xF005
#define ADSIGRP_SYM_RELEASEHND			0xF006
#define ADSIGRP_SYM_INFOBYNAME			0xF007
#define ADSIGRP_SYM_VERSION				0xF008
#define ADSIGRP_SYM_INFOBYNAMEEX			0xF009

#define ADSIGRP_SYM_DOWNLOAD				0xF00A
#define ADSIGRP_SYM_UPLOAD					0xF00B
#define ADSIGRP_SYM_UPLOADINFO			0xF00C

#define ADSIGRP_SYMNOTE						0xF010	// notification of named handle

#define ADSIGRP_IOIMAGE_RWIB				0xF020		// read/write input byte(s)
#define ADSIGRP_IOIMAGE_RWIX				0xF021		// read/write input bit
#define ADSIGRP_IOIMAGE_RWOB				0xF030		// read/write output byte(s)
#define ADSIGRP_IOIMAGE_RWOX				0xF031		// read/write output bit
#define ADSIGRP_IOIMAGE_CLEARI			0xF040		// write inputs to null
#define ADSIGRP_IOIMAGE_CLEARO			0xF050		// write outputs to null

#define ADSIGRP_DEVICE_DATA				0xF100		// state, name, etc...
#define ADSIOFFS_DEVDATA_ADSSTATE		0x0000		// ads state of device
#define ADSIOFFS_DEVDATA_DEVSTATE		0x0002		// device state

// ADS Error codes
#define	ERR_ADSERRS					0x0700

////////////////////////////////////////////////////////////////////////////////
// ADS error codes

#pragma	pack( push, 1)

////////////////////////////////////////////////////////////////////////////////

#define	ADSNOTIFICATION_PDATA( pAdsNotificationHeader )	\
	(	(ads_ui8*)	(((PAdsNotificationHeader)pAdsNotificationHeader->data )


typedef AdsSymbolEntry*  PAdsSymbolEntry;
typedef AdsSymbolEntry** PPAdsSymbolEntry;

#define	PADSSYMBOLNAME(p)			((char*)(((PAdsSymbolEntry)p)+1))
#define	PADSSYMBOLTYPE(p)			(((char*)(((PAdsSymbolEntry)p)+1))+((PAdsSymbolEntry)p)->nameLength+1)
#define	PADSSYMBOLCOMMENT(p)		(((char*)(((PAdsSymbolEntry)p)+1))+((PAdsSymbolEntry)p)->nameLength+1+((PAdsSymbolEntry)p)->typeLength+1)

#define	PADSNEXTSYMBOLENTRY(pEntry)	(*((ads_ui32*)(((char*)pEntry)+((PAdsSymbolEntry)pEntry)->entryLength)) \
						? ((PAdsSymbolEntry)(((char*)pEntry)+((PAdsSymbolEntry)pEntry)->entryLength)): NULL)



////////////////////////////////////////////////////////////////////////////////
#define	ADSDATATYPEFLAG_DATATYPE		0x00000001
#define	ADSDATATYPEFLAG_DATAITEM		0x00000002

#define	ADSDATATYPE_VERSION_NEWEST		0x00000001

#define	PADSDATATYPENAME(p)			((PCHAR)(((PAdsDatatypeEntry)p)+1))
#define	PADSDATATYPETYPE(p)			(((PCHAR)(((PAdsDatatypeEntry)p)+1))+((PAdsDatatypeEntry)p)->nameLength+1)
#define	PADSDATATYPECOMMENT(p)		(((PCHAR)(((PAdsDatatypeEntry)p)+1))+((PAdsDatatypeEntry)p)->nameLength+1+((PAdsDatatypeEntry)p)->typeLength+1)
#define	PADSDATATYPEARRAYINFO(p)	(PAdsDatatypeArrayInfo)(((PCHAR)(((PAdsDatatypeEntry)p)+1))+((PAdsDatatypeEntry)p)->nameLength+1+((PAdsDatatypeEntry)p)->typeLength+1+((PAdsDatatypeEntry)p)->commentLength+1)


////////////////////////////////////////////////////////////////////////////////
typedef struct
{
	uint32_t	nSymbols;
	uint32_t	nSymSize;
} AdsSymbolUploadInfo, *PAdsSymbolUploadInfo;

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
typedef enum nAmsRouterEvent	//KlausBue 11/99 
{
	AMSEVENT_ROUTERSTOP		= 0,
	AMSEVENT_ROUTERSTART		= 1,
	AMSEVENT_ROUTERREMOVED	= 2
}AmsRouterEvent;
#pragma	pack( pop )

#ifndef ADS_CONST
#define ADS_CONST
#endif

