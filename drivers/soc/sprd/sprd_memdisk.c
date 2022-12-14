#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/kernel.h>	/* pr_notice() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>

MODULE_LICENSE("Dual BSD/GPL");

static int memdisk_major;
module_param(memdisk_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE 512
#define MEMDISK_MINORS	16

struct memdisk_partition_info {
	unsigned long start;
	unsigned long size;	/* Device size in sectors */
	u8 *data;		/* The data array */
	const char *partition_name;
};
/*
 * The internal representation of our device.
 */
struct memdisk_dev {
	unsigned long size;	/* Device size in sectors */
	spinlock_t lock;	/* For mutual exclusion */
	struct request_queue *queue;	/* The device request queue */
	struct gendisk *gd;	/* The gendisk structure */
	struct blk_mq_tag_set tag_set;
	struct memdisk_partition_info *memdiskp[];
};
static int memdisks_count;
static struct memdisk_dev *memdisks;

/*
 * Handle an I/O request.
 */
static void memdisk_transfer(struct memdisk_dev *dev, sector_t sector,
			     unsigned long nsect, char *buffer, int write)
{
	int i;
	struct memdisk_partition_info *memdiskp = NULL;
	unsigned long offset = sector * hardsect_size;
	unsigned long nbytes = nsect * hardsect_size;

	if ((offset + nbytes) > (dev->size * hardsect_size)) {
		pr_notice("memdisk: Beyond-end write (%ld %ld)\n",
			  offset, nbytes);
		return;
	}

	for (i = 0; i < memdisks_count; i++)
		if (sector >= memdisks->memdiskp[i]->start &&
			sector < memdisks->memdiskp[i]->start + memdisks->memdiskp[i]->size) {
			offset = (sector - memdisks->memdiskp[i]->start) * hardsect_size;
			nbytes = nsect * hardsect_size;
			memdiskp = dev->memdiskp[i];
			break;
		}

	if (i == memdisks_count)
		return;

	if (write)
		memcpy(memdiskp->data + offset, buffer, nbytes);
	else
		memcpy(buffer, memdiskp->data + offset, nbytes);
}

static blk_status_t memdisk_queue_rq(struct blk_mq_hw_ctx *hctx,
					const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	struct request_queue *q = hctx->queue;
	struct memdisk_dev *dev = q->queuedata;
	struct bio_vec bvec;
	struct req_iterator iter;
	sector_t sector = blk_rq_pos(req);

	blk_mq_start_request(req);
	spin_lock_irq(&memdisks->lock);

	if (blk_rq_is_passthrough(req)) {
		pr_notice("Skip non-CMD request/n");
		blk_mq_end_request(req, BLK_STS_IOERR);
		spin_unlock_irq(&memdisks->lock);
		return BLK_STS_IOERR;
	}

	rq_for_each_segment(bvec, req, iter) {
		char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset;
		unsigned nsecs = bvec.bv_len >> SECTOR_SHIFT;
		memdisk_transfer(dev, sector, nsecs,
				buffer, rq_data_dir(req));
		sector += nsecs;
		kunmap_atomic(buffer);
	}

	blk_mq_end_request(req, BLK_STS_OK);
	spin_unlock_irq(&memdisks->lock);
	return BLK_STS_OK;
}

static const struct blk_mq_ops memdisk_mq_ops = {
	.queue_rq = memdisk_queue_rq,
};


/*
 * The HDIO_GETGEO ioctl is handled in blkdev_ioctl(), i
 * calls this. We need to implement getgeo, since we can't
 * use tools such as fdisk to partition the drive otherwise.
 */
int memdisk_getgeo(struct block_device *bd, struct hd_geometry *geo)
{
	long size;
	struct memdisk_dev *dev = bd->bd_disk->private_data;

	pr_notice("memdisk_getgeo. \n");
	size = dev->size * (hardsect_size / KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 4;

	return 0;
}

/*
 * The device operations structure.
 */
static struct block_device_operations memdisk_ops = {
	.owner = THIS_MODULE,
	.getgeo = memdisk_getgeo
};

/*
 * Set up our internal device.
 */
static void memdisk_setup_device(void)
{
	int i;
#ifdef CONFIG_SPRD_MEMDISK_PARTITION
	sector_t len;
	struct partition_meta_info info;
	struct hd_struct *part;
	sector_t start = 0;
#endif


	spin_lock_init(&memdisks->lock);

	memdisks->queue = blk_mq_init_sq_queue(&memdisks->tag_set, &memdisk_mq_ops,
							1, BLK_MQ_F_SHOULD_MERGE);

	if (memdisks->queue == NULL) {
		pr_notice("memdisk_setup_device blk_mq_init_sq_queue failure. \n");
		return;
	}

	blk_queue_logical_block_size(memdisks->queue, hardsect_size);
	memdisks->queue->queuedata = memdisks;
	/*
	 * And the gendisk structure.
	 */
	memdisks->gd = alloc_disk(MEMDISK_MINORS);
	if (!memdisks->gd) {
		pr_notice("memdisk_setup_device alloc_disk failure. \n");
		return;
	}
	memdisks->gd->major = memdisk_major;
	memdisks->gd->first_minor = 0;
	memdisks->gd->fops = &memdisk_ops;
	memdisks->gd->queue = memdisks->queue;
	memdisks->gd->private_data = memdisks;

#ifdef CONFIG_SPRD_MEMDISK_PARTITION
	sprintf(memdisks->gd->disk_name,  "memdisk0");
#else
	sprintf(memdisks->gd->disk_name,  "memdisk0p1");
#endif
	for (i = 0; i < memdisks_count; i++)
		memdisks->size += memdisks->memdiskp[i]->size;

	set_capacity(memdisks->gd,
		     (sector_t)(memdisks->size * (hardsect_size / KERNEL_SECTOR_SIZE)));
	add_disk(memdisks->gd);

#ifdef CONFIG_SPRD_MEMDISK_PARTITION
	for (i = 0; i < memdisks_count; i++) {
		sprintf(info.volname, "%s", memdisks->memdiskp[i]->partition_name);
		sprintf(info.uuid, "memdisk0.p%d", i);
		len = (sector_t)(memdisks->memdiskp[i]->size * (hardsect_size / KERNEL_SECTOR_SIZE));
		start = memdisks->memdiskp[i]->start * (hardsect_size / KERNEL_SECTOR_SIZE);
		part = add_partition(memdisks->gd, i+1, start, len, ADDPART_FLAG_NONE, &info);
		if (IS_ERR(part)) {
			printk(KERN_ERR " %s: p%d could not be added: %ld\n",
			       memdisks->gd->disk_name, i+1, -PTR_ERR(part));
			continue;
		}
	}
#endif

	pr_notice("memdisk_setup_device i:%d success.\n", i);
	return;
}

static void *memdisk_ram_vmap(phys_addr_t start, size_t size,
				 unsigned int memtype)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	if (memtype)
		prot = pgprot_noncached(PAGE_KERNEL);
	else
		prot = pgprot_writecombine(PAGE_KERNEL);

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n",
		       __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	vaddr = vm_map_ram(pages, page_count, -1, prot);
	kfree(pages);

	return vaddr;
}

static int __init memdisk_init(void)
{
	int i = 0;
	int ret = 0;
	int initialized_node_num = 0;
	const char *name;
	struct resource res = { 0 };
	struct device_node *np = NULL;
	struct device_node *memnp = NULL;
	struct device_node *child = NULL;
	struct memdisk_partition_info *memdiskp = NULL;

	pr_notice("sprd memdisk init \n");
	memdisk_major = register_blkdev(memdisk_major, "memdisk");
	if (memdisk_major <= 0) {
		pr_notice("memdisk: unable to get major number\n");
		return -EBUSY;
	}

	np = of_find_compatible_node(NULL, NULL, "sprd,memdisk");
	if (!np)
		return -ENODEV;

	for_each_child_of_node(np, child)
		memdisks_count++;

	memdisks = kzalloc(sizeof(struct memdisk_dev) +
		memdisks_count * sizeof(void *), GFP_KERNEL);
	if (!memdisks)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		memdiskp = kzalloc(sizeof(struct memdisk_partition_info),
			GFP_KERNEL);
		if (!memdiskp) {
			ret = -ENOMEM;
			goto err_1;
		}

		memnp = of_parse_phandle(child, "memory-region", 0);
		if (!memnp) {
			ret = -ENODEV;
			goto err_2;
		}

		ret = of_address_to_resource(memnp, 0, &res);
		if (0 != ret) {
			pr_notice("of_address_to_resource failed!\n");
			ret = -EINVAL;
			goto err_2;
		}


		ret = of_property_read_string(child, "label", &name);
		if (ret) {
			pr_notice("para lable failed!\n");
			ret = -EINVAL;
			goto err_2;
		}

#ifdef CONFIG_64BIT
		pr_notice("memdisk %d res start 0x%llx,end 0x%llx\n", i,
			  res.start, res.end);
#else
		pr_notice("memdisk %d res start 0x%x,end 0x%x\n", i,
			  res.start, res.end);
#endif
		memdiskp->data =
		    memdisk_ram_vmap(res.start, resource_size(&res), 0);
		if (!memdiskp->data) {
			pr_notice("sprd memdisk%d map error!\n", i);
			ret = -ENOMEM;
			goto err_2;
		}

		memdiskp->partition_name = name;
		memdiskp->size = resource_size(&res) / hardsect_size;
		memdiskp->start = (i == 0 ? 0 : memdisks->memdiskp[i-1]->start + memdisks->memdiskp[i-1]->size);
		memdisks->memdiskp[i] = memdiskp;

		i++;
		initialized_node_num++;
	}

	memdisk_setup_device();
	of_node_put(np);
	of_node_put(memnp);
	pr_notice("memdisk_init finished. \n");

	return 0;

err_2:
	kfree(memdiskp);

err_1:
	for (i = 0; i < memdisks_count; i++)
		if (memdisks->memdiskp[i]->data)
			vm_unmap_ram(memdisks->memdiskp[i]->data,
				memdisks->memdiskp[i]->size * hardsect_size);

	for (i = 0; i < initialized_node_num; i++)
		kfree(memdisks->memdiskp[i]);

	kfree(memdisks);

	return ret;
}

static void __exit memdisk_exit(void)
{
	int i;

	pr_notice("memdisk_exit. \n");

	for (i = 0; i < memdisks_count; i++)
		if (memdisks->memdiskp[i]->data)
			vm_unmap_ram(memdisks->memdiskp[i]->data,
				memdisks->memdiskp[i]->size * hardsect_size);

	if (memdisks->gd) {
		del_gendisk(memdisks->gd);
		put_disk(memdisks->gd);
	}
	if (memdisks->queue) {
		blk_cleanup_queue(memdisks->queue);
	}
	unregister_blkdev(memdisk_major, "memdisk");

	for (i = 0; i < memdisks_count; i++)
		kfree(memdisks->memdiskp[i]);

	kfree(memdisks);
}

module_init(memdisk_init);
module_exit(memdisk_exit);
