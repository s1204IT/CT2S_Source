/*
 * linux/arch/arm/mach-mmp/pxa1986_headsmp.S
 *
 * Copyright (C) 2012 Marvell, Inc.
 *
 * Author: Neil Zhang <zhangwm@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>


/*
 * PXA specific entry point for secondary CPUs.  This provides
 * a "holding pen" into which all secondary cores are held until we're
 * ready for them to initialise.
 */
ENTRY(pxa1986_secondary_startup)
	mrc	p15, 0, r0, c0, c0, 5
	bfc	r0, #24, #8 /* clear [31:24], leave CPU and cluster IDs */
	adr	r4, 1f
	ldmia	r4, {r5, r6}
	sub	r4, r4, r5
	add	r6, r6, r4
pen:	ldr	r7, [r6]
	cmp	r7, r0
	bne	pen

	/*
	 * we've been released from the holding pen: secondary_stack
	 * should now contain the SVC stack for this core
	 */
	/* Enable CCI slave port snooping and dvm for this cluster */
	ldr	r3, =0xd0090000
	tst	r0, #0x100
	addne	r3, r3, #0x5000
	addeq	r3, r3, #0x4000
	ldr	r2, [r3]
	orr	r2, r2, #3
	str	r2, [r3]
	/* No need to invalidate L1/L2 caches after reset on A7 and A15.
	This is done in hardware and canNOT be done in software on A15,
	because DCISW is upgraded to DCCISW */
	b	secondary_startup
ENDPROC(pxa1986_secondary_startup)

	.align	2
1:	.long	.
	.long	pen_release
