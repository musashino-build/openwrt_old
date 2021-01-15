/*
 *  NAND flash driver for the MikroTik RouterBOARD 750
 *
 *  Copyright (C) 2010-2012 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>

#define DRV_NAME	"rb750-nand"
#define DRV_VERSION	"0.2.0"
#define DRV_DESC	"NAND flash driver for the MikroTik RouterBOARD 750"

#define RB750_NAND_DATA_LINES	8
#define RB750_NAND_LINE_MASK	1

struct rb750_nand {
	struct device *dev;

	struct nand_chip chip;
	struct data_gpios *dgpio;
	struct ctrl_gpios *cgpio;
//	struct rb7xx_nand_platform_data *pdata;
//	struct gpio_desc **io0;
	struct gpio_desc *ale;
	struct gpio_desc *cle;
	struct gpio_desc *nce;
	struct gpio_desc *nre;
	struct gpio_desc *nwe;
	struct gpio_desc *rdy;
};

struct data_gpios {
	struct gpio_desc *io0;
	struct gpio_desc *io1;
	struct gpio_desc *io2;
	struct gpio_desc *io3;
	struct gpio_desc *io4;
	struct gpio_desc *io5;
	struct gpio_desc *io6;
	struct gpio_desc *io7;
};

struct ctrl_gpios {
	struct gpio_desc *ale;
	struct gpio_desc *cle;
	struct gpio_desc *nce;
	struct gpio_desc *nre;
	struct gpio_desc *nwe;
	struct gpio_desc *rdy;
};

static int rb750_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	switch (section) {
	case 0:
		oobregion->offset = 8;
		oobregion->length = 3;
		return 0;
	case 1:
		oobregion->offset = 13;
		oobregion->length = 3;
		return 0;
	default:
		return -ERANGE;
	}
}

static int rb750_ooblayout_free(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	switch (section) {
	case 0:
		oobregion->offset = 0;
		oobregion->length = 4;
		return 0;
	case 1:
		oobregion->offset = 4;
		oobregion->length = 1;
		return 0;
	case 2:
		oobregion->offset = 6;
		oobregion->length = 2;
		return 0;
	case 3:
		oobregion->offset = 11;
		oobregion->length = 2;
		return 0;
	default:
		return -ERANGE;
	}
}

static const struct mtd_ooblayout_ops rb750_nand_ecclayout_ops = {
	.ecc = rb750_ooblayout_ecc,
	.free = rb750_ooblayout_free,
};

static void rb750_nand_write(struct nand_chip *chip, const u8 *buf, unsigned len)
{
	struct rb750_nand *nand = chip->priv;
	struct gpio_desc **dgpio_p;
	unsigned i, j;
	u32 mask;

	/* set data lines to output mode */
	dgpio_p = &nand->dgpio->io0;
	for (i = 0; i < RB750_NAND_DATA_LINES; i++) {
		gpiod_direction_output(*dgpio_p, 0);
		dgpio_p++;
	}

	/* send data via data lines */
	for (i = 0; i != len; i++) {
		u32 data;

		data = buf[i];

		/* activate WE line (LOW) */
		gpiod_set_value_cansleep(nand->nwe, 0);

		dgpio_p = &nand->dgpio->io0;
		for (j = 0; j < RB750_NAND_DATA_LINES; j++) {
			mask = RB750_NAND_LINE_MASK << j;

			gpiod_set_value_cansleep(*dgpio_p, !!(data && mask));
			dgpio_p++;
		}

		/* deactivate WE line (HIGH) */
		gpiod_set_value_cansleep(nand->nwe, 1);
	}

	/* set data lines to input mode */
	dgpio_p = &nand->dgpio->io0;
	for (i = 0; i < RB750_NAND_DATA_LINES; i++) {
		gpiod_direction_input(*dgpio_p);
		dgpio_p++;
	}
}

static void rb750_nand_read(struct nand_chip *chip, u8 *read_buf, unsigned len)
{
	struct rb750_nand *nand = chip->priv;
	struct gpio_desc **dgpio_p;
	unsigned i, j;

	for (i = 0; i < len; i++) {
		u8 data = 0;

		/* activate RE line */
		gpiod_set_value_cansleep(nand->nre, 0);

		/* read input lines */
		dgpio_p = &nand->dgpio->io0;
		for (j = 0; j < RB750_NAND_DATA_LINES; j++) {
			data |= gpiod_get_value_cansleep(*dgpio_p)
				<< j;
			dgpio_p++;
		}

		/* deactivate RE line */
		gpiod_set_value_cansleep(nand->nre, 1);

		read_buf[i] = data;
	}
}

static void rb750_nand_select_chip(struct nand_chip *chip, int cs)
{
	struct rb750_nand *nand = chip->priv;
	int i;

	if (cs >= 0) {
		/* set input mode for data lines */
		struct gpio_desc **dgpio_p = &nand->dgpio->io0;
		for (i = 0; i < RB750_NAND_DATA_LINES; i++) {
			gpiod_direction_input(*dgpio_p);
			dgpio_p++;
		}

		/* deactivate RE and WE lines */
		gpiod_set_value_cansleep(nand->nre, 1);
		gpiod_set_value_cansleep(nand->nwe, 1);

		/* activate CE line */
		gpiod_set_value_cansleep(nand->nce, 0);
	} else {
		/* deactivate CE line */
		gpiod_set_value_cansleep(nand->nce, 1);

		/* set output mode for I/O 0 and RDY */
		gpiod_direction_output(nand->dgpio->io0, 0);
		gpiod_direction_output(nand->rdy, 0);
	}
}

static int rb750_nand_dev_ready(struct nand_chip *chip)
{
	struct rb750_nand *nand = chip->priv;

	return gpiod_get_value_cansleep(nand->rdy);
}

static void rb750_nand_cmd_ctrl(struct nand_chip *chip, int cmd,
				unsigned int ctrl)
{
	struct rb750_nand *nand = chip->priv;

	if (ctrl & NAND_CTRL_CHANGE) {
		gpiod_set_value_cansleep(nand->cle, !!(ctrl & NAND_CLE));
		gpiod_set_value_cansleep(nand->ale, !!(ctrl & NAND_ALE));
	}

	if (cmd != NAND_CMD_NONE) {
		u8 t = cmd;
		rb750_nand_write(chip, &t, 1);
	}
}

static u8 rb750_nand_read_byte(struct nand_chip *chip)
{
	u8 data = 0;
	rb750_nand_read(chip, &data, 1);
	return data;
}

static void rb750_nand_read_buf(struct nand_chip *chip, u8 *buf, int len)
{
	rb750_nand_read(chip, buf, len);
}

static void rb750_nand_write_buf(struct nand_chip *chip, const u8 *buf, int len)
{
	rb750_nand_write(chip, buf, len);
}

static int rb750_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rb750_nand *nand;
	struct mtd_info *mtd;
	struct gpio_desc **dgpio_p;
	int ret, i;
	char line_name[32];

	dev_info(dev, DRV_DESC " version " DRV_VERSION "\n");

	nand = devm_kzalloc(dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	platform_set_drvdata(pdev, nand);

	nand->dev = dev;

	dgpio_p = &nand->dgpio->io0;
	/* get gpio pins for data lines */
	for (i = 0; i < RB750_NAND_DATA_LINES; i++) {
		*dgpio_p = devm_gpiod_get_index(dev, "data", i, GPIOD_IN);
		if (IS_ERR(*dgpio_p))
			dev_err(dev, "missing gpio I/O %d: %ld\n", i, PTR_ERR(*dgpio_p));
		dgpio_p++;
	}

	if (IS_ERR(nand->dgpio->io0) || IS_ERR(nand->dgpio->io1) ||
	    IS_ERR(nand->dgpio->io2) || IS_ERR(nand->dgpio->io3) ||
	    IS_ERR(nand->dgpio->io4) || IS_ERR(nand->dgpio->io5) ||
	    IS_ERR(nand->dgpio->io6) || IS_ERR(nand->dgpio->io7))
		return -ENOENT;

	dgpio_p = &nand->dgpio->io0;
	for (i = 0; i < RB750_NAND_DATA_LINES; i++) {
		sprintf(line_name, "mikrotik:nand:io%d", i);
		gpiod_set_consumer_name(*dgpio_p, line_name);
		dgpio_p++;
	}

/*	nand->dgpio->io0 = devm_gpiod_get_index(dev, "data", 0, GPIOD_IN);
	if (IS_ERR(nand->dgpio->io0))
		dev_err(dev, "missing gpio IO0: %ld\n", PTR_ERR(nand->dgpio->io0));
*/
	nand->ale = devm_gpiod_get_index(dev, "ctrl", 1, GPIOD_OUT_LOW);
	if (IS_ERR(nand->ale))
		dev_err(dev, "missing gpio ALE: %ld\n", PTR_ERR(nand->ale));

	nand->cle = devm_gpiod_get_index(dev, "ctrl", 2, GPIOD_OUT_LOW);
	if (IS_ERR(nand->cle))
		dev_err(dev, "missing gpio CLE: %ld\n", PTR_ERR(nand->cle));

	nand->nce = devm_gpiod_get_index(dev, "ctrl", 3, GPIOD_OUT_HIGH);
	if (IS_ERR(nand->nce))
		dev_err(dev, "missing gpio nCE: %ld\n", PTR_ERR(nand->nce));

	nand->nre = devm_gpiod_get_index(dev, "ctrl", 4, GPIOD_OUT_HIGH);
	if (IS_ERR(nand->nre))
		dev_err(dev, "missing gpio NRE: %ld\n", PTR_ERR(nand->nre));

	nand->nwe = devm_gpiod_get_index(dev, "ctrl", 5, GPIOD_OUT_HIGH);
	if (IS_ERR(nand->nwe))
		dev_err(dev, "missing gpio NWE: %ld\n", PTR_ERR(nand->nwe));

	nand->rdy = devm_gpiod_get_index(dev, "ctrl", 6, GPIOD_IN);
	if (IS_ERR(nand->rdy))
		dev_err(dev, "missing gpio RDY: %ld\n", PTR_ERR(nand->rdy));

	if (IS_ERR(nand->ale) || IS_ERR(nand->cle) || IS_ERR(nand->nce) ||
	    IS_ERR(nand->nre) || IS_ERR(nand->nwe) || IS_ERR(nand->rdy))
		return -ENOENT;

	gpiod_set_consumer_name(nand->ale, "mikrotik:nand:ale");
	gpiod_set_consumer_name(nand->cle, "mikrotik:nand:cle");
	gpiod_set_consumer_name(nand->nce, "mikrotik:nand:nce");
	gpiod_set_consumer_name(nand->nre, "mikrotik:nand:nre");
	gpiod_set_consumer_name(nand->nwe, "mikrotik:nand:nwe");
	gpiod_set_consumer_name(nand->rdy, "mikrotik:nand:rdy");

	mtd = nand_to_mtd(&nand->chip);
	mtd->priv	= nand;
	mtd->owner	= THIS_MODULE;
	mtd->dev.parent = dev;
	mtd_set_of_node(mtd, dev->of_node);

	if (mtd->writesize == 512)
		mtd_set_ooblayout(mtd, &rb750_nand_ecclayout_ops);

	nand->chip.ecc.mode	= NAND_ECC_SOFT;
	nand->chip.ecc.algo	= NAND_ECC_HAMMING;
	nand->chip.options	= NAND_NO_SUBPAGE_WRITE;
	nand->chip.priv		= nand;

	/*
	 * legacy fields/hooks
	 * rework may be needed in the future
	 * (ref: nand_legacy in include/linux/mtd/rawnand.sh)
	 */
	nand->chip.legacy.select_chip	= rb750_nand_select_chip;
	nand->chip.legacy.cmd_ctrl	= rb750_nand_cmd_ctrl;
	nand->chip.legacy.dev_ready	= rb750_nand_dev_ready;
	nand->chip.legacy.read_byte	= rb750_nand_read_byte;
	nand->chip.legacy.write_buf	= rb750_nand_write_buf;
	nand->chip.legacy.read_buf	= rb750_nand_read_buf;
	nand->chip.legacy.chip_delay	= 25;

	ret = nand_scan(&nand->chip, 1);
	if (ret)
		return -ENXIO;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_release(&nand->chip);
		return ret;
	}

	return 0;
}

static int rb750_nand_remove(struct platform_device *pdev)
{
	struct rb750_nand *nand = platform_get_drvdata(pdev);

	nand_release(&nand->chip);

	return 0;
}

static const struct platform_device_id rb750_nand_id_table[] = {
	{ "mikrotik,rb750-nand", },
	{},
};
MODULE_DEVICE_TABLE(platform, rb750_nand_id_table);

static struct platform_driver rb750_nand_driver = {
	.probe	= rb750_nand_probe,
	.remove	= rb750_nand_remove,
	.id_table = rb750_nand_id_table,
	.driver	= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(rb750_nand_driver);

MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_LICENSE("GPL v2");
