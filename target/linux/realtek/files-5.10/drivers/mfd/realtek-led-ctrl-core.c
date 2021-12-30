// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/mfd/realtek-led-ctrl-common.h>
#include <linux/mfd/realtek-led-ctrl-core.h>

#define DRIVER_VERSION		"0.01"
#define MFD_CHILD_DEVS		2

int realtek_leds_init(struct realtek_led_ctrl *ctrl);
int realtek_miim_init(struct realtek_led_ctrl *ctrl);

unsigned int rtl_r32(struct realtek_led_ctrl *ledc, unsigned int reg)
{
	return ioread32(ledc->base + reg);
}

void rtl_w32(struct realtek_led_ctrl *ledc, unsigned int reg,
	     unsigned int val)
{
	iowrite32(val, ledc->base + reg);
}

void rtl_rmw32(struct realtek_led_ctrl *ledc, unsigned int reg,
	       unsigned int mask, unsigned int val)
{
	unsigned int _val;

	_val = rtl_r32(ledc, reg);
	_val &= ~mask;
	_val |= val;
	rtl_w32(ledc, reg, _val);
}

struct realtek_led_ctrl *intf_to_ledc(struct realtek_led_ctrl_intf *_intf)
{
	return container_of(_intf, struct realtek_led_ctrl, intf);
}

static const struct mfd_cell realtek_led_mfd_cells[] = {
	{
		.name		= "port-leds",
		.of_compatible	= "realtek,port-leds",
	},
	{
		.name		= "extgpio-miim",
		.of_compatible	= "realtek,extgpio-miim-i2c",
	},
};

static const struct realtek_led_ctrl_info realtek_ledc_info[] = {
	{
		.family = RTL8380_FAMILY_ID,
		.port_max = RTL838X_PORT_MAX,
		.pset_max = LED_PSET0,
		.pled_max = RTL83XX_PLED_NUM_MAX,
	},
	{
		.family = RTL8390_FAMILY_ID,
		.port_max = RTL839X_PORT_MAX,
		.pset_max = LED_PSET3,
		.pled_max = RTL83XX_PLED_NUM_MAX,
	},
};

static const struct of_device_id realtek_led_ctrl_ids[] = {
	{
		.compatible = "realtek,rtl838x-led-controller",
		.data = &realtek_ledc_info[0],
	},
	{
		.compatible = "realtek,rtl839x-led-controller",
		.data = &realtek_ledc_info[1],
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_led_ctrl_ids);

static int realtek_led_ctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct realtek_led_ctrl *ledc;

	pr_info("Realtek LED Controller Driver v%s\n", DRIVER_VERSION);

	ledc = devm_kzalloc(dev, sizeof(*ledc), GFP_KERNEL);
	if (!ledc) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	ledc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ledc->base))
		return PTR_ERR(ledc->base);

	spin_lock_init(&ledc->glb_lock);
	spin_lock_init(&ledc->pled_lock);
	mutex_init(&ledc->miim_lock);

	id = of_match_node(realtek_led_ctrl_ids, dev->of_node);
	ledc->info = (struct realtek_led_ctrl_info *)id->data;
	ledc->dev = dev;

	ret = realtek_leds_init(ledc);
	if (ret) {
		dev_err(dev, "failed to initialize LED functions\n");
		return ret;
	}

	ret = realtek_miim_init(ledc);
	if (ret) {
		dev_err(dev, "failed to initialize SMI functions\n");
		return ret;
	}

	dev_set_drvdata(dev, &ledc->intf);

	return devm_mfd_add_devices(dev, 0, realtek_led_mfd_cells,
				    MFD_CHILD_DEVS, NULL, 0, NULL);
}

static struct platform_driver realtek_led_ctrl_driver = {
	.probe = realtek_led_ctrl_probe,
//	.remove = 
	.driver = {
		.name = "realtek-led-controller",
		.of_match_table = realtek_led_ctrl_ids,
	},
};

module_platform_driver(realtek_led_ctrl_driver);

MODULE_DESCRIPTION("Realek Global LED Controller driver");
MODULE_LICENSE("GPL v2");
