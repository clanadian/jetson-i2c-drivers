#ifndef MPU6050_I2C_H
#define MPU6050_I2C_H

struct mpu6050_data {
    s16 accel_x, accel_y, accel_z;
    s16 temp;
    s16 gyro_x, gyro_y, gyro_z;
};

#endif