// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * System partition driver for NetBSD-based NEC Aterm series
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/math64.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define NEC_BLKHDR_LEN		0x40
#define NEC_BLKHDR_MAGIC	0x30534654 /* "0SFT" */
#define NEC_MAX_IMAGES		4

struct nec_block_header {
	char name[16];
	uint blkidx;
	uint dlen;
	uint magic;
	u8 pad[36];
} __attribute__((__packed__));

struct nec_syspart {
	struct mtd_info mtd;
	struct mtd_info *parent;
	uint32_t p_bs;		/* parent blocksize */
	uint32_t c_bs;		/* child blocksize */
	int nblk;		/* total blocks */
	int nblk_u;		/* using blocks */
};

static int offset_to_block(struct nec_syspart *sysp, loff_t offset,
			   uint32_t *boffset)
{
	if (offset > sysp->mtd.size)
		return -EINVAL;

	return (int)div_s64_rem(offset, sysp->c_bs, boffset);
}

static int nec_syspart_read(struct mtd_info *mtd, loff_t from, size_t len,
			    size_t *retlen, u_char *buf)
{
	struct nec_syspart *sysp = mtd->priv;
	size_t rlen, read, rlen_max;
	uint32_t nec_boffset;
	int startblk;
	int i, ret;
	loff_t roff;

	startblk = offset_to_block(sysp, from, &nec_boffset);
	rlen_max = sysp->c_bs - nec_boffset;
	i = startblk;

	if (startblk < 0)
		return startblk;

	while (len > 0) {
		rlen = (len > rlen_max) ? rlen_max : len;
		roff = sysp->p_bs * i
			+ NEC_BLKHDR_LEN
			+ ((i == startblk) ? nec_boffset : 0);

		ret = mtd_read(sysp->parent, roff, rlen, &read, buf);
		if (ret)
			return ret;
		else if (read != rlen)
			return -EIO;

		buf += read;
		len -= read;
		*retlen += read;
		if (i == startblk)
			rlen_max = sysp->c_bs;
		i++;
	}

	return 0;
}

static int nec_syspart_write(struct mtd_info *mtd, loff_t to, size_t len,
			     size_t *retlen, const u_char *buf)
{
	struct nec_syspart *sysp = mtd->priv;
	size_t wlen, written, wlen_max;
	uint32_t nec_boffset;
	int startblk;
	int i, ret;
	loff_t woff;

	startblk = offset_to_block(sysp, to, &nec_boffset);
	wlen_max = sysp->c_bs - nec_boffset;
	i = startblk;

	if (startblk < 0)
		return startblk;

	while (len > 0) {
		wlen = (len > wlen_max) ? wlen_max : len;
		woff = sysp->p_bs * i
			+ NEC_BLKHDR_LEN
			+ ((i == startblk) ? nec_boffset : 0);

		ret = mtd_write(sysp->parent, woff, wlen, &written, buf);
		if (ret)
			return ret;
		else if (written != wlen)
			return -EIO;

		buf += written;
		len -= written;
		*retlen += written;
		if (i == startblk)
			wlen_max = sysp->c_bs;
		i++;
	}

	return 0;
}

static int nec_syspart_parse_parts(struct nec_syspart *sysp,
				   struct mtd_partition *parts)
{
	struct mtd_info *pmtd = sysp->parent;
	struct nec_block_header header;
	u_char buf[NEC_BLKHDR_LEN];
	int i = 0, ret, nr_parts = 0;
	size_t read;

	while (i < sysp->nblk) {
		if (nr_parts >= NEC_MAX_IMAGES) {
			pr_warn("exceeds maximum partitions number (>4)\n");
			break;
		}

		ret = mtd_read(pmtd, (loff_t)(i * sysp->p_bs), NEC_BLKHDR_LEN,
			       &read, buf);
		if (ret)
			return ret;
		if (read != NEC_BLKHDR_LEN)
			return -EIO;

		memcpy(&header, buf, NEC_BLKHDR_LEN);
		if (header.magic != NEC_BLKHDR_MAGIC)
			break;

		parts[nr_parts].name = kstrdup(header.name, GFP_KERNEL);
		parts[nr_parts].offset = i * sysp->c_bs;
		parts[nr_parts].size = header.dlen;
		parts[nr_parts].of_node
			= of_find_node_by_name(mtd_get_of_node(&sysp->mtd),
					       header.name);

		nr_parts++;
		i += header.dlen / sysp->c_bs;
		if (header.dlen % sysp->c_bs)
			i++;
	}

	sysp->nblk_u = i;

	return nr_parts;
}

static int nec_syspart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mtd_info *pmtd;
	struct mtd_partition parts[NEC_MAX_IMAGES];
	struct nec_syspart *sysp;
	int ret;

	pmtd = of_get_mtd_device_by_node(np);
	if (IS_ERR(pmtd))
		return PTR_ERR(pmtd);

	dev_info(dev, "got parent mtd: \"%s\"\n", pmtd->name);

	if (!mtd_is_partition(pmtd)) {
		dev_err(dev, "parent mtd is not a partition\n");
		return -EINVAL;
	}

	sysp = devm_kzalloc(dev, sizeof(*sysp), GFP_KERNEL);
	if (!sysp)
		return -ENOMEM;

	sysp->p_bs = pmtd->erasesize;
	sysp->c_bs = pmtd->erasesize - NEC_BLKHDR_LEN;
	sysp->nblk = div_u64(pmtd->size, pmtd->erasesize);
	sysp->parent = pmtd;

	sysp->mtd.priv = sysp;
	sysp->mtd.dev.parent = dev;
	sysp->mtd.name = "nec-system";
	mtd_set_of_node(&sysp->mtd, np);

	sysp->mtd.type = MTD_DATAFLASH;
	sysp->mtd.flags = MTD_CAP_RAM;
	sysp->mtd.size = sysp->nblk * sysp->c_bs;
	sysp->mtd.erasesize = sysp->c_bs;
	sysp->mtd.writesize = 1;
	sysp->mtd.writebufsize = 1;

//TBD	sysp->_erase = nec_syspart_erase;
	sysp->mtd._write = nec_syspart_write;
	sysp->mtd._read = nec_syspart_read;

	memset(parts, 0, sizeof(parts[0]) * NEC_MAX_IMAGES);
	ret = nec_syspart_parse_parts(sysp, parts);
	if (ret < 0)
		return ret;

	dev_info(dev, "using %u blocks\n", sysp->nblk_u);

	return mtd_device_register(&sysp->mtd, parts, ret);
}

static int nec_syspart_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id nec_syspart_match[] = {
	{ .compatible = "nec,aterm-syspart" },
	{},
};
MODULE_DEVICE_TABLE(of, nec_syspart_match);

static struct platform_driver nec_syspart_driver = {
	.probe		= nec_syspart_probe,
	.remove		= nec_syspart_remove,
	.driver = {
		.name	= "nec-syspart",
		.owner	= THIS_MODULE,
		.of_match_table = nec_syspart_match,
	},
};
module_platform_driver(nec_syspart_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("System Partition on NetBSD-based Aterm series");
