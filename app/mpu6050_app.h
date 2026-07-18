#ifndef MPU6050_APP_H
#define MPU6050_APP_H

#include "../include/mpu6050_i2c.h"

int mpu6050_open(void);
int mpu6050_read_data(int fd, struct mpu6050_data *data);

#endif
