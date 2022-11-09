#ifndef _POWER_DOMAIN_ISP_H
#define _POWER_DOMAIN_ISP_H

int sc2_pwr_ctrl(u32 on, unsigned int mod_id);
void set_b52fw_loaded(void);
int get_b52fw_loaded(void);

#endif
