#ifndef __WACOM__
#define __WACOM__

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#define WACOM_SYSFS 1
#define ERR_REGISTER_SYSFS 1

struct wacom {
	struct device *dev;
	struct class *class;
	dev_t dev_t;
};

/*Added for Calibration purpose*/
typedef enum {
	STATE_NORMAL,
	STATE_QUERY,
	STATE_GETCAL,
	STATE_POINTS,
} NODE_STATE;

struct calibrationData {
  int   originX;
  int   originY;
  int   extentX;
  int   extentY; 
};

struct sPoint{
  int x;
  int y;
};
#endif
