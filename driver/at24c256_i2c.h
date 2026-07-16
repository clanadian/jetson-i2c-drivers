#ifndef AT24C256_I2C_H
#define AT24C256_I2C_H

struct at24c256_calib_data {
    s16 accel_offset_x, accel_offset_y, accel_offset_z;
    s16 gyro_offset_x, gyro_offset_y, gyro_offset_z;
};

#endif