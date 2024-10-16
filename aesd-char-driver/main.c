/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
#include "aesd-circular-buffer.h"


#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(type, arg, cmd)
#else
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(arg, cmd)
#endif

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

#define AESDCHAR_TMP_BUFFER_SIZE        256

struct aesd_dev aesd_device;





int aesd_open(struct inode *inode, struct file *filp)
{
	struct aesd_dev *dev;

    PDEBUG("open");

	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */

	return 0;          /* success */
}


int aesd_release(struct inode *inode, struct file *filp)
{
    //struct aesd_dev *dev = filp->private_data;

    PDEBUG("release");

    return 0;
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry * aesd_buffer;
    size_t aesd_buffer_rt;

    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

    while (retval != count) {
        size_t copy_cnt = 0;
        aesd_buffer = aesd_circular_buffer_find_entry_offset_for_fpos(dev->circular_buffer, (size_t)((*f_pos) + retval), &aesd_buffer_rt);

        if (aesd_buffer) {

            if ((aesd_buffer->size - aesd_buffer_rt) >= (count-retval))
                copy_cnt = count-retval;
            else
                copy_cnt = aesd_buffer->size - aesd_buffer_rt;

            if (copy_to_user((void*)(buf + retval), (void*)&aesd_buffer->buffptr[aesd_buffer_rt], copy_cnt)) {
                PERROR("error copying to user buffer\n");
                retval = -EFAULT;
                goto out;
            }

            PDEBUG("read circular buffer from pos %zu, obtain %zu bytes, copied %zu\n", \
                                                        (size_t)((*f_pos) + retval), \
                                                        aesd_buffer->size - aesd_buffer_rt, \
                                                        copy_cnt);
            retval += copy_cnt;

        } else {
            PDEBUG("read circular buffer from pos %zu, obtain 0 bytes\n", (size_t)((*f_pos) + retval));
            break;
        }
    }
    
    *f_pos += retval;
    PDEBUG("returned %zu bytes readed, f_pos: %zu\n", (size_t)retval, (size_t)*f_pos);

  out:
	mutex_unlock(&dev->lock);
	return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry new_entry;
    char *unlinked_buffptr;
    ssize_t retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
    
    // Copy data to temporal buffer
    if (AESDCHAR_TMP_BUFFER_SIZE < (dev->tmp_buffer.data_size + count)) {
        PDEBUG("temporal buffer insuficient to allocate %zu bytes", dev->tmp_buffer.data_size + count);
		retval = -EFAULT;
        // Clear temporal buffer
        dev->tmp_buffer.data_size = 0;
        memset(dev->tmp_buffer.buffer, 0x00, AESDCHAR_TMP_BUFFER_SIZE);
        goto out;
    }

    if (copy_from_user(&dev->tmp_buffer.buffer[dev->tmp_buffer.data_size], buf, count)) {
		retval = -EFAULT;
		goto out;
	}
    PDEBUG("Copy %zu bytes to tmp_buffer in offset %d", count, dev->tmp_buffer.data_size);
    dev->tmp_buffer.data_size += count;

    retval = count;

    if (dev->tmp_buffer.buffer[dev->tmp_buffer.data_size-1] == '\n')
    {
        PDEBUG("Try to write %d bytes. Requested %zu with offset %lld", dev->tmp_buffer.data_size, count, *f_pos);

        // Allocate buffer for the new string
        new_entry.buffptr = kmalloc(dev->tmp_buffer.data_size, GFP_KERNEL);
        if (!new_entry.buffptr){
            retval = -ENOMEM;
            goto out;
        }
        PDEBUG("Alocated %d bytes for new buffer string\n", dev->tmp_buffer.data_size);

        // Copy data to the new buffer
        memcpy((void*)new_entry.buffptr, dev->tmp_buffer.buffer, dev->tmp_buffer.data_size);

        new_entry.size = dev->tmp_buffer.data_size;

        unlinked_buffptr = (char*)aesd_circular_buffer_add_entry(dev->circular_buffer, &new_entry);

        // Free buffer containig the string unlinked from the circular buffer due to buffer full
        if (unlinked_buffptr){
            PDEBUG("Dealocated buffer string passed from circular buffer\n");
            kfree(unlinked_buffptr);
        }

        // Clear temporal buffer
        dev->tmp_buffer.data_size = 0;
        memset(dev->tmp_buffer.buffer, 0x00, AESDCHAR_TMP_BUFFER_SIZE);

        // update f_pos
        *f_pos += retval;
    }

out:
	mutex_unlock(&dev->lock);
	return retval;
}


loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t returned_off;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    returned_off = fixed_size_llseek(filp, offset, whence, dev->circular_buffer->size);
    PDEBUG("seek request to pos: %d, returned %d\n", (int)offset, (int)returned_off);
	mutex_unlock(&dev->lock);
    
    return returned_off;
}

/**
 * Adjust the file offset (f_pos) parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced command to locate)
 * and @param write_cmd_offset (the zero referenced offset into the command)
 * @return 0 if successful, negative if error occurred:
 *      -ERESTARTSYS if mutex could not be obtained
 *      -EINVAL if write command or write_cmd_offset was out of range
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
	int retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct list_buffer_entry_s *actual;
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
        
    if (write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        retval = -EINVAL;
        goto out;
    }

    int entr= 0;
    loff_t loff = 0;

    list_for_each_entry(actual, &aesd_device.circular_buffer->list_head, entries) {
        if (entr == write_cmd) {
            if (write_cmd_offset >= actual->entry.size) {
                retval = -EINVAL;
                goto out;
            }
            filp->f_pos = loff + write_cmd_offset;
            break;
        }
        loff += actual->entry.size;
        entr++;
    }

    if (entr < write_cmd)
        retval = -EINVAL;

out: 
    if (retval == 0) {
        PDEBUG("Seek request to write_cmd: %d, cmd_offset: %d, returned %d\n", (int)write_cmd, (int)write_cmd_offset, (int)filp->f_pos);
    } else {
        PERROR("Error adjusting f_pos, write_cmd: %d, cmd_offset: %d \n", (int)write_cmd, (int)write_cmd_offset);
	}
    mutex_unlock(&dev->lock);
    return retval;
}

/*
 * The ioctl() implementation
 */

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int retval = 0;
     
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) >= AESDCHAR_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {

	    case AESDCHAR_IOCSEEKTO:
            PDEBUG("IOCSEEDTO req received\n");
            struct aesd_seekto seekto;
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(struct aesd_seekto)) != 0) {
                retval = -EFAULT;
            } else {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            }
		    break;
	  
	    default:  /* redundant, as cmd was checked against MAXNR */
		    return -ENOTTY;
	}

	return retval;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
	.unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        PERROR("Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    // request a mayor number for aesdchar driver
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        PWARNING("Can't get major %d\n", aesd_major);
        return result;
    }
    PDEBUG("Driver aesdchar major, minor: %d, %d\n", MAJOR(dev), MINOR(dev));

    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    aesd_device.tmp_buffer.buffer = kmalloc(AESDCHAR_TMP_BUFFER_SIZE, GFP_KERNEL);
    if (!aesd_device.tmp_buffer.buffer) {
        PWARNING("Unable to allocate driver temporal memory\n");
        result = -ENOMEM;
        goto err_unregister_chrdev;
    }
    PDEBUG("Allocated %d bytes for temporal buffer\n", AESDCHAR_TMP_BUFFER_SIZE);
    // Clear temporal buffer
    aesd_device.tmp_buffer.data_size = 0;
    memset(aesd_device.tmp_buffer.buffer, 0x00, AESDCHAR_TMP_BUFFER_SIZE);

    
    aesd_device.circular_buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.circular_buffer) {
        PWARNING("Unable to allocate memory for circular buffer handler\n");
        result = -ENOMEM;
        goto err_deallocate_buffer;
    }
    PDEBUG("Allocated %zu bytes for circular buffer handler\n", sizeof(struct aesd_circular_buffer));

    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if (result) {
        goto err_deallocate_buffer_list;
    }
    
    aesd_circular_buffer_init(aesd_device.circular_buffer);
    PDEBUG("Driver setup success\n");

    return result;

err_deallocate_buffer_list:
    kfree(aesd_device.circular_buffer);
    PDEBUG("Deallocated %zu bytes from circular buffer handler\n", sizeof(struct aesd_circular_buffer));
err_deallocate_buffer:
    kfree(aesd_device.tmp_buffer.buffer);
    PDEBUG("Deallocated %d bytes from temporal buffer\n", AESDCHAR_TMP_BUFFER_SIZE);
err_unregister_chrdev:
    unregister_chrdev_region(dev, 1);
    PDEBUG("Driver unregistered. Error %d:\n", result);

    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct list_buffer_entry_s *actual, *tmp;
    int entr= 0;

    cdev_del(&aesd_device.cdev);

    // deallocate buffers remaining in circular_buffer
    // Iterate over every element in the list
    PDEBUG("Deallocating circular buffer entries\n");
    list_for_each_entry_safe(actual, tmp, &aesd_device.circular_buffer->list_head, entries) {
        // Free memory for string allocated in buffptr
        PDEBUG("Deallocating %d bytes from list element entry buffer\n", (int)actual->entry.size);
        kfree((void *)actual->entry.buffptr);

        // Delete the list entry from the list
        list_del(&actual->entries);

        // Free list entry memory
        PDEBUG("Deallocating %d bytes from list element entry\n", (int)sizeof(struct list_buffer_entry_s));
        kfree(actual);

        entr++;
    }
    PDEBUG("%d entries deallocated\n", entr);

    // Free dynamic memory allocated for the circular_buffer handler
    kfree(aesd_device.circular_buffer);
    PDEBUG("Deallocated %d bytes from circular buffer handler\n", (int)sizeof(struct aesd_circular_buffer));

    // Free dynamic memory allocated for the temporal buffer
    kfree(aesd_device.tmp_buffer.buffer);
    PDEBUG("Deallocated %d bytes from temporal buffer\n", (int)AESDCHAR_TMP_BUFFER_SIZE);

    mutex_destroy(&aesd_device.lock);
    
    unregister_chrdev_region(devno, 1);

    PDEBUG("module deinit success\n");
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
