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

//#define NEC_BLOCKSIZE		0xffc0
#define NEC_BLKHDR_LEN		0x40
#define NEC_BLKHDR_MAGIC	0x30534654 /* "0SFT" */

struct nec_block_header {
	char name[16];
	uint blkidx;
	uint dlen;
	uint magic;
	u8 pad[36];
} __attribute__((__packed__));

struct nec_syspart {
	struct mtd_info mtd;
	uint32_t p_bs;	/* parent blocksize */
	uint32_t c_bs;	/* child blocksize */
	int nblk;	/* total blocks */
	int nblk_u;	/* using blocks */
};

static int offset_to_block(struct nec_syspart *sysp, loff_t offset,
			   uint32_t *boffset)
{
	int i;

	if (offset > sysp->mtd.size)
		return -EINVAL;

	i = (int)div_s64_rem(offset, sysp->c_bs, boffset);
	if (boffset > 0)
		i++;

	return i;
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
		return -EINVAL;

	while (len > 0) {
		rlen = (len > rlen_max) ? rlen_max : len;
		roff = sysp->p_bs * i
			+ NEC_BLKHDR_LEN
			+ (i == startblk) ? nec_boffset : 0;

		ret = mtd_read(sysp->mtd.parent, roff, rlen, &read, buf);
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
		return -EINVAL;

	while (len > 0) {
		wlen = (len > wlen_max) ? wlen_max : len;
		woff = sysp->p_bs * i
			+ NEC_BLKHDR_LEN
			+ (i == startblk) ? nec_boffset : 0;

		ret = mtd_write(sysp->mtd.parent, woff, wlen, &written, buf);
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

static int nec_syspart_parse_parts(struct nec_syspart *sysp)
{
	struct mtd_info *pmtd = sysp->mtd.parent;
	struct nec_block_header header;
	u_char buf[NEC_BLKHDR_LEN];
	int i, ret;
	size_t read;

	for (i = 0; i < sysp->nblk; i++) {
		ret = mtd_read(pmtd, (loff_t)(i * sysp->p_bs), NEC_BLKHDR_LEN,
			       &read, buf);
		if (ret)
			return ret;
		if (read != NEC_BLKHDR_LEN)
			return -EIO;

		memcpy(&header, buf, NEC_BLKHDR_LEN);
		if (header.magic != NEC_BLKHDR_MAGIC)
			break;
		pr_info("name-> \"%s\", len-> 0x%08x (%u bytes), index-> %u\n",
			header.name, header.dlen, header.dlen, header.blkidx);
	}

	sysp->nblk_u = i;

	return 0;
}

static int nec_syspart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mtd_info *pmtd;
	struct nec_syspart *sysp;
	int ret;

	pmtd = of_get_mtd_device_by_node(np);
	if (IS_ERR(pmtd)) {
		dev_err(dev, "failed to get parent mtd device\n");
		return PTR_ERR(pmtd);
	}

	dev_info(dev, "got parent mtd: name-> \"%s\", erasefize-> 0x%08x,"
		      " writesize-> 0x%08x\n",
		 pmtd->name, pmtd->erasesize, pmtd->writesize);

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

	sysp->mtd.priv = sysp;
	sysp->mtd.dev.parent = dev;
	sysp->mtd.parent = pmtd;
	sysp->mtd.name = "nec-system";

	sysp->mtd.type = MTD_DATAFLASH;
	sysp->mtd.flags = MTD_CAP_NORFLASH;
	sysp->mtd.size = sysp->nblk * sysp->c_bs;
	sysp->mtd.erasesize = sysp->c_bs;
	sysp->mtd.writesize = 1;
	sysp->mtd.writebufsize = 1;

//TBD	sysp->_erase = nec_syspart_erase;
	sysp->mtd._write = nec_syspart_write;
	sysp->mtd._read = nec_syspart_read;

	ret = nec_syspart_parse_parts(sysp);
	if (ret)
		return ret;

	dev_info(dev, "using %u blocks in parent mtd\n", sysp->nblk_u);

	return 0;
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
