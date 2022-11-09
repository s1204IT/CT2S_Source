#ifndef _LINUX_ELAN_KTF2K_H
#define _LINUX_ELAN_KTF2K_H

#if 0
#define ELAN_X_MIN		0
#define ELAN_Y_MIN		0
#define ELAN_X_MAX      1280
#define ELAN_Y_MAX      2112
#else
#define ELAN_X_MIN	0
#define ELAN_Y_MIN	0
#define ELAN_X_MAX	3160
#define ELAN_Y_MAX	1960
#endif

#define ELAN_KTF2K_NAME "elan-ktf2k"

struct elan_ktf2k_i2c_platform_data {
	uint16_t version;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	int intr_gpio;
	int rst_gpio;
	int onoff_sw;
	int orient_gpio;
};

#endif /* _LINUX_ELAN_KTF2K_H */
