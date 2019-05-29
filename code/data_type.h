/**********************************************************************************************************/
//
//  Filename :          data_type.h
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
//      defined basic data type
//
//  Revision:
//  Date       		Author      	Description
//  06/26/15    	Billy   		Original
//
/*************************************************************************************************************/
#ifndef _DATA_TYPE_H_
#define _DATA_TYPE_H_

// data type definition
typedef signed char			int8_t;
typedef unsigned char		uint8_t;

typedef signed short		int16_t;
typedef unsigned short		uint16_t;

typedef signed int			int32_t;
typedef unsigned int		uint32_t;

typedef unsigned int		BOOL;

// common definition
#define TRUE				1
#define FALSE				0

// memory operation definition
#define REG32(addr)   		(*((volatile int32_t *)(addr)))
#define MEM32(addr)   		(*((int32_t *)(addr)))

#define REG16(addr)   		(*((volatile uint16_t *)(addr)))
#define MEM16(addr)  		(*((uint16_t *)(addr)))

#define REG8(addr)    		(*((volatile uint8_t *)(addr)))
#define MEM8(addr)    		(*((uint8_t *)(addr)))

#endif

