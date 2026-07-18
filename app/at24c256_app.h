#ifndef AT24C256_APP_H
#define AT24C256_APP_H

#include "../include/at24c256_i2c.h"

/* 한 번 호출로 open→read/write→close까지 처리 (EEPROM 주소 0 기준) */
int at24c256_read_calib(struct at24c256_calib_data *calib);
int at24c256_write_calib(const struct at24c256_calib_data *calib);

#endif
