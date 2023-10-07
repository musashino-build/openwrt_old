// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/byteorder/generic.h>

#include "mtdsplit.h"

#define FWINFO_BLKSIZE		0x200
#define FWINFO_BLK2LEN(x)	(x * FWINFO_BLKSIZE)
#define FWINFO_NMLEN		0x20

#define NR_PARTS		2

struct fortigate_fwinfo {
	uint32_t unknown0[4];			/* unknown (0x0 - 0xf) */
	uint8_t  img1_name[FWINFO_NMLEN];	/* image1 name */
	uint8_t  img2_name[FWINFO_NMLEN];	/* image2 name */
	uint32_t unknown1[72];			/* unknown (0x50 - 0x16f) */
	uint8_t  active_img;			/* active image (0/1) */
	uint8_t  unknown2[15];			/* unknown (0x171 - 0x17f) */
	uint32_t kern1_ofs_blk;			/* kernel1 offset (block) */
	uint32_t kern1_len_blk;			/* kernel1 length (block) */
	uint32_t root1_ofs_blk;			/* rootfs1 offset (block) */
	uint32_t root1_len_blk;			/* rootfs1 length (block) */
	uint32_t kern2_ofs_blk;			/* kernel2 offset (block) */
	uint32_t kern2_len_blk;			/* kernel2 length (block) */
	uint32_t root2_ofs_blk;			/* rootfs2 offset (block) */
	uint32_t root2_len_blk;			/* rootfs2 length (block) */
	uint32_t unknown[24];			/* unknown (0x1a0 - 0x1ff) */
} __attribute__ ((packed));

static int mtdsplit_parse_fortigate(struct mtd_info *mtd,
				    const struct mtd_partition **pparts,
				    struct mtd_part_parser_data *data)
{
	struct mtd_info *fwinfo_mtd = get_mtd_device_nm("firmware-info");
	struct mtd_partition *parts;
	struct fortigate_fwinfo fwinfo;
	int fw_index;
	size_t rootfs_offset;
	size_t retlen;
	int ret;

	/* check whether the current "mtd" is a partition or not */
	if (!mtd_is_partition(mtd))
		return -EINVAL;

	if (IS_ERR(fwinfo_mtd))
		return PTR_ERR(fwinfo_mtd);

	ret = mtd_read(fwinfo_mtd, 0, sizeof(fwinfo), &retlen,
		       (void *) &fwinfo);
	if (ret)
		return ret;
	if (retlen != sizeof(fwinfo))
		return -EIO;

	/* no firmware in primary/secondary */
	if (!fwinfo.kern1_len_blk && !fwinfo.kern2_len_blk)
		return -ENODEV;

	/* get partition index of mtd */
	if (mtd->part.offset == FWINFO_BLK2LEN(fwinfo.kern1_ofs_blk))
		fw_index = 0;
	else if (mtd->part.offset == FWINFO_BLK2LEN(fwinfo.kern2_ofs_blk))
		fw_index = 1;
	else
		return -EINVAL;

	if (fwinfo.active_img != fw_index)
		return -ENODEV;

	/*
	 * get rootfs offset in partition
	 *
	 * Calculate rootfs offset instead of using fwinfo.rootN_ofs_blk.
	 * rootN_ofs_blk is a offset from 0x0 of the flash, not partition,
	 * and a complicated calculation in sysupgrade script will be needed
	 * if we use that offset value of rootfs.
	 */
	rootfs_offset = (fw_index == 0) ?
				FWINFO_BLK2LEN(fwinfo.kern1_len_blk) :
				FWINFO_BLK2LEN(fwinfo.kern2_len_blk);
	rootfs_offset = mtd_roundup_to_eb(rootfs_offset, mtd);

	ret = mtd_find_rootfs_from(mtd, rootfs_offset, mtd->size,
				   &rootfs_offset, NULL);
	if (ret)
		return ret;

	parts = kcalloc(NR_PARTS, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	parts[0].name = KERNEL_PART_NAME;
	parts[0].offset = 0;
	parts[0].size = rootfs_offset;

	parts[1].name = ROOTFS_PART_NAME;
	parts[1].offset = rootfs_offset;
	parts[1].size = mtd->size - rootfs_offset;

	*pparts = parts;
	return NR_PARTS;
}

static const struct of_device_id mtdsplit_fortigate_of_match_table[] = {
	{ .compatible = "fortinet,fortigate-firmware" },
	{},
};
MODULE_DEVICE_TABLE(of, mtdsplit_fortigate_of_match_table);

static struct mtd_part_parser mtdsplit_fortigate_parser = {
	.owner = THIS_MODULE,
	.name = "fortigate-fw",
	.of_match_table = mtdsplit_fortigate_of_match_table,
	.parse_fn = mtdsplit_parse_fortigate,
	.type = MTD_PARSER_TYPE_FIRMWARE,
};

module_mtd_part_parser(mtdsplit_fortigate_parser);
