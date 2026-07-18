#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "../include/ssd1306_i2c.h"

#define SSD1306_I2C_DEV_NAME    "ssd1306_i2c"
#define SSD1306_MAX_LEN 1024

static const u8 ssd1306_init_cmds[] = {
    0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81,
    0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
};

static struct i2c_client *g_client;
static dev_t devno;
static struct class *ssd1306_class;
static struct cdev ssd1306_cdev;

static loff_t ssd1306_lseek(struct file *filp, loff_t offset, int whence){
    return fixed_size_llseek(filp, offset, whence, 168); // 21*8
}

static int ssd1306_set_pos(int page, int col_pixel)
{
    u8 cmd[4];

    cmd[0] = 0x00;
    cmd[1] = 0xB0 | page;
    cmd[2] = 0x00 | (col_pixel & 0x0F);
    cmd[3] = 0x10 | (col_pixel >> 4);

    return i2c_master_send(g_client, cmd, sizeof(cmd));
}

static ssize_t ssd1306_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos){
    int page, col_char, col_pixel;
    u8 *kbuf;
    int ret;

    if (!g_client)
        return -ENODEV;

    if (*ppos >= 168)
        return 0;

    if (count > SSD1306_MAX_LEN)
        return -EINVAL;

    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    page      = *ppos / 21;
    col_char  = *ppos % 21;
    col_pixel = 1 + col_char * 6;

    ret = ssd1306_set_pos(page, col_pixel);
    if (ret < 0) {
        kfree(kbuf);
        return ret;
    }

    kbuf[0] = 0x40;
    if (copy_from_user(kbuf + 1, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    ret = i2c_master_send(g_client, kbuf, count + 1);
    kfree(kbuf);
    if (ret < 0)
        return ret;

    *ppos += count / 6;
    if (*ppos > 167)
        *ppos = 167;

    return count;
}

static int ssd1306_clear(void)
{
    u8 blank[129] = { 0x40 };  // blank[0]=control byte(data), 나머지 128바이트는 0으로 초기화됨
    int page, ret;

    for (page = 0; page < 8; page++) {
        ret = ssd1306_set_pos(page, 0);
        if (ret < 0)
            return ret;

        ret = i2c_master_send(g_client, blank, sizeof(blank));
        if (ret < 0)
            return ret;
    }

    return 0;
}

static long ssd1306_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    if (!g_client)
        return -ENODEV;

    switch (cmd) {
    case SSD1306_IOC_CLEAR:
        return ssd1306_clear();
    default:
        return -ENOTTY;
    }
}

static struct file_operations ssd1306_fops = {
    .owner          = THIS_MODULE,
    .write          = ssd1306_write,
    .llseek         = ssd1306_lseek,
    .unlocked_ioctl = ssd1306_ioctl,
};

static int ssd1306_boot(struct i2c_client *client){
    int ret;
    ret = i2c_master_send(client, ssd1306_init_cmds, sizeof(ssd1306_init_cmds));
    if (ret < 0) {
        dev_err(&client->dev, "OLED boot failed: %d\n", ret);
        return ret; 
    }

    if (ret != sizeof(ssd1306_init_cmds)) {
        dev_err(&client->dev, "OLED boot incomplete: sent %d of %ld\n", ret, sizeof(ssd1306_init_cmds));
        return -EIO;
    }

    return 0;
}

static int ssd1306_probe(struct i2c_client *client, const struct i2c_device_id *id){
    int ret;
    struct device *dev;

    if (g_client)
        return -EBUSY;

    ret = ssd1306_boot(client);
    if (ret < 0)
        return ret;
    
    ret = alloc_chrdev_region(&devno, 0, 1, SSD1306_I2C_DEV_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&ssd1306_cdev, &ssd1306_fops);
    ret = cdev_add(&ssd1306_cdev, devno, 1);
    if (ret)
        goto err_chrdev;

    ssd1306_class = class_create(THIS_MODULE, SSD1306_I2C_DEV_NAME);
    if (IS_ERR(ssd1306_class)) {
        ret = PTR_ERR(ssd1306_class);
        goto err_cdev;
    }

    dev = device_create(ssd1306_class, NULL, devno, NULL, SSD1306_I2C_DEV_NAME);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err_class;
    }

    g_client = client;
    return 0;

    err_class:
        class_destroy(ssd1306_class);
    err_cdev:
        cdev_del(&ssd1306_cdev);
    err_chrdev:
        unregister_chrdev_region(devno, 1);
        return ret;
}

static int ssd1306_remove(struct i2c_client *client){
    device_destroy(ssd1306_class, devno);
    class_destroy(ssd1306_class);
    cdev_del(&ssd1306_cdev);
    unregister_chrdev_region(devno, 1);

    g_client = NULL;
    return 0;
}

static const struct of_device_id ssd1306_of_match[] = {
    { .compatible = "ssd1306-i2c"},
    {}
};
MODULE_DEVICE_TABLE(of,ssd1306_of_match);

/* id_table 없으면 이 커널의 i2c_device_probe()가 probe() 호출 전에 -ENODEV로 반환함 */
static const struct i2c_device_id ssd1306_id[] = {
    { "ssd1306-i2c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ssd1306_id);

static struct i2c_driver ssd1306_driver = {
    .driver = {
        .name = "ssd1306_i2c",
        .of_match_table = ssd1306_of_match,
    },
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
    .id_table = ssd1306_id,
};

module_i2c_driver(ssd1306_driver);

MODULE_LICENSE("GPL");