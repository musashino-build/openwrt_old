// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __REALTEK_LED_CTRL_CORE_H
#define __REALTEK_LED_CTRL_CORE_H

#include <linux/bitops.h>
#include <linux/leds.h>

/*
 * Global LED Control
 */
#define RTL83XX_LED_GLB_CTRL			0x0
/* RTL838x family */
#  define RTL838X_ASIC_CFG_8231_OFF		  30
#  define RTL838X_SYS_LED_MODE_OFF		  16
#  define RTL838X_SYS_LED_MODE_MASK		  (0x3 << RTL838X_SYS_LED_MODE_OFF)
#  define RTL838X_SYS_LED_EN_OFF		  15
#  define RTL838X_SYS_LED_EN_MASK		  (0x1 << RTL838X_SYS_LED_EN_OFF)
#  define RTL838X_LED_HIGH_MASK_OFF		  3
#  define RTL838X_LED_LOW_MASK_OFF		  0
#  define RTL838X_LED_MASK_MASK			  0x7
/* RTL839x family */
#  define RTL839X_EXT_GPIO_EN_OFF		  18
#  define RTL839X_EXT_GPIO_EN_MASK		  0x7
#  define RTL839X_SYS_LED_MODE_OFF		  15
#  define RTL839X_SYS_LED_MODE_MASK		  (0x3 << RTL839X_SYS_LED_MODE_OFF)
#  define RTL839X_SYS_LED_EN_OFF		  14
#  define RTL839X_SYS_LED_EN_MASK		  (0x1 << RTL839X_SYS_LED_EN_OFF)
#  define RTL839X_PLED_EN_OFF			  5
#  define RTL839X_PLED_EN_MASK			  (0x1 << RTL839X_PLED_EN_OFF)
#  define RTL839X_NUM_SEL_OFF			  2
#  define RTL839X_NUM_SEL_MASK			  (0x3 << RTL839X_NUM_SEL_OFF)
#  define RTL839X_IF_SEL_OFF			  0
#  define RTL839X_IF_SEL_MASK			  (0x3 << RTL839X_IF_SEL_OFF)

#define RTL838X_LED_MASK_0			0
#define RTL838X_LED_MASK_1			GENMASK(1,0)
#define RTL838X_LED_MASK_2			GENMASK(2,0)
#define RTL838X_LED_MASK_3			GENMASK(3,0)

/* SoC family IDs */
#define RTL8380_FAMILY_ID			0x8380
#define RTL8390_FAMILY_ID			0x8390
#define RTL9300_FAMILY_ID			0x9300
#define RTL9310_FAMILY_ID			0x9310

#define RTL838X_PORT_MAX			28
#define RTL839X_PORT_MAX			52

#define RTL83XX_PLED_NUM_MAX			3

struct realtek_register {
	uint32_t reg;
	uint32_t mask;
	uint32_t opt;
};

struct realtek_led_ctrl_info {
	uint32_t family;
	int port_max;
	int pset_max;
	int pled_max;
	const void *pled_regs;
	const void *miim_regs;
};

struct realtek_led_ctrl {
	void __iomem *base;
	struct device *dev;
	spinlock_t glb_lock;
	spinlock_t pled_lock;
	struct mutex miim_lock;
	struct realtek_led_ctrl_intf intf;
	struct realtek_led_ctrl_info *info;
	struct led_classdev sled_cdev;
};

unsigned int rtl_r32(struct realtek_led_ctrl *ledc, unsigned int reg);
void rtl_w32(struct realtek_led_ctrl *ledc, unsigned int reg, unsigned int val);
void rtl_rmw32(struct realtek_led_ctrl *ledc, unsigned int reg, unsigned int mask, unsigned int val);
struct realtek_led_ctrl *intf_to_ledc(struct realtek_led_ctrl_intf *_intf);

#endif /* __REALTEK_LED_CTRL_CORE_H */
