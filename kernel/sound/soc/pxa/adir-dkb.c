/*
 * linux/sound/soc/pxa/adir-dkb.c
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/regs-addr.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>


#define AUGP_APPS_INT_ADMA1_MASK	(0x1 << 19)
#define AUGP_APPS_INT_MASK	0x14

static int sspa_master = 1;
static DEVICE_INT_ATTR(ssp_master, 0644, sspa_master);

static struct of_device_id mmp_88pm805_dt_ids[] = {
	{ .compatible = "marvell,mmp-88pm805-snd-card", },
	{}
};
MODULE_DEVICE_TABLE(of, mmp_88pm805_dt_ids);

static int mmp_88pm805_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	cpu_dai->driver->playback.formats = SNDRV_PCM_FMTBIT_S16_LE;
	cpu_dai->driver->capture.formats = SNDRV_PCM_FMTBIT_S16_LE;
	cpu_dai->driver->playback.rates = SNDRV_PCM_RATE_44100;
	cpu_dai->driver->capture.rates = SNDRV_PCM_RATE_44100;
	return 0;
}

static int mmp_88pm805_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	if (sspa_master) {
		snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	} else {
		snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	}

	return 0;
}

static int i2s_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	int reg_audio_ckctr;

	reg_audio_ckctr = __raw_readl(get_audio_base_va() +
		AUGP_APPS_INT_MASK);
	reg_audio_ckctr |= AUGP_APPS_INT_ADMA1_MASK;
	__raw_writel(reg_audio_ckctr, get_audio_base_va() +
		AUGP_APPS_INT_MASK);
	return 0;
}

static struct snd_soc_ops mmp_88pm805_machine_ops[] = {
	{
	 .startup = mmp_88pm805_startup,
	 .hw_params = mmp_88pm805_hw_params,
	 },
};

static struct snd_soc_dai_link adir_asoc_dai[] = {
	{
	 .name = "88pm805 i2s",
	 .stream_name = "adir audio",
	 .codec_name = "88pm80x-codec",
	 .platform_name = "mmp-sspa-dai.0",
	 .cpu_dai_name = "mmp-sspa-dai.0",
	 .codec_dai_name = "88pm805-i2s",
	 .ops = &mmp_88pm805_machine_ops[0],
	 .init = i2s_dai_init,
	},
};

static struct snd_soc_card snd_soc_mmp_88pm805 = {
	 .name = "adir-dkb-hifi",
	 .dai_link = &adir_asoc_dai[0],
	 .num_links = ARRAY_SIZE(adir_asoc_dai),
};

static int mmp_88pm805_probe_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *sspa_np;
	int ret = 0;

	if (!np)
		return 1; /* no device tree */

	sspa_np = of_parse_phandle(np, "mrvl,sspa-controllers", 0);
	if (!sspa_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		return -EINVAL;
	}

	/* now, we don't use get codec node from dt*/
	adir_asoc_dai[0].cpu_dai_name = NULL;
	adir_asoc_dai[0].cpu_of_node = sspa_np;
	adir_asoc_dai[0].platform_name = NULL;
	adir_asoc_dai[0].platform_of_node = sspa_np;

	of_node_put(sspa_np);

	return ret;
}

static int mmp_88pm805_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	card = &snd_soc_mmp_88pm805;

	ret = mmp_88pm805_probe_dt(pdev);
	if (ret < 0)
		return ret;

	card->dev = &pdev->dev;
	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
				ret);

	/* add sspa_master sysfs entries */
	ret = device_create_file(&pdev->dev, &dev_attr_ssp_master.attr);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"%s: failed to add sspa_master sysfs files: %d\n",
			__func__, ret);
		goto err;
	}

	return ret;
err:
	snd_soc_unregister_card(card);

	return ret;

}

static int mmp_88pm805_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_ssp_master.attr);
	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver mmp_88pm805_audio_driver = {
	.driver		= {
		.name	= "mmp-88pm805-audio-hifi",
		.owner	= THIS_MODULE,
		.of_match_table = mmp_88pm805_dt_ids,
	},
	.probe		= mmp_88pm805_audio_probe,
	.remove		= mmp_88pm805_audio_remove,
};

module_platform_driver(mmp_88pm805_audio_driver);

/* Module information */
MODULE_DESCRIPTION("ALSA SoC 88PM805 Adir");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mmp-88pm805-audio-hifi");
