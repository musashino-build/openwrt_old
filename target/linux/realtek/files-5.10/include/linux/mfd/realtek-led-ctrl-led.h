// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __REALTEK_LED_CTRL_LED_H
#define __REALTEK_LED_CTRL_LED_H

#include <linux/bitops.h>

/* common definitions */
#define REG_BITS				(BITS_PER_TYPE(uint32_t))
#define REG_BYTES				sizeof(uint32_t)

#define FLDLEN2_PER_PORT			2
#define FLDLEN3_PER_PORT			3

#define PORT10_PER_REG				10
#define PORT16_PER_REG				16
#define PORT32_PER_REG				32

#define FLDLENX_PORTS_PER_REG(flen)		(REG_BITS / flen)

#define PORTS_TO_REGS(p,ppr)			((p / ppr) + ((p % ppr) ? 1 : 0))
#define FLDLENX_PORTS_TO_REGS(p,flen)		PORTS_TO_REGS(p, (REG_BITS / flen))

#define PREG_OFF(p,u)				((p / u) * REG_BYTES)
#define FLDLENX_PREG_OFF(p,flen)		PREG_OFF(p, (REG_BITS / flen))

#define PORTX_FLENX_PORT_OFF(port,ppr,flen)	(0 + (port % ppr) * flen)
#define PORT16_FLEN2_PORT_OFF(port)		\
		PORTX_FLENX_PORT_OFF(port, PORT16_PER_REG, FLDLEN2_PER_PORT)
#define PORT16_FLEN2_PORT_MASK(port)		(0x3 << PORT16_FLEN2_PORT_OFF(port))
#define PORT10_FLEN3_PORT_OFF(port)		\
		PORTX_FLENX_PORT_OFF(port, PORT10_PER_REG, FLDLEN3_PER_PORT)
#define PORT10_FLEN3_PORT_MASK(port)		(0x7 << PORT10_FLEN3_PORT_OFF(port))
#define FLDLENX_PORT_OFF(port,flen)		PORTX_FLENX_PORT_OFF(port, (REG_BITS / flen), flen)

#define LED_SW_P_MODE_FLDLEN			3
#define LED_SW_P_MODE_FLDMASK			GENMASK(2, 0)

/* RTL839x/RTL93xx common definitions */
// none

/* RTL838x family - LED Registers */
#define RTL838X_LED_MODE_CTRL			0x4
#  define RTL838X_HI_LEDX_MODE_SEL_OFF(index)	  (15 + index * 5)
#  define RTL838X_HI_LEDX_MODE_SEL_MASK(index)  (0x1f << RTL838X_HI_LEDX_MODE_SEL_OFF(index))
#  define RTL838X_LO_LEDX_MODE_SEL_OFF(index)	  (0 + index * 5)
#  define RTL838X_LO_LEDX_MODE_SEL_MASK(index)	  (0x1f << RTL838X_LO_LEDX_MODE_SEL_OFF(index))
#  define RTL838X_HL_LEDX_MODE_SEL_MASK(index)	  \
		(RTL838X_HI_LEDX_MODE_SEL_MASK(index) | RTL838X_LO_LEDX_MODE_SEL_MASK(index))
#define RTL838X_LED_P_EN_CTRL			0x8
#  define RTL838X_LED_P_EN_27_0_OFF(port)	  (0 + port)
#define RTL838X_LED_SW_CTRL			0xc
#define RTL838X_LEDX_SW_P_EN_CTRL(index)	(0x10 + index * 4)
#  define RTL838X_SW_CTRL_LED_EN_27_0_OFF(port)	  (0 + port)
#  define RTL838X_SW_CTRL_LED_EN_27_0_MASK(port)  (0x1 << RTL838X_SW_CTRL_LED_EN_27_0_OFF(port))
#define RTL838X_LED_SW_P_CTRL(port)		(0x1c + port * 4)
#  define RTL838X_SW_P_LEDX_MODE_OFF(index)	  (0 + index * 3)
#  define RTL838X_SW_P_LEDX_MODE_MASK(index)	  (0x7 << RTL838X_SW_P_LEDX_MODE_OFF(index))

/* RTL839x family - LED Registers */
#define RTL839X_LED_SET_2_3_CTRL		0x4
#define RTL839X_LED_SET_0_1_CTRL		0x8
#  define RTL839X_SET13_LEDX_SEL_OFF(index)	  (15 + index * 5)
#  define RTL839X_SET13_LEDX_SEL_MASK(index)	  (0x1f << RTL839X_SET13_LEDX_SEL_OFF(index))
#  define RTL839X_SET02_LEDX_SEL_OFF(index)	  (0 + index * 5)
#  define RTL839X_SET02_LEDX_SEL_MASK(index)	  (0x1f << RTL839X_SET02_LEDX_SEL_OFF(index))

#define RTL839X_LED_SET_X_CTRL(set)		(0x8 - (set / 2) * 4)
#  define RTL839X_SETX_LEDX_SEL_OFF(set,index)	  (15 * (set % 2) + index * 5)
#  define RTL839X_SETX_LEDX_SEL_MASK(set,index)	  (0x1f << RTL839X_SETX_LEDX_SEL_OFF(set,index))

#define RTL839X_LED_COPR_SET_SEL_CTRL(port)	(0xc  + PREG_OFF(port, PORT16_PER_REG))
#define RTL839X_LED_FIB_SET_SEL_CTRL(port)	(0x1c + PREG_OFF(port, PORT16_PER_REG))
#  define RTL839X_LED_SET_PSEL_OFF(port)	  (0 + (port % PORT16_PER_REG) * FLDLEN2_PER_PORT)
#  define RTL839X_LED_SET_PSEL_MASK(port)	  (0x3 << RTL839X_LED_SET_PSEL_OFF(port))
#define RTL839X_LED_COPR_PMASK_CTRL(port)	(0x2c + PREG_OFF(port, PORT32_PER_REG))
#define RTL839X_LED_FIB_PMASK_CTRL(port)	(0x34 + PREG_OFF(port, PORT32_PER_REG))
#  define RTL839X_LED_PMASK_OFF(port)		  (0 + (port % PORT32_PER_REG))
#  define RTL839X_LED_PMASK_MASK(port)		  (0x1 << RTL839X_LED_PMASK_OFF(port))
#define RTL839X_LED_COMBO_CTRL(port)		(0x3c + PREG_OFF(port, PORT32_PER_REG))
#  define RTL839X_LED_COMBO_OFF(port)		  (0 + (port % PORT32_PER_REG))
#  define RTL839X_LED_COMBO_MASK(port)		  (0x1 << RTL839X_LED_COMBO_OFF(port))
#define RTL839X_LED_SW_CTRL			0x44
#  define RTL839X_SW_LED_LOAD_MASK		0x1
#define RTL839X_LED_SW_P_EN_CTRL(port)		(0x48 + PREG_OFF(port, PORT10_PER_REG))
#  define RTL839X_LED_EN_OFF(port)		  (0 + (port % PORT10_PER_REG) * FLDLEN3_PER_PORT)
#  define RTL839X_LED_EN_MASK(port,index)	  ((0x1 << index) << RTL839X_LED_EN_OFF(port))
#define RTL839X_LED_SW_P_CTRL(port)		(0x60 + port * 4)
#  define RTL839X_FIB_LEDX_MODE_OFF(index)	  (9 + index * 3)
#  define RTL839X_FIB_LEDX_MODE_MASK(index)	  (0x7 << RTL839X_FIB_LEDX_MODE_OFF(index))
#  define RTL839X_COPR_LEDX_MODE_OFF(index)	  (0 + index * 3)
#  define RTL839X_COPR_LEDX_MODE_MASK(index)	  (0x7 << RTL839X_COPR_LEDX_MODE_OFF(index))

/* Port LED count */
enum {
	PORT_LED_NONE = 0,
	PORT_LED_1,
	PORT_LED_2,
	PORT_LED_3,
	RTL83XX_PORT_LED_MAX = PORT_LED_3,
};

/* RTL838x family specific software-control modes */
enum {
	RTL838X_LED_SW_P_512MS = 4,
	RTL838X_LED_SW_P_ON,
	RTL838X_LED_SW_P_256MS,
	RTL838X_LED_SW_P_1024MS,
};

struct realtek_pled_regs {
	uint32_t sled_en_mask;
	uint32_t sled_on_mask;
	uint32_t led_pset_sel_fbase;
	uint32_t led_pset_sel_cbase;
	uint32_t led_pmask_fbase;
	uint32_t led_pmask_cbase;
	uint32_t sw_p_en_base;
	uint32_t sw_p_base;
	struct realtek_register pled_num_reg;
	struct realtek_register swled_load_reg;
};

#endif /* __REALTEK_LED_CTRL_LED_H */
