// SPDX-License-Identifier: GPL-2.0-only
/*
 * Setup for the Realtek RTL838X SoC:
 *	Memory, Timer and Serial
 *
 * Copyright (C) 2020 B. Koblitz
 * based on the original BSP by
 * Copyright (C) 2006-2012 Tony Wu (tonywu@realtek.com)
 *
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/irqchip.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>

#include "mach-rtl83xx.h"

#define REALTEK_SYS_TYPE_LEN	32

struct rtl83xx_soc_info soc_info;
static char realtek_sys_type[REALTEK_SYS_TYPE_LEN];

const char *get_system_type(void)
{
	return realtek_sys_type;
}

static struct of_device_id realtek_soc_matches[] = {
	/* RTL838x family */
	{ .compatible = "realtek,rtl838x-soc", .data = (void *)RTL8380_FAMILY_ID },
	{ .compatible = "realtek,rtl8380-soc", .data = (void *)RTL8380_FAMILY_ID },
	{ .compatible = "realtek,rtl8381-soc", .data = (void *)RTL8380_FAMILY_ID },
	{ .compatible = "realtek,rtl8382-soc", .data = (void *)RTL8380_FAMILY_ID },
	/* RTL839x family */
	{ .compatible = "realtek,rtl839x-soc", .data = (void *)RTL8390_FAMILY_ID },
	{ .compatible = "realtek,rtl8391-soc", .data = (void *)RTL8390_FAMILY_ID },
	{ .compatible = "realtek,rtl8392-soc", .data = (void *)RTL8390_FAMILY_ID },
	{ .compatible = "realtek,rtl8393-soc", .data = (void *)RTL8390_FAMILY_ID },
	/* RTL930x family */
	{ .compatible = "realtek,rtl930x-soc", .data = (void *)RTL9300_FAMILY_ID },
	{ .compatible = "realtek,rtl9301-soc", .data = (void *)RTL9300_FAMILY_ID },
	{ .compatible = "realtek,rtl9302-soc", .data = (void *)RTL9300_FAMILY_ID },
	{ .compatible = "realtek,rtl9303-soc", .data = (void *)RTL9300_FAMILY_ID },
	/* RTL931x family */
	{ .compatible = "realtek,rtl931x-soc", .data = (void *)RTL9310_FAMILY_ID },
	{ .compatible = "realtek,rtl9313-soc", .data = (void *)RTL9310_FAMILY_ID },
	{ },
};

void __init identify_realtek_soc(void *dtb)
{
	const struct of_device_id *match, *_matched;
	uint32_t ninfo_addr = 0, cinfo_addr = 0, family, model, chip;
	char rev, rev_offset = 0;

	for (match = realtek_soc_matches; match->compatible[0]; match++)
		if (fdt_node_check_compatible(dtb, 0, match->compatible) == 0)
			_matched = match;

	if (!_matched)
		panic("unsupported platform detected");

	family = (uint32_t)_matched->data;
	switch (family) {
	case RTL8380_FAMILY_ID:
		ninfo_addr = RTL838X_MODEL_NAME_INFO;
		cinfo_addr = RTL838X_CHIP_INFO_ADDR;
		rev_offset = 1;
		break;
	case RTL8390_FAMILY_ID:
		ninfo_addr = RTL839X_MODEL_NAME_INFO;
		cinfo_addr = RTL839X_CHIP_INFO_ADDR;
		break;
	case RTL9300_FAMILY_ID:
	case RTL9310_FAMILY_ID:
		ninfo_addr = RTL93XX_MODEL_NAME_INFO;
		break;
	}

	model = sw_r32(ninfo_addr);
	pr_info("RTL83xx/RTL93xx model is %08x\n", model);

	if ((model >> 16 & 0xfff0) != family)
		panic("unsupported family detected (expected: %08x, read: %08x)",
		      family, model >> 16 & 0xfff0);

	if (family == RTL9300_FAMILY_ID || family == RTL9310_FAMILY_ID) {
		rev = model & 0xf;
	} else {
		if (family == RTL8380_FAMILY_ID)
			sw_w32_mask(0, 0x1, RTL838X_INT_RW_CTRL);
		sw_w32_mask(0xf0000000, 0xa0000000, cinfo_addr);
		chip = sw_r32(cinfo_addr);
		sw_w32_mask(0xf0000000, 0, cinfo_addr);
		if (family == RTL8380_FAMILY_ID)
			sw_w32_mask(0x3, 0, RTL838X_INT_RW_CTRL);
		rev = chip >> 16 & 0x1f;
	}

	/*
	 * - name suffix: 1->A, 2->B, ...
	 * - revision:
	 *   - RTL838x        : 1->A, 2->B, ...
	 *   - RTL839x/RTL93xx: 0->A, 1->B, ...
	 */
	snprintf(realtek_sys_type, REALTEK_SYS_TYPE_LEN,
		 "Realtek RTL%04x%c Rev.%c (RTL%03xx)",
		 model >> 16,
		 0x40 + (model >> 11 & 0x1f),
		 0x41 + rev - rev_offset,
		 family >> 4);
	pr_info("SoC: %s\n", realtek_sys_type);

	soc_info.id = model >> 16;
	soc_info.name = realtek_sys_type;
	soc_info.family = family;
}

void __init plat_mem_setup(void)
{
	void *dtb;

	set_io_port_base(KSEG1);

	dtb = get_fdt();
	if (!dtb)
		panic("no dtb found");

	identify_realtek_soc(dtb);

	/*
	 * Load the devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing
	 */
	__dt_setup_arch(dtb);
}

void plat_time_init_fallback(void)
{
	struct device_node *np;
	u32 freq = 500000000;

	np = of_find_node_by_name(NULL, "cpus");
	if (!np) {
		pr_err("Missing 'cpus' DT node, using default frequency.");
	} else {
		if (of_property_read_u32(np, "frequency", &freq) < 0)
			pr_err("No 'frequency' property in DT, using default.");
		else
			pr_info("CPU frequency from device tree: %dMHz", freq / 1000000);
		of_node_put(np);
	}
	mips_hpt_frequency = freq / 2;
}

void __init plat_time_init(void)
{
/*
 * Initialization routine resembles generic MIPS plat_time_init() with
 * lazy error handling. The final fallback is only needed until we have
 * converted all device trees to new clock syntax.
 */
	struct device_node *np;
	struct clk *clk;

	of_clk_init(NULL);

	mips_hpt_frequency = 0;
	np = of_get_cpu_node(0, NULL);
	if (!np) {
		pr_err("Failed to get CPU node\n");
	} else {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
		} else {
			mips_hpt_frequency = clk_get_rate(clk) / 2;
			clk_put(clk);
		}
	}

	if (!mips_hpt_frequency)
		plat_time_init_fallback();

	timer_probe();
}

void __init arch_init_irq(void)
{
	irqchip_init();
}
