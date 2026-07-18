/* 커널 드라이버와 유저스페이스 앱이 공유하는 ABI 계약 (ioctl 명령 코드) */
#ifndef SSD1306_I2C_H
#define SSD1306_I2C_H

#include <linux/ioctl.h>

#define SSD1306_IOC_MAGIC 'S'
#define SSD1306_IOC_CLEAR  _IO(SSD1306_IOC_MAGIC, 1)

#endif
