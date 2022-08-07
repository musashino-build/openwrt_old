// SPDX-License-Identifier: GPL-2.0-only
/*
 * prom.c
 * Early intialization code for the Realtek RTL838X SoC
 *
 * based on the original BSP by
 * Copyright (C) 2006-2012 Tony Wu (tonywu@realtek.com)
 * Copyright (C) 2020 B. Koblitz
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/cpu.h>
#include <asm/fw/fw.h>
#include <asm/smp-ops.h>
#include <asm/mips-cps.h>

#include <mach-rtl83xx.h>

extern char arcs_cmdline[];

const void *fdt;

#ifdef CONFIG_MIPS_MT_SMP
extern const struct plat_smp_ops vsmp_smp_ops;
static struct plat_smp_ops rtl_smp_ops;

static void rtl_init_secondary(void)
{
#ifndef CONFIG_CEVT_R4K
/*
 * These devices are low on resources. There might be the chance that CEVT_R4K
 * is not enabled in kernel build. Nevertheless the timer and interrupt 7 might
 * be active by default after startup of secondary VPE. With no registered
 * handler that leads to continuous unhandeled interrupts. In this case disable
 * counting (DC) in the core and confirm a pending interrupt.
 */
	write_c0_cause(read_c0_cause() | CAUSEF_DC);
	write_c0_compare(0);
#endif /* CONFIG_CEVT_R4K */
/*
 * Enable all CPU interrupts, as everything is managed by the external
 * controller. TODO: Standard vsmp_init_secondary() has special treatment for
 * Malta if external GIC is available. Maybe we need this too.
 */
	if (mips_gic_present())
		pr_warn("%s: GIC present. Maybe interrupt enabling required.\n", __func__);
	else
		set_c0_status(ST0_IM);
}
#endif /* CONFIG_MIPS_MT_SMP */

void __init prom_free_prom_memory(void)
{

}

void __init device_tree_init(void)
{
	if (!fdt_check_header(&__appended_dtb)) {
		fdt = &__appended_dtb;
		pr_info("Using appended Device Tree.\n");
	}
	initial_boot_params = (void *)fdt;
	unflatten_and_copy_device_tree();
}

void __init prom_init(void)
{
	/* uart0 */
	setup_8250_early_printk_port(0xb8002000, 2, 0);

	fw_init_cmdline();

	mips_cpc_probe();

	if (!register_cps_smp_ops())
		return;

#ifdef CONFIG_MIPS_MT_SMP
	if (cpu_has_mipsmt) {
		rtl_smp_ops = vsmp_smp_ops;
		rtl_smp_ops.init_secondary = rtl_init_secondary;
		register_smp_ops(&rtl_smp_ops);
		return;
	}
#endif

	register_up_smp_ops();
}
