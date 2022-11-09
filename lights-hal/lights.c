#include <hardware/lights.h>
#include <hardware/hardware.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#define LOG_TAG "lights"
#include <utils/Log.h>

//#define LIGHTS_DEBUG 0

const char * const LCD_BACKLIGHT = "/sys/class/backlight/lcd-bl/brightness";
const char * const LCD_MAX_BACKLIGHT = "/sys/class/backlight/lcd-bl/max_brightness";
const char * const KEYBOARD_BACKLIGHT = "";
const char * const BUTTON_BACKLIGHT = "/sys/class/leds/button-backlight/brightness";
const char * const BATTERY_BACKLIGHT = "";
const char * const ATTENTION_BACKLIGHT = "";

#define LED_PATH(path, num, node)  \
	sprintf(path, "/sys/class/leds/led%d/%s", num, node)

static int lights_device_open(const struct hw_module_t *module,
	const char *name, struct hw_device_t **device);

static struct hw_module_methods_t lights_module_methods = {
	.open      = lights_device_open
};

struct light_module_t
{
    struct hw_module_t common;
};

struct light_module_t HAL_MODULE_INFO_SYM = {
	common: {
		tag : HARDWARE_MODULE_TAG,
		version_major : 1,
		version_minor : 0,
		id : LIGHTS_HARDWARE_MODULE_ID,
		name : "lights module",
		author : "",
		methods : &lights_module_methods,
	}
};

struct my_light_device_t
{
	struct light_device_t dev;
	int max_brightness;
};

static int read_from_file( const char * const file_name, int *result )
{
	int fd = -1;
	int ret = -1;
	fd = open( file_name, O_RDONLY );
	if( fd >= 0 ){
		char buf[20];
		ret = read( fd, buf, sizeof(buf) - 1 );
		close(fd);
		if( ret > 0 ) {
			buf[ret] = '\0';
			*result = atoi(buf);
			ret = 0;
		}
	}

	return ret;
}

static int write_to_file(const char * const file_name, int value)
{
	int fd;
	int ret = -1;

#ifdef LIGHTS_DEBUG
	ALOGD("write %d to %s\n", value, file_name);
#endif

	fd = open(file_name, O_RDWR);
	if (fd >= 0) {
		char buffer[20];
		int bytes;
		memset(buffer, 0, 20);
		bytes = sprintf(buffer, "%d", value);
		ret = write(fd, buffer, bytes);
		close(fd);
		if( ret <= 0 ) {
			ALOGE("%s, fail to set brightness to:%s, errno:%d\n", file_name, buffer, errno);
			ret = 0;
		}
	}

	return ret;
}

unsigned char get_color_b(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	unsigned char color = 0xff;

	if (state)
		color = (state->color & 0xff) * mydev->max_brightness / 0xff;

	return color;
}

unsigned char get_color_r(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	unsigned char color = 0xff;

	if (state)
		color = ((state->color >> 16) & 0xff) * mydev->max_brightness / 0xff;

	return color;
}

unsigned char get_color_g(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	unsigned char color = 0xff;

	if (state)
		color = ((state->color >> 8) & 0xff) * mydev->max_brightness / 0xff;

	return color;
}

int get_flash_on_ms(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	int flash_on_ms = 0;

	if (state)
		flash_on_ms = state->flashOnMS;

	return flash_on_ms;
}

int get_flash_off_ms(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	int flash_off_ms = 0;

	if (state)
		flash_off_ms = state->flashOffMS;

	return flash_off_ms;
}

int get_flash_mode(struct light_device_t *dev, struct light_state_t const *state)
{
	struct my_light_device_t *mydev = (struct my_light_device_t *)dev;
	int flash_mode = 0;

	if (state)
		flash_mode = state->flashMode;

	return flash_mode;
}

int lights_set_lcd(struct light_device_t *dev, struct light_state_t const *state)
{
	unsigned char brightness = get_color_b(dev, state);;

	write_to_file(LCD_BACKLIGHT, brightness);
#ifdef BUTTONS_VIA_LCD
	lights_set_button(dev, state);
#endif

	return 0;
}

int lights_set_keyboard(struct light_device_t *dev, struct light_state_t const *state)
{
	return 0;
}

int lights_set_button(struct light_device_t *dev, struct light_state_t const *state)
{
	unsigned char brightness = get_color_b(dev, state);

	write_to_file(BUTTON_BACKLIGHT, brightness);

	return 0;
}

int lights_set_battery(struct light_device_t *dev, struct light_state_t const *state)
{
	return 0;
}

int lights_set_notifications(struct light_device_t *dev, struct light_state_t const *state)
{
	unsigned char brightness[3];
	int flash_on_ms = get_flash_on_ms(dev, state);
	int flash_off_ms = get_flash_off_ms(dev, state);
	int flash_mode = get_flash_mode(dev, state);
	char path[64];
	int i;

#ifdef LIGHTS_DEBUG
	ALOGD("state: color: %x, flashOnMS: %d, flashOffMS: %d, flashMode: %d\n",
		state->color, state->flashOnMS, state->flashOffMS, state->flashMode);
#endif

	//Led0: green, Led1: blue, Led2: red
	brightness[0] = get_color_g(dev, state);
	brightness[1] = get_color_b(dev, state);
	brightness[2] = get_color_r(dev, state);
	for (i = 0; i < 3; i++) {
		LED_PATH(path, i, "led_mode");
		write_to_file(path, flash_mode);
		LED_PATH(path, i, "brightness");
		write_to_file(path, brightness[i]);
		LED_PATH(path, i, "delay_on");
		write_to_file(path, flash_on_ms);
		LED_PATH(path, i, "delay_off");
		write_to_file(path, flash_off_ms);
	}

	return 0;
}

int lights_set_attention(struct light_device_t *dev, struct light_state_t const *state)
{
	return 0;
}

static int lights_device_close(struct hw_device_t *dev)
{
	struct my_light_device_t *light_dev = (struct my_light_device_t *)dev;

	if (light_dev)
		free(light_dev);
	
	return 0;
}

static int lights_device_open(const struct hw_module_t *module,
				const char *name, struct hw_device_t **device)
{
	int	status = -EINVAL;
	int ret;
#ifdef LIGHTS_DEBUG
	ALOGD("lights_device_open: device name: %s\n", name);
#endif

	struct light_device_t *dev = NULL;
	struct my_light_device_t *mydev = NULL;
    
	mydev = (struct my_light_device_t *)calloc(sizeof(struct my_light_device_t), 1);
	dev = &(mydev->dev);
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = (struct hw_module_t *)(module);
	dev->common.close = lights_device_close;

	mydev->max_brightness = 0xff;//default max brightness is 255
	*device = &dev->common;
    
	if (!strcmp(name, LIGHT_ID_BACKLIGHT)) {
		dev->set_light = lights_set_lcd;
		read_from_file( LCD_MAX_BACKLIGHT, &mydev->max_brightness);
		ret = access(LCD_BACKLIGHT, F_OK);
		if (ret != 0)
			return status;
	} else if (!strcmp(name, LIGHT_ID_KEYBOARD)) {
		dev->set_light = lights_set_keyboard;
		ret = access(KEYBOARD_BACKLIGHT, F_OK);
		if (ret != 0)
			return status;
	} else if (!strcmp(name, LIGHT_ID_BUTTONS)) {
		dev->set_light = lights_set_button;
		ret = access(BUTTON_BACKLIGHT, F_OK);
		if (ret != 0)
			return status;
	} else if (!strcmp(name, LIGHT_ID_BATTERY)) {
		dev->set_light = lights_set_battery;
		ret = access(BATTERY_BACKLIGHT, F_OK);
		if (ret != 0)
			return status;
	} else if (!strcmp(name, LIGHT_ID_NOTIFICATIONS)) {
		char path[64];
		dev->set_light = lights_set_notifications;
		LED_PATH(path, 0, "brightness");
		ret = access(path, F_OK);
		if (ret != 0)
			return status;
	} else if (!strcmp(name, LIGHT_ID_ATTENTION)) {
		dev->set_light = lights_set_attention;
		ret = access(ATTENTION_BACKLIGHT, F_OK);
		if (ret != 0)
			return status;
	} else
		return status;

	return 0;
}
