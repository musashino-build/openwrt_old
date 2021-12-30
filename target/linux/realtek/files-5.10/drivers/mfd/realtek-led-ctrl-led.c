// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/leds.h>
#include <linux/bitfield.h>

#include <linux/mfd/realtek-led-ctrl-common.h>
#include <linux/mfd/realtek-led-ctrl-core.h>
#include <linux/mfd/realtek-led-ctrl-led.h>

/*
 * SYS LED
 */
static void _sys_led_brightness_set(struct realtek_led_ctrl *ledc,
				    enum led_brightness brnss)
{
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;

	spin_lock(&ledc->glb_lock);
	rtl_rmw32(ledc, RTL83XX_LED_GLB_CTRL,
		  regs->sled_on_mask,
		  brnss > LED_OFF ? regs->sled_on_mask : 0);
	spin_unlock(&ledc->glb_lock);
}

static void sys_led_brightness_set(struct led_classdev *cdev,
				   enum led_brightness brnss)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(dev_get_drvdata(
							cdev->dev->parent));

	_sys_led_brightness_set(ledc, brnss);
}

static int sys_led_setup(struct realtek_led_ctrl *ledc)
{
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	struct device_node *sled_dn;
	struct led_init_data init_data = {};
	int ret = 0;
	bool led_en = false;

	sled_dn = of_get_child_by_name(ledc->dev->of_node, "sys-led");
	/*
	 * if the "sys-led" node is none or disabled,
	 * disable SYS_LED_EN field and exit
	 */
	if (!sled_dn || !of_device_is_available(sled_dn))
		goto exit_setup;

	init_data.fwnode = of_fwnode_handle(sled_dn);

	ledc->sled_cdev.brightness = LED_OFF;
	ledc->sled_cdev.brightness_set = sys_led_brightness_set;

	ret = devm_led_classdev_register_ext(ledc->dev, &ledc->sled_cdev,
					     &init_data);
	if (ret) {
		dev_err(ledc->dev, "failed to register SYS LED\n");

		goto exit_setup;
	};

	/* turn off */
	_sys_led_brightness_set(ledc, LED_OFF);
	led_en = true;
	dev_info(ledc->dev, "SYS LED registered\n");

exit_setup:
	if (regs->sled_en_mask) {
		spin_lock(&ledc->glb_lock);
		rtl_rmw32(ledc, RTL83XX_LED_GLB_CTRL, regs->sled_en_mask,
			  led_en ? regs->sled_en_mask : 0);
		spin_unlock(&ledc->glb_lock);

		dev_dbg(ledc->dev,
			"SYS_LED_EN %sabled\n", led_en ? "en" : "dis");
	}

	return ret;
}

/*
 * Port LED - ASIC
 */

/* set LED mode */
static int rtl83xx_pled_asic_set(struct realtek_led_ctrl_intf *intf,
				 int pset, int index, int mode)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	uint32_t reg, mask, val;

	/* check pset */
	if (pset > ledc->info->pset_max)
		return -ERANGE;

	/* check mode */
	if (mode < LED_MODE_LINK_ACT)
		return -EINVAL;
	else if (LED_MODE_LINK_ACT_100M_10M < mode &&
	    mode != LED_MODE_LINK_ACT_10G &&
	    mode != LED_MODE_DISABLE)
		return -EINVAL;
	/* no 10GbE support on RTL838x family */
	if (ledc->info->family == RTL8380_FAMILY_ID &&
	    mode == LED_MODE_LINK_ACT_10G)
		return -EINVAL;

	switch (ledc->info->family) {
	case RTL8380_FAMILY_ID:
		reg = RTL838X_LED_MODE_CTRL;
		mask = RTL838X_HL_LEDX_MODE_SEL_MASK(index);
		val = mode << RTL838X_HI_LEDX_MODE_SEL_OFF(index) |
		      mode << RTL838X_LO_LEDX_MODE_SEL_OFF(index);
		break;
	case RTL8390_FAMILY_ID:
		reg = RTL839X_LED_SET_X_CTRL(pset);
		mask = RTL839X_SETX_LEDX_SEL_MASK(pset, index);
		val = mode << RTL839X_SETX_LEDX_SEL_OFF(pset, index);
		break;
	default:
		return -ENOTSUPP;
	}

	spin_lock(&ledc->pled_lock);
	rtl_rmw32(ledc, reg, mask, val);
	spin_unlock(&ledc->pled_lock);

	return 0;
}

static int rtl838x_pled_asic_get(struct realtek_led_ctrl_intf *intf,
				 int pset, int index)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	uint32_t val;

	if (pset > ledc->info->pset_max)
		return -ERANGE;

	val = rtl_r32(ledc, RTL838X_LED_MODE_CTRL);
	val = (val & RTL838X_LO_LEDX_MODE_SEL_MASK(index))
		>> RTL838X_LO_LEDX_MODE_SEL_OFF(index);

	return val;
}

static int rtl839x_pled_asic_get(struct realtek_led_ctrl_intf *intf,
				 int pset, int index)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	uint32_t val;

	if (pset > ledc->info->pset_max)
		return -ERANGE;

	val = rtl_r32(ledc, RTL839X_LED_SET_X_CTRL(pset));
	val = (val & RTL839X_SETX_LEDX_SEL_MASK(pset, index))
		>> RTL839X_SETX_LEDX_SEL_OFF(pset, index);

	return val;
}

/* set LED pset */
static int
rtl839x_pled_pset_set(struct realtek_led_ctrl_intf *intf,
			     int pset, uint64_t set_ports, int type)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint32_t *reg_val, *reg_mask;
	uint32_t reg_base;
	size_t i, reg_off;
	int pmax = intf->port_max, regcnt;

	/*
	 *  HIGH(p52-32)  LOW(p31-0)
	 *   0x000F0FF0   0xF0FF0FFF
	 *   0x0000F000   0x0F000000
	 *   ...
	 */
	/* register count for LED_SET_PSEL */
	regcnt = PORTS_TO_REGS(pmax, PORT16_PER_REG);

	reg_val = devm_kzalloc(ledc->dev, sizeof(uint32_t) * regcnt,
			       GFP_KERNEL);
	reg_mask = devm_kzalloc(ledc->dev, sizeof(uint32_t) * regcnt,
				GFP_KERNEL);
	if (!reg_val || !reg_mask)
		return -ENOMEM;

	for (i = 0; i < pmax; i++) {
		if (!(intf->port_mask & BIT_ULL(i)))
			continue;

		if (!(set_ports & BIT_ULL(i)))
			continue;

		reg_off = i / PORT16_PER_REG;
		reg_val[reg_off] |= pset << PORT16_FLEN2_PORT_OFF(i);
		reg_mask[reg_off] |= PORT16_FLEN2_PORT_MASK(i);
/*		dev_info(ledc->dev,
			 "set%d, type->%s, regoff->0x%02x, val->0x%08x, mask->0x%08x\n",
			 pset,
			 type == MEDIA_TYPE_FIBRE ? "fibre" : "copper",
			 reg_off * 4, reg_val[reg_off],
			 reg_mask[reg_off]);
*/	}

	reg_base = (type == MEDIA_TYPE_FIBRE) ?
			regs->led_pset_sel_fbase : regs->led_pset_sel_cbase;

	spin_lock(&ledc->pled_lock);
	for (i = 0; i < regcnt; i++) {
		if (!reg_mask[i])
			continue;
		rtl_rmw32(ledc, reg_base + i * 4, reg_mask[i],
			  reg_val[i]);
	}
	spin_unlock(&ledc->pled_lock);

	return 0;
}

static int rtl839x_pled_pset_get(struct realtek_led_ctrl_intf *intf,
				   int pset, uint64_t *set_ports,
				   int type)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint32_t *reg_val, reg_base;
	size_t i, regcnt;
	int pmax = intf->port_max;

	regcnt = PORTS_TO_REGS(pmax, PORT16_PER_REG);

	reg_base = (type == MEDIA_TYPE_FIBRE) ?
			regs->led_pset_sel_fbase : regs->led_pset_sel_cbase;

	reg_val = devm_kzalloc(ledc->dev, sizeof(uint32_t) * regcnt,
			       GFP_KERNEL);
	if (!reg_val)
		return -ENOMEM;

	spin_lock(&ledc->pled_lock);
	for (i = 0; i < regcnt; i++)
		reg_val[i] = rtl_r32(ledc, reg_base + i * 4);
	spin_unlock(&ledc->pled_lock);

	*set_ports = 0;
	for (i = 0; i < pmax; i++) {
		size_t reg_off;

		if (!(intf->port_mask & BIT_ULL(i)))
			continue;

		reg_off = i / PORT16_PER_REG;
		if ((reg_val[reg_off] & PORT16_FLEN2_PORT_MASK(i))
			== (pset << PORT16_FLEN2_PORT_OFF(i)))
			*set_ports |= BIT_ULL(i);
	}

	return 0;
}

/* activate port LED */
static void rtl839x_pled_activate(struct realtek_led_ctrl_intf *intf)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);

	spin_lock(&ledc->glb_lock);
	rtl_rmw32(ledc, RTL83XX_LED_GLB_CTRL, RTL839X_PLED_EN_MASK,
		  RTL839X_PLED_EN_MASK);
	spin_unlock(&ledc->glb_lock);
}

/*
 * Port LED - User
 */
 
/* set ON/OFF/Blink */
static int rtl838x_pled_user_set(struct realtek_led_ctrl_intf *intf,
				  int port, int index, int mode)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	uint32_t val;
	uint32_t rtl838x_pled_map[] = {
		LED_SW_P_OFF,
		LED_SW_P_32MS,
		LED_SW_P_64MS,
		LED_SW_P_128MS,
		RTL838X_LED_SW_P_256MS,
		RTL838X_LED_SW_P_512MS,
		RTL838X_LED_SW_P_1024MS,
		RTL838X_LED_SW_P_ON,
	};

	val = rtl838x_pled_map[mode]
		<< RTL838X_SW_P_LEDX_MODE_OFF(index);

	spin_lock(&ledc->pled_lock);
	rtl_rmw32(ledc, RTL838X_LED_SW_P_CTRL(port),
		  RTL838X_SW_P_LEDX_MODE_MASK(index), val);
	spin_unlock(&ledc->pled_lock);

	return 0;
}

static void rtl839x_pled_user_load(struct realtek_led_ctrl *ledc)
{
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;

	rtl_rmw32(ledc, regs->swled_load_reg.reg,
		  regs->swled_load_reg.mask,
		  regs->swled_load_reg.mask);
}

static int rtl839x_pled_user_set(struct realtek_led_ctrl_intf *intf,
				 int port, int index, int mode)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint32_t val, mask;
	size_t bit_off;

	/* mode for fibre */
	bit_off = (ledc->info->pled_max + index) * LED_SW_P_MODE_FLDLEN;
	val = mode << bit_off;
	mask = LED_SW_P_MODE_FLDMASK << bit_off;
	/* mode for copper */
	bit_off = index * LED_SW_P_MODE_FLDLEN;
	val |= mode << bit_off;
	mask = LED_SW_P_MODE_FLDMASK << bit_off;

	spin_lock(&ledc->pled_lock);
	/* set user LED mode */
	rtl_rmw32(ledc, regs->sw_p_base + port * REG_BYTES, mask, val);

	/* load configuration for software-controlled LEDs */
	rtl839x_pled_user_load(ledc);
	spin_unlock(&ledc->pled_lock);

	return 0;
}

/* register/unregister as user LED */
static int rtl838x_pled_user_register(struct realtek_led_ctrl_intf *intf,
				      int port, int index, bool do_reg)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	uint32_t val;

	val = do_reg ? RTL838X_SW_CTRL_LED_EN_27_0_MASK(port) : 0;

	spin_lock(&ledc->pled_lock);
	rtl_rmw32(ledc, RTL838X_LEDX_SW_P_EN_CTRL(index),
		  RTL838X_SW_CTRL_LED_EN_27_0_MASK(port), val);
	spin_unlock(&ledc->pled_lock);
	
	return 0;
}

static int rtl839x_pled_user_register(struct realtek_led_ctrl_intf *intf,
				      int port, int index, bool do_reg)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint32_t mask;
	size_t reg_off;

	reg_off = FLDLENX_PREG_OFF(port, ledc->info->pled_max);
	mask = BIT(index)
		<< FLDLENX_PORT_OFF(port, ledc->info->pled_max);

	spin_lock(&ledc->pled_lock);
	rtl_rmw32(ledc, regs->sw_p_en_base + reg_off, mask,
		  do_reg ? mask : 0);
	spin_unlock(&ledc->pled_lock);

	return 0;
}

/* initialize LEDs on all ports */
static void rtl83xx_pled_user_init(struct realtek_led_ctrl_intf *intf)
{
	struct realtek_led_ctrl *ledc = intf_to_ledc(intf);
	struct realtek_led_ctrl_info *info = ledc->info;
	const struct realtek_pled_regs *regs = info->pled_regs;
	size_t i, regcnt;

	/* set mode "disabled" to all LEDs (turn off) */
	for (i = 0; i < info->port_max; i++)
		rtl_w32(ledc, regs->sw_p_base + i * REG_BYTES, 0);

	if (ledc->info->family != RTL8380_FAMILY_ID)
		rtl839x_pled_user_load(ledc);

	/* disable software-control on all LEDs */
	if (info->family == RTL8380_FAMILY_ID) {
		for (i = 0; i < intf->pled_num; i++)
			rtl_w32(ledc, RTL838X_LEDX_SW_P_EN_CTRL(i), 0);
	} else {
		regcnt = FLDLENX_PORTS_TO_REGS(info->port_max,
					       info->pled_max);
		for (i = 0; i < regcnt; i++)
			rtl_w32(ledc, regs->sw_p_en_base + i * REG_BYTES,
				0);
	}
}

/* get LED count per port */
static int rtl838x_pled_num_get(struct realtek_led_ctrl *ledc)
{
	unsigned long val;

	val = rtl_r32(ledc, RTL83XX_LED_GLB_CTRL);
	val &= RTL838X_LED_MASK_MASK << RTL838X_LED_HIGH_MASK_OFF |
	       RTL838X_LED_MASK_MASK << RTL838X_LED_LOW_MASK_OFF;
	/* High ports != Low ports */
	if ((val >> RTL838X_LED_HIGH_MASK_OFF)
		!= (val & RTL838X_LED_MASK_MASK))
		return PORT_LED_NONE;
	val = find_last_bit(&val, 3) + 1;

	if (val > RTL83XX_PORT_LED_MAX)
		return PORT_LED_NONE;
	
	return (uint32_t)val;
}

static int rtl839x_pled_num_get(struct realtek_led_ctrl *ledc)
{
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint32_t val;
	unsigned long mask = regs->pled_num_reg.mask;
	size_t bit_off;

	val = rtl_r32(ledc, regs->pled_num_reg.reg);
	/* field length of NUM_SEL is 2 */
	bit_off = find_first_bit(&mask, REG_BITS - 2);
	val = (val & mask) >> bit_off;

	/*
	 * RTL839x: 0 -> 0 (none), 1 -> 1, ...
	 * RTL93xx: 0 -> 1, 1 -> 2, ...
	 */
	return val + regs->pled_num_reg.opt;
}

/* get mask of ports which have available LED(s) */
static uint64_t rtl838x_port_mask_get(struct realtek_led_ctrl *ledc)
{
	return rtl_r32(ledc, RTL838X_LED_P_EN_CTRL);
}

static uint64_t rtl839x_port_mask_get(struct realtek_led_ctrl *ledc)
{
	const struct realtek_pled_regs *regs = ledc->info->pled_regs;
	uint64_t mask_copr = 0, mask_fib = 0;
	size_t regcnt = PORTS_TO_REGS(ledc->info->port_max, REG_BITS);

	if (regcnt > 1) {
		mask_copr = rtl_r32(ledc,
				    regs->led_pmask_cbase + REG_BYTES);
		mask_copr <<= 32;
	}
	mask_copr |= rtl_r32(ledc, regs->led_pmask_cbase);

	if (regcnt > 1) {
		mask_fib = rtl_r32(ledc,
				   regs->led_pmask_fbase + REG_BYTES);
		mask_fib <<= 32;
	}
	mask_fib |= rtl_r32(ledc, regs->led_pmask_fbase);

	return mask_copr | mask_fib;
}

/* Port LED Registers */
static const struct realtek_pled_regs rtl838x_pled_regs = {
	.sled_en_mask	= RTL838X_SYS_LED_EN_MASK,
	.sled_on_mask	= RTL838X_SYS_LED_MODE_MASK,
	.sw_p_base	= RTL838X_LED_SW_P_CTRL(0),
};

static const struct realtek_pled_regs rtl839x_pled_regs = {
	.sled_en_mask		= RTL839X_SYS_LED_EN_MASK,
	.sled_on_mask		= RTL839X_SYS_LED_MODE_MASK,
	.led_pset_sel_fbase	= RTL839X_LED_FIB_SET_SEL_CTRL(0),
	.led_pset_sel_cbase	= RTL839X_LED_COPR_SET_SEL_CTRL(0),
	.led_pmask_fbase	= RTL839X_LED_FIB_PMASK_CTRL(0),
	.led_pmask_cbase	= RTL839X_LED_COPR_PMASK_CTRL(0),
	.sw_p_en_base		= RTL839X_LED_SW_P_EN_CTRL(0),
	.sw_p_base		= RTL839X_LED_SW_P_CTRL(0),
	.pled_num_reg		= {
		.reg		= RTL83XX_LED_GLB_CTRL,
		.mask		= RTL839X_NUM_SEL_MASK,
		.opt		= 0,
	},
	.swled_load_reg		= {
		.reg		= RTL839X_LED_SW_CTRL,
		.mask		= RTL839X_SW_LED_LOAD_MASK,
	},
};

/* Port LED Operations */
static struct realtek_pled_ops rtl838x_pled_ops = {
	.port_led_asic_set	= rtl83xx_pled_asic_set,
	.port_led_asic_get	= rtl838x_pled_asic_get,
	.port_led_user_init	= rtl83xx_pled_user_init,
	.port_led_user_register	= rtl838x_pled_user_register,
	.port_led_user_set	= rtl838x_pled_user_set,
};

static struct realtek_pled_ops rtl839x_pled_ops = {
	.port_led_activate	= rtl839x_pled_activate,
	.port_led_pset_set	= rtl839x_pled_pset_set,
	.port_led_pset_get	= rtl839x_pled_pset_get,
	.port_led_asic_set	= rtl83xx_pled_asic_set,
	.port_led_asic_get	= rtl839x_pled_asic_get,
	.port_led_user_init	= rtl83xx_pled_user_init,
	.port_led_user_register	= rtl839x_pled_user_register,
	.port_led_user_set	= rtl839x_pled_user_set,
};

/*
 * LED setup on probing
 */
int realtek_leds_init(struct realtek_led_ctrl *ledc)
{
	int ret = 0;

	switch (ledc->info->family) {
	case RTL8380_FAMILY_ID:
		ledc->info->pled_regs = &rtl838x_pled_regs;
		ledc->intf.port_mask = rtl838x_port_mask_get(ledc);
		ledc->intf.pled_num = rtl838x_pled_num_get(ledc);
		ledc->intf.pled_ops = &rtl838x_pled_ops;
		break;
	case RTL8390_FAMILY_ID:
		ledc->info->pled_regs = &rtl839x_pled_regs;
		ledc->intf.port_mask = rtl839x_port_mask_get(ledc);
		ledc->intf.pled_num = rtl839x_pled_num_get(ledc);
		ledc->intf.pled_ops = &rtl839x_pled_ops;
		break;
	case RTL9300_FAMILY_ID:
	case RTL9310_FAMILY_ID:
	default:
		ret = -EINVAL;
	}

	ledc->intf.port_max = ledc->info->port_max;
	ledc->intf.pset_max = ledc->info->pset_max;

	/* setup Sys LED */
	ret = sys_led_setup(ledc);
	if (ret)
		return ret;

	return ret;
}
