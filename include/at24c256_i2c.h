/* 커널 드라이버와 유저스페이스 앱이 공유하는 ABI 계약 (EEPROM 저장 레이아웃) */
#ifndef AT24C256_I2C_H
#define AT24C256_I2C_H

#include <linux/types.h>

struct at24c256_calib_data {
    __s16 accel_offset_x, accel_offset_y, accel_offset_z;
    __s16 gyro_offset_x, gyro_offset_y, gyro_offset_z;
};

#endif
