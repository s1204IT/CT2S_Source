#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include "linux_types.h"
#include "osa_kernel.h"
typedef void *OsaRefT;

typedef struct {
	void *poolBase;		/*          Pointer to start of pool memory. */
	unsigned int poolSize;	/*          size of the pool. */
	char *name;		/*  [OP]    Pointer to a NULL terminated string. */
	unsigned char bSharedForIpc;	/*  [OP]    TRUE - The object can be accessed from all processes. */
} OsaMemCreateParamsT;

typedef struct {
	char *name;		/*  [OP]    Pointer to a NULL terminated string. */
	unsigned char bSharedForIpc;	/*  [OP]    TRUE - The object can be accessed from all processes. */
} OsaCriticalSectionCreateParamsT;

extern void *OsaMemGetPoolRef(char *poolName, void *pMem, void *pForFutureUse);
extern void OsaMemFree(void *pMem);
extern unsigned int OsaMemGetUserParam(void *pMem);
extern OSA_STATUS OsaMemSetUserParam(void *, unsigned int);
extern void *OsaMemAlloc(OsaRefT poolRef, unsigned int Size);
extern OsaRefT OsaCriticalSectionEnter(OsaRefT OsaRef, void *pForFutureUse);
extern void OsaCriticalSectionExit(OsaRefT OsaRef, void *pForFutureUse);
extern OSA_STATUS OsaMemCreatePool(OsaRefT *pOsaRef, OsaMemCreateParamsT *pParams);
extern OsaRefT OsaCriticalSectionCreate(OsaCriticalSectionCreateParamsT *pParams);

#endif /* _ALLOCATOR_H */
