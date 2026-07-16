#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include "mpu6050_i2c.h"

#define MPU6050_I2C_DEV_NAME   "mpu6050_i2c"
#define MPU6050_BLOCK_LEN  14

static struct i2c_client *g_client;
static dev_t devno;
static struct class *mpu6050_class;
static struct cdev mpu6050_cdev;

static void mpu6050_parse(const u8 *raw, struct mpu6050_data *data)
{
    data->accel_x = (raw[0] << 8) | raw[1];
    data->accel_y = (raw[2] << 8) | raw[3];
    data->accel_z = (raw[4] << 8) | raw[5];
    data->temp    = (raw[6] << 8) | raw[7];
    data->gyro_x  = (raw[8] << 8) | raw[9];
    data->gyro_y  = (raw[10] << 8) | raw[11];
    data->gyro_z  = (raw[12] << 8) | raw[13];
}

static ssize_t mpu6050_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
    int ret;
    u8 raw[MPU6050_BLOCK_LEN];
    struct mpu6050_data data = {0};

    if (!g_client)
        return -ENODEV;

    if (count < sizeof(data))
        return -EINVAL;

    ret = i2c_smbus_read_i2c_block_data(g_client, 0x3B, MPU6050_BLOCK_LEN, raw);
    if (ret != MPU6050_BLOCK_LEN)
        return ret;

    mpu6050_parse(raw, &data);

    if (copy_to_user(buf, &data, sizeof(data)))
        return -EFAULT;

    return sizeof(data);
}

static const struct file_operations mpu6050_fops = {
    .owner = THIS_MODULE,
    .read = mpu6050_read,
};

static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret;
    struct device *dev;
    s32 who;

    if (g_client)
        return -EBUSY;

    who = i2c_smbus_read_byte_data(client, 0x75);
    if (who < 0)
        return who;

    if (who != 0x68)
        return -ENODEV;

    ret = i2c_smbus_write_byte_data(client, 0x6B, 0x00);
    if (ret < 0)
        return ret;

    ret = alloc_chrdev_region(&devno, 0, 1, MPU6050_I2C_DEV_NAME);
    if (ret)
        return ret;

    cdev_init(&mpu6050_cdev, &mpu6050_fops);
    ret = cdev_add(&mpu6050_cdev, devno, 1);
    if (ret)
        goto err_chrdev;

    mpu6050_class = class_create(THIS_MODULE, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(mpu6050_class)) {
        ret = PTR_ERR(mpu6050_class);
        goto err_cdev;
    }

    dev = device_create(mpu6050_class, NULL, devno, NULL, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        goto err_class;
    }

    g_client = client;
    return 0;

    err_class:
        class_destroy(mpu6050_class);
    err_cdev:
        cdev_del(&mpu6050_cdev);
    err_chrdev:
        unregister_chrdev_region(devno, 1);
        return ret;
}

static int mpu6050_remove(struct i2c_client *client){
    device_destroy(mpu6050_class, devno);
    class_destroy(mpu6050_class);
    cdev_del(&mpu6050_cdev);
    unregister_chrdev_region(devno, 1);
    
    g_client = NULL;
    return 0;
}

static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "mpu6050-i2c" },
    { }
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

static struct i2c_driver mpu6050_driver = {
    .driver = {
        .name = "mpu6050_i2c",
        .of_match_table = mpu6050_of_match,
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
};

module_i2c_driver(mpu6050_driver);

MODULE_LICENSE("GPL");