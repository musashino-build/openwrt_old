// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __REALTEK_LED_CTRL_COMMON_H
#define __REALTEK_LED_CTRL_COMMON_H

#define MIIM_REG_INVAL		0xff

/*
 * LED_MODE_CTRL/LED_SET_*_*_CTRL patterns
 * on RTL838x/RTL839x family
 */
enum {
	LED_MODE_LINK_ACT = 0,
	LED_MODE_LINK,
	LED_MODE_ACT,
	LED_MODE_ACT_RX,
	LED_MODE_ACT_TX,
	LED_MODE_COL_DUP_FULL,
	LED_MODE_DUP_FULL,
	LED_MODE_LINK_1G,
	LED_MODE_LINK_100M,
	LED_MODE_LINK_10M,
	LED_MODE_LINK_ACT_1G,
	LED_MODE_LINK_ACT_100M,
	LED_MODE_LINK_ACT_10M,
	LED_MODE_LINK_ACT_1G_100M,
	LED_MODE_LINK_ACT_1G_10M,
	LED_MODE_LINK_ACT_100M_10M,
	LED_MODE_LINK_ACT_10G = 21,
	LED_MODE_DISABLE = 31,
};

/* LED sets */
enum {
	LED_PSET0 = 0,
	LED_PSET1,
	LED_PSET2,
	LED_PSET3,
	LED_PSET_MAX = LED_PSET3,
};

/* LED_SW_P_CTRL patterns */
enum {
	LED_SW_P_OFF = 0,
	LED_SW_P_32MS,
	LED_SW_P_64MS,
	LED_SW_P_128MS,
	LED_SW_P_256MS,
	LED_SW_P_512MS,
	LED_SW_P_1024MS,
	LED_SW_P_ON,
};

enum {
	MEDIA_TYPE_FIBRE = 0,
	MEDIA_TYPE_COPPER,
};

struct realtek_led_ctrl_intf;

struct realtek_pled_ops {
	void (*port_led_activate)(struct realtek_led_ctrl_intf *intf);
	int (*port_led_pset_set)(struct realtek_led_ctrl_intf *intf, int pset, uint64_t set_ports, int type);
	int (*port_led_pset_get)(struct realtek_led_ctrl_intf *intf, int pset, uint64_t *set_ports, int type);
	int (*port_led_asic_set)(struct realtek_led_ctrl_intf *intf, int pset, int index, int mode);
	int (*port_led_asic_get)(struct realtek_led_ctrl_intf *intf, int pset, int index);
	void (*port_led_user_init)(struct realtek_led_ctrl_intf *intf);
	int (*port_led_user_register)(struct realtek_led_ctrl_intf *intf, int port, int index, bool do_reg);
	int (*port_led_user_set)(struct realtek_led_ctrl_intf *intf, int port, int index, int mode);
};

struct realtek_miim_ops {
	void (*miim_enable)(struct realtek_led_ctrl_intf *intf);
	void (*miim_clk_set)(struct realtek_led_ctrl_intf *intf, int mode);
	int (*miim_read)(struct realtek_led_ctrl_intf *intf, u8 addr, u8 reg, u16 *data);
	int (*miim_write)(struct realtek_led_ctrl_intf *intf, u8 addr, u8 reg, u16 data);
};

struct realtek_led_ctrl_intf {
	int port_max;
	int pset_max;
	uint64_t port_mask;
	int pled_num;
	struct realtek_pled_ops *pled_ops;
	struct realtek_miim_ops *miim_ops;
};

#endif /* __REALTEK_LED_CTRL_COMMON_H */
