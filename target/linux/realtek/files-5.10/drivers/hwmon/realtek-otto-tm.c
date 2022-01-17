// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Thermal meter driver for Realtek RTL838x/RTL839x SoC
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/hwmon.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

/* Registers */
#define TM_CTRL0		0x0
#  define LOWCMP_EN_OFF		  30
#  define LOWCMP_EN_MASK	  BIT(LOWCMP_EN_OFF)
#  define LOW_THR_OFF		  24
#  define LOW_THR_MASK		  GENMASK(29,24)
#  define HIGHCMP_EN_OFF	  22
#  define HIGHCMP_EN_MASK	  BIT(HIGHCMP_EN_OFF)
#  define HIGH_THR_OFF		  16
#  define HIGH_THR_MASK		  GENMASK(21,16)
#  define REV_CMP_OUT_OFF	  1
#  define REV_CMP_OUT_MASK	  BIT(REV_CMP_OUT_OFF)
#  define ENABLE_OFF		  0
#  define ENABLE_MASK		  BIT(ENABLE_OFF)

#define TM_CTRL1		0x4
#  define PWRON_DLY_OFF		  0
#  define PWRON_DLY_MASK	  GENMASK(15,0)

#define TM_CTRL2		0x8
#  define COMPARE_DLY_OFF	  16
#  define COMPARE_DLY_MASK	  GENMASK(31,16)
#  define SAMPLE_DLY_OFF	  0
#  define SAMPLE_DLY_MASK	  GENMASK(15,0)

#define TM_RESULT		0xc
#  define DATA_SAMPLED_OFF	  9
#  define DATA_SAMPLED_MASK	  BIT(DATA_SAMPLED_OFF)
#  define DATA_VALID_OFF	  8
#  define DATA_VALID_MASK	  BIT(DATA_VALID_OFF)
#  define TEMP_OUT_OFF		  0
#  define TEMP_OUT_MASK		  GENMASK(5,0)

/* Constant Values */
#define INTR_LOW		BIT(1)
#define INTR_HIGH		BIT(0)

#define DRIVER_VERSION		"0.01"

struct realtek_thermal_meter {
	void __iomem *base;
	struct regmap *intr_regmap;
	u32 imr;
	u32 isr;
	struct device *dev;
};

static u32 tm_r32(struct realtek_thermal_meter *tm, u32 reg)
{
	return ioread32(tm->base + reg);
}

static void tm_w32(struct realtek_thermal_meter *tm, u32 reg,
		   u32 val)
{
	iowrite32(val, tm->base + reg);
}

static void tm_rmw32(struct realtek_thermal_meter *tm, u32 reg,
		     u32 mask, u32 val)
{
	u32 _val;

	_val = tm_r32(tm, reg);
	_val &= ~mask;
	_val |= val;
	tm_w32(tm, reg, _val);
}

static bool tm_is_enable(struct realtek_thermal_meter *tm)
{
	return !!(tm_r32(tm, TM_CTRL0) & ENABLE_MASK);
}

static int tm_temp_read_validate(struct realtek_thermal_meter *tm,
				 long *val)
{
	u32 _val;

	_val = tm_r32(tm, TM_RESULT);
	if (!(_val & DATA_VALID_MASK)) {
		if (!tm_is_enable(tm))
			dev_warn(tm->dev, "thermal meter is disabled\n");
		return -EINVAL;
	}
	_val &= TEMP_OUT_MASK;

	*val = _val;

	return 0;
}

static int tm_temp_read(struct realtek_thermal_meter *tm, u32 attr,
			long *val)
{
	u32 _val;
	size_t off;
	unsigned long mask;

	if (attr == hwmon_temp_input)
		return tm_temp_read_validate(tm, val);

	switch (attr) {
	case hwmon_temp_min:
		mask = LOW_THR_MASK;
		break;
	case hwmon_temp_max:
		mask = HIGH_THR_MASK;
		break;
	case hwmon_temp_min_alarm:
		mask = LOWCMP_EN_MASK;
		break;
	case hwmon_temp_max_alarm:
		mask = HIGHCMP_EN_MASK;
		break;
	default:
		return -EINVAL;
	}

	off = find_first_bit(&mask, 32);
	_val = tm_r32(tm, TM_CTRL0);
	_val &= mask;
	_val >>= off;
	*val = _val;

	return 0;
}

static int tm_chip_read(struct realtek_thermal_meter *tm, u32 attr,
			long *val)
{
	u32 _val;

	switch (attr) {
	case hwmon_chip_update_interval:
		_val = tm_r32(tm, TM_CTRL2);
		_val &= SAMPLE_DLY_MASK;
		*val = _val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tm_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int chan, long *val)
{
	struct realtek_thermal_meter *tm = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return tm_chip_read(tm, attr, val);
	case hwmon_temp:
		return tm_temp_read(tm, attr, val);
	default:
		return -EINVAL;
	}

	return 0;
};

static void tm_imr_set(struct realtek_thermal_meter *tm, u32 mask,
		       u32 bits)
{
	if (!tm->intr_regmap)
		return;

	regmap_update_bits(tm->intr_regmap, tm->imr, mask, bits);
}

static void tm_enable(struct realtek_thermal_meter *tm, bool en)
{
	tm_rmw32(tm, TM_CTRL0, ENABLE_MASK, en ? 1 : 0);
}

static int tm_temp_write(struct device *dev, u32 attr, long val)
{
	struct realtek_thermal_meter *tm = dev_get_drvdata(dev);
	size_t off;
	unsigned long mask;

	switch (attr) {
	case hwmon_temp_min:
		mask = LOW_THR_MASK;
		if (val < 0 || FIELD_MAX(LOW_THR_MASK) < val)
			return -EINVAL;
		break;
	case hwmon_temp_max:
		mask = HIGH_THR_MASK;
		if (val < 0 || FIELD_MAX(HIGH_THR_MASK) < val)
			return -EINVAL;
		break;
	case hwmon_temp_min_alarm:
		mask = LOWCMP_EN_MASK;
		val = !!(0 < val);
		tm_imr_set(tm, INTR_LOW, val ? INTR_LOW : 0);
		break;
	case hwmon_temp_max_alarm:
		mask = HIGHCMP_EN_MASK;
		val = !!(0 < val);
		tm_imr_set(tm, INTR_HIGH, val ? INTR_HIGH: 0);
		break;
	default:
		return -EINVAL;
	}

	off = find_first_bit(&mask, 32);
	tm_rmw32(tm, TM_CTRL0, mask, (u32)val << off);

	return 0;
}

static int tm_chip_write(struct device *dev, u32 attr, long val)
{
	struct realtek_thermal_meter *tm = dev_get_drvdata(dev);
//	u32 _val;

	switch (attr) {
	case hwmon_chip_update_interval:
		if (val < 0 ||
		    FIELD_MAX(SAMPLE_DLY_MASK) < val)
			return -EINVAL;
//		_val = ioread32(tm->base + TM_CTRL2);
//		_val &= ~SAMPLE_DLY_MASK;
//		_val |= (u32)val;
//		iowrite32(_val, tm->base + TM_CTRL2);
		tm_rmw32(tm, TM_CTRL2, SAMPLE_DLY_MASK, (u32)val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tm_write(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int chan, long val)
{
	switch (type) {
	case hwmon_chip:
		return tm_chip_write(dev, attr, val);
	case hwmon_temp:
		return tm_temp_write(dev, attr, val);
	default:
		return -EINVAL;
	}

	return 0;
}

static umode_t tm_is_visible(const void *data,
			     enum hwmon_sensor_types type,
			     u32 attr, int chan)
{
	switch (type) {
	case hwmon_chip:
		if (attr == hwmon_chip_update_interval)
			return 0644;
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
			return 0644;
		}
		break;
	default:
		break;
	}

	return 0;
}

static irqreturn_t tm_irq(int irq, void *data)
{
	struct realtek_thermal_meter *tm = data;
	u32 isr;
	long temp, thr;
	int ret;
	bool is_low;
	/*
	 * 1. read ISR and validate for TM
	 * 2. check high or low
	 * 3. read temp from result reg and check valid
	 * 4. read threshold value and compare with current temp
	 * 5. clear bit on ISR
	 */
	ret = regmap_read(tm->intr_regmap, tm->isr, &isr);
	if (ret) {
		dev_err(tm->dev,
			"failed to read ISR from regmap %d\n", ret);
		return IRQ_NONE;
	}
	/* if isr == 0, interrupt was not from this device */
	if (!isr)
		return IRQ_NONE;

	is_low = !!(isr & INTR_LOW);
	
	ret = tm_temp_read_validate(tm, &temp);
	if (ret)
		return IRQ_NONE;

	tm_temp_read(tm, is_low ? hwmon_temp_min : hwmon_temp_max, &thr);
	dev_info(tm->dev, "interrupt (%s): temp-> %ld, threshold-> %ld\n",
		 is_low ? "Low" : "High", temp, thr);
	/*
	 * if current temp is not reaching, interrupt may
	 * have been from other thermal meter
	 */
	if ((is_low && thr <= temp) || (!is_low && temp <= thr)) {
		dev_dbg(tm->dev,
			"current temperature is not reaching threshold\n");
		return IRQ_NONE;
	}

	regmap_write(tm->intr_regmap, tm->isr,
		     is_low ? INTR_LOW : INTR_HIGH);

	return IRQ_HANDLED;
}

static const struct hwmon_channel_info *realtek_otto_tm_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM),
	NULL
};

static const struct hwmon_ops realtek_otto_tm_ops = {
	.is_visible = tm_is_visible,
	.read = tm_read,
	.write = tm_write,
};

static const struct hwmon_chip_info realtek_otto_tm_chip_info = {
	.ops = &realtek_otto_tm_ops,
	.info = realtek_otto_tm_info,
};

static int realtek_irq_setup(struct realtek_thermal_meter *tm, int irq)
{
	struct device_node *np = tm->dev->of_node, *intc_np;
	struct of_phandle_args arg;
	const u32 *prop;
	size_t index;
	int ret, proplen, i;

	ret = devm_request_irq(tm->dev, irq, tm_irq, IRQF_SHARED,
			       dev_name(tm->dev), tm);
	if (ret) {
		dev_err(tm->dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	/* get phandle to swcore-intc with register index */
	ret = of_parse_phandle_with_fixed_args(
			np, "realtek,swcore-interrupt-reg", 1, 0, &arg);
	if (ret)
		return ret;

	intc_np = arg.np;
	index = arg.args[0];
	if (index < 0)
		return -EINVAL;

	tm->intr_regmap = syscon_node_to_regmap(intc_np);
	if (IS_ERR(tm->intr_regmap))
		return PTR_ERR(tm->intr_regmap);

	prop = of_get_property(intc_np, "realtek,register-list",
			       &proplen);
	if (!prop)
		return -ENOENT;

	if (proplen % 3) {
		dev_err(tm->dev,
			"register list has invalid length (%d)\n",
			proplen);
		return -EINVAL;
	}

	/* search register index */
	for (i = 0; i < proplen; i += 3 * sizeof(u32), prop += 3) {
		if (*prop != index)
			continue;

		tm->imr = *(prop + 1);
		tm->isr = *(prop + 2);

		dev_dbg(tm->dev, "imr-> 0x%04x, isr-> 0x%04x\n",
			 tm->imr, tm->isr);
		return 0;
	}

	return -EINVAL;
}

static int realtek_otto_tm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon;
	struct realtek_thermal_meter *tm;
	int ret, irq;

	pr_info("Realtek Thermal Meter driver v%s\n", DRIVER_VERSION);

	tm = devm_kzalloc(dev, sizeof(*tm), GFP_KERNEL);
	if (!tm) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	tm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tm->base)) {
		dev_err(dev, "failed to remap resource\n");
		return PTR_ERR(tm->base);
	}

	tm->dev = dev;

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		dev_info(dev, "got irq from dt: %d\n", irq);

		ret = realtek_irq_setup(tm, irq);
		if (ret)
			return ret;
	} else {
		dev_warn(dev, "no irq available, disabling...");
	}

	dev_set_drvdata(dev, tm);

	hwmon = devm_hwmon_device_register_with_info(dev, dev_name(dev),
						     tm,
						     &realtek_otto_tm_chip_info,
						     NULL);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	tm_enable(tm, true);

	return 0;
}

static const struct of_device_id realtek_otto_tm_ids[] = {
	{ .compatible = "realtek,rtl838x-thermal", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_otto_tm_ids)

static struct platform_driver realtek_otto_tm_driver = {
	.probe = realtek_otto_tm_probe,
	.driver = {
		.name = "realtek-otto-thermal",
		.owner = THIS_MODULE,
		.of_match_table = realtek_otto_tm_ids,
	},
};
module_platform_driver(realtek_otto_tm_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Thermal meter driver for Realtek RTL838x/RTL839x SoC");
