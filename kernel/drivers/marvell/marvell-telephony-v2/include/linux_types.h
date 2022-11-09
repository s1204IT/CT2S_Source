#ifndef __LINUX_TYPES_H__
#define __LINUX_TYPES_H__
typedef unsigned char BOOL;
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;

#ifdef TRUE
#undef TRUE
#endif /* TRUE */
#define TRUE    1

#ifdef FALSE
#undef FALSE
#endif /* FALSE */
#define FALSE   0
#endif /*__LINUX_TYPES_H__*/

#define ASSERT(cnd)	BUG_ON(!(cnd))
#define ACIDBG_LNX_PRINT        printk
#define ACIDBG_TRACE(gROUP, lEVEL, tEXT)
#define ACIDBG_TRACE_1P(gROUP, lEVEL, tEXT, pARAM1)
#define ACIDBG_TRACE_2P(gROUP, lEVEL, tEXT, pARAM1, pARAM2)
#define ACIDBG_TRACE_3P(gROUP, lEVEL, tEXT, pARAM1, pARAM2, pARAM3)
