#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h> 
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cdev.h>

#include "mpu6050_i2c.h"

#define MPU6050_I2C_DEV_NAME    "mpu6050_i2c_dev"
#define MPU6050_I2C_DEV_MAJOR    230

#define MPU6050_I2C_BLOCK_LEN    14

static const struct of_device_id mpu6050_i2c_of_match[] = {
    { .compatible = "mpu6050-i2c"},
    {}
};
MODULE_DEVICE_TABLE(of, mpu6050_i2c_of_match);

static int mpu6050_i2c_init(void);
static int mpu6050_i2c_read_block_data(const struct i2c_client *client, u8 command, u8 length, u8 *values);
static void mpu6050_i2c_exit(void);
static int mpu6050_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mpu6050_i2c_remove(struct i2c_client *client);
static int mpu6050_i2c_open(struct inode *inode, struct file *filp);
static int mpu6050_i2c_release(struct inode *inode, struct file *filp);
static ssize_t mpu6050_i2c_read(struct file *filp, char __user *buf);

static struct i2c_driver mpu6050_i2c_driver = {
    .driver = {
        .name = "mpu6050_i2c",
        .of_match_table = mpu6050_i2c_of_match,
    },
    .probe  = mpu6050_i2c_probe,
    .remove = mpu6050_i2c_remove
};

static struct file_operations mpu6050_i2c_fops = {
    .open     = mpu6050_i2c_open,
    .release  = mpu6050_i2c_release,
    .read     = mpu6050_i2c_read
};

static struct i2c_client *g_client;
static dev_t devno;
static struct class *mpu6050_i2c_class;
static struct cdev mpu6050_cdev;


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

    cdev_init(&mpu6050_cdev, &mpu6050_i2c_fops);
    ret = cdev_add(&mpu6050_cdev, devno, 1);
    if (ret < 0) {
        unregister_chrdev_region(devno, 1);
        i2c_del_driver(&mpu6050_i2c_driver);
        return ret;
    }

    mpu6050_i2c_class = class_create(THIS_MODULE, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(mpu6050_i2c_class)){
        unregister_chrdev_region(devno, 1);
        i2c_del_driver(&mpu6050_i2c_driver);
        return PTR_ERR(mpu6050_i2c_class);
    }

    dev = device_create(mpu6050_i2c_class, NULL, devno,
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

    val = i2c_smbus_read_i2c_block_data(client, command, length, values);
    if (val != MPU6050_I2C_BLOCK_LEN){
        printk(KERN_ERR "mpu6050 read block fail: %d\n",val);
        return val;
    }

    return val;
}

static void mpu6050_i2c_exit(void){
    device_destroy(mpu6050_i2c_class, devno);
    class_destroy(mpu6050_i2c_class);
    cdev_del(&mpu6050_cdev);
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

    static int mpu6050_i2c_remove(struct i2c_client *client){
        g_client = NULL;
        printk(KERN_INFO "mpu6050 i2c disconnected\n");
        return 0;
    }

static int mpu6050_i2c_open(struct inode *inode, struct file *filp){
    try_module_get(THIS_MODULE);
    return 0;
}

static int mpu6050_i2c_release(struct inode *inode, struct file *filp){
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t mpu6050_i2c_read(struct file *filp, char __user *buf, size_t count, loff_t *off) {
    u8 raw[MPU6050_I2C_BLOCK_LEN];
    struct mpu6050_data data;

    mpu6050_i2c_read_block_data(g_client, 0x3B, raw, MPU6050_I2C_BLOCK_LEN);
    mpu6050_i2c_parsing(raw, &data)

    copy_to_user(buf, &data, sizeof(data));
    return sizeof(data);
}

static void mpu6050_i2c_parsing(const u8 *raw, struct mpu6050_data *data) {
    data->accel_x = (raw[0] << 8) | raw[1];
    data->accel_y = (raw[2] << 8) | raw[3];
    data->accel_z = (raw[4] << 8) | raw[5];
    data->temp    = (raw[6] << 8) | raw[7];
    data->gyro_x  = (raw[8] << 8) | raw[9];
    data->gyro_y  = (raw[10] << 8) | raw[11];
    data->gyro_z  = (raw[12] << 8) | raw[13];
}

module_init(mpu6050_i2c_init);
module_exit(mpu6050_i2c_exit);

MODULE_LICENSE("GPL");