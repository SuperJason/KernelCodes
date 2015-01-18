/* 
 * A simple char driver
 *
 * Create: Jason Jan.17th 2015
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		  /* everything... */
#include <linux/cdev.h>
#include <linux/device.h> /* class_create() */

#define ab_dbg(fmt, args...) printk(KERN_NOTICE	fmt, ## args)

#define DRIVER_NAME "achar"
#define CLASS_NAME "aclass"

static int achar_major = 0;
static int achar_minor = 0;
static int achar_nr_devs = 4;	/* number of bare achar devices */
static int achar_quantum = 4000;
static int achar_qset = 1000;

/*
 * Representation of achar quantum sets.
 */
struct achar_qset {
    void **data;
    struct achar_qset *next;
};

struct class *aclass; /* class structure */

struct achar_dev {
    struct achar_qset *data;  /* Pointer to first quantum set */
    int quantum;              /* the current quantum size */
    int qset;                 /* the current array size */
    unsigned long size;       /* amount of data stored here */
    unsigned int access_key;  /* used by acharuid and acharpriv */
    struct semaphore sem;     /* mutual exclusion semaphore     */
    struct cdev cdev;	        /* Char device structure		*/
    struct device *device;    /* device structure */
    char name[32];            /* device name */
};

struct achar_dev *achar_devices;	/* allocated in achar_init_module */

/*
 * Empty out the achar device; must be called with the device
 * semaphore held.
 */
int achar_trim(struct achar_dev *dev)
{
    struct achar_qset *next, *dptr;
    int qset = dev->qset;   /* "dev" is not-null */
    int i;

    ab_dbg("achar - %s()\n", __func__);

    for (dptr = dev->data; dptr; dptr = next) { /* all the list items */
        if (dptr->data) {
            for (i = 0; i < qset; i++)
                kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = achar_quantum;
    dev->qset = achar_qset;
    dev->data = NULL;

    return 0;
}
/*
 * Open and close
 */

int achar_open(struct inode *inode, struct file *filp)
{
    ab_dbg("achar - %s()\n", __func__);
#if 0
    struct achar_dev *dev; /* device information */

    dev = container_of(inode->i_cdev, struct achar_dev, cdev);
    filp->private_data = dev; /* for other methods */

    /* now trim to 0 the length of the device if open was write-only */
    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        achar_trim(dev); /* ignore errors */
        up(&dev->sem);
    }
#endif
    return 0;          /* success */
}

int achar_release(struct inode *inode, struct file *filp)
{
    ab_dbg("achar - %s()\n", __func__);
    return 0;
}

ssize_t achar_read (struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
    ab_dbg("achar - %s()\n", __func__);
    return 0; 
}

ssize_t achar_write (struct file *filp, const char __user *buf, size_t count,
        loff_t *pos)
{
    ab_dbg("achar - %s()\n", __func__);
    return count; 
}

struct file_operations achar_fops = {
    .owner =    THIS_MODULE,
#if 0
    .llseek =   achar_llseek,
    .ioctl =    achar_ioctl,
#endif
    .read =     achar_read,
    .write =    achar_write,
    .open =     achar_open,
    .release =  achar_release,
};

/*
 * Set up the char_dev structure for this device.
 */
static int achar_setup_cdev(struct achar_dev *dev, int index)
{
    int err = 0; 
    dev_t devno = MKDEV(achar_major, achar_minor + index);

    ab_dbg("achar - %s(dev, %d)\n", __func__, index);

    snprintf(dev->name, 32, "achar%c", index + '0');
    /* 
     * Create device
     */
    dev->device = device_create(aclass, NULL, devno, NULL,
            dev->name);
    if (IS_ERR(dev->device)) {
        err = PTR_ERR(dev->device);
        printk(KERN_ERR "%s: device_create failed %d\n",
                dev->name, err);
        goto error_class_device_create;
    }


    cdev_init(&dev->cdev, &achar_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &achar_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (err) {
        printk(KERN_NOTICE "Error %d adding achar%d", err, index);
        goto error_cdev_add;
    }

    return err;

error_cdev_add:
    device_destroy(aclass, devno);
error_class_device_create:
    return err;
}

void achar_cleanup_module(void)
{
    int i;
    dev_t devno;

    ab_dbg("achar - %s()\n", __func__);

    /* Get rid of our char dev entries */
    if (achar_devices) {
        for (i = 0; i < achar_nr_devs; i++) {
            achar_trim(achar_devices + i);
            cdev_del(&achar_devices[i].cdev);
            devno = MKDEV(achar_major, achar_minor + i);
            device_destroy(aclass, devno);
        }
        kfree(achar_devices);
    }

    class_destroy(aclass);

#ifdef SCULL_DEBUG /* use proc only if debugging */
    achar_remove_proc();
#endif

    /* cleanup_module is never called if registering failed */
    devno = MKDEV(achar_major, achar_minor);
    unregister_chrdev_region(devno, achar_nr_devs);

    /* and call the cleanup functions for friend devices */
#if 0
    achar_p_cleanup();
    achar_access_cleanup();
#endif
}


int achar_init_module(void)
{
    int result, i, err;
    dev_t dev = 0;

    ab_dbg("achar - %s()\n", __func__);

    /*
     * Get a range of minor numbers to work with, asking for a dynamic
     * major unless directed otherwise at load time.
     */
    if (achar_major) {
        dev = MKDEV(achar_major, achar_minor);
        result = register_chrdev_region(dev, achar_nr_devs, DRIVER_NAME);
    } else {
        result = alloc_chrdev_region(&dev, achar_minor, achar_nr_devs,
                DRIVER_NAME);
        achar_major = MAJOR(dev);
    }
    if (result < 0) {
        printk(KERN_WARNING "achar: can't get major %d\n", achar_major);
        return result;
    }
    ab_dbg("achar - %s: char_major = %d\n", __func__, achar_major);

    /*
     * Create class
     */
    aclass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(aclass)) {
        err = PTR_ERR(aclass);
        printk(KERN_ERR "%s: couldn't create class rc = %d\n",
                CLASS_NAME, err);
        goto error_class_create;
    }

    /* 
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */
    achar_devices = kmalloc(achar_nr_devs * sizeof(struct achar_dev), GFP_KERNEL);
    if (!achar_devices) {
        result = -ENOMEM;
        goto error_achar_devices_allc;  /* Make this more graceful */
    }
    memset(achar_devices, 0, achar_nr_devs * sizeof(struct achar_dev));

    /* Initialize each device. */
    for (i = 0; i < achar_nr_devs; i++) {
        achar_devices[i].quantum = achar_quantum;
        achar_devices[i].qset = achar_qset;
#if 0
        init_MUTEX(&achar_devices[i].sem);
#endif
        achar_setup_cdev(&achar_devices[i], i);
    }

    /* At this point call the init function for any friend device */
    dev = MKDEV(achar_major, achar_minor + achar_nr_devs);
#if 0
    dev += achar_p_init(dev);
    dev += achar_access_init(dev);
#endif

#ifdef ACHAR_DEBUG /* only when debugging */
    achar_create_proc();
#endif

    return 0; /* succeed */

error_achar_devices_allc:
error_class_create:
    achar_cleanup_module();
    return result;
}

module_init(achar_init_module);
module_exit(achar_cleanup_module);

MODULE_LICENSE("Dual BSD/GPL");
