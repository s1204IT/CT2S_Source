/*
 *  linux/arch/arm64/mach/fuseinfo-pxa1928.S
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

/*
 *  int smc_get_fuse_info(u64 function_id, void *arg)
 */
ENTRY(smc_get_fuse_info)
	mov	x7, x1
	smc	#0
	str	w1, [x7], #4
	str	w2, [x7], #4
	str	w3, [x7], #4
	str	w4, [x7], #4
	str	w5, [x7], #4
	str	w6, [x7], #4
	ret
ENDPROC(smc_get_fuse_info)

/*
 * smc_config_timer2(unsigned int function_id, struct smc_timer2_params *params)
 * struct smc_timer2_params {
 * 	u64 offset;
 *	u64 value;
 *	u64 ret;
 * }
 */
ENTRY(smc_config_timer2)
	mov x7, x1
	ldr x1, [x7, #0]
	ldr x2, [x7, #8]
	ldr x3, [x7, #16]
	smc #0
	str x0, [x7, #16]
	ret
ENDPROC(smc_config_timer2)
