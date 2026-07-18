/* 커널 드라이버와 유저스페이스 앱이 공유하는 ABI 계약 —
 * read()로 오가는 바이트를 양쪽이 같은 레이아웃으로 해석하기 위한 헤더.
 * __s16은 <linux/types.h>가 커널/유저 양쪽에 제공하는 타입이라 typedef 불필요. */
#ifndef MPU6050_I2C_H
#define MPU6050_I2C_H

#include <linux/types.h>

struct mpu6050_data {
    __s16 accel_x, accel_y, accel_z;
    __s16 temp;
    __s16 gyro_x, gyro_y, gyro_z;
};

#endif
