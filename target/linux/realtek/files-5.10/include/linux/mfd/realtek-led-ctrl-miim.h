// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __REALTEK_LED_CTRL_MDIO_H
#define __REALTEK_LED_CTRL_MDIO_H

/* External GPIO Indirect Access */
#define RTL838X_EXT_GPIO_INDRT_ACCESS		0x9c
#define RTL839X_EXT_GPIO_INDRT_ACCESS		0x140
#  define GPIO_DATA_OFF				  16
#  define GPIO_DATA_MASK			  GENMASK(31,16)
#  define GPIO_REG_OFF				  7
#  define GPIO_REG_MASK				  GENMASK(11,7)
#  define GPIO_PHY_ADDR_OFF			  2
#  define GPIO_PHY_ADDR_MASK			  GENMASK(6,2)
#  define GPIO_RWOP_OFF				  1
#  define GPIO_RWOP_MASK			  BIT(1)
#  define GPIO_CMD_OFF				  0
#  define GPIO_CMD_MASK				  BIT(0)

/* RTL838x family */
#define RTL838X_EXTRA_GPIO_CTRL			0xe0
#  define RTL838X_MDC_PERIOD_OFF		  8
#  define RTL838X_MDC_PERIOD_MASK		  GENMASK(9,8)
#  define RTL838X_SYNC_GPIO_OFF			  7
#  define RTL838X_SYNC_GPIO_MASK		  BIT(7)
#  define RTL838X_EXTRA_GPIO_EN_OFF		  0
#  define RTL838X_EXTRA_GPIO_EN_MASK		  BIT(0)

/* common definitions */
#define SLAVE_MAX_ADDR				FIELD_MAX(GPIO_PHY_ADDR_MASK)
#define REG_MAX_ADDR				FIELD_MAX(GPIO_REG_MASK)

#define OP_RD					0x0
#define OP_WR					0x1

#define CMD_EXE					0x1

struct realtek_miim_regs {
	uint32_t indrt_acc;
	struct realtek_register extgpio_en;
	struct realtek_register mdc_period;
};

#endif /* __REALTEK_LED_CTRL_MDIO_H */
