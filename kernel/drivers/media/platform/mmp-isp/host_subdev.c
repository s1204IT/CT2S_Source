#include <linux/module.h>
#include <linux/platform_device.h>

#include <media/host_subdev.h>

#define HOST_SUBDEV_DRV_NAME	"host-subdev-pdrv"

struct v4l2_subdev *get_guest_sd(struct host_subdev *host,
					__u32 type, __u32 addr)
{
	struct guest_dscr *dscr;

	if (unlikely(host == NULL))
		return NULL;

	list_for_each_entry(dscr, &host->guest_list, hook)
		if (dscr->type == type && dscr->addr == addr)
			return dscr->guest;
	return NULL;
}
EXPORT_SYMBOL(get_guest_sd);

struct v4l2_subdev *get_host_sd(struct v4l2_subdev *guest)
{
	struct host_subdev *host = v4l2_get_subdev_hostdata(guest);

	if (host && (host->subdev.grp_id == V4L2_SUBDEV_GID_LOG_SUBDEV))
		return &host->subdev;
	return NULL;
}
EXPORT_SYMBOL(get_host_sd);

int host_subdev_add_guest(struct host_subdev *host, struct v4l2_subdev *guest,
				__u32 type, __u32 addr)
{
	struct guest_dscr *dscr;

	if (unlikely(host == NULL))
		return -EINVAL;

	dscr = devm_kzalloc(host->dev, sizeof(struct guest_dscr), GFP_KERNEL);
	if (unlikely(dscr == NULL))
		return -ENOMEM;

	dscr->guest = guest;
	dscr->type = type;
	dscr->addr = addr;
	dscr->host = host;
	INIT_LIST_HEAD(&dscr->hook);
	list_add_tail(&dscr->hook, &host->guest_list);
	host->guest_cnt++;
	v4l2_set_subdev_hostdata(guest, host);
	return v4l2_ctrl_add_handler(&host->ctrl_handler,
						guest->ctrl_handler, NULL);
}
EXPORT_SYMBOL(host_subdev_add_guest);

struct host_subdev *host_subdev_create(struct device *parent,
					const char *name, int id, void *pdata)
{
	struct platform_device *hdev = devm_kzalloc(parent,
						sizeof(struct platform_device),
						GFP_KERNEL);
	int ret = 0;

	if (hdev == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	hdev->name = HOST_SUBDEV_DRV_NAME;
	hdev->id = id;
	hdev->dev.platform_data = pdata;
	ret = platform_device_register(hdev);
	if (ret < 0) {
		pr_err("unable to create host subdev: %d\n", ret);
		goto err;
	}

	return platform_get_drvdata(hdev);
err:
	return NULL;
}
EXPORT_SYMBOL(host_subdev_create);
/**************************** dispatching functions ***************************/

static int hsd_core_init(struct v4l2_subdev *hsd, u32 val)
{
	struct host_subdev *host = v4l2_get_subdevdata(hsd);
	struct guest_dscr *dscr;
	int ret = 0;

	list_for_each_entry(dscr, &host->guest_list, hook) {
		ret = v4l2_subdev_call(dscr->guest, core, init, val);
		if (ret < 0)
			return ret;
	}
	/*
	 * At init time, host subdev configure its subdev function handler
	 * to enable dispatching to guest subdevs. maybe move to hsd_core_init?
	 */
	/*
	 * the strategy is:
	 * if (guest_op) op = (op)? hsd_op : guest_op;
	 */
	ret = v4l2_ctrl_handler_setup(&host->ctrl_handler);
	if (ret < 0)
		goto err;
err:
	return ret;
}

static long hsd_core_ioctl(struct v4l2_subdev *hsd, unsigned int cmd, void *arg)
{
	struct host_subdev *host = v4l2_get_subdevdata(hsd);
	struct guest_dscr *dscr;
	int ret = 0;

	list_for_each_entry(dscr, &host->guest_list, hook) {
		ret = v4l2_subdev_call(dscr->guest, core, ioctl, cmd, arg);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int hsd_core_g_register(struct v4l2_subdev *hsd,
				struct v4l2_dbg_register *reg)
{
	struct host_subdev *host = v4l2_get_subdevdata(hsd);
	struct guest_dscr *dscr;
	int ret = 0;

	list_for_each_entry(dscr, &host->guest_list, hook) {
		/*
		 * use match.type and match.addr as mask to decide
		 * which guest(s) to dispatch this action to
		 */
		if (((dscr->type & reg->match.type) != dscr->type) ||
			((dscr->addr & reg->match.addr) != dscr->addr))
			continue;
		ret = v4l2_subdev_call(dscr->guest, core, g_register, reg);
		/* only get value from the 1st matched subdev */
		if (ret >= 0)
			break;
	}
	return 0;
}

static int hsd_core_s_register(struct v4l2_subdev *hsd,
				const struct v4l2_dbg_register *reg)
{
	struct host_subdev *host = v4l2_get_subdevdata(hsd);
	struct guest_dscr *dscr;
	int ret = 0;

	list_for_each_entry(dscr, &host->guest_list, hook) {
		/*
		 * use match.type and match.addr as mask to decide
		 * which guest(s) to dispatch this action to
		 */
		if (((dscr->type & reg->match.type) != dscr->type) ||
			((dscr->addr & reg->match.addr) != dscr->addr))
			continue;
		ret = v4l2_subdev_call(dscr->guest, core, s_register, reg);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* TODO: Add more hsd_OPS_FN here */

static const struct v4l2_subdev_core_ops hsd_core_ops = {
	.init		= &hsd_core_init,
	.ioctl		= &hsd_core_ioctl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= &hsd_core_g_register,
	.s_register	= &hsd_core_s_register,
#endif
};
static const struct v4l2_subdev_video_ops hsd_video_ops;
static const struct v4l2_subdev_sensor_ops hsd_sensor_ops;
static const struct v4l2_subdev_pad_ops hsd_pad_ops;
/* default version of host subdev just dispatch every subdev call to guests */
static const struct v4l2_subdev_ops hsd_ops = {
	.core	= &hsd_core_ops,
	.video	= &hsd_video_ops,
	.sensor	= &hsd_sensor_ops,
	.pad	= &hsd_pad_ops,
};

/************************* host subdev implementation *************************/

static int host_subdev_open(struct v4l2_subdev *hsd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int host_subdev_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_core_ops host_subdev_core_ops;
static const struct v4l2_subdev_video_ops host_subdev_video_ops;
static const struct v4l2_subdev_sensor_ops host_subdev_sensor_ops;
static const struct v4l2_subdev_pad_ops host_subdev_pad_ops;
static const struct v4l2_subdev_ops host_subdev_ops = {
	.core	= &host_subdev_core_ops,
	.video	= &host_subdev_video_ops,
	.sensor	= &host_subdev_sensor_ops,
	.pad	= &host_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops host_subdev_internal_ops = {
	.open	= host_subdev_open,
	.close	= host_subdev_close,
};

static int host_subdev_remove(struct platform_device *pdev)
{
	struct host_subdev *host = platform_get_drvdata(pdev);

	if (unlikely(host == NULL))
		return -EINVAL;

	/* for each physical device, unbund */
	while (!list_empty(&host->guest_list)) {
		struct guest_dscr *dscr = list_first_entry(&host->guest_list,
					struct guest_dscr, hook);
		v4l2_set_subdev_hostdata(dscr->guest, NULL);
		list_del(&dscr->hook);
		host->guest_cnt--;
		devm_kfree(host->dev, dscr);
	}
	media_entity_cleanup(&host->subdev.entity);
	v4l2_ctrl_handler_free(&host->ctrl_handler);
	devm_kfree(host->dev, host);
	return 0;
}

static int host_subdev_probe(struct platform_device *pdev)
{
	/* pdev->dev.platform_data */
	struct host_subdev *host;
	struct v4l2_subdev *sd;
	int ret = 0;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (unlikely(host == NULL))
		return -ENOMEM;

	platform_set_drvdata(pdev, host);
	host->dev = &pdev->dev;
	INIT_LIST_HEAD(&host->guest_list);

	ret = v4l2_ctrl_handler_init(&host->ctrl_handler, 16);
	if (ret < 0)
		goto err;
	host->subdev.ctrl_handler = &host->ctrl_handler;

	/* v4l2_subdev_init(sd, &host_subdev_ops); */
	sd = &host->subdev;
	ret = media_entity_init(&sd->entity, 1, &host->pad, 0);
	if (ret < 0)
		goto err;
	v4l2_subdev_init(sd, &hsd_ops);
	sd->internal_ops = &host_subdev_internal_ops;
	v4l2_set_subdevdata(sd, host);
	sd->grp_id = V4L2_SUBDEV_GID_LOG_SUBDEV;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strcpy(sd->name, pdev->name);
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	dev_info(host->dev, "host subdev created\n");
	return ret;
err:
	host_subdev_remove(pdev);
	return ret;
}

static struct platform_driver __refdata host_subdev_pdrv = {
	.probe	= host_subdev_probe,
	.remove	= host_subdev_remove,
	.driver	= {
		.name   = HOST_SUBDEV_DRV_NAME,
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(host_subdev_pdrv);

MODULE_DESCRIPTION("host v4l2 subdev bus driver");
MODULE_AUTHOR("Jiaquan Su <jiaquan.lnx@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:host-subdev-pdrv");
