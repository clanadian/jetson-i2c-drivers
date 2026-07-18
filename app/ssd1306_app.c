#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ssd1306_app.h"
#include "ssd1306_fonts.h"
#include "../include/ssd1306_i2c.h"

#define SSD1306_DEV "/dev/ssd1306_i2c"
#define FONT_HDR    4   /* 테이블 앞 4바이트: 0x00, 폭(6), 높이(8), 시작문자(0x20) */
#define FONT_W      6

int ssd1306_open(void)
{
    int fd = open(SSD1306_DEV, O_WRONLY);
    if (fd < 0)
        perror("ssd1306 open fail");
    return fd;
}

int ssd1306_clear(int fd)
{
    if (ioctl(fd, SSD1306_IOC_CLEAR) < 0) {
        perror("ssd1306 clear fail");
        return -1;
    }
    return 0;
}

/* 문자열을 글자당 6바이트 비트맵으로 바꿔 row(0~7)번째 줄에 출력 */
int ssd1306_print(int fd, int row, const char *s)
{
    uint8_t buf[SSD1306_COLS * FONT_W];
    int len = 0;

    if (row < 0 || row >= SSD1306_ROWS)
        return -1;

    for (; *s && len < SSD1306_COLS; s++, len++) {
        int c = (*s < 0x20 || *s > 0x7E) ? ' ' : *s;
        memcpy(&buf[len * FONT_W],
               &ssd1306xled_font6x8[FONT_HDR + (c - 0x20) * FONT_W], FONT_W);
    }

    if (lseek(fd, row * SSD1306_COLS, SEEK_SET) < 0) {
        perror("ssd1306 lseek fail");
        return -1;
    }

    if (write(fd, buf, len * FONT_W) != len * FONT_W) {
        perror("ssd1306 write fail");
        return -1;
    }

    return 0;
}
