#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#define V4L2_SUBDEV_GID_LOG_SUBDEV	0x12345678

/*
 * @name:	name of this host sub-device
 * @subdev:	the container subdev which acts on behalf of all containee
 * @dev:	short cut to device
 * @pad:	the pad entity that is used by the container subdev
 * @guest_list	a list to link up all the containee
 * @guest_cnt	number of contained subdev
 */
struct host_subdev {
	char			*name;
	struct v4l2_subdev	subdev;
	struct device		*dev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;

	struct list_head	guest_list;
	int			guest_cnt;
};

/*
 * @hook:	the list head hook used to link to host_subdev::guest_list
 * @guest:	pointer to the contained subdev, A.K.A.: guest subdev
 * @type:	type code used to identify guest subdev
 * @addr:	address code used to identify guest subdev
 * @host	pointer to the container subdev, A.K.A.: host subdev
 */
struct guest_dscr {
	struct list_head	hook;
	struct v4l2_subdev	*guest;
	__u32			type;
	__u32			addr;
	struct host_subdev	*host;
};

int host_subdev_add_guest(struct host_subdev *host, struct v4l2_subdev *guest,
				__u32 type, __u32 addr);
struct v4l2_subdev *get_guest_sd(struct host_subdev *host,
					__u32 type, __u32 addr);
struct v4l2_subdev *get_host_sd(struct v4l2_subdev *guest);
struct host_subdev *host_subdev_create(struct device *parent,
					const char *name, int id, void *pdata);
