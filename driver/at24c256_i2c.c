#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>

#include "at24c256_i2c.h"

#define AT24C256_I2C_DEV_NAME    "at24c256_i2c"
#define AT24C256_WRITE_TIMEOUT_MS 5
#define AT24C256_PAGE_SIZE 64

static struct i2c_client *g_client;
static dev_t devno;
static struct class *at24c256_class;
static struct cdev at24c256_cdev;

static int at24c256_wait_ready(void)
{
    unsigned long timeout = jiffies + msecs_to_jiffies(AT24C256_WRITE_TIMEOUT_MS);

    do {
        if (i2c_master_send(g_client, NULL, 0) >= 0)
            return 0;
    } while (time_before(jiffies, timeout));

    return -EIO;
}

static ssize_t at24c256_write(struct file *filp, const char __user *buf, size_t count, loff_t *off)
{
    u8 *kbuf;
    int ret;

    if (!g_client)
        return -ENODEV;

    if (count > AT24C256_PAGE_SIZE)
        return -EINVAL;

    if (count == 0)
        return 0;

    kbuf = kzalloc(count + 2, GFP_KERNEL);
    if (!kbuf) 
        return -ENOMEM;

    kbuf[0] = (u8)(*off >> 8);
    kbuf[1] = (u8)(*off & 0xFF);

    if (copy_from_user(kbuf + 2, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    ret = i2c_master_send(g_client, kbuf, count + 2);
    kfree(kbuf);

    if (ret < 0)
        return ret;

    ret = at24c256_wait_ready();
    if (ret < 0)
        return ret;

    *off += count;
    return count;
}

static ssize_t at24c256_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
    u8 addr[2];
    u8 *kbuf;
    int ret;

    if (!g_client)
        return -ENODEV;

    if (count > AT24C256_PAGE_SIZE)
        return -EINVAL;

    if (count == 0)
        return 0;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    addr[0] = (u8)(*off >> 8);
    addr[1] = (u8)(*off & 0xFF);

    struct i2c_msg msgs[2] = {
        { 
            .addr = g_client->addr, 
            .flags = 0,               // 쓰기 설정
            .len = 2, 
            .buf = addr 
        },
        { 
            .addr = g_client->addr, 
            .flags = I2C_M_RD,        // 읽기 설정
            .len = count, 
            .buf = kbuf 
        }
    };

    ret = i2c_transfer(g_client->adapter, msgs, 2);
    if (ret < 0) {
        kfree(kbuf);
        return ret;
    }

    if (copy_to_user(buf, kbuf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);

    *off += count;
    return count;
}

static struct file_operations at24c256_fops = {
    .owner = THIS_MODULE,
    .write = at24c256_write,
    .read  = at24c256_read
};

static int at24c256_probe(struct i2c_client *client, const struct i2c_device_id *id){
    int ret;
    struct device *dev;

    if (g_client)
        return -EBUSY;

    ret = alloc_chrdev_region(&devno, 0, 1, AT24C256_I2C_DEV_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&at24c256_cdev, &at24c256_fops);
    ret = cdev_add(&at24c256_cdev, devno, 1);
    if (ret)
        goto err_chrdev;

    at24c256_class = class_create(THIS_MODULE, AT24C256_I2C_DEV_NAME);
    if (IS_ERR(at24c256_class)) {
        ret = PTR_ERR(at24c256_class);
        goto err_cdev;
    }

    dev = device_create(at24c256_class, NULL, devno, NULL, AT24C256_I2C_DEV_NAME);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err_class;
    }

    g_client = client;
    return 0;

    err_class:
        class_destroy(at24c256_class);
    err_cdev:
        cdev_del(&at24c256_cdev);
    err_chrdev:
        unregister_chrdev_region(devno, 1);
        return ret;
}

static int at24c256_remove(struct i2c_client *client){
    device_destroy(at24c256_class, devno);
    class_destroy(at24c256_class);
    cdev_del(&at24c256_cdev);
    unregister_chrdev_region(devno, 1);

    g_client = NULL;
    return 0;
}

static const struct of_device_id at24c256_of_match[] = {
    { .compatible = "at24c256-i2c"},
    {}
};
MODULE_DEVICE_TABLE(of, at24c256_of_match);

static struct i2c_driver at24c256_driver = {
    .driver = {
        .name = "at24c256_i2c",
        .of_match_table = at24c256_of_match,
    },
    .probe = at24c256_probe,
    .remove = at24c256_remove
};

module_i2c_driver(at24c256_driver);

MODULE_LICENSE("GPL");
