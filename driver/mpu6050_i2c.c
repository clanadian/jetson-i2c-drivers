#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h> 
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include "mpu6050_i2c.h"

#define MPU6050_I2C_DEV_NAME    "mpu6050_i2c_dev"
#define MPU6050_I2C_DEV_MAJOR    230

#define MPU6050_I2C_BLOCK_LEN    14

static const struct of_device_id mpu6050_i2c_of_match[] = {
    { .compatible = "mpu6050-i2c"},
    {}
};
MODULE_DEVICE_TABLE(of, mpu6050_i2c_of_match);

static int mpu6050_i2c_probe(struct spi_device *spi);
static void mpu6050_i2c_remove(struct spi_device *spi);
static int mpu6050_i2c_open(struct inode *inode, struct file *filp);
static int mpu6050_i2c_release(struct inode *inode, struct file *filp);
static long mpu6050_i2c_read(struct file *filp, unsigned int cmd, unsigned long arg);

static struct i2c_driver mpu6050_i2c_driver = {
    .driver = {
        .name = "mpu6050_i2c",
        .of_match_table = mpu6050_i2c_of_match,
    },
    .probe  = mpu6050_i2c_probe,
    .remove = mpu6050_i2c_remove
};

static struct file_operations mpu6050_i2c_driver = {
    .open     = mpu6050_i2c_open,
    .release  = mpu6050_i2c_release,
    .read     = mpu6050_i2c_read
};

static struct i2c_client *g_client;
static dev_t devno;
static struct class *mpu6050_i2c_class;


static int mpu6050_i2c_init(void){
    int ret;
    struct device *dev;

    ret = i2c_add_driver(&mpu6050_i2c_driver);
    if (ret < 0) return ret;

    ret = alloc_chrdev_region(&devno, 0, 1, MPU6050_I2C_DEV_NAME);
    if (ret < 0){
        i2c_del_driver(&mpu6050_i2c_driver);
        return ret;
    }

    mpu6050_i2c_class = class_create(MPU6050_I2C_DEV_NAME);
    if (IS_ERR(mpu6050_i2c_class)){
        unregister_chrdev_region(devno, 1);
        i2c_del_driver(&mpu6050_i2c_driver);
        return PTR_ERR(mpu6050_i2c_class);
    }

    dev = device_create(mpu6050_i2c_class, NULL, MKDEV(MPU6050_I2C_DEV_MAJOR, 0),
                        NULL, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(dev)){
        class_destroy(mpu6050_i2c_class);
        unregister_chrdev_region(devno, 1);
        i2c_del_driver(&mpu6050_i2c_driver);
        return PTR_ERR(dev);
    }

    printk("mpu6050 init succeeded\n");
    return 0;
}

static int mpu6050_i2c_read_block_data(const struct i2c_client *client, u8 command, u8 length, u8 *values){
    s32 val;

    val = i2c_smbus_read_i2c_block_data(client, command, length, *values);
    if (val != MPU6050_I2C_BLOCK_LEN){
        printk(KERN_ERR "mpu6050 read block fail: %d\n",val);
        return val;
    }

    return val;
}

static void mpu6050_i2c_exit(void){
    device_destroy(mpu6050_i2c_class, MKDEV(MPU6050_I2C_DEV_MAJOR, 0));
    class_destroy(mpu6050_i2c_class);
    unregister_chrdev_region(devno, 1);
    i2c_del_driver(&mpu6050_i2c_driver);
}

static int mpu6050_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id){
    s32 who_am_i = i2c_smbus_read_byte_data(client, 0x75);
    if (who_am_i != 0x68){
        printk(KERN_ERR "mpu6050 not found: %d\n",who_am_i);
        return -ENODEV;
    }

    int ret = i2c_smbus_write_byte_data(client, 0x6B, 0x00);
    if (ret < 0) return ret;

    g_client = client;

    return 0;
}

static int mpu6050_i2c_remove(){
    g_client = NULL;
    printk(KERN_INFO "mpu6050 i2c disconnected\n");
    return 0;
}

static int mpu6050_i2c_open(struct inode *inode, sturct file *flip){
    try_module_get(THIS_MODULE);
    return 0;
}

static int mpu6050_i2c_release(struct inode *inode, struct file *flip){
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t mpu6050_i2c_read(struct file *flip, char __user *buf){
    u8 raw[MPU6050_I2C_BLOCK_LEN];
    struct mpu6050_data data;

    mpu6050_i2c_read_block(g_client, 0x3B, raw, MPU6050_I2C_BLOCK_LEN);
    //파싱

    copy_to_user(buf, &data, sizeof(data));
    return sizeof(data);
}

module_init(mpu6050_i2c_init);
module_exit(mpu6050_i2c_exit);

MODULE_LICENSE("GPL");