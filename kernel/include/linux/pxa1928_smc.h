/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __PXA1928_SMC_H__
#define __PXA1928_SMC_H__

#define LC_COMMAND_START        0xC2000000
#define LC_READ_TIMER2_REG      (LC_COMMAND_START + 0x4001)
#define LC_WRITE_TIMER2_REG     (LC_COMMAND_START + 0x4002)

struct smc_timer2_params {
	u64 offset;
	u64 value;
	u64 ret;
};
extern unsigned int smc_config_timer2(unsigned int function_id, struct smc_timer2_params *params);
extern void smc_timer2_write(unsigned int offset, unsigned int value);
extern unsigned int smc_timer2_read(unsigned int offset);
#endif


