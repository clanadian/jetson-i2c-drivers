#ifndef SSD1306_APP_H
#define SSD1306_APP_H

#define SSD1306_COLS 21   /* 드라이버가 한 줄 21칸 x 8줄, 글자당 6바이트로 해석 */
#define SSD1306_ROWS 8

int ssd1306_open(void);
int ssd1306_clear(int fd);
int ssd1306_print(int fd, int row, const char *s);

#endif
