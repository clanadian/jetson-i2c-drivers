#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#include "../include/mpu6050_i2c.h"

#define MPU6050_I2C_DEV_NAME   "mpu6050_i2c"
#define MPU6050_WHOAMI_RETRIES 3
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
    u8 reg = 0x3B;
    u8 raw[MPU6050_BLOCK_LEN];
    struct mpu6050_data data = {0};
    struct i2c_msg msgs[2];

    if (!g_client)
        return -ENODEV;

    if (count < sizeof(data))
        return -EINVAL;

    /* i2c_smbus_read_i2c_block_data()가 이 버스/기기 조합에서 유독 불안정해서
     * at24c256처럼 i2c_transfer()로 직접 메시지를 구성함 (레지스터 주소 write
     * + repeated start 후 read, 시간 정합성 위해 여전히 한 트랜잭션으로 처리). */
    msgs[0].addr = g_client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;

    msgs[1].addr = g_client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = MPU6050_BLOCK_LEN;
    msgs[1].buf = raw;

    ret = i2c_transfer(g_client->adapter, msgs, 2);
    if (ret != 2) {
        pr_err("mpu6050_i2c: block read failed/partial: %d\n", ret);
        return (ret < 0) ? ret : -EIO;
    }

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
    int retry;

    if (g_client)
        return -EBUSY;

    /* 브레드보드 배선은 순간적인 NAK/타임아웃(EREMOTEIO)이 흔하므로,
     * 진짜 미연결/고장과 구분하기 위해 몇 번 재시도 후 포기함. */
    for (retry = 0; ; retry++) {
        who = i2c_smbus_read_byte_data(client, 0x75);
        if (who >= 0)
            break;
        if (retry >= MPU6050_WHOAMI_RETRIES - 1) {
            pr_err("mpu6050_i2c: WHO_AM_I read failed: %d\n", who);
            return who;
        }
        msleep(10);
    }

    if (who != 0x70) {
        pr_err("mpu6050_i2c: unexpected WHO_AM_I: 0x%02x\n", who);
        return -ENODEV;
    }

    ret = i2c_smbus_write_byte_data(client, 0x6B, 0x00);
    if (ret < 0) {
        pr_err("mpu6050_i2c: PWR_MGMT_1 write failed: %d\n", ret);
        return ret;
    }

    /* CONFIG(0x1A) DLPF_CFG=3: 칩 내장 저역통과필터 켜기 (accel 44Hz/gyro 42Hz).
     * 파워온 기본값은 필터 꺼짐(260Hz)이라 raw 값 노이즈가 수백 LSB씩 출렁임. */
    ret = i2c_smbus_write_byte_data(client, 0x1A, 0x03);
    if (ret < 0) {
        pr_err("mpu6050_i2c: CONFIG(DLPF) write failed: %d\n", ret);
        return ret;
    }

    ret = alloc_chrdev_region(&devno, 0, 1, MPU6050_I2C_DEV_NAME);
    if (ret) {
        pr_err("mpu6050_i2c: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&mpu6050_cdev, &mpu6050_fops);
    ret = cdev_add(&mpu6050_cdev, devno, 1);
    if (ret) {
        pr_err("mpu6050_i2c: cdev_add failed: %d\n", ret);
        goto err_chrdev;
    }

    mpu6050_class = class_create(THIS_MODULE, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(mpu6050_class)) {
        ret = PTR_ERR(mpu6050_class);
        pr_err("mpu6050_i2c: class_create failed: %d\n", ret);
        goto err_cdev;
    }

    dev = device_create(mpu6050_class, NULL, devno, NULL, MPU6050_I2C_DEV_NAME);
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        pr_err("mpu6050_i2c: device_create failed: %d\n", ret);
        goto err_class;
    }

    pr_info("mpu6050_i2c: probe OK\n");

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

/* 이 커널의 i2c_device_probe()는 of_match_table로 매칭에 성공해도
 * id_table이 NULL이면 실제 probe()를 호출하지 않고 -ENODEV로 반환함.
 * 값 자체(0)는 안 쓰이고, NULL이 아니라는 사실만 필요함. */
static const struct i2c_device_id mpu6050_id[] = {
    { "mpu6050-i2c", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6050_id);

static struct i2c_driver mpu6050_driver = {
    .driver = {
        .name = "mpu6050_i2c",
        .of_match_table = mpu6050_of_match,
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
    .id_table = mpu6050_id,
};

module_i2c_driver(mpu6050_driver);

MODULE_LICENSE("GPL");