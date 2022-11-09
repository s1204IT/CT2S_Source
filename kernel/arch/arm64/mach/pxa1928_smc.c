#include <linux/types.h>
#include <linux/pxa1928_smc.h>

void smc_timer2_write(unsigned int offset, unsigned int value)
{
	struct smc_timer2_params params;

	params.offset = (u64)offset;
	params.value = (u64)value;
	smc_config_timer2(LC_WRITE_TIMER2_REG, &params);
	return;
}

unsigned int smc_timer2_read(unsigned int offset)
{
	struct smc_timer2_params params;

	params.offset = (u64)offset;
	params.value = 0;
	smc_config_timer2(LC_READ_TIMER2_REG, &params);
	return params.ret;
}
