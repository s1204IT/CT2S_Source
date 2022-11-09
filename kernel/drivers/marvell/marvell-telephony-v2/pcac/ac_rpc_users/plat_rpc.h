#ifndef _PLAT_RPC_H_
#define _PLAT_RPC_H_

/*******************************************************************************
*
* Title: Platform functions over Generic RPC
*
* Filename: plat_rpc.h
*
* Author:   Idan Hemdat
*
* Created: 04/09/2010
*
* Notes:
******************************************************************************/

/********  USIM *************/

typedef struct {
#define RPC_SIM_MASTER_READ	0
#define RPC_SIM_SLAVE_READ	1
	unsigned short simType;
} rpcSimIn_S;

typedef struct {
#define RPC_SIM_DETECT_NOSIM	0
#define RPC_SIM_DETECT_SIM	1
	unsigned long simIsInside;
} rpcSimOut_S;

#define RPC_SIM_OK			0
#define RPC_SIM_INVALID_PARAMS		1
#define RPC_SIM_NO_DETECTION		2
#define RPC_SIM_INVALID_SIM_TYPE	3

unsigned long rpcSimDetect(unsigned short InBufLen, void *pInBuf, unsigned short OutBufLen, void *pOutBuf);
/**************************************************************************/

/******** TEMPERATURE **********/
typedef struct {
	unsigned long reserved;
} rpcTemperatureIn_S;

typedef struct {
	unsigned long adcTemp;
	signed long celsiusTemp;
} rpcTemperatureOut_S;

#define RPC_TEMP_OK		0
#define RPC_TEMP_INVALID_PARAMS 1
#define RPC_TEMP_READ_FAIL	2

unsigned long rpcRfTemperatureTest(unsigned short InBufLen, void *pInBuf, unsigned short OutBufLen, void *pOutBuf);
/**************************************************************************/

#endif /* _PLAT_RPC_H_ */
