/*
 * Sample disk driver, from the beginning.
 *
 * Jan.14th, 2015 Create
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blkdev.h> /* register_blkdev() */
#include <linux/vmalloc.h> /* vmalloc(), vfree() */

#define ab_dbg(fmt, args...) printk(KERN_NOTICE	fmt, ## args)

static int ablock_major = 0;
module_param(ablock_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 1024;	/* How big the drive is */
module_param(nsectors, int, 0);
static int ndevices = 4;
module_param(ndevices, int, 0);

/*
 * Minor number and partition management.
 */
#define ABLOCK_MINORS	16

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#define KERNEL_SECTOR_SIZE	512

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY	30*HZ

/*
 * The different "request modes" we can use.
 */
enum {
	RM_SIMPLE  = 0,	/* The extra-simple request function */
	RM_FULL    = 1,	/* The full-blown version */
	RM_NOQUEUE = 2,	/* Use make_request */
};
static int request_mode = RM_SIMPLE;
module_param(request_mode, int, 0);

/*
 * The internal representation of our device.
 */
struct ablock_dev {
        int size;                       /* Device size in sectors */
        u8 *data;                       /* The data array */
        short users;                    /* How many users */
        short media_change;             /* Flag a media change? */
        spinlock_t lock;                /* For mutual exclusion */
        struct request_queue *queue;    /* The device request queue */
        struct gendisk *gd;             /* The gendisk structure */
        struct timer_list timer;        /* For simulated media changes */
};

static struct ablock_dev *Devices = NULL;

/*
 * The direct make request version.
 */
void ablock_make_request(struct request_queue *q, struct bio *bio)
{
    ab_dbg("ablock - %s()\n", __func__);
#if 0
	struct ablock_dev *dev = q->queuedata;
	int status;

	status = ablock_xfer_bio(dev, bio);
	bio_endio(bio, bio->bi_size, status);
	return 0;
#endif
}

/*
 * Smarter request function that "handles clustering".
 */
static void ablock_full_request(struct request_queue *q)
{
    ab_dbg("ablock - %s()\n", __func__);
#if 0
    struct request *req;
    int sectors_xferred;
    struct ablock_dev *dev = q->queuedata;

    while ((req = elv_next_request(q)) != NULL) {
        if (! blk_fs_request(req)) {
            printk (KERN_NOTICE "Skip non-fs request\n");
            end_request(req, 0);
            continue;
        }
        sectors_xferred = ablock_xfer_request(dev, req);
        if (! end_that_request_first(req, 1, sectors_xferred)) {
            blkdev_dequeue_request(req);
            end_that_request_last(req);
        }
    }
#endif
}

/*
 * Handle an I/O request.
 */
static void ablock_transfer(struct ablock_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
    unsigned long offset = sector*KERNEL_SECTOR_SIZE;
    unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;

    ab_dbg("ablock - %s()\n", __func__);

    if ((offset + nbytes) > dev->size) {
        printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
        return;
    }
    if (write)
        memcpy(dev->data + offset, buffer, nbytes);
    else
        memcpy(buffer, dev->data + offset, nbytes);
}

/*
 * The simple form of the request function.
 */
static void ablock_request(struct request_queue *q)
{
    struct request *req;
    int err = 0;

    ab_dbg("ablock - %s()\n", __func__);
    req = blk_fetch_request(q);
    while (req) {
        struct ablock_dev *dev = req->rq_disk->private_data;

        ab_dbg("ablock ablock_request: dev %d dir %d sec %ld, nr %d f %x\n", 
                dev - Devices,
                rq_data_dir(req),
                (long int)blk_rq_pos(req), 
                blk_rq_cur_sectors(req),
                req->cmd_flags);
        ablock_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req), 
                req->buffer, rq_data_dir(req));

        if (!__blk_end_request_cur(req, err))
            req = blk_fetch_request(q);
    }
}

/*
 * Open and close.
 */

static int ablock_open(struct block_device *bdev, fmode_t mode)
{
    struct ablock_dev *dev = bdev->bd_disk->private_data;

    ab_dbg("ablock - %s()\n", __func__);

    del_timer_sync(&dev->timer);
    spin_lock(&dev->lock);
    if (! dev->users) 
        check_disk_change(bdev);
    dev->users++;
    spin_unlock(&dev->lock);

    return 0;
}

static int ablock_release(struct gendisk *gd, fmode_t mode)
{
    struct ablock_dev *dev = gd->private_data;

    ab_dbg("ablock - %s()\n", __func__);

    spin_lock(&dev->lock);
    dev->users--;

    if (!dev->users) {
        dev->timer.expires = jiffies + INVALIDATE_DELAY;
        add_timer(&dev->timer);
    }
    spin_unlock(&dev->lock);

    return 1;
}

/*
 * Look for a (simulated) media change.
 */
int ablock_media_changed(struct gendisk *gd)
{
    struct ablock_dev *dev = gd->private_data;

    ab_dbg("ablock - %s()\n", __func__);

    return dev->media_change;
}

/*
 * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
 * with open.  That needs to be reevaluated.
 */
int ablock_revalidate(struct gendisk *gd)
{
    struct ablock_dev *dev = gd->private_data;

    ab_dbg("ablock - %s()\n", __func__);

    if (dev->media_change) {
        dev->media_change = 0;
        memset (dev->data, 0, dev->size);
    }

    return 0;
}

/*
 * The "invalidate" function runs out of the device timer; it sets
 * a flag to simulate the removal of the media.
 */
void ablock_invalidate(unsigned long ldev)
{
    struct ablock_dev *dev = (struct ablock_dev *) ldev;

    ab_dbg("ablock - %s()\n", __func__);

    spin_lock(&dev->lock);
    if (dev->users || !dev->data) 
        printk (KERN_WARNING "ablock: timer sanity check failed\n");
    else
        dev->media_change = 1;
    spin_unlock(&dev->lock);
}

/*
 * The ioctl() implementation
 */

#if 0
int ablock_ioctl (struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
	long size;
	struct hd_geometry geo;
	struct ablock_dev *dev = filp->private_data;

	switch(cmd) {
	    case HDIO_GETGEO:
        	/*
		 * Get geometry: since we are a virtual device, we have to make
		 * up something plausible.  So we claim 16 sectors, four heads,
		 * and calculate the corresponding number of cylinders.  We set the
		 * start of data at sector four.
		 */
		size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
		geo.cylinders = (size & ~0x3f) >> 6;
		geo.heads = 4;
		geo.sectors = 16;
		geo.start = 4;
		if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}

	return -ENOTTY; /* unknown command */
}
#else
int ablock_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long param)
{
    ab_dbg("ablock - %s()\n", __func__);

    return -ENOTTY; /* unknown command */
}
#endif

/*
 * The device operations structure.
 */
static struct block_device_operations ablock_ops = {
	.owner           = THIS_MODULE,
	.open 	         = ablock_open,
	.release 	       = ablock_release,
	.media_changed   = ablock_media_changed,
	.revalidate_disk = ablock_revalidate,
	.ioctl	         = ablock_ioctl
};

/*
 * Set up our internal device.
 */
static void setup_device(struct ablock_dev *dev, int which)
{
    ab_dbg("ablock - %s(): %d\n", __func__, which);
    /*
     * Get some memory.
     */
    memset (dev, 0, sizeof (struct ablock_dev));
    dev->size = nsectors*hardsect_size;
    ab_dbg("ablock setup_device: vmalloc(%d)\n", dev->size);
    dev->data = vmalloc(dev->size);
    if (dev->data == NULL) {
        printk (KERN_NOTICE "vmalloc failure.\n");
        return;
    }
    ab_dbg("ablock setup_device: spin_lock_init()\n");
    spin_lock_init(&dev->lock);

    /*
     * The timer which "invalidates" the device.
     */
    ab_dbg("ablock setup_device: init_timer()\n");
    init_timer(&dev->timer);
    dev->timer.data = (unsigned long) dev;
    dev->timer.function = ablock_invalidate;

    /*
     * The I/O queue, depending on whether we are using our own
     * make_request function or not.
     */
    switch (request_mode) {
        case RM_NOQUEUE:
            ab_dbg("ablock setup_device: blk_alloc_queue()\n");
            dev->queue = blk_alloc_queue(GFP_KERNEL);
            if (dev->queue == NULL)
                goto out_vfree;
            ab_dbg("ablock setup_device: blk_queue_make_request()\n");
            blk_queue_make_request(dev->queue, ablock_make_request);
            break;

        case RM_FULL:
            ab_dbg("ablock setup_device: blk_init_queue() RM_FULL\n");
            dev->queue = blk_init_queue(ablock_full_request, &dev->lock);
            if (dev->queue == NULL)
                goto out_vfree;
            break;

        default:
            printk(KERN_NOTICE "Bad request mode %d, using simple\n", request_mode);
            /* fall into.. */

        case RM_SIMPLE:
            ab_dbg("ablock setup_device: blk_init_queue() RM_SIMPLE\n");
            dev->queue = blk_init_queue(ablock_request, &dev->lock);
            if (dev->queue == NULL)
                goto out_vfree;
            break;
    }
    /* TODO 
    dev->queue->hardsect_size = hardsect_size; */
    dev->queue->queuedata = dev;
    /*
     * And the gendisk structure.
     */
    ab_dbg("ablock setup_device: alloc_disk()\n");
    dev->gd = alloc_disk(ABLOCK_MINORS);
    if (! dev->gd) {
        printk (KERN_NOTICE "alloc_disk failure\n");
        goto out_vfree;
    }
    dev->gd->major = ablock_major;
    dev->gd->first_minor = which*ABLOCK_MINORS;
    dev->gd->fops = &ablock_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf (dev->gd->disk_name, 32, "ablock%c", which + 'a');
    ab_dbg("ablock setup_device: set_capacity()\n");
    set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
    ab_dbg("ablock setup_device: add_disk()\n");
    add_disk(dev->gd);
    return;

out_vfree:
    if (dev->data)
        vfree(dev->data);
}

static int __init ablock_init(void)
{
    int i;

    ab_dbg("ablock - %s()\n", __func__);
    /*
     * Get registered.
     */
    ablock_major = register_blkdev(ablock_major, "ablock");
    printk(KERN_ALERT "ablock: ablock_major = %d \n", ablock_major);
    if (ablock_major <= 0) {
        printk(KERN_WARNING "ablock: unable to get major number\n");
        return -EBUSY;
    }
    /*
     * Allocate the device array, and initialize each one.
     */
    Devices = kmalloc(ndevices*sizeof (struct ablock_dev), GFP_KERNEL);
    if (Devices == NULL)
        goto out_unregister;
    for (i = 0; i < ndevices; i++) 
        setup_device(Devices + i, i);

    return 0;

out_unregister:
    unregister_blkdev(ablock_major, "ablock");
    return -ENOMEM;
}

static void ablock_exit(void)
{
    int i;

    ab_dbg("ablock - %s()\n", __func__);
    for (i = 0; i < ndevices; i++) {
        struct ablock_dev *dev = Devices + i;

        del_timer_sync(&dev->timer);
        if (dev->gd) {
            del_gendisk(dev->gd);
            put_disk(dev->gd);
        }
        if (dev->queue) {
            if (request_mode == RM_NOQUEUE)
                blk_put_queue(dev->queue);
            else
                blk_cleanup_queue(dev->queue);
        }
        if (dev->data)
            vfree(dev->data);
    }
    unregister_blkdev(ablock_major, "ablock");
    kfree(Devices);
}
	
module_init(ablock_init);
module_exit(ablock_exit);

MODULE_LICENSE("Dual BSD/GPL");
