/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/******************************************************************************
**  FILENAME:   	dbgLogClass.h
**
**  PURPOSE:		Defined debug log class, its variables and interfaces (APIs)
**
**  NOTES:
**
******************************************************************************/

#if !defined(_DBG_LOG_H_)
#define _DBG_LOG_H_

#include "dbgLogOs.h"

/* Define data strucutre */

#define DBG_LOG_FUNC_START  	0
#define DBG_LOG_FUNC_END		0xFFFFFFFF
#define DBG_LOG_FUNC_END_ERROR  0xEEEEEEEE
#define DBG_LOG_NAME_SIZE   	8

/*MACRO APIs */
/************************************************************************\
 * The following macro declares a pointer to Debug Log class inside user code.
 * Should be used to declare a global pointer.
 *
 * Example of usage:
 * DBGLOG_DECLARE_VAR(SHMEM);
\************************************************************************/
#define DBGLOG_DECLARE_VAR(nAME)				\
	pDbgLogS	_##nAME##DbgLog

/************************************************************************\
* The following macro declares an extern to the pointer that was declared
* by DBGLOG_DECLARE_VAR
* Should be used by each C source file that does not contain DBGLOG_DECLARE_VAR().
*
* Example of usage:
* DBGLOG_EXTERN_VAR(SHMEM);
\************************************************************************/
#define DBGLOG_EXTERN_VAR(nAME) 				\
	extern pDbgLogS _##nAME##DbgLog

/************************************************************************\
* The following macro initializes a Debug Log object
* Should be used once during init
*
* Example of usage:
* DBGLOG_INIT(SHMEM,1024);
\************************************************************************/
#define DBGLOG_INIT(nAME, sIZE) 						\
	do {												\
		_##nAME##DbgLog =   DbgLogCreate(#nAME, sIZE);  \
		ASSERT(_##nAME##DbgLog);						\
	} while (0)

/************************************************************************\
* The following macro adds a log entry
*
*
* Example of usage:
* DBGLOG_ADD(SHMEM,1,0,2);
\************************************************************************/
#define DBGLOG_ADD(nAME, eVENT, dATA1, dATA2)   						\
	do {																\
		DbgLogAddEntry(_##nAME##DbgLog, (unsigned int)eVENT, (unsigned int)dATA1, (unsigned int)dATA2); \
	} while (0)

/************************************************************************\
* The following macro enables or disabled logging
* The function DbgLogEnable returns previous state
*
\************************************************************************/
#define DBGLOG_ENABLE(nAME, eNABLE) 									\
		DbgLogEnable(_##nAME##DbgLog, eNABLE)

#define DBGLOG_DELAYED_STOP(nAME, cOUNT)								 \
		DbgLogSetDelayedStop(_##nAME##DbgLog, cOUNT, __FILE__, __LINE__)

/* This macro converts from log name to a pointer to its main database structure */
#define DBGLOG_GET_INFO(nAME)   _##nAME##DbgLog
#define DBGLOG_GET_SIZE(nAME)   (sizeof(DbgLogS) + _##nAME##DbgLog->size * sizeof(DbgLogEnteryS))

#define DBGLOG_PRINT(nAME)  			\
		DbgLogPrint(_##nAME##DbgLog)

#define DBGLOG_PRINT_BINARY(nAME)   		   \
		DbgLogPrintBinary(_##nAME##DbgLog)

#if 0
/************************************************************************\
* The following group of macros send trace to an interface associated with level parameter
*
\************************************************************************/
#define DBGLOG_TRACE(cat1, cat2, cat3, level, fmt)  								\
	{   																			\
		if (GetLevelRedirection(level) == DBGLOG_TO_ACAT) {  						\
			DIAG_FILTER(cat1, cat2, cat3, level)									\
			diagTextPrintf(fmt);													\
		}   																		\
	}
#define DBGLOG_TRACE1(cat1, cat2, cat3, level, fmt, val1)
#define DBGLOG_TRACE2(cat1, cat2, cat3, level, fmt, val1, val2)
#define DBGLOG_TRACE3(cat1, cat2, cat3, level, fmt, val1, var2, val3)
#define DBGLOG_TRACEBUF(cat1, cat2, cat3, level, fmt, buffer, length)

/************************************************************************\
 * Each user of dbgLogClass should define its own events enum. This enum
 * should be exported to ACAT so that it could be analyzed by offline tool.
 * Important - enum size MUST be 32 bit !!!
\************************************************************************/

/* define Enum valuse */
/* Log events definitions (currently numbers 100-150 are free for XDA). */
/*ICAT EXPORTED ENUM */
typedef enum {
	SHMEM_DBG_EVENT_EMPTY = 0,

	/*ACIPCD events */
	MDB_ACIPCDRegister,

	/*Must be Last Event */
	MDB_ShmemDebugLastEntry = 0xFFFFFFFF
} SHMEM_EventTypeE;
#endif

/*typedef   	unsigned int unsigned int */

/* Log entry */
typedef struct {
	unsigned int timeStamp;
	unsigned int event;
	unsigned int eventData1;
	unsigned int eventData2;
} DbgLogEnteryS;

/* Log structure (holds the log and log related variables) */
typedef struct {
	unsigned int size;
#if !defined(__KERNEL__)
	volatile unsigned int nextLogEntryIndex;
#else
	atomic_t nextLogEntryIndex;
#endif
	unsigned int totalLogEntries;
	unsigned int logEnabled;
	unsigned int cyclic;
	unsigned int addTS;
	unsigned int delayedStopCount;
	char delayedLogFileName[256];
	unsigned int delayedLogFileLine;
	unsigned int delayedOriginalEvent;

	char name[DBG_LOG_NAME_SIZE];
	/* Must be last structure member */
	DbgLogEnteryS dbgLogEnteries[1];
} DbgLogS;

typedef DbgLogS * pDbgLogS;
/*typedef   	struct DbgLogS* pDbgLogS; */

/* Costructors & Destructors */
pDbgLogS DbgLogCreate(char *name, unsigned int num_log_elements);
void DbgLogDestroy(pDbgLogS This);

/* define collection func */
void DbgLogAddEntry(pDbgLogS This, unsigned int eventID, unsigned int data, unsigned int data2);

/* Enable or disable logging. returns previous state */
unsigned int DbgLogEnable(pDbgLogS This, unsigned int isEnable);

void DbgLogSetDelayedStop(pDbgLogS This, unsigned int count, const char *fileName, unsigned int line);

/*Printing the DBG Log */
void DbgLogPrint(pDbgLogS This);
void DbgLogPrintBinary(pDbgLogS This);

/* Error handling hooks */
/*void  DbgLogRegisterEEHCallback   	(pDbgLogEEHCallback 	ee_callback); */

#if 0

struct {
	DbgLogDatabaseS *dbgLogDB;

	void (*AddEntry) (pDbgLogS This, unsigned int eventID, unsigned int data, unsigned int data2);
	void (*Destory) (pDbgLogS This);

} DbgLogS;

void shmemDlDebugSetDelayedStop(unsigned int count, const char *func_name, unsigned int line);
void shmemDebugInit(void);
void shmemDebugStop(void);
void shmemDebugRegisterWithErrorHandler(void);
SHMEM_EventLogS *shmemGetLogPtr(void);

#if defined(SHMEM_LOG_ENABLED)

#define SHMEM_DEBUG_LOG(EventID, Data)  			shmemDebugLog(EventID, (unsigned int)Data, 0xEEEEEEEE)
#define SHMEM_DEBUG_LOG2(EventID, Data, Data2)  	shmemDebugLog(EventID, (unsigned int)Data, (unsigned int)Data2)

#if !defined	(_SHMEM_LOG_API_)
extern
#endif
SHMEM_EventLogS _shmemLog;

#else

/* for regular work (NO DEBUG) - do nothing */
#define SHMEM_DEBUG_LOG(EventID, Data)
#define SHMEM_DEBUG_LOG2(EventID, Data, Data2)

#endif /*SHMEM_LOG_ENABLED */

#endif
#endif /*_DBG_LOG_H_ */
