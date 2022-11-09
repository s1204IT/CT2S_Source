/**********************************
*
* osa_kernel.h
*
********************************/

#ifndef _OSA_KERNEL_H_
#define _OSA_KERNEL_H_

#include <linux/wait.h>

typedef struct {
	wait_queue_head_t waitFlag;
	unsigned int flag;
} flag_wait_t;

#define OSA_FLAG_AND            5
#define OSA_FLAG_AND_CLEAR      6
#define OSA_FLAG_OR             7
#define OSA_FLAG_OR_CLEAR       8

typedef unsigned char OSA_STATUS;

enum {
	OS_SUCCESS = 0,		/* 0x0 -no errors                                        */
	OS_FAIL,		/* 0x1 -operation failed code                            */
	OS_TIMEOUT,		/* 0x2 -Timed out waiting for a resource                 */
	OS_NO_RESOURCES,	/* 0x3 -Internal OS resources expired                    */
	OS_INVALID_POINTER,	/* 0x4 -0 or out of range pointer value                  */
	OS_INVALID_REF,		/* 0x5 -invalid reference                                */
	OS_INVALID_DELETE,	/* 0x6 -deleting an unterminated task                    */
	OS_INVALID_PTR,		/* 0x7 -invalid memory pointer                           */
	OS_INVALID_MEMORY,	/* 0x8 -invalid memory pointer                           */
	OS_INVALID_SIZE,	/* 0x9 -out of range size argument                       */
	OS_INVALID_MODE,	/* 0xA, 10 -invalid mode                                 */
	OS_INVALID_PRIORITY,	/* 0xB, 11 -out of range task priority                   */
	OS_UNAVAILABLE,		/* 0xC, 12 -Service requested was unavailable or in use  */
	OS_POOL_EMPTY,		/* 0xD, 13 -no resources in resource pool                */
	OS_QUEUE_FULL,		/* 0xE, 14 -attempt to send to full messaging queue      */
	OS_QUEUE_EMPTY,		/* 0xF, 15 -no messages on the queue                     */
	OS_NO_MEMORY,		/* 0x10, 16 -no memory left                              */
	OS_DELETED,		/* 0x11, 17 -service was deleted                         */
	OS_SEM_DELETED,		/* 0x12, 18 -semaphore was deleted                       */
	OS_MUTEX_DELETED,	/* 0x13, 19 -mutex was deleted                           */
	OS_MSGQ_DELETED,	/* 0x14, 20 -msg Q was deleted                           */
	OS_MBOX_DELETED,	/* 0x15, 21 -mailbox Q was deleted                       */
	OS_FLAG_DELETED,	/* 0x16, 22 -flag was deleted                            */
	OS_INVALID_VECTOR,	/* 0x17, 23 -interrupt vector is invalid                 */
	OS_NO_TASKS,		/* 0x18, 24 -exceeded max # of tasks in the system       */
	OS_NO_FLAGS,		/* 0x19, 25 -exceeded max # of flags in the system       */
	OS_NO_SEMAPHORES,	/* 0x1A, 26 -exceeded max # of semaphores in the system  */
	OS_NO_MUTEXES,		/* 0x1B, 27 -exceeded max # of mutexes in the system     */
	OS_NO_QUEUES,		/* 0x1C, 28 -exceeded max # of msg queues in the system  */
	OS_NO_MBOXES,		/* 0x1D, 29 -exceeded max # of mbox queues in the system */
	OS_NO_TIMERS,		/* 0x1E, 30 -exceeded max # of timers in the system      */
	OS_NO_MEM_POOLS,	/* 0x1F, 31 -exceeded max # of mem pools in the system   */
	OS_NO_INTERRUPTS,	/* 0x20, 32 -exceeded max # of isr's in the system       */
	OS_FLAG_NOT_PRESENT,	/* 0x21, 33 -requested flag combination not present      */
	OS_UNSUPPORTED,		/* 0x22, 34 -service is not supported by the OS          */
	OS_NO_MEM_CELLS,	/* 0x23, 35 -no global memory cells                      */
	OS_DUPLICATE_NAME,	/* 0x24, 36 -duplicate global memory cell name           */
	OS_INVALID_PARM		/* 0x25, 37 -invalid parameter                           */
};

#define FLAG_INIT(_flagRef)                                                              \
({                                                                                       \
	flag_wait_t  *ll_flagRef           = _flagRef;                                       \
	int ll_status                      = OS_SUCCESS;                                     \
	init_waitqueue_head(&ll_flagRef->waitFlag);                                           \
	ll_flagRef->flag = 0;                                                                \
	ll_status;                                                                           \
})

#define FLAG_WAIT(_flagRef, _mask, _operation, _flags, _timeout)                         \
({                                                                                       \
	flag_wait_t  *ll_flagRef           = _flagRef;                                       \
	unsigned int ll_mask               = _mask;                                          \
	unsigned int ll_operation          = _operation;                                     \
	unsigned int *ll_flags             = (unsigned int *)_flags;                         \
	unsigned int ll_timeout            = _timeout;                                       \
	int ll_rc = 0, ll_status = OS_FAIL;                                                  \
	if ((ll_operation == OSA_FLAG_AND) || (ll_operation == OSA_FLAG_AND_CLEAR)) {        \
		while (1) {                                                                      \
			ll_rc = wait_event_interruptible_timeout(ll_flagRef->waitFlag,               \
								((ll_flagRef->flag & ll_mask) == ll_mask), ll_timeout);  \
			if ((ll_rc > 0) && ((ll_flagRef->flag & ll_mask) == ll_mask)) {              \
				*ll_flags = ll_flagRef->flag;                                            \
				if (ll_operation == OSA_FLAG_AND_CLEAR)                                  \
					ll_flagRef->flag &= ~ll_mask;	/* clear bit(s)*/                    \
				ll_status = OS_SUCCESS;                                                  \
				break;                                                                   \
			} else if (ll_rc <= 0)                                                       \
				break;                                                                   \
		}                                                                                \
	} else if ((ll_operation == OSA_FLAG_OR) || (ll_operation == OSA_FLAG_OR_CLEAR)) {   \
		while (1) {                                                                      \
			ll_rc = wait_event_interruptible_timeout(ll_flagRef->waitFlag,               \
								((ll_flagRef->flag & ll_mask) != 0), ll_timeout);        \
			if ((ll_rc > 0) && ((ll_flagRef->flag & ll_mask) != 0)) {                    \
				*ll_flags = ll_flagRef->flag;                                            \
				if (ll_operation == OSA_FLAG_OR_CLEAR)                                   \
					ll_flagRef->flag &= ~ll_mask;	/* clear bit(s)*/                    \
				ll_status = OS_SUCCESS;                                                  \
				break;                                                                   \
			} else if (ll_rc <= 0)                                                       \
				break;                                                                   \
		}                                                                                \
	}                                                                                    \
	ll_status;                                                                           \
})

#define FLAG_SET(_flagRef, _mask, _operation)                                            \
({                                                                                       \
	flag_wait_t  *ll_flagRef           = _flagRef;                                       \
	unsigned int ll_mask               = _mask;                                          \
	unsigned int ll_operation          = _operation;                                     \
	int ll_status = OS_SUCCESS;                                                          \
	if ((ll_operation == OSA_FLAG_AND)) {                                                \
		ll_flagRef->flag &= ll_mask;                                                     \
	} else if ((ll_operation == OSA_FLAG_OR)) {                                          \
		ll_flagRef->flag |= ll_mask;                                                     \
	} else                                                                               \
		ll_status = OS_FAIL;                                                             \
	wake_up_all(&ll_flagRef->waitFlag);                                                  \
	ll_status;                                                                           \
})

#endif
