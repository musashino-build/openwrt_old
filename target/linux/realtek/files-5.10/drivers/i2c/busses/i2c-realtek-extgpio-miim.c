// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek MIIM-to-I2C Driver
 *
 * Media Independent Interface Management (MIIM) is a name called
 * by Realtek, it is MDIO.
 * This driver provides a support for the interface to the external
 * GPIO chip (RTL8231) as a I2C bus.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>

#include <linux/mfd/realtek-led-ctrl-common.h>

#define DRIVER_VERSION			"0.01"

#define MIIM_DEF_PHYADDR_I2C_BASE	0x8
#define MIIM_PHYADDR_MAX		0x1f
#define MIIM_REG_MAX			0x1f

#define MIIM_MSG_MAX_RD			2
#define MIIM_MSG_MAX_WR			3

struct realtek_miim {
	struct i2c_adapter adap;
	struct realtek_led_ctrl_intf *intf;
	struct device *dev;
	struct mutex lock;
	size_t i2c_base;
//	struct clk *clk;
};

static int realtek_miim_master_xfer(struct i2c_adapter *adap,
				   struct i2c_msg *msgs, int num)
{
	struct realtek_miim *miim = i2c_get_adapdata(adap);
	struct realtek_led_ctrl_intf *intf = miim->intf;
	u8 addr, reg = MIIM_REG_INVAL;
	u16 val = 0;
	int i, ret, len;
	bool do_read;

	/*
	 * - msg0->write (buf->reg only),
	 *   msg1->read                   : i2cget with register addr
	 * - msg0->write (buf->reg + data): i2cset
	 * - msg0->read                   : i2cget with no register addr, i2cdetect
	 */
	for (i = 0; i < num; i++) {
		do_read = !!(msgs[i].flags & I2C_M_RD);
		len = msgs[i].len;

		/* write address */
		addr = i2c_8bit_addr_from_msg(&msgs[i]);

		dev_dbg(miim->dev,
			"msg_num-> %d, cur_num-> %d, slave addr-> 0x%02x (0x%02x), buflen-> %d\n",
			num, i, addr, addr >> 1, msgs[i].len);

		if ((do_read && msgs[i].len > 2) ||
		    (!do_read && msgs[i].len > 3)) {
			dev_err(miim->dev, "message buffer is too large\n");
			return -EINVAL;
		}

		if (do_read) {
//			dev_info(miim->dev,
//				 "READ , addr-> 0x%02x (raw: 0x%02x), reg-> 0x%02x, read_len-> %d\n",
//				 addr >> 1, addr, reg, len);
			break;
		}

		reg = msgs[i].buf[0];
		if (reg > MIIM_REG_MAX)
			return -EINVAL;

		if (len - 1 > 0) {
			memcpy(&val, &msgs[i].buf[1], msgs[i].len - 1);
			val = cpu_to_le16(val);
		}

//		dev_info(miim->dev,
//			 "WRITE, addr-> 0x%02x (raw: 0x%02x), reg-> 0x%02x, val-> 0x%04x, data_len-> %d\n",
//			 addr >> 1, addr, reg, val, len - 1);
	}

	addr >>= 1;
	if (addr - miim->i2c_base > MIIM_PHYADDR_MAX)
		return -EINVAL;

	if (do_read) {
		ret = intf->miim_ops->miim_read(intf,
					      addr - miim->i2c_base,
					      reg, &val);
		if (ret)
			goto exit;
//		dev_info(miim->dev, "read val-> 0x%04x\n", val);
		val = cpu_to_le16(val);
		memcpy(msgs[i].buf, &val, len);

	} else
		ret = intf->miim_ops->miim_write(intf,
					       addr - miim->i2c_base,
					       reg, val);

exit:
	return ret ? ret : num;
}

static uint32_t realtek_miim_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA;
}

static const struct i2c_adapter_quirks realtek_miim_quirks = {
	.flags = I2C_AQ_COMB_WRITE_THEN_READ | I2C_AQ_NO_REP_START,
	.max_write_len = MIIM_MSG_MAX_WR,
	.max_read_len = MIIM_MSG_MAX_RD,
};

static const struct i2c_algorithm realtek_miim_algo = {
	.master_xfer	= realtek_miim_master_xfer,
	.functionality	= realtek_miim_func,
};

static int realtek_extgpio_miim_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct realtek_led_ctrl_intf *intf = dev_get_drvdata(dev->parent);
	struct realtek_miim *miim;
	int ret;

	pr_info("Realtek MIIM-to-I2C driver v%s\n", DRIVER_VERSION);

	miim = devm_kzalloc(dev, sizeof(*miim), GFP_KERNEL);
	if (!miim) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(dev->of_node,
				   "realtek,i2c-base-address",
				   &miim->i2c_base);
	if (ret || 
	    miim->i2c_base < 0x8 || 0x58 < miim->i2c_base) {
		dev_warn(dev,
			 "no or invalid base address of I2C specified, using default (%d)",
			 MIIM_DEF_PHYADDR_I2C_BASE);
		miim->i2c_base = MIIM_DEF_PHYADDR_I2C_BASE;
	} else {
		dev_info(dev, "I2C base address-> 0x%02x\n",
			 miim->i2c_base);
	}

	miim->intf = intf;

	miim->dev = dev;
	miim->adap.owner = THIS_MODULE;
	miim->adap.algo = &realtek_miim_algo;
	miim->adap.quirks = &realtek_miim_quirks;
	miim->adap.retries = 1;
	miim->adap.dev.parent = dev;
	miim->adap.dev.of_node = dev->of_node;
	strlcpy(miim->adap.name, dev_name(dev), sizeof(miim->adap.name));

	i2c_set_adapdata(&miim->adap, miim);

	dev_set_drvdata(dev, miim);

	mutex_init(&miim->lock);

	ret = i2c_add_adapter(&miim->adap);
	if (ret)
		return ret;

	intf->miim_ops->miim_enable(intf);

	return 0;
}

static const struct of_device_id realtek_extgpio_miim_ids[] = {
	{ .compatible = "realtek,extgpio-miim-i2c" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, realtek_extgpio_miim_ids);

static struct platform_driver realtek_extgpio_miim_driver = {
	.probe = realtek_extgpio_miim_probe,
	.driver = {
		.name = "realtek-extgpio-miim-i2c",
		.of_match_table = realtek_extgpio_miim_ids,
	},
};
module_platform_driver(realtek_extgpio_miim_driver);

MODULE_DESCRIPTION("Realtek MIIM-to-I2C driver for external GPIO chip");
MODULE_LICENSE("GPL v2");
