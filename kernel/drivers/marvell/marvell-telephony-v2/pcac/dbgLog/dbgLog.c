/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/******************************************************************************
**
**  FILENAME:       dbgLogClass.c
**
**  PURPOSE:        Implements debug log class
**
******************************************************************************/
#ifdef OSA_LINUX
#if !defined(__KERNEL__)
#include <stdio.h>
#include <sys/atomics.h>
#endif
#endif
#include "dbgLog.h"

pDbgLogS DbgLogCreate(char *name, unsigned int num_log_elements)
{
	unsigned int alloc_size = sizeof(DbgLogS) + num_log_elements * sizeof(DbgLogEnteryS);
	pDbgLogS dbg_log = DBG_LOG_MALLOC(alloc_size);

	if (dbg_log) {
		memset(dbg_log, 0, alloc_size);
		dbg_log->cyclic = TRUE;
		dbg_log->addTS = TRUE;
		dbg_log->logEnabled = TRUE;
#if !defined(__KERNEL__)
		dbg_log->nextLogEntryIndex = 0;
#else
		atomic_set(&dbg_log->nextLogEntryIndex, 0);
#endif
		dbg_log->totalLogEntries = 0;
		dbg_log->size = num_log_elements;
		strncpy(dbg_log->name, name, DBG_LOG_NAME_SIZE - 1);
		dbg_log->name[DBG_LOG_NAME_SIZE - 1] = '\0';	/*just in case wasn't null-terminlated */

		dbg_log->delayedStopCount = 0;
		dbg_log->delayedLogFileLine = 0xFFFFFFFF;
		dbg_log->delayedOriginalEvent = 0xFFFFFFFF;

		DbgLogOsInit(dbg_log);

		return dbg_log;
	} else
		return NULL;
}

void DbgLogDestroy(pDbgLogS This)
{
	if (This) {
		DBG_LOG_FREE(This);
	}
}

/**
 * Tiny dbg logging function. Specified for quick writes to debug array.
 * Not protected and thus under
 *   heavy load + multiple contexts access + relatively small array size
 * It might happen that two context will get same index of array to write
 * into and thus garbaging the data in the entry.
 *
 * @param This       Pointer to Log Struct
 * @param event      event data - client specific
 * @param eventData1 event specific data - 4 bytes
 * @param eventData2 event specific data additional 4 bytes
 */
void DbgLogAddEntry(pDbgLogS This, unsigned int event, unsigned int eventData1, unsigned int eventData2)
{
	unsigned int currIndex, nextIndex;

	if (!This->logEnabled)
		return;

#if !defined(__KERNEL__)
	/* __atomic_inc(x) == {return x++;} */
	currIndex = __atomic_inc((int *)&This->nextLogEntryIndex);
	nextIndex = currIndex + 1;
#else
	/* atomic_inc_return(x) == {return ++x;} */
	nextIndex = atomic_inc_return(&This->nextLogEntryIndex);
	currIndex = nextIndex - 1;
#endif

	if (currIndex >= This->size) {
		unsigned int wrap_currIndex, wrap_nextIndex;
		/* if the log is not in cyclic mode disable logging. */
		if (!This->cyclic) {
			This->logEnabled = FALSE;
			return;
		}
		/* Wrap indexes */
		wrap_currIndex = currIndex % This->size;
		wrap_nextIndex = (wrap_currIndex + 1) % This->size;
		/* Try to wrap out This->nextLogEntryIndex - it is OK */
		/* to fail here since we will do it in one of next tries */
		/* So attention: This->nextLogEntryIndex may really exceed This->size */
#if !defined(__KERNEL__)
		/*__atomic_cmpxchg(old, new, val) == {old == val ? val = new} */
		__atomic_cmpxchg(nextIndex, wrap_nextIndex, (int *)&This->nextLogEntryIndex);
#else
		/*atomic_cmpxchg(val, old, new) == {old == val ? val = new} */
		atomic_cmpxchg(&This->nextLogEntryIndex, nextIndex, wrap_nextIndex);
#endif
		currIndex = wrap_currIndex;
	}
	/*check if this is a delayed stop scenario */
	if (This->delayedStopCount != 0) {
		/*if counter is 0 - stop the log */
		/*Pay attention - that bellow decrement is unsafe too thus we add '<=' */
		if (--This->delayedStopCount <= 0) {
			This->logEnabled = FALSE;
			This->delayedStopCount = 0;
			DBG_LOG_PRINT("\nDBG Log DELAYED STOP finished !!!\n");
		}
	}

	This->totalLogEntries++;
	/* logging in progress. */
	if (This->addTS) {
		This->dbgLogEnteries[currIndex].timeStamp = dbgLogGetTimestamp();
	} else {
		This->dbgLogEnteries[currIndex].timeStamp = 0;
	}

	This->dbgLogEnteries[currIndex].event = event;
	This->dbgLogEnteries[currIndex].eventData1 = eventData1;
	This->dbgLogEnteries[currIndex].eventData2 = eventData2;
}

unsigned int DbgLogEnable(pDbgLogS This, unsigned int isEnable)
{
	unsigned int currentlyIsEnabled = This->logEnabled;

	This->logEnabled = isEnable;

	return currentlyIsEnabled;
}

void DbgLogSetDelayedStop(pDbgLogS This, unsigned int count, const char *fileName, unsigned int line)
{
	/*check if delayed STOP already activated - if yes - exit */
	if (This->delayedLogFileLine != 0xFFFFFFFF) {
		DBG_LOG_PRINT("DBG Log DELAYED STOP already activated (will stop in %d events)!!!\n",
			      This->delayedStopCount);
		return;
	}
#if defined(__KERNEL__)		/*FLAVOR_APP */
	DBG_LOG_PRINT("\nDBG Log: DbgLogSetDelayedStop is activated !!! (file=%s, line=%d) \n", fileName, line);
#endif

	DbgLogAddEntry(This, 1010101010, 0xA0A0A0A0, 0xA0A0A0A0);
	This->delayedStopCount = count;
	This->delayedLogFileLine = line;
#if !defined(__KERNEL__)
	This->delayedOriginalEvent = This->nextLogEntryIndex;
#else
	This->delayedOriginalEvent = atomic_read(&This->nextLogEntryIndex);
#endif
	strncpy(This->delayedLogFileName, fileName, sizeof(This->delayedLogFileName) - 1);
}

void DbgLogPrintBinary(pDbgLogS This)
{
#if !defined(__KERNEL__)
	UNUSEDPARAM(This);
#else
	int i, size;
	unsigned int logEnabledSave;

	char *pWalk = (char *)This;

	/*stop logging for now */
	logEnabledSave = This->logEnabled;
	This->logEnabled = 0;

	size = sizeof(DbgLogS) + This->size * sizeof(DbgLogEnteryS);

	for (i = 0; i < size; i++) {
		printk("%c", (char)pWalk[i]);
	}
	/*restore logging */
	This->logEnabled = logEnabledSave;
#endif
}

void DbgLogPrint(pDbgLogS This)
{
	unsigned int i = 0, j = 0, nentry = 0;
	/*unsigned int totalToPrint; */
	char str[200];

#if defined(__KERNEL__)
	snprintf(str, sizeof(str) - 1, "\nDBG Log: Print log in KERNEL (name=%s) !!!!!\n", This->name);
#else
	snprintf(str, sizeof(str) - 1, "\nDBG Log: Print log in USER (name=%s) !!!!!\n", This->name);
#endif

	DBG_LOG_PRINT("%s", str);

	/*stop logging for now */
	This->logEnabled = 0;
#if !defined(__KERNEL__)
	nentry = This->nextLogEntryIndex;
#else
	nentry = atomic_read(&This->nextLogEntryIndex);
#endif

	DBG_LOG_PRINT("DBG Log info: size = %d, Total Entries = %d, (nextEntry = %d - since in order)\n", This->size, This->totalLogEntries, nentry);	/*YG: This->delayedOriginalEvent - not currently supported */

	/*check if this delayed STOP */
	if (This->delayedLogFileLine != 0xFFFFFFFF) {
		DBG_LOG_PRINT
		    ("DBG Log DELAYED info: This is a DELAYED STOP log: fileName = %s, lineNum = %d (originalEvent = %d, count = %d)\n",
		     This->delayedLogFileName, This->delayedLogFileLine, This->delayedOriginalEvent,
		     This->delayedStopCount);
	}

	/* in order to print log 'in order' we start to print from current location to the end and then cycle it to the start */
	if (This->totalLogEntries >= This->size) {
		for (i = nentry, j = 0; i < This->size; i++, j++) {
			DBG_LOG_PRINT("Idx=%5d: Event= %8d, data1= %8x, data2= %8x, time=%lu\n",
				      j, This->dbgLogEnteries[i].event, This->dbgLogEnteries[i].eventData1,
				      This->dbgLogEnteries[i].eventData2,
				      (unsigned long)This->dbgLogEnteries[i].timeStamp);
		}
	}
	for (i = 0; i < nentry; i++, j++) {
		DBG_LOG_PRINT("Idx=%5d: Event= %8d, data1= %8x, data2= %8x, time=%lu\n",
			      j, This->dbgLogEnteries[i].event, This->dbgLogEnteries[i].eventData1,
			      This->dbgLogEnteries[i].eventData2, (unsigned long)This->dbgLogEnteries[i].timeStamp);
	}

#if defined(ACI_LNX_KERNEL)
	DBG_LOG_PRINT("\nDBG Log: Print log in KERNEL - end of log\n");
#else
	DBG_LOG_PRINT("\nDBG Log: Print log in USER   - end of log\n");
#endif

	/*restart logging */
	This->logEnabled = 1;
}

#if 0
pDbgLogS NewDbgLog(unsigned int num_log_elements)
{
	pDbgLogS dbg_log = DBG_LOG_MALLOC(sizeof(*pDbgLogS));

	dbg_log->dbgLogDB = DBG_LOG_MALLOC(sizeof(*dbgLogDB) + num_log_elements * sizeof(DbgLogEnteryS));

	memset(dbg_log->dbgLogDB->dbgLogEnteries, 0, num_log_elements * sizeof(DbgLogEnteryS));
	dbg_log->dbgLogDB->cyclic = TRUE;
	dbg_log->dbgLogDB->addTS = TRUE;
	dbg_log->dbgLogDB->logEnabled = TRUE;
	dbg_log->dbgLogDB->nextLogEntryIndex = 0;
	dbg_log->dbgLogDB->totalLogEntries = 0;
	dbg_log->dbgLogDB->size = num_log_elements;
	dbg_log->Destory = Destory;
	dbg_log->AddEntry = dbgLogAddEntry;

	return dbg_log;

}

void shmemDebugInit(void)
{
	_shmemLog.nextEntryIndex = 0;
	_shmemLog.runningNumOfEntries = 0;
	_shmemLog.logEnabled = TRUE;
	_shmemLog.cyclic = TRUE;
	_shmemLog.addTS = FALSE;	/*TRUE; */
	_shmemLog.size = SHMEM_EVENT_LOG_SIZE;
/*
    _shmemLog.delayedStopCount      = DELAYED_STOP_COUNTER_DISABLED;
    _shmemLog.delayedLogFileLine    =   0;
    _shmemLog.delayedOriginalEvent  =   0xFFFFFFFF;
 */

	/* Call OS specific init function */
	SHMEM_DEBUG_LOG_INIT(_shmemLog);

	SHMEM_DEBUG_LOG_REGISTER_WITH_ERROR_HANDLER(_shmemLog);
}

/*
 *  shmemDebugStop()
 *  Stop recording: used by EEHandler on assert - see registration below
 */
void shmemDebugStop(void)
{
	_shmemLog.logEnabled = FALSE;
}

SHMEM_EventLogS *shmemGetLogPtr(void)
{
	return &_shmemLog;
}

#if 0
void shmemDlDebugSetDelayedStop(unsigned int count, const char *func_name, unsigned int line)
{
	SHMEM_DEBUG_LOG2(MDB_MslDlDebugSetDelayedStop, 0xAAAA, 0x9999);
	_shmemLog.delayedStopCount = count;
	_shmemLog.delayedLogFileLine = line;
	_shmemLog.delayedOriginalEvent = _shmemLog.nextEntryIndex;
	strncpy(_shmemLog.delayedLogFileName, func_name, 255);
}
#endif /*if 0 */

#endif
