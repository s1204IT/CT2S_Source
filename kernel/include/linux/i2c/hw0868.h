#ifndef __HW0868_H__
#define __HW0868_H__

struct hw0868_platform_data {
	int		irq_gpio;
	int		power_gpio;
	int		reset_gpio;
	int		sleep_gpio;
	int		config_gpio;
	int		invert_x;
	int		invert_y;
	int		swap_xy;
};

#endif // !__HW0868_H__
