// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek LED Controller
 *   - Media Independent Interface Management (MIIM, MDIO) for the
 *     external GPIO expander (RTL8231)
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/iopoll.h>
#include <linux/bitfield.h>

#include <linux/mfd/realtek-led-ctrl-common.h>
#include <linux/mfd/realtek-led-ctrl-core.h>
#include <linux/mfd/realtek-led-ctrl-miim.h>

#define MIIM_TIMEOUT_MS		3

static int miim_wait(struct realtek_led_ctrl *ledc, uint32_t *val)
{
	const struct realtek_miim_regs *regs = ledc->info->miim_regs;
	int ret;
	uint32_t _val;

//	dev_info(ledc->dev, "%s called\n", __func__);

	ret = readl_relaxed_poll_timeout(ledc->base + regs->indrt_acc,
					 _val, !(_val & GPIO_CMD_MASK),
					 10, MIIM_TIMEOUT_MS * 1000);

//	if (ret)
//		dev_err(ledc->dev, "miim timeout: %d\n", ret);
	dev_dbg(ledc->dev, "wait result-> %d, val-> 0x%08x\n", ret, _val);

	if (val)
		*val = _val;

	return ret;
}

static int miim_read(struct realtek_led_ctrl_intf *intf,
		     u8 addr, u8 reg, u16 *data)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_miim_regs *regs = ledc->info->miim_regs;
	uint32_t val, mask;
	u8 _addr;
	int ret;

//	dev_info(ledc->dev, "%s called\n", __func__);
	mutex_lock(&ledc->miim_lock);
	val = rtl_r32(ledc, regs->indrt_acc);
	_addr = (u8)FIELD_GET(GPIO_PHY_ADDR_MASK, val);

	val = FIELD_PREP(GPIO_PHY_ADDR_MASK, addr) |
	      FIELD_PREP(GPIO_RWOP_MASK, OP_RD) |
	      FIELD_PREP(GPIO_CMD_MASK, CMD_EXE);
	mask = GPIO_PHY_ADDR_MASK | GPIO_RWOP_MASK | GPIO_CMD_MASK;

	if (reg != MIIM_REG_INVAL) {
		val |= FIELD_PREP(GPIO_REG_MASK, reg);
		mask |= GPIO_REG_MASK;
	} else if (_addr != addr) {
		val |= FIELD_PREP(GPIO_REG_MASK, 0);
		mask |= GPIO_REG_MASK;
	}

	dev_dbg(ledc->dev,
		"addr-> 0x%02x, reg-> 0x%02x, val-> 0x%08x, mask-> 0x%08x\n",
		addr, reg, val, mask);
	rtl_rmw32(ledc, regs->indrt_acc, mask, val);

	ret = miim_wait(ledc, &val);
	if (ret) {
		dev_dbg(ledc->dev, "miim_wait error (read): %d\n", ret);
		goto exit;
	}

	*data = (u16)FIELD_GET(GPIO_DATA_MASK, val);

exit:
	mutex_unlock(&ledc->miim_lock);

	return ret;
}

static int miim_write(struct realtek_led_ctrl_intf *intf,
		      u8 addr, u8 reg, u16 data)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_miim_regs *regs = ledc->info->miim_regs;
	uint32_t val, mask;
	u8 _addr;
	int ret;

//	dev_info(ledc->dev, "%s called\n", __func__);

	mutex_lock(&ledc->miim_lock);
	val = rtl_r32(ledc, regs->indrt_acc);
	_addr = (u8)FIELD_GET(GPIO_PHY_ADDR_MASK, val);

	val = FIELD_PREP(GPIO_DATA_MASK, data) |
	      FIELD_PREP(GPIO_PHY_ADDR_MASK, addr) |
	      FIELD_PREP(GPIO_RWOP_MASK, OP_WR) |
	      FIELD_PREP(GPIO_CMD_MASK, CMD_EXE);
	mask = GPIO_DATA_MASK | GPIO_PHY_ADDR_MASK |
	       GPIO_RWOP_MASK | GPIO_CMD_MASK;

	if (reg != MIIM_REG_INVAL) {
		val |= FIELD_PREP(GPIO_REG_MASK, reg);
		mask |= GPIO_REG_MASK;
	} else if (_addr != addr) {
		val |= FIELD_PREP(GPIO_REG_MASK, 0);
		mask |= GPIO_REG_MASK;
	}

	dev_dbg(ledc->dev,
		"addr-> 0x%02x, reg-> 0x%02x, val-> 0x%08x, mask-> 0x%08x\n",
		addr, reg, val, mask);
	rtl_rmw32(ledc, regs->indrt_acc, mask, val);

	ret = miim_wait(ledc, NULL);
	if (ret) {
		dev_dbg(ledc->dev, "miim_wait error (write): %d\n", ret);
	}

	mutex_unlock(&ledc->miim_lock);

	return ret;
}

static void miim_clk_set(struct realtek_led_ctrl_intf *intf, int mode)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_miim_regs *regs = ledc->info->miim_regs;

	/*
	 * initial setup required? datasheet of RTL8231 says
	 * "MDC operating speed is 0-25MHz"
	 * and the clock is configured to 5.2MHz(0x1) on I-O DATA
	 * BSH-G24MB by default, 20.8MHz(0x3) is also usable and
	 * faster than 5.2MHz
	 *
	 * - i2cdump -y 0 0x8 (5.2MHz):
	 *     real	0m 0.35s
	 *     user	0m 0.01s
	 *     sys	0m 0.01s
	 *
	 * - i2cdump -y 0 0x8 (20.8MHz):
	 *     real	0m 0.16s
	 *     user	0m 0.01s
	 *     sys	0m 0.02s
	 */
	if (regs->mdc_period.mask)
		rtl_rmw32(ledc, regs->mdc_period.reg,
			  regs->mdc_period.mask, mode);
}

static void miim_enable(struct realtek_led_ctrl_intf *intf)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_miim_regs *regs = ledc->info->miim_regs;

	/* enable interface */
	if (regs->extgpio_en.mask &&
	    regs->extgpio_en.reg == RTL83XX_LED_GLB_CTRL)
		spin_lock(&ledc->glb_lock);

	rtl_rmw32(ledc, regs->extgpio_en.reg, regs->extgpio_en.mask,
		  regs->extgpio_en.mask);

	if (regs->extgpio_en.mask &&
	    regs->extgpio_en.reg == RTL83XX_LED_GLB_CTRL)
		spin_unlock(&ledc->glb_lock);
}

/* MIIM Registers */
static const struct realtek_miim_regs rtl838x_miim_regs = {
	.indrt_acc	= RTL838X_EXT_GPIO_INDRT_ACCESS,
	.extgpio_en	= {
		.reg	= RTL838X_EXTRA_GPIO_CTRL,
		.mask	= RTL838X_EXTRA_GPIO_EN_MASK,
	},
	.mdc_period	= {
		.reg	= RTL838X_EXTRA_GPIO_CTRL,
		.mask	= RTL838X_MDC_PERIOD_MASK,
	},
};

static const struct realtek_miim_regs rtl839x_miim_regs = {
	.indrt_acc	= RTL839X_EXT_GPIO_INDRT_ACCESS,
	.extgpio_en	= {
		.reg	= RTL83XX_LED_GLB_CTRL,
		.mask	= RTL839X_EXT_GPIO_EN_MASK,
	},
};

/* MIIM Operations */
static struct realtek_miim_ops rtl838x_miim_ops = {
	.miim_enable	= miim_enable,
	.miim_clk_set	= miim_clk_set,
	.miim_read	= miim_read,
	.miim_write	= miim_write,
};

static struct realtek_miim_ops rtl839x_miim_ops = {
	.miim_enable	= miim_enable,
	.miim_read	= miim_read,
	.miim_write	= miim_write,
};

/*
 * LED setup on probing
 */
int realtek_miim_init(struct realtek_led_ctrl *ledc)
{
	int ret = 0;

	switch (ledc->info->family) {
	case RTL8380_FAMILY_ID:
		ledc->info->miim_regs = &rtl838x_miim_regs;
		ledc->intf.miim_ops = &rtl838x_miim_ops;
		break;
	case RTL8390_FAMILY_ID:
		ledc->info->miim_regs = &rtl839x_miim_regs;
		ledc->intf.miim_ops = &rtl839x_miim_ops;
		break;
	case RTL9300_FAMILY_ID:
	case RTL9310_FAMILY_ID:
	default:
		ret = -EINVAL;
	}

	return ret;
}
