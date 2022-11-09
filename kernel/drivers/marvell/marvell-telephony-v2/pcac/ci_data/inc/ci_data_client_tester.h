/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client API headerfile
    Filename:    ci_data_client_api.h
    Description: Contains defines/structs/prototypes for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_TESTER_H_)
#define      _CI_DATA_CLIENT_TESTER_H_

#include "linux_types.h"

/************************************************************************
 * API's
 ***********************************************************************/

void CiDataTesterPrintDatabase(void);

void CiDataTesterRunCommand(char oper, char *params);
void CiDataControlLogging(const char *buf, size_t *index);

#endif /* _CI_DATA_CLIENT_TESTER_H_ */
